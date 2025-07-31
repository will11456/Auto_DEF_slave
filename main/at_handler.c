#include "mqtt.h"
#include "pin_map.h"
#include "main.h"
#include "modem.h"
#include "uart.h"
#include "esp_log.h"
#include "freertos/task.h"
#include <string.h>
#include <stdio.h>
#include "display.h"
#include "at_handler.h"


static const char *TAG = "AT HANDLER";

static QueueHandle_t active_response_queue = NULL;

void at_handler_set_response_queue(QueueHandle_t queue) {
    active_response_queue = queue;
}


// Task: reads UART events, dispatches attribute URCs and AT responses
void rx_task(void *arg) {
    uint8_t data[256];
    char line[SIM7600_UART_BUF_SIZE];
    size_t line_idx = 0;
    bool in_rpc_block = false;

    ESP_LOGI(TAG, "RX task started");

    while (1) {
        int len = uart_read_bytes(SIM7600_UART_PORT, data, sizeof(data), pdMS_TO_TICKS(20));
        if (len > 0) {
            for (int i = 0; i < len; i++) {
                char c = (char)data[i];
                if (c == '\r') continue;

                if (c == '>') {
                    // Flush line before prompt
                    if (line_idx > 0) {
                        line[line_idx] = '\0';
                        if (!in_rpc_block &&
                            (strstr(line, "+QMTRECV:") || strstr(line, "RDY") ||
                             strstr(line, "SMS DONE") || strstr(line, "PB DONE"))) {
                            mqtt_handle_urc(line);
                        } else if (!in_rpc_block) {
                            if (active_response_queue) {
                                xQueueSend(active_response_queue, line, 0);
                            }
                        }
                        line_idx = 0;
                    }

                    // Send prompt line separately
                    if (!in_rpc_block) {
                        line[0] = '>';
                        line[1] = '\0';
                        if (active_response_queue) {
                            xQueueSend(active_response_queue, line, 0);
                        }
                    }
                    continue;
                }

                // End of line
                if (c == '\n' || line_idx >= SIM7600_UART_BUF_SIZE - 1) {
                    line[line_idx] = '\0';
                    if (line_idx > 0) {
                        if (strstr(line, "+CMQTTRXSTART:")) {
                            in_rpc_block = true;
                            xQueueSend(incoming_queue, line, 0);
                        } else if (strstr(line, "+CMQTTRXEND:")) {
                            xQueueSend(incoming_queue, line, 0);
                            in_rpc_block = false;
                        } else if (in_rpc_block) {
                            xQueueSend(incoming_queue, line, 0);
                        } else if (strstr(line, "+QMTRECV:") || strstr(line, "RDY") ||
                                   strstr(line, "SMS DONE") || strstr(line, "PB DONE")) {
                            xQueueSend(incoming_queue, line, 0);
                        } else {
                            if (active_response_queue) {
                                xQueueSend(active_response_queue, line, 0);
                            }
                        }
                    }
                    line_idx = 0;
                } else {
                    line[line_idx++] = c;
                }
            }
        }
    }
}



void tx_task(void *arg) {
    char command[SIM7600_UART_BUF_SIZE];

    ESP_LOGI("TX", "tx_task started");

    while (1) {
        if (xQueueReceive(at_send_queue, &command, portMAX_DELAY) == pdTRUE) {
            // Flush UART input to avoid stale junk
            
            sim7600_send_command(command);
        }
    }
}
