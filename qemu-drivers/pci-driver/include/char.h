#pragma once
#include "linux/cdev.h"
#include "linux/device.h"
#include "linux/device/class.h"
#include <linux/fs.h>

extern struct file_operations edu_fops;
extern int edu_major;
extern struct class *class;
