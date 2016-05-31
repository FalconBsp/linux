/**
 * IIO simulator module.
 *
 * Copyright 2014 Sony Corporation
 *
 * Based on drivers/staging/iio/iio_simple_dummy_buffer.c
 *
 * Copyright (c) 2011 Jonathan Cameron
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * Buffer handling elements of industrial I/O reference driver.
 * Uses the kfifo buffer.
 *
 * To test without hardware use the sysfs trigger.
 */

#include <linux/kernel.h>
#include <linux/export.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/bitmap.h>
#include <linux/module.h>
#include <linux/moduleparam.h>

#include <linux/iio/iio.h>
#include <linux/iio/trigger_consumer.h>
#include <linux/iio/kfifo_buf.h>

#include "iio_simple_sim.h"

#if 0
#define PR_DBG(fmt, ...) pr_info("[%s:%d] " fmt "\n", \
				 __func__, __LINE__, __VA_ARGS__)
#else
#define PR_DBG(fmt, ...) do {} while (0)
#endif

#ifdef CONFIG_IIO_SIMPLE_SIM_HARDWARE_BUFFER_MODE
#define BUFFER_MODE INDIO_BUFFER_HARDWARE

#ifdef CONFIG_IIO_SIMPLE_SIM_HARDWARE_BUFFER_SIZE
#define FIFO_SIZE CONFIG_IIO_SIMPLE_SIM_HARDWARE_BUFFER_SIZE
#else
#define FIFO_SIZE 4096
#endif

#define FIFO_IN32(FIFO, VALUE)  kfifo_in((FIFO), (VALUE), sizeof(s32))
#define FIFO_IN64(FIFO, VALUE)  kfifo_in((FIFO), (VALUE), sizeof(s64))
#define FIFO_OUT32(FIFO, VALUE) kfifo_out((FIFO), (VALUE), sizeof(s32))
#define FIFO_OUT64(FIFO, VALUE) kfifo_out((FIFO), (VALUE), sizeof(s64))

struct iio_sim {
	struct iio_dev *dev;
	struct kfifo fifo;
	size_t fifo_size;
	u8 *buf;
};
static struct iio_sim g_iio_sim;

static struct iio_sim *get_iio_sim(void)
{
	return &g_iio_sim;
}

#else
#define BUFFER_MODE INDIO_BUFFER_TRIGGERED

#endif

/*
 * Timestamp of IIO is desigend as s64, but module_param can handle ulong
 * at a max.
 *     g_timestamp    : lower 32 bits of timestamp value.
 *     g_timestamp_hi : higher 32 bits of timestamp value.
 */
static u32 g_timestamp = 0;
static s32 g_timestamp_hi = 0;

module_param_named(timestamp, g_timestamp, uint, S_IRUSR | S_IWUSR);
module_param_named(timestamp_hi, g_timestamp_hi, int, S_IRUSR | S_IWUSR);

static int iio_simple_put_sim_values_to(struct iio_dev *indio_dev,
#if BUFFER_MODE == INDIO_BUFFER_HARDWARE
				  struct kfifo *fifo
#else
				  s32 *data
#endif
				  )
{
	int len = 0;
	s32 sim_value;
	s64 timestamp;
	int ret;

	if (!bitmap_empty(indio_dev->active_scan_mask, indio_dev->masklength)) {
		/*
		 * Three common options here:
		 * hardware scans: certain combinations of channels make
		 *   up a fast read.  The capture will consist of all of them.
		 *   Hence we just call the grab data function and fill the
		 *   buffer without processing.
		 * software scans: can be considered to be random access
		 *   so efficient reading is just a case of minimal bus
		 *   transactions.
		 * software culled hardware scans:
		 *   occasionally a driver may process the nearest hardware
		 *   scan to avoid storing elements that are not desired. This
		 *   is the fiddliest option by far.
		 * Here let's pretend we have random access. And the values are
		 * in the constant table fakedata.
		 */
		int i, j;
		for (i = 0, j = 0;
		     i < bitmap_weight(indio_dev->active_scan_mask,
				       indio_dev->masklength);
		     i++, j++) {
			j = find_next_bit(indio_dev->active_scan_mask,
					  indio_dev->masklength, j);
			/* random access read from the 'device' */
			ret = iio_sim_get_sim_value(indio_dev, j, &sim_value);
			if (ret < 0)
				goto out;
			PR_DBG("sim_value : 0x%x", sim_value);
#if BUFFER_MODE == INDIO_BUFFER_HARDWARE
			ret = FIFO_IN32(fifo, &sim_value);
			if (ret == 0) {
				ret = -ENOBUFS;
				goto out;
			}
			ret = 0;
#else
			data[i] = sim_value;
#endif
			len += sizeof(sim_value);
		}
	}
	/* Store the timestamp at an 8 byte aligned offset */
	if (indio_dev->scan_timestamp) {
		timestamp = ((s64) g_timestamp_hi << 32) | g_timestamp;
#if BUFFER_MODE == INDIO_BUFFER_HARDWARE
		if (len % sizeof(s64)) {
			s32 pad = 0xffffffff;

			PR_DBG("pad : 0x%x", pad);
			ret = FIFO_IN32(fifo, &pad);
			if (ret == 0) {
				ret = -ENOBUFS;
				goto out;
			}
		}
		PR_DBG("timestamp : %lld", timestamp);
		ret = FIFO_IN64(fifo, &timestamp);
		if (ret == 0) {
			ret = -ENOBUFS;
			goto out;
		}
		ret = 0;
#else
		PR_DBG("timestamp : %lld", timestamp);
		*(s64 *)((u8 *)data + ALIGN(len, sizeof(s64))) = timestamp;
#endif
	}
out:
	return ret;
}

