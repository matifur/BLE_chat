#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#include "chat.h"

static const char *TAG = "main";

void app_main(void)
{
    ESP_LOGI(TAG, "Starting chat demo...");

    ESP_ERROR_CHECK(chat_io_init());

    char line[128];

    printf("\r\n--- Simple chat echo demo ---\r\n");
    printf("Wpisz linie tekstu i nacisnij Enter.\r\n");
    printf("Kazda linia zostanie wyswietlona jako WIADOMOSC Z ZEWNATRZ.\r\n\r\n");

    while (1) {
        chat_io_print_prompt();

        esp_err_t err = chat_io_read_line(line, sizeof(line), portMAX_DELAY);
        if (err == ESP_OK) {
            // Symulujemy, że to co wpisales przyszlo Z ZEWNATRZ (np. po BLE)
            chat_message_t incoming = {
                .direction = CHAT_DIR_REMOTE,
                .sender    = "REMOTE",   // na razie na sztywno
                .timestamp = NULL,       // w przyszłości: string z NTP
                .text      = line,
            };
            chat_io_print_message(&incoming);

            // Gdy dojdzie BLE:
            //  - tu możesz potraktować 'line' jako wiadomosc DO wyslania
            //  - a w callbacku od BLE tworzysz chat_message_t i wołasz chat_io_print_message()
        } else if (err == ESP_ERR_TIMEOUT) {
            // tu raczej nie wejdziemy, bo portMAX_DELAY
            continue;
        } else {
            ESP_LOGE(TAG, "chat_io_read_line error: %s", esp_err_to_name(err));
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }
}
