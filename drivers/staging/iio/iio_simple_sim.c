/**
 * IIO simulator module.
 *
 * Copyright 2014 Sony Corporation
 *
 * Based on drivers/staging/iio/iio_simple_dummy.c
 *
 * Copyright (c) 2011 Jonathan Cameron
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * A reference industrial I/O driver to illustrate the functionality available.
 *
 * There are numerous real drivers to illustrate the finer points.
 * The purpose of this driver is to provide a driver with far more comments
 * and explanatory notes than any 'real' driver would have.
 * Anyone starting out writing an IIO driver should first make sure they
 * understand all of this driver except those bits specifically marked
 * as being present to allow us to 'fake' the presence of hardware.
 */
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/moduleparam.h>

#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>
#include <linux/iio/events.h>
#include <linux/iio/buffer.h>
#include "iio_simple_sim.h"

/*
 * A few elements needed to fake a bus for this driver
 * Note instances parameter controls how many of these
 * dummy devices are registered.
 */
static unsigned instances = 1;
module_param(instances, int, 0);

/* Pointer array used to fake bus elements */
static struct iio_dev **iio_sim_devs;

/* Fake a name for the part number, usually obtained from the id table */
static const char *iio_sim_part_number = "iio_sim_part_no";

/*
 * FIXME: For quick implementation, variable of sim_values is declared here to
 * simulate sensor data, instead of modifying st->accel_val and so on.
 * To implement with utilizing st->accel_val, we should implement callbacks with
 * module_param_call for sysfs set/get handling, or others.
 */
static s32 sim_values[] = {
	[accelx] = 0,
	[accely] = 0,
	[accelz] = 0,
	[gyrox] = 0,
	[gyroy] = 0,
	[gyroz] = 0,
};
module_param_named(accel_x, sim_values[accelx], int, S_IRUSR | S_IWUSR);
module_param_named(accel_y, sim_values[accely], int, S_IRUSR | S_IWUSR);
module_param_named(accel_z, sim_values[accelz], int, S_IRUSR | S_IWUSR);
module_param_named(gyro_x, sim_values[gyrox], int, S_IRUSR | S_IWUSR);
module_param_named(gyro_y, sim_values[gyroy], int, S_IRUSR | S_IWUSR);
module_param_named(gyro_z, sim_values[gyroz], int, S_IRUSR | S_IWUSR);

/**
 * struct iio_sim_scale - realworld to register mapping
 * @val: first value in read_raw - here integer part.
 * @val2: second value in read_raw etc - here micro part.
 */
struct iio_sim_scale {
	int val;
	int val2;
};

static struct iio_sim_scale sim_scales[] = {
	[accelx] = { 1, 0 }, /* 1.000000 */
	[accely] = { 1, 0 }, /* 1.000000 */
	[accelz] = { 1, 0 }, /* 1.000000 */
	[gyrox] = { 1, 0 }, /* 1.000000 */
	[gyroy] = { 1, 0 }, /* 1.000000 */
	[gyroz] = { 1, 0 }, /* 1.000000 */
};

/**
 * struct iio_sim_calibscale - realworld to register mapping
 * @val: first value in read_raw - here integer part.
 * @val2: second value in read_raw etc - here micro part.
 * @regval: register value - magic device specific numbers.
 */
struct iio_sim_calibscale {
	int val;
	int val2;
	int regval; /* what would be written to hardware */
};

static struct iio_sim_calibscale sim_calibscales[] = {
	[accelx] = { 1, 0, 0x0 }, /* 1.000000 */
	[accely] = { 1, 0, 0x0 }, /* 1.000000 */
	[accelz] = { 1, 0, 0x0 }, /* 1.000000 */
	[gyrox] = { 1, 0, 0x0 }, /* 1.000000 */
	[gyroy] = { 1, 0, 0x0 }, /* 1.000000 */
	[gyroz] = { 1, 0, 0x0 }, /* 1.000000 */
};

#define IIO_CHAN_SOFT_ACCEL(ch, index)					\
	{								\
		.type = IIO_ACCEL,					\
		.modified = 1,						\
		/* Channel 2 is use for modifiers */			\
		.channel2 = ch,						\
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |		\
		/*							\
		 * Internal bias and gain correction values. Applied	\
		 * by the hardware or driver prior to userspace		\
		 * seeing the readings. Typically part of hardware	\
		 * calibration.						\
		 */							\
		BIT(IIO_CHAN_INFO_CALIBSCALE) |				\
		BIT(IIO_CHAN_INFO_CALIBBIAS) |				\
		BIT(IIO_CHAN_INFO_SCALE),				\
		.scan_index = index,					\
		.scan_type = { /* Description of storage in buffer */	\
			.sign = 's', /* signed */			\
			.realbits = 32, /* 32 bits */			\
			.storagebits = 32, /* 32 bits used for storage */ \
			.shift = 0, /* zero shift */			\
		},							\
	}								\

