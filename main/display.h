#ifndef DISPLAY_H
#define DISPLAY_H

#include "driver/spi_master.h"
#include "pin_map.h"

// Define SPI configuration
#define SPI_HOST      SPI2_HOST
#define DMA_CHANNEL   1

#define LVGL_LOCK_WAIT_TIME (3000 / portTICK_PERIOD_MS)

extern SemaphoreHandle_t xLVGLSemaphore;

void lvgl_unlock(void);
bool lvgl_lock(TickType_t timeout);

void run_display_task(void *pvParameter);
void spi_bus_init(void);
void display_spi_init(void);

#endif // DISPLAY_H
