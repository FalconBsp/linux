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

#include <linux/firmware.h>
#include <linux/module.h>
#include <linux/semaphore.h>
#include <linux/slab.h>
#include <asm/pgtable.h>
#include <asm/cacheflush.h>
#include "hdcpss.h"
#include "amdgpu_psp_if.h"
#include "amdgpu.h"
#include "../dal/include/hdcp_types.h"
#include "../dal/include/dal_interface.h"

#define FIRMWARE_CARRIZO	"amdgpu/carrizo_psp.bin"
MODULE_FIRMWARE(FIRMWARE_CARRIZO);

int hdcpss_notify_ta(struct hdcpss_data *);

static struct hdcpss_data hdcp_data;

#ifndef HDCPSS_USE_TEST_TA
int hdcpss_read_An_Aksv(struct hdcpss_data *hdcp, u32 display_index)
{
	int ret = 0;
	int i = 0;

	hdcp->tci_buf_addr->HDCP_14_Message.CommandHeader.
		commandId = HDCP_CMD_HOST_CMDS;
	hdcp->tci_buf_addr->HDCP_14_Message.
		CmdHDCPCmdInput.ulDisplayIndex = display_index;
	hdcp->tci_buf_addr->HDCP_14_Message.
		CmdHDCPCmdInput.bCommandCode = HDCP_14_COMMAND_CREATE_SESSION;

	ret = hdcpss_notify_ta(hdcp);

	if (!ret) {
		if (hdcp->tci_buf_addr->HDCP_14_Message.ResponseHeader.
			responseId == IS_RSP(HDCP_14_COMMAND_CREATE_SESSION)) {
			dev_dbg(hdcp->adev->dev, "An received :\n");
			for (i = 0; i < 8; i++) {
				dev_info(hdcp->adev->dev, "%x\t",
					hdcp->tci_buf_addr->
					HDCP_14_Message.RspHDCPCmdOutput.
					GetAnAksv.An[i]);
			}
			dev_dbg(hdcp->adev->dev, "Aksv received :\n");
			for (i = 0; i < 5; i++) {
				dev_info(hdcp->adev->dev, "%x\t",
					hdcp->tci_buf_addr->
					HDCP_14_Message.RspHDCPCmdOutput.
					GetAnAksv.Aksv[i]);
			}
		}
	} else {
		dev_dbg(hdcp->adev->dev, "Invalid Response from TL !\n");
	}
	return ret;
}

bool hdcpss_write_An(struct hdcpss_data *hdcp, u32 display_index)
{
	struct hdcp_protection_message message;
	bool ret = 0;
	uint8_t An[8];

	memcpy(An, hdcp->tci_buf_addr->HDCP_14_Message.
			RspHDCPCmdOutput.GetAnAksv.An,
			sizeof(An));

	message.version = HDCP_VERSION_14;
	message.link = HDCP_LINK_PRIMARY;
	message.msg_id = HDCP_MESSAGE_ID_WRITE_AN;
	message.length = sizeof(An);
	message.data = An;

	ret = dal_process_hdcp_msg(hdcp->adev->dm.dal, display_index, &message);

	return ret;
}

bool hdcpss_write_Aksv(struct hdcpss_data *hdcp, u32 display_index)
{
	struct hdcp_protection_message message;
	bool ret = 0;
	uint8_t Aksv[5];

	memcpy(Aksv, hdcp->tci_buf_addr->HDCP_14_Message.
			RspHDCPCmdOutput.GetAnAksv.Aksv,
			sizeof(Aksv));

	message.version = HDCP_VERSION_14;
	message.link = HDCP_LINK_PRIMARY;
	message.msg_id = HDCP_MESSAGE_ID_WRITE_AKSV;
	message.length = sizeof(Aksv);
	message.data = Aksv;

	ret = dal_process_hdcp_msg(hdcp->adev->dm.dal, display_index, &message);

	return ret;
}

