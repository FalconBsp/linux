/*
 * Copyright (c) 2013-2014 TRUSTONIC LIMITED
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

#include "psp_comm_trace.h"

static struct device *dev;
static struct dentry *psp_comm_dentry;

int psp_comm_enable_trace(struct trace_info *trace_info)
{
	int ret = -EIO;
	u64 val = (u64) trace_info->trace_address;
	struct psp_comm_cmd_set_trace_buf set_trace_buf_cmd;

	if (trace_info->trace_flag == TRUE) {
		dev_info(dev, " %s : PSP Traces already enabled\n", __func__);
		return 0;
	}

	if (NULL == trace_info->trace_address) {
		dev_err(dev, " %s : Trace buffer not allocated\n", __func__);
		return -EFAULT;
	}

	set_trace_buf_cmd.tracebuf_hiphy_addr = upper_32_bits(val);
	set_trace_buf_cmd.tracebuf_lophy_addr = lower_32_bits(val);
	set_trace_buf_cmd.tracebuf_len = trace_info->trace_buf_size;

	dev_dbg(dev, " %s : tracebuf_hiphy_addr 0x%x\n", __func__,
		 set_trace_buf_cmd.tracebuf_hiphy_addr);
	dev_dbg(dev, " %s : tracebuf_lophy_addr 0x%x\n", __func__,
		 set_trace_buf_cmd.tracebuf_lophy_addr);
	dev_dbg(dev, " %s : tracebuf_len %u\n", __func__,
		 set_trace_buf_cmd.tracebuf_len);

	ret = psp_comm_send_buffer(0, &set_trace_buf_cmd,
				   sizeof(set_trace_buf_cmd),
				   PSP_COMM_CMD_ID_SET_TRACE_BUF);
	if (ret != 0) {
		dev_err(dev, " %s : set trace buffer failed\n", __func__);
		return ret;
	}

	trace_info->trace_flag = TRUE;

	dev_info(dev, " %s : PSP Trace messages enabled\n", __func__);
	return ret;
}

int psp_comm_disable_trace(struct trace_info *trace_info)
{
	int ret = -EIO;
	struct psp_comm_cmd_set_trace_buf set_trace_buf_cmd;

	if (trace_info->trace_flag == FALSE) {
		dev_info(dev, " %s : PSP Traces already disabled\n", __func__);
		return 0;
	}

	set_trace_buf_cmd.tracebuf_len = 0;

	ret = psp_comm_send_buffer(0, &set_trace_buf_cmd,
				   sizeof(set_trace_buf_cmd),
				   PSP_COMM_CMD_ID_SET_TRACE_BUF);
	if (ret != 0) {
		dev_err(dev, " %s : failed to disable PSP trace message\n",
			__func__);
		return ret;
	}

	trace_info->trace_flag = FALSE;

	dev_info(dev, " %s : PSP Trace messages disabled\n", __func__);
	return ret;
}


static void log_EOL(struct trace_info *trace_info, u16 source)
{
	if (!strnlen(trace_info->log_line, LOG_LINE_SIZE)) {
		/* In case a TA tries to print a 0x0 */
		trace_info->log_line_len = 0;
		return;
	}

	if (trace_info->prev_source) {
		/* MobiCore Userspace */
		printk("%03x|%s\n", trace_info->prev_source,
		       trace_info->log_line);
	} else {
		/* MobiCore kernel */
		printk("%s\n", trace_info->log_line);
	}

	trace_info->log_line[0] = 0;
	trace_info->log_line_len = 0;
}

static void log_char(struct trace_info *trace_info, char ch, u16 source)
{
	if ('\n' == ch || '\r' == ch) {
		log_EOL(trace_info, source);
		return;
	}

	if (trace_info->log_line_len >= LOG_LINE_SIZE - 1 || source !=
	    trace_info->prev_source)
		log_EOL(trace_info, source);

	trace_info->log_line[trace_info->log_line_len] = ch;
	trace_info->log_line[trace_info->log_line_len + 1] = 0;
	trace_info->log_line_len++;
	trace_info->prev_source = source;
}

static void log_integer(struct trace_info *trace_info, u32 format, u32 value,
			u16 source)
{
	int digits = 1;
	int negative = 0;
	u32 digit_base = 1;
	u32 base = (format & LOG_INTEGER_DECIMAL) ? 10 : 16;
	int width = (format & LOG_LENGTH_MASK) >> LOG_LENGTH_SHIFT;
	const char hex2ascii[16] = {
		'0', '1', '2', '3', '4', '5', '6', '7',
		'8', '9', 'a', 'b', 'c', 'd', 'e', 'f' };

	if ((format & LOG_INTEGER_SIGNED) != 0 && ((signed int)value) < 0) {
		negative = 1;
		value = (u32)(-(signed int)value);
		width--;
	}

	/* Find length and divider to get largest digit */
	while (value / digit_base >= base) {
		digit_base *= base;
		++digits;
	}

	if (width > digits) {
		char ch = (base == 10) ? ' ' : '0';

		while (width > digits) {
			log_char(trace_info, ch, source);
			--width;
		}
	}

	if (negative)
		log_char(trace_info, '-', source);

	while (digits-- > 0) {
		u32 d = value / digit_base;

		log_char(trace_info, hex2ascii[d], source);
		value = value - d * digit_base;
		digit_base /= base;
	}
}

