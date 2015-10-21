/*
* tee_driver.c - TEE client (teeapi) driver
*
* Copyright 2015 Advanced Micro Devices, Inc.
*
* Permission is hereby granted, free of charge, to any person obtaining a
* copy of this software and associated documentation files (the "Software"),
* to deal in the Software without restriction, including without limitation
* the rights to use, copy, modify, merge, publish, distribute, sublicense,
* and/or sell copies of the Software, and to permit persons to whom the
* Software is furnished to do so, subject to the following conditions:
*
* The above copyright notice and this permission notice shall be included in
* all copies or substantial portions of the Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
* IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
* THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
* OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
* ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
* OTHER DEALINGS IN THE SOFTWARE.
*
* Authors: AMD
*
*/

#include "tee_driver.h"
#include <linux/psp_comm_if.h>

static struct kobject *tee_kobj;

static const struct file_operations tee_fileopertaions = {
	.owner          = THIS_MODULE,
	.open           = tee_open,
	.release        = tee_release,
	.unlocked_ioctl = tee_ioctl,
	.compat_ioctl   = tee_ioctl,
	.mmap           = tee_mmap,
};

static struct miscdevice tee_device = {
	.name   = TEE_DRV_MOD_DEVNODE,
	.minor  = MISC_DYNAMIC_MINOR,
	.fops   = &tee_fileopertaions,
};

static struct kobj_attribute debug_attr =
__ATTR(status, S_IRUGO|S_IWUSR,
		(void *)tee_status, (void *)tee_reset);


static struct attribute *sysfs_attrs[] = {
	&debug_attr.attr,
	NULL
};

static struct attribute_group tee_attribute_group = {
	.attrs = sysfs_attrs
};

u64 tee_allocate_memory(u32 requestedsize)
{
	void *addr;
	u64 order = 0;

	order = sizetoorder(requestedsize);
	pr_debug(" %s : req-sz 0x%llx 4k-po 0x%llx\n",
			__func__, (u64)requestedsize, (u64)order);
	do {
		/* pages should be 4K aligned */
		addr = (void *)__get_free_pages(GFP_KERNEL, order);
		if (NULL == addr) {
			pr_err(
			" %s : addr(bulk-comm/map/tci/user) failed 0x%llx\n",
				__func__, (u64)addr);
			break;
		}
	} while (FALSE);
	pr_debug(" %s : addr(bulk-comm/map/tci/user) addr 0x%llx\n",
			__func__, (u64)addr);
	return (u64)addr;
}

void tee_free_memory(void *addr, u32 requestedsize)
{
	u64 order = 0;

	order = sizetoorder(requestedsize);
	if (addr) {
		pr_debug("free (bulk-comm/map/tci/user) addr 0x%llx\n",
				(u64)addr);
		free_pages((u64)addr, sizetoorder(requestedsize));
	}
}

static void tee_range_check(u32 location)
{
	if (location == MAX_SESSIONS_SUPPORTED) {
		tee_drv_data.tee_error = TEE_ERR_UNKNOWN_SESSION;
		pr_err(" %s : session exceed\n", __func__);
	}
}

static int tee_session_location(u32 session)
{
	int i;

	for (i = 0; i < MAX_SESSIONS_SUPPORTED; i++) {
		if (tee_drv_data.tee_session[i].sessionid == session)
			break;
	}
	tee_range_check(i);
	return i;
}

static void tee_terminate_wait(u32 session)
{
	int ret = 0;
	/* make sure waitnotification ends */
	if (tee_drv_data.tee_session[session].status == TEE_NOTIFY_DONE) {
		pr_debug(" %s : wait notification in progress\n",
				__func__);
		tee_drv_data.tee_session[session].status = TEE_NOTIFY_CLEAR;
		up(&tee_drv_data.tee_sem);
		while (tee_drv_data.tee_session[session].status
				== TEE_NOTIFY_NONE)
			;
		ret = down_interruptible(&tee_drv_data.tee_sem);
		if (0 != ret)
			pr_debug(" %s : down_interruptible failed %d",
					__func__, ret);
	}
}

static void tee_clean_tci(u32 location)
{
	tee_free_memory((void *)tee_drv_data.tee_tci[location].virtaddr,
			tee_drv_data.tee_tci[location].length);
	tee_drv_data.tee_tci[location].virtaddr = 0x0;
	tee_drv_data.tee_tci[location].uid = 0x0;
	tee_drv_data.tee_tci[location].sessionid = 0x0;
	tee_drv_data.tee_tci[location].physaddr = 0x0;
	tee_drv_data.tee_tci[location].useraddr = 0x0;
}

static void tee_clean_map(u32 session, u32 location)
{
	tee_free_memory((void *)tee_drv_data.tee_session[session].
			map_virtaddr[location],
			tee_drv_data.tee_session[session].
			map_length[location]);
	tee_drv_data.tee_session[session].map_virtaddr[location] = 0x0;
	tee_drv_data.tee_session[session].map_physaddr[location] = 0x0;
	tee_drv_data.tee_session[session].map_secaddr[location] = 0x0;
	tee_drv_data.tee_session[session].map_length[location] = 0x0;
	tee_drv_data.tee_session[session].map_useraddr[location] = 0x0;
}

static void tee_close_session(u32 session)
{
	cmdbuf.cmdclose.sessionid = session;
	psp_comm_send_buffer(TEE_CLIENT_TYPE, (union tee_cmd_buf *)&cmdbuf,
			sizeof(union tee_cmd_buf), TEE_CMD_ID_CLOSE_SESSION);
}

