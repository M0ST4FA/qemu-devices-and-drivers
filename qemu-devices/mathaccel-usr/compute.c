#include "compute.h"
#include "common.h"
#include "dma.h"
#include "include/common.h"
#include "libvfio-user.h"
#include <errno.h>

int consume_submission_queue(struct vfu_ctx *ctx) {
	// struct math_device_state *state = vfu_get_private(ctx);
	int ret = 0;
	struct math_sq_entry current_sq_entry = {0};
	struct math_cq_entry current_cq_entry = {0};

	do {
		ret = dma_read_next(ctx, &current_sq_entry);
		if (ret < 0)
			break;

		execute_operation(&current_sq_entry, &current_cq_entry);

		ret = dma_write_next(ctx, &current_cq_entry);
		if (ret < 0)
			break;

	} while (1);

	if (errno == EAGAIN)
		return 0;
	else
		return -1;
};

int execute_operation(struct math_sq_entry *cmd, struct math_cq_entry *res) {
	switch (cmd->opcode) {
		case MATH_OP_ADD:
			res->result = cmd->arg1 + cmd->arg2;
			break;

		case MATH_OP_SUB:
			res->result = cmd->arg1 - cmd->arg2;
			break;

		case MATH_OP_MUL:
			res->result = cmd->arg1 * cmd->arg2;
			break;

		case MATH_OP_DIV:
			res->result = cmd->arg1 / cmd->arg2;
			break;

		default:
			res->result = -1;
			break;
	}

	if (res->result != (uint64_t)-1)
		res->status = COMPLETION_SUCCESS;
	else
		res->status = COMPLETION_ERROR;

	res->valid = 1;

	return 0;
}
