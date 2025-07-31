#include "cJSON.h"
#include "gnss.h"
#include "pin_map.h"
#include "main.h"
#include "modem.h"
#include "display.h"
#include "uart.h"
#include "data.h"
#include "mqtt.h"
#include "publish.h"



static const char *TAG = "PUBLISH";

#define MQTT_PUBLISH_FREQ      1 //interval for publishing data in minutes



//functions//
void publish_data(void) {
    if (publish_trigger) {
        //ESP_LOGW(TAG, "Publishing data triggered externally");
        xSemaphoreGive(publish_trigger);
    }
}


//Task thread
void publish_task(void *pvParameter){

    publish_trigger = xSemaphoreCreateBinary();  // Create the publish trigger semaphore
    
    xEventGroupWaitBits(systemEvents, MQTT_INIT, pdFALSE, pdFALSE, portMAX_DELAY);
    ESP_LOGW(TAG, "publish task active");

    
    

    const TickType_t publish_interval = 60000 * MQTT_PUBLISH_FREQ / portTICK_PERIOD_MS; //timeout for publishing data

    while(1){


        // Wait for either external trigger or timeout for periodic publish
        xSemaphoreTake(publish_trigger, publish_interval);


        sensor_data_t data;
        GNSSLocation gnss_data;

        //Get sensor and GNSS data
        xSemaphoreTake(data_mutex, portMAX_DELAY);  // Lock the mutex before reading shared data
        data = shared_sensor_data;  // Copy the shared sensor data to local variable
        xSemaphoreGive(data_mutex);   // Release the mutex after reading

        xSemaphoreTake(gnss_mutex, portMAX_DELAY);  // Lock the mutex before reading GNSS data
        gnss_data = shared_gnss_data;  // Copy the GNSS data to local variable
        xSemaphoreGive(gnss_mutex);   // Release the mutex after reading


        // Get shared attribute data
        float auxRange = 0, auxMax = 0, extRange = 0, extMax = 0;
        int fillTime = 0, purgeTime = 0, sleepTimeout = 0, minDEFLevel = 0;

        nvs_handle_t h;
        if (nvs_open(NS_ATTR, NVS_READONLY, &h) == ESP_OK) {
            size_t fsize = sizeof(float);

            // Float values (stored as blobs)
            if (nvs_get_blob(h, KEY_AUX_RANGE, &auxRange, &fsize) != ESP_OK)
                ESP_LOGW(TAG, "Failed to get AUX_RANGE");

            fsize = sizeof(float);
            if (nvs_get_blob(h, KEY_AUX_MAX, &auxMax, &fsize) != ESP_OK)
                ESP_LOGW(TAG, "Failed to get AUX_MAX");

            fsize = sizeof(float);
            if (nvs_get_blob(h, KEY_EXT_RANGE, &extRange, &fsize) != ESP_OK)
                ESP_LOGW(TAG, "Failed to get EXT_RANGE");

            fsize = sizeof(float);
            if (nvs_get_blob(h, KEY_EXT_MAX, &extMax, &fsize) != ESP_OK)
                ESP_LOGW(TAG, "Failed to get EXT_MAX");

            // Integer values
            if (nvs_get_i32(h, KEY_FILL_TIME, &fillTime) != ESP_OK)
                ESP_LOGW(TAG, "Failed to get FILL_TIME");

            if (nvs_get_i32(h, KEY_PURGE_TIME, &purgeTime) != ESP_OK)
                ESP_LOGW(TAG, "Failed to get PURGE_TIME");

            if (nvs_get_i32(h, KEY_SLEEP_TIMEOUT, &sleepTimeout) != ESP_OK)
                ESP_LOGW(TAG, "Failed to get SLEEP_TIMEOUT");

            if (nvs_get_i32(h, KEY_MIN_DEF_LEVEL, &minDEFLevel) != ESP_OK)
                ESP_LOGW(TAG, "Failed to get MIN_DEF_LEVEL");

            nvs_close(h);
        } else {
            ESP_LOGE(TAG, "Failed to open NVS namespace: %s", NS_ATTR);
        }




         // Create a cJSON object
        cJSON *root = cJSON_CreateObject();
        if (root == NULL) {
        ESP_LOGE(TAG, "Failed to create cJSON object");
        return;
        }

        // Add data to the JSON object
        
        cJSON_AddNumberToObject(root, "Internal_Tank", data.int_tank);
        cJSON_AddNumberToObject(root, "External_Tank", data.ext_tank); 
        cJSON_AddNumberToObject(root, "Aux_Tank", data.aux_tank);
        cJSON_AddNumberToObject(root, "PT1000", data.pt1000);
        cJSON_AddNumberToObject(root, "Battery_volts", data.batt_volt);
        cJSON_AddNumberToObject(root, "Temperature", data.temp);
        cJSON_AddNumberToObject(root, "Pressure", data.pres);
        cJSON_AddNumberToObject(root, "Humidity", data.rh);
        cJSON_AddStringToObject(root, "Status", data.status);
        cJSON_AddStringToObject(root, "Mode", data.mode);
        cJSON_AddNumberToObject(root, "CSQ", data.csq);
        cJSON_AddBoolToObject(root, "CAN_Status", data.can_status);

        //Outputs
        cJSON_AddBoolToObject(root, "OUT1", data.out1);
        cJSON_AddBoolToObject(root, "OUT2", data.out2);
        cJSON_AddBoolToObject(root, "NPN1", data.npn1);
        cJSON_AddBoolToObject(root, "NPN2", data.npn2);

        //GNSS
        cJSON_AddNumberToObject(root, "Lat", gnss_data.latitude);
        cJSON_AddNumberToObject(root, "Lon", gnss_data.longitude);
        cJSON_AddNumberToObject(root, "Alt", gnss_data.altitude);
        cJSON_AddStringToObject(root, "Timestamp", gnss_data.timestamp);

        // Shared attributes
        cJSON_AddNumberToObject(root, "AuxTankRange", auxRange);
        cJSON_AddNumberToObject(root, "AuxTankMax", auxMax);
        cJSON_AddNumberToObject(root, "ExtTankRange", extRange);
        cJSON_AddNumberToObject(root, "ExtTankMax", extMax);
        
        //Settings
        cJSON_AddNumberToObject(root, "FillTime", fillTime);
        cJSON_AddNumberToObject(root, "PurgeTime", purgeTime);
        cJSON_AddNumberToObject(root, "SleepTimeout", sleepTimeout);
        cJSON_AddNumberToObject(root, "MinDEFLevel", minDEFLevel);



        
         // Convert the cJSON object to a string
        char *json_string = cJSON_PrintUnformatted(root);

        //ESP_LOGE(TAG, "%s", json_string);

        if (json_string == NULL) {
        ESP_LOGE(TAG, "Failed to print JSON string");
        cJSON_Delete(root);
        continue;
        
        }

        //verify the mqtt is up and running
        xEventGroupWaitBits(systemEvents, MQTT_INIT, pdFALSE, pdFALSE, portMAX_DELAY);
        

        sim7600_mqtt_publish(MQTT_TOPIC_PUB, json_string);
            
        ESP_LOGW(TAG, "published data!");
        cJSON_Delete(root);
        free(json_string);

        
    }


}




