#include "freertos/idf_additions.h"
#include "pin_map.h"
#include "main.h"
#include "modem.h"
#include "display.h"
#include "uart.h"
#include "data.h"
#include "mqtt.h"
#include "publish.h"


#define UART_NUM UART_NUM_2


#define MQTT_USER   "07ac444748e9232b"
#define MQTT_PASS   "7ho0anm3u0q0pa1pgwsxu53nw1xqboi9"

#define BROKER_URL  "mqtt://mqtt.akenza.io"     
#define BROKER_PORT 1883

const char* root_ca = CA_CERT;

extern TaskHandle_t mqttTaskHandle; //externally refernced task handle

static const char* TAG = "MQTT";

esp_mqtt_client_handle_t mqtt_client;

static EventGroupHandle_t event_group = NULL;
static const int CONNECT_BIT = BIT0;
static const int GOT_DATA_BIT = BIT2;
static const int DISCONNECTED_BIT = BIT3;


/////////////CONFIG///////////////
#define ESP_MODEM_CONFIG() \
    {                                  \
        .dte_buffer_size = 512,        \
        .task_stack_size = 4096,       \
        .task_priority = 5,            \
        .uart_config = {               \
            .port_num = UART_NUM,                 \
            .data_bits = UART_DATA_8_BITS,          \
            .stop_bits = UART_STOP_BITS_1,          \
            .parity = UART_PARITY_DISABLE,          \
            .flow_control = ESP_MODEM_FLOW_CONTROL_NONE,\
            .source_clk = ESP_MODEM_DEFAULT_UART_CLK,   \
            .baud_rate = 115200,                    \
            .tx_io_num = 34,                        \
            .rx_io_num = 35,                        \
            .rts_io_num = 27,                       \
            .cts_io_num = 23,                       \
            .rx_buffer_size = 4096,                 \
            .tx_buffer_size = 512,                  \
            .event_queue_size = 30,                 \
       },                                           \
    }

typedef struct esp_modem_dte_config esp_modem_dte_config_t;


/////////////////FUNCTIONS //////////////////////////////////



static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    ESP_LOGD(TAG, "Event dispatched from event loop base=%s, event_id=%" PRIu32, base, event_id);
    esp_mqtt_event_handle_t event = event_data;
    esp_mqtt_client_handle_t client = event->client;
    int msg_id;
    switch ((esp_mqtt_event_id_t)event_id) {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");
        break;
    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "MQTT_EVENT_DISCONNECTED");
        xEventGroupSetBits(event_group, DISCONNECTED_BIT);
        break;

    case MQTT_EVENT_SUBSCRIBED:
        ESP_LOGI(TAG, "MQTT_EVENT_SUBSCRIBED, msg_id=%d", event->msg_id);
        break;
    case MQTT_EVENT_UNSUBSCRIBED:
        ESP_LOGI(TAG, "MQTT_EVENT_UNSUBSCRIBED, msg_id=%d", event->msg_id);
        break;
    case MQTT_EVENT_PUBLISHED:
        ESP_LOGI(TAG, "MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id);
        break;
    case MQTT_EVENT_DATA:
        ESP_LOGI(TAG, "MQTT_EVENT_DATA");
        printf("TOPIC=%.*s\r\n", event->topic_len, event->topic);
        printf("DATA=%.*s\r\n", event->data_len, event->data);
        xEventGroupSetBits(event_group, GOT_DATA_BIT);
        break;
    case MQTT_EVENT_ERROR:
        ESP_LOGI(TAG, "MQTT_EVENT_ERROR");
        break;
    default:
        ESP_LOGI(TAG, "MQTT other event id: %d", event->event_id);
        break;
    }
}

static void on_ppp_changed(void *arg, esp_event_base_t event_base,
                           int32_t event_id, void *event_data)
{
    ESP_LOGI(TAG, "PPP state changed event %" PRIu32, event_id);
    if (event_id == NETIF_PPP_ERRORUSER) {
        /* User interrupted event from esp-netif */
        esp_netif_t **p_netif = event_data;
        ESP_LOGI(TAG, "User interrupted event from netif:%p", *p_netif);
    }
}


static void on_ip_event(void *arg, esp_event_base_t event_base,
                        int32_t event_id, void *event_data)
{
    ESP_LOGD(TAG, "IP event! %" PRIu32, event_id);
    if (event_id == IP_EVENT_PPP_GOT_IP) {
        esp_netif_dns_info_t dns_info;

        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        esp_netif_t *netif = event->esp_netif;

        ESP_LOGI(TAG, "Modem Connect to PPP Server");
        ESP_LOGI(TAG, "~~~~~~~~~~~~~~");
        ESP_LOGI(TAG, "IP          : " IPSTR, IP2STR(&event->ip_info.ip));
        ESP_LOGI(TAG, "Netmask     : " IPSTR, IP2STR(&event->ip_info.netmask));
        ESP_LOGI(TAG, "Gateway     : " IPSTR, IP2STR(&event->ip_info.gw));
        esp_netif_get_dns_info(netif, 0, &dns_info);
        ESP_LOGI(TAG, "Name Server1: " IPSTR, IP2STR(&dns_info.ip.u_addr.ip4));
        esp_netif_get_dns_info(netif, 1, &dns_info);
        ESP_LOGI(TAG, "Name Server2: " IPSTR, IP2STR(&dns_info.ip.u_addr.ip4));
        ESP_LOGI(TAG, "~~~~~~~~~~~~~~");
        xEventGroupSetBits(event_group, CONNECT_BIT);

        ESP_LOGI(TAG, "GOT ip event!!!");
    } else if (event_id == IP_EVENT_PPP_LOST_IP) {
        ESP_LOGI(TAG, "Modem Disconnect from PPP Server");
    } else if (event_id == IP_EVENT_GOT_IP6) {
        ESP_LOGI(TAG, "GOT IPv6 event!");

        ip_event_got_ip6_t *event = (ip_event_got_ip6_t *)event_data;
        ESP_LOGI(TAG, "Got IPv6 address " IPV6STR, IPV62STR(event->ip6_info.ip));
    }
}


