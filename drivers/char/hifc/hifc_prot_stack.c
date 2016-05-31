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
#include <linux/workqueue.h>
#include <linux/sched.h>
#include <linux/spinlock.h>

#include "hifc.h"
#include "hifc_prot_task_mgr.h"

#define PAYLD_LEN_MAX      256
#define INTFA_BUF_LEN      8
#define INTFA_GEPT_BUF_LEN 4
#define INTCLR_BUF_LEN     2
#define INTCLR_COMM        8
#define FIFO_LEN_BUF_LEN   2
#define GEPT_LEN_BUF_LEN   2
#define BITS_PER_BYTE      8

#define INT_FIFO_0         0
#define INT_FIFO_21        21
#define INT_EXT_0          22
#define INT_EXT_8          29
#define INT_COMM           30
#define INT_RESV           31
#define INT_GEPT_0         32
#define INT_GEPT_1         33
#define INT_GEPT_2         34
#define INT_GEPT_3         35
#define INT_GEPT_31        63

#define MESG_LEN_BYTE_NUM  2

enum COMM_ST_TAG {
	COMM_ST_NONE = 0,
	COMM_ST_READY
};

enum RW_BUF_TAG {
	RW_BUF_RD_0 = 0,
	RW_BUF_RD_1,
	RW_BUF_WT_0,
	RW_BUF_WT_1,
	RW_BUF_MAX
};

enum FLG_TYPE_TAG {
	FLG_TYPE_UNLOCK = 0,
	FLG_TYPE_LOCK
};

enum INT_CATEG_TAG {
	INT_CATEG_COMM = 0,
	INT_CATEG_EXT,
	INT_CATEG_GEPT,
	INT_CATEG_FIFO,
	INT_CATEG_MAX
};

static inline enum RW_BUF_TAG get_wt_buf(uint32_t *gept_mask)
{
	static enum RW_BUF_TAG wb = RW_BUF_WT_0;

	wb = ((*gept_mask & (1 << RW_BUF_WT_0)
	     && !(*gept_mask & (1 << RW_BUF_WT_1))))
	     ? RW_BUF_WT_0
	     : RW_BUF_WT_1;

	*gept_mask ^= (1 << RW_BUF_WT_0);
	*gept_mask ^= (1 << RW_BUF_WT_1);

	return wb;
}

static inline enum RW_BUF_TAG get_rd_buf(uint32_t *gept_mask)
{
	static enum RW_BUF_TAG rb = RW_BUF_RD_0;

	rb = ((*gept_mask & 1 << RW_BUF_RD_0)
	     && !(*gept_mask & (1 << RW_BUF_RD_1)))
	     ? RW_BUF_RD_0
	     : RW_BUF_RD_1;

	*gept_mask ^= (1 << RW_BUF_RD_0);
	*gept_mask ^= (1 << RW_BUF_RD_1);

	return rb;
}

static void tsk_r_intfa(struct hifc_data *hdata, void *data)
{
	const struct hifc_bus_ops *bops    = NULL;
	uint64_t **intfa                   = NULL;
	uint8_t recv_buf[HEAD_WRAP_LEN
			 + INTFA_BUF_LEN]  = {0};
	struct hifc_prot_task *task        = (struct hifc_prot_task *)data;
	struct prot_kcache_obj *kcache_obj = hdata->kcobj;
	int32_t ret                        = 0;
	int32_t i                          = 0;
	uint8_t buf_ofst                   = 0;

	WARN_ON(!hdata || !data);
	bops = hdata->bdata->bops;
	intfa = (uint64_t **)task->t_data;

	/* do not check kmem_cache_alloc fail */
	*intfa = kmem_cache_alloc(kcache_obj->intfa_cache,
				  GFP_KERNEL | __GFP_ZERO);

	ret = bops->spz_drv_recv_icmd(hdata->bdata->client,
				      ICMD_FIX_RD_INTFA_8,
				      recv_buf, INTFA_BUF_LEN, 0,
				      FLG_TYPE_UNLOCK, &buf_ofst);

	for (i = 0; i < INTFA_BUF_LEN; ++i) {
		/* TODO magic num */
		**intfa |= ((uint64_t) *(recv_buf + buf_ofst + i))
			   << (i * BITS_PER_BYTE);
	}

	task->result = ret;
}

