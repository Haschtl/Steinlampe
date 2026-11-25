#include "comms.h"

#include "lamp_config.h"
#include "settings.h"
#include <vector>
#include <deque>
#include <algorithm>
#if ENABLE_BT_SERIAL && ENABLE_BT_MIDI
#include "midi_bt.h"
#endif
#if ENABLE_BLE && ENABLE_BLE_MIDI
#include "midi_ble.h"
#endif

#if ENABLE_BT_SERIAL
#include <BluetoothSerial.h>
#include <esp_spp_api.h>
#endif

#if ENABLE_BLE
// Native ESP-IDF NimBLE host API, not the NimBLE-Arduino C++ wrapper -
// switched because esp_ble_mesh (see spikes/ble-mesh/) needs to be the sole
// owner of ble_svc_gap_init()/ble_svc_gatt_init() on the shared host;
// NimBLE-Arduino calls those itself too, with no coordination between the
// two, which crashed in a boot loop on real hardware when both were present
// in one image (documented in spikes/ble-mesh/README.md's "Decision gate").
// This is the same native API that spike's Mesh node already uses.
#include "esp32-hal-bt.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/ble_gap.h"
#include "host/ble_gatt.h"
#include "host/ble_store.h"
#include "host/ble_hs_id.h"
#include "host/util/util.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include "store/config/ble_store_config.h"
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

// Not actually declared by ble_store_config.h (only read/write/delete are) -
// Espressif's own bleprph example forward-declares it the same way.
extern "C" void ble_store_config_init(void);
#endif

/**
 * @file comms.cpp
 * @brief Implements USB, Bluetooth Serial, and BLE command handling.
 */

// Provided by main.cpp to route parsed command strings.
void handleCommand(const String &line);
void saveSettings();

// Trusted device storage (shared across helpers)
static std::vector<String> trustedBle;
static std::vector<String> trustedBt;
static uint32_t trustBootMs = 0;
static const uint32_t TRUST_GRACE_MS = Settings::BT_TRUST_GRACE_MS;
static const size_t TRUST_MAX = 12;

static bool feedbackArmed = !Settings::FEEDBACK_NEEDS_HANDSHAKE;
static String bleName = Settings::BLE_NAME_DEFAULT;
static String btName = Settings::BT_NAME_DEFAULT;
static uint32_t bootMsComm = 0;
static uint32_t lastBtActivityMs = 0;
#if ENABLE_BT_PAIRING
static bool btPairPending = false;
static uint32_t btPairStartMs = 0;
static String btPendingAddr;
static const uint32_t BT_PAIR_TIMEOUT_MS = 20000;
#if ENABLE_BLE
// BLE's equivalent of the above: a physical-gesture fallback for a genuinely
// new (now bonded, but not yet trusted) device outside the trust grace
// window, so first-time pairing doesn't require rebooting the lamp to reopen
// that window. Reached from the GAP event callback's ENC_CHANGE handling
// once a connection has bonded but its identity address isn't on the trust
// list - see comms.cpp. Same 20s budget as BT-Classic's gesture window, same
// confirmation mechanism (confirmPendingPairing, called from the switch/poti
// handlers), same "commands are gated until confirmed" posture - deliberately
// no light/blink feedback, only text.
static bool blePairPending = false;
static uint32_t blePairStartMs = 0;
static String blePendingAddr;
static uint16_t blePendingConnId = 0;
static const uint32_t BLE_PAIR_TIMEOUT_MS = 20000;
#endif
#endif
// Line buffers
static String bufferUsb;
#if ENABLE_BT_SERIAL
static uint32_t btSleepAfterBootMs = Settings::BT_SLEEP_AFTER_BOOT_MS;
static uint32_t btSleepAfterBleMs = Settings::BT_SLEEP_AFTER_BLE_MS;
static String bufferBt;
static String lastSppAddr;
BluetoothSerial serialBt;
static bool btSerialActive = false;
static bool btSerialDisabled = false;
#endif

inline bool feedbackAllowed()
{
  return !Settings::FEEDBACK_NEEDS_HANDSHAKE || feedbackArmed;
}
inline void armFeedback() { feedbackArmed = true; }

String normalizeAddr(const String &in)
{
  String a = in;
  a.trim();
  a.toUpperCase();
  a.replace("-", ":");
  // ensure colon separated
  return a;
}

bool addrEqual(const String &a, const String &b) { return normalizeAddr(a) == normalizeAddr(b); }

bool listContains(const std::vector<String> &list, const String &addr)
{
  const String norm = normalizeAddr(addr);
  return std::any_of(list.begin(), list.end(), [&](const String &v)
                     { return v.equalsIgnoreCase(norm); });
}

bool addToList(std::vector<String> &list, const String &addr)
{
  String norm = normalizeAddr(addr);
  if (norm.isEmpty())
    return false;
  if (listContains(list, norm))
    return false;
  if (list.size() >= TRUST_MAX)
    list.erase(list.begin()); // drop oldest
  list.push_back(norm);
  return true;
}

bool removeFromList(std::vector<String> &list, const String &addr)
{
  String norm = normalizeAddr(addr);
  auto it = std::remove_if(list.begin(), list.end(), [&](const String &v)
                           { return v.equalsIgnoreCase(norm); });
  if (it != list.end())
  {
    list.erase(it, list.end());
    return true;
  }
  return false;
}

String joinList(const std::vector<String> &list)
{
  String out;
  for (size_t i = 0; i < list.size(); ++i)
  {
    out += list[i];
    if (i + 1 < list.size())
      out += ',';
  }
  return out;
}

void parseList(const String &csv, std::vector<String> &out)
{
  out.clear();
  int start = 0;
  while (start < csv.length())
  {
    int comma = csv.indexOf(',', start);
    if (comma < 0)
      comma = csv.length();
    String part = csv.substring(start, comma);
    part.trim();
    if (!part.isEmpty())
    {
      addToList(out, part);
    }
    start = comma + 1;
  }
}

