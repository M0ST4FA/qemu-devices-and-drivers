#include "linux/device.h"
#include "linux/interrupt.h"
#include "linux/irqdomain.h"
#include "linux/platform_device.h"
#include "linux/printk.h"
#include <linux/module.h>

#include "../../qemu-devices/lux/include/hw.h"
#include "lux.h"

[[maybe_unused]]
static irqreturn_t lazy_timer_isr(int irq, void *dev_id) {
	pr_alert(LUX_TIMER_DRIVER_NAME ": TIMER FIRRRRRRRRRRRRRRRRRED!\n");
	return IRQ_NONE;
}

static int lux_driver_timer_probe(struct platform_device *platdev) {
	struct device *parent = platdev->dev.parent;
	struct lux_function *lux_function = dev_get_drvdata(parent);
	int virq, ret;

	// 1. Get a virq for the timer
	if (!lux_function || !lux_function->irq_domain) {
		pr_err(LUX_TIMER_DRIVER_NAME ": Either the PCI function is not initialized or the IRQ domain is not initialized\n");
		return -ENODEV;
	}

	virq = irq_find_mapping(lux_function->irq_domain, HWIRQ_TIMER);
	if (!virq) {
		pr_err(LUX_TIMER_DRIVER_NAME ": Failed to find virq mapping for timer.\n");
		return -EINVAL;
	}

	// 2. Request the virq (i.e. register a handler for it)
	ret = devm_request_irq(&platdev->dev, virq, lazy_timer_isr,
						   IRQF_TIMER | IRQF_IRQPOLL, LUX_TIMER_DRIVER_NAME,
						   platdev);
	if (ret < 0) {
		pr_err(LUX_TIMER_DRIVER_NAME ": Failed to request virq (register a handler with it)\n");
		return ret;
	}

	pr_info(LUX_TIMER_DRIVER_NAME ": Lazy timer loaded. Ready for the storm.\n");

	return 0;
}

static void lux_driver_timer_remove(struct platform_device *platdev) {
	pr_info(LUX_TIMER_DRIVER_NAME ": Lazy timer unloaded. Ready for the storm.\n");
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
