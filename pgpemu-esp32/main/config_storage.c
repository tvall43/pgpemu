#include "config_storage.h"

#include "log_tags.h"
#include "settings.h"

#include "esp_log.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "nvs.h"

static const char KEY_AUTOCATCH[] = "catch";
static const char KEY_AUTOSPIN[] = "spin";
static const char KEY_POWERBANK_PING[] = "ping";

static nvs_handle_t user_settings_handle;

static bool nvs_read_check(esp_err_t err, const char *name);
static bool nvs_write_check(esp_err_t err, const char *name);

void init_config_storage()
{
    esp_err_t err = nvs_open("user_settings", NVS_READWRITE, &user_settings_handle);
    ESP_ERROR_CHECK(err);
}

// use_mutex=false is for use in app_main when the mutex isn't available yet
void read_stored_settings(bool use_mutex)
{
    esp_err_t err;
    int8_t autocatch = 0, autospin = 0, powerbank_ping = 0;

    if (use_mutex)
    {
        if (!xSemaphoreTake(settings.mutex, portMAX_DELAY))
        {
            ESP_LOGE(CONFIG_STORAGE_TAG, "cannot get settings mutex");
            return;
        }
    }

    // read bool settings
    err = nvs_get_i8(user_settings_handle, KEY_AUTOCATCH, &autocatch);
    if (nvs_read_check(err, KEY_AUTOCATCH))
    {
        settings.autocatch = (bool)autocatch;
    }
    err = nvs_get_i8(user_settings_handle, KEY_AUTOSPIN, &autospin);
    if (nvs_read_check(err, KEY_AUTOSPIN))
    {
        settings.autospin = (bool)autospin;
    }
    err = nvs_get_i8(user_settings_handle, KEY_POWERBANK_PING, &powerbank_ping);
    if (nvs_read_check(err, KEY_POWERBANK_PING))
    {
        settings.powerbank_ping = (bool)powerbank_ping;
    }

    if (use_mutex)
    {
        xSemaphoreGive(settings.mutex);
    }

    ESP_LOGI(CONFIG_STORAGE_TAG, "settings read from nvs");
}

bool write_config_storage()
{
    esp_err_t err;
    if (!xSemaphoreTake(settings.mutex, portMAX_DELAY))
    {
        ESP_LOGE(CONFIG_STORAGE_TAG, "cannot get settings mutex");
        return false;
    }

    bool all_ok = true;

    err = nvs_set_i8(user_settings_handle, KEY_AUTOCATCH, settings.autocatch);
    all_ok = all_ok && nvs_write_check(err, KEY_AUTOCATCH);
    err = nvs_set_i8(user_settings_handle, KEY_AUTOSPIN, settings.autospin);
    all_ok = all_ok && nvs_write_check(err, KEY_AUTOSPIN);
    err = nvs_set_i8(user_settings_handle, KEY_POWERBANK_PING, settings.powerbank_ping);
    all_ok = all_ok && nvs_write_check(err, KEY_POWERBANK_PING);

    // give it back in any of the following cases
    xSemaphoreGive(settings.mutex);

    err = nvs_commit(user_settings_handle);
    if (err != ESP_OK)
    {
        ESP_LOGE(CONFIG_STORAGE_TAG, "commit failed");
        return false;
    }

    return all_ok;
}

void close_config_storage()
{
    nvs_close(user_settings_handle);
}

static bool nvs_read_check(esp_err_t err, const char *name)
{
    switch (err)
    {
    case ESP_OK:
        return true;
    case ESP_ERR_NVS_NOT_FOUND:
        ESP_LOGW(CONFIG_STORAGE_TAG, "nvs value %s is not initialized yet!", name);
        break;
    default:
        ESP_LOGE(CONFIG_STORAGE_TAG, "nvs error reading %s: %s", name, esp_err_to_name(err));
    }

    return false;
}

static bool nvs_write_check(esp_err_t err, const char *name)
{
    switch (err)
    {
    case ESP_OK:
        return true;
    default:
        ESP_LOGE(CONFIG_STORAGE_TAG, "nvs error writing %s: %s", name, esp_err_to_name(err));
    }

    return false;
}