/*
 * BMI160 spi library driver
 *
 * Copyright 2015 Sony Corporation
 *
 * Based on drivers/iio/common/st_sensors/st_sensors_spi.c
 *
 * STMicroelectronics sensors spi library driver
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
#include <linux/gpio.h>
#include <linux/iio/iio.h>

#include <linux/iio/common/bmi160_spi.h>

#define BMI160_SPI_READ		0x80

static unsigned int bmi160_spi_get_irq(struct iio_dev *indio_dev)
{
	struct bmi160_data *sdata = iio_priv(indio_dev);

#ifdef CONFIG_IIO_BMI160_USE_GPIO_IRQ
	return gpio_to_irq(sdata->gpio);
#else
	return to_spi_device(sdata->dev)->irq;
#endif
}

static int bmi160_spi_read(struct bmi160_transfer_buffer *tb,
	struct device *dev, u8 reg_addr, int len, u8 *data)
{
	int err;

	struct spi_transfer xfers[] = {
		{
			.tx_buf = tb->tx_buf,
			.bits_per_word = 8,
			.len = 1,
		},
		{
			.rx_buf = tb->rx_buf,
			.bits_per_word = 8,
			.len = len,
		}
	};

	mutex_lock(&tb->buf_lock);
	tb->tx_buf[0] = reg_addr | BMI160_SPI_READ;

	err = spi_sync_transfer(to_spi_device(dev), xfers, ARRAY_SIZE(xfers));
	if (err)
		goto acc_spi_read_error;

	memcpy(data, tb->rx_buf, len*sizeof(u8));
	mutex_unlock(&tb->buf_lock);
	return len;

acc_spi_read_error:
	mutex_unlock(&tb->buf_lock);
	return err;
}

static int bmi160_spi_read_byte(struct bmi160_transfer_buffer *tb,
				struct device *dev, u8 reg_addr, u8 *res_byte)
{
	return bmi160_spi_read(tb, dev, reg_addr, 1, res_byte);
}

static int bmi160_spi_read_multiple_byte(
	struct bmi160_transfer_buffer *tb, struct device *dev,
			u8 reg_addr, int len, u8 *data)
{
	return bmi160_spi_read(tb, dev, reg_addr, len, data);
}

static int bmi160_spi_write_byte(struct bmi160_transfer_buffer *tb,
				struct device *dev, u8 reg_addr, u8 data)
{
	int err;

	struct spi_transfer xfers = {
		.tx_buf = tb->tx_buf,
		.bits_per_word = 8,
		.len = 2,
	};

	mutex_lock(&tb->buf_lock);
	tb->tx_buf[0] = reg_addr;
	tb->tx_buf[1] = data;

	err = spi_sync_transfer(to_spi_device(dev), &xfers, 1);
	mutex_unlock(&tb->buf_lock);

	return err;
}

static const struct bmi160_transfer_function bmi160_tf_spi = {
	.read_byte = bmi160_spi_read_byte,
	.write_byte = bmi160_spi_write_byte,
	.read_multiple_byte = bmi160_spi_read_multiple_byte,
};

void bmi160_spi_configure(struct iio_dev *indio_dev,
			struct spi_device *spi, struct bmi160_data *sdata)
{
	spi_set_drvdata(spi, indio_dev);

	indio_dev->dev.parent = &spi->dev;
	indio_dev->name = spi->modalias;

	sdata->tf = &bmi160_tf_spi;
	sdata->get_irq = bmi160_spi_get_irq;
	sdata->tb.max_read_len = BMI160_RX_MAX_LENGTH;
}
EXPORT_SYMBOL(bmi160_spi_configure);

MODULE_AUTHOR("Sony Corporation");
MODULE_DESCRIPTION("BMI160 spi driver");
MODULE_LICENSE("GPL v2");
