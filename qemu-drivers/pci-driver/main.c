#include "common.h"
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
	edu_major = register_chrdev(0, EDU_DRIVER_NAME, &edu_fops);
	if (edu_major < 0) {
		pr_alert("Registering character device failed with error %d\n", edu_major);
		return edu_major;
	};

	class = class_create(EDU_DRIVER_NAME);
	device_create(class, NULL, MKDEV(edu_major, 0), &global_device, "%s-polling", EDU_DRIVER_NAME);
	device_create(class, NULL, MKDEV(edu_major, 1), NULL, "%s-irq", EDU_DRIVER_NAME);
	pr_info("Registered character devices for userspace interaction\n");

	// 2. Register interface with kernel PCI core
	return pci_register_driver(&edu_driver);
}

static void __exit edu_exit(void) {
	pr_info("edu: exit\n");

	// Destroy PCI driver
	pci_unregister_driver(&edu_driver);

	// Destroy character devices
	device_destroy(class, MKDEV(edu_major, 0));
	device_destroy(class, MKDEV(edu_major, 1));

	// Destroy device class
	class_destroy(class);

	// Unregister character device
	unregister_chrdev(edu_major, EDU_DRIVER_NAME);
}

module_init(edu_init);
module_exit(edu_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("m0st4fa");
MODULE_DESCRIPTION("QEMU edu PCI driver");
