/*
 * BMI160 core library driver
 *
 * Copyright 2014 Sony Corporation
 *
 * Based on drivers/iio/common/st_sensors/st_sensors_core.c
 *
 * STMicroelectronics sensors core library driver
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
#include <linux/delay.h>
#include <linux/iio/iio.h>
#include <asm/unaligned.h>

#include <linux/iio/common/bmi160.h>

#define POLLING_INTERVAL_US	100

static void bmi160_sync_write(struct iio_dev *indio_dev, bool enabled)
{
	struct bmi160_data *sdata = iio_priv(indio_dev);

	if (enabled)
		/*
		 * sdata->sensor->idle.normal_us is initialized from
		 * BMI160_IDLE_REG_UPDATE_NORMAL_US which currently
		 * is defined as 2 us.
		 * Based on Documentation/timers/timers-howto.txt,
		 * udelay() is preferred for 'A FEW' usecs' delay
		 * ( < ~10us )
		 * So, udelay() instead of usleep_range() is used here.
		 */
		udelay(sdata->sensor->idle.normal_us);
	else
		usleep_range(sdata->sensor->idle.others_us,
				sdata->sensor->idle.others_us);
}

int bmi160_write_data(struct iio_dev *indio_dev, bool enabled,
		u8 reg_addr, u8 data)
{
	int err;
	struct bmi160_data *sdata = iio_priv(indio_dev);

	err = sdata->tf->write_byte(&sdata->tb, sdata->dev, reg_addr, data);

	bmi160_sync_write(indio_dev, enabled);

	return err;
}

int bmi160_write_data_with_mask(struct iio_dev *indio_dev, bool enabled,
		u8 reg_addr, u8 mask, u8 data)
{
	int err;
	u8 new_data;
	struct bmi160_data *sdata = iio_priv(indio_dev);

	err = sdata->tf->read_byte(&sdata->tb, sdata->dev, reg_addr, &new_data);
	if (err < 0)
		goto bmi160_write_data_with_mask_error;

	new_data = ((new_data & (~mask)) | ((data << __ffs(mask)) & mask));
	err = bmi160_write_data(indio_dev, enabled, reg_addr, new_data);

bmi160_write_data_with_mask_error:
	return err;
}

int bmi160_match_odr_hz(struct iio_dev *indio_dev,
		struct bmi160_sensor *sensor,
		unsigned int hz_integer, unsigned int hz_fract,
		int *index_odr_avl)
{
	int i, ret = -EINVAL;
	struct bmi160_sensor_spec *spec = sensor->spec;

	for (i = 0; i < BMI160_ODR_LIST_MAX; i++) {
		if (spec->odr.odr_avl[i].hz.integer == 0 &&
		    spec->odr.odr_avl[i].hz.fract == 0)
			goto bmi160_match_odr_error;

		if (spec->odr.odr_avl[i].hz.integer == hz_integer &&
		    spec->odr.odr_avl[i].hz.fract == hz_fract) {
			*index_odr_avl = i;
			ret = 0;
			break;
		}
	}

bmi160_match_odr_error:
	return ret;
}
EXPORT_SYMBOL(bmi160_match_odr_hz);

int bmi160_match_odr_value(struct iio_dev *indio_dev,
		struct bmi160_sensor *sensor,
		u8 odr_value, int *index_odr_avl)
{
	int i, ret = -EINVAL;
	struct bmi160_sensor_spec *spec = sensor->spec;

	for (i = 0; i < BMI160_ODR_LIST_MAX; i++) {
		if (spec->odr.odr_avl[i].hz.integer == 0 &&
		    spec->odr.odr_avl[i].hz.fract == 0)
			goto bmi160_match_odr_value_error;

		if (spec->odr.odr_avl[i].value == odr_value) {
			*index_odr_avl = i;
			ret = 0;
			break;
		}
	}

bmi160_match_odr_value_error:
	return ret;
}
EXPORT_SYMBOL(bmi160_match_odr_value);

