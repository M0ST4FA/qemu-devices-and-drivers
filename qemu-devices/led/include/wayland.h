#pragma once

#include <stdint.h>

#include "buffer.h"
#include "wayland-client-protocol.h"
#include "xdg-shell-client-protocol.h"

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

struct wayland_client {
	/* Globals */
	struct wl_display *display;
	struct wl_registry *registry;
	struct wl_compositor *compositor;
	struct wl_shm *shm;
	struct xdg_wm_base *xdg_wm_base;
	struct wl_seat *wl_seat;
	struct wl_pointer *wl_pointer;

	/* Objects */
	struct wl_surface *wl_surface;
	struct xdg_surface *xdg_surface;
	struct xdg_toplevel *xdg_toplevel;
	struct wl_callback *frame_callback;

	// Others
	struct window window;
	struct render_buffer render_buffer;
};

int wayland_client_init(struct wayland_client *state);

void wayland_client_redraw(struct wayland_client *client_state);

static inline int wayland_client_run_loop(struct wayland_client *client_state) {
	int ret = 0;

	while (1) {
		ret = wl_display_dispatch(client_state->display);
	}

	return ret;
}

static inline void wayland_client_destroy(struct wayland_client *client_state) {
	render_buffer_destroy(&client_state->render_buffer);

	if (client_state->xdg_toplevel != NULL)
		xdg_toplevel_destroy(client_state->xdg_toplevel);
	if (client_state->xdg_surface != NULL)
		xdg_surface_destroy(client_state->xdg_surface);
	if (client_state->wl_surface != NULL)
		wl_surface_destroy(client_state->wl_surface);
	if (client_state->xdg_wm_base != NULL)
		xdg_wm_base_destroy(client_state->xdg_wm_base);
	if (client_state->shm != NULL)
		wl_shm_destroy(client_state->shm);
	if (client_state->compositor != NULL)
		wl_compositor_destroy(client_state->compositor);
	if (client_state->registry != NULL)
		wl_registry_destroy(client_state->registry);
	if (client_state->display != NULL)
		wl_display_disconnect(client_state->display);
}
