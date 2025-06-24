#pragma once

#include <stdbool.h>

typedef struct {
    float latitude;
    float longitude;
    float altitude;
} GnssLocation;

bool gnss_power_on(void);
bool gnss_power_off(void);
bool gnss_get_location(GnssLocation *loc);
void gnss_task(void *param);
