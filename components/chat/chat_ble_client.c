// #include "chat_ble_common.h"
// #include "chat_ble.h"
// #include "chat.h"

// #include <string.h>
// #include "esp_log.h"
// #include "esp_gattc_api.h"
// #include "esp_gap_ble_api.h"
// #include "esp_gatt_defs.h"
// #include "esp_gatt_common_api.h"
// #include "freertos/FreeRTOS.h"
// #include "freertos/task.h"

// static const char *TAG = "chat_ble_cli";

// // ======================================================================
// // STATE
// // ======================================================================

// static esp_gatt_if_t s_gattc_if     = ESP_GATT_IF_NONE;
// static uint16_t      s_conn_id      = 0;
// static bool          s_connected    = false;

// static uint16_t s_service_start_handle = 0;
// static uint16_t s_service_end_handle   = 0;

// static uint16_t s_rx_char_handle = 0;   // Client writes -> server receives
// static uint16_t s_tx_char_handle = 0;   // Server notifies -> client receives

// static esp_bd_addr_t s_peer_addr = {0};  // server MAC

// // ======================================================================
// // Helpers
// // ======================================================================

// bool chat_ble_client_is_connected(void)
// {
//     return s_connected;
// }

// static void chat_ble_print_remote(const char *text)
// {
//     chat_message_t msg = {
//         .direction = CHAT_DIR_REMOTE,
//         .sender    = "REMOTE",
//         .timestamp = NULL,
//         .text      = text,
//     };
//     chat_io_print_message(&msg);
// }

// // ======================================================================
// // Sending (client -> server) through RX characteristic WRITE
// // ======================================================================

// esp_err_t chat_ble_client_send(const char *text)
// {
//     if (!text) return ESP_ERR_INVALID_ARG;
//     if (!s_connected || s_rx_char_handle == 0) return ESP_ERR_INVALID_STATE;

//     size_t len = strlen(text);
//     if (len > CHAT_BLE_MAX_PACKET_LEN) len = CHAT_BLE_MAX_PACKET_LEN;

//     esp_err_t err = esp_ble_gattc_write_char(
//         s_gattc_if,
//         s_conn_id,
//         s_rx_char_handle,
//         len,
//         (uint8_t *)text,
//         ESP_GATT_WRITE_TYPE_NO_RSP,
//         ESP_GATT_AUTH_REQ_NONE
//     );

//     if (err != ESP_OK) {
//         ESP_LOGE(TAG, "gattc_write_char failed: %s", esp_err_to_name(err));
//     }
//     return err;
// }

// // ======================================================================
// // Enable notifications on TX characteristic
// // ======================================================================

// static void enable_tx_notifications(void)
// {
//     // Find CCC descriptor first (client characteristic config)
//     esp_ble_gattc_descr_elem_t descr_elem;
//     uint16_t count = 1;

//     esp_err_t err = esp_ble_gattc_get_descr_by_char_handle(
//         s_gattc_if,
//         s_conn_id,
//         s_tx_char_handle,
//         ESP_GATT_UUID_CHAR_CLIENT_CONFIG,
//         &descr_elem,
//         &count
//     );

//     if (err != ESP_OK || count == 0) {
//         ESP_LOGE(TAG, "CCC descriptor not found for TX char");
//         return;
//     }

//     uint8_t notify_en[2] = {0x01, 0x00};

//     err = esp_ble_gattc_write_char_descr(
//         s_gattc_if,
//         s_conn_id,
//         descr_elem.handle,
//         sizeof(notify_en),
//         notify_en,
//         ESP_GATT_WRITE_TYPE_RSP,
//         ESP_GATT_AUTH_REQ_NONE
//     );

//     if (err != ESP_OK) {
//         ESP_LOGE(TAG, "write CCC descriptor failed: %s", esp_err_to_name(err));
//     } else {
//         ESP_LOGI(TAG, "Notifications enabled on TX characteristic");
//     }
// }

// // ======================================================================
// // GATTC CALLBACK — handles connect/disconnect, discovery, notifications
// // ======================================================================

// static void gattc_cb(esp_gattc_cb_event_t event,
//                      esp_gatt_if_t gattc_if,
//                      esp_ble_gattc_cb_param_t *param)
// {
//     switch (event) {

//     case ESP_GATTC_REG_EVT:
//         ESP_LOGI(TAG, "Client registered (app_id=%d)", param->reg.app_id);
//         s_gattc_if = gattc_if;

