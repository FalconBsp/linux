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

#include "irq_service_dce110.h"

#include "dce/dce_11_0_d.h"
#include "dce/dce_11_0_sh_mask.h"
#include "ivsrcid/ivsrcid_vislands30.h"

static bool hpd_ack(
	struct irq_service *irq_service,
	const struct irq_source_info *info)
{
	uint32_t addr = info->status_reg;
	uint32_t value = dal_read_reg(irq_service->ctx, addr);
	uint32_t current_status =
		get_reg_field_value(
			value,
			DC_HPD_INT_STATUS,
			DC_HPD_SENSE_DELAYED);

	dal_irq_service_ack_generic(irq_service, info);

	value = dal_read_reg(irq_service->ctx, info->enable_reg);

	set_reg_field_value(
		value,
		current_status ? 0 : 1,
		DC_HPD_INT_CONTROL,
		DC_HPD_INT_POLARITY);

	dal_write_reg(irq_service->ctx, info->enable_reg, value);

	return true;
}

static const struct irq_source_info_funcs hpd_irq_info_funcs = {
	.set = NULL,
	.ack = hpd_ack
};

static const struct irq_source_info_funcs hpd_rx_irq_info_funcs = {
	.set = NULL,
	.ack = NULL
};

static const struct irq_source_info_funcs pflip_irq_info_funcs = {
	.set = NULL,
	.ack = NULL
};

static const struct irq_source_info_funcs vblank_irq_info_funcs = {
	.set = NULL,
	.ack = NULL
};

#define hpd_int_entry(reg_num)\
	[DAL_IRQ_SOURCE_HPD1 + reg_num] = {\
		.enable_reg = mmHPD ## reg_num ## _DC_HPD_INT_CONTROL,\
		.enable_mask = DC_HPD_INT_CONTROL__DC_HPD_INT_EN_MASK,\
		.enable_value = {\
			DC_HPD_INT_CONTROL__DC_HPD_INT_EN_MASK,\
			~DC_HPD_INT_CONTROL__DC_HPD_INT_EN_MASK\
		},\
		.ack_reg = mmHPD ## reg_num ## _DC_HPD_INT_CONTROL,\
		.ack_mask = DC_HPD_INT_CONTROL__DC_HPD_INT_ACK_MASK,\
		.ack_value = DC_HPD_INT_CONTROL__DC_HPD_INT_ACK_MASK,\
		.status_reg = mmHPD ## reg_num ## _DC_HPD_INT_STATUS,\
		.funcs = &hpd_irq_info_funcs\
	}

#define hpd_rx_int_entry(reg_num)\
	[DAL_IRQ_SOURCE_HPD1RX + reg_num] = {\
		.enable_reg = mmHPD ## reg_num ## _DC_HPD_INT_CONTROL,\
		.enable_mask = DC_HPD_INT_CONTROL__DC_HPD_RX_INT_EN_MASK,\
		.enable_value = {\
			DC_HPD_INT_CONTROL__DC_HPD_RX_INT_EN_MASK,\
			~DC_HPD_INT_CONTROL__DC_HPD_RX_INT_EN_MASK },\
		.ack_reg = mmHPD ## reg_num ## _DC_HPD_INT_CONTROL,\
		.ack_mask = DC_HPD_INT_CONTROL__DC_HPD_RX_INT_ACK_MASK,\
		.ack_value = DC_HPD_INT_CONTROL__DC_HPD_RX_INT_ACK_MASK,\
		.status_reg = mmHPD ## reg_num ## _DC_HPD_INT_STATUS,\
		.funcs = &hpd_rx_irq_info_funcs\
	}
