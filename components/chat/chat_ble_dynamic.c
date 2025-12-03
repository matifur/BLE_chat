// // #include "chat_ble.h"
// // #include "chat_ble_common.h"
// // #include "chat.h"
// // #include "esp_random.h"   
// // #include <string.h>
// // #include "esp_log.h"
// // #include "esp_bt.h"
// // #include "esp_bt_main.h"
// // #include "esp_gap_ble_api.h"

// // // =============================================================
// // // Forward declarations from server/client modules
// // // =============================================================

// // esp_err_t chat_ble_server_init(void);
// // esp_err_t chat_ble_client_init(const esp_bd_addr_t peer_addr);

// // esp_err_t chat_ble_server_send(const char *text);
// // esp_err_t chat_ble_client_send(const char *text);

// // bool chat_ble_server_is_connected(void);
// // bool chat_ble_client_is_connected(void);

// // // =============================================================
// // // Internal state
// // // =============================================================

// // static chat_ble_role_t s_role = CHAT_ROLE_UNDECIDED;
// // static esp_bd_addr_t s_server_addr = {0};   // used if we become client

// // static bool s_found_server = false;

// // // We cache GAP callback pointer so we can re-register the one we need later
// // //static esp_gap_ble_cb_t g_gap_event_handler_replaced = NULL;

// // // =============================================================
// // // Minimal advertising parser to detect CHAT_SERVICE_UUID
// // // =============================================================

// // // Look for complete local name "ESP32_CHAT" in advertisement
// // static bool adv_contains_chat_name(const uint8_t *adv, uint8_t len)
// // {
// //     const char target_name[] = "ESP32_CHAT";
// //     const uint8_t target_len = sizeof(target_name) - 1;

// //     uint8_t index = 0;
// //     while (index < len) {
// //         uint8_t field_len = adv[index++];
// //         if (field_len == 0 || index + field_len > len) break;

// //         uint8_t type = adv[index];

// //         if (type == ESP_BLE_AD_TYPE_NAME_CMPL) {
// //             const uint8_t *name = &adv[index + 1];
// //             uint8_t name_len = field_len - 1;

// //             if (name_len == target_len &&
// //                 memcmp(name, target_name, target_len) == 0) {
// //                 return true;
// //             }
// //         }

// //         index += field_len;
// //     }
// //     return false;
// // }


// // // =============================================================
// // // Temporary GAP callback used during scanning phase
// // // =============================================================

// // static void gap_temp_scan_handler(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param)
// // {
// //     switch (event) {
// //     case ESP_GAP_BLE_SCAN_RESULT_EVT: {
// //         const esp_ble_gap_cb_param_t *r = param;
// //         if (r->scan_rst.search_evt == ESP_GAP_SEARCH_INQ_RES_EVT) {
// //             // Check if this advertisement contains our service UUID
// //             if (adv_contains_chat_name(r->scan_rst.ble_adv, r->scan_rst.adv_data_len)) {

// //                 ESP_LOGI(CHAT_BLE_TAG, "Detected CHAT service advertisement -> will become CLIENT");

// //                 s_found_server = true;
// //                 memcpy(s_server_addr, r->scan_rst.bda, sizeof(esp_bd_addr_t));

// //                 // Stop scanning
// //                 esp_ble_gap_stop_scanning();
// //             }
// //         }
// //         break;
// //     }
// //     default:
// //         break;
// //     }
// // }

// // // =============================================================
// // // chat_ble_init(): dynamic role selection
// // // =============================================================

// // esp_err_t chat_ble_init(void)
// // {
// //     esp_err_t ret;

// //     // ---------------------------------------------------------
// //     // 1. Init BLE controller + Bluedroid
// //     // ---------------------------------------------------------
// //     esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();

// //     ret = esp_bt_controller_init(&bt_cfg);
// //     if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
// //         ESP_LOGE(CHAT_BLE_TAG, "BT controller init failed: %s", esp_err_to_name(ret));
// //         return ret;
// //     }

// //     ret = esp_bt_controller_enable(ESP_BT_MODE_BLE);
// //     if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
// //         ESP_LOGE(CHAT_BLE_TAG, "BT controller enable failed: %s", esp_err_to_name(ret));
// //         return ret;
// //     }

