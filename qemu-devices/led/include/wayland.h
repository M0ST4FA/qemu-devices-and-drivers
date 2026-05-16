#pragma once

#include <signal.h>
#include <stdint.h>
#include <unistd.h>

#include "buffer.h"
#include "protocol.h"
#include "wayland-client-protocol.h"
#include "xdg-shell-client-protocol.h"

#define LED_NR 24
#define LED_COLS 8
#define LED_ROWS 3

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

	// Others
	struct window window;
	struct render_buffer render_buffer;
	struct led_grid led_grid;
	int client_fd;
};

extern volatile sig_atomic_t paused;

int wayland_client_init(struct wayland_client *state);

void wayland_client_redraw(struct wayland_client *client_state);

int wayland_client_run_loop(struct wayland_client *client_state, struct protocol_state *protocol_state);

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
