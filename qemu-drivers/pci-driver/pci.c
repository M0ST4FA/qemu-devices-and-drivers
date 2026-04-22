#include "pci.h"
#include "asm-generic/int-ll64.h"
#include "asm-generic/pci_iomap.h"
#include "linux/interrupt.h"
#include "linux/irqreturn.h"
#include "linux/pci_regs.h"
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

struct qemuedu_pci_device edu_dev;

// 2. Probe and remove
static int edu_probe(struct pci_dev *pdev, const struct pci_device_id *id) {
	int ret = 0, i = 0;

	printk(KERN_INFO PCI_DEVICE_NAME ": probe called\n");
	ret = pci_enable_device(pdev);
	if (ret)
		pr_err(PCI_DEVICE_NAME ": pci_enable_device failed\n");
	pr_info(PCI_DEVICE_NAME ": device enabled\n");

	// Read some information from config space
	pr_info("edu: vendor=0x%x device=0x%x\n", pdev->vendor, pdev->device);
	pr_info("edu: class=0x%x irq=%d\n", pdev->class, pdev->irq);

	// Read the BARs. In each case, you have two components: start address and region size.
	for (i = 0; i < PCI_STD_NUM_BARS; i++) {
		resource_size_t start = pci_resource_start(pdev, i);
		resource_size_t len = pci_resource_len(pdev, i);

		if (len > 0)
			pr_info("edu: BAR %d -> start=%pa len=%pa\n", i, &start, &len);
	}

	// Print config space
	u16 command;
	pci_read_config_word(pdev, PCI_COMMAND, &command);

	// Request BAR region
	ret = pci_request_regions(pdev, PCI_DEVICE_NAME "_driver");
	if (ret)
		goto error_disable_device;

	void *__iomem bar_base;
	bar_base = pci_iomap(pdev, 0, 0);
	// FIXME: should be: bar_base = pci_iomap(pdev, 0, 4096);

	if (!bar_base) {
		ret = -ENOMEM;
		goto error_release_region;
	}

	// Request IRQ
	int irq = pdev->irq;
	ret = request_irq(irq, edu_irq_handler, IRQF_SHARED, PCI_DEVICE_NAME, pdev);
	if (ret)
		goto error_release_iomap;

	// Initialize device
	edu_dev.pdev = pdev;
	edu_dev.base = bar_base;
	init_waitqueue_head(&edu_dev.wq);

	pci_set_drvdata(pdev, &edu_dev);

	// Success path
	return ret;

	// Failure path
error_release_iomap:
	pci_iounmap(pdev, bar_base);
error_release_region:
	pci_release_regions(pdev);
error_disable_device:
	pci_disable_device(pdev);
	return ret;
}

static void edu_remove(struct pci_dev *pdev) {
	pr_info("edu: remove called\n");
	struct qemuedu_pci_device *edev = pci_get_drvdata(pdev);

	if (edev && edev->base)
		pci_iounmap(pdev, edev->base);

	free_irq(pdev->irq, pdev);
	pci_release_regions(pdev);
	pci_disable_device(pdev);
}

static inline bool is_computing_factorial(struct qemuedu_pci_device *edev) {
	return edu_hw_read(edev, EDU_REG_STATUS) & EDU_STATUS_COMPUTING;
}

irqreturn_t edu_irq_handler(int irq, void *dev_id) {
	pr_info(PCI_DEVICE_NAME ": Received interrupt");

	struct pci_dev *pdev = dev_id;
	struct qemuedu_pci_device *edev = pci_get_drvdata(pdev);

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
	.name = PCI_DEVICE_NAME "_driver",
	.id_table = edu_pci_ids,
	.probe = edu_probe,
	.remove = edu_remove,
};
