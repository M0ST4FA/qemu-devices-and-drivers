#pragma once

#include <stdio.h>

#include "libvfio-user.h"

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

ssize_t bar0_access(vfu_ctx_t *vfu_ctx, char *const buf, size_t count, loff_t offset, const bool is_write);
ssize_t bar1_access(vfu_ctx_t *vfu_ctx, char *const buf, size_t count, loff_t offset, const bool is_write);
