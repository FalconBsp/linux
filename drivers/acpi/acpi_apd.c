/*
 * AMD ACPI support for ACPI2platform device.
 *
 * Copyright (c) 2014, AMD Corporation.
 * Authors: Ken Xue <Ken.Xue@amd.com>
 *	Peng, Carl <Carl.Peng@amd.com>
 *	Wu, Jeff <Jeff.Wu@amd.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/acpi.h>
#include <linux/clk.h>
#include <linux/clkdev.h>
#include <linux/clk-provider.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>

#include "internal.h"

ACPI_MODULE_NAME("acpi_apd");
struct apd_private_data;

struct apd_device_desc {
	bool clk_required;
	bool fix_rate_root_clock;
	const char *clk_name;
	unsigned long	rate;
	size_t prv_size_override;
	void (*setup)(struct apd_private_data *pdata);
};

struct apd_private_data {
	void __iomem *mmio_base;
	resource_size_t mmio_size;
	struct clk *clk;
	const struct apd_device_desc *dev_desc;
};

static struct apd_device_desc amd_i2c_desc = {
	.clk_required = true,
	.fix_rate_root_clock = true,
	.clk_name = "i2c_clk",
	.rate = 136192000, /*(133 * 1024 * 1000)*/
};

static struct apd_device_desc amd_uart_desc = {
        .clk_required = true,
        .fix_rate_root_clock = true,
        .clk_name = "uart_clk",
        .rate = 48000000,
};

static const struct acpi_device_id acpi_apd_device_ids[] = {
	/* Generic apd devices */
	{ "AMD0010", (unsigned long)&amd_i2c_desc },
	{ "AMD0020", (unsigned long)&amd_uart_desc },
	{ "AMD0030", (unsigned long)NULL },
	{ }
};

static int is_memory(struct acpi_resource *res, void *not_used)
{
	struct resource r;

	return !acpi_dev_resource_memory(res, &r);
}

static int register_device_clock(struct acpi_device *adev,
				 struct apd_private_data *pdata)
{
	const struct apd_device_desc *dev_desc = pdata->dev_desc;
	struct clk *clk = ERR_PTR(-ENODEV);

	clk = pdata->clk;
	if (!clk && dev_desc->fix_rate_root_clock) {
		clk = clk_register_fixed_rate(&adev->dev, dev_name(&adev->dev),
				      NULL, CLK_IS_ROOT, dev_desc->rate);
		pdata->clk = clk;
		clk_register_clkdev(clk, NULL, dev_name(&adev->dev));
	}

	return 0;
}

static int acpi_apd_create_device(struct acpi_device *adev,
				   const struct acpi_device_id *id)
{
	struct apd_device_desc *dev_desc;
	struct apd_private_data *pdata;
	struct resource_list_entry *rentry;
	struct list_head resource_list;
	int ret;

	dev_desc = (struct apd_device_desc *)id->driver_data;
	if (!dev_desc)
		return acpi_create_platform_device(adev, id);

	pdata = kzalloc(sizeof(*pdata), GFP_KERNEL);
	if (!pdata)
		return -ENOMEM;

	INIT_LIST_HEAD(&resource_list);
	ret = acpi_dev_get_resources(adev, &resource_list, is_memory, NULL);
	if (ret < 0)
		goto err_out;

	list_for_each_entry(rentry, &resource_list, node)
		if (resource_type(&rentry->res) == IORESOURCE_MEM) {
			if (dev_desc->prv_size_override)
				pdata->mmio_size = dev_desc->prv_size_override;
			else
				pdata->mmio_size = resource_size(&rentry->res);
			pdata->mmio_base = ioremap(rentry->res.start,
						   pdata->mmio_size);
			break;
		}

	acpi_dev_free_resource_list(&resource_list);

	pdata->dev_desc = dev_desc;

	if (dev_desc->clk_required) {
		ret = register_device_clock(adev, pdata);
		if (ret) {
			/* Skip the device, but continue the namespace scan. */
			ret = 0;
			goto err_out;
		}
	}

	/*
	 * This works around a known issue in ACPI tables where apd devices
	 * have _PS0 and _PS3 without _PSC (and no power resources), so
	 * acpi_bus_init_power() will assume that the BIOS has put them into D0.
	 */
	ret = acpi_device_fix_up_power(adev);
	if (ret) {
		/* Skip the device, but continue the namespace scan. */
		ret = 0;
		goto err_out;
	}

	if (dev_desc->setup)
		dev_desc->setup(pdata);

	adev->driver_data = pdata;
	ret = acpi_create_platform_device(adev, id);
	if (ret > 0)
		return ret;

	adev->driver_data = NULL;

 err_out:
	kfree(pdata);
	return ret;
}

static ssize_t apd_device_desc_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	int ret;
	struct acpi_device *adev;
	struct apd_private_data *pdata;

	ret = acpi_bus_get_device(ACPI_HANDLE(dev), &adev);
	if (WARN_ON(ret))
		return ret;

	pdata = acpi_driver_data(adev);
	if (WARN_ON(!pdata || !pdata->dev_desc))
		return -ENODEV;

	if (pdata->dev_desc->clk_required)
		return sprintf(buf, "Required clk: %s %s %ld\n",
		pdata->dev_desc->clk_name,
		pdata->dev_desc->fix_rate_root_clock ?
		"fix rate" : "no fix rate",
		pdata->dev_desc->rate);
	else
		return sprintf(buf, "No need clk\n");
}

static DEVICE_ATTR(device_desc, S_IRUSR, apd_device_desc_show, NULL);

static struct attribute *apd_attrs[] = {
	&dev_attr_device_desc.attr,
	NULL,
};

static struct attribute_group apd_attr_group = {
	.attrs = apd_attrs,
	.name = "apd_ltr",
};

static int acpi_apd_platform_notify(struct notifier_block *nb,
				     unsigned long action, void *data)
{
	struct platform_device *pdev = to_platform_device(data);
	struct apd_private_data *pdata;
	struct acpi_device *adev;
	const struct acpi_device_id *id;
	int ret = 0;

	id = acpi_match_device(acpi_apd_device_ids, &pdev->dev);
	if (!id || !id->driver_data)
		return 0;

	if (acpi_bus_get_device(ACPI_HANDLE(&pdev->dev), &adev))
		return 0;

	pdata = acpi_driver_data(adev);
	if (!pdata || !pdata->mmio_base)
		return 0;

	if (action == BUS_NOTIFY_ADD_DEVICE)
		ret = sysfs_create_group(&pdev->dev.kobj, &apd_attr_group);
	else if (action == BUS_NOTIFY_DEL_DEVICE)
		sysfs_remove_group(&pdev->dev.kobj, &apd_attr_group);

	return ret;
}

static struct notifier_block acpi_apd_nb = {
	.notifier_call = acpi_apd_platform_notify,
};

static struct acpi_scan_handler apd_handler = {
	.ids = acpi_apd_device_ids,
	.attach = acpi_apd_create_device,
};

void __init acpi_apd_init(void)
{
	bus_register_notifier(&platform_bus_type, &acpi_apd_nb);
	acpi_scan_add_handler(&apd_handler);
}
