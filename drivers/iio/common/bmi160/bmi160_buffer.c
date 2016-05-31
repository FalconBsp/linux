/*
 *  BMI160 buffer library driver
 *
 *  Copyright 2014 Sony Corporation
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  version 2 of the  License.
 *
 *  THIS  SOFTWARE  IS PROVIDED   ``AS  IS'' AND   ANY  EXPRESS OR IMPLIED
 *  WARRANTIES,   INCLUDING, BUT NOT  LIMITED  TO, THE IMPLIED WARRANTIES OF
 *  MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN
 *  NO  EVENT  SHALL   THE AUTHOR  BE    LIABLE FOR ANY   DIRECT, INDIRECT,
 *  INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 *  NOT LIMITED   TO, PROCUREMENT OF  SUBSTITUTE GOODS  OR SERVICES; LOSS OF
 *  USE, DATA,  OR PROFITS; OR  BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 *  ANY THEORY OF LIABILITY, WHETHER IN  CONTRACT, STRICT LIABILITY, OR TORT
 *  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 *  THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *  You should have received a copy of the  GNU General Public License along
 *  with this program; if not, write  to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/iio/iio.h>
#include <linux/iio/buffer.h>
#include <linux/iio/kfifo_buf.h>
#include <linux/iio/sysfs.h>
#include <linux/interrupt.h>
#include <linux/irqreturn.h>
#include <linux/sched.h>

#include <asm/unaligned.h>

#include <linux/iio/common/bmi160.h>

#define THRESHOLD_DEFAULT 10

#define IRQ_FLAGS IRQF_TRIGGER_LOW

#ifdef DEBUG_COUNTUP
static unsigned short debug_counter = -1;
module_param_named(counter, debug_counter, ushort, 0644);

static short debug_countup_idx;
module_param_named(countup_idx, debug_countup_idx, short, 0644);
#endif

#ifdef DEBUG_OPT_IF
struct iio_dev *debug_indio_dev;
#endif

typedef unsigned long long timestamp_t; /* in ns */

static inline void get_timestamp(timestamp_t *timestamp)
{
	*timestamp = sched_clock();
}

static inline s64 timestamp_to_s64(timestamp_t *timestamp)
{
	return (s64) *timestamp;
}

static inline timestamp_t ul_to_timestamp(unsigned long value)
{
	return (timestamp_t) value;
}

static inline timestamp_t add_timestamp(timestamp_t *lhs, timestamp_t *rhs)
{
	return *lhs + *rhs;
}

static inline timestamp_t sub_timestamp(timestamp_t *lhs, timestamp_t *rhs)
{
	return *lhs - *rhs;
}

static unsigned long get_frame_interval(struct iio_dev *indio_dev)
{
	return NSEC_PER_USEC * bmi160_get_clock_us(indio_dev);
}

static int to_bmi160_sensor_type(enum iio_chan_type type)
{
	switch (type) {
	case IIO_ANGL_VEL:
		return BMI160_GYR;
	case IIO_ACCEL:
		return BMI160_ACC;
	default:
		return -EINVAL;
	}
}

static int is_scan_set(struct iio_dev *indio_dev, enum iio_chan_type type,
		int channel)
{
	int i, ret;

	ret = 0;
	for (i = 0; i < indio_dev->num_channels; i++)
		if (indio_dev->channels[i].type == type &&
		    indio_dev->channels[i].channel2 == channel) {
			ret = iio_scan_mask_query(indio_dev, indio_dev->buffer,
					i);
			break;
		}

	if (ret < 0) {
		dev_err(&indio_dev->dev,
				"Error on iio_scan_mask_query(), err = %d\n",
				ret);
		ret = 0;
	}
	return ret;
}

#define is_scan_any_axis_set(INDIO_DEV, IIO_CHAN_TYPE)			\
	(is_scan_set((INDIO_DEV), (IIO_CHAN_TYPE), IIO_MOD_X) ||	\
	 is_scan_set((INDIO_DEV), (IIO_CHAN_TYPE), IIO_MOD_Y) ||	\
	 is_scan_set((INDIO_DEV), (IIO_CHAN_TYPE), IIO_MOD_Z))