static void tsk_r_comm(struct hifc_data *hdata, void *data)
{
	const struct hifc_bus_ops *bops    = NULL;
	struct hifc_prot_task *task        = (struct hifc_prot_task *)data;
	uint16_t comm_clr_mask             = 0;
	uint8_t send_buf[HEAD_WRAP_LEN
			 + INTCLR_BUF_LEN] = {0};
	int32_t ret                        = 0;
	int32_t i                          = 0;

	WARN_ON(!hdata || !data);
	bops = hdata->bdata->bops;
	hdata->comm_ready = COMM_ST_READY;
	comm_clr_mask |= (1 << INTCLR_COMM);

	for (i = 0; i < INTCLR_BUF_LEN; ++i) {
		*(send_buf + HEAD_PLACE_HOLDER + i)
			= (uint8_t)(comm_clr_mask >> (i * BITS_PER_BYTE));
	}

	ret = bops->spz_drv_send_icmd(hdata->bdata->client,
				      ICMD_FIX_WT_INT_CLR_2,
				      send_buf,
				      INTCLR_BUF_LEN,
				      0,
				      FLG_TYPE_UNLOCK);
	task->result = ret;
}

static void tsk_s_mask_intfa(struct hifc_data *hdata, void *data)
{
	const struct hifc_bus_ops *bops    = NULL;
	struct hifc_prot_task *task        = (struct hifc_prot_task *)data;
	uint64_t intfa                     = 0;
	uint8_t send_buf[HEAD_WRAP_LEN
			 + INTFA_BUF_LEN]  = {0};
	int32_t ret                        = 0;
	int32_t i                          = 0;

	WARN_ON(!hdata || !data);
	bops = hdata->bdata->bops;
	intfa = *(uint64_t *)(task->t_data);

	for (i = 0; i < INTFA_BUF_LEN; ++i) {
		*(send_buf + HEAD_PLACE_HOLDER + i) =
			(uint8_t)((uint64_t)intfa >> (i * BITS_PER_BYTE));
	}

	ret = bops->spz_drv_send_icmd(hdata->bdata->client,
				      ICMD_FIX_WT_INT_MASK_8,
				      send_buf,
				      INTFA_BUF_LEN,
				      0,
				      FLG_TYPE_UNLOCK);

	task->result = ret;
}

static void tsk_s_unmask_intfa(struct hifc_data *hdata, void *data)
{
	const struct hifc_bus_ops *bops    = NULL;
	struct hifc_prot_task *task        = (struct hifc_prot_task *)data;
	struct prot_kcache_obj *kcache_obj = hdata->kcobj;
	uint64_t *intfa                    = NULL;
	uint8_t send_buf[HEAD_WRAP_LEN
			 + INTFA_BUF_LEN]  = {0};
	int32_t ret                        = 0;
	int32_t i                          = 0;

	WARN_ON(!hdata || !data);
	bops = hdata->bdata->bops;
	intfa = (uint64_t *)(task->t_data);

	/* once comm notify bit masked, do not clear */
	*intfa &= (~((uint64_t)1 << INT_COMM));

	/* wt buffer bits do not unmask here, */
	/* will be unmasked after each wt operation */
	*intfa &= (~((uint64_t)1 << INT_GEPT_2));
	*intfa &= (~((uint64_t)1 << INT_GEPT_3));

	for (i = 0; i < INTFA_BUF_LEN; ++i) {
		*(send_buf + HEAD_PLACE_HOLDER + i) =
			(uint8_t)((uint64_t)(*intfa) >> (i * BITS_PER_BYTE));
	}

	ret = bops->spz_drv_send_icmd(hdata->bdata->client,
				      ICMD_FIX_WT_INT_UNMASK_8,
				      send_buf,
				      INTFA_BUF_LEN,
				      0,
				      FLG_TYPE_UNLOCK);

	task->result = ret;

	if (intfa) {
		kmem_cache_free(kcache_obj->intfa_cache, intfa);
		intfa = NULL;
	}
}

