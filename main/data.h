#ifndef DATA_H
#define DATA_H

#include <stdio.h>
#include "string.h"
#include "message_ids.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include  "ui.h"
#include "ui_helpers.h"
#include "driver/uart.h"
#include "esp_sleep.h"

#define MESSAGE_QUEUE_SIZE 10   // Adjust the size according to your needs


// Data structure to hold the sensor data
typedef struct {
    
    float int_tank;
    float ext_tank;
    float aux_tank;
    float batt_volt;
    float temp;
    float pres;
    float rh;
    float pt1000;
    char status[32];
    char mode[32];


} sensor_data_t;

// Declare the sensor data structure and mutex
extern sensor_data_t shared_sensor_data;
extern SemaphoreHandle_t data_mutex;

// Structure to hold decoded UART message information
typedef struct {
    int message_id;
    int message_type;
    uint16_t data0;
    uint16_t data1;
    uint16_t data2;
    uint16_t data3;
} DecodedMessage;




// Queue and Semaphore Handles
extern QueueHandle_t message_queue;
extern SemaphoreHandle_t message_semaphore;

// Function to decode a UART message
void decode_uart_message(const char *input_message, DecodedMessage *decoded_msg);

// Function to handle messages based on their ID
void handle_message(const DecodedMessage *decoded_msg);

// Handle Functions
void handle_bme280_message(const DecodedMessage *decoded_msg);
void handle_tank_message(const DecodedMessage *decoded_msg);
void handle_mode_message(const DecodedMessage *decoded_msg);
void handle_comms_message(const DecodedMessage *decoded_msg);
void handle_24v_out_message(const DecodedMessage *decoded_msg);
void handle_batt_message(const DecodedMessage *decoded_msg);
void handle_analog_out_message(const DecodedMessage *decoded_msg);
void handle_420_inputs_message(const DecodedMessage *decoded_msg);
void handle_analog_inputs_message(const DecodedMessage *decoded_msg);
void handle_pt1000_message(const DecodedMessage *decoded_msg);
void handle_status_message(const DecodedMessage *decoded_msg);
void handle_system_message(const DecodedMessage *decoded_msg);

// Task function for monitoring the message queue
void data_task(void *param);

#endif // DATA_H
