#include "asm-generic/barrier.h"
#include "asm-generic/ioctl.h"
#include "asm/pgtable.h"
#include "linux/cdev.h"
#include "linux/container_of.h"
#include "linux/device.h"
#include "linux/device/class.h"
#include "linux/err.h"
#include "linux/fs.h"
#include "linux/gfp_types.h"
#include "linux/init.h"
#include "linux/interrupt.h"
#include "linux/irqdomain.h"
#include "linux/irqreturn.h"
#include "linux/mm.h"
#include "linux/mm_types.h"
#include "linux/pci.h"
#include "linux/platform_device.h"
#include "linux/printk.h"
#include "linux/slab.h"
#include "linux/stddef.h"
#include "linux/swait.h"
#include "linux/types.h"
#include "linux/uaccess.h"
#include <linux/module.h>

#include "../../qemu-devices/lux/include/hw.h"
#include "lux.h"
#include "lux_ioctl.h"

struct class *lux_class;
extern struct irq_domain *lux_global_irq_domain;

struct lux_cdev {
	struct lux_function *lux_function;
	struct cdev cdev;
	struct device *device;
	dev_t cdev_id;

	enum led_irq_cause last_irq_cause;
	bool was_error;
	struct swait_queue_head wq;
};

static irqreturn_t lux_smart_led_irq_handler(int virq, void *dev) {
	struct lux_cdev *cdev = dev;
	void __iomem *base = cdev->lux_function->bar[0];
	cdev->last_irq_cause = readl(base + REG_LED_IRQ_CAUSE);
	cdev->was_error = false;

	pr_info(LUX_CHAR_DRIVER_NAME ": LED irq occured: %s\n",
			led_irq_cause_names[cdev->last_irq_cause]);

	swake_up_all(&cdev->wq);

	return IRQ_HANDLED;
}

static irqreturn_t lux_smart_led_err_irq_handler(int virq, void *dev) {
	struct lux_cdev *cdev = dev;
	void __iomem *base = cdev->lux_function->bar[0];
	cdev->last_irq_cause = readl(base + REG_LED_IRQ_CAUSE);
	cdev->was_error = true;

	pr_info(LUX_CHAR_DRIVER_NAME ": LED error occured: %s\n",
			led_irq_cause_names[cdev->last_irq_cause]);

	swake_up_all(&cdev->wq);

	return IRQ_HANDLED;
}

static int lux_smart_open(struct inode *inode, struct file *filp) {
	int ret = 0;
	struct lux_cdev *device = NULL;
	device = container_of(inode->i_cdev, struct lux_cdev, cdev);

	void __iomem *base = device->lux_function->bar[0];

	filp->private_data = device;

	if (!lux_global_irq_domain)
		return -EAGAIN; // Try again later

	int virq = irq_find_mapping(lux_global_irq_domain, HWIRQ_LED);
	int virq_err = irq_find_mapping(lux_global_irq_domain, HWIRQ_LED_ERR);

	if (!virq || !virq_err) {
		pr_err(LUX_CHAR_DRIVER_NAME ": Failed to request virq for LED (%d) or its errors (%d)\n", virq, virq_err);
		return -EBUSY;
	}

	ret = request_irq(virq, lux_smart_led_irq_handler,
					  IRQF_NO_THREAD, "lux-led", device);
	if (ret < 0) {
		pr_err(LUX_CHAR_DRIVER_NAME ": Failed to setup the LED irq handler\n");
		return ret;
	}

	ret = request_irq(virq_err, lux_smart_led_err_irq_handler,
					  IRQF_NO_THREAD, "lux-led-err", device);
	if (ret < 0) {
		free_irq(virq, device);
		pr_err(LUX_CHAR_DRIVER_NAME ": Failed to setup the LED error irq handler\n");
		return ret;
	}

	writel(LED_CTRL_IRQ_EN, base + REG_LED_CTRL);

	return ret;
}

static int lux_smart_release(struct inode *inode, struct file *filp) {
	struct lux_cdev *device = NULL;
	device = container_of(inode->i_cdev, struct lux_cdev, cdev);

	void __iomem *base = device->lux_function->bar[0];

	filp->private_data = device;

	int virq = irq_find_mapping(lux_global_irq_domain, HWIRQ_LED);
	int virq_err = irq_find_mapping(lux_global_irq_domain, HWIRQ_LED_ERR);

	free_irq(virq, device);
	free_irq(virq_err, device);

	writel(0, base + REG_LED_CTRL);

	return 0;
}

