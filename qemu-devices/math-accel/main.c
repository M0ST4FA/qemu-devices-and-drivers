#include "libvfio-user.h"
#include "pci_caps/msi.h"
#include <asm-generic/errno.h>
#include <err.h>
#include <errno.h>
#include <linux/pci_regs.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define RECONNECT_MAX 5

static void print_usage_exit(const char *prog_name) {
	printf("Usage: %s <unix_socket_path>\n", prog_name);
	exit(EXIT_FAILURE);
}

struct math_device_state {
	uint32_t data;
	uint32_t cmd;
	uint32_t status;
};

struct msicap msi_cap = {
	.hdr.id = PCI_CAP_ID_MSI,
	.hdr.next = 0,
	.mc = {
		.msie = 0, // MSI Enable; start disabled
		.mmc = 0,  // Multi Messages Capable, 0 = 1, 2 = 2, 2 = 4
		.c64 = 1,  // 64-bit addresses
		.pvm = 0,  // Per-vector masking
	},
};

static ssize_t
bar0_write(struct math_device_state *state, char *const buf, size_t count, loff_t offset) {
	uint32_t val = *((uint32_t *)buf);
	printf("[HW] Write %u to offset 0x%lx\n", val, offset);

	switch (offset) {
		case 0x00:
			state->data = val;
			break;
		case 0x04:
			state->cmd = val;
			if (val == 1) {
				printf("[HW] Executing acceleration operation...\n");
				state->data = state->data * 2;
				state->cmd = 0;	   // means we're ready to take other ops
				state->status = 1; // done
			}
			break;
		default:
			return -1;
	}

	return 0;
}

static ssize_t bar0_read(struct math_device_state *state, char *const buf, size_t count, loff_t offset) {
	uint32_t val = 0;

	switch (offset) {
		case 0x00:
			val = state->data;
			break;

		case 0x08:
			val = state->status;

			// Reading the status register clears interrupt status
			if (state->status == 1) {
				printf("[HW] Driver read status, clearing interrupt\n");
				state->status = 0;
			}
			break;

		default:
			val = 0;
	}

	*((uint32_t *)buf) = val;
	printf("[HW] Read %u from offset 0x%lx\n", val, offset);
	return 0;
}

/*
 * The MMIO callback. Fires each time the Linux guest tries to read or write memory from BAR0.
 * */
static ssize_t bar0_access(vfu_ctx_t *vfu_ctx, char *const buf, size_t count, loff_t offset, const bool is_write) {
	struct math_device_state *state = vfu_get_private(vfu_ctx);

	if (count != 4) {
		// Force the driver to use 4 byte reads/writes
		fprintf(stderr, "[HW]: Non-32-bit access attempted\n");
		return -1;
	}

	if (is_write) {
		if (bar0_write(state, buf, count, offset) == 0) {
			printf("[HW] Firing MSI interrupt!\n");
			sleep(2);					 // Delay for experiment with concurrency chaos
			vfu_irq_trigger(vfu_ctx, 0); // 0 is the first MSI vector
			goto success;
		} else {
			goto error;
		}
	}

	if (bar0_read(state, buf, count, offset) < 0)
		goto error;

success:
	return count;

error:
	return -1;
};

int main(int argc, char *argv[]) {
	int ret, conn_tries;
	struct math_device_state state = {0, 0, 0};

	if (argc != 2)
		print_usage_exit(argv[0]);

	const char *socket_path = argv[1];

	if (access(socket_path, F_OK) == 0) {
		printf("Socket already exists...removing it\n");
		if (unlink(socket_path) < 0)
			err(EXIT_FAILURE, "unlink: %s\n", strerror(errno));
	}

	// 1. Create the VFIO context and the UNIX socket
	printf("[HW] Initializing math-accel on %s\n", socket_path);
	vfu_ctx_t *vfu_ctx = vfu_create_ctx(VFU_TRANS_SOCK, socket_path, 0, &state, VFU_DEV_TYPE_PCI);
	if (vfu_ctx == NULL) {
		if (errno == EINTR) {
			err(EXIT_FAILURE, "%s\n", "Interrupted");
		} else {
			err(EXIT_FAILURE, "%s\n", "Failed to initialize device emulation");
		}
	}

	// 2. Setup the basic PCI header
	ret = vfu_pci_init(vfu_ctx, VFU_PCI_TYPE_CONVENTIONAL, PCI_HEADER_TYPE_NORMAL, 0);
	if (ret < 0)
		err(EXIT_FAILURE, "%s\n", "Failed to initialize PCI");

	vfu_pci_set_id(vfu_ctx, 0x1234, 0x5678, 0, 0);

	// 3. Setup BAR 0
	ret = vfu_setup_region(vfu_ctx, VFU_PCI_DEV_BAR0_REGION_IDX,
						   4096, &bar0_access,
						   VFU_REGION_FLAG_RW, NULL, 0,
						   -1, 0);
	if (ret < 0)
		err(EXIT_FAILURE, "%s\n", "Failed to setup BAR0");

	// 4. Setup Interrupts (MSI)
	ret = vfu_setup_device_nr_irqs(vfu_ctx, VFU_DEV_MSI_IRQ, 1);

	if (ret < 0)
		err(EXIT_FAILURE, "%s\n", "Failed to setup IRQs");

	ret = vfu_pci_add_capability(vfu_ctx, 0, 0, &msi_cap);
	if (ret < 0)
		err(EXIT_FAILURE, "%s\n", "Failed to setup capabilities");

	// 5. Finalize the configuration
	ret = vfu_realize_ctx(vfu_ctx);

	// 6. Wait for QEMU to connect
	conn_tries = 0;
	// Connection loop; can be turned into a while loop but I'm too lazy
connect:
	conn_tries += 1;
	if (conn_tries >= RECONNECT_MAX)
		err(EXIT_FAILURE, "Maximum number of reconnections (%d) reached", RECONNECT_MAX);

	printf("[HW] Waiting for QEMU to plug in the device...\n");
	ret = vfu_attach_ctx(vfu_ctx);
	if (ret < 0)
		err(EXIT_FAILURE, "%s\n", "Failed to attach");

	printf("[HW] System powered on! Running event loop.\n");

	// 7. The main hardware loop
	ret = vfu_run_ctx(vfu_ctx);
	if (ret < 0) {
		if (errno == ENOTCONN) // Client closed the connection
			goto connect;	   // Try to reconnect again; alternatively, you can ctrl+C (SIGINT) the program and reexec it

		vfu_destroy_ctx(vfu_ctx);
		err(EXIT_FAILURE, "%s: %s\n", "vfu_run_ctx", strerror(errno));
	}

	vfu_destroy_ctx(vfu_ctx);

	return 0;
}