// //     ret = esp_bluedroid_init();
// //     if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
// //         ESP_LOGE(CHAT_BLE_TAG, "Bluedroid init failed: %s", esp_err_to_name(ret));
// //         return ret;
// //     }

// //     ret = esp_bluedroid_enable();
// //     if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
// //         ESP_LOGE(CHAT_BLE_TAG, "Bluedroid enable failed: %s", esp_err_to_name(ret));
// //         return ret;
// //     }

// //     ESP_LOGI(CHAT_BLE_TAG, "BLE controller + bluedroid initialized");
// //     uint32_t delay_ms = (esp_random() % 1400) + 200;  // 200–1600 ms
// //     vTaskDelay(pdMS_TO_TICKS(delay_ms));
// //     // ---------------------------------------------------------
// //     // 2. Register temporary GAP callback for scanning
// //     // ---------------------------------------------------------
// //     ret = esp_ble_gap_register_callback(gap_temp_scan_handler);
// //     if (ret != ESP_OK) {
// //         ESP_LOGE(CHAT_BLE_TAG, "gap register failed: %s", esp_err_to_name(ret));
// //         return ret;
// //     }

// //     // ---------------------------------------------------------
// //     // 3. Configure and start scanning
// //     // ---------------------------------------------------------
// //     esp_ble_scan_params_t scan_params = {
// //         .scan_type              = BLE_SCAN_TYPE_ACTIVE,
// //         .own_addr_type          = BLE_ADDR_TYPE_PUBLIC,
// //         .scan_filter_policy     = BLE_SCAN_FILTER_ALLOW_ALL,
// //         .scan_interval          = 0x50,
// //         .scan_window            = 0x30,
// //         .scan_duplicate         = BLE_SCAN_DUPLICATE_DISABLE
// //     };

// //     ESP_ERROR_CHECK(esp_ble_gap_set_scan_params(&scan_params));

// //     ESP_LOGI(CHAT_BLE_TAG, "Scanning for CHAT service...");

// //     // Scan for fixed time
// //     uint32_t t_start = xTaskGetTickCount();
// //     uint32_t timeout_ticks = pdMS_TO_TICKS(CHAT_ROLE_SCAN_TIMEOUT_MS);

// //     while (xTaskGetTickCount() - t_start < timeout_ticks) {
// //         if (s_found_server) break;
// //         vTaskDelay(pdMS_TO_TICKS(20));
// //     }

// //     // ---------------------------------------------------------
// //     // 4. Decide role
// //     // ---------------------------------------------------------
// //     if (s_found_server) {
// //         s_role = CHAT_ROLE_CLIENT;
// //         ESP_LOGI(CHAT_BLE_TAG, "Role selected: CLIENT");

// //         // Switch to full client mode
// //         return chat_ble_client_init(s_server_addr);

// //     } else {
// //         s_role = CHAT_ROLE_SERVER;
// //         ESP_LOGI(CHAT_BLE_TAG, "Role selected: SERVER");

// //         // Stop scanning (if still running)
// //         esp_ble_gap_stop_scanning();

// //         // Switch to full server mode
// //         return chat_ble_server_init();
// //     }
// // }

// // // =============================================================
// // // Public API: delegate to server or client implementation
// // // =============================================================

// // esp_err_t chat_ble_send(const char *text)
// // {
// //     if (s_role == CHAT_ROLE_SERVER)
// //         return chat_ble_server_send(text);
// //     else if (s_role == CHAT_ROLE_CLIENT)
// //         return chat_ble_client_send(text);
// //     else
// //         return ESP_ERR_INVALID_STATE;
// // }

// // bool chat_ble_is_connected(void)
// // {
// //     if (s_role == CHAT_ROLE_SERVER)
// //         return chat_ble_server_is_connected();
// //     else if (s_role == CHAT_ROLE_CLIENT)
// //         return chat_ble_client_is_connected();
// //     return false;
// // }

// // chat_ble_role_t chat_ble_get_role(void)
// // {
// //     return s_role;
// // }

