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

#if ENABLE_BLE_MESH_PROVISIONER
/**
 * @brief Whether a provisioner scan/add session is currently running - other
 * BLE scan users (presence_ble.cpp) must not call ble_gap_disc() while this
 * is true, since NimBLE only allows one active discovery procedure at a
 * time and the second caller gets silently refused (BLE_HS_EALREADY).
 */
bool meshProvisioningSessionIsActive();

/**
 * @brief One-time (per lamp) network identity setup - this lamp becomes
 * able to provision others. netKey/appKey are 16 bytes each; pass nullptr
 * for either to generate a fresh random one (the first lamp in a new mesh)
 * instead of joining an existing one (every subsequent lamp that should
 * also be able to provision, using the hex values the first lamp's own
 * bootstrap printed - see the command's own feedback). Persists the keys
 * and re-applies them automatically on every boot afterwards. Safe to call
 * again later with the same values (idempotent); calling it with different
 * values replaces the stored identity.
 */
void meshNetworkCreate(const uint8_t *netKey, const uint8_t *appKey);

/// Whether meshNetworkCreate() has run (persisted) on this lamp yet.
bool meshHasNetworkIdentity();

/// Enables provisioner scanning for the given duration (default handled by
/// the caller) - refuses (with feedback) if no network identity yet, or if
/// NimBLE's single discovery slot is already busy.
void meshProvisionScanStart(uint32_t seconds);

void meshProvisionScanStop();

/// Call every loop() iteration - auto-stops the scan session once its
/// duration elapses.
void meshProvisionTick();

/// Human-readable listing of discovered-but-not-yet-added devices, one per
/// line, numbered for meshProvisionAdd()'s index argument.
String meshProvisionListText();

/// Provisions discovered-list entry `index` (as listed by
/// meshProvisionListText(), 1-based) and, once provisioning and AppKey
/// Add/Model App Bind complete, optionally subscribes it to `group` (0 =
/// none). Returns false immediately if the index is out of range.
bool meshProvisionAdd(uint8_t index, uint16_t group);

/// Forgets a node by its mesh address (local bookkeeping only - the node
/// itself stays fully provisioned, this doesn't send it a Node Reset).
bool meshProvisionRemove(uint16_t addr);

/// Human-readable listing of this lamp's persisted known-nodes list.
String meshKnownNodesText();

// ---------- persistence.cpp's own load/save hooks ----------
// Raw storage + CSV (de)serialization, same split as gestures.h/gestures.cpp:
// mesh.cpp owns the data and format, persistence.cpp owns the actual NVS
// prefs.getBytes()/putBytes()/getString()/putString() calls.
extern uint8_t meshLocalNetKey[16];
extern uint8_t meshLocalAppKey[16];
extern bool meshHasLocalKeys;
String meshKnownNodesCsv();
void meshSetKnownNodesFromCsv(const String &csv);
#endif // ENABLE_BLE_MESH_PROVISIONER

#endif
