#include "gpio.h"
#include "gpio_pin_active_monitor.h"
#include "wl_ctrl.h"
#include <string.h>

#include <signal.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdio.h>
#include <unistd.h>

void gpio_dumpdebug(struct GPIO *gpio) {
  unsigned prev_gpio_in = 0;
  while (1) {
    prev_gpio_in = gpio_get_and_print_delta(gpio, prev_gpio_in);
    sleep(1);
  }
}

atomic_bool gUsrStop = false;
void sighandler(int) { gUsrStop = true; }

int main() {
  signal(SIGINT, sighandler);

  // Because a PIR will be motion based, we want a low threshold and a long history
  struct GpioPinActiveMonitor_args args = {
      .sensor_pin = 26,
      .sensor_poll_period_secs = 2,
      .monitor_window_seconds = 30,
      .rising_edge_active_threshold_pct = 20,
      .falling_edge_inactive_threshold_pct = 10,
  };
  struct GpioPinActiveMonitor *mon = gpio_active_monitor_init(args);
  if (!mon) {
    return 1;
  }

  struct wl_ctrl *display_state = wl_ctrl_init();
  if (!display_state) {
    printf("No display\n");
    gpio_active_monitor_free(mon);
    return 1;
  }

  bool isOnNow = false;
  while (!gUsrStop) {
    if (gpio_active_monitor_pin_active(mon) && !isOnNow) {
      wl_ctrl_display_on(display_state);
      isOnNow = true;
      printf("PIR reports on %f pct, turn on display\n", gpio_active_monitor_active_pct(mon));
    } else if (!gpio_active_monitor_pin_active(mon) && isOnNow) {
      wl_ctrl_display_off(display_state);
      isOnNow = false;
      printf("PIR reports off %f pct, shutdown display\n", gpio_active_monitor_active_pct(mon));
    } else {
      // display state matches wanted state
      printf("Active? %f / %i\n", gpio_active_monitor_active_pct(mon),
             gpio_active_monitor_pin_active(mon));
    }
    sleep(1);
  }

  gpio_active_monitor_free(mon);
  wl_ctrl_free(display_state);
  return 0;
}
