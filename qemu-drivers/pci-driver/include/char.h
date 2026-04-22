#pragma once
#include "linux/cdev.h"
#include "linux/device.h"
#include "linux/device/class.h"
#include "linux/types.h"
#include <linux/fs.h>

#include "edu.h"
#include "pci.h"

#define DEVICE_NAME "edu-pci"

int edu_open(struct inode *inode, struct file *filp);
int edu_release(struct inode *inode, struct file *filp);

ssize_t edu_read(struct file *filp, char __user *user_buf, size_t user_len, loff_t *user_off);
ssize_t edu_write(struct file *filp, const char __user *user_buf, size_t user_len, loff_t *user_off);

ssize_t edu_ioctl(struct file *filp, unsigned int cmd, unsigned long arg);

extern struct qemuedu_device {
	struct cdev char_device;
	struct device *device;
	struct qemuedu_pci_device *qpdev;
} edu_char_dev;
int edu_char_dev_init(struct qemuedu_device *dev, dev_t dev_id);
void edu_char_dev_destroy(struct qemuedu_device *dev);

extern struct file_operations fops;
extern int major;
extern struct class *class;