static void tsk_r_fifo(struct hifc_data *hdata, void *data)
{
	const struct hifc_bus_ops *bops          = NULL;
	struct disp_hdlr_data *dhdlr             = NULL;
	struct hifc_prot_task *task              =
						(struct hifc_prot_task *)data;
	uint8_t *fifo_buf                        = NULL;
	uint8_t fifo_len_buf[HEAD_WRAP_LEN
			     + FIFO_LEN_BUF_LEN] = {0};
	uint16_t fifo_len                        = 0;
	int32_t fifo_num                         = 0;
	int32_t ret                              = 0;
	int32_t i                                = 0;
	uint8_t buf_ofst                         = 0;
	uint8_t *mesg_buf                        = NULL;
	uint8_t *mesg_len_low_byte               = NULL;
	uint8_t *mesg_len_high_byte              = NULL;
	uint16_t mesg_len                        = 0;

	WARN_ON(!hdata || !data);
	bops = hdata->bdata->bops;
	dhdlr = hdata->dhdlr;
	fifo_num = (int32_t)(task->t_data);

	ret = bops->spz_drv_recv_icmd(hdata->bdata->client,
				      ICMD_FIX_RD_FIFO_0 + (uint8_t)fifo_num,
				      fifo_len_buf, FIFO_LEN_BUF_LEN, 0,
				      FLG_TYPE_UNLOCK, &buf_ofst);

	if (ret >= 0) {
		for (i = 0; i < FIFO_LEN_BUF_LEN; ++i) {
			fifo_len |=
				((uint16_t) *(fifo_len_buf + buf_ofst + i))
				<< (i * BITS_PER_BYTE);
		}

		fifo_buf = kzalloc(fifo_len + HEAD_WRAP_LEN,
				   GFP_KERNEL | __GFP_DMA);

		if (fifo_buf) {
			ret = bops->spz_drv_recv_icmd(hdata->bdata->client,
						      ICMD_VAR_RD_FIFO_0 +
						      (uint8_t)fifo_num,
						      fifo_buf, fifo_len, 0,
						      FLG_TYPE_UNLOCK,
						      &buf_ofst);
			if (ret >= 0) {
				mesg_len = fifo_len + MESG_LEN_BYTE_NUM;
				mesg_buf = fifo_buf + buf_ofst
					   - MESG_LEN_BYTE_NUM;

				mesg_len_high_byte  = mesg_buf;
				mesg_len_low_byte   = mesg_buf + 1;
				*mesg_len_high_byte =
					(fifo_len & 0xff00) >> BITS_PER_BYTE;
				*mesg_len_low_byte  = fifo_len & 0x00ff;

				ret = dhdlr->spz_disp_recv_mesg(mesg_buf,
								mesg_len);
			}
			kfree(fifo_buf);
			fifo_buf = NULL;
		}
	}

	task->result = ret;
}

