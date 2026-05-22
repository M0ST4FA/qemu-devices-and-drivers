#include <stdio.h>
#include <string.h>

#include "bar.h"
#include "device.h"
#include "hw.h"
#include "libvfio-user.h"
#include "logger.h"

static inline ssize_t bar1_read(struct led_grid_device *device,
								char *const buf, size_t count, loff_t offset) {
	if (count == BAR1_REG_SIZE * 2) { // Writing both registers at once
		int led_id = offset / sizeof(struct smart_led);
		struct smart_led *led = &device->leds[led_id];
		memcpy(buf, led, BAR1_REG_SIZE * 2);

		pr_log("debug", "op: READ LED, state: %d, color: %x",
			   led->state, *(uint32_t *)led->color);

	} else if (count == BAR1_REG_SIZE) {
		int led_id = offset / sizeof(struct smart_led);
		int reg_offset = offset % BAR1_REG_SIZE;
		struct smart_led *led = &device->leds[led_id];

		if (reg_offset == REG_STATE) {
			*(int32_t *)buf = led->state;
			pr_log("debug", "op: READ STATE, state: %x", led->state);
		}
		if (reg_offset == REG_COLOR) {
			*(int32_t *)buf = *(int32_t *)led->color;
			pr_log("debug", "op: READ COLOR, color: %x", *(int32_t *)led->color);
		}
	} else
		return -1;

	return count;
}

static inline int bar1_write_smart_led(struct led_grid_device *device,
									   char *const buf, loff_t offset) {
	struct smart_led led;
	memcpy(&led, buf, BAR1_REG_SIZE * 2);
	int led_id = offset / sizeof(struct smart_led);

	int32_t led_reg_state = led.state;
	int64_t mask = (led_reg_state & 1ULL) << led_id;

	if ((led_reg_state & 1) == 0) {
		if (device_clr_led_states(device, mask) < 0) {
			pr_log("error", "Failed to set the state of led %d to %x",
				   led_id, led_reg_state);
			return -1;
		} else
			pr_log("debug", "op: WRITE LED, state: %x", led_reg_state);
	}

	if ((led_reg_state & 1) == 1) {
		if (device_set_led_states(device, mask) < 0) {
			pr_log("error", "Failed to set the state of led %d to %x",
				   led_id, led_reg_state);
			return -1;
		} else
			pr_log("debug", "op: WRITE LED, state: %x", led_reg_state);
	}

	if (device_set_led_color(device, led_id, (uint8_t *)buf) < 0) {
		pr_log("error", "Failed to set the color of led %d to %lx",
			   led_id, *(int64_t *)buf);
		return -1;
	} else
		pr_log("debug", "op: WRITE LED, color: %x", *(uint32_t *)led.color);

	return 0;
}

static inline int bar1_write_smart_led_component(struct led_grid_device *device,
												 char *const buf, loff_t offset) {
	// 1. Get LED id and register offset
	int led_id = offset / sizeof(struct smart_led);
	int reg_offset = offset % BAR1_REG_SIZE;

	// 2. Set values
	if (reg_offset == REG_STATE) {
		int32_t led_reg_state = *(uint32_t *)buf;
		int64_t mask = (led_reg_state & 1ULL) << led_id;

		if ((led_reg_state & 1) == 0) {
			if (device_clr_led_states(device, mask) < 0) {
				pr_log("error", "Failed to set the state of led %d to %lx",
					   led_id, mask);
				return -1;
			} else
				pr_log("debug", "op: WRITE STATE, state: %x", led_reg_state);
		}

		if ((led_reg_state & 1) == 1) {
			if (device_set_led_states(device, mask) < 0) {
				pr_log("error", "Failed to set the state of led %d to %lx",
					   led_id, mask);
				return -1;
			} else
				pr_log("debug", "op: WRITE STATE, state: %x", led_reg_state);
		}
	}

	if (reg_offset == REG_COLOR) {
		if (device_set_led_color(device, led_id, (uint8_t *)buf) < 0) {
			pr_log("error", "Failed to set the color of led %d to %lx",
				   led_id, *(int64_t *)buf);
			return -1;
		} else
			pr_log("debug", "op: WRITE COLOR, color: %x", *(uint32_t *)buf);
	}

	return 0;
}

static inline ssize_t bar1_write(struct led_grid_device *device,
								 char *const buf, size_t count, loff_t offset) {
	int ret = -1;

	switch (count) {
		case BAR1_REG_SIZE * 2:
			ret = bar1_write_smart_led(device, buf, offset);
			break;

		case BAR1_REG_SIZE:
			ret = bar1_write_smart_led_component(device, buf, offset);
			break;

		default:
			pr_log("error", "Writing to unkown register");
			// ret is already set to -1
	}

	if (ret < 0)
		return -1;
	else
		return count;
}

ssize_t bar1_access(vfu_ctx_t *vfu_ctx, char *const buf, size_t count, loff_t offset, const bool is_write) {
	struct led_grid_device *device = vfu_get_private(vfu_ctx);

	if (is_write)
		return bar1_write(device, buf, count, offset);
	else
		return bar1_read(device, buf, count, offset);

	return 0;
}
