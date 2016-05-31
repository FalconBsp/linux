/*
 * BMI160 accelerometer and gyroscope driver
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
#include "bmi160_accel_gyro.h"

#define BMI160_NUMBER_ALL_CHANNELS	7
#define BMI160_NUMBER_DATA_CHANNELS	6

#define BMI160_ACC_GYR_WAI_EXP		BMI160_REG_CHIPID_DEFAULT

/*
 * "keep_enabled" makes sensors not to be toggled between enabled/disabled
 * (i.e. keep sensors always enabled). If true, sensor data can be read faster.
 * If false, power consumption of a device can be kept lower.
 */
static bool keep_enabled = true;
module_param(keep_enabled, bool, 0444);

enum bmi160_accel_gyro_scan {
	BMI160_SCAN_GYR_X = 0,
	BMI160_SCAN_GYR_Y,
	BMI160_SCAN_GYR_Z,
	BMI160_SCAN_ACC_X,
	BMI160_SCAN_ACC_Y,
	BMI160_SCAN_ACC_Z,
	BMI160_SCAN_TIMESTAMP,
};

static const struct iio_chan_spec bmi160_accel_gyro_channels[] = {
	BMI160_LSM_CHANNELS(IIO_ANGL_VEL, BMI160_SCAN_GYR_X, IIO_MOD_X, IIO_LE,
		BMI160_REALBITS, BMI160_GYR_OUT_X_L_ADDR),
	BMI160_LSM_CHANNELS(IIO_ANGL_VEL, BMI160_SCAN_GYR_Y, IIO_MOD_Y, IIO_LE,
		BMI160_REALBITS, BMI160_GYR_OUT_Y_L_ADDR),
	BMI160_LSM_CHANNELS(IIO_ANGL_VEL, BMI160_SCAN_GYR_Z, IIO_MOD_Z, IIO_LE,
		BMI160_REALBITS, BMI160_GYR_OUT_Z_L_ADDR),
	BMI160_LSM_CHANNELS(IIO_ACCEL, BMI160_SCAN_ACC_X, IIO_MOD_X, IIO_LE,
		BMI160_REALBITS, BMI160_ACC_OUT_X_L_ADDR),
	BMI160_LSM_CHANNELS(IIO_ACCEL, BMI160_SCAN_ACC_Y, IIO_MOD_Y, IIO_LE,
		BMI160_REALBITS, BMI160_ACC_OUT_Y_L_ADDR),
	BMI160_LSM_CHANNELS(IIO_ACCEL, BMI160_SCAN_ACC_Z, IIO_MOD_Z, IIO_LE,
		BMI160_REALBITS, BMI160_ACC_OUT_Z_L_ADDR),
	IIO_CHAN_SOFT_TIMESTAMP(BMI160_SCAN_TIMESTAMP)
};

extern struct bmi160_sensor_spec bmi160_accel_spec;
extern struct bmi160_sensor_spec bmi160_gyro_spec;

static const struct bmi160 bmi160_accel_gyro_sensors[] = {
	{
		.wai = BMI160_ACC_GYR_WAI_EXP,
		.sensors_supported = {
			[0] = BMI160_ACC_GYR_DEV_NAME,
		},
		.ch = (struct iio_chan_spec *)bmi160_accel_gyro_channels,
		.sensors = {
			[BMI160_ACC] = {
				.spec = &bmi160_accel_spec,
			},
			[BMI160_GYR] = {
				.spec = &bmi160_gyro_spec,
			},
		},
		.idle = {
			.normal_us = BMI160_IDLE_REG_UPDATE_NORMAL_US,
			.others_us = BMI160_IDLE_REG_UPDATE_OTHERS_US,
		},
	},
};

static int bmi160_accel_gyro_read_raw(struct iio_dev *indio_dev,
			struct iio_chan_spec const *ch, int *val,
							int *val2, long mask)
{
	int err;
	struct bmi160_data *agdata = iio_priv(indio_dev);
	struct bmi160_sensor *sensor;

	switch (ch->type) {
	case IIO_ACCEL:
		sensor = &agdata->sensor->sensors[BMI160_ACC];
		break;
	case IIO_ANGL_VEL:
		sensor = &agdata->sensor->sensors[BMI160_GYR];
		break;
	default:
		return -EINVAL;
	}
	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		err = bmi160_read_info_raw(indio_dev, sensor, ch, val);
		if (err < 0)
			goto read_error;

		return IIO_VAL_INT;
	case IIO_CHAN_INFO_SCALE:
		*val = 0;
		*val2 = sensor->current_fullscale->gain;
		return IIO_VAL_INT_PLUS_MICRO;
	default:
		return -EINVAL;
	}

read_error:
	return err;
}