static void tsk_r_rd_gept(struct hifc_data *hdata, void *data)
{
	const struct hifc_bus_ops *bops          = NULL;
	struct disp_hdlr_data *dhdlr             = NULL;
	struct hifc_prot_task *task              =
						(struct hifc_prot_task *)data;
	uint8_t *gept_buf                        = NULL;
	uint8_t gept_len_buf[HEAD_WRAP_LEN
			     + GEPT_LEN_BUF_LEN] = {0};
	uint16_t gept_len                        = 0;
	uint8_t icmd                             = ICMD_FIX_RD_NOP;
	uint8_t icmd_get_len                     = ICMD_FIX_RD_NOP;
	int32_t ret                              = 0;
	int32_t i                                = 0;
	uint8_t buf_ofst                         = 0;
	uint8_t *mesg_buf                        = NULL;
	uint8_t *mesg_len_low_byte               = NULL;
	uint8_t *mesg_len_high_byte              = NULL;
	uint16_t mesg_len                        = 0;

	WARN_ON(!hdata || !data);
	bops = hdata->bdata->bops;
	dhdlr = hdata->dhdlr;
	/* TODO GNSS data reading requires AddrOffset? */

	icmd = (RW_BUF_RD_0 == get_rd_buf(&hdata->gept_mask))
	       ? ICMD_VAR_RW_GEPT_0
	       : ICMD_VAR_RW_GEPT_1;

	icmd_get_len = (ICMD_VAR_RW_GEPT_0 == icmd)
		       ? ICMD_FIX_RD_LEN_GEPT_0
		       : ICMD_FIX_RD_LEN_GEPT_1;

	ret = bops->spz_drv_recv_icmd(hdata->bdata->client, icmd_get_len,
				      gept_len_buf, GEPT_LEN_BUF_LEN, 0,
				      FLG_TYPE_UNLOCK, &buf_ofst);

	if (ret >= 0) {
		for (i = 0; i < GEPT_LEN_BUF_LEN; ++i) {
			gept_len |= ((uint16_t) *(gept_len_buf + buf_ofst + i))
				    << (i * BITS_PER_BYTE);
		}

		gept_buf = kzalloc(gept_len + HEAD_WRAP_LEN,
				   GFP_KERNEL | __GFP_DMA);

		if (gept_buf) {
			ret = bops->spz_drv_recv_icmd(hdata->bdata->client,
						      icmd,
						      gept_buf,
						      gept_len,
						      0,
						      FLG_TYPE_UNLOCK,
						      &buf_ofst);

			if (ret >= 0) {
				mesg_len = gept_len + MESG_LEN_BYTE_NUM;
				mesg_buf = gept_buf + buf_ofst
					   - MESG_LEN_BYTE_NUM;

				mesg_len_high_byte  = mesg_buf;
				mesg_len_low_byte   = mesg_buf + 1;
				*mesg_len_high_byte =
					(gept_len & 0xff00) >> BITS_PER_BYTE;
				*mesg_len_low_byte  = gept_len & 0x00ff;

				ret = dhdlr->spz_disp_recv_mesg(mesg_buf,
								mesg_len);
			}
			kfree(gept_buf);
			gept_buf = NULL;
		}
	}

	task->result = ret;
}

static void tsk_r_wt_gept(struct hifc_data *hdata, void *data)
{
	struct hifc_prot_tmgr_ops *tmgr_ops = &hdata->tmgr->mops;
	struct hifc_prot_wt_rq *wt_rq       = &hdata->tmgr->wt_rqueue;
	struct hifc_prot_req *entry         = NULL;

	if (!list_empty(&wt_rq->head)) {
		entry = list_entry(wt_rq->head.next,
				   struct hifc_prot_req, req);

		spin_lock_irq(&wt_rq->lock);
		list_del(&entry->req);
		spin_unlock_irq(&wt_rq->lock);

		tmgr_ops->spz_prot_tmgr_submit_task(&spz_hifc_prot_tmgr,
						    entry->tsk);
	}
}

static void tsk_s_unmask_wt_intfa(struct hifc_data *hdata, void *data)
{
	const struct hifc_bus_ops *bops        = NULL;
	uint32_t wt_buf_mask                   = 0;
	uint8_t send_buf[HEAD_WRAP_LEN
			 + INTFA_GEPT_BUF_LEN] = {0};
	int32_t i                              = 0;

	WARN_ON(!hdata);
	bops = hdata->bdata->bops;
	wt_buf_mask = ((1 << RW_BUF_WT_0) | (1 << RW_BUF_WT_1));

	for (i = 0; i < INTFA_GEPT_BUF_LEN; ++i) {
		*(send_buf + HEAD_PLACE_HOLDER + i)
			= (uint8_t)(wt_buf_mask >> (i * BITS_PER_BYTE));
	}

	bops->spz_drv_send_icmd(hdata->bdata->client,
				ICMD_FIX_WT_INT_UNMASK_4,
				send_buf,
				INTFA_GEPT_BUF_LEN,
				0,
				FLG_TYPE_UNLOCK);
}