#define is_scan_all_axes_set(INDIO_DEV, IIO_CHAN_TYPE)			\
	(is_scan_set((INDIO_DEV), (IIO_CHAN_TYPE), IIO_MOD_X) &&	\
	 is_scan_set((INDIO_DEV), (IIO_CHAN_TYPE), IIO_MOD_Y) &&	\
	 is_scan_set((INDIO_DEV), (IIO_CHAN_TYPE), IIO_MOD_Z))

static int validate_chan_order(struct iio_dev *indio_dev)
{
	int i, err, idx, chan_idx[BMI160_NSENSOR_TYPES];
	enum iio_chan_type type;

	for (i = 0; i < ARRAY_SIZE(chan_idx); i++)
		chan_idx[i] = -1;

	i = 0;
	while (i < indio_dev->num_channels) {
		type = indio_dev->channels[i].type;
		switch (type) {
		case IIO_ANGL_VEL:
		case IIO_ACCEL:
			/* The datasheet defines channels are ordered as,
			 *     X axis => Y axis => Z axis
			 * for each type of sensor */
			if (indio_dev->channels[i + 1].type != type ||
			    indio_dev->channels[i + 2].type != type ||
			    indio_dev->channels[i + 0].channel2 != IIO_MOD_X ||
			    indio_dev->channels[i + 1].channel2 != IIO_MOD_Y ||
			    indio_dev->channels[i + 2].channel2 != IIO_MOD_Z) {
				err = -EINVAL;
				goto err0;
			}

			idx = to_bmi160_sensor_type(type);
			if (idx < 0) {
				err = idx;
				goto err0;
			}
			chan_idx[idx] = i;

			i += BMI160_NUM_AXES;
			break;
		case IIO_TIMESTAMP:
			i++;
			break;
		default:
			dev_warn(&indio_dev->dev,
					"Unrecognized type of channel : %d\n",
					type);
			i++;
			break;
		}
	}
	/* The datasheet defines channels of sensor are ordered as,
	 *    gyroscope => accelerometer
	 *
	 * FIXME if external magnetmeter (e.g. BMM150) is supported. */
	if (chan_idx[BMI160_GYR] >= 0 && chan_idx[BMI160_ACC] >= 0)
		if (chan_idx[BMI160_GYR] >= chan_idx[BMI160_ACC]) {
			err = -EINVAL;
			goto err0;
		}

	return 0;

err0:
	dev_err(&indio_dev->dev, "Invalid channels order\n");
	return err;
}

/*
 * The datasheet has no description about the condition of the device operating
 * in normal/suspend mode, while there is a description about the normal/suspend
 * modes which are associated with each sensor. IOW, we cannot decide how long
 * we should wait for post write to some registers which are not associtated
 * with certain sensor such as FIFO watermark to be completed. In order to
 * ensure post write to be completed, this driver assumes the device to be
 * operating in normal mode if all sensors are enabled.
 */
static bool is_all_sensors_enabled(struct iio_dev *indio_dev)
{
	int i;
	struct bmi160_data *sdata = iio_priv(indio_dev);

	for (i = 0; i < ARRAY_SIZE(sdata->sensor->sensors); i++)
		if (!sdata->sensor->sensors[i].enabled)
			return false;

	return true;
}

static size_t get_frame_size(struct iio_dev *indio_dev)
{
	return (is_scan_any_axis_set(indio_dev, IIO_ACCEL) +
		is_scan_any_axis_set(indio_dev, IIO_ANGL_VEL)) *
		BMI160_NUM_AXES * (BMI160_DATABITS / BITS_PER_BYTE);
}

static ssize_t read_fifo_count(struct iio_dev *indio_dev)
{
	int err;
	char buf[BMI160_REG_FIFO_LENGTH_SIZE];
	struct bmi160_data *sdata = iio_priv(indio_dev);

	err = sdata->tf->read_multiple_byte(&sdata->tb, sdata->dev,
			BMI160_REG_FIFO_LENGTH_0, sizeof(buf), buf);
	if (err < 0)
		goto err0;

	return get_unaligned_le16(buf);

err0:
	dev_err(&indio_dev->dev,
			"Failed to read fifo count, err = %d\n",
			err);
	return err;
}

