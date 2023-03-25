#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "esp_gatt_defs.h"
#include "esp_log.h"
#include "esp_system.h"

#include "log_tags.h"
#include "pgp_autobutton.h"
#include "settings.h"

void handle_led_notify_from_app(esp_gatt_if_t gatts_if, uint16_t conn_id, const uint8_t *buffer)
{
    int number_of_patterns = buffer[3] & 0x1f;
    int priority = (buffer[3] >> 5) & 0x7;

    ESP_LOGD(LED_TAG, "LED: Pattern Count = %d, priority = %d", number_of_patterns, priority);

    // total duration / 50 ms
    int pattern_duration = 0;
    // number of shakes when catching
    int count_ballshake = 0;
    // how many times does each color occur in the pattern?
    int count_red = 0;
    int count_green = 0;
    int count_blue = 0;
    int count_yellow = 0;
    int count_white = 0;
    int count_other = 0; // colors like pink which aren't used
    int count_off = 0, count_notoff = 0;

    // 1 pattern = 3 bytes
    for (int i = 0; i < number_of_patterns; i++)
    {
        int p = 4 + 3 * i;
        const uint8_t *pat = &buffer[p];

        uint8_t duration = pat[0];
        uint8_t red = pat[1] & 0xf;
        uint8_t green = (pat[1] >> 4) & 0xf;
        uint8_t blue = pat[2] & 0xf;
        bool interpolate = (pat[2] & 0x80) != 0;
        char inter_ch = interpolate ? 'i' : ' ';
        bool vibration = (pat[2] & 0x70) != 0;
        char vib_ch = vibration ? 'v' : ' ';
        ESP_LOGD(LED_TAG, "*(%3d) #%x%x%x %c%c", duration, red, green, blue, vib_ch, inter_ch);

        pattern_duration += duration;

        // parse colors roughly
        if (!red && !green && !blue)
        {
            count_off++;
        }
        else
        {
            count_notoff++;

            // special hack to detect blinking white at the start (beginning of catch animation)
            // this pattern is up to 3 times:
            // D (629037) PGPEMU: *(3) #888
            // D (629037) PGPEMU: *(9) #000
            // D (629047) PGPEMU: *(16) #000
            if (i <= 3 * 3)
            {
                if (red && green && blue)
                {
                    count_ballshake++;
                }
            }

            if (red && !green && !blue)
            {
                count_red++;
            }
            else if (!red && green && !blue)
            {
                count_green++;
            }
            else if (!red && !green && blue)
            {
                count_blue++;
            }
            else if (red && green && !blue)
            {
                count_yellow++;
            }
            else if (red && green && blue)
            {
                count_white++;
            }
            else
            {
                count_other++;
            }
        }
    }

    ESP_LOGI(LED_TAG, "LED pattern total duration: %d ms", pattern_duration * 50);

    bool press_button = false;

    if (count_off && !count_notoff)
    {
        ESP_LOGD(LED_TAG, "Turn LEDs off.");
    }
    else if (count_white && count_white == count_notoff)
    {
        // only white
        ESP_LOGW(LED_TAG, "Can't spin Pokestop. Bag is full.");
    }
    else if (count_red && count_off && count_red == count_notoff)
    {
        // blinking just red
        ESP_LOGW(LED_TAG, "Pokeballs are empty or Pokestop went out of range.");
    }
    else if (count_red && !count_off && count_red == count_notoff)
    {
        // only red
        ESP_LOGW(LED_TAG, "Can't spin Pokestop for some reason.");
    }
    else if (count_green && count_green == count_notoff)
    {
        // blinking green
        ESP_LOGI(LED_TAG, "Pokemon in range!");
        if (get_setting(&settings.autocatch))
        {
            press_button = true;
        }
    }
    else if (count_yellow && count_yellow == count_notoff)
    {
        // blinking yellow
        ESP_LOGI(LED_TAG, "New Pokemon in range!");
        press_button = true;
    }
    else if (count_blue && count_blue == count_notoff)
    {
        // blinking blue
        ESP_LOGI(LED_TAG, "Pokestop in range!");
        if (get_setting(&settings.autospin))
        {
            press_button = true;
        }
    }
    else if (count_ballshake)
    {
        if (count_blue && count_green)
        {
            ESP_LOGI(LED_TAG, "Caught Pokemon after %d ball shakes.", count_ballshake);
        }
        else if (count_red)
        {
            ESP_LOGI(LED_TAG, "Pokemon fled after %d ball shakes.", count_ballshake);
        }
        else
        {
            ESP_LOGW(LED_TAG, "I don't know what the Pokemon did after %d ball shakes.", count_ballshake);
        }
    }
    else if (count_red && count_green && count_blue && !count_off)
    {
        // blinking grb-grb...
        ESP_LOGI(LED_TAG, "Got items from Pokestop.");
    }
    else
    {
        if (get_setting(&settings.autospin) || get_setting(&settings.autocatch))
        {
            ESP_LOGW(LED_TAG, "Unhandled Color pattern, pushing button in any case");
            press_button = true;
        }
        else
        {
            ESP_LOGW(LED_TAG, "Unhandled Color pattern");
        }
    }

    if (press_button)
    {
        int pattern_ms = pattern_duration * 50;

        // random button press delay between 1000 and 2500 ms
        int delay = 1000 + esp_random() % 1501;
        if (delay < pattern_ms)
        {
            ESP_LOGD(LED_TAG, "queueing push button after %d ms, conn_id = %d", delay, conn_id);

            button_queue_item_t item;
            item.gatts_if = gatts_if;
            item.conn_id = conn_id;
            item.delay = delay;
            xQueueSend(button_queue, &item, portMAX_DELAY);
        }
    }

    // currently all things which require button interaction are 30 s long.
    // check for this in case the app changes.
    if (press_button != (pattern_duration * 50 == 30000))
    {
        ESP_LOGW(LED_TAG, "button press vs. duration mismatch");
    }
}