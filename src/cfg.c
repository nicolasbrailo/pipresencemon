#include "cfg.h"
#include "json.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

static bool maybe_realloc(const char *k, size_t *sz, size_t read_sz, struct CommandConfig **cmds) {
  if (*sz != 0) {
    if (*sz != read_sz) {
      fprintf(stderr,
              "Config error: %s changed size while processing, new size %zu, expected %zu\n", k,
              read_sz, *sz);
      return false;
    }

    // Already alloc'd
    return true;
  }

  *sz = read_sz;
  *cmds = malloc(*sz * sizeof(struct CommandConfig));
  if (!*cmds) {
    fprintf(stderr, "Config error: %s bad alloc\n", k);
    return false;
  }

  return true;
}

static bool parse_cmd(struct json_object* handle, struct CommandConfig *cmd) {
  bool ok = true;
  ok &= json_get_bool(handle, "should_restart_on_crash", &cmd->should_restart_on_crash);
  ok &= json_get_size_t(handle, "max_restarts", &cmd->max_restarts, 0, 99);
  ok &= json_get_strdup(handle, "cmd", &cmd->cmd);
  return ok;
}

static bool parse_on_occupancy(size_t arr_len, size_t idx, struct json_object* handle, void *usr) {
  struct PiPresenceMonConfig *cfg = usr;
  return maybe_realloc("on_occupancy", &cfg->on_occupancy_sz, arr_len, &cfg->on_occupancy) &&
         parse_cmd(handle, &cfg->on_occupancy[idx]);
}

static bool parse_on_vacancy(size_t arr_len, size_t idx, struct json_object* handle, void *usr) {
  struct PiPresenceMonConfig *cfg = usr;
  return maybe_realloc("on_vacancy", &cfg->on_vacancy_sz, arr_len, &cfg->on_vacancy) &&
         parse_cmd(handle, &cfg->on_vacancy[idx]);
}

struct PiPresenceMonConfig *pipresencemon_cfg_init(const char *fpath) {
  bool ok = true;
  struct PiPresenceMonConfig *cfg = malloc(sizeof(struct PiPresenceMonConfig));
  struct json_object* cfgbase = json_init(fpath);
  if (!cfg || !cfgbase) {
    ok = false;
    goto err;
  }

  cfg->on_occupancy_sz = 0;
  cfg->on_vacancy_sz = 0;
  cfg->on_occupancy = NULL;
  cfg->on_vacancy = NULL;

  ok &= json_get_bool(cfgbase, "gpio_debug", &cfg->gpio_debug);
  ok &= json_get_bool(cfgbase, "gpio_use_mock", &cfg->gpio_use_mock);
  ok &= json_get_size_t(cfgbase, "sensor_pin", &cfg->sensor_pin, 0, 40);
  ok &= json_get_size_t(cfgbase, "sensor_poll_period_secs", &cfg->sensor_poll_period_secs, 1, 30);
  ok &= json_get_size_t(cfgbase, "sensor_monitor_window_seconds",
                       &cfg->sensor_monitor_window_seconds, 5, 100);
  ok &= json_get_size_t(cfgbase, "rising_edge_occupancy_threshold_pct",
                       &cfg->rising_edge_occupancy_threshold_pct, 10, 100);
  ok &= json_get_size_t(cfgbase, "falling_edge_vacancy_threshold_pct",
                       &cfg->falling_edge_vacancy_threshold_pct, 1, 100);
  ok &= json_get_size_t(cfgbase, "vacancy_motion_timeout_seconds",
                       &cfg->vacancy_motion_timeout_seconds, 1, 600);
  ok &= json_get_size_t(cfgbase, "restart_cmd_wait_time_seconds",
                       &cfg->restart_cmd_wait_time_seconds, 0, 100);
  ok &= json_get_size_t(cfgbase, "crash_on_repeated_cmd_failure_count",
                       &cfg->crash_on_repeated_cmd_failure_count, 0, 50);
  ok &= json_get_arr(cfgbase, "on_occupancy", parse_on_occupancy, cfg);
  ok &= json_get_arr(cfgbase, "on_vacancy", parse_on_vacancy, cfg);

