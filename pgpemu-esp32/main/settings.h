#ifndef SETTINGS_H
#define SETTINGS_H

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

typedef struct
{
    SemaphoreHandle_t mutex;
    bool autocatch, autospin, powerbank_ping;
} Settings;

extern Settings settings;

void init_settings();
void settings_ready();
bool toggle_setting(bool *var);
bool get_setting(bool *var);

#endif /* SETTINGS_H */
