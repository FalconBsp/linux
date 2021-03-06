/*
 * Copyright 2013 Emilio López
 *
 * Emilio López <emilio@elopez.com.ar>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/clk-provider.h>
#include <linux/clkdev.h>
#include <linux/clk/sunxi.h>
#include <linux/of.h>
#include <linux/of_address.h>

#include "clk-factors.h"

static DEFINE_SPINLOCK(clk_lock);

/* Maximum number of parents our clocks have */
#define SUNXI_MAX_PARENTS	5

/**
 * sun4i_osc_clk_setup() - Setup function for gatable oscillator
 */

#define SUNXI_OSC24M_GATE	0

static void __init sun4i_osc_clk_setup(struct device_node *node)
{
	struct clk *clk;
	struct clk_fixed_rate *fixed;
	struct clk_gate *gate;
	const char *clk_name = node->name;
	u32 rate;

	/* allocate fixed-rate and gate clock structs */
	fixed = kzalloc(sizeof(struct clk_fixed_rate), GFP_KERNEL);
	if (!fixed)
		return;
	gate = kzalloc(sizeof(struct clk_gate), GFP_KERNEL);
	if (!gate) {
		kfree(fixed);
		return;
	}

	if (of_property_read_u32(node, "clock-frequency", &rate))
		return;

	/* set up gate and fixed rate properties */
	gate->reg = of_iomap(node, 0);
	gate->bit_idx = SUNXI_OSC24M_GATE;
	gate->lock = &clk_lock;
	fixed->fixed_rate = rate;

	clk = clk_register_composite(NULL, clk_name,
			NULL, 0,
			NULL, NULL,
			&fixed->hw, &clk_fixed_rate_ops,
			&gate->hw, &clk_gate_ops,
			CLK_IS_ROOT);

	if (!IS_ERR(clk)) {
		of_clk_add_provider(node, of_clk_src_simple_get, clk);
		clk_register_clkdev(clk, clk_name, NULL);
	}
}
CLK_OF_DECLARE(sun4i_osc, "allwinner,sun4i-osc-clk", sun4i_osc_clk_setup);



/**
 * sun4i_get_pll1_factors() - calculates n, k, m, p factors for PLL1
 * PLL1 rate is calculated as follows
 * rate = (parent_rate * n * (k + 1) >> p) / (m + 1);
 * parent_rate is always 24Mhz
 */

static void sun4i_get_pll1_factors(u32 *freq, u32 parent_rate,
				   u8 *n, u8 *k, u8 *m, u8 *p)
{
	u8 div;

	/* Normalize value to a 6M multiple */
	div = *freq / 6000000;
	*freq = 6000000 * div;

	/* we were called to round the frequency, we can now return */
	if (n == NULL)
		return;

	/* m is always zero for pll1 */
	*m = 0;

	/* k is 1 only on these cases */
	if (*freq >= 768000000 || *freq == 42000000 || *freq == 54000000)
		*k = 1;
	else
		*k = 0;

	/* p will be 3 for divs under 10 */
	if (div < 10)
		*p = 3;

	/* p will be 2 for divs between 10 - 20 and odd divs under 32 */
	else if (div < 20 || (div < 32 && (div & 1)))
		*p = 2;

	/* p will be 1 for even divs under 32, divs under 40 and odd pairs
	 * of divs between 40-62 */
	else if (div < 40 || (div < 64 && (div & 2)))
		*p = 1;

	/* any other entries have p = 0 */
	else
		*p = 0;

	/* calculate a suitable n based on k and p */
	div <<= *p;
	div /= (*k + 1);
	*n = div / 4;
}



/**
 * sun4i_get_pll5_factors() - calculates n, k factors for PLL5
 * PLL5 rate is calculated as follows
 * rate = parent_rate * n * (k + 1)
 * parent_rate is always 24Mhz
 */

