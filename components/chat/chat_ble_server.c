#include "chat_ble_common.h"
#include "chat_ble.h"
#include "chat.h"
#include "ntp.h"
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_gatts_api.h"
#include "esp_gap_ble_api.h"
#include "esp_gatt_common_api.h"

static const char *TAG = "chat_ble_srv";

// BLE server state
static esp_gatt_if_t s_gatts_if      = ESP_GATT_IF_NONE;
static uint16_t      s_service_handle = 0;
static uint16_t      s_char_rx_handle = 0;   // client writes here
static uint16_t      s_char_tx_handle = 0;   // we notify here
static uint16_t      s_conn_id        = 0;
static bool          s_connected      = false;

// Ping measurement state
static TickType_t s_ping_start_ticks = 0;
static bool       s_ping_in_flight   = false;

// Forward declaration of internal send function
esp_err_t chat_ble_server_send(const char *text);

// Simple advertising params, Server must advertise for client to see it, we do not advertise UUID of service, it is known
// from dynamic logic, we send packets with name = "ESP32_CHAT"
static esp_ble_adv_params_t s_adv_params = {
    .adv_int_min       = 0x20,
    .adv_int_max       = 0x40,
    .adv_type          = ADV_TYPE_IND,
    .own_addr_type     = BLE_ADDR_TYPE_PUBLIC,
    .channel_map       = ADV_CHNL_ALL,
    .adv_filter_policy = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY,
};

// Optional: advertise with device name (no UUID here)
static esp_ble_adv_data_t s_adv_data = {
    .set_scan_rsp        = false,
    .include_name        = true,   // we will set name to "ESP32_CHAT"
    .include_txpower     = false,  
    .min_interval        = 0x20,
    .max_interval        = 0x40,
    .appearance          = 0,
    .manufacturer_len    = 0,
    .p_manufacturer_data = NULL,
    .service_data_len    = 0,
    .p_service_data      = NULL,
    .service_uuid_len    = 0,      // <-- NO UUID in adv payload
    .p_service_uuid      = NULL,   // <-- NULL pointer
    .flag                = (ESP_BLE_ADV_FLAG_GEN_DISC | ESP_BLE_ADV_FLAG_BREDR_NOT_SPT),
};

// Forward declarations
static void gap_server_cb(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param);
static void gatts_server_cb(esp_gatts_cb_event_t event,
                            esp_gatt_if_t gatts_if,
                            esp_ble_gatts_cb_param_t *param);

// Helpers
bool chat_ble_server_is_connected(void)
{
    return s_connected;
}

// Receiving message from client
static void chat_ble_print_remote(const char *text)
{
    if (!text) return;

    // Expected incoming format: "HH:MM:SS|MESSAGE"
    char sender_ts[16] = "--------";
    char msg_text[CHAT_BLE_MAX_PACKET_LEN];

    const char *sep = strchr(text, '|');
    if (sep != NULL) {
        // Extract sender timestamp
        size_t ts_len = sep - text;
        if (ts_len < sizeof(sender_ts)) {
            memcpy(sender_ts, text, ts_len);
            sender_ts[ts_len] = '\0';
        }

        // Extract the remaining message
        strncpy(msg_text, sep + 1, sizeof(msg_text));
        msg_text[sizeof(msg_text) - 1] = '\0';
    } else {
        // No timestamp provided - old format fallback
        strncpy(msg_text, text, sizeof(msg_text));
        msg_text[sizeof(msg_text) - 1] = '\0';
    }

        // Check for special PING/PONG control messages
    bool is_ping = (strcmp(msg_text, "__PING__") == 0);
    bool is_pong = (strcmp(msg_text, "__PONG__") == 0);

    if (is_ping) {
        // Auto-reply with PONG
        const char *reply_ts = ntp_get_timestr();
        char payload[32];
        snprintf(payload, sizeof(payload), "%s|__PONG__", reply_ts);
        (void)chat_ble_server_send(payload);
    }

    if (is_pong && s_ping_in_flight) {
        // Compute RTT using local tick counter
        TickType_t now        = xTaskGetTickCount();
        TickType_t diff_ticks = now - s_ping_start_ticks;
        s_ping_in_flight      = false;

        uint32_t diff_ms = diff_ticks * portTICK_PERIOD_MS;

        char info_text[64];
        snprintf(info_text, sizeof(info_text),
                 "Ping RTT: %u ms", (unsigned)diff_ms);

        const char *ts = ntp_get_timestr();
        chat_message_t info = {
            .direction = CHAT_DIR_LOCAL,
            .sender    = "PING",
            .timestamp = ts,
            .text      = info_text,
        };
        chat_io_print_message(&info);
    }

    // Receiver-side timestamp (local)
    const char *local_ts = ntp_get_timestr();

    chat_message_t msg = {
        .direction = CHAT_DIR_REMOTE,
        .sender    = sender_ts,
        .timestamp = local_ts,  // receiver timestamp
        .text      = msg_text,
    };

    chat_io_print_message(&msg);
}


