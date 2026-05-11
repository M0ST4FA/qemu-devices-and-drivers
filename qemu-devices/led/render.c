#include <errno.h>
#include <time.h>

#include "buffer.h"
#include "logger.h"
#include "render.h"

int render_draw(struct render_buffer *buffer) {

	if (buffer->shmp == NULL) {
		pr_log("error", "Trying to draw into unallocated memory");
		return -1;
	}

	pr_log("debug", "Rendering");
	int height = buffer->dimensions.height;
	int width = buffer->dimensions.width;
	int stride = buffer->dimensions.width * PIXEL_BYTE_WIDTH;
	int center_x = width / 2;
	int center_y = height / 2;
	int bar_thickness = MIN(width, height) / 5;

	struct timespec current_time = {0};
	// No need to check for failure; even if it failed, 0 initialization would save us
	if (clock_gettime(CLOCK_REALTIME, &current_time) < 0)
		pr_log_libcerror(errno, "clock_gettime");

	for (int x = 0; x < width; x++)
		for (int y = 0; y < height; y++) {

			struct pixel {
				// little-endian ARGB
				uint8_t blue;
				uint8_t green;
				uint8_t red;
				uint8_t alpha;
			} *pixel = (struct pixel *)(buffer->shmp + y * stride + x * PIXEL_BYTE_WIDTH);

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
}
