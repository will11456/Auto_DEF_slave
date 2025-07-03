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

    xEventGroupWaitBits(systemEvents, MQTT_INIT, pdFALSE, pdFALSE, portMAX_DELAY);
    ESP_LOGW(TAG, "publish task active");

    
    publish_trigger = xSemaphoreCreateBinary();  // Create the publish trigger semaphore

    const TickType_t publish_interval = 60000 * MQTT_PUBLISH_FREQ / portTICK_PERIOD_MS; //timeout for publishing data

    while(1){


        // Wait for either external trigger or timeout for periodic publish
        xSemaphoreTake(publish_trigger, publish_interval);


        sensor_data_t data;

        // Lock the mutex before reading shared data
        xSemaphoreTake(data_mutex, portMAX_DELAY);

        // Copy the shared sensor data to local variable
        data = shared_sensor_data;

        // Release the mutex after reading
        xSemaphoreGive(data_mutex);

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

        cJSON_AddBoolToObject(root, "Output1", data.out1);
        cJSON_AddBoolToObject(root, "Output2", data.out2);
        cJSON_AddBoolToObject(root, "NPN1", data.npn1);
        cJSON_AddBoolToObject(root, "NPN2", data.npn2);


        
         // Convert the cJSON object to a string
        char *json_string = cJSON_PrintUnformatted(root);

        //ESP_LOGE(TAG, "%s", json_string);

        if (json_string == NULL) {
        ESP_LOGE(TAG, "Failed to print JSON string");
        cJSON_Delete(root);
        return;
        }

        //verify the mqtt is up and running
        xEventGroupWaitBits(systemEvents, MQTT_INIT, pdFALSE, pdFALSE, portMAX_DELAY);
        

        //use mutex to protect AT commands
        if (!MODEM_LOCK(3000)) {
        ESP_LOGW(TAG, "❌ Could not lock modem for MQTT publish");
            return;
        }

        sim7600_mqtt_publish(MQTT_TOPIC_PUB, json_string);
        MODEM_UNLOCK();

        ESP_LOGW(TAG, "published data!");
        cJSON_Delete(root);
        free(json_string);

        
    }


}




