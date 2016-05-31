/*
 * BMI160 library driver
 *
 * Copyright 2014 Sony Corporation
 *
 * Based on include/linux/iio/common/st_sensors.h
 *
 * STMicroelectronics sensors library driver
 *
 * Copyright 2012-2013 STMicroelectronics Inc.
 *
 * Denis Ciocca <denis.ciocca@st.com>
 *
 * Licensed under the GPL-2.
 */

#ifndef BMI160_H
#define BMI160_H

#include <linux/i2c.h>
#include <linux/spi/spi.h>
#include <linux/irqreturn.h>
#include <linux/iio/trigger.h>
#include <linux/bitops.h>

/* Register Map */
#define BMI160_REG_CHIPID		0x0000
/* reserved				0x0001 */
#define BMI160_REG_ERR_REG		0x0002
#define BMI160_REG_PMU_STATUS		0x0003
#define BMI160_REG_DATA_0		0x0004
#define BMI160_REG_DATA_1		0x0005
#define BMI160_REG_DATA_2		0x0006
#define BMI160_REG_DATA_3		0x0007
#define BMI160_REG_DATA_4		0x0008
#define BMI160_REG_DATA_5		0x0009
#define BMI160_REG_DATA_6		0x000a
#define BMI160_REG_DATA_7		0x000b
#define BMI160_REG_DATA_8		0x000c
#define BMI160_REG_DATA_9		0x000d
#define BMI160_REG_DATA_10		0x000e
#define BMI160_REG_DATA_11		0x000f
#define BMI160_REG_DATA_12		0x0010
#define BMI160_REG_DATA_13		0x0011
#define BMI160_REG_DATA_14		0x0012
#define BMI160_REG_DATA_15		0x0013
#define BMI160_REG_DATA_16		0x0014
#define BMI160_REG_DATA_17		0x0015
#define BMI160_REG_DATA_18		0x0016
#define BMI160_REG_DATA_19		0x0017
#define BMI160_REG_SENSORTIME_0		0x0018
#define BMI160_REG_SENSORTIME_1		0x0019
#define BMI160_REG_SENSORTIME_2		0x001a
#define BMI160_REG_STATUS		0x001b
#define BMI160_REG_INT_STATUS_0		0x001c
#define BMI160_REG_INT_STATUS_1		0x001d
#define BMI160_REG_INT_STATUS_2		0x001e
#define BMI160_REG_INT_STATUS_3		0x001f
#define BMI160_REG_TEMPERATURE_0	0x0020
#define BMI160_REG_TEMPERATURE_1	0x0021
#define BMI160_REG_FIFO_LENGTH_0	0x0022
#define BMI160_REG_FIFO_LENGTH_1	0x0023
#define BMI160_REG_FIFO_DATA		0x0024
/* reserved				0x0025 - 0x003f */
#define BMI160_REG_ACC_CONF		0x0040
#define BMI160_REG_ACC_RANGE		0x0041
#define BMI160_REG_GYR_CONF		0x0042
#define BMI160_REG_GYR_RANGE		0x0043
#define BMI160_REG_MAG_CONF		0x0044
#define BMI160_REG_FIFO_DOWNS		0x0045
#define BMI160_REG_FIFO_CONFIG_0	0x0046
#define BMI160_REG_FIFO_CONFIG_1	0x0047
/* reserved				0x0048 - 0x004a */
#define BMI160_REG_MAG_IF_0		0x004b
#define BMI160_REG_MAG_IF_1		0x004c
#define BMI160_REG_MAG_IF_2		0x004d
#define BMI160_REG_MAG_IF_3		0x004e
#define BMI160_REG_MAG_IF_4		0x004f
#define BMI160_REG_INT_EN_0		0x0050
#define BMI160_REG_INT_EN_1		0x0051
#define BMI160_REG_INT_EN_2		0x0052
#define BMI160_REG_INT_OUT_CTRL		0x0053
#define BMI160_REG_INT_LATCH		0x0054
#define BMI160_REG_INT_MAP_0		0x0055
#define BMI160_REG_INT_MAP_1		0x0056
#define BMI160_REG_INT_MAP_2		0x0057
#define BMI160_REG_INT_DATA_0		0x0058
#define BMI160_REG_INT_DATA_1		0x0059
#define BMI160_REG_INT_LOWHIGH_0	0x005a
#define BMI160_REG_INT_LOWHIGH_1	0x005b
#define BMI160_REG_INT_LOWHIGH_2	0x005c
#define BMI160_REG_INT_LOWHIGH_3	0x005d
#define BMI160_REG_INT_LOWHIGH_4	0x005e
#define BMI160_REG_INT_MOTION_0		0x005f
#define BMI160_REG_INT_MOTION_1		0x0060
#define BMI160_REG_INT_MOTION_2		0x0061
#define BMI160_REG_INT_MOTION_3		0x0062
#define BMI160_REG_INT_TAP_0		0x0063
#define BMI160_REG_INT_TAP_1		0x0064
#define BMI160_REG_INT_ORIENT_0		0x0065
#define BMI160_REG_INT_ORIENT_1		0x0066
#define BMI160_REG_INT_FLAT_0		0x0067
#define BMI160_REG_INT_FLAT_1		0x0068
#define BMI160_REG_FOC_CONF		0x0069
#define BMI160_REG_CONF			0x006a
#define BMI160_REG_IF_CONF		0x006b
#define BMI160_REG_PMU_TRIGGER		0x006c
#define BMI160_REG_SELF_TEST		0x006d
/* reserved				0x006e - 0x006f */
#define BMI160_REG_NV_CONF		0x0070
#define BMI160_REG_OFFSET_0		0x0071
#define BMI160_REG_OFFSET_1		0x0072
#define BMI160_REG_OFFSET_2		0x0073
#define BMI160_REG_OFFSET_3		0x0074
#define BMI160_REG_OFFSET_4		0x0075
#define BMI160_REG_OFFSET_5		0x0076
#define BMI160_REG_OFFSET_6		0x0077
#define BMI160_REG_STEPCOUNTER		0x0078
#define BMI160_REG_INT_STEP_DET_CONF	0x007a
/* reserved				0x007b - 0x007d */
#define BMI160_REG_CMD			0x007e
/* reserved				0x007f */

