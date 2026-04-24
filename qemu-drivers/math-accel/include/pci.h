#include "linux/cdev.h"
#include "linux/pci.h"
#include "linux/spinlock_types.h"
#include "linux/types.h"
#include "linux/wait.h"

#define MATHACCEL_DRIVER_NAME "math-accel"
#define MATHACCEL_DEVICE_ID 0x1234
#define MATHACCEL_VENDOR_ID 0x5678

#define MATHACCEL_REG_DATA 0x0
#define MATHACCEL_REG_CMD 0x4
#define MATHACCEL_REG_STATUS 0x8

#define MATHACCEL_CMD_MULTIPLY 0x1
#define MATHACCEL_STATUS_DONE 0x1

#define MATHACCEL_DEV_NR 4
#define MATHACCEL_RINGBUFFER_SIZE 256

extern struct pci_driver mathaccel_pci_driver;

enum math_op : u8 {
	MATH_OP_ADD = 0,
	MATH_OP_SUB,
	MATH_OP_MUL,
	MATH_OP_DIV,
};

#pragma pack(push, 1)
struct math_sq_entry {
	enum math_op opcode;
	u8 reserved[3];
	u32 cmd_id;
	u32 arg1;
	u32 arg2;
};

struct math_cq_entry {
	u32 cmd_id;
	u64 result;
};
#pragma pack(pop)

struct mathaccel_device {
	struct pci_dev *pdev;
	void *__iomem bar[1];
	struct cdev cdev;
	int minor;
	char name[64];

	struct wait_queue_head wq;

	// Protects result and completion variable
	struct spinlock lock;
	u64 result;
	int done;

	atomic_t shutting_down;
	atomic_t counter;

	// DMA
	struct math_sq_entry *sq_cpu_addr;
	dma_addr_t sq_dma_addr;

	struct math_cq_entry *cq_cpu_addr;
	dma_addr_t cq_dma_addr;
};

extern struct mathaccel_device mathaccel_dev[MATHACCEL_DEV_NR];
extern struct kmem_cache *mathaccel_cache;
