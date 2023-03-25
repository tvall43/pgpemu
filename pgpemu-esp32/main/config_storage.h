#ifndef CONFIG_STORAGE_H
#define CONFIG_STORAGE_H

#include <stdbool.h>

void init_config_storage();
void read_stored_settings(bool use_mutex);
bool write_config_storage();
void close_config_storage();

#endif /* CONFIG_STORAGE_H */