static int bmi160_set_odr_value(struct iio_dev *indio_dev,
		struct bmi160_sensor *sensor, u8 odr_value)
{
	int err;
	struct bmi160_sensor_spec *spec = sensor->spec;

	err = bmi160_write_data_with_mask(indio_dev, sensor->enabled,
			spec->odr.addr, spec->odr.mask, odr_value);
	if (err < 0)
		goto bmi160_match_odr_error;

bmi160_match_odr_error:
	return err;
}

static int bmi160_set_odr_hz(struct iio_dev *indio_dev,
		struct bmi160_sensor *sensor, unsigned int hz_integer,
		unsigned int hz_fract)
{
	int err;
	int index_odr_avl;
	struct bmi160_sensor_spec *spec = sensor->spec;

	err = bmi160_match_odr_hz(indio_dev, sensor, hz_integer,
			hz_fract, &index_odr_avl);
	if (err < 0)
		goto match_odr_hz_error;

	err = bmi160_set_odr_value(indio_dev, sensor,
			spec->odr.odr_avl[index_odr_avl].value);

match_odr_hz_error:
	return err;
}

static int bmi160_match_fs(struct iio_dev *indio_dev,
		struct bmi160_sensor *sensor, unsigned int fs,
		int *index_fs_avl)
{
	int i, ret = -EINVAL;
	struct bmi160_sensor_spec *spec = sensor->spec;

	for (i = 0; i < BMI160_FULLSCALE_AVL_MAX; i++) {
		if (spec->fs.fs_avl[i].num == 0)
			goto bmi160_match_odr_error;

		if (spec->fs.fs_avl[i].num == fs) {
			*index_fs_avl = i;
			ret = 0;
			break;
		}
	}

bmi160_match_odr_error:
	return ret;
}

int bmi160_match_fs_value(struct iio_dev *indio_dev,
		struct bmi160_sensor *sensor, u8 fs_value, int *index_fs_avl)
{
	int i, ret = -EINVAL;
	struct bmi160_sensor_spec *spec = sensor->spec;

	for (i = 0; i < BMI160_FULLSCALE_AVL_MAX; i++) {
		if (spec->fs.fs_avl[i].num == 0)
			goto bmi160_match_fs_value_error;

		if (spec->fs.fs_avl[i].value == fs_value) {
			*index_fs_avl = i;
			ret = 0;
			break;
		}
	}

bmi160_match_fs_value_error:
	return ret;
}
EXPORT_SYMBOL(bmi160_match_fs_value);

static int bmi160_set_fullscale(struct iio_dev *indio_dev,
		struct bmi160_sensor *sensor,unsigned int fs)
{
	int err, i = 0;
	struct bmi160_sensor_spec *spec = sensor->spec;

	err = bmi160_match_fs(indio_dev, sensor, fs, &i);
	if (err < 0)
		goto bmi160_accel_set_fullscale_error;

	err = bmi160_write_data_with_mask(indio_dev, sensor->enabled,
				spec->fs.addr,
				spec->fs.mask,
				spec->fs.fs_avl[i].value);
	if (err < 0)
		goto bmi160_accel_set_fullscale_error;

	sensor->current_fullscale = &spec->fs.fs_avl[i];
	return err;

bmi160_accel_set_fullscale_error:
	dev_err(&indio_dev->dev, "failed to set new fullscale.\n");
	return err;
}

static int bmi160_wait_enabled(struct iio_dev *indio_dev,
	       	struct bmi160_sensor *sensor, bool enable,
		unsigned int timeout_ms)
{
	int err;
	u8 value;
	unsigned long expire;
	struct bmi160_data *sdata = iio_priv(indio_dev);
	struct bmi160_sensor_spec *spec = sensor->spec;

	expire = jiffies + msecs_to_jiffies(timeout_ms);
	for (;;) {
		err = sdata->tf->read_byte(&sdata->tb, sdata->dev,
				spec->pw_stat.addr, &value);
		if (err < 0)
			goto wait_enabled_error;

		value &= spec->pw_stat.mask;
		value >>= __ffs(spec->pw_stat.mask);
		if ((enable && value == spec->pw_stat.value_on) ||
		    (!enable && value == spec->pw_stat.value_off))
			break;

		if (time_after(jiffies, expire)) {
			dev_warn(&indio_dev->dev,
					"Power status didn't change in %ums\n",
					timeout_ms);
			err = -ETIMEDOUT;
			break;
		}
		usleep_range(POLLING_INTERVAL_US, POLLING_INTERVAL_US);
	}

wait_enabled_error:
	return err;
}

