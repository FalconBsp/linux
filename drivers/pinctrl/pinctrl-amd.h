/*
 * GPIO driver for AMD
 *
 * Copyright (c) 2014 Ken Xue <Ken.Xue@amd.com>
 *		Jeff Wu <Jeff.Wu@amd.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 */

#ifndef _PINCTRL_AMD_H
#define _PINCTRL_AMD_H

#define TOTAL_NUMBER_OF_PINS	192
#define AMD_GPIO_PINS_PER_BANK  64
#define AMD_GPIO_TOTAL_BANKS    3

#define AMD_GPIO_PINS_BANK0     63
#define AMD_GPIO_PINS_BANK1     64
#define AMD_GPIO_PINS_BANK2     56

#define WAKE_INT_MASTER_REG 0xfc
#define EOI_MASK (1 << 29)

union gpio_pin_reg {
	struct {
	u32 debounce_tmr_out:4;
	u32 debounce_tmr_out_unit:1;
#define DEBOUNCE_TYPE_NO_DEBOUNCE               0x0
#define DEBOUNCE_TYPE_PRESERVE_LOW_GLITCH       0x1
#define DEBOUNCE_TYPE_PRESERVE_HIGH_GLITCH      0x2
#define DEBOUNCE_TYPE_REMOVE_GLITCH             0x3
	u32 debounce_cntrl:2;
	u32 debounce_tmr_large:1;

#define EDGE_TRAGGER	0x0
#define LEVEL_TRIGGER	0x1
	u32 level_trig:1;

#define ACTIVE_HIGH	0x0
#define ACTIVE_LOW	0x1
#define BOTH_EADGE	0x1
	u32 active_level:2;

#define ENABLE_INTERRUPT	0x1
#define DISABLE_INTERRUPT	0x0
	u32 interrupt_enable:1;
#define ENABLE_INTERRUPT_MASK	0x0
#define DISABLE_INTERRUPT_MASK	0x1
	u32 interrupt_mask:1;

	u32 wake_cntrl:3;
	u32 pin_sts:1;

	u32 drv_strength_sel:2;
	u32 pull_up_sel:1;
	u32 pull_up_enable:1;
	u32 pull_down_enable:1;

	u32 output_value:1;
	u32 output_enable:1;
	u32 sw_cntrl_in:1;
	u32 sw_cntrl_en:1;
	u32 reserved0:2;

#define CLR_INTR_STAT	1
	u32 interrupt_sts:1;
	u32 wake_sts:1;
	u32 reserved1:2;
	};
	u32 reg_u32;
};

struct amd_pingroup {
	const char *name;
	const unsigned *pins;
	unsigned npins;
};

struct amd_function {
	const char *name;
	const char * const *groups;
	unsigned ngroups;
};

struct amd_gpio_irq_pin {
	struct list_head	list;
	u32	pin_num;
};

struct amd_gpio {
	spinlock_t              lock;
	struct list_head	irq_list;/* mapped irq pin list */
	void __iomem            *base;
	int                     irq;

	const struct amd_pingroup *groups;
	u32 ngroups;
	struct pinctrl_dev *pctrl;
	struct irq_domain *domain;
	struct gpio_chip        gc;
	struct resource         *res;
	struct platform_device  *pdev;
};

