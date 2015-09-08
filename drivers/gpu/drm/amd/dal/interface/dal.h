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

#ifndef __DAL_DAL_H__
#define __DAL_DAL_H__

#include "include/dal_interface.h"

/*
* DAL - Display Abstraction Layer
* represents the main high level object that provides
* abstracted display services. One such object needs to
* be created per GPU ASIC.
*/

struct dal {
	struct dal_init_data init_data;
	struct dal_context dal_context;
	struct adapter_service *adapter_srv;
	struct timing_service *timing_srv;
	struct topology_mgr *topology_mgr;
	struct display_service *display_service;
	struct hw_sequencer *hws;
	struct mode_manager *mm;
	struct irq_service *irqs;
};

/* debugging macro definitions */
#define DAL_IF_TRACE()	\
	dal_logger_write(dal_context->logger, \
		LOG_MAJOR_INTERFACE_TRACE, \
		LOG_MINOR_COMPONENT_DAL_INTERFACE, \
		"DAL_IF_TRACE: %s()\n", __func__)

#define DAL_IF_NOT_IMPLEMENTED() \
	DAL_LOGGER_NOT_IMPL(LOG_MINOR_COMPONENT_DAL_INTERFACE, \
			"DAL_IF:%s()\n", __func__)

#define DAL_IF_ERROR(...) \
	dal_logger_write(dal_context->logger, \
		LOG_MAJOR_ERROR, \
		LOG_MINOR_COMPONENT_DAL_INTERFACE, \
		__VA_ARGS__)

enum {
	MAX_PLANE_NUM = 4
};

#endif
