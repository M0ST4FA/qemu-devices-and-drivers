#include "pci.h"
#include "asm-generic/bug.h"
#include "asm-generic/int-ll64.h"
#include "asm-generic/iomap.h"
#include "asm-generic/pci_iomap.h"
#include "char.h"
#include "linux/cdev.h"
#include "linux/device.h"
#include "linux/dma-mapping.h"
#include "linux/gfp_types.h"
#include "linux/idr.h"
#include "linux/interrupt.h"
#include "linux/irqreturn.h"
#include "linux/kdev_t.h"
#include "linux/mod_devicetable.h"
#include "linux/module.h"
#include "linux/pci.h"
#include "linux/printk.h"
#include "linux/slab.h"
#include "linux/spinlock.h"
#include "linux/wait.h"
#include <linux/atomic.h>

// 1. Main state
struct kmem_cache *mathaccel_cache;

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
	if (status != MATHACCEL_STATUS_DONE) { // Spurious interrupt
		pr_info(MATHACCEL_DRIVER_NAME ": spurious interrupt for (%d,%d)", MAJOR(firstdev_id), dev->minor);
		return IRQ_NONE;
	}

	number = ioread32(dev->bar[0] + MATHACCEL_REG_DATA);

	pr_info(MATHACCEL_DRIVER_NAME ": interrupt called and result is ready! irq: %d, pdev->irq: %d, computed value: %d", irq, dev->pdev->irq, number);

	spin_lock(&dev->lock);
	dev->result = number;

	// Set the condition variable
	dev->done = 1;
	spin_unlock(&dev->lock);

	// Wake up all processes waiting on the queue
	// They will use the condition variable to decide whether it was spurious
	wake_up(&dev->wq);

	return IRQ_HANDLED;
};

static int mathaccel_init_dma(struct mathaccel_device *math_dev) {
	struct device *model_dev = &math_dev->pdev->dev;
	// 1. Tell the kernel we support 64-bit DMA addresses
	dma_set_mask_and_coherent(model_dev, DMA_BIT_MASK(64));

	// 2. Allocate the submission queue and completion queue
	math_dev->sq_cpu_addr = dma_alloc_coherent(model_dev,
											   sizeof(struct math_sq_entry) * MATHACCEL_RINGBUFFER_SIZE,
											   &math_dev->sq_dma_addr, GFP_KERNEL);
	if (!math_dev->sq_cpu_addr) {
		pr_alert(MATHACCEL_DRIVER_NAME ": error during allocation of SQ DMA memory");
		return -ENOMEM;
	}
	pr_info(MATHACCEL_DRIVER_NAME ": allocated submission queue (virtual: %p, DMA: %llx)", math_dev->sq_cpu_addr, math_dev->sq_dma_addr);

	math_dev->cq_cpu_addr = dma_alloc_coherent(model_dev,
											   sizeof(struct math_cq_entry) * MATHACCEL_RINGBUFFER_SIZE,
											   &math_dev->cq_dma_addr, GFP_KERNEL);
	if (!math_dev->sq_cpu_addr) {
		pr_alert(MATHACCEL_DRIVER_NAME ": error during allocation of CQ DMA memory");
		dma_free_coherent(model_dev,
						  sizeof(struct math_sq_entry) * MATHACCEL_RINGBUFFER_SIZE,
						  math_dev->sq_cpu_addr, math_dev->sq_dma_addr);
		return -ENOMEM;
	}
	pr_info(MATHACCEL_DRIVER_NAME ": allocated completion queue (virtual: %p, DMA: %llx)", math_dev->cq_cpu_addr, math_dev->cq_dma_addr);

	// 3. Inform the hardware for the address we set up for it

	// 4. Inform the hardware how big the ring buffer is
	return 0;
};

static int mathaccel_probe(struct pci_dev *pdev, const struct pci_device_id *id_table) {
	struct mathaccel_device *priv_dev;
	int ret = 0, irq = 0, minor = 0, major = MAJOR(firstdev_id);

	minor = ida_alloc_max(&mathaccel_ida, MATHACCEL_DEV_NR, GFP_KERNEL);
	if (minor < 0) {
		pr_err(MATHACCEL_DRIVER_NAME ": failed to allocate minor number");
		return minor;
	}

	priv_dev = kmem_cache_zalloc(mathaccel_cache, GFP_KERNEL);
	priv_dev->minor = minor;
	priv_dev->pdev = pdev;

	// Initialize waiting infrastructure
	init_waitqueue_head(&priv_dev->wq);
	spin_lock_init(&priv_dev->lock);
	priv_dev->result = 0;
	priv_dev->done = 0;
	atomic_set(&priv_dev->shutting_down, 0);
	atomic_set(&priv_dev->counter, 0);

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

	ret = pci_alloc_irq_vectors(pdev, 1, 1, PCI_IRQ_MSI);
	if (ret < 0) {
		pr_alert(MATHACCEL_DRIVER_NAME ": failed to allocate irq vectors");
		goto error_request_irq_vec;
	}

	snprintf(priv_dev->name, 64, MATHACCEL_DRIVER_NAME "-%d", minor);

	irq = request_irq(pci_irq_vector(pdev, 0), mathaccel_irq_handler, IRQF_SHARED, priv_dev->name, priv_dev);
	if (irq < 0) {
		pr_alert(MATHACCEL_DRIVER_NAME ": failed to allocate irq handler");
		goto error_request_irq;
	}

	ret = mathaccel_init_dma(priv_dev);

	cdev_init(&priv_dev->cdev, &mathaccel_fops);
	priv_dev->cdev.owner = &__this_module;
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
	pci_iounmap(pdev, priv_dev->bar[0]);
error_iomap:
	pci_release_region(pdev, 0);
error_request_region:
	pci_disable_device(pdev);
error_enable_device:
	return ret;
}

static void mathaccel_remove(struct pci_dev *pdev) {
	struct mathaccel_device *math_dev = pci_get_drvdata(pdev);
	BUG_ON(math_dev == NULL);
	struct device *model_dev = &math_dev->pdev->dev;

	atomic_set_release(&math_dev->shutting_down, 1);
	wake_up_all(&math_dev->wq);

	dma_free_coherent(model_dev,
					  sizeof(struct math_sq_entry) * MATHACCEL_RINGBUFFER_SIZE,
					  math_dev->sq_cpu_addr, math_dev->sq_dma_addr);
	dma_free_coherent(model_dev,
					  sizeof(struct math_sq_entry) * MATHACCEL_RINGBUFFER_SIZE,
					  math_dev->cq_cpu_addr, math_dev->cq_dma_addr);

	cdev_del(&math_dev->cdev);
	pci_iounmap(pdev, math_dev->bar[0]);
	kmem_cache_free(mathaccel_cache, math_dev);

	pci_free_irq_vectors(pdev);
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
