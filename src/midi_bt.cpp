#include "lamp_config.h"

#if ENABLE_BT_SERIAL && ENABLE_BT_MIDI

#include "comms.h"
#include "midi_bt.h"

namespace
{
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
    }
    else
    {
      bool on = (type == 0x90) && data2 > 0;
      emitNote(on, chan, data1, data2);
    }
    waitingData1 = true;
  }
  else
  {
    resetStatus();
  }
}

#endif // ENABLE_BT_SERIAL && ENABLE_BT_MIDI
