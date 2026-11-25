#pragma once

#include <Arduino.h>

#include "lamp_config.h"

#if ENABLE_BLE_MESH

/**
 * @brief Bring up BLE Mesh (NLC Basic Lightness Controller profile) and
 * enable provisioning. Call once the NimBLE host has synced with the
 * controller (comms.cpp's bleOnSync()) - esp_ble_mesh_init() requires the
 * host's event loop to already be running.
 */
void meshSetup();

/**
 * @brief Send a Generic OnOff Set (unacknowledged) to a mesh address - group
 * or unicast, "all lamps" vs. "one lamp" is purely which address the caller
 * passes. Requires the node's Generic OnOff Client model to already have an
 * AppKey bound via nRF Mesh's Model App Bind - sends a feedback message and
 * does nothing otherwise.
 */
void meshSendOnOff(uint16_t addr, bool on);

/**
 * @brief Send a Light Lightness Set (unacknowledged) to a mesh address.
 * Same AppKey-bound precondition as meshSendOnOff().
 */
void meshSendLightness(uint16_t addr, float percent);

#endif
