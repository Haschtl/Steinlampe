#include <Arduino.h>

#include "lamp_config.h"
#include "settings.h"
#include "comms.h"

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
String lastBleAddr;
String lastBtAddr;
uint32_t lastPresenceSeenMs = 0;
uint32_t lastPresenceScanMs = 0;

// Active presence scan for target MAC (returns true if found)
bool presenceScanOnce()
{
#if ENABLE_BLE
    if (presenceAddr.isEmpty())
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
        if (addr == presenceAddr)
        {
            found = true;
            break;
        }
    }
    sendFeedback(String(F("[Presence] Scan target=")) + presenceAddr + F(" -> ") + (found ? F("found") : F("not found")));
    return found;
#else
    return false;
#endif
}
