#include "chat.h"

#include <stdio.h>
#include <string.h>

#include "driver/usb_serial_jtag.h"
#include "esp_log.h"

static const char *TAG = "chat_io";

static void chat_io_write_raw(const void *data, size_t len)
{
    if (!data || len == 0) {
        return;
    }
    /* Blokujemy się aż dane trafią do bufora TX USB */
    usb_serial_jtag_write_bytes(data, len, portMAX_DELAY);
}

esp_err_t chat_io_init(void)
{
    usb_serial_jtag_driver_config_t cfg = USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT();
    esp_err_t err = usb_serial_jtag_driver_install(&cfg);
    if (err == ESP_ERR_INVALID_STATE) {
        // Sterownik już zainstalowany (np. przez konsolę) – traktujemy jako OK
        ESP_LOGW(TAG, "usb_serial_jtag driver already installed");
        return ESP_OK;
    } else if (err != ESP_OK) {
        ESP_LOGE(TAG, "usb_serial_jtag_driver_install failed: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "Chat IO initialized using USB Serial/JTAG driver");
    return ESP_OK;
}

esp_err_t chat_io_read_line(char *buf, size_t buf_len, TickType_t timeout)
{
    if (!buf || buf_len < 2) {
        return ESP_ERR_INVALID_ARG;
    }

    size_t idx = 0;
    TickType_t start = xTaskGetTickCount();

    while (1) {
        TickType_t remain;

        if (timeout == portMAX_DELAY) {
            remain = portMAX_DELAY;
        } else {
            TickType_t now = xTaskGetTickCount();
            TickType_t elapsed = now - start;
            if (elapsed >= timeout) {
                if (idx == 0) {
                    return ESP_ERR_TIMEOUT;
                } else {
                    break;
                }
            }
            remain = timeout - elapsed;
        }

        uint8_t ch;
        int n = usb_serial_jtag_read_bytes(&ch, 1, remain);
        if (n < 0) {
            return ESP_FAIL;
        }
        if (n == 0) {
            // timeout tego czytania
            if (timeout == portMAX_DELAY) {
                continue;
            }
            if (idx == 0) {
                return ESP_ERR_TIMEOUT;
            } else {
                break;
            }
        }

        // ENTER = CR lub LF -> koniec linii
        if (ch == '\r' || ch == '\n') {
            break;
        }

        // Backspace / DEL -> usuń ostatni znak z bufora (BEZ ruszania ekranu)
        if (ch == '\b' || ch == 0x7F) {
            if (idx > 0) {
                idx--;
            }
            continue;
        }

        // Zwykły znak – dopisz do bufora (jeśli jest miejsce)
        if (idx < buf_len - 1) {
            buf[idx++] = (char)ch;
        }
        // jak nie ma miejsca, po prostu ignorujemy nadmiar
    }

    buf[idx] = '\0';
    return ESP_OK;
}

void chat_io_print_message(const chat_message_t *msg)
{
    if (!msg || !msg->text) {
        return;
    }

    const char *dir_str = (msg->direction == CHAT_DIR_REMOTE) ? "IN " : "OUT";
    const char *ts      = (msg->timestamp && msg->timestamp[0]) ? msg->timestamp : "--------";
    const char *sender  = (msg->sender && msg->sender[0]) ? msg->sender : "unknown";

    char line[256];
    int len = snprintf(line, sizeof(line),
                       "[%s][%s][%-7s]: %s\r\n",
                       dir_str, ts, sender, msg->text);
    if (len <= 0) {
        return;
    }
    if (len > (int)sizeof(line)) {
        len = sizeof(line);
    }

    chat_io_write_raw(line, (size_t)len);
}

void chat_io_print_prompt(void)
{
    const char prompt[] = "> ";
    chat_io_write_raw(prompt, sizeof(prompt) - 1);
}
