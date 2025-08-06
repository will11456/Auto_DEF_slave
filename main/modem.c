#include "esp_system.h"
#include "freertos/idf_additions.h"
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
#include "at_handler.h"




static const char *TAG = "MODEM";

//mutex to proect UART
SemaphoreHandle_t at_mutex = NULL;  
SemaphoreHandle_t publish_mutex = NULL;           // Mutex to protect AT command access
SemaphoreHandle_t publish_trigger = NULL;      //semaphore to trigger publish task

QueueHandle_t incoming_queue;
QueueHandle_t at_send_queue;
QueueHandle_t at_resp_queue;

// ===== Configuration =====

#define APN              "eapn1.net"
#define APN_USER         "DynamicF"
#define APN_PASS         "DynamicF"

#define MQTT_BROKER      "eu.thingsboard.cloud"
#define MQTT_PORT        1883
#define MQTT_CLIENT_ID   "esp32_dev1"
#define MQTT_USERNAME    "dev"
#define MQTT_PASSWORD    "dev"

#define MQTT_TOPIC_PUB   "v1/devices/me/telemetry"       //topic for publishing telemetry data
#define MQTT_ATRR_SUBSCRIBE "v1/devices/me/attributes"   //subscribe to attributes
#define MQTT_RPC_REQUEST "v1/devices/me/rpc/request/+"   //subscribe to RPC requests

#define MQTT_ATTR_REQUEST "v1/devices/me/attributes/request/1"  //topic for requesting attributes on
#define MQTT_ATTR_RESPONSE "v1/devices/me/attributes/response/+" //topic for responding to attributes
#define ATTR_REQUEST_ID 1



// ===== UART & GPIO Setup =====

