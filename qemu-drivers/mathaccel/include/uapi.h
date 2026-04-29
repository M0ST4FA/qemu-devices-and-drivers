#pragma once

#include "asm-generic/int-ll64.h"

#define MATHACCEL_IOC_MAGIC 0xff

enum math_op : __u32 {
	MATH_OP_ADD = 0,
	MATH_OP_SUB,
	MATH_OP_MUL,
	MATH_OP_DIV,
	MATH_OP_COUNT,
};

enum completion_status : __u32 {
	COMPLETION_SUCCESS = (1 << 0),
	COMPLETION_ERROR_UNKOWN_CMD = (1 << 1),
	COMPLETION_ERROR_DIV_BY_ZERO = (1 << 2),
	COMPLETION_ERROR_INTERNAL = (1 << 30),
};

struct [[gnu::packed]] mathaccel_req {
	enum math_op opcode;
	__u32 args[2];
	__u64 result; // kernel writes the result back here
	enum completion_status status;
	__u32 cmd_id; // kernel fills this in; userspace can use it for tracking

	__u8 _pad[4]; // align the struct to 8-byte boundary (32-byte in size; important for arrays)
};

#define MATHACCEL_IOC_COMPUTE _IOWR(MATHACCEL_IOC_MAGIC, 0, struct mathaccel_req)
