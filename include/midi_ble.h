#pragma once

#include "lamp_config.h"

#if ENABLE_BLE && ENABLE_BLE_MIDI
#include <BLEAdvertising.h>
#include <BLEServer.h>

// Minimal BLE-MIDI (receive only)
void setupBleMidi(BLEServer *server, BLEAdvertising *advertising);

#endif
