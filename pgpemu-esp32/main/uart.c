#include <string.h>

#include "driver/uart.h"
#include "esp_log.h"
#include "mbedtls/base64.h"

#include "uart.h"

#include "config_secrets.h"
#include "config_storage.h"
#include "log_tags.h"
#include "pgpemu.h"
#include "pgp_gap.h"
#include "pgp_gatts.h"
#include "secrets.h"
#include "settings.h"

#define EX_UART_NUM UART_NUM_0
#define BUF_SIZE 1024
#define RD_BUF_SIZE BUF_SIZE
static QueueHandle_t uart0_queue;

// scratchpad for reading/writing to nvs
static char tmp_clone_name[sizeof(PGP_CLONE_NAME)];
static uint8_t tmp_mac[sizeof(PGP_MAC)];
static uint8_t tmp_device_key[sizeof(PGP_DEVICE_KEY)];
static uint8_t tmp_blob[sizeof(PGP_BLOB)];

static void uart_event_task(void *pvParameters);
static void uart_secrets_handler();
static bool decode_to_buf(char targetType, uint8_t *inBuf, int inBytes);
void uart_restart_command();

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

                if (dtmp[0] == 't')
                {
                    // show connection status
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
                    // toggle autospin
                    if (!toggle_setting(&settings.autospin))
                    {
                        ESP_LOGE(UART_TAG, "failed!");
                    }
                    ESP_LOGI(UART_TAG, "autospin %s", get_setting(&settings.autospin) ? "on" : "off");
                }
                else if (dtmp[0] == 'c')
                {
                    // toggle autocatch
                    if (!toggle_setting(&settings.autocatch))
                    {
                        ESP_LOGE(UART_TAG, "failed!");
                    }
                    ESP_LOGI(UART_TAG, "autocatch %s", get_setting(&settings.autocatch) ? "on" : "off");
                }
                else if (dtmp[0] == 'p')
                {
                    // toggle ping
                    if (!toggle_setting(&settings.powerbank_ping))
                    {
                        ESP_LOGE(UART_TAG, "failed!");
                    }
                    ESP_LOGI(UART_TAG, "powerbank ping %s", get_setting(&settings.powerbank_ping) ? "on" : "off");
                }
                else if (dtmp[0] == 'l')
                {
                    // toggle verbose logging
                    if (!toggle_setting(&settings.verbose))
                    {
                        ESP_LOGE(UART_TAG, "failed!");
                    }
                    bool new_state = get_setting(&settings.verbose);
                    if (new_state)
                    {
                        log_levels_max();
                    }
                    else
                    {
                        log_levels_min();
                    }
                    ESP_LOGI(UART_TAG, "verbose %s", new_state ? "on" : "off");
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
                    // restart esp32
                    uart_restart_command();
                }
                else if (dtmp[0] == 'T')
                {
                    // show task list
                    char buf[1024]; // "min. 40 bytes per task"
                    vTaskList(buf);

                    ESP_LOGI(UART_TAG, "Task List:\nTask Name\tStatus\tPrio\tHWM\tTask\tAffinity\n%s", buf);
                }
                else if (dtmp[0] == 'X')
                {
                    // enter secrets mode
                    uart_secrets_handler();
                }
                else if (dtmp[0] == 'h' || dtmp[0] == '?')
                {
                    ESP_LOGI(UART_TAG, "Device: %s", PGP_CLONE_NAME);
                    ESP_LOGI(UART_TAG, "User Settings (lost on restart unless saved):");
                    ESP_LOGI(UART_TAG, "- s - toggle PGP autospin");
                    ESP_LOGI(UART_TAG, "- c - toggle PGP autocatch");
                    ESP_LOGI(UART_TAG, "- p - toggle powerbank ping");
                    ESP_LOGI(UART_TAG, "- l - toggle verbose logging");
                    ESP_LOGI(UART_TAG, "- S - save user settings permanently");
                    ESP_LOGI(UART_TAG, "Commands:");
                    ESP_LOGI(UART_TAG, "- h,? - help");
                    ESP_LOGI(UART_TAG, "- A - start BT advertising again");
                    ESP_LOGI(UART_TAG, "- X - edit secrets (select eg. slot 2 with 'X2!')");
                    ESP_LOGI(UART_TAG, "- t - show BT connection time");
                    ESP_LOGI(UART_TAG, "- T - show FreeRTOS task list");
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

void uart_secrets_handler()
{
    // uart rx buffer
    uint8_t buf[512];
    // pos in that buffer
    int pos = 0;

    // our signal that we're ready
    uart_write_bytes(EX_UART_NUM, "\n!\n", 3);
    fflush(stdout);

    // secrets slot id
    uint8_t chosen_slot = 9;
    // indicate if a slot was explicitly chosen
    bool slot_chosen = false;

    char state = 0;
    bool isLineBreak = false;
    bool running = true;
    while (running)
    {
        // read one byte at a time to the current buffer position
        int size = uart_read_bytes(EX_UART_NUM, buf + pos, 1, 10000 / portTICK_PERIOD_MS);
        if (size != 1)
        {
            // timeout, leave secrets mode
            break;
        }
        isLineBreak = (*(buf + pos) == '\n' || *(buf + pos) == '\r');

        switch (state)
        {
        case 0:
            // we received a command byte

            // reset buf pos
            pos = 0;
            switch (buf[0])
            {
            case '\n':
            case '\r':
            case 'X':
                // ignore
                break;

            case '?':
            case 'h':
                // help/list slots
                ESP_LOGW(UART_TAG, "Secrets Mode");
                ESP_LOGI(UART_TAG, "User Commands:");
                ESP_LOGI(UART_TAG, "- h,? - help");
                ESP_LOGI(UART_TAG, "- q - leave secrets mode");
                ESP_LOGI(UART_TAG, "- 0-9 - select secrets slot");
                ESP_LOGI(UART_TAG, "- ! - activate selected slot and restart");
                ESP_LOGI(UART_TAG, "- l - list slots");
                ESP_LOGI(UART_TAG, "- C - clear slot");
                ESP_LOGI(UART_TAG, "- there are additional commands but they are for use in secrets_upload.py only!");
                break;

            case 'l':
                show_secrets_slots();
                break;

            case '!':
                // activate selected slot and restart
                if (!slot_chosen)
                {
                    ESP_LOGE(UART_TAG, "no slot chosen!");
                }
                else
                {
                    ESP_LOGW(UART_TAG, "SETTING SLOT=%d AND RESTARTING", chosen_slot);
                    if (set_chosen_device(chosen_slot))
                    {
                        if (write_config_storage())
                        {
                            uart_restart_command();
                        }
                        else
                        {
                            ESP_LOGE(UART_TAG, "failed writing to nvs");
                        }
                    }
                    else
                    {
                        ESP_LOGE(UART_TAG, "failed editing settings");
                    }
                }
                break;

            case 's':
            {
                // get CRC of scratch buffer
                uint32_t crc = get_secrets_crc32(tmp_mac, tmp_device_key, tmp_blob);
                ESP_LOGI(UART_TAG, "slot=tmp crc=%08x", crc);
                break;
            }

            case 'S':
                // read nvs slot to scratch buffer and get CRC of that
                if (slot_chosen)
                {
                    if (read_secrets_id(chosen_slot, tmp_clone_name, tmp_mac, tmp_device_key, tmp_blob))
                    {
                        uint32_t crc = get_secrets_crc32(tmp_mac, tmp_device_key, tmp_blob);
                        ESP_LOGI(UART_TAG, "slot=%d crc=%08x", chosen_slot, crc);
                    }
                    else
                    {
                        ESP_LOGE(UART_TAG, "failed reading slot=%d", chosen_slot);
                    }
                }
                break;

            case '0':
            case '1':
            case '2':
            case '3':
            case '4':
            case '5':
            case '6':
            case '7':
            case '8':
            case '9':
                // select secrets slot
                chosen_slot = buf[0] - '0';
                slot_chosen = true;
                ESP_LOGW(UART_TAG, "slot=%d", chosen_slot);
                break;

            case 'N':
                // set name
            case 'M':
                // set mac
            case 'K':
                // set device key
            case 'B':
                // set blob
                state = buf[0];
                ESP_LOGW(UART_TAG, "set=%c", state);
                break;

            case 'C':
                // clear secret
                if (slot_chosen)
                {
                    ESP_LOGW(UART_TAG, "clear=%d", delete_secrets_id(chosen_slot));
                }
                break;

            case 'W':
                // write secret
                if (slot_chosen)
                {
                    ESP_LOGW(UART_TAG, "write=%d", write_secrets_id(chosen_slot, tmp_clone_name, tmp_mac, tmp_device_key, tmp_blob));
                }
                break;

            case 'q':
                // quit
            default:
                if (buf[0] != 'q')
                {
                    ESP_LOGE(UART_TAG, "invalid command '%c'", buf[0]);
                }
                running = false;
            }
            fflush(stdout);
            break;

        case 'N':
            // set name
            if (pos >= (sizeof(PGP_CLONE_NAME) - 1))
            {
                ESP_LOGE(UART_TAG, "name too long");
                running = false;
            }
            else if (isLineBreak)
            {
                // copy name to global var
                ESP_LOGW(UART_TAG, "N=[OK]");
                // overwrite \n with termination byte
                buf[pos] = 0;
                memcpy(tmp_clone_name, buf, pos + 1);
                // back to command mode
                state = 0;
                pos = 0;
            }
            else
            {
                pos++;
            }
            break;

        case 'M':
            // set mac
        case 'K':
            // set device key
        case 'B':
            // set blob
            if (pos >= (sizeof(buf) - 1))
            {
                // this shouldn't happen normally because our buf is generously oversized
                ESP_LOGE(UART_TAG, "data too long");
                running = false;
            }
            else if (isLineBreak)
            {
                // base64 decode and write to the correct global variable from secrets.c
                if (decode_to_buf(state, buf, pos))
                {
                    ESP_LOGW(UART_TAG, "%c=[OK]", state);
                }
                else
                {
                    ESP_LOGW(UART_TAG, "%c=[FAIL]", state);
                }
                // back to command mode
                state = 0;
                pos = 0;
            }
            else
            {
                pos++;
            }
            break;

        default:
            ESP_LOGE(UART_TAG, "invalid state");
            running = false;
        }
    }

    // our signal that we're done
    uart_write_bytes(EX_UART_NUM, "\nX\n", 3);
    fflush(stdout);
}

// get a base64 encoded buffer (inBuf) with inBytes and write it to the target secret
bool decode_to_buf(char targetType, uint8_t *inBuf, int inBytes)
{
    int len;
    uint8_t *outBuf;

    switch (targetType)
    {
    case 'M':
        // set mac
        outBuf = tmp_mac;
        len = sizeof(tmp_mac);
        break;

    case 'K':
        // set device key
        outBuf = tmp_device_key;
        len = sizeof(tmp_device_key);
        break;

    case 'B':
        // set blob
        outBuf = tmp_blob;
        len = sizeof(tmp_blob);
        break;

    default:
        ESP_LOGE(UART_TAG, "invalid targetType=%c", targetType);
        return false;
    }

    size_t writtenLen = 0;
    int res = mbedtls_base64_decode(outBuf, len, &writtenLen, inBuf, inBytes);

    if (res != 0)
    {
        ESP_LOGE(UART_TAG, "mbedtls_base64_decode returned %d", res);
        return false;
    }

    if (writtenLen != len)
    {
        ESP_LOGE(UART_TAG, "wrong writtenLen! inBytes=%d -base64-> wanted=%d, got=%d", inBytes, len, writtenLen);
        return false;
    }

    return true;
}

void uart_restart_command()
{
    ESP_LOGI(UART_TAG, "closing nvs");
    close_config_storage();
    ESP_LOGI(UART_TAG, "restarting");
    fflush(stdout);
    esp_restart();
}