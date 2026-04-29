#include "linux/irqreturn.h"
#include "linux/printk.h"
#include "linux/spinlock.h"

#include "device.h"
#include "dma.h"
#include "irq.h"

/* Handle error from bottom half
 * @note Expects to be called only in case there's an error; Oops otherwise
 * */
void mathaccel_irq_handle_error(struct mathaccel_device *dev) {
	int ret = 0;
	enum error_cause err_cause;

	pr_info(MATHACCEL_DRIVER_NAME ": interrupt indicating error for device (%d,%d)", MAJOR(firstdev_id), dev->minor);

	err_cause = readl(dev->bar[0] + REG_ERROR_CAUSE);
	BUG_ON(err_cause == ERR_CAUSE_NOERR);

	atomic_set(&dev->wakeup_cause, WAKEUP_CAUSE_ERROR);
	pr_info(MATHACCEL_DRIVER_NAME ":\t\terror cause: %d, resetting function (device)", err_cause);

	/* NOTE: both pci_reset_function() and pci_reset_bus() take locks, save state and then restore it later
	 * Resetting takes a lot of time and may wait.
	 * */

	ret = pci_reset_function(dev->pdev);
	if (ret == 0) {
		pr_info(MATHACCEL_DRIVER_NAME ":\t\tsuccessfullly reset function");
	} else {
		pr_info(MATHACCEL_DRIVER_NAME ":\t\tfailed to reset function (probably device doesn't support FLR), resetting bus");
		ret = pci_reset_bus(dev->pdev);
		if (!ret)
			pr_info(MATHACCEL_DRIVER_NAME ":\t\tfailed to reset bus (critical error)! code: %d", ret);
	}

	// 3. Reconfigure DMA
	ret = mathaccel_init_dma(dev);

	// 4. Reconfigure device
	int flags = readl(dev->bar[0] + REG_FLAGS);
	flags |= (FLAG_INT_ENABLED | FLAG_DMA_ENABLED);
	pci_set_master(dev->pdev);
	writel(flags, dev->bar[0] + REG_FLAGS);

	wake_up(&dev->wq);
};

void mathaccel_irq_read_legacy_cmd_result(struct mathaccel_device *dev) {
	u32 number;

	number = readl(dev->bar[0] + REG_ARG1);

	spin_lock(&dev->legacy_cmd_lock);
	dev->result = number;
	// Set the condition variable
	spin_unlock(&dev->legacy_cmd_lock);

	atomic_set(&dev->wakeup_cause, WAKEUP_CAUSE_CMD_DONE);

	wake_up(&dev->wq);
};

void mathaccel_irq_consume_completion_queue(struct mathaccel_device *dev) {
	atomic_set(&dev->wakeup_cause, WAKEUP_CAUSE_JOB_DONE);
	wake_up(&dev->wq);
};

irqreturn_t mathaccel_irq_handler(int irq, void *cookie) {
	struct mathaccel_device *dev = cookie;
	u32 state;

	// 1. Read status register and irq_cause to see why interrupt fired
	dev->irq_cause = readl(dev->bar[0] + REG_IRQ_CAUSE);
	state = readl(dev->bar[0] + REG_STATUS);

	pr_info(MATHACCEL_DRIVER_NAME ": top-half fired for (%d,%d)", MAJOR(firstdev_id), dev->minor);

	// 2. The action depends on the state
	switch (state) {
		case STATE_BUSY:
		case STATE_RESET:
		case STATE_COUNT:
			pr_info(MATHACCEL_DRIVER_NAME ": spurious interrupt for (%d,%d)", MAJOR(firstdev_id), dev->minor);
			return IRQ_NONE;
		case STATE_ERROR:
		case STATE_READY:
			wake_up(&dev->kthread_wq);
			break;
		default:
			pr_info(MATHACCEL_DRIVER_NAME ": error in interrupt handler for (%d,%d). unkown state", MAJOR(firstdev_id), dev->minor);
			break;
	}

	return IRQ_HANDLED;
};
