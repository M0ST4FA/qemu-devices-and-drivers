#include "hw.h"
#include "linux/completion.h"
#include "linux/irqreturn.h"
#include "linux/printk.h"
#include "linux/spinlock.h"

#include "device.h"
#include "dma.h"
#include "irq.h"
#include "linux/xarray.h"

/* Handle error from bottom half
 * @note Expects to be called only in case there's an error; Oops otherwise
 * */
void mathaccel_irq_handle_error(struct mathaccel_device *dev) {
	int ret = 0;
	enum error_cause err_cause;

	pr_info("%s: interrupt indicating error for device (%d,%d)", dev->name, MAJOR(firstdev_id), dev->minor);

	err_cause = readl(dev->bar[0] + REG_ERROR_CAUSE);
	BUG_ON(err_cause == ERR_CAUSE_NOERR);

	pr_info("%s:\t\terror cause: %d, resetting function (device)", dev->name, err_cause);

	/* NOTE: both pci_reset_function() and pci_reset_bus() take locks, save state and then restore it later
	 * Resetting takes a lot of time and may wait.
	 * */

	ret = pci_reset_function(dev->pdev);
	if (ret == 0) {
		pr_info("%s:\t\tsuccessfullly reset function", dev->name);
	} else {
		pr_info("%s:\t\tfailed to reset function (probably device doesn't support FLR), resetting bus", dev->name);
		ret = pci_reset_bus(dev->pdev);
		if (!ret)
			pr_info("%s:\t\tfailed to reset bus (critical error)! code: %d", dev->name, ret);
	}

	// 3. Reconfigure DMA
	ret = mathaccel_init_dma(dev);

	// 4. Reconfigure device
	int flags = readl(dev->bar[0] + REG_FLAGS);
	flags |= (FLAG_INT_ENABLED | FLAG_DMA_ENABLED);
	pci_set_master(dev->pdev);
	writel(flags, dev->bar[0] + REG_FLAGS);

	// 5. Wakeup every single sleeping task to make sure they never sleep for something that won't come (device state has reset by now)
	unsigned long cmd_id;
	struct mathaccel_pending *entry;
	xa_for_each(&dev->pending_submissions, cmd_id, entry) {
		entry->req.result = -1;
		entry->cause = COMPLETION_CAUSE_ERROR;
		complete(&entry->done);
	}
};

void mathaccel_irq_read_legacy_cmd_result(struct mathaccel_device *dev) {
	u32 number;

	number = readl(dev->bar[0] + REG_ARG1);

	dev->legacy_res = number;
	atomic_set(&dev->legacy_comp_cause, COMPLETION_CAUSE_CMD_DONE);

	wake_up_all(&dev->legacy_q);
};

void mathaccel_irq_consume_completion_queue(struct mathaccel_device *dev) {
	struct math_cq_entry *entry = NULL;

	spin_lock(&dev->dma_lock);

	/* IMPORTANT: We take this opportunity to update sq_head
	 * The alternative is to have an interrupt in which the handler would syncrhonize
	 * driver state with device state. This is too heavy of a solution IMHO.
	 * The idea for updating it here is that: if a the device has already produced output,
	 * it must have already consumed input. sq_head is its input pointer. So, it must
	 * have changed.
	 * */
	dev->sq_head = readl(dev->bar[0] + REG_DMA_SQ_HEAD);

	dev->cq_tail = readl(dev->bar[0] + REG_DMA_CQ_TAIL);
	u32 head = dev->cq_head;
	u32 tail = dev->cq_tail;

	if (head == tail) { // Spurious interrupt; nothing to consume
		pr_info("%s: Spurious interrupt. Completion queue is empty.\n", dev->name);
		spin_unlock(&dev->dma_lock);
		return;
	}

	pr_info("%s: About to wakeup sleepers on %d entries\n", dev->name, dev->cq_tail - dev->cq_head);

	// No need to spin on dma_lock because % ensures head will always be correct, and the worst possible outcome is spurious wakeup

	entry = &dev->cq_cpu_addr[head];
	while (entry->valid && head != tail) {
		struct mathaccel_pending *pending;

		pending = xa_load(&dev->pending_submissions, dev->cq_cpu_addr[head].cmd_id);
		if (pending) {
			pending->req.result = entry->result;
			pending->req.status = entry->status;
			pending->cause = COMPLETION_CAUSE_JOB_DONE;
			complete(&pending->done);
		} else
			pr_alert("%s: completion for unkown cmd_id %u\n", dev->name, entry->cmd_id);

		entry->valid = 0;
		head = (head + 1) % dev->ring_size;
		entry = &dev->cq_cpu_addr[head];
	}
	spin_unlock(&dev->dma_lock);

	dev->cq_head = head;
	writel(dev->cq_head, dev->bar[0] + REG_DMA_CQ_HEAD);
};

irqreturn_t mathaccel_irq_handler(int irq, void *cookie) {
	struct mathaccel_device *dev = cookie;
	u32 state;

	// 1. Read status register and irq_cause to see why interrupt fired
	atomic_set(&dev->irq_cause, readl(dev->bar[0] + REG_IRQ_CAUSE));
	state = readl(dev->bar[0] + REG_STATUS);

	pr_info("%s: top-half fired for (%d,%d)", dev->name, MAJOR(firstdev_id), dev->minor);

	// 2. The action depends on the state
	switch (state) {
		case STATE_BUSY:
		case STATE_RESET:
		case STATE_COUNT:
			pr_info("%s: spurious interrupt for (%d,%d)", dev->name, MAJOR(firstdev_id), dev->minor);
			return IRQ_NONE;
		case STATE_ERROR:
		case STATE_READY:
			wake_up_all(&dev->kthread_wq);
			break;
		default:
			pr_info("%s: error in interrupt handler for (%d,%d). unkown state", dev->name, MAJOR(firstdev_id), dev->minor);
			break;
	}

	return IRQ_HANDLED;
};
