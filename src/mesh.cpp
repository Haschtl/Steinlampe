// BLE Mesh integration (feat/ble-mesh Step 3): Bluetooth NLC Basic
// Lightness Controller (BLC) profile. Builds directly on the proven
// provisioning/Config Server/Health Server/Generic OnOff Server/Vendor
// Model pattern from spikes/ble-mesh/src/main.cpp, extended with the two
// models the NLC BLC profile actually mandates beyond that: Light Lightness
// Server (+ its required Setup Server) and Light LC Server (+ its Setup
// Server) - the real occupancy/ambient-light-reactive state machine. The
// Mesh component owns the state bindings between these (Generic OnOff <->
// Light Lightness <-> Light LC) internally per spec - this file only reacts
// to the fully-resolved state_change events and reflects them onto the real
// lamp, it never needs to keep the mesh-side states in sync with each other
// itself.
#include "lamp_config.h"

#if ENABLE_BLE_MESH

#include "lamp_mesh.h"
#include "comms.h"
#include "lamp_state.h"
#include "print.h"
#include "persistence.h"
#include <esp_mac.h>
#include <esp_random.h>

extern "C"
{
#include "esp_ble_mesh_common_api.h"
#include "esp_ble_mesh_config_model_api.h"
#include "esp_ble_mesh_health_model_api.h"
#include "esp_ble_mesh_defs.h"
#include "esp_ble_mesh_generic_model_api.h"
#include "esp_ble_mesh_lighting_model_api.h"
#include "esp_ble_mesh_networking_api.h"
#include "esp_ble_mesh_provisioning_api.h"
#if ENABLE_BLE_MESH_PROVISIONER
#include "esp_ble_mesh_local_data_operation_api.h" // esp_ble_mesh_get_primary_element_address()
#include "host/ble_gap.h"                          // ble_gap_disc_active()
#endif
}

// Provided by main.cpp/command.cpp's shared dispatcher - same function
// every other transport (USB/BT-Serial/BLE custom protocol/BLE-MIDI) feeds
// text commands into.
void handleCommand(const String &line);

#if ENABLE_BLE_MESH_PROVISIONER
// This lamp's own network identity once it can provision others - external
// linkage so persistence.cpp can load/save them directly (same pattern as
// quickMask/gestureBindings elsewhere in this codebase). Re-applied to
// esp_ble_mesh on every boot by meshApplyLocalIdentity() if meshHasLocalKeys
// is true (loaded from NVS before meshSetup() runs).
uint8_t meshLocalNetKey[16] = {0};
uint8_t meshLocalAppKey[16] = {0};
bool meshHasLocalKeys = false;
#endif