// Sending message from server to client, we use notification without confirmation (last parameter is set to false)
esp_err_t chat_ble_server_send(const char *text)
{
    if (!text) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_connected || s_gatts_if == ESP_GATT_IF_NONE || s_char_tx_handle == 0) {
        return ESP_ERR_INVALID_STATE;
    }

    size_t len = strlen(text);
    if (len == 0) {
        return ESP_OK;
    }
    if (len > CHAT_BLE_MAX_PACKET_LEN) {
        len = CHAT_BLE_MAX_PACKET_LEN;   // simple truncation, no fragmentation
    }

    esp_err_t err = esp_ble_gatts_send_indicate(
        s_gatts_if,
        s_conn_id,
        s_char_tx_handle,
        len,
        (uint8_t *)text,
        false  // notification (no confirmation)
    );
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "send_indicate failed: %s", esp_err_to_name(err));
    }
    return err;
}

esp_err_t chat_ble_server_ping(void)
{
    if (!s_connected || s_gatts_if == ESP_GATT_IF_NONE || s_char_tx_handle == 0) {
        return ESP_ERR_INVALID_STATE;
    }

    const char *ts = ntp_get_timestr();
    char payload[32];
    snprintf(payload, sizeof(payload), "%s|__PING__", ts);

    s_ping_start_ticks = xTaskGetTickCount();
    s_ping_in_flight   = true;

    return chat_ble_server_send(payload);
}


// Initialize BLE server: service + characteristics + advertising.
// Called from chat_ble_init() AFTER controller/bluedroid are ready.
esp_err_t chat_ble_server_init(void)
{
    ESP_LOGI(TAG, "Initializing CHAT BLE server...");

    // Register our GAP and GATTS callbacks
    ESP_ERROR_CHECK(esp_ble_gap_register_callback(gap_server_cb));  // advertising
    ESP_ERROR_CHECK(esp_ble_gatts_register_callback(gatts_server_cb));  // services, characteristics
    ESP_ERROR_CHECK(esp_ble_gatts_app_register(0x55));  // arbitrary app_id

    return ESP_OK;
}

