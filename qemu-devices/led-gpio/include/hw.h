#pragma once

#define LED_NR 64

#define VENDOR_ID (0x1234) // Private ID
#define DEVICE_ID (0x0001)
#define CLASS_BASE_ID (0x08)
#define CLASS_SUB_ID (0x80)
#define CLASS_PI_ID (0x00)

#define BAR0_REGION_SIZE 4096
#define BAR1_REGION_SIZE 4096
#define LUX_DIRECTION_OUT 1
#define LUX_DIRECTION_IN 0

#define MAX_DMA_REGIONS 1024

#define MAGIC (0x4750494F)
#define VERSION (0x1)

enum bar0_regs {
	REG_MAGIC = 0x00,	  // 4 bytes
	REG_VERSION = 0x04,	  // 4 bytes
	REG_DIRECTION = 0x08, // 8 bytes
	REG_DATA = 0x10,	  // 8 bytes
	REG_SET = 0x18,		  // 8 bytes
	REG_CLR = 0x20,		  // 8 bytes
};

static const char *reg_names[] = {
	[REG_MAGIC] = "REG_MAGIC",
	[REG_VERSION] = "REG_VERSION",
	[REG_DIRECTION] = "REG_DIRECTION",
	[REG_DATA] = "REG_DATA",
	[REG_SET] = "REG_SET",
	[REG_CLR] = "REG_CLR",
};
