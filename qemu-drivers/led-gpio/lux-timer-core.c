#include "asm-generic/int-ll64.h"
#include "asm-generic/siginfo.h"
#include "asm/io.h"
#include "linux/capability.h"
#include "linux/container_of.h"
#include "linux/cpumask.h"
#include "linux/dev_printk.h"
#include "linux/device.h"
#include "linux/device/devres.h"
#include "linux/fs.h"
#include "linux/hrtimer.h"
#include "linux/init.h"
#include "linux/interrupt.h"
#include "linux/irqdomain.h"
#include "linux/miscdevice.h"
#include "linux/platform_device.h"
#include "linux/printk.h"
#include <linux/clockchips.h>
#include <linux/clocksource.h>
#include <linux/module.h>
#include <linux/sched_clock.h>

#include "../../qemu-devices/lux/include/hw.h"
#include "linux/rbtree.h"
#include "linux/rbtree_types.h"
#include "linux/sched/signal.h"
#include "linux/smp.h"
#include "linux/spinlock.h"
#include "linux/wait.h"
#include "linux/workqueue.h"
#include "lux.h"
#include "lux_ioctl.h"

u64 lux_clocksource_current;

// ------------- CDEV INTERFACE ---------
int lux_timer_open(struct inode *inode, struct file *filp);
int lux_timer_release(struct inode *inode, struct file *filp);
ssize_t lux_timer_read(struct file *filp, char __user *buf, size_t len, loff_t *offset);
ssize_t lux_timer_write(struct file *filp, const char __user *buf, size_t len, loff_t *offset);
ssize_t lux_timer_ioctl(struct file *filp, unsigned cmd, unsigned long arg);

u64 lux_timer_read_virtual_time(struct lux_clock_subscriber *sub);
void lux_reprogram_timer(struct lux_clock *lux_clock);
void lux_timer_enqueue(struct lux_clock_subscriber *new_sub);

static struct file_operations misc_fops = {
	.owner = THIS_MODULE,
	.open = lux_timer_open,
	.release = lux_timer_release,
	.read = lux_timer_read,
	.write = lux_timer_write,
	.unlocked_ioctl = lux_timer_ioctl,
};

// ------------- CLOCKEVENT -------------
static int lux_ce_set_next_event(ulong delta, struct clock_event_device *ce) {
	struct lux_clock *lux_clock = container_of(ce, struct lux_clock, ce);
	void __iomem *base = lux_clock->base;

	// 1. Set comparator
	u64 now = readq(base + REG_TIMER_TIME);
	u64 cmp = now + delta;
	writeq(cmp, base + REG_TIMER_CMP);

	// 2. Make sure timer and IRQs are enabled
	u32 ctrl = readl(base + REG_TIMER_CTRL);
	ctrl |= (TIMER_BIT | IRQ_BIT);
	writel(ctrl, base + REG_TIMER_CTRL);

	return 0;
}

static int lux_ce_set_state_oneshot(struct clock_event_device *ce) {
	struct lux_clock *lux_clock = container_of(ce, struct lux_clock, ce);
	void __iomem *base = lux_clock->base;

	u32 ctrl = readl(base + REG_TIMER_CTRL);
	ctrl |= TIMER_BIT;
	writel(ctrl, base + REG_TIMER_CTRL);
	wmb();

	return 0;
}

static int lux_ce_set_state_oneshot_stopped(struct clock_event_device *ce) {
	struct lux_clock *lux_clock = container_of(ce, struct lux_clock, ce);
	void __iomem *base = lux_clock->base;

	u32 ctrl = readl(base + REG_TIMER_CTRL);
	ctrl &= ~IRQ_BIT;
	writel(ctrl, base + REG_TIMER_CTRL);
	wmb();

	return 0;
}

static int lux_ce_set_state_periodic(struct clock_event_device *ce) {
	struct lux_clock *lux_clock = container_of(ce, struct lux_clock, ce);
	void __iomem *base = lux_clock->base;

	u32 ctrl = readl(base + REG_TIMER_CTRL);
	ctrl |= (TIMER_BIT | IRQ_BIT | RELOAD_BIT);
	writel(ctrl, base + REG_TIMER_CTRL);
	wmb();

	return 0;
}

