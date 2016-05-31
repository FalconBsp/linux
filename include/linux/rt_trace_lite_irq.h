/*
 * rt_trace_lite types and constants
 *
 * Copyright 2008 Sony Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Suite 500, Boston, MA 02110, USA.
 */

#ifndef _LINUX_RT_TRACE_LITE_IRQ_H
#define _LINUX_RT_TRACE_LITE_IRQ_H

#define NR_IRQ_IPI (NR_IRQS + 0)
#define NR_IRQ_LOC (NR_IRQS + 1)
#define NR_IRQ_INV (NR_IRQS + 2)

#define NR_IRQS_EXT (NR_IRQ_INV + 1)

#endif