static int bmi160_accel_gyro_write_raw(struct iio_dev *indio_dev,
		struct iio_chan_spec const *chan, int val, int val2, long mask)
{
	int err;
	struct bmi160_data *agdata = iio_priv(indio_dev);
	struct bmi160_sensor *sensor;

	switch (chan->type) {
	case IIO_ACCEL:
		sensor = &agdata->sensor->sensors[BMI160_ACC];
		break;
	case IIO_ANGL_VEL:
		sensor = &agdata->sensor->sensors[BMI160_GYR];
		break;
	default:
		return -EINVAL;
	}
	switch (mask) {
	case IIO_CHAN_INFO_SCALE:
		err = bmi160_set_fullscale_by_gain(indio_dev, sensor, val2);
		break;
	default:
		return -EINVAL;
	}

	return err;
}

static ssize_t bmi160_accel_gyro_sysfs_get_sampling_frequency(
		struct device *dev, struct device_attribute *attr, char *buf)
{
	return bmi160_sysfs_get_sampling_frequency(dev, attr, buf);
}

static ssize_t bmi160_accel_gyro_sysfs_set_sampling_frequency(
		struct device *dev, struct device_attribute *attr,
		const char *buf, size_t count)
{
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct bmi160_data *agdata = iio_priv(indio_dev);

	return bmi160_sysfs_set_sampling_frequency(dev, attr, buf, count,
			agdata->sensor->sensors, 2);
}

static ssize_t bmi160_accel_gyro_sysfs_sampling_frequency_avail(
		struct device *dev, struct device_attribute *attr, char *buf)
{
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct bmi160_data *agdata = iio_priv(indio_dev);

	return bmi160_sysfs_sampling_frequency_avail(dev, attr, buf,
			agdata->sensor->sensors, 2);
}

static ssize_t bmi160_accel_gyro_sysfs_get_keep_enabled(
		struct device *dev, struct device_attribute *attr, char *buf)
{
	return scnprintf(buf, PAGE_SIZE, "%c\n", keep_enabled ? 'Y' : 'N');
}

static ssize_t bmi160_accel_gyro_sysfs_set_keep_enabled(
		struct device *dev, struct device_attribute *attr,
		const char *buf, size_t count)
{
	int i;
	ssize_t ret;
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct bmi160_data *agdata = iio_priv(indio_dev);
	struct bmi160_sensor *sensors[] = {
		&agdata->sensor->sensors[BMI160_ACC],
		&agdata->sensor->sensors[BMI160_GYR],
	};

	mutex_lock(&indio_dev->mlock);

	if (indio_dev->currentmode & INDIO_ALL_BUFFER_MODES) {
		ret = -EBUSY;
		goto out0;
	}

	ret = strtobool(buf, &keep_enabled);
	if (ret < 0)
		goto out0;

	for (i = 0; i < ARRAY_SIZE(sensors); i++)
		sensors[i]->keep_enabled = keep_enabled;

	ret = bmi160_enable_sensors(indio_dev, sensors, ARRAY_SIZE(sensors),
			keep_enabled);
	if (ret < 0)
		goto out0;

	ret = count;
out0:
	mutex_unlock(&indio_dev->mlock);

	return ret;
}

