#include "asm-generic/errno-base.h"
#include "asm-generic/ioctl.h"
#include "asm/current.h"
#include "linux/capability.h"
#include "linux/container_of.h"
#include "linux/errno.h"
#include "linux/list.h"
#include "linux/printk.h"
#include <linux/clockchips.h>
#include <linux/clocksource.h>
#include <linux/module.h>
#include <linux/sched_clock.h>
#include <linux/types.h>

#include "../../qemu-devices/lux/include/hw.h"
#include "linux/slab.h"
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

	struct lux_clock_subscriber *sub = kzalloc(sizeof(*sub), GFP_KERNEL);
	sub->lux_clock = lux_clock;

	filp->private_data = sub;

	list_add(&sub->node, &lux_clock->subscribers);

	pr_info(LUX_TIMER_DRIVER_NAME ": Opened character interface.\n");

	return 0;
}

static inline void lux_timer_enable_bits(struct lux_clock_subscriber *sub, int bits) {
	struct lux_clock *lux_clock = sub->lux_clock;

	if (bits == 0)
		return;

	u32 ctrl = readl(lux_clock->base + REG_TIMER_CTRL);

	if (!sub->enabled)
		if (bits & (TIMER_BIT | IRQ_BIT)) {
			atomic_inc(&lux_clock->enabled_users);
			sub->enabled = true;
		}

	if (!sub->periodic)
		if (bits & RELOAD_BIT) {
			atomic_inc(&lux_clock->periodic_users);
			sub->periodic = true;
		}

	ctrl |= bits;
	writel(ctrl, lux_clock->base + REG_TIMER_CTRL);
	wmb();
};

static inline void lux_timer_disable_bits(struct lux_clock_subscriber *sub, int bits) {
	struct lux_clock *lux_clock = sub->lux_clock;

	if (bits == 0)
		return;

	u32 ctrl = readl(lux_clock->base + REG_TIMER_CTRL);
	u32 init_ctrl = ctrl;

	if (sub->enabled)
		if (bits & (TIMER_BIT | IRQ_BIT)) {
			// If this is the last user
			if (atomic_dec_and_test(&lux_clock->enabled_users)) {
				if (bits & TIMER_BIT)
					ctrl &= ~TIMER_BIT;

				if (bits & IRQ_BIT)
					ctrl &= ~IRQ_BIT;
			}

			sub->enabled = false;
		}

	if (sub->periodic)
		if (bits & RELOAD_BIT) {
			// If this is the last periodic user
			if (atomic_dec_and_test(&lux_clock->periodic_users))
				ctrl &= ~RELOAD_BIT;

			sub->periodic = false;
		}

	if (ctrl ^ init_ctrl) {
		writel(ctrl, lux_clock->base + REG_TIMER_CTRL);
		wmb();
	}
};

static inline u64 lux_timer_read_virtual_time(struct lux_clock_subscriber *sub) {
	u64 physical_time;

	physical_time = readq(sub->lux_clock->base + REG_TIMER_TIME);
	rmb();

	return physical_time + sub->time_offset;
}

static inline void lux_timer_set_time(struct lux_clock_subscriber *sub, __u64 user_time) {
	struct lux_clock *lux_clock = sub->lux_clock;
	u64 physical_time;

	physical_time = readq(lux_clock->base + REG_TIMER_TIME);
	rmb();

	sub->time_offset = user_time - physical_time;

	// Should not reference count as this is administrative
	u32 ctrl = readl(lux_clock->base + REG_TIMER_CTRL);
	ctrl |= TIMER_BIT;
	writel(ctrl, lux_clock->base + REG_TIMER_CTRL);
	wmb();
}

static inline int disable_async(struct lux_clock_subscriber *sub) {
	if (!capable(CAP_SYS_TIME))
		return -EACCES;

	if (!sub->async)
		return 0;

	if (sub->async_task != current)
		return -EPERM;

	sub->async = false;
	sub->signo = 0;
	put_task_struct(sub->async_task);
	sub->async_task = NULL;

	return 0;
}

int lux_timer_release(struct inode *inode, struct file *filp) {
	struct lux_clock_subscriber *sub = filp->private_data;

	if (sub->async && sub->async_task == current)
		disable_async(sub);

	lux_timer_disable_bits(sub, TIMER_BIT | IRQ_BIT | RELOAD_BIT);

	list_del(&sub->node);
	kfree(sub);

	pr_info(LUX_TIMER_DRIVER_NAME ": Closed character interface.\n");

	return 0;
}

ssize_t lux_timer_read(struct file *filp, char __user *buf, size_t len, loff_t *offset) {
	int ret = 0;
	__u64 irq_data;

	struct lux_clock_subscriber *sub = filp->private_data;
	struct lux_clock *lux_clock = sub->lux_clock;

	if (len < sizeof(sub->irq_data))
		return -ENOSPC;

	if (!sub->async) { // Block only in synchronous mode
		ret = wait_event_interruptible(lux_clock->wait_queue, sub->irq_data != 0);
		if (ret < 0)
			goto out;
	}

	raw_spin_lock_bh(&sub->irq_lock);

	irq_data = sub->irq_data;
	sub->irq_data = 0;

	raw_spin_unlock_bh(&sub->irq_lock);

	ret = put_user(irq_data, (__u64 __user *)buf);
	if (ret < 0)
		goto out;

	*offset = 0; // Make sure offset is always 0; we ignore it anyways

out:
	if (ret == -ERESTART)
		pr_alert(LUX_TIMER_DRIVER_NAME ": [%d] Woke up due to signal...\n", current->pid);

	return ret;
}