namespace
{
constexpr const char *TAG = "mesh";

// Real per-device identity instead of the spike's hardcoded test array -
// otherwise every lamp running this firmware would advertise the exact same
// UUID and a provisioner couldn't tell them apart. Byte 0 is a fixed marker
// (purely so a packet capture/log is recognizable as ours at a glance);
// bytes 1-6 are this chip's own base MAC, which is already guaranteed
// unique per device (same source main.cpp's ensureBaseMac() relies on); the
// rest stay zero.
uint8_t devUuid[16] = {0};

esp_ble_mesh_prov_t provision = {
    .uuid = devUuid,
    .output_size = 0,
    .output_actions = 0,
    .input_size = 0,
    .input_actions = 0,
#if CONFIG_BLE_MESH_PROVISIONER
    // Required whenever the Provisioner role is compiled into the build
    // (CONFIG_BLE_MESH_PROVISIONER is set unconditionally in
    // sdkconfig.defaults, regardless of our own ENABLE_BLE_MESH_PROVISIONER
    // app flag) - this is the address/identity THIS device uses when IT
    // acts as a provisioner, not its identity as a node. prov_unicast_addr
    // here is a placeholder only: meshNetworkCreate() (mesh_provisioner.h)
    // overrides it at runtime via esp_ble_mesh_provisioner_set_primary_elem_addr()
    // once this lamp's real address (self-chosen for the first lamp, or its
    // own already-provisioned node address for every other lamp) is known -
    // it can't be known yet here, since esp_ble_mesh_init() runs before
    // this device has necessarily been provisioned as a node at all.
    .prov_uuid = devUuid,
    .prov_unicast_addr = 0x0001,
#endif
};

esp_ble_mesh_cfg_srv_t configServer = {
    .net_transmit = ESP_BLE_MESH_TRANSMIT(2, 20),
    .relay = ESP_BLE_MESH_RELAY_DISABLED,
    .relay_retransmit = ESP_BLE_MESH_TRANSMIT(2, 20),
    .beacon = ESP_BLE_MESH_BEACON_ENABLED,
    // Without this, the node stops being reachable via Proxy ~60s after
    // provisioning (its Node Identity advertising window expires and it
    // has no subnet to keep advertising on) - confirmed on real hardware
    // during the spike.
    .gatt_proxy = ESP_BLE_MESH_GATT_PROXY_ENABLED,
    .default_ttl = 7,
};

// Health Server is mandatory on the primary element per the Mesh spec
// (every node needs one) - and needs at least one self-test ID or model
// init fails outright for every model on the node, not just this one
// (confirmed on real hardware during the spike: "Model init failed (err
// -22)" cascading across the whole composition). One dummy ID is enough to
// be spec-valid without actually implementing a real self-test.
constexpr uint8_t healthTestIds[] = {0x00};
// Borrowed Espressif company ID (0x02E5) - see VENDOR_COMPANY_ID below and
// composition.cid further down, same reasoning: a real product needs its
// own assigned Bluetooth SIG company ID, not required to get this far.
constexpr uint16_t COMPANY_ID = 0x02E5;
esp_ble_mesh_health_srv_t healthServer = {
    .health_test = {
        .id_count = 1,
        .test_ids = healthTestIds,
        .company_id = COMPANY_ID,
    },
};
ESP_BLE_MESH_MODEL_PUB_DEFINE(healthPub, 2 + 1, ROLE_NODE);

// ---------- Generic OnOff Server ----------
ESP_BLE_MESH_MODEL_PUB_DEFINE(onoffPub, 2 + 1, ROLE_NODE);
esp_ble_mesh_gen_onoff_srv_t onoffServer = {
    .rsp_ctrl = {
        .get_auto_rsp = ESP_BLE_MESH_SERVER_AUTO_RSP,
        .set_auto_rsp = ESP_BLE_MESH_SERVER_AUTO_RSP,
    },
};

// ---------- Light Lightness Server/Setup Server ----------
// Both models are required together on the same element per spec (Setup
// Server extends Server) - ESP_BLE_MESH_MODEL_LIGHT_LIGHTNESS_SRV and
// ESP_BLE_MESH_MODEL_LIGHT_LIGHTNESS_SETUP_SRV below, sharing this one
// state struct.
esp_ble_mesh_light_lightness_state_t lightnessState = {
    .lightness_actual = 0,
    .lightness_default = 0,
    .lightness_range_min = 1,
    .lightness_range_max = 65535,
};
ESP_BLE_MESH_MODEL_PUB_DEFINE(lightnessPub, 2 + 3, ROLE_NODE);
esp_ble_mesh_light_lightness_srv_t lightnessServer = {
    .rsp_ctrl = {
        .get_auto_rsp = ESP_BLE_MESH_SERVER_AUTO_RSP,
        .set_auto_rsp = ESP_BLE_MESH_SERVER_AUTO_RSP,
    },
    .state = &lightnessState,
};
ESP_BLE_MESH_MODEL_PUB_DEFINE(lightnessSetupPub, 2 + 3, ROLE_NODE);
esp_ble_mesh_light_lightness_setup_srv_t lightnessSetupServer = {
    .rsp_ctrl = {
        .get_auto_rsp = ESP_BLE_MESH_SERVER_AUTO_RSP,
        .set_auto_rsp = ESP_BLE_MESH_SERVER_AUTO_RSP,
    },
    .state = &lightnessState,
};

// ---------- Light LC Server/Setup Server ----------
// The actual NLC occupancy/ambient-light-reactive state machine - fully
// implemented inside the esp_ble_mesh component (PI regulator, fade
// timing, the whole Light LC state machine), not a stub we have to drive
// ourselves. We only supply the Property defaults below (NLC's own
// recommended starting point - a real install would tune these per
// fixture/room) and react to the resolved on/off output.
esp_ble_mesh_light_control_t lightControl = {
    .prop_state = {
        .time_occupancy_delay = 0,
        .time_fade_on = 0,
        .time_run_on = 5000,
        .time_fade = 0,
        .time_prolong = 5000,
        .time_fade_standby_auto = 0,
        .time_fade_standby_manual = 0,
        .lightness_on = 65535,
        .lightness_prolong = 16384,
        .lightness_standby = 0,
        .ambient_luxlevel_on = 0,
        .ambient_luxlevel_prolong = 0,
        .ambient_luxlevel_standby = 0,
        .regulator_kiu = 250.0f,
        .regulator_kid = 25.0f,
        .regulator_kpu = 80.0f,
        .regulator_kpd = 80.0f,
        .regulator_accuracy = 2,
        .set_occupancy_to_1_delay = 0,
    },
};
ESP_BLE_MESH_MODEL_PUB_DEFINE(lcPub, 2 + 9, ROLE_NODE);
esp_ble_mesh_light_lc_srv_t lcServer = {
    .rsp_ctrl = {
        .get_auto_rsp = ESP_BLE_MESH_SERVER_AUTO_RSP,
        .set_auto_rsp = ESP_BLE_MESH_SERVER_AUTO_RSP,
    },
    .lc = &lightControl,
};
ESP_BLE_MESH_MODEL_PUB_DEFINE(lcSetupPub, 2 + 9, ROLE_NODE);
esp_ble_mesh_light_lc_setup_srv_t lcSetupServer = {
    .rsp_ctrl = {
        .get_auto_rsp = ESP_BLE_MESH_SERVER_AUTO_RSP,
        .set_auto_rsp = ESP_BLE_MESH_SERVER_AUTO_RSP,
    },
    .lc = &lightControl,
};

// ---------- Generic OnOff Client / Light Lightness Client ----------
// Lets this node send standard Set messages to OTHER nodes (any address the
// caller chooses - group or unicast, there's no device/group picker in this
// firmware, addresses are configured by hand via nRF Mesh) - the mesh-action
// side of the generalized input-gesture mapping (gestures.cpp). Different
// model IDs than the Server models above (0x1001/0x1301 vs. 0x1000/0x1300),
// so both coexist fine on the same element. Each needs its own AppKey bound
// once via nRF Mesh's Model App Bind before meshSendOnOff()/meshSendLightness()
// below can actually send anything - see their own guard checks.
esp_ble_mesh_client_t onoffClient{};
ESP_BLE_MESH_MODEL_PUB_DEFINE(onoffCliPub, 2 + 3, ROLE_NODE);
esp_ble_mesh_client_t lightnessClient{};
ESP_BLE_MESH_MODEL_PUB_DEFINE(lightnessCliPub, 2 + 3, ROLE_NODE);

#if ENABLE_BLE_MESH_PROVISIONER
// ---------- Config Client ----------
// Lets this node SEND Config messages (AppKey Add/Model App Bind/
// Subscription Add) to other nodes it provisions itself - required
// alongside the Provisioner role (self-provisioning plan), confirmed from
// Espressif's own fast_prov_client example: Config Server alone (already
// present above for the Node role) only lets this device be *configured*,
// not configure others. No pub context - Config Client never publishes.
esp_ble_mesh_client_t configClient{};
#endif

uint16_t lightnessFromPercent(float pct)
{
    return (uint16_t)constrain(pct * 65535.0f / 100.0f, 0.0f, 65535.0f);
}

float percentFromLightness(uint16_t lightness)
{
    return (lightness * 100.0f) / 65535.0f;
}

esp_ble_mesh_model_t rootModels[] = {
    ESP_BLE_MESH_MODEL_CFG_SRV(&configServer),
    ESP_BLE_MESH_MODEL_HEALTH_SRV(&healthServer, &healthPub),
    ESP_BLE_MESH_MODEL_GEN_ONOFF_SRV(&onoffPub, &onoffServer),
    ESP_BLE_MESH_MODEL_LIGHT_LIGHTNESS_SRV(&lightnessPub, &lightnessServer),
    ESP_BLE_MESH_MODEL_LIGHT_LIGHTNESS_SETUP_SRV(&lightnessSetupPub, &lightnessSetupServer),
    ESP_BLE_MESH_MODEL_LIGHT_LC_SRV(&lcPub, &lcServer),
    ESP_BLE_MESH_MODEL_LIGHT_LC_SETUP_SRV(&lcSetupPub, &lcSetupServer),
    ESP_BLE_MESH_MODEL_GEN_ONOFF_CLI(&onoffCliPub, &onoffClient),
    ESP_BLE_MESH_MODEL_LIGHT_LIGHTNESS_CLI(&lightnessCliPub, &lightnessClient),
#if ENABLE_BLE_MESH_PROVISIONER
    ESP_BLE_MESH_MODEL_CFG_CLI(&configClient),
#endif
};

// Vendor Model: one receive-only opcode carrying a raw text command, same
// syntax as every other transport in comms.cpp (USB/BT-Serial/BLE
// command characteristic/BLE-MIDI). Lets one lamp's switch/poti input - via
// a Mesh group publish, configured once per lamp through a provisioner app
// - drive other lamps using our own command language, not just generic
// on/off. No publish needed (receive-only), so nullptr for pub/user_data.
constexpr uint32_t VENDOR_OP_CMD = ESP_BLE_MESH_MODEL_OP_3(0x01, COMPANY_ID);

esp_ble_mesh_model_op_t vendorOps[] = {
    ESP_BLE_MESH_MODEL_OP(VENDOR_OP_CMD, 0),
    ESP_BLE_MESH_MODEL_OP_END,
};

esp_ble_mesh_model_t vendorModels[] = {
    ESP_BLE_MESH_VENDOR_MODEL(COMPANY_ID, 0x0001, vendorOps, NULL, NULL),
};

esp_ble_mesh_elem_t elements[] = {
    ESP_BLE_MESH_ELEMENT(0, rootModels, vendorModels),
};

esp_ble_mesh_comp_t composition = {
    .cid = COMPANY_ID,
    .element_count = 1,
    .elements = elements,
};

#if ENABLE_BLE_MESH_PROVISIONER
// Defined further down (with the rest of the self-provisioning logic) -
// forward-declared here since provisioningCallback() needs them and
// appears earlier in the file.
void recordDiscovered(const uint8_t uuid[16], const esp_ble_mesh_bd_addr_t addr,
                       esp_ble_mesh_addr_type_t addrType, int8_t rssi);
void beginNodeConfig(uint16_t targetAddr);
#endif

void provisioningCallback(esp_ble_mesh_prov_cb_event_t event, esp_ble_mesh_prov_cb_param_t *param)
{
    switch (event)
    {
    case ESP_BLE_MESH_PROV_REGISTER_COMP_EVT:
        ESP_LOGI(TAG, "mesh stack initialized, err=%d", param->prov_register_comp.err_code);
        break;
    case ESP_BLE_MESH_NODE_PROV_ENABLE_COMP_EVT:
        ESP_LOGI(TAG, "provisioning enabled, err=%d", param->node_prov_enable_comp.err_code);
        break;
    case ESP_BLE_MESH_NODE_PROV_COMPLETE_EVT:
        ESP_LOGI(TAG, "provisioning complete - node is now part of a mesh");
        sendFeedback(F("[Mesh] Provisioned"));
        break;
    case ESP_BLE_MESH_NODE_PROV_RESET_EVT:
        ESP_LOGI(TAG, "node reset - unprovisioned again");
        sendFeedback(F("[Mesh] Reset to unprovisioned"));
        break;
#if ENABLE_BLE_MESH_PROVISIONER
    case ESP_BLE_MESH_PROVISIONER_PROV_ENABLE_COMP_EVT:
        ESP_LOGI(TAG, "provisioner enable, err=%d", param->provisioner_prov_enable_comp.err_code);
        break;
    case ESP_BLE_MESH_PROVISIONER_PROV_DISABLE_COMP_EVT:
        ESP_LOGI(TAG, "provisioner disable, err=%d", param->provisioner_prov_disable_comp.err_code);
        break;
    case ESP_BLE_MESH_PROVISIONER_RECV_UNPROV_ADV_PKT_EVT:
        recordDiscovered(param->provisioner_recv_unprov_adv_pkt.dev_uuid,
                          param->provisioner_recv_unprov_adv_pkt.addr,
                          param->provisioner_recv_unprov_adv_pkt.addr_type,
                          param->provisioner_recv_unprov_adv_pkt.rssi);
        break;
    case ESP_BLE_MESH_PROVISIONER_PROV_COMPLETE_EVT:
        ESP_LOGI(TAG, "provisioner: node 0x%04x joined", param->provisioner_prov_complete.unicast_addr);
        beginNodeConfig(param->provisioner_prov_complete.unicast_addr);
        break;
    case ESP_BLE_MESH_PROVISIONER_ADD_LOCAL_APP_KEY_COMP_EVT:
        if (param->provisioner_add_app_key_comp.err_code == 0)
        {
            uint16_t myAddr = esp_ble_mesh_get_primary_element_address();
            esp_ble_mesh_provisioner_bind_app_key_to_local_model(
                myAddr, 0, ESP_BLE_MESH_MODEL_ID_GEN_ONOFF_SRV, ESP_BLE_MESH_CID_NVAL);
            esp_ble_mesh_provisioner_bind_app_key_to_local_model(
                myAddr, 0, ESP_BLE_MESH_MODEL_ID_LIGHT_LIGHTNESS_SRV, ESP_BLE_MESH_CID_NVAL);
            esp_ble_mesh_provisioner_bind_app_key_to_local_model(
                myAddr, 0, ESP_BLE_MESH_MODEL_ID_GEN_ONOFF_CLI, ESP_BLE_MESH_CID_NVAL);
            esp_ble_mesh_provisioner_bind_app_key_to_local_model(
                myAddr, 0, ESP_BLE_MESH_MODEL_ID_LIGHT_LIGHTNESS_CLI, ESP_BLE_MESH_CID_NVAL);
            sendFeedback(F("[Mesh] Network identity ready"));
        }
        else
        {
            sendFeedback(String(F("[Mesh] Add local AppKey failed: ")) +
                         String(param->provisioner_add_app_key_comp.err_code));
        }
        break;
#endif
    default:
        ESP_LOGI(TAG, "prov event %d", event);
        break;
    }
}

// Composition Data Get/Status is a pure GET - the Config Server model
// answers it internally without ever surfacing an app-level event, so this
// can't confirm whether it arrived or what was sent back. Only state-
// changing config ops (AppKey Add, Model App Bind, Subscription Add, ...)
// reach here at all, discriminated via ctx.recv_op - added purely as a
// real-hardware diagnostic for the "nRF Mesh shows no models / can't add
// an app key" report, to see whether these messages even reach the node.
void configServerCallback(esp_ble_mesh_cfg_server_cb_event_t event, esp_ble_mesh_cfg_server_cb_param_t *param)
{
    if (event != ESP_BLE_MESH_CFG_SERVER_STATE_CHANGE_EVT)
        return;
    ESP_LOGI(TAG, "config server state change, recv_op=0x%04x, src=0x%04x",
             param->ctx.recv_op, param->ctx.addr);
    if (param->ctx.recv_op == ESP_BLE_MESH_MODEL_OP_APP_KEY_ADD)
    {
        ESP_LOGI(TAG, "AppKey Add: net_idx=0x%04x app_idx=0x%04x",
                 param->value.state_change.appkey_add.net_idx,
                 param->value.state_change.appkey_add.app_idx);
    }
}

void genericServerCallback(esp_ble_mesh_generic_server_cb_event_t event, esp_ble_mesh_generic_server_cb_param_t *param)
{
    if (event != ESP_BLE_MESH_GENERIC_SERVER_STATE_CHANGE_EVT)
        return;
    if (param->ctx.recv_op == ESP_BLE_MESH_MODEL_OP_GEN_ONOFF_SET ||
        param->ctx.recv_op == ESP_BLE_MESH_MODEL_OP_GEN_ONOFF_SET_UNACK)
    {
        bool on = param->value.state_change.onoff_set.onoff != 0;
        setLampEnabled(on, "mesh onoff", false);
        sendFeedback(String(F("[Mesh] Generic OnOff -> ")) + (on ? F("ON") : F("OFF")));
    }
}

void lightingServerCallback(esp_ble_mesh_lighting_server_cb_event_t event, esp_ble_mesh_lighting_server_cb_param_t *param)
{
    if (event != ESP_BLE_MESH_LIGHTING_SERVER_STATE_CHANGE_EVT)
        return;
    if (param->ctx.recv_op == ESP_BLE_MESH_MODEL_OP_LIGHT_LIGHTNESS_SET ||
        param->ctx.recv_op == ESP_BLE_MESH_MODEL_OP_LIGHT_LIGHTNESS_SET_UNACK)
    {
        // Actual state (0..65535) is the mesh-wide standard range for Light
        // Lightness - map directly onto our own 0..100% brightness scale.
        uint16_t lightness = param->value.state_change.lightness_set.lightness;
        float pct = percentFromLightness(lightness);
        // The Mesh Model spec binds Light Lightness Actual <-> Generic
        // OnOff (lightness > 0 implies OnOff = on, 0 implies off);
        // esp_ble_mesh is supposed to re-derive and fire a matching Generic
        // OnOff state_change for genericServerCallback() to pick up, but
        // that wasn't observed reaching the real lamp here - confirmed live
        // via Home Assistant's SIG Mesh plugin: turning off (lightness ->
        // 0, which also visually dims to black on its own) then back on
        // (lightness -> nonzero only, no separate Generic OnOff Set seen)
        // left the lamp dark, because setBrightnessPercent() alone can't
        // un-gate main.cpp's `lampEnabled ? ... : 0.0f` output gate. Enforce
        // the binding explicitly instead of depending on the stack for it.
        bool shouldBeOn = lightness > 0;
        if (shouldBeOn != lampEnabled)
            setLampEnabled(shouldBeOn, "mesh lightness", shouldBeOn);
        setBrightnessPercent(pct, false, false, false);
        sendFeedback(String(F("[Mesh] Lightness -> ")) + fmtF(pct, 1) + F("%"));
    }
    else if (param->ctx.recv_op == ESP_BLE_MESH_MODEL_OP_LIGHT_LC_LIGHT_ONOFF_SET ||
             param->ctx.recv_op == ESP_BLE_MESH_MODEL_OP_LIGHT_LC_LIGHT_ONOFF_SET_UNACK)
    {
        // The Light LC state machine's own resolved on/off output (occupancy
        // timeout, fade-to-standby, etc. already applied internally) -
        // distinct from a direct Generic OnOff Set.
        bool on = param->value.state_change.lc_light_onoff_set.onoff != 0;
        setLampEnabled(on, "mesh lc", false);
        sendFeedback(String(F("[Mesh] LC OnOff -> ")) + (on ? F("ON") : F("OFF")));
    }
}

// meshSendOnOff()/meshSendLightness() send the _UNACK opcode variants
// (fire-and-forget, no response expected) - these callbacks only exist for
// diagnostics (e.g. a bad AppKey/address shows up as a timeout here) and
// future ack-based sends, not required for the UNACK sends to work today.
void genericClientCallback(esp_ble_mesh_generic_client_cb_event_t event, esp_ble_mesh_generic_client_cb_param_t *param)
{
    if (event == ESP_BLE_MESH_GENERIC_CLIENT_TIMEOUT_EVT)
        ESP_LOGW(TAG, "generic client timeout, opcode=0x%04x", param->params->opcode);
}

void lightingClientCallback(esp_ble_mesh_light_client_cb_event_t event, esp_ble_mesh_light_client_cb_param_t *param)
{
    if (event == ESP_BLE_MESH_LIGHT_CLIENT_TIMEOUT_EVT)
        ESP_LOGW(TAG, "lighting client timeout, opcode=0x%04x", param->params->opcode);
}

// Vendor models don't get their own typed callback like the Generic/Lighting
// Server models do - esp_ble_mesh_register_custom_model_callback() is the
// one entry point for every vendor model's traffic, dispatched by opcode.
void customModelCallback(esp_ble_mesh_model_cb_event_t event, esp_ble_mesh_model_cb_param_t *param)
{
    if (event != ESP_BLE_MESH_MODEL_OPERATION_EVT)
        return;
    if (param->model_operation.opcode != VENDOR_OP_CMD)
        return;
    String cmd;
    cmd.reserve(param->model_operation.length);
    for (uint16_t i = 0; i < param->model_operation.length; ++i)
        cmd += (char)param->model_operation.msg[i];
    handleCommand(cmd);
}

#if ENABLE_BLE_MESH_PROVISIONER
// ---------- Self-provisioning (Provisioner role) ----------
// This lamp provisioning another one, start to finish, start to finish:
// mesh provision scan -> RECV_UNPROV_ADV_PKT_EVT (recordDiscovered()) ->
// mesh provision add <n> -> PROVISIONER_PROV_COMPLETE_EVT
// (beginNodeConfig()) -> Config AppKey Add -> Model App Bind (OnOff Server)
// -> Model App Bind (Lightness Server) -> optional Subscription Add ->
// addKnownNode(). Exactly one node configuration in flight at a time
// (provStep != PROV_STEP_IDLE guards this), matching the one-active-
// provisioning-session design that also protects the single NimBLE scan
// slot (see meshProvisioningSessionIsActive()/presence_ble.cpp).

struct DiscoveredDev
{
    uint8_t uuid[16];
    esp_ble_mesh_bd_addr_t addr;
    esp_ble_mesh_addr_type_t addrType;
    int8_t rssi;
};
constexpr uint8_t MAX_DISCOVERED = 8;
DiscoveredDev discovered[MAX_DISCOVERED];
uint8_t discoveredCount = 0;

void recordDiscovered(const uint8_t uuid[16], const esp_ble_mesh_bd_addr_t addr,
                       esp_ble_mesh_addr_type_t addrType, int8_t rssi)
{
    for (uint8_t i = 0; i < discoveredCount; ++i)
        if (memcmp(discovered[i].uuid, uuid, 16) == 0)
        {
            discovered[i].rssi = rssi; // refresh RSSI on repeat sightings
            return;
        }
    if (discoveredCount >= MAX_DISCOVERED)
        return;
    DiscoveredDev &d = discovered[discoveredCount++];
    memcpy(d.uuid, uuid, 16);
    memcpy(d.addr, addr, sizeof(d.addr));
    d.addrType = addrType;
    d.rssi = rssi;
}

enum ProvStep : uint8_t
{
    PROV_STEP_IDLE = 0,
    PROV_STEP_APPKEY_ADD,
    PROV_STEP_BIND_ONOFF,
    PROV_STEP_BIND_LIGHTNESS,
    PROV_STEP_SUB_ADD,
};
ProvStep provStep = PROV_STEP_IDLE;
uint16_t provTargetAddr = 0;
uint16_t provTargetGroup = 0;
uint8_t provTargetUuid[16] = {0};

bool scanSessionActive = false;
uint32_t scanSessionDueMs = 0;

constexpr uint8_t MAX_KNOWN_NODES = 16;
constexpr uint8_t MAX_NODE_NAME_LEN = 16;
struct KnownNode
{
    uint16_t addr;
    uint32_t uuidShort; // first 4 bytes of the device UUID, display only
    // User-facing label (frontend's device list) - empty until the user
    // renames it via `mesh rename`/the UI; falls back to the raw address
    // client-side. Aggregate-initializing a KnownNode with just {addr,
    // uuidShort} (as addKnownNode() does) zero-fills this per C++ rules, so
    // it's always a valid empty C-string by default.
    char name[MAX_NODE_NAME_LEN + 1];
};
KnownNode knownNodes[MAX_KNOWN_NODES];
uint8_t knownNodeCount = 0;

uint32_t uuidShortOf(const uint8_t uuid[16])
{
    return ((uint32_t)uuid[0] << 24) | ((uint32_t)uuid[1] << 16) | ((uint32_t)uuid[2] << 8) | uuid[3];
}

void addKnownNode(uint16_t addr, const uint8_t uuid[16])
{
    for (uint8_t i = 0; i < knownNodeCount; ++i)
        if (knownNodes[i].addr == addr)
            return;
    if (knownNodeCount >= MAX_KNOWN_NODES)
        return;
    knownNodes[knownNodeCount++] = {addr, uuidShortOf(uuid)};
}

bool removeKnownNodeByAddr(uint16_t addr)
{
    for (uint8_t i = 0; i < knownNodeCount; ++i)
    {
        if (knownNodes[i].addr == addr)
        {
            for (uint8_t j = i; j + 1 < knownNodeCount; ++j)
                knownNodes[j] = knownNodes[j + 1];
            knownNodeCount--;
            return true;
        }
    }
    return false;
}

bool renameKnownNodeByAddr(uint16_t addr, const String &rawName)
{
    // Strip the CSV/log delimiters a name could otherwise break (same
    // sanitize-at-the-boundary approach as gestureBindingsToCsv() rather
    // than escaping them later in every consumer).
    String name = rawName;
    name.replace(';', ' ');
    name.replace(':', ' ');
    name.replace('\n', ' ');
    name.trim();
    for (uint8_t i = 0; i < knownNodeCount; ++i)
    {
        if (knownNodes[i].addr != addr)
            continue;
        strncpy(knownNodes[i].name, name.c_str(), MAX_NODE_NAME_LEN);
        knownNodes[i].name[MAX_NODE_NAME_LEN] = '\0';
        return true;
    }
    return false;
}

void failProvisioning(const String &reason)
{
    sendFeedback(String(F("[Mesh] Provisioning 0x")) + String(provTargetAddr, HEX) + F(" failed: ") + reason);
    provStep = PROV_STEP_IDLE;
}

// Config messages (AppKey Add/Model App Bind/Subscription Add) are
// encrypted with the target's Device Key, not an AppKey - ctx.app_idx here
// is unused by the stack for Config opcodes specifically, but every
// official example (e.g. provisioner/main.c's example_ble_mesh_set_msg_common())
// fills it in anyway, so this mirrors that rather than leaving it
// meaningfully undefined.
void sendConfigMessage(uint32_t opcode, esp_ble_mesh_cfg_client_set_state_t &set)
{
    esp_ble_mesh_client_common_param_t params{};
    params.opcode = opcode;
    params.model = configClient.model;
    params.ctx.net_idx = 0;
    params.ctx.app_idx = 0;
    params.ctx.addr = provTargetAddr;
    params.ctx.send_ttl = ESP_BLE_MESH_TTL_DEFAULT;
    params.msg_timeout = 0;

    esp_err_t err = esp_ble_mesh_config_client_set_state(&params, &set);
    if (err != ESP_OK)
        failProvisioning(String(F("send rc=")) + String(err));
}

void beginNodeConfig(uint16_t targetAddr)
{
    provStep = PROV_STEP_APPKEY_ADD;
    provTargetAddr = targetAddr;

    esp_ble_mesh_cfg_client_set_state_t set{};
    set.app_key_add.net_idx = 0;
    set.app_key_add.app_idx = 0;
    memcpy(set.app_key_add.app_key, meshLocalAppKey, 16);
    sendConfigMessage(ESP_BLE_MESH_MODEL_OP_APP_KEY_ADD, set);
}

void bindModel(uint16_t modelId)
{
    esp_ble_mesh_cfg_client_set_state_t set{};
    set.model_app_bind.element_addr = provTargetAddr;
    set.model_app_bind.model_app_idx = 0;
    set.model_app_bind.model_id = modelId;
    set.model_app_bind.company_id = ESP_BLE_MESH_CID_NVAL; // not a vendor model
    sendConfigMessage(ESP_BLE_MESH_MODEL_OP_MODEL_APP_BIND, set);
}

void subscribeModel(uint16_t modelId, uint16_t group)
{
    esp_ble_mesh_cfg_client_set_state_t set{};
    set.model_sub_add.element_addr = provTargetAddr;
    set.model_sub_add.sub_addr = group;
    set.model_sub_add.model_id = modelId;
    set.model_sub_add.company_id = ESP_BLE_MESH_CID_NVAL;
    sendConfigMessage(ESP_BLE_MESH_MODEL_OP_MODEL_SUB_ADD, set);
}

// Advances the node-configuration state machine once the previous step's
// response/timeout arrives (configClientCallback() below calls this).
void onConfigStepResult(uint32_t opcode, bool success)
{
    if (!success)
    {
        failProvisioning(String(F("opcode 0x")) + String(opcode, HEX) + F(" no response"));
        return;
    }
    switch (provStep)
    {
    case PROV_STEP_APPKEY_ADD:
        provStep = PROV_STEP_BIND_ONOFF;
        bindModel(ESP_BLE_MESH_MODEL_ID_GEN_ONOFF_SRV);
        break;
    case PROV_STEP_BIND_ONOFF:
        provStep = PROV_STEP_BIND_LIGHTNESS;
        bindModel(ESP_BLE_MESH_MODEL_ID_LIGHT_LIGHTNESS_SRV);
        break;
    case PROV_STEP_BIND_LIGHTNESS:
        if (provTargetGroup != 0)
        {
            provStep = PROV_STEP_SUB_ADD;
            subscribeModel(ESP_BLE_MESH_MODEL_ID_GEN_ONOFF_SRV, provTargetGroup);
            break;
        }
        // fall through - no group requested, done
    case PROV_STEP_SUB_ADD:
        addKnownNode(provTargetAddr, provTargetUuid);
        sendFeedback(String(F("[Mesh] Provisioned + configured 0x")) + String(provTargetAddr, HEX));
        provStep = PROV_STEP_IDLE;
        break;
    default:
        break;
    }
}

void configClientCallback(esp_ble_mesh_cfg_client_cb_event_t event, esp_ble_mesh_cfg_client_cb_param_t *param)
{
    if (provStep == PROV_STEP_IDLE)
        return;
    uint32_t opcode = param->params->opcode;
    if (event == ESP_BLE_MESH_CFG_CLIENT_SET_STATE_EVT)
        onConfigStepResult(opcode, true);
    else if (event == ESP_BLE_MESH_CFG_CLIENT_TIMEOUT_EVT)
        onConfigStepResult(opcode, false);
}
#endif // ENABLE_BLE_MESH_PROVISIONER

} // namespace

