#pragma once

#include <stddef.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// Skąd pochodzi wiadomość (lokalna = od użytkownika po UART,
// zdalna = np. z BLE, drugiej płytki)
typedef enum {
    CHAT_DIR_LOCAL = 0,
    CHAT_DIR_REMOTE,
} chat_direction_t;

// Struktura wiadomości - łatwa do rozbudowy (timestamp, id, itp.)
typedef struct {
    chat_direction_t direction;  // lokalna / zdalna
    const char *sender;          // np. "YOU", "NODE_A", itp. (może być NULL)
    const char *timestamp;       // np. "12:34:56" z NTP (może być NULL)
    const char *text;            // właściwa treść
} chat_message_t;

/**
 * Inicjalizacja warstwy IO czatu (UART0 -> USB).
 * Wołasz raz na starcie.
 */
esp_err_t chat_io_init(void);

/**
 * Czyta jedną linię tekstu od użytkownika (po UART).
 *
 * - buf: bufor na tekst
 * - buf_len: rozmiar bufora
 * - timeout: maksymalny czas oczekiwania (np. portMAX_DELAY)
 *
 * Zwraca:
 *  - ESP_OK        - jeśli przeczytano linię (bez \r\n, zakończoną '\0')
 *  - ESP_ERR_TIMEOUT - jeśli minął timeout bez danych
 *  - inny kod ESP_ERR_xxx przy błędach
 */
esp_err_t chat_io_read_line(char *buf, size_t buf_len, TickType_t timeout);

/**
 * Wyświetla wiadomość na "terminalu czatu" (UART).
 * Format: [IN ][HH:MM:SS][NODE_A]: treść
 *         [OUT][....     ][YOU   ]: treść
 *
 * Pola sender/timestamp mogą być NULL.
 */
void chat_io_print_message(const chat_message_t *msg);

/**
 * Pomocnicza funkcja: wypisanie prostego promptu ">"
 * (call opcjonalny, dla ładniejszego UX).
 */
void chat_io_print_prompt(void);

#ifdef __cplusplus
}
#endif
