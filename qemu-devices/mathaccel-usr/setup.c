#include "setup.h"
#include "bar.h"
#include "common.h"
#include "dma.h"
#include "fsm.h"
#include "libvfio-user.h"
#include <err.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static struct msicap msi_cap = {
	.hdr.id = PCI_CAP_ID_MSI,
	.hdr.next = 0,
	.mc = {
		.msie = 0, // MSI Enable; start disabled
		.mmc = 0,  // Multi Messages Capable, 0 = 1, 2 = 2, 2 = 4
		.c64 = 1,  // 64-bit addresses
		.pvm = 0,  // Per-vector masking
	},
};

static int on_device_reset(vfu_ctx_t *ctx, [[maybe_unused]] enum vfu_reset_type type) {

	switch (type) {
		case VFU_RESET_PCI_FLR:
			printf("[HW] FLR received, resetting device\n");
			break;
		case VFU_RESET_LOST_CONN:
			printf("[HW] Client disconnected, resetting device\n");
			break;
		case VFU_RESET_DEVICE:
			printf("[HW] Client requested reset, resetting device\n");
			break;
		default:
			break;
	}

	int ret = fsm_dispatch(ctx, EVT_RESET); // STATE_ANY => STATE_RESET
	if (ret < 0)
		return ret;

	return fsm_dispatch(ctx, EVT_INIT); // STATE_RESET => STATE_READY
}

void setup_bars_and_irqs(struct vfu_ctx *vfu_ctx) {
	int ret = 0;

	// 1. Setup BAR 0
	ret = vfu_setup_region(vfu_ctx, VFU_PCI_DEV_BAR0_REGION_IDX,
						   4096, &bar0_access,
						   VFU_REGION_FLAG_RW | VFU_REGION_FLAG_MEM | VFU_REGION_FLAG_64_BITS,
						   NULL, 0,
						   -1, 0);
	if (ret < 0)
		err(EXIT_FAILURE, "%s\n", "Failed to setup BAR0");

	// 2. Setup Interrupts (MSI)
	ret = vfu_setup_device_nr_irqs(vfu_ctx, VFU_DEV_MSI_IRQ, 1);

	if (ret < 0)
		err(EXIT_FAILURE, "%s\n", "Failed to setup IRQs");

	// 3. Setup FLR handler for reset logic (to support hotplugging)
	ret = vfu_setup_device_reset_cb(vfu_ctx, on_device_reset);
	if (ret < 0)
		err(EXIT_FAILURE, "%s\n", "Failed to setup FLR handler");
};

void setup_capabilities(struct vfu_ctx *vfu_ctx) {
	int ret = 0;

	ret = vfu_pci_add_capability(vfu_ctx, 0, 0, &msi_cap);
	if (ret < 0)
		err(EXIT_FAILURE, "%s\n", "Failed to setup capabilities");
}

void realize_and_connect(struct vfu_ctx *vfu_ctx) {
	int ret = 0;
	// 5. Finalize the configuration and initialize the device
	ret = vfu_realize_ctx(vfu_ctx);
	fsm_dispatch(vfu_ctx, EVT_INIT); // initial RESET => READY

	// 6. Wait for QEMU to connect
	// Connection loop; can be turned into a while loop but I'm too lazy
connect:
	printf("[HW] Waiting for QEMU to plug in the device...\n");
	ret = vfu_attach_ctx(vfu_ctx);
	if (ret < 0)
		err(EXIT_FAILURE, "%s\n", "Failed to attach");

	printf("[HW] System powered on! Running event loop.\n");

	// 7. The main hardware loop
	ret = vfu_run_ctx(vfu_ctx);
	if (ret < 0) {
		if (errno == ENOTCONN) { // Client closed the connection
			printf("[HW] QEMU disconnected, reseting the device...\n");

			// Reset math_device, you don't want stale buffers from previous interaction (would lead to disastrous results)
			fsm_dispatch(vfu_ctx, EVT_RESET);
			fsm_dispatch(vfu_ctx, EVT_INIT);

			goto connect; // Try to reconnect again; alternatively, you can ctrl+C (SIGINT) the program and reexec it
		}

		vfu_destroy_ctx(vfu_ctx);
		err(EXIT_FAILURE, "%s: %s\n", "vfu_run_ctx", strerror(errno));
	}
}