#if BUFFER_MODE == INDIO_BUFFER_HARDWARE

/* Number of enabled sensors. */
static int iio_sim_get_num_enabled_sensors(struct iio_dev *indio_dev)
{
	return bitmap_weight(indio_dev->active_scan_mask,
			     indio_dev->masklength);
}

static int iio_sim_put_sim_values_to_fifo(struct iio_dev *indio_dev,
					  struct kfifo *fifo)
{
	return iio_simple_put_sim_values_to(indio_dev, fifo);
}

static int iio_sim_get_fifo_all(struct iio_dev *indio_dev, struct kfifo *fifo,
				s32 *data, size_t len)
{
	s32 val;
	s64 timestamp;
	int nsensor;
	int cnt;
	int err;
	int i;

	nsensor = iio_sim_get_num_enabled_sensors(indio_dev);

	cnt = 0;
	while (kfifo_len(fifo) >= indio_dev->scan_bytes) {
		if (len < indio_dev->scan_bytes) {
			pr_warn("buffer size is not enough, %u bytes remains",
				kfifo_len(fifo));
			goto out;
		}

		for (i = 0; i < nsensor; i++) {
			err = FIFO_OUT32(fifo, &val);
			if (err == 0) {
				pr_warn("Something wrong, FIFO is empty"
					" though element is needed\n");
				goto out;
			}
			PR_DBG("val : 0x%x", val);
			*data++ = val;
			len -= sizeof(s32);
		}
		if (indio_dev->scan_timestamp) {
			/* Store the timestamp at an 8 byte aligned offset */
			if (nsensor % (sizeof(s64) / sizeof(s32))) {
				err = FIFO_OUT32(fifo, &val);
				if (err == 0) {
					pr_warn("Something wrong, FIFO is empty"
						" though element is needed\n");
					goto out;
				}
				PR_DBG("pad : 0x%x", val);
				data++;
				len -= sizeof(s32);
			}

			err = FIFO_OUT64(fifo, &timestamp);
			if (err == 0) {
				pr_warn("Something wrong, FIFO is empty"
					" though element is needed\n");
				goto out;
			}
			PR_DBG("timestamp : %lld", timestamp);
			*(s64 *) data = timestamp;
			data += sizeof(s64) / sizeof(s32);
			len -= sizeof(s64);
		}
		cnt++;
	}

out:
	return cnt;
}

static int iio_sim_alloc_fifo_buffer(struct iio_sim *iio_sim)
{
	int ret;

	iio_sim->fifo_size = FIFO_SIZE;

	ret = kfifo_alloc(&iio_sim->fifo, iio_sim->fifo_size, GFP_KERNEL);
	if (ret < 0)
		goto err0;

	iio_sim->buf = kmalloc(iio_sim->fifo_size, GFP_KERNEL);
	if (iio_sim->buf == NULL) {
		ret = -ENOMEM;
		goto err1;
	}

	return 0;

err1:
	kfifo_free(&iio_sim->fifo);
err0:
	return ret;
}

static void iio_sim_free_fifo_buffer(struct iio_sim *iio_sim)
{
	kfree(iio_sim->buf);
	kfifo_free(&iio_sim->fifo);
}

/* iio_buffer_is_active() is cloned from drivers/iio/industrialio-buffer.c,
 * to avoid modifying IIO core. */
