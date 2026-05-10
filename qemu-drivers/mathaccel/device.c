#include "linux/device.h"
#include "linux/cdev.h"
#include "linux/err.h"
#include "linux/idr.h"
#include "linux/pci.h"

#include "char.h"
#include "device.h"
#include "linux/printk.h"
#include "linux/slab.h"
#include "linux/spinlock.h"
#include "linux/xarray.h"

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
	math_dev->ring_size = MATHACCEL_RINGBUFFER_SIZE;

	xa_init(&math_dev->pending_submissions);

	init_waitqueue_head(&math_dev->kthread_wq);

	spin_lock_init(&math_dev->dma_lock);

	atomic_set(&math_dev->irq_cause, IRQ_CAUSE_NOIRQ);
	atomic_set(&math_dev->counter, 0);

	// xa_init_flags(&math_dev->active_submissions, XA_FLAGS_ALLOC);

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

/* Submit a single job through DMA.
 * @returns Index of cmd in submission queue. This will be the same in completion queue and should be used to obtain result.
 * */
int mathaccel_submit_one_cmd(struct mathaccel_device *math_dev, struct mathaccel_req *req) {
	// 1. Get the tail of the submission and make sure it is not full
	spin_lock(&math_dev->dma_lock);
	u32 sq_head = math_dev->sq_head;
	u32 sq_tail = math_dev->sq_tail;
	u32 sq_next_tail = (sq_tail + 1) % math_dev->ring_size;

	// Check for a full queue. We will handle blocking later
	if (sq_next_tail == sq_head) { // If next tail (the one after the current) would hit the current head (this would overwrite current head)
		spin_unlock(&math_dev->dma_lock);
		pr_alert(MATHACCEL_DRIVER_NAME ": submission queue is full");
		return -EBUSY;
	}
	// 2. Fill in the data
	struct math_sq_entry *current_entry = &math_dev->sq_cpu_addr[sq_tail % math_dev->ring_size];
	current_entry->cmd_id = req->cmd_id;
	current_entry->args[0] = req->args[0];
	current_entry->args[1] = req->args[1];
	current_entry->opcode = req->opcode;

	// 3. Advance tail
	math_dev->sq_tail = sq_next_tail;

	spin_unlock(&math_dev->dma_lock);
	return sq_tail; // Index
};

int mathaccel_completion_entry_valid(struct mathaccel_device *math_dev, int index) {
	int valid = 0;

	if (index >= math_dev->ring_size)
		return 0;

	struct math_cq_entry *entry = &math_dev->cq_cpu_addr[index];
	valid = READ_ONCE(entry->valid);

	return valid;
}
