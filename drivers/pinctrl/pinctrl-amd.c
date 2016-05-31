/*
 * GPIO driver for AMD
 *
 * Copyright (c) 2014 Ken Xue <Ken.Xue@amd.com>
 *				Jeff Wu <Jeff.Wu@amd.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 */

#include <linux/err.h>
#include <linux/bug.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/spinlock.h>
#include <linux/compiler.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/log2.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/irqchip/chained_irq.h>
#include <linux/irqdomain.h>
#include <linux/gpio.h>
#include <linux/slab.h>
#include <linux/platform_device.h>
#include <linux/mutex.h>
#include <linux/acpi.h>
#include <linux/seq_file.h>
#include <linux/interrupt.h>
#include <linux/pinctrl/pinconf.h>
#include <linux/pinctrl/pinconf-generic.h>
#include <linux/list.h>
#include "pinctrl-utils.h"
#include "pinctrl-amd.h"

static int amd_gpio_direction_input(struct gpio_chip *gc, unsigned offset)
{
	int ret = 0;
	unsigned long flags;
	union gpio_pin_reg pin;
	struct amd_gpio *gpio_dev = container_of(gc, struct amd_gpio, gc);

	if (offset >= gpio_dev->gc.ngpio) {
		dev_err(&gpio_dev->pdev->dev, "offset(%d) > ngpio\n", offset);
		ret = -EINVAL;
		goto exit;
	}

	spin_lock_irqsave(&gpio_dev->lock, flags);
	pin.reg_u32 = readl(gpio_dev->base + offset * 4);
	/*
	* Assuming BIOS or Bootloader sets specific debounce for the
	* GPIO. if not, set debounce to be  2.75ms and remove glitch.
	*/
	if (pin.debounce_tmr_out == 0) {
		pin.debounce_tmr_out = 0xf;
		pin.debounce_tmr_out_unit = 1;
		pin.debounce_tmr_large = 0;
		pin.debounce_cntrl = DEBOUNCE_TYPE_REMOVE_GLITCH;
	}

	pin.output_enable = 0;
	writel(pin.reg_u32, gpio_dev->base + offset * 4);
	spin_unlock_irqrestore(&gpio_dev->lock, flags);

exit:
	return ret;
}

static int amd_gpio_direction_output(struct gpio_chip *gc, unsigned offset,
		int value)
{
	int ret = 0;
	unsigned long flags;
	union gpio_pin_reg pin;
	struct amd_gpio *gpio_dev = container_of(gc, struct amd_gpio, gc);

	if (offset >= gpio_dev->gc.ngpio) {
		dev_err(&gpio_dev->pdev->dev, "offset(%d) > ngpio\n", offset);
		ret = -EINVAL;
		goto exit;
	}

	spin_lock_irqsave(&gpio_dev->lock, flags);

	pin.reg_u32 = readl(gpio_dev->base + offset * 4);
	pin.output_enable = 1;
	pin.output_value = !!value;
	writel(pin.reg_u32, gpio_dev->base + offset * 4);

	spin_unlock_irqrestore(&gpio_dev->lock, flags);

exit:
	return ret;
}

static int amd_gpio_get_value(struct gpio_chip *gc, unsigned offset)
{
	unsigned long flags;
	union gpio_pin_reg pin;
	struct amd_gpio *gpio_dev = container_of(gc, struct amd_gpio, gc);

	spin_lock_irqsave(&gpio_dev->lock, flags);
	pin.reg_u32 = readl(gpio_dev->base + offset * 4);
	spin_unlock_irqrestore(&gpio_dev->lock, flags);

	return pin.pin_sts;
}

static void amd_gpio_set_value(struct gpio_chip *gc, unsigned offset, int value)
{
	unsigned long flags;
	union gpio_pin_reg pin;
	struct amd_gpio *gpio_dev = container_of(gc, struct amd_gpio, gc);

	spin_lock_irqsave(&gpio_dev->lock, flags);
	pin.reg_u32 = readl(gpio_dev->base + offset * 4);
	pin.output_value = !!value;
	writel(pin.reg_u32, gpio_dev->base + offset * 4);
	spin_unlock_irqrestore(&gpio_dev->lock, flags);
}

