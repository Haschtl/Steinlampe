"""
Counterpart to patch_mesh_notify_tx.py's forwarding, for the disconnect side.

authorizeBleConn() (comms.cpp) lazily registers a mesh-routed connection into
bleConnHandles/bleClientConnected the first time it does any GATT access -
that's what makes sendFeedback()/flushBleNotification() actually queue and
send anything for it at all (see that function's own comment for the full
story: without it, those were permanently believing no BLE client was ever
connected on a mesh-enabled build, so nothing was ever queued in the first
place - not a pacing or delivery bug, nothing to deliver). There's no
equivalent lazy hook for the connection going away, though, so without this
patch bleConnHandles would just accumulate stale handles forever and
bleClientConnected would never go back to false once set.

Depends on patch_mesh_notify_tx.py having already run in this same build
(anchors on the forward-declaration block it leaves behind) - must be listed
after it in platformio.ini's extra_scripts.

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

MARKER = "QUARZLAMPE_MESH_DISCONNECT"

OLD_FORWARD_DECL = "extern void quarzlampeBleNotifyTxEvent(uint16_t attr_handle, int status, uint8_t indication);\n\n"
NEW_FORWARD_DECL = (
    OLD_FORWARD_DECL
    + f"/* {MARKER} */\n"
    + "extern void quarzlampeBleDisconnectEvent(uint16_t conn_handle);\n\n"
)

OLD_CASE = (
    "    case BLE_GAP_EVENT_DISCONNECT:\n"
    '        MODLOG_DFLT(INFO, "disconnect; reason=%d ", event->disconnect.reason);\n'
    '        MODLOG_DFLT(INFO, "\\n");\n'
    "#if BLE_MESH_DEV\n"
)
NEW_CASE = (
    "    case BLE_GAP_EVENT_DISCONNECT:\n"
    f"        /* {MARKER} */\n"
    "        quarzlampeBleDisconnectEvent(event->disconnect.conn.conn_handle);\n"
    '        MODLOG_DFLT(INFO, "disconnect; reason=%d ", event->disconnect.reason);\n'
    '        MODLOG_DFLT(INFO, "\\n");\n'
    "#if BLE_MESH_DEV\n"
)


def main():
    if not ADAPTER_C.exists():
        print(f"[mesh-disconnect patch] {ADAPTER_C} not found, skipping")
        return

    text = ADAPTER_C.read_text()
    if MARKER in text:
        return  # already patched

    if OLD_FORWARD_DECL not in text:
        print(f"[mesh-disconnect patch] forward-decl anchor not found in {ADAPTER_C} (did patch_mesh_notify_tx.py run first?), aborting")
        return
    patched = text.replace(OLD_FORWARD_DECL, NEW_FORWARD_DECL, 1)

    if OLD_CASE not in patched:
        print(f"[mesh-disconnect patch] case anchor not found in {ADAPTER_C}, aborting")
        return
    patched = patched.replace(OLD_CASE, NEW_CASE, 1)

    ADAPTER_C.write_text(patched)
    print(f"[mesh-disconnect patch] added BLE_GAP_EVENT_DISCONNECT forwarding in {ADAPTER_C}")


main()