//         // Initiate connection immediately
//         esp_ble_gattc_open(
//             gattc_if,
//             s_peer_addr,
//             BLE_ADDR_TYPE_PUBLIC,
//             true
//         );
//         break;

//     case ESP_GATTC_OPEN_EVT:
//         if (param->open.status == ESP_GATT_OK) {
//             ESP_LOGI(TAG, "Connected to server");
//             s_connected = true;
//             s_conn_id   = param->open.conn_id;

//             // Start service discovery
//             esp_ble_gattc_search_service(
//                 gattc_if,
//                 s_conn_id,
//                 &chat_uuid16(CHAT_SERVICE_UUID)
//             );
//         } else {
//             ESP_LOGE(TAG, "Connection failed: %d", param->open.status);
//         }
//         break;

//     case ESP_GATTC_SEARCH_RES_EVT:
//         // Check if this is our service
//         if (param->search_res.srvc_id.id.uuid.uuid.uuid16 == CHAT_SERVICE_UUID) {
//             ESP_LOGI(TAG, "Chat service found");
//             s_service_start_handle = param->search_res.start_handle;
//             s_service_end_handle   = param->search_res.end_handle;

//             // Discover characteristics
//             esp_ble_gattc_get_characteristic(
//                 gattc_if,
//                 s_conn_id,
//                 &param->search_res.srvc_id,
//                 NULL
//             );
//         }
//         break;

//     case ESP_GATTC_GET_CHAR_EVT: {
//         if (param->get_char.status != ESP_GATT_OK) break;

//         uint16_t char_uuid = param->get_char.char_id.uuid.uuid.uuid16;
//         uint16_t handle    = param->get_char.char_handle;

//         if (char_uuid == CHAT_CHAR_RX_UUID) {
//             s_rx_char_handle = handle;
//             ESP_LOGI(TAG, "RX characteristic found, handle=%d", handle);
//         }
//         else if (char_uuid == CHAT_CHAR_TX_UUID) {
//             s_tx_char_handle = handle;
//             ESP_LOGI(TAG, "TX characteristic found, handle=%d", handle);

//             // Enable notifications AFTER descriptors are known
//             enable_tx_notifications();
//         }
//         break;
//     }

//     case ESP_GATTC_NOTIFY_EVT: {
//         // Server sent us data
//         char buf[CHAT_BLE_MAX_PACKET_LEN + 1];
//         size_t len = param->notify.value_len;

//         if (len > CHAT_BLE_MAX_PACKET_LEN)
//             len = CHAT_BLE_MAX_PACKET_LEN;

//         memcpy(buf, param->notify.value, len);
//         buf[len] = '\0';

//         ESP_LOGI(TAG, "Notify from server: %s", buf);
//         chat_ble_print_remote(buf);
//         break;
//     }

//     case ESP_GATTC_DISCONNECT_EVT:
//         ESP_LOGI(TAG, "Disconnected from server");
//         s_connected = false;
//         s_rx_char_handle = 0;
//         s_tx_char_handle = 0;

//         // For Approach B: scanning will resume automatically (handled by dynamic init)
//         break;

//     default:
//         break;
//     }
// }

// // ======================================================================
// // GAP CALLBACK — minimal, client doesn't need much here
// // ======================================================================

// static void gap_client_cb(esp_gap_ble_cb_event_t event,
//                           esp_ble_gap_cb_param_t *param)
// {
//     switch (event) {
//     default:
//         break;
//     }
// }

// // ======================================================================
// // chat_ble_client_init() — called from dynamic role logic
// // ======================================================================

// esp_err_t chat_ble_client_init(const esp_bd_addr_t peer_addr)
// {
//     ESP_LOGI(TAG, "Initializing CHAT BLE client...");
//     memcpy(s_peer_addr, peer_addr, sizeof(esp_bd_addr_t));

//     // Register GATTC and GAP callbacks
//     ESP_ERROR_CHECK(esp_ble_gap_register_callback(gap_client_cb));
//     ESP_ERROR_CHECK(esp_ble_gattc_register_callback(gattc_cb));

//     // Register as GATT client app
//     ESP_ERROR_CHECK(esp_ble_gattc_app_register(0x44));  // arbitrary ID

//     return ESP_OK;
// }

#include "chat_ble_common.h"
#include "chat_ble.h"
#include "chat.h"

