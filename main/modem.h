#ifndef SIM800L_H
#define SIM800L_H

#include <stdint.h>
#include "esp_log.h"
#include <driver/uart.h>
#include <driver/gpio.h>
#include <string.h>

#define SIM800L_UART_BUF_SIZE 1024

// UART configuration
#define SIM800L_BAUD_RATE 9600

// SIM800L initialization and control functions
void sim800l_init(void);
void sim800l_power_on(void);
void sim800l_power_off(void);
void sim800l_reset(void);

// SIM800L communication functions
void sim800l_send_command(const char* command);
void send_at_command_test(const char* command);
void send_sms(const char* number, const char* message);
void read_sms(void);
void connect_to_internet(const char* apn, const char* user, const char* password);
void modem_task();

int sim800l_read_response(char* buffer, uint32_t buffer_size);


#endif // SIM800L_H
