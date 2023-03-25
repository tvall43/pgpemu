#ifndef PGP_HANDSHAKE_H
#define PGP_HANDSHAKE_H

#include <stdint.h>

#include "esp_gatt_defs.h"

void generate_first_challenge();
void handle_pgp_handshake(esp_gatt_if_t gatts_if,
                          const uint8_t *prepare_buf, int datalen,
                          int conn_id);

extern int cert_state;

#endif /* PGP_HANDSHAKE_H */
