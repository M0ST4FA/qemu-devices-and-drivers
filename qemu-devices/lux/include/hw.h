#pragma once

#include <linux/types.h>

#define LED_NR 64

#define VENDOR_ID (0x1234) // Private ID
#define F0_DEVICE_ID (0x000001)
#define F1_DEVICE_ID (0x000002)
#define CLASS_BASE_ID (0x08)
#define CLASS_SUB_ID (0x80)
#define CLASS_PI_ID (0x00)

#define F0_BAR0_REGION_SIZE 4096
#define F0_BAR1_REGION_SIZE 4096
#define F1_BAR0_REGION_SIZE 4096

#define MAX_DMA_REGIONS 1024

#define MAGIC (0x4750494F)
#define VERSION (0x1)

enum f0_bar0_regs {
	REG_MAGIC = 0x00,		  // 4 bytes
	REG_VERSION = 0x04,		  // 4 bytes
	REG_DIRECTION = 0x08,	  // 8 bytes
	REG_DATA = 0x10,		  // 8 bytes
	REG_SET = 0x18,			  // 8 bytes
	REG_CLR = 0x20,			  // 8 bytes
	REG_LED_CTRL = 0x28,	  // 4 bytes
	REG_LED_IRQ_CAUSE = 0x2c, // 4 bytes
};

enum led_direction {
	LUX_DIRECTION_OUT = 1,
	LUX_DIRECTION_IN = 0,
};

enum led_ctrl {
	LED_CTRL_IRQ_EN = (1 << 0),
};

enum led_irq_cause {
	LUX_IRQ_LED_ON = (1 << 0),
	LUX_IRQ_LED_OFF = (1 << 1),
	LUX_IRQ_LED_COLOR = (1 << 2),
	LUX_IRQ_LED_ERR_UNKNOWN_CMD = (1 << 3),
};

[[maybe_unused]]
static const char *f0_reg_names[] = {
	[REG_MAGIC] = "REG_MAGIC",
	[REG_VERSION] = "REG_VERSION",
	[REG_DIRECTION] = "REG_DIRECTION",
	[REG_DATA] = "REG_DATA",
	[REG_SET] = "REG_SET",
	[REG_CLR] = "REG_CLR",
	[REG_LED_CTRL] = "REG_LED_CTRL",
	[REG_LED_IRQ_CAUSE] = "REG_LED_IRQ_CAUSE",
};

[[maybe_unused]]
static const char *led_direction_names[] = {
	[LUX_DIRECTION_OUT] = "DIRECTION_OUT",
	[LUX_DIRECTION_IN] = "DIRECTION_IN",
};

static const char *led_ctrl_names[] = {
	[LED_CTRL_IRQ_EN] = "IRQ_EN",
};

[[maybe_unused]]
static const char *led_irq_cause_names[] = {
	[LUX_IRQ_LED_ON] = "LED_ON",
	[LUX_IRQ_LED_OFF] = "LED_OFF",
	[LUX_IRQ_LED_COLOR] = "LED_COLOR",
	[LUX_IRQ_LED_ERR_UNKNOWN_CMD] = "LED_ERR_UNKNOWN_CMD",
};

struct [[gnu::packed]] smart_led {
	// LED has two registers

	__u32 state;   // Bit 0: ON/OFF, the rest are reserverd
	__u8 color[4]; // 32-bit RGBA
};

enum f0_bar1_regs {
	REG_STATE = 0x00, // 4 bytes
	REG_COLOR = 0x04, // 4 bytes
};

#define F0_BAR1_REG_SIZE 4

enum f1_bar0_regs {
	REG_IRQ_STATUS = 0x00, // 4 bytes, RO, Which lines are currently asserting an interrupt
	REG_IRQ_MASK = 0x04,   // 4 bytes, RW, Which lines are allowed to propogate to the CPU
	REG_IRQ_ACK = 0x08,	   // 4 bytes, WO, Writing 1 clears a pending interrupt in the status register. Think of it as REG_IRQ_CLR.

	REG_TIMER_CTRL = 0x0C, // 4 bytes, RW, Bit 0 = Enable, Bit 1 = Mode, Bit 2 = IRQ Enable
	REG_TIMER_TIME = 0x10, // 8 bytes, RO, 64-bit timestamp since timer started
	REG_TIMER_CMP = 0x18,  // 8 bytes, RW, The value at which timer fires an IRQ
};

[[maybe_unused]]
static const char *f1_reg_names[] = {
	[REG_IRQ_STATUS] = "REG_IRQ_STATUS",
	[REG_IRQ_MASK] = "REG_IRQ_MASK",
	[REG_IRQ_ACK] = "REG_IRQ_ACK",

	[REG_TIMER_CTRL] = "REG_TIMER_CTRL",
	[REG_TIMER_TIME] = "REG_TIMER_TIME",
	[REG_TIMER_CMP] = "REG_TIMER_CMP",
};

// Assignment of IRQs to physical pins. This is always hardcoded even in real hardware.
// In real hardware, the assignment of devices to physical pins is announced via DT or ACPI
// or their equivalent.
// Here, we announce assignments through this enum
enum hwirqs {
	HWIRQ_TIMER = 0,
	HWIRQ_LED = 1,
	HWIRQ_LED_ERR = 2,
	HWIRQ_COUNT, // Number of assigned pins (NOTE: pins must be contiguous; driver relies on that for mapping to virqs.)
};

[[maybe_unused]]
static const char *hwirq_names[] = {
	[HWIRQ_TIMER] = "TIMER_IRQ",
	[HWIRQ_LED] = "LED_IRQ",
	[HWIRQ_LED_ERR] = "LED_ERR_IRQ",
};

#define TIMER_BIT (1ULL << 0)
#define IRQ_BIT (1ULL << 1)
#define RELOAD_BIT (1ULL << 2)
#define LUX_TIMER_RATE (1000ULL * 1000ULL * 1000ULL)