#if !defined(CONFIG_IIO_BMI160_SKIP_FIFO_FULL_CHECK)
static int is_fifo_full(struct iio_dev *indio_dev)
{
	int err;
	u8 status;
	struct bmi160_data *sdata = iio_priv(indio_dev);

	err = sdata->tf->read_byte(&sdata->tb, sdata->dev,
			BMI160_REG_INT_STATUS_1, &status);
	if (err < 0)
		goto err0;

	return (status & BMI160_REG_INT_STATUS_1_FFULL_MASK) ? 1 : 0;

err0:
	dev_err(&indio_dev->dev,
			"Failed to check FIFO full status, err = %d\n",
			err);
	return err;
}
#endif

static int flush_fifo(struct iio_dev *indio_dev)
{
	int err;

	err = bmi160_write_data(indio_dev, is_all_sensors_enabled(indio_dev),
			BMI160_REG_CMD, BMI160_REG_CMD_FIFO_FLUSH);
	if (err < 0)
		goto err0;

	return 0;

err0:
	dev_err(&indio_dev->dev,
			"Failed to flush FIFO, err = %d\n",
			err);
	return err;
}

static int config_fifo_irq(struct iio_dev *indio_dev, int enable)
{
	int err, i;
	bool is_enabled;
	u8 init[][2] = { /* address, value */
		{BMI160_REG_INT_OUT_CTRL, BMI160_REG_INT_OUT_CTRL_INT1_EN_MASK},
		{BMI160_REG_INT_EN_0, 0},
		{BMI160_REG_INT_EN_1, BMI160_REG_INT_EN_1_FWM_EN_MASK |
				      BMI160_REG_INT_EN_1_FFULL_EN_MASK},
		{BMI160_REG_INT_EN_2, 0},
	};

#ifdef CONFIG_IIO_BMI160_USE_GPIO_IRQ
	struct bmi160_data *sdata = iio_priv(indio_dev);

	if (!sdata->gpio_active_low)
		init[0][1] |= BMI160_REG_INT_OUT_CTRL_INT1_LVL_MASK;
#endif

	is_enabled = is_all_sensors_enabled(indio_dev);
	for (i = 0; i < ARRAY_SIZE(init); i++) {
		err = bmi160_write_data(indio_dev, is_enabled,
				init[i][0], (enable ? init[i][1] : 0));
		if (err < 0)
			goto err0;
	}

	return 0;

err0:
	dev_err(&indio_dev->dev,
			(enable ?
			 "Failed to config FIFO IRQ, err = %d\n" :
			 "Failed to unconfig FIFO IRQ, err = %d\n"),
			 err);
	return err;
}

static int enable_fifo_irq(struct iio_dev *indio_dev, int enable)
{
	int err, i;
	bool is_enabled;
	u8 init[][2] = { /* address, value */
		{BMI160_REG_INT_MAP_0, 0},
		{BMI160_REG_INT_MAP_1, BMI160_REG_INT_MAP_1_FWM_INT1_MASK},
		{BMI160_REG_INT_MAP_2, 0},
	};

	is_enabled = is_all_sensors_enabled(indio_dev);
	for (i = 0; i < ARRAY_SIZE(init); i++) {
		err = bmi160_write_data(indio_dev, is_enabled,
				init[i][0], (enable ? init[i][1] : 0));
		if (err < 0)
			goto err0;
	}

	return 0;

err0:
	dev_err(&indio_dev->dev,
			(enable ?
			 "Failed to enable IRQ, err = %d\n" :
			 "Failed to disable IRQ, err = %d\n"),
			err);
	return err;
}

