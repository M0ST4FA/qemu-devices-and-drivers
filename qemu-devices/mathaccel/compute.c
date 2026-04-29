#include "libvfio-user.h"
#include <sys/errno.h>

#include "compute.h"
#include "device.h"
#include "dma.h"
#include "fsm.h"

int consume_submission_queue(struct vfu_ctx *ctx) {
	// struct math_device_state *state = vfu_get_private(ctx);
	int ret = 0;
	struct math_sq_entry current_sq_entry = {0};
	struct math_cq_entry current_cq_entry = {0};
	struct math_device *dev = vfu_get_private(ctx);

	do {
		ret = dma_read_next(ctx, &current_sq_entry);
		if (ret < 0 && ret != -EAGAIN) {
			dev->error_cause |= ERR_CAUSE_DMA_READ;
			return fsm_dispatch(ctx, EVT_ERROR);
		}

		if (ret == -EAGAIN) // We processed all commands
			break;

		// Errors here are user-level, not device-level
		// Should not fail, and are already handled well by the function (write an error record)
		execute_operation(&current_sq_entry, &current_cq_entry);

		ret = dma_write_next(ctx, &current_cq_entry);
		if (ret < 0 && ret != -EAGAIN) {
			dev->error_cause |= ERR_CAUSE_DMA_WRITE;
			return fsm_dispatch(ctx, EVT_ERROR);
		}

	} while (1);

	// NOTE: Should never reach here, but just in case
	if (ret < 0 && ret != -EAGAIN) {
		dev->error_cause |= ERR_CAUSE_INTERNAL;
		return fsm_dispatch(ctx, EVT_ERROR);
	}

	// INVARIANT: ret == 0 || ret == -EAGAIN
	dev->irq_cause |= IRQ_CAUSE_JOB_DONE;
	return fsm_dispatch(ctx, EVT_JOB_DONE);
};

int execute_operation(struct math_sq_entry *cmd, struct math_cq_entry *res) {
	res->status = COMPLETION_SUCCESS;
	res->cmd_id = cmd->cmd_id;
	res->valid = 1;

	switch (cmd->opcode) {
		case MATH_OP_ADD:
			res->result = cmd->args[0] + cmd->args[1];
			break;

		case MATH_OP_SUB:
			res->result = cmd->args[0] - cmd->args[1];
			break;

		case MATH_OP_MUL:
			res->result = cmd->args[0] * cmd->args[1];
			break;

		case MATH_OP_DIV:
			if (cmd->args[1] == 0) {
				res->status = COMPLETION_ERROR_DIV_BY_ZERO;
				res->result = -1;
			} else
				res->result = cmd->args[0] / cmd->args[1];
			break;

		default:
			res->status = COMPLETION_ERROR_UNKOWN_CMD;
			res->result = -1;
			break;
	}

	if (res->status == COMPLETION_SUCCESS)
		return 0;
	else
		return -EINVAL;
}
