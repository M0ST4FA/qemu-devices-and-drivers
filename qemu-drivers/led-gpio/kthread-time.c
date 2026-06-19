#include "linux/completion.h"
#include "linux/container_of.h"
#include "linux/err.h"
#include "linux/hrtimer_types.h"
#include "linux/jiffies.h"
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
#include <linux/timer.h>

#define LUX_KTHREAD_TIME_NAME "lux-kthread-time"

struct timer_data {
	struct timer_list tl;
	struct hrtimer hrtimer;
	struct timer_list tltimer;

	ktime_t hr_deadline;

	ktime_t tl_deadline;

	struct completion hr_fired;
	struct completion tl_fired;
};

static struct task_struct *kthread = NULL;
static struct timer_data data;

static enum hrtimer_restart lux_kthread_hrtimer_function(struct hrtimer *t) {
	struct timer_data *data = container_of(t, struct timer_data, hrtimer);
	u64 curr_jiffies = jiffies;
	ktime_t curr_ktime = ktime_get();
	ktime_t diff = curr_ktime - data->hr_deadline;

	pr_info(LUX_KTHREAD_TIME_NAME ": High-res timer fired! Current ktime: %llums, jiffies: %llu, diff: %lldms\n",
			ktime_to_ms(curr_ktime), curr_jiffies, diff);

	data->hr_deadline = ktime_get();
	data->hr_deadline = ktime_add_ms(data->hr_deadline,
									 3000 + get_random_u64() % 500);

	complete_all(&data->hr_fired);

	// u64 overruns = hrtimer_forward(t, ktime_get(), data->hr_interval);
	hrtimer_set_expires(t, data->hr_deadline);
	pr_info(LUX_KTHREAD_TIME_NAME ": Forwarded timer to %llums\n",
			ktime_to_ms(data->hr_deadline));

	return HRTIMER_RESTART;
}

static void lux_kthread_tltimer_function(struct timer_list *t) {
	struct timer_data *data = timer_container_of(data, t, tltimer);
	u64 curr_jiffies = jiffies;
	s64 diff_jiffies = jiffies - data->tl_deadline;
	s64 diff_ms = jiffies_to_msecs(diff_jiffies);

	pr_info(LUX_KTHREAD_TIME_NAME ": tltimer fired! Current jiffies: %llu, diff: (%lld jiffies, %lldms)\n",
			curr_jiffies, diff_jiffies, diff_ms);
	complete_all(&data->tl_fired);
}

static void setup_timers(struct timer_data *data) {

	// 1. Calculate intervals and deadlines
	data->hr_deadline = ktime_get();
	data->hr_deadline = ktime_add_ms(data->hr_deadline,
									 3000 + get_random_u64() % 500);

	typeof(jiffies) tl_deadline = jiffies + secs_to_jiffies(2) + msecs_to_jiffies(get_random_u64() % 5000);
	data->tl_deadline = tl_deadline;

	// Activate hrtimer
	hrtimer_setup(&data->hrtimer, lux_kthread_hrtimer_function,
				  CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	hrtimer_start(&data->hrtimer, data->hr_deadline, HRTIMER_MODE_ABS);
	pr_info(LUX_KTHREAD_TIME_NAME ": [%d] Started hrtimer. Resolution: %uns\n", current->pid, hrtimer_resolution);

	// Activate tltimer
	timer_setup(&data->tltimer, lux_kthread_tltimer_function, 0);
	mod_timer(&data->tltimer, tl_deadline);
	pr_info(LUX_KTHREAD_TIME_NAME ": [%d] Started tltimer...\n", current->pid);
}

static void cancel_timers(struct timer_data *data) {
	// 1. Cancel hrtimer
	pr_info(LUX_KTHREAD_TIME_NAME ": [%d] Exiting kernel thread...Cleaning up\n", current->pid);
	if (hrtimer_callback_running(&data->hrtimer))
		pr_info(LUX_KTHREAD_TIME_NAME ": [%d] hrtimer is still running. Waiting for it to exit...\n", current->pid);
	else
		pr_info(LUX_KTHREAD_TIME_NAME ": [%d] %llums for hrtimer to expire. Waiting for it...\n",
				current->pid, ktime_to_ms(hrtimer_get_remaining(&data->hrtimer)));

	hrtimer_cancel(&data->hrtimer);

	// 2. Cancel tltimer
	if (timer_pending(&data->tltimer))
		pr_info(LUX_KTHREAD_TIME_NAME ": [%d] tltimer is still pending...\n", current->pid);

	timer_delete_sync(&data->tltimer);
}

static int lux_kthread_entry(void *arg) {
	int ret = 0;
	struct timer_data *data = arg;
	setup_timers(data);

	while (!kthread_should_stop()) {
		pr_info(LUX_KTHREAD_TIME_NAME ": [%d] Kernel thread running...\n", current->pid);

		if (!hrtimer_active(&data->hrtimer)) {
			data->hr_deadline = ktime_get();
			data->hr_deadline = ktime_add_ms(data->hr_deadline,
											 3000 + get_random_u64() % 500);

			hrtimer_start(&data->hrtimer, data->hr_deadline, HRTIMER_MODE_ABS);
			pr_info(LUX_KTHREAD_TIME_NAME ": [%d] Restarted hrtimer. Resolution: %uns\n", current->pid, hrtimer_resolution);
			pr_info(LUX_KTHREAD_TIME_NAME ": [%d] About to wait for %llums...\n",
					current->pid, ktime_to_ms(data->hr_deadline));
		}

		if (!timer_pending(&data->tltimer)) {
			typeof(jiffies) tl_deadline = jiffies + secs_to_jiffies(2) + msecs_to_jiffies(get_random_u64() % 5000);
			data->tl_deadline = tl_deadline;

			mod_timer(&data->tltimer, data->tl_deadline);
			pr_info(LUX_KTHREAD_TIME_NAME ": [%d] Restarted tltimer\n", current->pid);
			pr_info(LUX_KTHREAD_TIME_NAME ": [%d] About to wait until jiffies = %llu...\n",
					current->pid, data->tl_deadline);
		}

		wait_for_completion(&data->hr_fired);
		reinit_completion(&data->hr_fired);

		pr_info(LUX_KTHREAD_TIME_NAME ": [%d] Returned from waiting for hrtimer...\n", current->pid);

		wait_for_completion(&data->tl_fired);
		reinit_completion(&data->tl_fired);
	}

	cancel_timers(data);

	return ret;
}

static int __init lux_kthread_time_init(void) {

	init_completion(&data.hr_fired);
	init_completion(&data.tl_fired);

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
