#include "gpio.h"
#include "gpio_pin_active_monitor.h"
#include "wl_ctrl.h"
#include <string.h>

#include <signal.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/wait.h>

void gpio_dumpdebug(struct GPIO *gpio) {
  unsigned prev_gpio_in = 0;
  while (1) {
    prev_gpio_in = gpio_get_and_print_delta(gpio, prev_gpio_in);
    sleep(1);
  }
}

#define AMBIENCE_NO_MOTION_TIMEOUT 30

enum PresenceState {
  PRESENCE_USER_INTERACTIVE,
  PRESENCE_AMBIENCE,
  PRESENCE_NO,
};

atomic_bool gUsrStop = false;
void sighandler(int) { gUsrStop = true; }

void enter_ambience_mode(pid_t* ambience_app_pid) {
    if (*ambience_app_pid != 0) {
        return;
    }

    printf("Launching ambience app...\n");
    *ambience_app_pid = 42;
}
void leave_ambience_mode(pid_t* ambience_app_pid) {
    if (*ambience_app_pid == 0) {
        return;
    }
    printf("Stopping ambience app...\n");
    *ambience_app_pid = 0;
}

int main() {
  signal(SIGINT, sighandler);

  struct wl_ctrl *display_state = wl_ctrl_init("HDMI-A-1");
  if (!display_state) {
    printf("No display\n");
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

  // Because a PIR will be motion based, we want a low threshold and a long history
  struct GpioPinActiveMonitor_args args = {
      .sensor_pin = 26,
      .sensor_poll_period_secs = 2,
      .monitor_window_seconds = 30,
      .rising_edge_active_threshold_pct = 20,
      .falling_edge_inactive_threshold_pct = 10,
      .start_active = start_active,
  };
  struct GpioPinActiveMonitor *mon = gpio_active_monitor_init(args);
  if (!mon) {
    wl_ctrl_free(display_state);
    return 1;
  }

  enum PresenceState presence = start_active ? PRESENCE_AMBIENCE : PRESENCE_NO;
  size_t ambienceCountdownSecs = 0;
  pid_t ambience_app_pid = 0;
  while (!gUsrStop) {
    switch (presence) {
    case PRESENCE_USER_INTERACTIVE:
      printf("TODO - shouldn't be here\n");
      presence = PRESENCE_AMBIENCE;
      ambienceCountdownSecs = AMBIENCE_NO_MOTION_TIMEOUT;
      break;
    case PRESENCE_AMBIENCE:
      enter_ambience_mode(&ambience_app_pid);
      if (gpio_active_monitor_pin_active(mon)) {
        ambienceCountdownSecs = AMBIENCE_NO_MOTION_TIMEOUT;
      } else {
        ambienceCountdownSecs--;
      }
      printf("GPIO reports %f%% motion, gpio_state=%s ambienceCountdown=%lu secs\n",
             100.f * gpio_active_monitor_active_pct(mon),
             gpio_active_monitor_pin_active(mon) ? "active" : "inactive", ambienceCountdownSecs);
      if (ambienceCountdownSecs == 0) {
        printf("Ambience timeout, screen off\n");
        wl_ctrl_display_off(display_state);
        presence = PRESENCE_NO;
      }
      break;
    case PRESENCE_NO:
      leave_ambience_mode(&ambience_app_pid);
      printf("GPIO reports %f%% motion, gpio_state=%s\n",
             100.f * gpio_active_monitor_active_pct(mon),
             gpio_active_monitor_pin_active(mon) ? "active" : "inactive");
      if (gpio_active_monitor_pin_active(mon)) {
        printf("Motion detected, moving to ambience mode\n");
        wl_ctrl_display_on(display_state);
        presence = PRESENCE_AMBIENCE;
        ambienceCountdownSecs = AMBIENCE_NO_MOTION_TIMEOUT;
      }
      break;
    };

    sleep(1);
  }

  gpio_active_monitor_free(mon);
  wl_ctrl_free(display_state);
  return 0;
}
