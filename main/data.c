

#include "hal/uart_types.h"
#include "pin_map.h"
#include "main.h"
#include "modem.h"
#include "display.h"
#include "uart.h"
#include "data.h"
#include "mqtt.h"
#include "publish.h"
#include "certificates.h"
#include "message_ids.h"

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

        case MSG_ID_PUMP:
            handle_pump_message(decoded_msg);
            //ESP_LOGI(TAG, "recieved PUMP         message ID: %d", decoded_msg->message_id);
            break;

        case MSG_ID_COMMS:
            handle_comms_message(decoded_msg);
            //ESP_LOGI(TAG, "recieved COMM         message ID: %d", decoded_msg->message_id);
            break;

        case MSG_ID_24V_OUT:
            handle_24v_out_message(decoded_msg);
            //ESP_LOGI(TAG, "recieved 24V_OUT      message ID: %d", decoded_msg->message_id);
            break;

        case MSG_ID_BATT:
            handle_batt_message(decoded_msg);
            //ESP_LOGI(TAG, "recieved NPN_OUT      message ID: %d", decoded_msg->message_id);
            break;

        case MSG_ID_ANALOG_OUT:
            handle_analog_out_message(decoded_msg);
            //ESP_LOGI(TAG, "recieved ANALOG_OUT   message ID: %d", decoded_msg->message_id);
            break;

        case MSG_ID_420_INPUTS:
            handle_420_inputs_message(decoded_msg);
            //ESP_LOGI(TAG, "recieved 420_INPUTS   message ID: %d", decoded_msg->message_id);
            break;

        case MSG_ID_ANALOG_INPUTS:
            handle_analog_inputs_message(decoded_msg);
            //ESP_LOGI(TAG, "recieved ANALOG_IN    message ID: %d", decoded_msg->message_id);
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

    float int_float = (float)decoded_msg->data0;
    float ext_float = (float)decoded_msg->data1;
    float fuel_float = (float)decoded_msg->data2;

    //ESP_LOGW(TAG, "int: %f, ext: %f, fuel: %f", int_float, ext_float, fuel_float);

    //update the global data structure
    xSemaphoreTake(data_mutex, portMAX_DELAY);
    shared_sensor_data.int_tank = int_float;
    shared_sensor_data.ext_tank = ext_float;
    shared_sensor_data.fuel_tank = fuel_float;
    xSemaphoreGive(data_mutex);

    char int_buf[32];
    char ext_buf[32];
    char fuel_buf[32];

    snprintf(int_buf, 32, "%.0f", int_float);
    snprintf(ext_buf, 32, "%.0f", ext_float);
    snprintf(fuel_buf, 32, "%.0f", fuel_float);

    if (lvgl_lock(LVGL_LOCK_WAIT_TIME))
    {

        if (ext_float > 101){
            lv_textarea_set_text(ui_ExtTankTextArea, "-  ");
            lv_bar_set_value(ui_ExtTankBar, 0, LV_ANIM_OFF);

        }

        else if (ext_float < 101){
            lv_textarea_set_text(ui_ExtTankTextArea, ext_buf);
            lv_bar_set_value(ui_ExtTankBar, decoded_msg->data1, LV_ANIM_OFF);
        }
        
        if (int_float > 101){
            lv_textarea_set_text(ui_IntTankTextArea, "-  ");
            lv_bar_set_value(ui_IntTankBar, 0, LV_ANIM_OFF);

        }

        else if (int_float < 101){
            lv_textarea_set_text(ui_IntTankTextArea, int_buf);
            lv_bar_set_value(ui_IntTankBar, decoded_msg->data0, LV_ANIM_OFF);
        }
        
        if (fuel_float > 101){
            lv_textarea_set_text(ui_FuelTankTextArea, "-  ");
            lv_bar_set_value(ui_ExtTankBar1, 0, LV_ANIM_OFF);

        }

        else if (fuel_float < 101){
            lv_textarea_set_text(ui_FuelTankTextArea, fuel_buf);
            lv_bar_set_value(ui_ExtTankBar1, decoded_msg->data2, LV_ANIM_OFF);
        }
        
        
        
        lvgl_unlock();
    }


}


void handle_pump_message(const DecodedMessage *decoded_msg){
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
    

}

