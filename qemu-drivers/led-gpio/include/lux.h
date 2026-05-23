#pragma once
#include <linux/cdev.h>
#include <linux/pci.h>

#define LUX_CORE_DRIVER_NAME "lux-core"
#define LUX_CHIP_LABEL "lux-gpio-chip"
#define LUX_CHAR_DEVICE_NAME "lux-chardev"
#define LUX_CLASS_NAME "lux"

#define LUX_VENDOR_ID 0x1234
#define LUX_DEVICE_ID 0x0001

struct lux_device {
	struct pci_dev *pdev;
	struct cdev cdev;
	dev_t cdev_id;
	struct device *device;
	void __iomem *bar[2];
};

extern struct lux_device *global_lux;
