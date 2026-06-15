#pragma once

#include "asm-generic/ioctl.h"
#include <linux/ioctl.h>
#include <linux/types.h>

#define LUX_IOCTL_MAGIC ('L')

struct [[gnu::packed]] lux_hw_info {
	int num_leds;
	int rows;
	int cols;
};

// LED IOCTLs
#define LUX_IOCTL_CLEAR_SCREEN _IO(LUX_IOCTL_MAGIC, 0)
#define LUX_IOCTL_SET_DIRECTION_OUT _IOW(LUX_IOCTL_MAGIC, 1, int)
#define LUX_IOCTL_SET_DIRECTION_IN _IOW(LUX_IOCTL_MAGIC, 2, int)
#define LUX_IOCTL_GET_INFO _IOR(LUX_IOCTL_MAGIC, 3, struct lux_hw_info)

// TIMER IOCTLs
// 1. Time operations
#define LUX_TIME_RD _IOR(LUX_IOCTL_MAGIC, 4, __u64)
#define LUX_TIME_SET _IOW(LUX_IOCTL_MAGIC, 5, __u64)

#define LUX_CLOCK_OVERRUN(data) ((__u64)data >> 8)
#define LUX_CLOCK_IRQ_CAUSE(data) ((__u64)data & 0xFF)
// IRQ causes
#define LUX_CLOCK_PERIODIC (1 << 0)
#define LUX_CLOCK_ALARM (1 << 1)

// 2. Alarm operations
#define LUX_ALM_SET _IOW(LUX_IOCTL_MAGIC, 6, __u64) // Set reg TIME_CMP
#define LUX_ALM_RD _IOR(LUX_IOCTL_MAGIC, 7, __u64)	// Read reg TIME_CMP
#define LUX_AIE_ON _IO(LUX_IOCTL_MAGIC, 8)			// Set IRQ_BIT + TIMER_BIT
#define LUX_AIE_OFF _IO(LUX_IOCTL_MAGIC, 9)			// Clear IRQ_BIT + TIMER_BIT

#define LUX_TIMER_UNPRIV_MIN_DELTA (15625000) // Minimum timer delta for unprivileged user: 15,625,000 nanoseconds

// 3. Periodic operations
#define LUX_IRQP_SET _IOW(LUX_IOCTL_MAGIC, 10, __u64) // Write delta to REG_CMP and enable RELOAD_BIT
#define LUX_PIE_ON _IO(LUX_IOCTL_MAGIC, 12)			  // Set TIMER_BIT | IRQ_BIT | RELOAD_BIT
#define LUX_PIE_OFF _IO(LUX_IOCTL_MAGIC, 13)		  // Clear TIMER_BIT | IRQ_BIT | RELOAD_BIT

// 4. Enable asynchronous mode (read will return immediately)
#define LUX_ALM_ASYNC_ON _IO(LUX_IOCTL_MAGIC, 14)
#define LUX_ALM_ASYNC_OFF _IO(LUX_IOCTL_MAGIC, 15)

[[maybe_unused]]
static const char *ioctl_names[] = {
	[_IOC_NR(LUX_IOCTL_CLEAR_SCREEN)] = "CLEAR_SCREEN",
	[_IOC_NR(LUX_IOCTL_SET_DIRECTION_OUT)] = "SET_DIRECTION_OUT",
	[_IOC_NR(LUX_IOCTL_SET_DIRECTION_IN)] = "SET_DIRECTION_IN",
	[_IOC_NR(LUX_IOCTL_GET_INFO)] = "GET_INFO",

	[_IOC_NR(LUX_TIME_RD)] = "TIME_RD",
	[_IOC_NR(LUX_TIME_SET)] = "TIME_SET",

	[_IOC_NR(LUX_ALM_SET)] = "ALM_SET",
	[_IOC_NR(LUX_ALM_RD)] = "ALM_RD",
	[_IOC_NR(LUX_AIE_ON)] = "AIE_ON",
	[_IOC_NR(LUX_AIE_OFF)] = "AIE_OFF",

	[_IOC_NR(LUX_IRQP_SET)] = "IRQP_SET",
	[_IOC_NR(LUX_PIE_ON)] = "PIE_ON",
	[_IOC_NR(LUX_PIE_OFF)] = "PIE_OFF",

	[_IOC_NR(LUX_ALM_ASYNC_ON)] = "ALM_ASYNC_ON",
	[_IOC_NR(LUX_ALM_ASYNC_OFF)] = "ALM_ASYNC_OFF",
};
