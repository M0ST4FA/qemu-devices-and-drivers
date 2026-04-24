#include "./include/bar.h"
#include "./include/common.h"
#include "./include/dma.h"
#include "libvfio-user.h"
#include <stdint.h>
#include <stdio.h>
#include <sys/types.h>
#include <unistd.h>

static ssize_t bar0_write(struct vfu_ctx *ctx, char *const buf, [[maybe_unused]] size_t count, loff_t offset) {
	struct math_device_state *state = vfu_get_private(ctx);
	uint32_t val = *((uint32_t *)buf);
	printf("[HW] Write %u to offset 0x%lx\n", val, offset);

	switch (offset) {
		case REG_VAL:
			state->data = val;
			break;
		case REG_CMD:
			state->cmd = val;
			if (val == MATH_OP_MUL) {
				printf("[HW] Executing acceleration operation...\n");
				state->data = state->data * 2;
				state->status = STATUS_COMPLETED_CMD; // done
			}
			break;
		case REG_FLAGS:
			state->flags = val & FLAG_MASK;
			printf("Setting flags to %u\n", val & FLAG_MASK);
			break;
		case REG_DMA_SQ_BASE_LOWER:
			state->sq_base_addr = (vfu_dma_addr_t)(0x00000000ffffffff & (uint64_t)val);
			printf("[HW:DMA] Lower SQ base address set. SQ base: %p", state->sq_base_addr);
			break;
		case REG_DMA_SQ_BASE_UPPER:
			state->sq_base_addr = (vfu_dma_addr_t)((uint64_t)state->sq_base_addr | ((uint64_t)val << 32));
			printf("[HW:DMA] Upper SQ base address set. SQ base: %p", state->sq_base_addr);
			break;
		case REG_DMA_CQ_BASE_LOWER:
			state->cq_base_addr = (vfu_dma_addr_t)(0x00000000ffffffff & (uint64_t)val);
			printf("[HW:DMA] Lower CQ base address set. CQ base: %p", state->cq_base_addr);
			break;
		case REG_DMA_CQ_BASE_UPPER:
			state->cq_base_addr = (vfu_dma_addr_t)((uint64_t)state->cq_base_addr | (uint64_t)val << 32);
			printf("[HW:DMA] Upper CQ base address set. CQ base: %p", state->cq_base_addr);
			break;
		case REG_DMA_SQ_TAIL:
			state->sq_tail = val;
			printf("[HW:DMA] Tail of submission ring buffer updated by client. Head: %u, New tail: %u",
				   state->sq_head, val);
			printf("[HW:DMA] Heared a bell ring! Servicing...");
			break;
		case REG_DMA_CQ_HEAD:
			state->cq_head = val;
			printf("[HW:DMA] Head of completion ring buffer updated by client. Head: %u, New tail: %u",
				   state->sq_head, val);
			break;
		default:
			return -1;
	}

	return 0;
}

static ssize_t bar0_read(struct vfu_ctx *ctx, char *const buf, [[maybe_unused]] size_t count, loff_t offset) {
	struct math_device_state *state = vfu_get_private(ctx);
	uint32_t val = 0;

	if (offset > REG_OFFSET_MAX) {
		val = -1;
		goto finish;
	}

	switch (offset) {
		case REG_VAL:
			val = state->data;
			break;

		case REG_STATUS:
			val = state->status;

			// Reading the status register clears interrupt status
			if (state->status != STATUS_READY) {
				printf("[HW] Driver read status, setting it back to ready\n");
				state->status = STATUS_READY;
			}
			break;

		case REG_FLAGS:
			val = state->flags;
			break;

			// Client should use this to figure out how much device has consumed
		case REG_DMA_SQ_HEAD:
			val = state->sq_head;
			break;

			// Client should use this to figure out how much was produced
		case REG_DMA_CQ_TAIL:
			val = state->cq_tail;
			break;

		default:
			val = 0;
	}

finish:
	*((uint32_t *)buf) = val;
	printf("[HW] Read %u from offset 0x%lx\n", val, offset);
	return 0;
}

/*
 * The MMIO callback. Fires each time the Linux guest tries to read or write memory from BAR0.
 * */
ssize_t bar0_access(vfu_ctx_t *vfu_ctx, char *const buf, size_t count, loff_t offset, const bool is_write) {

	if (count != 4) {
		// Force the driver to use 4 byte reads/writes
		fprintf(stderr, "[HW]: Non-32-bit access attempted\n");
		return -1;
	}

	if (is_write) {
		if (bar0_write(vfu_ctx, buf, count, offset) == 0) {
			printf("[HW] Firing MSI interrupt!\n");
			// sleep(2);					 // Delay for experiment with concurrency chaos
			vfu_irq_trigger(vfu_ctx, 0); // 0 is the first MSI vector
			goto success;
		} else {
			goto error;
		}
	}

	if (bar0_read(vfu_ctx, buf, count, offset) < 0)
		goto error;

success:
	return count;

error:
	return -1;
};