/*  KERNCZ and WATERTON configuration*/
static const struct pinctrl_pin_desc wt_pins[] = {
	PINCTRL_PIN(0, "GPIO_0"),
	PINCTRL_PIN(1, "GPIO_1"),
	PINCTRL_PIN(2, "GPIO_2"),
	PINCTRL_PIN(3, "GPIO_3"),
	PINCTRL_PIN(4, "GPIO_4"),
	PINCTRL_PIN(5, "GPIO_5"),
	PINCTRL_PIN(6, "GPIO_6"),
	PINCTRL_PIN(7, "GPIO_7"),
	PINCTRL_PIN(8, "GPIO_8"),
	PINCTRL_PIN(9, "GPIO_9"),
	PINCTRL_PIN(10, "GPIO_10"),
	PINCTRL_PIN(11, "GPIO_11"),
	PINCTRL_PIN(12, "GPIO_12"),
	PINCTRL_PIN(13, "GPIO_13"),
	PINCTRL_PIN(14, "GPIO_14"),
	PINCTRL_PIN(15, "GPIO_15"),
	PINCTRL_PIN(16, "GPIO_16"),
	PINCTRL_PIN(17, "GPIO_17"),
	PINCTRL_PIN(18, "GPIO_18"),
	PINCTRL_PIN(19, "GPIO_19"),
	PINCTRL_PIN(20, "GPIO_20"),
	PINCTRL_PIN(23, "GPIO_23"),
	PINCTRL_PIN(24, "GPIO_24"),
	PINCTRL_PIN(25, "GPIO_25"),
	PINCTRL_PIN(26, "GPIO_26"),
	PINCTRL_PIN(27, "GPIO_27"),
	PINCTRL_PIN(28, "GPIO_28"),
	PINCTRL_PIN(39, "GPIO_39"),
	PINCTRL_PIN(40, "GPIO_40"),
	PINCTRL_PIN(43, "GPIO_43"),
	PINCTRL_PIN(44, "GPIO_44"),
	PINCTRL_PIN(45, "GPIO_45"),
	PINCTRL_PIN(46, "GPIO_46"),
	PINCTRL_PIN(47, "GPIO_47"),
	PINCTRL_PIN(48, "GPIO_48"),
	PINCTRL_PIN(49, "GPIO_49"),
	PINCTRL_PIN(50, "GPIO_50"),
	PINCTRL_PIN(51, "GPIO_51"),
	PINCTRL_PIN(52, "GPIO_52"),
	PINCTRL_PIN(53, "GPIO_53"),
	PINCTRL_PIN(54, "GPIO_54"),
	PINCTRL_PIN(55, "GPIO_55"),
	PINCTRL_PIN(56, "GPIO_56"),
	PINCTRL_PIN(57, "GPIO_57"),
	PINCTRL_PIN(58, "GPIO_58"),
	PINCTRL_PIN(59, "GPIO_59"),
	PINCTRL_PIN(60, "GPIO_60"),
	PINCTRL_PIN(61, "GPIO_61"),
	PINCTRL_PIN(62, "GPIO_62"),
	PINCTRL_PIN(64, "GPIO_64"),
	PINCTRL_PIN(65, "GPIO_65"),
	PINCTRL_PIN(66, "GPIO_66"),
	PINCTRL_PIN(68, "GPIO_68"),
	PINCTRL_PIN(69, "GPIO_69"),
	PINCTRL_PIN(71, "GPIO_71"),
	PINCTRL_PIN(76, "GPIO_76"),
	PINCTRL_PIN(84, "GPIO_84"),
	PINCTRL_PIN(85, "GPIO_85"),
	PINCTRL_PIN(90, "GPIO_90"),
	PINCTRL_PIN(92, "GPIO_92"),
	PINCTRL_PIN(95, "GPIO_95"),
	PINCTRL_PIN(96, "GPIO_96"),
	PINCTRL_PIN(97, "GPIO_97"),
	PINCTRL_PIN(98, "GPIO_98"),
	PINCTRL_PIN(99, "GPIO_99"),
	PINCTRL_PIN(100, "GPIO_100"),
	PINCTRL_PIN(101, "GPIO_101"),
	PINCTRL_PIN(102, "GPIO_102"),
	PINCTRL_PIN(104, "GPIO_104"),
	PINCTRL_PIN(105, "GPIO_105"),
	PINCTRL_PIN(106, "GPIO_106"),
	PINCTRL_PIN(107, "GPIO_107"),
	PINCTRL_PIN(108, "GPIO_108"),
	PINCTRL_PIN(109, "GPIO_109"),
	PINCTRL_PIN(110, "GPIO_110"),
	PINCTRL_PIN(111, "GPIO_111"),
	PINCTRL_PIN(112, "GPIO_112"),
	PINCTRL_PIN(113, "GPIO_113"),
	PINCTRL_PIN(114, "GPIO_114"),
	PINCTRL_PIN(117, "GPIO_117"),
	PINCTRL_PIN(118, "GPIO_118"),
	PINCTRL_PIN(119, "GPIO_119"),
	PINCTRL_PIN(120, "GPIO_120"),
	PINCTRL_PIN(121, "GPIO_121"),
	PINCTRL_PIN(122, "GPIO_122"),
	PINCTRL_PIN(123, "GPIO_123"),
	PINCTRL_PIN(126, "GPIO_126"),
	PINCTRL_PIN(129, "GPIO_129"),
	PINCTRL_PIN(130, "GPIO_130"),
	PINCTRL_PIN(131, "GPIO_131"),
	PINCTRL_PIN(132, "GPIO_132"),
	PINCTRL_PIN(133, "GPIO_133"),
	PINCTRL_PIN(135, "GPIO_135"),
	PINCTRL_PIN(136, "GPIO_136"),
	PINCTRL_PIN(137, "GPIO_137"),
	PINCTRL_PIN(138, "GPIO_138"),
	PINCTRL_PIN(139, "GPIO_139"),
	PINCTRL_PIN(140, "GPIO_140"),
	PINCTRL_PIN(141, "GPIO_141"),
	PINCTRL_PIN(142, "GPIO_142"),
	PINCTRL_PIN(143, "GPIO_143"),
	PINCTRL_PIN(144, "GPIO_144"),
	PINCTRL_PIN(145, "GPIO_145"),
	PINCTRL_PIN(146, "GPIO_146"),
	PINCTRL_PIN(147, "GPIO_147"),
	PINCTRL_PIN(148, "GPIO_148"),
	PINCTRL_PIN(149, "GPIO_149"),
	PINCTRL_PIN(150, "GPIO_150"),
	PINCTRL_PIN(151, "GPIO_151"),
	PINCTRL_PIN(160, "GPIO_160"),
	PINCTRL_PIN(161, "GPIO_161"),
	PINCTRL_PIN(162, "GPIO_162"),
	PINCTRL_PIN(163, "GPIO_163"),
	PINCTRL_PIN(164, "GPIO_164"),
	PINCTRL_PIN(165, "GPIO_165"),
	PINCTRL_PIN(166, "GPIO_166"),
	PINCTRL_PIN(167, "GPIO_167"),
	PINCTRL_PIN(168, "GPIO_168"),
	PINCTRL_PIN(169, "GPIO_169"),
	PINCTRL_PIN(170, "GPIO_170"),
	PINCTRL_PIN(171, "GPIO_171"),
	PINCTRL_PIN(172, "GPIO_172"),
	PINCTRL_PIN(173, "GPIO_173"),
	PINCTRL_PIN(174, "GPIO_174"),
	PINCTRL_PIN(175, "GPIO_175"),
	PINCTRL_PIN(176, "GPIO_176"),
	PINCTRL_PIN(177, "GPIO_177"),
	PINCTRL_PIN(178, "GPIO_178"),
	PINCTRL_PIN(179, "GPIO_179"),
	PINCTRL_PIN(180, "GPIO_180"),
	PINCTRL_PIN(181, "GPIO_181"),
	PINCTRL_PIN(182, "GPIO_182"),
};