static ssize_t lux_smart_write(struct file *filp, const char *buf,
							   size_t count, loff_t *offset) {
	struct lux_cdev *device = filp->private_data;
	struct smart_led led;
	int led_id;

	if (count != sizeof(struct smart_led))
		return -EINVAL;

	if (*offset >= LED_NR * sizeof(struct smart_led))
		return -ERANGE;

	led_id = *offset / sizeof(struct smart_led);

	if (copy_from_user(&led, buf, sizeof(struct smart_led)))
		return -EFAULT;

	void *led_base = device->lux_function->bar[1] + led_id * sizeof(struct smart_led);

	writeq(*(uint64_t *)&led, led_base);

	int wait_ret = swait_event_interruptible_exclusive(device->wq, READ_ONCE(device->last_irq_cause));
	if (wait_ret != 0) { // Usually -ERESTARTSYS; this condition is important to not break signal handling
		device->last_irq_cause = 0;
		return wait_ret;
	}

	if (device->last_irq_cause & LUX_IRQ_LED_ERR_UNKNOWN_CMD) {
		pr_err(LUX_CHAR_DRIVER_NAME ": Sent invalid command to LED\n");
		count = -EINVAL;
	}

	device->last_irq_cause = 0; // Reset wait condition

	*offset += sizeof(struct smart_led);

	return count;
}

static ssize_t lux_smart_read(struct file *filp, char *buf,
							  size_t count, loff_t *offset) {
	struct lux_cdev *device = filp->private_data;
	struct smart_led led;
	int led_id;

	if (count != sizeof(struct smart_led))
		return -EINVAL;

	if (*offset >= LED_NR * sizeof(struct smart_led))
		return -ERANGE;

	led_id = *offset / sizeof(struct smart_led);
	void *led_base = device->lux_function->bar[1] + led_id * sizeof(struct smart_led);

	*(uint64_t *)&led = readq(led_base);

	if (copy_to_user(buf, &led, sizeof(struct smart_led)))
		return -EFAULT;

	*offset += sizeof(struct smart_led);

	return count;
}

static int lux_smart_mmap(struct file *filp, struct vm_area_struct *vma) {
	int ret = 0;
	struct lux_cdev *lux_cdev = filp->private_data;
	int len = vma->vm_end - vma->vm_start;

	// 1. Get physical memory of BAR 1 (memory region of smart LEDs)
	resource_size_t led_iomem_phys = pci_resource_start(lux_cdev->lux_function->pdev, 1);
	if (!led_iomem_phys)
		return -ENODEV;

	// 2. Set correct protection attributes of pages and perform the mapping
	vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);
	ret = io_remap_pfn_range(vma, vma->vm_start,
							 PHYS_PFN(led_iomem_phys),
							 len,
							 vma->vm_page_prot);
	if (ret < 0) {
		pr_err(LUX_CHAR_DRIVER_NAME ": Failed to map physical address to proccess memory area (err: %d)", ret);
		return ret;
	}

	pr_info(LUX_CHAR_DRIVER_NAME ": Mapped virtual memory (addr: 0x%lx) of proc (PID: %d) to physical memory (addr: 0x%llx)\n",
			vma->vm_start, current->pid, led_iomem_phys);

	return 0;
}

static long lux_smart_ioctl(struct file *filp, uint cmd, ulong arg) {
	struct lux_cdev *lux_cdev = filp->private_data;
	void __iomem *bar0 = lux_cdev->lux_function->bar[0];

	// Command not recognized (more specifically, not for this device)
	if (_IOC_TYPE(cmd) != LUX_IOCTL_MAGIC)
		return -ENOTTY;

	switch (cmd) {
		case LUX_IOCTL_CLEAR_SCREEN:
			writeq(0x0ULL, bar0 + REG_DATA);
			wmb();
			readq(bar0 + REG_DIRECTION);
			break;

		case LUX_IOCTL_SET_DIRECTION_OUT:
			writeq(arg, bar0 + REG_DIRECTION);
			wmb();
			readq(bar0 + REG_DIRECTION);
			break;

		case LUX_IOCTL_SET_DIRECTION_IN:
			writeq(~arg, bar0 + REG_DIRECTION);
			wmb();
			readq(bar0 + REG_DIRECTION);
			break;

		case LUX_IOCTL_GET_INFO: {
#define LED_COLS 8
#define LED_ROWS 8

			struct lux_hw_info hw_info = {
				.num_leds = LED_NR,
				.cols = LED_COLS,
				.rows = LED_ROWS,
			};

			if (copy_to_user((void __user *)arg, &hw_info, sizeof(hw_info)))
				return -EFAULT;

		} break;

		default:
			// Command not recognized
			return -ENOTTY;
	}

	pr_info(LUX_CHAR_DRIVER_NAME ": Serviced ioctl %s\n", ioctl_names[_IOC_NR(cmd)]);

	return 0;
}

static struct file_operations lux_fops = {
	.owner = THIS_MODULE,
	.open = lux_smart_open,
	.release = lux_smart_release,
	.write = lux_smart_write,
	.read = lux_smart_read,
	.llseek = default_llseek,
	.mmap = lux_smart_mmap,
	.unlocked_ioctl = lux_smart_ioctl,
};

