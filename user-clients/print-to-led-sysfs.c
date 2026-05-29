#include <bits/time.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/gpio.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

#include "../qemu-devices/lux/include/hw.h"
#include "font.h"

#define LED_DEFAULT_COLOR (0xFF00FFFF)
#define LED_NAME_DEVICENAME ""
#define LED_NAME_COLOR "rgb:"
#define LED_NAME_FUNCTION "indicator"
#define LED_NAME LED_NAME_DEVICENAME LED_NAME_COLOR LED_NAME_FUNCTION

static inline void print_usage_exit(void) {
	printf("Usage: print-to-led-sysfs <string>\n");
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

static int open_sysfs_leds(int led_fds[LED_NR]) {
	char led_name[64] = {0};

	for (int i = 0; i < LED_NR; i++) {
		snprintf(led_name, 64, "/sys/class/leds/" LED_NAME "-%d/brightness",
				 i);
		led_fds[i] = open(led_name, O_WRONLY | O_CLOEXEC);
		if (led_fds[i] < 0) {
			fprintf(stderr, "open %s: %s\n", led_name, strerror(errno));
			return -1;
		}
	}

	return 0;
}

static inline void write_led(int led_fds[LED_NR], int led_id, bool led_open) {
	int ret = 0;

	if (led_open) {
		ret = write(led_fds[led_id], "1", sizeof("1"));
		if (ret < 0)
			perror("write");
	} else {
		ret = write(led_fds[led_id], "0", sizeof("0"));
		if (ret < 0)
			perror("write");
	}
}

static inline void clear_led_grid(int led_fds[LED_NR]) {
	for (int y = 0; y < ROW_NR; y++) {
		for (int x = 0; x < COL_NR; x++) {
			int led_id = y * 8 + x;

			write_led(led_fds, led_id, false);

			struct timespec ts = {.tv_sec = 0, .tv_nsec = 10000000};
			nanosleep(&ts, NULL);
		}
	}
}

int main(int argc, char *argv[]) {
	int ret;
	int led_fds[LED_NR] = {0};

	if (argc != 2)
		print_usage_exit();

	const char *str = argv[1];
	size_t str_len = strlen(str);

	if (open_sysfs_leds(led_fds) < 0)
		errx(EXIT_FAILURE, "Failed to open sysfs led brightness files\n");

	for (size_t i = 0; i < str_len; i++) {
		char c = str[i];

		for (int y = 0; y < ROW_NR; y++) {
			for (int x = 0; x < COL_NR; x++) {
				int led_id = y * 8 + x;
				uint8_t row = font[(uint8_t)c][y];
				int led_open = (row >> (7 - x)) & 1;

				write_led(led_fds, led_id, led_open);

				struct timespec ts = {.tv_sec = 0, .tv_nsec = 10000000};
				nanosleep(&ts, NULL);
			}
		}

		struct timespec ts = {.tv_sec = 0, .tv_nsec = 200000000};
		ret = nanosleep(&ts, NULL);
		if (ret < 0)
			err(EXIT_FAILURE, "nanosleep");
	}

	clear_led_grid(led_fds);

	for (int i = 0; i < LED_NR; i++)
		if (close(led_fds[i]) < 0)
			perror("close(led_fd)");

	return 0;
}
