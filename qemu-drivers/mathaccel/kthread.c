#include "kthread.h"
#include "linux/printk.h"
#include "linux/sched.h"
#include "linux/sched/signal.h"
#include "linux/sched/task.h"
#include "linux/wait.h"
#include <linux/delay.h>
#include <linux/kthread.h>

#include "device.h"
#include "irq.h"

static int kthread_main(void *data) {
	struct mathaccel_device *dev = data;
	enum irq_cause irq_cause;
	char kthread_name[64] = {0};

	snprintf(kthread_name, 64, "%s-%d", MATHACCEL_DRIVER_NAME, dev->minor);
	pr_info("%s: kernel thread started for device minor %d", kthread_name, dev->minor);

	while (!kthread_should_stop()) {

		DEFINE_WAIT(waitq_entry);
		do {
			prepare_to_wait(&dev->kthread_wq, &waitq_entry, TASK_INTERRUPTIBLE);

			if (signal_pending(current)) {
				// Don't know what to do here
			};

			if (kthread_should_stop())
				break;

			irq_cause = dev->irq_cause;
			if (irq_cause != IRQ_CAUSE_NOIRQ) {
				// NOTE: Do not modify the local irq_cause variable (only dev->irq_cause)
				dev->irq_cause = IRQ_CAUSE_NOIRQ;
				break;
			}

			schedule();
		} while (1);
		finish_wait(&dev->kthread_wq, &waitq_entry);

		if (kthread_should_stop())
			break;

		switch (irq_cause) {
			case IRQ_CAUSE_ERROR:
				pr_info("%s: handling error", kthread_name);
				mathaccel_irq_handle_error(dev);
				break;
			case IRQ_CAUSE_CMD_DONE:
				pr_info("%s: handling finished legacy command", kthread_name);
				mathaccel_irq_read_legacy_cmd_result(dev);
				break;
			case IRQ_CAUSE_JOB_DONE:
				pr_info("%s: consuming completion queue", kthread_name);
				mathaccel_irq_consume_completion_queue(dev);
				break;
			default:
				pr_info("%s: spurious wakeup. should never reach here.", kthread_name);
				BUG_ON(irq_cause == IRQ_CAUSE_NOIRQ);
		}
	}

	pr_info("%s: kernel thread stopping for device minor %d", kthread_name, dev->minor);
	return 0;
};

int spawn_kthread(struct mathaccel_device *math_dev) {
	int ret = 0;

	char kthread_name[64] = {0};
	snprintf(kthread_name, 64, "%s-%d", MATHACCEL_DRIVER_NAME, math_dev->minor);
	pr_info("%s: kernel thread started for device minor %d", kthread_name, math_dev->minor);

	math_dev->kthread = kthread_create(kthread_main, math_dev, "%s", kthread_name);
	if (IS_ERR(math_dev->kthread))
		return PTR_ERR(math_dev->kthread);

	get_task_struct(math_dev->kthread);

	ret = wake_up_process(math_dev->kthread);

	if (ret == 0) {
		pr_info("%s: kthread already running", kthread_name);
		return ret;
	}

	return 0;
};
