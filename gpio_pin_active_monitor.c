#include "gpio_pin_active_monitor.h"
#include "gpio.h"

#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <unistd.h>

struct GpioPinActiveMonitor {
  struct GPIO *gpio;
  size_t sensor_pin;

  size_t sensor_readings_write_idx;
  size_t sensor_readings_sz;
  atomic_size_t active_count_in_window;
  bool *sensor_readings;

  pthread_t thread_id;
  atomic_bool thread_stop;
  size_t poll_period_secs;

  float rising_edge_active_threshold;
  float falling_edge_inactive_threshold;
  atomic_bool currently_active;
};

static void *gpio_active_monitor_update(void *usr) {
  struct GpioPinActiveMonitor *mon = usr;
  while (!mon->thread_stop) {
    mon->active_count_in_window -=
        mon->sensor_readings[mon->sensor_readings_write_idx];
    mon->sensor_readings[mon->sensor_readings_write_idx] =
        gpio_get_pin(mon->gpio, mon->sensor_pin);
    mon->active_count_in_window +=
        mon->sensor_readings[mon->sensor_readings_write_idx];
    mon->sensor_readings_write_idx =
        (mon->sensor_readings_write_idx + 1) % mon->sensor_readings_sz;

    if (mon->currently_active && (gpio_active_monitor_active_pct(mon) <
                                  mon->falling_edge_inactive_threshold)) {
      mon->currently_active = false;
    } else if (!mon->currently_active && (gpio_active_monitor_active_pct(mon) >
                                          mon->rising_edge_active_threshold)) {
      mon->currently_active = true;
    }

    sleep(mon->poll_period_secs);
  }
  return NULL;
}

struct GpioPinActiveMonitor *
gpio_active_monitor_create(const struct GpioPinActiveMonitor_args args) {
  struct GPIO *gpio = gpio_open();
  if (!gpio) {
    return NULL;
  }

  struct GpioPinActiveMonitor *mon =
      malloc(sizeof(struct GpioPinActiveMonitor));
  if (!mon) {
    perror("GpioPinActiveMonitor bad alloc");
    return NULL;
  }

  mon->gpio = gpio;
  mon->sensor_pin = args.sensor_pin;

  mon->sensor_readings_write_idx = 0;
  mon->sensor_readings_sz =
      args.monitor_window_seconds / args.sensor_poll_period_secs;
  mon->active_count_in_window = 0;
  mon->sensor_readings =
      malloc(sizeof(mon->sensor_readings[0]) * mon->sensor_readings_sz);
  if (!mon->sensor_readings) {
    perror("GpioPinActiveMonitor bad window alloc");
    free(mon);
    return NULL;
  }
  bzero(mon->sensor_readings, mon->sensor_readings_sz);

  mon->rising_edge_active_threshold = args.rising_edge_active_threshold;
  mon->falling_edge_inactive_threshold = args.falling_edge_inactive_threshold;
  mon->currently_active = false;

  mon->poll_period_secs = args.sensor_poll_period_secs;
  mon->thread_stop = false;
  if (pthread_create(&mon->thread_id, NULL, gpio_active_monitor_update, mon) !=
      0) {
    perror("GpioPinActiveMonitor thread create error");
    free(mon->sensor_readings);
    free(mon);
    return NULL;
  }

  return mon;
}

void gpio_active_monitor_close(struct GpioPinActiveMonitor *mon) {
  mon->thread_stop = true;
  if (pthread_join(mon->thread_id, NULL) != 0) {
    perror("GpioPinActiveMonitor pthread_join fail");
  }

  gpio_close(mon->gpio);
  free(mon->sensor_readings);
  free(mon);
}

float gpio_active_monitor_active_pct(struct GpioPinActiveMonitor *mon) {
  return 1.f * mon->active_count_in_window / mon->sensor_readings_sz;
}

bool gpio_active_monitor_pin_active(struct GpioPinActiveMonitor *mon) {
  return mon->currently_active ? true : false;
}
