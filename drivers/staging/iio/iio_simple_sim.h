/**
 * IIO simulator module.
 *
 * Copyright 2014 Sony Corporation
 *
 * Based on drivers/staging/iio/iio_simple_dummy.h
 *
 * Copyright (c) 2011 Jonathan Cameron
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * Join together the various functionality of iio_simple_dummy driver
 */

#include <linux/kernel.h>

struct iio_sim_accel_calibscale;

/**
 * struct iio_sim_state - device instance specific state.
 * @accel_val:			cache for acceleration value
 * @accel_calibbias:		cache for acceleration calibbias
 * @accel_calibscale:		cache for acceleration calibscale
 * @lock:			lock to ensure state is consistent
 * @event_irq:			irq number for event line (faked)
 * @event_val:			cache for event theshold value
 * @event_en:			cache of whether event is enabled
 */
struct iio_sim_state {
	int accel_calibbias;
	struct iio_sim_scale *scales;
	struct iio_sim_calibscale *calibscales;
	struct mutex lock;
#ifdef CONFIG_IIO_SIMPLE_SIM_EVENTS
	int event_irq;
	int event_val;
	bool event_en;
#endif /* CONFIG_IIO_SIMPLE_SIM_EVENTS */
};

int iio_sim_get_sim_value(struct iio_dev *indio_dev, int channel, s32 *value);

#ifdef CONFIG_IIO_SIMPLE_SIM_EVENTS

struct iio_dev;

int iio_simple_sim_read_event_config(struct iio_dev *indio_dev,
				       u64 event_code);

int iio_simple_sim_write_event_config(struct iio_dev *indio_dev,
					u64 event_code,
					int state);

int iio_simple_sim_read_event_value(struct iio_dev *indio_dev,
				      u64 event_code,
				      int *val);

int iio_simple_sim_write_event_value(struct iio_dev *indio_dev,
				       u64 event_code,
				       int val);

int iio_simple_sim_events_register(struct iio_dev *indio_dev);
int iio_simple_sim_events_unregister(struct iio_dev *indio_dev);

#else /* Stubs for when events are disabled at compile time */

static inline int
iio_simple_sim_events_register(struct iio_dev *indio_dev)
{
	return 0;
};

static inline int
iio_simple_sim_events_unregister(struct iio_dev *indio_dev)
{
	return 0;
};

#endif /* CONFIG_IIO_SIMPLE_SIM_EVENTS*/

/**
 * enum iio_simple_sim_scan_elements - scan index enum
 * @voltage0:		the single ended voltage channel
 * @diffvoltage1m2:	first differential channel
 * @diffvoltage3m4:	second differenial channel
 * @accelx:		acceleration channel
 *
 * Enum provides convenient numbering for the scan index.
 */
enum iio_simple_sim_scan_elements {
	accelx,
	accely,
	accelz,
	gyrox,
	gyroy,
	gyroz,
};

#ifdef CONFIG_IIO_SIMPLE_SIM_BUFFER
int iio_simple_sim_configure_buffer(struct iio_dev *indio_dev,
	const struct iio_chan_spec *channels, unsigned int num_channels);
void iio_simple_sim_unconfigure_buffer(struct iio_dev *indio_dev);
#else
static inline int iio_simple_sim_configure_buffer(struct iio_dev *indio_dev,
	const struct iio_chan_spec *channels, unsigned int num_channels)
{
	return 0;
};
static inline
void iio_simple_sim_unconfigure_buffer(struct iio_dev *indio_dev)
{};
#endif /* CONFIG_IIO_SIMPLE_SIM_BUFFER */
