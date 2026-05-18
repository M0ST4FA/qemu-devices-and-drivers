#pragma once

#include "libvfio-user.h"

#define LED_NR 64

#define VENDOR_ID (0x1234) // Private ID
#define DEVICE_ID (0x0001)
#define CLASS_BASE_ID (0x08)
#define CLASS_SUB_ID (0x80)
#define CLASS_PI_ID (0x00)

#define BAR0_REGION_SIZE 4096
#define BAR1_REGION_SIZE 4096

#define MAX_DMA_REGIONS 1024

struct led {
};

struct led_grid_device {
	vfu_ctx_t *vfu_ctx;
	const char *socket_path;
	int sock_fd;
	struct led leds[LED_NR];
};

int device_init(struct led_grid_device *dev, const char *socket_path);

int device_run_eventloop(struct led_grid_device *dev);

void device_destroy(struct led_grid_device *dev);
