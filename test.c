#include "gpio_pin_active_monitor.h"
#include "gpio.h"

#include <stdatomic.h>
#include <stdbool.h>
#include <signal.h>
#include <unistd.h>
#include <stdio.h>

#include <wayland-client.h>

// WAYLAND_DISPLAY="wayland-1" wlr-randr --output HDMI-A-1 --off

void gpio_dumpdebug(struct GPIO *gpio) {
    unsigned prev_gpio_in = 0;
    while (1) {
        prev_gpio_in = gpio_get_and_print_delta(gpio, prev_gpio_in);
        sleep(1);
    }
}

atomic_bool gUsrStop = false;
void sighandler(int) {
    gUsrStop = true;
}

int main() {
    signal(SIGINT, sighandler);
    struct GpioPinActiveMonitor_args args = {
        .sensor_pin=26,
        .sensor_poll_period_secs=1,
        .monitor_window_seconds=30,
        .rising_edge_active_threshold=.1,
        .falling_edge_inactive_threshold=.6,
    };
    struct GpioPinActiveMonitor *mon = gpio_active_monitor_create(args);

    while (!gUsrStop) {
        printf("Active? %f / %i\n", gpio_active_monitor_active_pct(mon), gpio_active_monitor_pin_active(mon));
        sleep(1);
    }

    gpio_active_monitor_close(mon);
    return 0;
}

