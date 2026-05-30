#include "asm/io.h"
#include <linux/gpio/driver.h>
#include <linux/gpio/machine.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/printk.h>

#include "../../qemu-devices/lux/include/hw.h"
#include "linux/device/devres.h"
#include "linux/err.h"
#include "linux/ioport.h"
#include "linux/overflow.h"
#include "linux/slab.h"
#include "lux.h"

struct lux_gpio_chip {
	struct gpio_chip *chip;
	struct gpiod_lookup_table *led_lookup;
	struct platform_device *led_platdev;
	struct resource led_platdev_resources[0]; // Empty for now
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

static int lux_driver_gpio_probe(struct lux_device *device) {
	int ret = 0;

	// 1. Allocate private data struct
	struct lux_gpio_chip *lux_gpio_chip = kzalloc(sizeof(struct lux_gpio_chip), GFP_KERNEL);
	if (lux_gpio_chip == NULL)
		return -ENOMEM;

	*lux_gpio_chip->chip = (struct gpio_chip){
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

	// 2. Register with gpiolib
	ret = gpiochip_add_data(lux_gpio_chip->chip, device);
	if (ret < 0) {
		pr_err(LUX_CHIP_LABEL ": Error while registering chip\n");
		return ret;
	}
	device->prv_data = lux_gpio_chip;

	// 3. Register GPIO lookup table (maps pins to names) with the GPIO consumer API
	// This lookup table is then used by the consumer API to retrive a gpio descriptor based on name
	lux_gpio_chip->led_lookup = kzalloc(struct_size(lux_gpio_chip->led_lookup, table, 65),
										GFP_KERNEL);
	if (lux_gpio_chip->led_lookup == NULL) {
		pr_err(LUX_CHIP_LABEL ": Failed to allocate lookup table for GPIO consumer API\n");
		ret = -ENOMEM;
		goto cleanup;
	}
	lux_gpio_chip->led_lookup->dev_id = LUX_PLATFORM_DEVICE_NAME; // MUST match platform device name

	for (int i = 0; i < 64; i++) {
		lux_gpio_chip->led_lookup->table[i].key = LUX_CHIP_LABEL;
		lux_gpio_chip->led_lookup->table[i].chip_hwnum = i;
		lux_gpio_chip->led_lookup->table[i].con_id = "led"; // Same name for all
		lux_gpio_chip->led_lookup->table[i].idx = i;		// We use index to differentiate
		lux_gpio_chip->led_lookup->table[i].flags = GPIO_ACTIVE_HIGH;
	}
	// The 65th entry is guaranteed to be 0 by kzalloc

	gpiod_add_lookup_table(lux_gpio_chip->led_lookup);

	// 4. Register a platform device
	// This tells the kernel "Hey, new hardware just dropped!"
	// Notice that normally, you don't create a device; the driver model core creates it for you
	// Here we are creating a device
	// Name is used for driver matching, id indicates instance number (-1 if the only instance)
	lux_gpio_chip->led_platdev = platform_device_register_simple(LUX_PLATFORM_DEVICE_NAME, -1,
																 lux_gpio_chip->led_platdev_resources, 0);
	if (IS_ERR(lux_gpio_chip->led_platdev)) {
		pr_err(LUX_CHIP_LABEL ": Failed to register platform device");
		ret = PTR_ERR(lux_gpio_chip->led_platdev);
		goto cleanup;
	}

	pr_info(LUX_CHIP_LABEL ": Successfully registered chip and device");
	return 0;

cleanup:

	if (!IS_ERR_OR_NULL(lux_gpio_chip->led_platdev))
		platform_device_unregister(lux_gpio_chip->led_platdev);

	if (lux_gpio_chip->led_lookup) {
		gpiod_remove_lookup_table(lux_gpio_chip->led_lookup);
		kfree(lux_gpio_chip->led_lookup);
		lux_gpio_chip->led_lookup = NULL;
	}

	if (lux_gpio_chip->chip)
		gpiochip_remove(lux_gpio_chip->chip);

	if (lux_gpio_chip)
		kfree(lux_gpio_chip);

	return ret;
}

static void lux_driver_gpio_remove(struct lux_device *device) {
	struct lux_gpio_chip *lux_gpio_chip = device->prv_data;

	platform_device_unregister(lux_gpio_chip->led_platdev);
	gpiod_remove_lookup_table(lux_gpio_chip->led_lookup);
	gpiochip_remove(lux_gpio_chip->chip);
	kfree(lux_gpio_chip);
}

static struct lux_driver lux_gpio_chip_driver = {
	.name = "lux-gpio-chip",
	.supported_dev_id = LUX_F0_DEV_ID,
	.probe = lux_driver_gpio_probe,
	.remove = lux_driver_gpio_remove,
};

static int __init lux_gpio_init(void) {
	int ret = 0;

	ret = lux_register_driver(&lux_gpio_chip_driver);
	if (ret < 0)
		pr_err(LUX_CHIP_LABEL ": Failed to register lux driver for GPIO chip with lux core\n");

	return ret;
}

static void __exit lux_gpio_exit(void) {
	lux_unregister_driver(&lux_gpio_chip_driver);
}

module_init(lux_gpio_init);
module_exit(lux_gpio_exit);
MODULE_AUTHOR("m0st4fa");
MODULE_DESCRIPTION("GPIO frontend for Lux device");
MODULE_LICENSE("GPL");
