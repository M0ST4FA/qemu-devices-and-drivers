#include <linux/pci.h>

#define LUX_CORE_DRIVER_NAME "lux-core"
#define LUX_VENDOR_ID 0x1234
#define LUX_DEVICE_ID 0x0001

struct lux_device {
	struct pci_dev *pdev;
	void __iomem *bar[2];
};