int tee_driver_init(void)
{
	int     ret = 0;

	pr_debug(" %s :\n", __func__);
	do {
		tee_drv_data.tee_session = vzalloc(
				sizeof(struct tee_session_context)
				*MAX_SESSIONS_SUPPORTED);
		if (0 == tee_drv_data.tee_session) {
			pr_err(" %s : alloc(session) failed\n",
					__func__);
			ret = -ENOMEM;
			break;
		}
		pr_debug(" %s : alloc(session) addr 0x%llx\n",
				__func__, (u64)tee_drv_data.tee_session);
		tee_drv_data.tee_tci = vzalloc(
				sizeof(struct tee_tci_context)
				*MAX_SESSIONS_SUPPORTED);
		if (0 == tee_drv_data.tee_tci) {
			pr_debug(" %s : alloc(tci-s) failed\n",
					__func__);
			vfree(tee_drv_data.tee_session);
			ret = -ENOMEM;
			break;
		}
		pr_debug(" %s : alloc(tci-s) addr 0x%llx\n",
				__func__, (u64)tee_drv_data.tee_tci);
	} while (FALSE);
	pr_info(" %s : tee driver initialized\n", __func__);
	pr_debug(" %s : ret %d\n", __func__, ret);
	return (int)ret;
}

void tee_driver_exit(void)
{
	int i, j;

	pr_debug(" %s :\n", __func__);
	for (i = 0; i < MAX_SESSIONS_SUPPORTED; i++) {
		if (tee_drv_data.tee_session[i].sessionid != 0x0) {
			pr_debug(" %s : sid: 0x%llx close ",
			__func__, tee_drv_data.tee_session[i].sessionid);
			tee_close_session(tee_drv_data.tee_session[i].
								sessionid);
			tee_drv_data.tee_session[i].sessionid = 0x0;
			for (j = 0; j < MAX_BUFFERS_MAPPED; j++)
				tee_clean_map(i, j);
		}
	}
	for (i = 0; i < MAX_SESSIONS_SUPPORTED; i++)
		tee_clean_tci(i);
	if (tee_drv_data.tee_tci) {
		pr_debug(" %s : free(tci-s) addr 0x%llx\n",
				__func__, (u64)tee_drv_data.tee_tci);
		vfree(tee_drv_data.tee_tci);
		tee_drv_data.tee_tci = 0x0;
	}
	if (tee_drv_data.tee_session) {
		pr_debug(" %s : free(session) addr 0x%llx\n",
				__func__, (u64)tee_drv_data.tee_session);
		vfree(tee_drv_data.tee_session);
		tee_drv_data.tee_session = 0x0;
	}
}

int tee_open(struct inode *pinode, struct file *pfile)
{
	int ret = 0;
	struct tee_instance *instance;

	pr_debug(" %s :\n", __func__);
	ret = down_interruptible(&tee_drv_data.tee_sem);
	if (0 != ret)
		return ret;
	do {
		if (tee_drv_data.tee_instance_counter == 0) {
			ret = tee_driver_init();
			if (0 != ret)
				break;
		}
		instance = vzalloc(sizeof(struct tee_instance));
		if (0 == instance) {
			pr_debug(" %s : alloc(instance) failed\n",
					__func__);
			ret = -ENOMEM;
			up(&tee_drv_data.tee_sem);
			tee_driver_exit();
			break;
		}
		pr_debug(" %s : alloc(instance) addr 0x%llx\n",
				__func__,
				(u64)instance);
		sema_init(&instance->sem, 0x1);
		tee_drv_data.tee_instance_counter++;
		instance->uid = ++tee_drv_data.tee_uid;
		pfile->private_data = instance;
	} while (FALSE);
	up(&tee_drv_data.tee_sem);
	pr_debug(" %s : ret %d\n", __func__, ret);
	return (int)ret;
}

int tee_release(struct inode *pinode, struct file *pfile)
{
	int ret = 0;
	struct tee_instance *instance =
		(struct tee_instance *)pfile->private_data;
	pr_debug(" %s :\n", __func__);
	ret = down_interruptible(&tee_drv_data.tee_sem);
	if (0 != ret)
		return ret;
	do {
		if (tee_drv_data.tee_instance_counter == 0)
			break;
		if (instance) {
			pr_debug(" %s : free(instance) addr 0x%llx\n",
					__func__, (u64)instance);
			vfree(instance);
			instance = 0x0;
		}
		if (tee_drv_data.tee_instance_counter == 0)
			tee_driver_exit();
	} while (FALSE);
	up(&tee_drv_data.tee_sem);
	pr_debug(" %s : ret %d\n", __func__, ret);
	return (int)ret;
}

int tee_mmap(struct file *pfile, struct vm_area_struct *pvmarea)
{
	void *virtaddr = 0;
	void *physaddr = 0;
	u64 requestedsize = pvmarea->vm_end - pvmarea->vm_start;
	int ret = 0;
	u64 addr;
	struct tee_instance *instance =
		(struct tee_instance *)pfile->private_data;
	pr_debug(" %s :\n", __func__);
	ret = down_interruptible(&instance->sem);
	if (0 != ret)
		return ret;
	do {
		if (requestedsize == 0x0) {
			ret = -ENOMEM;
			break;
		}
		addr = tee_allocate_memory(requestedsize);
		virtaddr = (void *)addr;
		physaddr = (void *)virt_to_phys((void *)addr);
		if (0 == virtaddr) {
			pr_debug(" %s : alloc failed\n", __func__);
			ret = -ENOMEM;
			break;
		}
		/* mark it non-cacheable */
		pvmarea->vm_flags |= VM_IO |
			(VM_DONTEXPAND | VM_DONTDUMP);
		pvmarea->vm_page_prot =
			pgprot_noncached(pvmarea->vm_page_prot);
		ret = (int)remap_pfn_range(
				pvmarea,
				(pvmarea->vm_start),
				addrtopfn(physaddr),
				requestedsize,
				pvmarea->vm_page_prot);

		if (0 != ret) {
			pr_err(" %s : rfr failed\n", __func__);
			tee_free_memory((void *)addr, requestedsize);
			break;
		}
		instance->mmap.length = requestedsize;
		instance->mmap.virtaddr = (u64)virtaddr;
		instance->mmap.physaddr = (u64)physaddr;
		pr_debug(" mmap : pa 0x%llx va 0x%llx ,user vmas 0x%llx\n",
			(u64)instance->mmap.physaddr,
			(u64)instance->mmap.virtaddr,
			(u64)pvmarea->vm_start);
	} while (FALSE);
	up(&instance->sem);
	pr_debug(" %s : ret %d\n", __func__, ret);
	return (int)ret;
}

