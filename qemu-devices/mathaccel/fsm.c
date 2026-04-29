#include "libvfio-user.h"
#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "compute.h"
#include "device.h"
#include "fsm.h"
#include "mathaccel/include/hw.h"

struct transition {
	enum device_state next; // STATE_COUNT means invalid transition (reject)
	int (*action)(vfu_ctx_t *);
};

static int action_init(vfu_ctx_t *ctx) {
	struct math_device *dev = vfu_get_private(ctx);
	dev->sq_head = 0;
	dev->sq_tail = 0;

	dev->cq_head = 0;
	dev->cq_tail = 0;

	printf("[HW:FSM] Device initialized, entering READY\n");
	return 0;
};

static int action_reset(vfu_ctx_t *ctx) {
	struct math_device *dev = vfu_get_private(ctx);

	dev->flags = 0;
	dev->args[0] = dev->args[1] = 0;
	dev->cmd = MATH_OP_COUNT;

	dev->sq_base_addr = NULL;
	dev->cq_base_addr = NULL;

	dev->sq_head = 0;
	dev->sq_tail = 0;

	dev->cq_head = 0;
	dev->cq_tail = 0;

	printf("[HW:FSM] Device reset\n");
	return 0;
};

static int action_start_dma(vfu_ctx_t *ctx) {
	struct math_device *dev = vfu_get_private(ctx);

	if (!(dev->flags & FLAG_DMA_ENABLED)) {
		fprintf(stderr, "[HW:FSM] Attempting DMA operation when DMA is not enabled\n");
		dev->error_cause |= ERR_CAUSE_DMA_DISABLED;
		return fsm_dispatch(ctx, EVT_ERROR);
	};

	if (dev->sq_base_addr == NULL) {
		fprintf(stderr, "[HW:FSM] SQ base not set, can't start DMA\n");
		dev->error_cause |= ERR_CAUSE_DMA_BAD_QUEUE;
		return fsm_dispatch(ctx, EVT_ERROR);
	}
	if (dev->cq_base_addr == NULL) {
		fprintf(stderr, "[HW:FSM] CQ base not set, can't start DMA\n");
		dev->error_cause |= ERR_CAUSE_DMA_BAD_QUEUE;
		return fsm_dispatch(ctx, EVT_ERROR);
	}

	// This will dispatch EVT_ERROR and EVT_JOB_DONE itself
	return consume_submission_queue(ctx);
};

static int action_do_legacy_job(vfu_ctx_t *ctx) {
	struct math_device *dev = vfu_get_private(ctx);

	if (dev->cmd == MATH_OP_COUNT) { // No command set
		dev->error_cause |= ERR_CAUSE_CMD_UNKOWN;
		return fsm_dispatch(ctx, EVT_ERROR);
	}

	struct math_sq_entry current_sq_entry = {
		.opcode = dev->cmd,
		.args = {dev->args[0], dev->args[1]},
		.cmd_id = -1,
	};
	struct math_cq_entry current_cq_entry = {0};
	int ret = 0;

	ret = execute_operation(&current_sq_entry, &current_cq_entry);
	if (ret < 0) {
		dev->error_cause |= ERR_CAUSE_CMD_EXEC;
		return fsm_dispatch(ctx, EVT_ERROR);
	}

	dev->args[0] = current_cq_entry.result;
	dev->cmd = MATH_OP_COUNT;
	dev->irq_cause |= IRQ_CAUSE_CMD_DONE;
	return fsm_dispatch(ctx, EVT_CMD_DONE);
}

// NOTE: irq_cause must be set before calling this function
static int action_complete(vfu_ctx_t *ctx) {
	struct math_device *dev = vfu_get_private(ctx);

	printf("[HW:FSM] Work complete, signaling driver\n");

	if (dev->flags & FLAG_INT_ENABLED) { // Make sure interrupts are enabled before firing
		int ret = vfu_irq_trigger(ctx, 0);
		if (ret < 0) {
			fprintf(stderr, "[HW:FSM] CRITICAL: Failed to fire interrupt: %s\n", strerror(errno));
		} else {
			printf("[HW:FSM] Successfully fired interrupt!\n");
		}
	} else
		fprintf(stderr, "[HW:FSM]: Interrupts disabled. Not sending interrupt\n");

	return 0;
};

// NOTE: Error cause must be set before calling this function
static int action_error(vfu_ctx_t *ctx) {
	struct math_device *dev = vfu_get_private(ctx);

	fprintf(stderr, "[HW:FSM]: Entered ERROR state\n");
	(void)dev;

	dev->irq_cause |= IRQ_CAUSE_ERROR;

	// Trigger IRQ so that driver examines error (only if interrupts are enabled)
	if (dev->flags & FLAG_INT_ENABLED) {
		int ret = vfu_irq_trigger(ctx, 0);
		if (ret < 0) {
			fprintf(stderr, "[HW:FSM] CRITICAL: Failed to fire interrupt: %s\n", strerror(errno));
		} else {
			printf("[HW:FSM] Successfully fired interrupt!\n");
		}
	} else
		fprintf(stderr, "[HW:FSM]: Interrupts disabled. Not sending interrupt\n");

	return 0;
};

static const struct transition fsm[STATE_COUNT][EVT_COUNT] = {

	[STATE_RESET] = {
		[EVT_INIT] = {STATE_READY, action_init},
		// all others → { STATE_INVALID, NULL } = illegal
	},

	[STATE_READY] = {
		[EVT_SUBMIT_JOB] = {STATE_BUSY, action_start_dma},
		[EVT_SUBMIT_CMD] = {STATE_BUSY, action_do_legacy_job},
		[EVT_RESET] = {STATE_RESET, action_reset},
		[EVT_ERROR] = {STATE_ERROR, action_error},
	},

	[STATE_BUSY] = {
		[EVT_JOB_DONE] = {STATE_READY, action_complete},
		[EVT_CMD_DONE] = {STATE_READY, action_complete},
		[EVT_ERROR] = {STATE_ERROR, action_error},
		// EVT_SUBMIT_JOB → illegal (device busy)
	},

	[STATE_ERROR] = {
		[EVT_RESET] = {STATE_RESET, action_reset},
	},

};

int fsm_dispatch(vfu_ctx_t *ctx, enum device_event evt) {
	struct math_device *dev = vfu_get_private(ctx);
	struct transition t = fsm[dev->state][evt];

	if (t.next == STATE_INVALID) {
		fprintf(stderr, "[HW:fsm] Illegal transition: state=%d event=%d\n", dev->state, evt);
		return -EINVAL;
	}

	dev->state = t.next;
	if (t.action)
		return t.action(ctx);

	return 0;
};
