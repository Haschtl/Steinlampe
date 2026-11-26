"""
comms.cpp's flushBleNotification() paces outgoing BLE INDICATE chunks by
waiting for each one's real confirmation (BLE_GAP_EVENT_NOTIFY_TX, firing a
second time with a nonzero status once the peer actually acks it - see
bleIndicatePending's own comment in comms.cpp for the full story and why a
bare retry-on-error loop wasn't enough). That event only reaches our own GAP
callback for a connection that came in through our own advertising. Once BLE
Mesh is active, every real phone connection instead comes in through esp_ble_
mesh's own advertising, so its own gap_event_cb() (core/nimble_host/
adapter.c) owns that connection's GAP events entirely - and had no case at
all for BLE_GAP_EVENT_NOTIFY_TX, so the confirmation this app's own BLE
notify/indicate pacing depends on never arrived for any real device. Found
live, after a confirmed-working desktop test still crawled to a near-stall
against an actual phone (every single indicate chunk falling back to the 3s
safety timeout instead of ever seeing a real ack) - the same "mesh owns GAP
events for its own connections" gap already hit once before for
BLE_GAP_EVENT_ENC_CHANGE (see patch_mesh_enc_change_bond_cleanup.py).

Adds a case that forwards the event into comms.cpp's own
quarzlampeBleNotifyTxEvent(), which does the actual attr_handle/indication/
status check.

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

MARKER = "QUARZLAMPE_MESH_NOTIFY_TX"

OLD_FORWARD_DECL = "static int gap_event_cb(struct ble_gap_event *event, void *arg)\n{\n    struct ble_gap_conn_desc desc;\n    int rc;\n"
NEW_FORWARD_DECL = (
    f"/* {MARKER} */\n"
    "extern void quarzlampeBleNotifyTxEvent(uint16_t attr_handle, int status, uint8_t indication);\n\n"
    "static int gap_event_cb(struct ble_gap_event *event, void *arg)\n{\n    struct ble_gap_conn_desc desc;\n    int rc;\n"
)

OLD_CASE = (
    "    case BLE_GAP_EVENT_MTU:\n"
    '        MODLOG_DFLT(INFO, "mtu update event; conn_handle=%d cid=%d mtu=%d\\n",\n'
    "                    event->mtu.conn_handle,\n"
    "                    event->mtu.channel_id,\n"
    "                    event->mtu.value);\n"
    "        return 0;\n"
)
NEW_CASE = (
    f"    /* {MARKER} */\n"
    "    case BLE_GAP_EVENT_NOTIFY_TX:\n"
    "        quarzlampeBleNotifyTxEvent(event->notify_tx.attr_handle, event->notify_tx.status,\n"
    "                                   event->notify_tx.indication);\n"
    "        return 0;\n\n"
    + OLD_CASE
)


def main():
    if not ADAPTER_C.exists():
        print(f"[mesh-notify-tx patch] {ADAPTER_C} not found, skipping")
        return

    text = ADAPTER_C.read_text()
    if MARKER in text:
        return  # already patched

    if OLD_FORWARD_DECL not in text:
        print(f"[mesh-notify-tx patch] forward-decl anchor not found in {ADAPTER_C}, aborting")
        return
    patched = text.replace(OLD_FORWARD_DECL, NEW_FORWARD_DECL, 1)

    if OLD_CASE not in patched:
        print(f"[mesh-notify-tx patch] case anchor not found in {ADAPTER_C}, aborting")
        return
    patched = patched.replace(OLD_CASE, NEW_CASE, 1)

    ADAPTER_C.write_text(patched)
    print(f"[mesh-notify-tx patch] added BLE_GAP_EVENT_NOTIFY_TX forwarding in {ADAPTER_C}")


main()