static int set_fifo_watermark(struct iio_dev *indio_dev)
{
	int err;
	size_t watermark;
	struct bmi160_data *sdata = iio_priv(indio_dev);

	watermark = sdata->sensor->threshold * get_frame_size(indio_dev);
	/*
	 * FIXME: This check cannot ensure for IRQ for watermark to be raised.
	 */
	if (watermark > BMI160_FIFO_SIZE) {
		dev_err(&indio_dev->dev,
				"threshold can be set %d at max in current scan_elements setting\n",
				BMI160_FIFO_SIZE / get_frame_size(indio_dev));
		err= -EINVAL;
		goto err0;
	}
	watermark /= BMI160_REG_FIFO_CONFIG_0_UNIT_SIZE;

	err = bmi160_write_data(indio_dev, is_all_sensors_enabled(indio_dev),
			BMI160_REG_FIFO_CONFIG_0, watermark);
	if (err < 0) {
		dev_err(&indio_dev->dev,
				"Failed to set FIFO watermark, err = %d\n",
				err);
		goto err0;
	}

	return 0;

err0:
	return err;
}

static int set_fifo_entries(struct iio_dev *indio_dev, int enable)
{
	int err;
	u8 reg;
	bool sparse;
	struct bmi160_data *sdata = iio_priv(indio_dev);

	/* Flag when not all X/Y/Z axes of any sensor is enabled or
	 * disabled. Since each FIFO frame consists of a complete set of
	 * X/Y/Z axes data, we should compose returing buffer elements
	 * as requested by user. */
	sparse = false;

	reg = 0;
	if (enable) {
		if (is_scan_any_axis_set(indio_dev, IIO_ACCEL)) {
			reg |= BMI160_REG_FIFO_CONFIG_1_ACC_EN;
			if (!is_scan_all_axes_set(indio_dev, IIO_ACCEL))
				sparse = true;
		}
		if (is_scan_any_axis_set(indio_dev, IIO_ANGL_VEL)) {
			reg |= BMI160_REG_FIFO_CONFIG_1_GYR_EN;
			if (!is_scan_all_axes_set(indio_dev, IIO_ANGL_VEL))
				sparse = true;
		}
	}

	err = bmi160_write_data(indio_dev, is_all_sensors_enabled(indio_dev),
			BMI160_REG_FIFO_CONFIG_1, reg);
	if (err < 0)
		goto err0;

	sdata->sensor->sparse_frame = sparse;

	return 0;

err0:
	dev_err(&indio_dev->dev,
			(enable ?
			 "Failed to set sensor entries, err = %d\n" :
			 "Failed to unset sensor entries, err = %d\n"),
			err);
	return err;
}

static int reset_fifo(struct iio_dev *indio_dev)
{
	int err;

	err = flush_fifo(indio_dev);
	if (err < 0)
		goto out0;

	err = enable_fifo_irq(indio_dev, 0);
	if (err < 0)
		goto out0;

out0:
	return err;
}

static ssize_t read_fifo_frames(struct iio_dev *indio_dev, char *buf,
		size_t len, size_t frame_cnt)
{
	ssize_t readout;
	struct bmi160_data *sdata = iio_priv(indio_dev);

	if (len < get_frame_size(indio_dev) * frame_cnt) {
		readout = -ENOBUFS;
		goto err0;
	}

	readout = sdata->tf->read_multiple_byte(&sdata->tb, sdata->dev,
			BMI160_REG_FIFO_DATA,
			get_frame_size(indio_dev) * frame_cnt, buf);
	if (readout < 0)
		goto err0;

	return readout;

err0:
	dev_err(&indio_dev->dev,
			"Failed to read FIFO frames, err = %d\n",
			readout);
	return readout;
}

