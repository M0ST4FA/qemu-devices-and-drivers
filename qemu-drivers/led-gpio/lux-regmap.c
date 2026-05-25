#include "asm/io.h"
#include <linux/gpio.h>
#include <linux/gpio/driver.h>
#include <linux/gpio/regmap.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/printk.h>
#include <linux/regmap.h>

#include "../../qemu-devices/led-gpio/include/hw.h"
#include "linux/dev_printk.h"
#include "linux/err.h"
#include "lux.h"

#define REGMAP_DIR (REG_DIRECTION - 8)
#define REGMAP_DAT (REG_DATA - 8)
#define REGMAP_SET (REG_SET - 8)
#define REGMAP_CLR (REG_CLR - 8)

static const struct regmap_config lux_regmap_config = {
	.name = LUX_CHIP_LABEL,
	.reg_bits = 32,	 // Offsets are passed as 32-bit numbers
	.val_bits = 64,	 // Data values are passed as 64-bit numbers
	.reg_stride = 8, // Registers are spaced 8 bytes appart
	.max_register = REGMAP_CLR,
};

static int __init lux_gpio_init(void) {
	int ret = 0;
	struct regmap *map;
	struct gpio_regmap_config gpio_regmap_config = {0};
	struct gpio_regmap *gpio_map;
	void __iomem *regmap_base = NULL;

	if (global_lux == NULL || global_lux->bar[0] == NULL) {
		pr_err(LUX_CHIP_LABEL ": Lux device not present...probably module lux-core not loaded yet");
		return -ENODEV;
	}

	regmap_base = global_lux->bar[0] + 0x8; // Skip the first 2 4-byte registers

	// 1. Create the register map
	map = devm_regmap_init_mmio(&global_lux->pdev->dev,
								regmap_base, &lux_regmap_config);
	if (IS_ERR(map)) {
		pr_err(LUX_CHIP_LABEL ": Failed to initialize register map (err: %ld).", PTR_ERR(map));
		pr_err(LUX_CHIP_LABEL ": Registration will always fail because the device expects 64-bit registers, which regmap-mmio doesn't work with :)\n");

		return PTR_ERR(map);
	}

	// 2. Configer the GPIO behavior
	gpio_regmap_config.label = LUX_CHIP_LABEL;
	gpio_regmap_config.parent = &global_lux->pdev->dev;
	gpio_regmap_config.regmap = map;
	gpio_regmap_config.ngpio = 64;

	gpio_regmap_config.reg_dat_base = REGMAP_DAT;
	gpio_regmap_config.reg_set_base = REGMAP_SET;
	gpio_regmap_config.reg_clr_base = REGMAP_CLR;
	gpio_regmap_config.reg_dir_out_base = REGMAP_DIR;

	// 3. Register the GPIO regmap
	gpio_map = devm_gpio_regmap_register(&global_lux->pdev->dev, &gpio_regmap_config);
	if (IS_ERR(gpio_map)) {
		pr_err(LUX_CHIP_LABEL ": Failed to register gpio register map: %ld\n",
			   PTR_ERR(gpio_map));
		return PTR_ERR(gpio_map);
	}

	pr_info(LUX_CHIP_LABEL ": Successfully registered modern regmap chip");

	return ret;
}

static void __exit lux_gpio_exit(void) {
}

module_init(lux_gpio_init);
module_exit(lux_gpio_exit);
MODULE_AUTHOR("m0st4fa");
MODULE_DESCRIPTION("GPIO frontend for Lux device using regmap");
MODULE_LICENSE("GPL");