const unsigned i2c0_pins[] = {145, 146};

const unsigned i2c1_pins[] = {147, 148};

const unsigned i2c2_pins[] = {113, 114};

const unsigned i2c3_pins[] = {19, 20};

const unsigned i2c4_pins[] = {149, 150};

const unsigned i2c5_pins[] = {151, 152};

const unsigned emmc_pins[] = {160, 162, 163, 164, 165,
				166, 167, 168, 169, 170, 171};

const unsigned sd0_pins[] = {25, 95, 96, 97, 98, 99, 100, 101, 102};

const unsigned sd1_pins[] = {104, 105, 106, 107, 108, 109, 110, 111, 112};

const unsigned uart0_pins[] = {135, 136, 137, 138, 139, 149, 150, 151, 152};

const unsigned uart1_pins[] = {140, 141, 142, 143, 144};

const unsigned spi_pins[] = {76, 117, 118, 119, 120, 121, 122};

static const struct amd_pingroup amur_groups[] = {
	{
		.name = "i2c0",
		.pins = i2c0_pins,
		.npins = 2,
	},
	{
		.name = "i2c1",
		.pins = i2c1_pins,
		.npins = 2,
	},
	{
		.name = "i2c2",
		.pins = i2c2_pins,
		.npins = 2,
	},
	{
		.name = "i2c3",
		.pins = i2c3_pins,
		.npins = 2,
	},
	{
		.name = "i2c4",
		.pins = i2c4_pins,
		.npins = 2,
	},
	{
		.name = "i2c5",
		.pins = i2c5_pins,
		.npins = 2,
	},
	{
		.name = "emmc",
		.pins = emmc_pins,
		.npins = 11,
	},
	{
		.name = "sd0",
		.pins = sd0_pins,
		.npins = 9,
	},
	{
		.name = "sd1",
		.pins = sd1_pins,
		.npins = 9,
	},
	{
		.name = "uart0",
		.pins = uart0_pins,
		.npins = 9,
	},
	{
		.name = "uart1",
		.pins = uart1_pins,
		.npins = 5,
	},
	{
		.name = "spi",
		.pins = spi_pins,
		.npins = 7,
	},
};

#endif
