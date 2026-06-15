#pragma once

#include "linux/clocksource.h"
#include "linux/hrtimer.h"
#include "linux/miscdevice.h"
#include "linux/spinlock_types.h"
#include <linux/cdev.h>
#include <linux/clockchips.h>
#include <linux/pci.h>
#include <linux/types.h>

#define LUX_BUS_NAME "lux-core"
#define LUX_CHIP_LABEL "lux-gpio-chip"
#define LUX_CHAR_DRIVER_NAME "lux-chardev"
#define LUX_CLASS_NAME "lux"
#define LUX_LED_DEVICE_NAME "lux-led"
#define LUX_PLATFORM_DEVICE_NAME "lux-leds"
#define LUX_IRQ_DRIVER_NAME "lux-irq"
#define LUX_TIMER_DRIVER_NAME "lux-timer"

#define LED_NAME_DEVICENAME ""
#define LED_NAME_COLOR "rgb:"
#define LED_NAME_FUNCTION "indicator"
#define LED_NAME LED_NAME_DEVICENAME LED_NAME_COLOR LED_NAME_FUNCTION
#define LED_MC_NAME "rgb:smart"

#define LUX_F0_DEV_ID 0x4444
#define LUX_F1_DEV_ID 0x5555

#define LUX_TIMER_CS_RATING 300 // good enought to be selected some times

struct lux_driver;
struct lux_function;

struct lux_function {
	int dev_id; // Used for matchmaking
	void *prv_data;
	struct irq_domain *irq_domain;

	struct pci_dev *pdev;

	// Virtual addresses of the BARs
	void __iomem *bar[6];

	struct list_head node; // Allows the bus to keep a list of devices
};

struct lux_clock {
	struct clocksource cs;
	struct clock_event_device ce;
	struct miscdevice misc;
	void __iomem *base;
	int ce_cpu;
	unsigned long is_open;

	raw_spinlock_t irq_data_lock;
	__u64 irq_data;
	wait_queue_head_t wait_queue;
};
