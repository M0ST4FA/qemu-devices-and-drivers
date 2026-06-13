#include "linux/bitops.h"
#include "linux/err.h"
#include "linux/irq.h"
#include "linux/irqchip/chained_irq.h"
#include "linux/irqdesc.h"
#include "linux/irqdomain.h"
#include "linux/pci.h"
#include "linux/platform_device.h"
#include "linux/printk.h"
#include <linux/module.h>

#include "../../qemu-devices/lux/include/hw.h"
#include "linux/stddef.h"
#include "linux/types.h"
#include "lux.h"

struct lux_irq_chip {
	struct irq_domain *irq_domain;
	struct lux_function *lux_function;
	void __iomem *base;
	int parent_irq;
	uint num_irqs;
	uint virqs[HWIRQ_COUNT];
};

static void lux_irq_mask(struct irq_data *data) {
	struct lux_irq_chip *lux_chip = irq_data_get_irq_chip_data(data);
	void __iomem *mask_addr = lux_chip->base + REG_IRQ_MASK;
	uint32_t mask = readl(mask_addr);

	mask |= (1U << data->hwirq);
	writel(mask, mask_addr);
};
static void lux_irq_unmask(struct irq_data *data) {
	struct lux_irq_chip *lux_chip = irq_data_get_irq_chip_data(data);
	void __iomem *mask_addr = lux_chip->base + REG_IRQ_MASK;
	uint32_t mask = readl(mask_addr);

	mask &= ~(1U << data->hwirq);
	writel(mask, mask_addr);
};
static void lux_irq_ack(struct irq_data *data) {
	struct lux_irq_chip *lux_chip = irq_data_get_irq_chip_data(data);
	void __iomem *ack_addr = lux_chip->base + REG_IRQ_ACK;

	uint32_t ack_mask = (1U << data->hwirq);
	writel(ack_mask, ack_addr);
};

static struct irq_chip lux_irq_chip = {
	.name = LUX_IRQ_DRIVER_NAME "-chip",
	.irq_mask = lux_irq_mask,	  // Clear bit in REG_IRQ_MASK
	.irq_unmask = lux_irq_unmask, // Set bit in REG_IRQ_MASK
	.irq_ack = lux_irq_ack,		  // Write bit to REG_IRQ_ACK
};

static int lux_irq_domain_map(struct irq_domain *d, uint virq, irq_hw_number_t hwirq) {
	struct lux_irq_chip *lux_chip = d->host_data;

	// NOTE: You may want to use `handle_level_irq` flow handler instead
	irq_set_chip_and_handler(virq, &lux_irq_chip, handle_edge_irq);
	irq_set_chip_data(virq, lux_chip);

	return 0;
}

static struct irq_domain_ops lux_irq_domain_ops = {
	.map = lux_irq_domain_map, // Only one manadatory
};

static int create_hwirq_virq_mappings(struct lux_irq_chip *lux_chip) {
	struct irq_domain *d = lux_chip->irq_domain;

	for (int i = 0; i < HWIRQ_COUNT; i++) {
		lux_chip->virqs[i] = irq_create_mapping(d, i);
		if (lux_chip->virqs[i] == 0) {
			pr_err(LUX_IRQ_DRIVER_NAME ": Failed to map hwirq %s to virq\n",
				   hwirq_names[i]);
			return -1;
		} else
			pr_info(LUX_IRQ_DRIVER_NAME ": Mapped hwirq %s to virq in domain %s\n",
					hwirq_names[i], d->name);
	}

	lux_chip->num_irqs = HWIRQ_COUNT;

	return 0;
}

static void lux_irq_chained_handler(struct irq_desc *desc) {
	struct lux_irq_chip *lux_chip = irq_desc_get_handler_data(desc);
	struct irq_chip *parent_chip = irq_desc_get_chip(desc); // PCI MSI chip

	pr_info(LUX_IRQ_DRIVER_NAME ": domain ptr %p\n", lux_chip->irq_domain);

	// The parent will now mask the hwirq referenced by desc
	chained_irq_enter(parent_chip, desc);

	uint32_t status = readl(lux_chip->base + REG_IRQ_STATUS);
	uint32_t mask = readl(lux_chip->base + REG_IRQ_MASK);
	uint32_t pending = status & ~mask;

	// Dispatch each pending child irq
	while (pending) {
		int hwirq = __ffs(pending); // Find First Set (ffs). Returns its index.

		generic_handle_domain_irq(lux_chip->irq_domain, hwirq);

		pending &= ~(1 << hwirq);
	}

	chained_irq_exit(parent_chip, desc);
}

