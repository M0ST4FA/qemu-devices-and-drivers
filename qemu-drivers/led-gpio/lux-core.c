#include <linux/module.h>
#include <linux/pci.h>

#include "asm-generic/pci_iomap.h"
#include "linux/dev_printk.h"
#include "linux/device/devres.h"
#include "linux/err.h"
#include "linux/export.h"

#include "../../qemu-devices/lux/include/hw.h"
#include "linux/ioport.h"
#include "lux.h"

struct lux_device *global_lux = NULL;

static const struct pci_device_id lux_id_table[] = {
	{PCI_DEVICE(LUX_VENDOR_ID, LUX_DEVICE_ID)},
	{0},
};
MODULE_DEVICE_TABLE(pci, lux_id_table);

static int lux_request_pci_bars(struct device *dev,
								struct resource *bar0,
								struct resource *bar1) {

	struct resource *parent0, *parent1;
	struct resource *resources = devm_kzalloc(dev, sizeof(struct resource) * 4, GFP_KERNEL);
	if (!resources) {
		dev_err(dev, "Failed to allocate memory for resource structs\n");
		return -ENOMEM;
	}

	parent0 = devm_request_mem_region(dev, bar0->start, resource_size(bar0), LUX_CORE_DRIVER_NAME "-gpio");
	if (!parent0)
		return -EBUSY;

	parent1 = devm_request_mem_region(dev, bar1->start, resource_size(bar1), LUX_CORE_DRIVER_NAME "-smart");
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

static int lux_pci_probe(struct pci_dev *pdev, const struct pci_device_id *id_table) {
	int ret = 0;

	struct lux_device *lux_device = devm_kzalloc(&pdev->dev, sizeof(*lux_device), GFP_KERNEL);
	if (lux_device == NULL) {
		dev_err(&pdev->dev, "failed allocate device data structure");
		return -ENOMEM;
	}

	ret = pcim_enable_device(pdev);
	if (ret < 0) {
		dev_err(&pdev->dev, "failed to enable PCI device");
		return ret;
	}

	ret = lux_request_pci_bars(&pdev->dev, &pdev->resource[0], &pdev->resource[1]);
	if (ret < 0) {
		dev_err(&pdev->dev, "Failed to request PCI BARs\n");
		return ret;
	}

	lux_device->bar[0] = pcim_iomap(pdev, 0, 0);
	if (IS_ERR(lux_device->bar[0])) {
		dev_err(&pdev->dev, "failed to map BAR 0 into kernel virtual address space");
		return PTR_ERR(lux_device->bar[0]); // Likey virtual space is exhausted
	}

	lux_device->bar[1] = pcim_iomap(pdev, 1, 0);
	if (IS_ERR(lux_device->bar[1])) {
		dev_err(&pdev->dev, "Failed to map BAR 1 into kernel virtual address space");
		return PTR_ERR(lux_device->bar[1]);
	}

	pci_set_drvdata(pdev, lux_device);
	global_lux = lux_device;

	dev_info(&pdev->dev, "device probed and registered");

	return 0;
}

static void lux_pci_remove(struct pci_dev *pdev) {
	global_lux = NULL;
	dev_info(&pdev->dev, "device unregistered");
}

struct pci_driver lux_pci_driver = {
	.name = LUX_CORE_DRIVER_NAME,
	.id_table = lux_id_table,
	.probe = lux_pci_probe,
	.remove = lux_pci_remove,
};

module_pci_driver(lux_pci_driver);
EXPORT_SYMBOL_GPL(global_lux);

/* Already handled through `module_pci_driver()` macro
 * static int __init lux_core_init(void) {
	int ret = 0;

	ret = pci_register_driver(&lux_pci_driver);
	if (ret < 0) {
		pr_alert(LUX_CORE_DRIVER_NAME ": failed to register PCI driver");
		return -1;
	}

	return 0;
};

static void __exit lux_core_exit(void) {
	pci_unregister_driver(&lux_pci_driver);
};

module_init(lux_core_init);
module_exit(lux_core_exit);
*/
MODULE_AUTHOR("m0st4fa");
MODULE_DESCRIPTION("PCI functionality for LED grid module");
MODULE_LICENSE("GPL");
