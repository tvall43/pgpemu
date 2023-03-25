#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/timers.h"
#include "freertos/event_groups.h"
#include "freertos/queue.h"

#include "esp_system.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_bt.h"
#include "esp_gap_ble_api.h"
#include "esp_gatts_api.h"
#include "esp_gatt_common_api.h"
#include "esp_bt_main.h"

#include "pgpemu.h"

#include "config_secrets.h"
#include "config_storage.h"
#include "log_tags.h"
#include "pgp_cert.h"
#include "powerbank.h"
#include "secrets.h"
#include "settings.h"
#include "uart.h"

#define PROFILE_NUM 1
#define PROFILE_APP_IDX 0
#define ESP_APP_ID 0x55

#if 0
#define PGP_DEVICE_NAME "Pokemon PBP"
#define SVC_INST_ID 0
#define BATTERY_INST_ID 1
#define LED_BUTTON_INST_ID 2
#else
#define PGP_DEVICE_NAME "Pokemon GO Plus"
#define BATTERY_INST_ID 0
#define LED_BUTTON_INST_ID 1
#define CERT_INST_ID 2
#endif

/* The max length of characteristic value. When the gatt client write or prepare write,
 *  the data length must be less than MAX_VALUE_LENGTH.
 */
#define MAX_VALUE_LENGTH 500
#define PREPARE_BUF_MAX_SIZE 1024
#define CHAR_DECLARATION_SIZE (sizeof(uint8_t))

#define ADV_CONFIG_FLAG (1 << 0)
#define SCAN_RSP_CONFIG_FLAG (1 << 1)

static uint8_t adv_config_done = 0;
TickType_t connectionStart = 0;
TickType_t connectionEnd = 0;

uint16_t battery_handle_table[BATTERY_LAST_IDX];
uint16_t led_button_handle_table[LED_BUTTON_LAST_IDX];
uint16_t certificate_handle_table[CERT_LAST_IDX];

// in ESP32, bt_mac is base_mac + 2
uint8_t mac[6];
uint8_t bt_mac[6];

uint8_t session_key[16];
int cert_state = 0;

typedef struct
{
    uint16_t handle;
    uint8_t *prepare_buf;
    int prepare_len;
} prepare_type_env_t;

static prepare_type_env_t prepare_write_env;

static QueueHandle_t button_queue;

typedef struct
{
    // which session does this belong to
    esp_gatt_if_t gatts_if;
    uint16_t conn_id;

    // delay after which button is pressed
    int delay;
} button_queue_item_t;

static uint8_t raw_adv_data[] = {
    /* flags */
    0x02, 0x01, 0x06,
    /* device name */
    0x10, 0x09, 'P', 'o', 'k', 'e', 'm', 'o', 'n', ' ', 'G', 'O', ' ', 'P', 'l', 'u', 's',
    // 0x0c, 0x09, 'P', 'o', 'k', 'e', 'm', 'o', 'n', ' ', 'P', 'B', 'P',
    0x06, 0x20, 0x62, 0x04, 0xC5, 0x21, 0x00};
static uint8_t raw_scan_rsp_data[] = {
    /* flags */
    0x02, 0x01, 0x06,
    /* tx power */
    0x02, 0x0a, 0xeb,
    /* service uuid */
    0x03, 0x03, 0xFF, 0x00};

static esp_ble_adv_params_t adv_params = {
    .adv_int_min = 0x20,
    .adv_int_max = 0x40,
    .adv_type = ADV_TYPE_IND,
    .own_addr_type = BLE_ADDR_TYPE_PUBLIC,
    .peer_addr = {0},
    .peer_addr_type = BLE_ADDR_TYPE_PUBLIC,
    .channel_map = ADV_CHNL_ALL,
    .adv_filter_policy = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY,
};

struct gatts_profile_inst
{
    esp_gatts_cb_t gatts_cb;
    uint16_t gatts_if;
    uint16_t app_id;
    uint16_t conn_id;
    uint16_t service_handle;
    esp_gatt_srvc_id_t service_id;
    uint16_t char_handle;
    esp_bt_uuid_t char_uuid;
    esp_gatt_perm_t perm;
    esp_gatt_char_prop_t property;
    uint16_t descr_handle;
    esp_bt_uuid_t descr_uuid;
};

static void gatts_profile_event_handler(esp_gatts_cb_event_t event,
                                        esp_gatt_if_t gatts_if, esp_ble_gatts_cb_param_t *param);

/* One gatt-based profile one app_id and one gatts_if, this array will store the gatts_if returned by ESP_GATTS_REG_EVT */
static struct gatts_profile_inst pgp_profile_tab[PROFILE_NUM] = {
    [PROFILE_APP_IDX] = {
        .gatts_cb = gatts_profile_event_handler,
        .gatts_if = ESP_GATT_IF_NONE, /* Not get the gatt_if, so initial is ESP_GATT_IF_NONE */
        .app_id = 0,
        .conn_id = 0,
        .service_handle = 0,
        .char_handle = 0,
        .descr_handle = 0,
    },
};

/* Service */
static const uint16_t primary_service_uuid = ESP_GATT_UUID_PRI_SERVICE;
static const uint16_t character_declaration_uuid = ESP_GATT_UUID_CHAR_DECLARE;
static const uint16_t character_client_config_uuid = ESP_GATT_UUID_CHAR_CLIENT_CONFIG;
static const uint8_t char_prop_read = ESP_GATT_CHAR_PROP_BIT_READ;
static const uint8_t char_prop_write = ESP_GATT_CHAR_PROP_BIT_WRITE;

static const uint8_t char_prop_read_notify = ESP_GATT_CHAR_PROP_BIT_READ | ESP_GATT_CHAR_PROP_BIT_NOTIFY;
static const uint8_t char_prop_notify = ESP_GATT_CHAR_PROP_BIT_NOTIFY;
static const uint8_t dummy_value[2] = {0x00, 0x00};
static uint8_t cert_buffer[378];

static const uint16_t GATTS_SERVICE_UUID_BATTERY = 0x180f;
static const uint16_t GATTS_CHAR_UUID_BATTERY_LEVEL = 0x2a19;
static const uint8_t battery_char_value[1] = {0x60};

static uint8_t reconnect_challenge[32];
static int has_reconnect_key = 0;

static const esp_gatts_attr_db_t gatt_db_battery[BATTERY_LAST_IDX] = {

    // Service Declaration
    [IDX_BATTERY_SVC] =
        {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *)&primary_service_uuid, ESP_GATT_PERM_READ, sizeof(uint16_t), sizeof(GATTS_SERVICE_UUID_BATTERY), (uint8_t *)&GATTS_SERVICE_UUID_BATTERY}},

    /* Characteristic Declaration */
    [IDX_CHAR_BATTERY_LEVEL] =
        {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *)&character_declaration_uuid, ESP_GATT_PERM_READ, CHAR_DECLARATION_SIZE, CHAR_DECLARATION_SIZE, (uint8_t *)&char_prop_read_notify}},
    /* Characteristic Value */
    [IDX_CHAR_BATTERY_LEVEL_VAL] =
        {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *)&GATTS_CHAR_UUID_BATTERY_LEVEL, ESP_GATT_PERM_READ, MAX_VALUE_LENGTH, sizeof(battery_char_value), (uint8_t *)battery_char_value}},

    /* Client Characteristic Configuration Descriptor */
    [IDX_CHAR_BATTERY_LEVEL_CFG] =
        {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *)&character_client_config_uuid, ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE, sizeof(uint16_t), sizeof(dummy_value), (uint8_t *)dummy_value}},
};

