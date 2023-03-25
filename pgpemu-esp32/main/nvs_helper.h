#ifndef NVS_HELPER_H
#define NVS_HELPER_H

#include <stdbool.h>

#include "esp_system.h"

bool nvs_read_check(const char *tag, esp_err_t err, const char *name);
bool nvs_write_check(const char *tag, esp_err_t err, const char *name);

#endif /* NVS_HELPER_H */