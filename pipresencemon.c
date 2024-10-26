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
void sighandler(int) { gUsrStop = true; }

int main(int argc, const char** argv) {
  const char* cfg_fname = DEFAULT_CFG_FNAME;
  if (argc > 1) {
      cfg_fname = argv[1];
  }

  struct Config cfg;
  if (!cfg_read(cfg_fname, &cfg)) {
    fprintf(stderr, "Can't open config file %s\n", cfg_fname);
    return 1;
  }
  cfg_debug(&cfg);

  struct GpioPinActiveMonitor *gpio_mon = gpio_active_monitor_init_from_cfg(&cfg, /*start_active=*/true);
  if (!gpio_mon) {
    fprintf(stderr, "Failed to monitor gpio on startup\n");
    return 1;
  }

  struct OccupancyCommands *occupancy_cmds = occupancy_commands_init(&cfg);
  if (!occupancy_cmds) {
      free(gpio_mon);
      return 1;
  }

  signal(SIGINT, sighandler);
  while (!gUsrStop) {
    const bool occupancy = gpio_active_monitor_pin_active(gpio_mon);
    if (occupancy) {
      printf("Occupancy detected by GPIO sensor\n");
      occupancy_commands_on_occupancy(occupancy_cmds);
    } else {
      printf("No occupancy detected by GPIO sensor\n");
      occupancy_commands_on_vacancy(occupancy_cmds);
    }
    sleep(1);
  }

  occupancy_commands_free(occupancy_cmds);
  gpio_active_monitor_free(gpio_mon);

  return 0;

#if 0
  struct wl_ctrl *display_state = wl_ctrl_init(NULL);
  if (!ambience_cfg || !display_state || !config_ok) {
    fprintf(stderr, "Failed to startup\n");
    wl_ctrl_free(display_state);
    ambience_mode_free(ambience_cfg);
    cfg_free(config);
    return 1;
  }

  // Check init state of display, force activity pin to match display active state
  bool start_active = true;
  switch (wl_ctrl_display_query_state(display_state)) {
  case WL_CTRL_DISPLAYS_UNKNOWN: // fallthrough
  case WL_CTRL_DISPLAYS_INCONSISTENT:
    printf("Can't find display state, will try to switch on\n");
    wl_ctrl_display_on(display_state);
    // fallthrough
  case WL_CTRL_DISPLAYS_ON:
    printf("Managed display is on, assuming initial presence=1\n");
    break;
  case WL_CTRL_DISPLAYS_OFF:
    printf("Managed display is off, assuming initial presence=0\n");
    start_active = false;
    break;
  }


  enum PresenceState presence = start_active ? PRESENCE_AMBIENCE : PRESENCE_NO;
  size_t ambienceCountdownSecs = 0;
  signal(SIGINT, sighandler);
  while (!gUsrStop) {
    switch (presence) {
    case PRESENCE_USER_INTERACTIVE:
      printf("TODO - shouldn't be here\n");
      presence = PRESENCE_AMBIENCE;
      ambienceCountdownSecs = NO_PRESENCE_MOTION_TIMEOUT_SECONDS;
      break;
    case PRESENCE_AMBIENCE:
      ambience_mode_enter(ambience_cfg);
      if (gpio_active_monitor_pin_active(gpio_mon)) {
        ambienceCountdownSecs = NO_PRESENCE_MOTION_TIMEOUT_SECONDS;
      } else {
        ambienceCountdownSecs--;
      }
      printf("GPIO reports %f%% motion, gpio_state=%s ambienceCountdown=%lu secs\n",
             100.f * gpio_active_monitor_active_pct(gpio_mon),
             gpio_active_monitor_pin_active(gpio_mon) ? "active" : "inactive",
             ambienceCountdownSecs);
      if (ambienceCountdownSecs == 0) {
        printf("Ambience timeout, screen off\n");
        wl_ctrl_display_off(display_state);
        presence = PRESENCE_NO;
      }
      break;
    case PRESENCE_NO:
      ambience_mode_leave(ambience_cfg);
      printf("GPIO reports %f%% motion, gpio_state=%s\n",
             100.f * gpio_active_monitor_active_pct(gpio_mon),
             gpio_active_monitor_pin_active(gpio_mon) ? "active" : "inactive");
      if (gpio_active_monitor_pin_active(gpio_mon)) {
        printf("Motion detected, moving to ambience mode\n");
        wl_ctrl_display_on(display_state);
        presence = PRESENCE_AMBIENCE;
        ambienceCountdownSecs = NO_PRESENCE_MOTION_TIMEOUT_SECONDS;
      }
      break;
    };

    sleep(1);
  }

  wl_ctrl_free(display_state);



enum PresenceState {
  PRESENCE_USER_INTERACTIVE,
  PRESENCE_AMBIENCE,
  PRESENCE_NO,
};
#endif

  return 0;
}
