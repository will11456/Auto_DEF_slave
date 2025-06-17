
#include "pin_map.h"
#include "main.h"
#include "modem.h"
#include "display.h"
#include "uart.h"
#include "data.h"
#include "heartbeat.h"




#define MESSAGE_LENGTH 22 

static const char *TAG = "UART";

TaskHandle_t heartbeatTaskHandle = NULL;

// Function to initialize UART
void uart_init() {
    const uart_config_t uart_config = {
        .baud_rate = 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    uart_param_config(UART_NUM, &uart_config);
    uart_set_pin(UART_NUM, UART1_TXD, UART1_RXD, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    uart_driver_install(UART_NUM, UART_BUF_SIZE * 2, 0, 0, NULL, 0);
    ESP_LOGW(TAG, "UART 1 initialized");
}

// Task function to read and display UART messages
void uart_task(void *param) {
    
    
    // Create the message queue
     message_queue = xQueueCreate(MESSAGE_QUEUE_SIZE, sizeof(DecodedMessage));
    if (message_queue == NULL) {
        ESP_LOGE(TAG, "Failed to create message queue");
        return;
    }

    // Create the semaphore
    message_semaphore = xSemaphoreCreateBinary();
    if (message_semaphore == NULL) {
        ESP_LOGE(TAG, "Failed to create semaphore");
        return;
    }


    //wait until the display has initialized, so we dont give it data before!!
    xEventGroupWaitBits(systemEvents, DISPLAY_INIT, pdFALSE, pdFALSE, portMAX_DELAY);
    
    char received_message[MESSAGE_LENGTH + 1];  // Buffer for received UART message
    DecodedMessage decoded_msg;

    //Start Heartbead Task
    xTaskCreate(HeartbeatTask, "heartbeat_task", 2048*8, NULL, 5, &heartbeatTaskHandle);


    while (1) {
        // Read message from UART
        int len = uart_read_bytes(UART_NUM, received_message, 22, 10 / portTICK_PERIOD_MS);
        //ESP_LOGW(TAG, "msg: %u" , len);
        if (len == 22) {
            received_message[len] = '\0';  // Null-terminate the received string
            //ESP_LOGI(TAG, "Received message: %s", received_message);

            // Decode the message
            decode_uart_message(received_message, &decoded_msg);
            //ESP_LOGW(TAG, "Decoded message: %s", received_message);
            // Send the decoded message to the queue
            if (xQueueSend(message_queue, &decoded_msg, portMAX_DELAY) == pdTRUE) {
                // Signal the data task to process the message
                xSemaphoreGive(message_semaphore);
            } else {
                ESP_LOGE(TAG, "Failed to send message to queue");
            }
        }
    }
}
