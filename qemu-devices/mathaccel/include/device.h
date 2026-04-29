#pragma once

#include "libvfio-user.h"
#include <assert.h>
#include <stdint.h>

#include "mathaccel/include/hw.h"

struct math_device {
	enum math_op cmd;
	int32_t args[2];
	enum device_state state;
	enum device_flags flags;
	enum irq_cause irq_cause;
	enum error_cause error_cause;

	// Physical base address of SQ in kernel memory
	vfu_dma_addr_t sq_base_addr;
	// Head is managed by hardware (consumer)
	uint16_t sq_head;
	// Tail is managed by kernel (producer)
	uint16_t sq_tail;

	// Physical base address of CQ in kernel memory
	vfu_dma_addr_t cq_base_addr;
	uint16_t cq_head; // Actual register can have a different size from interface. Neat
	uint16_t cq_tail;

	// Ring size; constrained by hardware
	uint16_t ring_size;
};

#define MATH_DEVICE_DEFAULT_INIT \
	{                            \
		.args = {0, 0},          \
		.cmd = 0,                \
		.state = STATE_RESET,    \
		.flags = 0,              \
		.sq_base_addr = 0,       \
		.sq_head = 0,            \
		.sq_tail = 0,            \
		.cq_base_addr = 0,       \
		.cq_head = 0,            \
		.cq_tail = 0,            \
		.ring_size = 0,          \
	}
