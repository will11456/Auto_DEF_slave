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
    if (strstr(urc, "+QMTRECV:")) {
        const char *p_topic = strchr(urc, '"');
        if (!p_topic) return;
        p_topic++;
        const char *p_topic_end = strchr(p_topic, '"');
        if (!p_topic_end) return;
        size_t topic_len = p_topic_end - p_topic;
        char topic[64] = {0};
        memcpy(topic, p_topic, topic_len);

        const char *p_json = strchr(p_topic_end + 1, '{');
        if (!p_json) return;

        if (strcmp(topic, TOPIC_ATTR_UPDATES) == 0) {
            handle_shared_attributes(p_json);
        } else if (strncmp(topic, TOPIC_RPC_REQUEST_BASE, strlen(TOPIC_RPC_REQUEST_BASE)) == 0) {
            handle_rpc_request(topic, p_json);
        }
    }
}

// Parse shared attributes JSON and store floats (2dp) in NVS
static void handle_shared_attributes(const char *json) {
    cJSON *root = cJSON_Parse(json);
    if (!root) return;

    nvs_handle_t h;
    if (nvs_open(NS_ATTR, NVS_READWRITE, &h) == ESP_OK) {
        cJSON *item;
        float val;

        item = cJSON_GetObjectItem(root, KEY_AUX_RANGE);
        if (cJSON_IsNumber(item)) {
            val = (float)item->valuedouble;
            nvs_set_blob(h, KEY_AUX_RANGE, &val, sizeof(val));
        }

        item = cJSON_GetObjectItem(root, KEY_AUX_MAX);
        if (cJSON_IsNumber(item)) {
            val = (float)item->valuedouble;
            nvs_set_blob(h, KEY_AUX_MAX, &val, sizeof(val));
        }

        item = cJSON_GetObjectItem(root, KEY_EXT_RANGE);
        if (cJSON_IsNumber(item)) {
            val = (float)item->valuedouble;
            nvs_set_blob(h, KEY_EXT_RANGE, &val, sizeof(val));
        }

        item = cJSON_GetObjectItem(root, KEY_EXT_MAX);
        if (cJSON_IsNumber(item)) {
            val = (float)item->valuedouble;
            nvs_set_blob(h, KEY_EXT_MAX, &val, sizeof(val));
        }

        nvs_commit(h);
        nvs_close(h);
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
    send_at_command(buf,1000);
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
    // Format JSON
    char payload[128];
    snprintf(payload, sizeof(payload),
             "{\"%s\":%.2f,\"%s\":%.2f,\"%s\":%.2f,\"%s\":%.2f}",
             KEY_AUX_RANGE, auxRange,
             KEY_AUX_MAX,   auxMax,
             KEY_EXT_RANGE, extRange,
             KEY_EXT_MAX,   extMax);

    // Use helper to publish
    if (!sim7600_mqtt_publish(TOPIC_ATTR_UPDATES, payload)) {
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