#define pflip_int_entry(reg_num)\
	[DAL_IRQ_SOURCE_PFLIP1 + reg_num] = {\
		.enable_reg = mmDCP ## reg_num ## _GRPH_INTERRUPT_CONTROL,\
		.enable_mask =\
		GRPH_INTERRUPT_CONTROL__GRPH_PFLIP_INT_MASK_MASK,\
		.enable_value = {\
			GRPH_INTERRUPT_CONTROL__GRPH_PFLIP_INT_MASK_MASK,\
			~GRPH_INTERRUPT_CONTROL__GRPH_PFLIP_INT_MASK_MASK},\
		.ack_reg = mmDCP ## reg_num ## _GRPH_INTERRUPT_CONTROL,\
		.ack_mask = GRPH_INTERRUPT_STATUS__GRPH_PFLIP_INT_CLEAR_MASK,\
		.ack_value = GRPH_INTERRUPT_STATUS__GRPH_PFLIP_INT_CLEAR_MASK,\
		.status_reg = mmDCP ## reg_num ##_GRPH_INTERRUPT_STATUS,\
		.funcs = &pflip_irq_info_funcs\
	}

#define vblank_int_entry(reg_num)\
	[DAL_IRQ_SOURCE_CRTC1VSYNC + reg_num] = {\
		.enable_reg = mmCRTC ## reg_num ## _CRTC_INTERRUPT_CONTROL,\
		.enable_mask =\
		CRTC_INTERRUPT_CONTROL__CRTC_V_UPDATE_INT_MSK_MASK,\
		.enable_value = {\
			CRTC_INTERRUPT_CONTROL__CRTC_V_UPDATE_INT_MSK_MASK,\
			~CRTC_INTERRUPT_CONTROL__CRTC_V_UPDATE_INT_MSK_MASK},\
		.ack_reg = mmCRTC ## reg_num ## _CRTC_INTERRUPT_CONTROL,\
		.ack_mask =\
		CRTC_V_UPDATE_INT_STATUS__CRTC_V_UPDATE_INT_CLEAR_MASK,\
		.ack_value =\
		CRTC_V_UPDATE_INT_STATUS__CRTC_V_UPDATE_INT_CLEAR_MASK,\
		.funcs = &vblank_irq_info_funcs\
	}

static const struct irq_source_info
irq_source_info_dce110[DAL_IRQ_SOURCES_NUMBER] = {
	hpd_int_entry(0),
	hpd_int_entry(1),
	hpd_int_entry(2),
	hpd_int_entry(3),
	hpd_int_entry(4),
	hpd_int_entry(5),
	hpd_rx_int_entry(0),
	hpd_rx_int_entry(1),
	hpd_rx_int_entry(2),
	hpd_rx_int_entry(3),
	hpd_rx_int_entry(4),
	hpd_rx_int_entry(5),
	pflip_int_entry(0),
	pflip_int_entry(1),
	pflip_int_entry(2),
	pflip_int_entry(3),
	pflip_int_entry(4),
	pflip_int_entry(5),
	vblank_int_entry(0),
	vblank_int_entry(1),
	vblank_int_entry(2),
	vblank_int_entry(3),
	vblank_int_entry(4),
	vblank_int_entry(5),
};