static bool iio_buffer_is_active(struct iio_dev *indio_dev,
				 struct iio_buffer *buf)
{
	struct list_head *p;

	list_for_each(p, &indio_dev->buffer_list)
		if (p == &buf->buffer_list)
			return true;

	return false;
}

static int iio_syn_set(const char *str, struct kernel_param *param)
{
	struct iio_sim *iio_sim = get_iio_sim();
	struct iio_dev *indio_dev = iio_sim->dev;
	unsigned int usr_buf_len;
	int cnt;
	int ret;
	int i;

	mutex_lock(&indio_dev->mlock);

	if (!iio_buffer_is_active(indio_dev, indio_dev->buffer)) {
		ret = 0;
		goto out;
	}

	/* put data to local fifo */
	ret = iio_sim_put_sim_values_to_fifo(indio_dev, &iio_sim->fifo);
	if (ret == -ENOBUFS) {
		pr_warn("Some data lost because FIFO is full\n");
		/* continue followings */
		ret = 0;
	} else if (ret < 0) {
		pr_err("Failed on iio_sim_put_sim_values_to_fifo(), "
		       "err = %d\n", ret);
		goto out;
	}

	usr_buf_len = indio_dev->buffer->access->get_length(indio_dev->buffer);
	PR_DBG("usr_buf_len : %u", usr_buf_len);
	PR_DBG("kfifo_len/kfifo_size : %u/%u",
			kfifo_len(&iio_sim->fifo),
			kfifo_size(&iio_sim->fifo));
	/* notify if user requested size is filled or no more set of elements
	 * can be put */
	if (kfifo_len(&iio_sim->fifo) + indio_dev->scan_bytes >
	    usr_buf_len * indio_dev->scan_bytes ||
	    kfifo_len(&iio_sim->fifo) + indio_dev->scan_bytes >
	    kfifo_size(&iio_sim->fifo)) {
		u8 *buf = iio_sim->buf;

		cnt = iio_sim_get_fifo_all(indio_dev, &iio_sim->fifo,
					   (u32 *) buf, iio_sim->fifo_size);
		PR_DBG("cnt : %d", cnt);
		for (i = 0; i < cnt; i++) {
			iio_push_to_buffers(indio_dev, buf);
			buf += indio_dev->scan_bytes;
		}
	}
out:
	mutex_unlock(&indio_dev->mlock);

	return ret;
}

module_param_call(syn, iio_syn_set, NULL, NULL, 0200);

static int iio_sim_preenable(struct iio_dev *indio_dev)
{
	return iio_sw_buffer_preenable(indio_dev);
}

static int iio_sim_postenable(struct iio_dev *indio_dev)
{
	return 0;
}

static int iio_sim_predisable(struct iio_dev *indio_dev)
{
	return 0;
}

static int iio_sim_postdisable(struct iio_dev *indio_dev)
{
	struct iio_sim *iio_sim = get_iio_sim();

	kfifo_reset(&iio_sim->fifo);

	return 0;
}

#else

/**
 * iio_simple_sim_trigger_h() - the trigger handler function
 * @irq: the interrupt number
 * @p: private data - always a pointer to the poll func.
 *
 * This is the guts of buffered capture. On a trigger event occurring,
 * if the pollfunc is attached then this handler is called as a threaded
 * interrupt (and hence may sleep). It is responsible for grabbing data
 * from the device and pushing it into the associated buffer.
 */
static irqreturn_t iio_simple_sim_trigger_h(int irq, void *p)
{
	struct iio_poll_func *pf = p;
	struct iio_dev *indio_dev = pf->indio_dev;
	s32 *data;
	int ret;

	data = kmalloc(indio_dev->scan_bytes, GFP_KERNEL);
	if (data == NULL)
		goto done;

	ret = iio_simple_put_sim_values_to(indio_dev, data);
	BUG_ON(ret < 0);

	iio_push_to_buffers(indio_dev, (u8 *)data);

	kfree(data);

done:
	/*
	 * Tell the core we are done with this trigger and ready for the
	 * next one.
	 */
	iio_trigger_notify_done(indio_dev->trig);

	return IRQ_HANDLED;
}

#endif

