#include "gnss.h"
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define GNSS_TASK_DELAY_MS 60000
static const char *TAG = "GNSS";

// Provided externally
extern const char* send_at_command(const char *command, int timeout_ms);

// Parse a +CGNSINF line into location struct
static bool parse_gnss_line(const char *line, GnssLocation *loc) {
    int run, fix;
    char utc[32];
    float lat, lon, alt;

    int n = sscanf(line, "+CGNSINF: %d,%d,%31[^,],%f,%f,%f", &run, &fix, utc, &lat, &lon, &alt);
    if (n < 6 || fix != 1) {
        ESP_LOGW(TAG, "No valid fix or parse failure. Fields: %d, Fix: %d", n, fix);
        return false;
    }

    loc->latitude = lat;
    loc->longitude = lon;
    loc->altitude = alt;
    return true;
}

// Extract +CGNSINF: line from multiline response
static const char* extract_cgnsinf_line(const char *resp) {
    static char line[256];
    const char *ptr = resp;

    while (ptr && *ptr) {
        const char *end = strstr(ptr, "\r\n");
        if (!end) break;

        size_t len = end - ptr;
        if (len > 0 && len < sizeof(line) && strncmp(ptr, "+CGNSINF:", 9) == 0) {
            strncpy(line, ptr, len);
            line[len] = '\0';
            return line;
        }

        ptr = end + 2;
    }

    return NULL;
}

// Power GNSS on
bool gnss_power_on(void) {
    return send_at_command("AT+CGNSPWR=1", 1000) != NULL;
}

// Power GNSS off
bool gnss_power_off(void) {
    return send_at_command("AT+CGNSPWR=0", 1000) != NULL;
}

// Get GNSS location
bool gnss_get_location(GnssLocation *loc) {
    gnss_power_on();  // ensure powered on

    const char *resp = send_at_command("AT+CGNSINF", 2000);
    if (!resp) {
        ESP_LOGE(TAG, "No response from GNSS");
        return false;
    }

    const char *line = extract_cgnsinf_line(resp);
    if (!line) {
        ESP_LOGE(TAG, "CGNSINF line not found");
        return false;
    }

    return parse_gnss_line(line, loc);
}

// GNSS background task
void gnss_task(void *param) {
    GnssLocation loc;

    ESP_LOGI(TAG, "GNSS task started");
    if (!gnss_power_on()) {
        ESP_LOGE(TAG, "GNSS failed to power on");
        vTaskDelete(NULL);
    }

    vTaskDelay(pdMS_TO_TICKS(30000));  // time for first fix

    while (1) {
        if (gnss_get_location(&loc)) {
            printf("GNSS: Lat %.6f, Lon %.6f, Alt %.2f\n",
                   loc.latitude, loc.longitude, loc.altitude);
        } else {
            printf("GNSS: No fix yet\n");
        }

        vTaskDelay(pdMS_TO_TICKS(GNSS_TASK_DELAY_MS));
    }
}
