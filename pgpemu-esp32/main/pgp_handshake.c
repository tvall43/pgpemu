#include <stdint.h>
#include <string.h>

#include "esp_gatt_defs.h"
#include "esp_log.h"

#include "pgp_handshake.h"

#include "log_tags.h"
#include "pgp_bluetooth.h"
#include "pgp_cert.h"
#include "pgp_gatts.h"

uint8_t session_key[16];
// TODO this should be saved per connection
int cert_state = 0;

void generate_first_challenge()
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

void handle_pgp_handshake(esp_gatt_if_t gatts_if,
                          const uint8_t *prepare_buf, int datalen,
                          int conn_id)
{
    if (cert_state >= 1)
    {
        ESP_LOGD(HANDSHAKE_TAG, "Cert state %d, received %d", cert_state, datalen);
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

            // ESP_LOG_BUFFER_HEX(HANDSHAKE_TAG, temp, sizeof(temp));

            esp_ble_gatts_set_attr_value(certificate_handle_table[IDX_CHAR_SFIDA_TO_CENTRAL_VAL], 52, cert_buffer);

            // ESP_LOG_BUFFER_HEX(HANDSHAKE_TAG, cert_buffer, 52);

            cert_state = 1;
            esp_ble_gatts_send_indicate(gatts_if, conn_id, certificate_handle_table[IDX_CHAR_SFIDA_COMMANDS_VAL], sizeof(notify_data), notify_data, false);
        }
        else
        {
            ESP_LOGE(HANDSHAKE_TAG, "App sends incorrect len=%d", datalen);
        }
        break;
    }
    case 1:
    {
        // we need to decrypt and send challenge data from APP
        ESP_LOGD(HANDSHAKE_TAG, "Received: %d", datalen);

        uint8_t temp[20];
        memset(temp, 0, sizeof(temp));
        decrypt_next(prepare_buf, session_key, temp + 4);
        temp[0] = 0x02;
        uint8_t notify_data[4];
        memset(notify_data, 0, 4);
        notify_data[0] = 0x02;

        ESP_LOGD(HANDSHAKE_TAG, "Sending response");
        if (esp_log_level_get(HANDSHAKE_TAG) >= ESP_LOG_DEBUG)
        {
            ESP_LOG_BUFFER_HEX(HANDSHAKE_TAG, temp, sizeof(temp));
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
        ESP_LOGD(HANDSHAKE_TAG, "OK");

        uint8_t temp[20];
        memset(temp, 0, sizeof(temp));
        decrypt_next(prepare_buf, session_key, temp + 4);

        if (esp_log_level_get(HANDSHAKE_TAG) >= ESP_LOG_DEBUG)
        {

            ESP_LOG_BUFFER_HEX(HANDSHAKE_TAG, temp, sizeof(temp));
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
            ESP_LOGD(HANDSHAKE_TAG, "OK");
            if (esp_log_level_get(HANDSHAKE_TAG) >= ESP_LOG_DEBUG)
            {
                ESP_LOG_BUFFER_HEX(HANDSHAKE_TAG, prepare_buf,
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
        ESP_LOGD(HANDSHAKE_TAG, "OK");

        memset(cert_buffer, 0, 4);
        generate_reconnect_response(session_key, prepare_buf + 4, cert_buffer + 4);
        cert_buffer[0] = 5;

        if (esp_log_level_get(HANDSHAKE_TAG) >= ESP_LOG_DEBUG)
        {
            ESP_LOG_BUFFER_HEX(HANDSHAKE_TAG, cert_buffer, 20);
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
            ESP_LOGD(HANDSHAKE_TAG, "OK");

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
        ESP_LOGE(HANDSHAKE_TAG, "Unhandled state: %d", cert_state);
        break;
    }
}