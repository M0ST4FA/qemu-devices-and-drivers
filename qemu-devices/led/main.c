#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <syscall.h>
#include <unistd.h>
#include <wayland-client-core.h>
#include <wayland-client-protocol.h>
#include <wayland-util.h>

#include "xdg-shell-client-protocol.h"

#define PIXEL_BYTE_WIDTH 4
#define SURFACE_WIDTH 200
#define SURFACE_STRIDE (SURFACE_WIDTH * PIXEL_BYTE_WIDTH)
#define SURFACE_HEIGHT 200

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
	pr_log("error", "%s%s", msg, strerror(err));
}

struct client_state {
	/* Globals */
	struct wl_display *display;
	struct wl_registry *registry;
	struct wl_shm *shm;
	struct wl_shm_pool *shm_pool;
	struct wl_compositor *compositor;
	struct xdg_wm_base *xdg_wm_base;

	/* Objects */
	struct wl_surface *wl_surface;
	struct xdg_surface *xdg_surface;
	struct xdg_toplevel *xdg_toplevel;
};

static void registry_global_handler([[maybe_unused]] void *data, struct wl_registry *registry, uint32_t name, const char *interface, uint32_t version) {
	struct client_state *state = data;

	if (strcmp(interface, wl_compositor_interface.name) == 0) {
		state->compositor = wl_registry_bind(registry, name, &wl_compositor_interface, 6);
	} else if (strcmp(interface, wl_shm_interface.name) == 0) {
		state->shm = wl_registry_bind(registry, name, &wl_shm_interface, 2);
	} else if (strcmp(interface, xdg_wm_base_interface.name) == 0) {
		state->xdg_wm_base = wl_registry_bind(registry, name, &xdg_wm_base_interface, 7);
	}

	pr_log("debug", "interface: '%s', version: %u, name: %u", interface, version, name);
}

static void registry_global_remove_handler([[maybe_unused]] void *data, struct wl_registry *registry, uint32_t name) {
	pr_log("debug", "Removed interface: name: %u", name);
}

static void xdg_toplevel_configure_handler([[maybe_unused]] void *data, struct xdg_toplevel *xdg_toplevel,
										   int32_t width, int32_t height, struct wl_array *states) {
	pr_log("debug", "Configure %dx%d", width, height);
}
static void xdg_toplevel_configure_bounds_handler([[maybe_unused]] void *data, struct xdg_toplevel *xdg_toplevel,
												  int32_t width, int32_t height) {
	pr_log("debug", "Configure bounds %dx%d", width, height);
}

static void xdg_toplevel_close_handler([[maybe_unused]] void *data, struct xdg_toplevel *xdg_toplevel) {
	struct client_state *state = data;

	pr_log("debug", "Closing toplevel");
	wl_surface_destroy(state->wl_surface);
}

static void xdg_toplevel_wm_capabilities_handler(void *data, struct xdg_toplevel *xdg_toplevel, struct wl_array *) {
	pr_log("debug", "Server is communicating its capabilites");
}

static void xdg_surface_configure_handler([[maybe_unused]] void *data, struct xdg_surface *surface, uint32_t serial) {
	xdg_surface_ack_configure(surface, serial);
}

struct wl_registry_listener registry_listener = {
	.global = registry_global_handler,
	.global_remove = registry_global_remove_handler,
};

struct xdg_toplevel_listener xdg_toplevel_listener = {
	.configure = xdg_toplevel_configure_handler,
	.configure_bounds = xdg_toplevel_configure_bounds_handler,

	.close = xdg_toplevel_close_handler,
	.wm_capabilities = xdg_toplevel_wm_capabilities_handler,
};

struct xdg_surface_listener xdg_surface_listener = {
	.configure = xdg_surface_configure_handler,
};

int main() {
	int shm_fd, shm_size = SURFACE_HEIGHT * SURFACE_STRIDE;
	uint8_t *shared_mem = NULL;
	struct client_state state;

	// 1. Create display and registry (to query for global objects)
	state.display = wl_display_connect(NULL);
	state.registry = wl_display_get_registry(state.display);
	wl_registry_add_listener(state.registry, &registry_listener, &state);

	if (state.display)
		pr_log("debug", "Connected!");
	else {
		pr_log("error", "Couldn't connect");
		return 1;
	}

	// 2. Wait until event queue drains (all events the server has for the client at this moment are sent and dispatched).
	wl_display_roundtrip(state.display);

	if (state.compositor && state.shm && state.xdg_wm_base) {
		pr_log("debug", "Got all required global objects from server");
	} else {
		pr_log("error", "Some required global objects are not available");
		return 1;
	}

	// 3. Create a shared buffer
	shm_fd = syscall(SYS_memfd_create, "buffer", 0);
	if (shm_fd == -1) {
		pr_log_libcerror(shm_fd, "memfd_create");
		return 1;
	}
	ftruncate(shm_fd, shm_size);
	shared_mem = mmap(NULL, shm_size,
					  PROT_READ | PROT_WRITE, MAP_SHARED | MAP_FILE,
					  shm_fd, 0);
	if (shared_mem == MAP_FAILED) {
		pr_log_libcerror(errno, "mmap");
		return 1;
	}

	// 4. Pass the shm_fd to the server and tell it about the buffers inside the shm pool
	// TODO: All of these functions can fail, so do proper error handling
	state.shm_pool = wl_shm_create_pool(state.shm, shm_fd, shm_size);
	struct wl_buffer *buffer = wl_shm_pool_create_buffer(state.shm_pool, 0,
														 SURFACE_WIDTH, SURFACE_HEIGHT, SURFACE_STRIDE,
														 WL_SHM_FORMAT_XRGB8888);

	// 5. Create the surface and attach the buffer to it
	state.wl_surface = wl_compositor_create_surface(state.compositor);
	state.xdg_surface = xdg_wm_base_get_xdg_surface(state.xdg_wm_base, state.wl_surface);
	xdg_surface_add_listener(state.xdg_surface, &xdg_surface_listener, &state);
	state.xdg_toplevel = xdg_surface_get_toplevel(state.xdg_surface);
	xdg_toplevel_add_listener(state.xdg_toplevel, &xdg_toplevel_listener, &state);
	xdg_toplevel_set_title(state.xdg_toplevel, "LED");

	wl_surface_attach(state.wl_surface, buffer, 0, 0);
	wl_surface_commit(state.wl_surface);

	while (1) {
		wl_display_dispatch(state.display);
	}

	wl_display_disconnect(state.display);
	return 0;
}
