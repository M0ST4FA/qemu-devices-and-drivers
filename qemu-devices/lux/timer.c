#include <bits/time.h>
#include <time.h>

#include "device.h"
#include "hw.h"

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

	if (TIMER_IRQ_ENABLED(device) && TIMER_TRIGGER_VAL_REACHED(device)) {
		// Trigger irq line with chip
		device->irq_status |= (1U << HWIRQ_TIMER);

		// Handle autoreloading
		if (TIMER_RELOAD_ENABLED(device))
			device->timer_val = 0;
		else
			device->timer_ctrl &= ~IRQ_BIT;
	}

	return 0;
}
