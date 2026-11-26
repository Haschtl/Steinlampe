// BLE Mesh feasibility spike - see platformio.ini's header comment for what
// question this is answering and why it's structured this way.
//
// Minimal Mesh Node: Config Server (mandatory on the primary element - the
// Mesh spec requires it for AppKey binding during setup, see
// esp_ble_mesh_config_model_api.h) + a Generic OnOff Server + a Vendor
// Model. "Just Works" provisioning (no OOB, matching the firmware's own
// BLE pairing posture - see src/comms.cpp's
// NimBLEDevice::setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT) choice there,
// same reasoning: neither side has a display or keypad).
//
// The Generic OnOff Server proves standard Mesh models work at all (any
// Mesh app/controller can bind to it with zero custom code on either
// side). The Vendor Model is the other half of the real question: can a
// lamp receive our *own* existing text command syntax ("mode 3", "bri
// 50", ...) over Mesh, the same way it already does over the custom
// BLE/BT/USB GATT protocol in comms.cpp? That's the piece that would let
// one lamp's switch/poti input - via Mesh group publish, configured once
// per lamp through a provisioner app - drive other lamps using our own
// command language, not just generic on/off, without needing a
// from-scratch Mesh Configuration Client in our own app (see the plan
// discussion - that's a separate, bigger effort, deliberately not started
// here).
//
// Success looks like: a provisioner app (e.g. nRF Mesh) finds this device
// advertising unprovisioned, provisions it, binds an AppKey to both
// models, and either a Generic OnOff Set or a raw write to the vendor
// opcode (nRF Mesh's "Generic vendor model" raw-message feature) prints a
// line here.
//
// Written against the real esp_ble_mesh/NimBLE headers and confirmed on
// real hardware (see README.md's Status section): builds, boots clean,
// and advertises correctly on both provisioning bearers. The
// provisioning/model-binding roundtrip from an actual phone app is the
// next untested step.

#include <Arduino.h>

extern "C" {
#include "esp_ble_mesh_common_api.h"
#include "esp_ble_mesh_config_model_api.h"
#include "esp_ble_mesh_health_model_api.h"
#include "esp_ble_mesh_defs.h"
#include "esp_ble_mesh_generic_model_api.h"
#include "esp_ble_mesh_networking_api.h"
#include "esp_ble_mesh_provisioning_api.h"
#include "esp_log.h"
#include "nvs_flash.h"

#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"

#include "esp32-hal-bt.h"
}

namespace {

constexpr const char *TAG = "ble_mesh_spike";

// Made up for the spike - collides with nothing else on a test bench, but a
// real node should derive this from the chip's own MAC instead (see
// Espressif's onoff_server example for that pattern) so multiple lamps
// don't advertise the same identity.
uint8_t devUuid[16] = {0xdd, 0xdd, 0xa1, 0xb2, 0xc3, 0xd4, 0xe5, 0xf6,
                       0x07, 0x18, 0x29, 0x3a, 0x4b, 0x5c, 0x6d, 0x7e};

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
    // Left unset in the first pass, which defaulted it to disabled (0) -
    // that only mattered once the ~60s post-provisioning Node Identity
    // advertising window expired (confirmed on real hardware: the node
    // logged "No subnets to advertise on" and stopped appearing in the
    // app's Proxy scan entirely once that window passed). Without this,
    // the node is only reachable for about a minute after provisioning.
    .gatt_proxy = ESP_BLE_MESH_GATT_PROXY_ENABLED,
    .default_ttl = 7,
};

ESP_BLE_MESH_MODEL_PUB_DEFINE(onoffPub, 2 + 1, ROLE_NODE);
esp_ble_mesh_gen_onoff_srv_t onoffServer = {
    .rsp_ctrl = {
        .get_auto_rsp = ESP_BLE_MESH_SERVER_AUTO_RSP,
        .set_auto_rsp = ESP_BLE_MESH_SERVER_AUTO_RSP,
    },
};

