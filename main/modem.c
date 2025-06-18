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
#define APN_USER         "DynamicF"                     
#define APN_PASS         "DynamicF"                     

// MQTT CONFIG
#define MQTT_BROKER      "mqtt://mqtt.akenza.io"
#define MQTT_PORT        1883
#define MQTT_CLIENT_ID   "esp32sim7080"
#define MQTT_USERNAME    "07ac444748e9232b"                     
#define MQTT_PASSWORD    "7ho0anm3u0q0pa1pgwsxu53nw1xqboi9"                     

#define MQTT_TOPIC_PUB   "/up/7ho0anm3u0q0pa1pgwsxu53nw1xqboi9/id/E19917E90E406B5E"
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

void sim7080_send_command(const char *cmd) {
    uart_write_bytes(SIM7080_UART_PORT, cmd, strlen(cmd));
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


void send_at_command_test(const char *command) {
    char response[1024];

    ESP_LOGI(TAG, "Sending command: %s", command);
    sim7080_send_command(command);

    int len = sim7080_read_response(response, sizeof(response),3000);
    if (len > 0) {
        response[len] = '\0';
        ESP_LOGI(TAG, "Received response: %s", response);
    } else {
        ESP_LOGW(TAG, "No response received");
    }
}

bool sim7080_network_init(void) {
    char response[512];
    int rssi = 0;
    int attempt = 0;

    // 1. Wait for SIM to be ready
    ESP_LOGI("SIM7080", "Waiting for SIM (AT+CPIN?)...");
    for (int i = 0; i < 30; i++) {
        sim7080_send_command("AT+CPIN?");
        if (sim7080_read_response(response, sizeof(response), 3000) > 0 &&
            strstr(response, "+CPIN: READY")) {
            ESP_LOGI("SIM7080", "SIM is ready.");
            break;
        }
        ESP_LOGW("SIM7080", "SIM not ready yet...");
        vTaskDelay(pdMS_TO_TICKS(500));
    }

    // 2. Set APN
    char cmd[128];
    snprintf(cmd, sizeof(cmd), "AT+CGDCONT=1,\"IP\",\"%s\"", APN);
    sim7080_send_command(cmd);
    sim7080_read_response(response, sizeof(response), 3000);
    vTaskDelay(pdMS_TO_TICKS(500));

    // 3. Set LTE-only + Cat-M1 mode
    sim7080_send_command("AT+CNMP=38");  // LTE only
    sim7080_read_response(response, sizeof(response), 3000);
    vTaskDelay(pdMS_TO_TICKS(200));

    sim7080_send_command("AT+CMNB=1");   // Cat-M1 only
    sim7080_read_response(response, sizeof(response), 3000);
    vTaskDelay(pdMS_TO_TICKS(200));

    // 4. Set auto-operator selection
    sim7080_send_command("AT+COPS=0");
    sim7080_read_response(response, sizeof(response), 3000);
    vTaskDelay(pdMS_TO_TICKS(1000));

    // 5. Long operator scan
    ESP_LOGI("SIM7080", "Scanning for available networks (AT+COPS=?)...");
    sim7080_send_command("AT+COPS=?");
    if (sim7080_read_response(response, sizeof(response), 90000) > 0) {
        ESP_LOGI("SIM7080", "Operator scan result:\n%s", response);
    } else {
        ESP_LOGW("SIM7080", "No operators found or scan timed out.");
    }

    // 6. Wait for network registration
    ESP_LOGI("SIM7080", "Waiting for network registration...");
    while (true) {
        // Get signal strength
        sim7080_send_command("AT+CSQ");
        if (sim7080_read_response(response, sizeof(response), 3000) > 0) {
            char *csq = strstr(response, "+CSQ:");
            if (csq) {
                sscanf(csq, "+CSQ: %d", &rssi);
                if (rssi == 99) {
                    ESP_LOGW("SIM7080", "[%02d] Signal: Unknown (99)", attempt);
                } else {
                    ESP_LOGI("SIM7080", "[%02d] Signal strength (RSSI): %d", attempt, rssi);
                }
            }
        }

        // Get network registration
        sim7080_send_command("AT+CEREG?");
        if (sim7080_read_response(response, sizeof(response), 3000) > 0) {
            char *reg = strstr(response, "+CEREG:");
            if (reg) {
                ESP_LOGI("SIM7080", "[%02d] Registration status: %s", attempt, reg);

                if (strstr(reg, ",1") || strstr(reg, ",5")) {
                    ESP_LOGI("SIM7080", "✅ Modem registered to the network.");
                    return true;
                } else if (strstr(reg, ",3")) {
                    ESP_LOGE("SIM7080", "❌ Registration denied.");
                    return false;
                }
            }
        }

        attempt++;
        vTaskDelay(pdMS_TO_TICKS(2000));
    }

    return false; // should never hit
}




void sim7080_publish(const char *topic, const char *message) {
    char cmd[128];

    int msg_len = strlen(message);
    snprintf(cmd, sizeof(cmd), "AT+SMPUB=\"%s\",%d,1,0", topic, msg_len);

    send_at_command_test(cmd);
    vTaskDelay(pdMS_TO_TICKS(100));

    sim7080_send_command(message);
    uart_write_bytes(SIM7080_UART_PORT, "\x1A", 1); // Send Ctrl+Z

    ESP_LOGI("SIM7080", "Published to '%s': %s", topic, message);
}


void modem_task(void *param) {

    char cmd[128];
    sim7080_init();
    vTaskDelay(pdMS_TO_TICKS(1000));

    send_at_command_test("AT+COPS=0");   // Auto operator selection

    if (!sim7080_network_init()) {
        ESP_LOGE("SIM7080", "Network init failed.");
        vTaskDelete(NULL);
    }

   

    send_at_command_test("AT");

    // Set APN
    snprintf(cmd, sizeof(cmd), "AT+CGDCONT=1,\"IP\",\"%s\"", APN);
    send_at_command_test(cmd);

    // Set APN username and password (if provided)
    if (strlen(APN_USER) > 0) {
        snprintf(cmd, sizeof(cmd), "AT+CGAUTH=1,1,\"%s\",\"%s\"", APN_USER, APN_PASS);
        send_at_command_test(cmd);
    }

    // Attach to network
    send_at_command_test("AT+CGATT=1");
    vTaskDelay(pdMS_TO_TICKS(2000));

    // Activate PDP context
    send_at_command_test("AT+CGACT=1,1");
    vTaskDelay(pdMS_TO_TICKS(1000));

    // MQTT config
    snprintf(cmd, sizeof(cmd), "AT+SMCONF=\"URL\",\"%s\",%d", MQTT_BROKER, MQTT_PORT);
    send_at_command_test(cmd);

    snprintf(cmd, sizeof(cmd), "AT+SMCONF=\"CLIENTID\",\"%s\"", MQTT_CLIENT_ID);
    send_at_command_test(cmd);

    if (strlen(MQTT_USERNAME) > 0) {
        snprintf(cmd, sizeof(cmd), "AT+SMCONF=\"USERNAME\",\"%s\"", MQTT_USERNAME);
        send_at_command_test(cmd);
    }

    if (strlen(MQTT_PASSWORD) > 0) {
        snprintf(cmd, sizeof(cmd), "AT+SMCONF=\"PASSWORD\",\"%s\"", MQTT_PASSWORD);
        send_at_command_test(cmd);
    }

    // MQTT connect
    send_at_command_test("AT+SMCONN");
    vTaskDelay(pdMS_TO_TICKS(2000));

    

    

    while (1) {
        char incoming[512];
        int len = sim7080_read_response(incoming, sizeof(incoming), 3000);
        if (len > 0) {
            ESP_LOGI("MQTT", "Received: %s", incoming);
        }
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}

