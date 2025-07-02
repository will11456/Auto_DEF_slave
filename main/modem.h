#ifndef SIM7600_H
#define SIM7600_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// UART port and config
#define SIM7600_UART_PORT      UART_NUM_2
#define SIM7600_UART_BUF_SIZE  1024
#define SIM7600_BAUD_RATE      115200

// MQTT configuration (can also be moved to main config header)
#define MQTT_BROKER      "thingsboard.cloud"
#define MQTT_PORT        1883
#define MQTT_CLIENT_ID   "esp32_dev1"
#define MQTT_USERNAME    "mvfr2nhlu6io9yj8uy0e"
#define MQTT_PASSWORD    ""

#define MQTT_TOPIC_PUB   "v1/devices/me/telemetry"
#define MQTT_MESSAGE     "{\"temp\":25.5}"

// APN config
#define APN              "eapn1.net"
#define APN_USER         "DynamicFF"
#define APN_PASS         "DynamicFF"

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