void sim7600_init(void) {

    if (at_mutex == NULL) {
        at_mutex = xSemaphoreCreateMutex();
    }

    if (publish_mutex == NULL) {
        publish_mutex = xSemaphoreCreateMutex();
    }

    uart_config_t uart_config = {
        .baud_rate = SIM7600_BAUD_RATE,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    uart_driver_install(SIM7600_UART_PORT, UART_BUF_SIZE, 0, EVENT_QUEUE_LEN, NULL, 0);
    uart_param_config(SIM7600_UART_PORT, &uart_config);
    uart_set_pin(SIM7600_UART_PORT, MODEM_TX, MODEM_RX, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);

    at_resp_queue = xQueueCreate(AT_RESP_QUEUE_LEN, sizeof(char[SIM7600_UART_BUF_SIZE]));
    if (at_resp_queue == NULL) {
        ESP_LOGE(TAG, "Failed to create at_resp_queue");
    }

    
    at_send_queue = xQueueCreate(EVENT_QUEUE_LEN, sizeof(char[SIM7600_UART_BUF_SIZE]));
    if (at_send_queue == NULL) {            
        ESP_LOGE(TAG, "Failed to create at_send_queue");
    }

    incoming_queue = xQueueCreate(EVENT_QUEUE_LEN, sizeof(char[SIM7600_UART_BUF_SIZE]));
    if (incoming_queue == NULL) {
        ESP_LOGE(TAG, "Failed to create incoming_queue");
    }

    xTaskCreatePinnedToCore(rx_task, "rx_task", 2048*2, NULL, 1, NULL, 0);
    xTaskCreatePinnedToCore(tx_task, "tx_task", 2048*2, NULL, 1, NULL, 0);
    xTaskCreatePinnedToCore(mqtt_urc_task, "mqtt_urc_task", 2048*4, NULL, 1, NULL, 0);



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


const char *send_at_command(const char *command, int timeout_ms) {
    static char response[SIM7600_UART_BUF_SIZE];
    response[0] = '\0';

    // Create a temporary queue for this AT command
    QueueHandle_t temp_resp_queue = xQueueCreate(10, sizeof(char[SIM7600_UART_BUF_SIZE]));
    if (!temp_resp_queue) {
        ESP_LOGE("AT", "Failed to create response queue");
        return NULL;
    }

    // Set this queue as the active receiver in the RX task
    at_handler_set_response_queue(temp_resp_queue);

    // Flush any old responses
    char drain[SIM7600_UART_BUF_SIZE];
    while (xQueueReceive(temp_resp_queue, drain, 0) == pdTRUE);

    ESP_LOGI("MODEM", ">> %s", command);

    char copy[SIM7600_UART_BUF_SIZE];
    snprintf(copy, sizeof(copy), "%s", command);

    if (xQueueSend(at_send_queue, &copy, pdMS_TO_TICKS(100)) != pdTRUE) {
        ESP_LOGW("AT", "❌ Failed to enqueue command: %s", command);
        vQueueDelete(temp_resp_queue);
        at_handler_set_response_queue(NULL);
        return NULL;
    }

    TickType_t start = xTaskGetTickCount();
    TickType_t wait = pdMS_TO_TICKS(timeout_ms);
    bool got_any_line = false;

    char line[SIM7600_UART_BUF_SIZE];
    while ((xTaskGetTickCount() - start) < wait) {
        if (xQueueReceive(temp_resp_queue, line, pdMS_TO_TICKS(300)) == pdTRUE) {
            got_any_line = true;

            // Append to response buffer
            strncat(response, line, SIM7600_UART_BUF_SIZE - strlen(response) - 2);
            strncat(response, "\n", SIM7600_UART_BUF_SIZE - strlen(response) - 1);

            if (strcmp(line, "OK") == 0 || strcmp(line, "ERROR") == 0 || strcmp(line, ">") == 0) {
                ESP_LOGI("MODEM", "<< [Complete Response]\n%s", response);
                break;
            }
        }
    }

    if (!got_any_line) {
        ESP_LOGW("AT", "❌ No response (timeout %d ms): %s", timeout_ms, command);
    }

    vQueueDelete(temp_resp_queue);
    at_handler_set_response_queue(NULL);  // Unhook

    return got_any_line ? response : NULL;
}



bool send_raw_uart_data(const char *data) {
    if (!at_send_queue || data == NULL) {
        ESP_LOGW("TX", "❌ Cannot send raw data: null input or uninitialized queue");
        return false;
    }

    char copy[SIM7600_UART_BUF_SIZE];
    snprintf(copy, sizeof(copy), "%s", data);

    if (xQueueSend(at_send_queue, &copy, pdMS_TO_TICKS(100)) != pdTRUE) {
        ESP_LOGW("TX", "❌ Failed to enqueue raw data: %s", data);
        return false;
    }

    //ESP_LOGI("TX", "📤 Raw data enqueued: %s", data);
    return true;
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

                    if (strcmp(ip, "0.0.0.0") != 0 && strlen(ip) > 0) {
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
    
    send_at_command("AT+CNMP=38", 2000);  // 38 = LTE only, int timeout_ms)
    send_at_command("AT+CMNB=3", 2000);    // Set LTE-only preference
    
    send_at_command("AT+CMEE=2", 3000);
    send_at_command("AT+CGATT=1", 20000);
    send_at_command("AT+CGMR", 1000);

 


    char cmd[128];

    snprintf(cmd, sizeof(cmd), "AT+CGDCONT=1,\"IP\",\"%s\"", APN);
    resp = send_at_command(cmd, 5000);

    snprintf(cmd, sizeof(cmd), "AT+CGAUTH=1,1,\"%s\",\"%s\"", APN_USER, APN_PASS);
    resp = send_at_command(cmd, 5000);
    
    send_at_command("AT+CGACT=1,1", 30000);

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

    //helper function
    bool request_all_shared_attributes(void) {
        cJSON *root = cJSON_CreateObject();
        bool result = false;

        if (!root) {
            ESP_LOGE("ATTR_REQ", "Failed to create JSON object");
            return false;
        }

        cJSON_AddStringToObject(root, "sharedKeys",
            "AuxTankMax,AuxTankRange,ExtTankMax,ExtTankRange,FillTime,PurgeTime,SleepTimeout,MinDEFLevel");

        char *json_str = cJSON_PrintUnformatted(root);
        if (json_str) {
            ESP_LOGI("ATTR_REQ", "Requesting shared attributes: %s", json_str);
            result = sim7600_mqtt_publish(MQTT_ATTR_REQUEST, json_str);  // This should return bool
            free(json_str);
        } else {
            ESP_LOGE("ATTR_REQ", "Failed to serialize JSON");
        }

        cJSON_Delete(root);
        return result;
    }



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


        
        //Subscribe to telemetry attributes
        bool success = true;

        if (!sim7600_mqtt_subscribe(MQTT_ATRR_SUBSCRIBE, 1)) {
            ESP_LOGE("MQTT", "Failed to subscribe to ATTR_SUBSCRIBE");
            success = false;
        }
        vTaskDelay(1000 / portTICK_PERIOD_MS);

        if (!sim7600_mqtt_subscribe(MQTT_RPC_REQUEST, 1)) {
            ESP_LOGE("MQTT", "Failed to subscribe to RPC_REQUEST");
            success = false;
        }
        vTaskDelay(1000 / portTICK_PERIOD_MS);

        if (!sim7600_mqtt_subscribe(MQTT_ATTR_RESPONSE, 1)) {
            ESP_LOGE("MQTT", "Failed to subscribe to ATTR_RESPONSE");
            success = false;
        }
        vTaskDelay(1000 / portTICK_PERIOD_MS);

        if (!request_all_shared_attributes()) {
            ESP_LOGE("MQTT", "Failed to request shared attributes");
            success = false;
        }
        vTaskDelay(1000 / portTICK_PERIOD_MS);

        if (!success) {
            ESP_LOGE("MQTT", "One or more MQTT setup steps failed — restarting ESP");
            vTaskDelay(2000 / portTICK_PERIOD_MS); // small delay before reset (optional)
            esp_restart();
        }
        
        xEventGroupSetBits(systemEvents, MQTT_INIT);
        

        lvgl_lock(LVGL_LOCK_WAIT_TIME);
        lv_obj_set_style_text_color(ui_GSMTextArea, lv_color_hex(0x00FF00), LV_PART_MAIN | LV_STATE_DEFAULT);
        lvgl_unlock();
        
        return true;
    }


    
    

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
        send_raw_uart_data(topic);
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
        if (!xSemaphoreTake(publish_mutex, pdMS_TO_TICKS(10000))) {
        ESP_LOGW(TAG, "Timeout waiting for publish mutex");
        return false;
    }
        char cmd[128];
        const char *resp;

        // Flush UART input to avoid stale junk
        uart_flush_input(SIM7600_UART_PORT);
        vTaskDelay(pdMS_TO_TICKS(50));

        // Step 1: Set topic
        snprintf(cmd, sizeof(cmd), "AT+CMQTTTOPIC=0,%d", strlen(topic));
        resp = send_at_command(cmd, 10000);
        if (!resp || !strstr(resp, ">")) {
            ESP_LOGE(TAG, "❌ Failed to set topic");
            //esp_restart(); // Restart if failed to set topic
            xSemaphoreGive(publish_mutex);
            return false;
        }

        
        send_raw_uart_data(topic);
        vTaskDelay(pdMS_TO_TICKS(100));

        // Step 2: Set payload
        snprintf(cmd, sizeof(cmd), "AT+CMQTTPAYLOAD=0,%d", strlen(payload));
        resp = send_at_command(cmd, 5000);
        if (!resp || !strstr(resp, ">")) {
            ESP_LOGE(TAG, "❌ Failed to set payload");
            xSemaphoreGive(publish_mutex);
            return false;
        }

        
        send_raw_uart_data(payload);
        vTaskDelay(pdMS_TO_TICKS(100));

        // Step 3: Publish
        resp = send_at_command("AT+CMQTTPUB=0,1,60", 10000);
        if (resp && strstr(resp, "OK")) {
            ESP_LOGI(TAG, "✅ Published %s to topic: %s", payload, topic);
            vTaskDelay(200 / portTICK_PERIOD_MS); // Allow time for publish to complete
            xSemaphoreGive(publish_mutex);
            return true;
        } else {
            ESP_LOGE(TAG, "❌ Publish failed");
            xSemaphoreGive(publish_mutex);
            return false;
        }
        
    }

// ===== Modem Functions =====


    void modem_update_signal_quality(void) {
        
        if (!xSemaphoreTake(publish_mutex, pdMS_TO_TICKS(10000))) {
        ESP_LOGW(TAG, "Timeout waiting for publish mutex");
        esp_restart(); // Restart if mutex not available
        }

        uart_flush(UART_NUM_2);

        const char *resp = send_at_command("AT+CSQ", 5000);
        vTaskDelay(10 / portTICK_PERIOD_MS); // Allow time for response to be processed
        

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
        xSemaphoreGive(publish_mutex);

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
        

        modem_update_signal_quality(); 

        
    }
}


