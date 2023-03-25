#include "settings.h"

// runtime settings
Settings settings = {
    .mutex = NULL,
    .autocatch = true,
    .autospin = true,
    .powerbank_ping = false,
};

void init_settings()
{
    settings.mutex = xSemaphoreCreateMutex();
    xSemaphoreTake(settings.mutex, portMAX_DELAY); // block until end of this function
}

void settings_ready()
{
    xSemaphoreGive(settings.mutex);
}

bool toggle_setting(bool *var)
{
    if (!var || !xSemaphoreTake(settings.mutex, 10000 / portTICK_PERIOD_MS))
    {
        return false;
    }

    *var = !*var;

    xSemaphoreGive(settings.mutex);
    return true;
}

bool get_setting(bool *var)
{
    if (!var || !xSemaphoreTake(settings.mutex, portMAX_DELAY))
    {
        return false;
    }

    bool result = *var;

    xSemaphoreGive(settings.mutex);
    return result;
}