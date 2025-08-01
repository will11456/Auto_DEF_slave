#include "message_ids.h"
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

    send_message(MSG_ID_HEARTBEAT, MSG_TYPE_COMMAND, 0, 0, 0, 0);

}



void HeartbeatTask(void *args){

    while(1){

        send_heartbeat();
        vTaskDelay(pdMS_TO_TICKS(5000)); // 5 seconds
    }

}