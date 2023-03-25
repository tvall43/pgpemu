#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"

#include "esp_system.h"
#include "esp_log.h"
#include "nvs_flash.h"

#include "pgpemu.h"

#include "config_secrets.h"
#include "config_storage.h"
#include "log_tags.h"
#include "pgp_autobutton.h"
#include "pgp_bluetooth.h"
#include "pgp_gap.h"
#include "pgp_gatts.h"
#include "powerbank.h"
#include "secrets.h"
#include "settings.h"
#include "uart.h"

void app_main()
{
    // set log levels which let init msgs through
    log_levels_init();

    // uart menu
    init_uart();

    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // init nvs storage
    init_config_storage();

    // init settings mutex
    init_settings();
    // restore saved settings from nvs
    read_stored_settings(false);

    // restore log levels
    if (settings.verbose)
    {
        ESP_LOGI(PGPEMU_TAG, "log levels verbose");
        log_levels_max();
    }
    else
    {
        ESP_LOGI(PGPEMU_TAG, "log levels default");
        log_levels_min();
    }

    // make sure we're not turned off
    init_powerbank();

    // read secrets from nvs (settings are safe to use because mutex is still locked)
    read_secrets_id(settings.chosen_device, PGP_CLONE_NAME, PGP_MAC, PGP_DEVICE_KEY, PGP_BLOB);

    if (!PGP_VALID())
    {
        // release mutex
        settings_ready();
        ESP_LOGE(PGPEMU_TAG, "NO PGP SECRETS AVAILABLE IN SLOT %d! Set them using secrets_upload.py or chose another using the 'X' menu!", settings.chosen_device);
        return;
    }

    // start autobutton task
    xTaskCreate(auto_button_task, "auto_button_task", 2048, NULL, 11, NULL);

    // set clone mac and start bluetooth
    if (!init_bluetooth())
    {
        ESP_LOGI(PGPEMU_TAG, "bluetooth init failed");
        return;
    }

    // done
    ESP_LOGI(PGPEMU_TAG, "Device: %s", PGP_CLONE_NAME);
    ESP_LOGI(PGPEMU_TAG, "MAC: %02x:%02x:%02x:%02x:%02x:%02x",
             PGP_MAC[0], PGP_MAC[1], PGP_MAC[2],
             PGP_MAC[3], PGP_MAC[4], PGP_MAC[5]);
    ESP_LOGI(PGPEMU_TAG, "Ready.");

    // make settings available
    settings_ready();
}
