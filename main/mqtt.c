#include "esp_log.h"
#include "freertos/idf_additions.h"
#include "pin_map.h"
#include "main.h"
#include "modem.h"
#include "display.h"
#include "uart.h"
#include "data.h"
#include "mqtt.h"
#include "gnss.h"
#include "main.h"
#include "message_ids.h"
#include "publish.h"

#include <string.h>
#include <stdio.h>
#include <stdbool.h>
#include "esp_system.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "cJSON.h"

#define MQTT_CLIENT_IDX         0

#define TOPIC_ATTR_UPDATES      "v1/devices/me/attributes"
#define TOPIC_RPC_REQUEST_BASE  "v1/devices/me/rpc/request/"
#define TOPIC_ATTR_REQUEST_BASE "v1/devices/me/attributes/response/1"



static const char *TAG = "MQTT";


// Entry point: called from URC handler when +QMTRECV lines arrive
void mqtt_handle_urc(const char *urc) {
    static bool in_mqtt_block = false;
    static char current_topic[128] = {0};
    static char current_payload[512] = {0};

    ESP_LOGI(TAG, "Received URC: %s", urc);

    // Start of MQTT RX block
    if (strstr(urc, "+CMQTTRXSTART:")) {
        in_mqtt_block = true;
        current_topic[0] = '\0';
        current_payload[0] = '\0';
        return;
    }

    // Topic line (actual topic follows +CMQTTRXTOPIC)
    if (in_mqtt_block && strstr(urc, "+CMQTTRXTOPIC:")) {
        // Next line will contain the topic
        return;
    }

    // Payload line (after +CMQTTRXPAYLOAD)
    if (in_mqtt_block && strstr(urc, "+CMQTTRXPAYLOAD:")) {
        // Next line will contain JSON payload
        return;
    }

    // End of MQTT RX block
    if (strstr(urc, "+CMQTTRXEND:")) {
        in_mqtt_block = false;

        // Classify and dispatch
        if (strlen(current_topic) > 0 && strlen(current_payload) > 0) {
            if (strcmp(current_topic, TOPIC_ATTR_UPDATES) == 0) {
                handle_shared_attributes(current_payload);
            } 
            else if (strncmp(current_topic, TOPIC_RPC_REQUEST_BASE,
                               strlen(TOPIC_RPC_REQUEST_BASE)) == 0) {
                handle_rpc_request(current_topic, current_payload);
            } 
            else if (strncmp(current_topic, TOPIC_ATTR_REQUEST_BASE,
                                strlen(TOPIC_ATTR_REQUEST_BASE)) == 0) {
                handle_shared_attributes(current_payload);
            }
            
            else {
                ESP_LOGW(TAG, "Unhandled topic: %s with payload: %s",
                         current_topic, current_payload);
            }
        }
        return;
    }

    // If we're inside a message block but this line is not a header, it's data
    if (in_mqtt_block) {
        if (current_topic[0] == '\0') {
            // First data line after +CMQTTRXTOPIC is the topic
            strncpy(current_topic, urc, sizeof(current_topic) - 1);
        } else if (current_payload[0] == '\0' && strchr(urc, '{')) {
            // First line containing '{' after +CMQTTRXPAYLOAD is payload
            strncpy(current_payload, urc, sizeof(current_payload) - 1);
        }
    }
}


