#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Obsługuje komendy wpisywane w terminalu czatu.
 *
 * Zwraca true, jeśli linia została rozpoznana jako komenda
 * i w całości obsłużona (nie należy już wysyłać jej po BLE).
 * Zwraca false, jeśli to nie jest komenda - wtedy main powinien
 * potraktować ją jako zwykłą wiadomość do wysłania.
 */
bool chat_cmd_handle_line(const char *line);

#ifdef __cplusplus
}
#endif
