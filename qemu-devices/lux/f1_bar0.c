#include <stdio.h>
#include <string.h>

#include "bar.h"
#include "device.h"
#include "hw.h"
#include "libvfio-user.h"
#include "logger.h"

static inline ssize_t f1_bar0_read(struct lux_silicon *device,
								   char *const buf, size_t count, loff_t offset) {

	bool reading_from_irq = offset <= REG_IRQ_ACK;

	if (reading_from_irq && count != 4)
		return -1;

	switch ((enum f1_bar0_regs)offset) {
		case REG_IRQ_STATUS:
			*(uint32_t *)buf = device->irq_status;
			break;
		case REG_IRQ_MASK:
			*(uint32_t *)buf = device->irq_mask;
			break;

			// WO
		case REG_IRQ_ACK:
			return -1;

		case REG_TIMER_CTRL:
			if (count != 4)
				return -1;
			*(uint32_t *)buf = device->timer_ctrl;
			break;

		case REG_TIMER_TIME:
			if (count != 8)
				return -1;
			*(uint64_t *)buf = device->timer_val;
			break;

		case REG_TIMER_CMP:
			if (count != 8)
				return -1;
			*(uint64_t *)buf = device->timer_cmp;
			break;

		default:
			pr_log("error", "Reading from unkown register (offset: %ld)\n", offset);
			return -1;
	}

	if (count == 4)
		pr_log("debug", "op: READ, reg: %s, val: %b", f1_reg_names[offset], *(uint32_t *)buf);
	else if (count == 8)
		pr_log("debug", "op: READ, reg: %s, val: %lb", f1_reg_names[offset], *(uint64_t *)buf);

	return count;
}

static inline ssize_t f1_bar0_write(struct lux_silicon *device,
									char *const buf, size_t count, loff_t offset) {
	bool reading_from_irq = offset <= REG_IRQ_ACK;

	if (reading_from_irq && count != 4)
		return -1;

	switch ((enum f1_bar0_regs)offset) {
		// RO
		case REG_IRQ_STATUS:
			return -1;
		case REG_IRQ_MASK:
			device->irq_mask = *(uint32_t *)buf;
			break;

		case REG_IRQ_ACK: {
			uint32_t to_be_acked = *(uint32_t *)buf;
			device->irq_status &= ~to_be_acked;
		} break;

		case REG_TIMER_CTRL:
			if (count != 4)
				return -1;
			device->timer_ctrl = *(uint32_t *)buf;
			break;

			// RO
		case REG_TIMER_TIME:
			return -1;

		case REG_TIMER_CMP:
			if (count != 8)
				return -1;
			device->timer_cmp = *(uint64_t *)buf;

			break;

		default:
			pr_log("error", "Writing into unkown register (offset: %ld)\n", offset);
			return -1;
	}

	if (count == 4)
		pr_log("debug", "op: WRITE, reg: %s, val: %b", f1_reg_names[offset], *(uint32_t *)buf);
	else if (count == 8)
		pr_log("debug", "op: WRITE, reg: %s, val: %lb", f1_reg_names[offset], *(uint64_t *)buf);

	return count;
}

ssize_t f1_bar0_access(vfu_ctx_t *vfu_ctx, char *const buf,
					   size_t count, loff_t offset, const bool is_write) {
	struct lux_silicon *device = vfu_get_private(vfu_ctx);

	if (is_write)
		return f1_bar0_write(device, buf, count, offset);
	else
		return f1_bar0_read(device, buf, count, offset);

	return 0;
}
