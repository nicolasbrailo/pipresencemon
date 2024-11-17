#include "cfg.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void *cfg_init(const char *fpath) {
  FILE *fp = fopen(fpath, "r");
  if (!fp) {
    perror("Error opening file");
    return NULL;
  }

  fseek(fp, 0, SEEK_END);
  size_t fsz = ftell(fp);
  rewind(fp);

  char *buf = malloc(fsz + 1);
  if (!buf) {
    perror("cfg_init bad alloc");
    fclose(fp);
    return NULL;
  }

  fread(buf, 1, fsz, fp);
  buf[fsz] = '\0';
  fclose(fp);

  return buf;
}

static void cfg_free(void *handle) { free(handle); }

static const char *advance_ptr_to_val(const void *handle, const char *key_name) {
  const char *buf = handle;
  const char *start_pos = strstr(buf, key_name);
  if (!start_pos) {
    return NULL;
  }

  start_pos += strlen(key_name);
  while (*++start_pos != '=')
    ;
  while (*++start_pos == ' ')
    ;
  return start_pos;
}

static bool cfg_read_size_t(const void *handle, const char *key_name, size_t *val) {
  const char *start_pos = advance_ptr_to_val(handle, key_name);
  if (!start_pos) {
    fprintf(stderr, "Can't find config key \"%s\"\n", key_name);
    return false;
  }

  if (sscanf(start_pos, "%zu\n", val) == 1) {
    return true;
  }

  fprintf(stderr, "Can't parse number from config key \"%s\"\n", key_name);
  return false;
}

static int cfg_read_str(const void *handle, const char *key_name, char *val, size_t max_len) {
  const char *start_pos = advance_ptr_to_val(handle, key_name);
  if (!start_pos) {
    return 0;
  }

  const char *end_pos = start_pos;
  while (*end_pos != '\n' && *end_pos != '\0') {
    end_pos++;
  }

  int sz = end_pos - start_pos;
  if (sz <= 0) {
    fprintf(stderr, "Can't find string value for config key \"%s\"\n", key_name);
    return 0;
  }

  if ((unsigned)sz >= max_len) {
    fprintf(stderr, "Reading config key \"%s\" would overflow\n", key_name);
    return -1;
  }

  val[sz] = 0;
  strncpy(val, start_pos, sz);

  return sz + 1;
}

static bool cfg_read_bool(const void *handle, const char *key_name, bool *val) {
  char buff[100];
  size_t read = cfg_read_str(handle, key_name, buff, sizeof(buff));
  if (read <= 0) {
    return false;
  }

  if (strcmp(buff, "true") == 0) {
    *val = true;
    return true;
  } else if (strcmp(buff, "false") == 0) {
    *val = false;
    return true;
  }

  return false;
}

static int cfg_read_str_arr(void *h, const char key_root[], char *buff, size_t buff_sz) {
  memset(buff, 0, buff_sz);
  size_t offset = 0;
  size_t cnt = 0;
  while (true) {
    char key_name[50];
    int ret = snprintf(key_name, sizeof(key_name), "%s%zu", key_root, cnt);
    if (ret < 0) {
      perror("Error reading config key, snprintf fail\n");
      return -1;
    }
    if ((unsigned)ret > sizeof(key_name)) {
      fprintf(stderr, "Error reading config key '%s%zu', config key would truncate\n", key_root,
              cnt);
      return -1;
    }

    int read = cfg_read_str(h, key_name, &buff[offset], buff_sz - offset);
    if (read < 0) {
      return -1;
    }

    if (read == 0) {
      break;
    }

    offset += read;
    cnt += 1;
  }

  if (cnt == 0) {
    fprintf(stderr, "Error no keys for %s...\n", key_root);
    return -1;
  }

  return cnt;
}

static bool parse_should_restart_on_crash(void *h, size_t sz, const char *key_root,
                                          const bool default_opt, size_t should_restart_arr_sz,
                                          bool *should_restart_arr) {
  if (sz > should_restart_arr_sz) {
    printf(
        "Error: there are more occupancy transition commands than can be allocated in the "
        "should_restart vector. Please increase the size of this vector, or use less commands.\n");
    abort();
  }

  for (size_t i = 0; i < sz; ++i) {
    char key_name[50];
    int ret = snprintf(key_name, sizeof(key_name), "%s%zu", key_root, i);
    if (ret < 0) {
      perror("parse_should_restart_on_crash: Error reading config key, snprintf fail\n");
      return false;
    }
    if (!cfg_read_bool(h, key_name, &should_restart_arr[i])) {
      // if reading fail, use default
      should_restart_arr[i] = default_opt;
    }
  }

  return true;
}

