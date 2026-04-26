#include "linux/device.h"
#include "linux/atomic/atomic-instrumented.h"
#include "linux/cdev.h"
#include "linux/err.h"
#include "linux/idr.h"
#include "linux/pci.h"

#include "char.h"
#include "device.h"
#include "linux/printk.h"
#include "linux/slab.h"

struct kmem_cache *mathaccel_cache;
struct class *mathaccel_class;

DEFINE_IDA(mathaccel_ida);
dev_t firstdev_id;

int mathaccel_device_init(struct pci_dev *pdev) {
	struct mathaccel_device *math_dev = NULL;
	int ret = 0, minor = 0, major = MAJOR(firstdev_id);

	// 1. Identification and allocation of memory
	minor = ida_alloc_max(&mathaccel_ida, MATHACCEL_DEV_NR, GFP_KERNEL);
	if (minor < 0) {
		pr_err(MATHACCEL_DRIVER_NAME ": failed to allocate minor number");
		return minor;
	}

	math_dev = kmem_cache_zalloc(mathaccel_cache, GFP_KERNEL);
	if (math_dev == NULL) {
		pr_alert(MATHACCEL_DRIVER_NAME ": failed to allocate private device structure in slab");
		goto err_slab_alloc;
	}
	math_dev->minor = minor;
	math_dev->pdev = pdev;

	// 2. Initialize interrupt-driven IO infrastructure
	init_waitqueue_head(&math_dev->wq);
	init_waitqueue_head(&math_dev->kthread_wq);

	spin_lock_init(&math_dev->lock);
	math_dev->irq_cause = IRQ_CAUSE_NOIRQ;
	math_dev->result = 0;
	math_dev->done = 0;
	atomic_set(&math_dev->shutting_down, 0);
	atomic_set(&math_dev->counter, 0);

	// 3. Register with the character device subsystem
	cdev_init(&math_dev->cdev, &mathaccel_fops);
	math_dev->cdev.owner = &__this_module;
	/* This adds the pointer to the inode in memory with (major, minor) identifier (in field *i_cdev).
	 * You can use container_of() to extract priv_dev back.
	 */
	ret = cdev_add(&math_dev->cdev, MKDEV(major, minor), 1);
	if (ret < 0) {
		pr_alert(MATHACCEL_DRIVER_NAME ": failed to add cdev");
		goto err_cdev_add;
	}

	// 4. Give it a name and register it with sysfs
	snprintf(math_dev->name, 64, MATHACCEL_DRIVER_NAME "-%d", math_dev->minor);

	math_dev->model_dev = device_create(mathaccel_class, &pdev->dev,
										MKDEV(major, minor), math_dev,
										math_dev->name, minor);

	if (IS_ERR(math_dev->model_dev)) {
		pr_alert(MATHACCEL_DRIVER_NAME ": failed to create model device (sysfs device node) for minor %d", minor);
		ret = PTR_ERR(math_dev->model_dev);
		goto err_model_dev_create;
	}

	// 5. Link to PCI subsystem
	pci_set_drvdata(pdev, math_dev);
	return ret;

err_model_dev_create:
	cdev_del(&math_dev->cdev);
err_cdev_add:
	kmem_cache_free(mathaccel_cache, math_dev);
err_slab_alloc:
	ida_free(&mathaccel_ida, minor);
	return ret;
};

void mathaccel_device_destroy(struct mathaccel_device *math_dev) {
	// 1. Free resources
	device_destroy(mathaccel_class, math_dev->cdev.dev);
	ida_free(&mathaccel_ida, math_dev->minor);

	cdev_del(&math_dev->cdev);

	// 2. Free memory (must be freed last)
	kmem_cache_free(mathaccel_cache, math_dev);
};
