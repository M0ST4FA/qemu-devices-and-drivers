#include <stdio.h>
#include <string.h>

#include "bar.h"
#include "device.h"
#include "libvfio-user.h"

static inline ssize_t bar0_read(struct led_grid_device *device,
								char *const buf, size_t count, loff_t offset) {

	switch ((enum bar0_regs)offset) {
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

			// WO registers
		case REG_SET:
		case REG_CLR:
			break;
	}

	return count;
}

static inline ssize_t bar0_write(struct led_grid_device *device,
								 char *const buf, size_t count, loff_t offset) {

	switch ((enum bar0_regs)offset) {
		// RO registers
		case REG_MAGIC:
		case REG_VERSION:
			break;

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

				device_set_led_states(device, states);
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
	}

	return count;
}

ssize_t bar0_access(vfu_ctx_t *vfu_ctx, char *const buf,
					size_t count, loff_t offset, const bool is_write) {
	struct led_grid_device *device = vfu_get_private(vfu_ctx);

	if (is_write)
		return bar0_write(device, buf, count, offset);
	else
		return bar0_read(device, buf, count, offset);

	return 0;
}