#define IIO_CHAN_SOFT_GYRO(ch, index)					\
	{								\
		.type = IIO_ANGL_VEL,					\
		.modified = 1,						\
		/* Channel 2 is use for modifiers */			\
		.channel2 = ch,						\
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |		\
		/*							\
		 * Internal bias and gain correction values. Applied	\
		 * by the hardware or driver prior to userspace		\
		 * seeing the readings. Typically part of hardware	\
		 * calibration.						\
		 */							\
		BIT(IIO_CHAN_INFO_CALIBSCALE) |				\
		BIT(IIO_CHAN_INFO_CALIBBIAS) |				\
		BIT(IIO_CHAN_INFO_SCALE),				\
		.scan_index = index,					\
		.scan_type = { /* Description of storage in buffer */	\
			.sign = 's', /* signed */			\
			.realbits = 32, /* 32 bits */			\
			.storagebits = 32, /* 32 bits used for storage */ \
			.shift = 0, /* zero shift */			\
		},							\
	}								\

/*
 * iio_sim_channels - Description of available channels
 *
 * This array of structures tells the IIO core about what the device
 * actually provides for a given channel.
 */
static const struct iio_chan_spec iio_sim_channels[] = {
	/*
	 * 'modified' (i.e. axis specified) acceleration channel
	 * in_accel_z_raw
	 */
	IIO_CHAN_SOFT_ACCEL(IIO_MOD_X, accelx),
	IIO_CHAN_SOFT_ACCEL(IIO_MOD_Y, accely),
	IIO_CHAN_SOFT_ACCEL(IIO_MOD_Z, accelz),
	/*
	 * gyro channels
	 */
	IIO_CHAN_SOFT_GYRO(IIO_MOD_X, gyrox),
	IIO_CHAN_SOFT_GYRO(IIO_MOD_Y, gyroy),
	IIO_CHAN_SOFT_GYRO(IIO_MOD_Z, gyroz),
	/*
	 * Convenience macro for timestamps. "gyroz + 1" is the index in
	 * the buffer.
	 */
	IIO_CHAN_SOFT_TIMESTAMP(gyroz + 1),
};

int iio_sim_get_sim_value(struct iio_dev *indio_dev, int index, s32 *value)
{
	if (indio_dev == NULL)
		return -EINVAL;
	if (index < 0 || sizeof(sim_values) / sizeof(s32) <= index)
		return -EINVAL;
	if (value == NULL)
		return -EINVAL;

	*value = sim_values[index];

	return 0;
}

/**
 * iio_sim_read_raw() - data read function.
 * @indio_dev:	the struct iio_dev associated with this device instance
 * @chan:	the channel whose data is to be read
 * @val:	first element of returned value (typically INT)
 * @val2:	second element of returned value (typically MICRO)
 * @mask:	what we actually want to read as per the info_mask_*
 *		in iio_chan_spec.
 */
static int iio_sim_read_raw(struct iio_dev *indio_dev,
			      struct iio_chan_spec const *chan,
			      int *val,
			      int *val2,
			      long mask)
{
	struct iio_sim_state *st = iio_priv(indio_dev);
	int ret = -EINVAL;

	mutex_lock(&st->lock);
	switch (mask) {
	case IIO_CHAN_INFO_RAW: /* magic value - channel value read */
		switch (chan->type) {
		case IIO_ACCEL:
		case IIO_ANGL_VEL:
			*val = sim_values[chan->scan_index];
			ret = IIO_VAL_INT;
			break;
		default:
			break;
		}
		break;
	case IIO_CHAN_INFO_OFFSET:
		/* only single ended adc -> 7 */
		*val = 7;
		ret = IIO_VAL_INT;
		break;
	case IIO_CHAN_INFO_SCALE:
		switch (chan->type) {
		case IIO_ACCEL:
		case IIO_ANGL_VEL:
			*val = st->scales[chan->scan_index].val;
			*val2 = st->scales[chan->scan_index].val2;
			ret = IIO_VAL_INT_PLUS_MICRO;
			break;
		default:
			break;
		}
		break;
	case IIO_CHAN_INFO_CALIBBIAS:
		/* only the acceleration axis - read from cache */
		*val = st->accel_calibbias;
		ret = IIO_VAL_INT;
		break;
	case IIO_CHAN_INFO_CALIBSCALE:
		switch (chan->type) {
		case IIO_ACCEL:
		case IIO_ANGL_VEL:
			*val = st->calibscales[chan->scan_index].val;
			*val2 = st->calibscales[chan->scan_index].val2;
			ret = IIO_VAL_INT_PLUS_MICRO;
			break;
		default:
			break;
		}
		break;
	default:
		break;
	}
	mutex_unlock(&st->lock);
	return ret;
}

