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

	buffer->dimensions.width = 200;
	buffer->dimensions.height = 200;
}

int render_buffer_resize(struct render_buffer *buffer, struct wl_shm *wl_shm,
						 struct wl_surface *wl_surface, struct dimensions new_dims) {
	int ret = -1;
	int32_t new_width = new_dims.width;
	int32_t new_height = new_dims.height;

	if (buffer->shmp != NULL && buffer->wl_buffer != NULL &&
		buffer->dimensions.width == new_width && buffer->dimensions.height == new_height)
		return 0; // Nothing to do

	// 1. Start by destroying old buffer
	render_buffer_destroy(buffer);

	if (new_width > 0) {
		buffer->dimensions.width = new_width;
	}

	if (new_height > 0)
		buffer->dimensions.height = new_height;

	// 2. Create anonymous file and map it to proccess address space to act as render buffer
	buffer->shm_fd = syscall(SYS_memfd_create, "buffer", MFD_CLOEXEC | MFD_ALLOW_SEALING);
	if (buffer->shm_fd == -1) {
		pr_log_libcerror(buffer->shm_fd, "memfd_create");
		goto cleanup;
	}
	buffer->shm_size = buffer->dimensions.height * buffer->dimensions.width * PIXEL_BYTE_WIDTH;

	ret = ftruncate(buffer->shm_fd, buffer->shm_size);
	if (ret < 0) {
		pr_log_libcerror(errno, "ftruncate");
		goto cleanup;
	}

	ret = fcntl(buffer->shm_fd, F_ADD_SEALS, F_SEAL_SHRINK | F_SEAL_SEAL);
	if (ret < 0) {
		pr_log_libcerror(errno, "fcntl");
		goto cleanup;
	}

	// 3. Map page cache pages of file into our process
	buffer->shmp = mmap(NULL, buffer->shm_size,
						PROT_READ | PROT_WRITE, MAP_SHARED | MAP_FILE,
						buffer->shm_fd, 0);
	if (buffer->shmp == MAP_FAILED) {
		pr_log_libcerror(errno, "mmap");
		goto cleanup;
	}

	// 4. Communicate with Wayland compositor to share buffer with it
	buffer->wl_shm_pool = wl_shm_create_pool(wl_shm, buffer->shm_fd, buffer->shm_size);
	if (buffer->wl_shm_pool == NULL) {
		pr_log("error", "Failed to create shm_pool");
		goto cleanup;
	}

	buffer->wl_buffer = wl_shm_pool_create_buffer(buffer->wl_shm_pool, 0,
												  buffer->dimensions.width, buffer->dimensions.height,
												  buffer->dimensions.width * PIXEL_BYTE_WIDTH,
												  WL_SHM_FORMAT_ARGB8888);
	if (buffer->wl_buffer == NULL) {
		pr_log("error", "Failed to create wl_buffer");
		goto cleanup;
	}

	wl_surface_attach(wl_surface, buffer->wl_buffer, 0, 0);

	return 0;

cleanup:
	render_buffer_destroy(buffer);
	return ret;
}

void render_buffer_destroy(struct render_buffer *buffer) {
	int ret = 0;

	if (buffer->wl_buffer != NULL)
		wl_buffer_destroy(buffer->wl_buffer);
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
