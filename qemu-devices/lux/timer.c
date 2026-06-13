#include <bits/time.h>
#include <time.h>

#include "device.h"
#include "hw.h"

int device_timer_tick(struct lux_silicon *restrict device) {

	if (!TIMER_ENABLED(device))
		return 0;

	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
	uint64_t now_ns = (ts.tv_sec * LUX_TIMER_RATE) + (ts.tv_nsec);
	uint64_t elapsed = now_ns - device->last_tick_ns;

	// This tick is the past tick next tick
	device->last_tick_ns = now_ns;

	device->timer_val += elapsed;

	if (TIMER_IRQ_ENABLED(device) && TIMER_TRIGGER_VAL_REACHED(device)) {
		// Trigger irq line with chip
		device->irq_status |= (1U << HWIRQ_TIMER);

		// Handle autoreloading
		if (TIMER_RELOAD_ENABLED(device))
			device->timer_cmp += device->last_delta_ns;
		else
			device->timer_ctrl &= ~IRQ_BIT;
	}

	return 0;
}