static int bmi160_set_enable(struct iio_dev *indio_dev,
		struct bmi160_sensor *sensor, bool enable)
{
	int err;
	struct bmi160_sensor_spec *spec = sensor->spec;

	if (sensor->keep_enabled && !enable)
		return 0;
	if (sensor->enabled == enable)
		return 0;

	err = bmi160_write_data(indio_dev, sensor->enabled,
			spec->pw_cntl.addr,
			(enable ?
			 spec->pw_cntl.value_on :
			 spec->pw_cntl.value_off));
	if (err < 0)
		goto set_enable_error;

	err = bmi160_wait_enabled(indio_dev, sensor, enable,
			(enable ?
			 spec->pw_cntl.timeout_on_ms :
			 spec->pw_cntl.timeout_off_ms));
	if (err < 0)
		goto set_enable_error;

	sensor->enabled = enable;

set_enable_error:
	return err;
}

int bmi160_enable_sensors(struct iio_dev *indio_dev,
		struct bmi160_sensor **sensors, int num_sensor,
		bool enable)
{
	/* caller should lock indio_dev->mlock while calling this function */
	int i, err, err2;

	for (i = 0; i < num_sensor; i++) {
		err = bmi160_set_enable(indio_dev, sensors[i], enable);
		if (err < 0) {
			dev_err(&indio_dev->dev,
				(enable ?
				"Failed to enable sensor#%d, err = %d\n" :
				"Failed to disable sensor#%d, err = %d\n"),
				i, err);
			goto set_enable_error;
		}
	}

	return 0;

set_enable_error:
	enable = !enable;
	for (i--; i >= 0; i--) {
		err2 = bmi160_set_enable(indio_dev, sensors[i], enable);
		if (err2 < 0)
			dev_err(&indio_dev->dev,
				(enable ?
				"Failed to enable sensor#%d, err = %d\n" :
				"Failed to disable sensor#%d, err = %d\n"),
				i, err2);
	}

	return err;
}
EXPORT_SYMBOL(bmi160_enable_sensors);

int bmi160_init_sensor(struct iio_dev *indio_dev,
		struct bmi160_sensor *sensor, struct bmi160_odr_hz *hz)
{
	int err;
	int index_odr_avl;
	struct bmi160_data *sdata = iio_priv(indio_dev);
	struct bmi160_sensor_spec *spec = sensor->spec;

	err = bmi160_match_odr_hz(indio_dev, sensor, hz->integer, hz->fract,
			&index_odr_avl);
	if (err < 0)
		goto init_error;

	err = bmi160_set_enable(indio_dev, sensor, sensor->keep_enabled);
	if (err < 0)
		goto init_error;

	err = sdata->tf->write_byte(&sdata->tb, sdata->dev,
			spec->fs.addr, spec->fs.def_value);
	if (err < 0)
		goto init_error;

	err = sdata->tf->write_byte(&sdata->tb, sdata->dev,
			spec->odr.addr, spec->odr.odr_avl[index_odr_avl].value);
	if (err < 0)
		goto init_error;

init_error:
	return err;
}
EXPORT_SYMBOL(bmi160_init_sensor);

unsigned int bmi160_get_clock_us(struct iio_dev *indio_dev)
{
	struct bmi160_data *sdata = iio_priv(indio_dev);

	return USEC_PER_SEC * BMI160_HZ_INT_TO_FRACT /
		(sdata->sensor->current_hz.integer *
		 BMI160_HZ_INT_TO_FRACT +
		 sdata->sensor->current_hz.fract);
}