static void sun4i_get_pll5_factors(u32 *freq, u32 parent_rate,
				   u8 *n, u8 *k, u8 *m, u8 *p)
{
	u8 div;

	/* Normalize value to a parent_rate multiple (24M) */
	div = *freq / parent_rate;
	*freq = parent_rate * div;

	/* we were called to round the frequency, we can now return */
	if (n == NULL)
		return;

	if (div < 31)
		*k = 0;
	else if (div / 2 < 31)
		*k = 1;
	else if (div / 3 < 31)
		*k = 2;
	else
		*k = 3;

	*n = DIV_ROUND_UP(div, (*k+1));
}



/**
 * sun4i_get_apb1_factors() - calculates m, p factors for APB1
 * APB1 rate is calculated as follows
 * rate = (parent_rate >> p) / (m + 1);
 */

static void sun4i_get_apb1_factors(u32 *freq, u32 parent_rate,
				   u8 *n, u8 *k, u8 *m, u8 *p)
{
	u8 calcm, calcp;

	if (parent_rate < *freq)
		*freq = parent_rate;

	parent_rate = (parent_rate + (*freq - 1)) / *freq;

	/* Invalid rate! */
	if (parent_rate > 32)
		return;

	if (parent_rate <= 4)
		calcp = 0;
	else if (parent_rate <= 8)
		calcp = 1;
	else if (parent_rate <= 16)
		calcp = 2;
	else
		calcp = 3;

	calcm = (parent_rate >> calcp) - 1;

	*freq = (parent_rate >> calcp) / (calcm + 1);

	/* we were called to round the frequency, we can now return */
	if (n == NULL)
		return;

	*m = calcm;
	*p = calcp;
}



/**
 * clk_sunxi_mmc_phase_control() - configures MMC clock phase control
 */

void clk_sunxi_mmc_phase_control(struct clk *clk, u8 sample, u8 output)
{
	#define to_clk_composite(_hw) container_of(_hw, struct clk_composite, hw)
	#define to_clk_factors(_hw) container_of(_hw, struct clk_factors, hw)

	struct clk_hw *hw = __clk_get_hw(clk);
	struct clk_composite *composite = to_clk_composite(hw);
	struct clk_hw *rate_hw = composite->rate_hw;
	struct clk_factors *factors = to_clk_factors(rate_hw);
	unsigned long flags = 0;
	u32 reg;

	if (factors->lock)
		spin_lock_irqsave(factors->lock, flags);

	reg = readl(factors->reg);

	/* set sample clock phase control */
	reg &= ~(0x7 << 20);
	reg |= ((sample & 0x7) << 20);

	/* set output clock phase control */
	reg &= ~(0x7 << 8);
	reg |= ((output & 0x7) << 8);

	writel(reg, factors->reg);

	if (factors->lock)
		spin_unlock_irqrestore(factors->lock, flags);
}
EXPORT_SYMBOL(clk_sunxi_mmc_phase_control);


/**
 * sun4i_get_mod0_factors() - calculates m, n factors for MOD0-style clocks
 * MMC rate is calculated as follows
 * rate = (parent_rate >> p) / (m + 1);
 */

static void sun4i_get_mod0_factors(u32 *freq, u32 parent_rate,
				   u8 *n, u8 *k, u8 *m, u8 *p)
{
	u8 div, calcm, calcp;

	/* These clocks can only divide, so we will never be able to achieve
	 * frequencies higher than the parent frequency */
	if (*freq > parent_rate)
		*freq = parent_rate;

	div = parent_rate / *freq;

	if (div < 16)
		calcp = 0;
	else if (div / 2 < 16)
		calcp = 1;
	else if (div / 4 < 16)
		calcp = 2;
	else
		calcp = 3;

	calcm = DIV_ROUND_UP(div, 1 << calcp);

	*freq = (parent_rate >> calcp) / calcm;

	/* we were called to round the frequency, we can now return */
	if (n == NULL)
		return;

	*m = calcm - 1;
	*p = calcp;
}



/**
 * sunxi_factors_clk_setup() - Setup function for factor clocks
 */

#define SUNXI_FACTORS_MUX_MASK 0x3

struct factors_data {
	int enable;
	int mux;
	struct clk_factors_config *table;
	void (*getter) (u32 *rate, u32 parent_rate, u8 *n, u8 *k, u8 *m, u8 *p);
};

