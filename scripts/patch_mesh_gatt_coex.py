"""
BLE Mesh + our own NimBLE GATT services can't coexist out of the box on
classic ESP32: esp_ble_mesh's bt_mesh_gatt_init() unconditionally calls
ble_gatts_start() a second time (the host's own automatic start already
called it once for our services), which corrupts the attribute table
(ble_uuid_cmp/NULL ha_uuid crash on real hardware).

Espressif has a real fix for exactly this - bt_mesh_gatts_svcs_add() lets
the app queue mesh's GATT services and skip mesh's own ble_gatts_start()
call, deferring to the host's single automatic one instead (see their
ble_mesh_example_init.c). It's gated behind CONFIG_BLE_MESH_USE_BLE_50,
which requires SOC_BLE_50_SUPPORTED (ESP32-S3/C3/C6/H2) - classic ESP32
doesn't have it, even though none of the gated code actually needs BLE 5.0
hardware. This patch widens that one guard so our own
QUARZLAMPE_MESH_GATT_COEX build flag enables the same fix on classic ESP32.

Idempotent: safe to run on every build (platformio re-extracts the
framework package rarely, but this checks before writing either way).
"""

import pathlib

Import("env")  # noqa: F821 - PlatformIO injects this

FRAMEWORK_DIR = env.PioPlatform().get_package_dir("framework-espidf")  # noqa: F821
ADAPTER_C = (
    pathlib.Path(FRAMEWORK_DIR)
    / "components"
    / "bt"
    / "esp_ble_mesh"
    / "core"
    / "nimble_host"
    / "adapter.c"
)

# adapter.c has many "#if CONFIG_BLE_MESH_USE_BLE_50" guards (most gate real
# BLE-5.0-only extended-advertising code that doesn't compile without
# CONFIG_BT_NIMBLE_EXT_ADV) - only two specific ones matter here: the
# g_gatts_svcs_add flag's declaration, and the one directly in front of
# bt_mesh_gatts_svcs_add()'s definition (which also selects the no-op
# bt_mesh_gatt_init() that skips its own ble_gatts_start() call). Match each
# via unique surrounding context so no other guard is touched.
REPLACEMENTS = [
    (
        "#if CONFIG_BLE_MESH_USE_BLE_50\nstatic bool g_gatts_svcs_add = false;\n#endif\n",
        "#if CONFIG_BLE_MESH_USE_BLE_50 || QUARZLAMPE_MESH_GATT_COEX\nstatic bool g_gatts_svcs_add = false;\n#endif\n",
    ),
    (
        "#if CONFIG_BLE_MESH_USE_BLE_50\nvoid bt_mesh_gatts_svcs_add(void)\n",
        "#if CONFIG_BLE_MESH_USE_BLE_50 || QUARZLAMPE_MESH_GATT_COEX\nvoid bt_mesh_gatts_svcs_add(void)\n",
    ),
]


def main():
    if not ADAPTER_C.exists():
        print(f"[mesh-gatt-coex patch] {ADAPTER_C} not found, skipping")
        return

    text = ADAPTER_C.read_text()
    if "QUARZLAMPE_MESH_GATT_COEX" in text:
        return  # already patched

    patched = text
    applied = 0
    for old, new in REPLACEMENTS:
        if old not in patched:
            print(f"[mesh-gatt-coex patch] anchor not found in {ADAPTER_C}: {old!r}")
            continue
        patched = patched.replace(old, new, 1)
        applied += 1

    if applied != len(REPLACEMENTS):
        print(f"[mesh-gatt-coex patch] only {applied}/{len(REPLACEMENTS)} anchors matched, aborting")
        return

    ADAPTER_C.write_text(patched)
    print(f"[mesh-gatt-coex patch] widened {applied} CONFIG_BLE_MESH_USE_BLE_50 guard(s) in {ADAPTER_C}")


main()
