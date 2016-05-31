/*
 * BMI160 accelerometer driver
 *
 * Copyright 2014 Sony Corporation
 *
 * Based on drivers/iio/accel/st_accel_core.c
 *
 * STMicroelectronics accelerometers driver
 *
 * Copyright 2012-2013 STMicroelectronics Inc.
 *
 * Denis Ciocca <denis.ciocca@st.com>
 *
 * Licensed under the GPL-2.
 */

#include <linux/iio/iio.h>

#include <linux/iio/common/bmi160.h>
#include "bmi160_accel.h"

/* FULLSCALE */
#define BMI160_ACC_FS_AVL_2G		2
#define BMI160_ACC_FS_AVL_4G		4
#define BMI160_ACC_FS_AVL_8G		8
#define BMI160_ACC_FS_AVL_16G		16

/* CUSTOM VALUES FOR SENSOR */
#define BMI160_ACC_FS_AVL_2_GAIN	599  /* 2 * 9.80665 * 1000000 / 32768 */
#define BMI160_ACC_FS_AVL_4_GAIN	1197 /* 4 * 9.80665 * 1000000 / 32768 */
#define BMI160_ACC_FS_AVL_8_GAIN	2394 /* 8 * 9.80665 * 1000000 / 32768 */
#define BMI160_ACC_FS_AVL_16_GAIN	4788 /*16 * 9.80665 * 1000000 / 32768 */

#define ODR_AVL1(VAL)							\
{									\
	.hz.integer = (VAL),						\
	.hz.fract = 0,							\
	.value = BMI160_REG_ACC_CONF_ODR_ ## VAL ## HZ,			\
}

#define ODR_AVL2(NUMER, DENOM)						\
{									\
	.hz.integer = (NUMER) / (DENOM),				\
	.hz.fract = (((NUMER) % (DENOM)) * BMI160_HZ_INT_TO_FRACT)	\
			/ (DENOM),					\
	.value = BMI160_REG_ACC_CONF_ODR_ ## NUMER ## _ ## DENOM ## HZ,	\
}

#define FS_AVL(VAL)							\
{									\
	.num = BMI160_ACC_FS_AVL_ ## VAL ## G,				\
	.value = BMI160_REG_ACC_RANGE_ ## VAL ## G,			\
	.gain = BMI160_ACC_FS_AVL_ ## VAL ## _GAIN,			\
}

struct bmi160_sensor_spec bmi160_accel_spec = {
	.odr = {
		.addr = BMI160_REG_ACC_CONF,
		.mask = BMI160_REG_ACC_CONF_ODR_MASK,
		.def_value = BMI160_REG_ACC_CONF_DEFAULT,
		.odr_avl = {
#if 0
/* To support these configurations, an implementation for undersampling related
 * handling is required. */
			ODR_AVL2(25, 32),
			ODR_AVL2(25, 16),
			ODR_AVL2(25, 8),
			ODR_AVL2(25, 4),
#endif
			ODR_AVL2(25, 2),
			ODR_AVL1(25),
			ODR_AVL1(50),
			ODR_AVL1(100),
			ODR_AVL1(200),
			ODR_AVL1(400),
			ODR_AVL1(800),
			ODR_AVL1(1600),
		},
	},
	.pw_cntl = {
		.addr = BMI160_REG_CMD,
		.value_on = BMI160_REG_CMD_SET_PMU_ACC_NORMAL,
		.value_off = BMI160_REG_CMD_SET_PMU_ACC_SUSPEND,
		.timeout_on_ms = BMI160_ACC_STARTUP_MAX_MS,
		/* there's no definition */
		.timeout_off_ms = BMI160_ACC_STARTUP_MAX_MS,
	},
	.pw_stat = {
		.addr = BMI160_REG_PMU_STATUS,
		.mask = BMI160_REG_PMU_STATUS_ACC_MASK,
		.value_on = BMI160_PMU_MODE_ACC_NORMAL,
		.value_off = BMI160_PMU_MODE_ACC_SUSPEND,
	},
	.fs = {
		.addr = BMI160_REG_ACC_RANGE,
		.mask = BMI160_REG_ACC_RANGE_MASK,
		.def_value = BMI160_REG_ACC_RANGE_DEFAULT,
		.fs_avl = {
			FS_AVL(2),
			FS_AVL(4),
			FS_AVL(8),
			FS_AVL(16),
		},
	},
	.drdy = {
		.addr = BMI160_REG_STATUS,
		.mask = BMI160_REG_STATUS_DRDY_ACC,
	},
};

ssize_t bmi160_accel_sysfs_scale_avail(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct bmi160_data *adata = iio_priv(indio_dev);

	return bmi160_sysfs_scale_avail(dev, attr, buf,
			&adata->sensor->sensors[BMI160_ACC]);
}
