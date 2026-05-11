#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <syscall.h>
#include <unistd.h>
#include <wayland-client-protocol.h>
#include <wayland-client.h>

#include "xdg-shell-client-protocol.h"

#define PIXEL_BYTE_WIDTH 4
#define MIN(a, b) ((a) < (b) ? (a) : (b))

[[gnu::format(printf, 2, 3)]]
static inline void pr_log(const char *level, const char *format, ...) {
	va_list args;

	fprintf(stderr, "[%s] ", level);

	va_start(args, format);

	vfprintf(stderr, format, args);

	va_end(args);

	fprintf(stderr, "\n");
}

static inline void pr_log_libcerror(int err, const char *msg) {
	pr_log("error", "%s: %s", msg, strerror(err));
}

struct surface_dimensions {
	uint32_t width;
	uint32_t height;
	uint32_t stride;
};

struct client_state {
	/* Globals */
	struct wl_display *display;
	struct wl_registry *registry;
	struct wl_shm *shm;
	struct wl_shm_pool *shm_pool;
	struct wl_buffer *buffer;
	struct wl_compositor *compositor;
	struct xdg_wm_base *xdg_wm_base;

	/* Objects */
	struct wl_surface *wl_surface;
	struct xdg_surface *xdg_surface;
	struct xdg_toplevel *xdg_toplevel;

	/* Others */
	int shm_fd;
	int shm_size;
	uint8_t *shmp;
	struct surface_dimensions surface_dimensions;
};

static void xdg_wm_base_ping_handler([[maybe_unused]] void *data,
									 struct xdg_wm_base *xdg_wm_base,
									 uint32_t serial) {
	xdg_wm_base_pong(xdg_wm_base, serial);
}

static const struct xdg_wm_base_listener xdg_wm_base_listener = {
	.ping = xdg_wm_base_ping_handler,
};

static void registry_global_handler(void *data, struct wl_registry *registry,
									uint32_t name, const char *interface,
									uint32_t version) {
	struct client_state *state = data;

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
	}

	// pr_log("debug", "global: interface='%s', version=%u, name=%u",
	// 	   interface, version, name);
}

static void registry_global_remove_handler([[maybe_unused]] void *data,
										   [[maybe_unused]] struct wl_registry *registry,
										   uint32_t name) {
	pr_log("debug", "Removed interface: name: %u", name);
}

static void xdg_toplevel_configure_handler([[maybe_unused]] void *data, struct xdg_toplevel *xdg_toplevel,
										   int32_t width, int32_t height, struct wl_array *states) {
	struct client_state *state = data;
	pr_log("debug", "Configure %dx%d", width, height);

	if (width > 0)
		state->surface_dimensions.width = width;
	if (height > 0)
		state->surface_dimensions.height = height;
}
static void xdg_toplevel_configure_bounds_handler([[maybe_unused]] void *data, struct xdg_toplevel *xdg_toplevel,
												  int32_t width, int32_t height) {
	struct client_state *state = data;
	pr_log("debug", "Configure bounds %dx%d", width, height);

	if (width > 0)
		state->surface_dimensions.width = width;
	if (height > 0)
		state->surface_dimensions.height = height;
}

static void xdg_toplevel_close_handler([[maybe_unused]] void *data, struct xdg_toplevel *xdg_toplevel) {
	struct client_state *state = data;

	pr_log("debug", "Closing toplevel");
	wl_surface_destroy(state->wl_surface);
	state->wl_surface = NULL;
}

static void xdg_toplevel_wm_capabilities_handler(void *data, struct xdg_toplevel *xdg_toplevel, struct wl_array *) {
	pr_log("debug", "Server is communicating its capabilites");
}

static void xdg_surface_configure_handler([[maybe_unused]] void *data, struct xdg_surface *surface, uint32_t serial) {
	struct client_state *state = data;
	xdg_surface_ack_configure(surface, serial);

	int shm_fd, shm_size, ret;

	shm_fd = syscall(SYS_memfd_create, "buffer", MFD_CLOEXEC | MFD_ALLOW_SEALING);
	if (shm_fd == -1) {
		pr_log_libcerror(shm_fd, "memfd_create");
		exit(EXIT_FAILURE);
	}
	shm_size = state->surface_dimensions.height * state->surface_dimensions.stride;
	ret = ftruncate(shm_fd, shm_size);
	if (ret < 0) {
		pr_log_libcerror(errno, "ftruncate");
		exit(EXIT_FAILURE);
	}
	ret = fcntl(shm_fd, F_ADD_SEALS, F_SEAL_SHRINK | F_SEAL_SEAL);
	if (ret < 0) {
		pr_log_libcerror(errno, "fcntl");
		exit(EXIT_FAILURE);
	}

	state->shmp = mmap(NULL, shm_size,
					   PROT_READ | PROT_WRITE, MAP_SHARED | MAP_FILE,
					   shm_fd, 0);
	if (state->shmp == MAP_FAILED) {
		pr_log_libcerror(errno, "mmap");
		exit(EXIT_FAILURE);
	}

	// 4. Pass the shm_fd to the server and tell it about the buffers inside the shm pool
	// TODO: All of these functions can fail, so do proper error handling
	state->shm_pool = wl_shm_create_pool(state->shm, shm_fd, shm_size);
	if (state->shm_pool == NULL) {
		pr_log("error", "Failed to create shm_pool");
		exit(EXIT_FAILURE);
	}

	state->buffer = wl_shm_pool_create_buffer(state->shm_pool, 0,
											  state->surface_dimensions.width, state->surface_dimensions.height,
											  state->surface_dimensions.stride,
											  WL_SHM_FORMAT_XRGB8888);
	if (state->buffer == NULL) {
		pr_log("error", "Failed to create wl_buffer");
		exit(EXIT_FAILURE);
	}

	wl_surface_attach(state->wl_surface, state->buffer, 0, 0);
	wl_surface_commit(state->wl_surface);
}

