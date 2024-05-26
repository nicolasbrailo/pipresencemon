#pragma once

#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <errno.h>
#include <getopt.h>
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wayland-client.h>
#include "outman_wproto.h"

struct randr_state;
struct randr_head;

struct randr_head {
	struct randr_state *state;
	struct zwlr_output_head_v1 *wlr_head;
	struct wl_list link;
	char *name, *description;
	char *make, *model, *serial_number;
	bool enabled;
};

struct randr_state {
	struct zwlr_output_manager_v1 *output_manager;
	struct wl_display *display;
	struct wl_registry *registry;
	struct wl_list heads;
	uint32_t serial;
	bool has_serial;
	bool running;
	bool failed;
};


void wl_display_turn_on();
void wl_display_turn_off();


int init_things(struct randr_state *state);
void deinit_things(struct randr_state *state);
void toggle(struct randr_state *state);


