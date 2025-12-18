#pragma once

#include "Arduino.h"
#include "vector"

// Demo cycling
extern bool demoActive;
extern uint32_t demoDwellMs;
extern std::vector<uint8_t> demoList;
extern size_t demoIndex;
extern uint32_t demoLastSwitchMs;

/**
 * @brief Build and start a demo cycle through quick-enabled modes with fixed dwell.
 */
void startDemo(uint32_t dwellMs);

void stopDemo();