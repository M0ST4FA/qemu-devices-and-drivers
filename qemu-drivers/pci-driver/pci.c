#include "pci.h"
#include "asm-generic/pci_iomap.h"
#include "char.h"
#include "linux/cdev.h"
#include "linux/interrupt.h"
#include "linux/irqreturn.h"
#include "linux/kdev_t.h"
#include "linux/printk.h"
#include "linux/spinlock.h"
#include "linux/types.h"
#include "linux/wait.h"
#include <linux/module.h>
#include <linux/pci.h>

// 1. Declare identifiers used by PCI core for matching device to driver
static const struct pci_device_id edu_pci_ids[] = {
	{PCI_DEVICE(PCI_VENDOR_ID_QEMU, PCI_DEVICE_ID_EDU)},
	{0},
};

MODULE_DEVICE_TABLE(pci, edu_pci_ids);

struct edu_dev edu_dev;

// 2. Probe and remove
static int edu_probe(struct pci_dev *pdev, const struct pci_device_id *id) {
	int ret = 0;

	cdev_init(&edu_dev.cdev, &edu_fops);
	edu_dev.pdev = pdev;

	printk(KERN_INFO EDU_DRIVER_NAME ": probe called\n");
	ret = pci_enable_device(pdev);
	if (ret) {
		pr_err(EDU_DRIVER_NAME ": pci_enable_device failed\n");
		goto error_device_enable;
	}

	// Request BAR region
	ret = pci_request_regions(pdev, EDU_DRIVER_NAME "_driver");
	if (ret)
		goto error_request_regions;

	void *__iomem bar_base;
	bar_base = pci_iomap(pdev, 0, 4096);

	if (!bar_base) {
		ret = -ENOMEM;
		goto error_iomap;
	}

	// Request IRQ
	int irq = pdev->irq;
	ret = request_irq(irq, edu_irq_handler, IRQF_SHARED, EDU_DRIVER_NAME, &edu_dev);
	if (ret)
		goto error_request_irq;

	// Initialize device
	edu_dev.pdev = pdev;
	edu_dev.base = bar_base;
	init_waitqueue_head(&edu_dev.wq);

	pci_set_drvdata(pdev, &edu_dev);

	ret = cdev_add(&edu_dev.cdev, MKDEV(edu_major, 0), 1);
	if (ret < 0) {
		pr_alert(EDU_DRIVER_NAME ": failed to add character device");
		goto error_cdev;
	}

	// Success path
	return ret;

	// Failure path
error_cdev:
error_request_irq:
	pci_iounmap(pdev, bar_base);
error_iomap:
	pci_release_regions(pdev);
error_request_regions:
	pci_disable_device(pdev);
error_device_enable:
	return ret;
}

static void edu_remove(struct pci_dev *pdev) {
	pr_info(EDU_DRIVER_NAME ": remove called\n");
	struct edu_dev *edev = pci_get_drvdata(pdev);

	if (edev && edev->base) {
		cdev_del(&edev->cdev);
		pci_iounmap(pdev, edev->base);
	}

	free_irq(pdev->irq, pdev);
	pci_release_regions(pdev);
	pci_disable_device(pdev);
}

static inline bool is_computing_factorial(struct edu_dev *edev) {
	return edu_hw_read(edev, EDU_REG_STATUS) & EDU_STATUS_COMPUTING;
}

irqreturn_t edu_irq_handler(int irq, void *dev_id) {
	pr_info(EDU_DRIVER_NAME ": Received interrupt");

	struct pci_dev *pdev = dev_id;
	struct edu_dev *edev = pci_get_drvdata(pdev);

	if (!edev)
		return IRQ_NONE;

	if (is_computing_factorial(edev)) // Not our interrupt
		return IRQ_NONE;

	spin_lock(&edev->lock);

	edev->result = edu_hw_read(edev, EDU_REG_FACTORIAL);
	edev->done = 1;

	spin_unlock(&edev->lock);

	// Ack interrupt
	edu_hw_write(edev, EDU_REG_IRQACK, 1);

	wake_up_interruptible(&edev->wq);

	return IRQ_HANDLED;
};

// 3. Create PCI driver struct that PCI driver core will end up using
struct pci_driver edu_driver = {
	.name = EDU_DRIVER_NAME,
	.id_table = edu_pci_ids,
	.probe = edu_probe,
	.remove = edu_remove,
};
