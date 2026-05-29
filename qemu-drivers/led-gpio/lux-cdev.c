#include "asm/io.h"
#include "linux/cdev.h"
#include "linux/container_of.h"
#include "linux/device.h"
#include "linux/device/class.h"
#include "linux/err.h"
#include "linux/fs.h"
#include "linux/printk.h"
#include "linux/stddef.h"
#include "linux/types.h"
#include <linux/module.h>

#include "../../qemu-devices/lux/include/hw.h"
#include "linux/uaccess.h"
#include "lux.h"

struct class *lux_class;

static int lux_smart_open(struct inode *inode, struct file *filp) {
	struct lux_device *device = NULL;

	device = container_of(inode->i_cdev, struct lux_device, cdev);

	filp->private_data = device;

	return 0;
}

static int lux_smart_release(struct inode *inode, struct file *filp) {

	return 0;
}

static ssize_t lux_smart_write(struct file *filp, const char *buf,
							   size_t count, loff_t *offset) {
	struct lux_device *device = filp->private_data;
	struct smart_led led;
	int led_id;

	if (count != sizeof(struct smart_led))
		return -EINVAL;

	if (*offset >= LED_NR * sizeof(struct smart_led))
		return -ERANGE;

	led_id = *offset / sizeof(struct smart_led);

	if (copy_from_user(&led, buf, sizeof(struct smart_led)))
		return -EFAULT;

	void *led_base = device->bar[1] + led_id * sizeof(struct smart_led);

	writeq(*(uint64_t *)&led, led_base);

	*offset += sizeof(struct smart_led);

	return count;
}

static ssize_t lux_smart_read(struct file *filp, char *buf,
							  size_t count, loff_t *offset) {
	struct lux_device *device = filp->private_data;
	struct smart_led led;
	int led_id;

	if (count != sizeof(struct smart_led))
		return -EINVAL;

	if (*offset >= LED_NR * sizeof(struct smart_led))
		return -ERANGE;

	led_id = *offset / sizeof(struct smart_led);
	void *led_base = device->bar[1] + led_id * sizeof(struct smart_led);

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

static int __init lux_cdev_init(void) {
	int ret = 0;
	bool cdev_added = false;

	if (global_lux == NULL || global_lux->bar[1] == NULL) {
		pr_err(LUX_CHAR_DEVICE_NAME ": Core driver not loaded or BAR 1 not mapped\n");
		return -ENODEV;
	}

	ret = alloc_chrdev_region(&global_lux->cdev_id, 0, 1, LUX_CHAR_DEVICE_NAME);
	if (ret < 0) {
		pr_err(LUX_CHAR_DEVICE_NAME ": Failed to allocate major and minor numbers for lux device");
		return ret;
	}

	cdev_init(&global_lux->cdev, &lux_fops);
	ret = cdev_add(&global_lux->cdev, global_lux->cdev_id, 1);
	if (ret < 0) {
		pr_err(LUX_CHAR_DEVICE_NAME ": Failed to add cdev to VFS");
		goto cleanup;
	}
	cdev_added = true;

	lux_class = class_create(LUX_CLASS_NAME);
	if (IS_ERR(lux_class)) {
		pr_err(LUX_CHAR_DEVICE_NAME ": Failed to create class");
		ret = PTR_ERR(lux_class);
		goto cleanup;
	}

	global_lux->device = device_create(lux_class, NULL,
									   global_lux->cdev_id, global_lux,
									   LUX_CLASS_NAME "-%d", MINOR(global_lux->cdev_id));
	if (IS_ERR(global_lux->device)) {
		pr_err(LUX_CHAR_DEVICE_NAME ": Failed to create sysfs device");
		ret = PTR_ERR(global_lux->device);
		goto cleanup;
	}

	return 0;
cleanup:

	if (!IS_ERR_OR_NULL(global_lux->device)) {
		device_destroy(lux_class, global_lux->cdev_id);
		global_lux->device = NULL;
	}

	if (!IS_ERR_OR_NULL(lux_class)) {
		class_destroy(lux_class);
		lux_class = NULL;
	}

	unregister_chrdev_region(global_lux->cdev_id, 1);
	global_lux->cdev_id = 0;

	if (cdev_added)
		cdev_del(&global_lux->cdev);

	return ret;
}

static void __exit lux_cdev_exit(void) {

	if (global_lux == NULL) {
		pr_alert(LUX_CHAR_DEVICE_NAME ": Private struct is not allocated! (We're in exit path)");
		return;
	}

	if (!IS_ERR_OR_NULL(global_lux->device)) {
		device_destroy(lux_class, global_lux->cdev_id);
		global_lux->device = NULL;
	}

	if (!IS_ERR_OR_NULL(lux_class)) {
		class_destroy(lux_class);
		lux_class = NULL;
	}

	unregister_chrdev_region(global_lux->cdev_id, 1);
	global_lux->cdev_id = 0;

	cdev_del(&global_lux->cdev);
}

module_init(lux_cdev_init);
module_exit(lux_cdev_exit);
MODULE_AUTHOR("m0st4fa");
MODULE_DESCRIPTION("Character driver for exposing smart LED functionality of Lux");
MODULE_LICENSE("GPL");
