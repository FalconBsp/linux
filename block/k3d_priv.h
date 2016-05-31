/*
 * linux/block/k3d_priv.h
 * Copyright 2007,2013 Sony Corporation.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; version 2
 * of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 */

#ifndef __K3D_PRIV_H__
#define __K3D_PRIV_H__

#include <linux/module.h>
#include <linux/init.h>
#include <linux/list.h>
#include <linux/slab.h>
#include <linux/blkdev.h>
#include <linux/freezer.h>
#include <linux/k3d.h>
#include <linux/delay.h>

struct k3d_info {
	struct list_head  dev_list;
	struct gendisk    *disk;
	atomic_t          ref_count;
	atomic_t          changed_state;
	char              name[BDEVNAME_SIZE];
	struct blkdev_io_error *bioe;
	int               flag;
	atomic_t          flag_wait_unregister;
	struct delayed_work dwork;
};

struct k3d_info *find_k3dinfo_by_name(const char *name);

/* for debug */
#undef K3D_DEBUG
#undef K3D_DEBUG2
#undef K3D_DEBUG0

#ifdef K3D_DEBUG
#define k3d_printk(x...)	printk(x)
#else
#define k3d_printk(x...)	do {} while (0)
#endif

#ifdef K3D_DEBUG2
#define k3d_printk2(x...)	printk(x)
#else
#define k3d_printk2(x...)	do {} while (0)
#endif

#ifdef K3D_DEBUG0
#define k3d_printk0(x...)	printk(x)
#else
#define k3d_printk0(x...)	do {} while (0)
#endif

#endif /* ! __K3D_PRIV_H__ */