#define BMI160_REG_CHIPID_DEFAULT	0xd0

#define BMI160_REG_PMU_STATUS_ACC_MASK	0x30
#define BMI160_REG_PMU_STATUS_GYR_MASK	0x0c

#define BMI160_PMU_MODE_ACC_SUSPEND	0x00
#define BMI160_PMU_MODE_ACC_NORMAL	0x01
#define BMI160_PMU_MODE_ACC_LOW1	0x02
#define BMI160_PMU_MODE_ACC_LOW2	0x03

#define BMI160_PMU_MODE_GYR_SUSPEND	0x00
#define BMI160_PMU_MODE_GYR_NORMAL	0x01
#define BMI160_PMU_MODE_GYR_FAST	0x03

#define BMI160_REG_STATUS_DRDY_ACC	0x80
#define BMI160_REG_STATUS_DRDY_GYR	0x40

#define BMI160_REG_INT_STATUS_1_FWM_MASK	0x40
#define BMI160_REG_INT_STATUS_1_FFULL_MASK	0x20

#define BMI160_REG_FIFO_LENGTH_SIZE	2

#define BMI160_REG_ACC_CONF_ODR_MASK	0x0f
#define BMI160_REG_ACC_CONF_ODR_25_32HZ	0x01
#define BMI160_REG_ACC_CONF_ODR_25_16HZ	0x02
#define BMI160_REG_ACC_CONF_ODR_25_8HZ	0x03
#define BMI160_REG_ACC_CONF_ODR_25_4HZ	0x04
#define BMI160_REG_ACC_CONF_ODR_25_2HZ	0x05
#define BMI160_REG_ACC_CONF_ODR_25HZ	0x06
#define BMI160_REG_ACC_CONF_ODR_50HZ	0x07
#define BMI160_REG_ACC_CONF_ODR_100HZ	0x08
#define BMI160_REG_ACC_CONF_ODR_200HZ	0x09
#define BMI160_REG_ACC_CONF_ODR_400HZ	0x0a
#define BMI160_REG_ACC_CONF_ODR_800HZ	0x0b
#define BMI160_REG_ACC_CONF_ODR_1600HZ	0x0c
#define BMI160_REG_ACC_CONF_DEFAULT	0x28

