
#ifndef AT_TASK_H
#define AT_TASK_H

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


//queue setup
extern QueueHandle_t incoming_queue;
extern QueueHandle_t at_send_queue;
extern QueueHandle_t at_resp_queue;

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

// Forward declaration of the task function
void rx_task(void *arg);
void tx_task(void *arg);

// External queues used by rx_task (defined elsewhere)
extern QueueHandle_t at_resp_queue;

void at_handler_set_response_queue(QueueHandle_t queue);

#endif // RX_TASK_H