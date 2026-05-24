#include <bits/time.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "../qemu-devices/led-gpio/include/hw.h"
#include "font.h"

#define LED_DEFAULT_COLOR (0xFF00FFFF)

static inline void print_usage_exit(void) {
	printf("Usage: print-to-led <string>\n");
	exit(EXIT_SUCCESS);
}

void get_led_from_char(const char c, int y, int x, struct smart_led *led) {
	uint8_t row = font[c][y];

	int32_t led_state = (row >> (7 - x)) & 1;

	led->state = led_state;
	led->color[0] = 255;
	led->color[1] = 255;
	led->color[2] = 100;
	led->color[3] = 255;
}

int main(int argc, char *argv[]) {
	int ret = 0;

	if (argc != 2)
		print_usage_exit();

	char *str = argv[1];
	size_t str_len = strlen(str);

	struct smart_led led;

	int fd = open("/dev/lux-0", O_WRONLY | O_CLOEXEC);
	if (fd < 0)
		err(EXIT_FAILURE, "open %s", "/dev/lux-0");

	for (size_t i = 0; i < str_len; i++) {
		const char c = str[i];

		for (int y = 0; y < ROW_NR; y++) {
			for (int x = 0; x < COL_NR; x++) {
				get_led_from_char(c, y, x, &led);
				int led_id = y * 8 + x;
				pwrite(fd, &led, sizeof(led), led_id * sizeof(led));

				struct timespec ts = {.tv_sec = 0, .tv_nsec = 1000000};
				nanosleep(&ts, NULL);
			}
		}

		ret = sleep(1);
	}

	return 0;
}
