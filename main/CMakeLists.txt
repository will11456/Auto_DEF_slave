idf_component_register(SRCS "heartbeat.c" "publish.c" "mqtt.c" "data.c" "modem.c" "main.c" "display.c" "uart.c" 
                    INCLUDE_DIRS ""
                    REQUIRES ui lvgl_esp32_drivers mqtt esp_timer json)
