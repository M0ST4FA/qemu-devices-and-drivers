#include <linux/input-event-codes.h>
#include <stdio.h>
#include <stdlib.h>
#include <wayland-client-protocol.h>
#include <wayland-util.h>

#include "buffer.h"
#include "logger.h"
#include "protocol.h"
#include "wayland-internal.h"
#include "wayland.h"

static const struct wl_pointer_listener wl_pointer_listener = {
	.enter = wl_pointer_enter_handler,
	.leave = wl_pointer_leave_handler,
	.motion = wl_pointer_motion_handler,

	.button = wl_pointer_button_handler,

	.axis = wl_pointer_axis_handler,
	.axis_relative_direction = wl_pointer_axis_relative_direction_handler,
	.axis_source = wl_pointer_axis_source_handler,
	.axis_value120 = wl_pointer_axis_value120_handler,
	.axis_stop = wl_pointer_axis_stop_handler,

	.frame = wl_pointer_frame_handler,
};

static const struct wl_keyboard_listener wl_keyboard_listener = {
	.enter = wl_keyboard_enter_handler,
	.leave = wl_keyboard_leave_handler,
	.key = wl_keyboard_key_handler,
	.keymap = wl_keyboard_keymap_handler,
	.modifiers = wl_keyboard_modifiers_handler,
	.repeat_info = wl_keyboard_repeat_info_handler,
};

// SEAT
void wl_seat_name_handler([[maybe_unused]] void *data, [[maybe_unused]] struct wl_seat *wl_seat, const char *name) {
	pr_log("debug", "Seat name: %s", name);
};
void wl_seat_capabilities_hander([[maybe_unused]] void *data, struct wl_seat *wl_seat, uint32_t capabilities) {
	struct wayland_client *client = data;
	pr_log("debug", "Seat capabilities:");

	// POINTER
	if (capabilities & WL_SEAT_CAPABILITY_POINTER) {
		pr_log("debug", "\tPOINTER");

		if (client->wl_pointer == NULL) {
			client->wl_pointer = wl_seat_get_pointer(wl_seat);
			wl_pointer_add_listener(client->wl_pointer, &wl_pointer_listener, client);
		}
	} else if (client->wl_pointer != NULL) {
		pr_log("debug", "Previous POINTER removed from seat");
		wl_pointer_destroy(client->wl_pointer);
		client->wl_pointer = NULL;
	}

	// KEYBOARD
	if (capabilities & WL_SEAT_CAPABILITY_KEYBOARD) {
		pr_log("debug", "\tKEYBOARD");

		if (client->wl_keyboard == NULL) {
			client->wl_keyboard = wl_seat_get_keyboard(wl_seat);
			wl_keyboard_add_listener(client->wl_keyboard, &wl_keyboard_listener, client);
		}
	} else if (client->wl_keyboard != NULL) {
		pr_log("debug", "Previous KEYBOARD removed from seat");
		wl_keyboard_destroy(client->wl_keyboard);
		client->wl_keyboard = NULL;
	}

	// TOUCH
	if (capabilities & WL_SEAT_CAPABILITY_TOUCH) {
		pr_log("debug", "\tTOUCH");
		if (client->wl_touch == NULL) {
			client->wl_touch = wl_seat_get_touch(wl_seat);
		}
	} else if (client->wl_touch != NULL) {
		pr_log("debug", "Previous TOUCH removed from seat");
		wl_touch_destroy(client->wl_touch);
		client->wl_touch = NULL;
	}
};

// POINTER
void wl_pointer_enter_handler([[maybe_unused]] void *data, [[maybe_unused]] struct wl_pointer *pointer,
							  uint32_t serial, [[maybe_unused]] struct wl_surface *surface,
							  wl_fixed_t x, wl_fixed_t y) {
	struct wayland_client *client = data;
	struct pointer_event *event = &client->pointer_event;

	event->event_mask |= POINTER_EVENT_ENTER;
	event->serial = serial;
	event->surface_x = x;
	event->surface_y = y;
};

void wl_pointer_leave_handler([[maybe_unused]] void *data, [[maybe_unused]] struct wl_pointer *pointer,
							  uint32_t serial, [[maybe_unused]] struct wl_surface *surface) {
	struct wayland_client *client = data;
	struct pointer_event *event = &client->pointer_event;

	event->event_mask |= POINTER_EVENT_LEAVE;
	event->serial = serial;
};

void wl_pointer_motion_handler([[maybe_unused]] void *data, [[maybe_unused]] struct wl_pointer *pointer,
							   uint32_t time, wl_fixed_t x, wl_fixed_t y) {

	struct wayland_client *client = data;
	struct pointer_event *event = &client->pointer_event;

	event->event_mask |= POINTER_EVENT_MOTION;
	event->time = time;
	event->surface_x = x;
	event->surface_y = y;
};
void wl_pointer_button_handler(void *data, [[maybe_unused]] struct wl_pointer *pointer,
							   [[maybe_unused]] uint32_t serial, [[maybe_unused]] uint32_t time,
							   uint32_t button, uint32_t state) {
	struct wayland_client *client = data;
	struct pointer_event *event = &client->pointer_event;

	event->event_mask |= POINTER_EVENT_BUTTON;
	event->serial = serial;
	event->time = time;

	event->button = button;
	event->state = state;
}

