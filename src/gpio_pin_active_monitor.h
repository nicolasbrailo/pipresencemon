#pragma once

#include <stdbool.h>
#include <stddef.h>

struct GpioPinActiveMonitor;
struct PiPresenceMonConfig;

struct GpioPinActiveMonitor *gpio_active_monitor_init(const struct PiPresenceMonConfig *cfg);
void gpio_active_monitor_free(struct GpioPinActiveMonitor *mon);

size_t gpio_active_monitor_active_pct(struct GpioPinActiveMonitor *mon);
bool gpio_active_monitor_pin_active(struct GpioPinActiveMonitor *mon);
