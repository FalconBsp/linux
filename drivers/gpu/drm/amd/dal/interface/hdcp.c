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

#include "dal_services.h"

#include "include/dal_interface.h"
#include "include/i2caux_interface.h"
#include "include/connector_interface.h"
#include "include/topology_mgr_interface.h"

#include "dal.h"

static const bool hdcp_cmd_is_read[] = {
	[HDCP_MESSAGE_ID_READ_BKSV] = true,
	[HDCP_MESSAGE_ID_READ_RI_R0] = true,
	[HDCP_MESSAGE_ID_READ_PJ] = true,
	[HDCP_MESSAGE_ID_WRITE_AKSV] = false,
	[HDCP_MESSAGE_ID_WRITE_AINFO] = false,
	[HDCP_MESSAGE_ID_WRITE_AN] = false,
	[HDCP_MESSAGE_ID_READ_VH_X] = true,
	[HDCP_MESSAGE_ID_READ_BCAPS] = true,
	[HDCP_MESSAGE_ID_READ_BSTATUS] = true,
	[HDCP_MESSAGE_ID_READ_KSV_FIFO] = true,
	[HDCP_MESSAGE_ID_READ_BINFO] = true,
};

static const uint8_t hdcp_i2c_offsets[] = {
	[HDCP_MESSAGE_ID_READ_BKSV] = 0x0,
	[HDCP_MESSAGE_ID_READ_RI_R0] = 0x8,
	[HDCP_MESSAGE_ID_READ_PJ] = 0xA,
	[HDCP_MESSAGE_ID_WRITE_AKSV] = 0x10,
	[HDCP_MESSAGE_ID_WRITE_AINFO] = 0x15,
	[HDCP_MESSAGE_ID_WRITE_AN] = 0x18,
	[HDCP_MESSAGE_ID_READ_VH_X] = 0x20,
	[HDCP_MESSAGE_ID_READ_BCAPS] = 0x40,
	[HDCP_MESSAGE_ID_READ_BSTATUS] = 0x41,
	[HDCP_MESSAGE_ID_READ_KSV_FIFO] = 0x43,
	[HDCP_MESSAGE_ID_READ_BINFO] = 0xFF,
};

struct protection_properties {
	bool supported;
	bool (*process_transaction)(
		struct i2caux *i2caux,
		struct ddc *ddc,
		struct adapter_service *as,
		struct hdcp_protection_message *message_info);
};

static const struct protection_properties non_supported_protection = {
	.supported = false
};

static bool hdmi_14_process_transaction(
	struct i2caux *i2caux,
	struct ddc *ddc,
	struct adapter_service *as,
	struct hdcp_protection_message *message_info)
{
	const uint8_t hdcp_i2c_addr_link_primary = 0x3a; /* 0x74 >> 1 */
	const uint8_t hdcp_i2c_addr_link_secondary = 0x3b; /* 0x76 >> 1 */
	struct i2c_command i2c_command;
	uint8_t offset = hdcp_i2c_offsets[message_info->msg_id];
	struct i2c_payload i2c_payloads[] = {
		{ true, 0, 1, &offset },
		/* actual hdcp payload, will be filled later, zeroed for now */
		{ 0 }
	};

	switch (message_info->link) {
	case HDCP_LINK_SECONDARY:
		i2c_payloads[0].address = hdcp_i2c_addr_link_secondary;
		i2c_payloads[1].address = hdcp_i2c_addr_link_secondary;
		break;
	case HDCP_LINK_PRIMARY:
	default:
		i2c_payloads[0].address = hdcp_i2c_addr_link_primary;
		i2c_payloads[1].address = hdcp_i2c_addr_link_primary;
		break;
	}

	i2c_payloads[1].write = !hdcp_cmd_is_read[message_info->msg_id];
	i2c_payloads[1].length = message_info->length;
	i2c_payloads[1].data = message_info->data;

	i2c_command.number_of_payloads = ARRAY_SIZE(i2c_payloads);
	i2c_command.payloads = i2c_payloads;
	i2c_command.engine = I2C_COMMAND_ENGINE_SW;
	i2c_command.speed = dal_adapter_service_get_sw_i2c_speed(as);

