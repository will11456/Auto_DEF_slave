#include "pin_map.h"
#include "main.h"
#include "modem.h"
#include "display.h"
#include "uart.h"
#include "data.h"
#include "mqtt.h"
#include "publish.h"
#include "certificates.h"

// Endpoint IDs:
// kHGxA0iiPn    - test board
//  E19917E90E406B5E  - DEF-0004
//                    - DEF-0005

static const char *TAG = "PUBLISH";

#define MQTT_TOPIC  "/up/7ho0anm3u0q0pa1pgwsxu53nw1xqboi9/id/E19917E90E406B5E" // get this from the device iot dashboard
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
        if (data.int_tank != 65535) {
            cJSON_AddNumberToObject(root, "Internal_Tank", data.int_tank);
        }
        if (data.ext_tank < 65500) {
        cJSON_AddNumberToObject(root, "External_Tank", data.ext_tank); 
        }
        if (data.fuel_tank < 65500) {       
        cJSON_AddNumberToObject(root, "Fuel_Tank", data.fuel_tank);
        }

        cJSON_AddNumberToObject(root, "Battery_volts", data.batt_volt);
        cJSON_AddNumberToObject(root, "Temperature", data.temp);
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

        sim7080_publish(MQTT_TOPIC, json_string);
        ESP_LOGW(TAG, "published data!");
        cJSON_Delete(root);
        free(json_string);

        vTaskDelay(60000 * MQTT_PUBLISH_FREQ / portTICK_PERIOD_MS);
    }




}




