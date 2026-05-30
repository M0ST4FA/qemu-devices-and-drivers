#include "asm-generic/errno.h"
#include "dt-bindings/leds/common.h"
#include "linux/array_size.h"
#include "linux/container_of.h"
#include "linux/dev_printk.h"
#include "linux/device.h"
#include "linux/device/devres.h"
#include "linux/gfp_types.h"
#include "linux/init.h"
#include "linux/mod_devicetable.h"
#include <linux/err.h>
#include <linux/gpio/consumer.h>
#include <linux/gpio/machine.h>
#include <linux/led-class-multicolor.h>
#include <linux/leds.h>
#include <linux/module.h>
#include <linux/platform_device.h>

#include "../../qemu-devices/lux/include/hw.h"
#include "lux.h"

static struct platform_device_id lux_platdev_ids[] = {
	{
		.name = LUX_PLATFORM_DEVICE_NAME,
		.driver_data = 0ULL,
	},
	{},
};
MODULE_DEVICE_TABLE(platform, lux_platdev_ids);

enum lux_color {
	COLOR_RED,
	COLOR_GREEN,
	COLOR_BLUE
};

struct lux_mc_led {
	struct led_classdev_mc clsdev;
	struct mc_subled subled_info[3];
	int led_id;
};

static void lux_mc_led_set_brightness(struct led_classdev *led_clsdev,
									  enum led_brightness brightness) {
	struct led_classdev_mc *mc_clsdev = lcdev_to_mccdev(led_clsdev);
	struct lux_mc_led *lux_led = container_of(mc_clsdev, struct lux_mc_led, clsdev);

	// 1. Get the physical pointer for this LED
	void __iomem *led_base = global_lux->bar[1] + (lux_led->led_id * sizeof(struct smart_led));
	struct smart_led led_data = {0};
	led_mc_calc_color_components(mc_clsdev, brightness);

	// 2. Set values
	led_data.color[0] = mc_clsdev->subled_info[COLOR_RED].brightness;
	led_data.color[1] = mc_clsdev->subled_info[COLOR_GREEN].brightness;
	led_data.color[2] = mc_clsdev->subled_info[COLOR_BLUE].brightness;
	led_data.color[3] = 255;

	led_data.state = (brightness > 0) ? 1 : 0;

	// 3. Write values into device
	writeq(*(uint64_t *)&led_data, led_base);
}

static int lux_mc_led_hw_control_is_supported(
	struct led_classdev *led_clsdev,
	unsigned long flag) {

	pr_info(LUX_LED_DEVICE_NAME ": checking whether HW acceleration is supported or not for trigger %ld\n", flag);

	return -EOPNOTSUPP;
};

static int lux_mc_led_platform_probe(struct platform_device *platdev) {
	int ret = 0;
	struct device *dev = &platdev->dev;
	struct lux_mc_led *leds;

	leds = devm_kcalloc(dev, 64, sizeof(struct lux_mc_led), GFP_KERNEL);
	if (!leds)
		return -ENOMEM;

	for (int i = 0; i < 64; i++) {
		struct lux_mc_led *led = leds + i;

		led->led_id = i;

		// 1. Setup subleds
		led->subled_info[COLOR_RED].color_index = LED_COLOR_ID_RED;
		led->subled_info[COLOR_GREEN].color_index = LED_COLOR_ID_GREEN;
		led->subled_info[COLOR_BLUE].color_index = LED_COLOR_ID_BLUE;

		led->subled_info[COLOR_RED].intensity = 255;
		led->subled_info[COLOR_GREEN].intensity = 255;
		led->subled_info[COLOR_BLUE].intensity = 255;

		led->clsdev.subled_info = led->subled_info;
		led->clsdev.num_colors = ARRAY_SIZE(led->subled_info);

		// 2. Setup the main class device
		led->clsdev.led_cdev.name = devm_kasprintf(dev, GFP_KERNEL, LED_MC_NAME "-%d", i);
		led->clsdev.led_cdev.brightness_set = lux_mc_led_set_brightness;
		led->clsdev.led_cdev.hw_control_is_supported = lux_mc_led_hw_control_is_supported;
		led->clsdev.led_cdev.max_brightness = 255;

		// 3. Register with the LED subsystem
		ret = devm_led_classdev_multicolor_register_ext(dev, &led->clsdev, NULL);
		if (ret < 0) {
			dev_err(dev, "Failed to register " LED_MC_NAME "-%d with multicolor LED class (err: %d)\n", i, ret);
			return ret;
		}
	}

	platform_set_drvdata(platdev, leds);

	dev_info(dev, "Successfully Registered 64 Smart Multicolor LEDs\n");

	return 0;
}

static void lux_mc_led_platform_remove(struct platform_device *platdev) {
	struct device *device = &platdev->dev;

	dev_info(device, "Successfully Unregistered 64 Smart Multicolor LEDs\n");
}

struct platform_driver lux_led_platform_driver = {
	.probe = lux_mc_led_platform_probe,
	.remove = lux_mc_led_platform_remove,
	.driver = {
		.name = "lux-led-mc", // Bind the driver to the platform device
		.owner = THIS_MODULE,
	},
	.id_table = lux_platdev_ids,
};
module_platform_driver(lux_led_platform_driver);

MODULE_AUTHOR("m0st4fa");
MODULE_DESCRIPTION("Multicolor LED subsystem driver for lux device");
MODULE_LICENSE("GPL");
