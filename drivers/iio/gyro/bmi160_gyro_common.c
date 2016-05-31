/*
 * BMI160 gyroscope driver
 *
 * Copyright 2014 Sony Corporation
 *
 * Based on drivers/iio/gyro/st_gyro_core.c
 *
 * STMicroelectronics gyroscopes driver
 *
 * Copyright 2012-2013 STMicroelectronics Inc.
 *
 * Denis Ciocca <denis.ciocca@st.com>
 *
 * Licensed under the GPL-2.
 */

#include <linux/iio/iio.h>

#include <linux/iio/common/bmi160.h>
#include "bmi160_gyro.h"

/* FULLSCALE */
#define BMI160_GYR_FS_AVL_125DPS	125
#define BMI160_GYR_FS_AVL_250DPS	250
#define BMI160_GYR_FS_AVL_500DPS	500
#define BMI160_GYR_FS_AVL_1000DPS	1000
#define BMI160_GYR_FS_AVL_2000DPS	2000

/* CUSTOM VALUES FOR SENSOR */
#define BMI160_GYR_FS_AVL_125_GAIN	67   /* 125 * 3.14159 / 180 /
						32768 * 1000000 */
#define BMI160_GYR_FS_AVL_250_GAIN	133  /* 250 * 3.14159 / 180 /
						32768 * 1000000 */
#define BMI160_GYR_FS_AVL_500_GAIN	266  /* 500 * 3.14159 / 180 /
						32768 * 1000000 */
#define BMI160_GYR_FS_AVL_1000_GAIN	533  /* 1000 * 3.14159 / 180 /
						32768 * 1000000 */
#define BMI160_GYR_FS_AVL_2000_GAIN	1065 /* 2000 * 3.14159 / 180 /
						32768 * 1000000 */

#define ODR_AVL1(VAL)							\
{									\
	.hz.integer = (VAL),						\
	.hz.fract = 0,							\
	.value = BMI160_REG_GYR_CONF_ODR_ ## VAL ## HZ,			\
}

#define ODR_AVL2(NUMER, DENOM)						\
{									\
	.hz.integer = (NUMER) / (DENOM),				\
	.hz.fract = (((NUMER) % (DENOM)) * BMI160_HZ_INT_TO_FRACT)	\
			/ (DENOM),					\
	.value = BMI160_REG_GYR_CONF_ODR_ ## NUMER ## _ ## DENOM ## HZ,	\
}

#define FS_AVL(VAL)							\
{									\
	.num = BMI160_GYR_FS_AVL_ ## VAL ## DPS,			\
	.value = BMI160_REG_GYR_RANGE_ ## VAL ## DPS,			\
	.gain = BMI160_GYR_FS_AVL_ ## VAL ## _GAIN,			\
}

struct bmi160_sensor_spec bmi160_gyro_spec = {
	.odr = {
		.addr = BMI160_REG_GYR_CONF,
		.mask = BMI160_REG_GYR_CONF_ODR_MASK,
		.def_value = BMI160_REG_GYR_CONF_DEFAULT,
		.odr_avl = {
#if 0
/* To support these configurations, an implementation for undersampling related
 * handling is required. */
			ODR_AVL2(25, 32),
			ODR_AVL2(25, 16),
			ODR_AVL2(25, 8),
			ODR_AVL2(25, 4),
			ODR_AVL2(25, 2),
#endif
			ODR_AVL1(25),
			ODR_AVL1(50),
			ODR_AVL1(100),
			ODR_AVL1(200),
			ODR_AVL1(400),
			ODR_AVL1(800),
			ODR_AVL1(1600),
			ODR_AVL1(3200),
		},
	},
	.pw_cntl = {
		.addr = BMI160_REG_CMD,
		.value_on = BMI160_REG_CMD_SET_PMU_GYR_NORMAL,
		.value_off = BMI160_REG_CMD_SET_PMU_GYR_SUSPEND,
		.timeout_on_ms = BMI160_GYR_STARTUP_MAX_MS,
		/* there's no definition */
		.timeout_off_ms = BMI160_GYR_STARTUP_MAX_MS,
	},
	.pw_stat = {
		.addr = BMI160_REG_PMU_STATUS,
		.mask = BMI160_REG_PMU_STATUS_GYR_MASK,
		.value_on = BMI160_PMU_MODE_GYR_NORMAL,
		.value_off = BMI160_PMU_MODE_GYR_SUSPEND,
	},
	.fs = {
		.addr = BMI160_REG_GYR_RANGE,
		.mask = BMI160_REG_GYR_RANGE_MASK,
		.def_value = BMI160_REG_GYR_RANGE_DEFAULT,
		.fs_avl = {
			FS_AVL(2000),
			FS_AVL(1000),
			FS_AVL(500),
			FS_AVL(250),
			FS_AVL(125),
		},
	},
	.drdy = {
		.addr = BMI160_REG_STATUS,
		.mask = BMI160_REG_STATUS_DRDY_GYR,
	},
};

ssize_t bmi160_gyro_sysfs_scale_avail(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct bmi160_data *gdata = iio_priv(indio_dev);

	return bmi160_sysfs_scale_avail(dev, attr, buf,
			&gdata->sensor->sensors[BMI160_GYR]);
}
