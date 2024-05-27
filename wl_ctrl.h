#pragma once

struct wl_ctrl;

// interesting_screen -> Screen name as reported by wlr-randr. If NULL, will try to manage all
// screens.
struct wl_ctrl *wl_ctrl_init(const char *interesting_screen);
void wl_ctrl_free(struct wl_ctrl *);
void wl_print_display_names(struct wl_ctrl *);

void wl_ctrl_display_on(struct wl_ctrl *);
void wl_ctrl_display_off(struct wl_ctrl *);

enum WlCtrlDisplayState {
  WL_CTRL_DISPLAYS_UNKNOWN,
  WL_CTRL_DISPLAYS_INCONSISTENT,
  WL_CTRL_DISPLAYS_ON,
  WL_CTRL_DISPLAYS_OFF,
};

enum WlCtrlDisplayState wl_ctrl_display_query_state(struct wl_ctrl *);
