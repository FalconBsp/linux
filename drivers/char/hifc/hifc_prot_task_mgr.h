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

#ifndef SPZ_HIFC_PROT_TASK_MGR_H_
#define SPZ_HIFC_PROT_TASK_MGR_H_

#include <linux/spinlock.h>
#include <linux/completion.h>
#include <linux/kthread.h>
#include <linux/slab.h>

struct hifc_data;

/* Priority is higher while value is larger */
enum TASK_PRI_TAG {
	TASK_PRI_MESG = 0,
	TASK_PRI_FIFO,
	TASK_PRI_IRQ,
	TASK_PRI_MAX
};

enum TASK_TYP_TAG {
	TASK_TYP_SYNC = 0,
	TASK_TYP_ASYNC,
};

typedef void (*task_func)(struct hifc_data *hdata, void *data);

struct hifc_prot_task {
	struct list_head   tsk;
	enum TASK_PRI_TAG  t_pri;
	enum TASK_TYP_TAG  t_typ;
	task_func          t_func;
	void               *t_data;
	struct             completion done;
	int32_t            result;
};

struct hifc_prot_req {
	struct list_head req;
	struct hifc_prot_task *tsk;
};

struct hifc_prot_task_tq {
	struct list_head head;
	spinlock_t lock;
	wait_queue_head_t todo;
};

struct hifc_prot_wt_rq {
	struct list_head head;
	spinlock_t lock;
};

struct hifc_prot_tmgr_ops {
	struct hifc_prot_task *
		(*spz_tmgr_create_task)(struct hifc_prot_tmgr *tmgr,
					task_func func, void *data,
					enum TASK_PRI_TAG pri);
	struct hifc_prot_req *
		(*spz_tmgr_create_request)(struct hifc_prot_tmgr *tmgr,
					   struct hifc_prot_task *task);
	int32_t (*spz_prot_tmgr_submit_wt_req)(struct hifc_prot_tmgr *tmgr,
					       struct hifc_data *hdata,
					       struct hifc_prot_task *task,
					       struct hifc_prot_req *request);
	void    (*spz_prot_tmgr_submit_task)(struct hifc_prot_tmgr *tmgr,
					     struct hifc_prot_task *task);
	int32_t (*spz_prot_tmgr_submit_sync_task)(struct hifc_prot_tmgr *tmgr,
			struct hifc_prot_task *task);
	int32_t (*spz_prot_tmgr_init)(struct hifc_prot_tmgr *tmgr,
				      struct device *dev,
				      struct hifc_data *hdata);
	int32_t (*spz_prot_tmgr_deinit)(struct hifc_prot_tmgr *tmgr);
};

struct tmgr_kcache_obj {
	struct kmem_cache *task_cache;
	struct kmem_cache *req_cache;
};

struct hifc_prot_tmgr {
	struct task_struct *tq_thread;
	struct completion tq_thread_stop;
	struct hifc_prot_task_tq tqueue; /* task queue */
	struct hifc_prot_wt_rq wt_rqueue; /* write request queue */
	struct hifc_prot_tmgr_ops mops;
	struct tmgr_kcache_obj *kcobj;
};

extern struct hifc_prot_tmgr spz_hifc_prot_tmgr;

#endif /* SPZ_HIFC_PROT_TASK_MGR_H_ */
