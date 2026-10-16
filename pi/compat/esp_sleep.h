#pragma once
#include "esp_err.h"
typedef int gpio_num_t;
inline esp_err_t esp_sleep_enable_ext0_wakeup(gpio_num_t, int) { return ESP_OK; }
inline esp_err_t esp_sleep_enable_timer_wakeup(uint64_t) { return ESP_OK; }
// There is no deep sleep on the Pi: the display driver blanks the backlight
// instead, so this never returns into a powered-down state.
void esp_deep_sleep_start();
