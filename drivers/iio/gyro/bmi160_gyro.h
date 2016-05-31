/*
 * BMI160 gyroscope driver
 *
 * Copyright 2014 Sony Corporation
 *
 * Based on drivers/iio/gyro/st_gyro.h
 *
 * STMicroelectronics gyroscopes driver
 *
 * Copyright 2012-2013 STMicroelectronics Inc.
 *
 * Denis Ciocca <denis.ciocca@st.com>
 * v. 1.0.0
 * Licensed under the GPL-2.
 */

#ifndef BMI160_GYRO_H
#define BMI160_GYRO_H

#include <linux/types.h>
#include <linux/iio/common/bmi160.h>

#define BMI160_GYR_DEV_NAME	"bmi160"

/* VALUE FOR SENSORS */
#define BMI160_GYR_OUT_X_L_ADDR		BMI160_REG_DATA_8
#define BMI160_GYR_OUT_Y_L_ADDR		BMI160_REG_DATA_10
#define BMI160_GYR_OUT_Z_L_ADDR		BMI160_REG_DATA_12

int bmi160_gyro_common_probe(struct iio_dev *indio_dev);
void bmi160_gyro_common_remove(struct iio_dev *indio_dev);

ssize_t bmi160_gyro_sysfs_scale_avail(struct device *dev,
		struct device_attribute *attr, char *buf);

#ifdef CONFIG_IIO_BUFFER
int bmi160_gyro_allocate_ring(struct iio_dev *indio_dev);
void bmi160_gyro_deallocate_ring(struct iio_dev *indio_dev);
int bmi160_gyro_trig_set_state(struct iio_trigger *trig, bool state);
#define BMI160_GYR_TRIGGER_SET_STATE (&bmi160_gyro_trig_set_state)
#else /* CONFIG_IIO_BUFFER */
static inline int bmi160_gyro_allocate_ring(struct iio_dev *indio_dev)
{
	return 0;
}
static inline void bmi160_gyro_deallocate_ring(struct iio_dev *indio_dev)
{
}
#define BMI160_GYR_TRIGGER_SET_STATE NULL
#endif /* CONFIG_IIO_BUFFER */

#endif /* BMI160_GYRO_H */
