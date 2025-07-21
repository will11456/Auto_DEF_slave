#include "gnss.h"
#include "freertos/idf_additions.h"
#include "pin_map.h"
#include "main.h"
#include "modem.h"
#include "display.h"
#include "uart.h"
#include "data.h"
#include "mqtt.h"
#include "publish.h"

#define GNSS_TASK_DELAY_MS 60000
static const char *TAG = "GNSS";

extern const char* send_at_command(const char *command, int timeout_ms);

// Parse a +CGPSINFO line into location struct
static bool parse_gpsinfo_line(const char *line, GNSSLocation *shared_gnss_data) {
    char lat_str[16], lat_dir;
    char lon_str[16], lon_dir;
    char date[16], time[16];
    float altitude = 0.0;

    int fields = sscanf(line, "+CGPSINFO: %15[^,],%c,%15[^,],%c,%15[^,],%15[^,],%f",
                        lat_str, &lat_dir, lon_str, &lon_dir, date, time, &altitude);
    if (fields < 7 || lat_str[0] == '\0' || lon_str[0] == '\0') {
        ESP_LOGW(TAG, "Invalid GPS fix or parse failure. Fields: %d", fields);
        return false;
    }

    // Convert to decimal degrees
    float lat = atof(lat_str);
    float lon = atof(lon_str);

    int lat_deg = (int)(lat / 100);
    int lon_deg = (int)(lon / 100);
    float lat_min = lat - (lat_deg * 100);
    float lon_min = lon - (lon_deg * 100);

    lat = lat_deg + lat_min / 60.0;
    lon = lon_deg + lon_min / 60.0;

    if (lat_dir == 'S') lat = -lat;
    if (lon_dir == 'W') lon = -lon;

    xSemaphoreTake(gnss_mutex, portMAX_DELAY);
    shared_gnss_data->latitude = lat;
    shared_gnss_data->longitude = lon;
    shared_gnss_data->altitude = altitude;
    xSemaphoreGive(gnss_mutex);

    return true;
}

// Extract +CGPSINFO: line from response
static const char* extract_gpsinfo_line(const char *resp) {
    static char line[256];
    const char *ptr = strstr(resp, "+CGPSINFO:");
    if (!ptr) return NULL;

    const char *end = strstr(ptr, "\r\n");
    if (!end) return NULL;

    size_t len = end - ptr;
    if (len >= sizeof(line)) len = sizeof(line) - 1;

    strncpy(line, ptr, len);
    line[len] = '\0';
    return line;
}

// Power GNSS on
bool gnss_power_on(void) {
    return send_at_command("AT+CGPS=1,1", 1000) != NULL;
}

// Power GNSS off
bool gnss_power_off(void) {
    return send_at_command("AT+CGPS=0", 1000) != NULL;
}

// Get GNSS location
bool gnss_get_location(GNSSLocation *shared_gnss_data) {
    const char *resp = send_at_command("AT+CGPSINFO", 2000);
    if (!resp) {
        ESP_LOGE(TAG, "No response from GPS");
        return false;
    }

    const char *line = extract_gpsinfo_line(resp);
    if (!line) {
        ESP_LOGE(TAG, "CGPSINFO line not found");
        return false;
    }

    return parse_gpsinfo_line(line, shared_gnss_data);
}

// GNSS background task
void gnss_task(void *param) {
    xEventGroupWaitBits(systemEvents, MQTT_INIT, pdFALSE, pdFALSE, portMAX_DELAY);
    vTaskDelay(pdMS_TO_TICKS(5000));

    ESP_LOGW(TAG, "GNSS task started");

    if (!gnss_power_on()) {
        ESP_LOGE(TAG, "GPS failed to power on");
        vTaskDelete(NULL);
    }

    vTaskDelay(pdMS_TO_TICKS(30000));  // Allow GPS to get first fix

    GNSSLocation shared_gnss_data;

    while (1) {
        if (gnss_get_location(&shared_gnss_data)) {
            ESP_LOGI(TAG, "GPS: Lat %.6f, Lon %.6f, Alt %.2f",
                     shared_gnss_data.latitude,
                     shared_gnss_data.longitude,
                     shared_gnss_data.altitude);
        } else {
            ESP_LOGW(TAG, "GPS: No fix or invalid data");
        }

        vTaskDelay(pdMS_TO_TICKS(GNSS_TASK_DELAY_MS));
    }
}
