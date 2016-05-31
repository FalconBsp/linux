/*
 * BMI160 i2c library driver
 *
 * Copyright 2014 Sony Corporation
 *
 * Based on drivers/iio/common/st_sensors/st_sensors_i2c.c
 *
 * STMicroelectronics sensors i2c library driver
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

#include <linux/iio/common/bmi160_i2c.h>

#ifdef CONFIG_IIO_BMI160_USE_GPIO_IRQ
/*
 * TODO:
 *  - consider to use of_get_gpio()
 *  - move to another file
 */
static unsigned short gpio_int_num = 155;
module_param(gpio_int_num, ushort, 0444);
#endif

static unsigned int bmi160_i2c_get_irq(struct iio_dev *indio_dev)
{
#ifdef CONFIG_IIO_BMI160_USE_GPIO_IRQ
	return gpio_to_irq(gpio_int_num);
#else
	struct bmi160_data *sdata = iio_priv(indio_dev);

	return to_i2c_client(sdata->dev)->irq;
#endif
}

static int bmi160_i2c_read_byte(struct bmi160_transfer_buffer *tb,
				struct device *dev, u8 reg_addr, u8 *res_byte)
{
	int err;

	err = i2c_smbus_read_byte_data(to_i2c_client(dev), reg_addr);
	if (err < 0)
		goto bmi160_accel_i2c_read_byte_error;

	*res_byte = err & 0xff;

bmi160_accel_i2c_read_byte_error:
	return err < 0 ? err : 0;
}

static int bmi160_i2c_read_multiple_byte(
		struct bmi160_transfer_buffer *tb, struct device *dev,
			u8 reg_addr, int len, u8 *data)
{
	return i2c_smbus_read_i2c_block_data(to_i2c_client(dev),
							reg_addr, len, data);
}

static int bmi160_i2c_write_byte(struct bmi160_transfer_buffer *tb,
				struct device *dev, u8 reg_addr, u8 data)
{
	return i2c_smbus_write_byte_data(to_i2c_client(dev), reg_addr, data);
}

static const struct bmi160_transfer_function bmi160_tf_i2c = {
	.read_byte = bmi160_i2c_read_byte,
	.write_byte = bmi160_i2c_write_byte,
	.read_multiple_byte = bmi160_i2c_read_multiple_byte,
};

void bmi160_i2c_configure(struct iio_dev *indio_dev,
		struct i2c_client *client, struct bmi160_data *sdata)
{
	i2c_set_clientdata(client, indio_dev);

	indio_dev->dev.parent = &client->dev;
	indio_dev->name = client->name;

	sdata->tf = &bmi160_tf_i2c;
	sdata->get_irq = bmi160_i2c_get_irq;
	sdata->tb.max_read_len = I2C_SMBUS_BLOCK_MAX;
}
EXPORT_SYMBOL(bmi160_i2c_configure);

MODULE_AUTHOR("Sony Corporation");
MODULE_DESCRIPTION("BMI160 i2c driver");
MODULE_LICENSE("GPL v2");