ssize_t tee_reset(struct kobject *kobj,
		struct kobj_attribute *attr,
		char *buf,
		size_t count)
{
	int ret = 0, i, j;
	int sessionid;

	pr_debug(" %s :\n", __func__);
	ret = sscanf(buf, "%du", &sessionid);
	if (0 != ret)
		return ret;
	pr_debug(" %s : sid: %d to be cleaned; echo 999 to clean all\n",
		__func__, sessionid);
	if (tee_drv_data.tee_instance_counter) {
		ret = down_interruptible(&tee_drv_data.tee_sem);
		if (0 != ret) {
			pr_debug(" %s : down_interruptible failed %d",
					__func__, ret);
			return 0;
		}
		do {
			pr_debug(" %s : instance present %d\n",
					__func__,
					tee_drv_data.tee_instance_counter);
			if ((!tee_drv_data.tee_session) ||
					(!tee_drv_data.tee_tci)) {
				pr_err(" null data\n");
				break;
			}
			for (i = 0; i < MAX_SESSIONS_SUPPORTED; i++) {
				if ((tee_drv_data.tee_session[i].sessionid
								== sessionid) ||
					((tee_drv_data.tee_session[i]
						.sessionid != 0x0) &&
						 (sessionid == 999))) {
					pr_debug(
					" %s : sid: 0x%llx pos:%d close\n",
								__func__,
						tee_drv_data.tee_session[i]
								.sessionid,
						i);
					tee_terminate_wait(i);
					tee_close_session(
					tee_drv_data.tee_session[i].sessionid);
					tee_drv_data.tee_session[i]
							.sessionid = 0x0;
					for (j = 0;
						j < MAX_BUFFERS_MAPPED; j++) {
							tee_clean_map(i, j);
						}
						tee_clean_tci(i);
					}
				}
		} while (FALSE);
			up(&tee_drv_data.tee_sem);
	} else
		pr_debug(" %s : no instance present\n",
				__func__);
	return count;
}

static void tee_debug_session(void)
{
	int i, j;

	for (i = 0; i < MAX_SESSIONS_SUPPORTED; i++) {
		if (tee_drv_data.tee_session[i].sessionid != 0x0) {
			pr_debug(" %s : sid: 0x%llx position:%d\n",
					__func__,
			tee_drv_data.tee_session[i].sessionid, i);
			for (j = 0; j < MAX_BUFFERS_MAPPED; j++) {
				if (tee_drv_data.tee_session[i]
						.map_virtaddr[j])
					pr_debug(
			" %s : sid: 0x%llx position:%d mapped addr 0x%llx\n",
							__func__,
					tee_drv_data.tee_session[i].
					sessionid,
					i,
					(u64)tee_drv_data.tee_session[i].
					map_virtaddr[j]);
				pr_debug(
					" %s : position:%d len 0x%llx\n",
					__func__, j,
					tee_drv_data.tee_session[i].
					map_length[j]);
			}
			if (tee_drv_data.tee_tci[i].virtaddr)
				pr_debug(
				" %s : sid: 0x%llx position:%d\n",
				__func__,
				tee_drv_data.tee_session[i].sessionid, i);
			pr_debug(
				" %s : tci addr 0x%llx len 0x%llx\n",
				__func__,
				tee_drv_data.tee_tci[i].virtaddr,
				tee_drv_data.tee_tci[i].length);
		}
	}
}

ssize_t tee_status(struct kobject *kobj,
		struct kobj_attribute *attr,
		char *buf)
{
	int ret = 0;

	pr_debug(" %s :\n", __func__);

	if (tee_drv_data.tee_instance_counter) {
		pr_debug(" %s : instance present %d\n",
				__func__,
				tee_drv_data.tee_instance_counter);
		ret = down_interruptible(&tee_drv_data.tee_sem);
		if (0 != ret) {
			pr_debug(" %s : down_interruptible failed %d",
					__func__, ret);
			return 0;
		}
		do {
			if ((!tee_drv_data.tee_session) ||
					(!tee_drv_data.tee_tci)) {
				pr_err(" null data\n");
				break;
			}
			tee_debug_session();
		} while (FALSE);
		up(&tee_drv_data.tee_sem);
	} else
		pr_debug(" %s : no instance present\n", __func__);
	return 0;
}

static int handle_open_session(struct tee_instance *instance,
		union tee_open_session *puserparams)
{
	int ret = 0, i;
	union tee_open_session params;

