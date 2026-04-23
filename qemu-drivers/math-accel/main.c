#include "char.h"
#include "linux/fs.h"
#include "linux/pci.h"
#include "linux/printk.h"
#include "linux/slab.h"
#include "pci.h"
#include <linux/module.h>

static int __init mathaccel_init(void) {
	int ret = 0;

	ret = alloc_chrdev_region(&firstdev_id, 0, 255, MATHACCEL_DRIVER_NAME);
	if (ret < 0) {
		pr_alert(MATHACCEL_DRIVER_NAME ": failed to allocate chrdev region");
		return ret;
	}

	mathaccel_cache = kmem_cache_create(MATHACCEL_DRIVER_NAME "_cache", sizeof(struct mathaccel_device), NULL, SLAB_HWCACHE_ALIGN);
	if (mathaccel_cache == NULL) {
		pr_alert(MATHACCEL_DRIVER_NAME ": failed to allocate device slab cache");
		return -ENOMEM;
	}

	return pci_register_driver(&mathaccel_pci_driver);
}

static void __exit mathaccel_exit(void) {
	pci_unregister_driver(&mathaccel_pci_driver);
	kmem_cache_destroy(mathaccel_cache);
	unregister_chrdev_region(firstdev_id, 255);
}

module_init(mathaccel_init);
module_exit(mathaccel_exit);
MODULE_DESCRIPTION("Driver for my custom math-accel vfio device");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("m0st4fa");
