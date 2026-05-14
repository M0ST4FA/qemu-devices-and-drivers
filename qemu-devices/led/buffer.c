#pragma once

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <wayland-client-core.h>
#include <wayland-client-protocol.h>

#include "buffer.h"
#include "logger.h"

void render_buffer_init(struct render_buffer *buffer) {
	memset(buffer, 0, sizeof(*buffer));

	buffer->current_buffer_idx = 0;
}

static void wl_buffer_release_handler(void *data, struct wl_buffer *wl_buffer) {
	struct render_buffer *render_buffer = data;

	if (wl_buffer == render_buffer->wl_buffers[0])
		render_buffer->buffer_busy[0] = 0;

	if (wl_buffer == render_buffer->wl_buffers[1])
		render_buffer->buffer_busy[1] = 0;
}

struct wl_buffer_listener wl_buffer_listener = {
	.release = wl_buffer_release_handler,
};

static inline int create_and_map_shm(struct render_buffer *buffer) {
	int ret = 0;
	int32_t height = buffer->dimensions.height;
	int32_t stride = buffer->dimensions.width * PIXEL_BYTE_WIDTH;

	ret = buffer->shm_fd = syscall(SYS_memfd_create, "buffer", MFD_CLOEXEC | MFD_ALLOW_SEALING);
	if (buffer->shm_fd == -1) {
		pr_log_libcerror(buffer->shm_fd, "memfd_create");
		return ret;
	}
	buffer->shm_size = height * stride * WL_BUFFER_NR;

	ret = ftruncate(buffer->shm_fd, buffer->shm_size);
	if (ret < 0) {
		pr_log_libcerror(errno, "ftruncate");
		return ret;
	}

	ret = fcntl(buffer->shm_fd, F_ADD_SEALS, F_SEAL_SHRINK | F_SEAL_SEAL);
	if (ret < 0) {
		pr_log_libcerror(errno, "fcntl");
		return ret;
	}

	buffer->shmp = mmap(NULL, buffer->shm_size,
						PROT_READ | PROT_WRITE, MAP_SHARED | MAP_FILE,
						buffer->shm_fd, 0);
	if (buffer->shmp == MAP_FAILED) {
		pr_log_libcerror(errno, "mmap");
		return -1;
	}

	return 0;
}

static inline int create_wl_buffers(struct render_buffer *buffer, struct wl_shm *wl_shm) {
	int32_t height = buffer->dimensions.height;
	int32_t width = buffer->dimensions.width;
	int32_t stride = buffer->dimensions.width * PIXEL_BYTE_WIDTH;

	buffer->wl_shm_pool = wl_shm_create_pool(wl_shm, buffer->shm_fd, buffer->shm_size);
	if (buffer->wl_shm_pool == NULL) {
		pr_log("error", "Failed to create shm_pool");
		return -1;
	}

	buffer->wl_buffers[0] = wl_shm_pool_create_buffer(buffer->wl_shm_pool, 0,
													  width, height, stride,
													  WL_SHM_FORMAT_ARGB8888);
	wl_buffer_add_listener(buffer->wl_buffers[0], &wl_buffer_listener, buffer);

	buffer->wl_buffers[1] = wl_shm_pool_create_buffer(buffer->wl_shm_pool, height * stride,
													  width, height, stride,
													  WL_SHM_FORMAT_ARGB8888);
	wl_buffer_add_listener(buffer->wl_buffers[1], &wl_buffer_listener, buffer);

	if (buffer->wl_buffers[0] == NULL || buffer->wl_buffers[1] == NULL) {
		pr_log("error", "Failed to create wl_buffer");
		return -1;
	}

	return 0;
}

int render_buffer_resize(struct render_buffer *buffer, struct wl_shm *wl_shm,
						 struct dimensions new_dims) {
	int ret = -1;
	int32_t new_width = new_dims.width;
	int32_t new_height = new_dims.height;

	if (buffer->shmp != NULL &&
		buffer->dimensions.width == new_width && buffer->dimensions.height == new_height)
		return 0; // Nothing to do

	// 1. Start by destroying old buffer
	render_buffer_destroy(buffer);
	buffer->buffer_busy[0] = 0;
	buffer->buffer_busy[1] = 0;

	if (new_width > 0) {
		buffer->dimensions.width = new_width;
	}

	if (new_height > 0)
		buffer->dimensions.height = new_height;

	// 2. Create anonymous file and map it to proccess address space to act as render buffer
	if (create_and_map_shm(buffer) < 0)
		goto cleanup;

	// 3. Communicate with Wayland compositor to share buffer with it
	if (create_wl_buffers(buffer, wl_shm) < 0)
		goto cleanup;

	return 0;

cleanup:
	render_buffer_destroy(buffer);
	return ret;
}

void render_buffer_destroy(struct render_buffer *buffer) {
	int ret = 0;

	if (buffer->wl_buffers[0] != NULL)
		wl_buffer_destroy(buffer->wl_buffers[0]);
	if (buffer->wl_buffers[1] != NULL)
		wl_buffer_destroy(buffer->wl_buffers[1]);

	if (buffer->wl_shm_pool != NULL)
		wl_shm_pool_destroy(buffer->wl_shm_pool);

	if (buffer->shmp) {
		ret = munmap(buffer->shmp, buffer->shm_size);
		buffer->shmp = NULL;

		if (ret < 0)
			pr_log_libcerror(errno, "munmap");
	}
	if (buffer->shm_fd > STDERR_FILENO) { // it starts as 0
		ret = close(buffer->shm_fd);
		buffer->shm_fd = 0;

		if (ret < 0)
			pr_log_libcerror(errno, "close");
	}
}
