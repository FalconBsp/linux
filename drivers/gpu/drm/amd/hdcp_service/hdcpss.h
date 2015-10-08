/*
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

#ifndef __HDCPSS_H__
#define __HDCPSS_H__

#include <linux/types.h>
#include "tl_hdcp_public.h"

#define HDCPSS_USE_TEST_TA	1

#define PSP_GFX_CMD_BUF_VERSION	0x00000001
#define RET_OK			0
#define CMD_ID_TEE_TEST		13

#define BIT_REPEATER			6
#define BIT_FIFO_READY			5

#define DEVICE_COUNT_MASK		0x7F
#define BIT_MAX_DEVS_EXCEDDED		7
#define BIT_MAX_CASCADE_EXCEDDED	3

/* TEE Gfx Command IDs for the ring buffer interface. */
enum gfx_cmd_id {
	GFX_CMD_ID_LOAD_TA	= 0x00000001,	/* load TA		*/
	GFX_CMD_ID_UNLOAD_TA	= 0x00000002,	/* unload TA		*/
	GFX_CMD_ID_NOTIFY_TA	= 0x00000003,	/* notify TA		*/
	GFX_CMD_ID_LOAD_ASD	= 0x00000004,	/* load ASD Driver	*/
};

/* Command to load Trusted Application binary into PSP OS. */
struct gfx_cmd_load_ta {
	u32	app_phy_addr_hi;	/* bits[63:32] of TA physical addr   */
	u32	app_phy_addr_lo;	/* bits[31:0] of TA physical addr    */
	u32	app_len;		/* length of the TA binary	     */
	u32	tci_buf_phy_addr_hi;	/* bits[63:32] of TCI physical addr  */
	u32	tci_buf_phy_addr_lo;	/* bits[31:0] of TCI physical addr   */
	u32	tci_buf_len;		/* length of the TCI buffer in bytes */
};

/* Command to Unload Trusted Application binary from PSP OS */
struct gfx_cmd_unload_ta {
	u32	session_id;	/* Session ID of the loaded TA */
};

/* Command to notify TA that it has to execute command in TCI buffer */
struct gfx_cmd_notify_ta {
	u32	session_id;	/* Session ID of the TA */
};

/*
 * Structure of GFX Response buffer.
 * For GPCOM I/F it is part of GFX_CMD_RESP buffer, for RBI
 * it is separate buffer.
 * Total size of GFX Response buffer = 32 bytes
 */
struct gfx_resp {
	u32	status;		/* status of command execution */
	u32	session_id;	/* session ID in response to LoadTa command */
	u32	resv[6];
};

/*
 * Structure for Command buffer pointed by GFX_RB_FRAME.CmdBufAddrHi
 * and GFX_RB_FRAME.CmdBufAddrLo.
 * Total Size of gfx_cmd_resp = 128 bytes
 * union occupies 24 bytes.
 * resp buffer is of 32 bytes
 */
struct gfx_cmd_resp {
	u32	buf_size;		/* total size of the buffer in bytes */
	u32	buf_version;		/* version of the buffer structure */
	u32	cmd_id;			/* command ID */
	u32	resp_buf_addr_hi;	/* Used for RBI ring only */
	u32	resp_buf_addr_lo;	/* Used for RBI ring only */
	u32	resp_offset;		/* Used for RBI ring only */
	u32	resp_buf_size;		/* Used for RBI ring only */

	union {
		struct gfx_cmd_load_ta	 load_ta;
		struct gfx_cmd_unload_ta unload_ta;
		struct gfx_cmd_notify_ta notify_ta;
	} u;

	unsigned char   resv1[28];
	struct gfx_resp resp;		/* Response buffer for GPCOM ring */
	unsigned char   resv2[16];
};

#ifdef HDCPSS_USE_TEST_TA

/**
 *  Key info data structure
 */
typedef struct {
	uint32_t	key_blob;           /**< Key blob buffer */
	uint32_t	key_blob_len;       /**< Length of key blob buffer */
	uint32_t	key_metadata;       /**< Key metadata */
} get_key_info_t;


/**
 *  Test data structure
 */
typedef struct {
	uint32_t	a;	/**< Key blob buffer */
	uint32_t	b;      /**< Length of key blob buffer */
	uint32_t	c;      /**< Key metadata */
} test_t;

/**
 * TCI message data.
 */
typedef struct {
	union {
		command_t     command;
		response_t    response;
	};

	union {
		get_key_info_t   get_key_info;
		test_t           test;
	};

} tciMessage_t, *tciMessage_ptr;

/**
 * Overall TCI structure.
 */
typedef struct {
	tciMessage_t message;   /**< TCI message */
} tci_t;
#endif

struct hdcpss_data {
	u32			session_id;
	u32			cmd_buf_size;
	u32			ta_size;
	u32			tci_size;
	struct gfx_cmd_resp	*cmd_buf_addr;
#ifdef HDCPSS_USE_TEST_TA
	tci_t			*tci_buf_addr;
#else
	HDCP_TCI		*tci_buf_addr;
#endif
	void			*ta_buf_addr;
	uint8_t			Bksv[5];
	uint8_t			Bcaps;
	uint8_t			R_Prime[2];
	uint8_t			*ksv_fifo_buf;
	u32			ksv_list_size;
	uint8_t			V_Prime[20];
	uint8_t			bstatus[2];
	struct amdgpu_device	*adev;
	uint8_t			is_repeater;
};

#endif