#include "asm/io.h"
#include "linux/container_of.h"
#include "linux/dev_printk.h"
#include "linux/device.h"
#include "linux/device/devres.h"
#include "linux/interrupt.h"
#include "linux/irqdomain.h"
#include "linux/platform_device.h"
#include "linux/printk.h"
#include <linux/clocksource.h>
#include <linux/module.h>

#include "../../qemu-devices/lux/include/hw.h"
#include "lux.h"

struct lux_cs {
	struct clocksource cs;
	void __iomem *base;
};

[[maybe_unused]]
static irqreturn_t notrace lazy_timer_isr(int irq, void *dev_id) {
	pr_alert(LUX_TIMER_DRIVER_NAME ": TIMER FIRRRRRRRRRRRRRRRRRED!\n");

	return IRQ_NONE;
}

static u64 notrace lux_clocksource_read(struct clocksource *cs) {
	struct lux_cs *lux_cs = container_of(cs, struct lux_cs, cs);
	void __iomem *base = lux_cs->base;

	return readq(base + REG_TIMER_TIME);
}

static int notrace lux_clocksource_enable(struct clocksource *cs) {
	struct lux_cs *lux_cs = container_of(cs, struct lux_cs, cs);
	void __iomem *base = lux_cs->base;

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
	struct lux_cs *lux_cs = container_of(cs, struct lux_cs, cs);
	void __iomem *base = lux_cs->base;

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

static int lux_driver_timer_probe(struct platform_device *platdev) {
	struct device *dev = &platdev->dev;
	struct device *parent = dev->parent;
	struct lux_function *lux_function = dev_get_drvdata(parent);
	int virq, ret;
	struct lux_cs *lux_cs = NULL;

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

	// 2. Request the virq (i.e. register a handler for it)
	ret = devm_request_irq(dev, virq, lazy_timer_isr,
						   IRQF_TIMER | IRQF_IRQPOLL, LUX_TIMER_DRIVER_NAME,
						   platdev);
	if (ret < 0) {
		dev_err(dev, LUX_TIMER_DRIVER_NAME ": Failed to request virq (register a handler with it)\n");
		return ret;
	}

	// 3. Register the clocksource
	lux_cs = devm_kzalloc(dev, sizeof(*lux_cs), GFP_KERNEL);
	lux_cs->base = lux_function->bar[0];
	struct clocksource *cs = &lux_cs->cs;
	cs->name = LUX_TIMER_DRIVER_NAME "-cs";
	cs->rating = LUX_TIMER_CS_RATING;
	cs->flags = CLOCK_SOURCE_IS_CONTINUOUS | CLOCK_SOURCE_SUSPEND_NONSTOP;
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
	platform_set_drvdata(platdev, lux_cs);

	pr_info(LUX_TIMER_DRIVER_NAME ": Clocksource and clockevent loaded. Ready for the storm from the clockevent.\n");

	return 0;
}

static void lux_driver_timer_remove(struct platform_device *platdev) {
	struct lux_cs *lux_cs = platform_get_drvdata(platdev);
	clocksource_unregister(&lux_cs->cs);

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
