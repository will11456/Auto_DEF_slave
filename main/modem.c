#include "pin_map.h"
#include "main.h"
#include "modem.h"
#include "display.h"
#include "uart.h"
#include "data.h"

// UART port number for SIM800L
#define SIM800L_UART_PORT UART_NUM_2

// UART buffer size
#define SIM800L_UART_BUF_SIZE 1024

static const char* TAG = "MODEM";


void sim800l_init(void) {
    // Configure UART parameters
    const uart_config_t uart_config = {
        .baud_rate = SIM800L_BAUD_RATE,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    // Install UART driver
    ESP_ERROR_CHECK(uart_driver_install(SIM800L_UART_PORT, SIM800L_UART_BUF_SIZE, 0, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(SIM800L_UART_PORT, &uart_config));

    // Set UART pins
    ESP_ERROR_CHECK(uart_set_pin(SIM800L_UART_PORT, MODEM_TX, MODEM_RX, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));

    // Configure power and reset pins as outputs
    gpio_set_direction(MODEM_PWR_KEY, GPIO_MODE_OUTPUT);

    // Initially power off the SIM800L
    sim800l_power_off();
}

void sim800l_power_on(void) {
    // Toggle the power key to power on the SIM800L
    ESP_LOGW(TAG, "Powering on SIM800L");
    gpio_set_level(MODEM_PWR_KEY, 1);
    vTaskDelay(pdMS_TO_TICKS(1000));
    gpio_set_level(MODEM_PWR_KEY, 0);
    vTaskDelay(pdMS_TO_TICKS(5000)); // Wait for the module to power on
}

void sim800l_power_off(void) {
    // Toggle the power key to power off the SIM800L
    ESP_LOGW(TAG, "Powering off SIM800L");
    gpio_set_level(MODEM_PWR_KEY, 1);
    vTaskDelay(pdMS_TO_TICKS(1200));
    gpio_set_level(MODEM_PWR_KEY, 0);
    vTaskDelay(pdMS_TO_TICKS(5000)); // Wait for the module to power off
}



void sim800l_send_command(const char* command) {
    // Send AT command to the SIM800L
    uart_write_bytes(SIM800L_UART_PORT, command, strlen(command));
    uart_write_bytes(SIM800L_UART_PORT, "\r\n", 2);
}

int sim800l_read_response(char* buffer, uint32_t buffer_size) {
    // Read response from the SIM800L
    int len = uart_read_bytes(SIM800L_UART_PORT, (uint8_t*)buffer, buffer_size - 1, pdMS_TO_TICKS(5000));
    if (len > 0) {
        buffer[len] = '\0'; // Null-terminate the received string
    }
    return len;
}

void send_at_command_test(const char* command) {
    char response[1024];  // Buffer to hold the response from SIM800L

    // Log the command being sent
    ESP_LOGI(TAG, "Sending command: %s", command);

    // Send the AT command
    sim800l_send_command(command);

    // Read the response from the SIM800L
    int len = sim800l_read_response(response, sizeof(response));

    // Check if a response was received
    if (len > 0) {
        // Null-terminate and log the response
        response[len] = '\0';
        ESP_LOGI(TAG, "Received response: %s", response);
    } else {
        // Log that no response was received
        ESP_LOGW(TAG, "No response received for command: %s", command);
    }
}





void send_sms(const char* phone_number, const char* message) {
    char response[1024];
    char command[160];

    // Ensure SIM800L is initialized and powered on before calling this function

    // Set SMS text mode
    ESP_LOGI(TAG, "Setting SMS to text mode");
    sim800l_send_command("AT+CMGF=1");
    vTaskDelay(pdMS_TO_TICKS(1000));
    int len = sim800l_read_response(response, sizeof(response));
    if (len > 0) {
        response[len] = '\0'; // Ensure the response is null-terminated
        ESP_LOGI(TAG, "Response to AT+CMGF=1: %s", response);
    } else {
        ESP_LOGW(TAG, "No response for CMGF command");
    }

    // Prepare command to set the recipient's phone number
    snprintf(command, sizeof(command), "AT+CMGS=\"%s\"", phone_number);
    ESP_LOGI(TAG, "Setting recipient: %s", phone_number);
    sim800l_send_command(command);
    vTaskDelay(pdMS_TO_TICKS(1000));
    len = sim800l_read_response(response, sizeof(response));
    if (len > 0) {
        response[len] = '\0';
        ESP_LOGI(TAG, "Response to AT+CMGS: %s", response);
    } else {
        ESP_LOGW(TAG, "No response for CMGS command");
    }

    // Send the message text
    ESP_LOGI(TAG, "Sending message: %s", message);
    uart_write_bytes(SIM800L_UART_PORT, message, strlen(message));

    // End the message with Ctrl+Z (ASCII 26)
    uart_write_bytes(SIM800L_UART_PORT, "\x1A", 1);
    ESP_LOGI(TAG, "Ending message with Ctrl+Z");
    vTaskDelay(pdMS_TO_TICKS(5000));  // Wait for the message to be sent
    len = sim800l_read_response(response, sizeof(response));
    if (len > 0) {
        response[len] = '\0';
        ESP_LOGI(TAG, "Final response after sending SMS: %s", response);
    } else {
        ESP_LOGW(TAG, "No final response received");
    }
}



void read_sms(void) {
    char response[2048];
    char command[32];
    
    // Ensure SIM800L is initialized and powered on before calling this function

    // Set SMS text mode
    ESP_LOGI(TAG, "Setting SMS to text mode");
    sim800l_send_command("AT+CMGF=1");
    vTaskDelay(pdMS_TO_TICKS(1000));
    int len = sim800l_read_response(response, sizeof(response));
    if (len > 0) {
        response[len] = '\0';
        ESP_LOGI(TAG, "Response to AT+CMGF=1: %s", response);
    } else {
        ESP_LOGW(TAG, "No response for CMGF command");
    }

    // List all messages
    ESP_LOGI(TAG, "Listing all SMS messages");
    sim800l_send_command("AT+CMGL=\"ALL\"");
    vTaskDelay(pdMS_TO_TICKS(2000));  // Wait for response
    len = sim800l_read_response(response, sizeof(response));
    if (len > 0) {
        response[len] = '\0';
        ESP_LOGI(TAG, "Messages: %s", response);
    } else {
        ESP_LOGW(TAG, "No response for CMGL command");
    }
}


void connect_to_internet(const char* apn, const char* user, const char* password) {
    char response[1024];

    // Ensure SIM800L is initialized and powered on before calling this function

    // Set the SIM800L to GPRS mode
    ESP_LOGI(TAG, "Setting SIM800L to GPRS mode");
    sim800l_send_command("AT+SAPBR=3,1,\"Contype\",\"GPRS\"");
    vTaskDelay(pdMS_TO_TICKS(1000));
    int len = sim800l_read_response(response, sizeof(response));
    if (len > 0) {
        response[len] = '\0';
        ESP_LOGI(TAG, "Response: %s", response);
    } else {
        ESP_LOGW(TAG, "No response for GPRS mode command");
    }

    // Set the APN
    ESP_LOGI(TAG, "Setting APN to: %s", apn);
    char command[128];
    snprintf(command, sizeof(command), "AT+SAPBR=3,1,\"APN\",\"%s\"", apn);
    sim800l_send_command(command);
    vTaskDelay(pdMS_TO_TICKS(1000));
    len = sim800l_read_response(response, sizeof(response));
    if (len > 0) {
        response[len] = '\0';
        ESP_LOGI(TAG, "Response: %s", response);
    } else {
        ESP_LOGW(TAG, "No response for APN command");
    }

    // Set the user name, if necessary
    if (user && strlen(user) > 0) {
        ESP_LOGI(TAG, "Setting user name: %s", user);
        snprintf(command, sizeof(command), "AT+SAPBR=3,1,\"USER\",\"%s\"", user);
        sim800l_send_command(command);
        vTaskDelay(pdMS_TO_TICKS(1000));
        len = sim800l_read_response(response, sizeof(response));
        if (len > 0) {
            response[len] = '\0';
            ESP_LOGI(TAG, "Response: %s", response);
        } else {
            ESP_LOGW(TAG, "No response for USER command");
        }
    }

    // Set the password, if necessary
    if (password && strlen(password) > 0) {
        ESP_LOGI(TAG, "Setting password");
        snprintf(command, sizeof(command), "AT+SAPBR=3,1,\"PWD\",\"%s\"", password);
        sim800l_send_command(command);
        vTaskDelay(pdMS_TO_TICKS(1000));
        len = sim800l_read_response(response, sizeof(response));
        if (len > 0) {
            response[len] = '\0';
            ESP_LOGI(TAG, "Response: %s", response);
        } else {
            ESP_LOGW(TAG, "No response for PWD command");
        }
    }

    // Open a GPRS context
    ESP_LOGI(TAG, "Opening GPRS context");
    sim800l_send_command("AT+SAPBR=1,1");
    vTaskDelay(pdMS_TO_TICKS(2000));  // Wait for the context to open
    len = sim800l_read_response(response, sizeof(response));
    if (len > 0) {
        response[len] = '\0';
        ESP_LOGI(TAG, "Response: %s", response);
    } else {
        ESP_LOGW(TAG, "No response for opening GPRS context");
    }

    // Query GPRS context status
    ESP_LOGI(TAG, "Querying GPRS context status");
    sim800l_send_command("AT+SAPBR=2,1");
    vTaskDelay(pdMS_TO_TICKS(1000));
    len = sim800l_read_response(response, sizeof(response));
    if (len > 0) {
        response[len] = '\0';
        ESP_LOGI(TAG, "Response: %s", response);
        if (strstr(response, "+SAPBR: 1,1")) {
            ESP_LOGI(TAG, "GPRS context opened successfully");
        } else {
            ESP_LOGW(TAG, "Failed to open GPRS context");
        }
    } else {
        ESP_LOGW(TAG, "No response for GPRS context status");
    }

    // Get the IP address
    // ESP_LOGI(TAG, "Getting IP address");
    // sim800l_send_command("AT+CIFSR");
    // vTaskDelay(pdMS_TO_TICKS(2000));  // Wait for the IP address
    // len = sim800l_read_response(response, sizeof(response));
    // if (len > 0) {
    //     response[len] = '\0';
    //     ESP_LOGI(TAG, "IP Address: %s", response);
    // } else {
    //     ESP_LOGW(TAG, "No response for IP address request");
    // }
}


void modem_task(void *param) {

    //power on modem
    //sim800l_power_on();
    //vTaskDelay(1000 / portTICK_PERIOD_MS);
    
    // send_at_command_test("AT");
    // vTaskDelay(1000 / portTICK_PERIOD_MS);


    // ESP_LOGI(TAG, "Connecting to the internet");
    // connect_to_internet(APN, USER, PASS);
    // xEventGroupSetBits(systemEvents, MODEM_GPRS_CON);
    // ESP_LOGW(TAG, "Signalled modem connected");

    
    // send_sms("07852709248", "Hello World! v3");
    // ESP_LOGW(TAG, "SMS sent");


    // vTaskDelay(10000 / portTICK_PERIOD_MS);

    //read_sms();

    
    while(1){

    
    vTaskDelay(pdMS_TO_TICKS(100)); // Add delay to avoid busy looping
    }
}