// Parse shared attributes JSON and store floats (2dp) in NVS
void handle_shared_attributes(const char *json) {
    ESP_LOGI(TAG, "Handling shared attributes: %s", json);

    cJSON *root = cJSON_Parse(json);
    if (!root) {
        ESP_LOGW(TAG, "Failed to parse shared attributes JSON");
        return;
    }

   

    cJSON *container = cJSON_GetObjectItem(root, "shared");
    if (!cJSON_IsObject(container)) {
        container = root;  // fallback to root if not wrapped
    }


    nvs_handle_t h;
    if (nvs_open(NS_ATTR, NVS_READWRITE, &h) == ESP_OK) {
        cJSON *item;
        float float_val;
        int int_val;

        // AUX_RANGE
        item = cJSON_GetObjectItem(container, KEY_AUX_RANGE);
        if (cJSON_IsNumber(item)) {
            float_val = (float)item->valuedouble;
            ESP_LOGI(TAG, "KEY_AUX_RANGE = %.2f", float_val);
            nvs_set_blob(h, KEY_AUX_RANGE, &float_val, sizeof(float_val));
        } else {
            ESP_LOGI(TAG, "KEY_AUX_RANGE not found in JSON");
        }

        // AUX_MAX
        item = cJSON_GetObjectItem(container, KEY_AUX_MAX);
        if (cJSON_IsNumber(item)) {
            float_val = (float)item->valuedouble;
            ESP_LOGI(TAG, "KEY_AUX_MAX = %.2f", float_val);
            nvs_set_blob(h, KEY_AUX_MAX, &float_val, sizeof(float_val));
        } else {
            ESP_LOGI(TAG, "KEY_AUX_MAX not found in JSON");
        }

        // EXT_RANGE
        item = cJSON_GetObjectItem(container, KEY_EXT_RANGE);
        if (cJSON_IsNumber(item)) {
            float_val = (float)item->valuedouble;
            ESP_LOGI(TAG, "KEY_EXT_RANGE = %.2f", float_val);
            nvs_set_blob(h, KEY_EXT_RANGE, &float_val, sizeof(float_val));
        } else {
            ESP_LOGI(TAG, "KEY_EXT_RANGE not found in JSON");
        }

        // EXT_MAX
        item = cJSON_GetObjectItem(container, KEY_EXT_MAX);
        if (cJSON_IsNumber(item)) {
            float_val = (float)item->valuedouble;
            ESP_LOGI(TAG, "KEY_EXT_MAX = %.2f", float_val);
            nvs_set_blob(h, KEY_EXT_MAX, &float_val, sizeof(float_val));
        } else {
            ESP_LOGI(TAG, "KEY_EXT_MAX not found in JSON");
        }

        // Fill time (int)
        item = cJSON_GetObjectItem(container, KEY_FILL_TIME);
        if (cJSON_IsNumber(item)) {
            int_val = (int)item->valuedouble;
            ESP_LOGI(TAG, "KEY_FILL_TIME = %d", int_val);
            nvs_set_i32(h, KEY_FILL_TIME, int_val);
        }

        // Purge time (int)
        item = cJSON_GetObjectItem(container, KEY_PURGE_TIME);
        if (cJSON_IsNumber(item)) {
            int_val = (int)item->valuedouble;
            ESP_LOGI(TAG, "KEY_PURGE_TIME = %d", int_val);
            nvs_set_i32(h, KEY_PURGE_TIME, int_val);
        }

        // Sleep timeout (int)
        item = cJSON_GetObjectItem(container, KEY_SLEEP_TIMEOUT);
        if (cJSON_IsNumber(item)) {
            int_val = (int)item->valuedouble;
            ESP_LOGI(TAG, "KEY_SLEEP_TIMEOUT = %d", int_val);
            nvs_set_i32(h, KEY_SLEEP_TIMEOUT, int_val);
        }

        // Min DEF Level (int)
        item = cJSON_GetObjectItem(container, KEY_MIN_DEF_LEVEL);
        if (cJSON_IsNumber(item)) {
            int_val = (int)item->valuedouble;
            ESP_LOGI(TAG, "KEY_MIN_DEF_LEVEL = %d", int_val);
            nvs_set_i32(h, KEY_MIN_DEF_LEVEL, int_val);
        }

        // Commit changes to NVS
        nvs_commit(h);
        nvs_close(h);

        // Retrieve integer values after saving
        int fillTime = 0, purgeTime = 0, sleepTimeout = 0, minDEFLevel = 0;
        if (nvs_open(NS_ATTR, NVS_READONLY, &h) == ESP_OK) {
            nvs_get_i32(h, KEY_FILL_TIME, &fillTime);
            nvs_get_i32(h, KEY_PURGE_TIME, &purgeTime);
            nvs_get_i32(h, KEY_SLEEP_TIMEOUT, &sleepTimeout);
            nvs_get_i32(h, KEY_MIN_DEF_LEVEL, &minDEFLevel);
        } else {
            ESP_LOGW(TAG, "Failed to open NVS for reading");
        }
        

        // Send the values in data0–data3
        ESP_LOGI(TAG, "Sending updated system message with fillTime: %d, purgeTime: %d, sleepTimeout: %d, minDEFLevel: %d",
                 fillTime, purgeTime, sleepTimeout, minDEFLevel);
        send_message(MSG_ID_SETTINGS, MSG_TYPE_DATA, (uint16_t)fillTime, (uint16_t)purgeTime, (uint16_t)sleepTimeout, (uint16_t)minDEFLevel);
        publish_data();

        
    } else {
        ESP_LOGW(TAG, "Failed to open NVS namespace: %s", NS_ATTR);
    }
    cJSON_Delete(root);
}


