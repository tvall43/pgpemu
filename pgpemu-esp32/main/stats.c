#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs.h"

#include "stats.h"

#include "log_tags.h"

static void stats_task(void *pvParameters);

static const char *KEY_RUNTIME_10MIN = "runtime10min";

static uint32_t runtime = 0;

void init_stats()
{
    xTaskCreate(stats_task, "stats_task", 2048, NULL, 9, NULL);
}

uint32_t stats_get_runtime()
{
    return 10 * runtime;
}

static uint32_t read_runtime()
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open("stats", NVS_READONLY, &handle);
    if (err != ESP_OK)
    {
        ESP_LOGW(STATS_TAG, "%s nvs open failed: %s", __func__, esp_err_to_name(err));
        if (err == ESP_ERR_NVS_NOT_FOUND)
        {
            return 0; // first run, namespace doesn't exist yet
        }
        return UINT32_MAX; // error
    }

    uint32_t runtime = 0;
    err = nvs_get_u32(handle, KEY_RUNTIME_10MIN, &runtime);
    if (err != ESP_OK)
    {
        ESP_LOGW(STATS_TAG, "%s nvs read %s failed: %s", __func__, KEY_RUNTIME_10MIN, esp_err_to_name(err));

        nvs_close(handle);
        return 0;
    }

    nvs_close(handle);
    return runtime;
}

static void write_runtime(uint32_t runtime)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open("stats", NVS_READWRITE, &handle);
    if (err != ESP_OK)
    {
        ESP_LOGE(STATS_TAG, "%s nvs open failed: %s", __func__, esp_err_to_name(err));
        return;
    }

    err = nvs_set_u32(handle, KEY_RUNTIME_10MIN, runtime);
    if (err != ESP_OK)
    {
        ESP_LOGE(STATS_TAG, "%s nvs write %s failed: %s", __func__, KEY_RUNTIME_10MIN, esp_err_to_name(err));
        nvs_close(handle);
        return;
    }

    nvs_commit(handle);
    nvs_close(handle);
}

static void stats_task(void *pvParameters)
{
    TickType_t previousWakeTime = xTaskGetTickCount();
    runtime = read_runtime();

    if (runtime == UINT32_MAX)
    {
        ESP_LOGE(STATS_TAG, "failed starting task");
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(STATS_TAG, "task start");

    while (true)
    {
        // every 10 minutes
        vTaskDelayUntil(&previousWakeTime, (10 * 60 * 1000) / portTICK_PERIOD_MS);

        runtime++;
        write_runtime(runtime);
        ESP_LOGD(STATS_TAG, "runtime counter now: %d min", 10 * runtime);
    }

    vTaskDelete(NULL);
}