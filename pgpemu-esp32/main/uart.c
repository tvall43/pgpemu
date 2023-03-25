#include "driver/uart.h"
#include "esp_log.h"

#include "uart.h"

#include "config_storage.h"
#include "log_tags.h"
#include "pgpemu.h"
#include "secrets.h"
#include "settings.h"

#define EX_UART_NUM UART_NUM_0
#define BUF_SIZE 1024
#define RD_BUF_SIZE BUF_SIZE
static QueueHandle_t uart0_queue;

static void uart_event_task(void *pvParameters);

void init_uart()
{
    /* Configure parameters of an UART driver,
     * communication pins and install the driver */
    uart_config_t uart_config = {
        .baud_rate = 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .rx_flow_ctrl_thresh = 0,
        .use_ref_tick = 0};

    uart_param_config(EX_UART_NUM, &uart_config);

    // Set UART pins (using UART0 default pins ie no changes.)
    uart_set_pin(EX_UART_NUM, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    // Install UART driver, and get the queue.
    uart_driver_install(EX_UART_NUM, BUF_SIZE * 2, BUF_SIZE * 2, 20, &uart0_queue, 0);

    // Create a task to handler UART event from ISR
    xTaskCreate(uart_event_task, "uart_event_task", 4096, NULL, 12, NULL);
}

static void uart_event_task(void *pvParameters)
{
    uart_event_t event;

    uint8_t *dtmp = (uint8_t *)malloc(RD_BUF_SIZE);
    for (;;)
    {
        // Waiting for UART event.
        if (xQueueReceive(uart0_queue, (void *)&event, portMAX_DELAY))
        {
            bzero(dtmp, RD_BUF_SIZE);
            ESP_LOGD(UART_TAG, "uart[%d] event:", EX_UART_NUM);

            switch (event.type)
            {
            // Event of UART receving data
            /*We'd better handler data event fast, there would be much more data events than
            other types of events. If we take too much time on data event, the queue might
            be full.*/
            case UART_DATA:
                ESP_LOGD(UART_TAG, "[UART DATA]: %d bytes", event.size);
                uart_read_bytes(EX_UART_NUM, dtmp, event.size, portMAX_DELAY);

                if (dtmp[0] == 'q')
                {
                    ESP_LOGI(UART_TAG, "[push button]");
                    uint8_t notify_data[2];
                    notify_data[0] = 0x03;
                    notify_data[1] = 0xff;

                    // esp_ble_gatts_send_indicate(last_if, last_conn_id, led_button_handle_table[IDX_CHAR_BUTTON_VAL],
                    //                             sizeof(notify_data), notify_data, false);
                }
                else if (dtmp[0] == 'w')
                {
                    ESP_LOGI(UART_TAG, "[unpush button]");
                    uint8_t notify_data[2];
                    notify_data[0] = 0x00;
                    notify_data[1] = 0x00;

                    // esp_ble_gatts_send_indicate(last_if, last_conn_id, led_button_handle_table[IDX_CHAR_BUTTON_VAL],
                    //                             sizeof(notify_data), notify_data, false);
                }
                else if (dtmp[0] == 't')
                {
                    TickType_t now = xTaskGetTickCount();
                    if (connectionStart != 0)
                    {
                        ESP_LOGI(UART_TAG, "connected for %d ms", pdTICKS_TO_MS(now - connectionStart));
                    }
                    else if (connectionEnd != 0)
                    {
                        ESP_LOGI(UART_TAG, "disconnected for %d ms", pdTICKS_TO_MS(now - connectionEnd));
                    }
                    else
                    {
                        ESP_LOGI(UART_TAG, "no connection yet");
                    }
                }
                else if (dtmp[0] == 's')
                {
                    if (!toggle_setting(&settings.autospin))
                    {
                        ESP_LOGE(UART_TAG, "failed!");
                    }
                    ESP_LOGI(UART_TAG, "autospin %s", get_setting(&settings.autospin) ? "on" : "off");
                }
                else if (dtmp[0] == 'c')
                {
                    if (!toggle_setting(&settings.autocatch))
                    {
                        ESP_LOGE(UART_TAG, "failed!");
                    }
                    ESP_LOGI(UART_TAG, "autocatch %s", get_setting(&settings.autocatch) ? "on" : "off");
                }
                else if (dtmp[0] == 'p')
                {
                    if (!toggle_setting(&settings.powerbank_ping))
                    {
                        ESP_LOGE(UART_TAG, "failed!");
                    }
                    ESP_LOGI(UART_TAG, "powerbank ping %s", get_setting(&settings.powerbank_ping) ? "on" : "off");
                }
                else if (dtmp[0] == 'A')
                {
                    ESP_LOGI(UART_TAG, "starting advertising YOLO");
                    pgp_advertise();
                }
                else if (dtmp[0] == 'S')
                {
                    ESP_LOGI(UART_TAG, "saving configuration to nvs");
                    bool ok = write_config_storage();
                    if (ok)
                    {
                        ESP_LOGI(UART_TAG, "success!");
                    }
                }
                else if (dtmp[0] == 'R')
                {
                    ESP_LOGI(UART_TAG, "closing nvs");
                    close_config_storage();
                    ESP_LOGI(UART_TAG, "restarting");
                    fflush(stdout);
                    esp_restart();
                }
                else if (dtmp[0] == 'T')
                {
                    char buf[1024]; // "min. 40 bytes per task"
                    vTaskList(buf);

                    ESP_LOGI(UART_TAG, "Task List:\nTask Name\tStatus\tPrio\tHWM\tTask\tAffinity\n%s", buf);
                }
                else if (dtmp[0] == 'h' || dtmp[0] == '?')
                {
                    ESP_LOGI(UART_TAG, "Device: %s", PGP_CLONE_NAME);
                    ESP_LOGI(UART_TAG, "User Settings (lost on restart):");
                    ESP_LOGI(UART_TAG, "- s - toggle PGP autospin");
                    ESP_LOGI(UART_TAG, "- c - toggle PGP autocatch");
                    ESP_LOGI(UART_TAG, "- p - toggle powerbank ping");
                    ESP_LOGI(UART_TAG, "Commands:");
                    ESP_LOGI(UART_TAG, "- h,? - help");
                    //ESP_LOGI(UART_TAG, "- w,q - (un)push virtual PGP button");
                    ESP_LOGI(UART_TAG, "- A - start BT advertising again to connect another phone (EXPERIMENTAL)");
                    ESP_LOGI(UART_TAG, "- t - show BT connection time");
                    ESP_LOGI(UART_TAG, "- T - show FreeRTOS task list");
                    ESP_LOGI(UART_TAG, "- S - save user settings permanently");
                    ESP_LOGI(UART_TAG, "- R - restart");
                }

                break;
            // Event of HW FIFO overflow detected
            case UART_FIFO_OVF:
                ESP_LOGW(UART_TAG, "hw fifo overflow");
                // If fifo overflow happened, you should consider adding flow control for your application.
                // The ISR has already reset the rx FIFO,
                // As an example, we directly flush the rx buffer here in order to read more data.
                uart_flush_input(EX_UART_NUM);
                xQueueReset(uart0_queue);
                break;
            // Event of UART ring buffer full
            case UART_BUFFER_FULL:
                ESP_LOGW(UART_TAG, "ring buffer full");
                // If buffer full happened, you should consider encreasing your buffer size
                // As an example, we directly flush the rx buffer here in order to read more data.
                uart_flush_input(EX_UART_NUM);
                xQueueReset(uart0_queue);
                break;
            // Event of UART RX break detected
            case UART_BREAK:
                ESP_LOGI(UART_TAG, "uart rx break");
                break;
            // Event of UART parity check error
            case UART_PARITY_ERR:
                ESP_LOGI(UART_TAG, "uart parity error");
                break;
            // Event of UART frame error
            case UART_FRAME_ERR:
                ESP_LOGI(UART_TAG, "uart frame error");
                break;
            // Others
            default:
                ESP_LOGI(UART_TAG, "uart event type: %d", event.type);
                break;
            }
        }
    }

    free(dtmp);
    dtmp = NULL;
    vTaskDelete(NULL);
}