static int amd_gpio_to_irq(struct gpio_chip *gc, unsigned offset)
{
	unsigned int  ret;
	struct amd_gpio *gpio_dev = container_of(gc, struct amd_gpio, gc);

	ret = irq_create_mapping(gpio_dev->domain, offset);

	return ret;
}

static int amd_gpio_set_debounce(struct gpio_chip *gc, unsigned offset,
		unsigned debounce)
{
	unsigned long flags;
	union gpio_pin_reg pin;
	struct amd_gpio *gpio_dev = container_of(gc, struct amd_gpio, gc);

	spin_lock_irqsave(&gpio_dev->lock, flags);
	pin.reg_u32 = readl(gpio_dev->base + offset * 4);

	if (debounce) {
		pin.debounce_cntrl = DEBOUNCE_TYPE_REMOVE_GLITCH;
		/*
		Debounce	Debounce	Timer	Max
		TmrLarge	TmrOutUnit	Unit	Debounce
							Time
		0	0	61 usec (2 RtcClk)	976 usec
		0	1	244 usec (8 RtcClk)	3.9 msec
		1	0	15.6 msec (512 RtcClk)	250 msec
		1	1	62.5 msec (2048 RtcClk)	1 sec
		*/

		if (debounce < 61) {
			pin.debounce_tmr_out = 1;
			pin.debounce_tmr_out_unit = 0;
			pin.debounce_tmr_large = 0;
		} else if (debounce < 976) {
			pin.debounce_tmr_out = debounce / 61;
			pin.debounce_tmr_out_unit = 0;
			pin.debounce_tmr_large = 0;
		} else if (debounce < 3900) {
			pin.debounce_tmr_out = debounce / 244;
			pin.debounce_tmr_out_unit = 1;
			pin.debounce_tmr_large = 0;
		} else if (debounce < 250000) {
			pin.debounce_tmr_out = debounce / 15600;
			pin.debounce_tmr_out_unit = 0;
			pin.debounce_tmr_large = 1;
		} else if (debounce < 1000000) {
			pin.debounce_tmr_out = debounce / 62500;
			pin.debounce_tmr_out_unit = 1;
			pin.debounce_tmr_large = 1;
		} else {
			pin.debounce_cntrl = DEBOUNCE_TYPE_NO_DEBOUNCE;
			return -EINVAL;
		}
	} else {
		pin.debounce_tmr_out_unit = 0;
		pin.debounce_tmr_large = 0;
		pin.debounce_tmr_out = 0;
		pin.debounce_cntrl = DEBOUNCE_TYPE_NO_DEBOUNCE;
	}
	writel(pin.reg_u32, gpio_dev->base + offset * 4);
	spin_unlock_irqrestore(&gpio_dev->lock, flags);

	return 0;
}

