#include <asm-generic/errno.h>
#include <errno.h>
#include <stdint.h>
#include <string.h>
#include <sys/poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include "../led/include/protocol.h"
#include "bar.h"
#include "device.h"
#include "hw.h"
#include "libvfio-user.h"
#include "logger.h"

static inline vfu_ctx_t *device_setup_function(const char *restrict sock_path, void *restrict private, int dev_id) {
	int ret = 0;
	vfu_ctx_t *vfu_ctx = NULL;

	// 1. Setup context
	pr_log("debug", "Initializing function...socket: %s", sock_path);
	vfu_ctx = vfu_create_ctx(VFU_TRANS_SOCK, sock_path,
							 LIBVFIO_USER_FLAG_ATTACH_NB, private, VFU_DEV_TYPE_PCI);

	if (vfu_ctx == NULL) {
		pr_log_libcerror(errno, "vfu_create_ctx");
		return NULL;
	}

	ret = vfu_pci_init(vfu_ctx, VFU_PCI_TYPE_EXPRESS,
					   PCI_HEADER_TYPE_NORMAL, 0);
	if (ret < 0) {
		pr_log_libcerror(errno, "vfu_pci_init");
		return NULL;
	}

	// 2. Setup header
	vfu_pci_set_id(vfu_ctx, VENDOR_ID, dev_id, 0, 0);
	vfu_pci_set_class(vfu_ctx, CLASS_BASE_ID, CLASS_SUB_ID, CLASS_PI_ID);

	return vfu_ctx;
}

static inline int device_setup_ctx_and_header(
	struct lux_silicon *restrict device,
	const char *restrict f0_sock_path,
	const char *restrict f1_sock_path) {

	device->f0_sock_path = f0_sock_path;
	device->f1_sock_path = f1_sock_path;

	device->f0_ctx = device_setup_function(device->f0_sock_path, device, F0_DEVICE_ID);
	if (device->f0_ctx == NULL) {
		pr_log("error", "Failed to initialize function 0\n");
		return -1;
	};

	device->f1_ctx = device_setup_function(device->f1_sock_path, device, F1_DEVICE_ID);
	if (device->f1_ctx == NULL) {
		pr_log("error", "Failed to initialize function 1\n");
		return -1;
	};

	return 0;
}

static inline int device_setup_regions(struct lux_silicon *restrict device) {
	int ret = 0;
	vfu_ctx_t *f0_ctx = device->f0_ctx;
	int f0_region_flags = VFU_REGION_FLAG_MEM | VFU_REGION_FLAG_RW;

	vfu_ctx_t *f1_ctx = device->f1_ctx;
	int f1_region_flags = VFU_REGION_FLAG_MEM | VFU_REGION_FLAG_RW;

	// function 0 BAR0
	ret = vfu_setup_region(f0_ctx, VFU_PCI_DEV_BAR0_REGION_IDX,
						   F0_BAR0_REGION_SIZE, &f0_bar0_access,
						   f0_region_flags,
						   NULL, 0, -1, 0);
	if (ret < 0) {
		pr_log_libcerror(errno, "vfu_setup_region(bar0)");
		return -1;
	}

	// function 0 BAR1
	ret = vfu_setup_region(f0_ctx, VFU_PCI_DEV_BAR1_REGION_IDX,
						   F0_BAR1_REGION_SIZE, &f0_bar1_access,
						   f0_region_flags,
						   NULL, 0, -1, 0);
	if (ret < 0) {
		pr_log_libcerror(errno, "vfu_setup_region(bar1)");
		return -1;
	}

	// function 1 BAR0
	ret = vfu_setup_region(f1_ctx, VFU_PCI_DEV_BAR0_REGION_IDX,
						   F1_BAR0_REGION_SIZE, &f1_bar0_access,
						   f1_region_flags,
						   NULL, 0, -1, 0);

	return 0;
}

static inline int device_setup_irqs(vfu_ctx_t *ctx, int count) {
	int ret = 0;

	ret = vfu_setup_device_nr_irqs(ctx, VFU_DEV_MSI_IRQ, count);
	if (ret < 0) {
		pr_log_libcerror(errno, "vfu_setup_device_nr_irqs");
		return -1;
	}

	return 0;
}

static inline int device_setup_capabilities(struct lux_silicon *restrict device) {

	return 0;
}

