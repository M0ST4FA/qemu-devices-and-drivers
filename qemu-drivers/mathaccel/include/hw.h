#pragma once

// Definitions that are shared between hardware and driver
#include "asm-generic/int-ll64.h"
#include "uapi.h"

enum register_offsets : __u32 {
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
// static_assert(REG_OFFSET_MAX == 64, "You forgot to change the maximum register offset");

// NOTE: A device is basically a state machine, and the driver has to respect that.
// Here are the states
enum device_state : __u32 {
	STATE_INVALID = 0,
	STATE_RESET,
	STATE_READY,
	STATE_BUSY,
	STATE_ERROR,
	STATE_COUNT,
};

enum device_flags : __u32 {
	FLAG_INT_ENABLED = (1 << 0),
	FLAG_DMA_ENABLED = (1 << 1),

	FLAG_MASK = 0b11,
};
// static_assert(FLAG_MASK == 0b11, "You forgot to change the flag mask");

enum irq_cause : __u32 {
	IRQ_CAUSE_NOIRQ = 0,

	IRQ_CAUSE_JOB_DONE = (1 << 0),
	IRQ_CAUSE_CMD_DONE = (1 << 1),
	IRQ_CAUSE_ERROR = (1 << 2),
};

enum error_cause : __u32 {
	ERR_CAUSE_NOERR = 0,

	ERR_CAUSE_DMA_READ = (1 << 0),		// failed to read from SQ
	ERR_CAUSE_DMA_WRITE = (1 << 1),		// failed to write to CQ
	ERR_CAUSE_DMA_BAD_QUEUE = (1 << 2), // SQ/CQ not configured
	ERR_CAUSE_DMA_DISABLED = (1 << 3),	// DMA is disabled
	ERR_CAUSE_CMD_UNKOWN = (1 << 4),	// unkown command (only during legacy command execution)
	ERR_CAUSE_CMD_EXEC = (1 << 5),		// error during cmd execution

	ERR_CAUSE_INTERNAL = (1 << 30), // unexpected internal error
};

struct math_sq_entry {
	enum math_op opcode;
	__u32 cmd_id;
	__s32 args[2];
};

struct math_cq_entry {
	__u32 cmd_id;
	enum completion_status status; // 0 = success, 0 > = error
	__s64 result;
	__u32 valid; // 1 = Hardware wrote this, 0 = Empty slot
	__u8 pad[4]; // To make it 24-byte aligned
};
