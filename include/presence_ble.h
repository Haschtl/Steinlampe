#pragma once

#include <Arduino.h>
#include <vector>

#include "settings.h"

// PresenceBLE tracking (BLE phone-proximity based auto on/off)
extern bool presenceBleEnabled;
extern uint32_t presenceBleGraceMs;
extern uint32_t presenceBleGraceDeadline;
extern bool presenceBlePrevConnected;
extern bool presenceBleDetected;
extern String presenceBleAddr;
extern std::vector<String> presenceBleDevices;
extern int presenceBleRssiThreshold;
extern bool presenceBleAutoOn;
extern bool presenceBleAutoOff;
extern bool presenceBleAlwaysOverride;
extern bool presenceBleLastOffByPresence;
extern String lastBleAddr;
extern String lastBtAddr;
extern uint32_t lastPresenceBleSeenMs;
extern uint32_t lastPresenceBleScanMs;

// Active presence scan for target MAC (returns true if found)
bool presenceBleScanOnce();
String presenceBleListCsv();
bool presenceBleAddDevice(const String &addr);
bool presenceBleRemoveDevice(const String &addr);
void presenceBleClearDevices();
bool presenceBleHasDevices();
bool presenceBleIsTarget(const String &addr);
