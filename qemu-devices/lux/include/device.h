#pragma once

#include "libvfio-user.h"
#include <stdint.h>
#include <sys/poll.h>

#include "hw.h"

struct lux_silicon {
	// F0 (PCIe function 0) state
	struct {
		vfu_ctx_t *f0_ctx;
		const char *f0_sock_path;
		int f0_sock_fd;

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

		struct {
			uint32_t irq_status; // Bit 0 = timer
			uint32_t irq_mask;

			uint32_t timer_ctrl;
			uint64_t timer_val;
			uint64_t timer_cmp;
		};
	};

	// Shared state
};

int device_init(struct lux_silicon *restrict device, const char *f0_sock_path, const char *f1_sock_path);

uint64_t device_get_led_states(struct lux_silicon *restrict device);
int device_set_led_states(struct lux_silicon *restrict device, uint64_t states);
int device_clr_led_states(struct lux_silicon *restrict device, uint64_t states);
int device_set_led_color(struct lux_silicon *restrict device,
						 int32_t led_id, uint8_t color[4]);
int device_get_led_color(struct lux_silicon *restrict device,
						 int32_t led_id, uint64_t *color);

int device_run_eventloop(struct lux_silicon *restrict dev);

int device_handle_vfu_events(vfu_ctx_t *restrict ctx, struct pollfd *pfd, const char *name);

void device_destroy(struct lux_silicon *restrict device);