// uuid in reversed order
uint8_t GATTS_SERVICE_UUID_LED_BUTTON[ESP_UUID_LEN_128] = {0xeb, 0x9a, 0x93, 0xb9, 0xb5, 0x82, 0x4c, 0x5c, 0xa3, 0x63, 0xcb, 0x67, 0x62, 0x4, 0xc5, 0x21};
uint8_t GATTS_CHAR_UUID_LED[ESP_UUID_LEN_128] = {0xec, 0x9a, 0x93, 0xb9, 0xb5, 0x82, 0x4c, 0x5c, 0xa3, 0x63, 0xcb, 0x67, 0x62, 0x4, 0xc5, 0x21};
uint8_t GATTS_CHAR_UUID_BUTTON[ESP_UUID_LEN_128] = {0xed, 0x9a, 0x93, 0xb9, 0xb5, 0x82, 0x4c, 0x5c, 0xa3, 0x63, 0xcb, 0x67, 0x62, 0x4, 0xc5, 0x21};
uint8_t GATTS_CHAR_UUID_UNKNOWN[ESP_UUID_LEN_128] = {0xee, 0x9a, 0x93, 0xb9, 0xb5, 0x82, 0x4c, 0x5c, 0xa3, 0x63, 0xcb, 0x67, 0x62, 0x4, 0xc5, 0x21};
uint8_t GATTS_CHAR_UUID_UPDATE_REQUEST[ESP_UUID_LEN_128] = {0xef, 0x9a, 0x93, 0xb9, 0xb5, 0x82, 0x4c, 0x5c, 0xa3, 0x63, 0xcb, 0x67, 0x62, 0x4, 0xc5, 0x21};
uint8_t GATTS_CHAR_UUID_FW_VERSION[ESP_UUID_LEN_128] = {0xf0, 0x9a, 0x93, 0xb9, 0xb5, 0x82, 0x4c, 0x5c, 0xa3, 0x63, 0xcb, 0x67, 0x62, 0x4, 0xc5, 0x21};

static const uint8_t led_char_value[100] = {0};

static const uint8_t button_value[2] = {0};

uint8_t GATTS_SERVICE_UUID_CERTIFICATE[ESP_UUID_LEN_128] = {0x37, 0x8e, 0xd, 0xef, 0x8e, 0x8b, 0x7f, 0xab, 0x33, 0x44, 0x89, 0x5b, 0x9, 0x77, 0xe8, 0xbb};
uint8_t GATTS_CHAR_UUID_CENTRAL_TO_SFIDA[ESP_UUID_LEN_128] = {0x38, 0x8e, 0xd, 0xef, 0x8e, 0x8b, 0x7f, 0xab, 0x33, 0x44, 0x89, 0x5b, 0x9, 0x77, 0xe8, 0xbb};
uint8_t GATTS_CHAR_UUID_SFIDA_COMMANDS[ESP_UUID_LEN_128] = {0x39, 0x8e, 0xd, 0xef, 0x8e, 0x8b, 0x7f, 0xab, 0x33, 0x44, 0x89, 0x5b, 0x9, 0x77, 0xe8, 0xbb};
uint8_t GATTS_CHAR_UUID_SFIDA_TO_CENTRAL[ESP_UUID_LEN_128] = {0x3a, 0x8e, 0xd, 0xef, 0x8e, 0x8b, 0x7f, 0xab, 0x33, 0x44, 0x89, 0x5b, 0x9, 0x77, 0xe8, 0xbb};
/*
  {ESP_GATT_AUTO_RSP/ESP_GATT_RSP_BY_APP},
  {uuid_length, uuid_val, attribute permission, max length of element, current length of element, element value array}
*/

static const esp_gatts_attr_db_t gatt_db_led_button[LED_BUTTON_LAST_IDX] = {
    // Service Declaration
    [IDX_LED_BUTTON_SVC] =
        {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *)&primary_service_uuid, ESP_GATT_PERM_READ, ESP_UUID_LEN_128, ESP_UUID_LEN_128, (uint8_t *)&GATTS_SERVICE_UUID_LED_BUTTON}},

    /* Characteristic Declaration */
    [IDX_CHAR_LED] =
        {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *)&character_declaration_uuid, ESP_GATT_PERM_READ, CHAR_DECLARATION_SIZE, CHAR_DECLARATION_SIZE, (uint8_t *)&char_prop_write}},
    [IDX_CHAR_LED_VAL] =
        {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_128, (uint8_t *)&GATTS_CHAR_UUID_LED, ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE, MAX_VALUE_LENGTH, sizeof(led_char_value), (uint8_t *)led_char_value}},

    [IDX_CHAR_BUTTON] =
        {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *)&character_declaration_uuid, ESP_GATT_PERM_READ, CHAR_DECLARATION_SIZE, CHAR_DECLARATION_SIZE, (uint8_t *)&char_prop_notify}},
    [IDX_CHAR_BUTTON_VAL] =
        {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_128, (uint8_t *)&GATTS_CHAR_UUID_BUTTON, ESP_GATT_PERM_READ, MAX_VALUE_LENGTH, sizeof(button_value), (uint8_t *)button_value}},

    /* Client Characteristic Configuration Descriptor */
    [IDX_CHAR_BUTTON_CFG] =
        {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *)&character_client_config_uuid, ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE, sizeof(uint16_t), sizeof(dummy_value), (uint8_t *)dummy_value}},

    /* Characteristic Declaration */
    [IDX_CHAR_UNKNOWN] =
        {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *)&character_declaration_uuid, ESP_GATT_PERM_READ, CHAR_DECLARATION_SIZE, CHAR_DECLARATION_SIZE, (uint8_t *)&char_prop_write}},
    [IDX_CHAR_UNKNOWN_VAL] =
        {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_128, (uint8_t *)&GATTS_CHAR_UUID_UNKNOWN, ESP_GATT_PERM_READ, MAX_VALUE_LENGTH, sizeof(led_char_value), (uint8_t *)led_char_value}},

    /* Characteristic Declaration */
    [IDX_CHAR_UPDATE_REQUEST] =
        {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *)&character_declaration_uuid, ESP_GATT_PERM_READ, CHAR_DECLARATION_SIZE, CHAR_DECLARATION_SIZE, (uint8_t *)&char_prop_write}},
    [IDX_CHAR_UPDATE_REQUEST_VAL] =
        {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_128, (uint8_t *)&GATTS_CHAR_UUID_UPDATE_REQUEST, ESP_GATT_PERM_READ, MAX_VALUE_LENGTH, sizeof(led_char_value), (uint8_t *)led_char_value}},

    /* Characteristic Declaration */
    [IDX_CHAR_FW_VERSION] =
        {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *)&character_declaration_uuid, ESP_GATT_PERM_READ, CHAR_DECLARATION_SIZE, CHAR_DECLARATION_SIZE, (uint8_t *)&char_prop_read}},
    [IDX_CHAR_FW_VERSION_VAL] =
        {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_128, (uint8_t *)&GATTS_CHAR_UUID_FW_VERSION, ESP_GATT_PERM_READ, MAX_VALUE_LENGTH, sizeof(led_char_value), (uint8_t *)led_char_value}},

};

