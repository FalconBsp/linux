/*
 * Copyright 2003,2006,2007,2013 Sony Corporation.
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
 *
 */

#ifndef __K3D_H__
#define __K3D_H__

extern int __init k3d_init(void);

extern int k3d_get_disk_change(struct gendisk *disk, int call_daemon);

extern void k3d_check_status(struct gendisk *disk, unsigned int *events);

extern int k3d_get_disk(struct gendisk *disk);
extern int k3d_put_disk(struct gendisk *disk);
#ifdef CONFIG_SNSC_BLOCK_CHECK_DISK
extern int k3d_save_disk_flag(struct gendisk *disk, int flag);
extern int k3d_load_disk_flag(struct gendisk *disk, int *flag);
#endif

#ifdef __KERNEL__
extern int k3d_register_disk(struct gendisk *bdev);
extern void k3d_unregister_disk(struct gendisk *bdev);

/* block device I/O error */
struct blkdev_io_error {
	struct list_head e_list;
	dev_t e_dev;
	atomic_t e_count;
};

extern struct blkdev_io_error *bioe_get_entry(struct gendisk *disk);

#endif /* __KERNEL__ */

#endif /* ! __K3D_H__ */