#ifdef CONFIG_DEBUG_FS
static void amd_gpio_dbg_show(struct seq_file *s, struct gpio_chip *gc)
{
	unsigned long flags;
	union gpio_pin_reg pin;
	unsigned int bank, i, pin_count;
	struct amd_gpio *gpio_dev = container_of(gc, struct amd_gpio, gc);

	const char *level_trig = "Edge trigger ";
	const char *active_level = "Active high ";
	const char *interrupt_enable0 = " ";
	const char *interrupt_enable1 = " ";
	const char *wake_cntrl0 = " ";
	const char *wake_cntrl1 = " ";
	const char *wake_cntrl2 = " ";
	const char *pin_sts = "Pin is low ";
	const char *pull_up_sel = "4k pull-up ";
	const char *pull_up_enable = "Pull-up is disabled ";
	const char *pull_down_enable = "Pull-down is disabled ";
	const char *output_value = "Output value low ";
	const char *output_enable = "Output is disabled ";
	const char *sw_cntrl_en = "Disabled SW controlled GPIO in ";

	for (bank = 0; bank < AMD_GPIO_TOTAL_BANKS; bank++) {
		seq_printf(s, "GPIO bank%d\t", bank);

		switch (bank) {
		case 0:
			i = 0;
			pin_count = AMD_GPIO_PINS_BANK0;
			break;
		case 1:
			i = 64;
			pin_count = AMD_GPIO_PINS_BANK1 + i;
			break;
		case 2:
			i = 128;
			pin_count = AMD_GPIO_PINS_BANK2 + i;
			break;
		}

		for (i = 0; i < pin_count; i++) {
			seq_printf(s, "pin%d\t", i);
			spin_lock_irqsave(&gpio_dev->lock, flags);
			pin.reg_u32 = readl(gpio_dev->base + i * 4);
			spin_unlock_irqrestore(&gpio_dev->lock, flags);

			if (pin.level_trig & BIT(0))
				level_trig = "Level trigger ";

			if (pin.active_level & BIT(0))
				active_level = "Active low ";
			else if (pin.active_level & BIT(1))
				active_level = "Active on both ";
			else
				active_level = "";

			if (pin.interrupt_enable)
				interrupt_enable0 =
					"Enable interrupt status ";
			else if (pin.interrupt_mask)
				interrupt_enable1 =
					"Enable interrupt delivery ";
			else {
				interrupt_enable0 = "";
				interrupt_enable1 = "";
			}

			if (pin.wake_cntrl & BIT(0))
				wake_cntrl0 = "Enable wake in S0i3 state ";
			else if (pin.wake_cntrl & BIT(1))
				wake_cntrl1 = "Enable wake in S3 state ";
			else if (pin.wake_cntrl & BIT(2))
				wake_cntrl2 = "Enable wake in S4/S5 state ";
			else {
				wake_cntrl0 = "";
				wake_cntrl1 = "";
				wake_cntrl2 = "";
			}

			if (pin.pin_sts & BIT(0))
				pin_sts = "Pin is high ";

			if (pin.pull_up_sel & BIT(0))
				pull_up_sel = "8k pull-up ";

			if (pin.pull_up_enable & BIT(0))
				pull_up_enable = "pull-up is enabled ";

			if (pin.pull_down_enable & BIT(0))
				pull_down_enable = "pull-down is enabled ";

			if (pin.output_value & BIT(0))
				output_value = "output value high ";

			if (pin.output_enable & BIT(0))
				output_enable = "output is enabled ";

			if (pin.sw_cntrl_en & BIT(0))
				sw_cntrl_en = "Enable SW controlled GPIO in ";

			seq_printf(s, "%s %s %s %s %s %s\n"
				" %s %s %s %s %s %s %s %s\n",
				level_trig, active_level, interrupt_enable0,
				interrupt_enable1, wake_cntrl0, wake_cntrl1,
				wake_cntrl2, pin_sts, pull_up_sel,
				pull_up_enable, pull_down_enable,
				output_value, output_enable, sw_cntrl_en);
		}
	}
}
#else
#define amd_gpio_dbg_show NULL
#endif

static void amd_gpio_irq_enable(struct irq_data *d)
{
	unsigned long flags;
	union gpio_pin_reg pin;
	struct amd_gpio *gpio_dev = irq_data_get_irq_chip_data(d);

	spin_lock_irqsave(&gpio_dev->lock, flags);
	pin.reg_u32 = readl(gpio_dev->base + (d->hwirq)*4);
	/*
	* Assuming BIOS or Bootloader sets specific debounce for the
	* GPIO. if not, set debounce to be  2.75ms.
	*/
	if (pin.debounce_tmr_out == 0) {
		pin.debounce_tmr_out = 0xf;
		pin.debounce_tmr_out_unit = 1;
		pin.debounce_tmr_large = 0;
	}
	pin.interrupt_enable = ENABLE_INTERRUPT;
	pin.interrupt_mask = DISABLE_INTERRUPT_MASK;
	writel(pin.reg_u32, gpio_dev->base + (d->hwirq)*4);
	spin_unlock_irqrestore(&gpio_dev->lock, flags);
}