static struct clk_factors_config sun4i_pll1_config = {
	.nshift = 8,
	.nwidth = 5,
	.kshift = 4,
	.kwidth = 2,
	.mshift = 0,
	.mwidth = 2,
	.pshift = 16,
	.pwidth = 2,
};

static struct clk_factors_config sun4i_pll5_config = {
	.nshift = 8,
	.nwidth = 5,
	.kshift = 4,
	.kwidth = 2,
};

static struct clk_factors_config sun4i_apb1_config = {
	.mshift = 0,
	.mwidth = 5,
	.pshift = 16,
	.pwidth = 2,
};

/* user manual says "n" but it's really "p" */
static struct clk_factors_config sun4i_mod0_config = {
	.mshift = 0,
	.mwidth = 4,
	.pshift = 16,
	.pwidth = 2,
};

static const struct factors_data sun4i_pll1_data __initconst = {
	.table = &sun4i_pll1_config,
	.getter = sun4i_get_pll1_factors,
};

static const struct factors_data sun4i_pll5_data __initconst = {
	.enable = 31,
	.table = &sun4i_pll5_config,
	.getter = sun4i_get_pll5_factors,
};

static const struct factors_data sun4i_apb1_data __initconst = {
	.table = &sun4i_apb1_config,
	.getter = sun4i_get_apb1_factors,
};

static const struct factors_data sun4i_mod0_data __initconst = {
	.enable = 31,
	.mux = 24,
	.table = &sun4i_mod0_config,
	.getter = sun4i_get_mod0_factors,
};

static struct clk * __init sunxi_factors_clk_setup(struct device_node *node,
						const struct factors_data *data)
{
	struct clk *clk;
	struct clk_factors *factors;
	struct clk_gate *gate = NULL;
	struct clk_mux *mux = NULL;
	struct clk_hw *gate_hw = NULL;
	struct clk_hw *mux_hw = NULL;
	const char *clk_name = node->name;
	const char *parents[SUNXI_MAX_PARENTS];
	void *reg;
	int i = 0;
	unsigned long flags = 0;

	reg = of_iomap(node, 0);

	/* if we have a mux, we will have >1 parents */
	while (i < SUNXI_MAX_PARENTS &&
	       (parents[i] = of_clk_get_parent_name(node, i)) != NULL)
		i++;

	factors = kzalloc(sizeof(struct clk_factors), GFP_KERNEL);
	if (!factors)
		return NULL;

	/* To prevent it from being turned off in clk_disable_unused()*/
	if(!strcmp("pll5", clk_name))
		flags |= CLK_IGNORE_UNUSED;

	/* To prevent it from being turned off in clk_disable_unused()*/
	if (!strcmp("pll6", clk_name))
		flags |= CLK_IGNORE_UNUSED;

	/* Add a gate if this factor clock can be gated */
	if (data->enable) {
		gate = kzalloc(sizeof(struct clk_gate), GFP_KERNEL);
		if (!gate) {
			kfree(factors);
			return NULL;
		}

		/* set up gate properties */
		gate->reg = reg;
		gate->bit_idx = data->enable;
		gate->lock = &clk_lock;
		gate_hw = &gate->hw;
	}

	/* Add a mux if this factor clock can be muxed */
	if (data->mux) {
		mux = kzalloc(sizeof(struct clk_mux), GFP_KERNEL);
		if (!mux) {
			kfree(factors);
			kfree(gate);
			return NULL;
		}

		/* set up gate properties */
		mux->reg = reg;
		mux->shift = data->mux;
		mux->mask = SUNXI_FACTORS_MUX_MASK;
		mux->lock = &clk_lock;
		mux_hw = &mux->hw;
	}

	/* set up factors properties */
	factors->reg = reg;
	factors->config = data->table;
	factors->get_factors = data->getter;
	factors->lock = &clk_lock;

	clk = clk_register_composite(NULL, clk_name,
			parents, i,
			mux_hw, &clk_mux_ops,
			&factors->hw, &clk_factors_ops,
			gate_hw, &clk_gate_ops, flags);

	if (!IS_ERR(clk)) {
		of_clk_add_provider(node, of_clk_src_simple_get, clk);
		clk_register_clkdev(clk, clk_name, NULL);
	}

	return clk;
}



