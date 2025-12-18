#pragma once

#include <Arduino.h>

#include "settings.h"

// Presence tracking
extern bool presenceEnabled;
extern uint32_t presenceGraceMs;
extern uint32_t presenceGraceDeadline;
extern bool presencePrevConnected;
extern bool presenceDetected;
extern String presenceAddr;
extern String lastBleAddr;
extern String lastBtAddr;
extern uint32_t lastPresenceSeenMs;
extern uint32_t lastPresenceScanMs;

// Active presence scan for target MAC (returns true if found)
bool presenceScanOnce();