#include <string.h>
#include "esp_log.h"
#include "esp_gap_ble_api.h"
#include "esp_gattc_api.h"
#include "esp_gatt_defs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "chat_ble_client";

// ============================================================================
// CLIENT STATE
// ============================================================================

static esp_gatt_if_t s_gattc_if      = ESP_GATT_IF_NONE;
static uint16_t      s_conn_id       = 0;
static bool          s_connected     = false;

static uint16_t s_service_start      = 0;
static uint16_t s_service_end        = 0;

static uint16_t s_rx_handle          = 0;   // client → server writes
static uint16_t s_tx_handle          = 0;   // server → client notifies
static uint16_t s_tx_ccc_handle      = 0;   // descriptor to enable notifications

static esp_bd_addr_t s_peer_addr     = {0};

// ============================================================================
// HELPERS
// ============================================================================

bool chat_ble_client_is_connected(void)
{
    return s_connected;
}

static void chat_ble_print_remote(const char *text)
{
    chat_message_t msg = {
        .direction = CHAT_DIR_REMOTE,
        .sender    = "REMOTE",
        .timestamp = NULL,
        .text      = text,
    };
    chat_io_print_message(&msg);
}

// ============================================================================
// SEND MESSAGE: client → server (WRITE to RX characteristic)
// ============================================================================

esp_err_t chat_ble_client_send(const char *text)
{
    if (!text) return ESP_ERR_INVALID_ARG;
    if (!s_connected || s_rx_handle == 0) return ESP_ERR_INVALID_STATE;

    size_t len = strlen(text);
    if (len > CHAT_BLE_MAX_PACKET_LEN)
        len = CHAT_BLE_MAX_PACKET_LEN;

    esp_err_t err = esp_ble_gattc_write_char(
        s_gattc_if,
        s_conn_id,
        s_rx_handle,
        len,
        (uint8_t *)text,
        ESP_GATT_WRITE_TYPE_NO_RSP,
        ESP_GATT_AUTH_REQ_NONE
    );

    if (err != ESP_OK)
        ESP_LOGE(TAG, "write_char failed: %s", esp_err_to_name(err));

    return err;
}

// ============================================================================
// ENABLE TX NOTIFICATIONS
// ============================================================================

static void enable_tx_notifications(void)
{
    if (s_tx_ccc_handle == 0) {
        ESP_LOGE(TAG, "No CCC descriptor for TX characteristic!");
        return;
    }

    uint8_t notify_en[2] = {0x01, 0x00};

    esp_err_t err = esp_ble_gattc_write_char_descr(
        s_gattc_if,
        s_conn_id,
        s_tx_ccc_handle,
        sizeof(notify_en),
        notify_en,
        ESP_GATT_WRITE_TYPE_RSP,
        ESP_GATT_AUTH_REQ_NONE
    );

    if (err != ESP_OK)
        ESP_LOGE(TAG, "Enable notify failed: %s", esp_err_to_name(err));
    else
        ESP_LOGI(TAG, "Notifications enabled");
}

// ============================================================================
// DISCOVERY HELPERS: Find characteristics and descriptor
// ============================================================================

static void discover_characteristics(void)
{
    esp_bt_uuid_t rx_uuid = chat_uuid16(CHAT_CHAR_RX_UUID);
    esp_bt_uuid_t tx_uuid = chat_uuid16(CHAT_CHAR_TX_UUID);

    esp_gattc_char_elem_t result_chars[10];
    uint16_t count = 0;

    // --- Find RX characteristic ---
    if (esp_ble_gattc_get_char_by_uuid(
            s_gattc_if,
            s_conn_id,
            s_service_start,
            s_service_end,
            rx_uuid,
            result_chars,
            &count) == ESP_OK && count > 0)
    {
        s_rx_handle = result_chars[0].char_handle;
        ESP_LOGI(TAG, "RX characteristic found, handle=%d", s_rx_handle);
    }

    // --- Find TX characteristic ---
    count = 0;
    if (esp_ble_gattc_get_char_by_uuid(
            s_gattc_if,
            s_conn_id,
            s_service_start,
            s_service_end,
            tx_uuid,
            result_chars,
            &count) == ESP_OK && count > 0)
    {
        s_tx_handle = result_chars[0].char_handle;
        ESP_LOGI(TAG, "TX characteristic found, handle=%d", s_tx_handle);
    }

    // --- Find CCC descriptor for TX characteristic ---
    if (s_tx_handle != 0) {
        esp_bt_uuid_t ccc_uuid = {
            .len = ESP_UUID_LEN_16,
            .uuid = { .uuid16 = ESP_GATT_UUID_CHAR_CLIENT_CONFIG }
        };

        esp_gattc_descr_elem_t descr_elem[5];
        count = 0;

        if (esp_ble_gattc_get_descr_by_char_handle(
                s_gattc_if,
                s_conn_id,
                s_tx_handle,
                ccc_uuid,
                descr_elem,
                &count) == ESP_OK && count > 0)
        {
            s_tx_ccc_handle = descr_elem[0].handle;
            ESP_LOGI(TAG, "CCC descriptor found, handle=%d", s_tx_ccc_handle);
        }
    }

    // If TX found + descriptor found → enable notifications
    if (s_tx_ccc_handle != 0)
        enable_tx_notifications();
}

