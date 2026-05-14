#include <errno.h>
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

static inline void move_ball(struct ball *ball, struct dimensions dim) {
	// 1. Move ball
	ball->dy -= ball->gravity;

	ball->x += ball->dx;
	ball->y += ball->dy;

	ball->dx *= ball->friction;
	ball->dy *= ball->friction;

	// 2. Bounce off left/right walls
	if (ball->x - ball->radius < 0) {				 // ball overflowed left wall
		ball->x = ball->radius;						 // push the ball back by "radius" amount
		ball->dx = -ball->dx * ball->bounce;		 // flip direction in x axis
	} else if (ball->x + ball->radius > dim.width) { // ball overflowed right wall
		ball->x = dim.width - ball->radius;			 // push ball back
		ball->dx = -ball->dx * ball->bounce;		 // flip direction in x axis
	}

	// 3. Bounce off ground/roof
	if (ball->y - ball->radius < 0) { // ball overflowed ground
		ball->y = ball->radius;
		ball->dy = -ball->dy * ball->bounce;
	} else if (ball->y + ball->radius > dim.height) { // ball overflowed roof
		ball->y = dim.height - ball->radius;
		ball->dy = -ball->dy * ball->bounce;
	}
}

static inline void render_clear_screen(struct render_buffer *buffer, int buf_idx, uint8_t color[4]) {
	int32_t height = buffer->dimensions.height;
	int32_t width = buffer->dimensions.width;
	int32_t stride = buffer->dimensions.width * PIXEL_BYTE_WIDTH;
	size_t frame_size = height * stride;
	uint8_t *front_buffer = buffer->shmp + frame_size * buf_idx;

	for (int y = 0; y < height; y++)
		for (int x = 0; x < width; x++) {

			// 1. Get pixel
			struct pixel {
				// little-endian ARGB
				uint8_t blue;
				uint8_t green;
				uint8_t red;
				uint8_t alpha;
			} *pixel = (struct pixel *)(front_buffer + y * stride + x * PIXEL_BYTE_WIDTH);

			pixel->red = color[0];
			pixel->green = color[1];
			pixel->blue = color[2];
			pixel->alpha = color[3];
		}
}

static inline void render_ball(struct render_buffer *buffer, int buf_idx, struct ball *ball) {
	int32_t height = buffer->dimensions.height;
	int32_t width = buffer->dimensions.width;
	int32_t stride = buffer->dimensions.width * PIXEL_BYTE_WIDTH;
	size_t frame_size = height * stride;
	uint8_t *front_buffer = buffer->shmp + frame_size * buf_idx;

	for (int y = 0; y < height; y++)
		for (int x = 0; x < width; x++) {

			// 1. Get pixel
			struct pixel {
				// little-endian ARGB
				uint8_t blue;
				uint8_t green;
				uint8_t red;
				uint8_t alpha;
			} *pixel = (struct pixel *)(front_buffer + y * stride + x * PIXEL_BYTE_WIDTH);

			// 2. Determine if pixel is within ball boundary (Thank Pythagoras)
			float dist_x = x - ball->x;
			float dist_y = y - ball->y;
			if (!(dist_x * dist_x + dist_y * dist_y <= ball->radius * ball->radius))
				continue; // if pixel is not within ball, then continue to next pixel

			// 3. Render pixel
			pixel->red = ball->color[0];
			pixel->green = ball->color[1];
			pixel->blue = ball->color[2];
			pixel->alpha = ball->color[3];
		}
}

int render_draw(struct wayland_client *client) {
	struct render_buffer *buffer = &client->render_buffer;
	struct ball *ball = &client->ball;

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

	// 3. Move ball
	move_ball(ball, buffer->dimensions);

	// 4. Render
	pr_log("debug", "Rendering");
	uint8_t bg_color[4] = {114, 160, 193, 125};
	render_clear_screen(buffer, buf_idx, bg_color);
	render_ball(buffer, buf_idx, ball);

	return buf_idx;
}
