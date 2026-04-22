#include "char.h"
#include "asm-generic/barrier.h"
#include "asm-generic/int-ll64.h"
#include "asm-generic/rwonce.h"
#include "asm/io.h"
#include "asm/uaccess.h"
#include "asm/vdso/processor.h"
#include "linux/device/class.h"
#include "linux/fs.h"
#include "linux/printk.h"
#include "linux/spinlock.h"
#include "linux/uaccess.h"
#include "linux/wait.h"
#include "pci.h"

#define BUF_SIZE 512

struct file_operations fops = {
	.open = edu_open,
	.release = edu_release,

	.read = edu_read,
	.write = edu_write,

	.unlocked_ioctl = edu_ioctl,
};
int major;
struct class *class;

int edu_open(struct inode *inode, struct file *filp) {
	pr_info(DEVICE_NAME ": opened character device for edu-pci");

	filp->private_data = &edu_dev;

	return 0;
};
int edu_release(struct inode *inode, struct file *filp) {
	pr_info(DEVICE_NAME ": closed character device for edu-pci");
	return 0;
};

static inline bool is_computing_factorial(struct edu_dev *edev) {
	return edu_hw_read(edev, EDU_REG_STATUS) & EDU_STATUS_COMPUTING;
}

ssize_t edu_read(struct file *filp, char __user *user_buf, size_t user_len, loff_t *user_off) {
	if (*user_off > 0)
		return 0; // EOF

	u32 res = 0;
	struct edu_dev *edev = filp->private_data;
	pr_info(DEVICE_NAME ": edev address %p", edev);
	char buf[BUF_SIZE] = {0};
	ssize_t ret;

	if (!edev)
		return -ENODEV;

	// do {
	// 	spin_lock(&edev->lock);
	//
	// 	done = edu_hw_read(edev, EDU_REG_IRQSTATUS);
	//
	// 	spin_unlock(&edev->lock);
	//
	// 	if (!done)
	// 		schedule();
	//
	// } while (!done);
	ret = wait_event_interruptible(edev->wq, READ_ONCE(edev->done));
	if (ret)
		return ret;

	pr_info(DEVICE_NAME ": device is done computing factorial");

	spin_lock(&edev->lock);
	ret = edev->result;
	spin_unlock(&edev->lock);

	if ((ret = snprintf(buf, BUF_SIZE, "%u", res)) < 0) {
		pr_alert(DEVICE_NAME ": failed to convert factorial result to string");
		return ret;
	};

	pr_info(DEVICE_NAME ": result of operation %u", res);
	pr_info(DEVICE_NAME ": value copied to buffer %s", buf);

	if (copy_to_user(user_buf, buf, ret))
		return -EFAULT;

	*user_off += ret;
	pr_info(DEVICE_NAME ": user offset now %lld", *user_off);
	return ret;
};
ssize_t edu_write(struct file *filp, const char __user *user_buf, size_t user_len, loff_t *user_off) {

	struct edu_dev *edev = filp->private_data;
	u32 number;
	ssize_t ret = 0, n = user_len < BUF_SIZE - 1 ? user_len : BUF_SIZE - 1; // cap copied amount to BUF_SIZE
	char buf[BUF_SIZE] = {0};

	if (!edev)
		return -ENODEV;

	spin_lock(&edev->lock);

	edev->done = 0;

	spin_unlock(&edev->lock);

	if (copy_from_user(buf, user_buf, n))
		return -EFAULT;

	buf[n] = '\0';

	if (buf[n - 1] == '\n')
		buf[n - 1] = '\0';

	if ((ret = kstrtou32(buf, 10, &number))) {
		pr_alert(DEVICE_NAME ": failure converting from string input (%s) to integer", buf);
		return ret;
	}

	if (number > 12)
		return -EINVAL;

	pr_info("Writing %d to factorial register...", number);
	edu_hw_write(edev, EDU_REG_STATUS, EDU_STATUS_RAISEIRQ);
	edu_hw_write(edev, EDU_REG_FACTORIAL, number);
	// wmb();
	// edu_hw_write(edev, EDU_REG_STATUS, 0);

	return n;
};

static ssize_t ioctl_ident(struct edu_dev *edev, u32 __user *arg) {
	u32 val = readl(edev->base + EDU_REG_IDENT);
	return put_user(val, arg);
}

static ssize_t ioctl_liveness(struct edu_dev *edev, u32 __user *arg) {
	u32 challange;

	// 1. Get challange from user
	if (get_user(challange, arg))
		return -EFAULT;

	// 2. Write to device and read back
	edu_hw_write(edev, EDU_REG_LIVENESS, challange);
	rmb(); // Make sure write happens before read; not a problem in X86, but for perfection sake
	challange = edu_hw_read(edev, EDU_REG_LIVENESS);

	// 3. Inform userspace
	return put_user(challange, arg);
}

ssize_t edu_ioctl(struct file *filp, unsigned int cmd, unsigned long arg) {
	struct edu_dev *edev = filp->private_data;

	switch (cmd) {
		case EDU_IOCTL_IDENT:
			return ioctl_ident(edev, (u32 __user *)arg);
		case EDU_IOCTL_LIVENESS:
			return ioctl_liveness(edev, (u32 __user *)arg);
		default:
			return -ENOTTY;
	}
};
