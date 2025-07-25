

#include "freertos/idf_additions.h"
#include "hal/uart_types.h"
#include "pin_map.h"
#include "main.h"
#include "modem.h"
#include "display.h"
#include "uart.h"
#include "data.h"
#include "mqtt.h"
#include "publish.h"
#include "message_ids.h"
#include "config_values.h"

static const char *TAG = "Data";

// Queue and Semaphore Handles
QueueHandle_t message_queue;
SemaphoreHandle_t message_semaphore;



// Task function to monitor the queue and handle messages
void data_task(void *param) {

    DecodedMessage received_msg;


    while (1) {
        // Wait until the semaphore is given by the UART task
        if (xSemaphoreTake(message_semaphore, portMAX_DELAY) == pdTRUE) {
            // Try to receive a message from the queue
            if (xQueueReceive(message_queue, &received_msg, 0) == pdTRUE) {
                handle_message(&received_msg);
            }
        }
    }
}

// Function to decode a UART message
void decode_uart_message(const char *input_message, DecodedMessage *decoded_msg) {
    decoded_msg->message_type = input_message[0] - '0';
    sscanf(input_message + 1, "%4X", &decoded_msg->message_id);
    sscanf(input_message + 6, "%4hX%4hX%4hX%4hX",
           &decoded_msg->data0, &decoded_msg->data1,
           &decoded_msg->data2, &decoded_msg->data3);

}

// Function to handle messages based on their ID
void handle_message(const DecodedMessage *decoded_msg) {
    switch (decoded_msg->message_id) {
        case MSG_ID_BME280:
            handle_bme280_message(decoded_msg);
            //ESP_LOGI(TAG, "recieved BME280       message ID: %d", decoded_msg->message_id);
            break;
        
        case MSG_ID_TANK_LEVEL:
            handle_tank_message(decoded_msg);
            //ESP_LOGI(TAG, "recieved TANK LEVEL   message ID: %d", decoded_msg->message_id);
            break;

        case MSG_ID_MODE:
            handle_mode_message(decoded_msg);
            //ESP_LOGI(TAG, "recieved PUMP         message ID: %d", decoded_msg->message_id);
            break;

        case MSG_ID_COMMS:
            handle_comms_message(decoded_msg);
            //ESP_LOGI(TAG, "recieved COMM         message ID: %d", decoded_msg->message_id);
            break;

        case MSG_ID_24V_OUT:
            handle_output_message(decoded_msg);
            //ESP_LOGI(TAG, "recieved 24V_OUT      message ID: %d", decoded_msg->message_id);
            break;

        case MSG_ID_BATT:
            handle_batt_message(decoded_msg);
            //ESP_LOGI(TAG, "recieved NPN_OUT      message ID: %d", decoded_msg->message_id);
            break;

                
        case MSG_ID_PT1000:
            handle_pt1000_message(decoded_msg);
            //ESP_LOGI(TAG, "recieved PT1000       message ID: %d", decoded_msg->message_id);
            break;

        case MSG_ID_STATUS:
            handle_status_message(decoded_msg);
            //ESP_LOGI(TAG, "recieved ERROR        message ID: %d", decoded_msg->message_id);
            break;

        case MSG_ID_SYSTEM:
            handle_system_message(decoded_msg);
            break;


        default:
            //ESP_LOGW(TAG, "Unknown message ID: %d", decoded_msg->message_id);
            break;
    }
}

////////HANDLING FUNCTIONS///////////

