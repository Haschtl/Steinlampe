#pragma once

#include <Arduino.h>
#include <vector>

#include "settings.h"

// Presence tracking
extern bool presenceEnabled;
extern uint32_t presenceGraceMs;
extern uint32_t presenceGraceDeadline;
extern bool presencePrevConnected;
extern bool presenceDetected;
extern String presenceAddr;
extern std::vector<String> presenceDevices;
extern int presenceRssiThreshold;
extern bool presenceAutoOn;
extern bool presenceAutoOff;
extern bool presenceLastOffByPresence;
extern String lastBleAddr;
extern String lastBtAddr;
extern uint32_t lastPresenceSeenMs;
extern uint32_t lastPresenceScanMs;

// Active presence scan for target MAC (returns true if found)
bool presenceScanOnce();
String presenceListCsv();
bool presenceAddDevice(const String &addr);
bool presenceRemoveDevice(const String &addr);
void presenceClearDevices();
bool presenceHasDevices();
bool presenceIsTarget(const String &addr);