void meshSetup()
{
    devUuid[0] = 0xDD;
    esp_efuse_mac_get_default(devUuid + 1);

    esp_err_t err = esp_ble_mesh_init(&provision, &composition);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "esp_ble_mesh_init failed: %d", err);
        return;
    }
    // esp_ble_mesh_init() -> bt_mesh_proxy_server_init() unconditionally
    // overwrites its own internal `device_name` buffer (esp_ble_mesh/core/
    // proxy_server.c) to the hardcoded "ESP-BLE-MESH" - confirmed on real
    // hardware as the lamp showing up as "ESP-BLE-MESH" instead of its own
    // name in a SIG Mesh provisioner app and in this project's own web app.
    // An earlier attempt "fixed" this via ble_svc_gap_device_name_set(),
    // which only changes the NimBLE GAP service's Device Name
    // characteristic - that's a completely separate piece of state from
    // proxy_server.c's own static buffer, which is what both its
    // unprovisioned PB-GATT advertising (gatt_prov_adv_create) AND its
    // post-provisioning Proxy advertising (gatt_proxy_adv_create) actually
    // read from when building the "Complete Local Name" AD field most
    // scanners/pickers show - so that fix had no effect at all. The real
    // setter for that buffer is bt_mesh_set_device_name(), exposed
    // publicly as esp_ble_mesh_set_unprovisioned_device_name() despite its
    // name (the API doc scopes it to the unprovisioned/PB-GATT case, but
    // the implementation is the same single buffer both advertising paths
    // share - verified by reading proxy_server.c directly).
    // Only fails on a too-long name (ESP_BLE_MESH_DEVICE_NAME_MAX_LEN, ~29
    // bytes) - setBleName() itself has no upper bound, so this is reachable
    // with a long custom name. Log rather than ESP_ERROR_CHECK: leaving the
    // lamp advertising as "ESP-BLE-MESH" is the pre-existing (bad) behavior
    // this call is fixing, not a reason to abort boot over a name the user
    // typed.
    esp_err_t nameErr = esp_ble_mesh_set_unprovisioned_device_name(getBleName().c_str());
    if (nameErr != ESP_OK)
        ESP_LOGW(TAG, "esp_ble_mesh_set_unprovisioned_device_name failed: %d (name too long?)", nameErr);
    ESP_ERROR_CHECK(esp_ble_mesh_register_prov_callback(provisioningCallback));
    ESP_ERROR_CHECK(esp_ble_mesh_register_config_server_callback(configServerCallback));
    ESP_ERROR_CHECK(esp_ble_mesh_register_generic_server_callback(genericServerCallback));
    ESP_ERROR_CHECK(esp_ble_mesh_register_lighting_server_callback(lightingServerCallback));
    ESP_ERROR_CHECK(esp_ble_mesh_register_custom_model_callback(customModelCallback));
    ESP_ERROR_CHECK(esp_ble_mesh_register_generic_client_callback(genericClientCallback));
    ESP_ERROR_CHECK(esp_ble_mesh_register_light_client_callback(lightingClientCallback));
