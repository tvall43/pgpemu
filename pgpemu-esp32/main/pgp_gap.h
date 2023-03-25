#ifndef PGP_GAP_H
#define PGP_GAP_H

#include "esp_gap_ble_api.h"

#define ADV_CONFIG_FLAG (1 << 0)
#define SCAN_RSP_CONFIG_FLAG (1 << 1)
extern uint8_t adv_config_done;

// start BT advertising
void pgp_advertise();

void gap_event_handler(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param);

#endif /* PGP_GAP_H */