static unsigned int bmi160_calc_dataready_timeout_us(struct iio_dev *indio_dev)
{
	unsigned int timeout;

	/* us for 1 clock */
	timeout = bmi160_get_clock_us(indio_dev);

	/* 2 clocks in worst situation */
	timeout *= 2;

	/* FIXME: some other parameters seem to be involved such as
	 * bandwidth setting. */
	timeout *= 10;

	return timeout;
}

static int bmi160_wait_dataready(struct iio_dev *indio_dev,
		struct bmi160_sensor *sensor, unsigned int timeout_us)
{
	int err;
	u8 value;
	unsigned long expire;
	int poll_int_us;
	struct bmi160_data *sdata = iio_priv(indio_dev);
	struct bmi160_sensor_spec *spec = sensor->spec;

	poll_int_us = bmi160_get_clock_us(indio_dev) / 10;
	expire = jiffies + usecs_to_jiffies(timeout_us);
	for (;;) {
		err = sdata->tf->read_byte(&sdata->tb, sdata->dev,
				spec->drdy.addr, &value);
		if (err < 0)
			goto wait_error;
		if (value & spec->drdy.mask)
			break;

		if (time_after(jiffies, expire)) {
			dev_warn(&indio_dev->dev,
					"Data ready status isn't set in %uus\n",
					timeout_us);
			err = -ETIMEDOUT;
			break;
		}
		usleep_range(poll_int_us, poll_int_us);
	}

wait_error:
	return err;
}

int bmi160_wait_sensors_dataready(struct iio_dev *indio_dev,
		struct bmi160_sensor **sensors, int num_sensor)
{
	int i, err;

	for (i = 0; i < num_sensor; i++) {
		err = bmi160_wait_dataready(indio_dev, sensors[i],
				bmi160_calc_dataready_timeout_us(indio_dev));
		if (err < 0)
			goto wait_dataready_err;
	}
	err = 0;

wait_dataready_err:
	return err;
}
EXPORT_SYMBOL(bmi160_wait_sensors_dataready);

int bmi160_set_fullscale_by_gain(struct iio_dev *indio_dev,
		struct bmi160_sensor *sensor, int scale)
{
	int err = -EINVAL, i;
	struct bmi160_sensor_spec *spec = sensor->spec;

	if (indio_dev->currentmode & INDIO_ALL_BUFFER_MODES) {
		err = -EBUSY;
		goto bmi160_match_scale_error;
	}

	for (i = 0; i < BMI160_FULLSCALE_AVL_MAX; i++) {
		if ((spec->fs.fs_avl[i].gain == scale) &&
		    (spec->fs.fs_avl[i].gain != 0)) {
			err = 0;
			break;
		}
	}
	if (err < 0)
		goto bmi160_match_scale_error;

	mutex_lock(&indio_dev->mlock);
	err = bmi160_set_fullscale(indio_dev, sensor,
					spec->fs.fs_avl[i].num);
	mutex_unlock(&indio_dev->mlock);

bmi160_match_scale_error:
	return err;
}
EXPORT_SYMBOL(bmi160_set_fullscale_by_gain);

static int bmi160_read_axis_data(struct iio_dev *indio_dev,
							u8 ch_addr, int *data)
{
	int err;
	u8 outdata[BMI160_BYTE_FOR_CHANNEL];
	struct bmi160_data *sdata = iio_priv(indio_dev);

	err = sdata->tf->read_multiple_byte(&sdata->tb, sdata->dev,
				ch_addr, BMI160_BYTE_FOR_CHANNEL,
				outdata);
	if (err < 0)
		goto read_error;

	*data = (s16)get_unaligned_le16(outdata);

read_error:
	return err;
}