static void tsk_r_swint(struct hifc_data *hdata, void *data)
{
}

struct gept_mesg {
	uint8_t *payload;
	uint16_t len;
	uint8_t ofst;
	enum FLG_TYPE_TAG flg;
	enum RW_BUF_TAG wt_buf;
};

static struct gept_mesg *create_gept_mesg(struct hifc_data *hdata,
					  uint8_t *mesg,
					  const uint16_t len,
					  const uint8_t ofst,
					  enum FLG_TYPE_TAG flg)
{
	struct gept_mesg *gm;
	struct prot_kcache_obj *kcache_obj = hdata->kcobj;

	gm = kmem_cache_alloc(kcache_obj->gept_mesg_cache,
			      GFP_KERNEL | __GFP_ZERO);

	if (!gm) {
		HIFC_ERR("%s: no memory to allocate gept_mesg\n",
		       __func__);
		goto error;
	}

	gm->payload = kzalloc(len + HEAD_WRAP_LEN,
			      GFP_KERNEL | __GFP_DMA);

	if (!gm->payload) {
		HIFC_ERR("%s: no memory to allocate gept_mesg payload\n",
			__func__);
		goto error;
	}

	memcpy(gm->payload + HEAD_PLACE_HOLDER, mesg + ofst, len);
	gm->len    = len;
	gm->ofst   = ofst;
	gm->flg    = flg;
	gm->wt_buf = RW_BUF_MAX;

	return gm;

error:
	if (gm && !gm->payload) {
		kmem_cache_free(kcache_obj->gept_mesg_cache, gm);
		gm = NULL;
	}

	return NULL;
}

static int32_t destroy_gept_mesg(struct hifc_data *hdata,
				 struct gept_mesg **gm)
{
	struct prot_kcache_obj *kcache_obj = hdata->kcobj;

	kfree((*gm)->payload);
	(*gm)->payload = NULL;

	kmem_cache_free(kcache_obj->gept_mesg_cache, *gm);
	(*gm) = NULL;

	return 0;
}

static void tsk_s_gept_split(struct hifc_data *hdata, void *data)
{
	const struct hifc_bus_ops *bops = NULL;
	struct gept_mesg *gm            = NULL;
	struct hifc_prot_task *task     = (struct hifc_prot_task *)data;
	uint8_t icmd                    = ICMD_FIX_RD_NOP;
	int32_t ret                     = 0;

	WARN_ON(!hdata || !data);
	bops = hdata->bdata->bops;
	gm   = (struct gept_mesg *)task->t_data;

	icmd = (RW_BUF_WT_0 == gm->wt_buf)
	       ? (((!gm->ofst) && (FLG_TYPE_UNLOCK == gm->flg))
		  ? ICMD_VAR_RW_GEPT_2
		  : ICMD_VAR_RW_OFST_GEPT_2)
	       : (((!gm->ofst) && (FLG_TYPE_UNLOCK == gm->flg))
		  ? ICMD_VAR_RW_GEPT_3
		  : ICMD_VAR_RW_OFST_GEPT_3);

	ret = bops->spz_drv_send_icmd(hdata->bdata->client, icmd, gm->payload,
				      gm->len, gm->ofst, gm->flg);
	destroy_gept_mesg(hdata, &gm);

	task->result = ret;
}

