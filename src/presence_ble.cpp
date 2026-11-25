#include <Arduino.h>

#include "lamp_config.h"
#include "settings.h"
#include "comms.h"
#include "presence_ble.h"

#if ENABLE_BLE
#include "host/ble_gap.h"
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

// Replicates NimBLEAddress's display format (see bleAddrToString() in
// comms.cpp, duplicated here rather than shared via a header since both are
// two-line helpers and the alternative is a new internal header just for
// them) - val[] is little-endian on the wire, standard MAC notation
// displays it reversed.
static String presenceBleAddrToString(const ble_addr_t &a)
{
    char buf[18];
    snprintf(buf, sizeof(buf), "%02x:%02x:%02x:%02x:%02x:%02x",
             a.val[5], a.val[4], a.val[3], a.val[2], a.val[1], a.val[0]);
    return String(buf);
}
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
bool presenceBleAlwaysOverride = Settings::PRESENCE_BLE_HW_OVERRIDE_DEFAULT;
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

#if ENABLE_BLE
namespace
{
    SemaphoreHandle_t presenceScanDone = nullptr;
    bool presenceScanFound = false;

    int presenceScanEventCb(struct ble_gap_event *event, void *arg)
    {
        (void)arg;
        switch (event->type)
        {
        case BLE_GAP_EVENT_DISC:
        {
            String addr = presenceBleAddrToString(event->disc.addr);
            int rssi = event->disc.rssi;
            if (presenceBleIsTarget(addr) && rssi >= presenceBleRssiThreshold)
                presenceScanFound = true;
            break;
        }
        case BLE_GAP_EVENT_DISC_COMPLETE:
            if (presenceScanDone)
                xSemaphoreGive(presenceScanDone);
            break;
        default:
            break;
        }
        return 0;
    }
} // namespace
#endif

// Active presence scan for target MAC (returns true if found). Blocks the
// caller for the scan duration - same synchronous call shape the previous
// NimBLE-Arduino getResults(SCAN_TIME_MS, false) blocking call had, now
// implemented as native async ble_gap_disc() + a semaphore the discovery
// callback signals on BLE_GAP_EVENT_DISC_COMPLETE.
bool presenceBleScanOnce()
{
#if ENABLE_BLE
    if (!presenceBleHasDevices())
        return false;
    if (!presenceScanDone)
        presenceScanDone = xSemaphoreCreateBinary();
    if (!presenceScanDone)
        return false;

    const uint32_t SCAN_TIME_MS = 3000;
    presenceScanFound = false;

    struct ble_gap_disc_params params = {};
    params.itvl = 320;
    params.window = 80;
    params.passive = 0; // active scan, matches the previous setActiveScan(true)
    params.filter_duplicates = 0;

    int rc = ble_gap_disc(BLE_OWN_ADDR_PUBLIC, SCAN_TIME_MS, &params, presenceScanEventCb, NULL);
    if (rc != 0)
    {
        sendFeedback(String(F("[PresenceBLE] Scan start failed rc=")) + String(rc));
        return false;
    }

    // Generous margin over SCAN_TIME_MS so a slow DISC_COMPLETE callback
    // can't desync this wait from the scan it's actually waiting on.
    xSemaphoreTake(presenceScanDone, pdMS_TO_TICKS(SCAN_TIME_MS + 1000));

    bool found = presenceScanFound;
    if (found)
    {
        lastPresenceBleSeenMs = millis();
        presenceBleDetected = true;
        presenceBlePrevConnected = true;
    }
    sendFeedback(String(F("[PresenceBLE] Scan targets=")) + presenceBleListCsv() + F(" -> ") +
                 (found ? F("found") : F("not found")) + F(" @rssi>=") + String(presenceBleRssiThreshold));
    return found;
#else
    return false;
#endif
}
