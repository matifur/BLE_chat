#include "ntp.h"
#include "esp_sntp.h"
#include "esp_log.h"
#include <time.h>

static const char *TAG = "ntp";

static char time_str[16]; // HH:MM:SS

esp_err_t ntp_sync_time(const char *server)
{
    if (!server) {
        server = "pool.ntp.org"; // domyślny
    }

    ESP_LOGI(TAG, "Initializing SNTP with server: %s", server);

    sntp_setoperatingmode(SNTP_OPMODE_POLL);
    sntp_setservername(0, (char*)server);
    sntp_init();

    // czekamy na synchronizację czasu
    int retry = 0;
    time_t now = 0;
    struct tm timeinfo = {0};
    do {
        time(&now);
        localtime_r(&now, &timeinfo);
        vTaskDelay(200 / portTICK_PERIOD_MS);
        retry++;
    } while (timeinfo.tm_year < (2020 - 1900) && retry < 20);

    if (retry == 20) {
        ESP_LOGW(TAG, "NTP sync failed, using local time");
        return ESP_ERR_TIMEOUT;
    }

    ESP_LOGI(TAG, "NTP time synchronized");
    return ESP_OK;
}

const char* ntp_get_timestr(void)
{
    time_t now;
    struct tm timeinfo;
    time(&now);
    localtime_r(&now, &timeinfo);
    snprintf(time_str, sizeof(time_str), "%02d:%02d:%02d",
             timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
    return time_str;
}