static const struct wl_registry_listener registry_listener = {
	.global = registry_global_handler,
	.global_remove = registry_global_remove_handler,
};

static const struct xdg_toplevel_listener xdg_toplevel_listener = {
	.configure = xdg_toplevel_configure_handler,
	.configure_bounds = xdg_toplevel_configure_bounds_handler,

	.close = xdg_toplevel_close_handler,
	.wm_capabilities = xdg_toplevel_wm_capabilities_handler,
};

static const struct xdg_surface_listener xdg_surface_listener = {
	.configure = xdg_surface_configure_handler,
};

static inline bool have_required_globals(const struct client_state *state) {
	return state->compositor != NULL && state->shm != NULL &&
		   state->xdg_wm_base != NULL;
}

static void client_state_release(struct client_state *state) {
	if (state->xdg_toplevel != NULL)
		xdg_toplevel_destroy(state->xdg_toplevel);
	if (state->xdg_surface != NULL)
		xdg_surface_destroy(state->xdg_surface);
	if (state->wl_surface != NULL)
		wl_surface_destroy(state->wl_surface);
	if (state->buffer != NULL)
		wl_buffer_destroy(state->buffer);
	if (state->shm_pool != NULL)
		wl_shm_pool_destroy(state->shm_pool);
	if (state->xdg_wm_base != NULL)
		xdg_wm_base_destroy(state->xdg_wm_base);
	if (state->shm != NULL)
		wl_shm_destroy(state->shm);
	if (state->compositor != NULL)
		wl_compositor_destroy(state->compositor);
	if (state->registry != NULL)
		wl_registry_destroy(state->registry);
	if (state->display != NULL)
		wl_display_disconnect(state->display);
}

int main() {
	int ret = 1;
	struct client_state state = {0};
	state.surface_dimensions.height = 200;
	state.surface_dimensions.width = 200;
	state.surface_dimensions.stride = 200 * PIXEL_BYTE_WIDTH;

	// 1. Create display and registry (to query for global objects)
	state.display = wl_display_connect(NULL);
	if (state.display == NULL) {
		pr_log("error", "Couldn't connect to Wayland display");
		goto cleanup;
	}

	pr_log("debug", "Connected to Wayland display");

	state.registry = wl_display_get_registry(state.display);
	if (state.registry == NULL) {
		pr_log("error", "Couldn't get Wayland registry");
		goto cleanup;
	}

	wl_registry_add_listener(state.registry, &registry_listener, &state);

	// 2. Wait until event queue drains (all events the server has for the client at this moment are sent and dispatched).
	if (wl_display_roundtrip(state.display) < 0) {
		pr_log("error", "Wayland registry roundtrip failed");
		goto cleanup;
	}

	if (have_required_globals(&state)) {
		pr_log("debug", "Got all required global objects from server");
	} else {
		pr_log("error", "Some required global objects are not available");
		goto cleanup;
	}

	// 3. Create the surface (hierarchy)
	state.wl_surface = wl_compositor_create_surface(state.compositor);
	state.xdg_surface = xdg_wm_base_get_xdg_surface(state.xdg_wm_base, state.wl_surface);
	xdg_surface_add_listener(state.xdg_surface, &xdg_surface_listener, &state);
	state.xdg_toplevel = xdg_surface_get_toplevel(state.xdg_surface);
	xdg_toplevel_add_listener(state.xdg_toplevel, &xdg_toplevel_listener, &state);
	xdg_toplevel_set_title(state.xdg_toplevel, "LED");

	// Signal the surface is ready to be configured
	wl_surface_commit(state.wl_surface);

	// 4. Wait for the surface to be configured, attach the buffer to it, and commit

	while (1) {
		wl_display_dispatch(state.display);
	}

	ret = 0;

cleanup:
	client_state_release(&state);

	return ret;
}
