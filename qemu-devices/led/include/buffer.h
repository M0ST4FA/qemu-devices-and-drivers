#pragma once

#include <stdint.h>
#include <wayland-client-protocol.h>

#define PIXEL_BYTE_WIDTH 4
#define MIN(a, b) ((a) < (b) ? (a) : (b))

struct dimensions {
	int32_t width;
	int32_t height;
};

struct render_buffer {
	int shm_fd;
	int shm_size;
	uint8_t *shmp;
	struct dimensions dimensions;

	struct wl_shm_pool *wl_shm_pool;
	struct wl_buffer *wl_buffer;
};

void render_buffer_init(struct render_buffer *buffer);
int render_buffer_resize(struct render_buffer *buffer, struct wl_shm *wl_shm,
						 struct wl_surface *wl_surface, struct dimensions new_dims);
void render_buffer_destroy(struct render_buffer *buffer);
