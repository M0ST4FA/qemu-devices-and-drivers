#include <bits/time.h>
#include <time.h>

#include "device.h"

int device_timer_tick(struct lux_silicon *restrict device) {

	if (!TIMER_ENABLED(device))
		return 0;

	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	uint64_t now_ms = (ts.tv_sec * 1000) + (ts.tv_nsec / (1000 * 1000));
	uint64_t elapsed = now_ms - device->last_tick_ms;

	// This tick is the past tick next tick
	device->last_tick_ms = now_ms;

	device->timer_val += elapsed;

	vfu_irq_trigger(device->f1_ctx, 0);

	return 0;
}
