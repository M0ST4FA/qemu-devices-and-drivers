#pragma once

#include "linux/cdev.h"
#include "linux/wait.h"

#define EDU_DRIVER_NAME "edu_pci"

struct edu_dev {
	struct pci_dev *pdev;
	void __iomem *base;

	wait_queue_head_t wq;
	spinlock_t lock;

	struct cdev cdev;

	u32 result;
	bool done;
};

extern struct edu_dev global_device;