#ifdef CONFIG_IIO_BMI160_DIRECT_BURST_READ
static ssize_t bmi160_sysfs_accel_anglvel_x_y_z_raw(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	int err;
	int i;
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct bmi160_data *agdata = iio_priv(indio_dev);
	struct bmi160_sensor *sensors[] = {
		&agdata->sensor->sensors[BMI160_ACC],
		&agdata->sensor->sensors[BMI160_GYR],
	};
	struct iio_chan_spec const *chans_addr[] = {
		/* address ordered */
		[0] = &bmi160_accel_gyro_channels[BMI160_SCAN_GYR_X],
		[1] = &bmi160_accel_gyro_channels[BMI160_SCAN_GYR_Y],
		[2] = &bmi160_accel_gyro_channels[BMI160_SCAN_GYR_Z],
		[3] = &bmi160_accel_gyro_channels[BMI160_SCAN_ACC_X],
		[4] = &bmi160_accel_gyro_channels[BMI160_SCAN_ACC_Y],
		[5] = &bmi160_accel_gyro_channels[BMI160_SCAN_ACC_Z],
	};
	struct iio_chan_spec const *chans_show[ARRAY_SIZE(chans_addr)];
	int vals[ARRAY_SIZE(chans_addr)];

	for (i = 0; i < ARRAY_SIZE(chans_addr); i++)
		chans_show[i] = &bmi160_accel_gyro_channels[i];

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

static IIO_DEVICE_ATTR(in_accel_anglvel_x_y_z_raw, S_IRUGO,
		bmi160_sysfs_accel_anglvel_x_y_z_raw, NULL, 0);
#endif

static IIO_DEV_ATTR_SAMP_FREQ(S_IWUSR | S_IRUGO,
		bmi160_accel_gyro_sysfs_get_sampling_frequency,
		bmi160_accel_gyro_sysfs_set_sampling_frequency);

static IIO_DEV_ATTR_SAMP_FREQ_AVAIL(
		bmi160_accel_gyro_sysfs_sampling_frequency_avail);

static IIO_DEVICE_ATTR(in_accel_scale_available, S_IRUGO,
		bmi160_accel_sysfs_scale_avail, NULL, 0);

static IIO_DEVICE_ATTR(in_anglvel_scale_available, S_IRUGO,
		bmi160_gyro_sysfs_scale_avail, NULL, 0);

static IIO_DEVICE_ATTR(keep_enabled, S_IWUSR | S_IRUGO,
		bmi160_accel_gyro_sysfs_get_keep_enabled,
		bmi160_accel_gyro_sysfs_set_keep_enabled, 0);

static struct attribute *bmi160_accel_gyro_attributes[] = {
	&iio_dev_attr_sampling_frequency_available.dev_attr.attr,
	&iio_dev_attr_in_accel_scale_available.dev_attr.attr,
	&iio_dev_attr_in_anglvel_scale_available.dev_attr.attr,
	&iio_dev_attr_sampling_frequency.dev_attr.attr,
	&iio_dev_attr_keep_enabled.dev_attr.attr,
#ifdef CONFIG_IIO_BMI160_DIRECT_BURST_READ
	&iio_dev_attr_in_accel_anglvel_x_y_z_raw.dev_attr.attr,
#endif
	NULL,
};

static const struct attribute_group bmi160_accel_gyro_attribute_group = {
	.attrs = bmi160_accel_gyro_attributes,
};

static const struct iio_info accel_gyro_info = {
	.driver_module = THIS_MODULE,
	.attrs = &bmi160_accel_gyro_attribute_group,
	.read_raw = &bmi160_accel_gyro_read_raw,
	.write_raw = &bmi160_accel_gyro_write_raw,
};

#if defined(CONFIG_IIO_TRIGGERED_BUFFER)
static const struct iio_trigger_ops bmi160_accel_gyro_trigger_ops = {
	.owner = THIS_MODULE,
	.set_trigger_state = BMI160_ACC_GYR_TRIGGER_SET_STATE,
};
#define BMI160_ACC_GYR_TRIGGER_OPS (&bmi160_accel_gyro_trigger_ops)
#elif defined(CONFIG_IIO_BUFFER)
static int bmi160_accel_gyro_buffer_preenable(struct iio_dev *indio_dev)
{
	struct bmi160_data *agdata = iio_priv(indio_dev);
	struct bmi160_sensor *sensors[] = {
		&agdata->sensor->sensors[BMI160_ACC],
		&agdata->sensor->sensors[BMI160_GYR],
	};

	return bmi160_buffer_preenable(indio_dev, sensors,
			ARRAY_SIZE(sensors));
}

static int bmi160_accel_gyro_buffer_postenable(struct iio_dev *indio_dev)
{
	struct bmi160_data *agdata = iio_priv(indio_dev);
	struct bmi160_sensor *sensors[] = {
		&agdata->sensor->sensors[BMI160_ACC],
		&agdata->sensor->sensors[BMI160_GYR],
	};

	return bmi160_buffer_postenable(indio_dev, sensors,
			ARRAY_SIZE(sensors));
}

static int bmi160_accel_gyro_buffer_predisable(struct iio_dev *indio_dev)
{
	struct bmi160_data *agdata = iio_priv(indio_dev);
	struct bmi160_sensor *sensors[] = {
		&agdata->sensor->sensors[BMI160_ACC],
		&agdata->sensor->sensors[BMI160_GYR],
	};

	return bmi160_buffer_predisable(indio_dev, sensors,
			ARRAY_SIZE(sensors));
}

static int bmi160_accel_gyro_buffer_postdisable(struct iio_dev *indio_dev)
{
	struct bmi160_data *agdata = iio_priv(indio_dev);
	struct bmi160_sensor *sensors[] = {
		&agdata->sensor->sensors[BMI160_ACC],
		&agdata->sensor->sensors[BMI160_GYR],
	};

	return bmi160_buffer_postdisable(indio_dev, sensors,
			ARRAY_SIZE(sensors));
}

static const struct iio_buffer_setup_ops bmi160_accel_gyro_buffer_setup_ops = {
	.preenable = &bmi160_accel_gyro_buffer_preenable,
	.postenable = &bmi160_accel_gyro_buffer_postenable,
	.predisable = &bmi160_accel_gyro_buffer_predisable,
	.postdisable = &bmi160_accel_gyro_buffer_postdisable,
};
#else
#define BMI160_ACC_GYR_TRIGGER_OPS NULL
#endif

static int bmi160_common_probe(struct iio_dev *indio_dev,
		struct bmi160_sensor *sensor)
{
	int err;
	int index;
	u8 value;
	struct bmi160_data *agdata = iio_priv(indio_dev);
	struct bmi160_sensor_spec *spec = sensor->spec;

	sensor->keep_enabled = keep_enabled;
	value = (spec->fs.def_value & spec->fs.mask) >> __ffs(spec->fs.mask);
	err = bmi160_match_fs_value(indio_dev, sensor, value, &index);
	if (err < 0)
		goto bmi160_common_probe_error;
	sensor->current_fullscale = &spec->fs.fs_avl[index];

	err = bmi160_init_sensor(indio_dev, sensor,
			&agdata->sensor->current_hz);

bmi160_common_probe_error:
	return err;
}

int bmi160_accel_gyro_common_probe(struct iio_dev *indio_dev)
{
	int err;
	struct bmi160_data *agdata = iio_priv(indio_dev);

	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->info = &accel_gyro_info;

	err = bmi160_check_device_support(indio_dev,
				ARRAY_SIZE(bmi160_accel_gyro_sensors),
				bmi160_accel_gyro_sensors);
	if (err < 0)
		goto bmi160_accel_gyro_common_probe_error;

	indio_dev->channels = agdata->sensor->ch;
	indio_dev->num_channels = BMI160_NUMBER_ALL_CHANNELS;

	agdata->sensor->current_hz.integer = BMI160_HZ_INT_TO_INIT;
	agdata->sensor->current_hz.fract = BMI160_HZ_FRACT_TO_INIT;

	err = bmi160_common_probe(indio_dev,
			&agdata->sensor->sensors[BMI160_ACC]);
	if (err < 0)
		goto bmi160_accel_gyro_common_probe_error;
	err = bmi160_common_probe(indio_dev,
			&agdata->sensor->sensors[BMI160_GYR]);
	if (err < 0)
		goto bmi160_accel_gyro_common_probe_error;

	if (agdata->get_irq(indio_dev) > 0) {
#if defined(CONFIG_IIO_BUFFER) && !defined(CONFIG_IIO_TRIGGERED_BUFFER)
		err = bmi160_buffer_probe(indio_dev,
				&bmi160_accel_gyro_buffer_setup_ops);
		if (err < 0)
			goto bmi160_accel_gyro_common_probe_error;
#else
		err = bmi160_accel_gyro_allocate_ring(indio_dev);
		if (err < 0)
			goto bmi160_accel_gyro_common_probe_error;

		err = bmi160_allocate_trigger(indio_dev,
						 BMI160_ACC_GYR_TRIGGER_OPS);
		if (err < 0)
			goto bmi160_accel_gyro_probe_trigger_error;
#endif
	}

	err = iio_device_register(indio_dev);
	if (err)
		goto bmi160_accel_gyro_device_register_error;

	return err;

bmi160_accel_gyro_device_register_error:
#if defined(CONFIG_IIO_BUFFER) && !defined(CONFIG_IIO_TRIGGERED_BUFFER)
	if (agdata->get_irq(indio_dev) > 0)
		bmi160_buffer_remove(indio_dev);
#else
	if (agdata->get_irq(indio_dev) > 0)
		bmi160_deallocate_trigger(indio_dev);
bmi160_accel_gyro_probe_trigger_error:
	if (agdata->get_irq(indio_dev) > 0)
		bmi160_accel_gyro_deallocate_ring(indio_dev);
#endif
bmi160_accel_gyro_common_probe_error:
	return err;
}
EXPORT_SYMBOL(bmi160_accel_gyro_common_probe);

void bmi160_accel_gyro_common_remove(struct iio_dev *indio_dev)
{
	struct bmi160_data *agdata = iio_priv(indio_dev);

	iio_device_unregister(indio_dev);
#if defined(CONFIG_IIO_BUFFER) && !defined(CONFIG_IIO_TRIGGERED_BUFFER)
	if (agdata->get_irq(indio_dev) > 0)
		bmi160_buffer_remove(indio_dev);
#else
	if (agdata->get_irq(indio_dev) > 0) {
		bmi160_deallocate_trigger(indio_dev);
		bmi160_accel_gyro_deallocate_ring(indio_dev);
	}
#endif
	iio_device_free(indio_dev);
}
EXPORT_SYMBOL(bmi160_accel_gyro_common_remove);

MODULE_AUTHOR("Sony Corporation");
MODULE_DESCRIPTION("BMI160 accelerometer and gyroscope driver");
MODULE_LICENSE("GPL v2");
