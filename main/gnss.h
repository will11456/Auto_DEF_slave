#ifndef GNSS_H
#define GNSS_H

#include <stdio.h>
#include <stdlib.h>
#include "driver/gpio.h"
#include "driver/i2c.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"
#include <esp_log.h>
#include "esp_system.h"
#include "cJSON.h"
#include <string.h>
#include "esp_netif.h"
#include "esp_netif_ppp.h"
#include "freertos/idf_additions.h"
#include "mqtt_client.h"
#include "esp_modem_api.h"
#include "sdkconfig.h"


typedef struct {
    float latitude;
    float longitude;
    float altitude;
} GNSSLocation;


extern GNSSLocation shared_gnss_data;
extern SemaphoreHandle_t gnss_mutex;

bool gnss_power_on(void);
bool gnss_power_off(void);
bool gnss_get_location(GNSSLocation *loc);
void gnss_task(void *param);


#endif