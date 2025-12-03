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
    esp_gatt_status_t status;
    uint16_t count;

    // --- Find RX characteristic ---
    count = sizeof(result_chars) / sizeof(result_chars[0]);
    status = esp_ble_gattc_get_char_by_uuid(
                 s_gattc_if,
                 s_conn_id,
                 s_service_start,
                 s_service_end,
                 rx_uuid,
                 result_chars,
                 &count);

    if (status == ESP_GATT_OK && count > 0) {
        s_rx_handle = result_chars[0].char_handle;
        ESP_LOGI(TAG, "RX characteristic found, handle=%d", s_rx_handle);
    } else {
        ESP_LOGW(TAG, "RX characteristic NOT found");
    }

    // --- Find TX characteristic ---
    count = sizeof(result_chars) / sizeof(result_chars[0]);
    status = esp_ble_gattc_get_char_by_uuid(
                 s_gattc_if,
                 s_conn_id,
                 s_service_start,
                 s_service_end,
                 tx_uuid,
                 result_chars,
                 &count);

    if (status == ESP_GATT_OK && count > 0) {
        s_tx_handle = result_chars[0].char_handle;
        ESP_LOGI(TAG, "TX characteristic found, handle=%d", s_tx_handle);
    } else {
        ESP_LOGW(TAG, "TX characteristic NOT found");
    }

    // --- Register for TX notifications ---
    if (s_tx_handle != 0) {
        esp_err_t err = esp_ble_gattc_register_for_notify(
            s_gattc_if,
            s_peer_addr,
            s_tx_handle
        );

        if (err != ESP_OK) {
            ESP_LOGE(TAG, "register_for_notify failed: %s",
                     esp_err_to_name(err));
        } else {
            ESP_LOGI(TAG, "Registered for notify on TX characteristic");
        }
    }
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

        case ESP_GATTC_REG_FOR_NOTIFY_EVT: {
        if (param->reg_for_notify.status != ESP_GATT_OK) {
            ESP_LOGE(TAG, "REG_FOR_NOTIFY failed, status=%d",
                     param->reg_for_notify.status);
            break;
        }

        if (param->reg_for_notify.handle != s_tx_handle) {
            // Some other handle – ignore
            break;
        }

        ESP_LOGI(TAG, "REG_FOR_NOTIFY OK, finding CCC descriptor...");

        esp_bt_uuid_t ccc_uuid = {
            .len = ESP_UUID_LEN_16,
            .uuid = { .uuid16 = ESP_GATT_UUID_CHAR_CLIENT_CONFIG }
        };

        esp_gattc_descr_elem_t descr_elem[5];
        uint16_t count = sizeof(descr_elem) / sizeof(descr_elem[0]);
        esp_gatt_status_t status = esp_ble_gattc_get_descr_by_char_handle(
            gattc_if,
            s_conn_id,
            s_tx_handle,
            ccc_uuid,
            descr_elem,
            &count
        );

        if (status == ESP_GATT_OK && count > 0) {
            s_tx_ccc_handle = descr_elem[0].handle;
            ESP_LOGI(TAG, "CCC descriptor found, handle=%d", s_tx_ccc_handle);
            enable_tx_notifications();   // writes 0x0001 to CCC
        } else {
            ESP_LOGE(TAG, "CCC descriptor NOT found, status=%d, count=%u",
                     status, (unsigned)count);
        }
        break;
    }

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
