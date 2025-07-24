#ifndef MQTT_H
#define MQTT_H

#include "esp_err.h"



// Handle incoming MQTT URC (to be called from your URC handler)
void mqtt_handle_urc(const char *urc);
void publish_stored_attributes(void);

// Getter functions to retrieve stored values from NVS
esp_err_t mqtt_get_aux_range(float *out_val);
esp_err_t mqtt_get_aux_max(float *out_val);
esp_err_t mqtt_get_ext_range(float *out_val);
esp_err_t mqtt_get_ext_max(float *out_val);

#endif // MQTT_H