bool withinGrace()
{
  if (TRUST_GRACE_MS==0)
    return true;
  return (millis() - trustBootMs) < TRUST_GRACE_MS;
}

bool allowBleAddr(const String &addr)
{
  if (addr.isEmpty())
    return withinGrace(); // no MAC? allow only during grace
  if (withinGrace())
    return true;
  return listContains(trustedBle, addr);
}

bool allowBtAddr(const String &addr)
{
  if (addr.isEmpty())
    return withinGrace(); // no MAC? allow only during grace
  if (withinGrace())
    return true;
  return listContains(trustedBt, addr);
}

// Shared by USB and BT-Serial. Sized to comfortably fit an "ota chunk <base64>" command
// (Serial/USB OTA chunks are up to 4096 raw bytes -> ~5464 base64 chars, see
// frontend/src/lib/ota.ts) on top of normal short commands.
static const size_t LINE_BUFFER_MAX = 5600;

/**
 * @brief Append a character to the line buffer and dispatch full commands.
 */
void processInputChar(String &buffer, char c)
{
  if (c == '\r')
    return;
  if (c == '\n')
  {
    buffer.trim();
    if (!buffer.isEmpty())
    {
      armFeedback();
      handleCommand(buffer);
    }
    buffer = "";
  }
  else if (buffer.length() < LINE_BUFFER_MAX)
  {
    buffer += c;
  }
}

#if ENABLE_BLE
static void startBleAdvertising(); // fwd decl, used by setBleName()
static int bleGapEvent(struct ble_gap_event *event, void *arg); // fwd decl, used by startBleAdvertising()
#endif

void setBleName(const String &name)
{
#if ENABLE_BLE
  if (name.length() >= 2)
  {
    bleName = name;
    // Live rename of advertisement can be flaky; store for next restart.
    if (ble_hs_synced())
    {
      ble_gap_adv_stop();
      startBleAdvertising();
    }
  }
#else
  (void)name;
#endif
}

void setBtName(const String &name)
{
#if ENABLE_BT_SERIAL
  if (name.length() < 2)
    return;
  btName = name;
  if (serialBt.hasClient())
    serialBt.disconnect();
  serialBt.end();
  serialBt.begin(btName);
#else
  (void)name;
#endif
}

String getBleName() { return bleName; }
String getBtName() { return btName; }

#if ENABLE_BT_SERIAL

static void powerOffBtSerial(const char *reason)
{
  if (btSerialDisabled)
    return;
#if ENABLE_BT_SERIAL
  if (serialBt.hasClient())
    serialBt.disconnect();
  serialBt.end();
  btSerialActive = false;
#endif
  btSerialDisabled = true;
  if (feedbackAllowed())
  {
    Serial.print(F("[BT] Serial disabled"));
    if (reason && reason[0])
    {
      Serial.print(F(" ("));
      Serial.print(reason);
      Serial.print(F(")"));
    }
    Serial.println();
  }
}

String formatMac(const uint8_t bda[6])
{
  char buf[18];
  snprintf(buf, sizeof(buf), "%02X:%02X:%02X:%02X:%02X:%02X", bda[0], bda[1], bda[2], bda[3], bda[4], bda[5]);
  return String(buf);
}

void sppCallbackLocal(esp_spp_cb_event_t event, esp_spp_cb_param_t *param)
{
  switch (event)
  {
  case ESP_SPP_SRV_OPEN_EVT:
    if (param)
    {
      lastSppAddr = formatMac(param->srv_open.rem_bda);
      bool known = listContains(trustedBt, lastSppAddr);
      if (!known)
      {
        if (!allowBtAddr(lastSppAddr))
        {
          if (feedbackAllowed())
            Serial.println(F("[BT] Rejected unknown device"));
          esp_spp_disconnect(param->srv_open.handle);
          return;
        }
#if ENABLE_BT_PAIRING
        btPairPending = true;
        btPairStartMs = millis();
        btPendingAddr = lastSppAddr;
        if (feedbackAllowed())
          sendFeedback(String(F("[BT] Pair request ")) + lastSppAddr + F(" – confirm via switch or poti"));
#else
        known = true;
#endif
      }
      else
      {
#if ENABLE_BT_PAIRING
        btPairPending = false;
        btPendingAddr = "";
#endif
      }
      // Always allow the connection attempt; commands are gated until confirmed.
      lastBtActivityMs = millis();
      if (addToList(trustedBt, lastSppAddr) && feedbackAllowed())
        saveSettings();
      if (known && feedbackAllowed())
      {
        Serial.print(F("[BT] Client connected: "));
        Serial.println(lastSppAddr);
        sendFeedback(String(F("[BT] Client connected ")) + lastSppAddr);
      }
    }
    else
    {
      lastBtActivityMs = millis();
      if (feedbackAllowed())
        Serial.println(F("[BT] Client connected"));
    }
    break;
  case ESP_SPP_CLOSE_EVT:
    if (param)
    {
      String addr = lastSppAddr;
      if (feedbackAllowed())
      {
        Serial.print(F("[BT] Client disconnected: "));
        Serial.println(addr);
        sendFeedback(String(F("[BT] Client disconnected ")) + addr);
      }
    }
    else
    {
      if (feedbackAllowed())
        Serial.println(F("[BT] Client disconnected"));
    }
#if ENABLE_BT_PAIRING
    btPairPending = false;
    btPendingAddr = "";
#endif
    break;
  default:
    break;
  }
}
#endif