#if ENABLE_BLE_MESH_PROVISIONER
    ESP_ERROR_CHECK(esp_ble_mesh_register_config_client_callback(configClientCallback));
    // Loaded from NVS (persistence.cpp) before meshSetup() runs - re-apply
    // on every boot, same idempotent path meshNetworkCreate() itself uses.
    if (meshHasLocalKeys)
    {
        uint16_t myAddr = esp_ble_mesh_get_primary_element_address();
        if (myAddr == 0)
            myAddr = 1; // not yet a provisioned node - this lamp is the network root
        esp_ble_mesh_provisioner_set_primary_elem_addr(myAddr);
        esp_ble_mesh_provisioner_add_local_net_key(meshLocalNetKey, 0);
        esp_ble_mesh_provisioner_add_local_app_key(meshLocalAppKey, 0, 0);
        // ESP_BLE_MESH_PROVISIONER_ADD_LOCAL_APP_KEY_COMP_EVT (above) does
        // the Model App Bind to our own models once this completes.
    }
#endif
    err = esp_ble_mesh_node_prov_enable((esp_ble_mesh_prov_bearer_t)(ESP_BLE_MESH_PROV_ADV | ESP_BLE_MESH_PROV_GATT));
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "esp_ble_mesh_node_prov_enable failed: %d", err);
        return;
    }
    Serial.println(F("[Mesh] Provisioning aktiv."));
}

