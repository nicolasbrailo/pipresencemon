#pragma once

#include <stdbool.h>
#include <stddef.h>

struct GpioPinActiveMonitor_args {
    size_t sensor_pin;
    size_t sensor_poll_period_secs;
    size_t monitor_window_seconds;

    float rising_edge_active_threshold;
    float falling_edge_inactive_threshold;
};

struct GpioPinActiveMonitor;

struct GpioPinActiveMonitor* gpio_active_monitor_create(struct GpioPinActiveMonitor_args args);
void gpio_active_monitor_close(struct GpioPinActiveMonitor* mon);

float gpio_active_monitor_active_pct(struct GpioPinActiveMonitor* mon);
bool gpio_active_monitor_pin_active(struct GpioPinActiveMonitor* mon);

