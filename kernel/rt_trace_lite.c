/*
 *  Light weight capture and reporting of irq off times.
 *
 *  Inspired by and some code copied from 2.6.24-rt1 kernel/latency_trace.c
 *  If all trace options are enabled then the overhead is similar to the
 *  overhead of kernel/latency_trace.c.  If only one trace option is enabled
 *  then the overhead is less than kernel/latency_trace.c.
 *
 *  WARNING: (don't say I didn't warn you...)
 *  The CONFIG_* ifdefs make this code somewhat ugly and fragile.  I would love
 *  to rip most of them out, but they allow a compile-time choice of lower
 *  overhead vs more information.  Both choices are important.
 *
 * Copyright (C) 2008-2010 Sony Corporation of America, Frank Rowand
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

#include <linux/percpu.h>
#include <linux/seq_file.h>
#include <linux/clocksource.h>
#include <linux/proc_fs.h>
#include <linux/rtc.h>
#include <linux/sched.h>
#include <linux/kallsyms.h>
#include <linux/kernel.h>
#include <linux/rt_trace_lite_irq.h>
#include <linux/rt_trace_lite.h>
#include <linux/ftrace.h>
#define GET_CYCLES_BITS 64

#ifdef CONFIG_SNSC_5_RT_TRACE_PMU_TRACE

static int pmu_trace_enable;

static char *rt_debug_5_d1_name[] = {
	D5_PMU_D1_NAMES,
	NULL
};

static char *rt_debug_5_d2_name[] = {
	D5_PMU_D2_NAMES,
	NULL
};

DEFINE_PER_CPU(unsigned long, debug_5_rt_pmu_trace_data_1);
DEFINE_PER_CPU(unsigned long, debug_5_rt_pmu_trace_data_2);

#endif

#ifdef CONFIG_SNSC_5_RT_TRACE_WITH_PMU

static int pmu_hist_evt0_div_evt1;
static int pmu_hist_evt0_percent_ccnt;
static unsigned long scale_bin_evt_ccnt = 1;
static unsigned long scale_bin_evt_0    = 1;
static unsigned long scale_bin_evt_1    = 1;
static unsigned long offset_bin_evt_ccnt;
static unsigned long offset_bin_evt_0;
static unsigned long offset_bin_evt_1;
#endif

int trace_print_loc_1 = 0;
int trace_use_raw_cycles = 0;

int irqs_dump_cpu = 0;
int irqs_off_dump_threshold = 0;
int irqs_preempt_off_dump_threshold = 0;
int preempt_off_dump_threshold = 0;

#if defined(CONFIG_SNSC_LITE_IRQSOFF_DEBUG_MARK_WITH_PMU) \
 || defined(CONFIG_SNSC_5_RT_TRACE_WITH_PMU)
static int mark_with_pmu_enable = 0;
#endif

#define BIN_PMU_MAX 4095
#define BIN_PMU_NUM (BIN_PMU_MAX + 1)

#define BIN_MAX 1023
#define BIN_NUM (BIN_MAX + 1)

#define MARK_MAX CONFIG_SNSC_LITE_IRQSOFF_DEBUG_MARK_MAX
#define MARK_SAVE_MAX 10

struct mark_usec {
	unsigned long loc_0;
	unsigned long loc_1;
	unsigned long time;
	unsigned long data1;
	unsigned long data2;
#ifdef CONFIG_SNSC_LITE_IRQSOFF_DEBUG_MARK_WITH_PMU
	unsigned long ccnt;
	unsigned long pmn0;
	unsigned long pmn1;
#endif
};

struct mark_raw {
	cycle_t loc_0;
	cycle_t loc_1;
	cycle_t time;
	cycle_t data1;
	cycle_t data2;
#ifdef CONFIG_SNSC_LITE_IRQSOFF_DEBUG_MARK_WITH_PMU
	cycle_t ccnt;
	cycle_t pmn0;
	cycle_t pmn1;
#endif
};


#ifdef CONFIG_SNSC_DEBUG_IRQ_DURATION

struct irq_stat {
	unsigned long      count;
	unsigned long long total;
	unsigned int       min;
	unsigned int       max;
};

DEFINE_PER_CPU(struct irq_stat[NR_IRQS_EXT], lite_irq_stat);

#endif

struct pmu_data {
	unsigned long      enter_ccnt;
	unsigned long      enter_evt0;
	unsigned long      enter_evt1;

	unsigned long      ccnt_count;
	unsigned long long ccnt_total;
	unsigned long      ccnt_min;
	unsigned long      ccnt_max;

	unsigned long      evt0_count;
	unsigned long long evt0_total;
	unsigned long      evt0_min;
	unsigned long      evt0_max;

	unsigned long      evt1_count;
	unsigned long long evt1_total;
	unsigned long      evt1_min;
	unsigned long      evt1_max;

#ifdef CONFIG_SNSC_5_RT_TRACE_WITH_PMU_HISTOGRAM
	unsigned long      bin_ccnt[BIN_PMU_NUM];
	unsigned long      bin_evt0[BIN_PMU_NUM];
	unsigned long      bin_evt1[BIN_PMU_NUM];
#endif
};

#define PMU_TRACE_MAX 19999
#define PMU_TRACE_NUM (PMU_TRACE_MAX + 1)
struct pmu_trace {
	unsigned long long ts;
	unsigned long      ccnt;
	unsigned long      evt0;
	unsigned long      evt1;
	unsigned long      data_1;
	unsigned long      data_2;
};

/*
 * Some of the CONFIG_SNSC_LITE_IRQSOFF_DEBUG_MARK_DEEP_EIP_STACK code might
 * be a little cleaner if arrays were used to hold the various *_eip0 .. *_eip5
 * fields.  But that would also increase the cost of accessing *_eip0, which
 * should be minimized for the performance sensitive cases.  So use slightly
 * ugly code.
 */

struct _off_data {
	cycle_t            off_time;
	unsigned long      off_eip0;
#ifdef CONFIG_SNSC_LITE_IRQSOFF_DEBUG
	unsigned long      off_eip1;
#ifdef CONFIG_SNSC_LITE_IRQSOFF_DEBUG_MARK_DEEP_EIP_STACK
	unsigned long      off_eip2;
	unsigned long      off_eip3;
	unsigned long      off_eip4;
	unsigned long      off_eip5;
#endif
#endif
	unsigned long      count;
	unsigned long long total;
	unsigned long      min;
	unsigned long      max;
#ifdef CONFIG_SNSC_5_RT_TRACE_WITH_PMU
	struct pmu_data   *pmu_data;
#endif
#ifdef CONFIG_SNSC_5_RT_TRACE_PMU_TRACE
	unsigned int       pmu_trace_next;
	struct pmu_trace  *pmu_trace;
#endif
#ifdef CONFIG_SNSC_LITE_IRQSOFF_DEBUG
	unsigned long      end_eip0;
	unsigned long      end_eip1;
#ifdef CONFIG_SNSC_LITE_IRQSOFF_DEBUG_MARK_DEEP_EIP_STACK
	unsigned long      end_eip2;
	unsigned long      end_eip3;
	unsigned long      end_eip4;
	unsigned long      end_eip5;
#endif
	unsigned long      start_eip0;
	unsigned long      start_eip1;
#ifdef CONFIG_SNSC_LITE_IRQSOFF_DEBUG_MARK_DEEP_EIP_STACK
	unsigned long      start_eip2;
	unsigned long      start_eip3;
	unsigned long      start_eip4;
	unsigned long      start_eip5;
#endif
	int                next_mark;
	int                next_mark_save;
#ifdef CONFIG_SNSC_LITE_IRQSOFF_DEBUG_MARK_WITH_PMU
	unsigned long      ccnt;
	unsigned long      pmn0;
	unsigned long      pmn1;
#endif
	struct mark_raw    mark[MARK_MAX + 1];
	unsigned long      mark_save_off_time  [MARK_SAVE_MAX + 1];
	unsigned long      mark_save_max       [MARK_SAVE_MAX + 1];
	unsigned long      mark_save_end_eip0  [MARK_SAVE_MAX + 1];
	unsigned long      mark_save_end_eip1  [MARK_SAVE_MAX + 1];
#ifdef CONFIG_SNSC_LITE_IRQSOFF_DEBUG_MARK_DEEP_EIP_STACK
	unsigned long      mark_save_end_eip2  [MARK_SAVE_MAX + 1];
	unsigned long      mark_save_end_eip3  [MARK_SAVE_MAX + 1];
	unsigned long      mark_save_end_eip4  [MARK_SAVE_MAX + 1];
	unsigned long      mark_save_end_eip5  [MARK_SAVE_MAX + 1];
#endif
	unsigned long      mark_save_start_eip0[MARK_SAVE_MAX + 1];
	unsigned long      mark_save_start_eip1[MARK_SAVE_MAX + 1];
#ifdef CONFIG_SNSC_LITE_IRQSOFF_DEBUG_MARK_DEEP_EIP_STACK
	unsigned long      mark_save_start_eip2[MARK_SAVE_MAX + 1];
	unsigned long      mark_save_start_eip3[MARK_SAVE_MAX + 1];
	unsigned long      mark_save_start_eip4[MARK_SAVE_MAX + 1];
	unsigned long      mark_save_start_eip5[MARK_SAVE_MAX + 1];
#endif
#ifdef CONFIG_SNSC_LITE_IRQSOFF_DEBUG_MARK_WITH_PMU
	unsigned long      mark_save_ccnt      [MARK_SAVE_MAX + 1];
	unsigned long      mark_save_pmn0      [MARK_SAVE_MAX + 1];
	unsigned long      mark_save_pmn1      [MARK_SAVE_MAX + 1];
#endif
	struct mark_usec   mark_save           [MARK_SAVE_MAX + 1][MARK_MAX + 1];
#endif
#ifdef CONFIG_SNSC_LITE_IRQSOFF_DEBUG_HISTOGRAM
	unsigned long      bin[BIN_NUM];
#endif
} ____cacheline_aligned_in_smp;

struct _off_entry {
	struct _off_data *od;
	int               cpu;
	int               bin_index;
	int               last_index_printed;
	char             *name;
	int               no_eip;
};

#if defined(CONFIG_SNSC_LITE_IRQSOFF_DEBUG_MARK_WITH_PMU) \
 || defined(CONFIG_SNSC_5_RT_TRACE_WITH_PMU)
struct pmu_info {
	unsigned int evt0;
	unsigned int evt1;
};

struct pmu_info s_mark_with_pmu;
#endif

/* __________________________________________________________________________ */
static unsigned long notrace cycles_to_us(cycle_t delta)
{
	if (!trace_use_raw_cycles)
		return cycles_to_usecs(delta);
#ifdef CONFIG_X86
	do_div(delta, cpu_khz/1000+1);
#elif defined(CONFIG_PPC)
	delta = mulhwu(tb_to_us, delta);
#elif defined(CONFIG_ARM)
	delta = mach_cycles_to_usecs(delta);
#elif defined(CONFIG_MIPS)
	do_div(delta, mips_hpt_frequency/1000000);
#else
	#error Implement cycles_to_usecs.
#endif

	return (unsigned long) delta;
}


static inline cycle_t notrace now(void)
{
	if (trace_use_raw_cycles)
		return get_cycles();
	/* This is used in 2.6.23-RT */
	/* return get_monotonic_cycles(); */
	return irsoff_clocksource_read();
}


static inline int notrace cycle_delta(cycle_t a, cycle_t b)
{
	if ((GET_CYCLES_BITS == 32) && trace_use_raw_cycles) {
		return (unsigned)a - (unsigned)b;
	} else {
		return a - b;
	}
}


/* __________________________________________________________________________ */


/* __________________________________________________________________________ */
/* functions for handling /proc/ files */


/*
 * treat file record number (*pos) of /proc/irqs_off as data record number,
 * ignore header and footer because they are printed by _off_show() as:
 *   (header): part of the first record for each cpu
 *   (footer): part of the last record for each cpu
 */


static void *_off_next(struct seq_file *m, void *v, loff_t *pos)
{
	struct _off_entry *entry;

	entry = v;

	if (++(entry->bin_index) > BIN_MAX) {
		if (++(entry->cpu) >= NR_CPUS) {
			return NULL;
		} else {
			entry->od++;
			entry->bin_index = 0;
		}
	}

	*pos = (entry->cpu * BIN_NUM) + entry->bin_index;

	return v;
}


static inline void print_bin(struct seq_file *m, struct _off_data *od, int bin_index)
{
#ifdef CONFIG_SNSC_LITE_IRQSOFF_DEBUG_HISTOGRAM
	seq_printf(m, "%5d\t%16lu\n", bin_index, od->bin[bin_index]);
#endif
}


#ifdef CONFIG_SNSC_5_RT_TRACE_WITH_PMU
static inline void print_bin_bin(struct seq_file *m, unsigned long bin[],
	unsigned int offset, unsigned int scale, int bin_index)
{
	seq_printf(m, "%8d\t%16lu\n",
			offset + (scale * bin_index),
			bin[bin_index]);
}
#endif


#ifdef CONFIG_SNSC_LITE_IRQSOFF_DEBUG
static void notrace print_name_eip(struct seq_file *m, unsigned long eip)
{
	char namebuf[KSYM_NAME_LEN+1];
	unsigned long size, offset;
	const char *sym_name;
	char *modname;

	if (eip) {
		sym_name = kallsyms_lookup(eip, &size, &offset, &modname, namebuf);
		if (sym_name)
			seq_printf(m, "%08lx %s+%#lx/%#lx",
				   eip, sym_name, offset, size);
		else
			seq_printf(m, "%08lx", eip);
	} else
		seq_printf(m, "0");
}


