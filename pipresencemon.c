#include "ambience_mode.h"
#include "cfg.h"
#include "gpio.h"
#include "gpio_pin_active_monitor.h"
#include "wl_ctrl.h"

#include <signal.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define CFG_FNAME "pipresencemon.cfg"

void gpio_dumpdebug(struct GPIO *gpio) {
  unsigned prev_gpio_in = 0;
  while (1) {
    prev_gpio_in = gpio_get_and_print_delta(gpio, prev_gpio_in);
    sleep(1);
  }
}

enum PresenceState {
  PRESENCE_USER_INTERACTIVE,
  PRESENCE_AMBIENCE,
  PRESENCE_NO,
};

atomic_bool gUsrStop = false;
void sighandler(int) { gUsrStop = true; }

int main() {
  void *config = cfg_init(CFG_FNAME);
  if (!config) {
    fprintf(stderr, "Can't open config file %s\n", CFG_FNAME);
    return 1;
  }

  size_t NO_PRESENCE_MOTION_TIMEOUT_SECONDS;
  bool config_ok = true;
  config_ok = config_ok & cfg_read_size_t(config, "no_presence_motion_timeout_seconds",
                                          &NO_PRESENCE_MOTION_TIMEOUT_SECONDS);

  struct AmbienceModeCfg *ambience_cfg = ambience_mode_init(config);
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

  struct GpioPinActiveMonitor *gpio_mon = gpio_active_monitor_init_from_cfg(config, start_active);
  if (!gpio_mon) {
    fprintf(stderr, "Failed to monitor gpio on startup\n");
    wl_ctrl_free(display_state);
    ambience_mode_free(ambience_cfg);
    return 1;
  }

  cfg_free(config);

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

  gpio_active_monitor_free(gpio_mon);
  wl_ctrl_free(display_state);
  ambience_mode_free(ambience_cfg);

  return 0;
}