#if ENABLE_BLE
uint16_t bleCommandCharHandle = 0;
uint16_t bleStatusCharHandle = 0;
bool bleClientConnected = false;
// Most recent BLE client address
String bleLastAddr;
static std::deque<String> bleNotifyQueue;
static String bleNotifyCurrent;
static size_t bleNotifyOffset = 0;
static uint32_t bleLastNotifyMs = 0;
static SemaphoreHandle_t bleNotifyMutex = nullptr;
static constexpr size_t BLE_NOTIFY_QUEUE_MAX = 64;
static constexpr uint32_t BLE_NOTIFY_INTERVAL_MS = 8;
// Matches CONFIG_BT_NIMBLE_MAX_CONNECTIONS (not overridden in this project's
// sdkconfig, default 3). Keeps us from re-arming advertising once the
// controller can't accept another BLE link anyway.
static constexpr uint8_t BLE_MAX_CONCURRENT_CLIENTS = 3;
// Tracks connected conn_handles - the native API has no single "list all
// connected peers" helper the way NimBLEServer::getPeerDevices() did.
static std::vector<uint16_t> bleConnHandles;

// Parses one of our settings.h UUID strings ("d94d86d7-...") into a native
// ble_uuid128_t at runtime, rather than hand-transcribing reversed byte
// arrays for BLE_UUID128_INIT (error-prone, and would duplicate the UUID
// strings that already live in settings.h as the single source of truth).
static ble_uuid128_t uuid128FromString(const char *str)
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
  // BLE_UUID128_INIT-style storage is byte-reversed (little-endian) relative
  // to the human-readable/display order parsed above.
  for (int i = 0; i < 16; i++)
    u.value[i] = display[15 - i];
  return u;
}

// NimBLEAddress::toString()'s equivalent: ble_addr_t.val[] is stored
// little-endian on the wire; standard MAC notation displays it reversed.
String bleAddrToString(const ble_addr_t &a)
{
  char buf[18];
  snprintf(buf, sizeof(buf), "%02x:%02x:%02x:%02x:%02x:%02x",
           a.val[5], a.val[4], a.val[3], a.val[2], a.val[1], a.val[0]);
  return String(buf);
}

static void queueBleNotification(const String &line)
{
  if (!bleNotifyMutex)
    return;

  String payload = line;
  payload += '\n';
  if (xSemaphoreTake(bleNotifyMutex, pdMS_TO_TICKS(20)) != pdTRUE)
    return;

  if (line.startsWith(F("STATE|")))
  {
    for (auto it = bleNotifyQueue.begin(); it != bleNotifyQueue.end();)
    {
      if (it->startsWith(F("STATE|")))
        it = bleNotifyQueue.erase(it);
      else
        ++it;
    }
  }
  while (bleNotifyQueue.size() >= BLE_NOTIFY_QUEUE_MAX)
    bleNotifyQueue.pop_front();
  bleNotifyQueue.push_back(payload);
  xSemaphoreGive(bleNotifyMutex);
}

static void flushBleNotification()
{
  if (!bleClientConnected || !bleStatusCharHandle)
  {
    bleNotifyCurrent = "";
    bleNotifyOffset = 0;
    if (bleNotifyMutex && xSemaphoreTake(bleNotifyMutex, 0) == pdTRUE)
    {
      bleNotifyQueue.clear();
      xSemaphoreGive(bleNotifyMutex);
    }
    return;
  }

  uint32_t now = millis();
  if ((now - bleLastNotifyMs) < BLE_NOTIFY_INTERVAL_MS)
    return;

  if (bleNotifyCurrent.isEmpty())
  {
    if (!bleNotifyMutex || xSemaphoreTake(bleNotifyMutex, 0) != pdTRUE)
      return;
    if (!bleNotifyQueue.empty())
    {
      bleNotifyCurrent = bleNotifyQueue.front();
      bleNotifyQueue.pop_front();
      bleNotifyOffset = 0;
    }
    xSemaphoreGive(bleNotifyMutex);
    if (bleNotifyCurrent.isEmpty())
      return;
  }

  size_t chunkSize = 20;
  if (!bleConnHandles.empty())
  {
    chunkSize = 514;
    for (uint16_t connHandle : bleConnHandles)
    {
      uint16_t mtu = ble_att_mtu(connHandle);
      size_t peerChunkSize = mtu > 3 ? (size_t)mtu - 3 : 20;
      if (peerChunkSize < chunkSize)
        chunkSize = peerChunkSize;
    }
  }

  size_t remaining = bleNotifyCurrent.length() - bleNotifyOffset;
  size_t length = remaining < chunkSize ? remaining : chunkSize;
  const uint8_t *data = reinterpret_cast<const uint8_t *>(bleNotifyCurrent.c_str() + bleNotifyOffset);
  // ble_gatts_notify_custom() consumes the mbuf regardless of outcome and
  // returns a status - only advance past this chunk on success, same
  // retry-on-congestion reasoning as the old NimBLECharacteristic::notify()
  // return-value check this replaces.
  bool sent = false;
  for (uint16_t connHandle : bleConnHandles)
  {
    struct os_mbuf *om = ble_hs_mbuf_from_flat(data, length);
    if (om && ble_gatts_notify_custom(connHandle, bleStatusCharHandle, om) == 0)
      sent = true;
  }
  bleLastNotifyMs = now;
  if (!sent)
    return;
  bleNotifyOffset += length;

  if (bleNotifyOffset >= bleNotifyCurrent.length())
  {
    bleNotifyCurrent = "";
    bleNotifyOffset = 0;
  }
}

// Finds the bond matching addr (if any) among NimBLE's own bond store and
// deletes it. Separate from the trustedBle list: that's our app-level "is
// this device allowed to talk to us" decision, while a bond is the BLE
// stack's own encryption keys for a peer. Used so revoking a device (forget,
// a never-confirmed pairing timing out, or an outright rejection) also drops
// its crypto keys instead of leaving a "ghost" bond the stack would happily
// re-encrypt against later despite the device no longer being trusted.
static constexpr int BLE_MAX_BONDS = 16;

