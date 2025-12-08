#include "comms.h"

#include "lamp_config.h"
#include "settings.h"
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
#include <BLEAdvertising.h>
// #include <BLEAdvertisementData.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#endif

/**
 * @file comms.cpp
 * @brief Implements USB, Bluetooth Serial, and BLE command handling.
 */

// Provided by main.cpp to route parsed command strings.
void handleCommand(String line);
void blePresenceUpdate(bool connected, const String &addr);

    namespace
{
bool feedbackArmed = !Settings::FEEDBACK_NEEDS_HANDSHAKE;
inline bool feedbackAllowed()
{
  return !Settings::FEEDBACK_NEEDS_HANDSHAKE || feedbackArmed;
}
inline void armFeedback()
{
  feedbackArmed = true;
}
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
  else if (buffer.length() < 64)
  {
    buffer += c;
  }
}

// Line buffer for USB serial input
String bufferUsb;

#if ENABLE_BT_SERIAL
BluetoothSerial serialBt;
// Line buffer for BT serial input
String bufferBt;
String lastSppAddr;

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
      if (feedbackAllowed())
      {
        Serial.print(F("[BT] Client connected: "));
        Serial.println(lastSppAddr);
        sendFeedback(String(F("[BT] Client connected ")) + lastSppAddr);
      }
    }
    else
    {
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
    break;
  default:
    break;
  }
}
#endif

#if ENABLE_BLE
BLEServer *bleServer = nullptr;
BLECharacteristic *bleCommandCharacteristic = nullptr;
BLECharacteristic *bleStatusCharacteristic = nullptr;
bool bleClientConnected = false;
// Most recent BLE client address
String bleLastAddr;

/**
 * @brief Handles BLE server connection events.
 */
class LampBleServerCallbacks : public BLEServerCallbacks
{
  String formatAddr(const uint8_t bda[6])
  {
    char buf[18];
    snprintf(buf, sizeof(buf), "%02X:%02X:%02X:%02X:%02X:%02X", bda[0], bda[1], bda[2], bda[3], bda[4], bda[5]);
    return String(buf);
  }

  // void onConnect(BLEServer *server) override
  // {
  //   bleClientConnected = true;
  //   // Serial.println(F("[BLE] Verbunden."));
  //   blePresenceUpdate(true, bleLastAddr);
  // }

  void onConnect(BLEServer *server, esp_ble_gatts_cb_param_t *param) override
  {
    bleClientConnected = true;
    String addr = formatAddr(param->connect.remote_bda);
    bleLastAddr = addr;
    if (feedbackAllowed())
    {
      Serial.print(F("[BLE] Verbunden: "));
      Serial.println(addr);
    }
    blePresenceUpdate(true, addr);
  }

  // void onDisconnect(BLEServer *server) override
  // {
  //   bleClientConnected = false;
  //   // Serial.println(F("[BLE] Getrennt."));
  //   blePresenceUpdate(false, bleLastAddr);
  //   BLEDevice::startAdvertising();
  // }

  void onDisconnect(BLEServer *server, esp_ble_gatts_cb_param_t *param) override
  {
    bleClientConnected = false;
    String addr = formatAddr(param->disconnect.remote_bda);
    if (feedbackAllowed())
    {
      Serial.print(F("[BLE] Getrennt: "));
      Serial.println(addr);
    }
    bleLastAddr = addr;
    blePresenceUpdate(false, addr);
    BLEDevice::startAdvertising();
  }
};

/**
 * @brief Receives BLE writes, splits into lines, and forwards commands.
 */
class LampBleCommandCallbacks : public BLECharacteristicCallbacks
{
  void onWrite(BLECharacteristic *characteristic) override
  {
    std::string value = characteristic->getValue();
    if (value.empty())
      return;
    String line;
    for (char c : value)
    {
      if (c == '\r' || c == '\n')
      {
        line.trim();
        if (!line.isEmpty())
        {
          armFeedback();
          handleCommand(line);
        }
        line = "";
      }
      else if (line.length() < 96)
      {
        line += c;
      }
    }
    line.trim();
    if (!line.isEmpty())
    {
      handleCommand(line);
    }
  }
};

/**
 * @brief Configure and advertise the BLE service.
 */
