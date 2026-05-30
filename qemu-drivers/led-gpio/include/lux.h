#pragma once
#include <linux/cdev.h>
#include <linux/pci.h>
#include <linux/types.h>

#define LUX_BUS_NAME "lux-bus"
#define LUX_CHIP_LABEL "lux-gpio-chip"
#define LUX_CHAR_DRIVER_NAME "lux-chardev"
#define LUX_CLASS_NAME "lux"
#define LUX_LED_DEVICE_NAME "lux-led"
#define LUX_PLATFORM_DEVICE_NAME "lux-leds"

#define LED_NAME_DEVICENAME ""
#define LED_NAME_COLOR "rgb:"
#define LED_NAME_FUNCTION "indicator"
#define LED_NAME LED_NAME_DEVICENAME LED_NAME_COLOR LED_NAME_FUNCTION
#define LED_MC_NAME "rgb:smart"

#define LUX_F0_DEV_ID 0x4444
#define LUX_F1_DEV_ID 0x5555

struct lux_driver;
struct lux_device;

struct lux_device {
	int dev_id; // Used for matchmaking
	struct lux_driver *driver;
	void *prv_data;

	struct pci_dev *pdev;
	void __iomem *bar[6];

	struct list_head node; // Allows the bus to keep a list of devices
};

struct lux_driver {
	const char *name;
	int supported_dev_id; // A driver can support only one, so no need for ID table
	typeof(int(struct lux_device *)) *probe;
	typeof(void(struct lux_device *)) *remove;
	struct list_head node; // Allows the bus to keep a list of drivers
};

int lux_register_driver(struct lux_driver *);
void lux_unregister_driver(struct lux_driver *);