static bool deleteBleBondByAddr(const String &addr)
{
  ble_addr_t peers[BLE_MAX_BONDS];
  int count = 0;
  ble_store_util_bonded_peers(peers, &count, BLE_MAX_BONDS);
  for (int i = 0; i < count; ++i)
  {
    if (addrEqual(bleAddrToString(peers[i]), addr))
      return ble_store_util_delete_peer(&peers[i]) == 0;
  }
  return false;
}

String trustGetBleBondsCsv()
{
  ble_addr_t peers[BLE_MAX_BONDS];
  int count = 0;
  ble_store_util_bonded_peers(peers, &count, BLE_MAX_BONDS);
  String csv;
  for (int i = 0; i < count; ++i)
  {
    if (csv.length() > 0)
      csv += ",";
    csv += bleAddrToString(peers[i]);
  }
  return csv;
}

bool trustClearBleBonds()
{
  ble_addr_t peers[BLE_MAX_BONDS];
  int count = 0;
  ble_store_util_bonded_peers(peers, &count, BLE_MAX_BONDS);
  if (count == 0)
    return false;
  for (int i = 0; i < count; ++i)
    ble_store_util_delete_peer(&peers[i]);
  return true;
}

// Persists across GATT writes so a command split across two "Write Without Response" PDUs
// (no trailing '\n' in the first one) is reassembled instead of being dispatched truncated.
// Only ever touched from the main loop (see pollCommunications) - never from the access callback.
static String bufferBle;

// Raw bytes handed off from the GATT access callback to the main loop. That
// callback runs on the NimBLE host task, which has a small internal stack;
// handleCommand() is a large dispatcher that used to run directly from the
// old Bluedroid onWrite() callback and could overflow a small callback
// stack (reproduced on ESP32-S3: "Guru Meditation Error ... Stack canary
// watchpoint triggered", reliably right after a few quick commands). The
// access callback now only ever appends bytes here; pollCommunications()
// drains this into bufferBle and dispatches on the main loop's much larger
// stack instead.
static String bleRxPending;
static SemaphoreHandle_t bleRxMutex = nullptr;

static ble_uuid128_t bleServiceUuid;
static ble_uuid128_t bleCommandCharUuid;
static ble_uuid128_t bleStatusCharUuid;

static int bleCommandAccessCb(uint16_t conn_handle, uint16_t attr_handle,
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
  lastBtActivityMs = millis();
  // Stack buffer, not heap - this runs on the host task for every single
  // write. 600 comfortably covers any single ATT write at our 517-byte
  // preferred MTU (minus ATT op/handle overhead); a command spanning
  // multiple writes is reassembled in bleRxPending/bufferBle regardless.
  uint8_t buf[600];
  uint16_t copyLen = len < sizeof(buf) ? len : (uint16_t)sizeof(buf);
  if (os_mbuf_copydata(ctxt->om, 0, copyLen, buf) != 0)
    return 0;
  if (!bleRxMutex || xSemaphoreTake(bleRxMutex, pdMS_TO_TICKS(20)) != pdTRUE)
    return 0;
  if (bleRxPending.length() < LINE_BUFFER_MAX)
  {
    size_t room = LINE_BUFFER_MAX - bleRxPending.length();
    size_t n = copyLen < room ? copyLen : room;
    for (size_t i = 0; i < n; i++)
      bleRxPending += (char)buf[i];
  }
  xSemaphoreGive(bleRxMutex);
  return 0;
}

// Backs the status characteristic's READ access - its current value is
// whatever updateBleStatus() last set. Notifications (the primary way
// clients actually receive status) go through flushBleNotification()
// instead; this is only hit on an explicit GATT read.
static String bleStatusReadValue;

static int bleStatusAccessCb(uint16_t conn_handle, uint16_t attr_handle,
                             struct ble_gatt_access_ctxt *ctxt, void *arg)
{
  (void)conn_handle;
  (void)attr_handle;
  (void)arg;
  if (ctxt->op != BLE_GATT_ACCESS_OP_READ_CHR)
    return 0;
  int rc = os_mbuf_append(ctxt->om, bleStatusReadValue.c_str(), bleStatusReadValue.length());
  return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
}

static struct ble_gatt_chr_def bleSvcChars[] = {
    {
        .uuid = &bleCommandCharUuid.u,
        .access_cb = bleCommandAccessCb,
        // _ENC on both characteristics: belt-and-suspenders alongside the GAP
        // event callback's explicit ble_gap_security_initiate() call. That
        // call forces pairing the instant a link comes up, but it's still an
        // async handshake - without _ENC here, a write arriving in the
        // (normally brief) window before it completes would reach
        // bleRxPending, and from there handleCommand(), over a still-
        // unencrypted link. With _ENC, the stack itself refuses GATT access
        // until the link is actually encrypted, regardless of that timing.
        .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP | BLE_GATT_CHR_F_WRITE_ENC,
        .val_handle = &bleCommandCharHandle,
    },
    {
        .uuid = &bleStatusCharUuid.u,
        .access_cb = bleStatusAccessCb,
        // NOTIFY only (no INDICATE) - the firmware only ever calls
        // ble_gatts_notify_custom(), and the host manages the 0x2902 CCCD
        // automatically from the characteristic's flags.
        .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_READ_ENC | BLE_GATT_CHR_F_NOTIFY,
        .val_handle = &bleStatusCharHandle,
    },
    {0},
};

static struct ble_gatt_svc_def bleGattSvcs[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &bleServiceUuid.u,
        .characteristics = bleSvcChars,
    },
    {0},
};

