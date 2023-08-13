#include <math.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs.h"
#include "esp_sleep.h"

#include "log_tags.h"

#include "driver/adc.h"
#include "esp_adc_cal.h"

#define uS_TO_S 1000000ULL

static esp_adc_cal_characteristics_t adc1_chars;

uint8_t read_battery_value()
{
    uint8_t percentage = 50; // default value if the measurement failes
    float voltage = adc1_get_raw(ADC1_CHANNEL_7) / 4096.0 * 6.79;
    if (voltage > 0.5){ // measurement successful
        percentage = 3775.5 * pow(voltage, 4) - 59209 * pow(voltage, 3) + 347670 * pow(voltage, 2) - 905717 * voltage + 883083;
        if (voltage > 4.19) percentage = 100;
        else if (voltage <= 3.70) percentage = 0;
        if (percentage > 100) percentage = 100;
        else if (percentage < 0) percentage = 0;
    }
    ESP_LOGI("voltage:", "%f", voltage);
    ESP_LOGI("BATTERY", "voltage: %f percentage: %i", voltage, percentage);
    return percentage;
}

static const char *TAG_OVERDISCHARGE = "OVERDISCHARGE PROTECTION";

void overdischarge_protection_task(void *pvParameters)	// enter deep sleep forever if the battery reaches 0%
{
    ESP_LOGI(TAG_OVERDISCHARGE, "[overdischarge protection task start]");

    while (true)
    {
        if (read_battery_value() == 0){
            ESP_LOGI(TAG_OVERDISCHARGE, "battery reached 0%% -> deep sleep forever to prevent overdischarge");
            esp_sleep_enable_timer_wakeup(uS_TO_S * 10000000);
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
}
