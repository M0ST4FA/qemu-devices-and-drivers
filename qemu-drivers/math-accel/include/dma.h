#pragma once

#include "device.h"

struct [[gnu::packed]] math_sq_entry {
	enum math_op opcode;
	uint32_t cmd_id;
	uint32_t args[2];
};

struct [[gnu::packed]] math_cq_entry {
	uint32_t cmd_id;
	enum completion_status status; // 0 = success, 0 > = error
	uint64_t result;
	uint32_t valid;	  // 1 = Hardware wrote this, 0 = Empty slot
	uint32_t padding; // To make it 24-byte aligned
};

int mathaccel_init_dma(struct mathaccel_device *math_dev);
int mathaccel_release_dma(struct mathaccel_device *math_dev);
