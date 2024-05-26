#pragma once

struct wl_display;

struct wl_display* wl_display_init();
void wl_display_free(struct wl_display*);
void wl_display_on(struct wl_display*);
void wl_display_off(struct wl_display*);