// ============================================================================
// GATTC CALLBACK
// ============================================================================

static void gattc_cb(esp_gattc_cb_event_t event,
                     esp_gatt_if_t gattc_if,
                     esp_ble_gattc_cb_param_t *param)
{
    switch (event) {

    case ESP_GATTC_REG_EVT:
        ESP_LOGI(TAG, "Client registered");
        s_gattc_if = gattc_if;

        esp_ble_gattc_open(gattc_if, s_peer_addr, BLE_ADDR_TYPE_PUBLIC, true);
        break;

    case ESP_GATTC_OPEN_EVT:
        if (param->open.status == ESP_GATT_OK) {
            ESP_LOGI(TAG, "Connected to server");
            s_connected = true;
            s_conn_id   = param->open.conn_id;

            // Begin service discovery
            // esp_ble_gattc_search_service(
            //     gattc_if,
            //     s_conn_id,
            //     &chat_uuid16(CHAT_SERVICE_UUID)
            // );
            esp_bt_uuid_t service_uuid = chat_uuid16(CHAT_SERVICE_UUID);

            esp_ble_gattc_search_service(
                gattc_if,
                s_conn_id,
                &service_uuid
            );


        } else {
            ESP_LOGE(TAG, "Connection failed, status=%d", param->open.status);
        }
        break;

    case ESP_GATTC_SEARCH_RES_EVT:
        if (param->search_res.srvc_id.uuid.uuid.uuid16 == CHAT_SERVICE_UUID) {
            s_service_start = param->search_res.start_handle;
            s_service_end   = param->search_res.end_handle;
            ESP_LOGI(TAG, "Chat service found: start=%d end=%d",
                     s_service_start, s_service_end);
        }
        break;

    case ESP_GATTC_SEARCH_CMPL_EVT:
        if (s_service_start != 0 && s_service_end != 0) {
            ESP_LOGI(TAG, "Service discovery complete, discovering chars…");
            discover_characteristics();
        }
        break;

    case ESP_GATTC_NOTIFY_EVT: {
        size_t len = param->notify.value_len;

        char buf[CHAT_BLE_MAX_PACKET_LEN + 1];
        if (len > CHAT_BLE_MAX_PACKET_LEN)
            len = CHAT_BLE_MAX_PACKET_LEN;

        memcpy(buf, param->notify.value, len);
        buf[len] = '\0';

        ESP_LOGI(TAG, "Notify: %s", buf);
        chat_ble_print_remote(buf);
        break;
    }

    case ESP_GATTC_DISCONNECT_EVT:
        ESP_LOGW(TAG, "Disconnected from server");
        s_connected = false;
        break;

    default:
        break;
    }
}

// ============================================================================
// GAP CALLBACK (minimal for client)
// ============================================================================

static void gap_client_cb(esp_gap_ble_cb_event_t event,
                          esp_ble_gap_cb_param_t *param)
{
    // client does not need much here
}

// ============================================================================
// INIT ENTRY POINT (called from dynamic logic)
// ============================================================================

esp_err_t chat_ble_client_init(const esp_bd_addr_t peer_addr)
{
    ESP_LOGI(TAG, "Initializing CHAT BLE client…");
    memcpy(s_peer_addr, peer_addr, sizeof(esp_bd_addr_t));

    ESP_ERROR_CHECK(esp_ble_gap_register_callback(gap_client_cb));
    ESP_ERROR_CHECK(esp_ble_gattc_register_callback(gattc_cb));

    ESP_ERROR_CHECK(esp_ble_gattc_app_register(0x44));

    return ESP_OK;
}
