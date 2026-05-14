#include <bits/time.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <syscall.h>
#include <unistd.h>
#include <wayland-client-core.h>
#include <wayland-client-protocol.h>
#include <wayland-client.h>
#include <wayland-util.h>

#include "buffer.h"
#include "logger.h"
#include "protocol.h"
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
		client_state->window.pending_dim.width = 600;
		client_state->window.pending_dim.height = 400;
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

int wayland_client_init(struct wayland_client *client) {
	int ret = -1;

	// 1. Create display and registry (to query for global objects)
	client->display = wl_display_connect(NULL);
	if (client->display == NULL) {
		pr_log("error", "Couldn't connect to Wayland display");
		goto cleanup;
	}

	pr_log("debug", "Connected to Wayland display");

	client->registry = wl_display_get_registry(client->display);
	if (client->registry == NULL) {
		pr_log("error", "Couldn't get Wayland registry");
		goto cleanup;
	}

	wl_registry_add_listener(client->registry, &registry_listener, client);

	// 2. Wait until event queue drains (all events the server has for the client at this moment are sent and dispatched).
	if (wl_display_roundtrip(client->display) < 0) {
		pr_log("error", "Wayland registry roundtrip failed");
		goto cleanup;
	}

	if (have_required_globals(client)) {
		pr_log("debug", "Got all required global objects from server");
	} else {
		pr_log("error", "Some required global objects are not available");
		goto cleanup;
	}

	// 3. Create the surface (hierarchy)
	client->wl_surface = wl_compositor_create_surface(client->compositor);
	client->xdg_surface = xdg_wm_base_get_xdg_surface(client->xdg_wm_base, client->wl_surface);
	xdg_surface_add_listener(client->xdg_surface, &xdg_surface_listener, client);
	client->xdg_toplevel = xdg_surface_get_toplevel(client->xdg_surface);
	xdg_toplevel_add_listener(client->xdg_toplevel, &xdg_toplevel_listener, client);
	xdg_toplevel_set_title(client->xdg_toplevel, "LED");

	// 4. Initialize the window, the buffer and the ball
	render_buffer_init(&client->render_buffer);

	struct ball ball = {
		.x = 300,
		.y = 0,
		.dx = 0,
		.dy = 10,
		.gravity = -0.05,
		.friction = 0.99,
		.bounce = 0.95,
		.radius = 20,
		.color = {200, 133, 134, 1},
	};
	client->ball = ball;

	// 5. Signal the surface is ready to be configured
	wl_surface_commit(client->wl_surface);
	wl_display_roundtrip(client->display);

	return 0;

cleanup:
	wayland_client_destroy(client);
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
	buf_idx = render_draw(client_state);
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

int wayland_client_run_loop(struct wayland_client *client, struct protocol_state *protocol_state) {
	int ret = 0, wl_fd, connected_client_fd = -1, nfds;
	fd_set read_fds;

	wl_fd = wl_display_get_fd(client->display);

	while (1) {
		// 1. Prepare wayland for reading AND flush any pending outgoing messages
		while (wl_display_prepare_read(client->display)) { // read any event into the input queue
			// If preparation fails, it means there are already events in the internal queue
			wl_display_dispatch_pending(client->display); // dispatch messages on the input queue
		}
		wl_display_flush(client->display); // send any messages on the output queue

		// 2. Build the fdset (must be done inside the loop as syscall destroys it)
		FD_ZERO(&read_fds);
		FD_SET(wl_fd, &read_fds);
		FD_SET(protocol_state->server_fd, &read_fds);
		nfds = (protocol_state->server_fd > wl_fd ? protocol_state->server_fd : wl_fd) + 1;

		if (connected_client_fd != -1) {
			FD_SET(connected_client_fd, &read_fds);
			if (connected_client_fd >= nfds)
				nfds = connected_client_fd + 1;
		}

		// 3. Go to sleep
		ret = select(nfds, &read_fds, NULL, NULL, NULL);
		if (ret < 0) {
			pr_log_libcerror(errno, "select");
			wl_display_cancel_read(client->display);
			return -1;
		}

		// 4. Check who has data

		// a. Do we have a new client trying to connect?
		if (FD_ISSET(protocol_state->server_fd, &read_fds)) {
			int new_fd = accept4(protocol_state->server_fd, NULL, NULL, SOCK_NONBLOCK);
			if (new_fd >= 0) {
				pr_log("debug", "A client has connected!");

				// Drop client if and old one already exists (a more sophisticated app would handle more than just 1 client at a time :))
				if (connected_client_fd != -1) {
					pr_log("debug", "Dropping connection to new client...we can't betray our old client :|");
					close(new_fd);
				} else
					connected_client_fd = new_fd;
			}
		}

		// b. Did we receive a new command from any of our clients?
		if (connected_client_fd != -1 && FD_ISSET(connected_client_fd, &read_fds)) {
			struct ball_command cmd;
			int n = read(connected_client_fd, &cmd, sizeof(cmd));

			if (n == sizeof(cmd)) {
				pr_log("debug", "Received command: CMD %d", cmd.cmd);
			} else if (n == 0) {
				pr_log("debug", "Translator disconnected!");
				close(connected_client_fd);
				connected_client_fd = -1;
			}
		}

		// c. Did the compositor send us any new events?
		if (FD_ISSET(wl_fd, &read_fds)) {
			// Read the data from the wl_fd into wayland queue
			if (wl_display_read_events(client->display) < 0) {
				pr_log("error", "Failed to read wayland events");
				return -1;
			}
		} else
			// Someone else wokeup...cancle read state
			wl_display_cancel_read(client->display);

		// 5. Dispatch our input-event handler functions
		if (wl_display_dispatch_pending(client->display) < 0) {
			pr_log("error", "Wayland connection closed");
			return -1;
		}
	}

	return 0;
}
