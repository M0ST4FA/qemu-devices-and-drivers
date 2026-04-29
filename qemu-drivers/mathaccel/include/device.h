#pragma once

#include "asm/io.h"
#include "linux/cdev.h"
#include "linux/device.h"
#include "linux/pci.h"
#include "linux/spinlock.h"
#include "linux/spinlock_types.h"
#include "linux/types.h"
#include "linux/wait.h"
#include "linux/xarray.h"

#define MATHACCEL_DRIVER_NAME "mathaccel"
#define MATHACCEL_DEVICE_ID 0x1234
#define MATHACCEL_VENDOR_ID 0x5678

#define MATHACCEL_DEV_NR 4
#define MATHACCEL_RINGBUFFER_SIZE 256

#include "uapi.h"

enum register_offsets : uint32_t {
	REG_ARG1 = 0,
	REG_ARG2 = 4,
	REG_CMD = 8,
	REG_STATUS = 12,
	REG_FLAGS = 16,

	REG_DMA_EN = 20,

	REG_DMA_SQ_BASE_LOWER = 24,
	REG_DMA_SQ_BASE_UPPER = 28,
	REG_DMA_SQ_HEAD = 32,
	REG_DMA_SQ_TAIL = 36, // Updating the tail of the ring buffer rings the doorbell

	REG_DMA_CQ_BASE_LOWER = 40,
	REG_DMA_CQ_BASE_UPPER = 44,
	REG_DMA_CQ_HEAD = 48,
	REG_DMA_CQ_TAIL = 52,

	REG_DMA_RING_SIZE = 56,

	REG_IRQ_CAUSE = 60,
	REG_ERROR_CAUSE = 64,

	REG_OFFSET_MAX = 64,
};
static_assert(REG_OFFSET_MAX == 64, "You forgot to change the maximum register offset");

// NOTE: A device is basically a state machine, and the driver has to respect that.
// Here are the states
enum device_state : uint32_t {
	STATE_RESET = 0,
	STATE_READY,
	STATE_BUSY,
	STATE_ERROR,
	STATE_COUNT,
};

// FIXME: didn't implement this yet (either in driver or device)
enum device_flags : uint32_t {
	FLAG_INT_ENABLED = (1 << 0),
	FLAG_DMA_ENABLED = (1 << 1),

	FLAG_MASK = 0b11,
};
static_assert(FLAG_MASK == 0b11, "You forgot to change the flag mask");

enum irq_cause : uint32_t {
	IRQ_CAUSE_NOIRQ = 0,

	IRQ_CAUSE_JOB_DONE = (1 << 0),
	IRQ_CAUSE_CMD_DONE = (1 << 1),
	IRQ_CAUSE_ERROR = (1 << 2),
};

enum wakeup_cause : uint32_t {
	WAKEUP_CAUSE_JOB_DONE = (1 << 0),
	WAKEUP_CAUSE_CMD_DONE = (1 << 1),
	WAKEUP_CAUSE_ERROR = (1 << 2),
};

enum error_cause : uint32_t {
	ERR_CAUSE_NOERR = 0,

	ERR_CAUSE_DMA_READ = (1 << 0),		// failed to read from SQ
	ERR_CAUSE_DMA_WRITE = (1 << 1),		// failed to write to CQ
	ERR_CAUSE_DMA_BAD_QUEUE = (1 << 2), // SQ/CQ not configured
	ERR_CAUSE_CMD_UNKOWN = (1 << 3),	// unkown command (only during legacy command execution)
	ERR_CAUSE_CMD_EXEC = (1 << 4),		// error during cmd execution

	ERR_CAUSE_INTERNAL = (1 << 30), // unexpected internal error
};

struct [[gnu::packed]] math_sq_entry {
	enum math_op opcode;
	uint32_t cmd_id;
	uint32_t args[2];
};

struct [[gnu::packed]] math_cq_entry {
	uint32_t cmd_id;
	enum completion_status status; // 0 = success, 0 > = error
	uint64_t result;
	uint32_t valid;	  // 1 = Hardware wrote this, 0 = Empty slot
	uint32_t padding; // To make it 24-byte aligned
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
	u64 result;						   // Cached result of legacy command mode
	int done;						   // Legacy command done

	atomic_t shutting_down; // Indicates we're shutting down. Devices must return -ENODEV if this is true
	atomic_t counter;		// Counts number of accesses

	// struct xarray active_submissions; // Track active DMA submissions (used for cmd ID allocation)

	// DMA
	atomic_t cmdid_counter;
	atomic_t job_done;
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
