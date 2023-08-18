#include <math.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs.h"
#include "esp_sleep.h"
#include "esp_wifi.h"

#include "log_tags.h"

#include "pgp_handshake_multi.h"

//#include "esp_adc/adc_oneshot.h"
//#include "esp_adc/adc_cali.h"
//#include "esp_adc/adc_cali_scheme.h"

#include "driver/adc.h"
#include "esp_adc_cal.h"

#define uS_TO_S 1000000ULL

bool disconnected = true;

static esp_adc_cal_characteristics_t adc1_chars;

float read_battery_voltage()
{
    float voltage = adc1_get_raw(ADC1_CHANNEL_7) / 4095.0 * 7.1;
//    uint32_t voltage = esp_adc_cal_raw_to_voltage(adc1_get_raw(ADC1_CHANNEL_7), &adc1_chars);
    ESP_LOGI("BATTERY", "voltage: %f", voltage );
    return voltage;
}

uint8_t read_battery_value()
{
    uint8_t percentage = 50; // default value if the measurement failes
//    uint32_t voltage = esp_adc_cal_raw_to_voltage(adc1_get_raw(ADC1_CHANNEL_7), &adc1_chars); // / 4096.0 * 6.79;
//    uint32_t raw = adc_get_raw(ADC_UNIT_1, ADC_CHANNEL_7);
//    uint32_t voltage = adc_cali_raw_to_voltage(adc1_cali_chan1_handle, adc_raw[0][1], &voltage[0][1]));;
    float voltage = read_battery_voltage();
    if (voltage > 0.5){ // measurement successful
        percentage = 3775.5 * pow(voltage, 4) - 59209 * pow(voltage, 3) + 347670 * pow(voltage, 2) - 905717 * voltage + 883083;
//        percentage = (voltage - 1500) * 100 / (2100 - 1500);
        if (voltage > 4.19) percentage = 100;
        else if (voltage <= 3.00) percentage = 0;
        if (percentage > 100) percentage = 100;
        else if (percentage < 0) percentage = 0;
    }
    ESP_LOGI("BATTERY", "voltage: %f percentage: %i", voltage, percentage );
    return percentage;
}


static const char *TAG_POWERSAVE = "POWERSAVE";

void power_save_task(void *pvParameters)	// save power if not connected and periodically wake up
{
    ESP_LOGI(TAG_POWERSAVE, "[powersave task start]");
    esp_wifi_stop();
    disconnected = true;
    bool last_dis_state = true;
    int connection_init = 0;
    int boot_millis = 0;
    int last_awake = 10000; // 10s to connect quickly right after startup

    while (true)
    {
        vTaskDelay(50);
        boot_millis = xTaskGetTickCount() * 10;

        if (get_active_connections() == 0){
            if (boot_millis - last_awake > 10000){
                ESP_LOGI(TAG_POWERSAVE, "not connected for: %i ms -> light sleep", boot_millis - last_awake);
                esp_sleep_enable_timer_wakeup(uS_TO_S * 10);
                esp_light_sleep_start();
                last_awake = boot_millis;
            }
        }else{
            last_awake = boot_millis;
            if (last_dis_state){ // state switched to connected
                connection_init = boot_millis;
            }
//            if (get_cert_state(0) != 6 && boot_millis - connection_init > 60000){ // connection timeout
//                disconnected = true;
//                ESP_LOGI(TAG_POWERSAVE, "connection timed out");
//            }
        }

//        ESP_LOGI(TAG_POWERSAVE, "connected: %d", get_active_connections());
        last_dis_state = disconnected;
    }
}


static const char *TAG_OVERDISCHARGE = "OVERDISCHARGE PROTECTION";

void overdischarge_protection_task(void *pvParameters)	// enter deep sleep forever if the battery reaches 0%
{
    ESP_LOGI(TAG_OVERDISCHARGE, "[overdischarge protection task start]");

    while (true)
    {
        if (read_battery_voltage() < 2.9 ){
            ESP_LOGI(TAG_OVERDISCHARGE, "battery reached 0%% -> deep sleep forever to prevent overdischarge");
//            esp_sleep_enable_timer_wakeup(uS_TO_S * 10000000);
            esp_sleep_enable_timer_wakeup(uS_TO_S * 600); // lets not do forever so itll eventually wake up after charging for a bit. please dont use unprotected cells.
            esp_deep_sleep_start();
        }
        vTaskDelay(10000);
    }
}

void setup_battery_measurement(){
    esp_adc_cal_characteristics_t adc1_chars;
    esp_adc_cal_characterize(ADC_UNIT_1, ADC_ATTEN_DB_11, ADC_WIDTH_BIT_DEFAULT, 0, &adc1_chars);
    adc1_config_width(ADC_WIDTH_BIT_DEFAULT);
    adc1_config_channel_atten(ADC1_CHANNEL_7, ADC_ATTEN_DB_11);
    xTaskCreate(overdischarge_protection_task, "overdischarge_protection_task", 2048, NULL, 12, NULL);
//    xTaskCreate(power_save_task, "power_save_task", 2048, NULL, 12, NULL);
}
