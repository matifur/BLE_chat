#pragma once

#include "esp_err.h"

/**
 * Inicjalizacja NTP i synchronizacja czasu.
 * @param server - adres serwera NTP, np. "pool.ntp.org"
 */
esp_err_t ntp_sync_time(const char *server);

/**
 * Pobranie aktualnego czasu w formacie HH:MM:SS
 * @return wskaźnik do bufora statycznego
 */
const char* ntp_get_timestr(void);
