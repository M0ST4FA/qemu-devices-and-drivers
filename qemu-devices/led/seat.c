#include <linux/input-event-codes.h>
#include <wayland-client-protocol.h>

#include "logger.h"
#include "protocol.h"
#include "wayland-internal.h"

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
void wl_seat_name_handler([[maybe_unused]] void *data, struct wl_seat *wl_seat, const char *name) {
	pr_log("debug", "Seat name: %s", name);
};
void wl_seat_capabilities_hander([[maybe_unused]] void *data, struct wl_seat *wl_seat, uint32_t capabilities) {
	struct wayland_client *client = data;
	pr_log("debug", "Seat capabilities:");

	if (capabilities & WL_SEAT_CAPABILITY_POINTER) {
		pr_log("debug", "\tPOINTER");
		client->wl_pointer = wl_seat_get_pointer(wl_seat);
		wl_pointer_add_listener(client->wl_pointer, &wl_pointer_listener, client);
	}
	if (capabilities & WL_SEAT_CAPABILITY_KEYBOARD) {
		pr_log("debug", "\tKEYBOARD");
		client->wl_keyboard = wl_seat_get_keyboard(wl_seat);
		wl_keyboard_add_listener(client->wl_keyboard, &wl_keyboard_listener, client);
	}
	if (capabilities & WL_SEAT_CAPABILITY_TOUCH)
		pr_log("debug", "\tTOUCH");
};

// POINTER
void wl_pointer_enter_handler([[maybe_unused]] void *data, struct wl_pointer *pointer,
							  uint32_t serial, struct wl_surface *surface,
							  wl_fixed_t x, wl_fixed_t y) {

};

void wl_pointer_leave_handler([[maybe_unused]] void *data, struct wl_pointer *pointer,
							  uint32_t serial, struct wl_surface *surface) {

};

void wl_pointer_motion_handler([[maybe_unused]] void *data, struct wl_pointer *pointer,
							   uint32_t time,
							   wl_fixed_t x, wl_fixed_t y) {

};
void wl_pointer_button_handler(void *data, struct wl_pointer *pointer,
							   uint32_t serial, uint32_t time,
							   uint32_t button, uint32_t state) {
	struct wayland_client *client = data;

	if (client->client_fd < 0)
		return;

	// Log every raw button code so we can see what the touchpad is doing
	pr_log("debug", "Raw button event: code=%u, state=%u", button, state);

	// Let's respond on PRESSED (1) instead of RELEASED (0) for a snappier feel!
	if (state == WL_POINTER_BUTTON_STATE_RELEASED)
		return;

	if (button == BTN_RIGHT) {
		pr_log("debug", "Received right click");
	}

	if (button == BTN_LEFT) {
		pr_log("debug", "Received left click");
	}
}

void wl_pointer_axis_handler(
	void *data,
	struct wl_pointer *pointer,
	uint32_t time,
	uint32_t axis,
	wl_fixed_t value) {}

void wl_pointer_axis_source_handler(
	void *data,
	struct wl_pointer *wl_pointer,
	uint32_t axis_source) {}

void wl_pointer_axis_relative_direction_handler(
	void *data,
	struct wl_pointer *wl_pointer,
	uint32_t axis,
	uint32_t direction) {}

void wl_pointer_frame_handler(void *data, struct wl_pointer *wl_pointer) {}

void wl_pointer_axis_stop_handler(void *data, struct wl_pointer *wl_pointer, uint32_t time, uint32_t axis) {}

void wl_pointer_axis_discrete_handler(void *data, struct wl_pointer *wl_pointer, uint32_t axis, int32_t discrete) {}

void wl_pointer_axis_value120_handler(void *data, struct wl_pointer *wl_pointer, uint32_t axis, int32_t value120) {}
void wl_keyboard_enter_handler(void *data, struct wl_keyboard *keyboard,
							   uint32_t serial, struct wl_surface *surface, struct wl_array *keys) {

};

// KEYBOARD
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
