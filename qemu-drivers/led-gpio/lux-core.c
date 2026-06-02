#include "asm-generic/pci_iomap.h"
#include "linux/array_size.h"
#include "linux/dev_printk.h"
#include "linux/device.h"
#include "linux/device/devres.h"
#include "linux/err.h"
#include "linux/ioport.h"
#include "linux/mutex.h"
#include "linux/platform_device.h"
#include "linux/printk.h"
#include "linux/types.h"
#include <linux/mfd/core.h>
#include <linux/module.h>
#include <linux/pci.h>

#include "../../qemu-devices/lux/include/hw.h"
#include "lux.h"

static int lux_request_f0_pci_bars(struct device *dev,
								   struct resource *bar0,
								   struct resource *bar1) {

	struct resource *parent0, *parent1;
	struct resource *resources = devm_kzalloc(dev, sizeof(struct resource) * 4, GFP_KERNEL);
	if (!resources) {
		dev_err(dev, "Failed to allocate memory for resource structs\n");
		return -ENOMEM;
	}

	parent0 = devm_request_mem_region(dev, bar0->start, resource_size(bar0), LUX_BUS_NAME "-gpio");
	if (!parent0)
		return -EBUSY;

	parent1 = devm_request_mem_region(dev, bar1->start, resource_size(bar1), LUX_BUS_NAME "-smart");
	if (!parent1)
		return -EBUSY;

	resources[0] = (struct resource){
		.name = "magic",
		.start = bar0->start + REG_MAGIC,
		.end = bar0->start + REG_MAGIC + 3,
		.flags = IORESOURCE_MEM,
	};
	resources[1] = (struct resource){
		.name = "version",
		.start = bar0->start + REG_VERSION,
		.end = bar0->start + REG_VERSION + 3,
		.flags = IORESOURCE_MEM,
	};
	resources[2] = (struct resource){
		.name = "gpio-ctrl",
		.start = bar0->start + REG_DIRECTION,
		.end = bar0->start + REG_DIRECTION + 8 * 3,
		.flags = IORESOURCE_MEM,
	};
	resources[3] = (struct resource){
		.name = "smart-led",
		.start = bar1->start,
		.end = bar1->start + sizeof(struct smart_led) * LED_NR - 1,
		.flags = IORESOURCE_MEM,
	};

	if (devm_request_resource(dev, parent0, resources) < 0) {
		dev_err(dev, "Failed to claim magic register\n");
		return -EBUSY;
	};

	if (devm_request_resource(dev, parent0, resources + 1) < 0) {
		dev_err(dev, "Failed to claim version register\n");
		return -EBUSY;
	};

	if (devm_request_resource(dev, parent0, resources + 2) < 0) {
		dev_err(dev, "Failed to claim gpio registers\n");
		return -EBUSY;
	};

	if (devm_request_resource(dev, parent1, resources + 3) < 0) {
		dev_err(dev, "Failed to claim smart-led registers\n");
		return -EBUSY;
	};

	return 0;
}

static int lux_init_f0_device(struct lux_function *device) {
	int ret;
	struct pci_dev *pdev = device->pdev;

	ret = pcim_enable_device(pdev);
	if (ret < 0) {
		dev_err(&pdev->dev, "failed to enable PCI device");
		return ret;
	}

	ret = lux_request_f0_pci_bars(&pdev->dev, pci_resource_n(pdev, 0),
								  pci_resource_n(pdev, 1));
	if (ret < 0) {
		dev_err(&pdev->dev, "Failed to request PCI BARs\n");
		return ret;
	}

	device->bar[0] = pcim_iomap(pdev, 0, 0);
	if (IS_ERR(device->bar[0])) {
		dev_err(&pdev->dev, "failed to map BAR 0 into kernel virtual address space");
		return PTR_ERR(device->bar[0]); // Likey virtual space is exhausted
	}

	device->bar[1] = pcim_iomap(pdev, 1, 0);
	if (IS_ERR(device->bar[1])) {
		dev_err(&pdev->dev, "Failed to map BAR 1 into kernel virtual address space");
		return PTR_ERR(device->bar[1]);
	}

	pci_set_drvdata(pdev, device);

	dev_info(&pdev->dev, "device probed and registered");

	return 0;
}

static int lux_request_f1_pci_bars(struct device *dev,
								   struct resource *bar0) {

	struct resource *parent;
	struct resource *resources = devm_kzalloc(dev, sizeof(struct resource) * 4, GFP_KERNEL);
	if (!resources) {
		dev_err(dev, "Failed to allocate memory for resource structs\n");
		return -ENOMEM;
	}

	parent = devm_request_mem_region(dev, bar0->start, resource_size(bar0), LUX_BUS_NAME "-irq");
	if (!parent)
		return -EBUSY;

	const resource_size_t irq_start = bar0->start + REG_IRQ_STATUS;
	const resource_size_t irq_end = irq_start + 4 * 3 - 1;

	const resource_size_t timer_start = irq_end + 1;
	const resource_size_t timer_end = timer_start + (4 * 1) + (8 * 2) - 1;

	resources[0] = (struct resource){
		.name = "lux-irq",
		.start = irq_start,
		.end = irq_end,
		.flags = resource_type(bar0),
	};
	resources[1] = (struct resource){
		.name = "lux-timer",
		.start = timer_start,
		.end = timer_end,
		.flags = resource_type(bar0),
	};

	if (devm_request_resource(dev, parent, resources) < 0) {
		dev_err(dev, "Failed to claim IRQ chip registers\n");
		return -EBUSY;
	};

	if (devm_request_resource(dev, parent, resources + 1) < 0) {
		dev_err(dev, "Failed to claim timer chip registers\n");
		return -EBUSY;
	};

	return 0;
}

