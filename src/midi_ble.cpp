#include "lamp_config.h"

#if ENABLE_BLE && ENABLE_BLE_MIDI

#include "comms.h"

#include <BLEAdvertising.h>
#include <BLEServer.h>
#include <BLEUtils.h>

void handleCommand(String line);

namespace
{
  constexpr const char *MIDI_SERVICE_UUID = "03B80E5A-EDE8-4B33-A751-6CE34EC4C700";
  constexpr const char *MIDI_CHAR_UUID = "7772E5DB-3868-4112-A1A9-F2669D106BF3";
  // Simple MIDI→command mapping (fixed)
  constexpr uint8_t NOTE_TOGGLE = 59;     // B3
  constexpr uint8_t NOTE_PREV = 60;       // C4
  constexpr uint8_t NOTE_NEXT = 62;       // D4
  constexpr uint8_t NOTE_QUICK_BASE = 70; // F#4 -> quick slot 1
  constexpr uint8_t CC_BRIGHTNESS = 7;    // Channel volume maps to brightness
  constexpr uint8_t CC_MODE = 20;         // Maps to mode 1..8

  BLECharacteristic *midiChar = nullptr;

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
      // Map 0..127 -> mode 1..8
      uint8_t idx = 1 + (value * 7) / 127;
      dispatchCommand(String(F("mode ")) + String(idx));
    }
  }

  void handleMappedNote(uint8_t note, uint8_t vel)
  {
    if (vel == 0)
      return;
    if (note == NOTE_TOGGLE)
    {
      dispatchCommand(F("toggle"));
    }
    else if (note == NOTE_PREV)
    {
      dispatchCommand(F("prev"));
    }
    else if (note == NOTE_NEXT)
    {
      dispatchCommand(F("next"));
    }
    else if (note >= NOTE_QUICK_BASE && note < NOTE_QUICK_BASE + 8)
    {
      uint8_t idx = (note - NOTE_QUICK_BASE) + 1;
      dispatchCommand(String(F("mode ")) + String(idx));
    }
  }

  void handleMidiMessage(const uint8_t *data, size_t len)
  {
    uint8_t status = 0;
    for (size_t i = 0; i < len; ++i)
    {
      uint8_t b = data[i];
      // BLE MIDI wraps messages with timestamp bytes; skip a timestamp if two MSB bytes follow
      if ((b & 0x80) && (i + 1 < len) && (data[i + 1] & 0x80))
        continue;

      if (b & 0x80) // status
      {
        status = b;
        continue;
      }
      if (status == 0)
        continue;

      uint8_t type = status & 0xF0;
      uint8_t chan = status & 0x0F;
      if (type == 0x80 || type == 0x90) // Note Off / Note On
      {
        uint8_t note = b;
        uint8_t vel = (i + 1 < len) ? data[i + 1] : 0;
        String msg = F("[MIDI] ");
        msg += (type == 0x90 && vel > 0) ? F("NoteOn ") : F("NoteOff ");
        msg += F("ch=");
        msg += String(chan + 1);
        msg += F(" note=");
        msg += String(note);
        msg += F(" vel=");
        msg += String(vel);
        sendFeedback(msg);
        if (type == 0x90 && vel > 0)
          handleMappedNote(note, vel);
        // consume velocity byte
        ++i;
        status = 0;
      }
      else if (type == 0xB0 && (b == 0x07 || b == 0x0A)) // CC vol/pan
      {
        uint8_t value = (i + 1 < len) ? data[i + 1] : 0;
        String msg = F("[MIDI] CC ");
        msg += String(b);
        msg += F("=");
        msg += String(value);
        sendFeedback(msg);
        handleMappedCC(b, value);
        ++i;
        status = 0;
      }
      else
      {
        // ignore other statuses, reset to wait for next status
        status = 0;
      }
    }
  }

  class MidiCallbacks : public BLECharacteristicCallbacks
  {
    void onWrite(BLECharacteristic *c) override
    {
      std::string val = c->getValue();
      if (val.empty())
        return;
      handleMidiMessage(reinterpret_cast<const uint8_t *>(val.data()), val.size());
    }
  };
} // namespace

void setupBleMidi(BLEServer *server, BLEAdvertising *advertising)
{
  if (!server)
    return;
  BLEService *svc = server->createService(MIDI_SERVICE_UUID);
  midiChar = svc->createCharacteristic(MIDI_CHAR_UUID,
                                       BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR);
  midiChar->setCallbacks(new MidiCallbacks());
  svc->start();
  if (advertising)
    advertising->addServiceUUID(MIDI_SERVICE_UUID);
  Serial.println(F("[BLE-MIDI] Receive-only service aktiv."));
}

#endif // ENABLE_BLE && ENABLE_BLE_MIDI
