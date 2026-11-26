"""
A failed BLE re-encryption against an *existing* bond (NimBLE encryption-change
status != 0 - observed both from a hard reset during dev testing and after a
real reflash on a user's phone) leaves that stale bond sitting in the store
untouched. Every later reconnect attempt then keeps retrying the same broken
key, fails the same way, forever - the lamp is stuck refusing that device
until someone manually clears bonds on both ends.

esp_ble_mesh's own gap_event_cb() (core/nimble_host/adapter.c) is what actually
owns BLE_GAP_EVENT_ENC_CHANGE for any connection that came in through mesh's
own advertising (unprovisioned PB-GATT / Proxy) - which, on a mesh-enabled
build, is every normal phone/app connection. It currently just logs the status
and does nothing else. comms.cpp's own (non-mesh) ENC_CHANGE handler already
deletes the stale bond and terminates on failure - this patch gives mesh's
handler the same behavior, so the *next* connection attempt starts clean
instead of looping on a bond that will never re-encrypt successfully.

Idempotent: safe to run on every build.
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

MARKER = "QUARZLAMPE_MESH_ENC_CHANGE_BOND_CLEANUP"

OLD_INCLUDE = '#include "host/ble_hs.h"\n'
NEW_INCLUDE = '#include "host/ble_hs.h"\n#include "host/ble_store.h"\n'

OLD_CASE = (
    "    case BLE_GAP_EVENT_ENC_CHANGE:\n"
    "        /* Encryption has been enabled or disabled for this connection. */\n"
    '        MODLOG_DFLT(INFO, "encryption change event; status=%d ",\n'
    "                    event->enc_change.status);\n"
    "        rc = ble_gap_conn_find(event->connect.conn_handle, &desc);\n"
    "        assert(rc == 0);\n"
    '        MODLOG_DFLT(INFO, "\\n");\n'
)
NEW_CASE = (
    f"    /* {MARKER} */\n"
    "    case BLE_GAP_EVENT_ENC_CHANGE:\n"
    "        /* Encryption has been enabled or disabled for this connection. */\n"
    '        MODLOG_DFLT(INFO, "encryption change event; status=%d ",\n'
    "                    event->enc_change.status);\n"
    "        rc = ble_gap_conn_find(event->connect.conn_handle, &desc);\n"
    "        assert(rc == 0);\n"
    "        if (event->enc_change.status != 0) {\n"
    "            ble_store_util_delete_peer(&desc.peer_id_addr);\n"
    "            ble_gap_terminate(event->connect.conn_handle, BLE_ERR_REM_USER_CONN_TERM);\n"
    "        }\n"
    '        MODLOG_DFLT(INFO, "\\n");\n'
)


def main():
    if not ADAPTER_C.exists():
        print(f"[mesh-enc-change patch] {ADAPTER_C} not found, skipping")
        return

    text = ADAPTER_C.read_text()
    if MARKER in text:
        return  # already patched

    if OLD_CASE not in text:
        print(f"[mesh-enc-change patch] case anchor not found in {ADAPTER_C}, aborting")
        return
    patched = text.replace(OLD_CASE, NEW_CASE, 1)

    if OLD_INCLUDE not in patched:
        print(f"[mesh-enc-change patch] include anchor not found in {ADAPTER_C}, aborting")
        return
    patched = patched.replace(OLD_INCLUDE, NEW_INCLUDE, 1)

    ADAPTER_C.write_text(patched)
    print(f"[mesh-enc-change patch] added stale-bond cleanup on ENC_CHANGE failure in {ADAPTER_C}")


main()