/**
 * iio_sim_write_raw() - data write function.
 * @indio_dev:	the struct iio_dev associated with this device instance
 * @chan:	the channel whose data is to be written
 * @val:	first element of value to set (typically INT)
 * @val2:	second element of value to set (typically MICRO)
 * @mask:	what we actually want to write as per the info_mask_*
 *		in iio_chan_spec.
 *
 * Note that all raw writes are assumed IIO_VAL_INT and info mask elements
 * are assumed to be IIO_INT_PLUS_MICRO unless the callback write_raw_get_fmt
 * in struct iio_info is provided by the driver.
 */
static int iio_sim_write_raw(struct iio_dev *indio_dev,
			       struct iio_chan_spec const *chan,
			       int val,
			       int val2,
			       long mask)
{
	int ret = 0;
	struct iio_sim_state *st = iio_priv(indio_dev);

	mutex_lock(&st->lock);
	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		if (chan->output == 0)
			ret = -EINVAL;
		break;
	case IIO_CHAN_INFO_SCALE:
		switch (chan->type) {
		case IIO_ACCEL:
		case IIO_ANGL_VEL:
			st->scales[chan->scan_index].val = val;
			st->scales[chan->scan_index].val2 = val2;
			break;
		default:
			break;
		}
		break;
	case IIO_CHAN_INFO_CALIBSCALE:
		switch (chan->type) {
		case IIO_ACCEL:
		case IIO_ANGL_VEL:
			st->calibscales[chan->scan_index].val = val;
			st->calibscales[chan->scan_index].val2 = val2;
			break;
		default:
			break;
		}
		break;
	case IIO_CHAN_INFO_CALIBBIAS:
		st->accel_calibbias = val;
		break;

	default:
		ret = -EINVAL;
	}
	mutex_unlock(&st->lock);

	return ret;
}

/*
 * Device type specific information.
 */
static const struct iio_info iio_sim_info = {
	.driver_module = THIS_MODULE,
	.read_raw = &iio_sim_read_raw,
	.write_raw = &iio_sim_write_raw,
#ifdef CONFIG_IIO_SIMPLE_SIM_EVENTS
	.read_event_config = &iio_simple_sim_read_event_config,
	.write_event_config = &iio_simple_sim_write_event_config,
	.read_event_value = &iio_simple_sim_read_event_value,
	.write_event_value = &iio_simple_sim_write_event_value,
#endif /* CONFIG_IIO_SIMPLE_SIM_EVENTS */
};

/**
 * iio_sim_init_device() - device instance specific init
 * @indio_dev: the iio device structure
 *
 * Most drivers have one of these to set up default values,
 * reset the device to known state etc.
 */
static int iio_sim_init_device(struct iio_dev *indio_dev)
{
	struct iio_sim_state *st = iio_priv(indio_dev);

	st->scales = sim_scales;
	st->calibscales = sim_calibscales;

	return 0;
}

/**
 * iio_sim_probe() - device instance probe
 * @index: an id number for this instance.
 *
 * Arguments are bus type specific.
 * I2C: iio_sim_probe(struct i2c_client *client,
 *                      const struct i2c_device_id *id)
 * SPI: iio_sim_probe(struct spi_device *spi)
 */
