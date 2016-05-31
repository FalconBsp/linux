/*
 * linux/block/k3d.c
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

#include "k3d_priv.h"

/*
 * k3d lists and semaphores
 */
LIST_HEAD(k3d_info_list);
struct rw_semaphore k3d_info_list_sem;

static void k3d_handle_events(struct work_struct *work);
static void check_status_with_bioe(struct k3d_info *k3di);

/*
 * Methods to handle block_device I/O error notification.
 */
static LIST_HEAD(bioe_list);
static spinlock_t bioe_list_lock;

struct blkdev_io_error *bioe_get_entry(struct gendisk *disk)
{
	unsigned long flags;
	struct blkdev_io_error *bioe;
	dev_t dev;

	if (!disk) {
		k3d_printk0("%s NULL disk\n", __func__);
		return NULL;
	}

	dev = MKDEV(disk->major, disk->first_minor + 0);

	spin_lock_irqsave(&bioe_list_lock, flags);
	list_for_each_entry(bioe, &bioe_list, e_list) {
		if (bioe->e_dev == dev) {
			spin_unlock_irqrestore(&bioe_list_lock, flags);
			return bioe;
		}
	}
	spin_unlock_irqrestore(&bioe_list_lock, flags);
	return NULL;
}

static struct blkdev_io_error *bioe_register(struct gendisk *disk)
{
	struct blkdev_io_error *bioe;
	unsigned long flags;
	dev_t dev;

	bioe = bioe_get_entry(disk);
	if (bioe) {
		atomic_set(&bioe->e_count, 0);
		return bioe;
	}
	bioe = (struct blkdev_io_error *)
		kzalloc(sizeof(struct blkdev_io_error), GFP_KERNEL);
	if (!bioe)
		return NULL;
	dev = MKDEV(disk->major, disk->first_minor + 0);
	bioe->e_dev = dev;
	atomic_set(&bioe->e_count, 0);
	spin_lock_irqsave(&bioe_list_lock, flags);
	list_add(&bioe->e_list, &bioe_list);
	spin_unlock_irqrestore(&bioe_list_lock, flags);
	return bioe;
}

#ifdef NEVER_K3D_MODULE
static void bioe_unregister(struct gendisk *disk)
{
	struct blkdev_io_error *bioe;
	unsigned long flags;

	bioe = bioe_get_entry(disk);
	if (bioe) {
		spin_lock_irqsave(&bioe_list_lock, flags);
		list_del(&bioe->e_list);
		spin_unlock_irqrestore(&bioe_list_lock, flags);
		bioe->e_dev = 0;
		atomic_set(&bioe->e_count, 0);
		kfree(bioe);
	}
}
#endif

static struct k3d_info *find_k3dinfo(struct gendisk *disk)
{
	struct k3d_info *e;
	dev_t dev;

	if (!disk)
		return NULL;

	dev = MKDEV(disk->major, disk->first_minor + 0);

	list_for_each_entry(e, &k3d_info_list, dev_list) {
		if (e->bioe->e_dev == dev)
			return e;
	}
	return NULL;
}

struct k3d_info *find_k3dinfo_by_name(const char *name)
{
	struct k3d_info *e;

	if (!name)
		return NULL;

	list_for_each_entry(e, &k3d_info_list, dev_list) {
		if (!strcmp(name, e->name))
			return e;
	}
	return NULL;
}

/*
 * k3d checks the disk status with bioe. If k3d found the disk status
 * changed, k3d sends a quiery to the device later.
 */
static void check_status_with_bioe(struct k3d_info *k3di)
{
	if (atomic_read(&k3di->bioe->e_count) <= 0)
		return;

	k3d_printk("%s bioe error occurred\n", __func__);
	atomic_set(&k3di->changed_state, 1);
}

/*
 * arg. call_daemon is obsoleted. k3d thread is waken up later
 */
