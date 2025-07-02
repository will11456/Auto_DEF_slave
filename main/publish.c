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


void publish_task(void *pvParameter){

    xEventGroupWaitBits(systemEvents, MQTT_INIT, pdFALSE, pdFALSE, portMAX_DELAY);
    ESP_LOGW(TAG, "publish task active");

    while(1){

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
        
        //Only publish the tank values if they are present
        
        cJSON_AddNumberToObject(root, "Internal_Tank", data.int_tank);
        
        cJSON_AddNumberToObject(root, "External_Tank", data.ext_tank); 
        cJSON_AddNumberToObject(root, "Aux_Tank", data.aux_tank);

        cJSON_AddNumberToObject(root, "PT1000", data.pt1000);
        

        cJSON_AddNumberToObject(root, "Battery_volts", data.batt_volt);

        cJSON_AddNumberToObject(root, "Temperature", data.temp);
        cJSON_AddNumberToObject(root, "Pressure", data.pres);
        cJSON_AddNumberToObject(root, "Humidity", data.rh);

        
        cJSON_AddStringToObject(root, "Mode", data.status);
        cJSON_AddStringToObject(root, "Status", data.mode);


        
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

        sim7600_mqtt_publish(MQTT_TOPIC_PUB, json_string);
        ESP_LOGW(TAG, "published data!");
        cJSON_Delete(root);
        free(json_string);

        vTaskDelay(60000 * MQTT_PUBLISH_FREQ / portTICK_PERIOD_MS);
    }




}