static void tsk_s_gept(struct hifc_data *hdata, void *data)
{
	struct gept_mesg *gm        = NULL;
	struct hifc_prot_task *task = (struct hifc_prot_task *)data;

	WARN_ON(!hdata || !data);
	gm         = (struct gept_mesg *)task->t_data;
	gm->wt_buf = get_wt_buf(&hdata->gept_mask);

	tsk_s_gept_split(hdata, data);
}

static void free_wt_req_res(struct hifc_data *hdata,
			    struct gept_mesg **gm,
			    struct hifc_prot_task **task,
			    struct hifc_prot_req **req)
{
	struct hifc_prot_tmgr *tmgr = hdata->tmgr;
	struct prot_kcache_obj *pkcache_obj = hdata->kcobj;
	struct tmgr_kcache_obj *tkcache_obj = tmgr->kcobj;

	if (gm && *gm) {
		kmem_cache_free(pkcache_obj->gept_mesg_cache, *gm);
		*gm = NULL;
	}

	if (task && *task) {
		kmem_cache_free(tkcache_obj->task_cache, *task);
		*task = NULL;
	}

	if (req && *req) {
		kmem_cache_free(tkcache_obj->req_cache, *req);
		*req = NULL;
	}
}

static int32_t send_mesg(void *data, uint8_t *mesg, const size_t len)
{
	struct hifc_data *hdata               = (struct hifc_data *)data;
	struct hifc_prot_tmgr_ops *tmgr_ops   = &hdata->tmgr->mops;
	struct gept_mesg *gm                  = NULL;
	struct hifc_prot_task *task           = NULL;
	struct hifc_prot_task *wt_unmask_task = NULL;
	struct hifc_prot_req *req             = NULL;
	int32_t ret                           = 0;

	gm   = create_gept_mesg(hdata, mesg, len, 0, FLG_TYPE_UNLOCK);
	task = tmgr_ops->spz_tmgr_create_task(&spz_hifc_prot_tmgr,
					      tsk_s_gept, gm,
					      TASK_PRI_MESG);
	req  = tmgr_ops->spz_tmgr_create_request(&spz_hifc_prot_tmgr, task);

	if (gm && task && req) {
		wt_unmask_task =
			tmgr_ops->spz_tmgr_create_task(&spz_hifc_prot_tmgr,
						       tsk_s_unmask_wt_intfa,
						       NULL, TASK_PRI_IRQ);
		ret =
		tmgr_ops->spz_prot_tmgr_submit_wt_req(&spz_hifc_prot_tmgr,
						      hdata,
						      wt_unmask_task,
						      req);

		if (ret < 0)
			goto req_abn;
	} else {
		ret = -ENOMEM;
		goto free_mem;
	}

	return 0;

free_mem:
	free_wt_req_res(hdata, &gm, &task, &req);

req_abn:
	return ret;
}

