#ifndef MQTT_TASK_H
#define MQTT_TASK_H

#include <string.h>
#include "certificates.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "esp_netif.h"
#include "esp_netif_ppp.h"
#include "freertos/idf_additions.h"
#include "mqtt_client.h"
#include "esp_modem_api.h"
#include "esp_log.h"
#include "sdkconfig.h"
#include "freertos/event_groups.h"


#include "pin_map.h"
#include "main.h"
#include "modem.h"
#include "display.h"
#include "uart.h"
#include "data.h"
#include "mqtt.h"

#define TEST_TOPIC "test_topic"
#define TEST_DATA "test_data"
#define BROKER_URL "0fb45afeccc841aeb4c7605669e78e79.s1.eu.hivemq.cloud"

#define MQTT_USER   "07ac444748e9232b"
#define MQTT_PASS   "7ho0anm3u0q0pa1pgwsxu53nw1xqboi9"

extern esp_mqtt_client_handle_t mqtt_client;

// Flow control settings based on the config
#if defined(CONFIG_EXAMPLE_FLOW_CONTROL_NONE)
#define EXAMPLE_FLOW_CONTROL ESP_MODEM_FLOW_CONTROL_NONE
#elif defined(CONFIG_EXAMPLE_FLOW_CONTROL_SW)
#define EXAMPLE_FLOW_CONTROL ESP_MODEM_FLOW_CONTROL_SW
#elif defined(CONFIG_EXAMPLE_FLOW_CONTROL_HW)
#define EXAMPLE_FLOW_CONTROL ESP_MODEM_FLOW_CONTROL_HW
#endif

// Function declarations
#ifdef CONFIG_EXAMPLE_MODEM_DEVICE_CUSTOM
esp_err_t esp_modem_get_time(esp_modem_dce_t *dce_wrap, char *p_time);
#endif

#if defined(CONFIG_EXAMPLE_SERIAL_CONFIG_USB)
#include "esp_modem_usb_c_api.h"
#include "esp_modem_usb_config.h"
void usb_terminal_error_handler(esp_modem_terminal_error_t err);
#define CHECK_USB_DISCONNECTION(event_group) \
if ((xEventGroupGetBits(event_group) & USB_DISCONNECTED_BIT) == USB_DISCONNECTED_BIT) { \
    esp_modem_destroy(dce); \
    continue; \
}
#else
#define CHECK_USB_DISCONNECTION(event_group)
#endif



// Event handler functions
void static mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data);
void static on_ppp_changed(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data);
void static on_ip_event(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data);

// Task to handle MQTT connection and communication
void mqtt_task(void *param);

#endif // MQTT_TASK_H
