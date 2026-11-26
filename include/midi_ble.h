#pragma once

#include "lamp_config.h"

#if ENABLE_BLE && ENABLE_BLE_MIDI

// Minimal BLE-MIDI (receive only). Registers its own GATT service on the
// shared NimBLE host comms.cpp's startBle() already brought up - call after
// that service's own ble_gatts_add_svcs(), before ble_gatts_start().
void setupBleMidi();

#endif
