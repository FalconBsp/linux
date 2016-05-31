/*
 * Copyright 2014 Sony Corporation
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions, and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/device.h>
#include <linux/relay.h>
#include <linux/debugfs.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/semaphore.h>
#include <linux/ioctl.h>

#include "hifc.h"

#define HIFC_DIR "hifc_disp"
#define RELAY_SUB_BUF_LEN (1 * 1024) /* 1KB each */
#define RELAY_SUB_BUF_NUM 512 /* 1KB * 512 */

#define DEVCNT 1
#define DEVNAME "hifc"
#define CLASS_NAME "hifc_class"

#define DISP_IOC_MAGIC 'w'

struct disp_mesg {
	uint64_t buf;
	size_t len;
};

#define DISP_IOC_S_MESG _IOW(DISP_IOC_MAGIC, 0, struct disp_mesg)

#define DISP_IOC_MAXNR 1

struct hifc_disp_relay {
	struct rchan *chan;
	struct dentry *dir;
	struct rchan_callbacks cbs;
	size_t subbuf_len;
	size_t subbuf_num;
};

static struct hifc_disp_relay disp_relay;

struct hifc_disp_chrdev {
	struct class *hifc_class;
	struct cdev cdev;
	dev_t dev_num;
	struct semaphore sem;
	struct hifc_data *hdata;
};

static int32_t disp_recv_mesg(uint8_t *mesg, const size_t len)
{
	relay_write(disp_relay.chan, mesg, len);
/*    relay_flush(disp_relay.chan); */

	return 0;
}

static struct dentry *create_buf_file(const char *fn, struct dentry *parent,
				      umode_t mode, struct rchan_buf *buf,
				      int *is_gbl)
{
	struct dentry *buf_file = debugfs_create_file(fn, mode, parent, buf,
						      &relay_file_operations);
	HIFC_ALERT("%s\n", __func__);
	return buf_file;
}

static int32_t remove_buf_file(struct dentry *dentry)
{
	debugfs_remove(dentry);
	HIFC_ALERT("%s\n", __func__);
	return 0;
}

static int32_t  init_relay(struct hifc_disp_relay *disp_relay)
{
	disp_relay->subbuf_len          = RELAY_SUB_BUF_LEN;
	disp_relay->subbuf_num          = RELAY_SUB_BUF_NUM;
	disp_relay->cbs.create_buf_file = create_buf_file;
	disp_relay->cbs.remove_buf_file = remove_buf_file;
	disp_relay->dir                 = debugfs_create_dir(HIFC_DIR, NULL);
	disp_relay->chan                = relay_open("mesg",
						     disp_relay->dir,
						     disp_relay->subbuf_len,
						     disp_relay->subbuf_num,
						     &disp_relay->cbs,
						     NULL);
	if (!disp_relay->chan) {
		HIFC_ERR("%s no memory for rchan\n", __func__);
		goto error;
	}

	/* TODO if do not relay_write(), the per-cpu buffer file wont't */
	/* appear in user space */
	relay_write(disp_relay->chan, "ready", 5);

	return 0;

error:
	return -ENOMEM;
}

static int32_t disp_open(struct inode *inode, struct file *filp)
{
	struct hifc_disp_chrdev *chrdev =
				container_of(inode->i_cdev,
					     struct hifc_disp_chrdev,
					     cdev);

	filp->private_data = chrdev;

	return down_interruptible(&chrdev->sem);
}

static int32_t disp_send_mesg(struct file *filp, uint32_t cmd,
			      void __user *arg)
{
	struct hifc_disp_chrdev *chrdev =
		(struct hifc_disp_chrdev *)filp->private_data;
	const struct hifc_ops *pops     = chrdev->hdata->pops;
	struct disp_mesg *dmesg         = NULL;
	size_t arg_len                  = _IOC_SIZE(cmd);
	uint8_t *buf                    = 0;
	int32_t ret                     = 0;

	dmesg = kzalloc(arg_len, GFP_KERNEL);

	if (dmesg) {
		if (__copy_from_user(dmesg, arg, arg_len)) {
			ret = -EFAULT;
			goto out;
		}

		if (dmesg->buf) {
			buf = kzalloc(dmesg->len, GFP_KERNEL | __GFP_DMA);
			if (!buf) {
				ret = -ENOMEM;
				goto out;
			}

			if (copy_from_user(
				buf,
				(const uint8_t __user *)(uintptr_t)dmesg->buf,
				dmesg->len)) {

				ret = -EFAULT;
				goto out;
			}
#if 1
			ret = pops->spz_prot_send_mesg(chrdev->hdata,
						       buf, dmesg->len);
#else
			relay_write(disp_relay.chan, buf, dmesg->len); /* aws */
			relay_flush(disp_relay.chan);
#endif
		}
	}

out:

	kfree(dmesg);
	dmesg = NULL;

	kfree(buf);
	buf = NULL;

	return ret;
}