static int read_fifo(struct iio_dev *indio_dev, char *buf, size_t len,
		size_t *frame_cnt, timestamp_t *timestamp)
{
	int err;
#if !defined(CONFIG_IIO_BMI160_SKIP_FIFO_FULL_CHECK)
	int is_full;
#endif
	ssize_t readout, fifo_cnt, frame_cnt_tmp;
	timestamp_t duration;
	struct bmi160_data *sdata = iio_priv(indio_dev);

	*frame_cnt = 0;

	fifo_cnt = read_fifo_count(indio_dev);
	if (fifo_cnt < 0) {
		err = fifo_cnt;
		goto err1;
	}
	if (fifo_cnt == 0)
		goto out0;

	/* temporarily, store timestamp of the latest frame */
	get_timestamp(timestamp);

	/* read all FIFO frames */
	while (fifo_cnt > 0) {
		if (fifo_cnt < sdata->sensor->frame_size) {
			dev_warn(&indio_dev->dev,
					"fifo_cnt(%d) is less than frame_size(%d)\n",
					fifo_cnt, sdata->sensor->frame_size);
			goto out1;
		}

		frame_cnt_tmp = min((size_t) fifo_cnt, sdata->tb.max_read_len) /
				sdata->sensor->frame_size;
		while (len < frame_cnt_tmp * sdata->sensor->frame_size)
			frame_cnt_tmp--;
		if (frame_cnt_tmp == 0) {
			dev_warn(&indio_dev->dev,
					"No more buffer available, %d bytes reamin\n",
					fifo_cnt);
			goto out1;
		}

#if !defined(CONFIG_IIO_BMI160_SKIP_FIFO_FULL_CHECK)
		is_full = is_fifo_full(indio_dev);
		if (is_full < 0) {
			err = is_full;
			goto err1;
		}
		if (is_full) {
			dev_err(&indio_dev->dev,
					"FIFO has been full, fifo_cnt: %d, read_fifo_count(): %d\n",
					fifo_cnt, read_fifo_count(indio_dev));
			err = -EIO;
			goto err1;
		}
#endif

		readout = read_fifo_frames(indio_dev, buf, len, frame_cnt_tmp);
		if (readout < 0) {
			err = readout;
			goto err1;
		}
		if (readout != frame_cnt_tmp * sdata->sensor->frame_size) {
			dev_warn(&indio_dev->dev,
					"%d bytes are readout while %d bytes are expected\n",
					readout,
					frame_cnt_tmp *
					sdata->sensor->frame_size);
			*frame_cnt += readout / sdata->sensor->frame_size;
			goto out1;
		}
#ifdef DEBUG_COUNTUP
		if (debug_countup_idx >= 0 &&
		    debug_countup_idx < indio_dev->num_channels) {
			int i;
			char *field;

			field = buf + debug_countup_idx * sizeof(s16);
			for (i = 0; i < frame_cnt_tmp; i++) {
				put_unaligned_le16(debug_counter++, field);
				field += sdata->sensor->frame_size;
			}
		}
#endif
		*frame_cnt += frame_cnt_tmp;
		buf += readout;
		len -= readout;
		fifo_cnt -= readout;
	}

out1:
	duration = ul_to_timestamp(get_frame_interval(indio_dev) *
			(*frame_cnt - 1));
	*timestamp = sub_timestamp(timestamp, &duration);
out0:
	return 0;

err1:
	flush_fifo(indio_dev);
	return err;
}

static ssize_t set_sparse_frame_to_buf(struct iio_dev *indio_dev,
		const char *fifo_frame, char *buf, size_t len)
{
	int i, is_set;
	ssize_t written;
	size_t data_size;
	enum iio_chan_type type;

	written = 0;
	i = 0;
	while (i < indio_dev->num_channels) {
		type = indio_dev->channels[i].type;
		switch (type) {
		case IIO_ACCEL:
		case IIO_ANGL_VEL:
			if (!is_scan_any_axis_set(indio_dev, type)) {
				/* fifo_frame contains no data of the type */
				i += BMI160_NUM_AXES;
				continue;
			}

			data_size = indio_dev->channels[i].scan_type.storagebits
					/ BITS_PER_BYTE;

			is_set = iio_scan_mask_query(indio_dev,
					indio_dev->buffer, i);
			if (is_set < 0) {
				written = is_set;
				goto out0;
			}
			if (is_set) {
				if (len < data_size) {
					written = -ENOBUFS;
					goto out0;
				}
				memcpy(buf, fifo_frame, data_size);

				written += data_size;
				buf += data_size;
				len -= data_size;
			}
			fifo_frame += data_size;
			break;
		case IIO_TIMESTAMP:
			break;
		default:
			dev_warn(&indio_dev->dev,
					"Unrecognized type of channel : %d\n",
					type);
			break;
		}
		i++;
	}
out0:
	return written;
}

