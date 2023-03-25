#ifndef LOG_TAGS_H
#define LOG_TAGS_H

// before initialization
void log_levels_init();

// everything verbose
void log_levels_max();

// more manageable outputs
void log_levels_min();

static const char BT_GAP_TAG[] = "pgp_bt_gap";
static const char BT_GATTS_TAG[] = "pgp_bt_gatts";
static const char BT_TAG[] = "pgp_bluetooth";
static const char BUTTON_TASK_TAG[] = "button_task";
static const char CERT_TAG[] = "pgp_cert";
static const char CONFIG_SECRETS_TAG[] = "config_secrets";
static const char CONFIG_STORAGE_TAG[] = "config_storage";
static const char HANDSHAKE_TAG[] = "pgp_handshake";
static const char LED_TAG[] = "pgp_led";
static const char PGPEMU_TAG[] = "PGPEMU";
static const char POWERBANK_TASK_TAG[] = "powerbank_task";
static const char UART_TAG[] = "uart_events";

#endif /* LOG_TAGS_H */