static long disp_ioctl(struct file *filp, uint32_t cmd, u_long arg)
{
	void __user *argp = (void __user *)arg;
	int32_t ret = 0;

	if (DISP_IOC_MAGIC != (_IOC_TYPE(cmd))
	    && (DISP_IOC_MAXNR < _IOC_NR(cmd))) {
		ret = -ENOTTY;
		goto out;
	}

	if ((_IOC_DIR(cmd) & _IOC_WRITE)
	    && !access_ok(VERIFY_WRITE, argp, _IOC_SIZE(cmd))) {
		ret = -EFAULT;
		goto out;
	}

	if ((_IOC_DIR(cmd) & _IOC_READ)
	    && !access_ok(VERIFY_READ, argp, _IOC_SIZE(cmd))) {
		ret = -EFAULT;
		goto out;
	}

	switch (cmd) {
	case DISP_IOC_S_MESG:
		ret = disp_send_mesg(filp, cmd, argp);
		break;
	default:
		HIFC_ERR("%s non supported ioctl cmd yet\n",
			__func__);
	}

out:
	return ret;
}

static int32_t disp_close(struct inode *inode, struct file *filp)
{
	struct hifc_disp_chrdev *chrdev = container_of(inode->i_cdev,
						       struct hifc_disp_chrdev,
						       cdev);

	up(&chrdev->sem);
	return 0;
}

static const struct file_operations disp_fops = {
	.owner          = THIS_MODULE,
	.open           = disp_open,
	.unlocked_ioctl = disp_ioctl,
	.release        = disp_close
};

static struct hifc_disp_chrdev spz_disp_chrdev;

static struct disp_hdlr_data spz_disp_dhdlr = {
	.spz_disp_recv_mesg = disp_recv_mesg,
	.data               = &spz_disp_chrdev
};

static int32_t init_chrdev(struct hifc_data *hdata)
{
	int32_t ret = 0;
	struct device *dev;

	spz_disp_chrdev.hdata = hdata;

	ret = alloc_chrdev_region(&spz_disp_chrdev.dev_num,
				  0, DEVCNT, DEVNAME);

	HIFC_ALERT("%s spz_disp_chrdev major: %d\n",
	       __func__, MAJOR(spz_disp_chrdev.dev_num));

	if (ret) {
		HIFC_ERR("%s alloc cdev fail\n", __func__);
		goto error;
	}

	cdev_init(&spz_disp_chrdev.cdev, &disp_fops);
	ret = cdev_add(&spz_disp_chrdev.cdev, spz_disp_chrdev.dev_num, DEVCNT);

	if (ret) {
		HIFC_ERR("%s add spz_disp_chrdev fail\n", __func__);
		goto error;
	}

	spz_disp_chrdev.hifc_class = class_create(THIS_MODULE, CLASS_NAME);
	if (IS_ERR(spz_disp_chrdev.hifc_class)) {
		HIFC_ERR("%s create class fail\n", __func__);
		ret = -EINVAL;
		goto error;
	}

	dev = device_create(spz_disp_chrdev.hifc_class,
			    NULL, spz_disp_chrdev.dev_num,
			    NULL, "%s", DEVNAME);
	if (!dev) {
		HIFC_ERR("%s create device fail\n", __func__);
		ret = -EINVAL;
		goto error;
	}

	sema_init(&spz_disp_chrdev.sem, 1);

	return 0;

error:
	return ret;
}

int32_t spz_disp_probe(struct device *dev)
{
	int32_t ret = 0;
	struct hifc_data *hdata = NULL;

	hdata = (struct hifc_data *)dev_get_drvdata(dev);
	hdata->dhdlr = &spz_disp_dhdlr;

#if 0 /* test code */
	hdata->dhdlr->spz_disp_send_mesg(NULL, 0);
	hdata->pops->spz_prot_send_mesg(NULL, 0);
	hdata->bdata->bops->spz_drv_send_icmd(hdata, 0, NULL, 0, 0, 0);
#endif
	return ((ret = init_relay(&disp_relay)) ? ret
		: init_chrdev(hdata));
}
EXPORT_SYMBOL(spz_disp_probe);

static int32_t deinit_relay(struct hifc_disp_relay *disp_relay)
{
	if (disp_relay->chan) {
		relay_flush(disp_relay->chan);
		relay_close(disp_relay->chan);
		disp_relay->chan = NULL;
	}

	return 0;
}

static void deinit_chrdev(void)
{
	cdev_del(&spz_disp_chrdev.cdev);
	unregister_chrdev_region(spz_disp_chrdev.dev_num, DEVCNT);
	device_destroy(spz_disp_chrdev.hifc_class, spz_disp_chrdev.dev_num);
	class_destroy(spz_disp_chrdev.hifc_class);
}

int32_t spz_disp_remove(struct device *dev)
{
	struct hifc_data *hdata = (struct hifc_data *)dev_get_drvdata(dev);

	deinit_chrdev();
	hdata->dhdlr->data = NULL;
	hdata->dhdlr = NULL;

	return deinit_relay(&disp_relay);
}
EXPORT_SYMBOL(spz_disp_remove);

MODULE_LICENSE("GPL");
