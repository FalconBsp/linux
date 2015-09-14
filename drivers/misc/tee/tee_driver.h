/*
* tee_driver.h - tee api for user space
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
#ifndef _TEE_DRIVER_H_
#define _TEE_DRIVER_H_

#include <linux/ioctl.h>
#include <linux/types.h>
#include <linux/module.h>
#include <linux/vmalloc.h>
#include "tee_comm.h"

#define TEE_DRV_MOD_DEVNODE		"tee"
#define TEE_DRV_MOD_DEVNODE_FULLPATH	"/dev/" TEE_DRV_MOD_DEVNODE
#define TEE_TBASE_PRODUCT_ID_LEN	(64)
#define TEE_CLIENT_TYPE			0
#define TEE_RESPONSE_TIMEOUT		0x5000
/* msp detail */
struct tee_map {
	uint64_t	virtaddr;
	uint64_t	physaddr;
	uint64_t	length;
};

/* mmap */
union tee_mmap_info {
	struct {
	} in;
	struct {
		struct tee_map mmap;
	} out;
};

union tee_mmap_free {
	struct {
		struct tee_map mmap;
	} in;
	struct {
	} out;
};

/* open/close session */
union tee_open_session {
	struct {
		uint64_t tci_useraddr;
		struct tee_map tl;
	} in;
	struct {
		uint64_t sessionid;
	} out;
};

union tee_close_session {
	struct {
		uint64_t sessionid;
	} in;
	struct {
	} out;
};

/* map buffer */
union tee_map_buffer {
	struct {
		uint64_t sessionid;
		uint64_t useraddr;
		struct tee_map map_buffer;
	} in;
	struct {
		uint64_t securevirtual;
	} out;
};

union tee_map_info {
	struct {
		uint64_t sessionid;
		uint64_t securevirtual;
	} in;
	struct {
		uint64_t virtaddr;
		uint64_t useraddr;
	} out;
};

union tee_unmap_buffer {
	struct {
		uint32_t sessionid;
		uint32_t securevirtual;
	} in;
	struct {
	} out;
};

/* notify/wait-for-notify */
union tee_notify {
	struct {
		uint64_t sessionid;
	} in;
	struct {
	} out;
};

union tee_waitfornotification {
	struct {
		uint64_t sessionid;
		int64_t timeout;
	} in;
	struct {
	} out;
};

/* tci buffer */
union tee_tci_store {
	struct {
		uint64_t useraddr;
		struct tee_map tci;
	} in;
	struct {
	} out;
};

union tee_tci_info {
	struct {
		uint64_t useraddr;
	} in;
	struct {
		struct tee_map tci;
	} out;
};

union tee_tci_free {
	struct {
		uint64_t useraddr;
	} in;
	struct {
	} out;
};

/*  version info */
union tee_tbase_version_info {
	struct {
	} in;
	struct {
		char productid[TEE_TBASE_PRODUCT_ID_LEN];
		uint32_t versionmci;
		uint32_t versionso;
		uint32_t versionmclf;
		uint32_t versioncontainer;
		uint32_t versionmcconfig;
		uint32_t versiontlapi;
		uint32_t versiondrapi;
		uint32_t versioncmp;
	} out;
};

/* session error */
union tee_get_session_error {
	struct {
		uint64_t sessionid;
	} in;
	struct {
		uint64_t error;
	} out;
};

/* session error */
enum tee_error {
	TEE_ERR_NONE = 0x0,
	TEE_ERR_UNKNOWN_DEVICE = 0x7,
	TEE_ERR_UNKNOWN_SESSION = 0x8,
	TEE_ERR_UNKNOWN_TCI = 0x9,
	TEE_ERR_INVALID_PARAMETER = 0x11,
};

/* session cleanup */
union tee_get_session_clean {
	struct {
	} in;
	struct {
		uint64_t session_mask;
		uint64_t session_number;
	} out;
};

struct tee_session_context {
	u64 sessionid;
	u64 map_virtaddr[MAX_BUFFERS_MAPPED];
	u64 map_physaddr[MAX_BUFFERS_MAPPED];
	u64 map_useraddr[MAX_BUFFERS_MAPPED];
	u64 map_secaddr[MAX_BUFFERS_MAPPED];
	u64 map_length[MAX_BUFFERS_MAPPED];
	wait_queue_head_t wait;
	u32 error;
	bool status;
};

