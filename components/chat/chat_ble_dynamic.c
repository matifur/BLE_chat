#include "chat_ble.h"
#include "chat_ble_common.h"
#include "chat.h"

#include <string.h>
#include "esp_log.h"
#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_gap_ble_api.h"

// =============================================================
// Forward declarations from server/client modules
// =============================================================

esp_err_t chat_ble_server_init(void);
esp_err_t chat_ble_client_init(const esp_bd_addr_t peer_addr);

esp_err_t chat_ble_server_send(const char *text);
esp_err_t chat_ble_client_send(const char *text);

bool chat_ble_server_is_connected(void);
bool chat_ble_client_is_connected(void);

// =============================================================
// Internal state
// =============================================================

static chat_ble_role_t s_role = CHAT_ROLE_UNDECIDED;
static esp_bd_addr_t s_server_addr = {0};   // used if we become client

static bool s_found_server = false;

// We cache GAP callback pointer so we can re-register the one we need later
//static esp_gap_ble_cb_t g_gap_event_handler_replaced = NULL;

// =============================================================
// Minimal advertising parser to detect CHAT_SERVICE_UUID
// =============================================================

// static bool adv_contains_chat_uuid(const uint8_t *adv, uint8_t len)
// {
//     uint8_t index = 0;
//     while (index < len) {
//         uint8_t field_len = adv[index++];
//         if (field_len == 0) break;

//         uint8_t type = adv[index];
//         if (type == ESP_BLE_AD_TYPE_16SRV_CMPL ||
//             type == ESP_BLE_AD_TYPE_16SRV_PART) {

//             const uint8_t *p = &adv[index + 1];
//             uint8_t remain = field_len - 1;

//             while (remain >= 2) {
//                 uint16_t uuid = (uint16_t)p[0] | (uint16_t)(p[1] << 8);
//                 if (uuid == CHAT_SERVICE_UUID) {
//                     return true;
//                 }
//                 p += 2;
//                 remain -= 2;
//             }
//         }
//         index += field_len;
//     }
//     return false;
// }

// Look for complete local name "ESP32_CHAT" in advertisement
static bool adv_contains_chat_name(const uint8_t *adv, uint8_t len)
{
    const char target_name[] = "ESP32_CHAT";
    const uint8_t target_len = sizeof(target_name) - 1;

    uint8_t index = 0;
    while (index < len) {
        uint8_t field_len = adv[index++];
        if (field_len == 0 || index + field_len > len) break;

        uint8_t type = adv[index];

        if (type == ESP_BLE_AD_TYPE_NAME_CMPL) {
            const uint8_t *name = &adv[index + 1];
            uint8_t name_len = field_len - 1;

            if (name_len == target_len &&
                memcmp(name, target_name, target_len) == 0) {
                return true;
            }
        }

        index += field_len;
    }
    return false;
}


// =============================================================
// Temporary GAP callback used during scanning phase
// =============================================================

static void gap_temp_scan_handler(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param)
{
    switch (event) {
    case ESP_GAP_BLE_SCAN_RESULT_EVT: {
        const esp_ble_gap_cb_param_t *r = param;
        if (r->scan_rst.search_evt == ESP_GAP_SEARCH_INQ_RES_EVT) {
            // Check if this advertisement contains our service UUID
            if (adv_contains_chat_name(r->scan_rst.ble_adv, r->scan_rst.adv_data_len)) {

                ESP_LOGI(CHAT_BLE_TAG, "Detected CHAT service advertisement -> will become CLIENT");

                s_found_server = true;
                memcpy(s_server_addr, r->scan_rst.bda, sizeof(esp_bd_addr_t));

                // Stop scanning
                esp_ble_gap_stop_scanning();
            }
        }
        break;
    }
    default:
        break;
    }
}

// =============================================================
// chat_ble_init(): dynamic role selection
// =============================================================

esp_err_t chat_ble_init(void)
{
    esp_err_t ret;

    // ---------------------------------------------------------
    // 1. Init BLE controller + Bluedroid
    // ---------------------------------------------------------
    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();

    ret = esp_bt_controller_init(&bt_cfg);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(CHAT_BLE_TAG, "BT controller init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = esp_bt_controller_enable(ESP_BT_MODE_BLE);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(CHAT_BLE_TAG, "BT controller enable failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = esp_bluedroid_init();
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(CHAT_BLE_TAG, "Bluedroid init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = esp_bluedroid_enable();
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(CHAT_BLE_TAG, "Bluedroid enable failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(CHAT_BLE_TAG, "BLE controller + bluedroid initialized");

    // ---------------------------------------------------------
    // 2. Register temporary GAP callback for scanning
    // ---------------------------------------------------------
    ret = esp_ble_gap_register_callback(gap_temp_scan_handler);
    if (ret != ESP_OK) {
        ESP_LOGE(CHAT_BLE_TAG, "gap register failed: %s", esp_err_to_name(ret));
        return ret;
    }

    // ---------------------------------------------------------
    // 3. Configure and start scanning
    // ---------------------------------------------------------
    esp_ble_scan_params_t scan_params = {
        .scan_type              = BLE_SCAN_TYPE_ACTIVE,
        .own_addr_type          = BLE_ADDR_TYPE_PUBLIC,
        .scan_filter_policy     = BLE_SCAN_FILTER_ALLOW_ALL,
        .scan_interval          = 0x50,
        .scan_window            = 0x30,
        .scan_duplicate         = BLE_SCAN_DUPLICATE_DISABLE
    };

    ESP_ERROR_CHECK(esp_ble_gap_set_scan_params(&scan_params));

    ESP_LOGI(CHAT_BLE_TAG, "Scanning for CHAT service...");

    // Scan for fixed time
    uint32_t t_start = xTaskGetTickCount();
    uint32_t timeout_ticks = pdMS_TO_TICKS(CHAT_ROLE_SCAN_TIMEOUT_MS);

    while (xTaskGetTickCount() - t_start < timeout_ticks) {
        if (s_found_server) break;
        vTaskDelay(pdMS_TO_TICKS(20));
    }

    // ---------------------------------------------------------
    // 4. Decide role
    // ---------------------------------------------------------
    if (s_found_server) {
        s_role = CHAT_ROLE_CLIENT;
        ESP_LOGI(CHAT_BLE_TAG, "Role selected: CLIENT");

        // Switch to full client mode
        return chat_ble_client_init(s_server_addr);

    } else {
        s_role = CHAT_ROLE_SERVER;
        ESP_LOGI(CHAT_BLE_TAG, "Role selected: SERVER");

        // Stop scanning (if still running)
        esp_ble_gap_stop_scanning();

        // Switch to full server mode
        return chat_ble_server_init();
    }
}

// =============================================================
// Public API: delegate to server or client implementation
// =============================================================

esp_err_t chat_ble_send(const char *text)
{
    if (s_role == CHAT_ROLE_SERVER)
        return chat_ble_server_send(text);
    else if (s_role == CHAT_ROLE_CLIENT)
        return chat_ble_client_send(text);
    else
        return ESP_ERR_INVALID_STATE;
}

bool chat_ble_is_connected(void)
{
    if (s_role == CHAT_ROLE_SERVER)
        return chat_ble_server_is_connected();
    else if (s_role == CHAT_ROLE_CLIENT)
        return chat_ble_client_is_connected();
    return false;
}

chat_ble_role_t chat_ble_get_role(void)
{
    return s_role;
}
