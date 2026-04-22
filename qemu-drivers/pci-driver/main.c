#include "linux/device.h"
#include "linux/device/class.h"
#include "linux/fs.h"
#include "linux/kdev_t.h"
#include "linux/printk.h"
#include "linux/stddef.h"
#include <linux/module.h>
#include <linux/pci.h>

#include "char.h"
#include "pci.h"

// 4. Module init and exit
static int __init edu_init(void) {
	pr_info("edu: init\n");

	// 1. Register character device (interface to userspace)
	major = register_chrdev(0, DEVICE_NAME, &fops);
	if (major < 0) {
		pr_alert("Registering character device failed with error %d\n", major);
		return major;
	};

	class = class_create(DEVICE_NAME);
	device_create(class, NULL, MKDEV(major, 0), &edu_dev, "%s-polling", DEVICE_NAME);
	device_create(class, NULL, MKDEV(major, 1), NULL, "%s-irq", DEVICE_NAME);
	pr_info("Registered character devices for userspace interaction\n");

	// 2. Register interface with kernel PCI core
	return pci_register_driver(&edu_driver);
}

static void __exit edu_exit(void) {
	pr_info("edu: exit\n");

	// Destroy PCI driver
	pci_unregister_driver(&edu_driver);

	// Destroy character devices
	device_destroy(class, MKDEV(major, 0));
	device_destroy(class, MKDEV(major, 1));

	// Destroy device class
	class_destroy(class);

	// Unregister character device
	unregister_chrdev(major, DEVICE_NAME);
}

module_init(edu_init);
module_exit(edu_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("m0st4fa");
MODULE_DESCRIPTION("QEMU edu PCI driver");
