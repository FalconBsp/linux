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

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/mutex.h>
#include <linux/interrupt.h>
#include <linux/i2c.h>
#include <linux/gpio.h>
#include <linux/irq.h>
#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>
#include <linux/iio/trigger.h>
#include <linux/iio/buffer.h>

#include <linux/iio/common/bmi160.h>
#include "bmi160_accel.h"

#define BMI160_NUMBER_ALL_CHANNELS	4
#define BMI160_NUMBER_DATA_CHANNELS	3

#define BMI160_ACC_WAI_EXP		BMI160_REG_CHIPID_DEFAULT

static const struct iio_chan_spec bmi160_accel_channels[] = {
	BMI160_LSM_CHANNELS(IIO_ACCEL, BMI160_SCAN_X, IIO_MOD_X, IIO_LE,
		BMI160_REALBITS, BMI160_ACC_OUT_X_L_ADDR),
	BMI160_LSM_CHANNELS(IIO_ACCEL, BMI160_SCAN_Y, IIO_MOD_Y, IIO_LE,
		BMI160_REALBITS, BMI160_ACC_OUT_Y_L_ADDR),
	BMI160_LSM_CHANNELS(IIO_ACCEL, BMI160_SCAN_Z, IIO_MOD_Z, IIO_LE,
		BMI160_REALBITS, BMI160_ACC_OUT_Z_L_ADDR),
	IIO_CHAN_SOFT_TIMESTAMP(3)
};

extern struct bmi160_sensor_spec bmi160_accel_spec;

static const struct bmi160 bmi160_accel_sensors[] = {
	{
		.wai = BMI160_ACC_WAI_EXP,
		.sensors_supported = {
			[0] = BMI160_ACC_DEV_NAME,
		},
		.ch = (struct iio_chan_spec *)bmi160_accel_channels,
		.sensors = {
			[BMI160_ACC] = {
				.spec = &bmi160_accel_spec,
			},
		},
		.idle = {
			.normal_us = BMI160_IDLE_REG_UPDATE_NORMAL_US,
			.others_us = BMI160_IDLE_REG_UPDATE_OTHERS_US,
		},
	},
};

static int bmi160_accel_read_raw(struct iio_dev *indio_dev,
			struct iio_chan_spec const *ch, int *val,
							int *val2, long mask)
{
	int err;
	struct bmi160_data *adata = iio_priv(indio_dev);
	struct bmi160_sensor *asensor = &adata->sensor->sensors[BMI160_ACC];

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		err = bmi160_read_info_raw(indio_dev, asensor, ch, val);
		if (err < 0)
			goto read_error;

		return IIO_VAL_INT;
	case IIO_CHAN_INFO_SCALE:
		*val = 0;
		*val2 = asensor->current_fullscale->gain;
		return IIO_VAL_INT_PLUS_MICRO;
	default:
		return -EINVAL;
	}

read_error:
	return err;
}

static int bmi160_accel_write_raw(struct iio_dev *indio_dev,
		struct iio_chan_spec const *chan, int val, int val2, long mask)
{
	int err;
	struct bmi160_data *adata = iio_priv(indio_dev);
	struct bmi160_sensor *asensor = &adata->sensor->sensors[BMI160_ACC];

	switch (mask) {
	case IIO_CHAN_INFO_SCALE:
		err = bmi160_set_fullscale_by_gain(indio_dev, asensor, val2);
		break;
	default:
		return -EINVAL;
	}

	return err;
}

static ssize_t bmi160_accel_sysfs_get_sampling_frequency(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	return bmi160_sysfs_get_sampling_frequency(dev, attr, buf);
}

static ssize_t bmi160_accel_sysfs_set_sampling_frequency(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct bmi160_data *adata = iio_priv(indio_dev);

	return bmi160_sysfs_set_sampling_frequency(dev, attr, buf, count,
			&adata->sensor->sensors[BMI160_ACC], 1);
}

static ssize_t bmi160_accel_sysfs_sampling_frequency_avail(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct bmi160_data *adata = iio_priv(indio_dev);

	return bmi160_sysfs_sampling_frequency_avail(dev, attr, buf,
			&adata->sensor->sensors[BMI160_ACC], 1);
}

#ifdef CONFIG_IIO_BMI160_DIRECT_BURST_READ
static ssize_t bmi160_sysfs_accel_x_y_z_raw(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	int err;
	int i;
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct bmi160_data *adata = iio_priv(indio_dev);
	struct bmi160_sensor *sensors[] = {
		&adata->sensor->sensors[BMI160_ACC],
	};
	struct iio_chan_spec const *chans_addr[] = {
		/* address ordered */
		[0] = &bmi160_accel_channels[BMI160_SCAN_X],
		[1] = &bmi160_accel_channels[BMI160_SCAN_Y],
		[2] = &bmi160_accel_channels[BMI160_SCAN_Z],
	};
	struct iio_chan_spec const *chans_show[ARRAY_SIZE(chans_addr)];
	int vals[ARRAY_SIZE(chans_addr)];

	for (i = 0; i < ARRAY_SIZE(chans_addr); i++)
		chans_show[i] = &bmi160_accel_channels[i];

	err = bmi160_read_info_raw_burst(indio_dev,
			sensors, ARRAY_SIZE(sensors),
			chans_addr, ARRAY_SIZE(chans_addr), vals);
	if (err < 0)
		goto bmi160_read_info_raw_burst_error;

	return bmi160_set_burst_read_chans_to_buf(indio_dev,
			chans_show, chans_addr, vals,
			ARRAY_SIZE(chans_addr), buf, PAGE_SIZE);

bmi160_read_info_raw_burst_error:
	return err;
}

