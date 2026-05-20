#include <linux/module.h>
#include <linux/pci.h>

#include "asm-generic/pci_iomap.h"
#include "linux/dev_printk.h"
#include "linux/printk.h"
#include "lux.h"

static const struct pci_device_id lux_id_table[] = {
	{PCI_DEVICE(LUX_VENDOR_ID, LUX_DEVICE_ID)},
	{0},
};
MODULE_DEVICE_TABLE(pci, lux_id_table);

static int lux_pci_probe(struct pci_dev *pdev, const struct pci_device_id *id_table) {
	int ret = 0;

	struct lux_device *lux_device = devm_kzalloc(&pdev->dev, sizeof(*lux_device), GFP_KERNEL);
	if (lux_device == NULL) {
		pr_alert(LUX_CORE_DRIVER_NAME ": failed to allocate device structure");
		return -ENOMEM;
	}

	ret = pcim_enable_device(pdev);
	if (ret < 0) {
		dev_err(&pdev->dev, ": failed to enable PCI device");
		return ret;
	}

	lux_device->bar[0] = pcim_iomap_region(pdev, 0, LUX_CORE_DRIVER_NAME);
	if (!lux_device->bar[0]) {
		dev_err(&pdev->dev, ": failed to request BAR 0 or map it into kernel virtual address space");
		return -ENOMEM; // Likey virtual space is exhausted
	}

	pci_set_drvdata(pdev, lux_device);

	dev_info(&pdev->dev, ": device probed and registered");

	return 0;
}

static void lux_pci_remove(struct pci_dev *pdev) {
	dev_info(&pdev->dev, ": device unregistered");
}

struct pci_driver lux_pci_driver = {
	.name = LUX_CORE_DRIVER_NAME,
	.id_table = lux_id_table,
	.probe = lux_pci_probe,
	.remove = lux_pci_remove,
};

module_pci_driver(lux_pci_driver);

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