void meshSendOnOff(uint16_t addr, bool on)
{
    if (!onoffClient.model || onoffClient.model->keys[0] == ESP_BLE_MESH_KEY_UNUSED)
    {
        sendFeedback(F("[Mesh] Generic OnOff Client has no AppKey bound - bind it via nRF Mesh first"));
        return;
    }
    static uint8_t tid = 0;
    esp_ble_mesh_client_common_param_t params{};
    params.opcode = ESP_BLE_MESH_MODEL_OP_GEN_ONOFF_SET_UNACK;
    params.model = onoffClient.model;
    params.ctx.net_idx = 0; // single-subnet simplification, same as elsewhere in this file
    params.ctx.app_idx = onoffClient.model->keys[0];
    params.ctx.addr = addr;
    params.ctx.send_ttl = ESP_BLE_MESH_TTL_DEFAULT;
    params.msg_timeout = 0;

    esp_ble_mesh_generic_client_set_state_t set{};
    set.onoff_set.op_en = false;
    set.onoff_set.onoff = on ? 1 : 0;
    set.onoff_set.tid = tid++;

    esp_err_t err = esp_ble_mesh_generic_client_set_state(&params, &set);
    if (err != ESP_OK)
        sendFeedback(String(F("[Mesh] OnOff send to 0x")) + String(addr, HEX) + F(" failed: ") + String(err));
}

