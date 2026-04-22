#include "pci.h"
#include "asm-generic/int-ll64.h"
#include "asm-generic/iomap.h"
#include "asm-generic/pci_iomap.h"
#include "char.h"
#include "linux/gfp_types.h"
#include "linux/idr.h"
#include "linux/interrupt.h"
#include "linux/irqreturn.h"
#include "linux/kdev_t.h"
#include "linux/mod_devicetable.h"
#include "linux/module.h"
#include "linux/pci.h"
#include "linux/printk.h"

// 1. Main state
struct mathaccel_device mathaccel_dev_array[MATHACCEL_DEV_NR];

DEFINE_IDA(mathaccel_ida);

static const struct pci_device_id mathaccel_id_table[] = {
	{PCI_DEVICE(MATHACCEL_DEVICE_ID, MATHACCEL_VENDOR_ID)},
	{0},
};
MODULE_DEVICE_TABLE(pci, mathaccel_id_table);

// 2. Main functions
static irqreturn_t mathaccel_irq_handler(int irq, void *cookie) {
	struct mathaccel_device *dev = cookie;
	u32 status, number;

	// 1. Read status register to see why interrupt fired; This also resets command
	status = ioread32(dev->bar[0] + MATHACCEL_REG_STATUS);

	// 2. The action depends on the status
	if (status == MATHACCEL_STATUS_DONE) {
		number = ioread32(dev->bar[0] + MATHACCEL_REG_DATA);

		pr_info(MATHACCEL_DRIVER_NAME ": interrupt called and result is ready! irq: %d, pdev->irq: %d, computed value: %d", irq, dev->pdev->irq, number);
	}

	return IRQ_HANDLED;
};

static int mathaccel_probe(struct pci_dev *pdev, const struct pci_device_id *id_table) {
	struct mathaccel_device *priv_dev;
	int ret = 0, irq = 0, minor = 0, major = MAJOR(firstdev_id);

	minor = ida_alloc_max(&mathaccel_ida, MATHACCEL_DEV_NR, GFP_KERNEL);
	if (minor < 0) {
		pr_err(MATHACCEL_DRIVER_NAME ": failed to allocate minor number");
		return minor;
	}

	priv_dev = &mathaccel_dev_array[minor];
	priv_dev->minor = minor;
	// FIXME: Likely a bug (shouldn't copy)
	priv_dev->pdev = pdev;

	ret = pci_enable_device(pdev);
	if (ret < 0) {
		pr_alert(MATHACCEL_DRIVER_NAME ": failed to enable PCI device");
		goto error_enable_device;
	};

	ret = pci_request_region(pdev, 0, MATHACCEL_DRIVER_NAME);
	if (ret < 0) {
		pr_alert(MATHACCEL_DRIVER_NAME ": failed to request memory region");
		goto error_request_region;
	};

	priv_dev->bar[0] = pci_iomap(pdev, 0, 4096);
	if (priv_dev->bar[0] == NULL) {
		ret = -ENOMEM;
		pr_alert(MATHACCEL_DRIVER_NAME ": failed to map BAR0 into kernel virtual address space");
		goto error_iomap;
	}

	ret = pci_alloc_irq_vectors(pdev, 1, 1, PCI_IRQ_INTX);
	if (ret < 0) {
		pr_alert(MATHACCEL_DRIVER_NAME ": failed to allocate irq vectors");
		goto error_request_irq_vec;
	}

	irq = request_irq(pci_irq_vector(pdev, 0), mathaccel_irq_handler, IRQF_SHARED, MATHACCEL_DRIVER_NAME, priv_dev);
	if (irq < 0) {
		pr_alert(MATHACCEL_DRIVER_NAME ": failed to allocate irq handler");
		goto error_request_irq;
	}

	cdev_init(&priv_dev->cdev, &mathaccel_fops);
	/* This adds the pointer to the inode in memory with (major, minor) identifier (in field *i_cdev).
	 * You can use container_of() to extract priv_dev back.
	 */
	ret = cdev_add(&priv_dev->cdev, MKDEV(major, minor), 1);
	if (ret < 0) {
		pr_alert(MATHACCEL_DRIVER_NAME ": failed to add cdev");
		goto error_cdev;
	}

	pci_set_drvdata(pdev, priv_dev);

	return 0;

error_cdev:
error_request_irq:
	pci_free_irq_vectors(pdev);
error_request_irq_vec:
	pci_iounmap(pdev, mathaccel_dev_array[0].bar[0]);
error_iomap:
	pci_release_region(pdev, 0);
error_request_region:
	pci_disable_device(pdev);
error_enable_device:
	return ret;
}

static void mathaccel_remove(struct pci_dev *pdev) {
	// FIXME: Likely a bug (pdev will not necessarily contain math_dev arround it)
	struct mathaccel_device *math_dev = pci_get_drvdata(pdev);

	pci_free_irq_vectors(pdev);
	free_irq(pci_irq_vector(pdev, 0), pdev);
	pci_iounmap(pdev, math_dev->bar[0]);
	pci_release_region(pdev, 0);
	pci_disable_device(pdev);
}

// 3. Driver structure to be registered with PCI core
struct pci_driver mathaccel_pci_driver = {
	.name = MATHACCEL_DRIVER_NAME,
	.id_table = mathaccel_id_table,
	.probe = mathaccel_probe,
	.remove = mathaccel_remove,
};
