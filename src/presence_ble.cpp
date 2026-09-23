#include <Arduino.h>

#include "lamp_config.h"
#include "settings.h"
#include "comms.h"
#include "presence_ble.h"

#if ENABLE_BLE
#include <BLEDevice.h>
#endif

// PresenceBLE tracking
bool presenceBleEnabled = Settings::PRESENCE_BLE_DEFAULT_ENABLED;
uint32_t presenceBleGraceMs = Settings::PRESENCE_BLE_GRACE_MS_DEFAULT;
uint32_t presenceBleGraceDeadline = 0;
bool presenceBlePrevConnected = false;
bool presenceBleDetected = false;
String presenceBleAddr;
std::vector<String> presenceBleDevices;
int presenceBleRssiThreshold = Settings::PRESENCE_BLE_RSSI_THRESHOLD_DEFAULT;
bool presenceBleAutoOn = Settings::PRESENCE_BLE_AUTO_ON_DEFAULT;
bool presenceBleAutoOff = Settings::PRESENCE_BLE_AUTO_OFF_DEFAULT;
bool presenceBleLastOffByPresence = false;
String lastBleAddr;
String lastBtAddr;
uint32_t lastPresenceBleSeenMs = 0;
uint32_t lastPresenceBleScanMs = 0;

String presenceBleListCsv()
{
    String csv;
    for (size_t i = 0; i < presenceBleDevices.size(); ++i)
    {
        if (presenceBleDevices[i].length() == 0)
            continue;
        if (csv.length() > 0)
            csv += ",";
        csv += presenceBleDevices[i];
    }
    return csv;
}

bool presenceBleHasDevices()
{
    return !presenceBleDevices.empty();
}

bool presenceBleAddDevice(const String &addr)
{
    if (addr.length() == 0)
        return false;
    for (const auto &a : presenceBleDevices)
    {
        if (a.equalsIgnoreCase(addr))
            return false;
    }
    if (presenceBleDevices.size() >= 8)
        presenceBleDevices.erase(presenceBleDevices.begin()); // drop oldest to keep list bounded
    presenceBleDevices.push_back(addr);
    presenceBleAddr = presenceBleDevices.back(); // keep legacy single addr in sync
    return true;
}

bool presenceBleRemoveDevice(const String &addr)
{
    bool removed = false;
    for (auto it = presenceBleDevices.begin(); it != presenceBleDevices.end();)
    {
        if (it->equalsIgnoreCase(addr))
        {
            it = presenceBleDevices.erase(it);
            removed = true;
        }
        else
        {
            ++it;
        }
    }
    if (presenceBleDevices.empty())
        presenceBleAddr = "";
    else
        presenceBleAddr = presenceBleDevices.back();
    return removed;
}

void presenceBleClearDevices()
{
    presenceBleDevices.clear();
    presenceBleAddr = "";
}

bool presenceBleIsTarget(const String &addr)
{
    for (const auto &a : presenceBleDevices)
    {
        if (a.equalsIgnoreCase(addr))
            return true;
    }
    return false;
}

// Active presence scan for target MAC (returns true if found)
bool presenceBleScanOnce()
{
#if ENABLE_BLE
    if (!presenceBleHasDevices())
        return false;
    BLEScan *scan = BLEDevice::getScan();
    if (!scan)
        return false;
    const uint32_t SCAN_TIME_S = 3;
    scan->setActiveScan(true);
    scan->setInterval(320);
    scan->setWindow(80);
    BLEScanResults res = scan->start(SCAN_TIME_S, false);
    bool found = false;
    for (int i = 0; i < res.getCount(); ++i)
    {
        BLEAdvertisedDevice d = res.getDevice(i);
        String addr = d.getAddress().toString().c_str();
        int rssi = d.getRSSI();
        if (presenceBleIsTarget(addr) && rssi >= presenceBleRssiThreshold)
        {
            found = true;
            lastPresenceBleSeenMs = millis();
            presenceBleDetected = true;
            presenceBlePrevConnected = true;
            break;
        }
    }
    sendFeedback(String(F("[PresenceBLE] Scan targets=")) + presenceBleListCsv() + F(" -> ") +
                 (found ? F("found") : F("not found")) + F(" @rssi>=") + String(presenceBleRssiThreshold));
    return found;
#else
    return false;
#endif
}
