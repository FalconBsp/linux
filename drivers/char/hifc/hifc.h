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

#ifndef SPZ_HIFC_H_
#define SPZ_HIFC_H_

#include <linux/types.h>

#define ICMD_FIX_RD_NOP           0x00 /* fix read start */
#define ICMD_FIX_RD_INTFA_8       0x09
#define ICMD_FIX_RD_LEN_GEPT_0    0x10
#define ICMD_FIX_RD_LEN_GEPT_1    0x11
#define ICMD_FIX_RD_FIFO_0        0x30
#define ICMD_FIX_RD_FIFO_21       0x45 /* fix read end */
#define ICMD_FIX_WT_SLP           0x50 /* fix write start */
#define ICMD_FIX_WT_INT_MASK_4    0x53
#define ICMD_FIX_WT_INT_MASK_8    0x55
#define ICMD_FIX_WT_INT_UNMASK_4  0x57
#define ICMD_FIX_WT_INT_UNMASK_8  0x59
#define ICMD_FIX_WT_INT_CLR_2     0x5A
#define ICMD_FIX_WT_FIFO_21       0x75 /* fix write end */
#define ICMD_FIX_RW_GEPT_0        0x80 /* fix r/w start */
#define ICMD_FIX_RW_GEPT_31       0x9F /* fix r/w end */
#define ICMD_VAR_RW_GEPT_0        0xA0 /* variable r/w start */
#define ICMD_VAR_RW_GEPT_1        0xA1
#define ICMD_VAR_RW_GEPT_2        0xA2
#define ICMD_VAR_RW_GEPT_3        0xA3
#define ICMD_VAR_RW_GEPT_31       0xBF /* variable r/w end */
#define ICMD_VAR_RW_OFST_GEPT_0   0xC0 /* variable ofst r/w port start */
#define ICMD_VAR_RW_OFST_GEPT_1   0xC1
#define ICMD_VAR_RW_OFST_GEPT_2   0xC2
#define ICMD_VAR_RW_OFST_GEPT_3   0xC3
#define ICMD_VAR_RW_OFST_GEPT_31  0xDF /* variable ofst r/w port end */
#define ICMD_VAR_RD_FIFO_0        0xE0 /* variable read fifo start */
#define ICMD_VAR_RD_FIFO_21       0xF5 /* variable read fifo end */

#define MIN(a, b)                 ((a) > (b) ? (b) : (a))
#define MAX(a, b)                 ((a) > (b) ? (a) : (b))

#define HEAD_DUM_NUM_RD           0
#define HEAD_DUM_NUM_WT           0
#define HEAD_DUM_NUM_MAX          MAX((HEAD_DUM_NUM_RD), (HEAD_DUM_NUM_WT))

#define MSG_HEAD_LEN              4
#define HEAD_PLACE_HOLDER         (MSG_HEAD_LEN + HEAD_DUM_NUM_MAX)
/* max of read and write icmd tail dummy num */
#define TAIL_DUM_NUM              2
#define HEAD_WRAP_LEN             (HEAD_PLACE_HOLDER + TAIL_DUM_NUM)

#define HIFC_PR_FMT(fmt)          "HIFC: " fmt
#define HIFC_ERR(fmt, arg...)     pr_err(HIFC_PR_FMT(fmt), ##arg)
#define HIFC_WARN(fmt, arg...)    pr_warn(HIFC_PR_FMT(fmt), ##arg)
#define HIFC_INFO(fmt, arg...)    pr_info(HIFC_PR_FMT(fmt), ##arg)
#define HIFC_ALERT(fmt, arg...)   pr_alert(HIFC_PR_FMT(fmt), ##arg)

struct hifc_bus_ops {
	int32_t (*spz_drv_send_icmd)(void *client, const uint8_t icmd,
				     uint8_t *buf, const size_t len,
				     const uint8_t ofst, const uint8_t flg);
	int32_t (*spz_drv_recv_icmd)(void *client, const uint8_t icmd,
				     uint8_t *buf , const size_t len,
				     const uint8_t ofst, const uint8_t flg,
				     uint8_t *buf_ofst);
	void (*spz_drv_enable_irq)(const void *client);
};

struct hifc_bus_data {
	void *client;
	const struct hifc_bus_ops *bops;
};

struct disp_hdlr_data {
	int32_t (*spz_disp_recv_mesg)(uint8_t *mesg, const size_t len);
	void *data;
};

struct hifc_ops {
	int32_t (*spz_prot_send_mesg)(void *data, uint8_t *mesg,
				      const size_t len);
	void    (*spz_prot_recv_mesg)(struct work_struct *work);
};

struct prot_kcache_obj {
	struct kmem_cache *gept_mesg_cache;
	struct kmem_cache *intfa_cache;
};

struct hifc_data {
	const struct hifc_ops *pops;
	struct hifc_bus_data *bdata;
	struct disp_hdlr_data *dhdlr;
	struct hifc_prot_tmgr *tmgr;
	struct prot_kcache_obj *kcobj;
	struct work_struct work;
	uint32_t gept_mask;
	uint32_t comm_ready;
};

extern int32_t spz_disp_probe(struct device *dev);
extern int32_t spz_disp_remove(struct device *dev);
extern int32_t spz_prot_probe(struct device *dev,
			      struct hifc_bus_data *bdata);
extern int32_t spz_prot_remove(struct device *dev);

#endif /* SPZ_HIFC_H_ */