bool hdcpss_read_Bksv(struct hdcpss_data *hdcp, u32 display_index)
{
	struct hdcp_protection_message message;
	bool ret = 0;
	int i = 0;

	message.version = HDCP_VERSION_14;
	message.link = HDCP_LINK_PRIMARY;
	message.msg_id = HDCP_MESSAGE_ID_READ_BKSV;
	message.length = 5;
	message.data = hdcp->Bksv;

	ret = dal_process_hdcp_msg(hdcp->adev->dm.dal, display_index, &message);

	dev_info(hdcp->adev->dev, "Received BKsv\n");
	for (i = 0; i < 5; i++)
		dev_info(hdcp->adev->dev, "BKsv[%d] = %x\n", i, hdcp->Bksv[i]);

	return ret;
}

bool hdcpss_read_Bcaps(struct hdcpss_data *hdcp, u32 display_index)
{
	struct hdcp_protection_message message;
	bool ret = 0;

	message.version = HDCP_VERSION_14;
	message.link = HDCP_LINK_PRIMARY;
	message.msg_id = HDCP_MESSAGE_ID_READ_BCAPS;
	message.length = 1;
	message.data = &hdcp->Bcaps;

	ret = dal_process_hdcp_msg(hdcp->adev->dm.dal, display_index, &message);

	dev_info(hdcp->adev->dev, "Received BCaps = %x\n", hdcp->Bcaps);

	/* Check if Repeater bit is set */
	if (hdcp->Bcaps & (1 << BIT_REPEATER)) {
		dev_info(hdcp->adev->dev, "Connected Receiver is also a Repeater\n");
		hdcp->is_repeater = 1;
	}

	return ret;
}

int hdcpss_send_bksv_bcaps(struct hdcpss_data *hdcp, u32 display_index)
{
	int ret = 0;

	hdcp->tci_buf_addr->HDCP_14_Message
		.CommandHeader.commandId = HDCP_CMD_HOST_CMDS;
	hdcp->tci_buf_addr->HDCP_14_Message.
		CmdHDCPCmdInput.ulDisplayIndex = display_index;
	hdcp->tci_buf_addr->HDCP_14_Message.
		CmdHDCPCmdInput.bCommandCode = HDCP_14_COMMAND_SEND_DATA;

	memcpy(hdcp->tci_buf_addr->HDCP_14_Message.
			CmdHDCPCmdInput.buffer, hdcp->Bksv, 5);

	hdcp->tci_buf_addr->HDCP_14_Message.
		CmdHDCPCmdInput.buffer[5] = hdcp->Bcaps;

	ret = hdcpss_notify_ta(hdcp);

	if (hdcp->tci_buf_addr->HDCP_14_Message.
		ResponseHeader.responseId != IS_RSP(HDCP_14_COMMAND_SEND_DATA))
		dev_err(hdcp->adev->dev, "Invalid Response from TL !\n");

	return ret;
}

bool hdcpss_read_R0not(struct hdcpss_data *hdcp, u32 display_index)
{
	struct hdcp_protection_message message;
	bool ret = 0;
	int i = 0;

	message.version = HDCP_VERSION_14;
	message.link = HDCP_LINK_PRIMARY;
	message.msg_id = HDCP_MESSAGE_ID_READ_RI_R0;
	message.length = 2;
	message.data = hdcp->R_Prime;

	ret = dal_process_hdcp_msg(hdcp->adev->dm.dal, display_index, &message);

	dev_info(hdcp->adev->dev, "Received R0 prime\n");
	for (i = 0; i < 2; i++)
		dev_info(hdcp->adev->dev, "R0_Prime[%d] = %x\n", i,
						hdcp->R_Prime[i]);

	return ret;
}

