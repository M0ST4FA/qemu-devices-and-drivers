#include "linux/dma-mapping.h"
#include "linux/wordpart.h"

#include "device.h"
#include "dma.h"

[[nodiscard("DMA memory may not have been allocated")]]
int mathaccel_init_dma(struct mathaccel_device *math_dev) {
	struct device *model_dev = &math_dev->pdev->dev;

	// 1. Tell the kernel we support 64-bit DMA addresses
	dma_set_mask_and_coherent(model_dev, DMA_BIT_MASK(64));

	// 2. Allocate the submission queue and completion queue
	if (math_dev->sq_cpu_addr == NULL)
		math_dev->sq_cpu_addr = dma_alloc_coherent(model_dev,
												   sizeof(struct math_sq_entry) * math_dev->ring_size,
												   &math_dev->sq_dma_addr, GFP_KERNEL);
	if (!math_dev->sq_cpu_addr) {
		pr_alert(MATHACCEL_DRIVER_NAME ": error during allocation of SQ DMA memory");
		return -ENOMEM;
	}
	pr_info(MATHACCEL_DRIVER_NAME ": allocated submission queue (virtual: %p, DMA: %llx)", math_dev->sq_cpu_addr, math_dev->sq_dma_addr);

	if (math_dev->cq_cpu_addr == NULL)
		math_dev->cq_cpu_addr = dma_alloc_coherent(model_dev,
												   sizeof(struct math_cq_entry) * MATHACCEL_RINGBUFFER_SIZE,
												   &math_dev->cq_dma_addr, GFP_KERNEL);
	if (!math_dev->cq_cpu_addr) {
		pr_alert(MATHACCEL_DRIVER_NAME ": error during allocation of CQ DMA memory");
		dma_free_coherent(model_dev,
						  sizeof(struct math_sq_entry) * math_dev->ring_size,
						  math_dev->sq_cpu_addr, math_dev->sq_dma_addr);
		return -ENOMEM;
	}
	pr_info(MATHACCEL_DRIVER_NAME ": allocated completion queue (virtual: %p, DMA: %llx)", math_dev->cq_cpu_addr, math_dev->cq_dma_addr);

	atomic_set(&math_dev->cmdid_counter, 1);
	math_dev->sq_head = math_dev->sq_tail = 0;
	math_dev->cq_head = math_dev->cq_tail = 0;

	// 3. Inform the hardware for the address we set up for it
	writel(lower_32_bits(math_dev->sq_dma_addr), math_dev->bar[0] + REG_DMA_SQ_BASE_LOWER);
	writel(upper_32_bits(math_dev->sq_dma_addr), math_dev->bar[0] + REG_DMA_SQ_BASE_UPPER);

	writel(lower_32_bits(math_dev->cq_dma_addr), math_dev->bar[0] + REG_DMA_CQ_BASE_LOWER);
	writel(upper_32_bits(math_dev->cq_dma_addr), math_dev->bar[0] + REG_DMA_CQ_BASE_UPPER);

	// 4. Inform the hardware how big the ring buffer is
	writel(math_dev->ring_size, math_dev->bar[0] + REG_DMA_RING_SIZE);

	return 0;
};

int mathaccel_release_dma(struct mathaccel_device *math_dev) {
	struct device *model_dev = &math_dev->pdev->dev;

	dma_free_coherent(model_dev,
					  sizeof(struct math_sq_entry) * math_dev->ring_size,
					  math_dev->sq_cpu_addr, math_dev->sq_dma_addr);
	dma_free_coherent(model_dev,
					  sizeof(struct math_cq_entry) * math_dev->ring_size,
					  math_dev->cq_cpu_addr, math_dev->cq_dma_addr);
	return 0;
};
