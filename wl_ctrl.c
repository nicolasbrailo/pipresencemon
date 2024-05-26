// Hacked from https://git.sr.ht/~emersion/wlr-randr/tree/master/item/main.c
//
// Useful literature:
// https://wayland.app/protocols/
// https://wayland-book.com/registry/binding.html
// https://git.sr.ht/~emersion/wlr-randr/tree/master/item/main.c

#include "wl_ctrl.h"

#include <strings.h>

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

// Automatically generated from:
// https://gitlab.freedesktop.org/wlroots/wlr-protocols (see makefile)
#include "wl_protos/wlr-output-management-unstable-v1.h"

struct wl_ctrl;
struct randr_head;

struct randr_head {
  struct wl_ctrl *state;
  struct zwlr_output_head_v1 *wlr_head;
  struct wl_list link;
  char *name, *description;
  char *make, *model, *serial_number;
  bool enabled;
};

struct wl_ctrl {
  struct zwlr_output_manager_v1 *output_manager;
  struct wl_display *display;
  struct wl_registry *registry;
  struct wl_list heads;
  uint32_t serial;
  bool has_serial;
  bool running;
  bool failed;
};

static void
config_handle_succeeded(void *data,
                        struct zwlr_output_configuration_v1 *config) {
  struct wl_ctrl *state = data;
  zwlr_output_configuration_v1_destroy(config);
  state->running = false;
}

static void config_handle_failed(void *data,
                                 struct zwlr_output_configuration_v1 *config) {
  struct wl_ctrl *state = data;
  zwlr_output_configuration_v1_destroy(config);
  state->running = false;
  state->failed = true;

  fprintf(stderr, "failed to apply configuration\n");
}

static void
config_handle_cancelled(void *data,
                        struct zwlr_output_configuration_v1 *config) {
  struct wl_ctrl *state = data;
  zwlr_output_configuration_v1_destroy(config);
  state->running = false;
  state->failed = true;

  fprintf(stderr, "configuration cancelled, please try again\n");
}

static const struct zwlr_output_configuration_v1_listener config_listener = {
    .succeeded = config_handle_succeeded,
    .failed = config_handle_failed,
    .cancelled = config_handle_cancelled,
};

static void apply_state(struct wl_ctrl *state) {
  struct zwlr_output_configuration_v1 *config =
      zwlr_output_manager_v1_create_configuration(state->output_manager,
                                                  state->serial);
  zwlr_output_configuration_v1_add_listener(config, &config_listener, state);

  struct randr_head *head;
  wl_list_for_each(head, &state->heads, link) {
    if (!head->enabled) {
      zwlr_output_configuration_v1_disable_head(config, head->wlr_head);
    } else {
      struct zwlr_output_configuration_head_v1 *config_head =
          zwlr_output_configuration_v1_enable_head(config, head->wlr_head);
      zwlr_output_configuration_head_v1_destroy(config_head);
    }
  }

  zwlr_output_configuration_v1_apply(config);
}

// wlr_output_management will report a lot of properties about the display we
// don't care about
static void head_handle_physical_size(void *, struct zwlr_output_head_v1 *,
                                      int32_t, int32_t) {}
static void head_handle_position(void *, struct zwlr_output_head_v1 *, int32_t,
                                 int32_t) {}
static void head_handle_transform(void *, struct zwlr_output_head_v1 *,
                                  int32_t) {}
static void head_handle_scale(void *, struct zwlr_output_head_v1 *,
                              wl_fixed_t) {}
static void head_handle_adaptive_sync(void *, struct zwlr_output_head_v1 *,
                                      uint32_t) {}
static void head_handle_mode(void *, struct zwlr_output_head_v1 *,
                             struct zwlr_output_mode_v1 *) {}
static void head_handle_current_mode(void *, struct zwlr_output_head_v1 *,
                                     struct zwlr_output_mode_v1 *) {}

static void head_handle_name(void *data, struct zwlr_output_head_v1 *,
                             const char *name) {
  struct randr_head *head = data;
  head->name = strdup(name);
}

static void head_handle_description(void *data, struct zwlr_output_head_v1 *,
                                    const char *description) {
  struct randr_head *head = data;
  head->description = strdup(description);
}

static void head_handle_enabled(void *data, struct zwlr_output_head_v1 *,
                                int32_t enabled) {
  struct randr_head *head = data;
  head->enabled = !!enabled;
}

static void head_handle_finished(void *data, struct zwlr_output_head_v1 *) {
  struct randr_head *head = data;
  wl_list_remove(&head->link);
  if (zwlr_output_head_v1_get_version(head->wlr_head) >= 3) {
    zwlr_output_head_v1_release(head->wlr_head);
  } else {
    zwlr_output_head_v1_destroy(head->wlr_head);
  }
  free(head->name);
  free(head->description);
  free(head);
}

static void head_handle_make(void *data, struct zwlr_output_head_v1 *,
                             const char *make) {
  struct randr_head *head = data;
  head->make = strdup(make);
}

static void head_handle_model(void *data, struct zwlr_output_head_v1 *,
                              const char *model) {
  struct randr_head *head = data;
  head->model = strdup(model);
}

static void head_handle_serial_number(void *data, struct zwlr_output_head_v1 *,
                                      const char *serial_number) {
  struct randr_head *head = data;
  head->serial_number = strdup(serial_number);
}

