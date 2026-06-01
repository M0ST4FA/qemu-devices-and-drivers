#include <bits/time.h>
#include <err.h>
#include <fcntl.h>
#include <linux/gpio.h>
#include <math.h>
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
#define MAX_SAMPLES (LED_NR * 128) // Enough for 128 characters
#define WAIT_BETWEEN_PIXELS_NS (10000000)
#define WAIT_BETWEEN_CHARS_NS (20000000)

static inline void print_usage_exit(void) {
	printf("Usage: print-to-led-pwrite <string>\n");
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

static inline long timespec_diff_ns(struct timespec *start, struct timespec *end) {
	return (end->tv_sec - start->tv_sec) * 1000000000L +
		   (end->tv_nsec - start->tv_nsec);
}

static void print_stats(long *samples, int count) {
	if (count == 0)
		return;

	long min = samples[0], max = samples[0], sum = 0;
	for (int i = 0; i < count; i++) {
		if (samples[i] < min)
			min = samples[i];
		if (samples[i] > max)
			max = samples[i];
		sum += samples[i];
	}
	double avg = (double)sum / count;

	double variance = 0;
	for (int i = 0; i < count; i++) {
		double diff = (double)samples[i] - avg;
		variance += diff * diff;
	}
	variance /= count;
	double stddev = sqrt(variance);

	printf("\n=== pwrite() Profiling Results ===\n");
	printf("  Samples : %d writes\n", count);
	printf("  Min     : %ld ns\n", min);
	printf("  Max     : %ld ns\n", max);
	printf("  Avg     : %.1f ns\n", avg);
	printf("  Stddev  : %.1f ns  (jitter)\n", stddev);
	printf("  Total   : %ld ns (%.3f ms)\n", sum, (double)sum / 1000000.0);
}

int main(int argc, char *argv[]) {
	int lux_fd;

	if (argc != 2)
		print_usage_exit();

	const char *str = argv[1];
	size_t str_len = strlen(str);

	struct smart_led led;
	long latencies[MAX_SAMPLES];
	int sample_count = 0;

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

				struct timespec t1, t2;
				clock_gettime(CLOCK_MONOTONIC, &t1);
				pwrite(lux_fd, &led, sizeof(led), led_id * sizeof(led));
				clock_gettime(CLOCK_MONOTONIC, &t2);

				if (sample_count < MAX_SAMPLES)
					latencies[sample_count++] = timespec_diff_ns(&t1, &t2);

				// TODO: This is a hack. When poll() is added, replace it.
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

	print_stats(latencies, sample_count);

	return 0;
}
