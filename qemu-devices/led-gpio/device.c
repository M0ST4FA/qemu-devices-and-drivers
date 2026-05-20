#include <errno.h>
#include <stdint.h>
#include <sys/poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include "../led/include/protocol.h"
#include "bar.h"
#include "device.h"
#include "libvfio-user.h"
#include "logger.h"

static inline int device_setup_ctx_and_header(
	struct led_grid_device *restrict device,
	const char *restrict socket_path) {
	int ret;

	// 1. Setup context
	pr_log("debug", "Initializing...socket: %s", socket_path);
	device->vfu_ctx = vfu_create_ctx(VFU_TRANS_SOCK, socket_path,
									 0, device, VFU_DEV_TYPE_PCI);

	if (device->vfu_ctx == NULL) {
		pr_log_libcerror(errno, "vfu_create_ctx");
		return -1;
	}
	device->socket_path = socket_path;

	ret = vfu_pci_init(device->vfu_ctx, VFU_PCI_TYPE_EXPRESS,
					   PCI_HEADER_TYPE_NORMAL, 0);
	if (ret < 0) {
		pr_log_libcerror(errno, "vfu_pci_init");
		return -1;
	}

	// 2. Setup header
	vfu_pci_set_id(device->vfu_ctx, VENDOR_ID, DEVICE_ID, 0, 0);
	vfu_pci_set_class(device->vfu_ctx, CLASS_BASE_ID, CLASS_SUB_ID, CLASS_PI_ID);

	return 0;
}

static inline int device_setup_regions(struct led_grid_device *restrict device) {
	int ret = 0;
	vfu_ctx_t *ctx = device->vfu_ctx;
	int region_flags = VFU_REGION_FLAG_MEM | VFU_REGION_FLAG_RW;

	// BAR0
	ret = vfu_setup_region(ctx, VFU_PCI_DEV_BAR0_REGION_IDX,
						   BAR0_REGION_SIZE, &bar0_access,
						   region_flags,
						   NULL, 0, -1, 0);
	if (ret < 0) {
		pr_log_libcerror(errno, "vfu_setup_region(bar0)");
		return -1;
	}

	// BAR1
	ret = vfu_setup_region(ctx, VFU_PCI_DEV_BAR1_REGION_IDX,
						   BAR1_REGION_SIZE, &bar1_access,
						   region_flags,
						   NULL, 0, -1, 0);
	if (ret < 0) {
		pr_log_libcerror(errno, "vfu_setup_region(bar1)");
		return -1;
	}

	return 0;
}

static inline int device_setup_irqs(struct led_grid_device *restrict device, int count) {
	int ret = 0;
	vfu_ctx_t *ctx = device->vfu_ctx;

	ret = vfu_setup_device_nr_irqs(ctx, VFU_DEV_MSI_IRQ, count);
	if (ret < 0) {
		pr_log_libcerror(errno, "vfu_setup_device_nr_irqs");
		return -1;
	}

	return 0;
}

static inline int device_setup_capabilities(struct led_grid_device *restrict device) {

	return 0;
}

