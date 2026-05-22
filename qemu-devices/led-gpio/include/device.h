#pragma once

#include "libvfio-user.h"
#include <stdint.h>

#include "hw.h"

struct led_grid_device {
	vfu_ctx_t *vfu_ctx;
	const char *socket_path;
	int sock_fd;

	// Device state (Think: hardware registers, but abstracted)
	struct { // Logical encapsulation, outside access would not reference a member
		/* Direction register used for BAR0
		 * 1 bit per LED.
		 * */
		uint64_t direction;
		struct smart_led leds[LED_NR]; // Full LED state (LED register file)
	};
};

int device_init(struct led_grid_device *restrict device, const char *socket_path);

uint64_t device_get_led_states(struct led_grid_device *restrict device);
int device_set_led_states(struct led_grid_device *restrict device, uint64_t states);
int device_clr_led_states(struct led_grid_device *restrict device, uint64_t states);
int device_set_led_color(struct led_grid_device *restrict device,
						 int32_t led_id, uint8_t color[4]);
int device_get_led_color(struct led_grid_device *restrict device,
						 int32_t led_id, uint64_t *color);

int device_run_eventloop(struct led_grid_device *restrict dev);

void device_destroy(struct led_grid_device *restrict device);