/**
 * sunxi_mux_clk_setup() - Setup function for muxes
 */

#define SUNXI_MUX_GATE_WIDTH	2

struct mux_data {
	u8 shift;
};

static const struct mux_data sun4i_cpu_mux_data __initconst = {
	.shift = 16,
};

static const struct mux_data sun4i_apb1_mux_data __initconst = {
	.shift = 24,
};

static void __init sunxi_mux_clk_setup(struct device_node *node,
				       struct mux_data *data)
{
	struct clk *clk;
	const char *clk_name = node->name;
	const char *parents[5];
	void *reg;
	int i = 0;

	reg = of_iomap(node, 0);

	while (i < 5 && (parents[i] = of_clk_get_parent_name(node, i)) != NULL)
		i++;

	clk = clk_register_mux(NULL, clk_name, parents, i, 0, reg,
			       data->shift, SUNXI_MUX_GATE_WIDTH,
			       0, &clk_lock);

	if (clk) {
		of_clk_add_provider(node, of_clk_src_simple_get, clk);
		clk_register_clkdev(clk, clk_name, NULL);
	}
}



/**
 * sunxi_divider_clk_setup() - Setup function for simple divider clocks
 */

struct div_data {
	u8	shift;
	u8	pow;
	u8	width;
};

static const struct div_data sun4i_axi_data __initconst = {
	.shift	= 0,
	.pow	= 0,
	.width	= 2,
};

static const struct div_data sun4i_ahb_data __initconst = {
	.shift	= 4,
	.pow	= 1,
	.width	= 2,
};

static const struct div_data sun4i_apb0_data __initconst = {
	.shift	= 8,
	.pow	= 1,
	.width	= 2,
};

static void __init sunxi_divider_clk_setup(struct device_node *node,
					   struct div_data *data)
{
	struct clk *clk;
	const char *clk_name = node->name;
	const char *clk_parent;
	void *reg;

	reg = of_iomap(node, 0);

	clk_parent = of_clk_get_parent_name(node, 0);

	clk = clk_register_divider(NULL, clk_name, clk_parent, 0,
				   reg, data->shift, data->width,
				   data->pow ? CLK_DIVIDER_POWER_OF_TWO : 0,
				   &clk_lock);
	if (clk) {
		of_clk_add_provider(node, of_clk_src_simple_get, clk);
		clk_register_clkdev(clk, clk_name, NULL);
	}
}



/**
 * sunxi_gates_clk_setup() - Setup function for leaf gates on clocks
 */

#define SUNXI_GATES_MAX_SIZE	64

struct gates_data {
	DECLARE_BITMAP(mask, SUNXI_GATES_MAX_SIZE);
};

static const struct gates_data sun4i_axi_gates_data __initconst = {
	.mask = {1},
};

static const struct gates_data sun4i_ahb_gates_data __initconst = {
	.mask = {0x7F77FFF, 0x14FB3F},
};

static const struct gates_data sun5i_a10s_ahb_gates_data __initconst = {
	.mask = {0x147667e7, 0x185915},
};

static const struct gates_data sun5i_a13_ahb_gates_data __initconst = {
	.mask = {0x107067e7, 0x185111},
};

static const struct gates_data sun7i_a20_ahb_gates_data __initconst = {
	.mask = { 0x12f77fff, 0x16ff3f },
};

static const struct gates_data sun4i_apb0_gates_data __initconst = {
	.mask = {0x4EF},
};

static const struct gates_data sun5i_a10s_apb0_gates_data __initconst = {
	.mask = {0x469},
};

static const struct gates_data sun5i_a13_apb0_gates_data __initconst = {
	.mask = {0x61},
};

static const struct gates_data sun7i_a20_apb0_gates_data __initconst = {
	.mask = { 0x4ff },
};

static const struct gates_data sun4i_apb1_gates_data __initconst = {
	.mask = {0xFF00F7},
};