#define BMI160_REG_GYR_CONF_ODR_MASK	0x0f
#define BMI160_REG_GYR_CONF_ODR_25_32HZ	0x01
#define BMI160_REG_GYR_CONF_ODR_25_16HZ	0x02
#define BMI160_REG_GYR_CONF_ODR_25_8HZ	0x03
#define BMI160_REG_GYR_CONF_ODR_25_4HZ	0x04
#define BMI160_REG_GYR_CONF_ODR_25_2HZ	0x05
#define BMI160_REG_GYR_CONF_ODR_25HZ	0x06
#define BMI160_REG_GYR_CONF_ODR_50HZ	0x07
#define BMI160_REG_GYR_CONF_ODR_100HZ	0x08
#define BMI160_REG_GYR_CONF_ODR_200HZ	0x09
#define BMI160_REG_GYR_CONF_ODR_400HZ	0x0a
#define BMI160_REG_GYR_CONF_ODR_800HZ	0x0b
#define BMI160_REG_GYR_CONF_ODR_1600HZ	0x0c
#define BMI160_REG_GYR_CONF_ODR_3200HZ	0x0d
#define BMI160_REG_GYR_CONF_DEFAULT	0x28

#define BMI160_REG_ACC_RANGE_MASK	0x0f
#define BMI160_REG_ACC_RANGE_2G		0x03
#define BMI160_REG_ACC_RANGE_4G		0x05
#define BMI160_REG_ACC_RANGE_8G		0x08
#define BMI160_REG_ACC_RANGE_16G	0x0c
#define BMI160_REG_ACC_RANGE_DEFAULT	0x03

#define BMI160_REG_GYR_RANGE_MASK	0x07
#define BMI160_REG_GYR_RANGE_2000DPS	0x00
#define BMI160_REG_GYR_RANGE_1000DPS	0x01
#define BMI160_REG_GYR_RANGE_500DPS	0x02
#define BMI160_REG_GYR_RANGE_250DPS	0x03
#define BMI160_REG_GYR_RANGE_125DPS	0x04
#define BMI160_REG_GYR_RANGE_DEFAULT	0x00

#define BMI160_REG_FIFO_CONFIG_0_UNIT_SIZE	4

#define BMI160_REG_FIFO_CONFIG_1_GYR_EN		0x80
#define BMI160_REG_FIFO_CONFIG_1_ACC_EN		0x40

#define BMI160_REG_INT_EN_1_FWM_EN_MASK		0x40
#define BMI160_REG_INT_EN_1_FFULL_EN_MASK	0x20

#define BMI160_REG_INT_OUT_CTRL_INT1_EN_MASK	0x08
#define BMI160_REG_INT_OUT_CTRL_INT1_LVL_MASK	0x02

#define BMI160_REG_INT_MAP_1_FWM_INT1_MASK	0x40

#define BMI160_REG_CMD_SET_PMU_ACC_SUSPEND	\
	(0x10 + BMI160_PMU_MODE_ACC_SUSPEND)
#define BMI160_REG_CMD_SET_PMU_ACC_NORMAL	\
	(0x10 + BMI160_PMU_MODE_ACC_NORMAL)
#define BMI160_REG_CMD_SET_PMU_ACC_LOW1		\
	(0x10 + BMI160_PMU_MODE_ACC_LOW1)
#define BMI160_REG_CMD_SET_PMU_ACC_LOW2		\
	(0x10 + BMI160_PMU_MODE_ACC_LOW2)
#define BMI160_REG_CMD_SET_PMU_GYR_SUSPEND	\
	(0x14 + BMI160_PMU_MODE_GYR_SUSPEND)
#define BMI160_REG_CMD_SET_PMU_GYR_NORMAL	\
	(0x14 + BMI160_PMU_MODE_GYR_NORMAL)
#define BMI160_REG_CMD_SET_PMU_GYR_FAST		\
	(0x14 + BMI160_PMU_MODE_GYR_FAST)
#define BMI160_REG_CMD_FIFO_FLUSH		0xb0

#define BMI160_IDLE_REG_UPDATE_NORMAL_US	2
#define BMI160_IDLE_REG_UPDATE_OTHERS_US	450

#define BMI160_ACC_STARTUP_MAX_MS	10
#define BMI160_GYR_STARTUP_MAX_MS	80

#define BMI160_FIFO_SIZE		1024

#define BMI160_TX_MAX_LENGTH		2
#define BMI160_RX_MAX_LENGTH		1024

