#pragma once

struct wl_ctrl;

struct wl_ctrl *wl_ctrl_init();
void wl_ctrl_free(struct wl_ctrl *);

void wl_ctrl_display_on(struct wl_ctrl *);
void wl_ctrl_display_off(struct wl_ctrl *);