// Get a vector in parent domain and assign a chained handler
// NOTE: No need to free previously allocated IRQs in this function.
// The PCI device is enabled using pcim_ interface and it handles them automatically.
// Freeing them later may lead to double-free issues.
static int get_parent_irq_and_assign_handler(struct platform_device *platdev) {
	int ret = 0;
	struct lux_irq_chip *lux_chip = platform_get_drvdata(platdev);
	struct lux_function *lux_function = lux_chip->lux_function;

	ret = pci_alloc_irq_vectors(lux_function->pdev, 1, 1,
								PCI_IRQ_MSI | PCI_IRQ_MSIX | PCI_IRQ_AFFINITY);
	if (ret < 0) {
		pr_err(LUX_IRQ_DRIVER_NAME ": Failed to allocate a virq in the domain of PCI MSI (err: %d)\n", ret);
		return ret;
	}

	// `pci_irq_vector` converts a per-device number into linux irq (aka virq)
	lux_chip->parent_irq = pci_irq_vector(lux_function->pdev, 0);
	irq_set_chained_handler_and_data(lux_chip->parent_irq, lux_irq_chained_handler, lux_chip);

	return 0;
}

static int lux_driver_irq_probe(struct platform_device *platdev) {
	struct device *parent_dev = platdev->dev.parent;
	struct lux_function *lux_function = dev_get_drvdata(parent_dev);

	// 1. Create and setup state
	int ret = 0;
	struct irq_domain *lux_irq_domain = NULL;
	struct lux_irq_chip *lux_chip = devm_kzalloc(&platdev->dev,
												 sizeof(*lux_chip), GFP_KERNEL);
	if (!lux_chip)
		return -ENOMEM;
	platform_set_drvdata(platdev, lux_chip);
	lux_chip->lux_function = lux_function;

	// Guaranteed to succeed (otherwise, laoding pci-bus would've failed)
	lux_chip->base = lux_function->bar[0];
	// Mask all for security; you don't want an interrupt to fire now
	writel(0xFFFFFFFFU, lux_chip->base + REG_IRQ_MASK);
	wmb();
	readl(lux_chip->base + REG_IRQ_MASK);

	// 2. Get a vector in parent domain and assign a chained handler
	ret = get_parent_irq_and_assign_handler(platdev);
	if (ret < 0)
		return ret;

	// 3. Create own domain
	lux_irq_domain = irq_domain_create_linear(NULL, 32,
											  &lux_irq_domain_ops, lux_chip);
	if (IS_ERR_OR_NULL(lux_irq_domain)) {
		pr_err(LUX_IRQ_DRIVER_NAME ": Failed to register IRQ domain (err: %ld)\n", PTR_ERR(lux_irq_domain));
		return PTR_ERR(lux_irq_domain);
	}
	lux_chip->irq_domain = lux_irq_domain;
	lux_function->irq_domain = lux_irq_domain;

	// 4. Map hwirqs from our chip to virqs through the domain
	if (create_hwirq_virq_mappings(lux_chip) < 0) {
		pr_err(LUX_IRQ_DRIVER_NAME ": Failed to map hwirqs exposed by chip to Linux virqs\n");
		return -1;
	}

	// 5. Unmask all of the IRQs
	writel(0x0ULL, lux_chip->base + REG_IRQ_MASK);
	wmb();
	readl(lux_chip->base + REG_IRQ_MASK);

	return 0;
}

static void lux_driver_irq_remove(struct platform_device *platdev) {
	struct lux_irq_chip *lux_chip = platform_get_drvdata(platdev);
	struct lux_function *lux_function = lux_chip->lux_function;

	// Prevent interrupts registration of other interrupts using this domain
	lux_function->irq_domain = NULL;

	writel(0x0ULL, lux_chip->base + REG_IRQ_MASK);
	wmb();
	readl(lux_chip->base + REG_IRQ_MASK);

	irq_set_chained_handler_and_data(lux_chip->parent_irq, NULL, NULL);

	for (int i = 0; i < HWIRQ_COUNT; i++)
		irq_dispose_mapping(lux_chip->virqs[i]);

	irq_domain_remove(lux_chip->irq_domain);

	pci_free_irq_vectors(lux_function->pdev);

	// Not needed because lifetime of device is attached to `lux_function`
	// kfree(lux_irq_chip); // Note: Will lead to double-free bug
}

static struct platform_driver lux_irq_platdev_driver = {
	.driver = {
		.name = LUX_IRQ_DRIVER_NAME,
		.owner = THIS_MODULE,
	},
	.probe = lux_driver_irq_probe,
	.remove = lux_driver_irq_remove,
};

module_platform_driver(lux_irq_platdev_driver);

MODULE_AUTHOR("m0st4fa");
MODULE_DESCRIPTION("IRQ chip driver for Lux multifunction device");
MODULE_LICENSE("GPL");
