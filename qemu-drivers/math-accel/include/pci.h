#include "linux/cdev.h"
#include "linux/pci.h"

#define MATHACCEL_DRIVER_NAME "math-accel"
#define MATHACCEL_DEVICE_ID 0x1234
#define MATHACCEL_VENDOR_ID 0x5678

#define MATHACCEL_REG_DATA 0x0
#define MATHACCEL_REG_CMD 0x4
#define MATHACCEL_REG_STATUS 0x8

#define MATHACCEL_CMD_MULTIPLY 0x1
#define MATHACCEL_STATUS_DONE 0x1

#define MATHACCEL_DEV_NR 4

extern struct pci_driver mathaccel_pci_driver;

struct mathaccel_device {
	struct pci_dev *pdev;
	void *__iomem bar[1];
	struct cdev cdev;
	int minor;
};

extern struct mathaccel_device mathaccel_dev[MATHACCEL_DEV_NR];
