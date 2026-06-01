#include <bits/time.h>
#include <err.h>
#include <fcntl.h>
#include <linux/gpio.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

#include "../qemu-devices/lux/include/hw.h"
#include "../qemu-drivers/led-gpio/include/lux_ioctl.h"
#include "font.h"

#define GPIO_OUT_MASK (0xFFFFFFFFFFFFFFFFULL)
#define WAIT_BETWEEN_PIXELS_NS (10000000)
#define WAIT_BETWEEN_CHARS_NS (20000000)

static inline void print_usage_exit(void) {
	printf("Usage: print-to-led <string>\n");
	exit(EXIT_SUCCESS);
}

void get_led_from_char(const char c, int y, int x, struct smart_led *led) {
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

	if (argc != 2)
		print_usage_exit();

	const char *str = argv[1];
	size_t str_len = strlen(str);

	struct smart_led led;

	lux_fd = open("/dev/lux-0", O_WRONLY | O_CLOEXEC);
	if (lux_fd < 0)
		err(EXIT_FAILURE, "open %s", "/dev/lux-0");

	if (ioctl(lux_fd, LUX_IOCTL_SET_DIRECTION_OUT, GPIO_OUT_MASK) < 0)
		err(EXIT_FAILURE, "ioctl SET_DIRECTION_OUT %s", "/dev/lux-0");

	struct lux_hw_info info;
	if (ioctl(lux_fd, LUX_IOCTL_GET_INFO, &info) < 0)
		err(EXIT_FAILURE, "ioctl GET_INFO %s", "/dev/lux-0");
	printf("LEDS: %d, ROWS: %d, COLS: %d\n", info.num_leds, info.rows, info.cols);

	for (size_t i = 0; i < str_len; i++) {
		const char c = str[i];

		for (int y = 0; y < ROW_NR; y++) {
			for (int x = 0; x < COL_NR; x++) {
				get_led_from_char(c, y, x, &led);
				int led_id = y * 8 + x;
				pwrite(lux_fd, &led, sizeof(led), led_id * sizeof(led));

				struct timespec ts = {.tv_sec = 0, .tv_nsec = WAIT_BETWEEN_PIXELS_NS};
				nanosleep(&ts, NULL);
			}
		}

		struct timespec ts = {.tv_sec = 0, .tv_nsec = WAIT_BETWEEN_CHARS_NS};
		clock_nanosleep(CLOCK_MONOTONIC, 0, &ts, NULL);
	}

	if (ioctl(lux_fd, LUX_IOCTL_CLEAR_SCREEN) < 0)
		err(EXIT_FAILURE, "ioctl LUX_IOCTL_CLEAR_SCREEN %s", "/dev/lux-0");

	close(lux_fd);

	return 0;
}