ssize_t lux_timer_write(struct file *filp, const char __user *buf, size_t len, loff_t *offset) {
	return -ENOTSUPP;
}

ssize_t lux_timer_ioctl(struct file *filp, unsigned cmd, unsigned long arg) {
	struct lux_clock_subscriber *sub = filp->private_data;
	struct lux_clock *lux_clock = sub->lux_clock;

	__u64 time_now = 0, user_delta = 0, user_time = 0, reg_cmp = 0;

	if (_IOC_TYPE(cmd) != LUX_IOCTL_MAGIC) {
		pr_info(LUX_TIMER_DRIVER_NAME ":\n");
		return -ENOTTY;
	}

	if ((_IOC_DIR(cmd) & _IOC_READ) || (_IOC_DIR(cmd) & _IOC_WRITE))
		if (!access_ok((void __user *)arg, _IOC_SIZE(cmd)))
			return -EFAULT;

	switch (cmd) {
		case LUX_TIME_RD:
			time_now = lux_timer_read_virtual_time(sub);
			return put_user(time_now, (__u64 __user *)arg);
			break;

		case LUX_TIME_SET:
			if (!capable(CAP_SYS_TIME))
				return -EACCES;
			if (get_user(user_time, (__u64 __user *)arg) < 0)
				return -EFAULT;
			lux_timer_set_time(sub, user_time);
			break;

		case LUX_ALM_RD:
			reg_cmp = readq(lux_clock->base + REG_TIMER_CMP);
			rmb();
			reg_cmp += sub->time_offset; // Virtualize timer register
			return put_user(reg_cmp, (__u64 __user *)arg);
			break;

		case LUX_ALM_SET:
			if (!capable(CAP_SYS_TIME))
				return -EACCES;
			if (get_user(reg_cmp, (__u64 __user *)arg) < 0)
				return -EFAULT;

			time_now = lux_timer_read_virtual_time(sub);
			// Neutralize virtualization for now (cmp is not yet virtualized
			user_delta = reg_cmp - time_now;

			// Set a limit on unprivileged users
			if (user_delta < LUX_TIMER_UNPRIV_MIN_DELTA && !capable(CAP_SYS_RESOURCE))
				return -EACCES;

			writeq(reg_cmp - sub->time_offset, lux_clock->base + REG_TIMER_CMP);
			wmb();
			break;

		case LUX_AIE_ON:
			if (!capable(CAP_SYS_TIME))
				return -EACCES;
			lux_timer_enable_bits(sub, TIMER_BIT | IRQ_BIT);
			break;

		case LUX_AIE_OFF:
			if (!capable(CAP_SYS_TIME))
				return -EACCES;
			lux_timer_disable_bits(sub, TIMER_BIT | IRQ_BIT);
			break;

		case LUX_IRQP_SET:
			if (!capable(CAP_SYS_TIME))
				return -EACCES;
			if (get_user(user_delta, (__u64 __user *)arg) < 0)
				return -EFAULT;

			// Set a limit on unprivileged users
			if (user_delta < LUX_TIMER_UNPRIV_MIN_DELTA && !capable(CAP_SYS_RESOURCE))
				return -EACCES;

			time_now = lux_timer_read_virtual_time(sub);
			// Neutralize virtualization for now (cmp is not yet virtualized)
			reg_cmp = time_now + user_delta - sub->time_offset;

			writeq(reg_cmp, lux_clock->base + REG_TIMER_CMP);
			wmb();

			lux_timer_enable_bits(sub, TIMER_BIT | IRQ_BIT | RELOAD_BIT);
			break;

		case LUX_PIE_ON:
			if (!capable(CAP_SYS_TIME))
				return -EACCES;

			lux_timer_enable_bits(sub, TIMER_BIT | IRQ_BIT | RELOAD_BIT);
			break;

		case LUX_PIE_OFF:
			if (!capable(CAP_SYS_TIME))
				return -EACCES;

			lux_timer_disable_bits(sub, TIMER_BIT | IRQ_BIT | RELOAD_BIT);
			break;

		case LUX_ALM_ASYNC_ON:
			if (!capable(CAP_SYS_TIME))
				return -EACCES;

			if (sub->async)
				break;

			// Only one task can be in async mode for now
			if (sub->async_task)
				return -EBUSY;

			if (get_user(sub->signo, (__u32 __user *)arg) < 0)
				return -EFAULT;

			sub->async = true;
			sub->async_task = get_task_struct(current);

			break;

		case LUX_ALM_ASYNC_OFF:
			if (sub->async)
				return disable_async(sub);
			break;

		default:
			return -ENOTTY;
	}

	return 0;
}
