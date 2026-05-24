#pragma once

#include <signal.h>
#include <stdint.h>
#include <unistd.h>
#include <wayland-util.h>

#include "buffer.h"
#include "protocol.h"
#include "wayland-client-protocol.h"
#include "xdg-shell-client-protocol.h"

#define LED_NR 64
#define LED_COLS 8
#define LED_ROWS 8

enum window_flags {
	WIN_INITIALIZED = (1 << 0),
	WIN_PENDING_RESIZE = (1 << 1),
	WIN_CLOSED = (1 << 2),
};

struct window {
	struct dimensions current_dim;
	struct dimensions pending_dim;
	enum window_flags flags;
};

enum pointer_event_mask : uint32_t {
	POINTER_EVENT_ENTER = (1 << 0),
	POINTER_EVENT_LEAVE = (1 << 1),
	POINTER_EVENT_MOTION = (1 << 2),
	POINTER_EVENT_BUTTON = (1 << 3),
	POINTER_EVENT_AXIS = (1 << 4),
	POINTER_EVENT_AXIS_SOURCE = (1 << 5),
	POINTER_EVENT_AXIS_STOP = (1 << 6),
	POINTER_EVENT_AXIS_DISCRETE = (1 << 7),
};

struct pointer_event {
	enum pointer_event_mask event_mask;
	wl_fixed_t surface_x, surface_y;
	uint32_t button, state;
	uint32_t time;
	uint32_t serial;
	struct {
		bool valid;
		wl_fixed_t value;
		int32_t discrete;
	} axes[2];
	uint32_t axis_source;
};

struct led {
	int on;
	uint8_t color[4];
};

struct led_grid {
	struct led leds[LED_NR];
	uint8_t off_color[4];
};

struct wayland_client {
	/* Globals */
	struct wl_display *display;
	struct wl_registry *registry;
	struct wl_compositor *compositor;
	struct wl_shm *shm;
	struct xdg_wm_base *xdg_wm_base;
	struct wl_seat *wl_seat;

	/* Objects */
	struct wl_surface *wl_surface;
	struct xdg_surface *xdg_surface;
	struct xdg_toplevel *xdg_toplevel;
	struct wl_callback *frame_callback;

	struct wl_pointer *wl_pointer;
	struct wl_keyboard *wl_keyboard;
	struct wl_touch *wl_touch;

	// Others
	struct window window;
	struct pointer_event pointer_event; // Last cached pointer event
	struct render_buffer render_buffer;
	struct led_grid led_grid;
	int client_fd;
	bool shutting_down;
};

extern volatile sig_atomic_t paused;

int wayland_client_init(struct wayland_client *state);

void wayland_client_redraw(struct wayland_client *client_state);

int wayland_client_run_loop(struct wayland_client *client, struct protocol_state *protocol_state);

static inline void wayland_client_destroy(struct wayland_client *client) {
	render_buffer_destroy(&client->render_buffer);

	if (client->wl_seat != NULL) {
		wl_seat_destroy(client->wl_seat);
		client->wl_seat = NULL;
	}

	if (client->client_fd > STDERR_FILENO) {
		close(client->client_fd);
		client->client_fd = -1;
	}

	if (client->xdg_toplevel != NULL) {
		xdg_toplevel_destroy(client->xdg_toplevel);
		client->xdg_toplevel = NULL;
	}
	if (client->xdg_surface != NULL) {
		xdg_surface_destroy(client->xdg_surface);
		client->xdg_surface = NULL;
	}
	if (client->wl_surface != NULL) {
		wl_surface_destroy(client->wl_surface);
		client->wl_surface = NULL;
	}
	if (client->xdg_wm_base != NULL) {
		xdg_wm_base_destroy(client->xdg_wm_base);
		client->xdg_wm_base = NULL;
	}
	if (client->shm != NULL) {
		wl_shm_destroy(client->shm);
		client->shm = NULL;
	}
	if (client->compositor != NULL) {
		wl_compositor_destroy(client->compositor);
		client->compositor = NULL;
	}
	if (client->registry != NULL) {
		wl_registry_destroy(client->registry);
		client->registry = NULL;
	}
	if (client->display != NULL) {
		wl_display_disconnect(client->display);
		client->display = NULL;
	}
}
