#include "include/char.h"
#include "asm-generic/barrier.h"
#include "asm-generic/errno-base.h"
#include "asm-generic/iomap.h"
#include "linux/cdev.h"
#include "linux/container_of.h"
#include "linux/fs.h"
#include "linux/kdev_t.h"
#include "linux/printk.h"
#include "linux/types.h"
#include "pci.h"

#define BUF_SIZE 64

static int mathaccel_device_open(struct inode *inode, struct file *filp) {

	int minor = MINOR(inode->i_rdev);

	if (minor >= MATHACCEL_DEV_NR)
		return -ENODEV;

	filp->private_data = container_of(inode->i_cdev, struct mathaccel_device, cdev);

	return 0;
}

static int mathaccel_device_release(struct inode *inode, struct file *filp) {

	return 0;
}

static ssize_t mathaccel_device_write(struct file *filp, const char __user *user_buf, size_t user_len, loff_t *offset) {
	struct mathaccel_device *math_dev = filp->private_data;
	u32 number;
	ssize_t ret = 0, n = user_len < BUF_SIZE - 1 ? user_len : BUF_SIZE - 1; // cap copied amount to BUF_SIZE
	char buf[BUF_SIZE] = {0};

	if (!math_dev)
		return -ENODEV;

	if (copy_from_user(buf, user_buf, n))
		return -EFAULT;

	buf[n] = '\0';

	if (buf[n - 1] == '\n')
		buf[n - 1] = '\0';

	if ((ret = kstrtou32(buf, 10, &number))) {
		pr_alert(MATHACCEL_DRIVER_NAME ": failed to convert from string input (%s) to integer", buf);
		return ret;
	}

	iowrite32(number, math_dev->bar[0] + MATHACCEL_REG_DATA);
	wmb();
	iowrite32(MATHACCEL_CMD_MULTIPLY, math_dev->bar[0] + MATHACCEL_REG_CMD);

	return n;
};

static ssize_t mathaccel_device_read(struct file *filp, char __user *user_buf, size_t size, loff_t *offset) {
	return 0;

	if (*offset > 0)
		return 0; // EOF

	u32 res = 0;
	struct mathaccel_device *mdev = filp->private_data;
	char buf[BUF_SIZE] = {0};
	ssize_t ret;

	if (!mdev)
		return -ENODEV;

	// ret = wait_event_interruptible(mdev->wq, READ_ONCE(edev->done));
	if (ret)
		return ret;

	// pr_info(DEVICE_NAME ": device is done computing factorial");
	//
	// spin_lock(&mdev->lock);
	// ret = mdev->result;
	// spin_unlock(&mdev->lock);
	//
	// if ((ret = snprintf(buf, BUF_SIZE, "%u", res)) < 0) {
	// 	pr_alert(DEVICE_NAME ": failed to convert factorial result to string");
	// 	return ret;
	// };
	//
	// pr_info(DEVICE_NAME ": result of operation %u", res);
	// pr_info(DEVICE_NAME ": value copied to buffer %s", buf);
	//
	// if (copy_to_user(user_buf, buf, ret))
	// 	return -EFAULT;
	//
	// *user_off += ret;
	// pr_info(DEVICE_NAME ": user offset now %lld", *user_off);
	// return ret;

	return 0;
}

struct file_operations mathaccel_fops = {
	.open = mathaccel_device_open,
	.release = mathaccel_device_release,

	.write = mathaccel_device_write,
	.read = mathaccel_device_read,
};
dev_t firstdev_id;
