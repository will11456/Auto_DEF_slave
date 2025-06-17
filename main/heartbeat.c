#include "pin_map.h"
#include "main.h"
#include "modem.h"
#include "display.h"
#include "uart.h"
#include "data.h"
#include "mqtt.h"
#include "publish.h"
#include "heartbeat.h"

static const char *TAG = "HEARTBEAT";


void send_heartbeat(void){

    uint8_t init_msg[16] = {0};
    int txBytes = uart_write_bytes(UART_NUM_1, (const char *)init_msg, sizeof(init_msg));
    //ESP_LOGW(TAG, "Sent HEARTBEAT");

}



void HeartbeatTask(void *args){

    while(1){

        send_heartbeat();
        vTaskDelay(pdMS_TO_TICKS(5000)); // 5 seconds
    }

}