static const esp_gatts_attr_db_t gatt_db_certificate[CERT_LAST_IDX] = {
    // Service Declaration
    [IDX_CERT_SVC] =
        {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *)&primary_service_uuid, ESP_GATT_PERM_READ, ESP_UUID_LEN_128, ESP_UUID_LEN_128, (uint8_t *)&GATTS_SERVICE_UUID_CERTIFICATE}},

    /* Characteristic Declaration */
    [IDX_CHAR_CENTRAL_TO_SFIDA] =
        {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *)&character_declaration_uuid, ESP_GATT_PERM_READ, CHAR_DECLARATION_SIZE, CHAR_DECLARATION_SIZE, (uint8_t *)&char_prop_write}},
    [IDX_CHAR_CENTRAL_TO_SFIDA_VAL] =
        {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_128, (uint8_t *)&GATTS_CHAR_UUID_CENTRAL_TO_SFIDA, ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE, MAX_VALUE_LENGTH, sizeof(led_char_value), (uint8_t *)led_char_value}},

    [IDX_CHAR_SFIDA_COMMANDS] =
        {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *)&character_declaration_uuid, ESP_GATT_PERM_READ, CHAR_DECLARATION_SIZE, CHAR_DECLARATION_SIZE, (uint8_t *)&char_prop_notify}},
    [IDX_CHAR_SFIDA_COMMANDS_VAL] =
        {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_128, (uint8_t *)&GATTS_CHAR_UUID_SFIDA_COMMANDS, ESP_GATT_PERM_READ, MAX_VALUE_LENGTH, sizeof(led_char_value), (uint8_t *)led_char_value}},

    /* Client Characteristic Configuration Descriptor */
    [IDX_CHAR_SFIDA_COMMANDS_CFG] =
        {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *)&character_client_config_uuid, ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE, sizeof(uint16_t), sizeof(dummy_value), (uint8_t *)dummy_value}},

    /* Characteristic Declaration */
    [IDX_CHAR_SFIDA_TO_CENTRAL] =
        {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *)&character_declaration_uuid, ESP_GATT_PERM_READ, CHAR_DECLARATION_SIZE, CHAR_DECLARATION_SIZE, (uint8_t *)&char_prop_read}},
    [IDX_CHAR_SFIDA_TO_CENTRAL_VAL] =
        {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_128, (uint8_t *)&GATTS_CHAR_UUID_SFIDA_TO_CENTRAL, ESP_GATT_PERM_READ, MAX_VALUE_LENGTH, sizeof(cert_buffer), (uint8_t *)cert_buffer}},
};

const char *battery_char_names[] = {
    "BATTERY_SVC",
    "CHAR_BATTERY_LEVEL",
    "CHAR_BATTERY_LEVEL_VAL",
    "CHAR_BATTERY_LEVEL_CFG",
};

const char *led_button_char_names[] = {
    "LED_BUTTON_SVC",
    "CHAR_LED",
    "CHAR_LED_VAL",
    "CHAR_BUTTON",
    "CHAR_BUTTON_VAL",
    "CHAR_BUTTON_CFG",
    "CHAR_UNKNOWN",
    "CHAR_UNKNOWN_VAL",
    "CHAR_UPDATE_REQUEST",
    "CHAR_UPDATE_REQUEST_VAL",
    "CHAR_FW_VERSION",
    "CHAR_FW_VERSION_VAL"};

const char *cert_char_names[] = {
    "CERT_SVC",
    "CHAR_CENTRAL_TO_SFIDA",
    "CHAR_CENTRAL_TO_SFIDA_VAL",
    "CHAR_SFIDA_COMMANDS",
    "CHAR_SFIDA_COMMANDS_VAL",
    "CHAR_SFIDA_COMMANDS_CFG",
    "CHAR_SFIDA_TO_CENTRAL",
    "CHAR_SFIDA_TO_CENTRAL_VAL"};

const char *UNKNOWN_HANDLE_NAME = "<UNKNOWN HANDLE NAME>";

static int find_handle_index(uint16_t handle, uint16_t *handle_table, int count)
{
    for (int i = 0; i < count; i++)
    {
        if (handle_table[i] == handle)
            return i;
    }
    return -1;
}

// for debugging
static const char *char_name_from_handle(uint16_t handle)
{
    int idx;
    idx = find_handle_index(handle, battery_handle_table, BATTERY_LAST_IDX);
    if (idx >= 0)
        return battery_char_names[idx];
    idx = find_handle_index(handle, led_button_handle_table, LED_BUTTON_LAST_IDX);
    if (idx >= 0)
        return led_button_char_names[idx];

    idx = find_handle_index(handle, certificate_handle_table, CERT_LAST_IDX);
    if (idx >= 0)
        return cert_char_names[idx];

    return UNKNOWN_HANDLE_NAME;
}

static void generate_first_challenge()
{
    // fill in cert_buffer
    uint8_t the_challenge[16];
    memset(the_challenge, 0x41, 16);
    uint8_t main_nonce[16];
    memset(main_nonce, 0x42, 16);

    // use fixed key for easier debugging
    memset(session_key, 0x43, 16);
    uint8_t outer_nonce[16];
    memset(outer_nonce, 0x44, 16);
    generate_chal_0(bt_mac, the_challenge, main_nonce, session_key, outer_nonce,
                    (struct challenge_data *)cert_buffer);
}

void pgp_advertise()
{
    esp_ble_gap_start_advertising(&adv_params);
}

static void gap_event_handler(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param)
{
    switch (event)
    {
    case ESP_GAP_BLE_ADV_DATA_RAW_SET_COMPLETE_EVT:
        adv_config_done &= (~ADV_CONFIG_FLAG);
        if (adv_config_done == 0)
        {
            esp_ble_gap_start_advertising(&adv_params);
        }
        break;
    case ESP_GAP_BLE_SCAN_RSP_DATA_RAW_SET_COMPLETE_EVT:
        adv_config_done &= (~SCAN_RSP_CONFIG_FLAG);
        if (adv_config_done == 0)
        {
            esp_ble_gap_start_advertising(&adv_params);
        }
        break;
    case ESP_GAP_BLE_ADV_START_COMPLETE_EVT:
        /* advertising start complete event to indicate advertising start successfully or failed */
        if (param->adv_start_cmpl.status != ESP_BT_STATUS_SUCCESS)
        {
            ESP_LOGE(BT_TAG, "advertising start failed");
        }
        else
        {
            ESP_LOGI(BT_TAG, "advertising start successfully");
        }
        break;
    case ESP_GAP_BLE_ADV_STOP_COMPLETE_EVT:
        if (param->adv_stop_cmpl.status != ESP_BT_STATUS_SUCCESS)
        {
            ESP_LOGE(BT_TAG, "Advertising stop failed");
        }
        else
        {
            ESP_LOGI(BT_TAG, "Stop adv successfully\n");
        }
        break;
    case ESP_GAP_BLE_UPDATE_CONN_PARAMS_EVT:
        ESP_LOGI(BT_TAG, "update connection params status = %d, min_int = %d, max_int = %d,conn_int = %d,latency = %d, timeout = %d",
                 param->update_conn_params.status,
                 param->update_conn_params.min_int,
                 param->update_conn_params.max_int,
                 param->update_conn_params.conn_int,
                 param->update_conn_params.latency,
                 param->update_conn_params.timeout);
        break;
    default:
        break;
    }
}

