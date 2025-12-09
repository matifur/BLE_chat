#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_err.h"

#include "chat.h"
#include "chat_ble.h"
#include "ntp.h"
#include "chat_cmd.h"

bool chat_cmd_handle_line(const char *line)
{
    if (line == NULL || line[0] == '\0') {
        return false;
    }

        // --- /help ---
    if (strcmp(line, "/help") == 0) {
        const char *ts = ntp_get_timestr();

        static const char *help_lines[] = {
            "Available commands:",
            "/time          - show current NTP time",
            "/ntp [server]  - sync time using given or default NTP server",
            "/burst N       - send N messages for bandwidth test",
            "/ping          - measure RTT over BLE",
            NULL
        };

        for (int i = 0; help_lines[i] != NULL; i++) {
            chat_message_t msg = {
                .direction = CHAT_DIR_LOCAL,
                .sender    = "HELP",
                .timestamp = ts,
                .text      = help_lines[i],
            };
            chat_io_print_message(&msg);
        }

        return true;
    }
    // --- koniec /help ---


    // --- /ping ---
    if (strcmp(line, "/ping") == 0) {
        const char *ts = ntp_get_timestr();

        if (!chat_ble_is_connected()) {
            chat_message_t msg = {
                .direction = CHAT_DIR_LOCAL,
                .sender    = "PING",
                .timestamp = ts,
                .text      = "BLE not connected – cannot ping",
            };
            chat_io_print_message(&msg);
            return true;
        }

        esp_err_t err = chat_ble_ping();

        char info[64];
        if (err == ESP_OK) {
            snprintf(info, sizeof(info),
                     "Ping sent, waiting for PONG...");
        } else {
            snprintf(info, sizeof(info),
                     "Ping error: %s", esp_err_to_name(err));
        }

        chat_message_t msg = {
            .direction = CHAT_DIR_LOCAL,
            .sender    = "PING",
            .timestamp = ts,
            .text      = info,
        };
        chat_io_print_message(&msg);
        return true;
    }

    // --- /ntp [server] ---
    if (strncmp(line, "/ntp", 4) == 0) {
        const char *server = NULL;

        // próbujemy odczytać nazwę serwera po spacji
        if (line[4] == ' ' || line[4] == '\t') {
            server = line + 5; // po "/ntp "
            while (*server == ' ' || *server == '\t') {
                server++;
            }
            if (*server == '\0') {
                server = NULL; // brak nazwy => domyślny
            }
        }

        esp_err_t ntp_err = ntp_sync_time(server);
        const char *ts = ntp_get_timestr();

        char info_text[80];
        if (ntp_err == ESP_OK) {
            // limit 40 znaków, żeby nie było ostrzeżenia o truncation
            snprintf(info_text, sizeof(info_text),
                     "NTP sync OK (%.40s)", server ? server : "default");
        } else {
            snprintf(info_text, sizeof(info_text),
                     "NTP sync FAILED (%.40s)", esp_err_to_name(ntp_err));
        }

        chat_message_t msg = {
            .direction = CHAT_DIR_LOCAL,
            .sender    = "NTP",
            .timestamp = ts,
            .text      = info_text,
        };

        chat_io_print_message(&msg);
        return true;
    }

    // --- /burst N ---
    if (strncmp(line, "/burst", 6) == 0) {
        int count = 0;

        // Proste parsowanie: /burst N
        if (sscanf(line, "/burst %d", &count) != 1 || count <= 0) {
            const char *ts = ntp_get_timestr();
            chat_message_t msg = {
                .direction = CHAT_DIR_LOCAL,
                .sender    = "BURST",
                .timestamp = ts,
                .text      = "Usage: /burst N (N > 0)",
            };
            chat_io_print_message(&msg);
            return true;
        }

        // Bezpieczeństwo: nie pozwalamy na absurdalnie duże N
        if (count > 500) {
            count = 500;
        }

        if (!chat_ble_is_connected()) {
            const char *ts = ntp_get_timestr();
            chat_message_t msg = {
                .direction = CHAT_DIR_LOCAL,
                .sender    = "BURST",
                .timestamp = ts,
                .text      = "BLE not connected – cannot run burst",
            };
            chat_io_print_message(&msg);
            return true;
        }

        TickType_t start = xTaskGetTickCount();

        for (int i = 0; i < count; i++) {
            char payload[64];
            // proste payloady: "BURST 1", "BURST 2", ...
            snprintf(payload, sizeof(payload), "BURST %d", i + 1);
            (void)chat_ble_send(payload);
        }

        TickType_t end = xTaskGetTickCount();
        TickType_t elapsed_ticks = end - start;
        uint32_t elapsed_ms = elapsed_ticks * portTICK_PERIOD_MS;

        uint32_t msgs_per_sec = 0;
        uint32_t ms_per_msg   = 0;
        if (elapsed_ms > 0U) {
            msgs_per_sec = (uint32_t)((count * 1000U) / elapsed_ms);
        }
        if (count > 0) {
            ms_per_msg = (uint32_t)(elapsed_ms / (uint32_t)count);
        }

        char info_text[96];
        if (elapsed_ms == 0U) {
            snprintf(info_text, sizeof(info_text),
                     "Burst %d msgs in <1 ms", count);
        } else {
            snprintf(info_text, sizeof(info_text),
                     "Burst %d msgs in %u ms (%u msg/s, %u ms/msg)",
                     count,
                     (unsigned)elapsed_ms,
                     (unsigned)msgs_per_sec,
                     (unsigned)ms_per_msg);
        }

        const char *ts = ntp_get_timestr();
        chat_message_t msg = {
            .direction = CHAT_DIR_LOCAL,
            .sender    = "BURST",
            .timestamp = ts,
            .text      = info_text,
        };
        chat_io_print_message(&msg);

        return true;
    }

    // --- /time ---
    if (strcmp(line, "/time") == 0) {
        const char *ts = ntp_get_timestr();

        chat_message_t msg = {
            .direction = CHAT_DIR_LOCAL,
            .sender    = "TIME",
            .timestamp = ts,
            .text      = "Current NTP time",
        };

        chat_io_print_message(&msg);
        return true;
    }

    // Nie rozpoznano komendy -> to zwykła wiadomość
    return false;
}
