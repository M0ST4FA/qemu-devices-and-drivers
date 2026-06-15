#include "asm-generic/errno-base.h"
#include "asm-generic/ioctl.h"
#include "asm/uaccess.h"
#include "linux/capability.h"
#include "linux/container_of.h"
#include "linux/errno.h"
#include "linux/fs.h"
#include "linux/printk.h"
#include <linux/clockchips.h>
#include <linux/clocksource.h>
#include <linux/module.h>
#include <linux/sched_clock.h>
#include <linux/types.h>

#include "../../qemu-devices/lux/include/hw.h"
#include "linux/spinlock.h"
#include "linux/wait.h"
#include "lux.h"
#include "lux_ioctl.h"

int lux_timer_open(struct inode *inode, struct file *filp);
int lux_timer_release(struct inode *inode, struct file *filp);
ssize_t lux_timer_read(struct file *filp, char __user *buf, size_t len, loff_t *offset);
ssize_t lux_timer_write(struct file *filp, const char __user *buf, size_t len, loff_t *offset);
ssize_t lux_timer_ioctl(struct file *filp, unsigned cmd, unsigned long arg);

int lux_timer_open(struct inode *inode, struct file *filp) {
	struct lux_clock *lux_clock = container_of(filp->private_data, struct lux_clock, misc);

	if (test_and_set_bit(0, &lux_clock->is_open) == 1)
		return -EBUSY;

	pr_info(LUX_TIMER_DRIVER_NAME ": Opened character interface.\n");

	return 0;
}

int lux_timer_release(struct inode *inode, struct file *filp) {
	struct lux_clock *lux_clock = container_of(filp->private_data, struct lux_clock, misc);

	pr_info(LUX_TIMER_DRIVER_NAME ": Closed character interface.\n");
	clear_bit(0, &lux_clock->is_open);

	return 0;
}

ssize_t lux_timer_read(struct file *filp, char __user *buf, size_t len, loff_t *offset) {
	int ret = 0;
	__u64 irq_data;

	struct lux_clock *lux_clock = container_of(filp->private_data, struct lux_clock, misc);

	if (len < sizeof(lux_clock->irq_data))
		return -ENOSPC;

	ret = wait_event_interruptible(lux_clock->wait_queue, lux_clock->irq_data != 0);
	if (ret < 0)
		goto out;

	raw_spin_lock_irq(&lux_clock->irq_data_lock);

	irq_data = lux_clock->irq_data;

	lux_clock->irq_data = 0;

	raw_spin_unlock_irq(&lux_clock->irq_data_lock);

	ret = put_user(irq_data, (__u64 __user *)buf);
	if (ret < 0)
		goto out;

	*offset = 0; // Make sure offset is always 0; we ignore it anyways

out:
	if (ret == -ERESTART)
		pr_alert(LUX_TIMER_DRIVER_NAME ": [%d] Woke up due to signal...\n", current->pid);

	if (raw_spin_is_locked(&lux_clock->irq_data_lock))
		raw_spin_unlock_irq(&lux_clock->irq_data_lock);

	return ret;
}

ssize_t lux_timer_write(struct file *filp, const char __user *buf, size_t len, loff_t *offset) {
	return -ENOTSUPP;
}

static inline void lux_timer_enable_bits(struct lux_clock *lux_clock, int bits) {
	u32 ctrl = readl(lux_clock->base + REG_TIMER_CTRL);
	ctrl |= bits;
	writel(ctrl, lux_clock->base + REG_TIMER_CTRL);
	wmb();
};
static inline void lux_timer_disable_bits(struct lux_clock *lux_clock, int bits) {
	u32 ctrl = readl(lux_clock->base + REG_TIMER_CTRL);
	ctrl &= ~bits;
	writel(ctrl, lux_clock->base + REG_TIMER_CTRL);
	wmb();
};

ssize_t lux_timer_ioctl(struct file *filp, unsigned cmd, unsigned long arg) {
	struct lux_clock *lux_clock = container_of(filp->private_data, struct lux_clock, misc);
	__u64 time_now = 0, user_delta = 0, user_time = 0, reg_cmp = 0;

	if (_IOC_TYPE(cmd) != LUX_IOCTL_MAGIC) {
		pr_info(LUX_TIMER_DRIVER_NAME ":\n");
		return -ENOTTY;
	}

	switch (cmd) {
		case LUX_TIME_RD:
			time_now = readq(lux_clock->base + REG_TIMER_TIME);
			rmb();
			return put_user(time_now, (__u64 __user *)arg);
			break;

		case LUX_TIME_SET:
			if (!capable(CAP_SYS_TIME))
				return -EACCES;
			if (get_user(user_time, (__u64 __user *)arg) < 0)
				return -EFAULT;
			writeq(user_time, lux_clock->base + REG_TIMER_TIME);
			wmb();
			lux_timer_enable_bits(lux_clock, TIMER_BIT);
			break;

		case LUX_ALM_RD:
			reg_cmp = readq(lux_clock->base + REG_TIMER_CMP);
			rmb();
			return put_user(reg_cmp, (__u64 __user *)arg);
			break;

		case LUX_ALM_SET:
			if (!capable(CAP_SYS_TIME))
				return -EACCES;
			if (get_user(reg_cmp, (__u64 __user *)arg) < 0)
				return -EFAULT;

			time_now = readq(lux_clock->base + REG_TIMER_TIME);
			rmb();
			user_delta = reg_cmp - time_now;

			// Set a limit on unprivileged users
			if (user_delta < LUX_TIMER_UNPRIV_MIN_DELTA && !capable(CAP_SYS_RESOURCE))
				return -EACCES;

			writeq(reg_cmp, lux_clock->base + REG_TIMER_CMP);
			wmb();
			break;

		case LUX_AIE_ON:
			if (!capable(CAP_SYS_TIME))
				return -EACCES;
			lux_timer_enable_bits(lux_clock, TIMER_BIT | IRQ_BIT);
			break;

		case LUX_AIE_OFF:
			if (!capable(CAP_SYS_TIME))
				return -EACCES;
			lux_timer_disable_bits(lux_clock, TIMER_BIT | IRQ_BIT);
			break;

		case LUX_IRQP_SET:
			if (!capable(CAP_SYS_TIME))
				return -EACCES;
			if (get_user(user_delta, (__u64 __user *)arg) < 0)
				return -EFAULT;

			// Set a limit on unprivileged users
			if (user_delta < LUX_TIMER_UNPRIV_MIN_DELTA && !capable(CAP_SYS_RESOURCE))
				return -EACCES;

			time_now = readq(lux_clock->base + REG_TIMER_TIME);
			rmb();
			reg_cmp = time_now + user_delta;

			writeq(reg_cmp, lux_clock->base + REG_TIMER_CMP);
			wmb();
			lux_timer_enable_bits(lux_clock, TIMER_BIT | IRQ_BIT | RELOAD_BIT);
			break;

		case LUX_PIE_ON:
			if (!capable(CAP_SYS_TIME))
				return -EACCES;
			lux_timer_enable_bits(lux_clock, TIMER_BIT | IRQ_BIT | RELOAD_BIT);
			break;

		case LUX_PIE_OFF:
			if (!capable(CAP_SYS_TIME))
				return -EACCES;
			lux_timer_disable_bits(lux_clock, TIMER_BIT | IRQ_BIT | RELOAD_BIT);
			break;

		default:

			return -ENOTTY;
	}

	return 0;
}
