/*
 * Copyright 2012-14 Advanced Micro Devices, Inc.
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

#ifndef __DAL_HW_DDC_H__
#define __DAL_HW_DDC_H__

struct hw_ddc_mask {
	uint32_t DC_GPIO_DDC_MASK_MASK;
	uint32_t DC_GPIO_DDC_PD_EN_MASK;
	uint32_t DC_GPIO_DDC_RECV_MASK;
	uint32_t AUX_PAD_MODE_MASK;
	uint32_t AUX_POL_MASK;
	uint32_t DC_GPIO_DDCCLK_STR_MASK;
};

struct hw_ddc {
	struct hw_gpio base;
	struct hw_ddc_mask mask;
};

#define HW_DDC_FROM_BASE(hw_gpio) \
	container_of((HW_GPIO_FROM_BASE(hw_gpio)), struct hw_ddc, base)

bool dal_hw_ddc_construct(
	struct hw_ddc *pin,
	enum gpio_id id,
	uint32_t en,
	struct dal_context *ctx);

void dal_hw_ddc_destruct(
	struct hw_ddc *pin);

bool dal_hw_ddc_open(
	struct hw_gpio_pin *ptr,
	enum gpio_mode mode,
	void *options);

#endif