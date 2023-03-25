#include "settings.h"

#include "config_secrets.h"

// runtime settings
Settings settings = {
    .mutex = NULL,
    .chosen_device = 0,
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

bool set_chosen_device(uint8_t id)
{
    if (!is_valid_secrets_id(id))
    {
        return false;
    }
    if (!xSemaphoreTake(settings.mutex, portMAX_DELAY))
    {
        return false;
    }

    settings.chosen_device = id;

    xSemaphoreGive(settings.mutex);
    return true;
}