/*
 * BMI160 accelerometer and gyroscope driver
 *
 * Copyright 2015 Sony Corporation
 *
 * Based on drivers/iio/accel/st_accel_spi.c
 *
 * STMicroelectronics accelerometers driver
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
#include <linux/spi/spi.h>
#include <linux/iio/iio.h>
#include <linux/gpio.h>
#include <linux/of_gpio.h>
#include <linux/iio/common/bmi160.h>
#include <linux/iio/common/bmi160_spi.h>
#include "bmi160_accel_gyro.h"

static int bmi160_accel_gyro_spi_probe(struct spi_device *spi)
{
	u8 dummy;
	struct iio_dev *indio_dev;
	struct bmi160_data *agdata;
#ifdef CONFIG_IIO_BMI160_USE_GPIO_IRQ
	enum of_gpio_flags gpio_flags;
#endif
	int err;

	indio_dev = iio_device_alloc(sizeof(*agdata));
	if (indio_dev == NULL) {
		err = -ENOMEM;
		goto iio_device_alloc_error;
	}

	agdata = iio_priv(indio_dev);
	agdata->dev = &spi->dev;

#ifdef CONFIG_IIO_BMI160_USE_GPIO_IRQ
	agdata->gpio = of_get_gpio_flags(spi->dev.of_node, 0, &gpio_flags);
	if (!gpio_is_valid(agdata->gpio)) {
		err = -ENODEV;
		goto gpio_request_error;
	}

	agdata->gpio_active_low = gpio_flags & OF_GPIO_ACTIVE_LOW;

	err = gpio_request_one(agdata->gpio,
			       GPIOF_IN, "BMI160 irq");
	if (err)
		goto gpio_request_error;
#endif

	mutex_init(&agdata->tb.buf_lock);

	bmi160_spi_configure(indio_dev, spi, agdata);

	/* recommended single read. refer to p.84 in specsheet. */
	// TODO: confirm this is needed really?
	err = agdata->tf->read_byte(&agdata->tb, agdata->dev, 0x7f, &dummy);
	if (err < 0)
		goto bmi160_accel_gyro_common_probe_error;

	err = bmi160_accel_gyro_common_probe(indio_dev);
	if (err < 0)
		goto bmi160_accel_gyro_common_probe_error;

	return 0;

bmi160_accel_gyro_common_probe_error:
#ifdef CONFIG_IIO_BMI160_USE_GPIO_IRQ
	gpio_free(agdata->gpio);
gpio_request_error:
#endif
	iio_device_free(indio_dev);
iio_device_alloc_error:
	return err;
}

static int bmi160_accel_gyro_spi_remove(struct spi_device *spi)
{
	struct iio_dev *indio_dev = spi_get_drvdata(spi);
#ifdef CONFIG_IIO_BMI160_USE_GPIO_IRQ
	struct bmi160_data *agdata = iio_priv(indio_dev);

	gpio_free(agdata->gpio);
#endif
	bmi160_accel_gyro_common_remove(indio_dev);

	return 0;
}

static const struct spi_device_id bmi160_accel_gyro_id_table[] = {
	{ BMI160_ACC_GYR_DEV_NAME },
	{},
};
MODULE_DEVICE_TABLE(spi, bmi160_accel_gyro_id_table);

static struct spi_driver bmi160_accel_gyro_driver = {
	.driver = {
		.owner = THIS_MODULE,
		.name = "bmi160-accel-gyro-spi",
	},
	.probe = bmi160_accel_gyro_spi_probe,
	.remove = bmi160_accel_gyro_spi_remove,
	.id_table = bmi160_accel_gyro_id_table,
};
module_spi_driver(bmi160_accel_gyro_driver);

MODULE_AUTHOR("Sony Corporation");
MODULE_DESCRIPTION("BMI160 accelerometer and gyroscope spi driver");
MODULE_LICENSE("GPL v2");
