#include "main.h"
#include "display.h"
#include "pin_map.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"

#include"ili9341.h"

#include "lvgl.h"
#include "lvgl_helpers.h"
#include "driver/ledc.h"

#include "ui.h"


#include "../managed_components\lvgl__lvgl\src\hal\lv_hal_disp.h"

#define DISP_BUF_SIZE 12800

static const char *TAG = "DISPLAY";

spi_device_handle_t spi;


/////////////////INIT FUNTIONS //////////////////


//////////////////FUNCTIONS///////////////////////



#define LV_TICK_PERIOD_MS 1

void lvgl_unlock(void);
bool lvgl_lock(TickType_t timeout);



void run_display_task(void *pvParameter);
static void lv_tick_task(void *arg);


// LVGL is single threaded only - we must maintain a semaphore for access control
SemaphoreHandle_t xLVGLSemaphore;


// reqeust a lock on the lvgl instance. If successful in given timeout, return true
bool lvgl_lock(TickType_t timeout)
{
    int64_t start = esp_timer_get_time();
    int64_t end = 0;
    if (xSemaphoreTake(xLVGLSemaphore, timeout)) // try and obtain the mutex in our given timeout
    {
        end = esp_timer_get_time();
        //ESP_LOGD(TAG, "Waited :%lld to obtain mutex", (end - start) / 1000);
        return true;
    }
    end = esp_timer_get_time();
    TaskHandle_t lgvlMutexOwner = xSemaphoreGetMutexHolder(xLVGLSemaphore);
    ESP_LOGE(TAG, "Failed to obtain lvgl lock in %lldms, owned by: %s", ((end - start) / 1000), pcTaskGetName(lgvlMutexOwner));
    return false;
}
// release lock on lvgl
void lvgl_unlock(void) 
{
    if (xLVGLSemaphore != NULL)
    {
        xSemaphoreGive(xLVGLSemaphore);
    }
}


///////////////////////MAIN TASK/////////////////////////////

void run_display_task(void *pvParameter)
{
   
    gpio_set_direction(LCD_LED, GPIO_MODE_OUTPUT);
    gpio_set_level(LCD_LED, 1);


    lv_init();

    /* Initialize SPI or I2C bus used by the drivers */
    lvgl_driver_init();


    static lv_disp_draw_buf_t disp_buf1;
    static lv_color_t buf1_1[DISP_BUF_SIZE];            //DISP_BUF_SIZE
    static lv_color_t buf1_2[DISP_BUF_SIZE];            //DISP_BUF_SIZE
    lv_disp_draw_buf_init(&disp_buf1, buf1_1, buf1_2, DISP_BUF_SIZE);

    /*Create a display*/
    static lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv); /*Basic initialization*/
    disp_drv.draw_buf = &disp_buf1;
    disp_drv.flush_cb = ili9341_flush;
    disp_drv.hor_res = 320;
    disp_drv.ver_res = 240;
    disp_drv.antialiasing = 1;
    //disp_drv.rotated = 0;
    

    lv_disp_t *disp = lv_disp_drv_register(&disp_drv);



    /* Create and start a periodic timer interrupt to call lv_tick_inc */
    const esp_timer_create_args_t periodic_timer_args = {
        .callback = &lv_tick_task,
        .name = "periodic_gui"};
    esp_timer_handle_t periodic_timer;
    ESP_ERROR_CHECK(esp_timer_create(&periodic_timer_args, &periodic_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(periodic_timer, LV_TICK_PERIOD_MS * 1000));


    
    

    // call our squareline init func
    if (lvgl_lock(LVGL_LOCK_WAIT_TIME))
    {
        ui_init();
        ESP_LOGW(TAG, "UI initialized.");
        //signal that the display is ready
        xEventGroupSetBits(systemEvents, DISPLAY_INIT);
        lvgl_unlock();
    }
    
    
    



    while (1)
    {

        /* Delay 1 tick (assumes FreeRTOS tick is 10ms */
        vTaskDelay(pdMS_TO_TICKS(10));
        // we must lock our lvgl instance before we try and use it
        if (lvgl_lock(LVGL_LOCK_WAIT_TIME))
        {
            lv_task_handler();
            lvgl_unlock();
         }
    }

    vTaskDelete(NULL);
}

// this task is set to run at an interval, and defines the LVGL tick rate. 
static void lv_tick_task(void *arg)
{
    (void)arg;
    if (lvgl_lock(LVGL_LOCK_WAIT_TIME))
    {
        lv_tick_inc(LV_TICK_PERIOD_MS);
        lvgl_unlock();
    }
}