void handle_comms_message(const DecodedMessage *decoded_msg){

    
    if (lvgl_lock(LVGL_LOCK_WAIT_TIME)){
        if (decoded_msg->data0==CAN_INIT){
            
            lv_obj_set_style_text_color(ui_CANTextArea, lv_color_hex(0x40E0D0), LV_PART_MAIN | LV_STATE_DEFAULT);

        }
        else if (decoded_msg->data0==CAN_DATA){
            lv_obj_set_style_text_color(ui_CANTextArea, lv_color_hex(0x00FF00), LV_PART_MAIN | LV_STATE_DEFAULT);
        }
        else{
            lv_obj_set_style_text_color(ui_CANTextArea, lv_color_hex(0xFF0000), LV_PART_MAIN | LV_STATE_DEFAULT);
        }
        lvgl_unlock();
    }
}
    


void handle_24v_out_message(const DecodedMessage *decoded_msg){

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

void handle_analog_out_message(const DecodedMessage *decoded_msg){

    float volts_float = (float)decoded_msg->data0/1000.0;

    //ESP_LOGW(TAG, "msg %d", decoded_msg->data0);
        
    char volts_buf[32];

    snprintf(volts_buf, 32, "%.2f", volts_float);
    
    if (lvgl_lock(LVGL_LOCK_WAIT_TIME))
    {
        lv_textarea_set_text(ui_AnalogOutTextArea, volts_buf);
        lvgl_unlock();
    }


}

void handle_420_inputs_message(const DecodedMessage *decoded_msg){

    // // 4-20ma inputs 1
    // if (decoded_msg->data0 == 65535){
    //     if (lvgl_lock(LVGL_LOCK_WAIT_TIME))
    //     {
    //         lv_textarea_set_text(ui_FuelTankTextArea, "-  ");
    //         lvgl_unlock();
    //     }
    // }

    // else{
    //     float ma_1_float = (float)decoded_msg->data0/100.0;

    //     //ESP_LOGW(TAG, "msg %d", decoded_msg->data0);
            
    //     char ma_1_buf[32];

    //     snprintf(ma_1_buf, 32, "%.2f", ma_1_float);
        
    //     if (lvgl_lock(LVGL_LOCK_WAIT_TIME))
    //     {
    //         lv_textarea_set_text(ui_FuelTankTextArea, ma_1_buf);
    //         lvgl_unlock();
    //     }
    // }


    // 4-20ma inputs 2
//     if (decoded_msg->data1 == 65535){
//             if (lvgl_lock(LVGL_LOCK_WAIT_TIME))
//             {
//                 lv_textarea_set_text(ui_FuelTankTextArea, "-  ");
//                 lvgl_unlock();
//             }
//         }

//     else{
//         float ma_1_float = (float)decoded_msg->data1/100.0;
//         float fuel_level = (float)ma_1_float / (20 *100) * 1

//         //ESP_LOGW(TAG, "msg %d", decoded_msg->data1);
            
//         char ma_1_buf[32];

//         snprintf(ma_1_buf, 32, "%.2f", ma_1_float);
        
//         if (lvgl_lock(LVGL_LOCK_WAIT_TIME))
//         {
//             lv_textarea_set_text(ui_FuelTankTextArea, ma_1_buf);
//             lvgl_unlock();
//         }
//     }

}




void handle_analog_inputs_message(const DecodedMessage *decoded_msg){

    if (decoded_msg->data0 == 65535){
        if (lvgl_lock(LVGL_LOCK_WAIT_TIME))
        {
            lv_textarea_set_text(ui_AnalogIn1TextArea, "-  ");
            lvgl_unlock();
        }
    }

    else{

        float alg_1_float = (float)decoded_msg->data0/1000.0;

        //ESP_LOGW(TAG, "msg %d", decoded_msg->data0);
            
        char volts1_buf[32];

        snprintf(volts1_buf, 32, "%.2f", alg_1_float);
        
        if (lvgl_lock(LVGL_LOCK_WAIT_TIME))
        {
            lv_textarea_set_text(ui_AnalogIn1TextArea, volts1_buf);
            lvgl_unlock();
        }
    }

    
    if (decoded_msg->data1 == 65535){
        if (lvgl_lock(LVGL_LOCK_WAIT_TIME))
        {
            lv_textarea_set_text(ui_AnalogIn2TextArea, "-  ");
            lvgl_unlock();
        }
    }

    else{
        float alg_1_float = (float)decoded_msg->data1/1000.0;

        //ESP_LOGW(TAG, "msg %d", decoded_msg->data1);
            
        char volts2_buf[32];

        snprintf(volts2_buf, 32, "%.2f", alg_1_float);
        
        if (lvgl_lock(LVGL_LOCK_WAIT_TIME))
        {
            lv_textarea_set_text(ui_AnalogIn2TextArea, volts2_buf);
            lvgl_unlock();
        }
    }



}

void handle_pt1000_message(const DecodedMessage *decoded_msg){
    

    if (decoded_msg->data0 == 65535){
        if (lvgl_lock(LVGL_LOCK_WAIT_TIME))
    {
            lv_textarea_set_text(ui_PT1000TextArea, "-  ");
            lvgl_unlock();
    }
    }

    else{
        float temp_float = (float)decoded_msg->data0/10.0 - 50;
        
        char temp_buf[32];

        snprintf(temp_buf, 32, "%.1f", temp_float);
        
        if (lvgl_lock(LVGL_LOCK_WAIT_TIME))
        {
            lv_textarea_set_text(ui_PT1000TextArea, temp_buf);
            lvgl_unlock();
        }
    }
}

void handle_status_message(const DecodedMessage *decoded_msg){
    switch (decoded_msg->data0) {
        case PUMP_RUNNING:
            if (lvgl_lock(LVGL_LOCK_WAIT_TIME))
            {
                lv_obj_set_style_text_color(ui_ErrorTextArea, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
                lv_textarea_set_text(ui_ErrorTextArea, "Running");
                lvgl_unlock();
            }

            xSemaphoreTake(data_mutex, portMAX_DELAY);
            strcpy(shared_sensor_data.status, "Pump Running");
            //ESP_LOGW(TAG, "string: %s", shared_sensor_data.status);
            xSemaphoreGive(data_mutex);

            break;
        case PUMP_PURGING:
            if (lvgl_lock(LVGL_LOCK_WAIT_TIME))
            {
                lv_obj_set_style_text_color(ui_ErrorTextArea, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
                lv_textarea_set_text(ui_ErrorTextArea, "Purging..");
                lvgl_unlock();
            }
            xSemaphoreTake(data_mutex, portMAX_DELAY);
            strcpy(shared_sensor_data.status, "Pump Purging");
            xSemaphoreGive(data_mutex);
            break;
        case PUMP_STOPPED:
            if (lvgl_lock(LVGL_LOCK_WAIT_TIME))
            {
                lv_obj_set_style_text_color(ui_ErrorTextArea, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
                lv_textarea_set_text(ui_ErrorTextArea, "Stopped");
                lvgl_unlock();
            }
            xSemaphoreTake(data_mutex, portMAX_DELAY);
            strcpy(shared_sensor_data.status, "Pump Stopped");
            xSemaphoreGive(data_mutex);
            break;
        case PUMP_WAITING_TO_START:
            if (lvgl_lock(LVGL_LOCK_WAIT_TIME))
            {
                lv_obj_set_style_text_color(ui_ErrorTextArea, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
                lv_textarea_set_text(ui_ErrorTextArea, "Waiting");
                lvgl_unlock();
            }
            xSemaphoreTake(data_mutex, portMAX_DELAY);
            strcpy(shared_sensor_data.status, "Pump Waiting");
            xSemaphoreGive(data_mutex);
            break;
        case AUTO_ROUTINE_CHECKING:
            if (lvgl_lock(LVGL_LOCK_WAIT_TIME))
            {
                lv_obj_set_style_text_color(ui_ErrorTextArea, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
                lv_textarea_set_text(ui_ErrorTextArea, "Auto Run");
                lvgl_unlock();
            }
            xSemaphoreTake(data_mutex, portMAX_DELAY);
            strcpy(shared_sensor_data.status, "Auto: Running");
            xSemaphoreGive(data_mutex);
            break;
        case AUTO_ROUTINE_FILLING:
            if (lvgl_lock(LVGL_LOCK_WAIT_TIME))
            {
                lv_obj_set_style_text_color(ui_ErrorTextArea, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
                lv_textarea_set_text(ui_ErrorTextArea, "Filling");
                lvgl_unlock();
            }
            xSemaphoreTake(data_mutex, portMAX_DELAY);
            strcpy(shared_sensor_data.status, "Auto: Filling");
            xSemaphoreGive(data_mutex);
            break;
        case AUTO_ROUTINE_PURGING:
            if (lvgl_lock(LVGL_LOCK_WAIT_TIME))
            {
                lv_obj_set_style_text_color(ui_ErrorTextArea, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
                lv_textarea_set_text(ui_ErrorTextArea, "Purging");
                lvgl_unlock();
            }
            xSemaphoreTake(data_mutex, portMAX_DELAY);
            strcpy(shared_sensor_data.status, "Auto: Purging");
            xSemaphoreGive(data_mutex);
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


