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
#include <linux/kthread.h>
#include <linux/device.h>

#include "hifc.h"
#include "hifc_prot_task_mgr.h"

static struct hifc_prot_task *create_task(struct hifc_prot_tmgr *tmgr,
					  task_func func, void *data,
					  enum TASK_PRI_TAG pri)
{
	struct hifc_prot_task *task;
	struct tmgr_kcache_obj *kcache_obj = tmgr->kcobj;

	task = kmem_cache_alloc(kcache_obj->task_cache,
				GFP_KERNEL | __GFP_ZERO);

	if (!task) {
		HIFC_ERR("%s: no memory to allocate hifc_prot_task\n",
		       __func__);
		return NULL;
	}

	task->t_func = func;
	task->t_data = data;
	task->t_pri  = pri;
	task->t_typ  = TASK_TYP_ASYNC;

	return task;
}

static void destroy_task(struct hifc_prot_tmgr *tmgr,
			 struct hifc_prot_task **task)
{
	struct tmgr_kcache_obj *kcache_obj = tmgr->kcobj;

	if (*task) {
		kmem_cache_free(kcache_obj->task_cache, *task);
		*task = NULL;
	}
}

static void submit_task(struct hifc_prot_tmgr *tmgr,
			struct hifc_prot_task *task)
{
	struct list_head *ptr        = NULL;
	struct hifc_prot_task *entry = NULL;
	struct hifc_prot_task_tq *tq = &tmgr->tqueue;

	if (likely(task)) {
		spin_lock_irq(&tq->lock);
		list_for_each(ptr, &tq->head) {
			entry = list_entry(ptr, struct hifc_prot_task, tsk);
			if (entry->t_pri < task->t_pri) {
				list_add_tail(&task->tsk, ptr);
				wake_up(&tq->todo);
				spin_unlock_irq(&tq->lock);
				return;
			}
		}
		list_add_tail(&task->tsk, ptr);
		wake_up(&tq->todo);
		spin_unlock_irq(&tq->lock);
	}
}

static int32_t submit_sync_task(struct hifc_prot_tmgr *tmgr,
				struct hifc_prot_task *task)
{
	int32_t ret = 0;

	if (likely(task)) {
		init_completion(&task->done);
		task->t_typ = TASK_TYP_SYNC;
		submit_task(tmgr, task);
		wait_for_completion(&task->done);

		ret = task->result;

		destroy_task(tmgr, &task);
	}

	return ret;
}

static struct hifc_prot_req *create_request(struct hifc_prot_tmgr *tmgr,
					    struct hifc_prot_task *task)
{
	struct hifc_prot_req *req;
	struct tmgr_kcache_obj *kcache_obj = tmgr->kcobj;

	req = kmem_cache_alloc(kcache_obj->req_cache,
			       GFP_KERNEL | __GFP_ZERO);

	if (!req) {
		HIFC_ERR("%s: no memory to allocate hifc_prot_req\n",
			__func__);
		return NULL;
	}

	req->tsk = task;

	return req;
}

static void destroy_request(struct hifc_prot_tmgr *tmgr,
			    struct hifc_prot_req **request)
{
	struct tmgr_kcache_obj *kcache_obj = tmgr->kcobj;

	if (*request) {
		kmem_cache_free(kcache_obj->req_cache, *request);
		*request = NULL;
	}
}

static int32_t submit_wt_request(struct hifc_prot_tmgr *tmgr,
				 struct hifc_data *hdata,
				 struct hifc_prot_task *task,
				 struct hifc_prot_req *request)
{
	struct hifc_prot_wt_rq *wt_rq = &tmgr->wt_rqueue;
	int32_t ret                   = 0;

	if (likely(request)) {
		init_completion(&request->tsk->done);
		request->tsk->t_typ = TASK_TYP_SYNC;

		spin_lock_irq(&wt_rq->lock);
		list_add_tail(&request->req, &wt_rq->head);
		spin_unlock_irq(&wt_rq->lock);

		submit_task(tmgr, task);

		wait_for_completion(&request->tsk->done);

		ret = request->tsk->result;

		destroy_task(tmgr, &request->tsk);
		destroy_request(tmgr, &request);
	}