  if (cfg->rising_edge_occupancy_threshold_pct < cfg->falling_edge_vacancy_threshold_pct) {
    fprintf(stderr,
            "rising_edge_occupancy_threshold_pct must be higher than "
            "falling_edge_vacancy_threshold_pct, otherwise the configuration isn't stable\n");
    ok = false;
  }

  if (cfg->on_occupancy_sz == 0) {
    fprintf(stderr, "Warning: no occupancy commands specified, this looks buggy\n");
  }

  if (cfg->on_vacancy_sz == 0) {
    fprintf(stderr, "Warning: no vacancy commands specified, this looks buggy\n");
  }

  // fallthrough
err:
  json_free(cfgbase);

  if (ok) {
    return cfg;
  } else {
    pipresencemon_cfg_free(cfg);
    return NULL;
  }
}

void pipresencemon_cfg_free(struct PiPresenceMonConfig *cfg) {
  if (!cfg) {
    return;
  }

  if (cfg->on_occupancy) {
    for (size_t i = 0; i < cfg->on_occupancy_sz; ++i) {
      free((void *)cfg->on_occupancy[i].cmd);
    }
    free(cfg->on_occupancy);
  }

  if (cfg->on_vacancy) {
    for (size_t i = 0; i < cfg->on_vacancy_sz; ++i) {
      free((void *)cfg->on_vacancy[i].cmd);
    }
    free(cfg->on_vacancy);
  }

  free(cfg);
}

void cfg_debug(struct PiPresenceMonConfig *cfg) {
  printf("PiPresenceMonConfig: {\n");
  printf("\t gpio_debug: %d,\n", cfg->gpio_debug);
  printf("\t gpio_use_mock: %d,\n", cfg->gpio_use_mock);
  printf("\t sensor_pin: %zu,\n", cfg->sensor_pin);
  printf("\t sensor_poll_period_secs: %zu,\n", cfg->sensor_poll_period_secs);
  printf("\t sensor_monitor_window_seconds: %zu,\n", cfg->sensor_monitor_window_seconds);
  printf("\t rising_edge_occupancy_threshold_pct: %zu,\n",
         cfg->rising_edge_occupancy_threshold_pct);
  printf("\t falling_edge_vacancy_threshold_pct: %zu,\n", cfg->falling_edge_vacancy_threshold_pct);
  printf("\t vacancy_motion_timeout_seconds: %zu,\n", cfg->vacancy_motion_timeout_seconds);
  printf("\t restart_cmd_wait_time_seconds: %zu,\n", cfg->restart_cmd_wait_time_seconds);
  printf("\t crash_on_repeated_cmd_failure_count: %zu,\n",
         cfg->crash_on_repeated_cmd_failure_count);

  printf("\t on_occupancy: [\n");
  for (size_t i = 0; i < cfg->on_occupancy_sz; ++i) {
    printf("\t CommandConfig {\n");
    printf("\t\t cmd: %s\n", cfg->on_occupancy[i].cmd);
    printf("\t\t should_restart_on_crash: %d,\n", cfg->on_occupancy[i].should_restart_on_crash);
    printf("\t\t max_restarts: %zu,\n", cfg->on_occupancy[i].max_restarts);
    printf("\t },\n");
  }
  printf("\t ]\n");

  printf("\t on_vacancy: [\n");
  for (size_t i = 0; i < cfg->on_vacancy_sz; ++i) {
    printf("\t CommandConfig {\n");
    printf("\t\t cmd: %s\n", cfg->on_vacancy[i].cmd);
    printf("\t\t should_restart_on_crash: %d,\n", cfg->on_vacancy[i].should_restart_on_crash);
    printf("\t\t max_restarts: %zu,\n", cfg->on_vacancy[i].max_restarts);
    printf("\t },\n");
  }
  printf("\t ]\n");

  printf("}\n");
}

void cfg_each_cmd(const char *cmds, cfg_each_cmd_cb_t cb, void *usr) {}
