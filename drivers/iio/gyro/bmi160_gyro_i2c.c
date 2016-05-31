/*
 * BMI160 gyroscope driver
 *
 * Copyright 2014 Sony Corporation
 *
 * Based on drivers/iio/gyro/st_gyro_i2c.c
 *
 * STMicroelectronics gyroscopes driver
 *
 * Copyright 2012-2013 STMicroelectronics Inc.
 *
 * Denis Ciocca <denis.ciocca@st.com>
 *
 * Licensed under the GPL-2.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/iio/iio.h>

#include <linux/iio/common/bmi160.h>
#include <linux/iio/common/bmi160_i2c.h>
#include "bmi160_gyro.h"

static int bmi160_gyro_i2c_probe(struct i2c_client *client,
						const struct i2c_device_id *id)
{
	struct iio_dev *indio_dev;
	struct bmi160_data *gdata;
	int err;

	indio_dev = iio_device_alloc(sizeof(*gdata));
	if (indio_dev == NULL) {
		err = -ENOMEM;
		goto iio_device_alloc_error;
	}

	gdata = iio_priv(indio_dev);
	gdata->dev = &client->dev;

	bmi160_i2c_configure(indio_dev, client, gdata);

	err = bmi160_gyro_common_probe(indio_dev);
	if (err < 0)
		goto bmi160_gyro_common_probe_error;

	return 0;

bmi160_gyro_common_probe_error:
	iio_device_free(indio_dev);
iio_device_alloc_error:
	return err;
}

static int bmi160_gyro_i2c_remove(struct i2c_client *client)
{
	bmi160_gyro_common_remove(i2c_get_clientdata(client));

	return 0;
}

static const struct i2c_device_id bmi160_gyro_id_table[] = {
	{ BMI160_GYR_DEV_NAME },
	{},
};
MODULE_DEVICE_TABLE(i2c, bmi160_gyro_id_table);

static struct i2c_driver bmi160_gyro_driver = {
	.driver = {
		.owner = THIS_MODULE,
		.name = "bmi160-gyro-i2c",
	},
	.probe = bmi160_gyro_i2c_probe,
	.remove = bmi160_gyro_i2c_remove,
	.id_table = bmi160_gyro_id_table,
};
module_i2c_driver(bmi160_gyro_driver);

MODULE_AUTHOR("Sony Corporation");
MODULE_DESCRIPTION("BMI160 gyroscope i2c driver");
MODULE_LICENSE("GPL v2");