static inline int device_connect_to_led_grid(struct led_grid_device *restrict device) {
	int ret = 0;

	device->sock_fd = socket(PF_UNIX, SOCK_STREAM | SOCK_NONBLOCK, 0);
	if (device->sock_fd < 0) {
		pr_log_libcerror(errno, "socket");
		return -1;
	}

	struct sockaddr_un addr = {
		.sun_family = AF_UNIX,
		.sun_path = "\0" SERVER_SOCKET_NAME,
	};
	ret = connect(device->sock_fd, (void *)&addr, sizeof(addr));
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

int device_init(struct led_grid_device *restrict device, const char *socket_path) {
	int ret;

	// 1. Create context
	// 2. Setup the basic PCI header
	ret = device_setup_ctx_and_header(device, socket_path);
	if (ret < 0)
		goto cleanup;

	// 3. Setup regions
	ret = device_setup_regions(device);
	if (ret < 0)
		goto cleanup;

	// 4. Setup interrupts
	ret = device_setup_irqs(device, 1);
	if (ret < 0)
		goto cleanup;

	// 5. Setup device reset
	ret = vfu_setup_device_reset_cb(device->vfu_ctx, on_device_reset);
	if (ret < 0) {
		pr_log_libcerror(errno, "vfu_setup_device_reset_cb");
		goto cleanup;
	}

	// 6. Setup DMA
	ret = vfu_setup_device_dma(device->vfu_ctx, MAX_DMA_REGIONS,
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
	ret = vfu_realize_ctx(device->vfu_ctx);
	if (ret < 0) {
		pr_log_libcerror(errno, "vfu_realize_ctx");
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

uint64_t device_get_led_states(struct led_grid_device *restrict device) {
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
int device_set_led_states(struct led_grid_device *restrict device, uint64_t states) {
	uint64_t direction = device->direction;
	int count = 0; // Number of leds we've set
	struct led_command cmd = {0};

	for (int i = 0; i < LED_NR; i++) {
		// 1. Some necessary checks

		if (!((direction >> i) & 0x1)) {
			pr_log("error", "Trying to write to a GPIO pin marked for reading");
			continue;
		}

		if (!((states >> i) & 0x1))
			continue;

		// 2. Update register GPIO controller register state
		device->leds[i].state = 1; // Set bit 0 to 1

		// 3. Send command to LED controller (this is just the GPIO controller)
		cmd = (struct led_command){
			.cmd = CMD_ON,
			.led_id = i,
			.color = {0}, // If you don't do this, you'll send whatever garbage was on the stack in its place
		};
		write(device->sock_fd, &cmd, sizeof(cmd));
		// Notice how protocol is async, which is nice. Loop doesn't have to block.
		// Okay, it may block, but only in case socket write buffer is full.
		// But we don't have to wait for a reply from server
		count++;
	}

	return count;
}
int device_clr_led_states(struct led_grid_device *restrict device, uint64_t states) {
	uint64_t direction = device->direction;
	int count = 0; // Number of leds we've set
	struct led_command cmd = {0};

	for (int i = 0; i < LED_NR; i++) {
		// 1. Some necessary checks

		if (!((direction >> i) & 0x1)) {
			pr_log("error", "Trying to write to a GPIO pin marked for reading");
			continue;
		}

		if (!((states >> i) & 0x1))
			continue;

		// 2. Update register GPIO controller register state
		device->leds[i].state = 0; // Set bit 0 to 0

		// 3. Send command to LED controller (this is just the GPIO controller)
		cmd = (struct led_command){
			.cmd = CMD_OFF,
			.led_id = i,
		};
		write(device->sock_fd, &cmd, sizeof(cmd));
		// Notice how protocol is async, which is nice. Loop doesn't have to block.
		// Okay, it may block, but only in case socket write buffer is full.
		// But we don't have to wait for a reply from server
		count++;
	}

	return count;
}

int device_run_eventloop(struct led_grid_device *device) {
	int ret;

	if (vfu_attach_ctx(device->vfu_ctx) < 0) {
		pr_log_libcerror(errno, "vfu_attach_ctx");
		return -1;
	}

	struct pollfd fds[2] = {
		(struct pollfd){
			.fd = vfu_get_poll_fd(device->vfu_ctx),
			.events = POLLIN,
			.revents = 0,
		},
		(struct pollfd){
			.fd = device->sock_fd,
			.events = POLLIN,
			.revents = 0,
		},
	};

	while (1) {
		ret = poll(fds, 2, -1);
		if (ret == 0)
			continue;

		if (ret < 0) {
			if (errno == EINTR) {
				pr_log("debug", "Poll returned after being interrupted...repolling");
				continue;
			}

			pr_log_libcerror(errno, "poll");
			return -1;
		}

		// 1. Handle vfio-user command
		if (fds[0].revents & POLLIN) {
			ret = vfu_run_ctx(device->vfu_ctx);

			if (ret < 0) {
				if (errno == ENOTCONN) {
					pr_log("debug", "Kernel client disconnected, waiting for new one...");
					if (vfu_attach_ctx(device->vfu_ctx) < 0) {
						pr_log_libcerror(errno, "vfu_attach_ctx");
						return -1;
					};
					fds[0].fd = vfu_get_poll_fd(device->vfu_ctx);
				} else {
					pr_log_libcerror(errno, "vfu_run_ctx");
					return -1;
				}
			}
		}

		// 2. Handle LED device
		// We can only read disconnections as we're the producer of the socket
		if (fds[1].revents & (POLLERR | POLLHUP)) {
			pr_log("error", "LED device disconnected. Shutting down GPIO server...");
			return 0; // Not a vfu error, so main() should not treat it as an error state
		}
	}

	return 0;
}

void device_destroy(struct led_grid_device *device) {
	if (device->vfu_ctx != NULL)
		vfu_destroy_ctx(device->vfu_ctx);

	if (device->sock_fd > 0)
		close(device->sock_fd);

	if (device->socket_path)
		unlink(device->socket_path);
};
