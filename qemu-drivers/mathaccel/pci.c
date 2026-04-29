#include "linux/pci.h"
#include "asm-generic/bug.h"
#include "asm-generic/pci_iomap.h"
#include "kthread.h"
#include "linux/delay.h"
#include "linux/interrupt.h"
#include "linux/mod_devicetable.h"
#include "linux/module.h"
#include "linux/printk.h"
#include <linux/atomic.h>

#include "device.h"
#include "dma.h"
#include "irq.h"
#include "pci.h"

static const struct pci_device_id mathaccel_id_table[] = {
	{PCI_DEVICE(MATHACCEL_DEVICE_ID, MATHACCEL_VENDOR_ID)},
	{0},
};
MODULE_DEVICE_TABLE(pci, mathaccel_id_table);

static int mathaccel_probe(struct pci_dev *pdev, const struct pci_device_id *id_table) {
	struct mathaccel_device *math_dev = NULL;
	int ret = 0, irq = 0;

	// 1. Create private state-keeping device
	ret = mathaccel_device_init(pdev);
	if (ret != 0) {
		pr_alert(MATHACCEL_DRIVER_NAME ": failed to create private mathaccel_device");
		return ret; // All resources have been already deallocated by mathaccel_device_init() function
	}
	math_dev = pci_get_drvdata(pdev);

	ret = spawn_kthread(math_dev);
	if (ret < 0)
		goto error_spawn_kthread;

	// 2. Configure pdev
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

	math_dev->bar[0] = pci_iomap(pdev, 0, 4096);
	if (math_dev->bar[0] == NULL) {
		ret = -ENOMEM;
		pr_alert(MATHACCEL_DRIVER_NAME ": failed to map BAR0 into kernel virtual address space");
		goto error_iomap;
	}

	ret = pci_alloc_irq_vectors(pdev, 1, 1, PCI_IRQ_MSI);
	if (ret < 0) {
		pr_alert(MATHACCEL_DRIVER_NAME ": failed to allocate irq vectors");
		goto error_request_irq_vec;
	}

	irq = request_irq(pci_irq_vector(pdev, 0),
					  mathaccel_irq_handler,
					  IRQF_SHARED, math_dev->name, math_dev);
	if (irq < 0) {
		pr_alert(MATHACCEL_DRIVER_NAME ": failed to allocate irq handler");
		goto error_request_irq;
	}

	// 3. Configure DMA
	ret = mathaccel_init_dma(math_dev);

	// 4. Configure device
	int flags = readl(math_dev->bar[0] + REG_FLAGS);
	flags |= (FLAG_INT_ENABLED | FLAG_DMA_ENABLED);
	pci_set_master(pdev);
	writel(flags, math_dev->bar[0] + REG_FLAGS);

	return 0;

error_request_irq:
	pci_free_irq_vectors(pdev);
error_request_irq_vec:
	pci_iounmap(pdev, math_dev->bar[0]);
error_iomap:
	pci_release_region(pdev, 0);
error_request_region:
	pci_disable_device(pdev);
error_enable_device:
	stop_kthread(math_dev);
error_spawn_kthread:
	mathaccel_device_destroy(math_dev);
	return ret;
}

static void mathaccel_remove(struct pci_dev *pdev) {
	struct mathaccel_device *math_dev = pci_get_drvdata(pdev);
	BUG_ON(math_dev == NULL);

	// 1. Stop all consumers (kthread consuming device, and other tasks consuming buffers populated by kthread)
	// FIXME: TOCTOU bug here. Assume whe set it shutting_down after a device has already checked
	// Solution is to store the state of the device in the struct and check it atomically
	atomic_set_release(&math_dev->shutting_down, 1);
	wake_up_all(&math_dev->wq);

	// 2. Stop device activity
	int flags = readl(math_dev->bar[0] + REG_FLAGS);
	flags &= ~(FLAG_INT_ENABLED | FLAG_DMA_ENABLED);
	writel(flags, math_dev->bar[0] + REG_FLAGS);
	// Never issue FLR in the remove path (can reinable interrupts)
	// pci_reset_function(pdev);

	free_irq(pci_irq_vector(pdev, 0), math_dev); // blocks until last handler is done
	pci_free_irq_vectors(pdev);

	// 3. Now safe to stop the kthread - IRQ handler cannot fire and compete for locks
	stop_kthread(math_dev);

	// 4. Release kernel PCI and DMA resources
	mathaccel_release_dma(math_dev);
	pci_iounmap(pdev, math_dev->bar[0]);
	pci_release_region(pdev, 0);
	pci_disable_device(pdev);

	// 5. Free device
	mathaccel_device_destroy(math_dev);
}

struct pci_driver mathaccel_pci_driver = {
	.name = MATHACCEL_DRIVER_NAME,
	.id_table = mathaccel_id_table,
	.probe = mathaccel_probe,
	.remove = mathaccel_remove,
};
