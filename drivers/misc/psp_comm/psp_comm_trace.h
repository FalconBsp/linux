/*
 * Copyright (c) 2013 TRUSTONIC LIMITED
 * All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */

#ifndef _PSP_COMM_TRACE_H_
#define _PSP_COMM_TRACE_H_
#include <linux/kernel.h>
#include <linux/kthread.h>
#include <linux/device.h>
#include <linux/gfp.h>
#include <linux/delay.h>
#include <linux/debugfs.h>
#include <linux/uaccess.h>

/* define types */
#if !defined(TRUE)
#define TRUE (1 == 1)
#endif

#if !defined(FALSE)
#define FALSE (1 != 1)
#endif

#define PSP_COMM_DIR			"psp_comm"
#define PSP_TRACE_FILE			"enable_trace"

#define PSP_COMM_CMD_ID_SET_TRACE_BUF	0x00060000

/* Definitions for log version 2 */
#define LOG_TYPE_MASK			(0x0007)
#define LOG_TYPE_CHAR			0
#define LOG_TYPE_INTEGER		1
/* Field length */
#define LOG_LENGTH_MASK			(0x00F8)
#define LOG_LENGTH_SHIFT		3
/* Extra attributes */
#define LOG_EOL				(0x0100)
#define LOG_INTEGER_DECIMAL		(0x0200)
#define LOG_INTEGER_SIGNED		(0x0400)
#define LOG_LINE_SIZE			256

#define SUPPORTED_TRACE_BUF_VERSION	0x2

#define TRACE_BUFFER_LEN		102400

struct psp_comm_cmd_set_trace_buf {
	u32	tracebuf_hiphy_addr;
	u32	tracebuf_lophy_addr;
	u32	tracebuf_len;
};

/* Trace buffer structure */
struct trace_buf {
	u32	version;    /* Version of trace buffer */
	u32	length;     /* Length of allocated
			       buffer(includes header) */
	u32	write_pos;  /* Last write position */
	char	buf[1];     /* Start of the log buffer */
};

/* Trace Message structure */
struct log_msg {
	u16	ctrl;       /* Type and format of data */
	u16	source;     /* Unique value for each event source */
	u32	log_data;   /* Value, if any */
};

/* Trace Message Information */
struct trace_info {
	u16		prev_source;		 /* Previous log source */
	u32		log_pos;		 /* log previous position */
	u32		log_line_len;		 /* Log Line buffer current
						    length */
	char		log_line[LOG_LINE_SIZE]; /* Buffer for each line */
	void		*trace_address;		 /* Physical Address	*/
	void		*trace;			 /* Virtual Address	*/
	u32		trace_buf_size;		 /* Trace Buffer Size	*/
	u8		trace_flag;		 /* Trace Enabled flag	*/

	struct task_struct *log_thread;		 /* Thread for reading PSP
						    traces */
};

int psp_comm_trace_init(struct trace_info *trace_buf_info);
void free_psp_trace_resources(struct trace_info *trace_info);

extern int psp_comm_send_buffer(u32 client_type, void *buf, u32 buf_size, u32
				cmd_id);
extern struct device *psp_comm_get_device(void);
#endif