void startBle()
{
  BLEDevice::init(Settings::BLE_DEVICE_NAME);
  bleServer = BLEDevice::createServer();
  bleServer->setCallbacks(new LampBleServerCallbacks());
  BLEService *service = bleServer->createService(Settings::BLE_SERVICE_UUID);
  bleCommandCharacteristic = service->createCharacteristic(
      Settings::BLE_COMMAND_CHAR_UUID,
      BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR);
  bleCommandCharacteristic->setCallbacks(new LampBleCommandCallbacks());
  bleStatusCharacteristic = service->createCharacteristic(
      Settings::BLE_STATUS_CHAR_UUID,
      BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY | BLECharacteristic::PROPERTY_INDICATE);
  auto *cccStat = new BLE2902();
  cccStat->setNotifications(true);
  cccStat->setIndications(true);
  bleStatusCharacteristic->addDescriptor(cccStat);
  service->start();
  BLEAdvertising *advertising = BLEDevice::getAdvertising();
  advertising->addServiceUUID(Settings::BLE_SERVICE_UUID);

  // Keep lamp service in the primary advertisement (for WebBLE filters).
//   BLEAdvertisementData advData;
//   advData.setName(Settings::BLE_DEVICE_NAME);
//   advData.addServiceUUID(BLEUUID(Settings::BLE_SERVICE_UUID));
//   advertising->setAdvertisementData(advData);

//   // Put optional/extra services into the scan response to avoid overflowing the adv payload.
//   BLEAdvertisementData scanData;
// #if ENABLE_BLE_MIDI
  // scanData.addServiceUUID(BLEUUID(BLE_MIDI_SERVICE_UUID));
//   setupBleMidi(bleServer, advertising);
// #endif
//   advertising->setScanResponseData(scanData);
  advertising->setScanResponse(true);
  advertising->setMinPreferred(0x06);
  advertising->setMinPreferred(0x12);
#if ENABLE_BLE_MIDI
  setupBleMidi(bleServer, advertising);
#endif
  BLEDevice::startAdvertising();
  Serial.println(F("[BLE] Werbung aktiv. Über BLE-Kommandos steuerbar."));
}
#endif

#if ENABLE_BT_SERIAL
/**
 * @brief Initialize the optional Bluetooth SPP serial port.
 */
void startBtSerial()
{
  if (!serialBt.begin(Settings::BT_SERIAL_NAME))
  {
    Serial.println(F("[BT] Classic Serial konnte nicht gestartet werden."));
  }
  else
  {
    Serial.print(F("[BT] Classic Serial aktiv als '"));
    Serial.print(Settings::BT_SERIAL_NAME);
    Serial.println(F("'"));
    serialBt.register_callback([](esp_spp_cb_event_t event, esp_spp_cb_param_t *param)
                              { sppCallbackLocal(event, param); });
  }
}
} // namespace
#endif

void setupCommunications()
{
  #if ENABLE_BT_SERIAL
  startBtSerial();
  #endif
#if ENABLE_BLE
  startBle();
#else
  if (feedbackAllowed())
    Serial.println(F("[BLE] deaktiviert (ENABLE_BLE=0)."));
#endif
}

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
  if (serialBt.hasClient())
  {
    while (serialBt.available())
    {
      char c = (char)serialBt.read();
#if ENABLE_BT_MIDI
      processBtMidiByte((uint8_t)c);
#endif
      processInputChar(bufferBt, c);
    }
  }
#endif
}

/**
 * @brief Broadcast a single text line to Serial, BT Serial (if connected) and BLE notify.
 */
void sendFeedback(const String &line)
{
  if (!feedbackAllowed())
    return;
  Serial.println(line);
#if ENABLE_BT_SERIAL
  if (serialBt.hasClient())
  {
    serialBt.println(line);
  }
#endif
#if ENABLE_BLE
  // Send feedback only via status characteristic (notify/indicate) to keep GATT simple for clients.
  if (bleClientConnected && bleStatusCharacteristic)
  {
    bleStatusCharacteristic->setValue(line.c_str());
    bleStatusCharacteristic->notify();
  }
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
  bleStatusCharacteristic->setValue(statusPayload.c_str());
  if (bleClientConnected)
    bleStatusCharacteristic->notify();
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
  if (BLEDevice::getInitialized())
    return BLEDevice::getAddress().toString().c_str();
  else
    return F("N/A");
}