static void startBleAdvertising()
{
  if (ble_gap_adv_active())
    return;

  struct ble_hs_adv_fields fields = {};
  fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
  fields.tx_pwr_lvl_is_present = 1;
  fields.tx_pwr_lvl = BLE_HS_ADV_TX_PWR_LVL_AUTO;
  fields.name = (const uint8_t *)bleName.c_str();
  fields.name_len = bleName.length();
  fields.name_is_complete = 1;
  fields.appearance = 0;
  fields.appearance_is_present = 1;
  static const uint8_t slaveItvlRange[4] = {0x06, 0x00, 0x12, 0x00}; // matches the old setPreferredParams(0x06, 0x12)
  fields.slave_itvl_range = slaveItvlRange;
  ble_gap_adv_set_fields(&fields);

  // Service UUIDs go in the scan response, not the main ADV payload - a
  // 128-bit UUID is 18 bytes with its header, and the device name above
  // already uses a good chunk of the 31-byte legacy ADV payload. Matches
  // NimBLEAdvertising's own enableScanResponse(true) behavior, which split
  // overflow into the scan response the same way.
  static ble_uuid128_t scanRspUuids[2];
  scanRspUuids[0] = bleServiceUuid;
  uint8_t numUuids = 1;
#if ENABLE_BLE_MIDI
  extern ble_uuid128_t midiServiceUuid; // defined in midi_ble.cpp
  scanRspUuids[1] = midiServiceUuid;
  numUuids = 2;
#endif
  struct ble_hs_adv_fields rspFields = {};
  rspFields.uuids128 = scanRspUuids;
  rspFields.num_uuids128 = numUuids;
  rspFields.uuids128_is_complete = 1;
  ble_gap_adv_rsp_set_fields(&rspFields);

  struct ble_gap_adv_params advParams = {};
  advParams.conn_mode = BLE_GAP_CONN_MODE_UND;
  advParams.disc_mode = BLE_GAP_DISC_MODE_GEN;
  ble_gap_adv_start(BLE_OWN_ADDR_PUBLIC, NULL, BLE_HS_FOREVER, &advParams, bleGapEvent, NULL);
}

static int bleGapEvent(struct ble_gap_event *event, void *arg)
{
  (void)arg;
  switch (event->type)
  {
  case BLE_GAP_EVENT_CONNECT:
  {
    if (event->connect.status == 0)
    {
      struct ble_gap_conn_desc desc;
      if (ble_gap_conn_find(event->connect.conn_handle, &desc) != 0)
        break;
      bleConnHandles.push_back(event->connect.conn_handle);
      String addr = bleAddrToString(desc.peer_ota_addr);
      bleLastAddr = addr;
      bleClientConnected = true;
      lastBtActivityMs = millis();
      if (feedbackAllowed())
      {
        Serial.print(F("[BLE] Verbindung (ungesichert): "));
        Serial.println(addr);
      }
      if (ble_gap_security_initiate(event->connect.conn_handle) != 0)
      {
        if (feedbackAllowed())
          Serial.println(F("[BLE] Security-Request fehlgeschlagen"));
        ble_gap_terminate(event->connect.conn_handle, BLE_ERR_REM_USER_CONN_TERM);
      }
    }
    else
    {
      // Connection attempt itself failed - the controller stops advertising
      // regardless, re-arm it.
      startBleAdvertising();
    }
    break;
  }
  case BLE_GAP_EVENT_DISCONNECT:
  {
    uint16_t connHandle = event->disconnect.conn.conn_handle;
    auto it = std::find(bleConnHandles.begin(), bleConnHandles.end(), connHandle);
    if (it != bleConnHandles.end())
      bleConnHandles.erase(it);
    bleClientConnected = !bleConnHandles.empty();
    String addr = bleAddrToString(event->disconnect.conn.peer_id_addr);
    if (feedbackAllowed())
    {
      Serial.print(F("[BLE] Getrennt: "));
      Serial.println(addr);
    }
    bleLastAddr = addr;
#if ENABLE_BT_PAIRING
    // A device that disconnected (or was dropped - see the pairing-timeout
    // check in pollCommunications()) while waiting on the physical-gesture
    // confirmation never got to prove itself - forget it on both counts
    // instead of leaving a confirmed-nowhere bond sitting in the store.
    if (blePairPending && connHandle == blePendingConnId)
    {
      deleteBleBondByAddr(addr);
      blePairPending = false;
      blePendingAddr = "";
    }
#endif
    startBleAdvertising();
    break;
  }
  case BLE_GAP_EVENT_ENC_CHANGE:
  {
    // Fires once pairing/encryption (triggered by our own
    // ble_gap_security_initiate() call above) has run its course, success
    // or failure - this is where trust is actually decided now.
    uint16_t connHandle = event->enc_change.conn_handle;
    struct ble_gap_conn_desc desc;
    if (ble_gap_conn_find(connHandle, &desc) != 0)
      break;
    if (event->enc_change.status != 0 || !desc.sec_state.encrypted)
    {
      if (feedbackAllowed())
        Serial.println(F("[BLE] Pairing/Verschlüsselung fehlgeschlagen"));
      ble_gap_terminate(connHandle, BLE_ERR_REM_USER_CONN_TERM);
      break;
    }
    // Stable across the central's over-the-air address rotating on later
    // reconnects, now that it's bonded and resolvable.
    String addr = bleAddrToString(desc.peer_id_addr);
    bleLastAddr = addr;
    if (!allowBleAddr(addr))
    {
#if ENABLE_BT_PAIRING
      blePairPending = true;
      blePairStartMs = millis();
      blePendingAddr = addr;
      blePendingConnId = connHandle;
      lastBtActivityMs = millis();
      // Structured marker (force=true) so a connected client can tell
      // "deliberately holding you pending a physical gesture" apart from a
      // generic status-snapshot timeout and wait this out instead of giving
      // up and reconnecting (which would just restart this same 20s window
      // from zero on the firmware side).
      sendFeedback(String(F("BLE_PAIR_PENDING|")) + addr, true);
      if (feedbackAllowed())
        sendFeedback(String(F("[BLE] Pair request ")) + addr + F(" – confirm via switch or poti"));
#else
      if (feedbackAllowed())
      {
        Serial.print(F("[BLE] Rejecting unknown "));
        Serial.println(addr);
      }
      // A real (encrypted) bond now exists for this address even though
      // we're not authorizing it - drop the keys too, not just the
      // connection, so it can't silently re-encrypt against them later.
      deleteBleBondByAddr(addr);
      ble_gap_terminate(connHandle, BLE_ERR_REM_USER_CONN_TERM);
#endif
      break;
    }
    lastBtActivityMs = millis();
    if (addToList(trustedBle, addr))
      saveSettings();
    if (feedbackAllowed())
    {
      Serial.print(F("[BLE] Verbunden (gesichert): "));
      Serial.println(addr);
    }
    // The controller stops connectable advertising once a central connects;
    // re-arm it here (bounded by CONFIG_BT_NIMBLE_MAX_CONNECTIONS, default
    // 3) so additional trusted devices can still find and connect to the
    // lamp while this one stays connected.
    if ((int)bleConnHandles.size() < BLE_MAX_CONCURRENT_CLIENTS)
      startBleAdvertising();
    break;
  }
  case BLE_GAP_EVENT_ADV_COMPLETE:
    startBleAdvertising();
    break;
  default:
    break;
  }
  return 0;
}