void pgp_prepare_write_event_env(esp_gatt_if_t gatts_if, prepare_type_env_t *prepare_write_env, esp_ble_gatts_cb_param_t *param)
{
    ESP_LOGI(BT_TAG, "prepare write, handle = %d, value len = %d", param->write.handle, param->write.len);
    esp_gatt_status_t status = ESP_GATT_OK;
    if (prepare_write_env->prepare_buf == NULL)
    {
        prepare_write_env->handle = param->write.handle;
        prepare_write_env->prepare_buf = (uint8_t *)malloc(PREPARE_BUF_MAX_SIZE * sizeof(uint8_t));
        prepare_write_env->prepare_len = 0;
        if (prepare_write_env->prepare_buf == NULL)
        {
            ESP_LOGE(BT_TAG, "%s, Gatt_server prep no mem", __func__);
            status = ESP_GATT_NO_RESOURCES;
        }
    }
    else
    {
        if (param->write.offset > PREPARE_BUF_MAX_SIZE)
        {
            status = ESP_GATT_INVALID_OFFSET;
        }
        else if ((param->write.offset + param->write.len) > PREPARE_BUF_MAX_SIZE)
        {
            status = ESP_GATT_INVALID_ATTR_LEN;
        }
    }
    /*send response when param->write.need_rsp is true */
    if (param->write.need_rsp)
    {
        esp_gatt_rsp_t *gatt_rsp = (esp_gatt_rsp_t *)malloc(sizeof(esp_gatt_rsp_t));
        if (gatt_rsp != NULL)
        {

            /*
                gatt_rsp->attr_value.len = param->write.len;
                gatt_rsp->attr_value.handle = param->write.handle;
                gatt_rsp->attr_value.offset = param->write.offset;
                gatt_rsp->attr_value.auth_req = ESP_GATT_AUTH_REQ_NONE;
                memcpy(gatt_rsp->attr_value.value, param->write.value, param->write.len);
                esp_err_t response_err = esp_ble_gatts_send_response(gatts_if, param->write.conn_id, param->write.trans_id, status, gatt_rsp);
                if (response_err != ESP_OK){
                   ESP_LOGE(GATTS_TABLE_TAG, "Send response error");
                }
                free(gatt_rsp);
            */
        }
        else
        {
            ESP_LOGE(BT_TAG, "%s, malloc failed", __func__);
        }
    }
    if (status != ESP_GATT_OK)
    {
        return;
    }
    memcpy(prepare_write_env->prepare_buf + param->write.offset,
           param->write.value,
           param->write.len);
    prepare_write_env->prepare_len += param->write.len;
}

// prepare_write_env->prepare_buf
// param->write.len
// param->write.conn_id
void handle_protocol(esp_gatt_if_t gatts_if,
                     const uint8_t *prepare_buf, int datalen,
                     int conn_id)
{
    if (cert_state >= 1)
    {
        ESP_LOGD(CERT_TAG, "Cert state %d, received %d", cert_state, datalen);
    }

    switch (cert_state)
    {
    case 0:
    {
        if (datalen == 20)
        { // just assume server responds correctly
            uint8_t notify_data[4];
            memset(notify_data, 0, 4);
            notify_data[0] = 0x01;

            uint8_t nonce[16];

            memset(nonce, 0x42, 16);

            uint8_t temp[52];

            struct next_challenge *chal = (struct next_challenge *)temp;
            memset(temp, 0, sizeof(temp));
            generate_next_chal(0, session_key, nonce, chal);
            temp[0] = 0x01;

            memcpy(cert_buffer, temp, 52);

            // ESP_LOG_BUFFER_HEX(CERT_TAG, temp, sizeof(temp));

            esp_ble_gatts_set_attr_value(certificate_handle_table[IDX_CHAR_SFIDA_TO_CENTRAL_VAL], 52, cert_buffer);

            // ESP_LOG_BUFFER_HEX(CERT_TAG, cert_buffer, 52);

            cert_state = 1;
            esp_ble_gatts_send_indicate(gatts_if, conn_id, certificate_handle_table[IDX_CHAR_SFIDA_COMMANDS_VAL], sizeof(notify_data), notify_data, false);
        }
        else
        {
            ESP_LOGE(CERT_TAG, "App sends incorrect len=%d", datalen);
        }
        break;
    }
    case 1:
    {
        // we need to decrypt and send challenge data from APP
        ESP_LOGD(CERT_TAG, "Received: %d", datalen);

        uint8_t temp[20];
        memset(temp, 0, sizeof(temp));
        decrypt_next(prepare_buf, session_key, temp + 4);
        temp[0] = 0x02;
        uint8_t notify_data[4];
        memset(notify_data, 0, 4);
        notify_data[0] = 0x02;

        ESP_LOGD(CERT_TAG, "Sending response");
        if (esp_log_level_get(CERT_TAG) >= ESP_LOG_DEBUG)
        {
            ESP_LOG_BUFFER_HEX(CERT_TAG, temp, sizeof(temp));
        }

        memcpy(cert_buffer, temp, 20);
        esp_ble_gatts_set_attr_value(certificate_handle_table[IDX_CHAR_SFIDA_TO_CENTRAL_VAL], 20, cert_buffer);
        cert_state = 2;
        esp_ble_gatts_send_indicate(gatts_if, conn_id, certificate_handle_table[IDX_CHAR_SFIDA_COMMANDS_VAL], sizeof(notify_data), notify_data, false);
        break;
    }
    case 2:
    {
        // TODO: what do we use the data for?
        ESP_LOGD(CERT_TAG, "OK");

        uint8_t temp[20];
        memset(temp, 0, sizeof(temp));
        decrypt_next(prepare_buf, session_key, temp + 4);

        if (esp_log_level_get(CERT_TAG) >= ESP_LOG_DEBUG)
        {

            ESP_LOG_BUFFER_HEX(CERT_TAG, temp, sizeof(temp));
        }

        has_reconnect_key = 1;
        // should be random, but this is easier for debugging
        memset(reconnect_challenge, 0x46, 32);

        uint8_t notify_data[4] = {0x04, 0x00, 0x23, 0x00};
        cert_state = 6;
        esp_ble_gatts_send_indicate(gatts_if,
                                    conn_id,
                                    certificate_handle_table[IDX_CHAR_SFIDA_COMMANDS_VAL],
                                    sizeof(notify_data), notify_data, false);
        break;
    }
    case 3:
    {
        if (datalen == 20)
        { // just assume server responds correctly
            ESP_LOGD(CERT_TAG, "OK");
            if (esp_log_level_get(CERT_TAG) >= ESP_LOG_DEBUG)
            {
                ESP_LOG_BUFFER_HEX(CERT_TAG, prepare_buf,
                                   datalen);
            }

            uint8_t notify_data[4] = {0x04, 0x00, 0x01, 0x00};
            esp_ble_gatts_send_indicate(gatts_if,
                                        conn_id,
                                        certificate_handle_table[IDX_CHAR_SFIDA_COMMANDS_VAL],
                                        sizeof(notify_data), notify_data, false);
            cert_state = 4;
        }
        break;
    }
    case 4:
    {
        ESP_LOGD(CERT_TAG, "OK");

        memset(cert_buffer, 0, 4);
        generate_reconnect_response(session_key, prepare_buf + 4, cert_buffer + 4);
        cert_buffer[0] = 5;

        if (esp_log_level_get(CERT_TAG) >= ESP_LOG_DEBUG)
        {
            ESP_LOG_BUFFER_HEX(CERT_TAG, cert_buffer, 20);
        }

        uint8_t notify_data[4];
        memset(notify_data, 0, 4);
        notify_data[0] = 0x05;

        esp_ble_gatts_set_attr_value(certificate_handle_table[IDX_CHAR_SFIDA_TO_CENTRAL_VAL], 20, cert_buffer);
        esp_ble_gatts_send_indicate(gatts_if, conn_id, certificate_handle_table[IDX_CHAR_SFIDA_COMMANDS_VAL], sizeof(notify_data), notify_data, false);
        cert_state = 5;
        break;
    }
    case 5:
    {
        if (datalen == 5)
        { // just assume server responds correctly
            ESP_LOGD(CERT_TAG, "OK");

            uint8_t notify_data[4] = {0x04, 0x00, 0x02, 0x00};
            esp_ble_gatts_send_indicate(gatts_if,
                                        conn_id,
                                        certificate_handle_table[IDX_CHAR_SFIDA_COMMANDS_VAL],
                                        sizeof(notify_data), notify_data, false);
            cert_state = 6;
        }
        break;
    }
    default:
        ESP_LOGE(CERT_TAG, "Unhandled state: %d", cert_state);
        break;
    }
}