static void parse_log_msg(struct trace_info *trace_info, struct log_msg *msg)
{
	u32 ch = 0;

	switch (msg->ctrl & LOG_TYPE_MASK) {
	case LOG_TYPE_CHAR:
		ch = msg->log_data;
		while (ch != 0) {
			log_char(trace_info, ch & 0xFF, msg->source);
			ch >>= 8;
		}
		break;
	case LOG_TYPE_INTEGER:
		log_integer(trace_info, msg->ctrl, msg->log_data, msg->source);
		break;
	default:
		break;
	}

	if (msg->ctrl & LOG_EOL)
		log_EOL(trace_info, msg->source);
}

static u32 process_log(struct trace_info *trace_info, struct trace_buf
		       *trace_buffer)
{
	char *last_msg = trace_buffer->buf + trace_buffer->write_pos;
	char *buff = trace_buffer->buf + trace_info->log_pos;

	while (buff != last_msg) {
		parse_log_msg(trace_info, (struct log_msg *)buff);
		buff += sizeof(struct log_msg);

		/* wrap around */
		if ((buff + sizeof(struct log_msg)) >
		    ((char *)trace_buffer + trace_info->trace_buf_size)) {
			buff = trace_buffer->buf;
		}
	}
	return (u32) (buff - trace_buffer->buf);
}

static int allocate_trace_buffer(struct trace_info *trace_info)
{
	u64 order = get_order(TRACE_BUFFER_LEN);

	if (trace_info->trace != NULL)
		return 0;

	trace_info->trace = (void *)__get_free_pages(GFP_KERNEL, order);
	if (NULL == trace_info->trace) {
		dev_err(dev, " %s : trace buffer allocation failed\n",
			__func__);
		return -ENOMEM;
	}
	trace_info->trace_address = (void *) virt_to_phys(trace_info->trace);
	trace_info->trace_buf_size = TRACE_BUFFER_LEN;
	return 0;
}

static void deallocate_trace_buffer(struct trace_info *trace_info)
{
	if (NULL == trace_info->trace)
		return;

	free_pages((u64)trace_info->trace,
		   get_order(trace_info->trace_buf_size));

	trace_info->trace = NULL;
	trace_info->trace_address = NULL;
	trace_info->trace_buf_size = 0;
}


static int psp_log_thread(void *data)
{
	struct trace_info *trace_info = (struct trace_info *) data;
	struct trace_buf *trace_buffer;

	dev_info(dev, " %s : entry\n", __func__);

	while (!kthread_should_stop()) {
		msleep_interruptible(1000);

		if (trace_info->trace_flag == FALSE)
			continue;

		if (NULL == trace_info->trace)
			continue;

		trace_buffer = (struct trace_buf *) trace_info->trace;

		if (trace_buffer->version != SUPPORTED_TRACE_BUF_VERSION ||
		    trace_buffer->write_pos == trace_info->log_pos)
			continue;

		trace_info->log_pos = process_log(trace_info, trace_buffer);
	}

	deallocate_trace_buffer(trace_info);
	dev_info(dev, " %s : closing PSP Trace Polling thread\n", __func__);
	return 0;
}

static int start_psp_log_thread(struct trace_info *trace_info)
{
	struct task_struct *thread;

	if (trace_info->log_thread != NULL) {
		dev_dbg(dev, " %s : PSP Log thread already exists\n",
			__func__);
		return 0;
	}

	thread = kthread_run(psp_log_thread, trace_info, "PSP_Log_kthread");
	if (unlikely(IS_ERR(thread))) {
		dev_err(dev, " %s : error in starting PSP Log Polling thread 0x%lx\n",
			__func__, PTR_ERR(thread));
		return -EFAULT;
	}

	trace_info->log_thread = thread;
	return 0;
}

void stop_psp_log_thread(struct trace_info *trace_info)
{
	if (trace_info->log_thread != NULL) {
		dev_info(dev, " %s : stopping PSP Log thread\n", __func__);
		kthread_stop(trace_info->log_thread);
		trace_info->log_thread = NULL;
	}
}