int hdcpss_send_R0_Prime(struct hdcpss_data *hdcp, u32 display_index)
{
	int ret = 0;

	hdcp->tci_buf_addr->HDCP_14_Message
		.CommandHeader.commandId = HDCP_CMD_HOST_CMDS;
	hdcp->tci_buf_addr->HDCP_14_Message.
		CmdHDCPCmdInput.ulDisplayIndex = display_index;
	hdcp->tci_buf_addr->HDCP_14_Message.
		CmdHDCPCmdInput.bCommandCode = HDCP_14_COMMAND_SEND_DATA;

	memcpy(hdcp->tci_buf_addr->HDCP_14_Message.
			CmdHDCPCmdInput.buffer, &hdcp->R_Prime,
			sizeof(uint16_t));

	ret = hdcpss_notify_ta(hdcp);

	if (hdcp->tci_buf_addr->HDCP_14_Message.ResponseHeader.
			responseId != IS_RSP(HDCP_14_COMMAND_SEND_DATA))
		dev_err(hdcp->adev->dev, "Invalid Response from TL !\n");
	return ret;
}

bool hdcpss_read_bstatus(struct hdcpss_data *hdcp, u32 display_index)
{
	struct hdcp_protection_message message;
	bool ret = 0;
	int i = 0;

	message.version = HDCP_VERSION_14;
	message.link = HDCP_LINK_PRIMARY;
	message.msg_id = HDCP_MESSAGE_ID_READ_BSTATUS;
	message.length = 2;
	message.data = hdcp->bstatus;

	ret = dal_process_hdcp_msg(hdcp->adev->dm.dal, display_index, &message);
	dev_info(hdcp->adev->dev, "Received BStatus\n");
	for (i = 0; i < 2; i++)
		dev_info(hdcp->adev->dev, "BStatus[%d] = %x\n", i,
							hdcp->bstatus[i]);

	return ret;
}

bool hdcpss_read_ksv_fifo(struct hdcpss_data *hdcp, u32 display_index,
						u32 device_count)
{
	struct hdcp_protection_message message;
	bool ret = 0;
	int i = 0;

	/* TODO: Free this memory when not needed */
	hdcp->ksv_fifo_buf = (uint8_t *)kmalloc(5 * device_count, GFP_KERNEL);
	if (!hdcp->ksv_fifo_buf) {
		dev_err(hdcp->adev->dev, "memory allocation failure\n");
		return -ENOMEM;
	}
	message.version = HDCP_VERSION_14;
	message.link = HDCP_LINK_PRIMARY;
	message.msg_id = HDCP_MESSAGE_ID_READ_KSV_FIFO;
	message.length = device_count * 5;
	message.data = hdcp->ksv_fifo_buf;

	hdcp->ksv_list_size = device_count * 5;

	ret = dal_process_hdcp_msg(hdcp->adev->dm.dal, display_index, &message);

	dev_info(hdcp->adev->dev, "Received KSV FIFO data\n");
	for (i = 0; i < (device_count * 5); i++)
		dev_info(hdcp->adev->dev, "KSV FIFO buf[%d] = %x\n", i,
							hdcp->ksv_fifo_buf[i]);

	return ret;
}

bool hdcpss_read_v_prime(struct hdcpss_data *hdcp, u32 display_index)
{
	struct hdcp_protection_message message;
	bool ret = 0;
	int i = 0;

	message.version = HDCP_VERSION_14;
	message.link = HDCP_LINK_PRIMARY;
	message.msg_id = HDCP_MESSAGE_ID_READ_VH_X;
	message.length = 20;
	message.data = hdcp->V_Prime;

	ret = dal_process_hdcp_msg(hdcp->adev->dm.dal, display_index, &message);
	dev_info(hdcp->adev->dev, "Received V Prime\n");
	for (i = 0; i < 20; i++)
		dev_info(hdcp->adev->dev, "V_Prime[%d] = %x\n", i,
							hdcp->V_Prime[i]);

	return ret;
}

