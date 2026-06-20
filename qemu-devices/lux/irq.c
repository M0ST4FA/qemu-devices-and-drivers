#include <bits/time.h>
#include <errno.h>
#include <strings.h>

#include "device.h"
#include "libvfio-user.h"
#include "logger.h"

int device_irq_tick(struct lux_silicon *restrict device) {
	uint32_t pending = device->irq_status & ~device->irq_mask;

	// We clear pending only on ack
	for (int i = 0; i < HWIRQ_COUNT; i++) {
		uint32_t on = pending & (1 << i);

		if (on) {
			if (vfu_irq_trigger(device->f1_ctx, i) < 0)
				pr_log_libcerror(errno, "vfu_irq_trigger");
			else
				pr_log("debug", "Fired IRQ <%d>!", i);
		}
	}

	return 0;
}
