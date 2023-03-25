#ifndef CONFIG_STORAGE_H
#define CONFIG_STORAGE_H

#include <stdbool.h>

// open nvs partition
void init_config_storage();

// read settings from nvs
void read_stored_settings(bool use_mutex);

// write current settings to nvs
bool write_config_storage();

// close, used before a restart
void close_config_storage();

#endif /* CONFIG_STORAGE_H */
