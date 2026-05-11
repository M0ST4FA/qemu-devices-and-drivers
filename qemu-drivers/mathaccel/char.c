#include "asm-generic/errno-base.h"
#include "asm-generic/ioctl.h"
#include "asm/current.h"
#include "linux/cdev.h"
#include "linux/completion.h"
#include "linux/container_of.h"
#include "linux/errno.h"
#include "linux/fs.h"
#include "linux/kdev_t.h"
#include "linux/printk.h"
#include "linux/sched.h"
#include "linux/sched/signal.h"
#include "linux/types.h"
#include "linux/uaccess.h"
#include "linux/wait.h"
#include <linux/atomic.h>

#include "char.h"
#include "device.h"
#include "linux/xarray.h"
#include "uapi.h"

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

	if (n > 0 && buf[n - 1] == '\n')
		buf[n - 1] = '\0';

	if ((ret = kstrtou32(buf, 10, &number))) {
		pr_alert(MATHACCEL_DRIVER_NAME ": failed to convert from string input (%s) to integer", buf);
		return ret;
	}

	writel(number, math_dev->bar[0] + REG_ARG1);
	writel(2, math_dev->bar[0] + REG_ARG2);
	writel(MATH_OP_MUL, math_dev->bar[0] + REG_CMD);

	return n;
};

static ssize_t mathaccel_device_read(struct file *filp, char __user *user_buf, size_t size, loff_t *offset) {

	if (*offset > 0)
		return 0; // EOF

	pr_info(MATHACCEL_DRIVER_NAME ": [PID %d] entered read", current->pid);

	u32 res = 0;
	struct mathaccel_device *mdev = filp->private_data;
	char buf[BUF_SIZE] = {0};
	ssize_t ret = 0;

	if (!mdev)
		return -ENODEV;

	struct wait_queue_entry waitq_entry = {
		.private = get_current(),
		.func = autoremove_wake_function, // Will take the entry as an argument and remove it from waitq
		.entry = {&(waitq_entry.entry), .prev = &(waitq_entry.entry)},
	};

	do {
		// 1. Add myself to waitq
		prepare_to_wait(&mdev->legacy_q, &waitq_entry, TASK_INTERRUPTIBLE);

		// NOTE: in 2, we do compxchg to make sure only one consumer consumes the result

		// 2a. Check whether we woke up because of a device error
		if (atomic_read_acquire(&mdev->legacy_comp_cause) == COMPLETION_CAUSE_ERROR) {
			ret = -EIO;
			break;
		};

		// 2b. Check whether we woke up because a result is there
		if (atomic_cmpxchg(&mdev->legacy_comp_cause, COMPLETION_CAUSE_CMD_DONE, 0) == COMPLETION_CAUSE_CMD_DONE) {
			res = mdev->legacy_res;
			atomic_inc(&mdev->counter);
			break;
		}

		// 3. Handle signals
		if (signal_pending(current)) {
			ret = -ERESTARTSYS;
			break;
		}

		// 4. Check shutdown state
		// NOTE: We do not reset this to 0 (via compxchg) to make sure that all wakers see it
		if (atomic_read_acquire(&mdev->legacy_comp_cause) == COMPLETION_CAUSE_SHUTTING_DOWN) {
			ret = -ENODEV;
			break;
		}

		schedule();

	} while (1);
	// Remove yourself from the waitq and check errors
	finish_wait(&mdev->legacy_q, &waitq_entry); // Remove from waitq
	if (ret < 0)
		return ret;

	pr_info(MATHACCEL_DRIVER_NAME ": [PID %d] woke up with result %llu, counter: %d",
			current->pid, mdev->legacy_res, atomic_read(&mdev->counter));

	if ((ret = snprintf(buf, BUF_SIZE, "%u\n", res)) < 0) {
		pr_alert(MATHACCEL_DRIVER_NAME ": failed to convert math result to string");
		return ret;
	};

	if (copy_to_user(user_buf, buf, ret))
		return -EFAULT;

	*offset += ret;

	return ret;
}

static ssize_t mathaccel_ioc_compute(struct mathaccel_device *mdev, struct mathaccel_req __user *user_req) {
	int ret = 0, res_index;
	enum completion_cause wakeup_cause;
	struct mathaccel_pending *pending = NULL;
	struct mathaccel_req kernel_req;

	// a. Copy request from userspace
	if (copy_from_user(&kernel_req, user_req, sizeof(struct mathaccel_req)))
		return -EFAULT;

	// b. Validate input before touching hardware
	if (kernel_req.opcode >= MATH_OP_COUNT)
		return -EINVAL;

	// c. Give the command an ID, create a pending request for it, and write it into submission queue
	kernel_req.cmd_id = atomic_inc_return(&mdev->cmdid_counter);

	//  Create pending request
	pending = kzalloc(sizeof(*pending), GFP_KERNEL);
	init_completion(&pending->done);
	pending->cmd_id = kernel_req.cmd_id;
	pending->req = kernel_req;
	ret = xa_err(xa_store(&mdev->pending_submissions, pending->cmd_id, pending, GFP_KERNEL));
	if (ret != 0) {
		pr_info("%s: error creating pending submission\n", mdev->name);
		goto error;
	}

	res_index = ret = mathaccel_submit_one_cmd(mdev, &kernel_req);
	if (ret < 0)
		goto error;

	mathaccel_start_dma_job(mdev);

	// d. Block until device siganls completion of a job or shutting down
	ret = wait_for_completion_interruptible(&pending->done);
	wakeup_cause = pending->cause;

	// Spurious; we shouldn't have woken up
	if (ret || wakeup_cause == COMPLETION_CAUSE_CMD_DONE) {
		ret = -ERESTARTSYS;
		goto error;
	}

	// Shutting down
	if (wakeup_cause == COMPLETION_CAUSE_SHUTTING_DOWN) {
		ret = -ENODEV;
		goto error;
	}

	// An error has occured
	if (wakeup_cause == COMPLETION_CAUSE_ERROR) {
		ret = -EIO;
		goto error;
	}

	// e. Copy result back to user space; result is asynchronously copied into `pending` by irq handler
	ret = xa_err(xa_erase(&mdev->pending_submissions, kernel_req.cmd_id));
	if (ret != 0) {
		pr_info("%s: error while erasing pending submission\n", mdev->name);
		goto error;
	}
	if (copy_to_user(user_req, &pending->req, sizeof(struct mathaccel_req))) {
		ret = -EFAULT;
		goto error;
	}

	kfree(pending);
	return 0;

error:
	kfree(pending);
	return ret;
};

static ssize_t mathaccel_device_ioctl(struct file *filp, unsigned int cmd, unsigned long arg) {
	struct mathaccel_device *mdev = filp->private_data;
	int ret = 0;

	if (!mdev)
		return -ENODEV;

	// 1. Reject command types that are not ours (kernel encodes a bunch of stuff in cmd)
	if (_IOC_TYPE(cmd) != MATHACCEL_IOC_MAGIC)
		return -ENOTTY; // Standard response for this case: ENOTTY = wrong ioctl for this device

	// 2. Switch behavior depending on command
	switch (cmd) {
		case MATHACCEL_IOC_COMPUTE:
			return mathaccel_ioc_compute(mdev, (struct mathaccel_req __user *)arg);

		default:
			return -ENOTTY;
	}

	return ret;
};

struct file_operations mathaccel_fops = {
	.owner = &__this_module,

	.open = mathaccel_device_open,
	.release = mathaccel_device_release,

	.write = mathaccel_device_write,
	.read = mathaccel_device_read,
	.unlocked_ioctl = mathaccel_device_ioctl,
};