// GAP CALLBACK – advertising handling
static void gap_server_cb(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param)
{
    switch (event) {

    // Data for advertising set, we can advertise 
    case ESP_GAP_BLE_ADV_DATA_SET_COMPLETE_EVT:
        ESP_LOGI(TAG, "Adv data set, starting advertising");
        esp_ble_gap_start_advertising(&s_adv_params);
        break;

    // Advertising started
    case ESP_GAP_BLE_ADV_START_COMPLETE_EVT:
        if (param->adv_start_cmpl.status == ESP_BT_STATUS_SUCCESS) {
            ESP_LOGI(TAG, "Advertising started");
        } else {
            ESP_LOGE(TAG, "Failed to start advertising");
        }
        break;

    // Stopping advertising
    case ESP_GAP_BLE_ADV_STOP_COMPLETE_EVT:
        ESP_LOGI(TAG, "Advertising stopped");
        break;

    default:
        break;
    }
}
// GATTS CALLBACK – service, chars, connection, writes
static void gatts_server_cb(esp_gatts_cb_event_t event,
                            esp_gatt_if_t gatts_if,
                            esp_ble_gatts_cb_param_t *param)
{
    switch (event) {

    // Setting name of device to ESP32_CHAT, creating service
    case ESP_GATTS_REG_EVT: {
        ESP_LOGI(TAG, "GATTS_REG_EVT, app_id=%d", param->reg.app_id);
        s_gatts_if = gatts_if;

        // Set device name
        const char dev_name[] = "ESP32_CHAT";
        esp_ble_gap_set_device_name(dev_name);

        // Payload
        esp_err_t err = esp_ble_gap_config_adv_data(&s_adv_data);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "config_adv_data failed: %s", esp_err_to_name(err));
        }

        // Create primary service
        esp_gatt_srvc_id_t service_id = {
            .is_primary = true,
            .id = {
                .inst_id = 0,
                .uuid    = chat_uuid16(CHAT_SERVICE_UUID),
            }
        };

        esp_ble_gatts_create_service(gatts_if, &service_id, 10 /*num handles*/);
        break;
    }

    // Starting service and adding RX characteristics, client can write only to this address
    case ESP_GATTS_CREATE_EVT: {
        ESP_LOGI(TAG, "Service created, handle=%d", param->create.service_handle);
        s_service_handle = param->create.service_handle;

        // Start service
        esp_ble_gatts_start_service(s_service_handle);

        // Add RX characteristic (WRITE)
        esp_bt_uuid_t rx_uuid = chat_uuid16(CHAT_CHAR_RX_UUID);
        esp_gatt_char_prop_t rx_prop = ESP_GATT_CHAR_PROP_BIT_WRITE | ESP_GATT_CHAR_PROP_BIT_WRITE_NR;

        esp_attr_value_t rx_val = {
            .attr_max_len = CHAT_BLE_MAX_PACKET_LEN,
            .attr_len     = 0,
            .attr_value   = NULL,
        };

        esp_err_t err = esp_ble_gatts_add_char(
            s_service_handle,
            &rx_uuid,
            ESP_GATT_PERM_WRITE,
            rx_prop,
            &rx_val,
            NULL
        );
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "add RX char failed: %s", esp_err_to_name(err));
        }
        break;
    }

    // If characteristics for RX exists then add characteristics for TX
    case ESP_GATTS_ADD_CHAR_EVT: {
        uint16_t uuid16 = param->add_char.char_uuid.uuid.uuid16;
        if (uuid16 == CHAT_CHAR_RX_UUID) {
            s_char_rx_handle = param->add_char.attr_handle;
            ESP_LOGI(TAG, "RX characteristic added, handle=%d", s_char_rx_handle);

            // Now add TX characteristic (NOTIFY)
            esp_bt_uuid_t tx_uuid = chat_uuid16(CHAT_CHAR_TX_UUID);
            esp_gatt_char_prop_t tx_prop = ESP_GATT_CHAR_PROP_BIT_NOTIFY;

            esp_attr_value_t tx_val = {
                .attr_max_len = CHAT_BLE_MAX_PACKET_LEN,
                .attr_len     = 0,
                .attr_value   = NULL,
            };

            esp_err_t err = esp_ble_gatts_add_char(
                s_service_handle,
                &tx_uuid,
                ESP_GATT_PERM_READ,
                tx_prop,
                &tx_val,
                NULL
            );
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "add TX char failed: %s", esp_err_to_name(err));
            }
        } else if (uuid16 == CHAT_CHAR_TX_UUID) {
            s_char_tx_handle = param->add_char.attr_handle;
            ESP_LOGI(TAG, "TX characteristic added, handle=%d", s_char_tx_handle);
        }
        break;
    }

    // Client connected, store ID of connection, setting MTU, 
    case ESP_GATTS_CONNECT_EVT:
        ESP_LOGI(TAG, "Client connected, conn_id=%d", param->connect.conn_id);
        s_connected = true;
        s_conn_id   = param->connect.conn_id;
        esp_ble_gatt_set_local_mtu(517);
        break;

    // Client disconnected, back to advertising
    case ESP_GATTS_DISCONNECT_EVT:
        ESP_LOGI(TAG, "Client disconnected");
        s_connected = false;
        s_conn_id   = 0;

        // Re-start advertising to allow reconnection
        esp_ble_gap_start_advertising(&s_adv_params);
        break;

    // Client wrote a message, receiving this message, logging it and forwarding to UI
    case ESP_GATTS_WRITE_EVT:
        if (param->write.handle == s_char_rx_handle && param->write.len > 0) {
            // Client wrote to RX characteristic -> incoming chat message
            char buf[CHAT_BLE_MAX_PACKET_LEN + 1];
            size_t len = param->write.len;
            if (len > CHAT_BLE_MAX_PACKET_LEN) len = CHAT_BLE_MAX_PACKET_LEN;
            memcpy(buf, param->write.value, len);
            buf[len] = '\0';

            ESP_LOGI(TAG, "Received from client: %s", buf);
            chat_ble_print_remote(buf);
        }
        break;

    default:
        break;
    }
}
