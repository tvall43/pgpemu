#include "config_storage.h"

#include "config_secrets.h"
#include "log_tags.h"
#include "nvs_helper.h"
#include "settings.h"

#include "esp_log.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "nvs.h"

static const char KEY_AUTOCATCH[] = "catch";
static const char KEY_AUTOSPIN[] = "spin";
static const char KEY_POWERBANK_PING[] = "ping";
static const char KEY_CHOSEN_DEVICE[] = "device";
static const char KEY_VERBOSE[] = "verbose";

static nvs_handle_t user_settings_handle;

void init_config_storage()
{
    esp_err_t err;

    // initialize NVS
    err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    // open config partition
    err = nvs_open("user_settings", NVS_READWRITE, &user_settings_handle);
    ESP_ERROR_CHECK(err);
}

// use_mutex=false is for use in app_main when the mutex isn't available yet
void read_stored_settings(bool use_mutex)
{
    esp_err_t err;
    int8_t autocatch = 0, autospin = 0, powerbank_ping = 0, verbose = 0;
    uint8_t chosen_device = 0;

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
    if (nvs_read_check(CONFIG_STORAGE_TAG, err, KEY_AUTOCATCH))
    {
        settings.autocatch = (bool)autocatch;
    }
    err = nvs_get_i8(user_settings_handle, KEY_AUTOSPIN, &autospin);
    if (nvs_read_check(CONFIG_STORAGE_TAG, err, KEY_AUTOSPIN))
    {
        settings.autospin = (bool)autospin;
    }
    err = nvs_get_i8(user_settings_handle, KEY_POWERBANK_PING, &powerbank_ping);
    if (nvs_read_check(CONFIG_STORAGE_TAG, err, KEY_POWERBANK_PING))
    {
        settings.powerbank_ping = (bool)powerbank_ping;
    }
    err = nvs_get_i8(user_settings_handle, KEY_VERBOSE, &verbose);
    if (nvs_read_check(CONFIG_STORAGE_TAG, err, KEY_VERBOSE))
    {
        settings.verbose = (bool)verbose;
    }

    // read uint8_t settings
    err = nvs_get_u8(user_settings_handle, KEY_CHOSEN_DEVICE, &chosen_device);
    if (nvs_read_check(CONFIG_STORAGE_TAG, err, KEY_CHOSEN_DEVICE))
    {
        if (is_valid_secrets_id(chosen_device))
        {
            settings.chosen_device = chosen_device;
        }
        else
        {
            ESP_LOGE(CONFIG_STORAGE_TAG, "invalid chosen device %d", chosen_device);
        }
    }

    if (use_mutex)
    {
        xSemaphoreGive(settings.mutex);
    }

    ESP_LOGI(CONFIG_STORAGE_TAG, "user settings read from nvs");
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

    // bools
    err = nvs_set_i8(user_settings_handle, KEY_AUTOCATCH, settings.autocatch);
    all_ok = all_ok && nvs_write_check(CONFIG_STORAGE_TAG, err, KEY_AUTOCATCH);
    err = nvs_set_i8(user_settings_handle, KEY_AUTOSPIN, settings.autospin);
    all_ok = all_ok && nvs_write_check(CONFIG_STORAGE_TAG, err, KEY_AUTOSPIN);
    err = nvs_set_i8(user_settings_handle, KEY_POWERBANK_PING, settings.powerbank_ping);
    all_ok = all_ok && nvs_write_check(CONFIG_STORAGE_TAG, err, KEY_POWERBANK_PING);
    err = nvs_set_i8(user_settings_handle, KEY_VERBOSE, settings.verbose);
    all_ok = all_ok && nvs_write_check(CONFIG_STORAGE_TAG, err, KEY_VERBOSE);

    // uint8s
    err = nvs_set_u8(user_settings_handle, KEY_CHOSEN_DEVICE, settings.chosen_device);
    all_ok = all_ok && nvs_write_check(CONFIG_STORAGE_TAG, err, KEY_CHOSEN_DEVICE);

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