static void amd_gpio_irq_disable(struct irq_data *d)
{
	unsigned long flags;
	union gpio_pin_reg pin;
	struct amd_gpio *gpio_dev = irq_data_get_irq_chip_data(d);

	spin_lock_irqsave(&gpio_dev->lock, flags);
	pin.reg_u32 = readl(gpio_dev->base + (d->hwirq)*4);
	pin.interrupt_enable = DISABLE_INTERRUPT;
	pin.interrupt_mask =  ENABLE_INTERRUPT_MASK;
	writel(pin.reg_u32, gpio_dev->base + (d->hwirq)*4);
	spin_unlock_irqrestore(&gpio_dev->lock, flags);
}

static void amd_gpio_irq_mask(struct irq_data *d)
{
	unsigned long flags;
	union gpio_pin_reg pin;
	struct amd_gpio *gpio_dev = irq_data_get_irq_chip_data(d);

	spin_lock_irqsave(&gpio_dev->lock, flags);
	pin.reg_u32 = readl(gpio_dev->base + (d->hwirq)*4);
	pin.interrupt_mask =  ENABLE_INTERRUPT_MASK;
	writel(pin.reg_u32, gpio_dev->base + (d->hwirq)*4);
	spin_unlock_irqrestore(&gpio_dev->lock, flags);
}

static void amd_gpio_irq_unmask(struct irq_data *d)
{
	unsigned long flags;
	union gpio_pin_reg pin;
	struct amd_gpio *gpio_dev = irq_data_get_irq_chip_data(d);

	spin_lock_irqsave(&gpio_dev->lock, flags);
	pin.reg_u32 = readl(gpio_dev->base + (d->hwirq)*4);
	pin.interrupt_mask = DISABLE_INTERRUPT_MASK;
	writel(pin.reg_u32, gpio_dev->base + (d->hwirq)*4);
	spin_unlock_irqrestore(&gpio_dev->lock, flags);
}

static void amd_gpio_irq_eoi(struct irq_data *d)
{
	u32 reg;
	unsigned long flags;
	struct amd_gpio *gpio_dev = irq_data_get_irq_chip_data(d);

	spin_lock_irqsave(&gpio_dev->lock, flags);
	reg = readl(gpio_dev->base + WAKE_INT_MASTER_REG);
	reg |= EOI_MASK;
	writel(reg, gpio_dev->base + WAKE_INT_MASTER_REG);
	spin_unlock_irqrestore(&gpio_dev->lock, flags);
}

static int amd_gpio_irq_set_type(struct irq_data *d, unsigned int type)
{
	int ret = 0;
	unsigned long flags;
	union gpio_pin_reg pin;
	struct amd_gpio *gpio_dev = irq_data_get_irq_chip_data(d);

	spin_lock_irqsave(&gpio_dev->lock, flags);
	pin.reg_u32 = readl(gpio_dev->base + (d->hwirq)*4);

	switch (type & IRQ_TYPE_SENSE_MASK) {
	case IRQ_TYPE_EDGE_RISING:
		pin.level_trig = EDGE_TRAGGER;
		pin.active_level = ACTIVE_HIGH;
		pin.debounce_cntrl = DEBOUNCE_TYPE_REMOVE_GLITCH;
		__irq_set_handler_locked(d->irq, handle_edge_irq);
		break;

	case IRQ_TYPE_EDGE_FALLING:
		pin.level_trig = EDGE_TRAGGER;
		pin.active_level = ACTIVE_LOW;
		pin.debounce_cntrl = DEBOUNCE_TYPE_REMOVE_GLITCH;
		__irq_set_handler_locked(d->irq, handle_edge_irq);
		break;

	case IRQ_TYPE_EDGE_BOTH:
		pin.level_trig = EDGE_TRAGGER;
		pin.active_level = BOTH_EADGE;
		pin.debounce_cntrl = DEBOUNCE_TYPE_REMOVE_GLITCH;
		__irq_set_handler_locked(d->irq, handle_edge_irq);
		break;

	case IRQ_TYPE_LEVEL_HIGH:
		pin.level_trig = LEVEL_TRIGGER;
		pin.active_level = ACTIVE_HIGH;
		pin.debounce_cntrl = DEBOUNCE_TYPE_PRESERVE_LOW_GLITCH;
		__irq_set_handler_locked(d->irq, handle_level_irq);
		break;

	case IRQ_TYPE_LEVEL_LOW:
		pin.level_trig = LEVEL_TRIGGER;
		pin.active_level = ACTIVE_LOW;
		pin.debounce_cntrl = DEBOUNCE_TYPE_PRESERVE_HIGH_GLITCH;
		__irq_set_handler_locked(d->irq, handle_level_irq);
		break;

	case IRQ_TYPE_NONE:
		break;

	default:
		dev_err(&gpio_dev->pdev->dev, "Invalid type value\n");
		ret = -EINVAL;
		goto exit;
	}

	pin.interrupt_sts = CLR_INTR_STAT;
	writel(pin.reg_u32, gpio_dev->base + (d->hwirq)*4);
	spin_unlock_irqrestore(&gpio_dev->lock, flags);

exit:
	return ret;
}