	pr_debug(" %s :\n", __func__);
	ret = down_interruptible(&tee_drv_data.tee_sem);
	if (0 != ret)
		return ret;
	do {
		ret = copy_from_user(
				&(params.in),
				&(puserparams->in),
				sizeof(params.in));
		if (0 != ret)
			break;
		if ((!tee_drv_data.tee_session) ||
				(!tee_drv_data.tee_tci)) {
			pr_err(" null data\n");
			break;
		}
		pr_debug("params.in.tci_useraddr 0x%llx\n",
				params.in.tci_useraddr);
		pr_debug("params.in.tl.virtaddr 0x%llx\n",
				params.in.tl.virtaddr);
		/* identify tci memory with sessionid */
		for (i = 0; i < MAX_SESSIONS_SUPPORTED; i++) {
			if (tee_drv_data.tee_tci[i].uid == instance->uid) {
				if (tee_drv_data.tee_tci[i].useraddr ==
						params.in.tci_useraddr)
					break;
			}
		}
		tee_range_check(i);
		cmdbuf.cmdopen.servicephyaddrhi =
			high_adddress(
					(void *)params.in.tl.physaddr);
		cmdbuf.cmdopen.servicephyaddrlo =
			low_adddress(
					(void *)params.in.tl.physaddr);
		cmdbuf.cmdopen.servicelen = params.in.tl.length;
		cmdbuf.cmdopen.tcibufphyaddrhi =
			high_adddress(
					(void *)tee_drv_data.tee_tci[i].
					physaddr);
		cmdbuf.cmdopen.tcibufphyaddrlo =
			low_adddress(
				(void *)tee_drv_data.tee_tci[i].physaddr);
		cmdbuf.cmdopen.tcibuflen = tee_drv_data.tee_tci[i].length;

		flush_buffer((void *)params.in.tl.virtaddr,
				params.in.tl.length);
		psp_comm_send_buffer(TEE_CLIENT_TYPE,
				(union tee_cmd_buf *)&cmdbuf,
				sizeof(union tee_cmd_buf),
				TEE_CMD_ID_OPEN_SESSION);
		params.out.sessionid = cmdbuf.respopen.sessionid;
		tee_drv_data.tee_tci[i].sessionid = params.out.sessionid;
		pr_debug(" sid %d\n", tee_drv_data.tee_tci[i].sessionid);
		for (i = 0; i < MAX_SESSIONS_SUPPORTED; i++) {
			if (tee_drv_data.tee_session[i].sessionid == 0x0) {
				tee_drv_data.tee_session[i].sessionid =
					params.out.sessionid;
				init_waitqueue_head(&tee_drv_data.
						tee_session[i].wait);
				tee_drv_data.tee_session[i].
						status = TEE_NOTIFY_NONE;
				break;
			}
		}
		ret = copy_to_user(
				&(puserparams->out),
				&(params.out),
				sizeof(params.out));
		if (0 != ret)
			break;
	} while (FALSE);
	up(&tee_drv_data.tee_sem);
	pr_info(" %s : open session handled\n", __func__);
	return ret;
}

static int handle_close_session(struct tee_instance *instance,
		union tee_close_session *puserparams)
{
	int ret = 0, i, j;
	union tee_close_session params;

	pr_debug(" %s :\n", __func__);
	ret = down_interruptible(&tee_drv_data.tee_sem);
	if (0 != ret)
		return ret;
	do {
		ret = copy_from_user(
				&(params.in),
				&(puserparams->in),
				sizeof(params.in));
		if (0 != ret)
			break;
		if ((!tee_drv_data.tee_session) || (!tee_drv_data.tee_tci)) {
			pr_err(" null data\n");
			break;
		}
		tee_close_session(params.in.sessionid);
		for (i = 0; i < MAX_SESSIONS_SUPPORTED; i++) {
			if (tee_drv_data.tee_session[i].sessionid
					== params.in.sessionid) {
				for (j = 0; j < MAX_BUFFERS_MAPPED; j++)
					tee_clean_map(i, j);
				tee_drv_data.tee_session[i].sessionid = 0x0;
				break;
			}
		}
	} while (FALSE);
	up(&tee_drv_data.tee_sem);
	pr_debug(" %s : ret %d\n", __func__, ret);
	return ret;
}

static int handle_mmap_details(struct tee_instance *instance,
		union tee_mmap_info *puserparams)
{
	int ret = 0;
	union tee_mmap_info params;

	pr_debug(" %s :\n", __func__);
	ret = down_interruptible(&instance->sem);
	if (0 != ret)
		return ret;
	do {
		params.out.mmap.virtaddr = instance->mmap.virtaddr;
		params.out.mmap.physaddr = instance->mmap.physaddr;
		params.out.mmap.length = instance->mmap.length;
		ret = copy_to_user(
				&(puserparams->out),
				&(params.out),
				sizeof(params.out));
		if (0 != ret)
			break;
	} while (FALSE);
	up(&instance->sem);
	pr_debug(" %s : ret %d\n", __func__, ret);
	return ret;
}

static int handle_mmap_free(union tee_mmap_free *puserparams)
{
	int ret = 0;
	union tee_mmap_free params;

	pr_debug(" %s :\n", __func__);
	do {
		ret = copy_from_user(
				&(params.in),
				&(puserparams->in),
				sizeof(params.in));
		if (0 != ret)
			break;
		if ((!tee_drv_data.tee_session) || (!tee_drv_data.tee_tci)) {
			pr_err(" null data\n");
			break;
		}
		tee_free_memory((void *)params.in.mmap.virtaddr,
				params.in.mmap.length);
	} while (FALSE);
	pr_debug(" %s : ret %d\n", __func__, ret);
	return ret;
}

static int handle_map_info(union tee_map_info *puserparams)
{
	int ret = 0, i, j, k = 0;
	union tee_map_info params;

	pr_debug(" %s :\n", __func__);
	ret = down_interruptible(&tee_drv_data.tee_sem);
	if (0 != ret)
		return ret;
	do {
		ret = copy_from_user(
				&(params.in),
				&(puserparams->in),
				sizeof(params.in));
		if (0 != ret)
			break;
		if ((!tee_drv_data.tee_session) || (!tee_drv_data.tee_tci)) {
			pr_err(" null data\n");
			break;
		}
		for (i = 0; i < MAX_SESSIONS_SUPPORTED; i++) {
			if (tee_drv_data.tee_session[i].sessionid ==
							params.in.sessionid) {
				for (j = 0; j < MAX_BUFFERS_MAPPED; j++) {
					if (tee_drv_data.tee_session[i]
						.map_secaddr[j] ==
						params.in.securevirtual) {
						params.out.virtaddr =
							tee_drv_data.
						tee_session[i].map_virtaddr[j];
						params.out.useraddr =
							tee_drv_data.
						tee_session[i].map_useraddr[j];
						k = 1;
						break;
					}
				}
			}
			if (k == 1)
				break;
		}
		tee_range_check(i);
		pr_debug(" %s : map sva 0x%llx kva 0x%llx\n",
				__func__,
				tee_drv_data.tee_session[i].map_secaddr[j],
				tee_drv_data.tee_session[i].map_virtaddr[j]);
		ret = copy_to_user(
				&(puserparams->out),
				&(params.out),
				sizeof(params.out));
		if (0 != ret)
			break;
	} while (FALSE);
	up(&tee_drv_data.tee_sem);
	pr_debug(" %s : ret %d\n", __func__, ret);
	return ret;
}