// Health Server is mandatory on the primary element per the Mesh spec
// (every node needs one, same as Config Server) - missed in the first
// pass, which was surfacing as repeated "No Health Server context
// provided" warnings (internal mesh code - e.g. the attention timer -
// unconditionally expects one to exist).
//
// Fully zero-initializing it (as a first attempt) turned out to be a real
// regression, not just a cosmetic gap: health_srv_init() hard-requires at
// least one self-test ID (checked on real hardware - "No Health Test ID
// provided", "Model init failed (err -22)", which then cascaded into
// *every* other model failing to init too, breaking provisioning
// entirely, err=22 instead of err=0). One dummy test ID is enough to be
// spec-valid without actually implementing a real self-test.
constexpr uint8_t healthTestIds[] = {0x00};
esp_ble_mesh_health_srv_t healthServer = {
    .health_test = {
        .id_count = 1,
        .test_ids = healthTestIds,
        .company_id = 0x02E5, // same borrowed Espressif CID as composition.cid below
    },
};
ESP_BLE_MESH_MODEL_PUB_DEFINE(healthPub, 2 + 1, ROLE_NODE);

esp_ble_mesh_model_t rootModels[] = {
    ESP_BLE_MESH_MODEL_CFG_SRV(&configServer),
    ESP_BLE_MESH_MODEL_HEALTH_SRV(&healthServer, &healthPub),
    ESP_BLE_MESH_MODEL_GEN_ONOFF_SRV(&onoffPub, &onoffServer),
};

// Vendor Model: one receive-only opcode carrying a raw text command, same
// syntax as every other transport in comms.cpp. Borrows Espressif's own
// company ID (0x02E5, same one the composition below uses) - a real
// product needs its own assigned company ID, fine for a spike. No publish
// needed (this spike only receives), so no ESP_BLE_MESH_MODEL_PUB_DEFINE
// for it - nullptr for pub/user_data is fine for a model that never
// publishes and keeps no state of its own.
constexpr uint16_t VENDOR_COMPANY_ID = 0x02E5;
constexpr uint32_t VENDOR_OP_CMD = ESP_BLE_MESH_MODEL_OP_3(0x01, VENDOR_COMPANY_ID);

esp_ble_mesh_model_op_t vendorOps[] = {
    ESP_BLE_MESH_MODEL_OP(VENDOR_OP_CMD, 0),
    ESP_BLE_MESH_MODEL_OP_END,
};

esp_ble_mesh_model_t vendorModels[] = {
    ESP_BLE_MESH_VENDOR_MODEL(VENDOR_COMPANY_ID, 0x0001, vendorOps, NULL, NULL),
};

esp_ble_mesh_elem_t elements[] = {
    ESP_BLE_MESH_ELEMENT(0, rootModels, vendorModels),
};

esp_ble_mesh_comp_t composition = {
    .cid = 0x02E5, // Espressif's own CID, fine to borrow for a spike - a
                   // real product needs its own assigned company ID.
    .element_count = 1,
    .elements = elements,
};

void provisioningCallback(esp_ble_mesh_prov_cb_event_t event, esp_ble_mesh_prov_cb_param_t *param) {
    switch (event) {
    case ESP_BLE_MESH_PROV_REGISTER_COMP_EVT:
        ESP_LOGI(TAG, "mesh stack initialized, err=%d", param->prov_register_comp.err_code);
        break;
    case ESP_BLE_MESH_NODE_PROV_ENABLE_COMP_EVT:
        ESP_LOGI(TAG, "provisioning enabled, err=%d", param->node_prov_enable_comp.err_code);
        break;
    case ESP_BLE_MESH_NODE_PROV_COMPLETE_EVT:
        ESP_LOGI(TAG, "provisioning complete - node is now part of a mesh");
        break;
    default:
        ESP_LOGI(TAG, "prov event %d", event);
        break;
    }
}

void genericServerCallback(esp_ble_mesh_generic_server_cb_event_t event, esp_ble_mesh_generic_server_cb_param_t *param) {
    if (event == ESP_BLE_MESH_GENERIC_SERVER_STATE_CHANGE_EVT &&
        param->ctx.recv_op == ESP_BLE_MESH_MODEL_OP_GEN_ONOFF_SET) {
        ESP_LOGI(TAG, "Generic OnOff Set -> %d", param->value.state_change.onoff_set.onoff);
    } else {
        ESP_LOGI(TAG, "generic server event %d", event);
    }
}

