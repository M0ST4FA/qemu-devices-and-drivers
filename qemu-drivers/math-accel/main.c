#include "char.h"
#include "linux/fs.h"
#include "linux/pci.h"
#include "linux/printk.h"
#include "pci.h"
#include <linux/module.h>

static int __init mathaccel_init(void) {
	int ret = 0;

	ret = alloc_chrdev_region(&firstdev_id, 0, 3, MATHACCEL_DRIVER_NAME);
	if (ret < 0) {
		pr_alert(MATHACCEL_DRIVER_NAME ": failed to allocate chrdev region");
		return ret;
	}
	return pci_register_driver(&mathaccel_pci_driver);
}

static void __exit mathaccel_exit(void) {
	pci_unregister_driver(&mathaccel_pci_driver);
}

module_init(mathaccel_init);
module_exit(mathaccel_exit);
MODULE_DESCRIPTION("Driver for my custom math-accel vfio device");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("m0st4fa");
