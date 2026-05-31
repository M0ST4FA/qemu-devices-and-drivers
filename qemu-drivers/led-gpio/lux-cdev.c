#include "asm/io.h"
#include "linux/cdev.h"
#include "linux/container_of.h"
#include "linux/device.h"
#include "linux/device/class.h"
#include "linux/err.h"
#include "linux/fs.h"
#include "linux/gfp_types.h"
#include "linux/printk.h"
#include "linux/slab.h"
#include "linux/stddef.h"
#include "linux/types.h"
#include "linux/uaccess.h"
#include <linux/module.h>

#include "../../qemu-devices/lux/include/hw.h"
#include "lux.h"

struct class *lux_class;

struct lux_cdev {
	struct lux_device *lux_device;
	struct cdev cdev;
	struct device *device;
	dev_t cdev_id;
};

static int lux_smart_open(struct inode *inode, struct file *filp) {
	struct lux_cdev *device = NULL;

	device = container_of(inode->i_cdev, struct lux_cdev, cdev);

	filp->private_data = device;

	return 0;
}

static int lux_smart_release(struct inode *inode, struct file *filp) {

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

	void *led_base = device->lux_device->bar[1] + led_id * sizeof(struct smart_led);

	writeq(*(uint64_t *)&led, led_base);

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
	void *led_base = device->lux_device->bar[1] + led_id * sizeof(struct smart_led);

	*(uint64_t *)&led = readq(led_base);

	if (copy_to_user(buf, &led, sizeof(struct smart_led)))
		return -EFAULT;

	*offset += sizeof(struct smart_led);

	return count;
}

static struct file_operations lux_fops = {
	.owner = THIS_MODULE,
	.open = lux_smart_open,
	.release = lux_smart_release,
	.write = lux_smart_write,
	.read = lux_smart_read,
	.llseek = default_llseek,
};

static int lux_driver_cdev_probe(struct lux_device *lux_device) {
	int ret = 0;
	bool cdev_added = false;

	// 1. Allocate private data
	struct lux_cdev *lux_cdev = kzalloc(sizeof(struct lux_cdev), GFP_KERNEL);
	if (lux_cdev == NULL)
		return -ENOMEM;

	ret = alloc_chrdev_region(&lux_cdev->cdev_id, 0, 1, LUX_CHAR_DRIVER_NAME);
	if (ret < 0) {
		pr_err(LUX_CHAR_DRIVER_NAME ": Failed to allocate major and minor numbers for lux device");
		return ret;
	}
	lux_cdev->lux_device = lux_device;
	lux_device->prv_data = lux_cdev;

	// 2. Register with VFS
	cdev_init(&lux_cdev->cdev, &lux_fops);
	ret = cdev_add(&lux_cdev->cdev, lux_cdev->cdev_id, 1);
	if (ret < 0) {
		pr_err(LUX_CHAR_DRIVER_NAME ": Failed to add cdev to VFS");
		goto cleanup;
	}

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

	return ret;
}

static void lux_driver_cdev_remove([[maybe_unused]] struct lux_device *lux_device) {
	struct lux_cdev *lux_cdev = lux_device->prv_data;

	if (!IS_ERR_OR_NULL(lux_cdev->device)) {
		device_destroy(lux_class, lux_cdev->cdev_id);
		lux_cdev->device = NULL;
	}

	unregister_chrdev_region(lux_cdev->cdev_id, 1);
	lux_cdev->cdev_id = 0;

	cdev_del(&lux_cdev->cdev);
}

static struct lux_driver lux_driver = {
	.name = LUX_CHAR_DRIVER_NAME,
	.supported_dev_id = LUX_F0_DEV_ID,
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

	ret = lux_register_driver(&lux_driver);
	if (ret < 0) {
		pr_err(LUX_CHAR_DRIVER_NAME ": Failed to register character driver with lux bus\n");
		goto cleanup;
	};

	return 0;
cleanup:

	if (!IS_ERR_OR_NULL(lux_class)) {
		class_destroy(lux_class);
		lux_class = NULL;
	}

	return ret;
}

static void __exit lux_cdev_exit(void) {
	lux_unregister_driver(&lux_driver); // Calls the remove function for each device

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