static int lux_ce_set_state_shutdown(struct clock_event_device *ce) {
	struct lux_clock *lux_clock = container_of(ce, struct lux_clock, ce);
	void __iomem *base = lux_clock->base;

	u32 ctrl = readl(base + REG_TIMER_CTRL);
	ctrl &= ~(TIMER_BIT | IRQ_BIT | RELOAD_BIT);
	writel(ctrl, base + REG_TIMER_CTRL);
	wmb();

	return 0;
}

static void lux_timer_work_func(struct work_struct *t) {
	struct lux_clock *lux_clock = container_of(t, struct lux_clock, timer_work);
	struct rb_root_cached *rb_root = &lux_clock->subscribers;

	pr_alert(LUX_TIMER_DRIVER_NAME ": [PID %d] WORK FIRRRRRRRRRRRRRRRRRED!\n", current->pid);
	pr_alert(LUX_TIMER_DRIVER_NAME ": In interrupt: %ld, in hardirq: %ld, in atomic: %d, in task: %d\n",
			 in_interrupt(), in_hardirq(), in_atomic(), in_task());

	spin_lock_bh(&lux_clock->subscribers_lock);
	u64 phys_now = readq(lux_clock->base + REG_TIMER_TIME);
	rmb();

	while (1) {
		// 1. Get the earliest deadline
		struct rb_node *first = rb_first_cached(rb_root);
		if (!first)
			break; // Tree is empty

		struct lux_clock_subscriber *sub = rb_entry(first, struct lux_clock_subscriber, node);

		// 2. Check if it has actually expired
		if (sub->phys_deadline > phys_now) {
			break; // Nothing left to do: earliest deadline is in the future
		}

		// --- TIMER EXPIRED ---

		// 3. Remove it from the tree temporarily so that we can process it
		rb_erase_cached(&sub->node, rb_root);
		RB_CLEAR_NODE(&sub->node);

		// 4. Wake process up (either through waitqueue or signal)
		raw_spin_lock(&sub->irq_lock);
		sub->irq_data += (1 << 8);

		if (sub->periodic) {
			sub->irq_data |= LUX_CLOCK_PERIODIC;
			sub->deadline += sub->periodic_delta; // Emulated reload
			sub->phys_deadline = sub->deadline - sub->time_offset;
		} else {
			sub->irq_data |= LUX_CLOCK_ALARM;
			sub->deadline = 0;
			sub->phys_deadline = 0;
		}
		raw_spin_unlock(&sub->irq_lock);

		if (sub->async) {
			kernel_siginfo_t info = {0};

			info.si_signo = sub->signo;
			info.si_code = SI_TIMER;
			info.si_int = 1;

			if (sub->async_task != NULL) {
				pr_alert(LUX_TIMER_DRIVER_NAME ": Sending signal to task [%d]...\n",
						 sub->async_task->pid);

				if (send_sig_info(sub->signo, &info, sub->async_task) < 0)
					pr_alert(LUX_TIMER_DRIVER_NAME ": Unable to send signal...\n");
			}
		} else {
			wake_up_interruptible_sync(&sub->wait_queue);
		}

		// 5. Add it again if it were periodic
		if (sub->periodic)
			lux_timer_enqueue(sub);
	}

	// Reprogram timer
	lux_reprogram_timer(lux_clock);

	spin_unlock_bh(&lux_clock->subscribers_lock);
}

static irqreturn_t notrace lux_ce_timer_isr(int irq, void *dev_id) {
	struct lux_clock *lux_clock = dev_id;

	pr_alert(LUX_TIMER_DRIVER_NAME ": TIMER FIRRRRRRRRRRRRRRRRRED!\n");
	pr_alert(LUX_TIMER_DRIVER_NAME ": In interrupt: %lu, in hardirq: %lu, in atomic: %d, in task: %d\n",
			 in_interrupt(), in_hardirq(), in_atomic(), in_task());

	// 1. Ack the hardware
	// writel((1 << HWIRQ_TIMER), base + REG_IRQ_ACK);
	// wmb();
	// NOTE: No need, genirq already calls lux_irq_ack in its flow handler

	// 2. Handle cdev interface
	if (work_pending(&lux_clock->timer_work))
		pr_alert(LUX_TIMER_DRIVER_NAME ": Work is still pending...scheduling new work\n");
	queue_work_on(smp_processor_id(), lux_clock->workqueue, &lux_clock->timer_work);

	// 3. Wakeup the Linux schedular
	if (lux_clock->ce.event_handler)
		lux_clock->ce.event_handler(&lux_clock->ce);

	return IRQ_HANDLED;
}

