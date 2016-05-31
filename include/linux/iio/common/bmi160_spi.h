/*
 * BMI160 spi library driver
 *
 * Copyright 2015 Sony Corporation
 *
 * Based on include/linux/iio/common/st_sensors_spi.h
 *
 * STMicroelectronics sensors spi library driver
 *
 * Copyright 2012-2013 STMicroelectronics Inc.
 *
 * Denis Ciocca <denis.ciocca@st.com>
 *
 * Licensed under the GPL-2.
 */

#ifndef BMI160_SPI_H
#define BMI160_SPI_H

#include <linux/spi/spi.h>
#include <linux/iio/common/bmi160.h>

void bmi160_spi_configure(struct iio_dev *indio_dev,
			struct spi_device *spi, struct bmi160_data *sdata);

#endif /* BMI160_SPI_H */
