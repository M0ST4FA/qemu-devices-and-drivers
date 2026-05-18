#include <errno.h>
#include <unistd.h>

#include "bar.h"
#include "device.h"
#include "libvfio-user.h"
#include "logger.h"

static inline int device_setup_ctx_and_header(struct led_grid_device *device, const char *socket_path) {
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

static inline int device_setup_regions(struct led_grid_device *device) {
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

static inline int device_setup_irqs(struct led_grid_device *device, int count) {
	int ret = 0;
	vfu_ctx_t *ctx = device->vfu_ctx;

	ret = vfu_setup_device_nr_irqs(ctx, VFU_DEV_MSI_IRQ, count);
	if (ret < 0) {
		pr_log_libcerror(errno, "vfu_setup_device_nr_irqs");
		return -1;
	}

	return 0;
}

static inline int device_setup_capabilities(struct led_grid_device *device) {

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

int device_init(struct led_grid_device *device, const char *socket_path) {
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

	return 0;

cleanup:
	device_destroy(device);
	return -1;
};

int device_run_eventloop(struct led_grid_device *device) {

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
