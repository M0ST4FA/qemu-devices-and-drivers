#include "device.h"
#include "linux/irqreturn.h"

#include "irq.h"

void mathaccel_irq_handle_error(struct mathaccel_device *dev) {
	int ret = 0;
	enum irq_cause irq_cause = dev->irq_cause;
	enum error_cause err_cause;

	pr_info(MATHACCEL_DRIVER_NAME ": interrupt indicating error for device (%d,%d)", MAJOR(firstdev_id), dev->minor);
	if (!(irq_cause & IRQ_CAUSE_ERROR)) {
		pr_info(MATHACCEL_DRIVER_NAME ":\t\tthe cause of the IRQ is not an error. Probably a bug in the device");
		return;
	}

	err_cause = readl(dev->bar[0] + REG_ERROR_CAUSE);
	pr_info(MATHACCEL_DRIVER_NAME ":\t\terror cause: %d, resetting function (device)", err_cause);

	/* NOTE: both pci_reset_function() and pci_reset_bus() take locks, save state and then restore it later
	 * Resetting takes a lot of time and may wait.
	 * */

	ret = pci_reset_function(dev->pdev);
	if (ret == 0) {
		pr_info(MATHACCEL_DRIVER_NAME ":\t\tsuccessfullly reset function");
		return;
	}

	pr_info(MATHACCEL_DRIVER_NAME ":\t\tfailed to reset function (probably device doesn't support FLR), resetting bus");
	ret = pci_reset_bus(dev->pdev);
	if (!ret)
		pr_info(MATHACCEL_DRIVER_NAME ":\t\tfailed to reset bus (critical error)! code: %d", ret);
};

void mathaccel_irq_read_legacy_cmd_result(struct mathaccel_device *dev) {
	u32 number;

	number = readl(dev->bar[0] + REG_ARG1);

	spin_lock(&dev->lock);
	dev->result = number;
	// Set the condition variable
	dev->done = 1;
	spin_unlock(&dev->lock);

	wake_up(&dev->wq);
};

void mathaccel_irq_consume_completion_queue(struct mathaccel_device *dev) {

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