#ifdef CONFIG_IIO_BMI160_DIRECT_BURST_READ
static int bmi160_read_axis_data_burst(struct iio_dev *indio_dev,
		u8 ch_addr, int *data, int num_ch)
{
	int i, err;
	u8 outdata[BMI160_BYTE_FOR_CHANNEL * BMI160_NUM_AXES *
			BMI160_NSENSOR_TYPES];
	struct bmi160_data *sdata = iio_priv(indio_dev);

	if (num_ch > ARRAY_SIZE(outdata) / BMI160_BYTE_FOR_CHANNEL)
		return -EINVAL;

	err = sdata->tf->read_multiple_byte(&sdata->tb, sdata->dev,
			ch_addr, BMI160_BYTE_FOR_CHANNEL * num_ch,
			outdata);
	if (err < 0)
		goto read_error;

	for (i = 0; i < num_ch; i++)
		*data++ = (s16)get_unaligned_le16(outdata + sizeof(s16) * i);

read_error:
	return err;
}

#endif

int bmi160_read_info_raw(struct iio_dev *indio_dev,
		struct bmi160_sensor *sensor, struct iio_chan_spec const *ch,
		int *val)
{
	int err;

	mutex_lock(&indio_dev->mlock);
	if (indio_dev->currentmode & INDIO_ALL_BUFFER_MODES) {
		err = -EBUSY;
		goto read_error;
	} else {
		err = bmi160_set_enable(indio_dev, sensor, true);
		if (err < 0)
			goto read_error;

		err = bmi160_wait_dataready(indio_dev, sensor,
				bmi160_calc_dataready_timeout_us(indio_dev));
		if (err < 0)
			goto read_error;

		err = bmi160_read_axis_data(indio_dev, ch->address, val);
		if (err < 0)
			goto read_error;

		*val = *val >> ch->scan_type.shift;

		err = bmi160_set_enable(indio_dev, sensor, false);
	}
read_error:
	mutex_unlock(&indio_dev->mlock);

	return err;
}
EXPORT_SYMBOL(bmi160_read_info_raw);

#ifdef CONFIG_IIO_BMI160_DIRECT_BURST_READ
int bmi160_read_info_raw_burst(struct iio_dev *indio_dev,
		struct bmi160_sensor **sensors, int num_sensor,
		struct iio_chan_spec const **chans, int num_ch, int *vals)
{
	int i, err, err2;

	/* addresses in chans must be maintained by caller to be
	 * address ordered and no gap */
	for (i = 0; i < num_ch - 1; i++)
		if (chans[i]->address + chans[i]->scan_type.storagebits /
				BITS_PER_BYTE != chans[i + 1]->address)
			return -EINVAL;

	mutex_lock(&indio_dev->mlock);

	if (indio_dev->currentmode & INDIO_ALL_BUFFER_MODES) {
		err = -EBUSY;
		goto error0;
	}

	err = bmi160_enable_sensors(indio_dev, sensors, num_sensor, true);
	if (err < 0)
		goto error0;

	err = bmi160_wait_sensors_dataready(indio_dev, sensors, num_sensor);
	if (err < 0)
		goto error1;

	err = bmi160_read_axis_data_burst(indio_dev, chans[0]->address, vals,
			num_ch);
	if (err < 0)
		goto error1;
	for (i = 0; i < num_ch; i++)
		vals[i] = vals[i] >> chans[i]->scan_type.shift;

error1:
	err2 = bmi160_enable_sensors(indio_dev, sensors, num_sensor, false);
	if (err2 < 0 && err == 0)
		err = err2;
error0:
	mutex_unlock(&indio_dev->mlock);

	return err;
}
EXPORT_SYMBOL(bmi160_read_info_raw_burst);

int bmi160_set_burst_read_chans_to_buf(struct iio_dev *indio_dev,
		struct iio_chan_spec const **chans_show_ordered,
		struct iio_chan_spec const **chans_addr_ordered,
		int *values, int num, char *buf, size_t size)
{
	int show_idx, addr_idx;
	ssize_t len;

	len = 0;
	for (show_idx = 0; show_idx < num; show_idx++)
		for (addr_idx = 0; addr_idx < num; addr_idx++)
			if (chans_addr_ordered[addr_idx] ==
			    chans_show_ordered[show_idx]) {
				len += scnprintf(buf + len, size - len,
						"%d ", values[addr_idx]);
				break;
			}
	if (len > 0)
		buf[len - 1] = '\n';

	return len;
}
EXPORT_SYMBOL(bmi160_set_burst_read_chans_to_buf);
#endif

