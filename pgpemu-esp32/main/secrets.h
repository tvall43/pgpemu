#ifndef DEVICE_KEY_H
#define DEVICE_KEY_H

#include <stdbool.h>
#include <stdint.h>

extern char PGP_CLONE_NAME[16];
extern uint8_t PGP_MAC[6];
extern uint8_t PGP_DEVICE_KEY[16];
extern uint8_t PGP_BLOB[256];

bool PGP_VALID();

#endif
