#include "linux/container_of.h"
#include "linux/err.h"
#include "linux/printk.h"
#include "linux/stddef.h"
#include <linux/gpio/consumer.h>
#include <linux/gpio/machine.h>
#include <linux/leds.h>
#include <linux/module.h>

#include "lux.h"

struct lux_led_data {
	struct gpio_desc *gpiod;
	struct led_classdev clsdev;
};

struct lux_led_data led;

static void lux_led_set_brightness(struct led_classdev *led_clsdev,
								   enum led_brightness brightness) {
	struct lux_led_data *led_data = container_of(led_clsdev, struct lux_led_data, clsdev);
	gpiod_set_value(led_data->gpiod, brightness != LED_OFF);
}

static __init int lux_led_init(void) {
	int ret = 0;

	// 1. Ask the kernel for pin named led0
	led.gpiod = gpiod_get(NULL, "led0", GPIOD_OUT_LOW);
	if (IS_ERR(led.gpiod)) {
		pr_err(LUX_LED_DEVICE_NAME ": Failed to get descriptor for led0");
		return PTR_ERR(led.gpiod);
	}

	// 2. Configure the led class device
	led.clsdev.name = LUX_LED_DEVICE_NAME ":yellow:inidicator"; // devicename:color:function
	led.clsdev.brightness_set = lux_led_set_brightness;

	// 3. Register with the LED subsystem
	ret = led_classdev_register(NULL, &led.clsdev);
	if (ret < 0) {
		gpiod_put(led.gpiod);
		return ret;
	}

	return 0;
}

static __exit void lux_led_exit(void) {
	led_classdev_unregister(&led.clsdev);
	gpiod_put(led.gpiod);
}

module_init(lux_led_init);
module_exit(lux_led_exit);
MODULE_AUTHOR("m0st4fa");
MODULE_DESCRIPTION("LED subsystem driver for lux device");
MODULE_LICENSE("GPL");
