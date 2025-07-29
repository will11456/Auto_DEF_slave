#ifndef MQTT_H
#define MQTT_H

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
#include "lvgl.h"
#include <string.h>
#include <stdio.h>
#include <stdbool.h>
#include "esp_system.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "cJSON.h"

extern QueueHandle_t incoming_queue;
extern QueueHandle_t master_cmd_queue; 


// Handle incoming MQTT URC (to be called from your URC handler)
void mqtt_handle_urc(const char *urc);
void publish_stored_attributes(void);
void send_rpc_response(const char *req_id, cJSON *result);
void handle_shared_attributes(const char *json);
void handle_rpc_request(const char *topic, const char *json);

void handle_run(void);
void handle_stop(void);
void handle_reboot(void);

void int_to_hex_str(unsigned int num, char *str, int str_size);
void send_message(int message_id, int message_type, uint16_t data0, uint16_t data1, uint16_t data2, uint16_t data3);

void mqtt_urc_task(void *param);


// Getter functions to retrieve stored values from NVS
esp_err_t mqtt_get_aux_range(float *out_val);
esp_err_t mqtt_get_aux_max(float *out_val);
esp_err_t mqtt_get_ext_range(float *out_val);
esp_err_t mqtt_get_ext_max(float *out_val);

#endif // MQTT_H