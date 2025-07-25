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
#include "freertos/semphr.h"

#define SIM7600_UART_PORT UART_NUM_2
#define SIM7600_UART_BUF_SIZE 4096
#define SIM7600_BAUD_RATE 115200

#define EVENT_QUEUE_LEN    20
#define AT_RESP_QUEUE_LEN  20

static const char *TAG = "MODEM";

//mutex to proect UART
SemaphoreHandle_t at_mutex = NULL;             // Mutex to protect AT command access
SemaphoreHandle_t publish_trigger = NULL;      //semaphore to trigger publish task

// ===== Configuration =====

#define APN              "eapn1.net"
#define APN_USER         "DynamicF"
#define APN_PASS         "DynamicF"

#define MQTT_BROKER      "eu.thingsboard.cloud"
#define MQTT_PORT        1883
#define MQTT_CLIENT_ID   "esp32_dev1"
#define MQTT_USERNAME    "dev"
#define MQTT_PASSWORD    "dev"

#define MQTT_TOPIC_PUB   "v1/devices/me/telemetry"
#define MQTT_ATTR_PUBLISH "v1/devices/me/attributes"
#define MQTT_ATRR_SUBSCRIBE "v1/devices/me/attributes"
#define MQTT_RPC_REQUEST "v1/devices/me/rpc/request/+"

//queue setup
static QueueHandle_t uart_queue;  // Queue for unsolicited UART events
QueueHandle_t at_resp_queue;      // Queue for AT command responses



// ===== UART & GPIO Setup =====