int hdcpss_send_ksv_list(struct hdcpss_data *hdcp, u32 display_index)
{
	int ret = 0;

	hdcp->tci_buf_addr->HDCP_14_Message
		.CommandHeader.commandId = HDCP_CMD_HOST_CMDS;
	hdcp->tci_buf_addr->HDCP_14_Message.
		CmdHDCPCmdInput.ulDisplayIndex = display_index;
	hdcp->tci_buf_addr->HDCP_14_Message.
		CmdHDCPCmdInput.bCommandCode = HDCP_14_COMMAND_SEND_DATA;

	memcpy(hdcp->tci_buf_addr->HDCP_14_Message.
			CmdHDCPCmdInput.buffer, hdcp->ksv_fifo_buf,
			hdcp->ksv_list_size);
	memcpy(&hdcp->tci_buf_addr->HDCP_14_Message.
			CmdHDCPCmdInput.buffer[hdcp->ksv_list_size],
			hdcp->V_Prime,
			20);
	ret = hdcpss_notify_ta(hdcp);

	if (hdcp->tci_buf_addr->HDCP_14_Message.
		ResponseHeader.responseId != IS_RSP(HDCP_14_COMMAND_SEND_DATA))
		dev_err(hdcp->adev->dev, "Invalid Response from TL !\n");

	return ret;
}

/*
 * This function will be called when a connect event is detected.
 * It starts the authentication of the receiver.
 */
static int hdcpss_start_hdcp14_authentication(int display_index)
{
	struct hdcpss_data *hdcp = &hdcp_data;
	int ret = 0;
	uint8_t device_count = 0;

	/* Send CREATE_SESSION command to TA */
	ret = hdcpss_read_An_Aksv(hdcp, display_index);
	if (ret) {
		dev_err(hdcp->adev->dev, "Error in reading An and Aksv\n");
		return ret;
	}

	if (hdcp->tci_buf_addr->HDCP_14_Message.
			RspHDCPCmdOutput.bRequestType == REQUESTING_BKSV) {
		/* Write An to Receiver */
		ret = hdcpss_write_An(hdcp, display_index);
		if (ret) {
			dev_err(hdcp->adev->dev, "Error in writing An\n");
			return ret;
		}
		/* Write AKsv to Receiver */
		ret = hdcpss_write_Aksv(hdcp, display_index);
		if (ret) {
			dev_err(hdcp->adev->dev, "Error in writing AKsv\n");
			return ret;
		}
		/* Read BKsv from Receiver */
		ret = hdcpss_read_Bksv(hdcp, display_index);
		if (ret) {
			dev_err(hdcp->adev->dev, "Error in reading bksv\n");
			return ret;
		}
		/* Read BCaps from Receiver */
		ret = hdcpss_read_Bcaps(hdcp, display_index);
		if (ret) {
			dev_err(hdcp->adev->dev, "Error in reading bcaps\n");
			return ret;
		}
		/* Send BKsv and BCaps to TA */
		ret = hdcpss_send_bksv_bcaps(hdcp, display_index);
		if (ret) {
			dev_err(hdcp->adev->dev, "Error sending bksv bcaps\n");
			return ret;
		}
	}

	if (hdcp->tci_buf_addr->HDCP_14_Message.
			RspHDCPCmdOutput.bRequestType == REQUESTING_R_NOT) {
		/* Read R0' from Receiver */
		ret = hdcpss_read_R0not(hdcp, display_index);
		if (ret) {
			dev_err(hdcp->adev->dev, "Error in reading r0not\n");
			return ret;
		}
		/* Send R0' to TA */
		ret = hdcpss_send_R0_Prime(hdcp, display_index);
		if (ret) {
			dev_err(hdcp->adev->dev, "Error in sending r0 prime\n");
			return ret;
		}
	}

	/* Repeater Only */
	if (hdcp->is_repeater) {
		/* Poll for KSV FIFO Ready bit */
		do {
			hdcpss_read_Bcaps(hdcp, display_index);
		} while (!(hdcp->Bcaps & (1 << BIT_FIFO_READY)));

		/* Read Bstatus to know the DEVICE_COUNT */
		hdcpss_read_bstatus(hdcp, display_index);

		if (hdcp->bstatus[0] & (1 << BIT_MAX_DEVS_EXCEDDED))
			dev_err(hdcp->adev->dev,
					"Topology error: MAX_DEVS_EXCEDDED\n");
		else if (hdcp->bstatus[1] & (1 << BIT_MAX_CASCADE_EXCEDDED))
			dev_err(hdcp->adev->dev,
				"Topology error: MAX_CASCADE_EXCEDDED\n");
		else
			device_count = hdcp->bstatus[0] & DEVICE_COUNT_MASK;

		dev_info(hdcp->adev->dev, "Device count = %d\n", device_count);

		/* Read the KSV FIFO */
		hdcpss_read_ksv_fifo(hdcp, display_index, device_count);

		/* Read V' */
		hdcpss_read_v_prime(hdcp, display_index);

		/* Send the KSV list and V' to TA */
		ret = hdcpss_send_ksv_list(hdcp, display_index);
	}
	return 0;
}