// #include "chat_ble.h"
// #include "chat_ble_common.h"
// #include "chat.h"

// #include "esp_random.h"
// #include <string.h>
// #include "esp_log.h"
// #include "esp_bt.h"
// #include "esp_bt_main.h"
// #include "esp_gap_ble_api.h"
// #include "esp_system.h"   
// #include "esp_mac.h"


// // =============================================================
// // Forward declarations from server/client modules
// // =============================================================

// esp_err_t chat_ble_server_init(void);
// esp_err_t chat_ble_client_init(const esp_bd_addr_t peer_addr);

// esp_err_t chat_ble_server_send(const char *text);
// esp_err_t chat_ble_client_send(const char *text);

// bool chat_ble_server_is_connected(void);
// bool chat_ble_client_is_connected(void);

// // =============================================================
// // Internal state
// // =============================================================

// static chat_ble_role_t s_role = CHAT_ROLE_UNDECIDED;
// static esp_bd_addr_t s_server_addr = {0};   // used if we become client
// static bool s_found_server = false;

// // =============================================================
// // Minimal advertising parser to detect our name "ESP32_CHAT"
// // =============================================================

// static bool adv_contains_chat_name(const uint8_t *adv, uint8_t len)
// {
//     const char target_name[] = "ESP32_CHAT";
//     const uint8_t target_len = sizeof(target_name) - 1;

//     uint8_t index = 0;
//     while (index < len) {
//         uint8_t field_len = adv[index++];
//         if (field_len == 0 || index + field_len > len) break;

//         uint8_t type = adv[index];

//         if (type == ESP_BLE_AD_TYPE_NAME_CMPL) {
//             const uint8_t *name = &adv[index + 1];
//             uint8_t name_len = field_len - 1;

//             if (name_len == target_len &&
//                 memcmp(name, target_name, target_len) == 0) {
//                 return true;
//             }
//         }

//         index += field_len;
//     }
//     return false;
// }

// // =============================================================
// // Temporary GAP callback used during scanning phase (CLIENT only)
// // =============================================================

// static void gap_temp_scan_handler(esp_gap_ble_cb_event_t event,
//                                   esp_ble_gap_cb_param_t *param)
// {
//     switch (event) {

//     case ESP_GAP_BLE_SCAN_PARAM_SET_COMPLETE_EVT: {
//         esp_err_t status = param->scan_param_cmpl.status;
//         if (status != ESP_BT_STATUS_SUCCESS) {
//             ESP_LOGE(CHAT_BLE_TAG, "Scan param set failed, status = %d", status);
//         } else {
//             // Start scanning for the whole timeout; we'll stop early if we find server
//             ESP_LOGI(CHAT_BLE_TAG, "Scan params set, starting scan");
//             // timeout in seconds
//             uint32_t timeout_s = CHAT_ROLE_SCAN_TIMEOUT_MS / 1000;
//             if (timeout_s == 0) timeout_s = 1;
//             esp_ble_gap_start_scanning(timeout_s);
//         }
//         break;
//     }

//     case ESP_GAP_BLE_SCAN_RESULT_EVT: {
//         const esp_ble_gap_cb_param_t *r = param;
//         if (r->scan_rst.search_evt == ESP_GAP_SEARCH_INQ_RES_EVT) {
//             if (adv_contains_chat_name(r->scan_rst.ble_adv,
//                                        r->scan_rst.adv_data_len)) {

//                 ESP_LOGI(CHAT_BLE_TAG,
//                          "Detected CHAT advertisement, will become CLIENT");

//                 s_found_server = true;
//                 memcpy(s_server_addr, r->scan_rst.bda, sizeof(esp_bd_addr_t));

//                 // Stop scanning — we have the server
//                 esp_ble_gap_stop_scanning();
//             }
//         } else if (r->scan_rst.search_evt == ESP_GAP_SEARCH_INQ_CMPL_EVT) {
//             // Scan finished by timeout
//             ESP_LOGI(CHAT_BLE_TAG, "Scan complete");
//         }
//         break;
//     }

//     default:
//         break;
//     }
// }