static ssize_t set_frame_to_buf(struct iio_dev *indio_dev,
		const char *fifo_frame, char *buf, size_t len)
{
	struct bmi160_data *sdata = iio_priv(indio_dev);

	if (sdata->sensor->sparse_frame)
		return set_sparse_frame_to_buf(indio_dev, fifo_frame, buf, len);

	if (len < sdata->sensor->frame_size)
		return -ENOBUFS;

	memcpy(buf, fifo_frame, sdata->sensor->frame_size);
	return sdata->sensor->frame_size;
}

static int push_frames_to_buffers(struct iio_dev *indio_dev,
		size_t frame_cnt, timestamp_t *timestamp)
{
	int i, err;
	ssize_t len;
	char *frame;
	u8 buf[32];
	timestamp_t interval;
	struct bmi160_data *sdata = iio_priv(indio_dev);

	interval = ul_to_timestamp(get_frame_interval(indio_dev));
	frame = sdata->buffer_data;

	for (i = 0; i < frame_cnt; i++) {
		len = set_frame_to_buf(indio_dev, frame, buf, sizeof(buf));
		if (len < 0) {
			err = len;
			goto err0;
		}

		*(s64 *)(buf + ALIGN(len, sizeof(s64))) =
				timestamp_to_s64(timestamp);

		err = iio_push_to_buffers(indio_dev, buf);
		if (err < 0)
			goto err0;

		*timestamp = add_timestamp(timestamp, &interval);
		frame += sdata->sensor->frame_size;
	}

	return 0;

err0:
	return err;
}

static irqreturn_t irq_h(int irq, void *p)
{
	struct iio_dev *indio_dev = (struct iio_dev *) p;
	struct bmi160_data *sdata = iio_priv(indio_dev);

	if (irq != sdata->get_irq(indio_dev))
		return IRQ_NONE;

	disable_irq_nosync(irq);

	return IRQ_WAKE_THREAD;
}

static irqreturn_t worker_h(int irq, void *p)
{
	int err;
	size_t frame_cnt;
	timestamp_t timestamp;
	struct iio_dev *indio_dev = (struct iio_dev *) p;
	struct bmi160_data *sdata = iio_priv(indio_dev);

	if (irq != sdata->get_irq(indio_dev))
		return IRQ_NONE;

	mutex_lock(&indio_dev->mlock);

	if (!(indio_dev->currentmode & INDIO_ALL_BUFFER_MODES))
		goto out1;

	err = read_fifo(indio_dev, sdata->buffer_data, sdata->buffer_len,
			&frame_cnt, &timestamp);
	if (err < 0)
		goto out1;

	err = push_frames_to_buffers(indio_dev, frame_cnt, &timestamp);
	if (err < 0)
		goto out1;

out1:
	enable_irq(irq);

	mutex_unlock(&indio_dev->mlock);
	return IRQ_HANDLED;
}

static ssize_t get_threshold(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct bmi160_data *sdata = iio_priv(indio_dev);

	return scnprintf(buf, PAGE_SIZE, "%u\n", sdata->sensor->threshold);
}

static ssize_t set_threshold(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	int err;
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct bmi160_data *sdata = iio_priv(indio_dev);

	if (indio_dev->currentmode & INDIO_ALL_BUFFER_MODES)
		return -EBUSY;

	err = kstrtouint(buf, 10, &sdata->sensor->threshold);
	if (err < 0)
		return err;

	return count;
}

static ssize_t set_force_read(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct bmi160_data *sdata = iio_priv(indio_dev);
	size_t frame_cnt;
	timestamp_t timestamp;
	bool force_read;
	int err;

	err = strtobool(buf, &force_read);
	if (err < 0)
		return err;

	if (force_read == false)
		return count;

	mutex_lock(&indio_dev->mlock);

	if (!(indio_dev->currentmode & INDIO_ALL_BUFFER_MODES)) {
		err = -EINVAL;
		goto out1;
	}

	err = read_fifo(indio_dev, sdata->buffer_data, sdata->buffer_len,
			&frame_cnt, &timestamp);
	if (err < 0) {
		dev_err(&indio_dev->dev,
				"Error on read_fifo(), err = %d\n", err);
		goto out1;
	}

	err = push_frames_to_buffers(indio_dev, frame_cnt, &timestamp);
	if (err < 0 && err != -EBUSY) {
		dev_err(&indio_dev->dev,
				"Error on push_frames_to_buffers(), err = %d\n",
				err);
		goto out1;
	}

	err = count;
out1:
	mutex_unlock(&indio_dev->mlock);
	return err;
}

