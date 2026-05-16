#include <string.h>
#include <time.h>

#include "buffer.h"
#include "logger.h"
#include "render.h"
#include "wayland.h"

static inline int get_front_buffer_index(struct render_buffer *buffer) {
	int buffer_index = -1;
	if (buffer->buffer_busy[0] == 0)
		buffer_index = 0;
	else if (buffer->buffer_busy[1] == 0)
		buffer_index = 1;

	return buffer_index;
}

static inline void render_clear_screen(uint8_t *buffer, struct dimensions dim, uint8_t color[4]) {
	int32_t height = dim.height;
	int32_t width = dim.width;
	int32_t stride = width * PIXEL_BYTE_WIDTH;

	for (int y = 0; y < height; y++)
		for (int x = 0; x < width; x++) {

			// 1. Get pixel
			struct pixel {
				// little-endian ARGB
				uint8_t blue;
				uint8_t green;
				uint8_t red;
				uint8_t alpha;
			} *pixel = (struct pixel *)(buffer + y * stride + x * PIXEL_BYTE_WIDTH);

			pixel->red = color[0];
			pixel->green = color[1];
			pixel->blue = color[2];
			pixel->alpha = color[3];
		}
}

struct circle {
	int32_t center_x, center_y, radius;
	uint8_t color[4];
};

static inline void draw_circle(uint8_t *buffer, struct dimensions window_dim, struct circle *circle) {
	int32_t height = window_dim.height;
	int32_t width = window_dim.width;
	int32_t stride = width * PIXEL_BYTE_WIDTH;

	int32_t radius_squared = circle->radius * circle->radius;
	uint8_t circle_color[4] = {0};
	mempcpy(&circle_color, circle->color, sizeof(circle_color));

	for (int y = 0; y < height; y++)
		for (int x = 0; x < width; x++) {
			// Distance of pixel from circle center
			int32_t distance_x, distance_y, distance_squared;

			// 1. Determine wether the pixel is within the circle
			distance_x = (x - circle->center_x) * (x - circle->center_x);
			distance_y = (y - circle->center_y) * (y - circle->center_y);
			distance_squared = distance_x + distance_y;

			if (distance_squared > radius_squared)
				continue;

			// 2. Get the pixel
			struct pixel {
				// little-endian ARGB
				uint8_t blue;
				uint8_t green;
				uint8_t red;
				uint8_t alpha;
			} *pixel = (struct pixel *)(buffer + y * stride + x * PIXEL_BYTE_WIDTH);

			// 3. Colorize the pixel
			pixel->red = circle_color[0];
			pixel->green = circle_color[1];
			pixel->blue = circle_color[2];
			pixel->alpha = circle_color[3];
		}
}

static inline void render_led_grid(uint8_t *buffer, struct dimensions window_dim, struct led_grid *led_grid) {
	int32_t height = window_dim.height;
	int32_t width = window_dim.width;

	// 1. Determine single led dimensions
	int32_t led_radius = width / (LED_COLS * 2);
	int32_t spacing_x = width / LED_COLS;
	int32_t spacing_y = height / LED_ROWS;

	// 2. Draw the leds one by one
	for (int i = 0; i < LED_NR; i++) {
		struct led *led = &led_grid->leds[i];
		int32_t row, col;
		struct circle circle;

		// a. Determine position of led in the grid
		row = i / LED_COLS;
		col = i % LED_COLS;

		circle.center_x = (col * spacing_x) + (spacing_x / 2);
		circle.center_y = (row * spacing_y) + (spacing_y / 2);
		circle.radius = led_radius;

		// b. Determine color of led
		if (led->on)
			memcpy(circle.color, led->color, sizeof(circle.color));
		else
			memcpy(circle.color, led_grid->off_color, sizeof(circle.color));

		// c. Draw led
		draw_circle(buffer, window_dim, &circle);
	}
}

int render_draw(struct wayland_client *client) {
	struct render_buffer *buffer = &client->render_buffer;
	struct dimensions dim = buffer->dimensions;
	struct led_grid *led_grid = &client->led_grid;

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
	int32_t height = dim.height;
	int32_t width = dim.width;
	int32_t stride = width * PIXEL_BYTE_WIDTH;
	size_t frame_size = height * stride;

	uint8_t *front_buffer = buffer->shmp + frame_size * buf_idx;

	// 3. Render
	uint8_t bg_color[4] = {30, 40, 35, 255};
	render_clear_screen(front_buffer, dim, bg_color);
	render_led_grid(front_buffer, dim, led_grid);

	return buf_idx;
}
