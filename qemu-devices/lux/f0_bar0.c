#include <stdio.h>
#include <string.h>

#include "bar.h"
#include "device.h"
#include "hw.h"
#include "libvfio-user.h"
#include "logger.h"

static inline ssize_t f0_bar0_read(struct lux_silicon *device,
								   char *const buf, size_t count, loff_t offset) {

	switch ((enum f0_bar0_regs)offset) {
		case REG_MAGIC:
			if (count != 4)
				return -1;
			*(uint32_t *)buf = MAGIC;
			break;

		case REG_VERSION:
			if (count != 4)
				return -1;
			*(uint32_t *)buf = VERSION;
			break;

		case REG_DIRECTION:
			if (count != 8)
				return -1;
			memcpy(buf, &device->direction, 8);
			break;

			// pin states = LED states (it is virtualized hardware, so pins are just virtual)
		case REG_DATA:
			if (count != 8)
				return -1;
			uint64_t led_states = device_get_led_states(device);
			memcpy(buf, &led_states, 8);
			break;

		case REG_LED_CTRL:
			if (count != 4)
				return -1;
			*(uint32_t *)buf = device->led_ctrl;
			break;

		case REG_LED_IRQ_CAUSE:
			if (count != 4)
				return -1;
			*(uint32_t *)buf = device->led_irq_cause;
			device->led_irq_cause = 0; // Clear on read
			break;

			// WO registers
		case REG_SET:
		case REG_CLR:
			return -1;

		default:
			pr_log("error", "Reading from unkown register\n");
			return -1;
	}

	if (count == 4)
		pr_log("debug", "op: READ, reg: %s, val: %b", f0_reg_names[offset], *(uint32_t *)buf);
	else if (count == 8)
		pr_log("debug", "op: READ, reg: %s, val: %lb", f0_reg_names[offset], *(uint64_t *)buf);

	return count;
}

static inline ssize_t f0_bar0_write(struct lux_silicon *device,
									char *const buf, size_t count, loff_t offset) {

	switch ((enum f0_bar0_regs)offset) {
		// RO registers
		case REG_MAGIC:
		case REG_VERSION:
		case REG_LED_IRQ_CAUSE:
			return -1;

		case REG_DIRECTION:
			if (count == 4) {
				memcpy(&device->direction, buf, 4);
			} else if (count == 8) {
				memcpy(&device->direction, buf, 8);
			} else {
				return -1;
			}
			break;

		case REG_DATA:
			if (count != 8)
				return -1;
			{
				uint64_t states = *(uint64_t *)buf;
				uint64_t changed_leds = states ^ device_get_led_states(device);

				device_set_led_states(device, states & changed_leds);
				device_clr_led_states(device, ~states & changed_leds);
			}
			break;

			// WO registers
		case REG_SET:
			if (count != 8)
				return -1;
			{
				uint64_t mask = *(uint64_t *)buf;
				uint64_t current_states = device_get_led_states(device);
				// We only care about bits that are 1 in the mask AND currently 0
				uint64_t to_set = mask & ~current_states;

				device_set_led_states(device, to_set);
			}
			break;

		case REG_CLR:
			if (count != 8)
				return -1;
			{
				uint64_t mask = *(uint64_t *)buf;
				uint64_t current_states = device_get_led_states(device);
				// We only care about bits that are 1 in the mask AND currently 1
				uint64_t to_clr = mask & current_states;

				device_clr_led_states(device, to_clr);
			}
			break;

		case REG_LED_CTRL:
			if (count != 4)
				return -1;
			device->led_ctrl = *(uint32_t *)buf;
			break;

		default:
			pr_log("error", "Writing into unkown register (offset: %ld)\n", offset);
			return -1;
	}

	if (count == 4)
		pr_log("debug", "op: WRITE, reg: %s, val: %b", f0_reg_names[offset], *(uint32_t *)buf);
	else if (count == 8)
		pr_log("debug", "op: WRITE, reg: %s, val: %lb", f0_reg_names[offset], *(uint64_t *)buf);

	return count;
}

ssize_t f0_bar0_access(vfu_ctx_t *vfu_ctx, char *const buf,
					   size_t count, loff_t offset, const bool is_write) {
	struct lux_silicon *device = vfu_get_private(vfu_ctx);

	if (is_write)
		return f0_bar0_write(device, buf, count, offset);
	else
		return f0_bar0_read(device, buf, count, offset);

	return 0;
}