static int lux_driver_cdev_probe(struct platform_device *platdev) {
	int ret = 0;
	bool cdev_added = false;
	struct device *parent_dev = platdev->dev.parent;
	struct lux_function *lux_function = dev_get_drvdata(parent_dev);

	if (lux_function->dev_id != LUX_F0_DEV_ID)
		return -ENODEV;

	// 1. Allocate private data
	struct lux_cdev *lux_cdev = kzalloc(sizeof(struct lux_cdev), GFP_KERNEL);
	if (lux_cdev == NULL)
		return -ENOMEM;
	init_swait_queue_head(&lux_cdev->wq);

	ret = alloc_chrdev_region(&lux_cdev->cdev_id, 0, 1, LUX_CHAR_DRIVER_NAME);
	if (ret < 0) {
		pr_err(LUX_CHAR_DRIVER_NAME ": Failed to allocate major and minor numbers for lux device");
		return ret;
	}
	lux_cdev->lux_function = lux_function;
	platform_set_drvdata(platdev, lux_cdev);

	// 2. Register with VFS
	cdev_init(&lux_cdev->cdev, &lux_fops);
	ret = cdev_add(&lux_cdev->cdev, lux_cdev->cdev_id, 1);
	if (ret < 0) {
		pr_err(LUX_CHAR_DRIVER_NAME ": Failed to add cdev to VFS");
		goto cleanup;
	}

	cdev_added = true;

	// 3. Register with sysfs and Device Driver Model
	lux_cdev->device = device_create(lux_class, NULL,
									 lux_cdev->cdev_id, lux_cdev,
									 LUX_CLASS_NAME "-%d", MINOR(lux_cdev->cdev_id));
	if (IS_ERR(lux_cdev->device)) {
		pr_err(LUX_CHAR_DRIVER_NAME ": Failed to create sysfs device");
		ret = PTR_ERR(lux_cdev->device);
		goto cleanup;
	}

	pr_info(LUX_CHAR_DRIVER_NAME ": Probe finished successfully!");

	return 0;

cleanup:

	if (!IS_ERR_OR_NULL(lux_cdev->device)) {
		device_destroy(lux_class, lux_cdev->cdev_id);
		lux_cdev->device = NULL;
	}

	unregister_chrdev_region(lux_cdev->cdev_id, 1);
	lux_cdev->cdev_id = 0;

	if (cdev_added)
		cdev_del(&lux_cdev->cdev);

	kfree(lux_cdev);
	return ret;
}

static void lux_driver_cdev_remove([[maybe_unused]] struct platform_device *platdev) {
	struct lux_cdev *lux_cdev = platform_get_drvdata(platdev);

	if (!IS_ERR_OR_NULL(lux_cdev->device)) {
		device_destroy(lux_class, lux_cdev->cdev_id);
		lux_cdev->device = NULL;
	}

	unregister_chrdev_region(lux_cdev->cdev_id, 1);
	lux_cdev->cdev_id = 0;

	cdev_del(&lux_cdev->cdev);
	kfree(lux_cdev);
}

static struct platform_driver lux_cdev_platdev_driver = {
	.driver = {
		.name = LUX_CHAR_DRIVER_NAME,
		.owner = THIS_MODULE,
	},
	.probe = lux_driver_cdev_probe,
	.remove = lux_driver_cdev_remove,
};

static int __init lux_cdev_init(void) {
	int ret = 0;

	lux_class = class_create(LUX_CLASS_NAME);
	if (IS_ERR(lux_class)) {
		pr_err(LUX_CHAR_DRIVER_NAME ": Failed to create class");
		ret = PTR_ERR(lux_class);
		goto cleanup;
	}

	ret = platform_driver_register(&lux_cdev_platdev_driver);
	if (ret < 0) {
		pr_err(LUX_CHAR_DRIVER_NAME ": Failed to register platform driver.\n");
		goto cleanup;
	}

	return 0;
cleanup:

	if (!IS_ERR_OR_NULL(lux_class)) {
		class_destroy(lux_class);
		lux_class = NULL;
	}

	return ret;
}

static void __exit lux_cdev_exit(void) {
	platform_driver_unregister(&lux_cdev_platdev_driver);

	// This MUST come later after devices have been removed
	if (!IS_ERR_OR_NULL(lux_class)) {
		class_destroy(lux_class);
		lux_class = NULL;
	}
}

module_init(lux_cdev_init);
module_exit(lux_cdev_exit);
MODULE_AUTHOR("m0st4fa");
MODULE_DESCRIPTION("Character driver for exposing smart LED functionality of Lux");
MODULE_LICENSE("GPL");