bool cfg_read(const char *fpath, struct Config *cfg) {
  void *h = cfg_init(fpath);
  bool ok = true;

  ok &= cfg_read_bool(h, "gpio_debug", &cfg->gpio_debug);
  ok &= cfg_read_bool(h, "gpio_use_mock", &cfg->gpio_use_mock);
  ok &= cfg_read_size_t(h, "sensor_pin", &cfg->sensor_pin);
  ok &= cfg_read_size_t(h, "sensor_poll_period_secs", &cfg->sensor_poll_period_secs);
  ok &= cfg_read_size_t(h, "sensor_monitor_window_seconds", &cfg->sensor_monitor_window_seconds);
  ok &= cfg_read_size_t(h, "rising_edge_occupancy_threshold_pct",
                        &cfg->rising_edge_occupancy_threshold_pct);
  ok &= cfg_read_size_t(h, "falling_edge_vacancy_threshold_pct",
                        &cfg->falling_edge_vacancy_threshold_pct);
  ok &= cfg_read_size_t(h, "vacancy_motion_timeout_seconds", &cfg->vacancy_motion_timeout_seconds);
  ok &= cfg_read_size_t(h, "restart_cmd_wait_time_seconds", &cfg->restart_cmd_wait_time_seconds);
  ok &= cfg_read_bool(h, "restart_cmd_on_unexpected_exit", &cfg->restart_cmd_on_unexpected_exit);
  ok &= cfg_read_size_t(h, "crash_on_repeated_cmd_failure_count",
                        &cfg->crash_on_repeated_cmd_failure_count);

  {
    int read_cnt = cfg_read_str_arr(h, "on_occupancy_cmd", cfg->on_occupancy_cmds,
                                    sizeof(cfg->on_occupancy_cmds));
    if (read_cnt <= 0) {
      ok = false;
    } else {
      cfg->on_occupancy_cmds_cnt = (unsigned)read_cnt;
      parse_should_restart_on_crash(h, cfg->on_occupancy_cmds_cnt, "on_crash_restart_occupancy_cmd",
                                    cfg->restart_cmd_on_unexpected_exit,
                                    sizeof(cfg->occupancy_cmd_should_restart_on_crash),
                                    cfg->occupancy_cmd_should_restart_on_crash);
    }
  }

  {
    int read_cnt =
        cfg_read_str_arr(h, "on_vacancy_cmd", cfg->on_vacancy_cmds, sizeof(cfg->on_vacancy_cmds));
    if (read_cnt <= 0) {
      // Ignore if user wants no vacancy commands
      // ok = false;
    } else {
      cfg->on_vacancy_cmds_cnt = (unsigned)read_cnt;
      parse_should_restart_on_crash(h, cfg->on_vacancy_cmds_cnt, "on_crash_restart_occupancy_cmd",
                                    cfg->restart_cmd_on_unexpected_exit,
                                    sizeof(cfg->vacancy_cmd_should_restart_on_crash),
                                    cfg->vacancy_cmd_should_restart_on_crash);
    }
  }

  cfg_free(h);
  return ok;
}

void cfg_each_cmd(const char *cmds, cfg_each_cmd_cb_t cb, void *usr) {
  size_t cmd_i = 0;
  size_t cmd_f = 0;
  const size_t MAX_SZ = sizeof(((struct Config *)NULL)->on_occupancy_cmds);

  if (cmds[MAX_SZ - 1] != '\0') {
    fprintf(stderr, "cfg_each_cmd received a non-null-terminated string\n");
    abort();
    return;
  }

  size_t cmd_idx = 0;
  while (cmd_f < MAX_SZ) {
    while (cmds[cmd_f++] != '\0' && cmd_f < MAX_SZ)
      ;
    if (cmd_f - cmd_i == 1) {
      // Empty string, only \0 found
      return;
    }
    cb(usr, cmd_idx++, &cmds[cmd_i]);
    cmd_i = cmd_f;
  }
}

static void dbg_on_occupancy_cmds(void *usr, size_t i, const char *cmd) {
  const struct Config *cfg = usr;
  printf("on_occupancy_cmd[%zu]=%s (%s restart on crash)\n", i, cmd,
         cfg->occupancy_cmd_should_restart_on_crash[i] ? "will" : "won't");
}
static void dbg_on_vacancy_cmds(void *usr, size_t i, const char *cmd) {
  const struct Config *cfg = usr;
  printf("on_vacancy_cmd[%zu]=%s (%s restart on crash)\n", i, cmd,
         cfg->occupancy_cmd_should_restart_on_crash[i] ? "will" : "won't");
}

void cfg_debug(struct Config *cfg) {
  printf("Config debug\n");
  printf("sensor_pin=%zu\n", cfg->sensor_pin);
  printf("sensor_poll_period_secs=%zu\n", cfg->sensor_poll_period_secs);
  printf("sensor_monitor_window_seconds=%zu\n", cfg->sensor_monitor_window_seconds);
  printf("rising_edge_occupancy_threshold_pct=%zu\n", cfg->rising_edge_occupancy_threshold_pct);
  printf("falling_edge_vacancy_threshold_pct=%zu\n", cfg->falling_edge_vacancy_threshold_pct);
  printf("vacancy_motion_timeout_seconds=%zu\n", cfg->vacancy_motion_timeout_seconds);
  printf("restart_cmd_on_unexpected_exit=%s\n",
         (cfg->restart_cmd_on_unexpected_exit ? "true" : "false"));
  printf("restart_cmd_wait_time_seconds=%zu\n", cfg->restart_cmd_wait_time_seconds);
  printf("crash_on_repeated_cmd_failure_count=%zu\n", cfg->crash_on_repeated_cmd_failure_count);
  cfg_each_cmd(cfg->on_occupancy_cmds, &dbg_on_occupancy_cmds, cfg);
  cfg_each_cmd(cfg->on_vacancy_cmds, &dbg_on_vacancy_cmds, cfg);
}