static int handle_tci_store(struct tee_instance *instance,
		union tee_tci_store *puserparams)
{
	int ret = 0, i;
	union tee_tci_store params;

	pr_debug(" %s :\n", __func__);
	ret = down_interruptible(&tee_drv_data.tee_sem);
	if (0 != ret)
		return ret;
	do {
		ret = copy_from_user(
				&(params.in),
				&(puserparams->in),
				sizeof(params.in));
		if (0 != ret)
			break;
		if ((!tee_drv_data.tee_session) || (!tee_drv_data.tee_tci)) {
			pr_err(" null data\n");
			break;
		}
		pr_debug(" %s : tci ua 0x%llx kva 0x%llx uid 0x%llx\n",
				__func__, (u64)params.in.useraddr,
				(u64)params.in.tci.virtaddr,
				instance->uid);
		for (i = 0; i < MAX_SESSIONS_SUPPORTED; i++) {
			if (tee_drv_data.tee_tci[i].uid == 0x0) {
				tee_drv_data.tee_tci[i].uid = instance->uid;
				tee_drv_data.tee_tci[i].virtaddr
					= (u64)params.in.tci.virtaddr;
				tee_drv_data.tee_tci[i].physaddr
					= (u64)params.in.tci.physaddr;
				tee_drv_data.tee_tci[i].useraddr =
							params.in.useraddr;
				tee_drv_data.tee_tci[i].length =
							params.in.tci.length;
				break;
			}
		}
		pr_debug(" %s : tci ua 0x%llx kva 0x%llx uid 0x%llx\n",
				__func__, (u64)params.in.useraddr,
				(u64)tee_drv_data.tee_tci[i].virtaddr,
				instance->uid);
		tee_range_check(i);
		pr_debug(" %s : tci location %d and ua 0x%llx\n",
				__func__, i,
				(u64)tee_drv_data.tee_tci[i].useraddr);
	} while (FALSE);
	up(&tee_drv_data.tee_sem);
	pr_debug(" %s : ret %d\n", __func__, ret);
	return ret;
}

static int handle_tci_info(struct tee_instance *instance,
		union tee_tci_info *puserparams)
{
	int ret = 0, i;
	union tee_tci_info params;

	pr_debug(" %s :\n", __func__);
	ret = down_interruptible(&tee_drv_data.tee_sem);
	if (0 != ret)
		return ret;
	do {
		ret = copy_from_user(
				&(params.in),
				&(puserparams->in),
				sizeof(params.in));
		if (0 != ret)
			break;
		if ((!tee_drv_data.tee_session) || (!tee_drv_data.tee_tci)) {
			pr_err(" null data\n");
			break;
		}
		for (i = 0; i < MAX_SESSIONS_SUPPORTED; i++) {
			if ((tee_drv_data.tee_tci[i].uid == instance->uid) &&
					(tee_drv_data.tee_tci[i].useraddr ==
							params.in.useraddr)) {
				params.out.tci.virtaddr
					= tee_drv_data.tee_tci[i].virtaddr;
				params.out.tci.physaddr
					= tee_drv_data.tee_tci[i].physaddr;
				params.out.tci.length = tee_drv_data
							.tee_tci[i].length;
				break;
			}
		}
		tee_range_check(i);
		pr_debug(" %s : tci ua 0x%llx kva 0x%llx\n",
				__func__,
				(u64)params.in.useraddr,
				(u64)params.out.tci.virtaddr);
		/* send session id to user */
		ret = copy_to_user(
				&(puserparams->out),
				&(params.out),
				sizeof(params.out));
		if (0 != ret)
			break;
	} while (FALSE);
	up(&tee_drv_data.tee_sem);
	pr_debug(" %s : ret %d\n", __func__, ret);
	return ret;
}

static int handle_tci_free(struct tee_instance *instance,
		union tee_tci_free *puserparams)
{
	int ret = 0, i;
	union tee_tci_free params;

	pr_debug(" %s :\n", __func__);
	ret = down_interruptible(&tee_drv_data.tee_sem);
	if (0 != ret)
		return ret;
	do {
		ret = copy_from_user(
				&(params.in),
				&(puserparams->in),
				sizeof(params.in));
		if (0 != ret)
			break;
		if ((!tee_drv_data.tee_session) || (!tee_drv_data.tee_tci)) {
			pr_err(" null data\n");
			break;
		}
		pr_debug(" %s : tci ua 0x%llx uid 0x%llx\n",
				__func__,
				(u64)params.in.useraddr,
				instance->uid);
		for (i = 0; i < MAX_SESSIONS_SUPPORTED; i++) {
			if ((tee_drv_data.tee_tci[i].uid == instance->uid) &&
					(tee_drv_data.tee_tci[i].useraddr ==
							params.in.useraddr)) {
				tee_clean_tci(i);
				break;
			}
		}
		tee_range_check(i);
		pr_debug(" %s : tci location %d\n", __func__, i);
	} while (FALSE);
	up(&tee_drv_data.tee_sem);
	pr_debug(" %s : ret %d\n", __func__, ret);
	return ret;
}

static int handle_version_info(union tee_tbase_version_info *puserparams)
{
	int ret = 0;
	union tee_tbase_version_info params;

	pr_debug(" %s :\n", __func__);
	ret = down_interruptible(&tee_drv_data.tee_sem);
	if (0 != ret)
		return ret;
	do {
		psp_comm_send_buffer(TEE_CLIENT_TYPE,
				(union tee_cmd_buf *)&cmdbuf,
				sizeof(union tee_cmd_buf),
				 TEE_CMD_ID_GET_VERSION_INFO);
		params.out.versionmci =
			cmdbuf.respgetversioninfo.
			versioninfo.versionmci;
		params.out.versionso =
			cmdbuf.respgetversioninfo.
			versioninfo.versionso;
		params.out.versionmclf =
			cmdbuf.respgetversioninfo.
			versioninfo.versionmclf;
		params.out.versioncontainer =
			cmdbuf.respgetversioninfo.
			versioninfo.versioncontainer;
		params.out.versionmcconfig =
			cmdbuf.respgetversioninfo.
			versioninfo.versionmcconfig;
		params.out.versiontlapi =
			cmdbuf.respgetversioninfo.
			versioninfo.versiontlapi;
		params.out.versiondrapi =
			cmdbuf.respgetversioninfo.
			versioninfo.versiondrapi;
		params.out.versioncmp =
			cmdbuf.respgetversioninfo.
			versioninfo.versioncmp;
		memcpy(params.out.productid,
				cmdbuf.respgetversioninfo.
				versioninfo.productid,
				TEE_TBASE_PRODUCT_ID_LEN);
		pr_debug(" tee version %s\n", params.out.productid);
		ret = copy_to_user(
				&(puserparams->out),
				&(params.out),
				sizeof(params.out));
		if (0 != ret)
			break;
	} while (FALSE);
	up(&tee_drv_data.tee_sem);
	pr_debug(" %s : ret %d\n", __func__, ret);
	return ret;
}

