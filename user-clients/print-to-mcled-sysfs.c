#include <bits/time.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/gpio.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/signal.h>
#include <time.h>
#include <unistd.h>

#include "../qemu-devices/led-gpio/include/hw.h"
#include "font.h"

#define LED_DEFAULT_COLOR "41 51 92"
#define LED_DEFAULT_BRIGHTNESS "255"
#define LED_NAME "rgb:smart"
#define WAIT_BETWEEN_PIXELS_NS (10000000)
#define WAIT_BETWEEN_CHARS_NS (200000000)
#define ALARM_SEC (5)

struct led {
	char led_name[64];
	int brightness;
	int max_brightness;
	int multi_intensity;
};
typedef typeof(struct led[LED_NR]) led_array;

static inline void
print_usage_exit(void) {
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

static int open_sysfs_leds(led_array led_fds) {
	char file_name[64];

	for (int i = 0; i < LED_NR; i++) {
		struct led *led = led_fds + i;
		snprintf(led->led_name, 64, "/sys/class/leds/" LED_NAME "-%d", i);

		// 1. Brightness file
		snprintf(file_name, 64, "/sys/class/leds/" LED_NAME "-%d/brightness", i);
		led->brightness = open(file_name, O_RDWR | O_CLOEXEC);
		if (led->brightness < 0) {
			fprintf(stderr, "open %s: %s\n", file_name, strerror(errno));
			return -1;
		}

		// 2. Maximum brightness file
		snprintf(file_name, 64, "/sys/class/leds/" LED_NAME "-%d/max_brightness", i);
		led->max_brightness = open(file_name, O_RDONLY | O_CLOEXEC);
		if (led->max_brightness < 0) {
			fprintf(stderr, "open %s: %s\n", file_name, strerror(errno));
			return -1;
		}

		// 3. Color file
		snprintf(file_name, 64, "/sys/class/leds/" LED_NAME "-%d/multi_intensity", i);
		led->multi_intensity = open(file_name, O_WRONLY | O_CLOEXEC);
		if (led->multi_intensity < 0) {
			fprintf(stderr, "open %s: %s\n", file_name, strerror(errno));
			return -1;
		}
	}

	return 0;
}

static void close_sysfs_leds(led_array led_fds) {
	for (int i = 0; i < LED_NR; i++) {
		if (close(led_fds[i].brightness) < 0)
			perror("close(led_fd.brightness)");
		if (close(led_fds[i].max_brightness) < 0)
			perror("close(led_fd.max_brightness)");
		if (close(led_fds[i].multi_intensity) < 0)
			perror("close(led_fd.multi_intensity)");
	}
}

static inline void write_led(led_array led_fds, int led_id, bool led_open) {
	int ret = 0;
	char buf[4] = {0};
	int curr_brightness = 0;
	pread(led_fds[led_id].brightness, buf, 4, 0);
	curr_brightness = atoi(buf);

	if (led_open) {
		ret = pwrite(led_fds[led_id].multi_intensity,
					 LED_DEFAULT_COLOR, strlen(LED_DEFAULT_COLOR), 0);
		if (ret < 0)
			perror("write(multi_intensity)");

		ret = pwrite(led_fds[led_id].brightness,
					 LED_DEFAULT_BRIGHTNESS, strlen(LED_DEFAULT_BRIGHTNESS), 0);
		if (ret < 0)
			perror("write(brightness)");

	} else if (curr_brightness != 0) {
		ret = pwrite(led_fds[led_id].brightness, "0", strlen("0"), 0);
		if (ret < 0)
			perror("write(brightness)");
	}

	// If we don't wait, weird artifacts happen. The wait allows synchronization with hardware.
	struct timespec ts = {.tv_sec = 0, .tv_nsec = WAIT_BETWEEN_PIXELS_NS};
	nanosleep(&ts, NULL);
}

static inline void clear_led_grid(led_array led_fds) {
	for (int y = 0; y < ROW_NR; y++) {
		for (int x = 0; x < COL_NR; x++) {
			int led_id = y * 8 + x;

			write_led(led_fds, led_id, false);

			struct timespec ts = {.tv_sec = 0, .tv_nsec = WAIT_BETWEEN_PIXELS_NS};
			nanosleep(&ts, NULL);
		}
	}
}

void generic_signal_handler(int signo) {
	printf("Received signal %d\n", signo);

	if (signo == SIGALRM) {
		printf("Alarm fired! Exiting...\n");
		exit(EXIT_FAILURE);
	}

	if (signo == SIGTERM) {
		printf("Closing in %d seconds\n", ALARM_SEC);
		alarm(ALARM_SEC);
	}

	if (signo == SIGINT) {
		printf("Closing in %d seconds\n", ALARM_SEC);
		alarm(ALARM_SEC);
	}
}

static void register_signal_handlers() {
	struct sigaction sa;
	sa.sa_handler = generic_signal_handler;
	if (sigaction(SIGALRM, &sa, NULL) < 0)
		err(EXIT_FAILURE, "sigaction");

	if (sigaction(SIGTERM, &sa, NULL) < 0)
		err(EXIT_FAILURE, "sigaction");

	if (sigaction(SIGINT, &sa, NULL) < 0)
		err(EXIT_FAILURE, "sigaction");
}

int main(int argc, char *argv[]) {
	int ret;
	led_array led_fds = {0};

	if (argc != 2)
		print_usage_exit();

	register_signal_handlers();
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
			}
		}

		struct timespec ts = {.tv_sec = 0, .tv_nsec = WAIT_BETWEEN_CHARS_NS};
		ret = nanosleep(&ts, NULL);
		if (ret < 0) {
			if (ret == EINTR) {
				printf("Closing in %d seconds\n", ALARM_SEC);

				alarm(ALARM_SEC);
				pause();
			}

			err(EXIT_FAILURE, "nanosleep");
		}
	}

	clear_led_grid(led_fds);
	close_sysfs_leds(led_fds);

	return 0;
}