static void amd_irq_ack(struct irq_data *d)
{
	/* based on HW design,there is no need to ack HW
	before handle current irq. But this routine is
	necessary for handle_edge_irq */
	return;
}

static struct irq_chip amd_gpio_irqchip = {
	.name         = "amd_gpio",
	.irq_ack      = amd_irq_ack,
	.irq_enable   = amd_gpio_irq_enable,
	.irq_disable  = amd_gpio_irq_disable,
	.irq_mask     = amd_gpio_irq_mask,
	.irq_unmask   = amd_gpio_irq_unmask,
	.irq_eoi      = amd_gpio_irq_eoi,
	.irq_set_type = amd_gpio_irq_set_type,
};

static void amd_gpio_irq_handler(unsigned int irq, struct irq_desc *desc)
{
	u32 reg;
	int handled = 0;
	unsigned long flags;
	union gpio_pin_reg pin;
	struct list_head *pos, *nx;
	struct amd_gpio_irq_pin *irq_pin;
	struct irq_chip *chip = irq_get_chip(irq);
	struct amd_gpio *gpio_dev = irq_desc_get_handler_data(desc);

	chained_irq_enter(chip, desc);

	list_for_each_safe(pos, nx, &gpio_dev->irq_list) {
		irq_pin = list_entry(pos, struct amd_gpio_irq_pin, list);
		pin.reg_u32 = readl(gpio_dev->base + irq_pin->pin_num * 4);
		if (pin.interrupt_sts || pin.wake_sts) {
			irq = irq_find_mapping(gpio_dev->domain,
						irq_pin->pin_num);
			generic_handle_irq(irq);
			writel(pin.reg_u32,
				gpio_dev->base + irq_pin->pin_num * 4);
			handled++;
		}
	}

	/* No interrupts were flagged.
	* there are two cases for bad irq.
	* 1. pin_X interrupt sts is set during handling another pin's irq.
	* then bad irq will be reported, when handle pin_X interrupt. But
	* it is acceptable, beacuse pin_X interrupt was already handled
	*  correctly during privous amd_gpio_irq_handler.
	* 2. GPIO interrupt pin is not listed in irq_list. Maybe a issue.
	*/
	if (handled == 0)
		handle_bad_irq(irq, desc);

	/*enable GPIO interrupt again*/
	spin_lock_irqsave(&gpio_dev->lock, flags);
	reg = readl(gpio_dev->base + WAKE_INT_MASTER_REG);
	reg |= EOI_MASK;
	writel(reg, gpio_dev->base + WAKE_INT_MASTER_REG);
	spin_unlock_irqrestore(&gpio_dev->lock, flags);

	chained_irq_exit(chip, desc);
}

static int amd_get_groups_count(struct pinctrl_dev *pctldev)
{
	struct amd_gpio *gpio_dev = pinctrl_dev_get_drvdata(pctldev);

	return gpio_dev->ngroups;
}

