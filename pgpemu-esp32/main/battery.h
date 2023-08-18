#ifndef BATTERY_H
#define BATTERY_H

void setup_battery_measurement();

void power_save_task();

uint8_t read_battery_value();

#endif /* BATTERY_H */
