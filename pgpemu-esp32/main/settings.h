#ifndef SETTINGS_H
#define SETTINGS_H

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

typedef struct
{
    // any read/write must lock this
    SemaphoreHandle_t mutex;

    // set which PGP device presets stored in NVS should be cloned
    uint8_t chosen_device;

    // gotcha functions
    bool autocatch, autospin;
    
    // waste a bit of power to keep your powerbank from turning us off
    bool powerbank_ping;

    // verbose log messages
    bool verbose;
} Settings;

extern Settings settings;

void init_settings();
void settings_ready();
bool toggle_setting(bool *var);
bool get_setting(bool *var);
bool set_chosen_device(uint8_t id);

#endif /* SETTINGS_H */