// Vendor models don't get their own typed callback like the Generic
// Server models do - esp_ble_mesh_register_custom_model_callback() is the
// one entry point for every vendor model's traffic, dispatched by opcode.
void customModelCallback(esp_ble_mesh_model_cb_event_t event, esp_ble_mesh_model_cb_param_t *param) {
    if (event != ESP_BLE_MESH_MODEL_OPERATION_EVT) {
        ESP_LOGI(TAG, "custom model event %d", event);
        return;
    }
    if (param->model_operation.opcode != VENDOR_OP_CMD) {
        return;
    }
    String cmd;
    for (uint16_t i = 0; i < param->model_operation.length; ++i) {
        cmd += (char)param->model_operation.msg[i];
    }
    // Real integration would hand this to comms.cpp's shared
    // handleCommand() dispatcher, same as every other transport - this
    // spike only proves the message makes it here intact.
    ESP_LOGI(TAG, "Vendor CMD -> \"%s\"", cmd.c_str());
}

void bleMeshStart() {
    ESP_LOGI(TAG, "initializing mesh (NimBLE host)...");
    esp_err_t err = esp_ble_mesh_init(&provision, &composition);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ble_mesh_init failed: %d", err);
        return;
    }
    ESP_ERROR_CHECK(esp_ble_mesh_register_prov_callback(provisioningCallback));
    ESP_ERROR_CHECK(esp_ble_mesh_register_generic_server_callback(genericServerCallback));
    ESP_ERROR_CHECK(esp_ble_mesh_register_custom_model_callback(customModelCallback));
    err = esp_ble_mesh_node_prov_enable((esp_ble_mesh_prov_bearer_t)(ESP_BLE_MESH_PROV_ADV | ESP_BLE_MESH_PROV_GATT));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ble_mesh_node_prov_enable failed: %d", err);
    }
}

// The NimBLE host runs its event loop on its own FreeRTOS task - this is
// that task's entry point, same pattern NimBLE-Arduino itself uses
// internally (NimBLEDevice::host_task, for reference, though we aren't
// using that library here - this talks to the native ESP-IDF NimBLE host
// component directly instead, since it's the one esp_ble_mesh links
// against).
void nimbleHostTask(void *param) {
    (void)param;
    nimble_port_run();
    nimble_port_freertos_deinit();
}

void bluetoothInit() {
    // Confirmed on real hardware: without this, Arduino's own early startup
    // releases the Classic-BT controller memory before nimble_port_init()
    // gets a chance to use it ("esp_bt_controller_rom_mem_release already
    // released, mode 2", then "BLE_INIT: controller init failed",
    // ESP_ERR_INVALID_STATE). esp32-hal-bt.c has a strong, non-weak
    // btInUse() -> true on classic ESP32 that's supposed to prevent exactly
    // that release - but only if the linker actually pulls that
    // translation unit in, which nothing here otherwise references.
    // Calling btStarted() (a plain status check, return value unused) is
    // just there to force that link - same workaround NimBLE-Arduino's own
    // NimBLEDevice::init() uses for the same reason (see its own comment
    // there: "make sure the linker includes esp32-hal-bt.c so Arduino init
    // doesn't release BLE memory").
    btStarted();

    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    ESP_ERROR_CHECK(nimble_port_init());
    nimble_port_freertos_init(nimbleHostTask);
}

} // namespace

void setup() {
    Serial.begin(115200);
    delay(500);
    ESP_LOGI(TAG, "BLE Mesh spike booting");
    bluetoothInit();
    // Mesh init needs the host task above to have actually started polling
    // before esp_ble_mesh_init() can register with it - a short fixed delay
    // is a spike-grade shortcut, not something to carry into real firmware.
    delay(200);
    bleMeshStart();
}

void loop() {
    delay(1000);
}
