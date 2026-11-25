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
#include <esp_mac.h>

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
}

// Provided by main.cpp/command.cpp's shared dispatcher - same function
// every other transport (USB/BT-Serial/BLE custom protocol/BLE-MIDI) feeds
// text commands into.
void handleCommand(const String &line);

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

esp_ble_mesh_model_t rootModels[] = {
    ESP_BLE_MESH_MODEL_CFG_SRV(&configServer),
    ESP_BLE_MESH_MODEL_HEALTH_SRV(&healthServer, &healthPub),
    ESP_BLE_MESH_MODEL_GEN_ONOFF_SRV(&onoffPub, &onoffServer),
    ESP_BLE_MESH_MODEL_LIGHT_LIGHTNESS_SRV(&lightnessPub, &lightnessServer),
    ESP_BLE_MESH_MODEL_LIGHT_LIGHTNESS_SETUP_SRV(&lightnessSetupPub, &lightnessSetupServer),
    ESP_BLE_MESH_MODEL_LIGHT_LC_SRV(&lcPub, &lcServer),
    ESP_BLE_MESH_MODEL_LIGHT_LC_SETUP_SRV(&lcSetupPub, &lcSetupServer),
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
        float pct = (lightness * 100.0f) / 65535.0f;
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
    ESP_ERROR_CHECK(esp_ble_mesh_register_prov_callback(provisioningCallback));
    ESP_ERROR_CHECK(esp_ble_mesh_register_config_server_callback(configServerCallback));
    ESP_ERROR_CHECK(esp_ble_mesh_register_generic_server_callback(genericServerCallback));
    ESP_ERROR_CHECK(esp_ble_mesh_register_lighting_server_callback(lightingServerCallback));
    ESP_ERROR_CHECK(esp_ble_mesh_register_custom_model_callback(customModelCallback));
    err = esp_ble_mesh_node_prov_enable((esp_ble_mesh_prov_bearer_t)(ESP_BLE_MESH_PROV_ADV | ESP_BLE_MESH_PROV_GATT));
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "esp_ble_mesh_node_prov_enable failed: %d", err);
        return;
    }
    Serial.println(F("[Mesh] Provisioning aktiv."));
}

#endif // ENABLE_BLE_MESH