void meshSendLightness(uint16_t addr, float percent)
{
    if (!lightnessClient.model || lightnessClient.model->keys[0] == ESP_BLE_MESH_KEY_UNUSED)
    {
        sendFeedback(F("[Mesh] Light Lightness Client has no AppKey bound - bind it via nRF Mesh first"));
        return;
    }
    static uint8_t tid = 0;
    esp_ble_mesh_client_common_param_t params{};
    params.opcode = ESP_BLE_MESH_MODEL_OP_LIGHT_LIGHTNESS_SET_UNACK;
    params.model = lightnessClient.model;
    params.ctx.net_idx = 0;
    params.ctx.app_idx = lightnessClient.model->keys[0];
    params.ctx.addr = addr;
    params.ctx.send_ttl = ESP_BLE_MESH_TTL_DEFAULT;
    params.msg_timeout = 0;

    esp_ble_mesh_light_client_set_state_t set{};
    set.lightness_set.op_en = false;
    set.lightness_set.lightness = lightnessFromPercent(percent);
    set.lightness_set.tid = tid++;

    esp_err_t err = esp_ble_mesh_light_client_set_state(&params, &set);
    if (err != ESP_OK)
        sendFeedback(String(F("[Mesh] Lightness send to 0x")) + String(addr, HEX) + F(" failed: ") + String(err));
}