	return ret;
}

static int32_t task_proc(void *hdata)
{
	DECLARE_WAITQUEUE(wait, current);
	struct hifc_prot_task *entry = NULL;
	/*    struct hifc_prot_task *temp = NULL; */
	struct hifc_prot_tmgr *tmgr  = ((struct hifc_data *)hdata)->tmgr;
	struct hifc_prot_task_tq *tq = &tmgr->tqueue;

	set_current_state(TASK_INTERRUPTIBLE);

	while (!kthread_should_stop()) {
		add_wait_queue(&tq->todo, &wait);

		if (list_empty(&tq->head))
			schedule();
		else
			set_current_state(TASK_RUNNING);


		remove_wait_queue(&tq->todo, &wait);

		spin_lock_irq(&tq->lock);

		while (!list_empty(&tq->head)) {
			entry = list_entry(tq->head.next,
					   struct hifc_prot_task, tsk);
			/* list_for_each_entry_safe(
				entry, temp, &tq->head, tsk) { */
			list_del(&entry->tsk);
			spin_unlock_irq(&tq->lock);
			entry->t_func(hdata, entry);
			(TASK_TYP_ASYNC == entry->t_typ)
			? destroy_task(tmgr, &entry)
			: complete(&entry->done);
			spin_lock_irq(&tq->lock);
		}
		spin_unlock_irq(&tq->lock);

		set_current_state(TASK_INTERRUPTIBLE);
	}
	set_current_state(TASK_RUNNING);

	complete(&tmgr->tq_thread_stop);

	return 0;
}

int32_t init_hifc_prot_tmgr(struct hifc_prot_tmgr *tmgr,
			    struct device *dev,
			    struct hifc_data *hdata)
{
	struct hifc_prot_task_tq *tq       = &tmgr->tqueue;
	struct hifc_prot_wt_rq *wt_rq      = &tmgr->wt_rqueue;
	struct tmgr_kcache_obj *kcache_obj = tmgr->kcobj;

	spin_lock_init(&tq->lock);
	init_waitqueue_head(&tq->todo);
	INIT_LIST_HEAD(&tq->head);

	spin_lock_init(&wt_rq->lock);
	INIT_LIST_HEAD(&wt_rq->head);

	init_completion(&tmgr->tq_thread_stop);
	tmgr->tq_thread  = kthread_run(task_proc, hdata, dev_name(dev));

	kcache_obj->task_cache = kmem_cache_create("hifc_prot_task",
				 sizeof(struct hifc_prot_task), 0,
				 SLAB_HWCACHE_ALIGN, NULL);
	kcache_obj->req_cache  = kmem_cache_create("hifc_prot_req",
				 sizeof(struct hifc_prot_req), 0,
				 SLAB_HWCACHE_ALIGN, NULL);

	return 0;
}

int32_t deinit_hifc_prot_tmgr(struct hifc_prot_tmgr *tmgr)
{
	int32_t ret = 0;
	struct tmgr_kcache_obj *kcache_obj = tmgr->kcobj;

	ret = kthread_stop(tmgr->tq_thread);
	tmgr->tq_thread = NULL;
	wait_for_completion(&tmgr->tq_thread_stop);

	kmem_cache_destroy(kcache_obj->task_cache);
	kmem_cache_destroy(kcache_obj->req_cache);

	kcache_obj->task_cache = NULL;
	kcache_obj->req_cache  = NULL;

	tmgr->kcobj = NULL;

	return ret;
}

struct tmgr_kcache_obj tkcache_obj;

struct hifc_prot_tmgr spz_hifc_prot_tmgr = {
	.mops = {
		.spz_tmgr_create_task           = create_task,
		.spz_tmgr_create_request        = create_request,
		.spz_prot_tmgr_submit_wt_req    = submit_wt_request,
		.spz_prot_tmgr_submit_task      = submit_task,
		.spz_prot_tmgr_submit_sync_task = submit_sync_task,
		.spz_prot_tmgr_init             = init_hifc_prot_tmgr,
		.spz_prot_tmgr_deinit           = deinit_hifc_prot_tmgr
	},
	.kcobj = &tkcache_obj,
};