static const struct iio_buffer_setup_ops iio_simple_sim_buffer_setup_ops = {
#if BUFFER_MODE == INDIO_BUFFER_HARDWARE
	.preenable = &iio_sim_preenable,
	.postenable = &iio_sim_postenable,
	.predisable = &iio_sim_predisable,
	.postdisable = &iio_sim_postdisable,
#else
	/*
	 * iio_sw_buffer_preenable:
	 * Generic function for equal sized ring elements + 64 bit timestamp
	 * Assumes that any combination of channels can be enabled.
	 * Typically replaced to implement restrictions on what combinations
	 * can be captured (hardware scan modes).
	 */
	.preenable = &iio_sw_buffer_preenable,
	/*
	 * iio_triggered_buffer_postenable:
	 * Generic function that simply attaches the pollfunc to the trigger.
	 * Replace this to mess with hardware state before we attach the
	 * trigger.
	 */
	.postenable = &iio_triggered_buffer_postenable,
	/*
	 * iio_triggered_buffer_predisable:
	 * Generic function that simple detaches the pollfunc from the trigger.
	 * Replace this to put hardware state back again after the trigger is
	 * detached but before userspace knows we have disabled the ring.
	 */
	.predisable = &iio_triggered_buffer_predisable,
#endif
};

int iio_simple_sim_configure_buffer(struct iio_dev *indio_dev,
	const struct iio_chan_spec *channels, unsigned int num_channels)
{
	int ret;
	struct iio_buffer *buffer;

#if BUFFER_MODE == INDIO_BUFFER_HARDWARE
	struct iio_sim *iio_sim = get_iio_sim();

	iio_sim->dev = indio_dev;
#endif

	/* Allocate a buffer to use - here a kfifo */
	buffer = iio_kfifo_allocate(indio_dev);
	if (buffer == NULL) {
		ret = -ENOMEM;
		goto error_ret;
	}

	indio_dev->buffer = buffer;

	/* Enable timestamps by default */
	buffer->scan_timestamp = true;

	/*
	 * Tell the core what device type specific functions should
	 * be run on either side of buffer capture enable / disable.
	 */
	indio_dev->setup_ops = &iio_simple_sim_buffer_setup_ops;

#if BUFFER_MODE == INDIO_BUFFER_HARDWARE
	ret = iio_sim_alloc_fifo_buffer(iio_sim);
	if (ret < 0)
		goto error_free_buffer;
#else
	/*
	 * Configure a polling function.
	 * When a trigger event with this polling function connected
	 * occurs, this function is run. Typically this grabs data
	 * from the device.
	 *
	 * NULL for the bottom half. This is normally implemented only if we
	 * either want to ping a capture now pin (no sleeping) or grab
	 * a timestamp as close as possible to a data ready trigger firing.
	 *
	 * IRQF_ONESHOT ensures irqs are masked such that only one instance
	 * of the handler can run at a time.
	 *
	 * "iio_simple_sim_consumer%d" formatting string for the irq 'name'
	 * as seen under /proc/interrupts. Remaining parameters as per printk.
	 */
	indio_dev->pollfunc = iio_alloc_pollfunc(NULL,
						 &iio_simple_sim_trigger_h,
						 IRQF_ONESHOT,
						 indio_dev,
						 "iio_simple_sim_consumer%d",
						 indio_dev->id);

	if (indio_dev->pollfunc == NULL) {
		ret = -ENOMEM;
		goto error_free_buffer;
	}
#endif

#if BUFFER_MODE == INDIO_BUFFER_HARDWARE
	indio_dev->modes |= INDIO_BUFFER_HARDWARE;
#else
	/*
	 * Notify the core that this device is capable of buffered capture
	 * driven by a trigger.
	 */
	indio_dev->modes |= INDIO_BUFFER_TRIGGERED;
#endif

	ret = iio_buffer_register(indio_dev, channels, num_channels);
	if (ret)
		goto error_dealloc_pollfunc;

	return 0;

error_dealloc_pollfunc:
#if BUFFER_MODE == INDIO_BUFFER_HARDWARE
	iio_sim_free_fifo_buffer(iio_sim);
#else
	iio_dealloc_pollfunc(indio_dev->pollfunc);
#endif
error_free_buffer:
	iio_kfifo_free(indio_dev->buffer);
error_ret:
	return ret;

}

/**
 * iio_simple_sim_unconfigure_buffer() - release buffer resources
 * @indo_dev: device instance state
 */
void iio_simple_sim_unconfigure_buffer(struct iio_dev *indio_dev)
{
#if BUFFER_MODE == INDIO_BUFFER_HARDWARE
	struct iio_sim *iio_sim = get_iio_sim();
#endif

	iio_buffer_unregister(indio_dev);
#if BUFFER_MODE == INDIO_BUFFER_HARDWARE
	iio_sim_free_fifo_buffer(iio_sim);
#else
	iio_dealloc_pollfunc(indio_dev->pollfunc);
#endif
	iio_kfifo_free(indio_dev->buffer);
}