// ------------- SCHED_CLOCK -------------
static u64 notrace lux_sched_clock_read(void) {
	return lux_clocksource_current;
}

// ------------- CLOCKSOURCE -------------
static u64 notrace lux_clocksource_read(struct clocksource *cs) {
	struct lux_clock *lux_clock = container_of(cs, struct lux_clock, cs);
	void __iomem *base = lux_clock->base;

	lux_clocksource_current = readq(base + REG_TIMER_TIME);
	return lux_clocksource_current;
}

static int notrace lux_clocksource_enable(struct clocksource *cs) {
	struct lux_clock *lux_clock = container_of(cs, struct lux_clock, cs);
	void __iomem *base = lux_clock->base;

	u32 ctrl = readl(base + REG_TIMER_CTRL);
	if (ctrl & 1) {
		pr_info(LUX_TIMER_DRIVER_NAME ": Clocksource already enabled.\n");
		goto out;
	}

	ctrl |= 1;
	writel(ctrl, base + REG_TIMER_CTRL);
	wmb();

	pr_info(LUX_TIMER_DRIVER_NAME ": Enabled clocksource.\n");

out:
	return 0;
}

static void notrace lux_clocksource_disable(struct clocksource *cs) {
	struct lux_clock *lux_clock = container_of(cs, struct lux_clock, cs);
	void __iomem *base = lux_clock->base;

	u32 ctrl = readl(base + REG_TIMER_CTRL);
	if ((ctrl & 1) == 0) {
		pr_info(LUX_TIMER_DRIVER_NAME ": Clocksource already disabled.\n");
		goto out;
	}

	ctrl &= ~1;
	writel(ctrl, base + REG_TIMER_CTRL);
	wmb();

	pr_info(LUX_TIMER_DRIVER_NAME ": Disabled clocksource.\n");

out:
	return;
}

