#pragma once

#include "esp_err.h"
#include "esp_gap_ble_api.h"
#include "esp_gatt_defs.h"
#include "esp_gatt_common_api.h"
#include "esp_bt_defs.h"
#include "esp_random.h"


// =============================================================
//  Common BLE definitions for Chat App (Approach B)
// =============================================================

// 16-bit Service UUID
#define CHAT_SERVICE_UUID        0x1234

// Characteristic UUIDs
#define CHAT_CHAR_RX_UUID        0xABCD   // Client writes -> Server receives
#define CHAT_CHAR_TX_UUID        0xDCBA   // Server notifies -> Client receives

// Max BLE packet length (we keep it simple, no fragmentation yet)
#define CHAT_BLE_MAX_PACKET_LEN  180

// Initial scan timeout before deciding role (ms)
#define CHAT_ROLE_SCAN_TIMEOUT_MS  5000 // 5s    

// BLE roles
typedef enum {
    CHAT_ROLE_UNDECIDED = 0,
    CHAT_ROLE_SERVER,
    CHAT_ROLE_CLIENT
} chat_ble_role_t;

// Utility macro for logging
#define CHAT_BLE_TAG  "chat_ble"

// Helper inline: build 16-bit UUID struct
static inline esp_bt_uuid_t chat_uuid16(uint16_t uuid)
{
    esp_bt_uuid_t u = {
        .len = ESP_UUID_LEN_16,
        .uuid = { .uuid16 = uuid }
    };
    return u;
}