static const char *amd_get_group_name(struct pinctrl_dev *pctldev,
				      unsigned group)
{
	struct amd_gpio *gpio_dev = pinctrl_dev_get_drvdata(pctldev);

	return gpio_dev->groups[group].name;
}

static int amd_get_group_pins(struct pinctrl_dev *pctldev,
			      unsigned group,
			      const unsigned **pins,
			      unsigned *num_pins)
{
	struct amd_gpio *gpio_dev = pinctrl_dev_get_drvdata(pctldev);

	*pins = gpio_dev->groups[group].pins;
	*num_pins = gpio_dev->groups[group].npins;
	return 0;
}

static const struct pinctrl_ops amd_pinctrl_ops = {
	.get_groups_count	= amd_get_groups_count,
	.get_group_name		= amd_get_group_name,
	.get_group_pins		= amd_get_group_pins,
#ifdef CONFIG_OF
	.dt_node_to_map		= pinconf_generic_dt_node_to_map_group,
	.dt_free_map		= pinctrl_utils_dt_free_map,
#endif
};

static int amd_config_get(struct pinctrl_dev *pctldev,
			  unsigned int pin,
			  unsigned long *config)
{
	unsigned arg;
	unsigned long flags;
	union gpio_pin_reg gpio_pin;
	struct amd_gpio *gpio_dev = pinctrl_dev_get_drvdata(pctldev);
	enum pin_config_param param = pinconf_to_config_param(*config);

	spin_lock_irqsave(&gpio_dev->lock, flags);
	gpio_pin.reg_u32 = readl(gpio_dev->base + pin*4);
	spin_unlock_irqrestore(&gpio_dev->lock, flags);
	switch (param) {
	case PIN_CONFIG_INPUT_DEBOUNCE:
		arg = gpio_pin.debounce_tmr_out;
		break;

	case PIN_CONFIG_BIAS_PULL_DOWN:
		arg = gpio_pin.pull_down_enable;
		break;

	case PIN_CONFIG_BIAS_PULL_UP:
		arg = gpio_pin.pull_up_sel | (gpio_pin.pull_up_enable<<1);
		break;

	case PIN_CONFIG_DRIVE_STRENGTH:
		arg = gpio_pin.drv_strength_sel;
		break;

	default:
		dev_err(&gpio_dev->pdev->dev, "Invalid config param %04x\n",
			param);
		return -ENOTSUPP;
	}

	*config = pinconf_to_config_packed(param, arg);

	return 0;
}

static int amd_config_set(struct pinctrl_dev *pctldev, unsigned int pin,
				unsigned long *configs, unsigned num_configs)
{
	int i;
	unsigned arg;
	unsigned long flags;
	enum pin_config_param param;
	union gpio_pin_reg gpio_pin;
	struct amd_gpio *gpio_dev = pinctrl_dev_get_drvdata(pctldev);

	spin_lock_irqsave(&gpio_dev->lock, flags);
	for (i = 0; i < num_configs; i++) {
		param = pinconf_to_config_param(configs[i]);
		arg = pinconf_to_config_argument(configs[i]);
		gpio_pin.reg_u32 = readl(gpio_dev->base + pin*4);

		switch (param) {
		case PIN_CONFIG_INPUT_DEBOUNCE:
			gpio_pin.debounce_tmr_out = arg;
			break;

		case PIN_CONFIG_BIAS_PULL_DOWN:
			gpio_pin.pull_down_enable = arg;
			break;

		case PIN_CONFIG_BIAS_PULL_UP:
			gpio_pin.pull_up_sel = arg & BIT(0);
			gpio_pin.pull_up_enable = (arg>>1) & BIT(0);
			break;

		case PIN_CONFIG_DRIVE_STRENGTH:
			gpio_pin.drv_strength_sel = arg;
			break;

		default:
			dev_err(&gpio_dev->pdev->dev,
				"Invalid config param %04x\n", param);
			return -ENOTSUPP;
		}

		writel(gpio_pin.reg_u32, gpio_dev->base + pin*4);
	}
	spin_unlock_irqrestore(&gpio_dev->lock, flags);

	return 0;
}

