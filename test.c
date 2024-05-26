#if 0
#include <string.h>
#include "gpio_pin_active_monitor.h"
#include "gpio.h"

#include <stdatomic.h>
#include <stdbool.h>
#include <signal.h>
#include <unistd.h>
#include <stdio.h>

#include <wayland-client.h>
#include "wayland_wproto.h"
#include "wayfire_wproto.h"
#include "foo_wproto.h"

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

int main2() {
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

static void registry_handle_global(void *data, struct wl_registry *registry, uint32_t name, const char *interface, uint32_t version)
{
    if (strcmp(interface, zwlr_output_power_manager_v1_interface.name) == 0) {
        printf("interface: '%s', version: %d, name: %d\n", interface, version, name);
        uint32_t version_to_bind = version <= 4 ? version : 4;
        wl_registry_bind(registry, name, &zwlr_output_power_manager_v1_interface, version_to_bind);
        printf("%s %d %d\n", zwlr_output_power_manager_v1_interface.name, zwlr_output_power_manager_v1_interface.method_count, zwlr_output_power_manager_v1_interface.event_count);
    }
}

static void registry_handle_global_remove(void *data, struct wl_registry *registry, uint32_t name)
{
}

static const struct wl_registry_listener registry_listener = {
    .global = registry_handle_global,
    .global_remove = registry_handle_global_remove,
};

// https://wayland.app/protocols/
// https://wayland-book.com/registry/binding.html
// https://git.sr.ht/~emersion/wlr-randr/tree/master/item/main.c
int main() {
    struct wl_display *display = wl_display_connect("wayland-1");
    struct wl_registry *registry = wl_display_get_registry(display);
    wl_registry_add_listener(registry, &registry_listener, NULL);
    wl_display_roundtrip(display);

    //zwlr_output_configuration_v1_disable_head(config, head->wlr_head);
    //zwlr_output_configuration_v1_enable_head(config, head->wlr_head);

    printf("HOA\n");

    wl_display_disconnect(display);
    return 0;
}

#endif

// Hacked from https://git.sr.ht/~emersion/wlr-randr/tree/master/item/main.c


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

struct randr_mode {
	struct randr_head *head;
	struct zwlr_output_mode_v1 *wlr_mode;
	struct wl_list link;

	int32_t width, height;
	int32_t refresh; // mHz
	bool preferred;
};

enum randr_head_prop {
	RANDR_HEAD_MODE = 1 << 0,
	RANDR_HEAD_POSITION = 1 << 1,
	RANDR_HEAD_TRANSFORM = 1 << 2,
	RANDR_HEAD_SCALE = 1 << 3,
	RANDR_HEAD_ADAPTIVE_SYNC = 1 << 4,
};

struct randr_head {
	struct randr_state *state;
	struct zwlr_output_head_v1 *wlr_head;
	struct wl_list link;

	char *name, *description;
	char *make, *model, *serial_number;
	int32_t phys_width, phys_height; // mm
	struct wl_list modes;

	uint32_t changed; // enum randr_head_prop
	bool enabled;
	struct randr_mode *mode;
	struct {
		int32_t width, height;
		int32_t refresh;
	} custom_mode;
	int32_t x, y;
	enum wl_output_transform transform;
	double scale;
	enum zwlr_output_head_v1_adaptive_sync_state adaptive_sync_state;
};

struct randr_state {
	struct zwlr_output_manager_v1 *output_manager;

	struct wl_list heads;
	uint32_t serial;
	bool has_serial;
	bool running;
	bool failed;
};

static const char *output_transform_map[] = {
	[WL_OUTPUT_TRANSFORM_NORMAL] = "normal",
	[WL_OUTPUT_TRANSFORM_90] = "90",
	[WL_OUTPUT_TRANSFORM_180] = "180",
	[WL_OUTPUT_TRANSFORM_270] = "270",
	[WL_OUTPUT_TRANSFORM_FLIPPED] = "flipped",
	[WL_OUTPUT_TRANSFORM_FLIPPED_90] = "flipped-90",
	[WL_OUTPUT_TRANSFORM_FLIPPED_180] = "flipped-180",
	[WL_OUTPUT_TRANSFORM_FLIPPED_270] = "flipped-270",
};

static bool head_is_rotated(struct randr_head *head) {
	switch (head->transform) {
	case WL_OUTPUT_TRANSFORM_NORMAL:
	case WL_OUTPUT_TRANSFORM_FLIPPED:
	case WL_OUTPUT_TRANSFORM_180:
	case WL_OUTPUT_TRANSFORM_FLIPPED_180:
		return false;
	case WL_OUTPUT_TRANSFORM_90:
	case WL_OUTPUT_TRANSFORM_FLIPPED_90:
	case WL_OUTPUT_TRANSFORM_270:
	case WL_OUTPUT_TRANSFORM_FLIPPED_270:
		return true;
	}

	abort(); // unreachable
}

static int32_t head_height(struct randr_head *head) {
	if (head_is_rotated(head)) {
		return head->mode->width / head->scale;
	} else {
		return head->mode->height / head->scale;
	}
}

static int32_t head_width(struct randr_head *head) {
	if (head_is_rotated(head)) {
		return head->mode->height / head->scale;
	} else {
		return head->mode->width / head->scale;
	}
}

static void print_state(struct randr_state *state) {
	struct randr_head *head;
	wl_list_for_each(head, &state->heads, link) {
		printf("%s \"%s\" Enabled: %s\n", head->name, head->description, head->enabled ? "yes" : "no");
	}
}

static void print_json_optional_string(const char *str) {
	if (str == NULL) {
		printf("null");
		return;
	}

	printf("\"");
	for (size_t i = 0; str[i] != '\0'; i++) {
		char ch = str[i];
		switch (ch) {
		case '"':
			printf("\\\"");
			break;
		case '\\':
			printf("\\\\");
			break;
		case '\b':
			printf("\\b");
			break;
		case '\f':
			printf("\\f");
			break;
		case '\n':
			printf("\\n");
			break;
		case '\r':
			printf("\\r");
			break;
		case '\t':
			printf("\\t");
			break;
		default:
			if (ch > 0 && ch < 0x20) {
				printf("\\u%04x", ch);
			} else {
				printf("%c", ch);
			}
		}
	}
	printf("\"");
}

static void print_state_json(struct randr_state *state) {
	uint32_t version = zwlr_output_manager_v1_get_version(state->output_manager);

	printf("[");

	size_t heads_count = 0;
	struct randr_head *head;
	wl_list_for_each(head, &state->heads, link) {
		if (heads_count++) {
			printf(",");
		}
		printf("\n  {\n");

		printf("    \"name\": ");
		print_json_optional_string(head->name);
		printf(",\n");

		printf("    \"description\": ");
		print_json_optional_string(head->description);
		printf(",\n");

		printf("    \"make\": ");
		print_json_optional_string(head->make);
		printf(",\n");

		printf("    \"model\": ");
		print_json_optional_string(head->model);
		printf(",\n");

		printf("    \"serial\": ");
		print_json_optional_string(head->serial_number);
		printf(",\n");

		printf("    \"physical_size\": {\n");
		printf("      \"width\": %d,\n", head->phys_width);
		printf("      \"height\": %d\n", head->phys_height);
		printf("    },\n");

		printf("    \"enabled\": %s,\n", head->enabled ? "true" : "false");

		printf("    \"modes\": [");

		size_t modes_count = 0;
		struct randr_mode *mode;
		wl_list_for_each(mode, &head->modes, link) {
			if (modes_count++) {
				printf(",");
			}
			printf("\n      {\n");

			printf("        \"width\": %d,\n", mode->width);
			printf("        \"height\": %d,\n", mode->height);
			printf("        \"refresh\": %f,\n", (float)mode->refresh / 1000);
			printf("        \"preferred\": %s,\n", mode->preferred ? "true" : "false");
			printf("        \"current\": %s\n", head->mode == mode ? "true" : "false");

			printf("      }");
		}

		if (modes_count) {
			printf("\n    ");
		}
		printf("]");

		if (!head->enabled) {
			printf("\n");
		} else {
			printf(",\n");

			printf("    \"position\": {\n");
			printf("      \"x\": %d,\n", head->x);
			printf("      \"y\": %d\n", head->y);
			printf("    },\n");

			printf("    \"transform\": ");
			print_json_optional_string(output_transform_map[head->transform]);
			printf(",\n");

			printf("    \"scale\": %f,\n", head->scale);

			char *adaptive_sync = "null";
			if (version >= 4) {
				switch (head->adaptive_sync_state) {
				case ZWLR_OUTPUT_HEAD_V1_ADAPTIVE_SYNC_STATE_ENABLED:
					adaptive_sync = "true";
					break;
				case ZWLR_OUTPUT_HEAD_V1_ADAPTIVE_SYNC_STATE_DISABLED:
					adaptive_sync = "false";
					break;
				}
			}
			printf("    \"adaptive_sync\": %s\n", adaptive_sync);
		}

		printf("  }");
	}

	if (heads_count) {
		printf("\n");
	}
	printf("]\n");

	state->running = false;
}

static void config_handle_succeeded(void *data,
		struct zwlr_output_configuration_v1 *config) {
	struct randr_state *state = data;
	zwlr_output_configuration_v1_destroy(config);
	state->running = false;
}

static void config_handle_failed(void *data,
		struct zwlr_output_configuration_v1 *config) {
	struct randr_state *state = data;
	zwlr_output_configuration_v1_destroy(config);
	state->running = false;
	state->failed = true;

	fprintf(stderr, "failed to apply configuration\n");
}

static void config_handle_cancelled(void *data,
		struct zwlr_output_configuration_v1 *config) {
	struct randr_state *state = data;
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

static void apply_state(struct randr_state *state, bool dry_run) {
	struct zwlr_output_configuration_v1 *config =
		zwlr_output_manager_v1_create_configuration(state->output_manager,
		state->serial);
	zwlr_output_configuration_v1_add_listener(config, &config_listener, state);

	struct randr_head *head;
	wl_list_for_each(head, &state->heads, link) {
		if (!head->enabled) {
			zwlr_output_configuration_v1_disable_head(config, head->wlr_head);
			continue;
		}

		struct zwlr_output_configuration_head_v1 *config_head =
			zwlr_output_configuration_v1_enable_head(config, head->wlr_head);
		if (head->changed & RANDR_HEAD_MODE) {
			if (head->mode != NULL) {
				zwlr_output_configuration_head_v1_set_mode(config_head,
					head->mode->wlr_mode);
			} else {
				zwlr_output_configuration_head_v1_set_custom_mode(config_head,
					head->custom_mode.width, head->custom_mode.height,
					head->custom_mode.refresh);
			}
		}
		if (head->changed & RANDR_HEAD_POSITION) {
			zwlr_output_configuration_head_v1_set_position(config_head,
				head->x, head->y);
		}
		if (head->changed & RANDR_HEAD_TRANSFORM) {
			zwlr_output_configuration_head_v1_set_transform(config_head,
				head->transform);
		}
		if (head->changed & RANDR_HEAD_SCALE) {
			zwlr_output_configuration_head_v1_set_scale(config_head,
				wl_fixed_from_double(head->scale));
		}
		if (head->changed & RANDR_HEAD_ADAPTIVE_SYNC) {
			assert(zwlr_output_manager_v1_get_version(state->output_manager) >=
				ZWLR_OUTPUT_CONFIGURATION_HEAD_V1_SET_ADAPTIVE_SYNC_SINCE_VERSION);
			zwlr_output_configuration_head_v1_set_adaptive_sync(config_head,
				head->adaptive_sync_state);
		}
		zwlr_output_configuration_head_v1_destroy(config_head);
	}

		zwlr_output_configuration_v1_apply(config);
}

static void mode_handle_size(void *data, struct zwlr_output_mode_v1 *wlr_mode,
		int32_t width, int32_t height) {
	struct randr_mode *mode = data;
	mode->width = width;
	mode->height = height;
}

static void mode_handle_refresh(void *data,
		struct zwlr_output_mode_v1 *wlr_mode, int32_t refresh) {
	struct randr_mode *mode = data;
	mode->refresh = refresh;
}

static void mode_handle_preferred(void *data,
		struct zwlr_output_mode_v1 *wlr_mode) {
	struct randr_mode *mode = data;
	mode->preferred = true;
}

static void mode_handle_finished(void *data,
		struct zwlr_output_mode_v1 *wlr_mode) {
	struct randr_mode *mode = data;
	wl_list_remove(&mode->link);
	if (zwlr_output_mode_v1_get_version(mode->wlr_mode) >= 3) {
		zwlr_output_mode_v1_release(mode->wlr_mode);
	} else {
		zwlr_output_mode_v1_destroy(mode->wlr_mode);
	}
	free(mode);
}

static const struct zwlr_output_mode_v1_listener mode_listener = {
	.size = mode_handle_size,
	.refresh = mode_handle_refresh,
	.preferred = mode_handle_preferred,
	.finished = mode_handle_finished,
};

static void head_handle_name(void *data,
		struct zwlr_output_head_v1 *wlr_head, const char *name) {
	struct randr_head *head = data;
	head->name = strdup(name);
}

static void head_handle_description(void *data,
		struct zwlr_output_head_v1 *wlr_head, const char *description) {
	struct randr_head *head = data;
	head->description = strdup(description);
}

static void head_handle_physical_size(void *data,
		struct zwlr_output_head_v1 *wlr_head, int32_t width, int32_t height) {
	struct randr_head *head = data;
	head->phys_width = width;
	head->phys_height = height;
}

static void head_handle_mode(void *data,
		struct zwlr_output_head_v1 *wlr_head,
		struct zwlr_output_mode_v1 *wlr_mode) {
	struct randr_head *head = data;

	struct randr_mode *mode = calloc(1, sizeof(*mode));
	mode->head = head;
	mode->wlr_mode = wlr_mode;
	wl_list_insert(head->modes.prev, &mode->link);

	zwlr_output_mode_v1_add_listener(wlr_mode, &mode_listener, mode);
}

static void head_handle_enabled(void *data,
		struct zwlr_output_head_v1 *wlr_head, int32_t enabled) {
	struct randr_head *head = data;
	head->enabled = !!enabled;
	if (!enabled) {
		head->mode = NULL;
	}
}

static void head_handle_current_mode(void *data,
		struct zwlr_output_head_v1 *wlr_head,
		struct zwlr_output_mode_v1 *wlr_mode) {
	struct randr_head *head = data;
	struct randr_mode *mode;
	wl_list_for_each(mode, &head->modes, link) {
		if (mode->wlr_mode == wlr_mode) {
			head->mode = mode;
			return;
		}
	}
	fprintf(stderr, "received unknown current_mode\n");
	head->mode = NULL;
}

static void head_handle_position(void *data,
		struct zwlr_output_head_v1 *wlr_head, int32_t x, int32_t y) {
	struct randr_head *head = data;
	head->x = x;
	head->y = y;
}

static void head_handle_transform(void *data,
		struct zwlr_output_head_v1 *wlr_head, int32_t transform) {
	struct randr_head *head = data;
	head->transform = transform;
}

static void head_handle_scale(void *data,
		struct zwlr_output_head_v1 *wlr_head, wl_fixed_t scale) {
	struct randr_head *head = data;
	head->scale = wl_fixed_to_double(scale);
}

static void head_handle_finished(void *data,
		struct zwlr_output_head_v1 *wlr_head) {
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

static void head_handle_make(void *data,
		struct zwlr_output_head_v1 *wlr_head, const char *make) {
	struct randr_head *head = data;
	head->make = strdup(make);
}

static void head_handle_model(void *data,
		struct zwlr_output_head_v1 *wlr_head, const char *model) {
	struct randr_head *head = data;
	head->model = strdup(model);
}

static void head_handle_serial_number(void *data,
		struct zwlr_output_head_v1 *wlr_head, const char *serial_number) {
	struct randr_head *head = data;
	head->serial_number = strdup(serial_number);
}

static void head_handle_adaptive_sync(void *data,
		struct zwlr_output_head_v1 *wlr_head, uint32_t state) {
	struct randr_head *head = data;
	head->adaptive_sync_state = state;
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
		struct zwlr_output_manager_v1 *manager,
		struct zwlr_output_head_v1 *wlr_head) {
	struct randr_state *state = data;

	struct randr_head *head = calloc(1, sizeof(*head));
	head->state = state;
	head->wlr_head = wlr_head;
	head->scale = 1.0;
	wl_list_init(&head->modes);
	wl_list_insert(state->heads.prev, &head->link);

	zwlr_output_head_v1_add_listener(wlr_head, &head_listener, head);
}

static void output_manager_handle_done(void *data,
		struct zwlr_output_manager_v1 *manager, uint32_t serial) {
	struct randr_state *state = data;
	state->serial = serial;
	state->has_serial = true;
}

static void output_manager_handle_finished(void *data,
		struct zwlr_output_manager_v1 *manager) {
	// This space is intentionally left blank
}

static const struct zwlr_output_manager_v1_listener output_manager_listener = {
	.head = output_manager_handle_head,
	.done = output_manager_handle_done,
	.finished = output_manager_handle_finished,
};

static void registry_handle_global(void *data, struct wl_registry *registry,
		uint32_t name, const char *interface, uint32_t version) {
	struct randr_state *state = data;

	if (strcmp(interface, zwlr_output_manager_v1_interface.name) == 0) {
		uint32_t version_to_bind = version <= 4 ? version : 4;
		state->output_manager = wl_registry_bind(registry, name,
			&zwlr_output_manager_v1_interface, version_to_bind);
		zwlr_output_manager_v1_add_listener(state->output_manager,
			&output_manager_listener, state);
	}
}

static void registry_handle_global_remove(void *data,
		struct wl_registry *registry, uint32_t name) {
	// This space is intentionally left blank
}

static const struct wl_registry_listener registry_listener = {
	.global = registry_handle_global,
	.global_remove = registry_handle_global_remove,
};

static const struct option long_options[] = {
	{"help", no_argument, 0, 'h'},
	{"dryrun", no_argument, 0, 0},
	{"json", no_argument, 0, 0},
	{"output", required_argument, 0, 0},
	{"on", no_argument, 0, 0},
	{"off", no_argument, 0, 0},
	{"toggle", no_argument, 0, 0},
	{"mode", required_argument, 0, 0},
	{"preferred", no_argument, 0, 0},
	{"custom-mode", required_argument, 0, 0},
	{"pos", required_argument, 0, 0},
	{"left-of", required_argument, 0, 0},
	{"right-of", required_argument, 0, 0},
	{"above", required_argument, 0, 0},
	{"below", required_argument, 0, 0},
	{"transform", required_argument, 0, 0},
	{"scale", required_argument, 0, 0},
	{"adaptive-sync", required_argument, 0, 0},
	{0},
};

static bool parse_mode(const char *value, int *width, int *height,
		int *refresh) {
	*refresh = 0;

	// width + "x" + height
	char *cur = (char *)value;
	char *end;
	*width = strtol(cur, &end, 10);
	if (end[0] != 'x' || cur == end) {
		fprintf(stderr, "invalid mode: invalid width: %s\n", value);
		return false;
	}

	cur = end + 1;
	*height = strtol(cur, &end, 10);
	if (cur == end) {
		fprintf(stderr, "invalid mode: invalid height: %s\n", value);
		return false;
	}
	if (end[0] != '\0') {
		// whitespace + "px"
		cur = end;
		while (cur[0] == ' ') {
			cur++;
		}
		if (strncmp(cur, "px", 2) == 0) {
			cur += 2;
		}

		if (cur[0] != '\0') {
			// ("," or "@") + whitespace + refresh
			if (cur[0] == ',' || cur[0] == '@') {
				cur++;
			} else {
				fprintf(stderr, "invalid mode: expected refresh rate: %s\n",
					value);
				return false;
			}
			while (cur[0] == ' ') {
				cur++;
			}
			double refresh_hz = strtod(cur, &end);
			if ((end[0] != '\0' && strcmp(end, "Hz") != 0) ||
					cur == end || refresh_hz <= 0) {
				fprintf(stderr, "invalid mode: invalid refresh rate: %s\n",
					value);
				return false;
			}

			*refresh = (int)(refresh_hz * 1000); // Hz → mHz
		}
	}

	return true;
}

static void fixup_disabled_head(struct randr_head *head) {
	if (!head->mode && head->custom_mode.refresh == 0 &&
			head->custom_mode.width == 0 &&
			head->custom_mode.height == 0) {
		struct randr_mode *mode;
		wl_list_for_each(mode, &head->modes, link) {
			if (mode->preferred) {
				head->mode = mode;
				return;
			}
		}
		/* Pick first element if when there's no preferred mode */
		if (!wl_list_empty(&head->modes)) {
			head->mode = wl_container_of(head->modes.next,
					mode, link);
		}
	}
}

static bool parse_output_arg(struct randr_state *state, struct randr_head *head,
		const char *name, const char *value) {
	if (strcmp(name, "on") == 0) {
		if (!head->enabled) {
			fixup_disabled_head(head);
		}
		head->enabled = true;
	} else if (strcmp(name, "off") == 0) {
		head->enabled = false;
	} else if (strcmp(name, "toggle") == 0) {
		if (head->enabled) {
			head->enabled = false;
		} else {
			fixup_disabled_head(head);
			head->enabled = true;
		}
	} else if (strcmp(name, "mode") == 0) {
		int width, height, refresh;
		if (!parse_mode(value, &width, &height, &refresh)) {
			return false;
		}

		bool found = false;
		struct randr_mode *mode;
		wl_list_for_each(mode, &head->modes, link) {
			if (mode->width == width && mode->height == height &&
					(refresh == 0 || mode->refresh == refresh)) {
				found = true;
				break;
			}
		}

		if (!found) {
			fprintf(stderr, "unknown mode: %s\n", value);
			return false;
		}

		head->changed |= RANDR_HEAD_MODE;
		head->mode = mode;
		head->custom_mode.width = 0;
		head->custom_mode.height = 0;
		head->custom_mode.refresh = 0;
	} else if (strcmp(name, "preferred") == 0) {
		bool found = false;
		struct randr_mode *mode;
		wl_list_for_each(mode, &head->modes, link) {
			if (mode->preferred) {
				found = true;
				break;
			}
		}

		if (!found) {
			fprintf(stderr, "no preferred mode found\n");
			return false;
		}

		head->changed |= RANDR_HEAD_MODE;
		head->mode = mode;
		head->custom_mode.width = 0;
		head->custom_mode.height = 0;
		head->custom_mode.refresh = 0;
	} else if (strcmp(name, "custom-mode") == 0) {
		int width, height, refresh;
		if (!parse_mode(value, &width, &height, &refresh)) {
			return false;
		}

		head->changed |= RANDR_HEAD_MODE;
		head->mode = NULL;
		head->custom_mode.width = width;
		head->custom_mode.height = height;
		head->custom_mode.refresh = refresh;
	} else if (strcmp(name, "pos") == 0) {
		char *cur = (char *)value;
		char *end;
		int x = strtol(cur, &end, 10);
		if (end[0] != ',' || cur == end) {
			fprintf(stderr, "invalid position: %s\n", value);
			return false;
		}

		cur = end + 1;
		int y = strtol(cur, &end, 10);
		if (end[0] != '\0') {
			fprintf(stderr, "invalid position: %s\n", value);
			return false;
		}

		head->changed |= RANDR_HEAD_POSITION;
		head->x = x;
		head->y = y;
	} else if (strcmp(name, "left-of") == 0 ||
			strcmp(name, "right-of") == 0 ||
			strcmp(name, "above") == 0 ||
			strcmp(name, "below") == 0) {
		struct randr_head *relation;
		wl_list_for_each(relation, &state->heads, link) {
			if (strcmp(relation->name, value) != 0) {
				continue;
			}

			if (strcmp(name, "left-of") == 0) {
				head->y = relation->y;
				head->x = relation->x - head_width(head);
			} else if (strcmp(name, "right-of") == 0) {
				head->y = relation->y;
				head->x = relation->x + head_width(relation);
			} else if (strcmp(name, "above") == 0) {
				head->x = relation->x;
				head->y = relation->y - head_height(head);
			} else if (strcmp(name, "below") == 0) {
				head->x = relation->x;
				head->y = relation->y + head_height(relation);
			}

			head->changed |= RANDR_HEAD_POSITION;
			return true;
		}

		fprintf(stderr, "invalid output: %s\n", value);
		return false;
	} else if (strcmp(name, "transform") == 0) {
		bool found = false;
		size_t len =
			sizeof(output_transform_map) / sizeof(output_transform_map[0]);
		for (size_t i = 0; i < len; ++i) {
			if (strcmp(output_transform_map[i], value) == 0) {
				found = true;
				head->transform = i;
				break;
			}
		}

		if (!found) {
			fprintf(stderr, "invalid transform: %s\n", value);
			return false;
		}

		head->changed |= RANDR_HEAD_TRANSFORM;
	} else if (strcmp(name, "scale") == 0) {
		char *end;
		double scale = strtod(value, &end);
		if (end[0] != '\0' || value == end) {
			fprintf(stderr, "invalid scale: %s\n", value);
			return false;
		}

		head->changed |= RANDR_HEAD_SCALE;
		head->scale = scale;
	} else if (strcmp(name, "adaptive-sync") == 0) {
		if (zwlr_output_head_v1_get_version(head->wlr_head) <
				ZWLR_OUTPUT_CONFIGURATION_HEAD_V1_SET_ADAPTIVE_SYNC_SINCE_VERSION) {
			fprintf(stderr, "setting adaptive sync not supported by the compositor\n");
			return false;
		}
		if (strcmp(value, "enabled") == 0) {
			head->adaptive_sync_state = ZWLR_OUTPUT_HEAD_V1_ADAPTIVE_SYNC_STATE_ENABLED;
		} else if (strcmp(value, "disabled") == 0) {
			head->adaptive_sync_state = ZWLR_OUTPUT_HEAD_V1_ADAPTIVE_SYNC_STATE_DISABLED;
		} else {
			fprintf(stderr, "invalid adaptive sync state: %s\n", value);
			return false;
		}
		head->changed |= RANDR_HEAD_ADAPTIVE_SYNC;
	} else {
		fprintf(stderr, "invalid option: %s\n", name);
		return false;
	}

	return true;
}

int main(int argc, char *argv[]) {
	struct randr_state state = { .running = true };
	wl_list_init(&state.heads);

	struct wl_display *display = wl_display_connect(NULL);
	if (display == NULL) {
		fprintf(stderr, "failed to connect to display\n");
		return EXIT_FAILURE;
	}

	struct wl_registry *registry = wl_display_get_registry(display);
	wl_registry_add_listener(registry, &registry_listener, &state);

	if (wl_display_roundtrip(display) < 0) {
		fprintf(stderr, "wl_display_roundtrip failed\n");
		return EXIT_FAILURE;
	}

	if (state.output_manager == NULL) {
		fprintf(stderr, "compositor doesn't support "
			"wlr-output-management-unstable-v1\n");
		return EXIT_FAILURE;
	}

	while (!state.has_serial) {
		if (wl_display_dispatch(display) < 0) {
			fprintf(stderr, "wl_display_dispatch failed\n");
			return EXIT_FAILURE;
		}
	}

	bool changed = false;
	struct randr_head *current_head = NULL;

        print_state(&state);

        {
            struct randr_head *head;
            wl_list_for_each(head, &state.heads, link) {
                    head->changed = true;
                    if (head->enabled) {
                        printf("Will shutdown \"%s\"\n", head->name);
                        head->enabled = false;
                    } else {
                        printf("Will POWON \"%s\"\n", head->name);
                        fixup_disabled_head(head);
                        head->enabled = true;
                    }
            }
        }

        apply_state(&state, false);
        print_state(&state);
	//state.running = false;

	while (state.running && wl_display_dispatch(display) != -1) {
		// This space intentionally left blank
	}

	struct randr_head *head, *tmp_head;
	wl_list_for_each_safe(head, tmp_head, &state.heads, link) {
		struct randr_mode *mode, *tmp_mode;
		wl_list_for_each_safe(mode, tmp_mode, &head->modes, link) {
			zwlr_output_mode_v1_destroy(mode->wlr_mode);
			free(mode);
		}
		zwlr_output_head_v1_destroy(head->wlr_head);
		free(head->name);
		free(head->description);
		free(head->make);
		free(head->model);
		free(head->serial_number);
		free(head);
	}
	zwlr_output_manager_v1_destroy(state.output_manager);
	wl_registry_destroy(registry);
	wl_display_disconnect(display);

	return state.failed ? EXIT_FAILURE : EXIT_SUCCESS;
}
