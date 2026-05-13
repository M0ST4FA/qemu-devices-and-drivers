#include <bits/time.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <syscall.h>
#include <unistd.h>
#include <wayland-client-core.h>
#include <wayland-client-protocol.h>
#include <wayland-client.h>

#include "buffer.h"
#include "logger.h"
#include "render.h"
#include "wayland-internal.h"
#include "wayland.h"
#include "xdg-shell-client-protocol.h"

// LISTENERS ================================================
static const struct wl_registry_listener registry_listener = {
	.global = registry_global_handler,
	.global_remove = registry_global_remove_handler,
};

static const struct xdg_wm_base_listener xdg_wm_base_listener = {
	.ping = xdg_wm_base_ping_handler,
};

const struct xdg_toplevel_listener xdg_toplevel_listener = {
	.configure = xdg_toplevel_configure_handler,
	.configure_bounds = xdg_toplevel_configure_bounds_handler,

	.close = xdg_toplevel_close_handler,
	.wm_capabilities = xdg_toplevel_wm_capabilities_handler,
};

static const struct xdg_surface_listener xdg_surface_listener = {
	.configure = xdg_surface_configure_handler,
};

static const struct wl_callback_listener wl_frame_callback_listener = {
	.done = wl_callback_done_handler,
};

static const struct wl_pointer_listener wl_pointer_listener = {
	.enter = wl_pointer_enter_handler,
	.leave = wl_pointer_leave_handler,
	.motion = wl_pointer_motion_handler,
	.button = wl_pointer_button_handler,
	.axis = wl_pointer_axis_handler,
};

// HANDLERS ====================================================
void xdg_wm_base_ping_handler([[maybe_unused]] void *data,
							  struct xdg_wm_base *xdg_wm_base,
							  uint32_t serial) {
	xdg_wm_base_pong(xdg_wm_base, serial);
	pr_log("debug", "Received a ping and responded with a pong");
}

void registry_global_handler(void *data, struct wl_registry *registry,
							 uint32_t name, const char *interface,
							 uint32_t version) {
	struct wayland_client *state = data;

	if (strcmp(interface, wl_compositor_interface.name) == 0) {
		state->compositor = wl_registry_bind(registry, name,
											 &wl_compositor_interface,
											 MIN(version, 6));
	} else if (strcmp(interface, wl_shm_interface.name) == 0) {
		state->shm = wl_registry_bind(registry, name, &wl_shm_interface,
									  MIN(version, 2));
	} else if (strcmp(interface, xdg_wm_base_interface.name) == 0) {
		state->xdg_wm_base = wl_registry_bind(registry, name,
											  &xdg_wm_base_interface,
											  MIN(version, 7));
		if (state->xdg_wm_base != NULL)
			xdg_wm_base_add_listener(state->xdg_wm_base,
									 &xdg_wm_base_listener, state);
	} else if (strcmp(interface, wl_seat_interface.name) == 0) {
		state->wl_seat = wl_registry_bind(registry, name,
										  &wl_seat_interface,
										  MIN(version, 9));
		if (state->wl_seat != NULL) {
			state->wl_pointer = wl_seat_get_pointer(state->wl_seat);
			// if (state->wl_pointer != NULL)
			// 	wl_pointer_add_listener(state->wl_pointer, &wl_pointer_listener, state);
		}
	}
}

void registry_global_remove_handler([[maybe_unused]] void *data,
									[[maybe_unused]] struct wl_registry *registry,
									uint32_t name) {
	pr_log("debug", "Removed interface: name: %u", name);
}

void xdg_toplevel_configure_handler([[maybe_unused]] void *data, struct xdg_toplevel *xdg_toplevel,
									int32_t width, int32_t height, struct wl_array *states) {
	struct wayland_client *client_state = data;
	struct window *window = &client_state->window;
	struct dimensions current_dim = window->current_dim;

	pr_log("debug", "Configure toplevel %dx%d", width, height);

	if (width == 0 && height == 0) {
		client_state->window.pending_dim.width = 200;
		client_state->window.pending_dim.height = 200;
		client_state->window.flags = WIN_INITIALIZED | WIN_PENDING_RESIZE;
		return;
	}

	if (width == current_dim.width && height == current_dim.height)
		return;

	window->pending_dim.width = width;
	window->pending_dim.height = height;
	window->flags |= WIN_PENDING_RESIZE;
	pr_log("debug", "Changed dimensions from (%dx%d) to (%dx%d)",
		   current_dim.width, current_dim.height,
		   window->pending_dim.width, window->pending_dim.height);
}

void xdg_toplevel_configure_bounds_handler([[maybe_unused]] void *data, struct xdg_toplevel *xdg_toplevel,
										   int32_t width, int32_t height) {
	struct wayland_client *state = data;
	pr_log("debug", "Configure toplevel bounds %dx%d", width, height);
}

void xdg_toplevel_close_handler([[maybe_unused]] void *data, struct xdg_toplevel *xdg_toplevel) {
	struct wayland_client *state = data;

	pr_log("debug", "Closing toplevel");
	render_buffer_destroy(&state->render_buffer);
	wl_surface_destroy(state->wl_surface);
	state->wl_surface = NULL;
	exit(EXIT_SUCCESS);
}

void xdg_toplevel_wm_capabilities_handler(void *data, struct xdg_toplevel *xdg_toplevel, struct wl_array *) {
	pr_log("debug", "Server is communicating its capabilites");
}

