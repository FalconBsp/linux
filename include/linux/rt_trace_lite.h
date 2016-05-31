/*
 * rt_trace_lite functions
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

#ifndef _LINUX_RT_TRACE_LITE_H
#define _LINUX_RT_TRACE_LITE_H

#ifdef CONFIG_SNSC_DEBUG_IRQ_DURATION

extern void notrace irq_handler_rt_trace_enter(int irq);
extern void notrace irq_handler_rt_trace_exit(int irq);
extern void notrace irq_handler_rt_trace_exit_debug(int irq, int debug_num);
extern void show_rt_trace_irq_stat(struct seq_file *p, int irq);

extern ssize_t irq_stat_write(struct file *file, const char __user *buf,
				size_t count, loff_t *ppos);

#else

#define irq_handler_rt_trace_enter(irq) do {} while (0)
#define irq_handler_rt_trace_exit(irq) do {} while (0)
#define irq_handler_rt_trace_exit_debug(irq, debug_num) do {} while (0)
#define show_rt_trace_irq_stat(p, irq) do {} while (0)

#endif


#ifdef CONFIG_SNSC_LITE_IRQSOFF_DEBUG
extern void notrace trace_off_mark(unsigned long data1, unsigned long data2);
#else
#define             trace_off_mark(data1, data2) do {} while (0)
#endif

#ifdef CONFIG_SNSC_LITE_IRQSOFF_DEBUG_MARK_IRQ
#define trace_off_mark_irq(data1, data2) trace_off_mark(data1, data2)
#else
#define trace_off_mark_irq(data1, data2) do {} while (0)
#endif

#ifdef CONFIG_SNSC_LITE_IRQSOFF_DEBUG_MARK_IRQ_DETAIL
#define trace_off_mark_irq_dtl(data1, data2) trace_off_mark(data1, data2)
#else
#define trace_off_mark_irq_dtl(data1, data2) do {} while (0)
#endif


extern void debug_1_rt_trace_enter(void);
extern void debug_1_rt_trace_exit (void);

extern void debug_2_rt_trace_enter(void);
extern void debug_2_rt_trace_exit (void);

extern void debug_3_rt_trace_enter(void);
extern void debug_3_rt_trace_exit (void);

extern void debug_4_rt_trace_enter(void);
extern void debug_4_rt_trace_exit (void);

extern void debug_5_rt_trace_enter(void);
extern void debug_5_rt_trace_exit (void);

extern void debug_6_rt_trace_enter(void);
extern void debug_6_rt_trace_exit (void);

extern void rt_debug_1_init_name(char *name);
extern void rt_debug_2_init_name(char *name);
extern void rt_debug_3_init_name(char *name);
extern void rt_debug_4_init_name(char *name);
extern void rt_debug_5_init_name(char *name);
extern void rt_debug_6_init_name(char *name);

#if 0

Example of defining names and bitfields for D5_PMU_D*:

/*
 * These defines must be a set of contiguous bits, starting with 1.
 */
#define D5_PMU_D1_FUNCTION_A_LOCATION_1    0x00000001
#define D5_PMU_D1_FUNCTION_A_LOCATION_2    0x00000002
#define D5_PMU_D1_FUNCTION_B               0x00000004

#define D5_PMU_D1_NAMES             \
	"function_a_location_1",    \
	"function_a_location_2",    \
	"function_b",               \
	NULL

#endif


#define D5_PMU_D1_NAMES             \
	NULL

#define D5_PMU_D2_NAMES             \
	NULL


#ifdef CONFIG_SNSC_5_RT_TRACE_WITH_PMU

DECLARE_PER_CPU(unsigned long, debug_5_rt_pmu_trace_data_1);
DECLARE_PER_CPU(unsigned long, debug_5_rt_pmu_trace_data_2);

static inline void rt_debug_5_data_1_set(unsigned long value) {
	per_cpu(debug_5_rt_pmu_trace_data_1, raw_smp_processor_id()) = value;
}

static inline void rt_debug_5_data_2_set(unsigned long value) {
	per_cpu(debug_5_rt_pmu_trace_data_2, raw_smp_processor_id()) = value;
}

static inline void rt_debug_5_data_1_or(unsigned long value) {
	per_cpu(debug_5_rt_pmu_trace_data_1, raw_smp_processor_id()) |= value;
}

static inline void rt_debug_5_data_2_or(unsigned long value) {
	per_cpu(debug_5_rt_pmu_trace_data_2, raw_smp_processor_id()) |= value;
}

static inline void rt_debug_5_data_1_add(unsigned long value) {
	per_cpu(debug_5_rt_pmu_trace_data_1, raw_smp_processor_id()) += value;
}

static inline void rt_debug_5_data_2_add(unsigned long value) {
	per_cpu(debug_5_rt_pmu_trace_data_2, raw_smp_processor_id()) += value;
}

#else

static inline void rt_debug_5_data_1_set(unsigned long value) { }
static inline void rt_debug_5_data_2_set(unsigned long value) { }

static inline void rt_debug_5_data_1_or(unsigned long value) { }
static inline void rt_debug_5_data_2_or(unsigned long value) { }

static inline void rt_debug_5_data_1_add(unsigned long value) { }
static inline void rt_debug_5_data_2_add(unsigned long value) { }

#endif

#endif