static int handle_session_error(union tee_get_session_error *puserparams)
{
	int ret = 0, i;
	union tee_get_session_error params;

	pr_debug(" %s :\n", __func__);
	ret = down_interruptible(&tee_drv_data.tee_sem);
	if (0 != ret)
		return ret;
	do {
		ret = copy_from_user(
				&(params.in),
				&(puserparams->in),
				sizeof(params.in));
		if (0 != ret)
			break;
		if ((!tee_drv_data.tee_session) || (!tee_drv_data.tee_tci)) {
			pr_err(" null data\n");
			break;
		}
		i = tee_session_location(params.in.sessionid);
		if (i == MAX_SESSIONS_SUPPORTED)
			break;
		params.out.error = tee_drv_data.tee_error;
		ret = copy_to_user(
				&(puserparams->out),
				&(params.out),
				sizeof(params.out));
		if (0 != ret)
			break;
	} while (FALSE);
	up(&tee_drv_data.tee_sem);
	pr_debug(" %s : ret %d\n", __func__, ret);
	return ret;
}

static int handle_session_clean(union tee_get_session_clean *puserparams)
{
	int ret = 0, i, j;
	union tee_get_session_clean params;

	pr_debug(" %s :\n", __func__);
	ret = down_interruptible(&tee_drv_data.tee_sem);
	if (0 != ret)
		return ret;
	do {
		if ((!tee_drv_data.tee_session) || (!tee_drv_data.tee_tci)) {
			pr_err(" null data\n");
			break;
		}
		params.out.session_mask = 0x0;
		params.out.session_number = 0x0;
		for (i = 0; i < MAX_SESSIONS_SUPPORTED; i++) {
			if (tee_drv_data.tee_session[i].sessionid != 0x0) {
				pr_debug(
				" %s : sid:0x%llx position:%d closing\n",
					__func__,
				tee_drv_data.tee_session[i].sessionid, i);
				tee_terminate_wait(i);
				tee_close_session(tee_drv_data.
						tee_session[i].sessionid);
				/* copy session number and mask to user */
				params.out.session_mask |=
				1<<tee_drv_data.tee_session[i].sessionid;
				params.out.session_number++;
				tee_drv_data.tee_session[i].sessionid = 0x0;
				for (j = 0; j < MAX_BUFFERS_MAPPED; j++)
					tee_clean_map(i, j);
				tee_clean_tci(i);
			}
		}
		ret = copy_to_user(
				&(puserparams->out),
				&(params.out),
				sizeof(params.out));
		if (0 != ret)
			break;
	} while (FALSE);
	up(&tee_drv_data.tee_sem);
	pr_debug(" %s : ret %d\n", __func__, ret);
	return ret;
}

static int handle_map_buffer(union tee_map_buffer *puserparams)
{
	int ret = 0, i, j, k = 0;
	union tee_map_buffer params;

	pr_debug(" %s :\n", __func__);
	ret = down_interruptible(&tee_drv_data.tee_sem);
	if (0 != ret)
		return ret;
	do {
		ret = copy_from_user(
				&(params.in),
				&(puserparams->in),
				sizeof(params.in));
		if (0 != ret)
			break;
		if ((!tee_drv_data.tee_session) || (!tee_drv_data.tee_tci)) {
			pr_err(" null data\n");
			break;
		}
		cmdbuf.cmdmap.sessionid = params.in.sessionid;
		cmdbuf.cmdmap.membuflen = params.in.map_buffer.length;
		cmdbuf.cmdmap.memphyaddrhi =
			high_adddress((void *)
					params.in.map_buffer.physaddr);
		cmdbuf.cmdmap.memphyaddrlo =
			low_adddress((void *)
					params.in.map_buffer.physaddr);
		pr_debug(" %s : sid 0x%x map sva 0x%x pa 0x%x length 0x%x\n",
				__func__,
				cmdbuf.cmdmap.sessionid,
				(u32)cmdbuf.respmap.securevirtadr,
				(u32)cmdbuf.cmdmap.memphyaddrlo,
				cmdbuf.cmdmap.membuflen);
		psp_comm_send_buffer(TEE_CLIENT_TYPE,
				(union tee_cmd_buf *)&cmdbuf,
				sizeof(union tee_cmd_buf), TEE_CMD_ID_MAP);
		/* save map buffer context for given session */
		for (i = 0; i < MAX_SESSIONS_SUPPORTED; i++) {
			if (tee_drv_data.tee_session[i].sessionid ==
						params.in.sessionid) {
				for (j = 0; j < MAX_BUFFERS_MAPPED; j++) {
					if (tee_drv_data.tee_session[i]
								.map_secaddr[j]
							== 0x0) {
						tee_drv_data.tee_session[i]
							.map_physaddr[j] =
							(u64)params.in.
							map_buffer.physaddr;
						tee_drv_data.tee_session[i]
							.map_virtaddr[j] =
							(u64)params.in.
							map_buffer.virtaddr;
						tee_drv_data.tee_session[i]
							.map_secaddr[j] =
							cmdbuf.respmap.
							securevirtadr;
						tee_drv_data.tee_session[i]
							.map_length[j] =
							params.in.map_buffer
								.length;
						tee_drv_data.tee_session[i]
							.map_useraddr[j] =
							params.in.useraddr;
						k = 1;
						break;
					}
				}
			}
			if (k == 1)
				break;
		}
		tee_range_check(i);
		params.out.securevirtual = cmdbuf.respmap.securevirtadr;
		pr_debug(" %s : map sva 0x%x kva 0x%llx length 0x%llx\n",
				__func__,
				cmdbuf.respmap.securevirtadr,
				params.in.map_buffer.virtaddr,
				params.in.map_buffer.length);
		ret = copy_to_user(
				&(puserparams->out),
				&(params.out),
				sizeof(params.out));
		if (0 != ret)
			break;
	} while (FALSE);
	up(&tee_drv_data.tee_sem);
	pr_debug(" %s : ret %d\n", __func__, ret);
	return ret;
}