static void bleHostTask(void *param)
{
  (void)param;
  nimble_port_run();
  nimble_port_freertos_deinit();
}

static void bleOnReset(int reason)
{
  if (feedbackAllowed())
  {
    Serial.print(F("[BLE] Host reset, reason="));
    Serial.println(reason);
  }
}

static void bleOnSync()
{
  // Ensures a usable address is set up (classic ESP32 always has a factory
  // public BT MAC, so this is normally a no-op) before advertising, same
  // ordering as Espressif's own NimBLE examples.
  ble_hs_util_ensure_addr(0);
  // Force updateBleStatus()'s own one-time internal buffer reservation to
  // happen right here, in this safe, early, single-threaded context - not
  // lazily deferred to whenever the first real status print happens to run
  // post-boot. That deferred-until-first-real-use timing was itself part of
  // a real-hardware heap corruption bug fixed earlier (see git history).
  updateBleStatus("");
  startBleAdvertising();
  Serial.println(F("[BLE] Werbung aktiv. Über BLE-Kommandos steuerbar."));
}

/**
 * @brief Configure and advertise the BLE service.
 */
void startBle()
{
  // Confirmed on real hardware: without this, Arduino's own early startup
  // releases the Classic-BT controller memory before nimble_port_init()
  // gets a chance to use it. esp32-hal-bt.c has a strong, non-weak
  // btInUse() -> true on classic ESP32 that's supposed to prevent exactly
  // that release - but only if the linker actually pulls that translation
  // unit in, which nothing else here references. Calling btStarted() (a
  // plain status check, return value unused) just forces that link - the
  // same workaround NimBLE-Arduino's own NimBLEDevice::init() used, and the
  // one spikes/ble-mesh/'s native Mesh node already confirmed necessary.
  btStarted();

  ESP_ERROR_CHECK(nimble_port_init());

  ble_hs_cfg.reset_cb = bleOnReset;
  ble_hs_cfg.sync_cb = bleOnSync;
  ble_hs_cfg.store_status_cb = ble_store_util_status_rr;
  // Real pairing/bonding: bonding+LE-Secure-Connections, no MITM protection,
  // "Just Works" (no PIN/passkey - neither side has a display or keypad).
  // This only gets encryption and a stable, resolvable identity address for
  // the trust decision in the ENC_CHANGE handler above - it does NOT by
  // itself decide whether a device is *authorized*. Just Works pairing can
  // be initiated by any nearby device with zero human-visible confirmation,
  // so the actual authorization gate (trust grace window / physical
  // switch-or-poti gesture, same as before) still happens after bonding
  // completes, not instead of it.
  ble_hs_cfg.sm_bonding = 1;
  ble_hs_cfg.sm_mitm = 0;
  ble_hs_cfg.sm_sc = 1;
  ble_hs_cfg.sm_io_cap = BLE_HS_IO_NO_INPUT_OUTPUT;
  ble_hs_cfg.sm_our_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
  ble_hs_cfg.sm_their_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;

  ble_att_set_preferred_mtu(517);

  ble_svc_gap_init();
  ble_svc_gatt_init();
  // NVS-backed persistent bond storage - replaces NimBLEDevice's own
  // automatic bond persistence.
  ble_store_config_init();

  bleServiceUuid = uuid128FromString(Settings::BLE_SERVICE_UUID);
  bleCommandCharUuid = uuid128FromString(Settings::BLE_COMMAND_CHAR_UUID);
  bleStatusCharUuid = uuid128FromString(Settings::BLE_STATUS_CHAR_UUID);

  int rc = ble_gatts_count_cfg(bleGattSvcs);
  ESP_ERROR_CHECK(rc == 0 ? ESP_OK : ESP_FAIL);
  rc = ble_gatts_add_svcs(bleGattSvcs);
  ESP_ERROR_CHECK(rc == 0 ? ESP_OK : ESP_FAIL);

#if ENABLE_BLE_MIDI
  setupBleMidi();
#endif

  ble_svc_gap_device_name_set(bleName.c_str());
  // Deliberately NOT calling ble_gatts_start() here: the host calls it
  // itself (ble_hs_sync() -> ble_hs_start() -> ble_gatts_start()) once it
  // syncs with the controller, confirmed in Espressif's own bleprph example
  // (which never calls it either). Calling it a second time here corrupted
  // the attribute table on real hardware - ble_att_svr_free_start_mem()
  // from the host's own internal call freed and recalloc'd the same memory
  // our explicit call had just registered 19 attributes into, and the
  // stale ble_att_svr_list head (never reset) ended up aliasing the fresh
  // mempool's own internal free-block chain, crashing on the next lookup.

  nimble_port_freertos_init(bleHostTask);
}
#endif

