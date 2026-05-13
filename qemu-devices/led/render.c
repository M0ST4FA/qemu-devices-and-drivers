#include <errno.h>
#include <time.h>

#include "buffer.h"
#include "logger.h"
#include "render.h"

static inline int get_front_buffer_index(struct render_buffer *buffer) {
	int buffer_index = -1;
	if (buffer->buffer_busy[0] == 0)
		buffer_index = 0;
	else if (buffer->buffer_busy[1] == 0)
		buffer_index = 1;

	return buffer_index;
}

int render_draw(struct render_buffer *buffer) {

	// 1. Make sure buffer is allocated
	if (buffer->shmp == NULL) {
		pr_log("error", "Trying to draw into unallocated memory");
		return -1;
	}

	// 2. Get frontbuffer
	int buf_idx = get_front_buffer_index(buffer);
	if (buf_idx == -1) {
		pr_log("error", "Both buffers are busy! The compositor is slow");
		return -1; // IMPORTANT: Caller should skip and wait for next release event
	}

	int height = buffer->dimensions.height;
	int width = buffer->dimensions.width;
	int stride = buffer->dimensions.width * PIXEL_BYTE_WIDTH;
	uint8_t *front_buffer = buffer->shmp + height * stride * buf_idx;

	struct timespec current_time = {0};
	// No need to check for failure; even if it failed, 0 initialization would save us
	if (clock_gettime(CLOCK_REALTIME, &current_time) < 0)
		pr_log_libcerror(errno, "clock_gettime");

	// 3. Render
	pr_log("debug", "Rendering");
	for (int x = 0; x < width; x++)
		for (int y = 0; y < height; y++) {

			struct pixel {
				// little-endian ARGB
				uint8_t blue;
				uint8_t green;
				uint8_t red;
				uint8_t alpha;
			} *pixel = (struct pixel *)(front_buffer + y * stride + x * PIXEL_BYTE_WIDTH);

			// if ((x + y) % 30 < 10) {
			// 	pixel->alpha = 0;
			// 	continue;
			// }

			// if (!((80 < x && x < 120) || (80 < y && y < 120))) {
			// 	pixel->alpha = 0;
			// 	continue;
			// }

			pixel->alpha = MIN(50, (255 + current_time.tv_sec) % 255);
			pixel->red = (255 + current_time.tv_sec) % 255;
			pixel->green = 255;
			pixel->blue = x % 255;
		}

	return buf_idx;
}