void xdg_surface_configure_handler([[maybe_unused]] void *data, struct xdg_surface *surface, uint32_t serial) {
	struct wayland_client *state = data;
	xdg_surface_ack_configure(surface, serial);
	pr_log("debug", "Configure surface");

	wayland_client_redraw(state);
}

void wl_callback_done_handler([[maybe_unused]] void *data, struct wl_callback *wl_callback, uint32_t time) {
	struct wayland_client *client_state = data;

	wl_callback_destroy(wl_callback);
	client_state->frame_callback = NULL;

	wayland_client_redraw(client_state);
};

void wl_pointer_enter_handler([[maybe_unused]] void *data, struct wl_pointer *pointer,
							  uint32_t serial, struct wl_surface *surface,
							  wl_fixed_t x, wl_fixed_t y) {

	wl_pointer_set_cursor(pointer, serial, surface, x, y);
};

void wl_pointer_leave_handler([[maybe_unused]] void *data, struct wl_pointer *pointer,
							  uint32_t serial, struct wl_surface *surface) {

};

void wl_pointer_motion_handler([[maybe_unused]] void *data, struct wl_pointer *pointer,
							   uint32_t time,
							   wl_fixed_t x, wl_fixed_t y) {

};
void wl_pointer_button_handler(
	void *data,
	struct wl_pointer *pointer,
	uint32_t serial,
	uint32_t time,
	uint32_t button,
	uint32_t state) {}

void wl_pointer_axis_handler(
	void *data,
	struct wl_pointer *pointer,
	uint32_t time,
	uint32_t axis,
	wl_fixed_t value) {}

int wayland_client_init(struct wayland_client *client_state) {
	int ret = -1;

	// 1. Create display and registry (to query for global objects)
	client_state->display = wl_display_connect(NULL);
	if (client_state->display == NULL) {
		pr_log("error", "Couldn't connect to Wayland display");
		goto cleanup;
	}

	pr_log("debug", "Connected to Wayland display");

	client_state->registry = wl_display_get_registry(client_state->display);
	if (client_state->registry == NULL) {
		pr_log("error", "Couldn't get Wayland registry");
		goto cleanup;
	}

	wl_registry_add_listener(client_state->registry, &registry_listener, client_state);

	// 2. Wait until event queue drains (all events the server has for the client at this moment are sent and dispatched).
	if (wl_display_roundtrip(client_state->display) < 0) {
		pr_log("error", "Wayland registry roundtrip failed");
		goto cleanup;
	}

	if (have_required_globals(client_state)) {
		pr_log("debug", "Got all required global objects from server");
	} else {
		pr_log("error", "Some required global objects are not available");
		goto cleanup;
	}

	// 3. Create the surface (hierarchy)
	client_state->wl_surface = wl_compositor_create_surface(client_state->compositor);
	client_state->xdg_surface = xdg_wm_base_get_xdg_surface(client_state->xdg_wm_base, client_state->wl_surface);
	xdg_surface_add_listener(client_state->xdg_surface, &xdg_surface_listener, client_state);
	client_state->xdg_toplevel = xdg_surface_get_toplevel(client_state->xdg_surface);
	xdg_toplevel_add_listener(client_state->xdg_toplevel, &xdg_toplevel_listener, client_state);
	xdg_toplevel_set_title(client_state->xdg_toplevel, "LED");

	// 4. Initialize the window and the buffer
	client_state->window.pending_dim.width = 200;
	client_state->window.pending_dim.height = 200;
	client_state->window.flags = WIN_INITIALIZED | WIN_PENDING_RESIZE;

	render_buffer_init(&client_state->render_buffer);

	// 5. Signal the surface is ready to be configured
	wl_surface_commit(client_state->wl_surface);
	wl_display_roundtrip(client_state->display);

	return 0;

cleanup:
	wayland_client_destroy(client_state);
	return ret;
};

void wayland_client_redraw(struct wayland_client *client_state) {
	int buf_idx = -1;
	struct window *window = &client_state->window;
	struct render_buffer *render_buffer = &client_state->render_buffer;

	if (!(window->flags & WIN_INITIALIZED)) {
		pr_log("error", "Trying to render before initializing window");
		return;
	}

	// 1. Resize buffer if needed
	if (window->flags & WIN_PENDING_RESIZE) {
		pr_log("debug", "Resizing window");
		window->current_dim = window->pending_dim;
		window->flags &= ~WIN_PENDING_RESIZE;

		if (render_buffer_resize(render_buffer, client_state->shm, window->current_dim) < 0) {
			pr_log("error", "Failed to resize render buffer");
			return;
		}
	}

	// 2. Render
	buf_idx = render_draw(&client_state->render_buffer);
	if (buf_idx < 0) {
		pr_log("error", "Draw failed");
		return; // Skip committing buffer
	}

	// 3. Request frame callback for next frame
	client_state->frame_callback = wl_surface_frame(client_state->wl_surface);
	wl_callback_add_listener(client_state->frame_callback, &wl_frame_callback_listener, client_state);

	// 4. Commit current frame
	render_buffer->buffer_busy[buf_idx] = 1;
	wl_surface_attach(client_state->wl_surface, render_buffer->wl_buffers[buf_idx], 0, 0);
	wl_surface_damage_buffer(client_state->wl_surface, 0, 0,
							 window->current_dim.width, window->current_dim.height);
	wl_surface_commit(client_state->wl_surface);
}
