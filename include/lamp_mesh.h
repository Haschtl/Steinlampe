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

/**
 * @brief Pushes this lamp's current lampEnabled/masterBrightness into the
 * Generic OnOff Server's and Light Lightness Server's own state, publishing
 * a Status to whatever Publish Address a provisioner configured (if any).
 * Call after a poti/switch-driven local change - NOT from every
 * setLampEnabled()/setBrightnessPercent() call site, which would spam the
 * mesh with a Status on every pattern/ramp tick. Without this, those two
 * server models only ever reflect the last Set they received over the
 * mesh, never the lamp's real hardware-driven state - a provisioner's Get
 * reads stale data and no Status goes out even with Publish configured.
 */
void meshPublishLocalState();

/// "provisioner" (this lamp has its own network identity - see
/// meshHasNetworkIdentity()), "node" (this lamp has been provisioned by
/// something else - nRF Mesh, another lamp, any provisioner), or "none"
/// (neither has happened yet - a factory-fresh lamp). Always available (not
/// gated behind ENABLE_BLE_MESH_PROVISIONER) since a plain, non-provisioner
/// build can still become a "node". Exposed on STATUS| as mesh_role= for the
/// frontend's per-lamp role display - see memory: mesh role status UI.
String meshRoleText();

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

/// Listing of discovered-but-not-yet-added devices: a "[Mesh] Discovered
/// (<n>):" header line followed by exactly <n> "  DEV idx=.. uuid=.. rssi=.."
/// lines (idx is 1-based, for meshProvisionAdd()'s index argument) - this
/// exact shape (count header + fixed-field lines) is relied on by the
/// frontend's meshProtocol.ts to parse it off the shared log/notify stream.
String meshProvisionListText();

/// Provisions discovered-list entry `index` (as listed by
/// meshProvisionListText(), 1-based) and, once provisioning and AppKey
/// Add/Model App Bind complete, optionally subscribes it to `group` (0 =
/// none). Returns false immediately if the index is out of range.
bool meshProvisionAdd(uint8_t index, uint16_t group);

/// Forgets a node by its mesh address (local bookkeeping only - the node
/// itself stays fully provisioned, this doesn't send it a Node Reset).
bool meshProvisionRemove(uint16_t addr);

/// Sets (or clears, with an empty string) a known node's user-facing label.
/// Sanitizes out ';'/':'/newline (the CSV/log delimiters) before storing.
/// Returns false if `addr` isn't in the known-nodes list.
bool meshRenameNode(uint16_t addr, const String &name);

/// Listing of this lamp's persisted known nodes: a "[Mesh] Known nodes
/// (<n>):" header line followed by exactly <n> "  NODE addr=0x.. uuid=..
/// name=.." lines - same count-header shape as meshProvisionListText(), for
/// the same frontend-parsing reason.
String meshKnownNodesText();

/// Number of persisted known nodes - cheap enough for the periodic status
/// line (print.cpp's |mesh_nodes=), unlike the full meshKnownNodesText().
uint8_t meshKnownNodeCount();

/// This lamp's current NetKey/AppKey as "[Mesh] Keys: net=<32hex>
/// app=<32hex>" - re-displays what meshNetworkCreate() already printed once,
/// without having to re-run it (which would be a no-op given the same
/// values, but there's no reason to make the user remember them instead of
/// just asking again). Empty-ish placeholder text if meshHasLocalKeys is
/// false.
String meshKeysText();

/// Builds a Mesh Configuration Database ("nRF Mesh export") compatible JSON
/// document covering this lamp's own network identity (as netKeys[0]/
/// appKeys[0]) and every known node (as a nodes[] entry with its real
/// deviceKey/UUID/unicastAddress, re-read live from the mesh stack's own
/// provisioner node table - see esp_ble_mesh_provisioner_get_node_with_addr()
/// in mesh.cpp). Deliberately omits each model's "bind" array rather than
/// guessing - an importer then correctly treats every node as "not yet
/// configured" and does its own (idempotent) Model App Bind, rather than
/// this lamp claiming a binding state it hasn't verified. Requires
/// meshHasLocalKeys; returns an error feedback string otherwise.
String meshExportNrfJson();

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