int bmi160_check_device_support(struct iio_dev *indio_dev,
			int num_sensors_list, const struct bmi160 *sensors)
{
	u8 wai;
	int i, n, err;
	struct bmi160_data *sdata = iio_priv(indio_dev);

	err = sdata->tf->read_byte(&sdata->tb, sdata->dev,
					BMI160_WAI_ADDRESS, &wai);
	if (err < 0) {
		dev_err(&indio_dev->dev, "failed to read Who-Am-I register.\n");
		goto read_wai_error;
	}

	for (i = 0; i < num_sensors_list; i++) {
		if (sensors[i].wai == wai)
			break;
	}
	if (i == num_sensors_list) {
#ifdef CONFIG_IIO_BMI160_CONTINUE_UNKNOWN_REG_CHIPID
		i = 0;
		dev_warn(&indio_dev->dev, "unknown device: WhoAmI (0x%x)\n",
				wai);
		dev_warn(&indio_dev->dev, "continue with assuming it as 0x%x\n",
				sensors[i].wai);
#else
		goto device_not_supported;
#endif
	}

	for (n = 0; n < ARRAY_SIZE(sensors[i].sensors_supported); n++) {
		if (strcmp(indio_dev->name,
				&sensors[i].sensors_supported[n][0]) == 0)
			break;
	}
	if (n == ARRAY_SIZE(sensors[i].sensors_supported)) {
		dev_err(&indio_dev->dev, "device name and WhoAmI mismatch.\n");
		goto sensor_name_mismatch;
	}

	sdata->sensor = (struct bmi160 *)&sensors[i];

	return i;

#ifndef CONFIG_IIO_BMI160_CONTINUE_UNKNOWN_REG_CHIPID
device_not_supported:
#endif
	dev_err(&indio_dev->dev, "device not supported: WhoAmI (0x%x).\n", wai);
sensor_name_mismatch:
	err = -ENODEV;
read_wai_error:
	return err;
}
EXPORT_SYMBOL(bmi160_check_device_support);

static ssize_t bmi160_set_sampling_frequency_to_buf(struct bmi160_odr_hz *hz,
		char *buf, size_t size)
{
	ssize_t len;

 	len = scnprintf(buf, size, "%d", hz->integer);
 	if (hz->fract)
 		len += scnprintf(buf + len, size - len, "." BMI160_HZ_FRACT_FMT,
				hz->fract);

	return len;
}

ssize_t bmi160_sysfs_get_sampling_frequency(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	ssize_t len;
	struct bmi160_data *adata = iio_priv(dev_get_drvdata(dev));

	len = bmi160_set_sampling_frequency_to_buf(&adata->sensor->current_hz,
			buf, PAGE_SIZE);
	len += scnprintf(buf + len, PAGE_SIZE - len, "\n");

	return len;
}
EXPORT_SYMBOL(bmi160_sysfs_get_sampling_frequency);

ssize_t bmi160_sysfs_set_sampling_frequency(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size,
		struct bmi160_sensor *sensors, int num_sensor)
{
	int err, err2, i = 0;
	unsigned int hz_integer, hz_fract;
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct bmi160_data *sdata = iio_priv(indio_dev);

	if (indio_dev->currentmode & INDIO_ALL_BUFFER_MODES) {
		err = -EBUSY;
		goto conversion_error;
	}

	err = iio_str_to_fixpoint(buf, BMI160_HZ_INT_TO_FRACT / 10, &hz_integer,
			&hz_fract);
	if (err < 0)
		goto conversion_error;

	mutex_lock(&indio_dev->mlock);

