
#ifndef PUBLISH_H
#define PUBLISH_H

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
#include "certificates.h"
#include "esp_netif.h"
#include "esp_netif_ppp.h"
#include "freertos/idf_additions.h"
#include "mqtt_client.h"
#include "esp_modem_api.h"
#include "sdkconfig.h"


// Define the MQTT client handle
//static esp_mqtt_client_handle_t client;

// Function prototype for the publish task
void publish_task(void *pvParameter);

#endif // PUBLISH_H