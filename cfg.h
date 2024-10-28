#pragma once

#include <stdbool.h>
#include <stddef.h>

struct Config {
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
  bool restart_cmd_on_unexpected_exit;
  size_t restart_cmd_wait_time_seconds;
  size_t crash_on_repeated_cmd_failure_count;

  // Commands to be executed when transitioning from no-presence to presence
  size_t on_occupancy_cmds_cnt;
  char on_occupancy_cmds[500];
  bool occupancy_cmd_should_restart_on_crash[10];

  // Commands to be executed when transitioning from -presence to no-presence
  size_t on_vacancy_cmds_cnt;
  char on_vacancy_cmds[500];
  bool vacancy_cmd_should_restart_on_crash[10];
};

bool cfg_read(const char *fpath, struct Config *cfg);

typedef void (*cfg_each_cmd_cb_t)(void *usr, size_t cmd_idx, const char *cmd);
void cfg_each_cmd(const char *cmds, cfg_each_cmd_cb_t cb, void *usr);

void cfg_debug(struct Config *cfg);
