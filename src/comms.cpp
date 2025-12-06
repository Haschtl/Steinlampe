#include "comms.h"

#include "lamp_config.h"

#if ENABLE_BT_SERIAL
#include <BluetoothSerial.h>
#endif

#if ENABLE_BLE
#include <BLEAdvertising.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#endif

/**
 * @file comms.cpp
 * @brief Implements USB, Bluetooth Serial, and BLE command handling.
 */

// Provided by main.cpp to route parsed command strings.
void handleCommand(String line);

namespace
{
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
      handleCommand(buffer);
    buffer = "";
  }
  else if (buffer.length() < 64)
  {
    buffer += c;
  }
}

String bufferUsb;

#if ENABLE_BT_SERIAL
BluetoothSerial serialBt;
String bufferBt;
#endif

#if ENABLE_BLE
static const char *BLE_DEVICE_NAME = "Quarzlampe";
static const char *BLE_SERVICE_UUID = "d94d86d7-1eaf-47a4-9d1e-7a90bf34e66b";
static const char *BLE_COMMAND_CHAR_UUID = "4bb5047d-0d8b-4c5e-81cd-6fb5c0d1d1f7";

BLEServer *bleServer = nullptr;
BLECharacteristic *bleCommandCharacteristic = nullptr;
bool bleClientConnected = false;

/**
 * @brief Handles BLE server connection events.
 */
class LampBleServerCallbacks : public BLEServerCallbacks
{
  void onConnect(BLEServer *) override
  {
    bleClientConnected = true;
    Serial.println(F("[BLE] Verbunden."));
  }

  void onDisconnect(BLEServer *) override
  {
    bleClientConnected = false;
    Serial.println(F("[BLE] Getrennt."));
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
  BLEDevice::init(BLE_DEVICE_NAME);
  bleServer = BLEDevice::createServer();
  bleServer->setCallbacks(new LampBleServerCallbacks());
  BLEService *service = bleServer->createService(BLE_SERVICE_UUID);
  bleCommandCharacteristic = service->createCharacteristic(
      BLE_COMMAND_CHAR_UUID,
      BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR);
  bleCommandCharacteristic->setCallbacks(new LampBleCommandCallbacks());
  service->start();
  BLEAdvertising *advertising = BLEDevice::getAdvertising();
  advertising->addServiceUUID(BLE_SERVICE_UUID);
  advertising->setScanResponse(true);
  advertising->setMinPreferred(0x06);
  advertising->setMinPreferred(0x12);
  BLEDevice::startAdvertising();
  Serial.println(F("[BLE] Werbung aktiv. Über BLE-Kommandos steuerbar."));
}
#endif

/**
 * @brief Initialize the optional Bluetooth SPP serial port.
 */
void startBtSerial()
{
#if ENABLE_BT_SERIAL
  static const char *BT_SERIAL_NAME = "Quarzlampe-SPP";
  if (!serialBt.begin(BT_SERIAL_NAME))
  {
    Serial.println(F("[BT] Classic Serial konnte nicht gestartet werden."));
  }
  else
  {
    Serial.print(F("[BT] Classic Serial aktiv als '"));
    Serial.print(BT_SERIAL_NAME);
    Serial.println(F("'"));
  }
#endif
}
} // namespace

void setupCommunications()
{
  startBtSerial();
#if ENABLE_BLE
  startBle();
#else
  Serial.println(F("[BLE] deaktiviert (ENABLE_BLE=0)."));
#endif
}

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
      processInputChar(bufferBt, c);
    }
  }
#endif
}