void sim7600_init(void) {

    if (at_mutex == NULL) {
        at_mutex = xSemaphoreCreateMutex();
    }

    uart_config_t uart_config = {
        .baud_rate = SIM7600_BAUD_RATE,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    uart_driver_install(SIM7600_UART_PORT, UART_BUF_SIZE, 0, EVENT_QUEUE_LEN, &uart_queue, 0);
    uart_param_config(SIM7600_UART_PORT, &uart_config);
    uart_set_pin(SIM7600_UART_PORT, MODEM_TX, MODEM_RX, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);

    at_resp_queue = xQueueCreate(AT_RESP_QUEUE_LEN, sizeof(char[SIM7600_UART_BUF_SIZE]));
    if (at_resp_queue == NULL) {
        ESP_LOGE(TAG, "Failed to create at_resp_queue");
}

    gpio_set_direction(MODEM_PWR_KEY, GPIO_MODE_OUTPUT);
    gpio_set_direction(RAIL_4V_EN, GPIO_MODE_OUTPUT);

    sim7600_power_off();
    vTaskDelay(pdMS_TO_TICKS(1000));
    sim7600_power_on();
    vTaskDelay(pdMS_TO_TICKS(8000));
}

void sim7600_power_on(void) {
    ESP_LOGI(TAG, "Powering on SIM7600E");
    gpio_set_level(RAIL_4V_EN, 1);
    vTaskDelay(pdMS_TO_TICKS(200));

    gpio_set_level(MODEM_PWR_KEY, 0);
    vTaskDelay(pdMS_TO_TICKS(200));
    gpio_set_level(MODEM_PWR_KEY, 1);
    vTaskDelay(pdMS_TO_TICKS(5000));
}

void sim7600_power_off(void) {
    ESP_LOGI(TAG, "Powering off SIM7600E");
    gpio_set_level(RAIL_4V_EN, 0);
    vTaskDelay(pdMS_TO_TICKS(1000));
}

// ===== AT Communication =====

void sim7600_send_command(const char* command) {
    uart_write_bytes(SIM7600_UART_PORT, command, strlen(command));
    uart_write_bytes(SIM7600_UART_PORT, "\r\n", 2);
}

int sim7600_read_response(char *buffer, uint32_t buffer_size, int timeout_ms) {
    int total_len = 0;
    uint32_t start_time = xTaskGetTickCount() * portTICK_PERIOD_MS;

    while ((xTaskGetTickCount() * portTICK_PERIOD_MS - start_time) < timeout_ms &&
           total_len < buffer_size - 1) {
        int len = uart_read_bytes(SIM7600_UART_PORT, (uint8_t *)buffer + total_len,
                                  buffer_size - 1 - total_len, pdMS_TO_TICKS(100));
        if (len > 0) {
            total_len += len;
        }
    }

    buffer[(total_len < buffer_size) ? total_len : buffer_size - 1] = '\0';
    return total_len;
}

const char* send_at_command(const char *command, int timeout_ms) {
    static char response[SIM7600_UART_BUF_SIZE];
    response[0] = '\0';

    vTaskDelay(50 / portTICK_PERIOD_MS); // Small delay to allow previous commands to settle

    // Drain old responses
    char temp[SIM7600_UART_BUF_SIZE];
    while (xQueueReceive(at_resp_queue, temp, 0) == pdTRUE);

    sim7600_send_command(command);
    ESP_LOGI(TAG, ">> %s", command);

    TickType_t start = xTaskGetTickCount();
    TickType_t wait = pdMS_TO_TICKS(timeout_ms);
    bool got_any_line = false;

    while ((xTaskGetTickCount() - start) < wait) {
        if (xQueueReceive(at_resp_queue, temp, pdMS_TO_TICKS(500)) == pdTRUE) {
            // Log each line as it arrives
            //ESP_LOGI(TAG, "LINE << %s", temp);

            // Append to the cumulative response buffer
            strncat(response, temp, SIM7600_UART_BUF_SIZE - strlen(response) - 2);
            strncat(response, "\n", SIM7600_UART_BUF_SIZE - strlen(response) - 1);

            got_any_line = true;

            // Stop when OK/ERROR or '>' prompt is found
            if (strcmp(temp, "OK") == 0 || strcmp(temp, "ERROR") == 0 || strstr(temp, ">")) {
                ESP_LOGI(TAG, "<< [Complete Response]\n%s", response);
                return response;
            }
        }
    }

    if (got_any_line) {
        // Return partial response (no OK/ERROR seen)
        ESP_LOGW(TAG, "<< [Partial Response]\n%s", response);
        return response;
    }

    ESP_LOGW(TAG, "❌ No valid response (timeout %d ms) for: %s", timeout_ms, command);
    return NULL;
}






// ===== Network Init =====

bool sim7080_wait_for_sim_and_signal(int max_attempts, int delay_ms) {
    const char *resp;
    bool sim_ready = false;
    bool signal_ok = false;
    int rssi = 0;

    ESP_LOGI(TAG, "Waiting for SIM and signal...");
    vTaskDelay(3000 / portTICK_PERIOD_MS); // Allow time for SIM to initialize

    for (int attempt = 0; attempt < max_attempts; attempt++) {
        // SIM status
        resp = send_at_command("AT+CPIN?", 3000);
        if (resp && strstr(resp, "+CPIN: READY")) {
            sim_ready = true;
        } else {
            ESP_LOGW(TAG, "[%02d] SIM not ready", attempt + 1);
        }

        // Signal strength
        resp = send_at_command("AT+CSQ", 5000);
        if (resp) {
            char *csq_ptr = strstr(resp, "+CSQ:");
            if (csq_ptr) {
                int rssi_val = 0;
                if (sscanf(csq_ptr, "+CSQ: %d", &rssi_val) == 1) {
                    rssi = rssi_val;
                    ESP_LOGI(TAG, "[%02d] RSSI: %d", attempt + 1, rssi);
                    signal_ok = (rssi != 99);
                } else {
                    ESP_LOGW(TAG, "[%02d] Failed to parse RSSI", attempt + 1);
                }
            } else {
                ESP_LOGW(TAG, "[%02d] No +CSQ response", attempt + 1);
            }
        }

        if (sim_ready && signal_ok) {
            ESP_LOGI(TAG, "✅ SIM and signal ready");
            return true;
        }

        vTaskDelay(pdMS_TO_TICKS(delay_ms));
    }

    ESP_LOGE(TAG, "❌ SIM or signal not ready after %d attempts", max_attempts);
    return false;
}


bool sim7600_wait_for_ip(int timeout_ms) {
    const char *resp;
    char ip[32] = {0};

    ESP_LOGI(TAG, "🕒 Waiting for IP address (timeout: %d s)...", timeout_ms);
    uint32_t start_time = xTaskGetTickCount();
    const int interval_ms = 2000;

    while ((xTaskGetTickCount() - start_time) * portTICK_PERIOD_MS < timeout_ms) {
        resp = send_at_command("AT+CGPADDR=1", 20000);
        if (resp) {
            char *ptr = strstr(resp, "+CGPADDR: 1,");
            if (ptr) {
                ptr += strlen("+CGPADDR: 1,");
                const char *end = strchr(ptr, '\n');
                if (!end) end = strchr(ptr, '\0');
                if (end && (end - ptr) < sizeof(ip)) {
                    strncpy(ip, ptr, end - ptr);
                    ip[end - ptr] = '\0';

                    if (strcmp(ip, "0.0.0.0") != 0) {
                        ESP_LOGI(TAG, "✅ IP assigned: %s", ip);
                        return true;
                    } else {
                        ESP_LOGW(TAG, "⏳ IP still 0.0.0.0, retrying...");
                    }
                }
            }
        } else {
            ESP_LOGW(TAG, "❌ No response to AT+CGPADDR");
        }

        vTaskDelay(pdMS_TO_TICKS(interval_ms));
    }

    ESP_LOGE(TAG, "❌ Timeout waiting for valid IP address");
    return false;
}




bool sim7600_network_init(void) {
    const char *resp;

    //Check network registration
    int creg_attempts = 80;

    ESP_LOGI(TAG, "Checking network registration status...");

    for (int i = 0; i < creg_attempts; i++) {
        resp = send_at_command("AT+CREG?", 3000);
        if (resp) {
            char *ptr = strstr(resp, "+CREG:");
            if (ptr) {
                int n = 0, stat = 0;
                if (sscanf(ptr, "+CREG: %d,%d", &n, &stat) == 2) {
                    ESP_LOGI(TAG, "[%d] CREG: %d (0=not reg, 1=home, 5=roaming)", i + 1, stat);
                    if (stat == 1 || stat == 5) {
                        ESP_LOGI(TAG, "✅ Registered to network");
                        break;
                    }
                }
            }
        } else {
            ESP_LOGW(TAG, "[%d] No response to AT+CREG?", i + 1);
        }

        if (i == creg_attempts - 1) {
            ESP_LOGE(TAG, "❌ Network registration failed after %d attempts. Rebooting...", creg_attempts);
            return false;
        }

        vTaskDelay(pdMS_TO_TICKS(2000));
    }
    
    send_at_command("AT+CMEE=2", 3000);
    send_at_command("AT+CGATT=1", 20000);
    send_at_command("AT+CGMR", 1000);

 


    char cmd[128];

    snprintf(cmd, sizeof(cmd), "AT+CGDCONT=1,\"IP\",\"%s\"", APN);
    resp = send_at_command(cmd, 5000);

    snprintf(cmd, sizeof(cmd), "AT+CGAUTH=1,1,\"%s\",\"%s\"", APN_USER, APN_PASS);
    resp = send_at_command(cmd, 5000);
    
    send_at_command("AT+CGACT=1,1", 20000);

    send_at_command("AT+CGPADDR=1", 20000);


    if (!sim7600_wait_for_ip(60000)) {
        ESP_LOGE(TAG, "IP Assign failed");
        return false;
    }
    

    lvgl_lock(LVGL_LOCK_WAIT_TIME);
    lv_obj_set_style_text_color(ui_GSMTextArea, lv_color_hex(0x40E0D0), LV_PART_MAIN | LV_STATE_DEFAULT);
    lvgl_unlock();


    return true;
}

    // ===== MQTT =====

    //Setup
    bool sim7600_mqtt_cmqtt_setup(const char *broker, uint16_t port,
                                const char *client_id, const char *user, const char *pass) {
        char cmd[256];
        const char *resp;

        ESP_LOGI(TAG, "🚀 Starting MQTT service...");
        resp = send_at_command("AT+CMQTTSTART", 5000);
        if (!resp || !strstr(resp, "OK")) {
            ESP_LOGE(TAG, "❌ MQTT start failed");
            return false;
        }

        vTaskDelay(pdMS_TO_TICKS(300));

        ESP_LOGI(TAG, "🆔 Acquiring client...");
        snprintf(cmd, sizeof(cmd), "AT+CMQTTACCQ=0,\"%s\"", client_id);
        resp = send_at_command(cmd, 5000);
        if (!resp || !strstr(resp, "OK")) {
            ESP_LOGE(TAG, "❌ Client acquisition failed");
            return false;
        }

        vTaskDelay(pdMS_TO_TICKS(300));
        
        
        //send_at_command("AT+CGMR");
        
        ESP_LOGI(TAG, "🌐 Connecting to broker %s:%d...", broker, port);
        snprintf(cmd, sizeof(cmd), "AT+CMQTTCONNECT=0,\"tcp://%s:%d\",60,1,\"%s\",\"%s\"", broker, port, user, pass);
        resp = send_at_command(cmd,5000);
        if (!resp || !strstr(resp, "OK")) {
            ESP_LOGE(TAG, "❌ MQTT connect failed");
            return false;
        }
        //ESP_LOGI(TAG, "%s", resp);
        ESP_LOGI(TAG, "✅ MQTT connected to ThingsBoard");
        vTaskDelay(5000 / portTICK_PERIOD_MS);


        if (!MODEM_LOCK(3000)) {
        ESP_LOGW(TAG, "❌ Could not lock modem for MQTT publish");
        
        }
        //Subscribe to telemetry attributes
        sim7600_mqtt_subscribe(MQTT_ATRR_SUBSCRIBE, 1);
        vTaskDelay(500 / portTICK_PERIOD_MS);

        sim7600_mqtt_subscribe(MQTT_RPC_REQUEST, 1);
        vTaskDelay(500 / portTICK_PERIOD_MS);

        //request latest shared attributes on boot v1/devices/me/attributes/request/1
        

        MODEM_UNLOCK();

        
        xEventGroupSetBits(systemEvents, MQTT_INIT);

        lvgl_lock(LVGL_LOCK_WAIT_TIME);
        lv_obj_set_style_text_color(ui_GSMTextArea, lv_color_hex(0x00FF00), LV_PART_MAIN | LV_STATE_DEFAULT);
        lvgl_unlock();
        
        return true;
    }


    //Publish
    

    //Subscribe
    bool sim7600_mqtt_subscribe(const char *topic, int qos) {
    char cmd[64];
    const char *resp;
    int topic_len = strlen(topic);

    ESP_LOGI(TAG, "🔔 Subscribing to topic: %s (len=%d)", topic, topic_len);

    snprintf(cmd, sizeof(cmd), "AT+CMQTTSUBTOPIC=0,%d,%d", topic_len, qos);
    resp = send_at_command(cmd, 5000);

    if (!resp) {
        ESP_LOGE(TAG, "❌ No response from CMQTTSUBTOPIC");
        return false;
    }

    if (strstr(resp, "ERROR")) {
        ESP_LOGE(TAG, "❌ CMQTTSUBTOPIC returned ERROR: %s", resp);
        return false;
    }

    // Some firmwares don't show '>' but immediately expect topic data
    if (!strstr(resp, ">") && !strstr(resp, "OK")) {
        ESP_LOGW(TAG, "⚠️ Unexpected CMQTTSUBTOPIC response: %s", resp);
        return false;
    }

    // Send topic if '>' prompt expected
    if (strstr(resp, ">")) {
        uart_write_bytes(SIM7600_UART_PORT, topic, topic_len);
        vTaskDelay(pdMS_TO_TICKS(500));
    }

    resp = send_at_command("AT+CMQTTSUB=0", 5000);
    if (!resp || !strstr(resp, "OK")) {
        ESP_LOGE(TAG, "❌ Subscribe command failed: %s", resp ? resp : "NULL");
        return false;
    }

    ESP_LOGI(TAG, "✅ Subscribed to topic");
    return true;
}


    //Publish Function
    bool sim7600_mqtt_publish(const char *topic, const char *payload) {
        char cmd[128];
        const char *resp;

        // Step 1: Set topic
        snprintf(cmd, sizeof(cmd), "AT+CMQTTTOPIC=0,%d", strlen(topic));
        resp = send_at_command(cmd, 5000);
        if (!resp || !strstr(resp, ">")) {
            ESP_LOGE(TAG, "❌ Failed to set topic");
            return false;
        }
        uart_write_bytes(SIM7600_UART_PORT, topic, strlen(topic));
        vTaskDelay(pdMS_TO_TICKS(100));

        // Step 2: Set payload
        snprintf(cmd, sizeof(cmd), "AT+CMQTTPAYLOAD=0,%d", strlen(payload));
        resp = send_at_command(cmd, 5000);
        if (!resp || !strstr(resp, ">")) {
            ESP_LOGE(TAG, "❌ Failed to set payload");
            return false;
        }
        uart_write_bytes(SIM7600_UART_PORT, payload, strlen(payload));
        vTaskDelay(pdMS_TO_TICKS(100));

        // Step 3: Publish
        resp = send_at_command("AT+CMQTTPUB=0,1,60", 10000);
        if (resp && strstr(resp, "OK")) {
            ESP_LOGI(TAG, "✅ Published to topic: %s", topic);
            return true;
        } else {
            ESP_LOGE(TAG, "❌ Publish failed");
            return false;
        }
    }

// ===== Modem Functions =====


    void modem_update_signal_quality(void) {
        if (!MODEM_LOCK(3000)) {
            ESP_LOGW(TAG, "❌ Could not lock modem for signal check");
            return;
        }

        const char *resp = send_at_command("AT+CSQ", 5000);
        vTaskDelay(10 / portTICK_PERIOD_MS); // Allow time for response to be processed
        MODEM_UNLOCK();

        if (!resp) {
            ESP_LOGW(TAG, "⚠️ No response to AT+CSQ");
            return;
        }

        const char *csq = strstr(resp, "+CSQ:");
        if (!csq) {
            ESP_LOGW(TAG, "⚠️ +CSQ not found in response");
            return;
        }

        int rssi = 0, ber = 0;
        if (sscanf(csq, "+CSQ: %d,%d", &rssi, &ber) != 2) {
            ESP_LOGW(TAG, "⚠️ Failed to parse CSQ response");
            return;
        }

        // Update your global or shared telemetry structure
        shared_sensor_data.csq = rssi;

        ESP_LOGI(TAG, "📶 Signal updated: RSSI = %d, BER = %d", rssi, ber);
    }





// ===== Main Task =====

void modem_task(void *param) {
    sim7600_init();

    if (!sim7080_wait_for_sim_and_signal(500, 3000)) {
        ESP_LOGE(TAG, "Network discovery failed");
        esp_restart(); // Restart if SIM or signal not ready
    }

    if (!sim7600_network_init()) {
        ESP_LOGE(TAG, "Network init failed");
        esp_restart(); // Restart if network init failed
    }


    
    if (!sim7600_mqtt_cmqtt_setup(MQTT_BROKER, MQTT_PORT, MQTT_CLIENT_ID, MQTT_USERNAME, MQTT_PASSWORD)) {
        ESP_LOGE(TAG, "MQTT connection failed");
        esp_restart(); // Restart if MQTT connection failed
    }


    while (1) {

        vTaskDelay(pdMS_TO_TICKS(180000));
        uart_flush(UART_NUM_2);
        modem_update_signal_quality(); 
    }
}


// Task: reads UART events, dispatches attribute URCs and AT responses
void monitor_task(void *arg) {
    uint8_t data[256];
    char line[SIM7600_UART_BUF_SIZE];
    size_t line_idx = 0;

    bool in_rpc_block = false;  // Track if inside +CMQTTRXSTART -> +CMQTTRXEND block

    ESP_LOGI(TAG, "Monitor task started");

    while (1) {
        int len = uart_read_bytes(SIM7600_UART_PORT, data, sizeof(data), pdMS_TO_TICKS(20));
        if (len > 0) {
            for (int i = 0; i < len; i++) {
                char c = (char)data[i];
                if (c == '\r') continue;  // skip carriage returns

                // Special handling for '>' prompt
                if (c == '>') {
                    // Flush any accumulated line before the '>'
                    if (line_idx > 0) {
                        line[line_idx] = '\0';
                        //ESP_LOGI(TAG, "LINE << %s", line);  // Log every completed line

                        if (!in_rpc_block &&
                            (strstr(line, "+QMTRECV:") || strstr(line, "RDY") ||
                             strstr(line, "SMS DONE") || strstr(line, "PB DONE"))) {
                            mqtt_handle_urc(line);
                        } else if (!in_rpc_block) {
                            if (xQueueSend(at_resp_queue, line, 0) != pdTRUE) {
                                ESP_LOGW(TAG, "at_resp_queue full, dropping line: %s", line);
                            }
                        }
                        line_idx = 0;
                    }

                    // Push the standalone '>'
                    if (!in_rpc_block) {
                        line[0] = '>';
                        line[1] = '\0';
                        //ESP_LOGI(TAG, "LINE << >");  // Log standalone prompt
                        if (xQueueSend(at_resp_queue, line, 0) != pdTRUE) {
                            ESP_LOGW(TAG, "at_resp_queue full, dropping prompt: >");
                        }
                    }
                    continue;
                }

                // End of line
                if (c == '\n' || line_idx >= SIM7600_UART_BUF_SIZE - 1) {
                    line[line_idx] = '\0';

                    if (line_idx > 0) {
                        //ESP_LOGI(TAG, "LINE << %s", line);  // Log every completed line

                        // Handle the start of an RPC block
                        if (strstr(line, "+CMQTTRXSTART:")) {
                            in_rpc_block = true;
                            mqtt_handle_urc(line);
                        }
                        // Handle the end of an RPC block
                        else if (strstr(line, "+CMQTTRXEND:")) {
                            mqtt_handle_urc(line);
                            in_rpc_block = false;
                        }
                        // Lines inside RPC block
                        else if (in_rpc_block) {
                            mqtt_handle_urc(line);
                        }
                        // Regular URCs outside RPC blocks
                        else if (strstr(line, "+QMTRECV:") || strstr(line, "RDY") ||
                                 strstr(line, "SMS DONE") || strstr(line, "PB DONE")) {
                            mqtt_handle_urc(line);
                        }
                        // AT response line
                        else {
                            if (xQueueSend(at_resp_queue, line, 0) != pdTRUE) {
                                ESP_LOGW(TAG, "at_resp_queue full, dropping line: %s", line);
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


