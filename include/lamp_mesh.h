#pragma once

#include "lamp_config.h"

#if ENABLE_BLE_MESH

/**
 * @brief Bring up BLE Mesh (NLC Basic Lightness Controller profile) and
 * enable provisioning. Call once the NimBLE host has synced with the
 * controller (comms.cpp's bleOnSync()) - esp_ble_mesh_init() requires the
 * host's event loop to already be running.
 */
void meshSetup();

#endif