// // =============================================================
// // chat_ble_init(): dynamic role selection
// // =============================================================

// esp_err_t chat_ble_init(void)
// {
//     esp_err_t ret;

//     // ---------------------------------------------------------
//     // 1. Init BLE controller + Bluedroid
//     // ---------------------------------------------------------
//     esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();

//     ret = esp_bt_controller_init(&bt_cfg);
//     if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
//         ESP_LOGE(CHAT_BLE_TAG, "BT controller init failed: %s", esp_err_to_name(ret));
//         return ret;
//     }

//     ret = esp_bt_controller_enable(ESP_BT_MODE_BLE);
//     if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
//         ESP_LOGE(CHAT_BLE_TAG, "BT controller enable failed: %s", esp_err_to_name(ret));
//         return ret;
//     }

//     ret = esp_bluedroid_init();
//     if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
//         ESP_LOGE(CHAT_BLE_TAG, "Bluedroid init failed: %s", esp_err_to_name(ret));
//         return ret;
//     }

//     ret = esp_bluedroid_enable();
//     if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
//         ESP_LOGE(CHAT_BLE_TAG, "Bluedroid enable failed: %s", esp_err_to_name(ret));
//         return ret;
//     }

//     ESP_LOGI(CHAT_BLE_TAG, "BLE controller + bluedroid initialized");

//     // ---------------------------------------------------------
//     // 2. Decide role deterministically based on BT MAC
//     // ---------------------------------------------------------
//     uint8_t bt_mac[6] = {0};
//     ret = esp_read_mac(bt_mac, ESP_MAC_WIFI_STA);
//     if (ret != ESP_OK) {
//         ESP_LOGE(CHAT_BLE_TAG, "esp_read_mac(ESP_MAC_BT) failed: %s", esp_err_to_name(ret));
//         return ret;
//     }

//     ESP_LOGI(CHAT_BLE_TAG,
//              "Local BT MAC: %02X:%02X:%02X:%02X:%02X:%02X",
//              bt_mac[0], bt_mac[1], bt_mac[2],
//              bt_mac[3], bt_mac[4], bt_mac[5]);

//     bool start_as_server = ((bt_mac[5] & 0x01) == 0);

//     if (start_as_server) {
//         // This device is the SERVER
//         s_role = CHAT_ROLE_SERVER;
//         ESP_LOGI(CHAT_BLE_TAG, "Role selected: SERVER (MAC LSB even)");
//         return chat_ble_server_init();
//     }

//     // ---------------------------------------------------------
//     // 3. This device is CLIENT → perform scan to find server
//     // ---------------------------------------------------------
//     s_role = CHAT_ROLE_CLIENT;
//     s_found_server = false;
//     ESP_LOGI(CHAT_BLE_TAG, "Role selected: CLIENT (MAC LSB odd)");

//     // Register temporary GAP callback for scanning
//     ret = esp_ble_gap_register_callback(gap_temp_scan_handler);
//     if (ret != ESP_OK) {
//         ESP_LOGE(CHAT_BLE_TAG, "gap register failed: %s", esp_err_to_name(ret));
//         return ret;
//     }

//     esp_ble_scan_params_t scan_params = {
//         .scan_type          = BLE_SCAN_TYPE_ACTIVE,
//         .own_addr_type      = BLE_ADDR_TYPE_PUBLIC,
//         .scan_filter_policy = BLE_SCAN_FILTER_ALLOW_ALL,
//         .scan_interval      = 0x50,
//         .scan_window        = 0x30,
//         .scan_duplicate     = BLE_SCAN_DUPLICATE_DISABLE,
//     };

//     ESP_LOGI(CHAT_BLE_TAG, "Setting scan params and waiting for server...");
//     ESP_ERROR_CHECK(esp_ble_gap_set_scan_params(&scan_params));

//     // Wait until:
//     //  - we find a server (s_found_server == true), or
//     //  - overall timeout passed (safety net, in case server not present)
//     TickType_t t_start = xTaskGetTickCount();
//     TickType_t timeout_ticks = pdMS_TO_TICKS(CHAT_ROLE_SCAN_TIMEOUT_MS + 500);