#define BMI160_ODR_LIST_MAX		13
#define BMI160_FULLSCALE_AVL_MAX	5

#define BMI160_BYTE_FOR_CHANNEL		2
#define BMI160_SCAN_X			0
#define BMI160_SCAN_Y			1
#define BMI160_SCAN_Z			2
#define BMI160_NUM_AXES			3
#define BMI160_DATABITS			16
#define BMI160_REALBITS			16
#define BMI160_WAI_ADDRESS		BMI160_REG_CHIPID

enum bmi160_sensor_type {
	BMI160_GYR = 0,
	BMI160_ACC,
	BMI160_NSENSOR_TYPES
};

#define BMI160_HZ_INT_TO_INIT		100
#define BMI160_HZ_FRACT_TO_INIT		0

#define BMI160_HZ_INT_TO_FRACT		100 /* 2 digits of decimal fraction part
					     * are handled. this value is
					     * statically used in the preprocess
					     * phase. */
#define BMI160_HZ_FRACT_FMT		"%02d"

#define BMI160_MAX_NAME			17
#define BMI160_MAX_4WAI			7

#define BMI160_LSM_CHANNELS(device_type, index, mod, endian, bits, addr) \
{ \
	.type = device_type, \
	.modified = 1, \
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) | \
			BIT(IIO_CHAN_INFO_SCALE), \
	.scan_index = index, \
	.channel2 = mod, \
	.address = addr, \
	.scan_type = { \
		.sign = 's', \
		.realbits = bits, \
		.shift = BMI160_DATABITS - bits, \
		.storagebits = BMI160_DATABITS, \
		.endianness = endian, \
	}, \
}

struct bmi160_odr_hz {
	unsigned int integer;
	unsigned int fract;	/* Number of digits of decimal part which is
				 * defined w/ BMI160_HZ_INT_TO_FRACT is stored.
				 */
};

struct bmi160_odr_avl {
	struct bmi160_odr_hz hz;
	u8 value;
};

struct bmi160_odr {
	u8 addr;
	u8 mask;
	u8 def_value;
	struct bmi160_odr_avl odr_avl[BMI160_ODR_LIST_MAX];
};

struct bmi160_power_cntl {
	u8 addr;
	u8 mask;
	u8 value_off;
	u8 value_on;
	unsigned int timeout_off_ms;
	unsigned int timeout_on_ms;
};

struct bmi160_power_stat {
	u8 addr;
	u8 mask;
	u8 value_off;
	u8 value_on;
};

struct bmi160_idle_reg_update {
	unsigned int normal_us;
	unsigned int others_us;
};

struct bmi160_fullscale_avl {
	unsigned int num;
	u8 value;
	unsigned int gain;
};

struct bmi160_fullscale {
	u8 addr;
	u8 mask;
	u8 def_value;
	struct bmi160_fullscale_avl fs_avl[BMI160_FULLSCALE_AVL_MAX];
};

/**
 * struct bmi160_data_ready_irq - BMI160 device data-ready interrupt
 * @addr: address of the register.
 * @mask: mask to write the on/off value.
 * struct ig1 - represents the Interrupt Generator 1 of sensors.
 * @en_addr: address of the enable ig1 register.
 * @en_mask: mask to write the on/off value for enable.
 */
struct bmi160_data_ready_irq {
	u8 addr;
	u8 mask;
	struct {
		u8 en_addr;
		u8 en_mask;
	} ig1;
};

/**
 * struct bmi160_data_ready - BMI160 device data-ready status
 * @addr: address of the register.
 * @mask: mask which indicates data is ready.
 */
struct bmi160_data_ready {
	u8 addr;
	u8 mask;
};

/**
 * struct bmi160_transfer_buffer - BMI160 device I/O buffer
 * @buf_lock: Mutex to protect rx and tx buffers.
 * @tx_buf: Buffer used by SPI transfer function to send data to the sensors.
 *	This buffer is used to avoid DMA not-aligned issue.
 * @rx_buf: Buffer used by SPI transfer to receive data from sensors.
 *	This buffer is used to avoid DMA not-aligned issue.
 * @max_read_len: Max length of multiple read.
 */