// Handle RPC calls (button controls) without NVS persistence
void handle_rpc_request(const char *topic, const char *json) {
    const char *req_id = strrchr(topic, '/');
    if (!req_id) return;
    req_id++;

    cJSON *root = cJSON_Parse(json);
    if (!root) return;
    cJSON *method = cJSON_GetObjectItem(root, "method");
    cJSON *params = cJSON_GetObjectItem(root, "params");

    cJSON *result = cJSON_CreateObject();
    bool success = false;

    if (cJSON_IsString(method)) {
        const char *method_str = method->valuestring;
        ESP_LOGI("RPC", "Received method: %s", method_str);

        if (strcmp(method_str, "Run") == 0) {
            // Handle "run"
            ESP_LOGI("RPC", "Handling RUN command");
            handle_run();
            success = true;

        } else if (strcmp(method_str, "Stop") == 0) {
            // Handle "stop"
            ESP_LOGI("RPC", "Handling STOP command");
            handle_stop();
            success = true;

        } else if (strcmp(method_str, "Reboot") == 0) {
            // Handle "stop"
            ESP_LOGI("RPC", "Handling STOP command");
            handle_reboot();
            success = true;

        } else {
            ESP_LOGW("RPC", "Unknown RPC method: %s", method_str);
            cJSON_AddStringToObject(result, "error", "unknown method");
        }
    } else {
        cJSON_AddStringToObject(result, "error", "invalid or missing method");
    }

    cJSON_AddBoolToObject(result, "success", success);
    //send_rpc_response(req_id, result);

    
    cJSON_Delete(result);
    cJSON_Delete(root);
}








//Function to retrieve values from flash
esp_err_t mqtt_get_aux_range(float *out_val) {
    nvs_handle_t h;
    esp_err_t err = nvs_open(NS_ATTR, NVS_READONLY, &h);
    if (err != ESP_OK) return err;
    size_t required_size = sizeof(float);
    err = nvs_get_blob(h, KEY_AUX_RANGE, out_val, &required_size);
    nvs_close(h);
    return err;
}

esp_err_t mqtt_get_aux_max(float *out_val) {
    nvs_handle_t h;
    esp_err_t err = nvs_open(NS_ATTR, NVS_READONLY, &h);
    if (err != ESP_OK) return err;
    size_t required_size = sizeof(float);
    err = nvs_get_blob(h, KEY_AUX_MAX, out_val, &required_size);
    nvs_close(h);
    return err;
}

esp_err_t mqtt_get_ext_range(float *out_val) {
    nvs_handle_t h;
    esp_err_t err = nvs_open(NS_ATTR, NVS_READONLY, &h);
    if (err != ESP_OK) return err;
    size_t required_size = sizeof(float);
    err = nvs_get_blob(h, KEY_EXT_RANGE, out_val, &required_size);
    nvs_close(h);
    return err;
}

esp_err_t mqtt_get_ext_max(float *out_val) {
    nvs_handle_t h;
    esp_err_t err = nvs_open(NS_ATTR, NVS_READONLY, &h);
    if (err != ESP_OK) return err;
    size_t required_size = sizeof(float);
    err = nvs_get_blob(h, KEY_EXT_MAX, out_val, &required_size);
    nvs_close(h);
    return err;
}

///////////////message sending//////////////
void int_to_hex_str(unsigned int num, char *str, int str_size) {
    snprintf(str, str_size, "%04X", num);
}

void send_message(int message_id, int message_type, uint16_t data0, uint16_t data1, uint16_t data2, uint16_t data3) {
    // Temporary buffers
    char output_buffer[23];  // 1 byte for type + 4 bytes for each data field + null terminator
    char message_id_str[5];  // 4 characters for 2 bytes in hex + null terminator
    char data_type = message_type ? '1' : '0';

    // Convert the message ID to a hex string
    int_to_hex_str(message_id, message_id_str, sizeof(message_id_str));

    // Format the message
    snprintf(output_buffer, 23, "%c%s#%04X%04X%04X%04X",
             data_type, message_id_str,
             data0, data1, data2, data3);

    if (xQueueSend(master_cmd_queue, output_buffer, portMAX_DELAY) != pdTRUE) {
        ESP_LOGE("UART_SEND", "Failed to send message to queue");
    }
}








void handle_run(void) {
    ESP_LOGW(TAG, "Run command received");
    send_message(MSG_ID_SYSTEM, MSG_TYPE_COMMAND, RUN , 0, 0, 0);
    ESP_LOGI(TAG, "Run command sent to master");
}

void handle_stop(void) {
    ESP_LOGW(TAG, "Stop command received");
    send_message(MSG_ID_SYSTEM, MSG_TYPE_COMMAND, STOP , 0, 0, 0);
    ESP_LOGI(TAG, "Stop command sent to master");
}


void handle_reboot(void) {
    ESP_LOGW(TAG, "Reboot command received");
    send_message(MSG_ID_SYSTEM, MSG_TYPE_COMMAND, RESET , 0, 0, 0);
    vTaskDelay(1000 / portTICK_PERIOD_MS);  // Delay to ensure message is sent
    esp_restart();  // Restart the ESP32
    ESP_LOGI(TAG, "Reboot command sent to master");
}



void mqtt_urc_task(void *param) {
    char line[SIM7600_UART_BUF_SIZE];

    ESP_LOGI("MQTT_URC", "MQTT URC task started");

    while (1) {
        // Block until a line is received from the queue
        if (xQueueReceive(incoming_queue, &line, portMAX_DELAY) == pdTRUE) {
            mqtt_handle_urc(line);
        }
    }
}