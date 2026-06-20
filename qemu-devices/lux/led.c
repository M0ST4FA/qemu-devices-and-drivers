#include <asm-generic/errno.h>
#include <assert.h>
#include <err.h>
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include "../led/include/protocol.h"
#include "device.h"
#include "hw.h"
#include "logger.h"

int device_connect_to_led_grid(struct lux_silicon *restrict device) {
	int ret = 0;

	device->f0_sock_fd = socket(PF_UNIX, SOCK_STREAM | SOCK_NONBLOCK, 0);
	if (device->f0_sock_fd < 0) {
		pr_log_libcerror(errno, "socket");
		return -1;
	}

	struct sockaddr_un addr = {
		.sun_family = AF_UNIX,
		.sun_path = "\0" SERVER_SOCKET_NAME,
	};
	ret = connect(device->f0_sock_fd, (void *)&addr, sizeof(addr));
	if (ret < 0) {
		pr_log_libcerror(errno, "connect");
		return -1;
	}

	return 0;
}

uint64_t device_get_led_states(struct lux_silicon *restrict device) {
	uint64_t states = 0; // 1 bit per led, in-order

	for (int i = 0; i < LED_NR; i++) {
		if (i >= 64) {
			pr_log("debug",
				   "LED states is overflowing! We use a uint64_t, one bit per lid, for state. It seems that there are more than 64 LEDs");
			break; // Make hardware robust, to not break software
		}

		struct smart_led *led = &device->leds[i];

		const bool state = led->state & 1; // This should exclude each bit except bit 0
		const uint64_t mask = ((uint64_t)state << i);
		states |= mask;
	}

	return states;
};
int device_set_led_states(struct lux_silicon *restrict device, uint64_t states) {
	uint64_t direction = device->direction;
	int count = 0; // Number of leds we've set
	struct led_command cmd = {0};

	for (int i = 0; i < LED_NR; i++) {
		// 1. Some necessary checks

		if (!((states >> i) & 0x1)) // Not writing to this pin
			continue;
		else
			pr_log("debug", "Setting pin %d", i);

		if (((direction >> i) & 0x1) != LUX_DIRECTION_OUT) { // Not set for output
			pr_log("error", "Trying to write to a GPIO pin marked for reading");
			continue;
		}

		// 2. Update register GPIO controller register state
		device->leds[i].state = 1; // Set bit 0 to 1

		// 3. Send command to LED controller (this is just the GPIO controller)
		cmd = (struct led_command){
			.cmd = CMD_ON,
			.led_id = i,
			.color = {0}, // If you don't do this, you'll send whatever garbage was on the stack in its place
		};
		write(device->f0_sock_fd, &cmd, sizeof(cmd));
		// Notice how protocol is async, which is nice. Loop doesn't have to block.
		// Okay, it may block, but only in case socket write buffer is full.
		// But we don't have to wait for a reply from server
		count++;
	}

	return count;
}
int device_clr_led_states(struct lux_silicon *restrict device, uint64_t states) {
	uint64_t direction = device->direction;
	int count = 0; // Number of leds we've set
	struct led_command cmd = {0};

	for (int i = 0; i < LED_NR; i++) {
		// 1. Some necessary checks

		if (!((states >> i) & 0x1)) // Not writing to this pin
			continue;
		else
			pr_log("debug", "Clearing pin %d", i);

		if (((direction >> i) & 0x1) != LUX_DIRECTION_OUT) {
			pr_log("error", "Trying to write to a GPIO pin marked for reading");
			continue;
		}

		// 2. Update register GPIO controller register state
		device->leds[i].state = 0; // Set bit 0 to 0

		// 3. Send command to LED controller (this is just the GPIO controller)
		cmd = (struct led_command){
			.cmd = CMD_OFF,
			.led_id = i,
		};
		write(device->f0_sock_fd, &cmd, sizeof(cmd));
		// Notice how protocol is async, which is nice. Loop doesn't have to block.
		// Okay, it may block, but only in case socket write buffer is full.
		// But we don't have to wait for a reply from server
		count++;
	}

	return count;
}

int device_set_led_color(struct lux_silicon *restrict device,
						 int32_t led_id, uint8_t color[4]) {
	struct led_command cmd = {
		.cmd = CMD_SET_COLOR,
		.led_id = led_id,
		.color = {color[0], color[1], color[2], color[3]},
	};

	struct smart_led *led = &device->leds[led_id];
	memcpy(led->color, color, 4);

	int ret = write(device->f0_sock_fd, &cmd, sizeof(cmd));
	if (ret < 0) {
		pr_log_libcerror(errno, "write(device_set_led_color)");
		return ret;
	}

	return 0;
}

int device_get_led_color(struct lux_silicon *restrict device,
						 int32_t led_id, uint64_t *color) {
	struct smart_led *led = &device->leds[led_id];

	*color = *led->color;

	return 0;
}

int device_handle_led_protocol_events(struct lux_silicon *restrict device) {
	int fd = device->f0_sock_fd;
	struct led_event ev = {0};
	int n = read(fd, &ev, sizeof(ev));

	if (n < 0) {
		pr_log_libcerror(errno, "read: Couldn't read LED event");
		return -1;
	}

	if (n != sizeof(ev)) {
		pr_log("error", "Failed to read LED event...read only part of it");
		return -1;
	}

	switch (ev.ev) {
		case LEV_ON:
			device->led_irq_cause |= LUX_IRQ_LED_ON;
			break;

		case LEV_OFF:
			device->led_irq_cause |= LUX_IRQ_LED_OFF;
			break;

		case LEV_COLOR:
			device->led_irq_cause |= LUX_IRQ_LED_COLOR;
			break;

		case LEV_ERR:
			switch (ev.error_code) {
				case LERR_UNKNOWN_CMD:
					device->led_irq_cause |= LUX_IRQ_LED_ERR_UNKNOWN_CMD;
					break;
			}
			break;

		default:
			errx(EXIT_FAILURE, "Unhandled LED event\n");
	}

	if (device->led_ctrl & LED_CTRL_IRQ_EN) {
		if (ev.ev != LEV_ERR)
			device->irq_status |= (1 << HWIRQ_LED);
		else
			device->irq_status |= (1 << HWIRQ_LED_ERR);
	}

	return 0;
};
