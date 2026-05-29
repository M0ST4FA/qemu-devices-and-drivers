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

static inline void print_usage_exit(void) {
	printf("Usage: print-to-led <gpio_chip_path> <string>\n");
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

[[nodiscard("You must handle the error. If this fails, GPIO pins may not be configured for output.")]]
static int configure_gpio_lines(int gpiochip_fd) {
	int ret = 0;
	struct gpio_v2_line_request req = {0};
	req.num_lines = 64;
	for (int i = 0; i < 64; i++) {
		req.offsets[i] = i;
	}

	req.config.attrs[0].attr.id = GPIO_V2_LINE_ATTR_ID_FLAGS;
	req.config.attrs[0].attr.flags = GPIO_V2_LINE_FLAG_OUTPUT;
	req.config.attrs[0].mask = 0xFFFFFFFFFFFFFFFFULL;
	req.config.num_attrs = 1;

	ret = ioctl(gpiochip_fd, GPIO_V2_GET_LINE_IOCTL, &req); // Lock the pins for your process

	if (ret >= 0)
		ret = req.fd; // You can use it to configure later!

	return ret;
}

int main(int argc, char *argv[]) {
	int gpiochip_fd, gpiochip_config_fd, lux_fd;

	if (argc != 3)
		print_usage_exit();

	const char *gpiochip_path = argv[1];
	const char *str = argv[2];
	size_t str_len = strlen(str);

	struct smart_led led;

	gpiochip_fd = open(gpiochip_path, O_WRONLY | O_CLOEXEC);
	if (gpiochip_fd < 0)
		err(EXIT_FAILURE, "open %s", gpiochip_path);
	gpiochip_config_fd = configure_gpio_lines(gpiochip_fd);
	if (gpiochip_config_fd < 0)
		err(EXIT_FAILURE, "ioctl %s", gpiochip_path);

	lux_fd = open("/dev/lux-0", O_WRONLY | O_CLOEXEC);
	if (lux_fd < 0)
		err(EXIT_FAILURE, "open %s", "/dev/lux-0");

	for (size_t i = 0; i < str_len; i++) {
		const char c = str[i];

		for (int y = 0; y < ROW_NR; y++) {
			for (int x = 0; x < COL_NR; x++) {
				get_led_from_char(c, y, x, &led);
				int led_id = y * 8 + x;
				pwrite(lux_fd, &led, sizeof(led), led_id * sizeof(led));

				struct timespec ts = {.tv_sec = 0, .tv_nsec = 1000000};
				nanosleep(&ts, NULL);
			}
		}

		sleep(1);
	}

	close(lux_fd);
	close(gpiochip_config_fd);
	close(gpiochip_fd);

	return 0;
}