static inline int device_connect_to_led_grid(struct lux_silicon *restrict device) {
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

static int on_device_reset(vfu_ctx_t *ctx,
						   [[maybe_unused]] enum vfu_reset_type type) {
}

static void on_dma_register([[maybe_unused]] vfu_ctx_t *ctx,
							[[maybe_unused]] vfu_dma_info_t *dma_info) {
}

static void on_dma_unregister([[maybe_unused]] vfu_ctx_t *ctx,
							  [[maybe_unused]] vfu_dma_info_t *dma_info) {};

int device_init(struct lux_silicon *restrict device, const char *f0_sock_path, const char *f1_sock_path) {
	int ret;

	// 1. Create context
	// 2. Setup the basic PCI header
	ret = device_setup_ctx_and_header(device, f0_sock_path, f1_sock_path);
	if (ret < 0)
		goto cleanup;

	// 3. Setup regions
	ret = device_setup_regions(device);
	if (ret < 0)
		goto cleanup;

	// 4. Setup interrupts
	ret = device_setup_irqs(device->f1_ctx, 1);
	if (ret < 0) {
		goto cleanup;
	}

	// 5. Setup device reset
	ret = vfu_setup_device_reset_cb(device->f0_ctx, on_device_reset);
	if (ret < 0) {
		pr_log_libcerror(errno, "vfu_setup_device_reset_cb(f0)");
		goto cleanup;
	}

	// 6. Setup DMA
	ret = vfu_setup_device_dma(device->f0_ctx, MAX_DMA_REGIONS,
							   on_dma_register, on_dma_unregister);
	if (ret < 0) {
		pr_log_libcerror(errno, "vfu_setup_device_dma");
		goto cleanup;
	}

	// 7. Setup capabilites
	ret = device_setup_capabilities(device);
	if (ret < 0)
		goto cleanup;

	// 8. Realize device
	ret = vfu_realize_ctx(device->f0_ctx);
	if (ret < 0) {
		pr_log_libcerror(errno, "vfu_realize_ctx(f0)");
		goto cleanup;
	}
	ret = vfu_realize_ctx(device->f1_ctx);
	if (ret < 0) {
		pr_log_libcerror(errno, "vfu_realize_ctx(f1)");
		goto cleanup;
	}

	// 9. Connect to LED device
	ret = device_connect_to_led_grid(device);
	if (ret < 0)
		goto cleanup;

	return 0;

cleanup:
	device_destroy(device);
	return -1;
};

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

inline int device_set_led_color(struct lux_silicon *restrict device,
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

inline int device_get_led_color(struct lux_silicon *restrict device,
								int32_t led_id, uint64_t *color) {
	struct smart_led *led = &device->leds[led_id];

	*color = *led->color;

	return 0;
}

inline int device_handle_vfu_events(vfu_ctx_t *restrict ctx, struct pollfd *pfd, const char *name) {
	int ret = 0;

	if (!(pfd->revents & POLLIN)) // No event to handle
		return 0;

	ret = vfu_run_ctx(ctx);

	if (ret < 0) {
		if (errno == EAGAIN || errno == EWOULDBLOCK)
			return 0;

		if (errno == ENOTCONN) {
			pr_log("debug", "%s disconnected, waiting for new connection...", name);
			if (vfu_attach_ctx(ctx) < 0) {
				if (errno == EAGAIN || errno == EWOULDBLOCK)
					return 0;

				pr_log("error", "vfu_attach_ctx(%s): %s", name, strerror(errno));
				return -1;
			};
			pfd->fd = vfu_get_poll_fd(ctx);
		} else {
			pr_log("error", "vfu_run_ctx(%s): %s", name, strerror(errno));
			return -1;
		}
	}

	return 0;
};

inline int device_run_eventloop(struct lux_silicon *device) {
	int ret;

	if (vfu_attach_ctx(device->f0_ctx) < 0) {
		if (!(errno == EWOULDBLOCK || errno == EAGAIN)) {
			pr_log_libcerror(errno, "vfu_attach_ctx(f0)");
			return -1;
		}
	}

	if (vfu_attach_ctx(device->f1_ctx) < 0) {
		if (!(errno == EWOULDBLOCK || errno == EAGAIN)) {
			pr_log_libcerror(errno, "vfu_attach_ctx(f1)");
			return -1;
		}
	}

	struct pollfd fds[3] = {
		(struct pollfd){
			.fd = vfu_get_poll_fd(device->f0_ctx),
			.events = POLLIN,
			.revents = 0,
		},
		(struct pollfd){
			.fd = vfu_get_poll_fd(device->f1_ctx),
			.events = POLLIN,
			.revents = 0,
		},
		(struct pollfd){
			.fd = device->f0_sock_fd,
			.events = POLLIN,
			.revents = 0,
		},
	};

	while (1) {
		ret = poll(fds, sizeof(fds) / sizeof(fds[0]), 10);
		if (ret < 0 && errno != EINTR) {
			pr_log_libcerror(errno, "poll");
			return -1;
		}

		// 1. Hardware clock tick

		if (ret == 0 || (ret < 0 && errno == EINTR))
			continue;

		// 2. Handle vfio-user command
		if (device_handle_vfu_events(device->f0_ctx, &fds[0], "f0") < 0)
			return -1;

		if (device_handle_vfu_events(device->f1_ctx, &fds[1], "f1") < 0)
			return -1;

		// 3. Handle LED device
		// We can only read disconnections as we're the producer of the socket
		if (fds[2].revents & (POLLERR | POLLHUP)) {
			pr_log("error", "LED device disconnected. Shutting down GPIO server...");
			return 0; // Not a vfu error, so main() should not treat it as an error state
		}
	}

	return 0;
}

void device_destroy(struct lux_silicon *device) {
	if (device->f0_ctx != NULL) {
		vfu_destroy_ctx(device->f0_ctx);
		device->f0_ctx = NULL;
	}

	if (device->f0_sock_fd > 0) {
		close(device->f0_sock_fd);
		device->f0_sock_fd = 0;
	}

	if (device->f0_sock_path) {
		unlink(device->f0_sock_path);
		device->f0_sock_path = NULL;
	}

	if (device->f1_ctx != NULL) {
		vfu_destroy_ctx(device->f1_ctx);
		device->f1_ctx = NULL;
	}

	if (device->f1_sock_fd > 0) {
		close(device->f1_sock_fd);
		device->f1_sock_fd = 0;
	}

	if (device->f1_sock_path) {
		unlink(device->f1_sock_path);
		device->f1_sock_path = NULL;
	}
};