void wl_pointer_axis_handler(void *data, [[maybe_unused]] struct wl_pointer *pointer,
							 uint32_t time, uint32_t axis, wl_fixed_t value) {
	struct wayland_client *client = data;
	struct pointer_event *event = &client->pointer_event;

	event->event_mask |= POINTER_EVENT_AXIS;
	event->time = time;

	event->axes[axis].valid = 1;
	event->axes[axis].value = value;
}

void wl_pointer_axis_source_handler(void *data, [[maybe_unused]] struct wl_pointer *wl_pointer,
									uint32_t axis_source) {

	struct wayland_client *client = data;
	struct pointer_event *event = &client->pointer_event;

	event->event_mask |= POINTER_EVENT_AXIS_SOURCE;
	event->axis_source = axis_source;
}

void wl_pointer_axis_discrete_handler(void *data, struct wl_pointer *wl_pointer, uint32_t axis, int32_t discrete) {
	struct wayland_client *client = data;
	struct pointer_event *event = &client->pointer_event;

	event->event_mask |= POINTER_EVENT_AXIS_DISCRETE;
	event->axes[axis].valid = 1;
	event->axes[axis].discrete = discrete;
}

void wl_pointer_axis_stop_handler(void *data, struct wl_pointer *wl_pointer, uint32_t time, uint32_t axis) {
	struct wayland_client *client = data;
	struct pointer_event *event = &client->pointer_event;

	event->event_mask |= POINTER_EVENT_AXIS_STOP;
	event->time = time;
	event->axes[axis].valid = 1;
};

void wl_pointer_axis_value120_handler(void *data, struct wl_pointer *wl_pointer, uint32_t axis, int32_t value120) {
	struct wayland_client *client = data;
	struct pointer_event *event = &client->pointer_event;
}

void wl_pointer_axis_relative_direction_handler(void *data, [[maybe_unused]] struct wl_pointer *wl_pointer,
												uint32_t axis, uint32_t direction) {

	struct wayland_client *client = data;
	struct pointer_event *event = &client->pointer_event;
}

static inline void print_frame_event(struct pointer_event *event) {
	pr_log("debug", "pointer frame @ %d: ", event->time);

	char *axis_name[2] = {
		[WL_POINTER_AXIS_VERTICAL_SCROLL] = "vertical",
		[WL_POINTER_AXIS_HORIZONTAL_SCROLL] = "horizontal",
	};
	char *axis_source[4] = {
		[WL_POINTER_AXIS_SOURCE_WHEEL] = "wheel",
		[WL_POINTER_AXIS_SOURCE_FINGER] = "finger",
		[WL_POINTER_AXIS_SOURCE_CONTINUOUS] = "continuous",
		[WL_POINTER_AXIS_SOURCE_WHEEL_TILT] = "wheel tilt",
	};

	if (event->event_mask & POINTER_EVENT_ENTER) {
		pr_log("debug", "\tENTER %f, %f",
			   wl_fixed_to_double(event->surface_x),
			   wl_fixed_to_double(event->surface_y));
	}

	if (event->event_mask & POINTER_EVENT_LEAVE) {
		pr_log("debug", "\tLEAVE");
	}

	if (event->event_mask & POINTER_EVENT_MOTION) {
		pr_log("debug", "\tMOTION %f, %f",
			   wl_fixed_to_double(event->surface_x),
			   wl_fixed_to_double(event->surface_y));
	}

	if (event->event_mask & POINTER_EVENT_BUTTON) {
		pr_log("debug", "\tBUTTON %d %s",
			   event->button,
			   event->state == WL_POINTER_BUTTON_STATE_PRESSED ? "pressed" : "released");
	}

	if (event->event_mask & (POINTER_EVENT_AXIS | POINTER_EVENT_AXIS_SOURCE | POINTER_EVENT_AXIS_STOP | POINTER_EVENT_AXIS_DISCRETE)) {
		for (size_t i = 0; i < 2; i++) {
			if (!event->axes[i].valid)
				continue;

			if (event->event_mask & POINTER_EVENT_AXIS)
				pr_log("debug", "\tAXIS %s value %f",
					   axis_name[i],
					   wl_fixed_to_double(event->axes[i].value));

			if (event->event_mask & POINTER_EVENT_AXIS_DISCRETE)
				pr_log("debug", "\tAXIS %s discrete %f",
					   axis_name[i],
					   wl_fixed_to_double(event->axes[i].discrete));

			if (event->event_mask & POINTER_EVENT_AXIS_STOP)
				pr_log("debug", "\tAXIS %s stopped", axis_name[i]);

			if (event->event_mask & POINTER_EVENT_AXIS_SOURCE)
				pr_log("debug", "\tAXIS %s via %s",
					   axis_name[i],
					   axis_source[event->axis_source]);
		}
	}
}

