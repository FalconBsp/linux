/*
 * Copyright 2012-15 Advanced Micro Devices, Inc.
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

#ifndef __DAL_HDCP_TYPES_H__
#define __DAL_HDCP_TYPES_H__

enum hdcp_message_id {
	HDCP_MESSAGE_ID_READ_BKSV = 0,
	/* HDMI is called Ri', DP is called R0' */
	HDCP_MESSAGE_ID_READ_RI_R0,
	HDCP_MESSAGE_ID_READ_PJ,
	HDCP_MESSAGE_ID_WRITE_AKSV,
	HDCP_MESSAGE_ID_WRITE_AINFO,
	HDCP_MESSAGE_ID_WRITE_AN,
	HDCP_MESSAGE_ID_READ_VH_X,
	HDCP_MESSAGE_ID_READ_BCAPS,
	HDCP_MESSAGE_ID_READ_BSTATUS,
	HDCP_MESSAGE_ID_READ_KSV_FIFO,
	HDCP_MESSAGE_ID_READ_BINFO,
};

enum hdcp_version {
	HDCP_VERSION_14,
	HDCP_VERSION_22
};

enum hdcp_link {
	HDCP_LINK_PRIMARY,
	HDCP_LINK_SECONDARY
};

struct hdcp_protection_message {
	enum hdcp_version version;
	/* relevant only for DVI */
	enum hdcp_link link;
	enum hdcp_message_id msg_id;
	uint32_t length;
	uint8_t *data;
};

#endif