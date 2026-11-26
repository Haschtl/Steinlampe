#include "lamp_config.h"

#if ENABLE_BLE && ENABLE_BLE_MIDI

#include "comms.h"
#include "midi_ble.h"
#include "midi_show_control.h"

#include "host/ble_gatt.h"
#include "host/ble_hs.h"

void handleCommand(const String &line);

// Shared with comms.cpp's startBleAdvertising() (scan-response UUID list)
// and uuid128FromString() (the same runtime UUID-string parser comms.cpp
// defines statically - duplicated here rather than shared via a header
// since it's a two-line helper and the alternative is a new internal
// header just for it).
ble_uuid128_t midiServiceUuid;

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

  ble_uuid128_t midiCharUuid;
  uint16_t midiCharHandle = 0;

  ble_uuid128_t uuid128FromString(const char *str)
  {
    uint8_t display[16];
    int bi = 0;
    for (int i = 0; str[i] != '\0' && bi < 16;)
    {
      if (str[i] == '-')
      {
        i++;
        continue;
      }
      char hex[3] = {str[i], str[i + 1], '\0'};
      display[bi++] = (uint8_t)strtol(hex, nullptr, 16);
      i += 2;
    }
    ble_uuid128_t u{};
    u.u.type = BLE_UUID_TYPE_128;
    for (int i = 0; i < 16; i++)
      u.value[i] = display[15 - i];
    return u;
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

      // MIDI Show Control rides as SysEx (0xF0..0xF7), which the rest of
      // this function doesn't parse - feed every non-timestamp byte to the
      // reassembler unconditionally, it self-gates on being inside a SysEx
      // span and ignores everything else.
      feedMscByte(b);

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

  int midiAccessCb(uint16_t conn_handle, uint16_t attr_handle,
                   struct ble_gatt_access_ctxt *ctxt, void *arg)
  {
    (void)conn_handle;
    (void)attr_handle;
    (void)arg;
    if (ctxt->op != BLE_GATT_ACCESS_OP_WRITE_CHR)
      return 0;
    uint16_t len = OS_MBUF_PKTLEN(ctxt->om);
    if (len == 0)
      return 0;
    // BLE-MIDI packets are small (a handful of 3-byte MIDI messages plus
    // timestamp bytes); 64 is generous headroom, not a negotiated-MTU-sized
    // buffer like comms.cpp's command characteristic needs.
    uint8_t buf[64];
    uint16_t copyLen = len < sizeof(buf) ? len : (uint16_t)sizeof(buf);
    if (os_mbuf_copydata(ctxt->om, 0, copyLen, buf) != 0)
      return 0;
    handleMidiMessage(buf, copyLen);
    return 0;
  }

  struct ble_gatt_chr_def midiChars[] = {
      {
          .uuid = &midiCharUuid.u,
          .access_cb = midiAccessCb,
          .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP,
          .val_handle = &midiCharHandle,
      },
      {0},
  };

  struct ble_gatt_svc_def midiSvcs[] = {
      {
          .type = BLE_GATT_SVC_TYPE_PRIMARY,
          .uuid = &midiServiceUuid.u,
          .characteristics = midiChars,
      },
      {0},
  };
} // namespace

void setupBleMidi()
{
  midiServiceUuid = uuid128FromString(MIDI_SERVICE_UUID);
  midiCharUuid = uuid128FromString(MIDI_CHAR_UUID);

  int rc = ble_gatts_count_cfg(midiSvcs);
  ESP_ERROR_CHECK(rc == 0 ? ESP_OK : ESP_FAIL);
  rc = ble_gatts_add_svcs(midiSvcs);
  ESP_ERROR_CHECK(rc == 0 ? ESP_OK : ESP_FAIL);

  Serial.println(F("[BLE-MIDI] Receive-only service aktiv."));
}

#endif // ENABLE_BLE && ENABLE_BLE_MIDI
