#pragma once

#include <stdbool.h>
#include "esp_err.h"
#include "chat_ble_common.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Initialize BLE subsystem and dynamically choose role:
 *
 * 1. Start scanning for CHAT_SERVICE_UUID
 * 2. If we detect peer advertising -> become CLIENT
 * 3. If no peer detected before timeout -> become SERVER
 *
 * Then the appropriate BLE logic (server/client) is initialized.
 */
esp_err_t chat_ble_init(void);

/**
 * Send text to the other device over BLE.
 *
 * - In SERVER role: sends NOTIFICATION using TX characteristic
 * - In CLIENT role: writes into server's RX characteristic
 */
esp_err_t chat_ble_send(const char *text);

/**
 * Return true if there is an active BLE connection with the peer.
 */
bool chat_ble_is_connected(void);

/**
 * Return current BLE role after initialization.
 */
chat_ble_role_t chat_ble_get_role(void);

#ifdef __cplusplus
}
#endif