void free_psp_trace_resources(struct trace_info *trace_info)
{
	stop_psp_log_thread(trace_info);
	deallocate_trace_buffer(trace_info);
	debugfs_remove_recursive(psp_comm_dentry);
	psp_comm_dentry = NULL;
}

static int psp_trace_open(struct inode *inode, struct file *filp)
{
	filp->private_data = inode->i_private;
	return 0;
}

static ssize_t psp_trace_flag_read(struct file *filp, char __user *user_buf,
				   size_t count, loff_t *ppos)
{
	struct trace_info *trace_info = filp->private_data;
	char buff[10];
	size_t buf_size;
	int trace_flag = trace_info->trace_flag;

	buf_size = snprintf(buff, sizeof(buff), "%d\n", trace_flag);
	buf_size += 1;

	if (count < buf_size) {
		dev_err(dev, " %s : short user(dst) buffer\n", __func__);
		return -EINVAL;
	}

	if (*ppos >= buf_size) {
		/* wrap around and indicate end-of-read */
		*ppos = 0;
		return 0;
	}

	if (copy_to_user(user_buf, buff, buf_size)) {
		dev_err(dev, " %s : copy_to_user failed\n", __func__);
		return -EFAULT;
	}

	*ppos += buf_size;
	return buf_size;
}

static ssize_t psp_trace_flag_write(struct file *filp, const char __user
				    *user_buf, size_t count, loff_t *ppos)
{
	struct trace_info *trace_info = filp->private_data;
	char buff[10];
	const unsigned int base = 10;
	size_t buf_size = min(sizeof(buff), count);
	int trace_flag = 0;
	int ret = -EFAULT;

	if (0 == count) {
		dev_err(dev, " %s : invalid byte count\n", __func__);
		return -EINVAL;
	}

	if (strncpy_from_user(buff, user_buf, buf_size) < 0) {
		dev_err(dev, " %s : strncpy_from_user failed\n", __func__);
		return -EFAULT;
	}

	if (kstrtoint(buff, base, &trace_flag)) {
		dev_err(dev, " %s : invalid input\n", __func__);
		return -EINVAL;
	}

	dev_dbg(dev, " %s : trace_flag = %d\n", __func__, trace_flag);

	if (0 == trace_flag)
		ret = psp_comm_disable_trace(trace_info);
	else
		ret = psp_comm_enable_trace(trace_info);

	if (ret != 0) {
		dev_err(dev, " %s : failed to set trace flag\n", __func__);
		return ret;
	}

	return buf_size;
}

static const struct file_operations psp_trace_ops = {
	.owner = THIS_MODULE,
	.open  = psp_trace_open,
	.read  = psp_trace_flag_read,
	.write = psp_trace_flag_write,
};

int create_debugfs_entry(struct trace_info *trace_buf_info)
{
	struct dentry *file;

	psp_comm_dentry = debugfs_create_dir(PSP_COMM_DIR, NULL);
	if (NULL == psp_comm_dentry) {
		dev_err(dev, " %s : failed to create entry %s\n", __func__,
			PSP_COMM_DIR);
		return -ENOMEM;
	}

	file = debugfs_create_file(PSP_TRACE_FILE, S_IRUGO | S_IWUSR,
				   psp_comm_dentry, trace_buf_info,
				   &psp_trace_ops);
	if (NULL == file) {
		dev_err(dev, " %s : failed to create entry %s\n", __func__,
			PSP_TRACE_FILE);
		debugfs_remove(psp_comm_dentry);
		psp_comm_dentry = NULL;
		return -ENOMEM;
	}
	return 0;
}

int psp_comm_trace_init(struct trace_info *trace_buf_info)
{
	int ret = -EIO;

	dev = psp_comm_get_device();

	trace_buf_info->trace_flag = FALSE;
	trace_buf_info->log_pos = 0;
	trace_buf_info->log_thread = NULL;
	trace_buf_info->trace = NULL;
	trace_buf_info->trace_address = NULL;
	trace_buf_info->trace_buf_size = 0;

	ret = allocate_trace_buffer(trace_buf_info);
	if (ret != 0)
		return ret;

	ret = start_psp_log_thread(trace_buf_info);
	if (ret != 0) {
		deallocate_trace_buffer(trace_buf_info);
		return ret;
	}

	ret = create_debugfs_entry(trace_buf_info);
	if (ret != 0) {
		dev_err(dev, " %s : error in creating debugfs entry\n",
			__func__);
		free_psp_trace_resources(trace_buf_info);
		return ret;
	}

	if (psp_comm_enable_trace(trace_buf_info) != 0)
		dev_info(dev, " %s : error in enabling trace during init\n",
			 __func__);

	dev_info(dev, " %s : PSP trace init succeeded\n", __func__);
	return ret;
}