static const struct pinconf_ops amd_pinconf_ops = {
	.pin_config_get		= amd_config_get,
	.pin_config_set		= amd_config_set,
};

static struct pinctrl_desc amd_pinctrl_desc = {
	.pins	= wt_pins,
	.npins = ARRAY_SIZE(wt_pins),
	.pctlops = &amd_pinctrl_ops,
	.confops = &amd_pinconf_ops,
	.owner = THIS_MODULE,
};

static int amd_gpio_irq_map(struct irq_domain *d, unsigned int virq,
			    irq_hw_number_t hw)
{
	unsigned long flags;
	struct list_head *pos, *nx;
	struct amd_gpio_irq_pin *irq_pin;
	struct amd_gpio *gpio_dev = d->host_data;

	list_for_each_safe(pos, nx, &gpio_dev->irq_list) {
		irq_pin = list_entry(pos, struct amd_gpio_irq_pin, list);
		if (irq_pin->pin_num == hw)
			return 0;
	}

	irq_pin = devm_kzalloc(&gpio_dev->pdev->dev,
				sizeof(struct amd_gpio_irq_pin), GFP_KERNEL);
	irq_pin->pin_num = hw;
	spin_lock_irqsave(&gpio_dev->lock, flags);
	list_add_tail(&irq_pin->list, &gpio_dev->irq_list);
	spin_unlock_irqrestore(&gpio_dev->lock, flags);

	irq_set_chip_and_handler_name(virq, &amd_gpio_irqchip,
					handle_simple_irq, "amdgpio");
	irq_set_chip_data(virq, gpio_dev);

	return 0;
}

static void amd_gpio_irq_unmap(struct irq_domain *d, unsigned int virq)
{
	unsigned long flags;
	struct irq_data *data;
	struct list_head *pos, *nx;
	struct amd_gpio_irq_pin *irq_pin;
	struct amd_gpio *gpio_dev = d->host_data;

	data = irq_get_irq_data(virq);

	list_for_each_safe(pos, nx, &gpio_dev->irq_list) {
		irq_pin = list_entry(pos, struct amd_gpio_irq_pin, list);
		if (data->hwirq == irq_pin->pin_num) {
			spin_lock_irqsave(&gpio_dev->lock, flags);
			list_del(pos);
			spin_unlock_irqrestore(&gpio_dev->lock, flags);
			devm_kfree(&gpio_dev->pdev->dev, irq_pin);
		}
	}
}

static const struct irq_domain_ops amd_gpio_irq_ops = {
	.map = amd_gpio_irq_map,
	.unmap = amd_gpio_irq_unmap,
	.xlate = irq_domain_xlate_onetwocell,
};

static int amd_gpio_probe(struct platform_device *pdev)
{
	int ret = 0;
	struct resource *res;
	struct amd_gpio *gpio_dev;

	gpio_dev = devm_kzalloc(&pdev->dev,
				sizeof(struct amd_gpio), GFP_KERNEL);
	if (!gpio_dev)
		return -ENOMEM;

	spin_lock_init(&gpio_dev->lock);
	INIT_LIST_HEAD(&gpio_dev->irq_list);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(&pdev->dev, "Failed to get gpio io resource.\n");
		return -EINVAL;
	}

	gpio_dev->base = devm_ioremap_nocache(&pdev->dev, res->start,
						resource_size(res));
	if (IS_ERR(gpio_dev->base))
		return PTR_ERR(gpio_dev->base);

	gpio_dev->irq = platform_get_irq(pdev, 0);
	if (gpio_dev->irq < 0) {
		dev_err(&pdev->dev, "Failed to get gpio IRQ.\n");
		return -EINVAL;
	}

	gpio_dev->pdev = pdev;
	gpio_dev->gc.direction_input	= amd_gpio_direction_input;
	gpio_dev->gc.direction_output	= amd_gpio_direction_output;
	gpio_dev->gc.get			= amd_gpio_get_value;
	gpio_dev->gc.set			= amd_gpio_set_value;
	gpio_dev->gc.set_debounce	= amd_gpio_set_debounce;
	gpio_dev->gc.to_irq			= amd_gpio_to_irq;
	gpio_dev->gc.dbg_show		= amd_gpio_dbg_show;

	gpio_dev->gc.base			= 0;
	gpio_dev->gc.label			= pdev->name;
	gpio_dev->gc.owner			= THIS_MODULE;
	gpio_dev->gc.dev			= &pdev->dev;
	gpio_dev->gc.ngpio			= TOTAL_NUMBER_OF_PINS;
