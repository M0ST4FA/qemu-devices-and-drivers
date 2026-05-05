#pragma once

#include "linux/cdev.h"
#include "linux/device.h"
#include "linux/pci.h"
#include "linux/spinlock.h"
#include "linux/spinlock_types.h"
#include "linux/types.h"
#include "linux/wait.h"

#define MATHACCEL_DRIVER_NAME "mathaccel"
#define MATHACCEL_DEVICE_ID 0x1234
#define MATHACCEL_VENDOR_ID 0x5678

#define MATHACCEL_DEV_NR 4
#define MATHACCEL_RINGBUFFER_SIZE 256

#include "hw.h"
#include "uapi.h"

enum wakeup_cause : uint32_t {
	WAKEUP_CAUSE_JOB_DONE = (1 << 0),
	WAKEUP_CAUSE_CMD_DONE = (1 << 1),
	WAKEUP_CAUSE_ERROR = (1 << 2),

	WAKEUP_CAUSE_SHUTTING_DOWN = (1 << 30),
};

struct mathaccel_device {

	// Devices
	struct pci_dev *pdev;	  // Associated PCI subsystem device
	struct device *model_dev; // Linux driver model device
	struct cdev cdev;		  // Associated character subsystem device
	int minor;
	char name[64];
	atomic_t wakeup_cause; // Wakeup cause for

	// Resources
	void *__iomem bar[1]; // Cached PCI base address registers

	// Interrupt-driven IO support and caching
	struct spinlock legacy_cmd_lock; // Protects `result` and `done`
	enum irq_cause irq_cause;		 // Cached irq_cause for kthread
	struct wait_queue_head wq;		 // Wait queue for processes (interrupt-driven IO)
	struct task_struct *kthread;
	struct wait_queue_head kthread_wq; // Wait queue for kthreads (threaded interrupts)
	s64 result;						   // Cached result of legacy command mode

	atomic_t counter; // Counts number of accesses

	// struct xarray active_submissions; // Track active DMA submissions (used for cmd ID allocation)

	// DMA
	atomic_t cmdid_counter;
	u16 ring_size;

	struct spinlock dma_lock; // Protects access to: all `sq_*` fields, and all `cq_*` fields

	struct math_sq_entry *sq_cpu_addr; // Queue for submitting commands
	dma_addr_t sq_dma_addr;

	struct math_cq_entry *cq_cpu_addr; // Queue for receiving completed command results
	dma_addr_t cq_dma_addr;

	u16 sq_tail; // Points to the next free entry for submission
	u16 sq_head; // Points to first entry that device hasn't consumed yet

	u16 cq_tail; // Points to the next free completion entry that device hasn't filled yet
	u16 cq_head; // Points to the first entry that host hasn't consumed yet
};

int mathaccel_device_init(struct pci_dev *pdev);
void mathaccel_device_destroy(struct mathaccel_device *math_dev);

int mathaccel_completion_entry_valid(struct mathaccel_device *math_dev, int index);
int mathaccel_submit_one_cmd(struct mathaccel_device *math_dev, struct mathaccel_req *req);
int mathaccel_consume_one_cmd(struct mathaccel_device *math_dev, int index, struct mathaccel_req *req);
static inline void mathaccel_start_dma_job(struct mathaccel_device *math_dev) {
	// Lock to make sure noone can submit a job while we communicate with device
	spin_lock(&math_dev->dma_lock);

	// This has the side effect of starting the transaction
	writel(math_dev->sq_tail, math_dev->bar[0] + REG_DMA_SQ_TAIL);

	spin_unlock(&math_dev->dma_lock);
};

extern dev_t firstdev_id;
extern struct kmem_cache *mathaccel_cache;
extern struct class *mathaccel_class;