void handle_bme280_message(const DecodedMessage *decoded_msg) {

    // ESP_LOGI(TAG, "Handling BME280 Message:");
    // ESP_LOGI(TAG, "Message Type: %d", decoded_msg->message_type);
    // ESP_LOGI(TAG, "Data0: %04X", decoded_msg->data0);
    // ESP_LOGI(TAG, "Data1: %04X", decoded_msg->data1);
    // ESP_LOGI(TAG, "Data2: %04X", decoded_msg->data2);
    // ESP_LOGI(TAG, "Data3: %04X", decoded_msg->data3);

    float temp_float = (float)decoded_msg->data0 / 100.0f;
    float pres_float = (float)decoded_msg->data1 / 100.0f;
    float hum_float = (float)decoded_msg->data2 / 100.0f;

    //Update the shared data structure - using mutex
    xSemaphoreTake(data_mutex, portMAX_DELAY);
    shared_sensor_data.temp = temp_float;
    shared_sensor_data.pres = pres_float;
    shared_sensor_data.rh = hum_float;
    xSemaphoreGive(data_mutex);

    
    char temp_buf[32];
    char pres_buf[32];
    char hum_buf[32];

    
    snprintf(temp_buf, 32, "%.1f", temp_float);
    snprintf(pres_buf, 32, "%.1f", pres_float);
    snprintf(hum_buf,  32, "%.1f", hum_float);
    
    if (lvgl_lock(LVGL_LOCK_WAIT_TIME))
    {
        lv_textarea_set_text(ui_BMETempTextArea, temp_buf);
        lv_textarea_set_text(ui_BMEPresTextArea, pres_buf);
        lv_textarea_set_text(ui_BMEHumTextArea, hum_buf);
        lvgl_unlock();
    }
}


