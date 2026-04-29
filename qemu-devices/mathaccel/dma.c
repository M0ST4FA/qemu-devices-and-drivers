#include "dma.h"
#include "common.h"
#include "libvfio-user.h"
#include <alloca.h>
#include <assert.h>
#include <err.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/errno.h>
#include <sys/mman.h>
#include <unistd.h>

// Read next command from submission queue
int dma_read_next(struct vfu_ctx *ctx, struct math_sq_entry *cmd) {
	struct math_device *state = vfu_get_private(ctx);
	int ret = 0;

	if (state->sq_head == state->sq_tail) {
		printf("[HW:DMA] Submission queue is empty\n");
		return -EAGAIN;
	}

	// 1. Calculate the Guest Physical Address (GPA) of the current command (we read from the head)
	vfu_dma_addr_t gpa = state->sq_base_addr + (state->sq_head * sizeof(struct math_sq_entry));

	// 2. Translate into device address and read
	dma_sg_t *sg = alloca(dma_sg_size());
	assert(sg != NULL);
	ret = vfu_addr_to_sgl(ctx, gpa,
						  sizeof(*cmd), sg, 1, PROT_READ);
	if (ret < 0) {
		printf("[HW:DMA] DMA read failed at index %u\n", state->sq_head);
		return ret;
	}
	ret = vfu_sgl_read(ctx, sg, 1, cmd, 0);
	if (ret < 0) {
		printf("[HW:DMA] Failed to read SGL content into cmd at index %u\n", state->sq_head);
		return ret;
	}

	printf("[HW:DMA] Received cmd (IDX: %u, OP: %u, ARGS: %u, %u))\n",
		   state->sq_head, cmd->opcode, cmd->args[0], cmd->args[1]);

	// 3. Advance the hardware head of submission queue to indicate we've consumed it (we're the consumer)
	state->sq_head = (state->sq_head + 1) % state->ring_size;

	return ret;
}

// Write next result into completion queue
int dma_write_next(struct vfu_ctx *ctx, struct math_cq_entry *res) {
	struct math_device *state = vfu_get_private(ctx);
	int ret = 0;

	// 1. Calculate the Guest Physical Address (GPA) of the current result
	vfu_dma_addr_t gpa = state->cq_base_addr + (state->cq_tail * sizeof(struct math_sq_entry));

	// 2. Translate into device address and read
	dma_sg_t *sg = alloca(dma_sg_size());
	assert(sg != NULL);
	ret = vfu_addr_to_sgl(ctx, gpa,
						  sizeof(*res), sg, 1, PROT_WRITE);
	if (ret < 0) {
		printf("[HW:DMA] Failed to write result data into SGL at index %u\n", state->cq_tail);
		return ret;
	}

	ret = vfu_sgl_write(ctx, sg, 1, res, 0);
	if (ret < 0) {
		printf("[HW:DMA] DMA write failed at index %u\n", state->cq_tail);
		return ret;
	}

	printf("[HW:DMA] Written result (IDX: %u, RES: %lu)\n",
		   state->cq_tail, res->result);

	// 3. Update pointers to indicate we've produced a new entry
	state->sq_tail = (state->cq_tail + 1) % state->ring_size;

	return ret;
}