static int handle_unmap_buffer(union tee_unmap_buffer *puserparams)
{
	int ret = 0, i, j, k = 0;
	union tee_unmap_buffer params;

	pr_debug(" %s :\n", __func__);
	ret = down_interruptible(&tee_drv_data.tee_sem);
	if (0 != ret)
		return ret;
	do {
		ret = copy_from_user(
				&(params.in),
				&(puserparams->in),
				sizeof(params.in));
		if (0 != ret)
			break;
		if ((!tee_drv_data.tee_session) || (!tee_drv_data.tee_tci)) {
			pr_err(" null data\n");
			break;
		}
		cmdbuf.cmdunmap.sessionid = params.in.sessionid;
		cmdbuf.cmdunmap.securevirtadr = params.in.securevirtual;
		psp_comm_send_buffer(TEE_CLIENT_TYPE,
					 (union tee_cmd_buf *)&cmdbuf,
						sizeof(union tee_cmd_buf),
						TEE_CMD_ID_UNMAP);
		for (i = 0; i < MAX_SESSIONS_SUPPORTED; i++) {
			if (tee_drv_data.tee_session[i].sessionid ==
							params.in.sessionid) {
				for (j = 0; j < MAX_BUFFERS_MAPPED; j++) {
					if (tee_drv_data.tee_session[i]
							.map_secaddr[j] ==
						params.in.securevirtual) {
						pr_info(
					" %s : map sva 0x%llx kva 0x%llx\n",
							__func__,
							tee_drv_data.
							tee_session[i].
							map_secaddr[j],
							tee_drv_data.
							tee_session[i].
							map_virtaddr[j]);
						tee_clean_map(i, j);
						k = 1;
						break;
					}
				}
			}
			if (k == 1)
				break;
		}
		tee_range_check(i);
	} while (FALSE);
	up(&tee_drv_data.tee_sem);
	pr_debug(" %s : ret %d\n", __func__, ret);
	return ret;
}

static int handle_notify(struct tee_instance *instance,
		union tee_notify *puserparams)
{
	int ret = 0, i, j;
	union tee_notify params;

	pr_debug(" %s :\n", __func__);
	ret = down_interruptible(&tee_drv_data.tee_sem);
	if (0 != ret)
		return ret;
	do {
		ret = copy_from_user(
				&(params.in),
				&(puserparams->in),
				sizeof(params.in));
		if (0 != ret)
			break;
		if ((!tee_drv_data.tee_session) || (!tee_drv_data.tee_tci)) {
			pr_err(" null data\n");
			break;
		}
		pr_debug(" %s : sid 0x%llx\n",
				__func__, params.in.sessionid);
		psp_comm_send_notification(TEE_CLIENT_TYPE,
					(uint64_t *)&params.in.sessionid);
		i = tee_session_location(params.in.sessionid);
		if (i == MAX_SESSIONS_SUPPORTED)
			break;
		tee_drv_data.tee_session[i].status = TEE_NOTIFY_DONE;
		for (j = 0; j < MAX_BUFFERS_MAPPED; j++) {
			flush_buffer((void *)tee_drv_data.tee_session[i]
							.map_virtaddr[j],
					tee_drv_data.tee_session[i]
							.map_length[j]);
		}
		for (i = 0; i < MAX_SESSIONS_SUPPORTED; i++) {
			if (tee_drv_data.tee_tci[i].sessionid ==
							params.in.sessionid) {
				flush_buffer((void *)tee_drv_data
							.tee_tci[i].virtaddr,
						tee_drv_data
							.tee_tci[i].length);
				invalidate_buffer((void *)tee_drv_data
							.tee_tci[i].virtaddr,
						tee_drv_data
							.tee_tci[i].length);
			}
		}
	} while (FALSE);
	up(&tee_drv_data.tee_sem);
	pr_debug(" %s : ret %d\n", __func__, ret);
	return ret;
}

static int handle_waitfornotification(struct tee_instance *instance,
		union tee_waitfornotification *puserparams)
{
	int ret = 0, i, j, timeout;
	union tee_waitfornotification params;