#if ENABLE_BT_SERIAL
/**
 * @brief Initialize the optional Bluetooth SPP serial port.
 */
void startBtSerial()
{
  if (!serialBt.begin(btName))
  {
    Serial.println(F("[BT] Classic Serial konnte nicht gestartet werden."));
  }
  else
  {
    Serial.print(F("[BT] Classic Serial aktiv als '"));
    Serial.print(btName);
    Serial.println(F("'"));
    serialBt.register_callback([](esp_spp_cb_event_t event, esp_spp_cb_param_t *param)
                              { sppCallbackLocal(event, param); });
    btSerialActive = true;
  }
}
#endif

#if ENABLE_BT_SERIAL
void setBtSleepAfterBootMs(uint32_t ms) { btSleepAfterBootMs = ms; }
void setBtSleepAfterBleMs(uint32_t ms) { btSleepAfterBleMs = ms; }
uint32_t getBtSleepAfterBootMs() { return btSleepAfterBootMs; }
uint32_t getBtSleepAfterBleMs() { return btSleepAfterBleMs; }
#endif

void trustSetBootMs(uint32_t ms) { trustBootMs = ms; }

void trustSetLists(const String &bleCsv, const String &btCsv)
{
  parseList(bleCsv, trustedBle);
  parseList(btCsv, trustedBt);
}

String trustGetBleCsv() { return joinList(trustedBle); }
String trustGetBtCsv() { return joinList(trustedBt); }

bool trustAddBle(const String &addr, bool persist)
{
  if (addToList(trustedBle, addr))
  {
    if (persist)
      saveSettings();
    return true;
  }
  return false;
}
bool trustAddBt(const String &addr, bool persist)
{
  if (addToList(trustedBt, addr))
  {
    if (persist)
      saveSettings();
    return true;
  }
  return false;
}

bool trustRemoveBle(const String &addr, bool persist)
{
#if ENABLE_BLE
  // "Forget" should mean forget - drop the crypto bond along with the
  // app-level trust entry, so the device can't silently re-encrypt against
  // keys we otherwise no longer consider it trusted with.
  deleteBleBondByAddr(addr);
#endif
  if (removeFromList(trustedBle, addr))
  {
    if (persist)
      saveSettings();
    return true;
  }
  return false;
}
bool trustRemoveBt(const String &addr, bool persist)
{
  if (removeFromList(trustedBt, addr))
  {
    if (persist)
      saveSettings();
    return true;
  }
  return false;
}


/**
 * @brief Confirms whichever pending pairing (BT-Classic and/or BLE) is
 * currently waiting on a physical gesture - called from the switch/poti
 * handlers. Both are checked independently (not an if/else-if chain) since
 * a BT device and a BLE device could plausibly be pending confirmation at
 * the same time.
 */
void confirmPendingPairing(const char *reason)
{
  (void)reason; // unused if neither sub-block below is compiled in
#if ENABLE_BT_PAIRING
#if ENABLE_BT_SERIAL
  if (btPairPending)
  {
    if (!serialBt.hasClient())
    {
      btPairPending = false;
      btPendingAddr = "";
    }
    else
    {
      btPairPending = false;
      btPairStartMs = 0;
      if (btPendingAddr.length() > 0)
      {
        trustAddBt(btPendingAddr);
        lastSppAddr = btPendingAddr;
      }
      btPendingAddr = "";
      if (feedbackAllowed())
      {
        String msg = F("[BT] Pair confirmed");
        if (reason && reason[0])
        {
          msg += F(" (");
          msg += reason;
          msg += F(")");
        }
        sendFeedback(msg);
      }
      lastBtActivityMs = millis();
    }
  }
#endif
#if ENABLE_BLE
  if (blePairPending)
  {
    blePairPending = false;
    blePairStartMs = 0;
    if (blePendingAddr.length() > 0)
    {
      trustAddBle(blePendingAddr);
      bleLastAddr = blePendingAddr;
    }
    blePendingAddr = "";
    if (feedbackAllowed())
    {
      String msg = F("[BLE] Pair confirmed");
      if (reason && reason[0])
      {
        msg += F(" (");
        msg += reason;
        msg += F(")");
      }
      sendFeedback(msg);
    }
    lastBtActivityMs = millis();
  }
#endif
#endif
}

void trustListFeedback()
{
  sendFeedback(String(F("[Trust] BLE: ")) + trustGetBleCsv());
  sendFeedback(String(F("[Trust] BT : ")) + trustGetBtCsv());
#if ENABLE_BLE
  sendFeedback(String(F("[Trust] BLE bonds: ")) + trustGetBleBondsCsv());
#endif
}



void setupCommunications()
{
#if ENABLE_BLE
  if (!bleNotifyMutex)
    bleNotifyMutex = xSemaphoreCreateMutex();
  if (!bleRxMutex)
    bleRxMutex = xSemaphoreCreateMutex();
#endif
#if ENABLE_BT_SERIAL
  startBtSerial();
#endif
#if ENABLE_BLE
  startBle();
#else
  if (feedbackAllowed())
    Serial.println(F("[BLE] deaktiviert (ENABLE_BLE=0)."));
#endif
  bootMsComm = millis();
  lastBtActivityMs = bootMsComm;
}

#if ENABLE_BT_SERIAL
// Auto sleep for Classic BT serial after inactivity/timeout
static void maybeSleepBtSerial(uint32_t nowMs)
{
  if (!btSerialActive || btSerialDisabled)
    return;
  bool bootTimeout = (btSleepAfterBootMs > 0) && (nowMs - bootMsComm >= btSleepAfterBootMs);
  bool inactivityTimeout = (btSleepAfterBleMs > 0) && (lastBtActivityMs > 0) && (nowMs - lastBtActivityMs >= btSleepAfterBleMs);
  if (bootTimeout || inactivityTimeout)
    powerOffBtSerial(bootTimeout ? "boot timeout" : "idle timeout");
}
#endif