static const struct zwlr_output_head_v1_listener head_listener = {
    .name = head_handle_name,
    .description = head_handle_description,
    .physical_size = head_handle_physical_size,
    .mode = head_handle_mode,
    .enabled = head_handle_enabled,
    .current_mode = head_handle_current_mode,
    .position = head_handle_position,
    .transform = head_handle_transform,
    .scale = head_handle_scale,
    .finished = head_handle_finished,
    .make = head_handle_make,
    .model = head_handle_model,
    .serial_number = head_handle_serial_number,
    .adaptive_sync = head_handle_adaptive_sync,
};

static void output_manager_handle_head(void *data,
                                       struct zwlr_output_manager_v1 *,
                                       struct zwlr_output_head_v1 *wlr_head) {
  struct wl_ctrl *state = data;
  struct randr_head *head = calloc(1, sizeof(*head));
  head->state = state;
  head->wlr_head = wlr_head;
  wl_list_insert(state->heads.prev, &head->link);

  zwlr_output_head_v1_add_listener(wlr_head, &head_listener, head);
}

static void output_manager_handle_done(void *data,
                                       struct zwlr_output_manager_v1 *,
                                       uint32_t serial) {
  struct wl_ctrl *state = data;
  state->serial = serial;
  state->has_serial = true;
}

static void output_manager_handle_finished(void *,
                                           struct zwlr_output_manager_v1 *) {
  // This space is intentionally left blank
}

static const struct zwlr_output_manager_v1_listener output_manager_listener = {
    .head = output_manager_handle_head,
    .done = output_manager_handle_done,
    .finished = output_manager_handle_finished,
};

static void registry_handle_global(void *data, struct wl_registry *registry,
                                   uint32_t name, const char *interface,
                                   uint32_t version) {
  struct wl_ctrl *state = data;

  if (strcmp(interface, zwlr_output_manager_v1_interface.name) == 0) {
    uint32_t version_to_bind = version <= 4 ? version : 4;
    state->output_manager = wl_registry_bind(
        registry, name, &zwlr_output_manager_v1_interface, version_to_bind);
    zwlr_output_manager_v1_add_listener(state->output_manager,
                                        &output_manager_listener, state);
  }
}

static void registry_handle_global_remove(void *, struct wl_registry *,
                                          uint32_t) {
  // This space is intentionally left blank
}

static const struct wl_registry_listener registry_listener = {
    .global = registry_handle_global,
    .global_remove = registry_handle_global_remove,
};

#define RPI_DEFAULT_WL_DISPLAY_NAME "wayland-1"

struct wl_ctrl *wl_ctrl_init() {
  struct wl_ctrl *state = malloc(sizeof(struct wl_ctrl));
  bzero(state, sizeof(struct wl_ctrl));
  state->running = true;

  wl_list_init(&state->heads);

  state->display = wl_display_connect(RPI_DEFAULT_WL_DISPLAY_NAME);
  if (state->display == NULL) {
    fprintf(stderr, "failed to connect to display\n");
    return NULL;
  }

  state->registry = wl_display_get_registry(state->display);
  wl_registry_add_listener(state->registry, &registry_listener, state);

  if (wl_display_roundtrip(state->display) < 0) {
    fprintf(stderr, "wl_display_roundtrip failed\n");
    return NULL;
  }

  if (state->output_manager == NULL) {
    fprintf(stderr, "compositor doesn't support "
                    "wlr-output-management-unstable-v1\n");
    return NULL;
  }

  while (!state->has_serial) {
    if (wl_display_dispatch(state->display) < 0) {
      fprintf(stderr, "wl_display_dispatch failed\n");
      return NULL;
    }
  }

  return state;
}

void wl_ctrl_free(struct wl_ctrl *state) {
  struct randr_head *head, *tmp_head;
  wl_list_for_each_safe(head, tmp_head, &state->heads, link) {
    zwlr_output_head_v1_destroy(head->wlr_head);
    free(head->name);
    free(head->description);
    free(head->make);
    free(head->model);
    free(head->serial_number);
    free(head);
  }
  zwlr_output_manager_v1_destroy(state->output_manager);
  wl_registry_destroy(state->registry);
  wl_display_disconnect(state->display);
  free(state);
}

void wl_ctrl_display_on(struct wl_ctrl *state) {
  state->running = true;
  {
    struct randr_head *head;
    wl_list_for_each(head, &state->heads, link) {
      if (head->enabled) {
        printf("Display already ON %s \"%s\"\n", head->name, head->description);
      } else {
        printf("Will POWON %s \"%s\"\n", head->name, head->description);
        head->enabled = true;
      }
    }
  }

  apply_state(state);

  while (state->running && wl_display_dispatch(state->display) != -1) {
    // This space intentionally left blank
  }
}

void wl_ctrl_display_off(struct wl_ctrl *state) {
  state->running = true;
  {
    struct randr_head *head;
    wl_list_for_each(head, &state->heads, link) {
      if (head->enabled) {
        printf("Will shutdown %s \"%s\"\n", head->name, head->description);
        head->enabled = false;
      } else {
        printf("Display already off %s \"%s\"\n", head->name,
               head->description);
      }
    }
  }

  apply_state(state);

  while (state->running && wl_display_dispatch(state->display) != -1) {
    // This space intentionally left blank
  }
}
