#ifndef UART_H
#define UART_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include "data.h"
#include "esp_log.h"
#include "pin_map.h"

// UART configuration
#define UART_NUM UART_NUM_1
#define UART_BUF_SIZE (2048)


// Function to initialize UART with specified parameters
void uart_init(void);

// Task function for receiving UART messages in a FreeRTOS task
// This function should be passed to xTaskCreate to run the UART receive task
void uart_task(void *param);

#endif // UART_H
