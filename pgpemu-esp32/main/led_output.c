#include "driver/gpio.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#include "led_output.h"

typedef struct
{
    int red, green, blue;
    int duration_ms;
} LedEvent;

static const int CONFIG_GPIO_OUTPUT_RED = GPIO_NUM_27;
static const int CONFIG_GPIO_OUTPUT_GREEN = GPIO_NUM_26;
static const int CONFIG_GPIO_OUTPUT_BLUE = GPIO_NUM_25;

static void leds_off();
static void led_output_task(void *pvParameters);

static QueueHandle_t led_queue;

void init_led_output()
{
    gpio_config_t io_conf = {};
    io_conf.intr_type = GPIO_INTR_DISABLE;
    io_conf.mode = GPIO_MODE_OUTPUT;
    io_conf.pin_bit_mask = (1 << CONFIG_GPIO_OUTPUT_RED) | (1 << CONFIG_GPIO_OUTPUT_GREEN) | (1 << CONFIG_GPIO_OUTPUT_BLUE);
    io_conf.pull_down_en = 0;
    io_conf.pull_up_en = 0;
    gpio_config(&io_conf);

    leds_off();

    led_queue = xQueueCreate(10, sizeof(LedEvent));
    xTaskCreate(led_output_task, "led_output_task", 2048, NULL, 14, NULL);
}

void show_rgb_event(bool red, bool green, bool blue, int duration_ms)
{
    LedEvent led_event = {
        .red = 1 - red,
        .green = 1 - green,
        .blue = 1 - blue,
        .duration_ms = duration_ms,
    };
    xQueueSend(led_queue, &led_event, 0);
}

static void leds_off()
{
    gpio_set_level(CONFIG_GPIO_OUTPUT_RED, 1);
    gpio_set_level(CONFIG_GPIO_OUTPUT_GREEN, 1);
    gpio_set_level(CONFIG_GPIO_OUTPUT_BLUE, 1);
}

static void led_output_task(void *pvParameters)
{
    LedEvent led_event;

    while (true)
    {
        if (xQueueReceive(led_queue, &led_event, portMAX_DELAY))
        {
            gpio_set_level(CONFIG_GPIO_OUTPUT_RED, led_event.red);
            gpio_set_level(CONFIG_GPIO_OUTPUT_GREEN, led_event.green);
            gpio_set_level(CONFIG_GPIO_OUTPUT_BLUE, led_event.blue);

            if (led_event.duration_ms > 0)
            {
                vTaskDelay(pdMS_TO_TICKS(led_event.duration_ms));
                leds_off();
            }
        }
    }

    vTaskDelete(NULL);
}