static IIO_DEVICE_ATTR(threshold, S_IWUSR | S_IRUGO,
		get_threshold, set_threshold, 0);
static IIO_DEVICE_ATTR(force_read, S_IWUSR,
		NULL, set_force_read, 0);

static struct attribute *attrs[] = {
	&iio_dev_attr_threshold.dev_attr.attr,
	&iio_dev_attr_force_read.dev_attr.attr,
	NULL,
};

static const struct attribute_group attr_group = {
	.attrs = attrs,
};

int bmi160_buffer_preenable(struct iio_dev *indio_dev,
		struct bmi160_sensor **sensors, int num_sensor)
{
	int err;
	struct bmi160_data *sdata = iio_priv(indio_dev);

	sdata->sensor->frame_size = get_frame_size(indio_dev);

	err = set_fifo_entries(indio_dev, 1);
	if (err < 0)
		goto err0;

	err = enable_fifo_irq(indio_dev, 1);
	if (err < 0)
		goto err1;

	err = iio_sw_buffer_preenable(indio_dev);
	if (err < 0)
		goto err2;

	return 0;

err2:
	enable_fifo_irq(indio_dev, 0);
err1:
	set_fifo_entries(indio_dev, 0);
err0:
	return err;
}
EXPORT_SYMBOL(bmi160_buffer_preenable);

int bmi160_buffer_postenable(struct iio_dev *indio_dev,
		struct bmi160_sensor **sensors, int num_sensor)
{
	int err;

	err = set_fifo_watermark(indio_dev);
	if (err < 0)
		goto out0;

	err = bmi160_enable_sensors(indio_dev, sensors, num_sensor, true);
out0:
	return err;
}
EXPORT_SYMBOL(bmi160_buffer_postenable);

int bmi160_buffer_predisable(struct iio_dev *indio_dev,
		struct bmi160_sensor **sensors, int num_sensor)
{
	int err;

	err = bmi160_enable_sensors(indio_dev, sensors, num_sensor, false);

	return err;
}
EXPORT_SYMBOL(bmi160_buffer_predisable);

int bmi160_buffer_postdisable(struct iio_dev *indio_dev,
		struct bmi160_sensor **sensors, int num_sensor)
{
	int err, err2;

	err = set_fifo_entries(indio_dev, 0);

	err2 = enable_fifo_irq(indio_dev, 0);
	if (err2 < 0 && err == 0)
		err = err2;

	return err;
}
EXPORT_SYMBOL(bmi160_buffer_postdisable);

int bmi160_buffer_probe(struct iio_dev *indio_dev,
		const struct iio_buffer_setup_ops *setup_ops)
{
	int err;
	struct bmi160_data *sdata = iio_priv(indio_dev);
	unsigned int irq = sdata->get_irq(indio_dev);
#ifdef CONFIG_IIO_BMI160_USE_GPIO_IRQ
	int irq_flags = sdata->gpio_active_low ?
			IRQF_TRIGGER_LOW : IRQF_TRIGGER_HIGH;
#endif

#ifdef DEBUG_OPT_IF
	debug_indio_dev = indio_dev;
#endif

	/*
	 * Note that indio_dev->groups has 7 capacity,
	 * 1 for chan_attr(in_accel_x_raw, etc.), 1 for "buffer",
	 * 1 for "scan_elements", 1 for "trigger", 1 for "events"
	 * and 1 for NULL terminate. In here, the rest element is used.
	 */
	indio_dev->groups[indio_dev->groupcounter++] = &attr_group;

	indio_dev->buffer = iio_kfifo_allocate(indio_dev);
	if (!indio_dev->buffer) {
		err = -ENOMEM;
		goto err0;
	}

	sdata->buffer_len = BMI160_FIFO_SIZE;
	sdata->buffer_data = kmalloc(sdata->buffer_len, GFP_KERNEL);
	if (sdata->buffer_data == NULL) {
		err = -ENOMEM;
		goto err1;
	}

	err = validate_chan_order(indio_dev);
	if (err < 0)
		goto err2;

	err = reset_fifo(indio_dev);
	if (err < 0)
		goto err2;

	err = config_fifo_irq(indio_dev, 1);
	if (err < 0)
		goto err2;

	err = request_threaded_irq(irq,	irq_h, worker_h,
#ifdef CONFIG_IIO_BMI160_USE_GPIO_IRQ
			irq_flags, indio_dev->name, indio_dev);
#else
			IRQ_FLAGS, indio_dev->name, indio_dev);
