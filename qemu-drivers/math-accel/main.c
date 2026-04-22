#include <linux/module.h>

static int __init mathaccel_init(void) {

	return 0;
}

static void __exit mathaccel_exit(void) {
}

module_init(mathaccel_init);
module_exit(mathaccel_exit);
MODULE_DESCRIPTION("Driver for my custom math-accel vfio device");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("m0st4fa");