static int iio_sim_probe(int index)
{
	int ret;
	struct iio_dev *indio_dev;
	struct iio_sim_state *st;

	/*
	 * Allocate an IIO device.
	 *
	 * This structure contains all generic state
	 * information about the device instance.
	 * It also has a region (accessed by iio_priv()
	 * for chip specific state information.
	 */
	indio_dev = iio_device_alloc(sizeof(*st));
	if (indio_dev == NULL) {
		ret = -ENOMEM;
		goto error_ret;
	}

	st = iio_priv(indio_dev);
	mutex_init(&st->lock);

	iio_sim_init_device(indio_dev);
	/*
	 * With hardware: Set the parent device.
	 * indio_dev->dev.parent = &spi->dev;
	 * indio_dev->dev.parent = &client->dev;
	 */

	 /*
	 * Make the iio_dev struct available to remove function.
	 * Bus equivalents
	 * i2c_set_clientdata(client, indio_dev);
	 * spi_set_drvdata(spi, indio_dev);
	 */
	iio_sim_devs[index] = indio_dev;


	/*
	 * Set the device name.
	 *
	 * This is typically a part number and obtained from the module
	 * id table.
	 * e.g. for i2c and spi:
	 *    indio_dev->name = id->name;
	 *    indio_dev->name = spi_get_device_id(spi)->name;
	 */
	indio_dev->name = iio_sim_part_number;

	/* Provide description of available channels */
	indio_dev->channels = iio_sim_channels;
	indio_dev->num_channels = ARRAY_SIZE(iio_sim_channels);

	/*
	 * Provide device type specific interface functions and
	 * constant data.
	 */
	indio_dev->info = &iio_sim_info;

	/* Specify that device provides sysfs type interfaces */
	indio_dev->modes = INDIO_DIRECT_MODE;

	ret = iio_simple_sim_events_register(indio_dev);
	if (ret < 0)
		goto error_free_device;

	/*
	 * Configure buffered capture support and register the channels with the
	 * buffer, but avoid the output channel being registered by reducing the
	 * number of channels by 1.
	 */
	ret = iio_simple_sim_configure_buffer(indio_dev, iio_sim_channels,
			ARRAY_SIZE(iio_sim_channels));
	if (ret < 0)
		goto error_unregister_events;

	ret = iio_device_register(indio_dev);
	if (ret < 0)
		goto error_unconfigure_buffer;

	return 0;
error_unconfigure_buffer:
	iio_simple_sim_unconfigure_buffer(indio_dev);
error_unregister_events:
	iio_simple_sim_events_unregister(indio_dev);
error_free_device:
	iio_device_free(indio_dev);
error_ret:
	return ret;
}

/**
 * iio_sim_remove() - device instance removal function
 * @index: device index.
 *
 * Parameters follow those of iio_sim_probe for buses.
 */
static int iio_sim_remove(int index)
{
	int ret;
	/*
	 * Get a pointer to the device instance iio_dev structure
	 * from the bus subsystem. E.g.
	 * struct iio_dev *indio_dev = i2c_get_clientdata(client);
	 * struct iio_dev *indio_dev = spi_get_drvdata(spi);
	 */
	struct iio_dev *indio_dev = iio_sim_devs[index];


	/* Unregister the device */
	iio_device_unregister(indio_dev);

	/* Device specific code to power down etc */

	/* Buffered capture related cleanup */
	iio_simple_sim_unconfigure_buffer(indio_dev);

	ret = iio_simple_sim_events_unregister(indio_dev);
	if (ret)
		goto error_ret;

	/* Free all structures */
	iio_device_free(indio_dev);

error_ret:
	return ret;
}

/**
 * iio_sim_init() -  device driver registration
 *
 * Varies depending on bus type of the device. As there is no device
 * here, call probe directly. For information on device registration
 * i2c:
 * Documentation/i2c/writing-clients
 * spi:
 * Documentation/spi/spi-summary
 */
static __init int iio_sim_init(void)
{
	int i, ret;
	if (instances > 10) {
		instances = 1;
		return -EINVAL;
	}

	/* Fake a bus */
	iio_sim_devs = kcalloc(instances, sizeof(*iio_sim_devs),
				 GFP_KERNEL);
	/* Here we have no actual device so call probe */
	for (i = 0; i < instances; i++) {
		ret = iio_sim_probe(i);
		if (ret < 0)
			return ret;
	}
	return 0;
}
module_init(iio_sim_init);

/**
 * iio_sim_exit() - device driver removal
 *
 * Varies depending on bus type of the device.
 * As there is no device here, call remove directly.
 */
static __exit void iio_sim_exit(void)
{
	int i;
	for (i = 0; i < instances; i++)
		iio_sim_remove(i);
	kfree(iio_sim_devs);
}
module_exit(iio_sim_exit);

MODULE_AUTHOR("Jonathan Cameron <jic23@kernel.org>");
MODULE_DESCRIPTION("IIO simulator driver");
MODULE_LICENSE("GPL v2");
