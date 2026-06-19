#include "linux/completion.h"
#include "linux/container_of.h"
#include "linux/err.h"
#include "linux/hrtimer_types.h"
#include "linux/ktime.h"
#include "linux/printk.h"
#include "linux/time.h"
#include "linux/timekeeping.h"
#include "linux/types.h"
#include "linux/wait.h"
#include <linux/hrtimer.h>
#include <linux/kthread.h>
#include <linux/module.h>
#include <linux/random.h>

#define LUX_KTHREAD_TIME_NAME "lux-kthread-time"

struct timer_data {
	struct timer_list tl;
	struct hrtimer hrtimer;
	ktime_t expected;
	struct completion hrtimer_fired;
};

static struct task_struct *kthread = NULL;
static struct timer_data data;

static enum hrtimer_restart lux_kthread_hrtimer_function(struct hrtimer *t) {
	struct timer_data *data = container_of(t, struct timer_data, hrtimer);

	pr_info(LUX_KTHREAD_TIME_NAME ": High-res timer fired! Current ktime: %llums, jiffies: %lu\n",
			ktime_to_ms(ktime_get()), jiffies);

	ktime_t hrtimer_interval = 0;
	hrtimer_interval = ktime_set(3, 0);
	hrtimer_interval = ktime_add(hrtimer_interval, get_random_u64() % 500000);

	complete_all(&data->hrtimer_fired);

	u64 overruns = hrtimer_forward(t, ktime_get(), hrtimer_interval);
	pr_info(LUX_KTHREAD_TIME_NAME ": Forwarded timer by %llums. Overruns of previous timer: %llu\n",
			ktime_to_ms(hrtimer_interval), overruns);

	return HRTIMER_RESTART;
}

static int lux_kthread_entry(void *arg) {
	int ret = 0;
	struct timer_data *data = arg;

	ktime_t hrtimer_interval = 0;
	hrtimer_interval = ktime_set(3, 0);
	hrtimer_interval = ktime_add(hrtimer_interval, get_random_u64() % 500000);
	hrtimer_setup(&data->hrtimer, lux_kthread_hrtimer_function,
				  CLOCK_MONOTONIC, HRTIMER_MODE_REL);

	while (!kthread_should_stop()) {
		data->expected = hrtimer_interval;

		pr_info(LUX_KTHREAD_TIME_NAME ": [%d] Kernel thread running...\n", current->pid);

		if (!hrtimer_active(&data->hrtimer)) {
			hrtimer_start(&data->hrtimer, hrtimer_interval, HRTIMER_MODE_REL);
			pr_info(LUX_KTHREAD_TIME_NAME ": [%d] Started hrtimer. Resolution: %uns\n", current->pid, hrtimer_resolution);
		}

		pr_info(LUX_KTHREAD_TIME_NAME ": [%d] About to wait for %llums...\n",
				current->pid, ktime_to_ms(hrtimer_interval));
		wait_for_completion(&data->hrtimer_fired);
		reinit_completion(&data->hrtimer_fired);
	}

	pr_info(LUX_KTHREAD_TIME_NAME ": [%d] Exiting kernel thread...Cleaning up\n", current->pid);
	if (hrtimer_callback_running(&data->hrtimer))
		pr_info(LUX_KTHREAD_TIME_NAME ": [%d] hrtimer is still running. Waiting for it to exit...\n", current->pid);
	else
		pr_info(LUX_KTHREAD_TIME_NAME ": [%d] %llums for hrtimer to expire. Waiting for it to expire...\n",
				current->pid, ktime_to_ms(hrtimer_get_remaining(&data->hrtimer)));

	hrtimer_cancel(&data->hrtimer);

	return ret;
}

static int __init lux_kthread_time_init(void) {

	init_completion(&data.hrtimer_fired);

	kthread = kthread_run(lux_kthread_entry, &data, "lux-kthread-time");
	if (IS_ERR(kthread)) {
		pr_err(LUX_KTHREAD_TIME_NAME ": Failed to create kthread\n");
		return PTR_ERR(kthread);
	}

	return 0;
}

static void __exit lux_kthread_time_exit(void) {
	int ret = 0;

	if (kthread) {
		// Sets the termination flag and waits until kthread returns
		// This means it is the responsibility of the kthread entry function
		// to handle this correctly, otherwise it blocks module exit.
		ret = kthread_stop(kthread);
		pr_err(LUX_KTHREAD_TIME_NAME ": Thread finished with return value: %d\n", ret);
	}
}

module_init(lux_kthread_time_init);
module_exit(lux_kthread_time_exit);

MODULE_AUTHOR("m0st4fa");
MODULE_DESCRIPTION("Runs a kthread comparing Linux kernel clock and timer APIs with the POSIX ones.");
MODULE_LICENSE("GPL");