	pr_debug(" %s :\n", __func__);
	do {
		ret = down_interruptible(&tee_drv_data.tee_sem);
		if (0 != ret)
			break;
		ret = copy_from_user(
				&(params.in),
				&(puserparams->in),
				sizeof(params.in));
		if (0 != ret) {
			up(&tee_drv_data.tee_sem);
			break;
		}
		i = tee_session_location(params.in.sessionid);
		if (i == MAX_SESSIONS_SUPPORTED) {
			up(&tee_drv_data.tee_sem);
			break;
		}
		if (tee_drv_data.tee_session[i].status == TEE_NOTIFY_NONE) {
			pr_err(" %s : sid 0x%llx no notify in place\n",
								__func__,
					params.in.sessionid);
			up(&tee_drv_data.tee_sem);
			break;
		}
		up(&tee_drv_data.tee_sem);
		pr_debug(" %s : sid 0x%llx\n", __func__,
				params.in.sessionid);
		pr_debug(" %s : waiting for interrupt\n", __func__);
		pr_debug(" %s : timeout 0x%llx\n", __func__,
				params.in.timeout);
		timeout = params.in.timeout;
		if (timeout < 0x0)
			timeout = TEE_RESPONSE_TIMEOUT;
		/* negative timeout is as good as infinite timeout */
		wait_event_interruptible_timeout(tee_drv_data
					.tee_session[i].wait,
				tee_drv_data
					.tee_session[i].status
				== TEE_NOTIFY_CLEAR,
				msecs_to_jiffies(timeout));
		ret = down_interruptible(&tee_drv_data.tee_sem);
		if (0 != ret)
			break;
		tee_drv_data.tee_session[i].status = TEE_NOTIFY_NONE;
		for (j = 0; j < MAX_BUFFERS_MAPPED; j++) {
			invalidate_buffer(
					(void *)tee_drv_data.tee_session[i]
							.map_virtaddr[j],
					tee_drv_data.tee_session[i]
							.map_length[j]);
		}
		for (i = 0; i < MAX_SESSIONS_SUPPORTED; i++) {
			if (tee_drv_data.tee_tci[i].sessionid ==
						params.in.sessionid) {
				flush_buffer(
						(void *)tee_drv_data
							.tee_tci[i].virtaddr,
						tee_drv_data
							.tee_tci[i].length);
				invalidate_buffer(
						(void *)tee_drv_data
							.tee_tci[i].virtaddr,
						tee_drv_data
							.tee_tci[i].length);
			}
		}
		up(&tee_drv_data.tee_sem);
	} while (FALSE);
	pr_debug(" %s : ret %d\n", __func__, ret);
	return ret;
}

long tee_ioctl(struct file *pfile, unsigned int cmd, unsigned long arg)
{
	int ret;
	struct tee_instance *instance = pfile->private_data;

	pr_debug(" %s command %d :\n", __func__, _IOC_NR(cmd));
	switch (_IOC_NR(cmd)) {
	case TEE_MMAP_DETAILS:
			ret = handle_mmap_details(instance,
					(union tee_mmap_info *)arg);
			break;
	case TEE_MMAP_FREE:
			ret = handle_mmap_free(
					(union tee_mmap_free *)arg);
			break;
	case TEE_OPEN_SESSION:
			ret = handle_open_session(instance,
					(union tee_open_session *)arg);
			break;
	case TEE_CLOSE_SESSION:
			ret = handle_close_session(instance,
					(union tee_close_session *)arg);
			break;
	case TEE_MAP_BUFFER:
			ret = handle_map_buffer((union tee_map_buffer *)arg);
			break;
	case TEE_MAP_INFO:
			ret = handle_map_info((union tee_map_info *)arg);
			break;
	case TEE_UNMAP_BUFFER:
			ret = handle_unmap_buffer((union tee_unmap_buffer *)
									arg);
			break;
	case TEE_NOTIFY:
			ret = handle_notify(instance,
					(union tee_notify *)arg);
			break;
	case TEE_WAIT_FOR_NOTIFICATION:
			ret = handle_waitfornotification(instance,
					(union tee_waitfornotification *)arg);
			break;
	case TEE_TCI_STORE:
			ret = handle_tci_store(instance,
					(union tee_tci_store *)arg);
			break;
	case TEE_TCI_INFO:
			ret = handle_tci_info(instance,
					(union tee_tci_info *)arg);
			break;
	case TEE_TCI_FREE:
			ret = handle_tci_free(instance,
					(union tee_tci_free *)arg);
			break;
	case TEE_VERSION_INFO:
			ret = handle_version_info(
					(union tee_tbase_version_info *)arg);
			break;
	case TEE_SESSION_ERROR:
			ret = handle_session_error(
					(union tee_get_session_error *)arg);
			break;
	case TEE_SESSION_CLEAN:
			ret = handle_session_clean(
					(union tee_get_session_clean *)arg);
			break;
	default:
			ret = -EFAULT;
			break;
	}
	pr_debug(" %s : ret %d\n", __func__, ret);
	return (int)ret;
}

int tee_callbackfunc(void *notification_data)
{
	int i = 0;

	for (i = 0; i < MAX_SESSIONS_SUPPORTED; i++) {
		if (tee_drv_data.tee_session[i].sessionid ==
					*(u32 *)notification_data) {
			tee_drv_data.tee_session[i].status
				= TEE_NOTIFY_CLEAR;
			wake_up_interruptible(
					&tee_drv_data.tee_session[i].wait);
		}
	}
	return 0;
}

static int __init tee_init(
		void
		)
{
	int ret = -EIO;

	do {
		ret = misc_register(&tee_device);
		if (0 != ret)
			break;
		pr_debug(" %s : ret:%d Minor_number : %d\n", __func__,
							ret, tee_device.minor);
		tee_kobj = kobject_create_and_add("tee", kernel_kobj);
		if (!tee_kobj) {
			ret = -ENOMEM;
			misc_deregister(&tee_device);
			break;
		}
		ret = sysfs_create_group(tee_kobj, &tee_attribute_group);
		if (ret) {
			kobject_put(tee_kobj);
			misc_deregister(&tee_device);
			break;
		}
		ret = psp_comm_register_client(TEE_CLIENT_TYPE,
						&tee_callbackfunc);
		if (ret) {
			pr_err(" %s : Error register client", __func__);
			sysfs_remove_group(tee_kobj, &tee_attribute_group);
			kobject_put(tee_kobj);
			misc_deregister(&tee_device);
		}
		sema_init(&tee_drv_data.tee_sem, 0x1);
	} while (FALSE);
	pr_info(" %s : tee driver initialized ret : %d\n", __func__,
								ret);
	return (int)ret;
}

static void __exit tee_exit(
		void
		)
{
	sysfs_remove_group(tee_kobj, &tee_attribute_group);
	kobject_put(tee_kobj);
	misc_deregister(&tee_device);
	psp_comm_unregister_client(TEE_CLIENT_TYPE);
}

module_init(tee_init);
module_exit(tee_exit);

MODULE_AUTHOR("Advanced Micro Devices, Inc.");
MODULE_DESCRIPTION("tee driver");
MODULE_LICENSE("GPL");
