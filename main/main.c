#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_err.h"

#include "chat.h"
#include "chat_ble.h"

static const char *TAG = "main";

void app_main(void)
{
    ESP_LOGI(TAG, "Starting BLE Chat...");

    // Initialize UART console for chat I/O
    ESP_ERROR_CHECK(chat_io_init());

    // Initialize BLE dynamically (server/client decided at runtime)
    ESP_ERROR_CHECK(chat_ble_init());

    ESP_LOGI(TAG, "BLE initialized. Role = %s",
             chat_ble_get_role() == CHAT_ROLE_SERVER ? "SERVER" : "CLIENT");

    char line[128];

    while (1) {
        chat_io_print_prompt();

        esp_err_t err = chat_io_read_line(line, sizeof(line), portMAX_DELAY);
        if (err == ESP_OK) {

            // Print LOCAL message
            chat_message_t local = {
                .direction = CHAT_DIR_LOCAL,
                .sender    = "YOU",
                .timestamp = NULL,
                .text      = line,
            };
            chat_io_print_message(&local);

            // Send over BLE
            if (chat_ble_is_connected()) {
                chat_ble_send(line);
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