static inline void print_mark(struct seq_file *m, struct _off_data *od, int event)
{
	int k;

	for (k = 0; k <= MARK_MAX; k++) {
		if (od->mark_save[event][k].loc_0) {
			seq_printf(m,"#         %8ld ",
				cycles_to_us(cycle_delta(od->mark_save[event][k].time,
					od->mark_save_off_time[event])));

			print_name_eip(m, od->mark_save[event][k].loc_0);
			seq_printf(m," ");

#ifndef CONFIG_SNSC_LITE_IRQSOFF_DEBUG_MARK_WITH_PMU
			seq_printf(m," %8lx %8lx  %ld %ld\n",
				od->mark_save[event][k].data1,
				od->mark_save[event][k].data2,
				od->mark_save[event][k].data1,
				od->mark_save[event][k].data2);
#else
#define DELTA(a, b)     ((b)>=(a)?((b)-(a)):((b)+(ULONG_MAX-(a))))

			seq_printf(m," %8lx %8lx  %ld %ld  %lu %lu %lu\n",
			        od->mark_save[event][k].data1,
				od->mark_save[event][k].data2,
				od->mark_save[event][k].data1,
				od->mark_save[event][k].data2,
				DELTA(od->mark_save_ccnt[event], od->mark_save[event][k].ccnt),
			        DELTA(od->mark_save_pmn0[event], od->mark_save[event][k].pmn0),
			        DELTA(od->mark_save_pmn1[event], od->mark_save[event][k].pmn1));
#endif


			if (trace_print_loc_1) {
				seq_printf(m,"#                    (");
				print_name_eip(m, od->mark_save[event][k].loc_1);
				seq_printf(m,")\n");
			}
		}
	}
}
#endif /* CONFIG_SNSC_LITE_IRQSOFF_DEBUG */


