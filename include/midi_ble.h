#pragma once

#include "lamp_config.h"

#if ENABLE_BLE && ENABLE_BLE_MIDI
#include <NimBLEDevice.h>

// Minimal BLE-MIDI (receive only)
void setupBleMidi(NimBLEServer *server, NimBLEAdvertising *advertising);

#endif
