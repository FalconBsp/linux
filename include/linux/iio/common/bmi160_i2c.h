/*
 * BMI160 i2c library driver
 *
 * Copyright 2014 Sony Corporation
 *
 * Based on include/linux/iio/common/st_sensors_i2c.h
 *
 * STMicroelectronics sensors i2c library driver
 *
 * Copyright 2012-2013 STMicroelectronics Inc.
 *
 * Denis Ciocca <denis.ciocca@st.com>
 *
 * Licensed under the GPL-2.
 */

#ifndef BMI160_I2C_H
#define BMI160_I2C_H

#include <linux/i2c.h>
#include <linux/iio/common/bmi160.h>

void bmi160_i2c_configure(struct iio_dev *indio_dev,
		struct i2c_client *client, struct bmi160_data *sdata);

#endif /* BMI160_I2C_H */
