#include "pin_map.h"
#include "main.h"
#include "modem.h"
#include "uart.h"
#include "esp_log.h"
#include "freertos/task.h"
#include <string.h>

#define SIM7080_UART_PORT UART_NUM_2
#define SIM7080_UART_BUF_SIZE 1024
#define SIM7080_BAUD_RATE 115200

static const char *TAG = "SIM7080";


// ==========================
// SIM7080G CONFIGURATION
// ==========================

#define APN              "eapn1.net"
#define APN_USER         "DynamicFF"                     
#define APN_PASS         "DynamicFF"                     

// MQTT CONFIG
#define MQTT_BROKER      "thingsboard.cloud"
#define MQTT_PORT        1883
#define MQTT_CLIENT_ID   "esp32_dev1"
#define MQTT_USERNAME    "mvfr2nhlu6io9yj8uy0e"                     
#define MQTT_PASSWORD    ""                     

#define MQTT_TOPIC_PUB   "v1/devices/me/telemetry"
#define MQTT_MESSAGE     "hello from SIM7080G"
#define MQTT_TOPIC_SUB   "test/topic"


void sim7080_init(void) {
    const uart_config_t uart_config = {
        .baud_rate = SIM7080_BAUD_RATE,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    uart_driver_install(SIM7080_UART_PORT, SIM7080_UART_BUF_SIZE, 0, 0, NULL, 0);
    uart_param_config(SIM7080_UART_PORT, &uart_config);
    uart_set_pin(SIM7080_UART_PORT, MODEM_TX, MODEM_RX, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);

    gpio_set_direction(MODEM_PWR_KEY, GPIO_MODE_OUTPUT);
    gpio_set_direction(RAIL_4V_EN, GPIO_MODE_OUTPUT);

    sim7080_power_off();  // Optional reset before init
    vTaskDelay(pdMS_TO_TICKS(1000));
    sim7080_power_on();
    vTaskDelay(pdMS_TO_TICKS(10000));

}

void sim7080_power_on(void) {
    ESP_LOGI(TAG, "Powering on SIM7080G");
    gpio_set_level(RAIL_4V_EN, 1);
    vTaskDelay(pdMS_TO_TICKS(100));

    gpio_set_level(MODEM_PWR_KEY, 0);
    vTaskDelay(pdMS_TO_TICKS(100));
    gpio_set_level(MODEM_PWR_KEY, 1);
    vTaskDelay(pdMS_TO_TICKS(500));
    gpio_set_level(MODEM_PWR_KEY, 0);
  // wait for modem boot
}

void sim7080_power_off(void) {
    ESP_LOGI(TAG, "Powering off SIM7080G");
    gpio_set_level(RAIL_4V_EN, 1);
    vTaskDelay(pdMS_TO_TICKS(1000));
}


void sim7080_send_command(const char* command) {
    // Send AT command to the SIM800L
    uart_write_bytes(SIM7080_UART_PORT, command, strlen(command));
    uart_write_bytes(SIM7080_UART_PORT, "\r\n", 2);
}

int sim7080_read_response(char *buffer, uint32_t buffer_size, int timeout_ms) {
    int total_len = 0;
    int len = 0;
    const int chunk_timeout_ms = 100;

    uint32_t start_time = xTaskGetTickCount() * portTICK_PERIOD_MS;

    while ((xTaskGetTickCount() * portTICK_PERIOD_MS - start_time) < timeout_ms && total_len < buffer_size - 1) {
        len = uart_read_bytes(SIM7080_UART_PORT, (uint8_t *)buffer + total_len,
                              buffer_size - 1 - total_len, pdMS_TO_TICKS(chunk_timeout_ms));

        if (len > 0) {
            total_len += len;
            buffer[total_len] = '\0';

            // Exit early on standard terminators
            if (strstr(buffer, "\r\nOK\r\n") || strstr(buffer, "\r\nERROR\r\n")) {
                break;
            }
        }
    }

    return total_len;
}


const char* send_at_command(const char *command, int timeout_ms) {
    static char response[1024];  // persists after return
    response[0] = '\0';
    int total_len = 0;

    ESP_LOGI(TAG, "Sending command: %s", command);
    sim7080_send_command(command);  // Sends AT+... with \r\n

    uint32_t start_time = xTaskGetTickCount() * portTICK_PERIOD_MS;

    while ((xTaskGetTickCount() * portTICK_PERIOD_MS - start_time) < timeout_ms &&
           total_len < sizeof(response) - 1) {

        int len = uart_read_bytes(SIM7080_UART_PORT,
                                  (uint8_t *)response + total_len,
                                  sizeof(response) - 1 - total_len,
                                  pdMS_TO_TICKS(100));

        if (len > 0) {
            total_len += len;
            response[total_len] = '\0';

            if (strstr(response, "\r\nOK\r\n") || strstr(response, "\r\nERROR\r\n")) {
                break;
            }
        } else {
            vTaskDelay(pdMS_TO_TICKS(50));
        }
    }

    if (total_len > 0) {
        ESP_LOGI(TAG, "Received response:\n%s", response);
        return response;
    } else {
        ESP_LOGW(TAG, "No response received");
        return NULL;
    }
}



bool sim7080_wait_for_sim_and_signal(int max_attempts, int delay_ms) {
    char response[256];
    bool sim_ready = false;
    bool signal_ok = false;
    int rssi = 0;

    ESP_LOGI("SIM7080", "Waiting for SIM and signal...");

    for (int attempt = 0; attempt < max_attempts; attempt++) {
        // SIM status
        sim7080_send_command("AT+CPIN?");
        if (sim7080_read_response(response, sizeof(response), 3000) > 0 &&
            strstr(response, "+CPIN: READY")) {
            sim_ready = true;
        } else {
            ESP_LOGW("SIM7080", "[%02d] SIM not ready", attempt + 1);
        }

        // Signal strength
        sim7080_send_command("AT+CSQ");
        if (sim7080_read_response(response, sizeof(response), 3000) > 0) {
            char *csq_ptr = strstr(response, "+CSQ:");
            if (csq_ptr) {
                int rssi_val = 0;
                if (sscanf(csq_ptr, "+CSQ: %d", &rssi_val) == 1) {
                    rssi = rssi_val;
                    ESP_LOGI("SIM7080", "[%02d] RSSI: %d", attempt + 1, rssi);
                    if (rssi != 99) {
                        signal_ok = true;
                    } else {
                        signal_ok = false;
                    }
                } else {
                    ESP_LOGW("SIM7080", "[%02d] Failed to parse RSSI", attempt + 1);
                }
            } else {
                ESP_LOGW("SIM7080", "[%02d] No +CSQ response", attempt + 1);
            }
        }

        if (sim_ready && signal_ok) {
            ESP_LOGI("SIM7080", "✅ SIM and signal ready");
            return true;
        }

        vTaskDelay(pdMS_TO_TICKS(delay_ms));
    }

    ESP_LOGE("SIM7080", "❌ SIM or signal not ready after %d attempts", max_attempts);
    return false;
}




bool sim7080_network_init(void) {

    char response[512];
    int rssi = 0;
    int attempt = 0;

    
    //Wait for SIM to be ready
    if (!sim7080_wait_for_sim_and_signal(120, 3000)) {
        return false;
        }


    send_at_command("AT+CGMR", 3000);
    
    

    send_at_command("AT+CNMP=38", 60000); //Only use LTE not GSM
    vTaskDelay(pdMS_TO_TICKS(1000));
    send_at_command("AT+CMNB=2", 60000); //use both LTE-M and NB-IOT
    vTaskDelay(pdMS_TO_TICKS(1000));

    send_at_command("AT+CMEE=2", 60000);

    //turn radio off for band config
    send_at_command("AT+CFUN=0", 3000);
    vTaskDelay(pdMS_TO_TICKS(2000));

    send_at_command("AT+CBANDCFG=\"NB-IOT\",3,8,20,28", 3000);    
    send_at_command("AT+CBANDCFG=\"CAT-M\",1,3,8,20,28", 3000);

    // send_at_command("AT+CBANDCFG=\"NB-IOT\",\"ALL\"", 3000);    
    // send_at_command("AT+CBANDCFG=\"CAT-M\",\"ALL\"", 3000);

    
    send_at_command("AT+CFUN=1", 3000);
    vTaskDelay(pdMS_TO_TICKS(5000));

    send_at_command("AT+CGDCONT?", 3000); 
    send_at_command("AT+CPSI?", 3000); 

        
    //operator scan
    //send_at_command("AT+COPS=?", 1800000);

    
    //force operator selection
    // send_at_command("AT+COPS=1,2,\"23410\",7", 180000); // Example for UK O2, change as needed
    // send_at_command("AT+CGNAPN=?", 60000);

    //Set auto-operator selection
    send_at_command("AT+COPS=0", 60000); // Example for UK O2, change as needed
    vTaskDelay(pdMS_TO_TICKS(1000));

   

    // Wait for network registration with CEREG parsing and RF reset on denial
    ESP_LOGW("SIM7080", "Waiting for network registration...");
    
    while (true) {
        sim7080_send_command("AT+CEREG?");
        int len = sim7080_read_response(response, sizeof(response), 3000);

        if (len > 0) {
            response[len] = '\0';

            char *reg_line = strstr(response, "+CEREG:");
            if (reg_line) {
                int n, stat;
                if (sscanf(reg_line, "+CEREG: %d,%d", &n, &stat) == 2) {
                    ESP_LOGI("SIM7080", "Registration status code: %d", stat);

                    if (stat == 1 || stat == 5) {
                        ESP_LOGI("SIM7080", "✅ Modem registered to the network.");
                        return true;
                    }
                }        
            }
        }
        vTaskDelay(pdMS_TO_TICKS(3000));
    }

    return false; // should never hit
}



void sim7080_publish(const char *topic, const char *message) {
    char cmd[128];
    int msg_len = strlen(message);

    // 1. Send the publish command with topic and length
    snprintf(cmd, sizeof(cmd), "AT+SMPUB=\"%s\",%d,1,0", topic, msg_len);
    const char *resp = send_at_command(cmd, 2000);

    // 2. Wait for '>' prompt (ready to receive payload)
    if (!resp || !strstr(resp, ">")) {
        ESP_LOGE("SIM7080", "MQTT publish prompt not received");
        return;
    }

    // 3. Send the actual payload and end with Ctrl+Z
    uart_write_bytes(SIM7080_UART_PORT, message, msg_len);
    uart_write_bytes(SIM7080_UART_PORT, "\x1A", 1); // Ctrl+Z to finish

    ESP_LOGI("SIM7080", "Published to '%s': %s", topic, message);
}




bool wait_for_ip(int timeout_ms) {
    const char *resp = NULL;
    uint32_t start_time = xTaskGetTickCount();
    const int check_interval_ms = 1000;

    while ((xTaskGetTickCount() - start_time) * portTICK_PERIOD_MS < timeout_ms) {
        resp = send_at_command("AT+CGPADDR=1", 2000);
        if (resp) {
            const char *line = strstr(resp, "+CGPADDR: 1,");
            if (line) {
                line += strlen("+CGPADDR: 1,");
                const char *end = strchr(line, '\r');
                if (end && end > line) {
                    char ip[32] = {0};
                    strncpy(ip, line, end - line);
                    ip[end - line] = '\0';

                    if (strcmp(ip, "0.0.0.0") != 0) {
                        ESP_LOGI("SIM", "Got IP: %s", ip);
                        return true;
                    }
                }
            }
        }
        vTaskDelay(pdMS_TO_TICKS(check_interval_ms));
    }

    ESP_LOGW("SIM", "Timeout waiting for IP");
    return false;
}



void modem_task(void *param) {

    char cmd[128];
    char response[512];
    sim7080_init();
    vTaskDelay(pdMS_TO_TICKS(1000));

    send_at_command("AT+CFUN=1", 3000);
    



    if (!sim7080_network_init()) {
        ESP_LOGE("SIM7080", "Network init failed.");
        vTaskDelete(NULL);
    }

    send_at_command("AT+CRESET", 3000);
    vTaskDelay(pdMS_TO_TICKS(10000));  // 

    send_at_command("AT+COPS?", 3000); //check network connected to
    

    send_at_command("AT+CGNAPN", 3000);


    //check bands
    send_at_command("AT+CBANDCFG?", 10000);

    
    ESP_LOGW(TAG, "🛠️ Setting up PDP profile using SIMCom commands...");

    
    // 1. Set PDP context for index 1
    send_at_command("AT+CGDCONT=1,\"IP\",\"eapn1.net\"", 10000);
    vTaskDelay(pdMS_TO_TICKS(1000));

    // // 2. Set PDP authentication for context 1
    send_at_command("AT+CGAUTH=1,1,\"DynamicF\",\"DynamicF\"", 8000);
    vTaskDelay(pdMS_TO_TICKS(1000));


    // // 3. Attach to network
    send_at_command("AT+CGATT=1", 8000);
    vTaskDelay(pdMS_TO_TICKS(2000));

    // 4. Activate PDP context 1
    send_at_command("AT+CGACT=1,1", 8000);
    vTaskDelay(pdMS_TO_TICKS(3000));

    // 5. Check PDP context status
    send_at_command("AT+CGACT?", 8000);
    vTaskDelay(pdMS_TO_TICKS(1000));

    send_at_command("AT+CGATT?", 10000);
    vTaskDelay(pdMS_TO_TICKS(1000));
    
    // 6. Check PDP context configuration
    send_at_command("AT+CGDCONT?", 10000);
    vTaskDelay(pdMS_TO_TICKS(1000));

    send_at_command("AT+CGAUTH?", 10000);
    vTaskDelay(pdMS_TO_TICKS(1000));


/////////////////////////////

    char response2[256];
    char ip[32] = {0};
    int attempt = 0;


    //wait for IP
    if (wait_for_ip(120000)) {
        ESP_LOGI("NET", "IP address assigned, ready for MQTT");
    } else {
        ESP_LOGE("NET", "Failed to get IP address");
    }
    
    //test ping
    send_at_command("AT+PING=\"8.8.8.8\"", 30000);
    
    /////////////////////////////////////

    //send_at_command("AT+SMDISC", 3000);

    // MQTT config
    snprintf(cmd, sizeof(cmd), "AT+SMCONF=\"URL\",\"%s\",%d", MQTT_BROKER, MQTT_PORT);
    send_at_command(cmd, 10000);
    sim7080_read_response(response, sizeof(response), 20000);
    vTaskDelay(pdMS_TO_TICKS(200));

    // Set MQTT client ID
    snprintf(cmd, sizeof(cmd), "AT+SMCONF=\"CLIENTID\",\"%s\"", MQTT_CLIENT_ID);
    send_at_command(cmd, 10000);
    vTaskDelay(pdMS_TO_TICKS(200));

    if (strlen(MQTT_USERNAME) > 0) {
        snprintf(cmd, sizeof(cmd), "AT+SMCONF=\"USERNAME\",\"%s\"", MQTT_USERNAME);
        send_at_command(cmd, 10000);
        vTaskDelay(pdMS_TO_TICKS(200));
    }

    if (strlen(MQTT_PASSWORD) > 0) {
        snprintf(cmd, sizeof(cmd), "AT+SMCONF=\"PASSWORD\",\"%s\"", MQTT_PASSWORD);
        send_at_command(cmd, 10000);
        vTaskDelay(pdMS_TO_TICKS(200));
    }


    send_at_command("AT+SMSSL=0", 10000);


    // MQTT connect
    send_at_command("AT+SMCONN", 30000);
    vTaskDelay(pdMS_TO_TICKS(10000));

    send_at_command("AT+SMSTATE?", 2000);

    sim7080_publish("v1/devices/me/telemetry", "{\"temp\":25.5}");

    

    

    while (1) {
       
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}