static int32_t send_mesg_split(void *data, uint8_t *mesg, const size_t len)
{
	struct hifc_data *hdata               = (struct hifc_data *)data;
	struct hifc_prot_tmgr_ops *tmgr_ops   = &hdata->tmgr->mops;
	size_t rem_len                        = len;
	size_t snd_len                        = 0;
	size_t snt_len                        = 0;
	enum FLG_TYPE_TAG flg                          = FLG_TYPE_UNLOCK;
	struct gept_mesg *gm                  = NULL;
	struct hifc_prot_task *task           = NULL;
	struct hifc_prot_task *wt_unmask_task = NULL;
	struct hifc_prot_req *req             = NULL;
	enum RW_BUF_TAG wt_buf                         = RW_BUF_MAX;
	int32_t ret                           = 0;

	wt_buf = get_wt_buf(&hdata->gept_mask);

	while (rem_len) {
		snd_len    =
			(rem_len > PAYLD_LEN_MAX) ? PAYLD_LEN_MAX : rem_len;
		flg        =
			(rem_len > PAYLD_LEN_MAX)
			? FLG_TYPE_LOCK
			: FLG_TYPE_UNLOCK;
		gm         = create_gept_mesg(hdata, mesg,
					      snd_len, snt_len, flg);
		gm->wt_buf = wt_buf;
		task       =
			tmgr_ops->spz_tmgr_create_task(&spz_hifc_prot_tmgr,
						       tsk_s_gept_split,
						       gm,
						       TASK_PRI_MESG);
		req        =
			tmgr_ops->spz_tmgr_create_request(&spz_hifc_prot_tmgr,
							  task);

		if (gm && task && req) {
			wt_unmask_task =
			tmgr_ops->spz_tmgr_create_task(&spz_hifc_prot_tmgr,
						       tsk_s_unmask_wt_intfa,
						       NULL,
						       TASK_PRI_IRQ);
			ret =
			tmgr_ops->spz_prot_tmgr_submit_wt_req(
						&spz_hifc_prot_tmgr,
						hdata,
						wt_unmask_task,
						req);

			if (ret < 0)
				goto req_abn;

			snt_len += snd_len;
			rem_len -= snd_len;
		} else {
			ret = -ENOMEM;
			goto free_mem;
		}
	}

	HIFC_ALERT("%s\n", __func__);
	return 0;

free_mem:
	free_wt_req_res(hdata, &gm, &task, &req);

req_abn:
	return ret;
}

static int32_t prot_send_mesg(void *data, uint8_t *mesg, const size_t len)
{
	return (len > PAYLD_LEN_MAX) ? send_mesg_split(data, mesg, len)
	       : send_mesg(data, mesg, len);
}

static void prot_recv_mesg(struct work_struct *wk)
{
	struct hifc_data *hdata             =
				container_of(wk, struct hifc_data, work);
	struct hifc_prot_tmgr_ops *tmgr_ops = &hdata->tmgr->mops;
	int32_t ret                         = 0;
	/* alloc in tsk_r_intfa, free in tsk_s_unmask_intfa */
	uint64_t *intfa                     = NULL;
	int32_t i                           = 0;
	struct hifc_prot_task *task         = NULL;

	task = tmgr_ops->spz_tmgr_create_task(&spz_hifc_prot_tmgr,
					      tsk_r_intfa, &intfa,
					      TASK_PRI_IRQ);

	if (task) {
		ret =
		tmgr_ops->spz_prot_tmgr_submit_sync_task(&spz_hifc_prot_tmgr,
							 task);

		if ((ret >= 0) &&
		    (likely(COMM_ST_READY == hdata->comm_ready) ||
		    ((*intfa) & ((uint64_t)1 << INT_COMM)))) {
			task =
			tmgr_ops->spz_tmgr_create_task(&spz_hifc_prot_tmgr,
						       tsk_s_mask_intfa,
						       intfa,
						       TASK_PRI_IRQ);
			tmgr_ops->spz_prot_tmgr_submit_sync_task(
					&spz_hifc_prot_tmgr,
					task);

			for (i = 0; i < INT_GEPT_31; ++i) {
				if (!((*intfa) & ((uint64_t)1 << i)))
					continue;

				task =
				((i >= INT_FIFO_0) && (i <= INT_FIFO_21))
				? tmgr_ops->spz_tmgr_create_task(
						&spz_hifc_prot_tmgr,
						tsk_r_fifo,
						(void *)i,
						TASK_PRI_FIFO)
				: ((i >= INT_GEPT_0) && (i <= INT_GEPT_1))
				? tmgr_ops->spz_tmgr_create_task(
						&spz_hifc_prot_tmgr,
						tsk_r_rd_gept,
						(void *)i,
						TASK_PRI_MESG)
				: ((i >= INT_GEPT_2) && (i <= INT_GEPT_3))
				? tmgr_ops->spz_tmgr_create_task(
						&spz_hifc_prot_tmgr,
						tsk_r_wt_gept,
						(void *)i,
						TASK_PRI_IRQ)
				: ((i >= INT_EXT_0) && (i <= INT_EXT_8))
				? tmgr_ops->spz_tmgr_create_task(
						&spz_hifc_prot_tmgr,
						tsk_r_swint,
						(void *)i,
						TASK_PRI_MESG)
				: (INT_COMM == i)
				? tmgr_ops->spz_tmgr_create_task(
						&spz_hifc_prot_tmgr,
						tsk_r_comm,
						(void *)i,
						TASK_PRI_IRQ)
				: NULL;

				task ? tmgr_ops->spz_prot_tmgr_submit_task(
							&spz_hifc_prot_tmgr,
							task)
				     : HIFC_ERR(
					"%s: unsupported int num\n", __func__);
			}

			task =
			tmgr_ops->spz_tmgr_create_task(&spz_hifc_prot_tmgr,
						       tsk_s_unmask_intfa,
						       intfa,
						       TASK_PRI_MESG);
			tmgr_ops->spz_prot_tmgr_submit_task(
						&spz_hifc_prot_tmgr,
						task);
		}
	}

	hdata->bdata->bops->spz_drv_enable_irq(hdata->bdata->client);
}