/*
 *  This function will be called by DAL when it detects cable plug/unplug event
 *  int display_index : - Display identifier
 *  int event :- event = 1 (Connect),
 *               event = 0 (Disconnect)
 */

void hdcpss_notify_hotplug_detect(int event, int display_index)
{

	if (event) {
		printk("Connect event detected\n");
		hdcpss_start_hdcp14_authentication(display_index);
	} else {
		/* TODO: Handle disconnection */
		printk("Disconnect event detected\n");
	}
}
EXPORT_SYMBOL_GPL(hdcpss_notify_hotplug_detect);
#endif

static inline void *getpagestart(void *addr)
{
	return (void *)(((u64)(addr)) & PAGE_MASK);
}

static inline u32 getoffsetinpage(void *addr)
{
	return (u32)(((u64)(addr)) & (~PAGE_MASK));
}

static inline u32 getnrofpagesforbuffer(void *addrStart, u32 len)
{
	return (getoffsetinpage(addrStart) + len + PAGE_SIZE-1) / PAGE_SIZE;
}

static void flush_buffer(void *addr, u32 size)
{
	struct page *page;
	void *page_start = getpagestart(addr);
	int i;

	for (i = 0; i < getnrofpagesforbuffer(addr, size); i++) {
		page = virt_to_page(page_start);
		flush_dcache_page(page);
		page_start += PAGE_SIZE;
	}
}