// ------------- BACKBONE -------------
static int lux_driver_timer_probe(struct platform_device *platdev) {
	struct device *dev = &platdev->dev;
	struct device *parent = dev->parent;
	struct lux_function *lux_function = dev_get_drvdata(parent);
	int virq, ret;
	struct lux_clock *lux_clock = NULL;

	// 1. Get a virq for the timer
	if (!lux_function || !lux_function->irq_domain) {
		dev_err(dev, LUX_TIMER_DRIVER_NAME ": Either the PCI function is not initialized or the IRQ domain is not initialized\n");
		return -ENODEV;
	}

	virq = irq_find_mapping(lux_function->irq_domain, HWIRQ_TIMER);
	if (!virq) {
		dev_err(dev, LUX_TIMER_DRIVER_NAME ": Failed to find virq mapping for timer.\n");
		return -EINVAL;
	}
	lux_clock = devm_kzalloc(dev, sizeof(*lux_clock), GFP_KERNEL);
	lux_clock->base = lux_function->bar[0];

	// 2. Register interrupt handlers
	pr_info(LUX_IRQ_DRIVER_NAME ": domain ptr %p, virq: %d\n", lux_function->irq_domain, virq);
	ret = devm_request_irq(dev, virq, lux_ce_timer_isr,
						   IRQF_TIMER | IRQF_IRQPOLL, LUX_TIMER_DRIVER_NAME,
						   lux_clock);
	if (ret < 0) {
		dev_err(dev, LUX_TIMER_DRIVER_NAME ": Failed to request virq (register a handler with it)\n");
		return ret;
	}
	lux_clock->workqueue = create_singlethread_workqueue("lux-timer");
	if (lux_clock->workqueue == NULL) {
		dev_err(dev, LUX_TIMER_DRIVER_NAME ": Failed to allocate workqueue\n");
		return -ENOMEM;
	}

	INIT_WORK(&lux_clock->timer_work, lux_timer_work_func);

	// 3. Register the clocksource
	struct clocksource *cs = &lux_clock->cs;
	cs->name = LUX_TIMER_DRIVER_NAME "-cs";
	cs->rating = LUX_TIMER_CS_RATING;
	cs->flags = CLOCK_SOURCE_IS_CONTINUOUS | CLOCK_SOURCE_SUSPEND_NONSTOP;
	cs->mask = CLOCKSOURCE_MASK(64);
	cs->read = lux_clocksource_read;
	cs->enable = lux_clocksource_enable;
	cs->disable = lux_clocksource_disable;

	cs->shift = 24;
	cs->mult = clocksource_hz2mult(LUX_TIMER_RATE, cs->shift);

	ret = clocksource_register_hz(cs, LUX_TIMER_RATE);
	if (ret < 0) {
		dev_err(dev, LUX_TIMER_DRIVER_NAME ": Failed to register clocksource (err: %d)\n", ret);
		return -1;
	}
	platform_set_drvdata(platdev, lux_clock);

	// 4. Register sched_clock
	// sched_clock_register(lux_sched_clock_read, 64, 1000); // Doesn't work with KVM (hardcodes itself)

	// 5. Register clockevent
	struct clock_event_device *ce = &lux_clock->ce;
	ce->name = LUX_TIMER_DRIVER_NAME "-ce";
	ce->rating = 100;
	ce->features = CLOCK_EVT_FEAT_ONESHOT | CLOCK_EVT_FEAT_PERIODIC;
	ce->set_state_oneshot = lux_ce_set_state_oneshot;
	ce->set_state_oneshot_stopped = lux_ce_set_state_oneshot_stopped;
	ce->set_next_event = lux_ce_set_next_event;
	ce->set_state_periodic = lux_ce_set_state_periodic;
	ce->set_state_shutdown = lux_ce_set_state_shutdown;
	ce->cpumask = cpumask_of(0);
	ce->irq = virq;
	lux_clock->ce_cpu = 0;

	clockevents_config_and_register(ce, LUX_TIMER_RATE, 100000, 0xFFFFFFFF);

	// 6. Register miscdevice
	lux_clock->misc.minor = MISC_DYNAMIC_MINOR;
	lux_clock->misc.name = LUX_TIMER_DRIVER_NAME;
	lux_clock->misc.mode = 0666;
	lux_clock->misc.fops = &misc_fops;

	ret = misc_register(&lux_clock->misc);
	if (ret < 0) {
		pr_err(LUX_TIMER_DRIVER_NAME ": Failed to register miscdevice for cdev interface.\n");
		return ret;
	}
	lux_clock->subscribers = RB_ROOT_CACHED;

	pr_info(LUX_TIMER_DRIVER_NAME ": Clocksource and clockevent loaded. Ready for the storm from the clockevent.\n");

	return 0;
}

static void lux_driver_timer_remove(struct platform_device *platdev) {
	int ret = 0;
	struct device *dev = &platdev->dev;
	struct lux_clock *lux_clock = platform_get_drvdata(platdev);

	if (work_pending(&lux_clock->timer_work)) {
		pr_alert(LUX_TIMER_DRIVER_NAME ": Pending work detected while removing timer device...flushing\n");
		flush_work(&lux_clock->timer_work);
	}

	destroy_workqueue(lux_clock->workqueue);

	misc_deregister(&lux_clock->misc);

	ret = clocksource_unregister(&lux_clock->cs);
	if (ret < 0) {
		dev_err(dev, "Failed to unregister clock source.\n");
	}

	ret = clockevents_unbind_device(&lux_clock->ce, lux_clock->ce_cpu);
	if (ret < 0) {
		dev_err(dev, "Failed to unbind clockevent for CPU %d.\n", lux_clock->ce_cpu);
	}

	pr_info(LUX_TIMER_DRIVER_NAME ": Clocksource and clockevent unloaded.\n");
}

static struct platform_driver lux_timer_platdev_driver = {
	.driver = {
		.name = LUX_TIMER_DRIVER_NAME,
		.owner = THIS_MODULE,
	},
	.probe = lux_driver_timer_probe,
	.remove = lux_driver_timer_remove,
};

module_platform_driver(lux_timer_platdev_driver);
MODULE_AUTHOR("m0st4fa");
MODULE_DESCRIPTION("Timer chip driver for Lux multifunction device");
MODULE_LICENSE("GPL");
