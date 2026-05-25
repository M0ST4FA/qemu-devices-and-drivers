#include "asm/io.h"
#include <linux/gpio/driver.h>
#include <linux/gpio/machine.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/printk.h>

#include "../../qemu-devices/led-gpio/include/hw.h"
#include "lux.h"

static struct gpiod_lookup_table lux_led_lookup = {
	.dev_id = NULL,
	.table = {
		GPIO_LOOKUP("lux-gpio-chip", 0, "led0", GPIO_ACTIVE_HIGH),
		{},
	},
};

static int lux_gpio_direction_output(struct gpio_chip *gc, unsigned int offset, int value) {

	struct lux_device *lux_device = gpiochip_get_data(gc);
	void __iomem *bar0 = lux_device->bar[0];

	// 1. Set direction to output
	int64_t pin_mask = (1ULL << offset);
	int64_t original_direction = readq(bar0 + REG_DIRECTION);
	int64_t new_direction = pin_mask | original_direction;
	writeq(new_direction, bar0 + REG_DIRECTION);
	wmb();

	// 2. Set value
	if (value == 0)
		writeq(pin_mask, bar0 + REG_CLR);
	else
		writeq(pin_mask, bar0 + REG_SET);

	pr_info(LUX_CHIP_LABEL ": (direction_output) Direction: %llx, %u=%d", new_direction, offset, value);

	return 0;
}

static int lux_gpio_direction_input(struct gpio_chip *gc, unsigned int offset) {
	struct lux_device *lux_device = gpiochip_get_data(gc);
	void __iomem *bar0 = lux_device->bar[0];

	// 1. Set direction to input
	int64_t pin_mask = (1ULL << offset);
	int64_t original_direction = readq(bar0 + REG_DIRECTION);
	int64_t new_direction = ~pin_mask & original_direction;
	writeq(new_direction, bar0 + REG_DIRECTION);
	wmb();

	// 2. Read value
	int64_t value = (readq(bar0 + REG_DATA) >> offset) & 1ULL;

	pr_info(LUX_CHIP_LABEL ": (direction_input) Direction: %llx, %u=%lld", new_direction, offset, value);

	return 0;
}

static int lux_gpio_get_direction(struct gpio_chip *gc, unsigned int offset) {
	struct lux_device *lux_device = gpiochip_get_data(gc);
	void __iomem *bar0 = lux_device->bar[0];

	// Hardware: 1 is Output, 0 is Input.
	// Kernel: 1 is Input, 0 is Output.
	int hw_direction = (readq(bar0 + REG_DIRECTION) >> offset) & 1ULL;
	int kernel_direction = hw_direction == LUX_DIRECTION_OUT ? GPIO_LINE_DIRECTION_OUT
															 : GPIO_LINE_DIRECTION_IN;

	pr_info(LUX_CHIP_LABEL ": (get_direction) Direction of pin %d = hw:%d kernel:%d", offset, hw_direction, kernel_direction);

	return kernel_direction;
}

static int lux_gpio_set(struct gpio_chip *gc, unsigned int offset, int value) {
	struct lux_device *lux_device = gpiochip_get_data(gc);
	void __iomem *bar0 = lux_device->bar[0];

	int64_t pin_mask = (1ULL << offset);
	if (value == 0)
		writeq(pin_mask, bar0 + REG_CLR);
	else
		writeq(pin_mask, bar0 + REG_SET);

	pr_info(LUX_CHIP_LABEL ": (set) Set pin %d = %d", offset, value);

	return 0;
}
static int lux_gpio_get(struct gpio_chip *gc, unsigned int offset) {
	struct lux_device *lux_device = gpiochip_get_data(gc);
	void __iomem *bar0 = lux_device->bar[0];
	int value = (readq(bar0 + REG_DATA) >> offset) & 1ULL;

	pr_info(LUX_CHIP_LABEL ": (get) Get pin %d = %d", offset, value);

	return value;
}

static int lux_gpio_set_multiple(struct gpio_chip *gc,
								 unsigned long *mask, unsigned long *bits) {
	struct lux_device *lux_device = gpiochip_get_data(gc);
	void __iomem *bar0 = lux_device->bar[0];

	// Atomic set and clear (prevent RMW)
	writeq(*mask & *bits, bar0 + REG_SET);
	writeq(*mask & ~*bits, bar0 + REG_CLR);

	pr_info(LUX_CHIP_LABEL ": (set_multiple) Set a bunch of pins (%lx=%lx) :)", *mask, *bits);

	return 0;
}
static int lux_gpio_get_multiple(struct gpio_chip *gc,
								 unsigned long *mask, unsigned long *bits) {
	struct lux_device *lux_device = gpiochip_get_data(gc);
	void __iomem *bar0 = lux_device->bar[0];

	int64_t data_reg = readq(bar0 + REG_DATA);
	*bits = data_reg & *mask;

	pr_info(LUX_CHIP_LABEL ": (get_multiple) Get a bunch of pins (%lx=%lx) :)", *mask, *bits);

	return 0;
}

static struct gpio_chip lux_gpio_chip = {
	.label = LUX_CHIP_LABEL,
	.base = -1,
	.ngpio = 64,
	.owner = THIS_MODULE,
	.direction_output = lux_gpio_direction_output,
	.direction_input = lux_gpio_direction_input,
	.get_direction = lux_gpio_get_direction,
	.set = lux_gpio_set,
	.get = lux_gpio_get,
	.set_multiple = lux_gpio_set_multiple,
	.get_multiple = lux_gpio_get_multiple,
};

static int __init lux_gpio_init(void) {
	int ret = 0;

	if (global_lux == NULL || global_lux->bar[0] == NULL) {
		pr_err("Lux device not present...probably module lux-core not loaded yet");
		return -ENODEV;
	}

	ret = gpiochip_add_data(&lux_gpio_chip, global_lux);
	if (ret < 0) {
		pr_err(LUX_CHIP_LABEL ": Error while registering chip");
	}

	gpiod_add_lookup_table(&lux_led_lookup);

	pr_info(LUX_CHIP_LABEL ": Successfully registered chip");

	return ret;
}

static void __exit lux_gpio_exit(void) {
	gpiod_remove_lookup_table(&lux_led_lookup);
}

module_init(lux_gpio_init);
module_exit(lux_gpio_exit);
MODULE_AUTHOR("m0st4fa");
MODULE_DESCRIPTION("GPIO frontend for Lux device");
MODULE_LICENSE("GPL");
