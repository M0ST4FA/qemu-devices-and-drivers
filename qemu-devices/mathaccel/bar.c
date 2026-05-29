#include "libvfio-user.h"
#include <stdint.h>
#include <stdio.h>
#include <sys/types.h>
#include <unistd.h>

#include "bar.h"
#include "device.h"
#include "fsm.h"
#include "mathaccel/include/hw.h"

static ssize_t f0_bar0_write(struct vfu_ctx *ctx, char *const buf, [[maybe_unused]] size_t count, off_t offset) {
	struct math_device *dev = vfu_get_private(ctx);
	uint32_t val = *((uint32_t *)buf);
	printf("[HW] Write %u to offset 0x%lx\n\t", val, offset);

	switch (offset) {
		case REG_ARG1:
			dev->args[0] = val;
			break;
		case REG_ARG2:
			dev->args[1] = val;
			break;
		case REG_CMD:
			dev->cmd = val;
			return fsm_dispatch(ctx, EVT_SUBMIT_CMD);
			break;
		case REG_FLAGS:
			dev->flags = val & FLAG_MASK;
			printf("Setting flags to %u\n", val & FLAG_MASK);
			break;

		case REG_DMA_SQ_BASE_LOWER:
			dev->sq_base_addr = (vfu_dma_addr_t)(0x00000000ffffffff & (uint64_t)val);
			printf("Lower SQ base address set. SQ base: %p\n", dev->sq_base_addr);
			break;
		case REG_DMA_SQ_BASE_UPPER:
			dev->sq_base_addr = (vfu_dma_addr_t)((uint64_t)dev->sq_base_addr | ((uint64_t)val << 32));
			printf("Upper SQ base address set. SQ base: %p\n", dev->sq_base_addr);
			break;
		case REG_DMA_CQ_BASE_LOWER:
			dev->cq_base_addr = (vfu_dma_addr_t)(0x00000000ffffffff & (uint64_t)val);
			printf("Lower CQ base address set. CQ base: %p\n", dev->cq_base_addr);
			break;
		case REG_DMA_CQ_BASE_UPPER:
			dev->cq_base_addr = (vfu_dma_addr_t)((uint64_t)dev->cq_base_addr | (uint64_t)val << 32);
			printf("Upper CQ base address set. CQ base: %p\n", dev->cq_base_addr);
			break;

		case REG_DMA_SQ_TAIL:
			dev->sq_tail = val;
			printf("Tail of submission ring buffer updated by client. Head: %u, New tail: %u\n",
				   dev->sq_head, val);
			printf("Heared a bell ring! Servicing...\n");
			return fsm_dispatch(ctx, EVT_SUBMIT_JOB);
			break;
		case REG_DMA_CQ_HEAD:
			dev->cq_head = val;
			printf("Head of completion ring buffer updated by client. Head: %u, New tail: %u\n",
				   dev->sq_head, val);
			break;

		case REG_DMA_RING_SIZE:
			dev->ring_size = val;
			printf("Size of ring buffer set (%d)\n", val);
			break;
		default:
			printf("Writing to invalid register (or valid but hasn't been implemented yet). Register: %lu, value: %d\n",
				   offset, val);
			return -1;
	}

	return 0;
}

static ssize_t f0_bar0_read(struct vfu_ctx *ctx, char *const buf, [[maybe_unused]] size_t count, off_t offset) {
	struct math_device *dev = vfu_get_private(ctx);
	uint32_t val = 0;

	if (offset > REG_OFFSET_MAX) {
		val = -1;
		goto finish;
	}

	switch (offset) {
		case REG_ARG1:
			val = dev->args[0];
			break;
		case REG_ARG2:
			val = dev->args[1];
			break;

		case REG_STATUS:
			val = dev->state;
			break;

		case REG_IRQ_CAUSE:
			val = dev->irq_cause;
			dev->irq_cause = IRQ_CAUSE_NOIRQ; // writing 1 to clear is more accurate, but this is simpler
			break;

		case REG_ERROR_CAUSE:
			val = dev->error_cause;
			dev->error_cause = ERR_CAUSE_NOERR;
			break;

		case REG_FLAGS:
			val = dev->flags;
			break;

			// Client should use this to figure out how much device has consumed
		case REG_DMA_SQ_HEAD:
			val = dev->sq_head;
			break;

			// Client should use this to figure out how much was produced
		case REG_DMA_CQ_TAIL:
			val = dev->cq_tail;
			break;

		default:
			val = 0;
	}

finish:
	printf("[HW] Read %u from offset 0x%lx\n", val, offset);
	*((uint32_t *)buf) = val;
	return 0;
}

/*
 * The MMIO callback. Fires each time the Linux guest tries to read or write memory from BAR0.
 * */
ssize_t f0_bar0_access(vfu_ctx_t *vfu_ctx, char *const buf, size_t count, loff_t offset, const bool is_write) {

	if (count != 4) {
		// Force the driver to use 4 byte reads/writes
		fprintf(stderr, "[HW]: Non-32-bit access attempted\n");
		return -1;
	}

	if (is_write) {
		if (f0_bar0_write(vfu_ctx, buf, count, offset) == 0) {
			// sleep(2);					 // Delay for experiment with concurrency chaos
			goto success;
		} else {
			goto error;
		}
	}

	if (f0_bar0_read(vfu_ctx, buf, count, offset) < 0)
		goto error;

success:
	return count;

error:
	return -1;
};