static int lux_init_f1_device(struct lux_function *device) {
	int ret;
	struct pci_dev *pdev = device->pdev;

	ret = pcim_enable_device(pdev);
	if (ret < 0) {
		dev_err(&pdev->dev, "failed to enable PCI device");
		return ret;
	}

	ret = lux_request_f1_pci_bars(&pdev->dev, &pdev->resource[0]);
	if (ret < 0) {
		dev_err(&pdev->dev, "Failed to request PCI BARs\n");
		return ret;
	}

	device->bar[0] = pcim_iomap(pdev, 0, 0);
	if (IS_ERR(device->bar[0])) {
		dev_err(&pdev->dev, "Failed to map BAR 1 into kernel virtual address space");
		return PTR_ERR(device->bar[0]); // Likey virtual space is exhausted
	}

	pci_set_drvdata(pdev, device);

	dev_info(&pdev->dev, "Device probed and registered");

	return 0;
}

static inline int lux_function_init(struct lux_function *device,
									struct pci_dev *pdev, const struct pci_device_id *id) {

	if (id->device == F0_DEVICE_ID) {
		device->dev_id = LUX_F0_DEV_ID;
		device->pdev = pdev;
		if (lux_init_f0_device(device) < 0)
			return -1;
	} else if (id->device == F1_DEVICE_ID) {
		device->dev_id = LUX_F1_DEV_ID;
		device->pdev = pdev;
		if (lux_init_f1_device(device) < 0)
			return -1;
	} else { // This should catch a painful bug :|
		pr_err(LUX_BUS_NAME ": Unkown device (PCI device ID: 0x%x)", id->device);
		return -ENODEV;
	}

	return 0;
}

static struct mfd_cell lux_f0_cells[] = {
	{
		.name = LUX_CHIP_LABEL,
		.id = PLATFORM_DEVID_AUTO,
	},
	{
		.name = LUX_CHAR_DRIVER_NAME,
		.id = PLATFORM_DEVID_AUTO,
	},
};

static struct mfd_cell lux_f1_cells[] = {
	{
		.name = LUX_IRQ_DRIVER_NAME,
		.id = PLATFORM_DEVID_AUTO,
	},
	{
		.name = LUX_TIMER_DRIVER_NAME,
		.id = PLATFORM_DEVID_AUTO,
	},
};

static int lux_pci_probe(struct pci_dev *pdev, const struct pci_device_id *id) {
	int ret;

	// 1. Create and initialize device structure
	struct lux_function *device = devm_kzalloc(&pdev->dev,
											   sizeof(struct lux_function), GFP_KERNEL);
	if (IS_ERR_OR_NULL(device))
		return -ENOMEM;

	ret = lux_function_init(device, pdev, id);
	if (ret < 0) {
		dev_err(&pdev->dev, ": Bus failed to initialize lux device\n");
		goto cleanup;
	}

	// 2. Spawn MFD device
	if (device->dev_id == LUX_F0_DEV_ID) {
		ret = devm_mfd_add_devices(&pdev->dev, PLATFORM_DEVID_AUTO,
								   lux_f0_cells, ARRAY_SIZE(lux_f0_cells),
								   NULL, 0, NULL);

		if (ret < 0) {
			dev_err(&pdev->dev, "Failed to add F1 MFD devices.\n");
			goto cleanup;
		}
	} else if (device->dev_id == LUX_F1_DEV_ID) {
		ret = devm_mfd_add_devices(&pdev->dev, PLATFORM_DEVID_AUTO,
								   lux_f1_cells, ARRAY_SIZE(lux_f1_cells),
								   NULL, 0, NULL);
		if (ret < 0) {
			dev_err(&pdev->dev, "Failed to add F1 MFD devices.\n");
			goto cleanup;
		}
	} else {
		ret = -ENODEV;
		goto cleanup;
	}

	return 0;

cleanup:

	return ret;
}

static const struct pci_device_id lux_id_table[] = {
	{PCI_DEVICE(VENDOR_ID, F0_DEVICE_ID)},
	{PCI_DEVICE(VENDOR_ID, F1_DEVICE_ID)},
	{0},
};
MODULE_DEVICE_TABLE(pci, lux_id_table);

struct pci_driver lux_pci_driver = {
	.name = LUX_BUS_NAME,
	.id_table = lux_id_table,
	.probe = lux_pci_probe,
	// .remove = lux_pci_remove, // devm_ handles all cleanup for now
};

module_pci_driver(lux_pci_driver);

MODULE_AUTHOR("m0st4fa");
MODULE_DESCRIPTION("PCI functionality for LED grid module");
MODULE_LICENSE("GPL");
