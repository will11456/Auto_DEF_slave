
#include "pin_map.h"
#include "main.h"
#include "modem.h"
#include "display.h"
#include "uart.h"
#include "data.h"
#include "mqtt.h"
#include "gnss.h"
#include "main.h"

#include "heartbeat.h"
#include "publish.h"
#include "message_ids.h"



TaskHandle_t displayTaskHandle = NULL;
TaskHandle_t modemTaskHandle = NULL;
TaskHandle_t dataTaskHandle = NULL;
TaskHandle_t uartTaskHandle = NULL;
TaskHandle_t mqttTaskHandle = NULL;
TaskHandle_t publishTaskHandle = NULL;


EventGroupHandle_t systemEvents;

sensor_data_t shared_sensor_data;
GNSSLocation shared_gnss_data;

SemaphoreHandle_t data_mutex;
SemaphoreHandle_t gnss_mutex;


static const char* TAG = "MAIN";

void GPIOInit(void)
{
    gpio_reset_pin(MODEM_PWR_KEY);
    gpio_reset_pin(RAIL_4V_EN);

    gpio_set_direction(MODEM_PWR_KEY, GPIO_MODE_OUTPUT);
    gpio_set_direction(RAIL_4V_EN, GPIO_MODE_OUTPUT);

    gpio_set_level(MODEM_PWR_KEY, 0);
    gpio_set_level(RAIL_4V_EN, 0);
}

void mqtt_nvs_init(void) {
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
}

void app_main(void)
{

    xLVGLSemaphore = xSemaphoreCreateMutex();
    data_mutex = xSemaphoreCreateMutex();
    gnss_mutex = xSemaphoreCreateMutex();

    systemEvents = xEventGroupCreate();

    mqtt_nvs_init();


    GPIOInit();
    vTaskDelay(100 / portTICK_PERIOD_MS);

    // Initialize UART
    uart_init();
    vTaskDelay(100 / portTICK_PERIOD_MS);

    //Enable 4V rail
    gpio_set_level(RAIL_4V_EN, 1);

    
////////////////////////////TASKS///////////////////////////////////

    // Start Tasks
    xTaskCreatePinnedToCore(run_display_task, "display", 2048*12, NULL, 3, &displayTaskHandle, 0);
    xTaskCreatePinnedToCore(uart_task, "uart_task", 2048*8, NULL, 2, &uartTaskHandle, 1);
    xTaskCreatePinnedToCore(data_task, "data_task", 2048*8, NULL, 1, &dataTaskHandle, 0);
    xTaskCreatePinnedToCore(modem_task, "modem_task", 2048*8, NULL, 1, NULL, 1);
    xTaskCreatePinnedToCore(monitor_task, "monitor_task", 2048*8, NULL, 1, NULL, 0);

    vTaskDelay(3000 / portTICK_PERIOD_MS);

    //xTaskCreate(gnss_task, "gnss_task", 4096, NULL, 5, NULL);
    xTaskCreate(publish_task, "publsh_task", 2048*8, NULL, 5, &publishTaskHandle);


    

}