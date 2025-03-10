#pragma once

#include <stdbool.h>
#include <stddef.h>

struct CommandConfig {
  const char *cmd;
  bool should_restart_on_crash;
  size_t max_restarts;
};

struct PiPresenceMonConfig {
  bool gpio_debug;
  bool gpio_use_mock;

  // Pin to monitor
  size_t sensor_pin;

  // Sleep between sensor reads
  size_t sensor_poll_period_secs;

  // Time to keep sensor history
  size_t sensor_monitor_window_seconds;

  // When currently vacant, what % of readings in the sensor history need to indicate presence
  // before we consider there is occupancy
  size_t rising_edge_occupancy_threshold_pct;

  // When currently has presence, what % of readings need to be inactive before we say there's no
  // one present
  size_t falling_edge_vacancy_threshold_pct;

  // Minimum timeout before declaring no-presence
  size_t vacancy_motion_timeout_seconds;

  // Restart child cmds on crash
  size_t restart_cmd_wait_time_seconds;
  size_t crash_on_repeated_cmd_failure_count;

  // Commands to be executed when transitioning from no-presence to presence
  size_t on_occupancy_sz;
  struct CommandConfig* on_occupancy;

  // Commands to be executed when transitioning from -presence to no-presence
  size_t on_vacancy_sz;
  struct CommandConfig* on_vacancy;
};

struct PiPresenceMonConfig* pipresencemon_cfg_init(const char *fpath);
void pipresencemon_cfg_free(struct PiPresenceMonConfig* cfg);

typedef void (*cfg_each_cmd_cb_t)(void *usr, size_t cmd_idx, const char *cmd);
void cfg_each_cmd(const char *cmds, cfg_each_cmd_cb_t cb, void *usr);

void cfg_debug(struct PiPresenceMonConfig*cfg);
