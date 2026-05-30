#include "asm-generic/pci_iomap.h"
#include "linux/dev_printk.h"
#include "linux/device.h"
#include "linux/device/devres.h"
#include "linux/err.h"
#include "linux/export.h"
#include "linux/ioport.h"
#include <linux/module.h>
#include <linux/pci.h>

#include "../../qemu-devices/lux/include/hw.h"
#include "linux/list.h"
#include "linux/mutex.h"
#include "linux/printk.h"
#include "linux/slab.h"
#include "lux.h"

static const struct pci_device_id lux_id_table[] = {
	{PCI_DEVICE(VENDOR_ID, F0_DEVICE_ID)},
	{PCI_DEVICE(VENDOR_ID, F1_DEVICE_ID)},
	{0},
};
MODULE_DEVICE_TABLE(pci, lux_id_table);

static LIST_HEAD(lux_device_list);	// List of PCI devices (encapsulated in struct lux_device) of known IDs
static LIST_HEAD(lux_driver_list);	// List of registered drivers
static DEFINE_MUTEX(lux_bus_mutex); // Big Bus Lock (BBL)

int lux_register_driver(struct lux_driver *restrict driver) {
	struct lux_device *device;

	mutex_lock(&lux_bus_mutex);

	// 1. Add the driver to our permenant list
	list_add_tail(&lux_driver_list, &driver->node);

	// 2. Try matching the driver against any existing device
	list_for_each_entry(device, &lux_device_list, node) {
		if (device->driver) // Device is already matched to a driver
			continue;

		if (driver->supported_dev_id != device->dev_id)
			continue;

		if (driver->probe) {
			if (driver->probe(device) < 0)
				pr_alert(LUX_BUS_NAME ": Probe failed (dev_id: %d)\n", device->dev_id);
			else
				device->driver = driver;
		}
	};

	mutex_unlock(&lux_bus_mutex);

	return 0;
}

void lux_unregister_driver(struct lux_driver *restrict driver) {
	struct lux_device *device = NULL;

	mutex_lock(&lux_bus_mutex);

	// 1. Notice devices that the driver is being removed
	list_for_each_entry(device, &lux_device_list, node) {
		if (device->driver != driver) // Device not matched to this driver
			continue;

		driver->remove(device);
		device->driver = NULL;
	}

	// 2. Remove driver
	list_del(&driver->node); // We don't need to deallocate anything; driver storage is managed by module (consumer).

	mutex_unlock(&lux_bus_mutex);
}

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

static int lux_init_f0_device(struct lux_device *device) {
	int ret;
	struct pci_dev *pdev = device->pdev;

	ret = pcim_enable_device(pdev);
	if (ret < 0) {
		dev_err(&pdev->dev, "failed to enable PCI device");
		return ret;
	}

	ret = lux_request_f0_pci_bars(&pdev->dev, &pdev->resource[0], &pdev->resource[1]);
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

	// TODO: Think about these; maybe they are useful
	// resources[0] = (struct resource){
	// 	.name = "magic",
	// 	.start = bar0->start + REG_MAGIC,
	// 	.end = bar0->start + REG_MAGIC + 3,
	// 	.flags = IORESOURCE_MEM,
	// };
	// resources[1] = (struct resource){
	// 	.name = "version",
	// 	.start = bar0->start + REG_VERSION,
	// 	.end = bar0->start + REG_VERSION + 3,
	// 	.flags = IORESOURCE_MEM,
	// };

	// TODO: Needs extensive modification
	resources[0] = (struct resource){
		.name = "lux-irq",
		.start = bar0->start + REG_DIRECTION,
		.end = bar0->start + REG_DIRECTION + 8 * 3,
		.flags = IORESOURCE_MEM,
	};
	resources[1] = (struct resource){
		.name = "lux-timer",
		.start = bar0->start,
		.end = bar0->start + sizeof(struct smart_led) * LED_NR - 1,
		.flags = IORESOURCE_MEM,
	};

	if (devm_request_resource(dev, parent, resources) < 0) {
		dev_err(dev, "Failed to claim magic register\n");
		return -EBUSY;
	};

	if (devm_request_resource(dev, parent, resources + 1) < 0) {
		dev_err(dev, "Failed to claim version register\n");
		return -EBUSY;
	};

	return 0;
}

static int lux_init_f1_device(struct lux_device *device) {
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

static inline int lux_device_init(struct lux_device *device,
								  struct pci_dev *pdev, const struct pci_device_id *id) {

	if (id->device == F0_DEVICE_ID) {
		device->dev_id = LUX_F0_DEV_ID;
		device->pdev = pdev;
		if (lux_init_f0_device(device) < 0)
			return -1;
	} else if (id->device == F1_DEVICE_ID) {
		device->dev_id = LUX_F1_DEV_ID;
		device->pdev = pdev;
		// if (lux_init_f1_device(device) < 0)
		// 	return -1;
	} else { // This should catch a painful bug :|
		pr_err(LUX_BUS_NAME ": Unkown device (PCI device ID: %d)", id->device);
		return -1;
	}

	return 0;
}

static inline void lux_device_destroy(struct lux_device *device) { // Does nothing for now
}

static int lux_pci_probe(struct pci_dev *pdev, const struct pci_device_id *id) {
	int ret;

	// 1. Create and initialize device structure
	struct lux_device *device = kzalloc(sizeof(struct lux_device), GFP_KERNEL);
	if (IS_ERR_OR_NULL(device))
		return -ENOMEM;

	ret = lux_device_init(device, pdev, id);
	if (ret < 0) {
		dev_err(&pdev->dev, ": Bus failed to initialize lux device\n");
		goto cleanup;
	}
	list_add_tail(&device->node, &lux_device_list);

	// 2. Probe for any matching driver
	struct lux_driver *driver = NULL;

	mutex_lock(&lux_bus_mutex);

	list_for_each_entry(driver, &lux_driver_list, node) {
		if (driver->supported_dev_id != device->dev_id)
			continue;

		pr_info(LUX_BUS_NAME ": Probing new device (id: %d) with matching driver\n", device->dev_id);
		if (driver->probe)
			if (driver->probe(device) < 0)
				pr_alert(LUX_BUS_NAME ": Probe failed (dev_id: %d)\n", device->dev_id);
	};

	mutex_unlock(&lux_bus_mutex);

	return 0;

cleanup:

	if (device != NULL) {
		lux_device_destroy(device);
		kfree(device);
		device = NULL;
	}

	return ret;
}

static void lux_pci_remove([[maybe_unused]] struct pci_dev *pdev) {
	// 1. Tell all drivers their device is vanishing

	struct lux_device *device = pci_get_drvdata(pdev);

	device->driver->remove(device);
	list_del(&device->node);
	lux_device_destroy(device);
	kfree(device);
}

struct pci_driver lux_pci_driver = {
	.name = LUX_BUS_NAME,
	.id_table = lux_id_table,
	.probe = lux_pci_probe,
	.remove = lux_pci_remove,
};

EXPORT_SYMBOL_GPL(lux_register_driver);
EXPORT_SYMBOL_GPL(lux_unregister_driver);

module_pci_driver(lux_pci_driver);
MODULE_AUTHOR("m0st4fa");
MODULE_DESCRIPTION("PCI functionality for LED grid module");
MODULE_LICENSE("GPL");