#endif
	if (err < 0)
		goto err2;

	indio_dev->setup_ops = setup_ops;
	indio_dev->modes |= INDIO_BUFFER_HARDWARE;

	err = iio_buffer_register(indio_dev,
				  indio_dev->channels,
				  indio_dev->num_channels);
	if (err < 0)
		goto err3;

	sdata->sensor->threshold = THRESHOLD_DEFAULT;

	return 0;

err3:
	free_irq(irq, indio_dev);
err2:
	kfree(sdata->buffer_data);
err1:
	iio_kfifo_free(indio_dev->buffer);
err0:
	return err;
}
EXPORT_SYMBOL(bmi160_buffer_probe);

void bmi160_buffer_remove(struct iio_dev *indio_dev)
{
	struct bmi160_data *sdata = iio_priv(indio_dev);
	unsigned int irq = sdata->get_irq(indio_dev);

	free_irq(irq, indio_dev);

	mutex_lock(&indio_dev->mlock);

	iio_buffer_unregister(indio_dev);
	config_fifo_irq(indio_dev, 0);
	reset_fifo(indio_dev);
	kfree(sdata->buffer_data);
	iio_kfifo_free(indio_dev->buffer);

	mutex_unlock(&indio_dev->mlock);
}
EXPORT_SYMBOL(bmi160_buffer_remove);

/*
 * debug functions
 */
#ifdef DEBUG_OPT_IF
static int get_int_status(char *val, const struct kernel_param *kp)
{
	int ret;
        u8 status[4];
	struct iio_dev *indio_dev = debug_indio_dev;
	struct bmi160_data *sdata = iio_priv(indio_dev);

	mutex_lock(&indio_dev->mlock);
	ret = sdata->tf->read_multiple_byte(&sdata->tb, sdata->dev,
			BMI160_REG_INT_STATUS_0, sizeof(status), status);
	mutex_unlock(&indio_dev->mlock);
	if (ret < 0)
		goto err0;

	ret = scnprintf(val, PAGE_SIZE, "%02x %02x %02x %02x\n",
			status[0], status[1], status[2], status[3]);
err0:
	return ret;
}

static const struct kernel_param_ops check_int_status_ops = {
	.get = get_int_status,
};

static int get_fifo_cnt(char *val, const struct kernel_param *kp)
{
	int ret;
	struct iio_dev *indio_dev = debug_indio_dev;

	mutex_lock(&indio_dev->mlock);
	ret = read_fifo_count(indio_dev);
	mutex_unlock(&indio_dev->mlock);
	if (ret < 0)
		goto err0;

	ret = scnprintf(val, PAGE_SIZE, "%d\n", ret);
err0:
	return ret;
}

static const struct kernel_param_ops read_fifo_cnt_ops = {
	.get = get_fifo_cnt,
};

static int do_fifo_flush(const char *val, const struct kernel_param *kp)
{
	int ret;
	struct iio_dev *indio_dev = debug_indio_dev;

	mutex_lock(&indio_dev->mlock);
	ret = flush_fifo(indio_dev);
	mutex_unlock(&indio_dev->mlock);

	return ret;
}

static const struct kernel_param_ops fifo_flush_ops = {
	.set = do_fifo_flush
};

module_param_cb(check_int_status, &check_int_status_ops, NULL, 0600);
module_param_cb(fifo_cnt, &read_fifo_cnt_ops, NULL, 0400);
module_param_cb(fifo_flush, &fifo_flush_ops, NULL, 0600);

#endif /* DEBUG_OPT_IF */

MODULE_AUTHOR("Sony Corporation");
MODULE_DESCRIPTION("BMI160 buffer");
MODULE_LICENSE("GPL v2");