static IIO_DEVICE_ATTR(in_accel_x_y_z_raw, S_IRUGO,
		bmi160_sysfs_accel_x_y_z_raw, NULL, 0);
#endif

static IIO_DEV_ATTR_SAMP_FREQ(S_IWUSR | S_IRUGO,
		bmi160_accel_sysfs_get_sampling_frequency,
		bmi160_accel_sysfs_set_sampling_frequency);

static IIO_DEV_ATTR_SAMP_FREQ_AVAIL(
		bmi160_accel_sysfs_sampling_frequency_avail);

static IIO_DEVICE_ATTR(in_accel_scale_available, S_IRUGO,
		bmi160_accel_sysfs_scale_avail, NULL, 0);

static struct attribute *bmi160_accel_attributes[] = {
	&iio_dev_attr_sampling_frequency_available.dev_attr.attr,
	&iio_dev_attr_in_accel_scale_available.dev_attr.attr,
	&iio_dev_attr_sampling_frequency.dev_attr.attr,
#ifdef CONFIG_IIO_BMI160_DIRECT_BURST_READ
	&iio_dev_attr_in_accel_x_y_z_raw.dev_attr.attr,
#endif
	NULL,
};

static const struct attribute_group bmi160_accel_attribute_group = {
	.attrs = bmi160_accel_attributes,
};

static const struct iio_info accel_info = {
	.driver_module = THIS_MODULE,
	.attrs = &bmi160_accel_attribute_group,
	.read_raw = &bmi160_accel_read_raw,
	.write_raw = &bmi160_accel_write_raw,
};

#ifdef CONFIG_IIO_TRIGGER
static const struct iio_trigger_ops bmi160_accel_trigger_ops = {
	.owner = THIS_MODULE,
	.set_trigger_state = BMI160_ACC_TRIGGER_SET_STATE,
};
#define BMI160_ACC_TRIGGER_OPS (&bmi160_accel_trigger_ops)
#else
#define BMI160_ACC_TRIGGER_OPS NULL
#endif

int bmi160_accel_common_probe(struct iio_dev *indio_dev)
{
	int err;
	int index;
	u8 value;
	struct bmi160_data *adata = iio_priv(indio_dev);
	struct bmi160_sensor *asensor;
	struct bmi160_sensor_spec *aspec;

	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->info = &accel_info;

	err = bmi160_check_device_support(indio_dev,
				ARRAY_SIZE(bmi160_accel_sensors),
				bmi160_accel_sensors);
	if (err < 0)
		goto bmi160_accel_common_probe_error;

	asensor = &adata->sensor->sensors[BMI160_ACC];
	aspec = asensor->spec;

	indio_dev->channels = adata->sensor->ch;
	indio_dev->num_channels = BMI160_NUMBER_ALL_CHANNELS;

	value = (aspec->fs.def_value & aspec->fs.mask) >>
			__ffs(aspec->fs.mask);
	err = bmi160_match_fs_value(indio_dev, asensor, value, &index);
	if (err < 0)
		goto bmi160_accel_common_probe_error;
	asensor->current_fullscale = &aspec->fs.fs_avl[index];

	adata->sensor->current_hz.integer = BMI160_HZ_INT_TO_INIT;
	adata->sensor->current_hz.fract = BMI160_HZ_FRACT_TO_INIT;

	err = bmi160_init_sensor(indio_dev, asensor,
			&adata->sensor->current_hz);
	if (err < 0)
		goto bmi160_accel_common_probe_error;

	if (adata->get_irq_data_ready(indio_dev) > 0) {
		err = bmi160_accel_allocate_ring(indio_dev);
		if (err < 0)
			goto bmi160_accel_common_probe_error;

		err = bmi160_allocate_trigger(indio_dev,
						 BMI160_ACC_TRIGGER_OPS);
		if (err < 0)
			goto bmi160_accel_probe_trigger_error;
	}

	err = iio_device_register(indio_dev);
	if (err)
		goto bmi160_accel_device_register_error;

	return err;

bmi160_accel_device_register_error:
	if (adata->get_irq_data_ready(indio_dev) > 0)
		bmi160_deallocate_trigger(indio_dev);
bmi160_accel_probe_trigger_error:
	if (adata->get_irq_data_ready(indio_dev) > 0)
		bmi160_accel_deallocate_ring(indio_dev);
bmi160_accel_common_probe_error:
	return err;
}
EXPORT_SYMBOL(bmi160_accel_common_probe);

void bmi160_accel_common_remove(struct iio_dev *indio_dev)
{
	struct bmi160_data *adata = iio_priv(indio_dev);

	iio_device_unregister(indio_dev);
	if (adata->get_irq_data_ready(indio_dev) > 0) {
		bmi160_deallocate_trigger(indio_dev);
		bmi160_accel_deallocate_ring(indio_dev);
	}
	iio_device_free(indio_dev);
}
EXPORT_SYMBOL(bmi160_accel_common_remove);

MODULE_AUTHOR("Sony Corporation");
MODULE_DESCRIPTION("BMI160 accelerometer driver");
MODULE_LICENSE("GPL v2");