static const struct gates_data sun5i_a10s_apb1_gates_data __initconst = {
	.mask = {0xf0007},
};

static const struct gates_data sun5i_a13_apb1_gates_data __initconst = {
	.mask = {0xa0007},
};

static const struct gates_data sun7i_a20_apb1_gates_data __initconst = {
	.mask = { 0xff80ff },
};

static const struct gates_data sun7i_usb_gate_clk_data __initconst = {
	.mask = { 0x1c0 },
};

static void __init sunxi_gates_clk_setup(struct device_node *node,
					 struct gates_data *data)
{
	struct clk_onecell_data *clk_data;
	const char *clk_parent;
	const char *clk_name;
	void *reg;
	int qty;
	int i = 0;
	int j = 0;
	int ignore;

	reg = of_iomap(node, 0);

	clk_parent = of_clk_get_parent_name(node, 0);

	/* Worst-case size approximation and memory allocation */
	qty = find_last_bit(data->mask, SUNXI_GATES_MAX_SIZE);
	clk_data = kmalloc(sizeof(struct clk_onecell_data), GFP_KERNEL);
	if (!clk_data)
		return;
	clk_data->clks = kzalloc((qty+1) * sizeof(struct clk *), GFP_KERNEL);
	if (!clk_data->clks) {
		kfree(clk_data);
		return;
	}

	for_each_set_bit(i, data->mask, SUNXI_GATES_MAX_SIZE) {
		of_property_read_string_index(node, "clock-output-names",
					      j, &clk_name);

		/* No driver claims this clock, but it should remain gated */
		ignore = !strcmp("ahb_sdram", clk_name) ? CLK_IGNORE_UNUSED : 0;

		clk_data->clks[i] = clk_register_gate(NULL, clk_name,
						      clk_parent, ignore,
						      reg + 4 * (i/32), i % 32,
						      0, &clk_lock);
		WARN_ON(IS_ERR(clk_data->clks[i]));

		j++;
	}

	/* Adjust to the real max */
	clk_data->clk_num = i;

	of_clk_add_provider(node, of_clk_src_onecell_get, clk_data);
}



/**
 * sunxi_divs_clk_setup() helper data
 */

#define SUNXI_DIVS_MAX_QTY	2
#define SUNXI_DIVISOR_WIDTH	2

struct divs_data {
	const struct factors_data *factors; /* data for the factor clock */
	struct {
		u8 fixed; /* is it a fixed divisor? if not... */
		struct clk_div_table *table; /* is it a table based divisor? */
		u8 shift; /* otherwise it's a normal divisor with this shift */
		u8 pow;   /* is it power-of-two based? */
		u8 gate;  /* is it independently gateable? */
	} div[SUNXI_DIVS_MAX_QTY];
};

static struct clk_div_table pll6_sata_tbl[] = {
	{ .val = 0, .div = 6, },
	{ .val = 1, .div = 12, },
	{ .val = 2, .div = 18, },
	{ .val = 3, .div = 24, },
	{ } /* sentinel */
};

static const struct divs_data pll5_divs_data __initconst = {
	.factors = &sun4i_pll5_data,
	.div = {
		{ .shift = 0, .pow = 0, }, /* M, DDR */
		{ .shift = 16, .pow = 1, }, /* P, other */
	}
};

static const struct divs_data pll6_divs_data __initconst = {
	.factors = &sun4i_pll5_data,
	.div = {
		{ .shift = 0, .table = pll6_sata_tbl, .gate = 14 }, /* M, SATA */
		{ .fixed = 2 }, /* P, other */
	}
};

/**
 * sunxi_divs_clk_setup() - Setup function for leaf divisors on clocks
 *
 * These clocks look something like this
 *            ________________________
 *           |         ___divisor 1---|----> to consumer
 * parent >--|  pll___/___divisor 2---|----> to consumer
 *           |        \_______________|____> to consumer
 *           |________________________|
 */

