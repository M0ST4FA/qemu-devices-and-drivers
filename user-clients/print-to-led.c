#include <bits/time.h>
#include <err.h>
#include <fcntl.h>
#include <linux/gpio.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

#include "../qemu-devices/lux/include/hw.h"
#include "font.h"

#define WAIT_BETWEEN_PIXELS_NS (10000000)
#define WAIT_BETWEEN_CHARS_NS (20000000)

static inline void print_usage_exit(void) {
	printf("Usage: print-to-led <string>\n");
	exit(EXIT_SUCCESS);
}

void write_char_pixel_at_coords(const char c, int y, int x, struct smart_led *led) {
	uint8_t row = font[(uint8_t)c][y];

	int32_t led_state = (row >> (7 - x)) & 1;

	led->state = led_state;
	led->color[0] = 255;
	led->color[1] = 255;
	led->color[2] = 100;
	led->color[3] = 255;
}

int main(int argc, char *argv[]) {
	int lux_fd;
	struct smart_led *leds;

	if (argc != 2)
		print_usage_exit();

	const char *str = argv[1];
	size_t str_len = strlen(str);

	lux_fd = open("/dev/lux-0", O_RDWR | O_CLOEXEC);
	if (lux_fd < 0)
		err(EXIT_FAILURE, "open %s", "/dev/lux-0");

	leds = mmap(NULL, LED_NR * sizeof(struct smart_led), PROT_WRITE,
				MAP_FILE | MAP_SHARED, lux_fd, 0);
	if (leds == MAP_FAILED)
		err(EXIT_FAILURE, "mmap %s", "/dev/lux-0");

	for (size_t i = 0; i < str_len; i++) {
		const char c = str[i];

		for (int y = 0; y < ROW_NR; y++) {
			for (int x = 0; x < COL_NR; x++) {
				struct smart_led *pixe_led;
				int led_id;

				led_id = y * 8 + x;
				pixe_led = leds + led_id;

				write_char_pixel_at_coords(c, y, x, pixe_led);

				struct timespec ts = {.tv_sec = 0, .tv_nsec = WAIT_BETWEEN_PIXELS_NS};
				nanosleep(&ts, NULL);
			}
		}

		struct timespec ts = {.tv_sec = 0, .tv_nsec = WAIT_BETWEEN_CHARS_NS};
		clock_nanosleep(CLOCK_MONOTONIC, 0, &ts, NULL);
	}

	close(lux_fd);

	return 0;
}
