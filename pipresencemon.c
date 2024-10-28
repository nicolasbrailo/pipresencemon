#include "cfg.h"
#include "gpio_pin_active_monitor.h"
#include "occupancy_commands.h"

#include <signal.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define DEFAULT_CFG_FNAME "pipresencemon.cfg"

atomic_bool gUsrStop = false;
void sighandler(int _unused __attribute__((unused))) { gUsrStop = true; }

int main(int argc, const char **argv) {
  const char *cfg_fname = DEFAULT_CFG_FNAME;
  if (argc > 1) {
    cfg_fname = argv[1];
  }

  struct Config cfg;
  if (!cfg_read(cfg_fname, &cfg)) {
    fprintf(stderr, "Can't open config file %s\n", cfg_fname);
    return 1;
  }
  cfg_debug(&cfg);

  struct GpioPinActiveMonitor *gpio_mon = gpio_active_monitor_init(&cfg);
  struct OccupancyCommands *occupancy_cmds = occupancy_commands_init(&cfg);

  int ret = 0;
  if (!gpio_mon || !occupancy_cmds) {
    fprintf(stderr, "Startup fail\n");
    ret = 1;
    goto CLEANUP;
  }

  signal(SIGINT, sighandler);
  bool currently_occupied = gpio_active_monitor_pin_active(gpio_mon);
  if (currently_occupied) {
    printf("Startup assumes occupancy\n");
    occupancy_commands_on_occupancy(occupancy_cmds);
  } else if (currently_occupied) {
    printf("Startup assumes vacancy\n");
    occupancy_commands_on_vacancy(occupancy_cmds);
  }

  while (!gUsrStop) {
    const bool occupancy = gpio_active_monitor_pin_active(gpio_mon);
    const bool was_occupied = currently_occupied;
    currently_occupied = occupancy;
    if (!was_occupied && occupancy) {
      printf("Occupancy detected by GPIO sensor\n");
      occupancy_commands_on_occupancy(occupancy_cmds);
    } else if (was_occupied && !occupancy) {
      printf("Vacancy detected by GPIO sensor\n");
      occupancy_commands_on_vacancy(occupancy_cmds);
    }

    occupancy_commands_tick(occupancy_cmds);
    sleep(1);
  }

CLEANUP:
  occupancy_commands_free(occupancy_cmds);
  gpio_active_monitor_free(gpio_mon);
  return ret;
}