static void __init sunxi_divs_clk_setup(struct device_node *node,
					struct divs_data *data)
{
	struct clk_onecell_data *clk_data;
	const char *parent  = node->name;
	const char *clk_name;
	struct clk **clks, *pclk;
	struct clk_hw *gate_hw, *rate_hw;
	const struct clk_ops *rate_ops;
	struct clk_gate *gate = NULL;
	struct clk_fixed_factor *fix_factor;
	struct clk_divider *divider;
	void *reg;
	int i = 0;
	int flags, clkflags;

	/* Set up factor clock that we will be dividing */
	pclk = sunxi_factors_clk_setup(node, data->factors);

	reg = of_iomap(node, 0);

	clk_data = kmalloc(sizeof(struct clk_onecell_data), GFP_KERNEL);
	if (!clk_data)
		return;

	clks = kzalloc(SUNXI_DIVS_MAX_QTY * sizeof(struct clk *), GFP_KERNEL);
	if (!clks)
		goto free_clkdata;

	clk_data->clks = clks;

	/* It's not a good idea to have automatic reparenting changing
	 * our RAM clock! */
	clkflags = !strcmp("pll5", parent) ? 0 : CLK_SET_RATE_PARENT;

	for (i = 0; i < SUNXI_DIVS_MAX_QTY; i++) {
		if (of_property_read_string_index(node, "clock-output-names",
						  i, &clk_name) != 0)
			break;

		gate_hw = NULL;
		rate_hw = NULL;
		rate_ops = NULL;

		/* If this leaf clock can be gated, create a gate */
		if (data->div[i].gate) {
			gate = kzalloc(sizeof(*gate), GFP_KERNEL);
			if (!gate)
				goto free_clks;

			gate->reg = reg;
			gate->bit_idx = data->div[i].gate;
			gate->lock = &clk_lock;

			gate_hw = &gate->hw;
		}

		/* Leaves can be fixed or configurable divisors */
		if (data->div[i].fixed) {
			fix_factor = kzalloc(sizeof(*fix_factor), GFP_KERNEL);
			if (!fix_factor)
				goto free_gate;

			fix_factor->mult = 1;
			fix_factor->div = data->div[i].fixed;

			rate_hw = &fix_factor->hw;
			rate_ops = &clk_fixed_factor_ops;
		} else {
			divider = kzalloc(sizeof(*divider), GFP_KERNEL);
			if (!divider)
				goto free_gate;

			flags = data->div[i].pow ? CLK_DIVIDER_POWER_OF_TWO : 0;

			divider->reg = reg;
			divider->shift = data->div[i].shift;
			divider->width = SUNXI_DIVISOR_WIDTH;
			divider->flags = flags;
			divider->lock = &clk_lock;
			divider->table = data->div[i].table;

			rate_hw = &divider->hw;
			rate_ops = &clk_divider_ops;
		}

		/* Wrap the (potential) gate and the divisor on a composite
		 * clock to unify them */
		clks[i] = clk_register_composite(NULL, clk_name, &parent, 1,
						 NULL, NULL,
						 rate_hw, rate_ops,
						 gate_hw, &clk_gate_ops,
						 clkflags);

		WARN_ON(IS_ERR(clk_data->clks[i]));
		clk_register_clkdev(clks[i], clk_name, NULL);
	}

	/* The last clock available on the getter is the parent */
	clks[i++] = pclk;

	/* Adjust to the real max */
	clk_data->clk_num = i;

	of_clk_add_provider(node, of_clk_src_onecell_get, clk_data);

	return;

free_gate:
	kfree(gate);
free_clks:
	kfree(clks);
free_clkdata:
	kfree(clk_data);
}



/* Matches for factors clocks */
static const struct of_device_id clk_factors_match[] __initconst = {
	{.compatible = "allwinner,sun4i-pll1-clk", .data = &sun4i_pll1_data,},
	{.compatible = "allwinner,sun4i-apb1-clk", .data = &sun4i_apb1_data,},
	{.compatible = "allwinner,sun4i-mod0-clk", .data = &sun4i_mod0_data,},
	{}
};

