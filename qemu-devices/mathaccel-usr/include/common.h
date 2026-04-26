#pragma once

#include "libvfio-user.h"
#include <assert.h>
#include <stdint.h>

enum register_offsets : uint32_t {
	REG_ARG1 = 0,
	REG_ARG2 = 4,
	REG_CMD = 8,
	REG_STATUS = 12,
	REG_FLAGS = 16,

	REG_DMA_EN = 20,

	REG_DMA_SQ_BASE_LOWER = 24,
	REG_DMA_SQ_BASE_UPPER = 28,
	REG_DMA_SQ_HEAD = 32,
	REG_DMA_SQ_TAIL = 36, // Updating the tail of the ring buffer rings the doorbell

	REG_DMA_CQ_BASE_LOWER = 40,
	REG_DMA_CQ_BASE_UPPER = 44,
	REG_DMA_CQ_HEAD = 48,
	REG_DMA_CQ_TAIL = 52,

	REG_DMA_RING_SIZE = 56,

	REG_IRQ_CAUSE = 60,
	REG_ERROR_CAUSE = 64,

	REG_OFFSET_MAX = 64,
};
static_assert(REG_OFFSET_MAX == 64, "You forgot to change the maximum register offset");

// NOTE: A device is basically a state machine, and the driver has to respect that.
// Here are the states
enum device_state : uint32_t {
	STATE_RESET = 0,
	STATE_READY,
	STATE_BUSY,
	STATE_ERROR,
	STATE_COUNT,
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
	MATH_OP_COUNT,
};

enum completion_status : uint32_t {
	COMPLETION_SUCCESS = (1 << 0),
	COMPLETION_ERROR_UNKOWN_CMD = (1 << 1),
	COMPLETION_ERROR_DIV_BY_ZERO = (1 << 2),
	COMPLETION_ERROR_INTERNAL = (1 << 30),
};

enum irq_cause : uint32_t {
	IRQ_CAUSE_NOIRQ = 0,

	IRQ_CAUSE_JOB_DONE = (1 << 0),
	IRQ_CAUSE_CMD_DONE = (1 << 1),
	IRQ_CAUSE_ERROR = (1 << 2),
};

enum error_cause : uint32_t {
	ERR_CAUSE_NOERR = 0,

	ERR_CAUSE_DMA_DISABLED = (1 << 0),	// DMA operation when it is disabled
	ERR_CAUSE_DMA_READ = (1 << 1),		// failed to read from SQ
	ERR_CAUSE_DMA_WRITE = (1 << 2),		// failed to write to CQ
	ERR_CAUSE_DMA_BAD_QUEUE = (1 << 3), // SQ/CQ not configured
	ERR_CAUSE_CMD_UNKOWN = (1 << 4),	// unkown command (only during legacy command execution)
	ERR_CAUSE_CMD_EXEC = (1 << 5),		// error during cmd execution

	ERR_CAUSE_INTERNAL = (1 << 30), // unexpected internal error
};

#pragma pack(push, 1)
struct math_sq_entry {
	enum math_op opcode;
	uint32_t cmd_id;
	uint32_t args[2];
};

struct math_cq_entry {
	uint32_t cmd_id;
	enum completion_status status; // 0 = success, 1 = error
	uint64_t result;
	uint32_t valid;	  // 1 = Hardware wrote this, 0 = Empty slot
	uint32_t padding; // To make it 24-byte aligned
};
#pragma pack(pop)

struct math_device {
	uint32_t args[2];
	enum math_op cmd;
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