//     while (!s_found_server &&
//            (xTaskGetTickCount() - t_start < timeout_ticks)) {
//         vTaskDelay(pdMS_TO_TICKS(50));
//     }

//     if (s_found_server) {
//         ESP_LOGI(CHAT_BLE_TAG,
//                  "Server found, initializing client with addr "
//                  "%02X:%02X:%02X:%02X:%02X:%02X",
//                  s_server_addr[0], s_server_addr[1], s_server_addr[2],
//                  s_server_addr[3], s_server_addr[4], s_server_addr[5]);
//         return chat_ble_client_init(s_server_addr);
//     }

//     // Fallback: server not found in time.
//     // You can decide what you prefer:
//     //  - stay idle, or
//     //  - become a server anyway.
//     ESP_LOGW(CHAT_BLE_TAG,
//              "No server found within timeout, fallback to SERVER role");
//     s_role = CHAT_ROLE_SERVER;
//     return chat_ble_server_init();
// }

// // =============================================================
// // Public API: delegate to server or client implementation
// // =============================================================

// esp_err_t chat_ble_send(const char *text)
// {
//     if (s_role == CHAT_ROLE_SERVER)
//         return chat_ble_server_send(text);
//     else if (s_role == CHAT_ROLE_CLIENT)
//         return chat_ble_client_send(text);
//     else
//         return ESP_ERR_INVALID_STATE;
// }

// bool chat_ble_is_connected(void)
// {
//     if (s_role == CHAT_ROLE_SERVER)
//         return chat_ble_server_is_connected();
//     else if (s_role == CHAT_ROLE_CLIENT)
//         return chat_ble_client_is_connected();
//     return false;
// }

// chat_ble_role_t chat_ble_get_role(void)
// {
//     return s_role;
// }

#include "chat_ble.h"
#include "chat_ble_common.h"
#include "chat.h"

#include "esp_random.h"
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

// =============================================================
// Minimal advertising parser to detect CHAT name "ESP32_CHAT"
// =============================================================

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