void handle_tank_message(const DecodedMessage *decoded_msg) {

    int16_t int_tank_percent  = decoded_msg->data0;
    uint16_t ext_tank_ma = decoded_msg->data2;
    uint16_t aux_tank_ma  = decoded_msg->data1;
    //ESP_LOGW(TAG, "int: %d, ext: %d, aux: %d", int_tank_percent, ext_tank_ma, aux_tank_ma);

    float auxMax = 1.0f;
    float extMax = 1.0f;
    float auxRange = 1.0f;
    float extRange = 1.0f; // Default value if read fails

    //Get the values from NVS
    esp_err_t err = mqtt_get_aux_range(&auxRange);
    if (err != ESP_OK) {
        //ESP_LOGE(TAG, "Failed to get aux range from NVS: %s", esp_err_to_name(err));
        auxRange = 1.0f; // Default value if read fails
    }

    err = mqtt_get_ext_range(&extRange);
    if (err != ESP_OK) {
        //ESP_LOGE(TAG, "Failed to get ext range from NVS: %s", esp_err_to_name(err));
        extRange = 1.0f; // Default value if read fails
    }

    err = mqtt_get_aux_max(&auxMax);
    if (err != ESP_OK) {
        //ESP_LOGE(TAG, "Failed to get aux max from NVS: %s", esp_err_to_name(err));
        auxMax = 1.0f; // Default value if read fails
    }   

    err = mqtt_get_ext_max(&extMax);
    if (err != ESP_OK) {
        //ESP_LOGE(TAG, "Failed to get ext max from NVS: %s", esp_err_to_name(err));
        extMax = 1.0f; // Default value if read fails
    }
    
    char int_buf[32];
    char ext_buf[32];
    char aux_buf[32];

    //ESP_LOGI(TAG, "ext_tank_ma: %d, aux_tank_ma: %d", ext_tank_ma, aux_tank_ma);

    
    int16_t ext_tank_percent = ((float)((float)((ext_tank_ma - 400.0) * 1000 * extRange) / 1600)  /  1000 * extMax) * 100.0;
    int16_t aux_tank_percent = ((float)((float)((aux_tank_ma - 400.0) * 1000 * auxRange) / 1600)  /  1000 * auxMax) * 100.0;

    if (ext_tank_percent > 100){
        ext_tank_percent = -1;
    }

    if (aux_tank_percent > 100){
        aux_tank_percent = -1;
    }

    


    //update the global data structure
    xSemaphoreTake(data_mutex, portMAX_DELAY);
    shared_sensor_data.int_tank = int_tank_percent;
    shared_sensor_data.ext_tank = ext_tank_percent;
    shared_sensor_data.aux_tank = aux_tank_percent;
    xSemaphoreGive(data_mutex);

    //ESP_LOGI(TAG, "ext_tank_percent: %d, aux_tank_percent: %d", ext_tank_percent, aux_tank_percent);

    snprintf(int_buf, 32, "%d", int_tank_percent);
    snprintf(ext_buf, 32, "%d", ext_tank_percent);
    snprintf(aux_buf, 32, "%d", aux_tank_percent);




    if (lvgl_lock(LVGL_LOCK_WAIT_TIME))
    {

        if (ext_tank_percent == -1){
            lv_textarea_set_text(ui_ExtTankTextArea, "-  ");
            lv_bar_set_value(ui_ExtTankBar, 0, LV_ANIM_OFF);

        }

        else if (ext_tank_percent < 101){
            lv_textarea_set_text(ui_ExtTankTextArea, ext_buf);
            lv_bar_set_value(ui_ExtTankBar, ext_tank_percent, LV_ANIM_OFF);
            lv_obj_set_style_bg_color(ui_ExtTankBar, lv_color_hex(0x03A9F4), LV_PART_INDICATOR | LV_STATE_DEFAULT);

            if (ext_tank_percent <= 20){
                lv_obj_set_style_bg_color(ui_ExtTankBar, lv_color_hex(0xFF0000), LV_PART_INDICATOR | LV_STATE_DEFAULT);
            }
            
        }
        
        if (int_tank_percent == -1){
            lv_textarea_set_text(ui_IntTankTextArea, "-  ");
            lv_bar_set_value(ui_IntTankBar, 0, LV_ANIM_OFF);

        }

        else if (int_tank_percent < 101){
            lv_textarea_set_text(ui_IntTankTextArea, int_buf);
            lv_bar_set_value(ui_IntTankBar, int_tank_percent, LV_ANIM_OFF);
            lv_obj_set_style_bg_color(ui_IntTankBar, lv_color_hex(0x03A9F4), LV_PART_INDICATOR | LV_STATE_DEFAULT);

            if (int_tank_percent <= 20){
                lv_obj_set_style_bg_color(ui_IntTankBar, lv_color_hex(0xFF0000), LV_PART_INDICATOR | LV_STATE_DEFAULT);
            }
        }
        
        if (aux_tank_percent == -1){
            lv_textarea_set_text(ui_AuxTankTextArea, "-  ");
            lv_bar_set_value(ui_ExtTankBar1, 0, LV_ANIM_OFF);

        }

        else if (aux_tank_percent < 101){
            lv_textarea_set_text(ui_AuxTankTextArea, aux_buf);
            lv_bar_set_value(ui_ExtTankBar1, aux_tank_percent, LV_ANIM_OFF);
            lv_obj_set_style_bg_color(ui_ExtTankBar1, lv_color_hex(0x03A9F4), LV_PART_INDICATOR | LV_STATE_DEFAULT);

            if (aux_tank_percent <= 20){
                lv_obj_set_style_bg_color(ui_ExtTankBar1, lv_color_hex(0xFF0000), LV_PART_INDICATOR | LV_STATE_DEFAULT);
            }
        }
        
        
        
        lvgl_unlock();
    }


}


