#pragma once

#include "asm-generic/ioctl.h"
#include <linux/ioctl.h>

#define LUX_IOCTL_MAGIC ('L')

struct [[gnu::packed]] lux_hw_info {
	int num_leds;
	int rows;
	int cols;
};

#define LUX_IOCTL_CLEAR_SCREEN _IO(LUX_IOCTL_MAGIC, 0)
#define LUX_IOCTL_SET_DIRECTION_OUT _IOW(LUX_IOCTL_MAGIC, 1, int)
#define LUX_IOCTL_SET_DIRECTION_IN _IOW(LUX_IOCTL_MAGIC, 2, int)
#define LUX_IOCTL_GET_INFO _IOR(LUX_IOCTL_MAGIC, 3, struct lux_hw_info)

[[maybe_unused]]
static const char *ioctl_names[] = {
	[_IOC_NR(LUX_IOCTL_CLEAR_SCREEN)] = "CLEAR_SCREEN",
	[_IOC_NR(LUX_IOCTL_SET_DIRECTION_OUT)] = "SET_DIRECTION_OUT",
	[_IOC_NR(LUX_IOCTL_SET_DIRECTION_IN)] = "SET_DIRECTION_IN",
	[_IOC_NR(LUX_IOCTL_GET_INFO)] = "GET_INFO",
};