static inline void print_all_marks(struct seq_file *m, struct _off_data *od)
{
#ifdef CONFIG_SNSC_LITE_IRQSOFF_DEBUG
	int j;

#define print_max_start_eip(NUM)                                               \
	seq_printf(m,"#max start eip" #NUM "          : ");                    \
	print_name_eip(m, od->start_eip##NUM);                                 \
	seq_printf(m,"\n");

#define print_max_end_eip(NUM)                                                 \
	seq_printf(m,"#max end   eip" #NUM "          : ");                    \
	print_name_eip(m, od->end_eip##NUM);                                   \
	seq_printf(m,"\n");

#ifdef CONFIG_SNSC_LITE_IRQSOFF_DEBUG_MARK_DEEP_EIP_STACK
	print_max_start_eip(5);
	print_max_start_eip(4);
	print_max_start_eip(3);
	print_max_start_eip(2);
#endif

	print_max_start_eip(1);
	print_max_start_eip(0);

	print_max_end_eip(0);
	print_max_end_eip(1);

#ifdef CONFIG_SNSC_LITE_IRQSOFF_DEBUG_MARK_DEEP_EIP_STACK
	print_max_end_eip(2);
	print_max_end_eip(3);
	print_max_end_eip(4);
	print_max_end_eip(5);
#endif

	if (od->mark_save_start_eip0[0]) {
#ifndef CONFIG_SNSC_LITE_IRQSOFF_DEBUG_MARK_WITH_PMU
		seq_printf(m,"#mark fields    : time, location, data1, data2\n");
#else
		seq_printf(m,"#mark fields    : time, location, data1, data2, ccnt, pmn0, pmn1\n");
#endif
	}

#define print_start_eip(NUM)                                                   \
	seq_printf(m,"#      start eip" #NUM ": ");                            \
	print_name_eip(m, od->mark_save_start_eip##NUM[j]);                    \
	seq_printf(m,"\n");

#define print_end_eip(NUM)                                                     \
	seq_printf(m,"#      end   eip" #NUM ": ");                            \
	print_name_eip(m, od->mark_save_end_eip##NUM  [j]);                    \
	seq_printf(m,"\n");

	for (j = 0; j <= MARK_SAVE_MAX; j++) {
		if (od->mark_save_start_eip0[j]) {

			seq_printf(m,"#mark           : ----- %2d  max: %8ld -----\n",
					j, od->mark_save_max[j]);

#ifdef CONFIG_SNSC_LITE_IRQSOFF_DEBUG_MARK_DEEP_EIP_STACK
			print_start_eip(5);
			print_start_eip(4);
			print_start_eip(3);
			print_start_eip(2);
#endif

			print_start_eip(1);
			print_start_eip(0);

			print_mark(m, od, j);

			print_end_eip(0);
			print_end_eip(1);

#ifdef CONFIG_SNSC_LITE_IRQSOFF_DEBUG_MARK_DEEP_EIP_STACK
			print_end_eip(2);
			print_end_eip(3);
			print_end_eip(4);
			print_end_eip(5);
#endif
		}
	}
#endif /* CONFIG_SNSC_LITE_IRQSOFF_DEBUG */
}


static inline void print_cal_header(struct seq_file *m, struct _off_data *od, struct _off_entry *entry);

#ifdef CONFIG_SNSC_5_RT_TRACE_PMU_TRACE
static void show_pmu_trace(struct seq_file *m, struct _off_data *od)
{
	int k;
	unsigned long mask;

	/* only rt_debug_5 will have non-null bin */
	if (od->pmu_trace == NULL) {
		return;
	}

	seq_printf(m, "\n#PMU trace: ts ccnt evt0 evt1 data_1 data_2\n");

	mask = 1;
	for (k = 0; rt_debug_5_d1_name[k]; k++) {
		seq_printf(m, "# data_1 0x%016lx %s\n",
			   mask, rt_debug_5_d1_name[k]);
		mask <<= 1;
	}

	mask = 1;
	for (k = 0; rt_debug_5_d2_name[k]; k++) {
		seq_printf(m, "# data_2 0x%016lx %s\n",
			   mask, rt_debug_5_d2_name[k]);
		mask <<= 1;
	}

	for (k = 0; k < od->pmu_trace_next; k++) {
		seq_printf(m, "%llu %lu %lu %lu %lu %lu\n",
			   od->pmu_trace[k].ts,
			   od->pmu_trace[k].ccnt,
			   od->pmu_trace[k].evt0,
			   od->pmu_trace[k].evt1,
			   od->pmu_trace[k].data_1,
			   od->pmu_trace[k].data_2);
	}
}

#else

static inline void show_pmu_trace(struct seq_file *m, struct _off_data *od)
{
}
#endif


#ifdef CONFIG_SNSC_5_RT_TRACE_WITH_PMU
static void show_pmu_hist(struct seq_file *m, struct _off_data *od, int pmu_index, int cpu)
{
#define NAME_LEN 80

#ifdef CONFIG_SNSC_5_RT_TRACE_WITH_PMU_HISTOGRAM
	int bin_index;
	int last_index_printed = 0;
	unsigned long *bin;
	unsigned long offset;
	unsigned long scale;
#endif
	unsigned long count;
	unsigned long long total;
	unsigned long min;
	unsigned long max;
	char name[NAME_LEN];
	struct pmu_info *pi = &s_mark_with_pmu;

	if (od->pmu_data == NULL)
		return;

	if (pmu_index == 1) {

		snprintf(name, NAME_LEN, "PMU ccnt");
		count   = od->pmu_data->ccnt_count;
		total   = od->pmu_data->ccnt_total;
		min     = od->pmu_data->ccnt_min;
		max     = od->pmu_data->ccnt_max;
#ifdef CONFIG_SNSC_5_RT_TRACE_WITH_PMU_HISTOGRAM
		bin     = od->pmu_data->bin_ccnt;
		offset  = offset_bin_evt_ccnt;
		scale   = scale_bin_evt_ccnt;
#endif

	} else if (pmu_index == 2) {

		if (pmu_hist_evt0_div_evt1) {
			snprintf(name, NAME_LEN, "PMU evt0/evt1 = 0x%02x/0x%02x",
				pi->evt0, pi->evt1);
		} else if (pmu_hist_evt0_percent_ccnt) {
			snprintf(name, NAME_LEN,
				"PMU (evt0 * 1000) / ccnt = (0x%02x * 1000) / ccnt",
				pi->evt0);
		} else {
			snprintf(name, NAME_LEN, "PMU evt0 = 0x%02x", pi->evt0);
		}
		count   = od->pmu_data->evt0_count;
		total   = od->pmu_data->evt0_total;
		min     = od->pmu_data->evt0_min;
		max     = od->pmu_data->evt0_max;
#ifdef CONFIG_SNSC_5_RT_TRACE_WITH_PMU_HISTOGRAM
		bin     = od->pmu_data->bin_evt0;
		offset  = offset_bin_evt_0;
		scale   = scale_bin_evt_0;
#endif

	} else if (pmu_index == 3) {

		snprintf(name, NAME_LEN, "PMU evt1 = 0x%02x", pi->evt1);
		count   = od->pmu_data->evt1_count;
		total   = od->pmu_data->evt1_total;
		min     = od->pmu_data->evt1_min;
		max     = od->pmu_data->evt1_max;
#ifdef CONFIG_SNSC_5_RT_TRACE_WITH_PMU_HISTOGRAM
		bin     = od->pmu_data->bin_evt1;
		offset  = offset_bin_evt_1;
		scale   = scale_bin_evt_1;
#endif

	} else {
		return;
	}


	seq_printf(m, "\n");
	if (count) {
		seq_printf(m,"#%-26s cpu %d min,avg,max,count : "
			"%3ld %3lld %4ld %7ld\n",
			name,
			cpu,
			min,
			total / count,
			max,
			count);
	} else {
		seq_printf(m,"#%-26s cpu %d min,avg,max,count : "
			"%3d %3d %4d %7d\n",
			name, cpu, 0, 0, 0, 0);
	}


#ifdef CONFIG_SNSC_5_RT_TRACE_WITH_PMU_HISTOGRAM

	seq_printf(m, "#offset: %ld scale_factor: %ld max_bin: %ld\n",
			offset, scale, scale * BIN_PMU_MAX);

	for (bin_index = 0; bin_index <= BIN_PMU_MAX; bin_index++) {
		if (bin[bin_index] != 0) {

			if (last_index_printed) {
				if (last_index_printed < bin_index - 2)
					print_bin_bin(m, bin, offset, scale, last_index_printed + 1);

				if (last_index_printed < bin_index - 1)
					print_bin_bin(m, bin, offset, scale, bin_index - 1);
			}

			print_bin_bin(m, bin, offset, scale, bin_index);
			last_index_printed = bin_index;

		}
	}
#endif	/* CONFIG_SNSC_5_RT_TRACE_WITH_PMU_HISTOGRAM */

}

#else

static inline void show_pmu_hist(struct seq_file *m, struct _off_data *od, int pmu_index, int cpu)
{
}
#endif	/* CONFIG_SNSC_5_RT_TRACE_WITH_PMU */


static int _off_show(struct seq_file *m, void *v)
{
	int bin_index;
	struct _off_entry *entry;
	struct _off_data *od;

	entry     = v;
	bin_index = entry->bin_index;
	od        = entry->od;
	if (bin_index == 0) {

		entry->last_index_printed = 0;

		if (entry->no_eip)
			print_cal_header(m, od, entry);
		else {

			seq_printf(m,"#cpu                     : %8d\n",  entry->cpu);
			seq_printf(m,"#Minimum %-16s: %8lu\n", entry->name, od->min);
			seq_printf(m,"#Average %-16s: %8llu\n", entry->name,
					(od->count == 0) ? 0 : od->total/od->count);
			seq_printf(m,"#Maximum %-16s: %8lu\n", entry->name, od->max);
			seq_printf(m,"#Total samples           : %8lu\n", od->count);

			print_all_marks(m, od);

		}

#ifdef CONFIG_SNSC_LITE_IRQSOFF_DEBUG_HISTOGRAM
		seq_printf(m,"#usecs           samples\n");
#endif
	}

#ifdef CONFIG_SNSC_LITE_IRQSOFF_DEBUG_HISTOGRAM
	if (od->bin[bin_index] != 0) {

		int last_index_printed = entry->last_index_printed;

		if (last_index_printed) {
			if (last_index_printed < bin_index - 2)
				print_bin(m, od, last_index_printed + 1);

			if (last_index_printed < bin_index - 1)
				print_bin(m, od, bin_index - 1);
		}

		print_bin(m, od, bin_index);
		entry->last_index_printed = bin_index;

	}
#endif


	if (bin_index == BIN_MAX) {
		show_pmu_hist(m, od, 1, entry->cpu);
		show_pmu_hist(m, od, 2, entry->cpu);
		show_pmu_hist(m, od, 3, entry->cpu);
		show_pmu_trace(m, od);
	}


	if ((bin_index == BIN_MAX) && (entry->cpu != (NR_CPUS - 1))) {
		seq_printf(m, "\n");
	}

	return 0;
}


#ifdef CONFIG_SNSC_LITE_IRQSOFF_DEBUG
static void mark_reset(struct _off_data *od)
{
	int j, k;
	od->next_mark      = 0;
	od->next_mark_save = 0;
	for (k = 0; k <= MARK_MAX; k++)
		od->mark[k].loc_0 = 0;
	for (j = 0; j <= MARK_SAVE_MAX; j++) {

		od->mark_save_start_eip0[j] = 0;

		for (k = 0; k <= MARK_MAX; k++)
			od->mark_save[j][k].loc_0 = 0;
	}
}
#endif


static void _off_data_reset(struct _off_data *od)
{
#ifdef CONFIG_SNSC_LITE_IRQSOFF_DEBUG_HISTOGRAM
	int k;

	for (k=0; k <= BIN_MAX; k++)
		od->bin[k] = 0;
#endif

#ifdef CONFIG_SNSC_5_RT_TRACE_WITH_PMU
	if (od->pmu_data) {

		od->pmu_data->ccnt_count = 0;
		od->pmu_data->ccnt_total = 0;
		od->pmu_data->ccnt_max   = 0;
		od->pmu_data->ccnt_min   = 0;
		od->pmu_data->ccnt_min   = ~od->pmu_data->ccnt_min;

		od->pmu_data->evt0_count = 0;
		od->pmu_data->evt0_total = 0;
		od->pmu_data->evt0_max   = 0;
		od->pmu_data->evt0_min   = 0;
		od->pmu_data->evt0_min   = ~od->pmu_data->evt0_min;

		od->pmu_data->evt1_count = 0;
		od->pmu_data->evt1_total = 0;
		od->pmu_data->evt1_max   = 0;
		od->pmu_data->evt1_min   = 0;
		od->pmu_data->evt1_min   = ~od->pmu_data->evt1_min;

#ifdef CONFIG_SNSC_5_RT_TRACE_WITH_PMU_HISTOGRAM
		{
		int k;
		for (k=0; k <= BIN_PMU_MAX; k++) {
			od->pmu_data->bin_ccnt[k] = 0;
			od->pmu_data->bin_evt0[k] = 0;
			od->pmu_data->bin_evt1[k] = 0;
		}
		}
#endif

	}
#endif	/* CONFIG_SNSC_5_RT_TRACE_WITH_PMU */

#ifdef CONFIG_SNSC_5_RT_TRACE_PMU_TRACE
	od->pmu_trace_next = 0;
#endif

#ifdef CONFIG_SNSC_LITE_IRQSOFF_DEBUG
	od->off_eip1   = 0;
#ifdef CONFIG_SNSC_LITE_IRQSOFF_DEBUG_MARK_DEEP_EIP_STACK
	od->off_eip2   = 0;
	od->off_eip3   = 0;
	od->off_eip4   = 0;
	od->off_eip5   = 0;
#endif
	od->end_eip0   = 0;
	od->end_eip1   = 0;
#ifdef CONFIG_SNSC_LITE_IRQSOFF_DEBUG_MARK_DEEP_EIP_STACK
	od->end_eip2   = 0;
	od->end_eip3   = 0;
	od->end_eip4   = 0;
	od->end_eip5   = 0;
#endif
	od->start_eip0 = 0;
	od->start_eip1 = 0;
#ifdef CONFIG_SNSC_LITE_IRQSOFF_DEBUG_MARK_DEEP_EIP_STACK
	od->start_eip2 = 0;
	od->start_eip3 = 0;
	od->start_eip4 = 0;
	od->start_eip5 = 0;
#endif

	mark_reset(od);
#endif
	od->off_time   = 0;
	od->off_eip0   = 0;
	od->count      = 0;
	od->total      = 0;
	od->max        = 0;
	od->min        = 0;
	od->min        = ~od->min;
}


#define SNSC_RT_TRACE_COMMON(PREFIX)                                           \
                                                                               \
int PREFIX##_enable = 0;                                                       \
                                                                               \
static struct _off_data PREFIX##_data[NR_CPUS]                                 \
	____cacheline_aligned_in_smp = { [0 ... NR_CPUS-1] = { } };            \
                                                                               \
static DEFINE_SEMAPHORE(PREFIX##_mutex);                                       \
                                                                               \
static struct _off_entry PREFIX##_entry;                                       \
                                                                               \
static void *PREFIX##_start(struct seq_file *m, loff_t *pos)                   \
{                                                                              \
	int bin_index;                                                         \
	int cpu;                                                               \
	struct _off_entry *entry = &PREFIX##_entry;                            \
                                                                               \
	if (*pos > ((BIN_NUM * NR_CPUS) - 1))                                  \
		return NULL;                                                   \
                                                                               \
	down(&PREFIX##_mutex);                                                 \
                                                                               \
	cpu       = *pos / BIN_NUM;                                            \
	bin_index = *pos - (cpu * BIN_NUM);                                    \
                                                                               \
	entry->od        = PREFIX##_data + cpu;                                \
	entry->cpu       = cpu;                                                \
	entry->bin_index = bin_index;                                          \
                                                                               \
	return entry;                                                          \
}                                                                              \
                                                                               \
static void PREFIX##_stop(struct seq_file *m, void *v)                         \
{                                                                              \
	up(&PREFIX##_mutex);                                                   \
}                                                                              \
                                                                               \
static struct seq_operations PREFIX##_op = {                                   \
	.start = PREFIX##_start,                                               \
	.stop  = PREFIX##_stop,                                                \
	.next  = _off_next,                                                    \
	.show  = _off_show                                                     \
};                                                                             \
                                                                               \
static void PREFIX##_data_reset(void)                                          \
{                                                                              \
	int cpu;                                                               \
	int save_##PREFIX##_enable;                                            \
	struct _off_data *od;                                                  \
                                                                               \
	save_##PREFIX##_enable = PREFIX##_enable;                              \
	PREFIX##_enable = 0;                                                   \
                                                                               \
	for (cpu = 0; cpu < NR_CPUS; cpu++ ) {                                 \
		od = PREFIX##_data + cpu;                                      \
		_off_data_reset(od);                                           \
	}                                                                      \
                                                                               \
	PREFIX##_enable = save_##PREFIX##_enable;                              \
}                                                                              \
                                                                               \
static int PREFIX##_open(struct inode *inode, struct file *file)               \
{                                                                              \
	return seq_open(file, &PREFIX##_op);                                   \
}                                                                              \
                                                                               \
static ssize_t PREFIX##_write(struct file *file, const char __user *buf,       \
				size_t count, loff_t *ppos)                    \
{                                                                              \
	char c;                                                                \
                                                                               \
	if (count) {                                                           \
		if (get_user(c, buf))                                          \
			return -EFAULT;                                        \
		if (c == '0') {                                                \
			PREFIX##_data_reset();                                 \
		}                                                              \
	}                                                                      \
	return count;                                                          \
}                                                                              \
                                                                               \
static struct file_operations proc_##PREFIX##_operations = {                   \
	.open    = PREFIX##_open,                                              \
	.read    = seq_read,                                                   \
	.llseek  = seq_lseek,                                                  \
	.release = seq_release,                                                \
	.write   = PREFIX##_write,                                             \
};


#if defined(CONFIG_SNSC_LITE_IRQSOFF)
SNSC_RT_TRACE_COMMON(irqs_off)
#endif

#if defined(CONFIG_SNSC_LITE_IRQSOFF) && defined(CONFIG_SNSC_LITE_PREEMPTOFF)
SNSC_RT_TRACE_COMMON(irqs_preempt_off)
#endif

#if defined(CONFIG_SNSC_LITE_PREEMPTOFF)
SNSC_RT_TRACE_COMMON(preempt_off)
#endif


#ifdef CONFIG_SNSC_DEBUG_IRQ_DURATION

SNSC_RT_TRACE_COMMON(irqs_time)


/* Uses irqs_time_enable, must follow SNSC_RT_TRACE_COMMON(irqs_time) */

static void irq_stat_reset(void)
{
	int cpu;
	int irq;
	int save_irqs_time_enable;
	struct irq_stat *irq_stat;

	save_irqs_time_enable = irqs_time_enable;
	irqs_time_enable = 0;

	for (cpu = 0; cpu < NR_CPUS; cpu++ ) {
		for (irq = 0; irq < NR_IRQS_EXT; irq++) {
			irq_stat = &per_cpu(lite_irq_stat, cpu)[irq];
			irq_stat->count = 0;
			irq_stat->total = 0;
			irq_stat->min   = ~(typeof(irq_stat->min))0;
			irq_stat->max   = 0;
		}
	}

	irqs_time_enable = save_irqs_time_enable;
}


ssize_t irq_stat_write(struct file *file, const char __user *buf,
			size_t count, loff_t *ppos)
{
	char c;

	if (count) {
		if (get_user(c, buf))
			return -EFAULT;
		if (c == '0') {
			irq_stat_reset();
		}
	}
	return count;
}

void show_rt_trace_irq_stat(struct seq_file *p, int irq)
{
	int cpu;
	struct irq_stat *irq_stat;

	for_each_present_cpu(cpu) {
		irq_stat = &per_cpu(lite_irq_stat, cpu)[irq];

		seq_printf(p, " %10lu %4u %4llu %4u",
			irq_stat->count,
			(irq_stat->min == ~(typeof(irq_stat->min))0) ?
				0: irq_stat->min,
			irq_stat->total /
				((irq_stat->count) ? irq_stat->count : 1),
			irq_stat->max);
	}
}


#endif /* CONFIG_SNSC_DEBUG_IRQ_DURATION */

#if defined(CONFIG_SNSC_LITE_IRQSOFF_DEBUG_MARK_WITH_PMU) \
 || defined(CONFIG_SNSC_5_RT_TRACE_WITH_PMU)

static void stop_pmu(void *ignored);
static void start_pmu(void* arg);

#if __LINUX_ARM_ARCH__ == 6

static uint32_t read_ctl(void);

/*
 * Per-CPU PMCR
 */
#define PMCR_E		(1 << 0)	/* Enable */
#define PMCR_P		(1 << 1)	/* Count reset */
#define PMCR_C		(1 << 2)	/* Cycle counter reset */
#define PMCR_D		(1 << 3)	/* Cycle counter counts every 64th cpu cycle */
#define PMCR_IEN_PMN0	(1 << 4)	/* Interrupt enable count reg 0 */
#define PMCR_IEN_PMN1	(1 << 5)	/* Interrupt enable count reg 1 */
#define PMCR_IEN_CCNT	(1 << 6)	/* Interrupt enable cycle counter */
#define PMCR_OFL_PMN0	(1 << 8)	/* Count reg 0 overflow */
#define PMCR_OFL_PMN1	(1 << 9)	/* Count reg 1 overflow */
#define PMCR_OFL_CCNT	(1 << 10)	/* Cycle counter overflow */

#define PMCR_EVT1_SHIFT (12)
#define PMCR_EVT0_SHIFT (20)

#define PMCR_EVT_ICACHE_MISS (0x00)
#define PMCR_EVT_INST (0x08)

#define read_pmcr(value)\
{ \
	__asm__ __volatile__(\
		"mrc p15,0, %0, c15, c12, 0 \n\t"\
		: "=&r" (value): : "memory"\
		);\
}

#define write_pmcr(value)\
{ \
	__asm__ __volatile__(\
		"mcr p15,0, %0, c15, c12, 0 \n\t"\
		: :"r" (value)\
		);\
}

#define read_ccr(ret)\
{ \
	__asm__ __volatile__(\
		"mrc p15,0, %0, c15, c12, 1 \n\t"\
		: "=&r" (ret): : "memory"\
		);\
}

#define read_cr0(ret)\
{ \
	__asm__ __volatile__(\
		"mrc p15,0, %0, c15, c12, 2 \n\t"\
		: "=&r" (ret): : "memory"\
		);\
}

#define read_cr1(ret)\
{ \
	__asm__ __volatile__(\
		"mrc p15,0, %0, c15, c12, 3 \n\t"\
		: "=&r" (ret): : "memory"\
		);\
}

#elif __LINUX_ARM_ARCH__ == 7

#define PMU_ARMV7_CNT0		0x0
#define PMU_ARMV7_CNT1		0x1

#define PMCR_E			(1 << 0)
#define PMCR_C			(1 << 2)

#define PMCNTENSET_C		(1 << 31)
#define PMCNTENSET_CNT0		(1 << 0)
#define PMCNTENSET_CNT1		(1 << 1)

#define PMXEVTYPER_EVT		(0xff)


#define read_pmcr(value)\
{ \
	__asm__ __volatile__(\
		"mrc p15,0, %0, c9, c12, 0 \n\t"\
		: "=&r" (value): : "memory"\
		);\
}

#define write_pmcr(value)\
{ \
	__asm__ __volatile__(\
		"mcr p15,0, %0, c9, c12, 0 \n\t"\
		: :"r" (value)\
		);\
}

#define write_pm_sle_r(value)\
{ \
	__asm__ __volatile__(\
		"mcr p15,0, %0, c9, c12, 5 \n\t"\
		: :"r" (value)\
		);\
}

#define write_pmx_ev_type_r(value)\
{ \
	__asm__ __volatile__(\
		"mcr p15,0, %0, c9, c13, 1 \n\t"\
		: :"r" (value)\
		);\
}

#define read_pmx_ev_type_r(value)\
{ \
	__asm__ __volatile__(\
		"mrc p15,0, %0, c9, c13, 1 \n\t"\
		: "=&r" (value): : "memory"\
		);\
}

#define write_pmx_ev_cnt_r(value)\
{ \
	__asm__ __volatile__(\
		"mcr p15,0, %0, c9, c13, 2 \n\t"\
		: :"r" (value)\
		);\
}

#define read_pmx_ev_cnt_r(value)\
{ \
	__asm__ __volatile__(\
		"mrc p15,0, %0, c9, c13, 2 \n\t"\
		: "=&r" (value): : "memory"\
		);\
}

#define read_pm_ccnt_r(value)\
{ \
	__asm__ __volatile__(\
		"mrc p15,0, %0, c9, c13, 0 \n\t"\
		: "=&r" (value): : "memory"\
		);\
}

#define write_pm_cnt_en_set(value)\
{ \
	__asm__ __volatile__(\
		"mcr p15,0, %0, c9, c12, 1 \n\t"\
		: :"r" (value)\
		);\
}

#define write_pm_cnt_en_clr(value)\
{ \
	__asm__ __volatile__(\
		"mcr p15,0, %0, c9, c12, 2 \n\t"\
		: :"r" (value)\
		);\
}

#endif

static void stop_pmu(void *ignored)
{
	uint32_t value = 0;

	read_pmcr(value);
	value &= ~PMCR_E;
	write_pmcr(value);
}

static void start_pmu(void* arg)
{
	uint32_t value = 0;
	unsigned long evt0,evt1;
	struct pmu_info* pi = (struct pmu_info *)arg;

	evt0 = pi->evt0;
	evt1 = pi->evt1;
#if __LINUX_ARM_ARCH__ == 6
	value |= (evt0 << PMCR_EVT0_SHIFT);
	value |= (evt1 << PMCR_EVT1_SHIFT);
	value |= (PMCR_P|PMCR_C|PMCR_OFL_CCNT|PMCR_E);
	write_pmcr(value);
#elif __LINUX_ARM_ARCH__ == 7

	/*
	 * Initialize PMU counter 0
	 */
	value = PMU_ARMV7_CNT0;
	write_pm_sle_r(value);
	value = evt0;
	write_pmx_ev_type_r(value);
	value = 0;
	write_pmx_ev_cnt_r(value);

	/*
	 * Initialize PMU counter 1
	 */
	value = PMU_ARMV7_CNT1;
	write_pm_sle_r(value);
	value = evt1;
	write_pmx_ev_type_r(value);
	value = 0;
	write_pmx_ev_cnt_r(value);

	/*
	 * Initialize PMU cycle counter
	 */
	value = 0;
	value |= PMCR_C;
	write_pmcr(value);


	/*
	 * We are good to go, Start the counters
	 */
	value =0;
	value |= (PMCNTENSET_C|PMCNTENSET_CNT0|PMCNTENSET_CNT1);
	write_pm_cnt_en_set(value)

#endif
}

static void restart_pmu(void)
{
	uint32_t value = 0;

	/*
	 * TODO: Optimize for ARMV7, We do NOT want to enable all the counters.
	 */
	read_pmcr(value);
	value |= (PMCR_E);
	write_pmcr(value);
}

static uint32_t read_ccnt(void)
{
	uint32_t ret = 0;
#if __LINUX_ARM_ARCH__ == 6
	read_ccr(ret);
#elif __LINUX_ARM_ARCH__ == 7
	read_pm_ccnt_r(ret);
#endif
	return ret;
}

static uint32_t read_evt0(void)
{
	uint32_t ret = 0;
#if __LINUX_ARM_ARCH__ == 6
	read_cr0(ret);
#elif __LINUX_ARM_ARCH__ == 7
	ret = PMU_ARMV7_CNT0;
	write_pm_sle_r(ret);
	read_pmx_ev_cnt_r(ret);
#endif
	return ret;

}

static uint32_t read_evt1(void)
{
	uint32_t ret = 0;
#if __LINUX_ARM_ARCH__ == 6
	read_cr1(ret);
#elif __LINUX_ARM_ARCH__ == 7
	ret = PMU_ARMV7_CNT1;
	write_pm_sle_r(ret);
	read_pmx_ev_cnt_r(ret);
#endif
	return ret;

}

#if __LINUX_ARM_ARCH__ == 6

static uint32_t read_ctl(void)
{
	uint32_t value = 0;

	read_pmcr(value);
	return value;
}

#endif

static void *mark_with_pmu_seq_start(struct seq_file *s, loff_t *pos)
{
	if (*pos >= 1) {
		return NULL;
	}
	return &s_mark_with_pmu;
}

static void *mark_with_pmu_seq_next(struct seq_file *s, void *v, loff_t *pos)
{
	(*pos)++;
	return NULL;
}

static void mark_with_pmu_seq_stop(struct seq_file *s, void *v)
{
	; /* nothing to do */
}

static int mark_with_pmu_seq_show(struct seq_file *s, void *v)
{
	uint32_t ctlr = 0;
	uint32_t evt0, evt1;

#if __LINUX_ARM_ARCH__ == 6
	ctlr = read_ctl();
	evt0 = (ctlr >> PMCR_EVT0_SHIFT) & 0xFF;
	evt1 = (ctlr >> PMCR_EVT1_SHIFT) & 0xFF;

#elif __LINUX_ARM_ARCH__ == 7
	ctlr = PMU_ARMV7_CNT0;
	write_pm_sle_r(ctlr);
	read_pmx_ev_type_r(ctlr);
	evt0 = (ctlr & PMXEVTYPER_EVT);

	ctlr = PMU_ARMV7_CNT1;
	write_pm_sle_r(ctlr);
	read_pmx_ev_type_r(ctlr);
	evt1 = (ctlr & PMXEVTYPER_EVT);

#endif
	seq_printf(s, "ctrl:\n\tEVT0 %08X\n\tEVT1 %08X\n",
		   evt0, evt1);
	return 0;
}
static struct seq_operations mark_with_pmu_op = {
	.start = mark_with_pmu_seq_start,
	.next  = mark_with_pmu_seq_next,
	.stop  = mark_with_pmu_seq_stop,
	.show  = mark_with_pmu_seq_show
};


static int mark_with_pmu_open(struct inode *inode, struct file *file)
{
	return seq_open(file, &mark_with_pmu_op);
}

#define MARK_WITH_PMU_START_KEY		"start"
#define MARK_WITH_PMU_STOP_KEY		"stop"
#define MARK_WITH_PMU_SCALE_CCNT_KEY	"scale_ccnt"
#define MARK_WITH_PMU_SCALE_0_KEY	"scale_0"
#define MARK_WITH_PMU_SCALE_1_KEY	"scale_1"
#define MARK_WITH_PMU_OFFSET_CCNT_KEY	"offset_ccnt"
#define MARK_WITH_PMU_OFFSET_0_KEY	"offset_0"
#define MARK_WITH_PMU_OFFSET_1_KEY	"offset_1"
#define MARK_WITH_PMU_0_DIV_1_KEY	"evt0_div_evt1"
#define MARK_WITH_PMU_0_PCNT_CCNT_KEY	"evt0_percent_ccnt"
#define MARK_WITH_PMU_TRACE		"pmu_trace"

/*
 * See comments inside mark_with_pmu_write() that make assumptions based on
 * the value of MARK_WITH_PMU_MAX_INPUT.
 */
#define MARK_WITH_PMU_MAX_INPUT	256

static ssize_t mark_with_pmu_write(struct file *file, const char __user *buf,
				   size_t count, loff_t *ppos)
{
	unsigned long count0 = count;
	int len1;
	char comment[MARK_WITH_PMU_MAX_INPUT+1];
	char *next_field;
	unsigned long value, evt0, evt1;
	struct pmu_info *pi = &s_mark_with_pmu;
	unsigned long tmp;

	/*
	 * ASSUMPTION for the calls to simple_strtoul(,,0) from this
	 * function:
	 *   simple_strtoul() will call simple_guess_base() (since I am
	 *   passing in base == 0) which may access next_field[2].
	 *   MARK_WITH_PMU_MAX_INPUT is large enough that this will still
	 *   be within comment[]
	 */

	if (count0 > MARK_WITH_PMU_MAX_INPUT) {
		len1 = MARK_WITH_PMU_MAX_INPUT;
	} else {
		len1 = count0;
	}
	count0 -= len1;
	if (copy_from_user(comment, buf, len1)) {
		return -EFAULT;
	}
	comment[len1] = '\0';

	if (strncmp(comment, MARK_WITH_PMU_START_KEY,
		    strlen(MARK_WITH_PMU_START_KEY)) == 0) {

		printk(KERN_INFO "start pmu with event(%08X, %08X)\n",
		       pi->evt0, pi->evt1);
		on_each_cpu(start_pmu, pi, 1);
		mark_with_pmu_enable = 1;

	} else if (strncmp(comment, MARK_WITH_PMU_STOP_KEY,
			 strlen(MARK_WITH_PMU_STOP_KEY)) == 0) {

		printk(KERN_INFO "pmu stopped\n");
		on_each_cpu(stop_pmu, NULL, 1);
		mark_with_pmu_enable = 0;

	} else if (strncmp(comment, MARK_WITH_PMU_SCALE_CCNT_KEY,
			 strlen(MARK_WITH_PMU_SCALE_CCNT_KEY)) == 0) {

		next_field = &comment[strlen(MARK_WITH_PMU_SCALE_CCNT_KEY)];
		next_field = strstrip(next_field);

		tmp = simple_strtoul(next_field, NULL, 0);
		if (!tmp) {
			return -EFAULT;
		}
		scale_bin_evt_ccnt = tmp;

	} else if (strncmp(comment, MARK_WITH_PMU_SCALE_0_KEY,
			 strlen(MARK_WITH_PMU_SCALE_0_KEY)) == 0) {

		next_field = &comment[strlen(MARK_WITH_PMU_SCALE_0_KEY)];
		next_field = strstrip(next_field);

		tmp = simple_strtoul(next_field, NULL, 0);
		if (!tmp) {
			return -EFAULT;
		}
		scale_bin_evt_0 = tmp;

	} else if (strncmp(comment, MARK_WITH_PMU_SCALE_1_KEY,
			 strlen(MARK_WITH_PMU_SCALE_1_KEY)) == 0) {

		next_field = &comment[strlen(MARK_WITH_PMU_SCALE_1_KEY)];
		next_field = strstrip(next_field);

		tmp = simple_strtoul(next_field, NULL, 0);
		if (!tmp) {
			return -EFAULT;
		}
		scale_bin_evt_1 = tmp;

	} else if (strncmp(comment, MARK_WITH_PMU_OFFSET_CCNT_KEY,
			 strlen(MARK_WITH_PMU_OFFSET_CCNT_KEY)) == 0) {

		next_field = &comment[strlen(MARK_WITH_PMU_OFFSET_CCNT_KEY)];
		next_field = strstrip(next_field);

		offset_bin_evt_ccnt = simple_strtoul(next_field, NULL, 0);

	} else if (strncmp(comment, MARK_WITH_PMU_OFFSET_0_KEY,
			 strlen(MARK_WITH_PMU_OFFSET_0_KEY)) == 0) {

		next_field = &comment[strlen(MARK_WITH_PMU_OFFSET_0_KEY)];
		next_field = strstrip(next_field);

		offset_bin_evt_0 = simple_strtoul(next_field, NULL, 0);

	} else if (strncmp(comment, MARK_WITH_PMU_OFFSET_1_KEY,
			 strlen(MARK_WITH_PMU_OFFSET_1_KEY)) == 0) {

		next_field = &comment[strlen(MARK_WITH_PMU_OFFSET_1_KEY)];
		next_field = strstrip(next_field);

		offset_bin_evt_1 = simple_strtoul(next_field, NULL, 0);

	} else if (strncmp(comment, MARK_WITH_PMU_0_DIV_1_KEY,
			 strlen(MARK_WITH_PMU_0_DIV_1_KEY)) == 0) {

		next_field = &comment[strlen(MARK_WITH_PMU_0_DIV_1_KEY)];
		next_field = strstrip(next_field);

		pmu_hist_evt0_div_evt1 = simple_strtoul(next_field, NULL, 0);

	} else if (strncmp(comment, MARK_WITH_PMU_0_PCNT_CCNT_KEY,
			 strlen(MARK_WITH_PMU_0_PCNT_CCNT_KEY)) == 0) {

		next_field = &comment[strlen(MARK_WITH_PMU_0_PCNT_CCNT_KEY)];
		next_field = strstrip(next_field);

		pmu_hist_evt0_percent_ccnt = simple_strtoul(next_field, NULL, 0);

#ifdef CONFIG_SNSC_5_RT_TRACE_PMU_TRACE
	} else if (strncmp(comment, MARK_WITH_PMU_TRACE,
			 strlen(MARK_WITH_PMU_TRACE)) == 0) {

		next_field = &comment[strlen(MARK_WITH_PMU_TRACE)];
		next_field = strstrip(next_field);

		pmu_trace_enable = simple_strtoul(next_field, NULL, 0);
#endif

	} else {
		value = simple_strtoul(comment, NULL, 16);
		evt0 = (value >> 8) & 0xFF;
		evt1 = value & 0xFF;
		pi->evt0 = evt0;
		pi->evt1 = evt1;
	}

	return count;
}

static struct file_operations proc_mark_with_pmu_operations = {
	.open    = mark_with_pmu_open,
	.read    = seq_read,
	.llseek  = seq_lseek,
	.release = seq_release,
	.write   = mark_with_pmu_write,
};
#endif /* CONFIG_SNSC_LITE_IRQSOFF_DEBUG_MARK_WITH_PMU
       || CONFIG_SNSC_5_RT_TRACE_WITH_PMU */

#ifdef CONFIG_SNSC_LITE_IRQSOFF_DEBUG

/* _trace_off_mark() be inlined to be able to call CALLER_ADDR0 */
static inline void notrace _trace_off_mark(struct _off_data *od,
	unsigned long data1, unsigned long data2,
	unsigned long ccnt, unsigned long pmn0, unsigned long pmn1, int enable)
{
	if (!enable || !od->off_eip0)
		return;

	od->mark[od->next_mark].loc_0 = CALLER_ADDR0;
	od->mark[od->next_mark].loc_1 = CALLER_ADDR1;
	od->mark[od->next_mark].time  = now();
	od->mark[od->next_mark].data1 = data1;
	od->mark[od->next_mark].data2 = data2;
#ifdef CONFIG_SNSC_LITE_IRQSOFF_DEBUG_MARK_WITH_PMU
	od->mark[od->next_mark].ccnt = ccnt;
	od->mark[od->next_mark].pmn0 = pmn0;
	od->mark[od->next_mark].pmn1 = pmn1;
#endif

	od->next_mark++;
	if (od->next_mark > MARK_MAX)
		od->next_mark = 0;	/* next mark will overwrite oldest */
}


/*
 * There are no in-tree callers of trace_off_mark().
 * Calls to this function are temporarilly added by a developer who is
 * debugging the cause of a long irqs disabled and/or preempt off issue.
 */
void notrace trace_off_mark(unsigned long data1, unsigned long data2)
{
	int cpu = raw_smp_processor_id();
	unsigned long ccnt = 0;
	unsigned long pmn0 = 0;
	unsigned long pmn1 = 0;

#ifdef CONFIG_SNSC_LITE_IRQSOFF_DEBUG_MARK_WITH_PMU
	if (mark_with_pmu_enable) {
		stop_pmu(NULL);
		ccnt = read_ccnt();
		pmn0 = read_evt0();
		pmn1 = read_evt1();
	}
#endif

#if defined(CONFIG_SNSC_LITE_IRQSOFF)
	{
	struct _off_data *iod  = irqs_off_data         + cpu;
	_trace_off_mark(iod,  data1, data2, ccnt, pmn0, pmn1,
			irqs_off_enable);
	}
#endif

#if defined(CONFIG_SNSC_LITE_IRQSOFF) && defined(CONFIG_SNSC_LITE_PREEMPTOFF)
	{
	struct _off_data *ipod = irqs_preempt_off_data + cpu;
	_trace_off_mark(ipod, data1, data2, ccnt, pmn0, pmn1, irqs_preempt_off_enable);
	}
#endif

#if defined(CONFIG_SNSC_LITE_PREEMPTOFF)
	{
	struct _off_data *pod  = preempt_off_data      + cpu;
	_trace_off_mark(pod,  data1, data2, ccnt, pmn0, pmn1, preempt_off_enable);
	}
#endif

#ifdef CONFIG_SNSC_LITE_IRQSOFF_DEBUG_MARK_WITH_PMU
	if (mark_with_pmu_enable) {
		restart_pmu();
	}
#endif
}


static void notrace save_mark(struct _off_data *od)
{
	int k;
	int next_save = od->next_mark_save;

	od->mark_save_off_time  [next_save] = od->off_time;
	od->mark_save_start_eip0[next_save] = od->start_eip0;
	od->mark_save_start_eip1[next_save] = od->start_eip1;
#ifdef CONFIG_SNSC_LITE_IRQSOFF_DEBUG_MARK_DEEP_EIP_STACK
	od->mark_save_start_eip2[next_save] = od->start_eip2;
	od->mark_save_start_eip3[next_save] = od->start_eip3;
	od->mark_save_start_eip4[next_save] = od->start_eip4;
	od->mark_save_start_eip5[next_save] = od->start_eip5;
#endif
	od->mark_save_end_eip0  [next_save] = od->end_eip0;
	od->mark_save_end_eip1  [next_save] = od->end_eip1;
#ifdef CONFIG_SNSC_LITE_IRQSOFF_DEBUG_MARK_DEEP_EIP_STACK
	od->mark_save_end_eip2  [next_save] = od->end_eip2;
	od->mark_save_end_eip3  [next_save] = od->end_eip3;
	od->mark_save_end_eip4  [next_save] = od->end_eip4;
	od->mark_save_end_eip5  [next_save] = od->end_eip5;
#endif
	od->mark_save_max       [next_save] = od->max;
#ifdef CONFIG_SNSC_LITE_IRQSOFF_DEBUG_MARK_WITH_PMU
	od->mark_save_ccnt      [next_save] = od->ccnt;
	od->mark_save_pmn0      [next_save] = od->pmn0;
	od->mark_save_pmn1      [next_save] = od->pmn1;
#endif

	for (k = 0; k <= MARK_MAX; k++) {

		if (od->mark[k].loc_0) {

			od->mark_save[next_save][k].loc_0 = od->mark[k].loc_0;
			od->mark_save[next_save][k].loc_1 = od->mark[k].loc_1;
			od->mark_save[next_save][k].time  = od->mark[k].time;
			od->mark_save[next_save][k].data1 = od->mark[k].data1;
			od->mark_save[next_save][k].data2 = od->mark[k].data2;
#ifdef CONFIG_SNSC_LITE_IRQSOFF_DEBUG_MARK_WITH_PMU
			od->mark_save[next_save][k].ccnt = od->mark[k].ccnt;
			od->mark_save[next_save][k].pmn0 = od->mark[k].pmn0;
			od->mark_save[next_save][k].pmn1 = od->mark[k].pmn1;
#endif

		} else {
			od->mark_save[next_save][k].loc_0 = 0;
		}

		od->mark[k].loc_0 = 0;
	}

	od->next_mark_save++;
	if (od->next_mark_save > MARK_SAVE_MAX)
		od->next_mark_save = 0;	/* next save will overwrite oldest */
}

#endif /* CONFIG_SNSC_LITE_IRQSOFF_DEBUG */


/* __________________________________________________________________________ */

/*
 * Calling dump_stack() is a very large hammer that has a large negative
 * impact while interrupts are disabled or preemption is disabled.  It
 * should only rarely be enabled to investigate a specific problem.
 *
 * Only call dump_stack() on one processor, cpu irqs_dump_cpu.
 * Not restricting the processor will cause a large latency on other processor.
 *
 * in_dump_stack is used as a flag to prevent recursively calling dump_stack()
 * due to a large latency caused by calling dump_stack().  There is no need to
 * add the overhead of a mutex -- it is only accessed on a single processor
 * so the race window is fairly small.  If the race occurs the negative
 * impact is that: two simultaneous calls to dump_stack() result in
 * interleaved output on the console.
 */

#ifdef CONFIG_SNSC_LITE_IRQSOFF_DEBUG
static int in_dump_stack = 0;
#else
#define in_dump_stack 0
#endif

#ifdef CONFIG_SNSC_LITE_PREEMPTOFF

static DEFINE_PER_CPU(int, trace_cpu_idle);

/* Some archs do their cpu_idle with preemption off. Don't measure it */
static void notrace trace_preempt_enter_idle(void)
{
	__get_cpu_var(trace_cpu_idle) = 1;
}


static void notrace trace_preempt_exit_idle(void)
{
	__get_cpu_var(trace_cpu_idle) = 0;
}

#endif


/* __________________________________________________________________________ */

#if defined(CONFIG_SNSC_LITE_IRQSOFF) && defined(CONFIG_SNSC_LITE_PREEMPTOFF)
static inline void notrace trace_hardirqs_preempt_off(
#ifdef CONFIG_SNSC_LITE_IRQSOFF_DEBUG
	unsigned long caller0,
	unsigned long caller1
#ifdef CONFIG_SNSC_LITE_IRQSOFF_DEBUG_MARK_DEEP_EIP_STACK
	,
	unsigned long caller2,
	unsigned long caller3,
	unsigned long caller4,
	unsigned long caller5
#endif
#else
	void
#endif
	)
{
	int cpu = raw_smp_processor_id();
	struct _off_data *ipod = irqs_preempt_off_data + cpu;

	if (!irqs_preempt_off_enable || in_dump_stack) {
		ipod->off_eip0 = 0;
		return;
	}

	if (ipod->off_eip0)
		return;

#ifdef CONFIG_SNSC_LITE_IRQSOFF_DEBUG
	ipod->off_eip0 = caller0;
	ipod->off_eip1 = caller1;
#ifdef CONFIG_SNSC_LITE_IRQSOFF_DEBUG_MARK_DEEP_EIP_STACK
	ipod->off_eip2 = caller2;
	ipod->off_eip3 = caller3;
	ipod->off_eip4 = caller4;
	ipod->off_eip4 = caller5;
#endif
	ipod->off_time = now();

	ipod->next_mark = 0;
	{
	int k;
	for (k = 0; k <= MARK_MAX; k++)
		ipod->mark[k].loc_0 = 0;
	}
#else
	ipod->off_eip0 = 1;
	ipod->off_time = now();
#endif
}

static inline void notrace trace_hardirqs_preempt_on(
#ifdef CONFIG_SNSC_LITE_IRQSOFF_DEBUG
	unsigned long caller_eip0,
	unsigned long caller_eip1,
#ifdef CONFIG_SNSC_LITE_IRQSOFF_DEBUG_MARK_DEEP_EIP_STACK
	unsigned long caller_eip2,
	unsigned long caller_eip3,
	unsigned long caller_eip4,
	unsigned long caller_eip5,
#endif
#endif
	int other_off)
{
	int cpu = raw_smp_processor_id();
	unsigned long delta;
	struct _off_data *ipod = irqs_preempt_off_data + cpu;

	if (!irqs_preempt_off_enable || in_dump_stack) {
		ipod->off_eip0 = 0;
		return;
	}

	if (!ipod->off_eip0 || other_off)
		return;

	ipod->count++;

	delta = cycles_to_us(cycle_delta(now(), ipod->off_time));

	ipod->total += delta;

	if (delta > ipod->max) {
		ipod->max        = delta;
#ifdef CONFIG_SNSC_LITE_IRQSOFF_DEBUG
		ipod->start_eip0 = ipod->off_eip0;
		ipod->start_eip1 = ipod->off_eip1;
#ifdef CONFIG_SNSC_LITE_IRQSOFF_DEBUG_MARK_DEEP_EIP_STACK
		ipod->start_eip2 = ipod->off_eip2;
		ipod->start_eip3 = ipod->off_eip3;
		ipod->start_eip4 = ipod->off_eip4;
		ipod->start_eip4 = ipod->off_eip5;
#endif
		ipod->end_eip0   = caller_eip0;
		ipod->end_eip1   = caller_eip1;
#ifdef CONFIG_SNSC_LITE_IRQSOFF_DEBUG_MARK_DEEP_EIP_STACK
		ipod->end_eip2   = caller_eip2;
		ipod->end_eip3   = caller_eip3;
		ipod->end_eip4   = caller_eip4;
		ipod->end_eip4   = caller_eip5;
#endif

		save_mark(ipod);

		if (irqs_preempt_off_dump_threshold &&
		    (raw_smp_processor_id() == irqs_dump_cpu) &&
		    (delta > irqs_preempt_off_dump_threshold)) {
			in_dump_stack = 1;
			printk("\n\n"
				"=====  irqs/preempt on, cpu: %d  delta: %ld"
				"  =====\n\n",
				irqs_dump_cpu, delta);
			dump_stack();
			printk("\n");
			in_dump_stack = 0;
		}
#endif
	}

	if (delta < ipod->min)
		ipod->min = delta;

#ifdef CONFIG_SNSC_LITE_IRQSOFF_DEBUG_HISTOGRAM
	if (delta > BIN_MAX)
		delta = BIN_MAX;
	ipod->bin[delta]++;
#endif

	ipod->off_eip0 = 0;
}

#else

#ifdef CONFIG_SNSC_LITE_IRQSOFF_DEBUG

#ifdef CONFIG_SNSC_LITE_IRQSOFF_DEBUG_MARK_DEEP_EIP_STACK

# define trace_hardirqs_preempt_off(a0, a1, a2, a3, a4, a5)     do { } while (0)
# define trace_hardirqs_preempt_on(a0, a1, a2, a3, a4, a5, off) do { } while (0)

#else

# define trace_hardirqs_preempt_off(a0, a1)     do { } while (0)
# define trace_hardirqs_preempt_on(a0, a1, off) do { } while (0)

#endif

#else

# define trace_hardirqs_preempt_off()         do { } while (0)
# define trace_hardirqs_preempt_on(off)       do { } while (0)

#endif

#endif

/* __________________________________________________________________________ */

#ifdef CONFIG_SNSC_LITE_IRQSOFF

static inline void notrace _trace_hardirqs_off(
#ifdef CONFIG_SNSC_LITE_IRQSOFF_DEBUG
	unsigned long caller0,
	unsigned long caller1
#ifdef CONFIG_SNSC_LITE_IRQSOFF_DEBUG_MARK_DEEP_EIP_STACK
	,
	unsigned long caller2,
	unsigned long caller3,
	unsigned long caller4,
	unsigned long caller5
#endif
#else
	void
#endif
	)
{
	int cpu = raw_smp_processor_id();
	struct _off_data *iod = irqs_off_data + cpu;

#ifdef CONFIG_SNSC_PREEMPT_IRQS
	if(in_preempt_irqs())
		return;
#endif

	if (in_dump_stack) {
		iod->off_eip0 = 0;
		return;
	}

	/*
	 * test !irqs_disabled() because touch_critical_timing() is
	 * #defined as "trace_hardirqs_off(); add_preempt_count(0);"
	 */
	if (iod->off_eip0 || !irqs_disabled())
		return;

	if (!irqs_off_enable) {
		iod->off_eip0 = 0;
	} else {
#ifdef CONFIG_SNSC_LITE_IRQSOFF_DEBUG
		iod->off_eip0 = caller0;
		iod->off_eip1 = caller1;
#ifdef CONFIG_SNSC_LITE_IRQSOFF_DEBUG_MARK_DEEP_EIP_STACK
		iod->off_eip2 = caller2;
		iod->off_eip3 = caller3;
		iod->off_eip4 = caller4;
		iod->off_eip5 = caller5;
#endif
#ifdef CONFIG_SNSC_LITE_IRQSOFF_DEBUG_MARK_WITH_PMU
		if (mark_with_pmu_enable) {
			stop_pmu(NULL);
			iod->ccnt = read_ccnt();
			iod->pmn0 = read_evt0();
			iod->pmn1 = read_evt1();
		}
#endif
#else
		iod->off_eip0 = 1;
#endif
		iod->off_time = now();
	}

#ifdef CONFIG_SNSC_LITE_IRQSOFF_DEBUG
	iod->next_mark    = 0;
	{
	int k;
	for (k = 0; k <= MARK_MAX; k++)
		iod->mark[k].loc_0 = 0;
	}

	trace_hardirqs_preempt_off(caller0, caller1
#ifdef CONFIG_SNSC_LITE_IRQSOFF_DEBUG_MARK_DEEP_EIP_STACK
#ifdef CONFIG_SNSC_LITE_IRQSOFF_DEBUG_EIP_STACK_DEPTH_5
		, caller2, caller3, caller4, caller5
#elif defined CONFIG_SNSC_LITE_IRQSOFF_DEBUG_EIP_STACK_DEPTH_4
		, caller2, caller3, caller4, 0UL
#elif defined CONFIG_SNSC_LITE_IRQSOFF_DEBUG_EIP_STACK_DEPTH_3
		, caller2, caller3, 0UL, 0UL
#elif defined CONFIG_SNSC_LITE_IRQSOFF_DEBUG_EIP_STACK_DEPTH_2
		, caller2, 0UL, 0UL, 0UL
#endif
#endif
	);
#ifdef CONFIG_SNSC_LITE_IRQSOFF_DEBUG_MARK_WITH_PMU
	if (mark_with_pmu_enable) {
		restart_pmu();
	}
#endif
#else
	trace_hardirqs_preempt_off();
#endif
}


#ifdef CONFIG_SNSC_LITE_IRQSOFF_DEBUG

void notrace trace_hardirqs_off(void)
{
	_trace_hardirqs_off(CALLER_ADDR0, CALLER_ADDR1
#ifdef CONFIG_SNSC_LITE_IRQSOFF_DEBUG_MARK_DEEP_EIP_STACK
#ifdef CONFIG_SNSC_LITE_IRQSOFF_DEBUG_EIP_STACK_DEPTH_5
		, CALLER_ADDR2, CALLER_ADDR3, CALLER_ADDR4, CALLER_ADDR5
#elif defined CONFIG_SNSC_LITE_IRQSOFF_DEBUG_EIP_STACK_DEPTH_4
		, CALLER_ADDR2, CALLER_ADDR3, CALLER_ADDR4, 0UL
#elif defined CONFIG_SNSC_LITE_IRQSOFF_DEBUG_EIP_STACK_DEPTH_3
		, CALLER_ADDR2, CALLER_ADDR3, 0UL, 0UL
#elif defined CONFIG_SNSC_LITE_IRQSOFF_DEBUG_EIP_STACK_DEPTH_2
		, CALLER_ADDR2, 0UL, 0UL, 0UL
#endif
#endif
	);
}
EXPORT_SYMBOL(trace_hardirqs_off);


void notrace trace_hardirqs_off_caller(unsigned long caller)
{
	_trace_hardirqs_off(caller, 0
#ifdef CONFIG_SNSC_LITE_IRQSOFF_DEBUG_MARK_DEEP_EIP_STACK
			    , 0, 0, 0, 0
#endif
	);
}

#else

void notrace trace_hardirqs_off(void)
{
	_trace_hardirqs_off();
}
EXPORT_SYMBOL(trace_hardirqs_off);


void notrace trace_hardirqs_off_caller(unsigned long caller)
{
	_trace_hardirqs_off();
}

#endif


void notrace trace_hardirqs_on(void)
{
	unsigned long delta;
	int cpu = raw_smp_processor_id();
	struct _off_data *iod = irqs_off_data + cpu;

#ifdef CONFIG_SNSC_PREEMPT_IRQS
	if(in_preempt_irqs())
		return;
#endif

	if (in_dump_stack) {
		iod->off_eip0 = 0;
		return;
	}

	if (irqs_off_enable && iod->off_eip0) {
		iod->count++;

		delta = cycles_to_us(cycle_delta(now(), iod->off_time));

		iod->total += delta;

		if (delta > iod->max) {
			iod->max        = delta;
#ifdef CONFIG_SNSC_LITE_IRQSOFF_DEBUG
			iod->start_eip0 = iod->off_eip0;
			iod->start_eip1 = iod->off_eip1;
#ifdef CONFIG_SNSC_LITE_IRQSOFF_DEBUG_MARK_DEEP_EIP_STACK
			iod->start_eip2 = iod->off_eip2;
			iod->start_eip3 = iod->off_eip3;
			iod->start_eip4 = iod->off_eip4;
			iod->start_eip5 = iod->off_eip5;
#endif
			iod->end_eip0   = CALLER_ADDR0;
			iod->end_eip1   = CALLER_ADDR1;
#ifdef CONFIG_SNSC_LITE_IRQSOFF_DEBUG_MARK_DEEP_EIP_STACK
			iod->end_eip2   = CALLER_ADDR2;
			iod->end_eip3   = CALLER_ADDR3;
			iod->end_eip4   = CALLER_ADDR4;
			iod->end_eip5   = CALLER_ADDR5;
#endif

			save_mark(iod);

			if (irqs_off_dump_threshold &&
			    (raw_smp_processor_id() == irqs_dump_cpu) &&
			    (delta > irqs_off_dump_threshold)) {
				in_dump_stack = 1;
				printk("\n\n"
					"=====  irqs on, cpu: %d  delta: %ld"
					"  =====\n\n",
					irqs_dump_cpu, delta);
				dump_stack();
				printk("\n");
				in_dump_stack = 0;
			}
#endif
		}

		if (delta < iod->min)
			iod->min       = delta;

#ifdef CONFIG_SNSC_LITE_IRQSOFF_DEBUG_HISTOGRAM
		if (delta > BIN_MAX)
			delta = BIN_MAX;
		iod->bin[delta]++;
#endif
	}
	iod->off_eip0 = 0;

	trace_hardirqs_preempt_on(
#ifdef CONFIG_SNSC_LITE_IRQSOFF_DEBUG
		CALLER_ADDR0, CALLER_ADDR1,
#ifdef CONFIG_SNSC_LITE_IRQSOFF_DEBUG_MARK_DEEP_EIP_STACK
#ifdef CONFIG_SNSC_LITE_IRQSOFF_DEBUG_EIP_STACK_DEPTH_5
		CALLER_ADDR2, CALLER_ADDR3, CALLER_ADDR4, CALLER_ADDR5,
#elif defined CONFIG_SNSC_LITE_IRQSOFF_DEBUG_EIP_STACK_DEPTH_4
		CALLER_ADDR2, CALLER_ADDR3, CALLER_ADDR4, 0UL,
#elif defined CONFIG_SNSC_LITE_IRQSOFF_DEBUG_EIP_STACK_DEPTH_3
		CALLER_ADDR2, CALLER_ADDR3, 0UL, 0UL,
#elif defined CONFIG_SNSC_LITE_IRQSOFF_DEBUG_EIP_STACK_DEPTH_2
		CALLER_ADDR2, 0UL, 0UL, 0UL,
#endif
#endif
#endif
		((preempt_count() == 1) && !__get_cpu_var(trace_cpu_idle)) ||
		(preempt_count() > 1));
}
EXPORT_SYMBOL(trace_hardirqs_on);

#endif /* CONFIG_SNSC_LITE_IRQSOFF */

/* __________________________________________________________________________ */

#ifdef CONFIG_SNSC_LITE_PREEMPTOFF

#if 0
/*
 * No in tree callers of these functions, but it is implemented
 * in 2.6.23.11-rt14/latency-tracing.patch.
 * Catch any use of these functions at compile time by not implementing
 * them in this file and declaring as extern in include/linux/preempt.h
 */
void notrace mask_preempt_count(unsigned int mask)
{
}

void notrace unmask_preempt_count(unsigned int mask)
{
}
#endif


void notrace add_preempt_count(unsigned int val)
{
	int cpu = raw_smp_processor_id();
	struct _off_data *pod = preempt_off_data + cpu;

	preempt_count() += val;

#ifdef CONFIG_SNSC_PREEMPT_IRQS
	if(in_preempt_irqs())
		return;
#endif

	if (in_dump_stack) {
		pod->off_eip0 = 0;
		return;
	}

	if ((preempt_count() == 1) && __get_cpu_var(trace_cpu_idle)) {
		/* called by touch_critical_timing() in cpu_idle() */

	} else if (preempt_count() != val) {
		return;

	} else if (preempt_count() == 0) {
		/* touch_critical_timing() */
		return;
	}

	if (!preempt_off_enable || in_dump_stack) {
		pod->off_eip0 = 0;
	} else {
#ifdef CONFIG_SNSC_LITE_IRQSOFF_DEBUG
		pod->off_eip0 = CALLER_ADDR0;
		pod->off_eip1 = CALLER_ADDR1;
#ifdef CONFIG_SNSC_LITE_IRQSOFF_DEBUG_MARK_DEEP_EIP_STACK
		pod->off_eip2 = CALLER_ADDR2;
		pod->off_eip3 = CALLER_ADDR3;
		pod->off_eip4 = CALLER_ADDR4;
		pod->off_eip5 = CALLER_ADDR5;
#endif
#else
		pod->off_eip0 = 1;
#endif
		pod->off_time = now();
	}

#ifdef CONFIG_SNSC_LITE_IRQSOFF_DEBUG
	pod->next_mark    = 0;
	{
	int k;
	for (k = 0; k <= MARK_MAX; k++)
		pod->mark[k].loc_0 = 0;
	}

	trace_hardirqs_preempt_off(CALLER_ADDR0, CALLER_ADDR1
#ifdef CONFIG_SNSC_LITE_IRQSOFF_DEBUG_MARK_DEEP_EIP_STACK
#ifdef CONFIG_SNSC_LITE_IRQSOFF_DEBUG_EIP_STACK_DEPTH_5
		, CALLER_ADDR2, CALLER_ADDR3, CALLER_ADDR4, CALLER_ADDR5
#elif defined CONFIG_SNSC_LITE_IRQSOFF_DEBUG_EIP_STACK_DEPTH_4
		, CALLER_ADDR2, CALLER_ADDR3, CALLER_ADDR4, 0UL
#elif defined CONFIG_SNSC_LITE_IRQSOFF_DEBUG_EIP_STACK_DEPTH_3
		, CALLER_ADDR2, CALLER_ADDR3, 0UL, 0UL
#elif defined CONFIG_SNSC_LITE_IRQSOFF_DEBUG_EIP_STACK_DEPTH_2
		, CALLER_ADDR2, 0UL, 0UL, 0UL
#endif
#endif
	);
#else
	trace_hardirqs_preempt_off();
#endif
}
EXPORT_SYMBOL(add_preempt_count);


void notrace sub_preempt_count(unsigned int val)
{
	unsigned long delta;
	int cpu = raw_smp_processor_id();
	struct _off_data *pod = preempt_off_data + cpu;

#ifdef CONFIG_SNSC_PREEMPT_IRQS
	if(in_preempt_irqs()){
		preempt_count() -= val;
		return;
	}
#endif

	if ((preempt_count() == val) ||
	    ((preempt_count() == 1) && __get_cpu_var(trace_cpu_idle))
	   ) {

		if (preempt_off_enable && pod->off_eip0 && !in_dump_stack) {
			pod->count++;

			delta = cycles_to_us(cycle_delta(now(), pod->off_time));

			pod->total += delta;

			if (delta > pod->max) {
				pod->max        = delta;
#ifdef CONFIG_SNSC_LITE_IRQSOFF_DEBUG
				pod->start_eip0 = pod->off_eip0;
				pod->start_eip1 = pod->off_eip1;
#ifdef CONFIG_SNSC_LITE_IRQSOFF_DEBUG_MARK_DEEP_EIP_STACK
				pod->start_eip2 = pod->off_eip2;
				pod->start_eip3 = pod->off_eip3;
				pod->start_eip4 = pod->off_eip4;
				pod->start_eip5 = pod->off_eip5;
#endif
				pod->end_eip0   = CALLER_ADDR0;
				pod->end_eip1   = CALLER_ADDR1;
#ifdef CONFIG_SNSC_LITE_IRQSOFF_DEBUG_MARK_DEEP_EIP_STACK
				pod->end_eip2   = CALLER_ADDR2;
				pod->end_eip3   = CALLER_ADDR3;
				pod->end_eip4   = CALLER_ADDR4;
				pod->end_eip5   = CALLER_ADDR5;
#endif

				save_mark(pod);

				if (preempt_off_dump_threshold &&
				    (raw_smp_processor_id() == irqs_dump_cpu) &&
				    (delta > preempt_off_dump_threshold)) {
					in_dump_stack = 1;
					printk("\n\n"
						"=====  preempt on, cpu: %d  delta: %ld"
						"  =====\n\n",
						irqs_dump_cpu, delta);
					dump_stack();
					printk("\n");
					in_dump_stack = 0;
				}
#endif
			}

			if (delta < pod->min)
				pod->min       = delta;

#ifdef CONFIG_SNSC_LITE_IRQSOFF_DEBUG_HISTOGRAM
			if (delta > BIN_MAX)
				delta = BIN_MAX;
			pod->bin[delta]++;
#endif
		}
		pod->off_eip0 = 0;

		trace_hardirqs_preempt_on(
#ifdef CONFIG_SNSC_LITE_IRQSOFF_DEBUG
			CALLER_ADDR0, CALLER_ADDR1,
#ifdef CONFIG_SNSC_LITE_IRQSOFF_DEBUG_MARK_DEEP_EIP_STACK
#ifdef CONFIG_SNSC_LITE_IRQSOFF_DEBUG_EIP_STACK_DEPTH_5
		CALLER_ADDR2, CALLER_ADDR3, CALLER_ADDR4, CALLER_ADDR5,
#elif defined CONFIG_SNSC_LITE_IRQSOFF_DEBUG_EIP_STACK_DEPTH_4
		CALLER_ADDR2, CALLER_ADDR3, CALLER_ADDR4, 0UL,
#elif defined CONFIG_SNSC_LITE_IRQSOFF_DEBUG_EIP_STACK_DEPTH_3
		CALLER_ADDR2, CALLER_ADDR3, 0UL, 0UL,
#elif defined CONFIG_SNSC_LITE_IRQSOFF_DEBUG_EIP_STACK_DEPTH_2
		CALLER_ADDR2, 0UL, 0UL, 0UL,
#endif
#endif
#endif
			/*
			 * do not check trace_cpu_idle here --
			 * stop_critical_timing() also calls
			 * trace_hardirqs_on(), which will call
			 * trace_hardirqs_preempt_on()
			 */
			irqs_disabled());
	}

	preempt_count() -= val;
}
EXPORT_SYMBOL(sub_preempt_count);

#endif /* CONFIG_SNSC_LITE_PREEMPTOFF */

/* __________________________________________________________________________ */

#if defined(CONFIG_SNSC_LITE_IRQSOFF) || defined(CONFIG_SNSC_LITE_PREEMPTOFF)
/* start and stop critical timings used to for stoppage (in idle) */
void start_critical_timings(void)
{
#ifdef CONFIG_SNSC_LITE_IRQSOFF
	trace_hardirqs_off();
#endif

#ifdef CONFIG_SNSC_LITE_PREEMPTOFF
	add_preempt_count(0);

	trace_preempt_exit_idle();
#endif
}

void stop_critical_timings(void)
{
#ifdef CONFIG_SNSC_LITE_PREEMPTOFF
	trace_preempt_enter_idle();
#endif

#ifdef CONFIG_SNSC_LITE_IRQSOFF
	trace_hardirqs_on();
#endif

#ifdef CONFIG_SNSC_LITE_PREEMPTOFF
	sub_preempt_count(0);
#endif
}
#endif

/* __________________________________________________________________________ */

#ifdef CONFIG_SNSC_DEBUG_IRQ_DURATION

void notrace irq_handler_rt_trace_enter(int irq)
{
	int cpu = raw_smp_processor_id();
	struct _off_data *iod = irqs_time_data + cpu;

	if (irqs_time_enable) {
		iod->off_time = now();
	}
}


void notrace irq_handler_rt_trace_exit(int irq)
{
	unsigned long delta;
	int cpu = raw_smp_processor_id();
	struct _off_data *iod = irqs_time_data + cpu;
	struct irq_stat *irq_stat;

	if (irqs_time_enable && iod->off_time) {

		delta = cycles_to_us(cycle_delta(now(), iod->off_time));

		iod->count++;
		iod->total += delta;

		if (delta > iod->max)
			iod->max = delta;

		if (delta < iod->min)
			iod->min = delta;

#ifdef CONFIG_SNSC_LITE_IRQSOFF_DEBUG_HISTOGRAM
		if (delta > BIN_MAX)
			delta = BIN_MAX;
		iod->bin[delta]++;
#endif

		if (irq >= NR_IRQ_INV)
			irq = NR_IRQ_INV;

		irq_stat = &per_cpu(lite_irq_stat, cpu)[irq];
		irq_stat->count++;
		irq_stat->total += delta;

		if (delta > irq_stat->max)
			irq_stat->max = delta;

		if (delta < irq_stat->min)
			irq_stat->min = delta;
	}
	iod->off_time = 0;

}
#endif /* CONFIG_SNSC_DEBUG_IRQ_DURATION */


/* implementor test, normally this define is commented out */
/* #define SNSC_IRQS_OFF_VALIDATE_TIME */
/* zzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzz */

#ifdef SNSC_IRQS_OFF_VALIDATE_TIME

static volatile int dummy;


#if defined(CONFIG_MIPS)
static unsigned long raw_delta(cycle_t a, cycle_t b) {

	unsigned long delta;

	delta = (GET_CYCLES_BITS == 32) ? (unsigned)a - (unsigned)b : a - b;
	return delta;
}
static unsigned long raw_delta_usecs(cycle_t a, cycle_t b) {

	unsigned long delta;

	delta = (GET_CYCLES_BITS == 32) ? (unsigned)a - (unsigned)b : a - b;
	do_div(delta, mips_hpt_frequency/1000000);
	return delta;
}
#elif defined(CONFIG_ARM)
	#error Implement raw_delta_usecs()
#else
	#error Implement raw_delta_usecs()
#endif

#endif /* SNSC_IRQS_OFF_VALIDATE_TIME */

#ifdef CONFIG_SNSC_RT_TRACE
static __init void rt_debug_init(void);
#endif


static __init int irqs_off_init(void)
{

#if defined(CONFIG_SNSC_LITE_IRQSOFF) \
 || defined(CONFIG_SNSC_LITE_PREEMPTOFF) \
 || defined(CONFIG_SNSC_DEBUG_IRQ_DURATION) \
 || defined(CONFIG_SNSC_5_RT_TRACE_WITH_PMU)

	struct proc_dir_entry *entry;
#endif

	/* zzz todo: change from create_proc_entry() to the
	 * zzz       newer proc_create().
	 */

#if defined(CONFIG_SNSC_LITE_IRQSOFF)
	irqs_off_entry.name = "irq          off";
	irqs_off_data_reset();
	entry = create_proc_entry("irqs_off", 0, NULL);
	if (entry)
		entry->proc_fops = &proc_irqs_off_operations;
#endif

#if defined(CONFIG_SNSC_LITE_IRQSOFF) && defined(CONFIG_SNSC_LITE_PREEMPTOFF)
	irqs_preempt_off_entry.name = "irq/preempt  off";
	irqs_preempt_off_data_reset();
	entry = create_proc_entry("irqs_preempt_off", 0, NULL);
	if (entry)
		entry->proc_fops = &proc_irqs_preempt_off_operations;
#endif

#if defined(CONFIG_SNSC_LITE_PREEMPTOFF)
	preempt_off_entry.name = "preempt      off";
	preempt_off_data_reset();
	entry = create_proc_entry("preempt_off", 0, NULL);
	if (entry)
		entry->proc_fops = &proc_preempt_off_operations;
#endif

#ifdef CONFIG_SNSC_DEBUG_IRQ_DURATION
	irqs_time_entry.name = "irq time        ";
	irqs_time_data_reset();
	irq_stat_reset();
	entry = create_proc_entry("irqs_time", 0, NULL);
	if (entry)
		entry->proc_fops = &proc_irqs_time_operations;

#endif

#if defined(CONFIG_SNSC_LITE_IRQSOFF_DEBUG_MARK_WITH_PMU) \
 || defined(CONFIG_SNSC_5_RT_TRACE_WITH_PMU)
	entry = create_proc_entry("mark_with_pmu", 0, NULL);
	if (entry)
		entry->proc_fops = &proc_mark_with_pmu_operations;
#endif

#ifdef CONFIG_SNSC_RT_TRACE
	rt_debug_init();
#endif


#ifdef SNSC_IRQS_OFF_VALIDATE_TIME
{
	int j;
	cycle_t t1, t2, t3, t4;
	cycle_t mt1, mt2;

	if (!dummy) {
		t1  = get_cycles();
		mt1 = get_monotonic_cycles();
		t2  = get_cycles();
		for (j = 0; j < 1000000; j++) {
			dummy++;
			if (dummy == -1)
				dummy = 0;
		}
		t3  = get_cycles();
		mt2 = get_monotonic_cycles();
		t4  = get_cycles();
		dummy = 1;

		printk("\n\n");
		printk("t2  - t1  %5ld cycle\n", raw_delta(t2, t1));
		printk("t4  - t3  %5ld cycle\n", raw_delta(t4, t3));
		printk("t2  - t1  %5ld usec\n", raw_delta_usecs(t2, t1));
		printk("t4  - t3  %5ld usec\n", raw_delta_usecs(t4, t3));
		printk("t3  - t2  %5ld usec\n", raw_delta_usecs(t3, t2));
		printk("t3  - t1  %5ld usec\n", raw_delta_usecs(t3, t1));
		printk("mt2 - mt1 %5ld usec\n", cycles_to_usecs(mt2 - mt1));
		printk("t4  - t1  %5ld usec\n", raw_delta_usecs(t4, t1));
		printk("\n\n");
	}

}
#endif

	return 0;
}
__initcall(irqs_off_init);





/* __________________________________________________________________________ */

#ifdef CONFIG_SNSC_RT_TRACE

int rt_debug_enable = 0;

SNSC_RT_TRACE_COMMON(rt_debug_1)
SNSC_RT_TRACE_COMMON(rt_debug_2)
SNSC_RT_TRACE_COMMON(rt_debug_3)
SNSC_RT_TRACE_COMMON(rt_debug_4)
SNSC_RT_TRACE_COMMON(rt_debug_5)
SNSC_RT_TRACE_COMMON(rt_debug_6)


/* cal_ are calibration variables */

unsigned long cal_min;
unsigned long cal_avg;
unsigned long cal_max;

unsigned long cal_enter_exit_min;
unsigned long cal_enter_exit_avg;
unsigned long cal_enter_exit_max;

static inline void print_cal_header(struct seq_file *m, struct _off_data *od, struct _off_entry *entry)
{
	if (od->count) {
		seq_printf(m,"#adjusted %-16s cpu %d min,avg,max,count : "
			"%3ld %3lld %4ld %7ld\n",
			entry->name,
			entry->cpu,
			od->min - cal_min,
			(od->total / od->count) - cal_avg,
			od->max - cal_max,
			od->count);
	} else {
		seq_printf(m,"#adjusted %-16s cpu %d min,avg,max,count : "
			"%3ld %3ld %4ld %7ld\n",
			entry->name, entry->cpu, 0L, 0L, 0L, 0L);
	}

	seq_printf(m,"#calibrate min,avg,max   : %ld %ld %ld\n",
		cal_min, cal_avg, cal_max);
	seq_printf(m,"#overhead  min,avg,max   : %ld %ld %ld\n",
		cal_enter_exit_min, cal_enter_exit_avg, cal_enter_exit_max);
	seq_printf(m, "#offset: 0 scale_factor: 1 max_bin: %d\n", BIN_MAX);
}


/* __________________________________________________________________________ */

#ifdef CONFIG_SNSC_DEBUG_IRQ_DURATION
void notrace irq_handler_rt_trace_exit_debug(int irq, int debug_num)
{
	unsigned long delta;
	int cpu = raw_smp_processor_id();
	struct _off_data *iod = irqs_time_data + cpu;
	struct irq_stat *irq_stat;

	if (irqs_time_enable && iod->off_time) {

		delta = cycles_to_us(cycle_delta(now(), iod->off_time));

		iod->count++;
		iod->total += delta;

		if (delta > iod->max)
			iod->max = delta;

		if (delta < iod->min)
			iod->min = delta;

#ifdef CONFIG_SNSC_LITE_IRQSOFF_DEBUG_HISTOGRAM
		if (delta > BIN_MAX)
			delta = BIN_MAX;
		iod->bin[delta]++;
#endif

		if (irq >= NR_IRQ_INV)
			irq = NR_IRQ_INV;

		irq_stat = &per_cpu(lite_irq_stat, cpu)[irq];
		irq_stat->count++;
		irq_stat->total += delta;

		if (delta > irq_stat->max)
			irq_stat->max = delta;

		if (delta < irq_stat->min)
			irq_stat->min = delta;



		iod->off_time = 0;
		switch (debug_num) {
			case 1: iod = rt_debug_1_data + cpu; break;
			case 2: iod = rt_debug_2_data + cpu; break;
			case 3: iod = rt_debug_3_data + cpu; break;
			case 4: iod = rt_debug_4_data + cpu; break;
			case 5: iod = rt_debug_5_data + cpu; break;
			case 6: iod = rt_debug_6_data + cpu; break;
			default: iod = NULL;
		}

		if (iod) {
			iod->count++;
			iod->total += delta;

			if (delta > iod->max)
				iod->max = delta;

			if (delta < iod->min)
				iod->min = delta;

#ifdef CONFIG_SNSC_LITE_IRQSOFF_DEBUG_HISTOGRAM
			if (delta > BIN_MAX)
				delta = BIN_MAX;
			iod->bin[delta]++;
#endif

		}

	} else {
		iod->off_time = 0;
	}

}
#endif /* CONFIG_SNSC_DEBUG_IRQ_DURATION */


#ifdef CONFIG_SNSC_RT_TRACE_LOCK_STAT

/*
 * debug_1_rt_trace_enter() and debug_1_rt_trace_exit() are special.  They
 * are the only debug_*_rt_trace_*() functions that enable and disable
 * lock_stats_rt_trace (see CONFIG_SNSC_RT_TRACE_LOCK_STAT code).
 *
 *
 * debug_5_rt_trace_enter() and debug_5_rt_trace_exit() are special.
 *
 *    When CONFIG_SNSC_5_RT_TRACE_WITH_PMU=y, PMU statistics are
 *    collected and reported for debug_5_rt_trace_*().
 *
 *    When CONFIG_SNSC_5_RT_TRACE_WITH_PMU_HISTOGRAM=y, PMU statistics are
 *    collected and reported for debug_5_rt_trace_*() in histogram form.
 *
 *    When CONFIG_SNSC_5_RT_TRACE_PMU_TRACE=y, for each call of
 *    debug_5_rt_trace_enter() and debug_5_rt_trace_exit() a trace event
 *    of the PMU data is reported.
 *
 *
 * Q. Why did I tie lock_stats_rt_trace into debug_1_rt_trace_*()?
 *
 * A1. lock_stats_rt_trace is a per cpu variable to avoid the costs of cache
 * line bouncing between cpus.  This means that it must be disabled on the
 * same cpu that it was enabled on.  Since debug_1_rt_trace_*() has these
 * same requirements (and the data from debug_1_rt_trace_*() will sometimes
 * show the problem if these requirements are not followed), it seemed a
 * good idea to tie lock_stats_rt_trace into debug_1_rt_trace_*().
 *
 * A2. The reason I implemented lock_stats_rt_trace was to try to see if I
 * could correlate lock_stat histogram data with rt_trace histogram data.
 */

DECLARE_PER_CPU(int, lock_stats_rt_trace);

#endif

void notrace debug_1_rt_trace_enter(void)
{
	int cpu = raw_smp_processor_id();
	struct _off_data *iod = rt_debug_1_data + cpu;

	if (rt_debug_enable) {
#ifdef CONFIG_SNSC_RT_TRACE_LOCK_STAT
		per_cpu(lock_stats_rt_trace, cpu) = 1;
#endif
		iod->off_time = now();
	}
}


void notrace debug_1_rt_trace_exit(void)
{
	unsigned long delta;
	int cpu = raw_smp_processor_id();
	struct _off_data *iod = rt_debug_1_data + cpu;

	if (rt_debug_enable && iod->off_time) {

		delta = cycles_to_us(cycle_delta(now(), iod->off_time));

		iod->count++;
		iod->total += delta;

		if (delta > iod->max)
			iod->max = delta;

		if (delta < iod->min)
			iod->min = delta;

#ifdef CONFIG_SNSC_LITE_IRQSOFF_DEBUG_HISTOGRAM
		if (delta > BIN_MAX)
			delta = BIN_MAX;
		iod->bin[delta]++;
#endif
	}
#ifdef CONFIG_SNSC_RT_TRACE_LOCK_STAT
	per_cpu(lock_stats_rt_trace, cpu) = 0;
#endif
	iod->off_time = 0;

}


void notrace debug_2_rt_trace_enter(void)
{
	int cpu = raw_smp_processor_id();
	struct _off_data *iod = rt_debug_2_data + cpu;

	if (rt_debug_enable) {
		iod->off_time = now();
	}
}


void notrace debug_2_rt_trace_exit(void)
{
	unsigned long delta;
	int cpu = raw_smp_processor_id();
	struct _off_data *iod = rt_debug_2_data + cpu;

	if (rt_debug_enable && iod->off_time) {

		delta = cycles_to_us(cycle_delta(now(), iod->off_time));

		iod->count++;
		iod->total += delta;

		if (delta > iod->max)
			iod->max = delta;

		if (delta < iod->min)
			iod->min = delta;

#ifdef CONFIG_SNSC_LITE_IRQSOFF_DEBUG_HISTOGRAM
		if (delta > BIN_MAX)
			delta = BIN_MAX;
		iod->bin[delta]++;
#endif
	}
	iod->off_time = 0;

}



void notrace debug_3_rt_trace_enter(void)
{
	int cpu = raw_smp_processor_id();
	struct _off_data *iod = rt_debug_3_data + cpu;

	if (rt_debug_enable) {
		iod->off_time = now();
	}
}


void notrace debug_3_rt_trace_exit(void)
{
	unsigned long delta;
	int cpu = raw_smp_processor_id();
	struct _off_data *iod = rt_debug_3_data + cpu;

	if (rt_debug_enable && iod->off_time) {

		delta = cycles_to_us(cycle_delta(now(), iod->off_time));

		iod->count++;
		iod->total += delta;

		if (delta > iod->max)
			iod->max = delta;

		if (delta < iod->min)
			iod->min = delta;

#ifdef CONFIG_SNSC_LITE_IRQSOFF_DEBUG_HISTOGRAM
		if (delta > BIN_MAX)
			delta = BIN_MAX;
		iod->bin[delta]++;
#endif
	}
	iod->off_time = 0;

}


void notrace debug_4_rt_trace_enter(void)
{
	int cpu = raw_smp_processor_id();
	struct _off_data *iod = rt_debug_4_data + cpu;

	if (rt_debug_enable) {
		iod->off_time = now();
	}
}


void notrace debug_4_rt_trace_exit(void)
{
	unsigned long delta;
	int cpu = raw_smp_processor_id();
	struct _off_data *iod = rt_debug_4_data + cpu;

	if (rt_debug_enable && iod->off_time) {

		delta = cycles_to_us(cycle_delta(now(), iod->off_time));

		iod->count++;
		iod->total += delta;

		if (delta > iod->max)
			iod->max = delta;

		if (delta < iod->min)
			iod->min = delta;

#ifdef CONFIG_SNSC_LITE_IRQSOFF_DEBUG_HISTOGRAM
		if (delta > BIN_MAX)
			delta = BIN_MAX;
		iod->bin[delta]++;
#endif
	}
	iod->off_time = 0;

}


void notrace debug_5_rt_trace_enter(void)
{
	int cpu = raw_smp_processor_id();
	struct _off_data *iod = rt_debug_5_data + cpu;

	if (rt_debug_enable) {
		iod->off_time = now();
#ifdef CONFIG_SNSC_5_RT_TRACE_WITH_PMU
		if (mark_with_pmu_enable) {
			stop_pmu(NULL);
			iod->pmu_data->enter_ccnt = read_ccnt();
			iod->pmu_data->enter_evt0 = read_evt0();
			iod->pmu_data->enter_evt1 = read_evt1();
#ifdef CONFIG_SNSC_5_RT_TRACE_PMU_TRACE
			per_cpu(debug_5_rt_pmu_trace_data_1, cpu) = 0;
			per_cpu(debug_5_rt_pmu_trace_data_2, cpu) = 0;
#endif
			restart_pmu();
		}
#endif
	}
}


void notrace debug_5_rt_trace_exit(void)
{
	unsigned long delta;
	int cpu = raw_smp_processor_id();
	struct _off_data *iod = rt_debug_5_data + cpu;

	if (rt_debug_enable && iod->off_time) {

#ifdef CONFIG_SNSC_5_RT_TRACE_WITH_PMU
		if (mark_with_pmu_enable) {
			stop_pmu(NULL);
		}
#endif
		delta = cycles_to_us(cycle_delta(now(), iod->off_time));

		iod->count++;
		iod->total += delta;

		if (delta > iod->max)
			iod->max = delta;

		if (delta < iod->min)
			iod->min = delta;

#ifdef CONFIG_SNSC_LITE_IRQSOFF_DEBUG_HISTOGRAM
		if (delta > BIN_MAX)
			delta = BIN_MAX;
		iod->bin[delta]++;
#endif
#ifdef CONFIG_SNSC_5_RT_TRACE_WITH_PMU
		if ((mark_with_pmu_enable) && (iod->pmu_data)) {

			unsigned long delta_ccnt;
			unsigned long delta_evt1;

			/* -----  ccnt  ----- */

			delta = read_ccnt() - iod->pmu_data->enter_ccnt;
			delta_ccnt = delta;

#ifdef CONFIG_SNSC_5_RT_TRACE_PMU_TRACE
			if (pmu_trace_enable) {
				iod->pmu_trace[iod->pmu_trace_next].ts     = sched_clock();
				iod->pmu_trace[iod->pmu_trace_next].ccnt   = delta;
				iod->pmu_trace[iod->pmu_trace_next].data_1 =
					per_cpu(debug_5_rt_pmu_trace_data_1, cpu);
				iod->pmu_trace[iod->pmu_trace_next].data_2 =
					per_cpu(debug_5_rt_pmu_trace_data_2, cpu);
			}
#endif

			iod->pmu_data->ccnt_count++;
			iod->pmu_data->ccnt_total += delta;

			if (delta > iod->pmu_data->ccnt_max)
				iod->pmu_data->ccnt_max = delta;

			if (delta < iod->pmu_data->ccnt_min)
				iod->pmu_data->ccnt_min = delta;

#ifdef CONFIG_SNSC_5_RT_TRACE_WITH_PMU_HISTOGRAM
			if (delta <= offset_bin_evt_ccnt) {
				delta = 0;
			} else {
				delta = (delta - offset_bin_evt_ccnt) / scale_bin_evt_ccnt;
				if (delta > BIN_PMU_MAX)
					delta = BIN_PMU_MAX;
			}
			iod->pmu_data->bin_ccnt[delta]++;
#endif


			/* -----  evt1  ----- */

			delta = read_evt1() - iod->pmu_data->enter_evt1;
			delta_evt1 = delta;

#ifdef CONFIG_SNSC_5_RT_TRACE_PMU_TRACE
			if (pmu_trace_enable)
				iod->pmu_trace[iod->pmu_trace_next].evt1 = delta;
#endif

			iod->pmu_data->evt1_count++;
			iod->pmu_data->evt1_total += delta;

			if (delta > iod->pmu_data->evt1_max)
				iod->pmu_data->evt1_max = delta;

			if (delta < iod->pmu_data->evt1_min)
				iod->pmu_data->evt1_min = delta;

#ifdef CONFIG_SNSC_5_RT_TRACE_WITH_PMU_HISTOGRAM
			if (delta <= offset_bin_evt_1) {
				delta = 0;
			} else {
				delta = (delta - offset_bin_evt_1) / scale_bin_evt_1;
				if (delta > BIN_PMU_MAX)
					delta = BIN_PMU_MAX;
			}
			iod->pmu_data->bin_evt1[delta]++;
#endif


			/* -----  evt0  ----- */

			delta = read_evt0() - iod->pmu_data->enter_evt0;

#ifdef CONFIG_SNSC_5_RT_TRACE_PMU_TRACE
			if (pmu_trace_enable) {
				iod->pmu_trace[iod->pmu_trace_next].evt0 = delta;
				iod->pmu_trace_next++;
				if (iod->pmu_trace_next > PMU_TRACE_MAX)
					pmu_trace_enable = 0;
			}
#endif

			if (pmu_hist_evt0_div_evt1) {
				if (!delta_evt1) {
					delta = BIN_PMU_MAX;	/* infinity */
				} else {
					delta = delta / delta_evt1;
				}
			} else if (pmu_hist_evt0_percent_ccnt) {
				if (!delta_ccnt) {
					delta = BIN_PMU_MAX;	/* infinity */
				} else {
					/* delta is percent * 10 */
					delta = (1000 * delta) / delta_ccnt;
				}
				if (delta > BIN_PMU_MAX)
					delta = BIN_PMU_MAX;
			}

			iod->pmu_data->evt0_count++;
			iod->pmu_data->evt0_total += delta;

			if (delta > iod->pmu_data->evt0_max)
				iod->pmu_data->evt0_max = delta;

			if (delta < iod->pmu_data->evt0_min)
				iod->pmu_data->evt0_min = delta;

#ifdef CONFIG_SNSC_5_RT_TRACE_WITH_PMU_HISTOGRAM
			if (pmu_hist_evt0_div_evt1) {
				if (delta > BIN_PMU_MAX)
					delta = BIN_PMU_MAX;
			} else {
				if (delta <= offset_bin_evt_0) {
					delta = 0;
				} else {
					delta = (delta - offset_bin_evt_0) / scale_bin_evt_0;
					if (delta > BIN_PMU_MAX)
						delta = BIN_PMU_MAX;
				}
			}
			iod->pmu_data->bin_evt0[delta]++;
#endif


			if (mark_with_pmu_enable) {
				restart_pmu();
			}
		}
#endif	/* CONFIG_SNSC_5_RT_TRACE_WITH_PMU */
	}
	iod->off_time = 0;

}


void notrace debug_6_rt_trace_enter(void)
{
	int cpu = raw_smp_processor_id();
	struct _off_data *iod = rt_debug_6_data + cpu;

	if (rt_debug_enable) {
		iod->off_time = now();
	}
}


void notrace debug_6_rt_trace_exit(void)
{
	unsigned long delta;
	int cpu = raw_smp_processor_id();
	struct _off_data *iod = rt_debug_6_data + cpu;

	if (rt_debug_enable && iod->off_time) {

		delta = cycles_to_us(cycle_delta(now(), iod->off_time));

		iod->count++;
		iod->total += delta;

		if (delta > iod->max)
			iod->max = delta;

		if (delta < iod->min)
			iod->min = delta;

#ifdef CONFIG_SNSC_LITE_IRQSOFF_DEBUG_HISTOGRAM
		if (delta > BIN_MAX)
			delta = BIN_MAX;
		iod->bin[delta]++;
#endif
	}
	iod->off_time = 0;

}


/* __________________________________________________________________________ */

static __init void rt_debug_time_calibrate(void)
{
	int cpu;
	int k;
	unsigned long flags;
	struct _off_data *od;

	local_irq_save(flags);
	cpu = raw_smp_processor_id();
	rt_debug_enable = 1;

	for (k = 0; k < 1000000; k++) {
		debug_1_rt_trace_enter();
		debug_1_rt_trace_exit();
	}

	for (k = 0; k < 1000000; k++) {
		debug_2_rt_trace_enter();
		debug_3_rt_trace_enter();
		debug_3_rt_trace_exit();
		debug_2_rt_trace_exit();
	}

	rt_debug_enable = 0;
	local_irq_restore(flags);

	od = rt_debug_1_data + cpu;

	cal_avg = (od->count) ? (od->total / od->count) : 0;
	cal_min = od->min;
	cal_max = od->max;

	od = rt_debug_2_data + cpu;

	cal_enter_exit_avg = (od->count) ? (od->total / od->count) : 0;
	cal_enter_exit_min = od->min;
	cal_enter_exit_max = od->max;

	cal_enter_exit_avg -= cal_avg;
	cal_enter_exit_min -= cal_min;
	cal_enter_exit_max -= cal_max;
}


void rt_debug_1_init_name(char *name)
{
	rt_debug_1_entry.name = name;
}


void rt_debug_2_init_name(char *name)
{
	rt_debug_2_entry.name = name;
}


void rt_debug_3_init_name(char *name)
{
	rt_debug_3_entry.name = name;
}


void rt_debug_4_init_name(char *name)
{
	rt_debug_4_entry.name = name;
}


void rt_debug_5_init_name(char *name)
{
	rt_debug_5_entry.name = name;
}


void rt_debug_6_init_name(char *name)
{
	rt_debug_6_entry.name = name;
}


static __init void rt_debug_init(void)
{
	struct proc_dir_entry *entry;

	/* name may be overridden in debug code via rt_debug_*_init_name() */
	if (!rt_debug_1_entry.name)
		rt_debug_1_entry.name   = "rt_debug_1      ";
	if (!rt_debug_2_entry.name)
		rt_debug_2_entry.name   = "rt_debug_2      ";
	if (!rt_debug_3_entry.name)
		rt_debug_3_entry.name   = "rt_debug_3      ";
	if (!rt_debug_4_entry.name)
		rt_debug_4_entry.name   = "rt_debug_4      ";
	if (!rt_debug_5_entry.name)
		rt_debug_5_entry.name   = "rt_debug_5      ";
	if (!rt_debug_6_entry.name)
		rt_debug_6_entry.name   = "rt_debug_6      ";

	rt_debug_1_entry.no_eip = 1;
	rt_debug_2_entry.no_eip = 1;
	rt_debug_3_entry.no_eip = 1;
	rt_debug_4_entry.no_eip = 1;
	rt_debug_5_entry.no_eip = 1;
	rt_debug_6_entry.no_eip = 1;

#ifdef CONFIG_SNSC_5_RT_TRACE_WITH_PMU
	{
	/*
	 * od->pmu_data is only used by rt_debug_5 so do
	 * not allocate memory for other users of this structure.
	 */
	int cpu;
	struct _off_data *od;
	for (cpu = 0; cpu < NR_CPUS; cpu++ ) {
		od = rt_debug_5_data + cpu;
		od->pmu_data  = kmalloc(sizeof(*od->pmu_data), GFP_KERNEL);
		/*
		 * rt_debug_* should only be enabled in the config if it us
		 * going to be used for debugging.  If kmalloc failed the
		 * system is unusable for the debug task, so use KERN_EMERG.
		 */
		if (od->pmu_data == NULL) {
			printk(KERN_EMERG "\n\n");
			printk(KERN_EMERG "rt_debug_init() kmalloc of pmu_data failed.\n");
			printk(KERN_EMERG "\n\n");
		}

#ifdef CONFIG_SNSC_5_RT_TRACE_PMU_TRACE
		od->pmu_trace = kmalloc(PMU_TRACE_NUM * sizeof(*od->pmu_trace), GFP_KERNEL);
		if (od->pmu_trace == NULL) {
			printk(KERN_EMERG "\n\n");
			printk(KERN_EMERG "rt_debug_init() kmalloc of pmu_trace failed.\n");
			printk(KERN_EMERG "\n\n");
		}
#endif

	}
	}
#endif	/* CONFIG_SNSC_5_RT_TRACE_WITH_PMU */

	rt_debug_1_data_reset();
	rt_debug_2_data_reset();
	rt_debug_3_data_reset();
	rt_debug_4_data_reset();
	rt_debug_5_data_reset();
	rt_debug_6_data_reset();

	rt_debug_time_calibrate();

	/* clean up after calibrate */
	rt_debug_1_data_reset();
	rt_debug_2_data_reset();
	rt_debug_3_data_reset();
	rt_debug_4_data_reset();
	rt_debug_5_data_reset();
	rt_debug_6_data_reset();

	entry = create_proc_entry("rt_debug_1", 0, NULL);
	if (entry)
		entry->proc_fops = &proc_rt_debug_1_operations;

	entry = create_proc_entry("rt_debug_2", 0, NULL);
	if (entry)
		entry->proc_fops = &proc_rt_debug_2_operations;

	entry = create_proc_entry("rt_debug_3", 0, NULL);
	if (entry)
		entry->proc_fops = &proc_rt_debug_3_operations;

	entry = create_proc_entry("rt_debug_4", 0, NULL);
	if (entry)
		entry->proc_fops = &proc_rt_debug_4_operations;

	entry = create_proc_entry("rt_debug_5", 0, NULL);
	if (entry)
		entry->proc_fops = &proc_rt_debug_5_operations;

	entry = create_proc_entry("rt_debug_6", 0, NULL);
	if (entry)
		entry->proc_fops = &proc_rt_debug_6_operations;
}

#else

static inline void print_cal_header(struct seq_file *m, struct _off_data *od, struct _off_entry *entry)
{
}

#ifdef CONFIG_SNSC_DEBUG_IRQ_DURATION
void notrace irq_handler_rt_trace_exit_debug(int irq, int debug_num) {}
#endif
void notrace debug_1_rt_trace_enter(void) {}
void notrace debug_1_rt_trace_exit(void)  {}
void notrace debug_2_rt_trace_enter(void) {}
void notrace debug_2_rt_trace_exit(void)  {}
void notrace debug_3_rt_trace_enter(void) {}
void notrace debug_3_rt_trace_exit(void)  {}
void notrace debug_4_rt_trace_enter(void) {}
void notrace debug_4_rt_trace_exit(void)  {}
void notrace debug_5_rt_trace_enter(void) {}
void notrace debug_5_rt_trace_exit(void)  {}
void notrace debug_6_rt_trace_enter(void) {}
void notrace debug_6_rt_trace_exit(void)  {}
void rt_debug_1_init_name(char *name) {}
void rt_debug_2_init_name(char *name) {}
void rt_debug_3_init_name(char *name) {}
void rt_debug_4_init_name(char *name) {}
void rt_debug_5_init_name(char *name) {}
void rt_debug_6_init_name(char *name) {}

#endif /* CONFIG_SNSC_RT_TRACE */
