#include "linux/container_of.h"
#include "linux/dev_printk.h"
#include "linux/device.h"
#include "linux/device/devres.h"
#include "linux/err.h"
#include "linux/gfp_types.h"
#include "linux/init.h"
#include <linux/gpio/consumer.h>
#include <linux/gpio/machine.h>
#include <linux/leds.h>
#include <linux/module.h>
#include <linux/platform_device.h>

#include "linux/mod_devicetable.h"
#include "lux.h"

static struct platform_device_id lux_platdev_ids[] = {
	{
		.name = LUX_PLATFORM_DEVICE_NAME,
		.driver_data = 0ULL,
	},
	{},
};
MODULE_DEVICE_TABLE(platform, lux_platdev_ids);

struct lux_led_data {
	struct gpio_desc *gpiod;
	struct led_classdev clsdev;
};

static void lux_led_set_brightness(struct led_classdev *led_clsdev,
								   enum led_brightness brightness) {
	struct lux_led_data *led_data = container_of(led_clsdev, struct lux_led_data, clsdev);
	gpiod_set_value(led_data->gpiod, brightness != LED_OFF);
}

static int lux_led_platform_probe(struct platform_device *platdev) {
#define LED_ARRAY_SZ (sizeof(struct lux_led_data) * 64)

	struct device *device = &platdev->dev;
	struct lux_led_data *leds = devm_kzalloc(device, LED_ARRAY_SZ, GFP_KERNEL);
	int ret = 0;

	for (int i = 0; i < 64; i++) {
		struct lux_led_data *led = leds + i;

		// 1. Ask the kernel for pin named led0
		led->gpiod = devm_gpiod_get_index(device, "led", i, GPIOD_OUT_LOW);
		if (IS_ERR(led->gpiod)) {
			dev_err(device, "Failed to get descriptor for led%d (err: %ld)\n", i, PTR_ERR(led->gpiod));
			return PTR_ERR(led->gpiod);
		}

		// 2. Configure the led class device
		led->clsdev.name = devm_kasprintf(device, GFP_KERNEL, LED_NAME "-%d",
										  i);
		led->clsdev.brightness_set = lux_led_set_brightness;

		// 3. Register with the LED subsystem
		ret = devm_led_classdev_register(device, &led->clsdev);
		if (ret < 0) {
			dev_err(device, "Failed to register led%d with LED class (err: %d)\n", i, ret);
			return ret;
		}
	}

	platform_set_drvdata(platdev, leds);

	dev_info(device, "Successfully registered 64 LEDs\n");

	return 0;
}

static void lux_led_platform_remove(struct platform_device *platdev) {
	struct device *device = &platdev->dev;

	// Free some resources early; the rest will be freed later
	struct lux_led_data *leds = platform_get_drvdata(platdev);

	for (int i = 0; i < 64; i++) {
		struct lux_led_data *led = leds + i;

		devm_led_classdev_unregister(device, &led->clsdev);
		devm_gpiod_put(device, led->gpiod);
	}

	dev_info(device, "Successfully unregistered 64 LEDs\n");
}

struct platform_driver lux_led_platform_driver = {
	.probe = lux_led_platform_probe,
	.remove = lux_led_platform_remove,
	.driver = {
		.name = LUX_PLATFORM_DEVICE_NAME, // Bind the driver to the platform device
		.owner = THIS_MODULE,
	},
	.id_table = lux_platdev_ids,
};

// static __init int lux_led_init(void) {
// 	int ret = 0;
//
// 	ret = platform_driver_register(&lux_led_platform_driver);
// 	if (ret < 0) {
// 		pr_err(LUX_LED_DEVICE_NAME ": Failed to register platform driver (err: %d)", ret);
// 		return ret;
// 	}
//
// 	return 0;
// }
//
// static __exit void lux_led_exit(void) {
// 	platform_driver_unregister(&lux_led_platform_driver);
// }
//
// module_init(lux_led_init);
// module_exit(lux_led_exit);

// Way better thant the previous approach
// In fact, I hade a UAF bug for so long because I forgot to call
// platform_driver_unregister()
module_platform_driver(lux_led_platform_driver);

MODULE_AUTHOR("m0st4fa");
MODULE_DESCRIPTION("LED subsystem driver for lux device");
MODULE_LICENSE("GPL");