int hdcpss_load_ta(struct hdcpss_data *hdcp)
{
	int ret = 0;
	int fence_val = 0;

	/* Create the LOAD_TA command */
	hdcp->cmd_buf_addr->buf_size	= sizeof(struct gfx_cmd_resp);
	hdcp->cmd_buf_addr->buf_version	= PSP_GFX_CMD_BUF_VERSION;
	hdcp->cmd_buf_addr->cmd_id	= GFX_CMD_ID_LOAD_TA;

	hdcp->cmd_buf_addr->u.load_ta.app_phy_addr_hi =
			upper_32_bits(virt_to_phys(hdcp->ta_buf_addr));

	hdcp->cmd_buf_addr->u.load_ta.app_phy_addr_lo =
			lower_32_bits(virt_to_phys(hdcp->ta_buf_addr));

	hdcp->cmd_buf_addr->u.load_ta.app_len = hdcp->ta_size;

	hdcp->cmd_buf_addr->u.load_ta.tci_buf_phy_addr_hi =
			upper_32_bits(virt_to_phys(hdcp->tci_buf_addr));

	hdcp->cmd_buf_addr->u.load_ta.tci_buf_phy_addr_lo =
			lower_32_bits(virt_to_phys(hdcp->tci_buf_addr));

	hdcp->cmd_buf_addr->u.load_ta.tci_buf_len = hdcp->tci_size;

	dev_dbg(hdcp->adev->dev, "Dumping Load TA command contents\n");
	dev_dbg(hdcp->adev->dev, "Buf size = %d\n",
					hdcp->cmd_buf_addr->buf_size);
	dev_dbg(hdcp->adev->dev, "Buf verion = %x\n",
					hdcp->cmd_buf_addr->buf_version);
	dev_dbg(hdcp->adev->dev, "Command ID = %x\n",
					hdcp->cmd_buf_addr->cmd_id);
	dev_dbg(hdcp->adev->dev, "TA phy addr hi = %x\n",
			hdcp->cmd_buf_addr->u.load_ta.app_phy_addr_hi);
	dev_dbg(hdcp->adev->dev, "TA phy addr lo = %x\n",
			hdcp->cmd_buf_addr->u.load_ta.app_phy_addr_lo);
	dev_dbg(hdcp->adev->dev, "TA Size = %d\n",
				hdcp->cmd_buf_addr->u.load_ta.app_len);
	dev_dbg(hdcp->adev->dev, "TCI Phy addr hi  = %x\n",
		hdcp->cmd_buf_addr->u.load_ta.tci_buf_phy_addr_hi);
	dev_dbg(hdcp->adev->dev, "TCI Phy addr lo = %x\n",
		hdcp->cmd_buf_addr->u.load_ta.tci_buf_phy_addr_lo);
	dev_dbg(hdcp->adev->dev, "TCI Size = %d\n",
			hdcp->cmd_buf_addr->u.load_ta.tci_buf_len);

	/* Flush TA buffer */
	flush_buffer((void *)hdcp->ta_buf_addr, hdcp->ta_size);

	/* Flush TCI buffer */
#ifdef HDCPSS_USE_TEST_TA
	flush_buffer((tci_t *)hdcp->tci_buf_addr, hdcp->tci_size);
#else
	flush_buffer((HDCP_TCI *)hdcp->tci_buf_addr, hdcp->tci_size);
#endif

	/* Initialize fence value */
	fence_val = 0x11111111;

	ret = g2p_comm_send_command_buffer(hdcp->cmd_buf_addr,
					sizeof(struct gfx_cmd_resp),
					fence_val);
	if (ret) {
		dev_err(hdcp->adev->dev, "LOAD TA failed\n");
		dev_err(hdcp->adev->dev, "status = %d\n",
					hdcp->cmd_buf_addr->resp.status);
		free_pages((unsigned long)hdcp->tci_buf_addr,
						get_order(hdcp->tci_size));
		free_pages((unsigned long)hdcp->ta_buf_addr,
						get_order(hdcp->ta_size));
		free_pages((unsigned long)hdcp->cmd_buf_addr,
					get_order(hdcp->cmd_buf_size));
		return ret;
	}

	dev_dbg(hdcp->adev->dev, "status = %d\n",
					hdcp->cmd_buf_addr->resp.status);

	/* Check for response from PSP in Response buffer */
	if (!hdcp->cmd_buf_addr->resp.status)
		dev_dbg(hdcp->adev->dev, "session_id = %d\n",
				hdcp->cmd_buf_addr->resp.session_id);

	hdcp->session_id = hdcp->cmd_buf_addr->resp.session_id;

	dev_info(hdcp->adev->dev, "Loaded Trusted Application successfully\n");

	return ret;
}

int hdcpss_unload_ta(struct hdcpss_data *hdcp)
{
	int ret = 0;
	int fence_val = 0;

	/* Submit UNLOAD_TA command */
	hdcp->cmd_buf_addr->buf_size	= sizeof(struct gfx_cmd_resp);
	hdcp->cmd_buf_addr->buf_version	= PSP_GFX_CMD_BUF_VERSION;
	hdcp->cmd_buf_addr->cmd_id	= GFX_CMD_ID_UNLOAD_TA;

	hdcp->cmd_buf_addr->u.unload_ta.session_id = hdcp->session_id;

	dev_dbg(hdcp->adev->dev, "Command id = %d\n",
					hdcp->cmd_buf_addr->cmd_id);

	fence_val = 0x33333333;

	ret = g2p_comm_send_command_buffer(hdcp->cmd_buf_addr,
					sizeof(struct gfx_cmd_resp),
					fence_val);
	if (ret) {
		dev_err(hdcp->adev->dev, "Unloading TA failed\n");
		dev_err(hdcp->adev->dev, "status = %d\n",
					hdcp->cmd_buf_addr->resp.status);
		free_pages((unsigned long)hdcp->tci_buf_addr,
				get_order(hdcp->tci_size));
		free_pages((unsigned long)hdcp->ta_buf_addr,
				get_order(hdcp->ta_size));
		free_pages((unsigned long)hdcp->cmd_buf_addr,
				get_order(hdcp->cmd_buf_size));
		return ret;
	}

	dev_info(hdcp->adev->dev,
			"Unloaded Trusted Application successfully\n");

	return ret;
}