struct bmi160_transfer_buffer {
	struct mutex buf_lock;
	u8 rx_buf[BMI160_RX_MAX_LENGTH];
	u8 tx_buf[BMI160_TX_MAX_LENGTH] ____cacheline_aligned;
	size_t max_read_len;
};

/**
 * struct bmi160_transfer_function - BMI160 device I/O function
 * @read_byte: Function used to read one byte.
 * @write_byte: Function used to write one byte.
 * @read_multiple_byte: Function used to read multiple byte.
 */
struct bmi160_transfer_function {
	int (*read_byte) (struct bmi160_transfer_buffer *tb,
				struct device *dev, u8 reg_addr, u8 *res_byte);
	int (*write_byte) (struct bmi160_transfer_buffer *tb,
				struct device *dev, u8 reg_addr, u8 data);
	int (*read_multiple_byte) (struct bmi160_transfer_buffer *tb,
		struct device *dev, u8 reg_addr, int len, u8 *data);
};

/**
 * struct bmi160_sensor_spec - BMI160 spec for one type of sensor
 * @odr: Output data rate register and ODR list available.
 * @pw_cntl: Power controll register of the sensor.
 * @pw_stat: Power status register of the sensor.
 * @fs: Full scale register and full scale list available.
 * @drdy_irq: Data ready IRQ register of the sensor.
 * @drdy: Data ready status register of the sensor.
 */
struct bmi160_sensor_spec {
	struct bmi160_odr odr;
	struct bmi160_power_cntl pw_cntl;
	struct bmi160_power_stat pw_stat;
	struct bmi160_fullscale fs;
	struct bmi160_data_ready_irq drdy_irq;
	struct bmi160_data_ready drdy;
};

/**
 * struct bmi160_sensor - BMI160 spec/status for one type of sensor
 * @spec: Sensor spec.
 * @current_fullscale: Maximum range of measure by the sensor.
 * @enabled: Status of the sensor (false->off, true->on).
 * @keep_enabled: Not to toggle between enabled/disabled.
 */
struct bmi160_sensor {
	struct bmi160_sensor_spec *spec;
	struct bmi160_fullscale_avl *current_fullscale;
	bool enabled;
	bool keep_enabled;
};

/**
 * struct bmi160 - BMI160 sensor device
 * @wai: Contents of WhoAmI register.
 * @sensors_supported: List of supported sensors by struct itself.
 * @ch: IIO channels for the sensors.
 * @sensor: sensor status.
 * @current_odr: Output data rate of the sensor.
 * @idle: Idle time between write accesses.
 * @frame_size: FIFO regular frame size.
 * @threshold: FIFO threshold in number of frames.
 * @sparse_frame: partially axies are enabled.
 */
struct bmi160 {
	u8 wai;
	char sensors_supported[BMI160_MAX_4WAI][BMI160_MAX_NAME];
	struct iio_chan_spec *ch;
	struct bmi160_sensor sensors[BMI160_NSENSOR_TYPES];
	struct bmi160_odr_hz current_hz;
	struct bmi160_idle_reg_update idle;
	size_t frame_size;
	size_t threshold;
	bool sparse_frame;
};

/**
 * struct bmi160_data - BMI160 driver status
 * @dev: Pointer to instance of struct device (I2C or SPI).
 * @trig: The trigger in use by the core driver.
 * @sensor: Pointer to the current sensor struct in use.
 * @buffer_data: Data used by buffer part.
 * @buffer_len: Length of buffer_data.
 * @get_irq: Function to get the IRQ.
 * @tf: Transfer function structure used by I/O operations.
 * @tb: Transfer buffers and mutex used by I/O operations.
 */
struct bmi160_data {
	struct device *dev;
	struct iio_trigger *trig;
	struct bmi160 *sensor;

	char *buffer_data;
	size_t buffer_len;

#ifdef CONFIG_IIO_BMI160_USE_GPIO_IRQ
	int gpio; /* gpio for interrupt line */
	int gpio_active_low;
#endif

	unsigned int (*get_irq) (struct iio_dev *indio_dev);

	const struct bmi160_transfer_function *tf;
	struct bmi160_transfer_buffer tb;
};

#if defined(CONFIG_IIO_TRIGGERED_BUFFER)
irqreturn_t bmi160_trigger_handler(int irq, void *p);

int bmi160_get_buffer_element(struct iio_dev *indio_dev, u8 *buf);

