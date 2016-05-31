/*
 * BMI160 accelerometer and gyroscope driver
 *
 * Copyright 2014 Sony Corporation
 *
 * Based on drivers/iio/accel/st_accel.h
 *
 * STMicroelectronics accelerometers driver
 *
 * Copyright 2012-2013 STMicroelectronics Inc.
 *
 * Denis Ciocca <denis.ciocca@st.com>
 * v. 1.0.0
 * Licensed under the GPL-2.
 */

#ifndef BMI160_ACCEL_GYRO_H
#define BMI160_ACCEL_GYRO_H

#include <linux/types.h>
#include <linux/iio/common/bmi160.h>

#include "../accel/bmi160_accel.h"
#include "../gyro/bmi160_gyro.h"

#define BMI160_ACC_GYR_DEV_NAME	"bmi160"

int bmi160_accel_gyro_common_probe(struct iio_dev *indio_dev);
void bmi160_accel_gyro_common_remove(struct iio_dev *indio_dev);

ssize_t bmi160_accel_gyro_sysfs_scale_avail(struct device *dev,
		struct device_attribute *attr, char *buf);

#if defined(CONFIG_IIO_TRIGGERED_BUFFER)
int bmi160_accel_gyro_allocate_ring(struct iio_dev *indio_dev);
void bmi160_accel_gyro_deallocate_ring(struct iio_dev *indio_dev);
int bmi160_accel_gyro_trig_set_state(struct iio_trigger *trig, bool state);
#define BMI160_ACC_GYR_TRIGGER_SET_STATE (&bmi160_accel_gyro_trig_set_state)
#elif defined(CONFIG_IIO_BUFFER)
int bmi160_accel_gyro_buffer_probe(struct iio_dev *indio_dev);
void bmi160_accel_gyro_buffer_remove(struct iio_dev *indio_dev);
#else
static inline int bmi160_accel_gyro_allocate_ring(struct iio_dev *indio_dev)
{
	return 0;
}
static inline void bmi160_accel_gyro_deallocate_ring(struct iio_dev *indio_dev)
{
}
#define BMI160_ACC_GYR_TRIGGER_SET_STATE NULL
#endif /* CONFIG_IIO_BUFFER */

#endif /* BMI160_ACCEL_GYRO_H */
