#include <bits/time.h>
#include <errno.h>
#include <strings.h>

#include "device.h"
#include "libvfio-user.h"
#include "logger.h"

int device_irq_tick(struct lux_silicon *restrict device) {
	uint32_t pending = device->irq_status & ~device->irq_mask;

	// Edge triggered behavior: Fire and forget
	// We clear pending only on ack
	if (pending)
		if (vfu_irq_trigger(device->f1_ctx, 0) < 0)
			pr_log_libcerror(errno, "vfu_irq_trigger");

	return 0;
}
