// BLE Mesh feasibility spike - see platformio.ini's header comment for what
// question this is answering and why it's structured this way.
//
// Minimal Mesh Node: Config Server (mandatory on the primary element - the
// Mesh spec requires it for AppKey binding during setup, see
// esp_ble_mesh_config_model_api.h) + a Generic OnOff Server. "Just Works"
// provisioning (no OOB, matching the firmware's own BLE pairing posture -
// see src/comms.cpp's NimBLEDevice::setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT)
// choice there, same reasoning: neither side has a display or keypad).
//
// Success looks like: a provisioner app (e.g. nRF Mesh) finds this device
// advertising unprovisioned, provisions it, binds an AppKey to the OnOff
// model, and a Generic OnOff Set from the app prints a line here.
//
// Written against the real esp_ble_mesh/NimBLE headers (API shapes
// verified by reading framework-arduinoespressif32-libs@5.5.0's headers
// directly) but never compiled - no ESP-IDF toolchain was available on the
// machine that wrote this. The controller/NimBLE-host bring-up sequence in
// bluetoothInit() below is the part most likely to need fixing first - that
// specific API has shifted across ESP-IDF versions historically.

#include <Arduino.h>

extern "C" {
#include "esp_ble_mesh_common_api.h"
#include "esp_ble_mesh_config_model_api.h"
#include "esp_ble_mesh_defs.h"
#include "esp_ble_mesh_generic_model_api.h"
#include "esp_ble_mesh_provisioning_api.h"
#include "esp_log.h"
#include "nvs_flash.h"

#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
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
    .default_ttl = 7,
};

ESP_BLE_MESH_MODEL_PUB_DEFINE(onoffPub, 2 + 1, ROLE_NODE);
esp_ble_mesh_gen_onoff_srv_t onoffServer = {
    .rsp_ctrl = {
        .get_auto_rsp = ESP_BLE_MESH_SERVER_AUTO_RSP,
        .set_auto_rsp = ESP_BLE_MESH_SERVER_AUTO_RSP,
    },
};

esp_ble_mesh_model_t rootModels[] = {
    ESP_BLE_MESH_MODEL_CFG_SRV(&configServer),
    ESP_BLE_MESH_MODEL_GEN_ONOFF_SRV(&onoffPub, &onoffServer),
};

esp_ble_mesh_elem_t elements[] = {
    ESP_BLE_MESH_ELEMENT(0, rootModels, ESP_BLE_MESH_MODEL_NONE),
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

void bleMeshStart() {
    ESP_LOGI(TAG, "initializing mesh (NimBLE host)...");
    esp_err_t err = esp_ble_mesh_init(&provision, &composition);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ble_mesh_init failed: %d", err);
        return;
    }
    ESP_ERROR_CHECK(esp_ble_mesh_register_prov_callback(provisioningCallback));
    ESP_ERROR_CHECK(esp_ble_mesh_register_generic_server_callback(genericServerCallback));
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
