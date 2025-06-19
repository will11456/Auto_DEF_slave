#ifndef MODEM_H
#define MODEM_H

#include <stdint.h>

// ==========================
// SIM7080G PUBLIC INTERFACE
// ==========================

/**
 * @brief Initialize UART, GPIOs, power on SIM7080G, and connect to LTE + MQTT broker.
 * Should be called once from modem_task().
 */
void sim7080_init(void);

/**
 * @brief Power on the SIM7080G (toggle PWRKEY, enable rail).
 */
void sim7080_power_on(void);

/**
 * @brief Power off the SIM7080G (disable power rail).
 */
void sim7080_power_off(void);

/**
 * @brief Send an AT command (with CRLF) over UART.
 * @param cmd The command string without CRLF.
 */
void sim7080_send_command(const char *cmd);

/**
 * @brief Read a modem response into buffer.
 * @param buffer Output buffer.
 * @param buffer_size Max size of buffer.
 * @return Number of bytes read.
 */
int sim7080_read_response(char *buffer, uint32_t buffer_size, int timeout_ms);

/**
 * @brief Send an AT command and log the response (for debug/test).
 * @param command The command to send.
 */
void send_at_command_test(const char *command);

/**
 * @brief Publish a message to a given MQTT topic.
 * Must be called only after MQTT connection is established in sim7080_init().
 * @param topic MQTT topic name (e.g., "sensors/temp")
 * @param message Payload string to send
 */
void sim7080_publish(const char *topic, const char *message);

/**
 * @brief Modem task entry point.
 * Handles init and MQTT connect. Call this once during system startup.
 */
void modem_task(void *param);

/**
 * @brief Wait for SIM readiness and usable signal (RSSI ≠ 99).
 *
 * @param max_attempts Number of retries.
 * @param delay_ms Delay between retries in milliseconds.
 * @return true if ready and signal acquired, false otherwise.
 */
bool sim7080_wait_for_sim_and_signal(int max_attempts, int delay_ms);

#endif // MODEM_H
