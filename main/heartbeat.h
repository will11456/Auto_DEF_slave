#ifndef MAIN_HEARTBEATP_H_
#define MAIN_HEARTBEAT_H_

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

void send_heartbeat(void);
void HeartbeatTask(void *args);

#endif /* MAIN_HEARTBEAT_H_ */