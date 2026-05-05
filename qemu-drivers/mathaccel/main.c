#include "device.h"
#include "linux/device/class.h"
#include "linux/fs.h"
#include "linux/pci.h"
#include "linux/printk.h"
#include "linux/slab.h"
#include <linux/module.h>

#include "pci.h"

static int __init mathaccel_init(void) {
	int ret = 0;

	ret = alloc_chrdev_region(&firstdev_id, 0, 255, MATHACCEL_DRIVER_NAME);
	if (ret < 0) {
		pr_alert(MATHACCEL_DRIVER_NAME ": failed to allocate chrdev region");
		return ret;
	}

	mathaccel_class = class_create(MATHACCEL_DRIVER_NAME);
	if (IS_ERR(mathaccel_class)) {
		pr_alert(MATHACCEL_DRIVER_NAME ": failed to create sysfs class for device");
		goto err_create_class;
	}

	mathaccel_cache = kmem_cache_create(MATHACCEL_DRIVER_NAME "_cache", sizeof(struct mathaccel_device), NULL, SLAB_HWCACHE_ALIGN);
	if (mathaccel_cache == NULL) {
		pr_alert(MATHACCEL_DRIVER_NAME ": failed to allocate device slab cache");
		ret = -ENOMEM;
		goto err_allocate_cache;
	}

	ret = pci_register_driver(&mathaccel_pci_driver);
	if (ret < 0) {
		pr_alert(MATHACCEL_DRIVER_NAME ": failed to register pci driver");
		goto err_register_pci;
	}

	return 0;

err_create_class:
	unregister_chrdev_region(firstdev_id, 255);
err_allocate_cache:
	class_destroy(mathaccel_class);
err_register_pci:
	kmem_cache_destroy(mathaccel_cache);
	return ret;
}

static void __exit mathaccel_exit(void) {
	pci_unregister_driver(&mathaccel_pci_driver);
	unregister_chrdev_region(firstdev_id, 255);
	class_destroy(mathaccel_class);
	kmem_cache_destroy(mathaccel_cache);
}

module_init(mathaccel_init);
module_exit(mathaccel_exit);
MODULE_DESCRIPTION("Driver for my custom mathaccel vfio device");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("m0st4fa");