int hdcpss_notify_ta(struct hdcpss_data *hdcp)
{
	int ret = 0;
	int fence_val = 0;

	dev_dbg(hdcp->adev->dev, "session_id = %d\n", hdcp->session_id);

	/* Submit NOTIFY_TA command */
	hdcp->cmd_buf_addr->buf_size	= sizeof(struct gfx_cmd_resp);
	hdcp->cmd_buf_addr->buf_version	= PSP_GFX_CMD_BUF_VERSION;
	hdcp->cmd_buf_addr->cmd_id	= GFX_CMD_ID_NOTIFY_TA;
	hdcp->cmd_buf_addr->u.notify_ta.session_id = hdcp->session_id;

	/* Flush TCI buffer */
#ifdef HDCPSS_USE_TEST_TA
	flush_buffer((tci_t *)hdcp->tci_buf_addr, hdcp->tci_size);
#else
	flush_buffer((HDCP_TCI *)hdcp->tci_buf_addr, hdcp->tci_size);
#endif

	fence_val = 0x22222222;

	ret = g2p_comm_send_command_buffer(hdcp->cmd_buf_addr,
					sizeof(struct gfx_cmd_resp),
					fence_val);
	if (ret) {
		dev_err(hdcp->adev->dev, "NOTIFY TA failed\n");
		dev_err(hdcp->adev->dev, "status = %d\n",
					hdcp->cmd_buf_addr->resp.status);
		free_pages((unsigned long)hdcp->tci_buf_addr,
						get_order(hdcp->tci_size));
		free_pages((unsigned long)hdcp->ta_buf_addr,
						get_order(hdcp->ta_size));
		free_pages((unsigned long)hdcp->cmd_buf_addr,
					get_order(hdcp->cmd_buf_size));
		return ret;
	}
	dev_dbg(hdcp->adev->dev, "status = %d\n",
					hdcp->cmd_buf_addr->resp.status);

#ifdef HDCPSS_USE_TEST_TA
	if (RET_OK != hdcp->tci_buf_addr->message.response.header.returnCode)
		dev_err(hdcp->adev->dev, "Error from Trustlet = %d\n",
		hdcp->tci_buf_addr->message.response.header.returnCode);
#else
	if (RET_OK != hdcp->tci_buf_addr->HDCP_14_Message.ResponseHeader.
						returnCode) {
		dev_err(hdcp->adev->dev, "Error from Trustlet = %d\n",
			hdcp->tci_buf_addr->HDCP_14_Message.
						ResponseHeader.returnCode);
		ret = hdcp->tci_buf_addr->HDCP_14_Message.
						ResponseHeader.returnCode;
	}
#endif
	if (!hdcp->cmd_buf_addr->resp.status)
		dev_info(hdcp->adev->dev, "NOTIFY TA success\n");

	return ret;
}

/*
 *  This function needs to be called as part of init sequence probably
 *  from the display driver.
 *  This function is responsible to allocate required memory for
 *  command buffers and TCI.
 *  This function will require to load the HDCP Trusted application
 *  in the PSP secure world using LOAD_TA command.
 *
 */
