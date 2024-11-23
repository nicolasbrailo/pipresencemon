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

  size_t rising_edge_active_threshold_pct;
  size_t falling_edge_inactive_threshold_pct;
  // Current status, without inactivity timeout
  atomic_bool currently_active;
  // Follows currently_active, but has a delay of $vacancy_motion_timeout_seconds before
  // transitioning from active->inactive
  size_t vacancy_motion_timeout_seconds;
  size_t vacant_timeout_secs;
  atomic_bool active;
};

static void *gpio_active_monitor_update(void *usr) {
  struct GpioPinActiveMonitor *mon = usr;
  while (!mon->thread_stop) {
    bool pin_state = gpio_get_pin(mon->gpio, mon->sensor_pin);
    mon->active_count_in_window -= mon->sensor_readings[mon->sensor_readings_write_idx];
    mon->sensor_readings[mon->sensor_readings_write_idx] = pin_state;
    mon->active_count_in_window += mon->sensor_readings[mon->sensor_readings_write_idx];
    mon->sensor_readings_write_idx = (mon->sensor_readings_write_idx + 1) % mon->sensor_readings_sz;

    if (mon->gpio_debug) {
      printf("Pin %zu reports %s, active_pct=%zu\n", mon->sensor_pin, pin_state ? "active" : "inactive",
             gpio_active_monitor_active_pct(mon));
    }

    if (mon->currently_active &&
        (gpio_active_monitor_active_pct(mon) < mon->falling_edge_inactive_threshold_pct)) {
      printf("GPIO reports vacancy: %zu%% activity (smaller than threshold for vacancy = %zu%%)\n",
             gpio_active_monitor_active_pct(mon), mon->falling_edge_inactive_threshold_pct);
      printf("Waiting %zu seconds before reporting vacancy\n", mon->vacant_timeout_secs);
      mon->currently_active = false;

    } else if (!mon->currently_active &&
               (gpio_active_monitor_active_pct(mon) > mon->rising_edge_active_threshold_pct)) {
      printf("GPIO reports ocupancy: %zu%% activity (bigger than threshold for ocupancy = %zu%%)\n",
             gpio_active_monitor_active_pct(mon), mon->rising_edge_active_threshold_pct);
      mon->currently_active = true;
    }

    if (mon->currently_active) {
      mon->vacant_timeout_secs = mon->vacancy_motion_timeout_seconds;
      mon->active = true;
    } else {
      if (mon->vacant_timeout_secs > 0) {
        mon->vacant_timeout_secs -= 1;
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

  mon->vacancy_motion_timeout_seconds = cfg->vacancy_motion_timeout_seconds;
  mon->vacant_timeout_secs = cfg->vacancy_motion_timeout_seconds;
  mon->currently_active = start_active;
  mon->active = start_active;

  mon->sensor_readings = malloc(sizeof(mon->sensor_readings[0]) * mon->sensor_readings_sz);
  if (!mon->sensor_readings) {
    perror("GpioPinActiveMonitor bad window alloc");
    free(mon);
    return NULL;
  }
  memset(mon->sensor_readings, start_active, mon->sensor_readings_sz);

  mon->rising_edge_active_threshold_pct = cfg->rising_edge_occupancy_threshold_pct;
  mon->falling_edge_inactive_threshold_pct = cfg->falling_edge_vacancy_threshold_pct;

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

size_t gpio_active_monitor_active_pct(struct GpioPinActiveMonitor *mon) {
  return 100 * mon->active_count_in_window / mon->sensor_readings_sz;
}

bool gpio_active_monitor_pin_active(struct GpioPinActiveMonitor *mon) {
  return mon->active ? true : false;
}
