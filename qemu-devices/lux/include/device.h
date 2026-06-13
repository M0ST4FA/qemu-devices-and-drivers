#pragma once

#include "libvfio-user.h"
#include <errno.h>
#include <stdint.h>
#include <sys/poll.h>
#include <time.h>

#include "hw.h"
#include "logger.h"

struct lux_silicon {
	// F0 (PCIe function 0) state
	struct {
		vfu_ctx_t *f0_ctx;
		const char *f0_sock_path;
		int f0_sock_fd;
		bool f0_is_connected;

		// Device state (Think: hardware registers, but abstracted)
		struct { // Logical encapsulation, outside access would not reference a member
			/* Direction register used for BAR0
			 * 1 bit per LED.
			 * */
			uint64_t direction;
			struct smart_led leds[LED_NR]; // Full LED state (LED register file)
		};
	};

	// F1 state
	struct {
		vfu_ctx_t *f1_ctx;
		const char *f1_sock_path;
		int f1_sock_fd;
		bool f1_is_connected;

		struct {
			uint32_t irq_status; // Bit 0 = timer
			uint32_t irq_mask;

			uint32_t timer_ctrl;
			uint64_t timer_val;
			uint64_t timer_cmp;

			uint64_t last_tick_ns; /* Host process time at previous tick.
									  The difference between it and host time at current tick is elapsed time. */
			uint64_t last_delta_ns;
		};
	};

	// Shared state
};

// GENERAL
int device_init(struct lux_silicon *restrict device, const char *f0_sock_path, const char *f1_sock_path);
int device_run_eventloop(struct lux_silicon *restrict dev);
int device_handle_vfu_events(vfu_ctx_t *restrict ctx, struct pollfd *pfd, const char *name, bool *is_connected);
void device_destroy(struct lux_silicon *restrict device);

// LED
uint64_t device_get_led_states(struct lux_silicon *restrict device);
int device_connect_to_led_grid(struct lux_silicon *restrict device);
int device_set_led_states(struct lux_silicon *restrict device, uint64_t states);
int device_clr_led_states(struct lux_silicon *restrict device, uint64_t states);
int device_set_led_color(struct lux_silicon *restrict device,
						 int32_t led_id, uint8_t color[4]);
int device_get_led_color(struct lux_silicon *restrict device,
						 int32_t led_id, uint64_t *color);

// TIMER

#define TIMER_ENABLED(device) (device->timer_ctrl & TIMER_BIT)
#define TIMER_IRQ_ENABLED(device) (device->timer_ctrl & IRQ_BIT)
#define TIMER_RELOAD_ENABLED(device) (device->timer_ctrl & RELOAD_BIT)
#define TIMER_TRIGGER_VAL_REACHED(device) (device->timer_val >= device->timer_cmp)

static inline int device_timer_init(struct lux_silicon *restrict device) {
	int ret = 0;

	struct timespec ts;
	ret = clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
	if (ret < 0) {
		pr_log_libcerror(errno, "clock_gettime");
		return ret;
	}
	device->last_tick_ns = (ts.tv_sec * LUX_TIMER_RATE) + (ts.tv_nsec);

	return 0;
}
int device_timer_tick(struct lux_silicon *restrict device);
int device_irq_tick(struct lux_silicon *restrict device);
