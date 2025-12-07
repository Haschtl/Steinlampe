#include "lamp_config.h"

#if ENABLE_BT_SERIAL && ENABLE_BT_MIDI

#include "comms.h"
#include "midi_bt.h"

void handleCommand(String line);

namespace
{
  // Simple MIDI→command mapping (fixed)
  constexpr uint8_t NOTE_TOGGLE = 59;     // B3
  constexpr uint8_t NOTE_PREV = 60;       // C4
  constexpr uint8_t NOTE_NEXT = 62;       // D4
  constexpr uint8_t NOTE_QUICK_BASE = 70; // F#4 -> quick slot 1
  constexpr uint8_t CC_BRIGHTNESS = 7;    // Channel volume maps to brightness
  constexpr uint8_t CC_MODE = 20;         // Maps to mode 1..8

  uint8_t runningStatus = 0;
  uint8_t data1 = 0;
  bool waitingData1 = true;

  void resetStatus()
  {
    runningStatus = 0;
    waitingData1 = true;
    data1 = 0;
  }

  void emitNote(bool on, uint8_t chan, uint8_t note, uint8_t vel)
  {
    String msg = F("[MIDI-BT] ");
    msg += on ? F("NoteOn ") : F("NoteOff ");
    msg += F("ch=");
    msg += String(chan + 1);
    msg += F(" note=");
    msg += String(note);
    msg += F(" vel=");
    msg += String(vel);
    sendFeedback(msg);
  }

  void emitCC(uint8_t chan, uint8_t cc, uint8_t val)
  {
    String msg = F("[MIDI-BT] CC ");
    msg += String(cc);
    msg += F("=");
    msg += String(val);
    msg += F(" ch=");
    msg += String(chan + 1);
    sendFeedback(msg);
  }

  void dispatchCommand(const String &cmd)
  {
    handleCommand(cmd);
  }

  void handleMappedCC(uint8_t cc, uint8_t value)
  {
    if (cc == CC_BRIGHTNESS)
    {
      uint8_t pct = (uint8_t)((value * 100) / 127);
      dispatchCommand(String(F("bri ")) + String(pct));
    }
    else if (cc == CC_MODE)
    {
      uint8_t idx = 1 + (value * 7) / 127;
      dispatchCommand(String(F("mode ")) + String(idx));
    }
  }

  void handleMappedNote(uint8_t note, uint8_t vel)
  {
    if (vel == 0)
      return;
    if (note == NOTE_TOGGLE)
      dispatchCommand(F("toggle"));
    else if (note == NOTE_PREV)
      dispatchCommand(F("prev"));
    else if (note == NOTE_NEXT)
      dispatchCommand(F("next"));
    else if (note >= NOTE_QUICK_BASE && note < NOTE_QUICK_BASE + 8)
    {
      uint8_t idx = (note - NOTE_QUICK_BASE) + 1;
      dispatchCommand(String(F("mode ")) + String(idx));
    }
  }
}

void processBtMidiByte(uint8_t b)
{
  if (b & 0x80)
  {
    runningStatus = b;
    waitingData1 = true;
    data1 = 0;
    return;
  }
  if (runningStatus == 0)
    return;

  uint8_t type = runningStatus & 0xF0;
  uint8_t chan = runningStatus & 0x0F;

  if (type == 0x80 || type == 0x90 || type == 0xB0)
  {
    if (waitingData1)
    {
      data1 = b;
      waitingData1 = false;
      return;
    }
    // data2
    uint8_t data2 = b;
    if (type == 0xB0)
    {
      emitCC(chan, data1, data2);
      handleMappedCC(data1, data2);
    }
    else
    {
      bool on = (type == 0x90) && data2 > 0;
      emitNote(on, chan, data1, data2);
      if (on)
        handleMappedNote(data1, data2);
    }
    waitingData1 = true;
  }
  else
  {
    resetStatus();
  }
}

#endif // ENABLE_BT_SERIAL && ENABLE_BT_MIDI