int k3d_get_disk_change(struct gendisk *disk, int call_daemon)
{
	struct k3d_info *k3di;
	int rval = 0;

	k3di = find_k3dinfo(disk);
	if (k3di == NULL)
		goto end_func;

	if (k3di->disk != disk) {
		rval = 1;
		goto end_func;
	}

	check_status_with_bioe(k3di);

	rval = atomic_read(&k3di->changed_state);

end_func:
	return rval;
}
EXPORT_SYMBOL(k3d_get_disk_change);

/*
 * count up or down reference to the disk.
 */
int k3d_get_disk(struct gendisk *disk)
{
	struct k3d_info *k3di;
	int ret = 0;

	k3di = find_k3dinfo(disk);
	if (k3di == NULL) {
		printk(KERN_ERR "%s: not found device (dev=%s)\n",
			__func__, disk->disk_name);
		ret = -1;
		goto out;
	}

	atomic_inc(&k3di->ref_count);
out:
	return ret;
}
EXPORT_SYMBOL(k3d_get_disk);

int k3d_put_disk(struct gendisk *disk)
{
	struct k3d_info *k3di;
	int ret = 0;

	k3di = find_k3dinfo(disk);
	if (k3di == NULL) {
		printk(KERN_ERR "%s: not found device (dev=%s)\n",
			__func__, disk->disk_name);
		ret = -1;
		goto out;
	}
	atomic_dec(&k3di->ref_count);
out:
	return ret;
}
EXPORT_SYMBOL(k3d_put_disk);

#ifdef CONFIG_SNSC_BLOCK_CHECK_DISK
/*
 * save or load a disk flag.
 */
int k3d_save_disk_flag(struct gendisk *disk, int flag)
{
	struct k3d_info *k3di;
	int ret = 0;

	k3di = find_k3dinfo(disk);
	if (k3di == NULL) {
		ret = -1;
		goto out;
	}
	k3di->flag = flag;
 out:
	return ret;
}
EXPORT_SYMBOL(k3d_save_disk_flag);

int k3d_load_disk_flag(struct gendisk *disk, int *flag)
{
	struct k3d_info *k3di;
	int ret = 0;

	k3di = find_k3dinfo(disk);
	if (k3di == NULL) {
		ret = -1;
		goto out;
	}
	if (flag != NULL)
		*flag = k3di->flag;
 out:
	return ret;
}
EXPORT_SYMBOL(k3d_load_disk_flag);
#endif

/*
 * register and unregister the disk.
 */
int k3d_register_disk(struct gendisk *disk)
{
	struct k3d_info *k3di;
	int ret = 0;

	if (!disk->fops->check_events)
		return -EINVAL;

	if (!(disk->flags & GENHD_FL_REMOVABLE))
		return -EINVAL;

	k3di = find_k3dinfo(disk);
	if (k3di) {
		k3d_printk("%s re-registration\n", __func__);

		if (k3di->disk != disk)
			k3di->disk = disk;

		if (atomic_read(&k3di->ref_count) == 0) {
			atomic_set(&k3di->changed_state, 0);
			atomic_set(&k3di->bioe->e_count, 0);
		} else
			atomic_set(&k3di->changed_state, 1);
	} else {
		k3d_printk("%s first registration\n", __func__);
		k3di = (struct k3d_info *)
			kzalloc(sizeof(struct k3d_info), GFP_KERNEL);
		if (k3di == NULL) {
			printk(KERN_ERR "%s: NOMEM k3di\n", __func__);
			return -ENOMEM;
		}
		k3di->disk = disk;
		disk_name(disk, 0, k3di->name);

		atomic_set(&k3di->changed_state, 0);
		atomic_set(&k3di->ref_count, 0);
		atomic_set(&k3di->flag_wait_unregister, 0);
		k3di->bioe = bioe_register(disk);
		if (k3di->bioe == NULL) {
			printk(KERN_ERR "%s: NOMEM k3di->bioe\n", __func__);
			kfree(k3di);
			return -ENOMEM;
		}
		INIT_DELAYED_WORK(&k3di->dwork, k3d_handle_events);
		down_write(&k3d_info_list_sem);
		list_add(&k3di->dev_list, &k3d_info_list);
		up_write(&k3d_info_list_sem);
	}

	return ret;
}