struct tee_tci_context {
	u32 uid;
	u32 sessionid;
	u64 virtaddr;
	u64 physaddr;
	u64 useraddr;
	u64 length;
};

struct tee_instance {
	struct semaphore sem;
	struct tee_map mmap;
	u64 uid;
};

struct tee_driver_data {
	struct tee_session_context	*tee_session;
	struct tee_tci_context		*tee_tci;
	struct semaphore		tee_sem;
	u32				tee_instance_counter;
	u64				tee_uid;
	u32				tee_error;
};

struct tee_driver_data	tee_drv_data;

#define TEE_MMAP_DETAILS			(0x1)
#define TEE_MMAP_FREE				(0x2)
#define TEE_OPEN_SESSION			(0x3)
#define TEE_CLOSE_SESSION			(0x4)
#define TEE_MAP_BUFFER				(0x5)
#define TEE_MAP_INFO				(0x6)

#define TEE_UNMAP_BUFFER			(0x7)
#define TEE_NOTIFY				(0x8)
#define TEE_WAIT_FOR_NOTIFICATION		(0x9)
#define TEE_TCI_STORE				(0xA)
#define TEE_TCI_INFO				(0xB)

#define TEE_TCI_FREE				(0xC)
#define TEE_VERSION_INFO			(0xD)
#define TEE_SESSION_ERROR			(0xE)
#define TEE_SESSION_CLEAN			(0xF)

#define TEE_DEV_MAGIC				(0xAA)

#define TEE_KMOD_IOCTL_MMAP_DETAILS	_IOWR(TEE_DEV_MAGIC, (0x1), uint64_t)
#define TEE_KMOD_IOCTL_MMAP_FREE	_IOWR(TEE_DEV_MAGIC, (0x2), uint64_t)
#define TEE_KMOD_IOCTL_OPEN_SESSION	_IOWR(TEE_DEV_MAGIC, (0x3), uint64_t)
#define TEE_KMOD_IOCTL_CLOSE_SESSION	_IOWR(TEE_DEV_MAGIC, (0x4), uint64_t)
#define TEE_KMOD_IOCTL_MAP_BUFFER	_IOWR(TEE_DEV_MAGIC, (0x5), uint64_t)
#define TEE_KMOD_IOCTL_MAP_INFO		_IOWR(TEE_DEV_MAGIC, (0x6), uint64_t)

#define TEE_KMOD_IOCTL_UNMAP_BUFFER	_IOWR(TEE_DEV_MAGIC, (0x7), uint64_t)
#define TEE_KMOD_IOCTL_NOTIFY		_IOWR(TEE_DEV_MAGIC, (0x8), uint64_t)
#define TEE_KMOD_IOCTL_WAIT_FOR_NOTIFICAION \
	_IOWR(TEE_DEV_MAGIC, (0x9), uint64_t)
#define TEE_KMOD_IOCTL_TCI_STORE	_IOWR(TEE_DEV_MAGIC, (0xA), uint64_t)
#define TEE_KMOD_IOCTL_TCI_INFO		_IOWR(TEE_DEV_MAGIC, (0xB), uint64_t)

#define TEE_KMOD_IOCTL_TCI_FREE		_IOWR(TEE_DEV_MAGIC, (0xC), uint64_t)
#define TEE_KMOD_IOCTL_VERSION_INFO	_IOWR(TEE_DEV_MAGIC, (0xD), uint64_t)
#define TEE_KMOD_IOCTL_SESSION_ERROR	_IOWR(TEE_DEV_MAGIC, (0xE), uint64_t)
#define TEE_KMOD_IOCTL_SESSION_CLEAN	_IOWR(TEE_DEV_MAGIC, (0xF), uint64_t)

static inline u32 low_adddress(void *addr)
{
	return (u64)addr & 0xffffffff;
}

static inline u32 high_adddress(void *addr)
{
	return ((u64)addr > 0xffffffff) & 0xffffffff;
}

int tee_open(struct inode *pinode, struct file *pfile);
int tee_release(struct inode *pinode, struct file *pfile);
int tee_mmap(struct file *pfile, struct vm_area_struct *pvmarea);
long tee_ioctl(struct file *pfile, unsigned int cmd, unsigned long arg);

ssize_t tee_status(struct kobject *kobj,
		struct kobj_attribute *attr,
		char *buf);
ssize_t tee_reset(struct kobject *kobj,
		struct kobj_attribute *attr, char *buf,
		size_t count);
#endif

