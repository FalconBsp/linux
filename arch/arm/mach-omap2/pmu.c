/*
 * OMAP2 ARM Performance Monitoring Unit (PMU) Support
 *
 * Copyright (C) 2012 Texas Instruments, Inc.
 *
 * Contacts:
 * Jon Hunter <jon-hunter@ti.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#include <linux/of.h>
#include <linux/delay.h>

#include <asm/pmu.h>
#include <asm/cti.h>

#include "soc.h"
#include "omap_hwmod.h"
#include "omap_device.h"

static char *omap2_pmu_oh_names[] = {"mpu"};
static char *omap3_pmu_oh_names[] = {"mpu", "debugss"};
static char *omap4430_pmu_oh_names[] = {"l3_main_3", "l3_instr", "debugss"};
static struct platform_device *omap_pmu_dev;
static struct arm_pmu_platdata omap_pmu_data;
static struct cti omap4_cti[2];

/**
 * omap4_pmu_runtime_resume - PMU runtime resume callback
 * @dev		OMAP PMU device
 *
 * Platform specific PMU runtime resume callback for OMAP4430 devices to
 * configure the cross trigger interface for routing PMU interrupts. This
 * is called by the PM runtime framework.
 */
static int omap4_pmu_runtime_resume(struct device *dev)
{
	/*
	 * Wait for howmod to get ready. Since this code path
	 * is called while initializing events, It does not affect
	 * perf readings.
	 */
	udelay(20);
	/* configure CTI0 for PMU IRQ routing */
	cti_unlock(&omap4_cti[0]);
	cti_map_trigger(&omap4_cti[0], 1, 6, 2);
	cti_enable(&omap4_cti[0]);

	/* configure CTI1 for PMU IRQ routing */
	cti_unlock(&omap4_cti[1]);
	cti_map_trigger(&omap4_cti[1], 1, 6, 3);
	cti_enable(&omap4_cti[1]);

	return 0;
}

/**
 * omap4_pmu_runtime_suspend - PMU runtime suspend callback
 * @dev		OMAP PMU device
 *
 * Platform specific PMU runtime suspend callback for OMAP4430 devices to
 * disable the cross trigger interface interrupts. This is called by the
 * PM runtime framework.
 */
static int omap4_pmu_runtime_suspend(struct device *dev)
{
	cti_disable(&omap4_cti[0]);
	cti_disable(&omap4_cti[1]);

	return 0;
}

/**
 * omap4_pmu_handle_irq - PMU IRQ Handler
 * @irq		OMAP CTI IRQ number
 * @dev		OMAP PMU device
 * @handler	ARM PMU interrupt handler
 *
 * Platform specific PMU IRQ handler for OMAP4430 devices that route PMU
 * interrupts via cross trigger interface. This is called by the PMU driver.
 */
static irqreturn_t
omap4_pmu_handle_irq(int irq, void *dev, irq_handler_t handler)
{
	if (irq == OMAP44XX_IRQ_CTI0)
		cti_irq_ack(&omap4_cti[0]);
	else if (irq == OMAP44XX_IRQ_CTI1)
		cti_irq_ack(&omap4_cti[1]);

	return handler(irq, dev);
}

/**
 * omap4_init_cti - initialise cross trigger interface instances
 *
 * Initialises two cross trigger interface (CTI) instances in preparation
 * for routing PMU interrupts to the OMAP interrupt controller. Note that
 * this does not configure the actual CTI hardware but just the CTI
 * software structures to be used.
 */
static int __init omap4_init_cti(void)
{
	omap4_cti[0].base = ioremap(OMAP44XX_CTI0_BASE, SZ_4K);
	omap4_cti[1].base = ioremap(OMAP44XX_CTI1_BASE, SZ_4K);

	if (!omap4_cti[0].base || !omap4_cti[1].base) {
		pr_err("ioremap for OMAP4 CTI failed\n");
		return -ENOMEM;
	}

	cti_init(&omap4_cti[0], omap4_cti[0].base, OMAP44XX_IRQ_CTI0, 6);
	cti_init(&omap4_cti[1], omap4_cti[1].base, OMAP44XX_IRQ_CTI1, 6);

	return 0;
}

/**
 * omap2_init_pmu - creates and registers PMU platform device
 * @oh_num:	Number of OMAP HWMODs required to create PMU device
 * @oh_names:	Array of OMAP HWMODS names required to create PMU device
 *
 * Uses OMAP HWMOD framework to create and register an ARM PMU device
 * from a list of HWMOD names passed. Currently supports OMAP2, OMAP3
 * and OMAP4 devices.
 */
static int __init omap2_init_pmu(unsigned oh_num, char *oh_names[])
{
	int i;
	struct omap_hwmod *oh[3];
	char *dev_name = "arm-pmu";

	if ((!oh_num) || (oh_num > 3))
		return -EINVAL;

	for (i = 0; i < oh_num; i++) {
		oh[i] = omap_hwmod_lookup(oh_names[i]);
		if (!oh[i]) {
			pr_err("Could not look up %s hwmod\n", oh_names[i]);
			return -ENODEV;
		}
	}

	omap_pmu_dev = omap_device_build_ss(dev_name, -1, oh, oh_num,
				&omap_pmu_data, sizeof(omap_pmu_data));
	WARN(IS_ERR(omap_pmu_dev), "Can't build omap_device for %s.\n",
	     dev_name);

	return PTR_RET(omap_pmu_dev);
}

static int __init omap_init_pmu(void)
{
	int r;
	unsigned oh_num;
	char **oh_names;

	/*
	 * To create an ARM-PMU device the following HWMODs
	 * are required for the various OMAP2+ devices.
	 *
	 * OMAP24xx:	mpu
	 * OMAP3xxx:	mpu, debugss
	 * OMAP4430:	l3_main_3, l3_instr, debugss
	 * OMAP4460/70:	mpu, debugss
	 */
	if (cpu_is_omap443x()) {
		r = omap4_init_cti();
		if (r)
			return r;
		omap_pmu_data.handle_irq = omap4_pmu_handle_irq;
                omap_pmu_data.runtime_resume = omap4_pmu_runtime_resume;
                omap_pmu_data.runtime_suspend = omap4_pmu_runtime_suspend;
		oh_num = ARRAY_SIZE(omap4430_pmu_oh_names);
		oh_names = omap4430_pmu_oh_names;
	} else if (cpu_is_omap34xx() || cpu_is_omap44xx()) {
		oh_num = ARRAY_SIZE(omap3_pmu_oh_names);
		oh_names = omap3_pmu_oh_names;
	} else {
		oh_num = ARRAY_SIZE(omap2_pmu_oh_names);
		oh_names = omap2_pmu_oh_names;
	}

	return omap2_init_pmu(oh_num, oh_names);
}
omap_subsys_initcall(omap_init_pmu);
