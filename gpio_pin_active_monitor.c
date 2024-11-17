#include "gpio_pin_active_monitor.h"
#include "cfg.h"
#include "gpio.h"

#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

struct GpioPinActiveMonitor {
  struct GPIO *gpio;
  bool gpio_debug;
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
  // Current status, without inactivity timeout
  atomic_bool currently_active;
  // Follows currently_active, but has a delay of $vacancy_motion_timeout_seconds before
  // transitioning from active->inactive
  size_t vacancy_motion_timeout_seconds;
  size_t vacant_timeout;
  atomic_bool active;
};

static void *gpio_active_monitor_update(void *usr) {
  struct GpioPinActiveMonitor *mon = usr;
  while (!mon->thread_stop) {
    if (mon->gpio_debug) {
        // TODO printf("HOLA\n");
    }
    mon->active_count_in_window -= mon->sensor_readings[mon->sensor_readings_write_idx];
    mon->sensor_readings[mon->sensor_readings_write_idx] = gpio_get_pin(mon->gpio, mon->sensor_pin);
    mon->active_count_in_window += mon->sensor_readings[mon->sensor_readings_write_idx];
    mon->sensor_readings_write_idx = (mon->sensor_readings_write_idx + 1) % mon->sensor_readings_sz;

    if (mon->currently_active &&
        (gpio_active_monitor_active_pct(mon) < mon->falling_edge_inactive_threshold)) {
      printf("GPIO reports inactive, waiting %zu before reporting vacancy\n", mon->vacant_timeout);
      mon->currently_active = false;

    } else if (!mon->currently_active &&
               (gpio_active_monitor_active_pct(mon) > mon->rising_edge_active_threshold)) {
      printf("GPIO reports ocupancy\n");
      mon->currently_active = true;
    }

    if (mon->currently_active) {
      mon->vacant_timeout = mon->vacancy_motion_timeout_seconds;
      mon->active = true;
    } else {
      if (mon->vacant_timeout > 0) {
        mon->vacant_timeout -= 1;
      } else {
        if (mon->active) {
          printf("Reporting vacancy\n");
          mon->active = false;
        }
      }
    }

    sleep(mon->poll_period_secs);
  }
  return NULL;
}

struct GpioPinActiveMonitor *gpio_active_monitor_init(const struct Config *cfg) {
  const bool start_active = true;

  if (cfg->sensor_pin > GPIO_PINS) {
    fprintf(stderr, "Invalid pin number %zu (max %zu)\n", cfg->sensor_pin, GPIO_PINS);
    return NULL;
  }

  if (cfg->rising_edge_occupancy_threshold_pct < cfg->falling_edge_vacancy_threshold_pct) {
    fprintf(stderr,
            "A 'rising edge threshold' smaller than 'falling edge threshold' is not stable\n");
    return NULL;
  }

  struct GPIO *gpio = gpio_open(cfg->gpio_use_mock);
  if (!gpio) {
    return NULL;
  }

  struct GpioPinActiveMonitor *mon = malloc(sizeof(struct GpioPinActiveMonitor));
  if (!mon) {
    perror("GpioPinActiveMonitor bad alloc");
    return NULL;
  }

  mon->gpio = gpio;
  mon->gpio_debug = cfg->gpio_debug;
  mon->sensor_pin = cfg->sensor_pin;
  mon->sensor_readings_write_idx = 0;
  mon->sensor_readings_sz = cfg->sensor_monitor_window_seconds / cfg->sensor_poll_period_secs;
  mon->active_count_in_window = start_active ? mon->sensor_readings_sz : 0;

  mon->vacancy_motion_timeout_seconds = mon->vacancy_motion_timeout_seconds;
  mon->vacant_timeout = mon->vacancy_motion_timeout_seconds;
  mon->currently_active = start_active;
  mon->active = start_active;

  mon->sensor_readings = malloc(sizeof(mon->sensor_readings[0]) * mon->sensor_readings_sz);
  if (!mon->sensor_readings) {
    perror("GpioPinActiveMonitor bad window alloc");
    free(mon);
    return NULL;
  }
  memset(mon->sensor_readings, start_active, mon->sensor_readings_sz);

  mon->rising_edge_active_threshold = cfg->rising_edge_occupancy_threshold_pct / 100.f;
  mon->falling_edge_inactive_threshold = cfg->falling_edge_vacancy_threshold_pct / 100.f;

  mon->poll_period_secs = cfg->sensor_poll_period_secs;
  mon->thread_stop = false;
  if (pthread_create(&mon->thread_id, NULL, gpio_active_monitor_update, mon) != 0) {
    perror("GpioPinActiveMonitor thread create error");
    free(mon->sensor_readings);
    free(mon);
    return NULL;
  }

  return mon;
}

void gpio_active_monitor_free(struct GpioPinActiveMonitor *mon) {
  if (!mon) {
    return;
  }

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
  return mon->active ? true : false;
}
