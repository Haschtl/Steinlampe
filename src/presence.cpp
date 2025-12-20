#include <Arduino.h>

#include "lamp_config.h"
#include "settings.h"
#include "comms.h"
#include "presence.h"

#if ENABLE_BLE
#include <BLEDevice.h>
#endif

// Presence tracking
bool presenceEnabled = Settings::PRESENCE_DEFAULT_ENABLED;
uint32_t presenceGraceMs = Settings::PRESENCE_GRACE_MS_DEFAULT;
uint32_t presenceGraceDeadline = 0;
bool presencePrevConnected = false;
bool presenceDetected = false;
String presenceAddr;
std::vector<String> presenceDevices;
int presenceRssiThreshold = Settings::PRESENCE_RSSI_THRESHOLD_DEFAULT;
bool presenceAutoOn = Settings::PRESENCE_AUTO_ON_DEFAULT;
bool presenceAutoOff = Settings::PRESENCE_AUTO_OFF_DEFAULT;
bool presenceLastOffByPresence = false;
String lastBleAddr;
String lastBtAddr;
uint32_t lastPresenceSeenMs = 0;
uint32_t lastPresenceScanMs = 0;

String presenceListCsv()
{
    String csv;
    for (size_t i = 0; i < presenceDevices.size(); ++i)
    {
        if (presenceDevices[i].length() == 0)
            continue;
        if (csv.length() > 0)
            csv += ",";
        csv += presenceDevices[i];
    }
    return csv;
}

bool presenceHasDevices()
{
    return !presenceDevices.empty();
}

bool presenceAddDevice(const String &addr)
{
    if (addr.length() == 0)
        return false;
    for (const auto &a : presenceDevices)
    {
        if (a.equalsIgnoreCase(addr))
            return false;
    }
    if (presenceDevices.size() >= 8)
        presenceDevices.erase(presenceDevices.begin()); // drop oldest to keep list bounded
    presenceDevices.push_back(addr);
    presenceAddr = presenceDevices.back(); // keep legacy single addr in sync
    return true;
}

bool presenceRemoveDevice(const String &addr)
{
    bool removed = false;
    for (auto it = presenceDevices.begin(); it != presenceDevices.end();)
    {
        if (it->equalsIgnoreCase(addr))
        {
            it = presenceDevices.erase(it);
            removed = true;
        }
        else
        {
            ++it;
        }
    }
    if (presenceDevices.empty())
        presenceAddr = "";
    else
        presenceAddr = presenceDevices.back();
    return removed;
}

void presenceClearDevices()
{
    presenceDevices.clear();
    presenceAddr = "";
}

bool presenceIsTarget(const String &addr)
{
    for (const auto &a : presenceDevices)
    {
        if (a.equalsIgnoreCase(addr))
            return true;
    }
    return false;
}

// Active presence scan for target MAC (returns true if found)
bool presenceScanOnce()
{
#if ENABLE_BLE
    if (!presenceHasDevices())
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
        if (presenceIsTarget(addr) && rssi >= presenceRssiThreshold)
        {
            found = true;
            lastPresenceSeenMs = millis();
            presenceDetected = true;
            presencePrevConnected = true;
            break;
        }
    }
    sendFeedback(String(F("[Presence] Scan targets=")) + presenceListCsv() + F(" -> ") +
                 (found ? F("found") : F("not found")) + F(" @rssi>=") + String(presenceRssiThreshold));
    return found;
#else
    return false;
#endif
}