static void gap_temp_scan_handler(esp_gap_ble_cb_event_t event,
                                  esp_ble_gap_cb_param_t *param)
{
    switch (event) {

    case ESP_GAP_BLE_SCAN_PARAM_SET_COMPLETE_EVT: {
        // Scan params are set → now we can start scanning.
        esp_err_t status = param->scan_param_cmpl.status;
        if (status != ESP_BT_STATUS_SUCCESS) {
            ESP_LOGE(CHAT_BLE_TAG, "Scan param set failed, status = %d", status);
        } else {
            // Start scanning for the whole timeout (in seconds).
            uint32_t timeout_s = CHAT_ROLE_SCAN_TIMEOUT_MS / 1000;
            if (timeout_s == 0) timeout_s = 1;
            ESP_LOGI(CHAT_BLE_TAG,
                     "Scan params set, starting scan for %lu s",
                     (unsigned long)timeout_s);
            esp_ble_gap_start_scanning(timeout_s);
        }
        break;
    }

    case ESP_GAP_BLE_SCAN_RESULT_EVT: {
        const esp_ble_gap_cb_param_t *r = param;
        switch (r->scan_rst.search_evt) {
        case ESP_GAP_SEARCH_INQ_RES_EVT:
            // Got an advertising packet
            if (adv_contains_chat_name(r->scan_rst.ble_adv,
                                       r->scan_rst.adv_data_len)) {
                ESP_LOGI(CHAT_BLE_TAG,
                         "Detected CHAT advertisement -> will become CLIENT");

                s_found_server = true;
                memcpy(s_server_addr, r->scan_rst.bda, sizeof(esp_bd_addr_t));

                // Stop scanning, we found our server
                esp_ble_gap_stop_scanning();
            }
            break;

        case ESP_GAP_SEARCH_INQ_CMPL_EVT:
            // Passive info: scan finished by timeout
            ESP_LOGI(CHAT_BLE_TAG, "Scan complete (INQ_CMPL)");
            break;

        default:
            break;
        }
        break;
    }

    case ESP_GAP_BLE_SCAN_STOP_COMPLETE_EVT:
        ESP_LOGI(CHAT_BLE_TAG, "Scan stopped");
        break;

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
        ESP_LOGE(CHAT_BLE_TAG, "BT controller init failed: %s",
                 esp_err_to_name(ret));
        return ret;
    }

    ret = esp_bt_controller_enable(ESP_BT_MODE_BLE);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(CHAT_BLE_TAG, "BT controller enable failed: %s",
                 esp_err_to_name(ret));
        return ret;
    }

    ret = esp_bluedroid_init();
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(CHAT_BLE_TAG, "Bluedroid init failed: %s",
                 esp_err_to_name(ret));
        return ret;
    }

    ret = esp_bluedroid_enable();
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(CHAT_BLE_TAG, "Bluedroid enable failed: %s",
                 esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(CHAT_BLE_TAG, "BLE controller + bluedroid initialized");

    // Optional small random delay to desynchronize if both boot exactly together
    uint32_t delay_ms = (esp_random() % 400) + 100;  // 100–500 ms
    vTaskDelay(pdMS_TO_TICKS(delay_ms));

    // ---------------------------------------------------------
    // 2. Prepare to SCAN for an existing server
    // ---------------------------------------------------------
    s_found_server = false;
    s_role = CHAT_ROLE_UNDECIDED;
    memset(s_server_addr, 0, sizeof(s_server_addr));

    // Register temporary GAP callback for scanning
    ret = esp_ble_gap_register_callback(gap_temp_scan_handler);
    if (ret != ESP_OK) {
        ESP_LOGE(CHAT_BLE_TAG, "gap register failed: %s", esp_err_to_name(ret));
        return ret;
    }

    esp_ble_scan_params_t scan_params = {
        .scan_type          = BLE_SCAN_TYPE_ACTIVE,
        .own_addr_type      = BLE_ADDR_TYPE_PUBLIC,
        .scan_filter_policy = BLE_SCAN_FILTER_ALLOW_ALL,
        .scan_interval      = 0x50,
        .scan_window        = 0x30,
        .scan_duplicate     = BLE_SCAN_DUPLICATE_DISABLE
    };

    ESP_LOGI(CHAT_BLE_TAG,
             "Setting scan params, then scanning for CHAT service...");

    // This triggers ESP_GAP_BLE_SCAN_PARAM_SET_COMPLETE_EVT,
    // where we actually start scanning.
    ESP_ERROR_CHECK(esp_ble_gap_set_scan_params(&scan_params));

    // ---------------------------------------------------------
    // 3. Wait until:
    //    - we find a server (s_found_server == true), or
    //    - scan timeout passes (safety net)
    // ---------------------------------------------------------
    TickType_t t_start       = xTaskGetTickCount();
    TickType_t timeout_ticks = pdMS_TO_TICKS(CHAT_ROLE_SCAN_TIMEOUT_MS + 500);

    while (!s_found_server &&
           (xTaskGetTickCount() - t_start < timeout_ticks)) {
        vTaskDelay(pdMS_TO_TICKS(50));
    }

    // ---------------------------------------------------------
    // 4. Decide role based on scan result
    // ---------------------------------------------------------
    if (s_found_server) {
        // We found a server => become CLIENT
        s_role = CHAT_ROLE_CLIENT;

        ESP_LOGI(CHAT_BLE_TAG,
                 "Role selected: CLIENT. Server addr: "
                 "%02X:%02X:%02X:%02X:%02X:%02X",
                 s_server_addr[0], s_server_addr[1], s_server_addr[2],
                 s_server_addr[3], s_server_addr[4], s_server_addr[5]);

        // Client module will re-register its own GAP/GATT callbacks
        return chat_ble_client_init(s_server_addr);

    } else {
        // No server found => become SERVER
        s_role = CHAT_ROLE_SERVER;
        ESP_LOGI(CHAT_BLE_TAG,
                 "No server found within timeout -> Role selected: SERVER");

        // Stop scanning if still running (ignore error if already stopped)
        esp_ble_gap_stop_scanning();

        // Server module will re-register its own GAP/GATT callbacks
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