// Our Generic OnOff/Light Lightness Servers are otherwise receive-only (see
// this file's own top-of-file comment): they drive the real lamp from
// incoming mesh Set messages, but never the other way - a poti/switch-driven
// local change never reaches their own state, so a provisioner's Get reads
// stale data and no Status is published even if a Publish Address is
// configured. Call this right after such a local change (not from every
// setLampEnabled()/setBrightnessPercent() call site - pattern/ramp ticks
// would otherwise spam the mesh with a Status on every frame) to push the
// real state in, which also publishes it if a Publish Address is set.
// lightness 0 for "off" rather than the stale last brightness, matching the
// same Lightness<->OnOff binding enforced on the receive side above.
void meshPublishLocalState()
{
    if (onoffServer.model)
    {
        esp_ble_mesh_server_state_value_t value{};
        value.gen_onoff.onoff = lampEnabled ? 1 : 0;
        esp_ble_mesh_server_model_update_state(onoffServer.model, ESP_BLE_MESH_GENERIC_ONOFF_STATE, &value);
    }
    if (lightnessServer.model)
    {
        esp_ble_mesh_server_state_value_t value{};
        value.light_lightness_actual.lightness = lampEnabled ? lightnessFromPercent(masterBrightness * 100.0f) : 0;
        esp_ble_mesh_server_model_update_state(lightnessServer.model, ESP_BLE_MESH_LIGHT_LIGHTNESS_ACTUAL_STATE, &value);
    }
}

#if ENABLE_BLE_MESH_PROVISIONER
bool meshHasNetworkIdentity()
{
    return meshHasLocalKeys;
}

bool meshProvisioningSessionIsActive()
{
    return scanSessionActive;
}

void meshNetworkCreate(const uint8_t *netKey, const uint8_t *appKey)
{
    if (netKey)
        memcpy(meshLocalNetKey, netKey, 16);
    else
        esp_fill_random(meshLocalNetKey, 16);
    if (appKey)
        memcpy(meshLocalAppKey, appKey, 16);
    else
        esp_fill_random(meshLocalAppKey, 16);
    meshHasLocalKeys = true;
    saveSettings();

    uint16_t myAddr = esp_ble_mesh_get_primary_element_address();
    if (myAddr == 0)
        myAddr = 1;
    esp_ble_mesh_provisioner_set_primary_elem_addr(myAddr);
    esp_ble_mesh_provisioner_add_local_net_key(meshLocalNetKey, 0);
    esp_ble_mesh_provisioner_add_local_app_key(meshLocalAppKey, 0, 0);
    // The rest (Model App Bind to our own models) happens in
    // provisioningCallback()'s ESP_BLE_MESH_PROVISIONER_ADD_LOCAL_APP_KEY_COMP_EVT
    // handler once this completes - async, not inline here.

    String netHex, appHex;
    for (uint8_t i = 0; i < 16; ++i)
    {
        if (meshLocalNetKey[i] < 0x10)
            netHex += '0';
        netHex += String(meshLocalNetKey[i], HEX);
        if (meshLocalAppKey[i] < 0x10)
            appHex += '0';
        appHex += String(meshLocalAppKey[i], HEX);
    }
    sendFeedback(String(F("[Mesh] Network identity: net=")) + netHex + F(" app=") + appHex +
                 F(" - run the same 'mesh network create <net> <app>' on another lamp to let it provision too"));
}

void meshProvisionScanStart(uint32_t seconds)
{
    if (!meshHasLocalKeys)
    {
        sendFeedback(F("[Mesh] Run 'mesh network create' first"));
        return;
    }
    if (scanSessionActive)
    {
        sendFeedback(F("[Mesh] Scan already running"));
        return;
    }
    if (ble_gap_disc_active())
    {
        sendFeedback(F("[Mesh] BLE scan busy (presence scan?) - try again shortly"));
        return;
    }
    discoveredCount = 0;
    esp_err_t err = esp_ble_mesh_provisioner_prov_enable((esp_ble_mesh_prov_bearer_t)ESP_BLE_MESH_PROV_ADV);
    if (err != ESP_OK)
    {
        sendFeedback(String(F("[Mesh] Provisioner enable failed: ")) + String(err));
        return;
    }
    scanSessionActive = true;
    scanSessionDueMs = millis() + seconds * 1000UL;
    sendFeedback(String(F("[Mesh] Scanning for ")) + String(seconds) + F("s - 'mesh provision list' to see results"));
}

void meshProvisionScanStop()
{
    if (!scanSessionActive)
        return;
    esp_ble_mesh_provisioner_prov_disable((esp_ble_mesh_prov_bearer_t)ESP_BLE_MESH_PROV_ADV);
    scanSessionActive = false;
    sendFeedback(F("[Mesh] Scan stopped"));
}

void meshProvisionTick()
{
    if (scanSessionActive && (int32_t)(millis() - scanSessionDueMs) >= 0)
        meshProvisionScanStop();
}