static struct prot_kcache_obj pkcache_obj;

static const struct hifc_ops spz_prot_ops = {
	.spz_prot_send_mesg = prot_send_mesg,
	.spz_prot_recv_mesg = prot_recv_mesg
};

static struct hifc_data spz_hifc_data = {
	.pops       = &spz_prot_ops,
	.tmgr       = &spz_hifc_prot_tmgr,
	.kcobj      = &pkcache_obj,
	.gept_mask  = 0,
	.comm_ready = COMM_ST_NONE
};

int32_t spz_prot_probe(struct device *dev, struct hifc_bus_data *bdata)
{
	int32_t ret                         = 0;
	struct hifc_prot_tmgr_ops *tmgr_ops = &spz_hifc_prot_tmgr.mops;
	struct prot_kcache_obj *kcache_obj  = spz_hifc_data.kcobj;

	dev_set_drvdata(dev, &spz_hifc_data);

	spz_hifc_data.bdata      = bdata;
	spz_hifc_data.gept_mask |= (1 << RW_BUF_RD_0);
	spz_hifc_data.gept_mask |= (1 << RW_BUF_WT_0);

	INIT_WORK(&spz_hifc_data.work,
		spz_hifc_data.pops->spz_prot_recv_mesg);

	kcache_obj->gept_mesg_cache = kmem_cache_create("prot_gept_mesg",
				      sizeof(struct gept_mesg), 0,
				      SLAB_HWCACHE_ALIGN, NULL);
	kcache_obj->intfa_cache = kmem_cache_create("prot_intfa",
						    sizeof(uint64_t),
						    0,
						    SLAB_HWCACHE_ALIGN,
						    NULL);

	ret = tmgr_ops->spz_prot_tmgr_init(&spz_hifc_prot_tmgr,
					   dev,
					   &spz_hifc_data);

	return ret ? ret : spz_disp_probe(dev);
}
EXPORT_SYMBOL(spz_prot_probe);

int32_t spz_prot_remove(struct device *dev)
{
	int32_t ret                         = 0;
	struct hifc_prot_tmgr_ops *tmgr_ops = &spz_hifc_prot_tmgr.mops;
	struct prot_kcache_obj *kcache_obj  = spz_hifc_data.kcobj;

	kmem_cache_destroy(kcache_obj->gept_mesg_cache);
	kmem_cache_destroy(kcache_obj->intfa_cache);

	kcache_obj->gept_mesg_cache = NULL;
	kcache_obj->intfa_cache = NULL;

	spz_hifc_data.kcobj = NULL;

	ret  = spz_disp_remove(dev);
	ret &= tmgr_ops->spz_prot_tmgr_deinit(&spz_hifc_prot_tmgr);

	return ret;
}
EXPORT_SYMBOL(spz_prot_remove);

MODULE_LICENSE("GPL");