int bmi160_allocate_trigger(struct iio_dev *indio_dev,
				const struct iio_trigger_ops *trigger_ops);

void bmi160_deallocate_trigger(struct iio_dev *indio_dev);

#elif defined(CONFIG_IIO_BUFFER)
int bmi160_buffer_preenable(struct iio_dev *indio_dev,
		struct bmi160_sensor **sensors, int num_sensor);

int bmi160_buffer_postenable(struct iio_dev *indio_dev,
		struct bmi160_sensor **sensors, int num_sensor);

int bmi160_buffer_predisable(struct iio_dev *indio_dev,
		struct bmi160_sensor **sensors, int num_sensor);

int bmi160_buffer_postdisable(struct iio_dev *indio_dev,
		struct bmi160_sensor **sensors, int num_sensor);

int bmi160_enable_sensors(struct iio_dev *indio_dev,
		struct bmi160_sensor **sensors, int num_sensor,
		bool enable);

int bmi160_buffer_probe(struct iio_dev *indio_dev,
		const struct iio_buffer_setup_ops *setup_ops);

void bmi160_buffer_remove(struct iio_dev *indio_dev);

int bmi160_write_data(struct iio_dev *indio_dev, bool enabled,
		u8 reg_addr, u8 data);

int bmi160_write_data_with_mask(struct iio_dev *indio_dev, bool enabled,
		u8 reg_addr, u8 mask, u8 data);
#else
static inline int bmi160_allocate_trigger(struct iio_dev *indio_dev,
				const struct iio_trigger_ops *trigger_ops)
{
	return 0;
}
static inline void bmi160_deallocate_trigger(struct iio_dev *indio_dev)
{
	return;
}
#endif

int bmi160_init_sensor(struct iio_dev *indio_dev, struct bmi160_sensor *sensor,
		struct bmi160_odr_hz *hz);

unsigned int bmi160_get_clock_us(struct iio_dev *indio_dev);

int bmi160_match_odr_hz(struct iio_dev *indio_dev,
		struct bmi160_sensor *sensor,
		unsigned int hz_integer, unsigned int hz_fract,
		int *index_odr_avl);

int bmi160_match_odr_value(struct iio_dev *indio_dev,
		struct bmi160_sensor *sensor, u8 odr_value, int *index_odr_avl);

int bmi160_match_fs_value(struct iio_dev *indio_dev,
		struct bmi160_sensor *sensor, u8 fs_value, int *index_fs_avl);

int bmi160_set_fullscale_by_gain(struct iio_dev *indio_dev,
		struct bmi160_sensor *sensor, int scale);

int bmi160_read_info_raw(struct iio_dev *indio_dev,
		struct bmi160_sensor *sensor, struct iio_chan_spec const *ch,
		int *val);

#ifdef CONFIG_IIO_BMI160_DIRECT_BURST_READ
int bmi160_read_info_raw_burst(struct iio_dev *indio_dev,
		struct bmi160_sensor **sensors, int num_sensor,
		struct iio_chan_spec const **chans, int num_ch, int *vals);

int bmi160_set_burst_read_chans_to_buf(struct iio_dev *indio_dev,
		struct iio_chan_spec const **chans_show_ordered,
		struct iio_chan_spec const **chans_addr_ordered,
		int *values, int num, char *buf, size_t size);
#endif

int bmi160_enable_sensors(struct iio_dev *indio_dev,
		struct bmi160_sensor **sensors, int num_sensor,
		bool enable);

int bmi160_wait_sensors_dataready(struct iio_dev *indio_dev,
		struct bmi160_sensor **sensors, int num_sensor);

int bmi160_check_device_support(struct iio_dev *indio_dev,
			int num_sensors_list, const struct bmi160 *sensors);

ssize_t bmi160_sysfs_get_sampling_frequency(struct device *dev,
				struct device_attribute *attr, char *buf);

ssize_t bmi160_sysfs_set_sampling_frequency(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size,
		struct bmi160_sensor *sensors, int num_sensor);

ssize_t bmi160_sysfs_sampling_frequency_avail(struct device *dev,
		struct device_attribute *attr, char *buf,
		struct bmi160_sensor *sensors, int num_sensor);

ssize_t bmi160_sysfs_scale_avail(struct device *dev,
				struct device_attribute *attr, char *buf,
				struct bmi160_sensor *sensor);

#endif /* BMI160_H */