// Count-in-header + one fixed-shape "DEV ..."/"NODE ..." line per item -
// deliberately the same shape whether the list is empty or not (no separate
// "No devices found" text), so the frontend (frontend/src/lib/meshProtocol.ts)
// can parse this with one regex instead of two, by waiting for the header
// line on the shared log/notify stream and then reading exactly as many
// follow-on lines as the header's count says. Still perfectly readable as a
// plain log line by a human in the Log tab.
String meshProvisionListText()
{
    String out = String(F("[Mesh] Discovered (")) + String(discoveredCount) + F("):");
    for (uint8_t i = 0; i < discoveredCount; ++i)
    {
        out += F("\n  DEV idx=");
        out += String(i + 1);
        out += F(" uuid=");
        out += String(uuidShortOf(discovered[i].uuid), HEX);
        out += F(" rssi=");
        out += String(discovered[i].rssi);
    }
    return out;
}

bool meshProvisionAdd(uint8_t index, uint16_t group)
{
    if (index == 0 || index > discoveredCount)
    {
        sendFeedback(F("[Mesh] No such discovered device"));
        return false;
    }
    if (provStep != PROV_STEP_IDLE)
    {
        sendFeedback(F("[Mesh] A provisioning session is already in progress"));
        return false;
    }
    DiscoveredDev &d = discovered[index - 1];
    memcpy(provTargetUuid, d.uuid, 16);
    provTargetGroup = group;

    esp_ble_mesh_unprov_dev_add_t add{};
    memcpy(add.addr, d.addr, sizeof(add.addr));
    add.addr_type = d.addrType;
    memcpy(add.uuid, d.uuid, 16);
    add.oob_info = 0;
    add.bearer = (esp_ble_mesh_prov_bearer_t)ESP_BLE_MESH_PROV_ADV;
    esp_err_t err = esp_ble_mesh_provisioner_add_unprov_dev(
        &add, (esp_ble_mesh_dev_add_flag_t)(ADD_DEV_START_PROV_NOW_FLAG | ADD_DEV_RM_AFTER_PROV_FLAG));
    if (err != ESP_OK)
    {
        sendFeedback(String(F("[Mesh] Provisioning start failed: ")) + String(err));
        return false;
    }
    sendFeedback(F("[Mesh] Provisioning..."));
    return true;
}

bool meshProvisionRemove(uint16_t addr)
{
    // esp_ble_mesh_provisioner_delete_dev() (the obvious-looking API) only
    // covers the pre-provisioning queue/an in-progress provisioning link,
    // confirmed from its own doc comment ("delete device from queue, and
    // reset current provisioning link") - not an already-provisioned mesh
    // member. Actually kicking an already-provisioned node out requires
    // sending it a Node Reset Config message and handling its response,
    // which is a real feature on its own, not implemented here - this is
    // local bookkeeping only ("stop tracking this address in `mesh nodes`"),
    // the node itself is untouched and still fully provisioned.
    bool removed = removeKnownNodeByAddr(addr);
    saveSettings();
    sendFeedback(removed ? String(F("[Mesh] Forgot 0x")) + String(addr, HEX) + F(" (node itself not reset)")
                          : String(F("[Mesh] 0x")) + String(addr, HEX) + F(" wasn't in the known-nodes list"));
    return removed;
}

bool meshRenameNode(uint16_t addr, const String &name)
{
    bool ok = renameKnownNodeByAddr(addr, name);
    if (ok)
    {
        saveSettings();
        sendFeedback(String(F("[Mesh] Renamed 0x")) + String(addr, HEX) + F(" to ") + name);
    }
    else
    {
        sendFeedback(String(F("[Mesh] 0x")) + String(addr, HEX) + F(" not known"));
    }
    return ok;
}

uint8_t meshKnownNodeCount()
{
    return knownNodeCount;
}

// Same count-in-header shape as meshProvisionListText() above, for the same
// reason - one line per node, "NODE addr=0x.. uuid=.. name=.." (name= is
// just empty, not omitted, when unset, so the field is always in the same
// position for the frontend's regex).
String meshKnownNodesText()
{
    String out = String(F("[Mesh] Known nodes (")) + String(knownNodeCount) + F("):");
    for (uint8_t i = 0; i < knownNodeCount; ++i)
    {
        out += F("\n  NODE addr=0x");
        out += String(knownNodes[i].addr, HEX);
        out += F(" uuid=");
        out += String(knownNodes[i].uuidShort, HEX);
        out += F(" name=");
        out += knownNodes[i].name;
    }
    return out;
}

// <addr>:<uuidShort>:<name>;... - same semicolon-separated, no-spaces
// convention as gestureBindingsToCsv(), for the same reason (drops into
// importConfig()'s space-tokenizer unmodified). `name` can't itself contain
// ';'/':'/newline - renameKnownNodeByAddr() strips those at the point a name
// is set, not here, so this never has to re-sanitize or escape anything.
String meshKnownNodesCsv()
{
    String out;
    for (uint8_t i = 0; i < knownNodeCount; ++i)
    {
        if (!out.isEmpty())
            out += ';';
        out += String(knownNodes[i].addr, HEX);
        out += ':';
        out += String(knownNodes[i].uuidShort, HEX);
        out += ':';
        out += knownNodes[i].name;
    }
    return out;
}

void meshSetKnownNodesFromCsv(const String &csv)
{
    knownNodeCount = 0;
    if (csv.isEmpty())
        return;
    int start = 0;
    int len = csv.length();
    while (start < len && knownNodeCount < MAX_KNOWN_NODES)
    {
        int end = csv.indexOf(';', start);
        if (end < 0)
            end = len;
        String entry = csv.substring(start, end);
        start = end + 1;
        int c1 = entry.indexOf(':');
        if (c1 > 0)
        {
            // Tolerates both the current 3-field format and the pre-naming
            // 2-field one (addr:uuidShort, no trailing :name) already
            // persisted in NVS on real hardware from before this change -
            // those entries just come back with an empty name.
            int c2 = entry.indexOf(':', c1 + 1);
            KnownNode &n = knownNodes[knownNodeCount++];
            n.addr = (uint16_t)strtoul(entry.substring(0, c1).c_str(), nullptr, 16);
            n.uuidShort = (uint32_t)strtoul(entry.substring(c1 + 1, c2 > 0 ? c2 : entry.length()).c_str(), nullptr, 16);
            n.name[0] = '\0';
            if (c2 > 0)
            {
                String name = entry.substring(c2 + 1);
                strncpy(n.name, name.c_str(), MAX_NODE_NAME_LEN);
                n.name[MAX_NODE_NAME_LEN] = '\0';
            }
        }
    }
}
#endif // ENABLE_BLE_MESH_PROVISIONER

#endif // ENABLE_BLE_MESH
