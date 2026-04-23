#pragma once

#include "asm-generic/int-ll64.h"
#include "asm-generic/iomap.h"
#include "common.h"
#include "linux/irqreturn.h"
#include "linux/pci.h"

#define PCI_VENDOR_ID_QEMU 0x1234
#define PCI_DEVICE_ID_EDU 0x11e8

// edu-pci registers inside BAR0 (byte offsets)
#define EDU_REG_IDENT 0x00
#define EDU_REG_LIVENESS 0x04
#define EDU_REG_FACTORIAL 0x08 // 12 bytes
#define EDU_REG_STATUS 0x20
#define EDU_REG_IRQSTATUS 0x24
#define EDU_REG_IRQACK 0x64

// edu-pci register values
#define EDU_STATUS_COMPUTING 0x1
#define EDU_STATUS_RAISEIRQ 0x80

// public functions for use to implement userspace API
static inline u32 edu_hw_read(struct edu_dev *edu_dev, u32 reg) {
	return ioread32(edu_dev->base + reg);
}
static inline void edu_hw_write(struct edu_dev *edu_dev, u32 reg, u32 val) {
	iowrite32(val, edu_dev->base);
}

extern irqreturn_t edu_irq_handler(int irq, void *dev_id);

extern struct pci_driver edu_driver;
