#include "asm/io.h"
#include "linux/container_of.h"
#include "linux/cpumask.h"
#include "linux/cpumask_types.h"
#include "linux/dev_printk.h"
#include "linux/device.h"
#include "linux/device/devres.h"
#include "linux/hrtimer.h"
#include "linux/interrupt.h"
#include "linux/irqdomain.h"
#include "linux/platform_device.h"
#include "linux/printk.h"
#include <linux/clockchips.h>
#include <linux/clocksource.h>
#include <linux/module.h>
#include <linux/sched_clock.h>

#include "../../qemu-devices/lux/include/hw.h"
#include "lux.h"

u64 lux_clocksource_current;

struct lux_clock {
	struct clocksource cs;
	struct clock_event_device ce;
	void __iomem *base;
	int ce_cpu;
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
	ctrl |= (TIMER_BIT | IRQ_BIT);
	writel(ctrl, base + REG_TIMER_CTRL);
	wmb();

	return 0;
}

static int lux_ce_set_state_oneshot_stopped(struct clock_event_device *ce) {
	struct lux_clock *lux_clock = container_of(ce, struct lux_clock, ce);
	void __iomem *base = lux_clock->base;

	u32 ctrl = readl(base + REG_TIMER_CTRL);
	ctrl &= ~(TIMER_BIT | IRQ_BIT);
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

static irqreturn_t notrace lux_ce_timer_isr(int irq, void *dev_id) {
	struct lux_clock *lux_clock = dev_id;
	void __iomem *base = lux_clock->base;

	pr_alert(LUX_TIMER_DRIVER_NAME ": TIMER FIRRRRRRRRRRRRRRRRRED!\n");

	// 1. Ack the hardware
	u32 irq_status = readl(base + REG_IRQ_STATUS);
	irq_status |= (1 << HWIRQ_TIMER);
	writel(irq_status, base + REG_IRQ_ACK);
	wmb();

	// 2. Wakeup the Linux schedular
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

	// 2. Request the virq (i.e. register a handler for it)
	ret = devm_request_irq(dev, virq, lux_ce_timer_isr,
						   IRQF_TIMER | IRQF_IRQPOLL, LUX_TIMER_DRIVER_NAME,
						   lux_clock);
	if (ret < 0) {
		dev_err(dev, LUX_TIMER_DRIVER_NAME ": Failed to request virq (register a handler with it)\n");
		return ret;
	}

	// 3. Register the clocksource
	struct clocksource *cs = &lux_clock->cs;
	cs->name = LUX_TIMER_DRIVER_NAME "-cs";
	cs->rating = LUX_TIMER_CS_RATING;
	cs->flags = CLOCK_SOURCE_IS_CONTINUOUS | CLOCK_SOURCE_SUSPEND_NONSTOP | CLOCK_SOURCE_HAS_COUPLED_CLOCK_EVENT;
	cs->mask = CLOCKSOURCE_MASK(64);
	cs->read = lux_clocksource_read;
	cs->enable = lux_clocksource_enable;
	cs->disable = lux_clocksource_disable;

	cs->shift = 24;
	cs->mult = clocksource_hz2mult(LUX_TIMER_CS_RATE, cs->shift);

	ret = clocksource_register_hz(cs, LUX_TIMER_CS_RATE);
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
	ce->rating = 600;
	ce->features = CLOCK_EVT_FEAT_ONESHOT;
	ce->set_state_oneshot = lux_ce_set_state_oneshot;
	ce->set_state_oneshot_stopped = lux_ce_set_state_oneshot_stopped;
	ce->set_next_event = lux_ce_set_next_event;
	ce->set_state_periodic = lux_ce_set_state_periodic;
	ce->set_state_shutdown = lux_ce_set_state_shutdown;
	ce->cpumask = cpumask_of(0);
	ce->irq = virq;
	lux_clock->ce_cpu = 0;

	clockevents_config_and_register(ce, LUX_TIMER_CS_RATE, 1, 0xFFFFFFFF);

	pr_info(LUX_TIMER_DRIVER_NAME ": Clocksource and clockevent loaded. Ready for the storm from the clockevent.\n");

	return 0;
}

static void lux_driver_timer_remove(struct platform_device *platdev) {
	int ret = 0;
	struct device *dev = &platdev->dev;
	struct lux_clock *lux_clock = platform_get_drvdata(platdev);

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