static inline int get_underlying_led(struct wayland_client *client) {
	struct pointer_event *event = &client->pointer_event;

	if (!(event->event_mask & POINTER_EVENT_BUTTON))
		return -1;

	// 1. Calculate the column and row of the click
	int32_t width = client->window.current_dim.width;
	int32_t height = client->window.current_dim.height;

	int32_t radius, spacing_x, spacing_y, col, row;
	int32_t x = wl_fixed_to_int(event->surface_x),
			y = wl_fixed_to_int(event->surface_y);

	radius = width / (LED_COLS * 2);
	spacing_x = width / LED_COLS;
	spacing_y = height / LED_ROWS;

	col = x / spacing_x;
	row = y / spacing_y;

	// 2. Determine whether we're inside that led
	int32_t center_x, center_y, distance_x, distance_y, distance;
	center_x = spacing_x * col + spacing_x / 2;
	center_y = spacing_y * row + spacing_y / 2;

	distance_x = (x - center_x);
	distance_y = (y - center_y);
	distance = (distance_x * distance_x) + (distance_y * distance_y);

	if (distance > radius * radius)
		return -1;

	int index = LED_COLS * row + col;

	if (index >= LED_NR)
		return -1;

	return index;
}

void wl_pointer_frame_handler(void *data, [[maybe_unused]] struct wl_pointer *wl_pointer) {
	struct wayland_client *client = data;
	struct pointer_event *event = &client->pointer_event;

	// Use this for debugging when needed
	// print_frame_event(event);

	if (client->client_fd < 0)
		return;

	if (!(event->event_mask & POINTER_EVENT_BUTTON))
		return;

	// Let's respond on PRESSED (1) instead of RELEASED (0) for a snappier feel!
	if (event->state == WL_POINTER_BUTTON_STATE_RELEASED)
		return;

	if (event->button == BTN_RIGHT) {
		pr_log("debug", "Received right click");
		int index = get_underlying_led(client);

		struct led_command cmd;
		cmd.cmd = CMD_SET_COLOR;
		cmd.led_id = index;
		cmd.data.color[0] = rand() % 255;
		cmd.data.color[1] = rand() % 255;
		cmd.data.color[2] = rand() % 255;
		cmd.data.color[3] = 255;

		if (index < 0)
			pr_log("debug", "No LED under pointer");
		else
			write(client->client_fd, &cmd, sizeof(cmd));
	}

	if (event->button == BTN_LEFT) {
		pr_log("debug", "Received left click");
		int index = get_underlying_led(client);

		struct led_command cmd;
		cmd.cmd = CMD_TOGGLE;
		cmd.led_id = index;

		if (index < 0)
			pr_log("debug", "No LED under pointer");

		else
			write(client->client_fd, &cmd, sizeof(cmd));
	}
}

// KEYBOARD
void wl_keyboard_enter_handler(void *data, struct wl_keyboard *keyboard,
							   uint32_t serial, struct wl_surface *surface, struct wl_array *keys) {

};

void wl_keyboard_leave_handler(void *data, struct wl_keyboard *keyboard,
							   uint32_t serial, struct wl_surface *surface) {

};

void wl_keyboard_key_handler(void *data, struct wl_keyboard *keyboard,
							 uint32_t serial, uint32_t time, uint32_t key, uint32_t keystate) {
	struct wayland_client *client = data;

	if (client->client_fd < 0)
		return;

	if (keystate != WL_KEYBOARD_KEY_STATE_PRESSED)
		return;

	struct led_command cmd = {0};

	switch (key) {
		case KEY_SPACE:
			cmd.cmd = CMD_SET_COLOR;
			cmd.data.color[0] = time % 255;
			cmd.data.color[1] = time % 255;
			cmd.data.color[2] = time % 255;
			cmd.data.color[3] = 255;
			break;

		case KEY_UP:
			[[fallthrough]];
		case KEY_K:
			break;

		case KEY_DOWN:
			[[fallthrough]];
		case KEY_J:
			break;

		case KEY_LEFT:
			[[fallthrough]];
		case KEY_L:
			break;

		case KEY_RIGHT:
			[[fallthrough]];
		case KEY_H:
			break;
	}

	write(client->client_fd, &cmd, sizeof(cmd));
};

void wl_keyboard_keymap_handler(void *data, struct wl_keyboard *keyboard, uint32_t format, int fd, uint32_t size) {

};

void wl_keyboard_modifiers_handler(void *data, struct wl_keyboard *keyboard,
								   uint32_t serial, uint32_t mods_depressed, uint32_t mods_latched, uint32_t mods_locked, uint32_t group) {

};

void wl_keyboard_repeat_info_handler(void *data, struct wl_keyboard *keyboard,
									 int32_t rate, int32_t delay) {

};