int hdcpss_init(void *handle)
{
	int ret = 0;
	const char *fw_name = FIRMWARE_CARRIZO;
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	dev_dbg(adev->dev, "%s\n", __func__);

	hdcp_data.adev = adev;

	hdcp_data.cmd_buf_size = sizeof(struct gfx_cmd_resp);

	/* Allocate physically contiguous memory for the command buffer */
	hdcp_data.cmd_buf_addr = (struct gfx_cmd_resp *)
					__get_free_pages(GFP_KERNEL,
					get_order(hdcp_data.cmd_buf_size));
	if (!hdcp_data.cmd_buf_addr) {
		dev_err(adev->dev, "command buffer memory allocation failed\n");
		return -ENOMEM;
	}

	dev_dbg(adev->dev, "Command buffer address = %p\n",
						hdcp_data.cmd_buf_addr);

	ret = request_firmware(&adev->psp.fw, fw_name, adev->dev);
	if (ret) {
		dev_err(adev->dev, "amdgpu_psp: Can't load firmware \"%s\"\n",
						fw_name);
		return ret;
	}

	dev_dbg(adev->dev, "request_fw success: size of ta = %ld\n",
							adev->psp.fw->size);

	hdcp_data.ta_size = adev->psp.fw->size;

	/* Allocate physically contiguos memory to hold the TA binary */
	hdcp_data.ta_buf_addr = (u64 *)__get_free_pages(GFP_KERNEL,
						get_order(hdcp_data.ta_size));
	if (!hdcp_data.ta_buf_addr) {
		dev_err(adev->dev, "TA buffer memory allocation failed\n");
		free_pages((unsigned long)hdcp_data.cmd_buf_addr,
					get_order(hdcp_data.cmd_buf_size));
		return -ENOMEM;
	}
	dev_dbg(adev->dev, "TA buffer address = %p\n", hdcp_data.ta_buf_addr);

	/* Copy the TA binary into the allocated buffer */
	memcpy(hdcp_data.ta_buf_addr, adev->psp.fw->data, hdcp_data.ta_size);

	/* Allocate physically contiguos memory for TCI */
#ifdef HDCPSS_USE_TEST_TA
	hdcp_data.tci_size = sizeof(tci_t);
	hdcp_data.tci_buf_addr = (tci_t *)__get_free_pages(GFP_KERNEL,
						get_order(hdcp_data.tci_size));
#else
	hdcp_data.tci_size = sizeof(HDCP_TCI);
	hdcp_data.tci_buf_addr = (HDCP_TCI *)__get_free_pages(GFP_KERNEL,
						get_order(hdcp_data.tci_size));
#endif
	if (!hdcp_data.tci_buf_addr) {
		dev_err(adev->dev, "TCI memory allocation failure\n");
		free_pages((unsigned long)hdcp_data.ta_buf_addr,
						get_order(hdcp_data.ta_size));
		free_pages((unsigned long)hdcp_data.cmd_buf_addr,
					get_order(hdcp_data.cmd_buf_size));
		return -ENOMEM;
	}
	dev_dbg(adev->dev, "TCI buffer address = %p\n", hdcp_data.tci_buf_addr);

	ret = hdcpss_load_ta(&hdcp_data);

#ifdef HDCPSS_USE_TEST_TA
	hdcp_data.tci_buf_addr->message.command.header.
						commandId  = CMD_ID_TEE_TEST;
	hdcp_data.tci_buf_addr->message.test.a = 1;
	hdcp_data.tci_buf_addr->message.test.b = 1;
	hdcp_data.tci_buf_addr->message.test.c = 1;

	dev_info(adev->dev, "TCI commandID = %d\n",
		hdcp_data.tci_buf_addr->message.command.header.commandId);
	dev_info(adev->dev, "Host initialized TCI a = %d\n",
					hdcp_data.tci_buf_addr->message.test.a);
	dev_info(adev->dev, "Host initialized TCI b = %d\n",
					hdcp_data.tci_buf_addr->message.test.b);
	dev_info(adev->dev, "Host initialized TCI c = %d\n",
					hdcp_data.tci_buf_addr->message.test.c);

	ret = hdcpss_notify_ta(&hdcp_data);

	dev_info(adev->dev, "PSP updated a = %d\n",
					hdcp_data.tci_buf_addr->message.test.a);
	dev_info(adev->dev, "PSP updated b = %d\n",
					hdcp_data.tci_buf_addr->message.test.b);
	dev_info(adev->dev, "PSP updated c = %d\n",
					hdcp_data.tci_buf_addr->message.test.c);

	ret = hdcpss_unload_ta(&hdcp_data);
#endif
	dev_dbg(adev->dev, "%s exit\n", __func__);
	return 0;
}
EXPORT_SYMBOL_GPL(hdcpss_init);
