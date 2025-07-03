#ifndef SIM7600_H
#define SIM7600_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif


// ===== Configuration =====

#define APN              "eapn1.net"
#define APN_USER         "DynamicF"
#define APN_PASS         "DynamicF"

#define MQTT_BROKER      "eu.thingsboard.cloud"
#define MQTT_PORT        1883
#define MQTT_CLIENT_ID   "esp32_dev1"
#define MQTT_USERNAME    "dev"
#define MQTT_PASSWORD    "dev"

#define MQTT_TOPIC_PUB   "v1/devices/me/telemetry"

extern SemaphoreHandle_t at_mutex;
extern SemaphoreHandle_t publish_trigger; // Semaphore to trigger

//Mutex to protect UART access
#define MODEM_LOCK(timeout_ms) \
    (xSemaphoreTake(at_mutex, pdMS_TO_TICKS(timeout_ms)) == pdTRUE)

#define MODEM_UNLOCK() \
    xSemaphoreGive(at_mutex)


// === Public Modem Functions ===

void sim7600_init(void);
void sim7600_power_on(void);
void sim7600_power_off(void);

void sim7600_send_command(const char* command);
int  sim7600_read_response(char *buffer, uint32_t buffer_size, int timeout_ms);
const char* send_at_command(const char *command, int timeout_ms);

bool sim7600_wait_for_network(int attempts, int delay_ms);
bool sim7600_network_init(void);
bool sim7600_mqtt_connect(void);
bool sim7600_mqtt_publish(const char *topic, const char *message);

void modem_task(void *param);

#ifdef __cplusplus
}
#endif

#endif // SIM7600_H