/* Matches for divider clocks */
static const struct of_device_id clk_div_match[] __initconst = {
	{.compatible = "allwinner,sun4i-axi-clk", .data = &sun4i_axi_data,},
	{.compatible = "allwinner,sun4i-ahb-clk", .data = &sun4i_ahb_data,},
	{.compatible = "allwinner,sun4i-apb0-clk", .data = &sun4i_apb0_data,},
	{}
};

/* Matches for divided outputs */
static const struct of_device_id clk_divs_match[] __initconst = {
	{.compatible = "allwinner,sun4i-pll5-clk", .data = &pll5_divs_data,},
	{.compatible = "allwinner,sun4i-pll6-clk", .data = &pll6_divs_data,},
	{}
};

/* Matches for mux clocks */
static const struct of_device_id clk_mux_match[] __initconst = {
	{.compatible = "allwinner,sun4i-cpu-clk", .data = &sun4i_cpu_mux_data,},
	{.compatible = "allwinner,sun4i-apb1-mux-clk", .data = &sun4i_apb1_mux_data,},
	{}
};

/* Matches for gate clocks */
static const struct of_device_id clk_gates_match[] __initconst = {
	{.compatible = "allwinner,sun4i-axi-gates-clk", .data = &sun4i_axi_gates_data,},
	{.compatible = "allwinner,sun4i-ahb-gates-clk", .data = &sun4i_ahb_gates_data,},
	{.compatible = "allwinner,sun5i-a10s-ahb-gates-clk", .data = &sun5i_a10s_ahb_gates_data,},
	{.compatible = "allwinner,sun5i-a13-ahb-gates-clk", .data = &sun5i_a13_ahb_gates_data,},
	{.compatible = "allwinner,sun7i-a20-ahb-gates-clk", .data = &sun7i_a20_ahb_gates_data,},
	{.compatible = "allwinner,sun4i-apb0-gates-clk", .data = &sun4i_apb0_gates_data,},
	{.compatible = "allwinner,sun5i-a10s-apb0-gates-clk", .data = &sun5i_a10s_apb0_gates_data,},
	{.compatible = "allwinner,sun5i-a13-apb0-gates-clk", .data = &sun5i_a13_apb0_gates_data,},
	{.compatible = "allwinner,sun7i-a20-apb0-gates-clk", .data = &sun7i_a20_apb0_gates_data,},
	{.compatible = "allwinner,sun4i-apb1-gates-clk", .data = &sun4i_apb1_gates_data,},
	{.compatible = "allwinner,sun5i-a10s-apb1-gates-clk", .data = &sun5i_a10s_apb1_gates_data,},
	{.compatible = "allwinner,sun5i-a13-apb1-gates-clk", .data = &sun5i_a13_apb1_gates_data,},
	{.compatible = "allwinner,sun7i-a20-apb1-gates-clk", .data = &sun7i_a20_apb1_gates_data,},
	{.compatible = "allwinner,sun7i-usb-gate-clk", .data = &sun7i_usb_gate_clk_data,},
	{}
};

static void __init of_sunxi_table_clock_setup(const struct of_device_id *clk_match,
					      void *function)
{
	struct device_node *np;
	const struct div_data *data;
	const struct of_device_id *match;
	void (*setup_function)(struct device_node *, const void *) = function;

	for_each_matching_node(np, clk_match) {
		match = of_match_node(clk_match, np);
		data = match->data;
		setup_function(np, data);
	}
}

void __init sunxi_init_clocks(void)
{
	/* Register all the simple and basic clocks on DT */
	of_clk_init(NULL);

	/* Register factor clocks */
	of_sunxi_table_clock_setup(clk_factors_match, sunxi_factors_clk_setup);

	/* Register divider clocks */
	of_sunxi_table_clock_setup(clk_div_match, sunxi_divider_clk_setup);

	/* Register divided output clocks */
	of_sunxi_table_clock_setup(clk_divs_match, sunxi_divs_clk_setup);

	/* Register mux clocks */
	of_sunxi_table_clock_setup(clk_mux_match, sunxi_mux_clk_setup);

	/* Register gate clocks */
	of_sunxi_table_clock_setup(clk_gates_match, sunxi_gates_clk_setup);
}
