#ifndef MAIN_H
#define MAIN_H

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




//event handlers
extern EventGroupHandle_t systemEvents;

//task handles
// TaskHandle_t displayTaskHandle = NULL;
// TaskHandle_t modemTaskHandle = NULL;
// TaskHandle_t dataTaskHandle = NULL;
// TaskHandle_t uartTaskHandle = NULL;
//TaskHandle_t mqttTaskHandle = NULL;
// TaskHandle_t publishTaskHandle = NULL;

// Function Declarations
void GPIOInit(void);
void app_main(void);

//system events
typedef enum
{
    ERROR = 0,
    DISPLAY_INIT,
    MQTT_INIT

    
}systemEvent_t;



#endif // MAIN_H
