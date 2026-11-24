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
// // UUID duplicated here so we can place it into scan response only.
// static constexpr const char *BLE_MIDI_SERVICE_UUID = "03B80E5A-EDE8-4B33-A751-6CE34EC4C700";
#endif

#if ENABLE_BT_SERIAL
#include <BluetoothSerial.h>
#include <esp_spp_api.h>
#endif

#if ENABLE_BLE
#include <NimBLEDevice.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
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
// that window. Reached from onAuthenticationComplete() once a connection has
// bonded but its identity address isn't on the trust list - see comms.cpp.
// Same 20s budget as BT-Classic's gesture window, same confirmation
// mechanism (confirmPendingPairing, called from the switch/poti handlers),
// same "commands are gated until confirmed" posture - deliberately no
// light/blink feedback, only text.
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

void setBleName(const String &name)
{
#if ENABLE_BLE
  if (name.length() >= 2)
  {
    bleName = name;
    // Live rename of advertisement can be flaky; store for next restart.
    if (NimBLEDevice::isInitialized())
    {
      auto *adv = NimBLEDevice::getAdvertising();
      if (adv)
      {
        adv->stop();
        adv->start();
      }
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
NimBLEServer *bleServer = nullptr;
NimBLECharacteristic *bleCommandCharacteristic = nullptr;
NimBLECharacteristic *bleStatusCharacteristic = nullptr;
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
// Matches NimBLE's default CONFIG_BT_NIMBLE_MAX_CONNECTIONS (not overridden in
// this project's sdkconfig). Keeps us from re-arming advertising once the
// controller can't accept another BLE link anyway.
static constexpr uint8_t BLE_MAX_CONCURRENT_CLIENTS = 3;

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
  if (!bleClientConnected || !bleStatusCharacteristic || !bleServer)
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
  const auto peers = bleServer->getPeerDevices();
  if (!peers.empty())
  {
    chunkSize = 514;
    for (uint16_t connHandle : peers)
    {
      uint16_t mtu = bleServer->getPeerMTU(connHandle);
      size_t peerChunkSize = mtu > 3 ? (size_t)mtu - 3 : 20;
      if (peerChunkSize < chunkSize)
        chunkSize = peerChunkSize;
    }
  }

  size_t remaining = bleNotifyCurrent.length() - bleNotifyOffset;
  size_t length = remaining < chunkSize ? remaining : chunkSize;
  const uint8_t *data = reinterpret_cast<const uint8_t *>(bleNotifyCurrent.c_str() + bleNotifyOffset);
  // NimBLECharacteristic::notify() returns whether the stack actually accepted
  // the chunk. Only advance past it on success - on a congested link (this
  // drains every 8ms, faster than most negotiated connection intervals), the
  // old Bluedroid BLECharacteristic::notify() had no way to report that back,
  // so a dropped chunk here used to be silently skipped instead of retried,
  // corrupting the line downstream. Leaving bleNotifyCurrent/Offset untouched
  // on failure just retries the same chunk on the next tick.
  bool sent = bleStatusCharacteristic->notify(data, length);
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
// NimBLEAddress has no plain "parse this MAC string" constructor (the ctor
// that takes a string also wants an explicit address type, which we don't
// know from a bare trust-list entry) - so match by comparing toString()
// against the stack's own bond list instead of constructing one by hand.
static bool deleteBleBondByAddr(const String &addr)
{
  int count = NimBLEDevice::getNumBonds();
  for (int i = 0; i < count; ++i)
  {
    NimBLEAddress bonded = NimBLEDevice::getBondedAddress(i);
    if (addrEqual(String(bonded.toString().c_str()), addr))
      return NimBLEDevice::deleteBond(bonded);
  }
  return false;
}

String trustGetBleBondsCsv()
{
  String csv;
  int count = NimBLEDevice::getNumBonds();
  for (int i = 0; i < count; ++i)
  {
    if (csv.length() > 0)
      csv += ",";
    csv += NimBLEDevice::getBondedAddress(i).toString().c_str();
  }
  return csv;
}

bool trustClearBleBonds()
{
  if (NimBLEDevice::getNumBonds() == 0)
    return false;
  return NimBLEDevice::deleteAllBonds();
}

/**
 * @brief Handles BLE server connection and pairing/bonding events.
 *
 * The trust decision used to be made in onConnect(), keyed off the central's
 * over-the-air address. That address rotates on unbonded Android/iOS centrals
 * (BLE privacy), which is exactly what the old ENABLE_BLE_TRUST_TOKEN hack
 * existed to work around. Real bonding (see startBle()) replaces that: every
 * connection is pushed through pairing via NimBLEDevice::startSecurity(), and
 * the trust decision moves to onAuthenticationComplete(), keyed off the
 * resolved, stable identity address (NimBLEConnInfo::getIdAddress()) instead.
 */
class LampBleServerCallbacks : public NimBLEServerCallbacks
{
  void onConnect(NimBLEServer *server, NimBLEConnInfo &connInfo) override
  {
    String addr = String(connInfo.getAddress().toString().c_str());
    bleLastAddr = addr;
    bleClientConnected = true;
    lastBtActivityMs = millis();
    if (feedbackAllowed())
    {
      Serial.print(F("[BLE] Verbindung (ungesichert): "));
      Serial.println(addr);
    }
    if (!NimBLEDevice::startSecurity(connInfo.getConnHandle()))
    {
      if (feedbackAllowed())
        Serial.println(F("[BLE] Security-Request fehlgeschlagen"));
      server->disconnect(connInfo.getConnHandle());
    }
  }

  // Fires once pairing/encryption (triggered by our own startSecurity() call
  // above) has run its course, success or failure - this is where trust is
  // actually decided now.
  void onAuthenticationComplete(NimBLEConnInfo &connInfo) override
  {
    uint16_t connHandle = connInfo.getConnHandle();
    if (!connInfo.isEncrypted())
    {
      if (feedbackAllowed())
        Serial.println(F("[BLE] Pairing/Verschlüsselung fehlgeschlagen"));
      if (bleServer)
        bleServer->disconnect(connHandle);
      return;
    }
    // Stable across the central's over-the-air address rotating on later
    // reconnects, now that it's bonded and resolvable.
    String addr = String(connInfo.getIdAddress().toString().c_str());
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
      if (bleServer)
        bleServer->disconnect(connHandle);
#endif
      return;
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
    // re-arm it here (bounded by the controller's max BLE connection count,
    // CONFIG_BT_NIMBLE_MAX_CONNECTIONS, default 3) so additional trusted
    // devices can still find and connect to the lamp while this one stays
    // connected.
    if (bleServer && bleServer->getConnectedCount() < BLE_MAX_CONCURRENT_CLIENTS)
      bleServer->startAdvertising();
  }

  void onDisconnect(NimBLEServer *server, NimBLEConnInfo &connInfo, int reason) override
  {
    (void)reason;
    bleClientConnected = server->getConnectedCount() > 0;
    String addr = String(connInfo.getIdAddress().toString().c_str());
    if (feedbackAllowed())
    {
      Serial.print(F("[BLE] Getrennt: "));
      Serial.println(addr);
    }
    bleLastAddr = addr;
#if ENABLE_BT_PAIRING
    uint16_t connHandle = connInfo.getConnHandle();
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
    NimBLEDevice::startAdvertising();
  }
};

// Persists across GATT writes so a command split across two "Write Without Response" PDUs
// (no trailing '\n' in the first one) is reassembled instead of being dispatched truncated.
// Only ever touched from the main loop (see pollCommunications) - never from onWrite().
static String bufferBle;

// Raw bytes handed off from onWrite() to the main loop. onWrite() runs on Bluedroid's
// BTC_TASK, which has a small internal stack; handleCommand() is a large dispatcher that used
// to run directly from onWrite() and could overflow it (reproduced on ESP32-S3: "Guru
// Meditation Error ... Stack canary watchpoint triggered (BTC_TASK)", reliably right after a
// few quick commands). onWrite() now only ever appends bytes here; pollCommunications() drains
// this into bufferBle and dispatches on the main loop's much larger stack instead.
static String bleRxPending;
static SemaphoreHandle_t bleRxMutex = nullptr;

/**
 * @brief Receives BLE writes and hands the raw bytes off to the main loop (see bleRxPending).
 */
class LampBleCommandCallbacks : public NimBLECharacteristicCallbacks
{
  void onWrite(NimBLECharacteristic *characteristic, NimBLEConnInfo &connInfo) override
  {
    (void)connInfo;
    NimBLEAttValue value = characteristic->getValue();
    if (value.length() == 0)
      return;
    lastBtActivityMs = millis();
    if (!bleRxMutex || xSemaphoreTake(bleRxMutex, pdMS_TO_TICKS(20)) != pdTRUE)
      return;
    if (bleRxPending.length() < LINE_BUFFER_MAX)
    {
      size_t room = LINE_BUFFER_MAX - bleRxPending.length();
      size_t n = value.length() < room ? value.length() : room;
      const uint8_t *data = value.data();
      for (size_t i = 0; i < n; i++)
        bleRxPending += (char)data[i];
    }
    xSemaphoreGive(bleRxMutex);
  }
};

/**
 * @brief Configure and advertise the BLE service.
 */
void startBle()
{
  NimBLEDevice::init(bleName.c_str());
  NimBLEDevice::setMTU(517);
  // Real pairing/bonding: bonding+LE-Secure-Connections, no MITM protection,
  // "Just Works" (no PIN/passkey - neither side has a display or keypad).
  // This only gets encryption and a stable, resolvable identity address for
  // the trust decision in onAuthenticationComplete() below - it does NOT by
  // itself decide whether a device is *authorized*. Just Works pairing can
  // be initiated by any nearby device with zero human-visible confirmation,
  // so the actual authorization gate (trust grace window / physical
  // switch-or-poti gesture, same as before) still happens after bonding
  // completes, not instead of it.
  NimBLEDevice::setSecurityAuth(true, false, true);
  NimBLEDevice::setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT);
  bleServer = NimBLEDevice::createServer();
  bleServer->setCallbacks(new LampBleServerCallbacks());
  NimBLEService *service = bleServer->createService(Settings::BLE_SERVICE_UUID);
  // _ENC on both characteristics: belt-and-suspenders alongside onConnect()'s
  // explicit startSecurity() call. That call forces pairing the instant a
  // link comes up, but it's still an async handshake - without _ENC here, a
  // write arriving in the (normally brief) window before it completes would
  // reach bleRxPending, and from there handleCommand(), over a still-
  // unencrypted link. With _ENC, the stack itself refuses GATT access until
  // the link is actually encrypted, regardless of that timing.
  bleCommandCharacteristic = service->createCharacteristic(
      Settings::BLE_COMMAND_CHAR_UUID,
      NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR | NIMBLE_PROPERTY::WRITE_ENC);
  bleCommandCharacteristic->setCallbacks(new LampBleCommandCallbacks());
  // NOTIFY only (no INDICATE) - the firmware only ever calls notify(), and
  // NimBLE manages the 0x2902 CCCD automatically from the characteristic's
  // properties, so there's no separate descriptor to create/configure here
  // (unlike the old Bluedroid BLE2902).
  bleStatusCharacteristic = service->createCharacteristic(
      Settings::BLE_STATUS_CHAR_UUID,
      NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::READ_ENC | NIMBLE_PROPERTY::NOTIFY);
  // Force updateBleStatus()'s own one-time internal buffer reservation to
  // happen right here, in this safe, early, single-threaded setup context -
  // not lazily deferred to whenever the first real status print happens to
  // run post-boot. That deferred-until-first-real-use timing is exactly what
  // reproduced the TLSF heap corruption on real hardware: this characteristic
  // doesn't exist yet during the one boot-time printStatus(true) call (it
  // runs before setupCommunications()), so updateBleStatus() always no-ops
  // at boot and its lazy reserve() would otherwise happen for the first time
  // during a live command instead.
  updateBleStatus("");
  // NimBLEService::start() is a deprecated no-op in this NimBLE-Arduino
  // version - services start automatically with the server.
  NimBLEAdvertising *advertising = NimBLEDevice::getAdvertising();
  advertising->addServiceUUID(Settings::BLE_SERVICE_UUID);
  advertising->setAppearance(0);
  advertising->enableScanResponse(true);
  advertising->setPreferredParams(0x06, 0x12);
#if ENABLE_BLE_MIDI
  setupBleMidi(bleServer, advertising);
#endif
  NimBLEDevice::startAdvertising();
  Serial.println(F("[BLE] Werbung aktiv. Über BLE-Kommandos steuerbar."));
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
    // Never confirmed - forget the bond NimBLE's security handshake already
    // created for it, same as the onDisconnect() path for this case.
    deleteBleBondByAddr(blePendingAddr);
    if (bleServer)
      bleServer->disconnect(blePendingConnId);
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
  // ATT MTU. The main loop drains the queue so BLE callbacks never burst large
  // status snapshots into a single oversized notification.
  if ((allow || force) && bleClientConnected && bleStatusCharacteristic)
    queueBleNotification(line);
#endif
}


/**
 * @brief Update BLE status characteristic (read + notify if connected).
 */
void updateBleStatus(const String &statusPayload)
{
#if ENABLE_BLE
  if (!bleStatusCharacteristic)
    return;
  // Only refresh the readable characteristic value (with newline terminator).
  // Notification is handled by the preceding sendFeedback() of the same line,
  // so we avoid sending each status line twice.
  //
  // `payload` is function-static and reserved once, not a fresh local String
  // built via `statusPayload + "\n"` every call - that `operator+` always
  // allocates a brand-new temporary (StringSumHelper), which bypassed the
  // same fix applied to the STATUS/STATUS1/STATUS2 builders in print.cpp and
  // reproduced the exact same TLSF heap corruption on real hardware.
  // 600: comfortably above the largest of print.cpp's own reserved sizes
  // (STATUS1/lineIO's 500 is the biggest) plus the appended '\n' - this is
  // called for every STATUS/STATUS1/STATUS2/STATUS_END line in turn, so it
  // must cover whichever one of them is longest, not just one of them.
  static String payload;
  static bool payloadReserved = false;
  if (!payloadReserved)
  {
    payload.reserve(600);
    payloadReserved = true;
  }
  payload = statusPayload;
  payload += '\n';
  bleStatusCharacteristic->setValue(payload.c_str());
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
  if (NimBLEDevice::isInitialized())
    return NimBLEDevice::getAddress().toString().c_str();
  else
    return F("N/A");
#else
  return F("N/A");
#endif
}