static enum dal_irq_source to_dal_irq_source(
		struct irq_service *irq_service,
		uint32_t src_id,
		uint32_t ext_id)
{
	switch (src_id) {
	case VISLANDS30_IV_SRCID_D1_V_UPDATE_INT:
		return DAL_IRQ_SOURCE_CRTC1VSYNC;
	case VISLANDS30_IV_SRCID_D2_V_UPDATE_INT:
		return DAL_IRQ_SOURCE_CRTC2VSYNC;
	case VISLANDS30_IV_SRCID_D3_V_UPDATE_INT:
		return DAL_IRQ_SOURCE_CRTC3VSYNC;
	case VISLANDS30_IV_SRCID_D4_V_UPDATE_INT:
		return DAL_IRQ_SOURCE_CRTC4VSYNC;
	case VISLANDS30_IV_SRCID_D5_V_UPDATE_INT:
		return DAL_IRQ_SOURCE_CRTC5VSYNC;
	case VISLANDS30_IV_SRCID_D6_V_UPDATE_INT:
		return DAL_IRQ_SOURCE_CRTC6VSYNC;
	case VISLANDS30_IV_SRCID_D1_GRPH_PFLIP:
		return DAL_IRQ_SOURCE_PFLIP1;
	case VISLANDS30_IV_SRCID_D2_GRPH_PFLIP:
		return DAL_IRQ_SOURCE_PFLIP2;
	case VISLANDS30_IV_SRCID_D3_GRPH_PFLIP:
		return DAL_IRQ_SOURCE_PFLIP3;
	case VISLANDS30_IV_SRCID_D4_GRPH_PFLIP:
		return DAL_IRQ_SOURCE_PFLIP4;
	case VISLANDS30_IV_SRCID_D5_GRPH_PFLIP:
		return DAL_IRQ_SOURCE_PFLIP5;
	case VISLANDS30_IV_SRCID_D6_GRPH_PFLIP:
		return DAL_IRQ_SOURCE_PFLIP6;

	case VISLANDS30_IV_SRCID_HOTPLUG_DETECT_A:
		/* generic src_id for all HPD and HPDRX interrupts */
		switch (ext_id) {
		case VISLANDS30_IV_EXTID_HOTPLUG_DETECT_A:
			return DAL_IRQ_SOURCE_HPD1;
		case VISLANDS30_IV_EXTID_HOTPLUG_DETECT_B:
			return DAL_IRQ_SOURCE_HPD2;
		case VISLANDS30_IV_EXTID_HOTPLUG_DETECT_C:
			return DAL_IRQ_SOURCE_HPD3;
		case VISLANDS30_IV_EXTID_HOTPLUG_DETECT_D:
			return DAL_IRQ_SOURCE_HPD4;
		case VISLANDS30_IV_EXTID_HOTPLUG_DETECT_E:
			return DAL_IRQ_SOURCE_HPD5;
		case VISLANDS30_IV_EXTID_HOTPLUG_DETECT_F:
			return DAL_IRQ_SOURCE_HPD6;
		case VISLANDS30_IV_EXTID_HPD_RX_A:
			return DAL_IRQ_SOURCE_HPD1RX;
		case VISLANDS30_IV_EXTID_HPD_RX_B:
			return DAL_IRQ_SOURCE_HPD2RX;
		case VISLANDS30_IV_EXTID_HPD_RX_C:
			return DAL_IRQ_SOURCE_HPD3RX;
		case VISLANDS30_IV_EXTID_HPD_RX_D:
			return DAL_IRQ_SOURCE_HPD4RX;
		case VISLANDS30_IV_EXTID_HPD_RX_E:
			return DAL_IRQ_SOURCE_HPD5RX;
		case VISLANDS30_IV_EXTID_HPD_RX_F:
			return DAL_IRQ_SOURCE_HPD6RX;
		default:
			return DAL_IRQ_SOURCE_INVALID;
		}
		break;

	default:
		return DAL_IRQ_SOURCE_INVALID;
	}
}

static const struct irq_service_funcs irq_service_funcs_dce110 = {
		.to_dal_irq_source = to_dal_irq_source
};

bool construct(
	struct irq_service *irq_service,
	struct irq_service_init_data *init_data)
{
	if (!dal_irq_service_construct(irq_service, init_data))
		return false;

	irq_service->info = irq_source_info_dce110;
	irq_service->funcs = &irq_service_funcs_dce110;

	return true;
}

struct irq_service *dal_irq_service_dce110_create(
	struct irq_service_init_data *init_data)
{
	struct irq_service *irq_service = dal_alloc(sizeof(*irq_service));

	if (!irq_service)
		return NULL;

	if (construct(irq_service, init_data))
		return irq_service;

	dal_free(irq_service);
	return NULL;
}