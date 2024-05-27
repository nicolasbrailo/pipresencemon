#pragma once

#include <stdbool.h>
#include <stddef.h>

struct GpioPinActiveMonitor_args {
  // Pin where a sensor is connected. This class expects the sensor to be high when active.
  // Easiest way to find is with the gpio_get_and_print_delta function, as the memory offset doesn't
  // match either the pin or GPIO number
  size_t sensor_pin;

  // How often should we poll the sensor for an update
  size_t sensor_poll_period_secs;

  // How long is the history kept for this sensor. For example, if monitor_window_seconds=10 and
  // sensor_poll_period_secs=1, then we'll read the sensor every second. If in the last 10 seconds
  // the sensor reported activity 3 times, we'll consider the sensor 30% active.
  size_t monitor_window_seconds;

  // 'Active' is a boolean parameter; The (in)activity threshold determines how much activity
  // percent the sensor needs to report before we consider it (in)active. If the rising edge
  // requires 30% activity, the 'active' bit of this sensor will flip from false->true only iff 30%
  // of the last readings were active. The monitor supports some form of histeresys by letting you
  // configure rising and falling edges.
  size_t rising_edge_active_threshold_pct;
  size_t falling_edge_inactive_threshold_pct;

  // If true, the service starts on high state. This can be useful to avoid an initial transition of
  // the active state, if there is another signal that can be used to determine the most likely
  // start state of the pin.
  bool start_active;
};

struct GpioPinActiveMonitor;

struct GpioPinActiveMonitor *gpio_active_monitor_init_from_cfg(void *cfg, bool start_active);
struct GpioPinActiveMonitor *gpio_active_monitor_init(struct GpioPinActiveMonitor_args args);
void gpio_active_monitor_free(struct GpioPinActiveMonitor *mon);

float gpio_active_monitor_active_pct(struct GpioPinActiveMonitor *mon);
bool gpio_active_monitor_pin_active(struct GpioPinActiveMonitor *mon);
