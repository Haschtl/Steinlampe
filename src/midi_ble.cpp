#include "lamp_config.h"

#if ENABLE_BLE && ENABLE_BLE_MIDI

#include "comms.h"

#include <BLEAdvertising.h>
#include <BLEServer.h>
#include <BLEUtils.h>

namespace
{
  constexpr const char *MIDI_SERVICE_UUID = "03B80E5A-EDE8-4B33-A751-6CE34EC4C700";
  constexpr const char *MIDI_CHAR_UUID = "7772E5DB-3868-4112-A1A9-F2669D106BF3";

  BLECharacteristic *midiChar = nullptr;

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