/**
 * @brief Poll all enabled transports for incoming bytes and feed line parser.
 */
void pollCommunications()
{
  while (Serial.available())
  {
    char c = (char)Serial.read();
    processInputChar(bufferUsb, c);
  }

#if ENABLE_BT_SERIAL
  maybeSleepBtSerial(millis());
#if ENABLE_BT_PAIRING
  if (btPairPending && btPairStartMs > 0 && (millis() - btPairStartMs) > BT_PAIR_TIMEOUT_MS)
  {
    if (feedbackAllowed())
      sendFeedback(F("[BT] Pair request timed out"));
    if (serialBt.hasClient())
      serialBt.disconnect();
    btPairPending = false;
    btPendingAddr = "";
  }
#endif
  if (serialBt.hasClient())
  {
    while (serialBt.available())
    {
      lastBtActivityMs = millis();
      char c = (char)serialBt.read();
#if ENABLE_BT_MIDI
      processBtMidiByte((uint8_t)c);
#endif
#if ENABLE_BT_PAIRING
      if (btPairPending)
        continue; // commands are gated until pairing is confirmed
#endif
      processInputChar(bufferBt, c);
    }
  }
#endif
#if ENABLE_BLE
#if ENABLE_BT_PAIRING
  if (blePairPending && blePairStartMs > 0 && (millis() - blePairStartMs) > BLE_PAIR_TIMEOUT_MS)
  {
    if (feedbackAllowed())
      sendFeedback(F("[BLE] Pair request timed out"));
    // Never confirmed - forget the bond the security handshake already
    // created for it, same as the disconnect path for this case.
    deleteBleBondByAddr(blePendingAddr);
    ble_gap_terminate(blePendingConnId, BLE_ERR_REM_USER_CONN_TERM);
    blePairPending = false;
    blePendingAddr = "";
  }
#endif
  if (bleRxMutex && bleRxPending.length() && xSemaphoreTake(bleRxMutex, 0) == pdTRUE)
  {
    String chunk = bleRxPending;
    bleRxPending = "";
    xSemaphoreGive(bleRxMutex);
#if ENABLE_BT_PAIRING
    if (blePairPending)
    {
      // Commands are gated until pairing is confirmed - matches
      // BT-Classic's equivalent "if (btPairPending) continue;" above: the
      // bytes are simply dropped rather than buffered for later.
    }
    else
#endif
    {
      for (size_t i = 0; i < chunk.length(); i++)
        processInputChar(bufferBle, chunk[i]);
    }
  }
  flushBleNotification();
#endif
}

/**
 * @brief Broadcast a single text line to Serial, BT Serial (if connected) and BLE notify.
 */
void sendFeedback(const String &line, const bool &force)
{
  bool allow = feedbackAllowed();
  if (!allow && !force)
    return;

  Serial.println(line);
#if ENABLE_BT_SERIAL
  if ((allow || force) && serialBt.hasClient())
  {
    serialBt.println(line);
  }
#endif
#if ENABLE_BLE
  // Queue newline-delimited feedback and split it according to the negotiated
  // ATT MTU. The main loop drains the queue so the GATT access callback never
  // bursts large status snapshots into a single oversized notification.
  if ((allow || force) && bleClientConnected && bleStatusCharHandle)
    queueBleNotification(line);
#endif
}


/**
 * @brief Update BLE status characteristic (read + notify if connected).
 */
void updateBleStatus(const String &statusPayload)
{
#if ENABLE_BLE
  if (!bleStatusCharHandle)
    return;
  // Only refresh the readable characteristic value (with newline terminator).
  // Notification is handled by the preceding sendFeedback() of the same line,
  // so we avoid sending each status line twice.
  //
  // `payload` is function-static and reserved once, not a fresh local String
  // built via `statusPayload + "\n"` every call - that `operator+` always
  // allocates a brand-new temporary (StringSumHelper), which reproduced a
  // real TLSF heap corruption on real hardware (see git history around this
  // comment) when combined with print.cpp's own status-string builders.
  // 750: must cover whichever of print.cpp's own STATUS/STATUS1/STATUS2/
  // STATUS_END lines is longest, plus the appended '\n' - those are each
  // reserve(700) now (see print.cpp) after discovering their OWN reserve()
  // sizes had quietly fallen under the real measured content for a while
  // (420 vs. an actual 555 bytes, 450 vs. 494, etc.) - trust a fresh byte
  // count over any of these round numbers if the field set grows again,
  // this one included.
  static bool payloadReserved = false;
  if (!payloadReserved)
  {
    bleStatusReadValue.reserve(750);
    payloadReserved = true;
  }
  bleStatusReadValue = statusPayload;
  bleStatusReadValue += '\n';
#else
  (void)statusPayload;
#endif
}

/**
 * @return True if a BLE client is connected.
 */
bool bleActive()
{
#if ENABLE_BLE
  return bleClientConnected;
#else
  return false;
#endif
}

/**
 * @return True if a BT Serial client is connected.
 */
bool btHasClient()
{
#if ENABLE_BT_SERIAL
  return serialBt.hasClient();
#else
  return false;
#endif
}

/**
 * @return Last known BLE client MAC address (empty if none).
 */
String getLastBleAddr()
{
#if ENABLE_BLE
  return bleLastAddr;
#else
  return String();
#endif
}

String getBLEAddress(){
#if ENABLE_BLE
  if (!ble_hs_synced())
    return F("N/A");
  uint8_t addrVal[6];
  if (ble_hs_id_copy_addr(BLE_ADDR_PUBLIC, addrVal, NULL) != 0)
    return F("N/A");
  ble_addr_t a{};
  a.type = BLE_ADDR_PUBLIC;
  memcpy(a.val, addrVal, 6);
  return bleAddrToString(a);
#else
  return F("N/A");
#endif
}
