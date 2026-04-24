#pragma once

#include "libvfio-user.h"
#include <assert.h>
#include <stdint.h>

enum register_offsets : uint32_t {
	REG_VAL = 0x0,
	REG_CMD = 0x4,
	REG_STATUS = 0x8,
	REG_FLAGS = 0x12,

	REG_DMA_EN = 0x16,

	REG_DMA_SQ_BASE_LOWER = 0x20,
	REG_DMA_SQ_BASE_UPPER = 0x24,
	REG_DMA_SQ_HEAD = 0x28,
	REG_DMA_SQ_TAIL = 0x32, // Updating the tail of the ring buffer rings the doorbell

	REG_DMA_CQ_BASE_LOWER = 0x36,
	REG_DMA_CQ_BASE_UPPER = 0x40,
	REG_DMA_CQ_HEAD = 0x44,
	REG_DMA_CQ_TAIL = 0x48,

	REG_DMA_RING_SIZE = 0x52,

	REG_OFFSET_MAX = 0x52,
};
static_assert(REG_OFFSET_MAX == 0x52, "You forgot to change the maximum register offset");

// NOTE: A device is basically a state machine, and the driver has to respect that.
// Here are the states
enum device_status : uint32_t {
	STATUS_READY = 0,
	STATUS_BUSY,
	STATUS_COMPLETED_CMD,		// For normal command
	STATUS_COMPLETED_DMA_BATCH, // For DMA batch commands
};

enum device_flags : uint32_t {
	FLAG_INT_ENABLED = 0x0,
	FLAG_DMA_ENABLED = 0X1,

	FLAG_MASK = 0b11,
};
static_assert(FLAG_MASK == 0b11, "You forgot to change the flag mask");

enum math_op : uint32_t {
	MATH_OP_ADD = 0,
	MATH_OP_SUB,
	MATH_OP_MUL,
	MATH_OP_DIV,
};

enum completion_status : uint32_t {
	COMPLETION_SUCCESS,
	COMPLETION_ERROR,
};

#pragma pack(push, 1)
struct math_sq_entry {
	enum math_op opcode;
	uint32_t cmd_id;
	uint32_t arg1;
	uint32_t arg2;
};

struct math_cq_entry {
	uint32_t cmd_id;
	enum completion_status status; // 0 = success, 1 = error
	uint64_t result;
	uint32_t valid;	  // 1 = Hardware wrote this, 0 = Empty slot
	uint32_t padding; // To make it 24-byte aligned
};
#pragma pack(pop)

struct math_device_state {
	uint32_t data;
	enum math_op cmd;
	enum device_status status;
	enum device_flags flags;

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

#define MATH_DEVICE_STATE_DEFAULT_INIT \
	{                                  \
		.data = 0,                     \
		.cmd = 0,                      \
		.status = STATUS_READY,        \
		.flags = 0,                    \
		.sq_base_addr = 0,             \
		.sq_head = 0,                  \
		.sq_tail = 0,                  \
		.cq_base_addr = 0,             \
		.cq_head = 0,                  \
		.cq_tail = 0,                  \
		.ring_size = 0,                \
	}
