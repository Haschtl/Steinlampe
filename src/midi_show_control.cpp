#include "midi_show_control.h"

#if ENABLE_BLE_MIDI

#include "comms.h"

void handleCommand(const String &line);

namespace
{
  // Real MSC messages are a handful of bytes (header + a byte or two of
  // ASCII cue digits); this is a generous cap, not a measured worst case.
  // A sender that blows past it (not a real MSC message, or a bug) just has
  // the rest of its SysEx dropped and the whole message ignored once it
  // closes - never dispatched half-parsed.
  constexpr size_t MSC_BUF_MAX = 48;
  uint8_t mscBuf[MSC_BUF_MAX];
  size_t mscLen = 0;
  bool mscActive = false;
  bool mscOverflowed = false;

  // MMA MIDI Show Control command bytes this firmware acts on. SET/FIRE/
  // TIMED_GO/etc. are valid per the spec but deliberately not implemented -
  // nothing here needs them yet.
  constexpr uint8_t MSC_CMD_GO = 0x01;
  constexpr uint8_t MSC_CMD_STOP = 0x02;
  constexpr uint8_t MSC_CMD_RESUME = 0x03;

  void dispatchCommand(const String &cmd)
  {
    handleCommand(cmd);
  }

  // Parses the leading ASCII digits of the (optional) cue_number field
  // starting at mscBuf[offset], up to the next NUL/F7 terminator or the
  // first non-digit (cue_list/cue_path use the same NUL-separated layout,
  // but v1 only cares about the leading cue number). Returns -1 if the
  // field is absent (offset already at a terminator) or non-numeric.
  long parseCueNumber(size_t offset)
  {
    if (offset >= mscLen)
      return -1;
    long value = 0;
    bool any = false;
    for (size_t i = offset; i < mscLen; ++i)
    {
      uint8_t ch = mscBuf[i];
      if (ch == 0x00 || ch == 0xF7)
        break;
      if (ch < '0' || ch > '9')
        break;
      value = value * 10 + (ch - '0');
      any = true;
    }
    return any ? value : -1;
  }

  void dispatchMsc()
  {
    // F0 7F <device_ID> 02 <command_format> <command> [cue_number] ... F7
    // (the leading F0 and trailing F7 themselves aren't in mscBuf - see
    // feedMscByte below).
    if (mscLen < 4 || mscBuf[0] != 0x7F /* Universal Real Time SysEx */ || mscBuf[2] != 0x02 /* MIDI Show Control */)
      return;
    uint8_t commandFormat = mscBuf[3];
    // Lighting (General) or All-Types only, for v1 - device_ID (mscBuf[1])
    // isn't filtered on, any device ID is accepted.
    if (commandFormat != 0x01 && commandFormat != 0x7F)
      return;
    if (mscLen < 5)
      return;
    uint8_t command = mscBuf[4];
    long cue = parseCueNumber(5);

    String msg = F("[MSC] cmd=");
    msg += String(command);
    if (cue >= 0)
    {
      msg += F(" cue=");
      msg += String(cue);
    }
    sendFeedback(msg);

    switch (command)
    {
    case MSC_CMD_GO:
      // A cue number addresses the same "mode <N>" space command.cpp
      // already exposes (built-in patterns, then saved profile slots); no
      // cue number just advances, like a bare "GO" moving to the next cue.
      if (cue >= 0)
        dispatchCommand(String(F("mode ")) + String(cue));
      else
        dispatchCommand(F("next"));
      break;
    case MSC_CMD_STOP:
      // v1 simplification: this lamp has no "paused mid-cue" state, so STOP
      // maps to off rather than a true freeze.
      dispatchCommand(F("off"));
      break;
    case MSC_CMD_RESUME:
      dispatchCommand(F("on"));
      break;
    default:
      break;
    }
  }
} // namespace

void feedMscByte(uint8_t b)
{
  if (b == 0xF0) // SysEx start
  {
    mscActive = true;
    mscOverflowed = false;
    mscLen = 0;
    return;
  }
  if (!mscActive)
    return;
  if (b == 0xF7) // SysEx end
  {
    if (!mscOverflowed)
      dispatchMsc();
    mscActive = false;
    mscLen = 0;
    return;
  }
  if (mscLen < MSC_BUF_MAX)
    mscBuf[mscLen++] = b;
  else
    mscOverflowed = true;
}

#endif // ENABLE_BLE_MIDI
