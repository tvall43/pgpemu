#include "esp_system.h"
#include "esp_log.h"

#include "button_input.h"
#include "config_secrets.h"
#include "config_storage.h"
#include "led_output.h"
#include "log_tags.h"
#include "pgp_autobutton.h"
#include "pgp_bluetooth.h"
#include "pgp_gap.h"
#include "pgp_gatts.h"
#include "powerbank.h"
#include "secrets.h"
#include "settings.h"
#include "stats.h"
#include "uart.h"

void app_main()
{
    // set log levels which let init msgs through
    log_levels_init();

    // uart menu
    init_uart();

    // rgb led
    init_led_output();
    // show red
    show_rgb_event(true, false, false, 0);
    // push button
    init_button_input();

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

    // runtime counter
    init_stats();

    // start autobutton task
    if (!init_autobutton())
    {
        ESP_LOGI(PGPEMU_TAG, "creating button task failed");
        return;
    }

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

    // show green for 1 s
    show_rgb_event(false, true, false, 1000);
    // show blue until someone connects
    show_rgb_event(false, false, true, 0);
}