void mqtt_task(void *param){ 

    ESP_LOGW(TAG, "MQTT task started");

    /* Init and register system/core components */
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, ESP_EVENT_ANY_ID, &on_ip_event, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(NETIF_PPP_STATUS, ESP_EVENT_ANY_ID, &on_ppp_changed, NULL));

    /* Configure the PPP netif */
    esp_modem_dce_config_t dce_config = ESP_MODEM_DCE_DEFAULT_CONFIG(APN);
    esp_netif_config_t netif_ppp_config = ESP_NETIF_DEFAULT_PPP();
    esp_netif_t *esp_netif = esp_netif_new(&netif_ppp_config);
    assert(esp_netif);

    event_group = xEventGroupCreate();

    /* Configure the DTE */
    esp_modem_dte_config_t dte_config = ESP_MODEM_CONFIG();
    
    ESP_LOGW(TAG, "Initializing esp_modem for the SIM800 module...");
    esp_modem_dce_t *dce = esp_modem_new_dev(ESP_MODEM_DCE_SIM800, &dte_config, &dce_config, esp_netif);
    assert(dce);

    vTaskDelay(8000 / portTICK_PERIOD_MS);

    esp_err_t err;
    

        while (1) {

            // Put modem in data mode
            err = esp_modem_set_mode(dce, ESP_MODEM_MODE_DATA);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "esp_modem_set_mode(ESP_MODEM_MODE_DATA) failed with %d", err);
                ESP_LOGE(TAG, "No SIM?");
            }

            /* Wait for IP address with a timeout of 1 minute */
            ESP_LOGI(TAG, "Waiting for IP address");
            // Update GSM text colour
            lvgl_lock(LVGL_LOCK_WAIT_TIME);
            lv_obj_set_style_text_color(ui_GSMTextArea, lv_color_hex(0x40E0D0), LV_PART_MAIN | LV_STATE_DEFAULT);
            lvgl_unlock();

            EventBits_t bits = xEventGroupWaitBits(event_group, CONNECT_BIT, pdTRUE, pdFALSE, 60000 / portTICK_PERIOD_MS);

            if (bits & CONNECT_BIT) {
                // IP Acquired
                ESP_LOGW(TAG, "IP ACQUIRED");

                // Update GSM text colour
                lvgl_lock(LVGL_LOCK_WAIT_TIME);
                lv_obj_set_style_text_color(ui_GSMTextArea, lv_color_hex(0x00FF00), LV_PART_MAIN | LV_STATE_DEFAULT);
                lvgl_unlock();

                /* Config MQTT */
                esp_mqtt_client_config_t mqtt_config = {
                    .broker.address.uri = BROKER_URL,
                    .broker.address.port = BROKER_PORT,
                    .credentials.username = MQTT_USER, 
                    .credentials.authentication.password = MQTT_PASS
                };

                mqtt_client = esp_mqtt_client_init(&mqtt_config);
                esp_mqtt_client_register_event(mqtt_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
                esp_mqtt_client_start(mqtt_client);
                xEventGroupSetBits(systemEvents, MQTT_INIT);

                while(1){

                    ESP_LOGW(TAG, "Connected! Waiting for disconnection...");
                    EventBits_t bits = xEventGroupWaitBits(event_group, DISCONNECTED_BIT, pdTRUE, pdFALSE, portMAX_DELAY);
                    //indicate mqtt no longer up
                    xEventGroupClearBits(systemEvents, MQTT_INIT);

                    ESP_LOGW(TAG, "Disconnected! restarting modem routine");

                    //clear mqtt
                    esp_mqtt_client_stop(mqtt_client);
                    esp_mqtt_client_destroy(mqtt_client);
                    break;
                }
                
            } 
            // IP not acquired, restart modem
            ESP_LOGW(TAG, "IP not acquired, restarting modem...");
            esp_modem_destroy(dce);
            esp_netif_destroy(esp_netif);

            // Update GSM text colour
            lvgl_lock(LVGL_LOCK_WAIT_TIME);
            lv_obj_set_style_text_color(ui_GSMTextArea, lv_color_hex(0x40E0D0), LV_PART_MAIN | LV_STATE_DEFAULT);
            lvgl_unlock();

            sim800l_power_off();
            vTaskDelay(1000 / portTICK_PERIOD_MS); // Wait before trying again
            sim800l_power_on();

            vTaskDelay(10000 / portTICK_PERIOD_MS); // Wait before trying again


            /* Reinitialize modem and netif */
            esp_netif = esp_netif_new(&netif_ppp_config);
            assert(esp_netif);
            dce = esp_modem_new_dev(ESP_MODEM_DCE_SIM800, &dte_config, &dce_config, esp_netif);
            assert(dce);

            
            vTaskDelay(5000 / portTICK_PERIOD_MS);

                

            
        }

    }