static void k3d_unregister(struct k3d_info *k3di)
{
	while (atomic_read(&k3di->flag_wait_unregister))
		msleep(20);

	k3di->disk = NULL;

	k3d_printk0("%s: k3di->ref_count=%d\n",
		__func__, atomic_read(&k3di->ref_count));
}

#ifdef NEVER_K3D_MODULE
static void k3d_unregister_all(void)
{
	struct k3d_info *k3di, *nk3di;

	down_write(&k3d_info_list_sem);
	list_for_each_entry_safe(k3di, nk3di, &k3d_info_list, dev_list) {
		bioe_unregister(k3di->disk);
		k3d_unregister(k3di);
		list_del(&k3di->dev_list);
		kfree(k3di);
	}
	up_write(&k3d_info_list_sem);
}
#endif

void k3d_unregister_disk(struct gendisk *disk)
{
	struct k3d_info *k3di;

	k3di = find_k3dinfo(disk);
	if (k3di == NULL)
		goto out;
	k3d_unregister(k3di);
out:
	return;
}

static void k3d_handle_events(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct k3d_info *k3di = container_of(dwork, struct k3d_info, dwork);
	struct block_device *bdev;
	int ret;

	disk_clear_events(k3di->disk, DISK_EVENT_MEDIA_CHANGE |
		DISK_EVENT_EJECT_REQUEST);

	if (atomic_read(&k3di->ref_count))
		goto out;

	bdev = bdget_disk(k3di->disk, 0);
	if (bdev == NULL)
		goto out;

	/* Set bd_invalidated=1 to update disk status in blkdev_get()
	 * by calling:
	 * - invalidate_partions() for a disk without medium.
	 * - rescan_partion() for a disk with medium.
	 */
	bdev->bd_invalidated = 1;
	ret = blkdev_get(bdev, FMODE_READ, NULL);
	if (ret < 0) {
		if (ret != -ENOMEDIUM)
			goto out;
	} else
		blkdev_put(bdev, FMODE_READ);

	if ((loff_t)get_capacity(k3di->disk))
		atomic_set(&k3di->changed_state, 0);
	else
		atomic_set(&k3di->changed_state, 1);

out:
	atomic_dec(&k3di->flag_wait_unregister);
}

/*
 * k3d checks media for each disks.
 */
void k3d_check_status(struct gendisk *disk, unsigned int *events)
{
	int e_count;
	struct k3d_info *k3di;

	k3di = find_k3dinfo(disk);
	if (!k3di)
		return;

	atomic_inc(&k3di->flag_wait_unregister);

	e_count = atomic_read(&k3di->bioe->e_count);
	if (e_count > 0) {
		/* clear I/O error count */
		if (atomic_add_negative(-e_count, &k3di->bioe->e_count))
			BUG();
		*events |= DISK_EVENT_MEDIA_CHANGE;
	}

	if (*events & DISK_EVENT_MEDIA_CHANGE)
		/* "0" means start work without delay */
		queue_delayed_work(system_nrt_freezable_wq, &k3di->dwork, 0);
	else
		atomic_dec(&k3di->flag_wait_unregister);

	return;
}

/*
 * k3d init and exit methods.
 */
int __init k3d_init(void)
{
	k3d_printk("%s[%d:%s]: start\n", __func__,
		current->pid, current->comm);

	spin_lock_init(&bioe_list_lock);
	init_rwsem(&k3d_info_list_sem);
	return 0;
}

#ifdef NEVER_K3D_MODULE
void __exit k3d_exit(void)
{
	k3d_printk("%s[%d:%s]:\n", __func__, current->pid, current->comm);
	k3d_unregister_all();
	k3d_printk("%s[%d:%s]: stop daemon\n", __func__,
		current->pid, current->comm);
	k3d_printk("%s[%d:%s]: end\n", __func__,
		current->pid, current->comm);
}
#endif
