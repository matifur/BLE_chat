#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_err.h"
#include "nvs_flash.h"

#include "chat.h"
#include "chat_ble.h"
#include "wifi.h"
#include "ntp.h"

static const char *TAG = "main";

void app_main(void)
{
    ESP_LOGI(TAG, "Starting BLE Chat...");

    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(chat_io_init());
    ESP_ERROR_CHECK(chat_ble_init());

    ESP_ERROR_CHECK(wifi_init_sta());
    const char* ntp_server = "time.google.com"; // changeable NTP server
    esp_err_t err = ntp_sync_time(ntp_server);
    if (err != ESP_OK) {
    ESP_LOGW(TAG, "NTP failed to sync! Time will be wrong, but we continue.");
    // Nie robimy ESP_ERROR_CHECK, pozwalamy programowi iść dalej
    }

    ESP_LOGI(TAG, "BLE initialized. Role = %s",
             chat_ble_get_role() == CHAT_ROLE_SERVER ? "SERVER" : "CLIENT");

    char line[128];

    while (1) {
        chat_io_print_prompt();

        esp_err_t err = chat_io_read_line(line, sizeof(line), portMAX_DELAY);
        if (err == ESP_OK) {

            // Get sender timestamp (from NTP)
            const char* ts_sender = ntp_get_timestr();

            // Create message structure
            chat_message_t local = {
                .direction = CHAT_DIR_LOCAL,
                .sender    = "YOU",
                .timestamp = ts_sender,  // store sender timestamp here
                .text      = line,
            };

            // Print message (function will now append local timestamp)
            chat_io_print_message(&local);
            
            // Build payload: TIMESTAMP|MESSAGE
            char payload[160];
            snprintf(payload, sizeof(payload), "%s|%s", ts_sender, line);

            if (chat_ble_is_connected()) {
                chat_ble_send(payload);
            } else {
                ESP_LOGW(TAG, "Not connected – cannot send!");
            }

        } else if (err == ESP_ERR_TIMEOUT) {
            continue;
        } else {
            ESP_LOGE(TAG, "chat_io_read_line error: %s", esp_err_to_name(err));
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }
}