	return dal_i2caux_submit_i2c_command(i2caux, ddc, &i2c_command);
}

static const struct protection_properties hdmi_14_protection = {
	.supported = true,
	.process_transaction = hdmi_14_process_transaction
};

static const uint32_t hdcp_dpcd_addrs[] = {
	[HDCP_MESSAGE_ID_READ_BKSV] = 0x68000,
	[HDCP_MESSAGE_ID_READ_RI_R0] = 0x68005,
	[HDCP_MESSAGE_ID_READ_PJ] = 0xFFFFFFFF,
	[HDCP_MESSAGE_ID_WRITE_AKSV] = 0x68007,
	[HDCP_MESSAGE_ID_WRITE_AINFO] = 0x6803B,
	[HDCP_MESSAGE_ID_WRITE_AN] = 0x6800c,
	[HDCP_MESSAGE_ID_READ_VH_X] = 0x68014,
	[HDCP_MESSAGE_ID_READ_BCAPS] = 0x68028,
	[HDCP_MESSAGE_ID_READ_BSTATUS] = 0x68029,
	[HDCP_MESSAGE_ID_READ_KSV_FIFO] = 0x6802c,
	[HDCP_MESSAGE_ID_READ_BINFO] = 0x6802a,
};

static bool dp_11_process_transaction(
	struct i2caux *i2caux,
	struct ddc *ddc,
	struct adapter_service *as,
	struct hdcp_protection_message *message_info)
{
	struct aux_command aux_command;
	struct aux_payload aux_payload;

	aux_payload.address = hdcp_dpcd_addrs[message_info->msg_id];
	aux_payload.i2c_over_aux = false;
	aux_payload.write = !hdcp_cmd_is_read[message_info->msg_id];
	aux_payload.length = message_info->length;
	aux_payload.data = message_info->data;

	aux_command.number_of_payloads = 1;
	aux_command.payloads = &aux_payload;
	aux_command.defer_delay = 0;
	aux_command.max_defer_write_retry = 0;

	return dal_i2caux_submit_aux_command(i2caux, ddc, &aux_command);
}

static const struct protection_properties dp_11_protection = {
	.supported = true,
	.process_transaction = dp_11_process_transaction
};

static const struct protection_properties *get_protection_properties_by_signal(
	enum signal_type st)
{
	switch (st) {
	case SIGNAL_TYPE_DVI_SINGLE_LINK:
	case SIGNAL_TYPE_DVI_SINGLE_LINK1:
	case SIGNAL_TYPE_DVI_DUAL_LINK:
	case SIGNAL_TYPE_HDMI_TYPE_A:
		return &hdmi_14_protection;
	case SIGNAL_TYPE_DISPLAY_PORT:
		return &dp_11_protection;
	default:
		return &non_supported_protection;
	}
}

bool dal_process_hdcp_msg(
	struct dal *dal,
	uint32_t display_index,
	struct hdcp_protection_message *message_info)
{
	struct adapter_service *as = dal->adapter_srv;
	struct i2caux *i2caux = dal_adapter_service_get_i2caux(as);
	struct display_path *dp;
	struct connector *connector;
	struct ddc *ddc;
	bool result = false;

	const struct protection_properties *protection_props;

	if (!message_info)
		return false;

	if (message_info->msg_id < HDCP_MESSAGE_ID_READ_BKSV ||
		message_info->msg_id > HDCP_MESSAGE_ID_READ_BINFO)
		return false;

	dp =
		dal_tm_display_index_to_display_path(
			dal->topology_mgr,
			display_index);

	if (!dp)
		return false;

	protection_props =
		get_protection_properties_by_signal(
			dal_display_path_get_active_signal(
				dp, SINK_LINK_INDEX));

	if (!protection_props->supported)
		return false;

	connector = dal_display_path_get_connector(dp);

	ddc = dal_adapter_service_obtain_ddc(
		as,
		dal_connector_get_graphics_object_id(connector));

	if (!ddc)
		return false;

	result =
		protection_props->process_transaction(
			i2caux,
			ddc,
			as,
			message_info);

	dal_adapter_service_release_ddc(as, ddc);
	return result;
}