#if defined(CONFIG_OF_GPIO)
	gpio_dev->gc.of_node			= pdev->dev.of_node;
#endif

	gpio_dev->groups = amur_groups;
	gpio_dev->ngroups = ARRAY_SIZE(amur_groups);

	amd_pinctrl_desc.name = dev_name(&pdev->dev);
	gpio_dev->pctrl = pinctrl_register(&amd_pinctrl_desc,
					&pdev->dev, gpio_dev);
	if (!gpio_dev->pctrl) {
		dev_err(&pdev->dev, "Couldn't register pinctrl driver\n");
		return -ENODEV;
	}

	gpio_dev->domain = irq_domain_add_linear(pdev->dev.of_node,
						TOTAL_NUMBER_OF_PINS,
					      &amd_gpio_irq_ops, gpio_dev);
	if (!gpio_dev->domain) {
		ret = -ENOSYS;
		dev_err(&pdev->dev, "Failed to register irq domain\n");
		goto out1;
	}

	ret = gpiochip_add(&gpio_dev->gc);
	if (ret)
		goto out2;

	ret = gpiochip_add_pin_range(&gpio_dev->gc, dev_name(&pdev->dev),
				0, 0, TOTAL_NUMBER_OF_PINS);
	if (ret) {
		dev_err(&pdev->dev, "Failed to add pin range\n");
		goto out3;
	}

	irq_set_handler_data(gpio_dev->irq, gpio_dev);
	irq_set_chained_handler(gpio_dev->irq, amd_gpio_irq_handler);

	platform_set_drvdata(pdev, gpio_dev);

	dev_dbg(&pdev->dev, "amd gpio driver loaded\n");
	return ret;

out3:
	ret = gpiochip_remove(&gpio_dev->gc);
	if (ret) {
		dev_err(&pdev->dev, "Failed to remove gpiochip\n");
		return ret;
	}

out2:
	irq_domain_remove(gpio_dev->domain);

out1:
	pinctrl_unregister(gpio_dev->pctrl);
	return ret;
}

static int amd_gpio_remove(struct platform_device *pdev)
{
	int ret;
	struct amd_gpio *gpio_dev;

	gpio_dev = platform_get_drvdata(pdev);

	irq_set_chained_handler(gpio_dev->irq, NULL);
	ret = gpiochip_remove(&gpio_dev->gc);
	if (ret) {
		dev_err(&pdev->dev, "Failed to remove gpiochip\n");
		return ret;
	}

	irq_domain_remove(gpio_dev->domain);
	pinctrl_unregister(gpio_dev->pctrl);

	return 0;
}

static const struct of_device_id amd_gpio_of_match[] = {
	{ .compatible = "amur,gpio" },
	{},
};
MODULE_DEVICE_TABLE(of, amd_gpio_of_match);

static const struct acpi_device_id amd_gpio_acpi_match[] = {
	{ "AMD0030", 0 },
	{ },
};
MODULE_DEVICE_TABLE(acpi, amd_gpio_acpi_match);

static struct platform_driver amd_gpio_driver = {
	.driver		= {
		.name	= "amd_gpio",
		.owner	= THIS_MODULE,
		.of_match_table = of_match_ptr(amd_gpio_of_match),
		.acpi_match_table = ACPI_PTR(amd_gpio_acpi_match),
	},
	.probe		= amd_gpio_probe,
	.remove		= amd_gpio_remove,
};

module_platform_driver(amd_gpio_driver);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Ken Xue <Ken.Xue@amd.com>, Jeff Wu <Jeff.Wu@amd.com>");
MODULE_DESCRIPTION("AMD GPIO pinctrl driver");
