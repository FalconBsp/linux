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

#ifndef SPZ_HIFC_DRVIF_H_
#define SPZ_HIFC_DRVIF_H_

#define BYTES_PER_LINE    8

enum CMD_FMT_TAG {
	CMD_FMT_WT_1 = 0,
	CMD_FMT_WT_2,
	CMD_FMT_WT_3,
	CMD_FMT_RD_0,
	CMD_FMT_RD_1,
	CMD_FMT_RD_2,
	CMD_FMT_RD_3,
	CMD_FMT_RW_1,
	CMD_FMT_RW_2,
	CMD_FMT_RW_3,
	CMD_FMT_MAX
};

static inline enum CMD_FMT_TAG get_cmd_fmt(const uint8_t icmd)
{
	return ((icmd  == ICMD_FIX_RD_NOP)
		? CMD_FMT_RD_0 :
		((icmd >  ICMD_FIX_RD_NOP)
		 && (icmd <= ICMD_FIX_RD_FIFO_21))      ? CMD_FMT_RD_1 :
		((icmd >= ICMD_FIX_WT_SLP)
		 && (icmd <= ICMD_FIX_WT_FIFO_21))      ? CMD_FMT_WT_1 :
		((icmd >= ICMD_FIX_RW_GEPT_0)
		 && (icmd <= ICMD_FIX_RW_GEPT_31))      ? CMD_FMT_RW_1 :
		((icmd >= ICMD_VAR_RW_GEPT_0)
		 && (icmd <= ICMD_VAR_RW_GEPT_31))      ? CMD_FMT_RW_2 :
		((icmd >= ICMD_VAR_RW_OFST_GEPT_0)
		 && (icmd <= ICMD_VAR_RW_OFST_GEPT_31)) ? CMD_FMT_RW_3 :
		((icmd >= ICMD_VAR_RD_FIFO_0)
		 && (icmd <= ICMD_VAR_RD_FIFO_21))      ? CMD_FMT_RD_2 :
		CMD_FMT_MAX);
}

static inline uint8_t get_len_low_byte(const uint16_t len)
{
	return len & 0x00FF;
}

static inline uint8_t get_flg_len_high_byte(const uint16_t len,
		const uint8_t flg)
{
	return ((flg & 0b00000011) << 6) | ((len >> 8) & 0b00111111);
}

static void dump_buffer(const uint8_t *buf, const uint16_t len)
{
#if 0
	int32_t i = 0;

	for (i = 0; i < len; ++i) {
		if (!(i % BYTES_PER_LINE))
			HIFC_ALERT("\n");

		HIFC_ALERT(" %02x", *buf++);
	}

	HIFC_DBG_PRINT("\n");
#endif
}

#endif /* SPZ_HIFC_DRVIF_H_ */
