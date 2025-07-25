#include "esp_log.h"
#include "pin_map.h"
#include "main.h"
#include "modem.h"
#include "display.h"
#include "uart.h"
#include "data.h"
#include "mqtt.h"
#include "gnss.h"
#include "main.h"

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

// NVS namespace and attribute keys
#define NS_ATTR                 "shared_attrs"
#define KEY_AUX_RANGE           "AuxTankRange"
#define KEY_AUX_MAX             "AuxTankMax"
#define KEY_EXT_RANGE           "ExtTankRange"
#define KEY_EXT_MAX             "ExtTankMax"

static const char *TAG = "MQTT";




// Forward declarations
static void handle_shared_attributes(const char *json);
static void handle_rpc_request(const char *topic, const char *json);
static void send_rpc_response(const char *req_id, cJSON *result);



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
            } else if (strncmp(current_topic, TOPIC_RPC_REQUEST_BASE,
                               strlen(TOPIC_RPC_REQUEST_BASE)) == 0) {
                handle_rpc_request(current_topic, current_payload);
            } else {
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
static void handle_shared_attributes(const char *json) {
    ESP_LOGI(TAG, "Handling shared attributes: %s", json);

    cJSON *root = cJSON_Parse(json);
    if (!root) {
        ESP_LOGW(TAG, "Failed to parse shared attributes JSON");
        return;
    }

    nvs_handle_t h;
    if (nvs_open(NS_ATTR, NVS_READWRITE, &h) == ESP_OK) {
        cJSON *item;
        float val;

        // AUX_RANGE
        item = cJSON_GetObjectItem(root, KEY_AUX_RANGE);
        if (cJSON_IsNumber(item)) {
            val = (float)item->valuedouble;
            ESP_LOGI(TAG, "KEY_AUX_RANGE = %.2f", val);
            nvs_set_blob(h, KEY_AUX_RANGE, &val, sizeof(val));
        } else {
            ESP_LOGI(TAG, "KEY_AUX_RANGE not found in JSON");
        }

        // AUX_MAX
        item = cJSON_GetObjectItem(root, KEY_AUX_MAX);
        if (cJSON_IsNumber(item)) {
            val = (float)item->valuedouble;
            ESP_LOGI(TAG, "KEY_AUX_MAX = %.2f", val);
            nvs_set_blob(h, KEY_AUX_MAX, &val, sizeof(val));
        } else {
            ESP_LOGI(TAG, "KEY_AUX_MAX not found in JSON");
        }

        // EXT_RANGE
        item = cJSON_GetObjectItem(root, KEY_EXT_RANGE);
        if (cJSON_IsNumber(item)) {
            val = (float)item->valuedouble;
            ESP_LOGI(TAG, "KEY_EXT_RANGE = %.2f", val);
            nvs_set_blob(h, KEY_EXT_RANGE, &val, sizeof(val));
        } else {
            ESP_LOGI(TAG, "KEY_EXT_RANGE not found in JSON");
        }

        // EXT_MAX
        item = cJSON_GetObjectItem(root, KEY_EXT_MAX);
        if (cJSON_IsNumber(item)) {
            val = (float)item->valuedouble;
            ESP_LOGI(TAG, "KEY_EXT_MAX = %.2f", val);
            nvs_set_blob(h, KEY_EXT_MAX, &val, sizeof(val));
        } else {
            ESP_LOGI(TAG, "KEY_EXT_MAX not found in JSON");
        }

        nvs_commit(h);
        nvs_close(h);
    } else {
        ESP_LOGW(TAG, "Failed to open NVS namespace: %s", NS_ATTR);
    }
    cJSON_Delete(root);
}


// Handle RPC calls (button controls) without NVS persistence
static void handle_rpc_request(const char *topic, const char *json) {
    const char *req_id = strrchr(topic, '/');
    if (!req_id) return;
    req_id++;

    cJSON *root   = cJSON_Parse(json);
    if (!root) return;
    cJSON *method = cJSON_GetObjectItem(root, "method");
    cJSON *params = cJSON_GetObjectItem(root, "params");

    cJSON *result = cJSON_CreateObject();

    if (cJSON_IsString(method)) {
        // Dispatch to user-defined action
        //control_action(method->valuestring, params);
        cJSON_AddBoolToObject(result, "success", true);
    } else {
        cJSON_AddBoolToObject(result, "success", false);
        cJSON_AddStringToObject(result, "error", "invalid method");
    }

    send_rpc_response(req_id, result);
    cJSON_Delete(result);
    cJSON_Delete(root);
}

// Publish RPC response
static void send_rpc_response(const char *req_id, cJSON *result) {
    char topic[64];
    char *payload = cJSON_PrintUnformatted(result);
    snprintf(topic, sizeof(topic), "v1/devices/me/rpc/response/%s", req_id);

    char buf[256];
    snprintf(buf, sizeof(buf), "AT+QMTPUB=%d,0,1,\"%s\",%s",
             MQTT_CLIENT_IDX, topic, payload);
    send_at_command(buf, 5000);
    free(payload);
}


// Publish stored attribute values from NVS as a single JSON via MQTT publish helper
void publish_stored_attributes(void) {
    float auxRange = 0, auxMax = 0, extRange = 0, extMax = 0;
    nvs_handle_t h;
    if (nvs_open(NS_ATTR, NVS_READONLY, &h) == ESP_OK) {
        size_t size = sizeof(float);
        nvs_get_blob(h, KEY_AUX_RANGE, &auxRange, &size);
        size = sizeof(float);
        nvs_get_blob(h, KEY_AUX_MAX,   &auxMax,   &size);
        size = sizeof(float);
        nvs_get_blob(h, KEY_EXT_RANGE, &extRange, &size);
        size = sizeof(float);
        nvs_get_blob(h, KEY_EXT_MAX,   &extMax,   &size);
        nvs_close(h);
    }
   

    // Create a cJSON object
    cJSON *attr = cJSON_CreateObject();
    if (attr == NULL) {
    ESP_LOGE(TAG, "Failed to create cJSON object");
    return;
    }

    cJSON_AddNumberToObject(attr, KEY_AUX_MAX, auxMax);
    cJSON_AddNumberToObject(attr, KEY_AUX_RANGE, auxRange);
    cJSON_AddNumberToObject(attr, KEY_EXT_MAX, extMax);
    cJSON_AddNumberToObject(attr, KEY_EXT_RANGE, extRange);

    char *json_string = cJSON_PrintUnformatted(attr);


    // Use helper to publish
    if (!sim7600_mqtt_publish(TOPIC_ATTR_UPDATES, json_string)) {
        ESP_LOGE(TAG, "Failed to publish stored attributes");
    }

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