void handle_led_notify_from_app(esp_gatt_if_t gatts_if, uint16_t conn_id, const uint8_t *buffer)
{
    int number_of_patterns = buffer[3] & 0x1f;
    int priority = (buffer[3] >> 5) & 0x7;

    ESP_LOGI(PGPEMU_TAG, "LED: Pattern Count = %d, priority = %d", number_of_patterns, priority);

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
        ESP_LOGD(PGPEMU_TAG, "*(%3d) #%x%x%x %c%c", duration, red, green, blue, vib_ch, inter_ch);

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

    ESP_LOGI(PGPEMU_TAG, "LED: Pattern total duration: %d ms", pattern_duration * 50);

    bool press_button = false;

    if (count_off && !count_notoff)
    {
        ESP_LOGI(PGPEMU_TAG, "Turn LEDs off.");
    }
    else if (count_white && count_white == count_notoff)
    {
        // only white
        ESP_LOGW(PGPEMU_TAG, "Can't spin Pokestop. Bag is full.");
    }
    else if (count_red && count_off && count_red == count_notoff)
    {
        // blinking just red
        ESP_LOGW(PGPEMU_TAG, "Pokeballs are empty or Pokestop went out of range.");
    }
    else if (count_red && !count_off && count_red == count_notoff)
    {
        // only red
        ESP_LOGW(PGPEMU_TAG, "Can't spin Pokestop for some reason.");
    }
    else if (count_green && count_green == count_notoff)
    {
        // blinking green
        ESP_LOGI(PGPEMU_TAG, "Pokemon in range!");
        if (get_setting(&settings.autocatch))
        {
            press_button = true;
        }
    }
    else if (count_yellow && count_yellow == count_notoff)
    {
        // blinking yellow
        ESP_LOGI(PGPEMU_TAG, "New Pokemon in range!");
        press_button = true;
    }
    else if (count_blue && count_blue == count_notoff)
    {
        // blinking blue
        ESP_LOGI(PGPEMU_TAG, "Pokestop in range!");
        if (get_setting(&settings.autospin))
        {
            press_button = true;
        }
    }
    else if (count_ballshake)
    {
        if (count_blue && count_green)
        {
            ESP_LOGI(PGPEMU_TAG, "Caught Pokemon after %d ball shakes.", count_ballshake);
        }
        else if (count_red)
        {
            ESP_LOGI(PGPEMU_TAG, "Pokemon fled after %d ball shakes.", count_ballshake);
        }
        else
        {
            ESP_LOGW(PGPEMU_TAG, "I don't know what the Pokemon did after %d ball shakes.", count_ballshake);
        }
    }
    else if (count_red && count_green && count_blue && !count_off)
    {
        // blinking grb-grb...
        ESP_LOGI(PGPEMU_TAG, "Got items from Pokestop.");
    }
    else
    {
        if (get_setting(&settings.autospin) || get_setting(&settings.autocatch))
        {
            ESP_LOGW(PGPEMU_TAG, "Unhandled Color pattern, pushing button in any case");
            press_button = true;
        }
        else
        {
            ESP_LOGW(PGPEMU_TAG, "Unhandled Color pattern");
        }
    }

    if (press_button)
    {
        int pattern_ms = pattern_duration * 50;

        // random button press delay between 1000 and 2500 ms
        int delay = 1000 + esp_random() % 1501;
        if (delay < pattern_ms)
        {
            ESP_LOGI(PGPEMU_TAG, "queueing push button after %d ms, conn_id = %d", delay, conn_id);

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
        ESP_LOGW(PGPEMU_TAG, "button press vs. duration mismatch");
    }
}

void pgp_exec_write_event_env(esp_gatt_if_t gatts_if, prepare_type_env_t *prepare_write_env, esp_ble_gatts_cb_param_t *param)
{
    if (param->exec_write.exec_write_flag == ESP_GATT_PREP_WRITE_EXEC && prepare_write_env->prepare_buf)
    {
        ESP_LOG_BUFFER_HEX(BT_TAG, prepare_write_env->prepare_buf, prepare_write_env->prepare_len);

        // it seems that this will be called only by Android version of the Pokemon Go App
        ESP_LOGI(BT_TAG, "WRITE EVT");

        if (certificate_handle_table[IDX_CHAR_CENTRAL_TO_SFIDA_VAL] == prepare_write_env->handle)
        {

            handle_protocol(gatts_if,
                            prepare_write_env->prepare_buf,
                            prepare_write_env->prepare_len,
                            param->write.conn_id);
        }
        else if (led_button_handle_table[IDX_CHAR_LED_VAL] == prepare_write_env->handle)
        {
            handle_led_notify_from_app(gatts_if, param->write.conn_id, prepare_write_env->prepare_buf);
        }
    }
    else
    {
        ESP_LOGI(BT_TAG, "ESP_GATT_PREP_WRITE_CANCEL");
    }
    if (prepare_write_env->prepare_buf)
    {
        free(prepare_write_env->prepare_buf);
        prepare_write_env->prepare_buf = NULL;
    }
    prepare_write_env->prepare_len = 0;
}

static void gatts_profile_event_handler(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if, esp_ble_gatts_cb_param_t *param)
{
    switch (event)
    {
    case ESP_GATTS_REG_EVT:
    {
        esp_err_t set_dev_name_ret = esp_ble_gap_set_device_name(PGP_DEVICE_NAME);
        if (set_dev_name_ret)
        {
            ESP_LOGE(BT_TAG, "set device name failed, error code = %x", set_dev_name_ret);
        }
        esp_err_t raw_adv_ret = esp_ble_gap_config_adv_data_raw(raw_adv_data, sizeof(raw_adv_data));
        if (raw_adv_ret)
        {
            ESP_LOGE(BT_TAG, "config raw adv data failed, error code = %x ", raw_adv_ret);
        }
        adv_config_done |= ADV_CONFIG_FLAG;

        esp_err_t raw_scan_ret = esp_ble_gap_config_scan_rsp_data_raw(raw_scan_rsp_data, sizeof(raw_scan_rsp_data));
        if (raw_scan_ret)
        {
            ESP_LOGE(BT_TAG, "config raw scan rsp data failed, error code = %x", raw_scan_ret);
        }
        adv_config_done |= SCAN_RSP_CONFIG_FLAG;
        /* esp_err_t create_attr_ret = esp_ble_gatts_create_attr_tab(gatt_db, gatts_if, HRS_IDX_NB, SVC_INST_ID); */
        /* if (create_attr_ret){ */
        /*     ESP_LOGE(GATTS_TABLE_TAG, "create attr table failed, error code = %x", create_attr_ret); */
        /* } */
        esp_err_t create_attr_ret = esp_ble_gatts_create_attr_tab(gatt_db_battery, gatts_if, BATTERY_LAST_IDX, BATTERY_INST_ID);
        if (create_attr_ret)
        {
            ESP_LOGE(BT_TAG, "create attr table for battery failed, error code = %x", create_attr_ret);
        }

        create_attr_ret = esp_ble_gatts_create_attr_tab(gatt_db_led_button, gatts_if, LED_BUTTON_LAST_IDX, LED_BUTTON_INST_ID);
        if (create_attr_ret)
        {
            ESP_LOGE(BT_TAG, "create attr table for led button failed, error code = %x", create_attr_ret);
        }

        create_attr_ret = esp_ble_gatts_create_attr_tab(gatt_db_certificate, gatts_if, CERT_LAST_IDX, CERT_INST_ID);
        if (create_attr_ret)
        {
            ESP_LOGE(BT_TAG, "create attr table for cert  failed, error code = %x", create_attr_ret);
        }
    }
    break;
    case ESP_GATTS_READ_EVT:
        ESP_LOGI(BT_TAG, "ESP_GATTS_READ_EVT state=%d %s", cert_state,
                 char_name_from_handle(param->read.handle));
        if (cert_state == 1)
        {
            ESP_LOGD(BT_TAG, "DATA SENT TO APP");
            if (esp_log_level_get(BT_TAG) >= ESP_LOG_DEBUG)
            {
                ESP_LOG_BUFFER_HEX(BT_TAG, cert_buffer, 52);
            }
        }
        break;
    case ESP_GATTS_WRITE_EVT:
        ESP_LOGI(BT_TAG, "ESP_GATTS_WRITE_EVT: %s", char_name_from_handle(param->write.handle));
        if (!param->write.is_prep)
        {
            // the data length of gattc write  must be less than MAX_VALUE_LENGTH.
            ESP_LOGI(BT_TAG, "GATT_WRITE_EVT (state=%d), handle = %d, value len = %d, value :", cert_state, param->write.handle, param->write.len);
            ESP_LOGD(BT_TAG, "DATA FROM APP");
            if (esp_log_level_get(BT_TAG) >= ESP_LOG_DEBUG)
            {
                ESP_LOG_BUFFER_HEX(BT_TAG, param->write.value, param->write.len);
            }

            if (certificate_handle_table[IDX_CHAR_SFIDA_COMMANDS_CFG] == param->write.handle)
            {

                uint16_t descr_value = param->write.value[1] << 8 | param->write.value[0];
                if (descr_value == 0x0001)
                {

                    uint8_t notify_data[4];
                    memset(notify_data, 0, 4);

                    if (has_reconnect_key)
                    {
                        memset(cert_buffer, 0, 36);
                        cert_buffer[0] = 3;
                        notify_data[0] = 3;

                        memcpy(cert_buffer + 4, reconnect_challenge, 32);
                        esp_ble_gatts_set_attr_value(certificate_handle_table[IDX_CHAR_SFIDA_TO_CENTRAL_VAL], 36, cert_buffer);
                        cert_state = 3;
                    }
                    else
                    {
                        generate_first_challenge();
                        esp_ble_gatts_set_attr_value(certificate_handle_table[IDX_CHAR_SFIDA_TO_CENTRAL_VAL], 378, cert_buffer);
                    }

                    ESP_LOGI(BT_TAG, "start CERT PAIRING len = %d", param->write.len);
                    ESP_LOGI(BT_TAG, "notify enable");
                    // the size of notify_data[] need less than MTU size
                    esp_ble_gatts_send_indicate(gatts_if, param->write.conn_id, certificate_handle_table[IDX_CHAR_SFIDA_COMMANDS_VAL],
                                                sizeof(notify_data), notify_data, false);
                }
                else if (descr_value == 0x000)
                {
                    ESP_LOGI(BT_TAG, "notify disable");
                }
            }
            else if (certificate_handle_table[IDX_CHAR_CENTRAL_TO_SFIDA_VAL] == param->write.handle)
            {
                ESP_LOGI(BT_TAG, "Write CENTRAL TO SFIDA: state=%d len=%d", cert_state, param->write.len);

                handle_protocol(gatts_if,
                                param->write.value,
                                param->write.len,
                                param->write.conn_id);
            }
            else if (led_button_handle_table[IDX_CHAR_LED_VAL] == param->write.handle)
            {
                handle_led_notify_from_app(gatts_if, param->write.conn_id, param->write.value);
                return;
            }
            else if (led_button_handle_table[IDX_CHAR_BUTTON_CFG] == param->write.handle)
            {
                ESP_LOGW(BT_TAG, "unhandled CHAR_BUTTON_CFG");
                return;
            }
            else
            {
                ESP_LOGW(BT_TAG, "unhandled data: handle: %d", param->write.handle);
                // dump all tables
                for (int i = 0; i < BATTERY_LAST_IDX; i++)
                {
                    ESP_LOGD(BT_TAG, "handle: batt %d=%s",
                             battery_handle_table[i],
                             char_name_from_handle(battery_handle_table[i]));
                }
                for (int i = 0; i < LED_BUTTON_LAST_IDX; i++)
                {
                    ESP_LOGD(BT_TAG, "handle: ledb %d=%s",
                             led_button_handle_table[i],
                             char_name_from_handle(led_button_handle_table[i]));
                }
                for (int i = 0; i < CERT_LAST_IDX; i++)
                {
                    ESP_LOGD(BT_TAG, "handle: cert %d=%s",
                             certificate_handle_table[i],
                             char_name_from_handle(certificate_handle_table[i]));
                }
            }

            /* send response when param->write.need_rsp is true*/
            if (param->write.need_rsp)
            {
                esp_ble_gatts_send_response(gatts_if, param->write.conn_id, param->write.trans_id, ESP_GATT_OK, NULL);
            }
        }
        else
        {
            /* handle prepare write */
            pgp_prepare_write_event_env(gatts_if, &prepare_write_env, param);
        }
        break;
    case ESP_GATTS_EXEC_WRITE_EVT:
        // the length of gattc prapare write data must be less than MAX_VALUE_LENGTH.
        ESP_LOGD(BT_TAG, "ESP_GATTS_EXEC_WRITE_EVT");
        pgp_exec_write_event_env(gatts_if, &prepare_write_env, param);
        break;
    case ESP_GATTS_MTU_EVT:
        ESP_LOGD(BT_TAG, "ESP_GATTS_MTU_EVT, MTU %d", param->mtu.mtu);
        break;
    case ESP_GATTS_CONF_EVT:
        ESP_LOGD(BT_TAG, "ESP_GATTS_CONF_EVT, status = %d", param->conf.status);
        break;
    case ESP_GATTS_START_EVT:
        ESP_LOGD(BT_TAG, "SERVICE_START_EVT, status %d, service_handle %d", param->start.status, param->start.service_handle);
        break;
    case ESP_GATTS_CONNECT_EVT:
        ESP_LOGD(BT_TAG, "ESP_GATTS_CONNECT_EVT, conn_id = %d", param->connect.conn_id);
        if (esp_log_level_get(BT_TAG) >= ESP_LOG_DEBUG)
        {
            ESP_LOG_BUFFER_HEX(BT_TAG, param->connect.remote_bda, 6);
        }

        esp_ble_conn_update_params_t conn_params = {
            .min_int = 0,
            .max_int = 0,
            .latency = 0,
            .timeout = 0,
        };
        memcpy(conn_params.bda, param->connect.remote_bda, sizeof(esp_bd_addr_t));
        /* For the IOS system, please reference the apple official documents about the ble connection parameters restrictions. */
        conn_params.latency = 0;
        conn_params.max_int = 0x20; // max_int = 0x20*1.25ms = 40ms
        conn_params.min_int = 0x10; // min_int = 0x10*1.25ms = 20ms
        conn_params.timeout = 400;  // timeout = 400*10ms = 4000ms
        // start sent the update connection parameters to the peer device.
        esp_ble_gap_update_conn_params(&conn_params);

        esp_ble_set_encryption(param->connect.remote_bda, ESP_BLE_SEC_ENCRYPT_MITM);

        connectionStart = xTaskGetTickCount();
        connectionEnd = 0;
        break;
    case ESP_GATTS_DISCONNECT_EVT:
        cert_state = 0;
        connectionEnd = xTaskGetTickCount();
        ESP_LOGW(BT_TAG, "ESP_GATTS_DISCONNECT_EVT, reason = %d, connection duration = %d ms", param->disconnect.reason, pdTICKS_TO_MS(connectionEnd - connectionStart));
        connectionStart = 0;

        esp_ble_gap_start_advertising(&adv_params);
        break;
    case ESP_GATTS_CREAT_ATTR_TAB_EVT:
    {
        if (param->add_attr_tab.status != ESP_GATT_OK)
        {
            ESP_LOGE(BT_TAG, "create attribute table failed, error code=0x%x", param->add_attr_tab.status);
        }
        int found = 0;
        if (param->add_attr_tab.svc_uuid.len == ESP_UUID_LEN_16)
        {

            if (param->add_attr_tab.svc_uuid.uuid.uuid16 == GATTS_SERVICE_UUID_BATTERY)
            {
                memcpy(battery_handle_table, param->add_attr_tab.handles, sizeof(battery_handle_table));
                esp_ble_gatts_start_service(battery_handle_table[IDX_BATTERY_SVC]);
                ESP_LOGD(BT_TAG, "create battery attribute table success, handle = %d", param->add_attr_tab.num_handle);
                found = 1;
            }
        }

        if (param->add_attr_tab.svc_uuid.len == ESP_UUID_LEN_128)
        {
            if (memcmp(param->add_attr_tab.svc_uuid.uuid.uuid128, GATTS_SERVICE_UUID_LED_BUTTON, ESP_UUID_LEN_128) == 0)
            {
                memcpy(led_button_handle_table, param->add_attr_tab.handles, sizeof(led_button_handle_table));
                esp_err_t response_err = esp_ble_gatts_start_service(led_button_handle_table[IDX_LED_BUTTON_SVC]);
                if (response_err != ESP_OK)
                {
                    ESP_LOGE(BT_TAG, "failed starting service: %d", response_err);
                }
                ESP_LOGD(BT_TAG, "create led button attribute table success, handle = %d", param->add_attr_tab.num_handle);
                found = 1;
            }
            if (memcmp(param->add_attr_tab.svc_uuid.uuid.uuid128, GATTS_SERVICE_UUID_CERTIFICATE, ESP_UUID_LEN_128) == 0)
            {
                memcpy(certificate_handle_table, param->add_attr_tab.handles, sizeof(certificate_handle_table));
                esp_err_t response_err = esp_ble_gatts_start_service(certificate_handle_table[IDX_CERT_SVC]);
                if (response_err != ESP_OK)
                {
                    ESP_LOGE(BT_TAG, "failed starting service: %d", response_err);
                }
                ESP_LOGD(BT_TAG, "create cert attribute table success, handle = %d", param->add_attr_tab.num_handle);
                found = 1;
            }
        }

        if (!found)
        {
            ESP_LOGE(BT_TAG, "service not found");
        }
        break;
    }
    case ESP_GATTS_STOP_EVT:
    case ESP_GATTS_OPEN_EVT:
    case ESP_GATTS_CANCEL_OPEN_EVT:
    case ESP_GATTS_CLOSE_EVT:
    case ESP_GATTS_LISTEN_EVT:
    case ESP_GATTS_CONGEST_EVT:
    case ESP_GATTS_UNREG_EVT:
    case ESP_GATTS_DELETE_EVT:
    default:
        ESP_LOGE(BT_TAG, "unhandled event");
        break;
    }
}

static void gatts_event_handler(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if, esp_ble_gatts_cb_param_t *param)
{

    /* If event is register event, store the gatts_if for each profile */
    if (event == ESP_GATTS_REG_EVT)
    {
        if (param->reg.status == ESP_GATT_OK)
        {
            pgp_profile_tab[PROFILE_APP_IDX].gatts_if = gatts_if;
        }
        else
        {
            ESP_LOGE(BT_TAG, "reg app failed, app_id %04x, status %d",
                     param->reg.app_id,
                     param->reg.status);
            return;
        }
    }
    do
    {
        int idx;
        for (idx = 0; idx < PROFILE_NUM; idx++)
        {
            /* ESP_GATT_IF_NONE, not specify a certain gatt_if, need to call every profile cb function */
            if (gatts_if == ESP_GATT_IF_NONE || gatts_if == pgp_profile_tab[idx].gatts_if)
            {
                if (pgp_profile_tab[idx].gatts_cb)
                {
                    pgp_profile_tab[idx].gatts_cb(event, gatts_if, param);
                }
            }
        }
    } while (0);
}

static void auto_button_task(void *pvParameters)
{
    button_queue_item_t item;

    ESP_LOGI(BUTTON_TASK_TAG, "[button task start]");

    while (1)
    {
        if (xQueueReceive(button_queue, &item, portMAX_DELAY))
        {
            // according to u/EeveesGalore's docs (https://i.imgur.com/7oWjMNu.png) button is sampled every 50 ms
            // byte 0 = samples0,1 (2=LSBit)
            // byte 1 = samples2-9 (10=LSBit)
            // randomize at which sample the button press starts and ends (min. diff 200 ms)
            int press_start = esp_random() % 6; // start at sample 0-5
            int press_last = press_start + 4 + esp_random() % (10 - press_start - 4);
            //               ^--min value--^                  ^-min distance to 10-^
            int press_duration = press_last - press_start + 1;

            // set bits where the button is pressed
            uint16_t button_pattern = 0;
            for (int i = 0; i < 10; i++)
            {
                button_pattern <<= 1; // this gets shifted 10 times total
                if (i >= press_start && i <= press_last)
                {
                    button_pattern |= 1; // button is pressed
                }
            }
            button_pattern &= 0x03ff; // just to be safe

            // make little endian byte array for sending
            uint8_t notify_data[2] = {
                (button_pattern >> 8) & 0x03,
                button_pattern & 0xff};

            ESP_LOGI(BUTTON_TASK_TAG, "pressing button after %d ms for %d ms, conn_id = %d", item.delay, press_duration * 50, item.conn_id);
            vTaskDelay(item.delay / portTICK_PERIOD_MS);

            esp_ble_gatts_send_indicate(item.gatts_if,
                                        item.conn_id,
                                        led_button_handle_table[IDX_CHAR_BUTTON_VAL],
                                        sizeof(notify_data), notify_data, false);
        }
    }
}

void app_main()
{
    // configure log levels
    esp_log_level_set(UART_TAG, ESP_LOG_INFO);
    esp_log_level_set(BT_TAG, ESP_LOG_DEBUG);
    esp_log_level_set(CERT_TAG, ESP_LOG_DEBUG);
    esp_log_level_set(PGPEMU_TAG, ESP_LOG_DEBUG);
    esp_log_level_set(BUTTON_TASK_TAG, ESP_LOG_DEBUG);

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

    // Init queues
    button_queue = xQueueCreate(10, sizeof(button_queue_item_t));
    if (button_queue == 0)
    {
        ESP_LOGE(PGPEMU_TAG, "%s creating button queue failed", __func__);
        return;
    }

    if (esp_log_level_get(CERT_TAG) >= ESP_LOG_DEBUG)
    {
        ESP_LOGD(CERT_TAG, "cert buffer:");
        ESP_LOG_BUFFER_HEX(CERT_TAG, cert_buffer, sizeof(cert_buffer));
    }

    xTaskCreate(auto_button_task, "auto_button_task", 2048, NULL, 11, NULL);

    ESP_ERROR_CHECK(esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT));

    memcpy(bt_mac, PGP_MAC, 6);
    memcpy(mac, PGP_MAC, 6);

    mac[5] -= 2; // TODO: check what happens if last byte is 0 or 1

    esp_base_mac_addr_set(mac);

    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    ret = esp_bt_controller_init(&bt_cfg);
    if (ret)
    {
        ESP_LOGE(BT_TAG, "%s enable controller failed: %s", __func__, esp_err_to_name(ret));
        return;
    }

    ret = esp_bt_controller_enable(ESP_BT_MODE_BLE);
    if (ret)
    {
        ESP_LOGE(BT_TAG, "%s enable controller failed: %s", __func__, esp_err_to_name(ret));
        return;
    }

    ret = esp_bluedroid_init();
    if (ret)
    {
        ESP_LOGE(BT_TAG, "%s init bluetooth failed: %s", __func__, esp_err_to_name(ret));
        return;
    }

    ret = esp_bluedroid_enable();
    if (ret)
    {
        ESP_LOGE(BT_TAG, "%s enable bluetooth failed: %s", __func__, esp_err_to_name(ret));
        return;
    }

    ret = esp_ble_gatts_register_callback(gatts_event_handler);
    if (ret)
    {
        ESP_LOGE(BT_TAG, "gatts register error, error code = %x", ret);
        return;
    }

    ret = esp_ble_gap_register_callback(gap_event_handler);
    if (ret)
    {
        ESP_LOGE(BT_TAG, "gap register error, error code = %x", ret);
        return;
    }

    ret = esp_ble_gatts_app_register(ESP_APP_ID);
    if (ret)
    {
        ESP_LOGE(BT_TAG, "gatts app register error, error code = %x", ret);
        return;
    }

    /* set the security iocap & auth_req & key size & init key response key parameters to the stack*/
    esp_ble_auth_req_t auth_req = ESP_LE_AUTH_BOND; // bonding with peer device after authentication
    esp_ble_io_cap_t iocap = ESP_IO_CAP_NONE;       // set the IO capability to No output No input
    uint8_t key_size = 16;                          // the key size should be 7~16 bytes
    uint8_t init_key = ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK;
    uint8_t rsp_key = ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK;
    esp_ble_gap_set_security_param(ESP_BLE_SM_AUTHEN_REQ_MODE, &auth_req, sizeof(uint8_t));
    esp_ble_gap_set_security_param(ESP_BLE_SM_IOCAP_MODE, &iocap, sizeof(uint8_t));
    esp_ble_gap_set_security_param(ESP_BLE_SM_MAX_KEY_SIZE, &key_size, sizeof(uint8_t));
    /* If your BLE device act as a Slave, the init_key means you hope which types of key of the master should distribut to you,
    and the response key means which key you can distribut to the Master;
    If your BLE device act as a master, the response key means you hope which types of key of the slave should distribut to you,
    and the init key means which key you can distribut to the slave. */
    esp_ble_gap_set_security_param(ESP_BLE_SM_SET_INIT_KEY, &init_key, sizeof(uint8_t));
    esp_ble_gap_set_security_param(ESP_BLE_SM_SET_RSP_KEY, &rsp_key, sizeof(uint8_t));

    esp_err_t local_mtu_ret = esp_ble_gatt_set_local_mtu(500);
    if (local_mtu_ret)
    {
        ESP_LOGE(BT_TAG, "set local MTU failed, error code = %x", local_mtu_ret);
    }

    ESP_LOGI(PGPEMU_TAG, "Device: %s", PGP_CLONE_NAME);
    ESP_LOGI(PGPEMU_TAG, "Ready.");

    settings_ready();
}
