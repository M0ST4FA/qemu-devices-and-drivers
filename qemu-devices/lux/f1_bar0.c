#include <stdio.h>
#include <string.h>

#include "bar.h"
#include "device.h"
#include "hw.h"
#include "libvfio-user.h"
#include "logger.h"

static inline ssize_t f1_bar0_read(struct lux_silicon *device,
								   char *const buf, size_t count, loff_t offset) {

	switch ((enum f1_bar0_regs)offset) {
		case REG_IRQ_STATUS:
		case REG_IRQ_MASK:
		case REG_IRQ_ACK:
		case REG_TIMER_CTRL:
		case REG_TIMER_TIME:
		case REG_TIMER_CMP:
			break;
	}

	if (count == 4)
		pr_log("debug", "op: READ, reg: %s, val: %b", f0_reg_names[offset], *(uint32_t *)buf);
	else if (count == 8)
		pr_log("debug", "op: READ, reg: %s, val: %lb", f0_reg_names[offset], *(uint64_t *)buf);

	return count;
}

static inline ssize_t f1_bar0_write(struct lux_silicon *device,
									char *const buf, size_t count, loff_t offset) {

	switch ((enum f1_bar0_regs)offset) {
		case REG_IRQ_STATUS:
		case REG_IRQ_MASK:
		case REG_IRQ_ACK:
		case REG_TIMER_CTRL:
		case REG_TIMER_TIME:
		case REG_TIMER_CMP:
			break;
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
