#pragma once

#include "linux/kthread.h"
#include "linux/printk.h"

#include "device.h"
#include "linux/sched/task.h"

int spawn_kthread(struct mathaccel_device *math_dev);

static inline void stop_kthread(struct mathaccel_device *math_dev) {
	if (!math_dev->kthread)
		return;

	if (kthread_stop(math_dev->kthread) < 0)
		pr_info(MATHACCEL_DRIVER_NAME "failed to stop kernel thread");

	put_task_struct(math_dev->kthread);
	math_dev->kthread = NULL;
};