void handle_mode_message(const DecodedMessage *decoded_msg){
    if (decoded_msg->data0==1){
        if (lvgl_lock(LVGL_LOCK_WAIT_TIME)){
        lv_obj_set_style_text_color(ui_PumpMANTextArea, lv_color_hex(0xFF0000), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_color(ui_PumpAUTOTextArea, lv_color_hex(0x00FF00), LV_PART_MAIN | LV_STATE_DEFAULT);
        lvgl_unlock();
        }
        xSemaphoreTake(data_mutex, portMAX_DELAY);
        strcpy(shared_sensor_data.mode, "Auto");
        xSemaphoreGive(data_mutex);
    }
    if (decoded_msg->data0==0){
        if (lvgl_lock(LVGL_LOCK_WAIT_TIME)){
        lv_obj_set_style_text_color(ui_PumpMANTextArea, lv_color_hex(0x00FF00), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_color(ui_PumpAUTOTextArea, lv_color_hex(0xFF0000), LV_PART_MAIN | LV_STATE_DEFAULT);
        lvgl_unlock();
        }
        xSemaphoreTake(data_mutex, portMAX_DELAY);
        strcpy(shared_sensor_data.mode, "Manual");
        xSemaphoreGive(data_mutex);
    }
    
    publish_data(); // Publish the mode change to MQTT

}

void handle_comms_message(const DecodedMessage *decoded_msg){

    
    if (lvgl_lock(LVGL_LOCK_WAIT_TIME)){
        if (decoded_msg->data0==CAN_INIT){

            //Update the shared data structure - using mutex
            xSemaphoreTake(data_mutex, portMAX_DELAY);
            shared_sensor_data.can_status = false;
            xSemaphoreGive(data_mutex);
            
            lv_obj_set_style_text_color(ui_CANTextArea, lv_color_hex(0x40E0D0), LV_PART_MAIN | LV_STATE_DEFAULT);
            publish_data();
        }
        else if (decoded_msg->data0==CAN_DATA){

            xSemaphoreTake(data_mutex, portMAX_DELAY);
            shared_sensor_data.can_status = true;
            xSemaphoreGive(data_mutex);
            
            lv_obj_set_style_text_color(ui_CANTextArea, lv_color_hex(0x00FF00), LV_PART_MAIN | LV_STATE_DEFAULT);
            publish_data();
        }
        else if (decoded_msg->data0==CAN_ERROR){
            lv_obj_set_style_text_color(ui_CANTextArea, lv_color_hex(0xFF0000), LV_PART_MAIN | LV_STATE_DEFAULT);
        }
        lvgl_unlock();
    }
}
    


void handle_output_message(const DecodedMessage *decoded_msg){

}

void handle_batt_message(const DecodedMessage *decoded_msg){

        if (decoded_msg->data0 == 65535){
        if (lvgl_lock(LVGL_LOCK_WAIT_TIME))
        {
            lv_textarea_set_text(ui_BattVTextArea, "-  ");
            lvgl_unlock();
        }
    }

    else{

        float batt_float = (float)decoded_msg->data0/1000.0;

        //Update the shared data structure - using mutex
        xSemaphoreTake(data_mutex, portMAX_DELAY);
        shared_sensor_data.batt_volt = batt_float;
        xSemaphoreGive(data_mutex);

        //ESP_LOGW(TAG, "msg %d", decoded_msg->data0);
            
        char volts_buf[32];

        snprintf(volts_buf, 32, "%.1f", batt_float);
        
        if (lvgl_lock(LVGL_LOCK_WAIT_TIME))
        {
            lv_textarea_set_text(ui_BattVTextArea, volts_buf);
            lvgl_unlock();
        }
    }

}

void handle_pt1000_message(const DecodedMessage *decoded_msg){
    
    int16_t pt1000  = decoded_msg->data0;

    

    if (pt1000 == -1){
        if (lvgl_lock(LVGL_LOCK_WAIT_TIME))
    {
            lv_textarea_set_text(ui_PT1000TextArea, "-  ");
            lvgl_unlock();
    }

    xSemaphoreTake(data_mutex, portMAX_DELAY);
    shared_sensor_data.pt1000 = pt1000;
    xSemaphoreGive(data_mutex);
    }

    else{
        float temp_float = (float)pt1000/10.0 - 50;
        
        char temp_buf[32];

        snprintf(temp_buf, 32, "%.1f", temp_float);
        
        if (lvgl_lock(LVGL_LOCK_WAIT_TIME))
        {
            lv_textarea_set_text(ui_PT1000TextArea, temp_buf);
            lvgl_unlock();
        }

        xSemaphoreTake(data_mutex, portMAX_DELAY);
        shared_sensor_data.pt1000 = temp_float;
        xSemaphoreGive(data_mutex);
    }
    
    
}

void handle_status_message(const DecodedMessage *decoded_msg){
    switch (decoded_msg->data0) {
        case PUMP_RUNNING:
            if (lvgl_lock(LVGL_LOCK_WAIT_TIME))
            {
                lv_obj_set_style_text_color(ui_ErrorTextArea, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
                lv_obj_set_style_border_color(ui_ErrorPanel, lv_color_hex(0x003F5A), LV_PART_MAIN | LV_STATE_DEFAULT);

                lv_textarea_set_text(ui_ErrorTextArea, "Running");
                lvgl_unlock();
            }
            ESP_LOGW(TAG, "Pump Running");
            xSemaphoreTake(data_mutex, portMAX_DELAY);
            strcpy(shared_sensor_data.status, "Pump Running");
            //ESP_LOGW(TAG, "string: %s", shared_sensor_data.status);
            xSemaphoreGive(data_mutex);
            publish_data();

            break;
        case PUMP_PURGING:
            if (lvgl_lock(LVGL_LOCK_WAIT_TIME))
            {
                lv_obj_set_style_text_color(ui_ErrorTextArea, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
                lv_obj_set_style_border_color(ui_ErrorPanel, lv_color_hex(0x003F5A), LV_PART_MAIN | LV_STATE_DEFAULT);

                lv_textarea_set_text(ui_ErrorTextArea, "Purging..");
                lvgl_unlock();
            }
            xSemaphoreTake(data_mutex, portMAX_DELAY);
            strcpy(shared_sensor_data.status, "Pump Purging");
            xSemaphoreGive(data_mutex);
            publish_data();
            break;
        case PUMP_STOPPED:
            if (lvgl_lock(LVGL_LOCK_WAIT_TIME))
            {
                lv_obj_set_style_text_color(ui_ErrorTextArea, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
                lv_obj_set_style_border_color(ui_ErrorPanel, lv_color_hex(0x003F5A), LV_PART_MAIN | LV_STATE_DEFAULT);

                lv_textarea_set_text(ui_ErrorTextArea, "Stopped");
                lvgl_unlock();
            }
            xSemaphoreTake(data_mutex, portMAX_DELAY);
            strcpy(shared_sensor_data.status, "Pump Stopped");
            xSemaphoreGive(data_mutex);
            publish_data();
            break;
        case PUMP_WAITING_TO_START:
            if (lvgl_lock(LVGL_LOCK_WAIT_TIME))
            {
                lv_obj_set_style_text_color(ui_ErrorTextArea, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
                lv_obj_set_style_border_color(ui_ErrorPanel, lv_color_hex(0x003F5A), LV_PART_MAIN | LV_STATE_DEFAULT);

                lv_textarea_set_text(ui_ErrorTextArea, "Waiting");
                lvgl_unlock();
            }
            xSemaphoreTake(data_mutex, portMAX_DELAY);
            strcpy(shared_sensor_data.status, "Pump Waiting");
            xSemaphoreGive(data_mutex);
            publish_data();
            break;
        case AUTO_ROUTINE_CHECKING:
            if (lvgl_lock(LVGL_LOCK_WAIT_TIME))
            {
                lv_obj_set_style_text_color(ui_ErrorTextArea, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
                lv_obj_set_style_border_color(ui_ErrorPanel, lv_color_hex(0x003F5A), LV_PART_MAIN | LV_STATE_DEFAULT);

                lv_textarea_set_text(ui_ErrorTextArea, "Auto Run");
                lvgl_unlock();
            }
            xSemaphoreTake(data_mutex, portMAX_DELAY);
            strcpy(shared_sensor_data.status, "Auto: Running");
            xSemaphoreGive(data_mutex);
            publish_data();
            break;
        case AUTO_ROUTINE_FILLING:
            if (lvgl_lock(LVGL_LOCK_WAIT_TIME))
            {
                lv_obj_set_style_text_color(ui_ErrorTextArea, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
                lv_obj_set_style_border_color(ui_ErrorPanel, lv_color_hex(0x003F5A), LV_PART_MAIN | LV_STATE_DEFAULT);

                lv_textarea_set_text(ui_ErrorTextArea, "Filling");
                lvgl_unlock();
            }
            xSemaphoreTake(data_mutex, portMAX_DELAY);
            strcpy(shared_sensor_data.status, "Auto: Filling");
            xSemaphoreGive(data_mutex);
            publish_data();
            break;
        case AUTO_ROUTINE_PURGING:
            if (lvgl_lock(LVGL_LOCK_WAIT_TIME))
            {
                lv_obj_set_style_text_color(ui_ErrorTextArea, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
                lv_obj_set_style_border_color(ui_ErrorPanel, lv_color_hex(0x003F5A), LV_PART_MAIN | LV_STATE_DEFAULT);

                lv_textarea_set_text(ui_ErrorTextArea, "Purging");
                lvgl_unlock();
            }
            xSemaphoreTake(data_mutex, portMAX_DELAY);
            strcpy(shared_sensor_data.status, "Auto: Purging");
            xSemaphoreGive(data_mutex);
            publish_data();

            break;

        case PUMP_ERROR:
            if (lvgl_lock(LVGL_LOCK_WAIT_TIME))
            {
                lv_obj_set_style_border_color(ui_ErrorPanel, lv_color_hex(0xFF0000), LV_PART_MAIN | LV_STATE_DEFAULT);
                lvgl_unlock();
            }
            vTaskDelay(20/ portTICK_PERIOD_MS);
            publish_data();
            break;
        
        
        default:
            ESP_LOGW(TAG, "Unknown Status Message: %d", decoded_msg->data0);
            break;
    }

    switch (decoded_msg->data1) {
        case FILL_ERROR:
            if (lvgl_lock(LVGL_LOCK_WAIT_TIME))
            {
                lv_obj_set_style_text_color(ui_ErrorTextArea, lv_color_hex(0xFF0000), LV_PART_MAIN | LV_STATE_DEFAULT);
                lv_textarea_set_text(ui_ErrorTextArea, "Fill Error");
                lvgl_unlock();
            }
            xSemaphoreTake(data_mutex, portMAX_DELAY);
            strcpy(shared_sensor_data.status, "Fill Error");
            xSemaphoreGive(data_mutex);
            break;

        case COMM_ERROR:
            if (lvgl_lock(LVGL_LOCK_WAIT_TIME))
            {
                lv_obj_set_style_text_color(ui_ErrorTextArea, lv_color_hex(0xFF0000), LV_PART_MAIN | LV_STATE_DEFAULT);
                lv_textarea_set_text(ui_ErrorTextArea, "Comm Error");
                lvgl_unlock();
            }

            xSemaphoreTake(data_mutex, portMAX_DELAY);
            strcpy(shared_sensor_data.status, "Comm Error");
            xSemaphoreGive(data_mutex);
            break;
    }

}

void handle_system_message(const DecodedMessage *decoded_msg){

    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
    ESP_ERROR_CHECK(esp_sleep_enable_ext0_wakeup(UART1_RXD, 1));
    gpio_pulldown_en(UART1_RXD);
    
    ESP_LOGE(TAG, "Sleep message recieved, goodnight...");
    vTaskDelay( 3000 / portTICK_PERIOD_MS);
    //maybe send a 'am sleeping message to cloud'
    esp_deep_sleep_start();

}