	for (i = 0; i < num_sensor; i++) {
		err = bmi160_set_odr_hz(indio_dev, &sensors[i], hz_integer,
				hz_fract);
		if (err < 0)
			goto set_odr_hz_error;
	}
	sdata->sensor->current_hz.integer = hz_integer;
	sdata->sensor->current_hz.fract = hz_fract;

set_odr_hz_error:
	if (err < 0) {
		for (; i >= 0; i--) {
			err2 = bmi160_set_odr_hz(indio_dev, &sensors[i],
					sdata->sensor->current_hz.integer,
					sdata->sensor->current_hz.fract);
			if (err2 < 0)
				dev_err(dev, "Inconsistency may occur between "
					     "internal settings.\n");
		}
	}

	mutex_unlock(&indio_dev->mlock);

conversion_error:
	return err < 0 ? err : size;
}
EXPORT_SYMBOL(bmi160_sysfs_set_sampling_frequency);

ssize_t bmi160_sysfs_sampling_frequency_avail(struct device *dev,
		struct device_attribute *attr, char *buf,
		struct bmi160_sensor *sensors, int num_sensor)
{
	int i, j, len = 0, num_cmn;
	struct bmi160_sensor_spec *spec, *spec2;
	struct bmi160_odr_hz *hz, *hz2;
	struct bmi160_odr_hz hz_cmn[BMI160_ODR_LIST_MAX];

	if (num_sensor == 2) {
		/* returns common frequencies */
		num_cmn = 0;
		spec = sensors[0].spec;
		spec2 = sensors[1].spec;

		for (i = 0; i < BMI160_ODR_LIST_MAX; i++) {
			hz = &spec->odr.odr_avl[i].hz;

			if (hz->integer == 0 && hz->fract == 0)
				break;

			for (j = 0; j < BMI160_ODR_LIST_MAX; j++) {
				hz2 = &spec2->odr.odr_avl[j].hz;

				if (hz2->integer == 0 && hz2->fract == 0)
					break;

				if (hz->integer == hz2->integer &&
				    hz->fract == hz2->fract) {
					hz_cmn[num_cmn].integer = hz2->integer;
					hz_cmn[num_cmn].fract = hz2->fract;
					num_cmn++;
					break;
				}
			}
		}
		for (i = 0; i < num_cmn; i++) {
			len += bmi160_set_sampling_frequency_to_buf(&hz_cmn[i],
					buf + len, PAGE_SIZE - len);
			len += scnprintf(buf + len, PAGE_SIZE - len, " ");
		}
	} else if (num_sensor == 1) {
		spec = sensors[0].spec;

		for (i = 0; i < BMI160_ODR_LIST_MAX; i++) {
			if (spec->odr.odr_avl[i].hz.integer == 0 &&
			    spec->odr.odr_avl[i].hz.fract == 0)
				break;

			len += bmi160_set_sampling_frequency_to_buf(
					&spec->odr.odr_avl[i].hz,
					buf + len, PAGE_SIZE - len);
			len += scnprintf(buf + len, PAGE_SIZE - len, " ");
		}
	} else {
		if (num_sensor > 2)
			dev_err(dev, "Not implemented to support much than 2 "
					"types of sensor.\n");
		len = -EINVAL;
		goto error;
	}
	buf[len - 1] = '\n';

error:
	return len;
}
EXPORT_SYMBOL(bmi160_sysfs_sampling_frequency_avail);

ssize_t bmi160_sysfs_scale_avail(struct device *dev,
				struct device_attribute *attr, char *buf,
				struct bmi160_sensor *sensor)
{
	int i, len = 0;
	struct bmi160_sensor_spec *spec = sensor->spec;

	for (i = 0; i < BMI160_FULLSCALE_AVL_MAX; i++) {
		if (spec->fs.fs_avl[i].num == 0)
			break;

		len += scnprintf(buf + len, PAGE_SIZE - len, "0.%06u ",
					spec->fs.fs_avl[i].gain);
	}
	buf[len - 1] = '\n';

	return len;
}
EXPORT_SYMBOL(bmi160_sysfs_scale_avail);

MODULE_AUTHOR("Sony Corporation");
MODULE_DESCRIPTION("BMI160 core");
MODULE_LICENSE("GPL v2");
