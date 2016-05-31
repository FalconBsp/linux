/*
 *  (1) Light weight tracing of process migration
 *  (2) Light weight measurement of statistics of process migration
 *
 * Copyright (C) 2010 Sony Corporation of America, Frank Rowand
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

/*
 * DESIGN GOAL:
 *
 *   A tool to measure process migration, with low overhead (specifically,
 *   lower overhead than ftrace).
 *
 *   There are two tools in this file:
 *
 *
 *     - mt (migration trace):  CONFIG_SNSC_LITE_MIGRATION_TRACE
 *       A tool to collect detailed migration information.  The duration of
 *       the trace is limited by the amount of available memory, and is
 *       typically measured in minutes.
 *
 *     - mstat (migration statistics):  CONFIG_SNSC_LITE_MIGRATION_STAT
 *       A lower overhead tool that can collect data over a long time.  The
 *       memory usage of this tool is much smaller, but the amount of detail
 *       is much lower.  The duration of the measurement could be hours, days,
 *       or even longer.
 *
 * CAVEATS:
 *
 *   Implemented for 32 bit ARM.  Will probably need to be fixed if used
 *   for anything else.
 *
 * ASSUMPTIONS:
 *
 *   sched_clock() is (approximately) synchronized across cpus
 *   sched_clock() never decreases and does not wrap around
 *   void * fits in a local_t
 */

#include <linux/percpu.h>
#include <linux/seq_file.h>

#include <linux/debugfs.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/uaccess.h>
#include <linux/preempt.h>
#include <linux/sched.h>
#include <linux/time.h>
#include <linux/migrate_trace_lite.h>
/* for smp_call_function() */
#include <linux/smp.h>

#include <asm/local.h>


/*
 * LOCATIONs will be or'ed into upper 16 bits of loc_and_len
 */
#define MT_EVENT_LEN_MASK                 0x000000ff
#define MT_EVENT_UNUSED_MASK              0x0000ff00
#define MT_EVENT_LOCATION_MASK            0xffff0000

#define MT_EVENT_LOCATION_SHIFT 16
#define LOC_NUM(loc) (loc >> MT_EVENT_LOCATION_SHIFT)

/*
 * struct mstat.cause_cnt[] is accessed via "LOC_NUM() - 1", assuming that
 * LOC_NUM(MT_EVENT_LOCATION_reserved) is zero and not a valid cause.
 */

#define MT_EVENT_LOCATION_reserved        0x00000000
#define MT_EVENT_LOCATION___migrate_task  0x00010000
#define MT_EVENT_LOCATION_pull_one_task   0x00020000
#define MT_EVENT_LOCATION_pull_rt_task    0x00030000
#define MT_EVENT_LOCATION_pull_task       0x00040000
#define MT_EVENT_LOCATION_push_rt_task    0x00050000
#define MT_EVENT_LOCATION_sched_fork      0x00060000
#define MT_EVENT_LOCATION_try_to_wake_up  0x00070000

#define MT_EVENT_LOCATION_MAX_CAUSE LOC_NUM(MT_EVENT_LOCATION_try_to_wake_up)

/* above events are a cause of migration */

/*
 * events that report comm
 */

#define MT_EVENT_LOCATION_copy_process    0x00080000
#define MT_EVENT_LOCATION_set_task_comm   0x00090000
#define MT_EVENT_LOCATION_task_list       0x000a0000

/*
 * Other useful events
 */

#define MT_EVENT_LOCATION_mark            0x000b0000

#define MT_EVENT_LOCATION_MAX_EVENT LOC_NUM(MT_EVENT_LOCATION_mark)


/*
 * Callers of set_task_cpu() that are not actually migrations, so thus
 * not logged or counted.
 *
 *   copy_process() (second call site, just before set_task_cpu())
 *   kthread_bind()
 *   init_idle()
 */

/* names must be in same order as above MT_EVENT_LOCATION_* defines */
static char *mt_location_func_name[] = {
	"reserved",
	"__migrate_task",
	"pull_one_task",
	"pull_rt_task",
	"pull_task",
	"push_rt_task",
	"sched_fork",
	"try_to_wake_up",

	"copy_process",
	"set_task_comm",
	"task_list",
	"mark",
};




/* __________________________________________________________________________ */

#ifdef CONFIG_SNSC_LITE_MIGRATION_STAT


/* redefine time related constants as ULL to avoid overflow in calculations */
#undef NSEC_PER_MSEC
#undef NSEC_PER_SEC

#define NSEC_PER_MSEC   1000000ULL
#define NSEC_PER_SEC    1000000000ULL

/*
 * Update MSTAT_VERSION when the stat file format changes.
 * Format of MSTAT_VERSION is major.minor.fix
 *
 * Update fix if the stat file format does not change.
 *
 * Update minor if adding new events or if changes to existing events will
 * not cause post processing tools to be unable to process the stat file.
 * Post processing tools might not be able to take advantage of the new events
 * in the stat file or additional information * added to existing events, but
 * will still properly process everything else.)
 *
 * Update major if post processing tools _must_ be updated to process the
 * stat file.
 *
 * The number of fields, the order of fields, and the meaning of fields are
 * not tied to MSTAT_VERSION.  Post-processing tools are expected to get
 * this information from the header lines of the stat file.
 */
#define MSTAT_VERSION "1.0.0"


/*
 * CONFIG_SNSC_LITE_MIGRATION_STAT_DATA_SIZE is number of hours with a
 * period of one second
 */
#define MAX_PERIOD (60 * 60 * CONFIG_SNSC_LITE_MIGRATION_STAT_DATA_SIZE)

/*
 * mstat.cause_cnt[] is accessed via "LOC_NUM() - 1", assuming that
 * LOC_NUM(MT_EVENT_LOCATION_reserved) is zero and not a valid cause.
 */
struct mstat {
	local_t cause_cnt[MAX_PERIOD][MT_EVENT_LOCATION_MAX_CAUSE];
} ____cacheline_aligned_in_smp;

static struct mstat mstat[NR_CPUS] ____cacheline_aligned_in_smp;


static DEFINE_RT_MUTEX(mstat_trace_mutex);

static int mstat_enable;
static int mstat_trc_not_empty;

static struct timeval mstat_start_gtod_tv;
static unsigned long long mstat_start_sched_clock;
static unsigned long long mstat_stop_sched_clock;
static unsigned long long period_nsec = NSEC_PER_SEC;


struct mstat_entry {
	unsigned long long  pos;
};

static struct mstat_entry mstat_entry;


static void mstat_init_data(void)
{
	rt_mutex_lock(&mstat_trace_mutex);

	/*
	 * side effect: disables mstat
	 */
	mstat_enable = 0;

	mstat_trc_not_empty = 0;

	mstat_stop_sched_clock = mstat_start_sched_clock;

	memset(&mstat, 0, sizeof(mstat));

	rt_mutex_unlock(&mstat_trace_mutex);
}

/*
 * ----- begin debug fs related functions
 */

static void *mstat_start(struct seq_file *m, loff_t *pos)
{
	struct mstat_entry *entry = &mstat_entry;
	unsigned long long end_index;

	rt_mutex_lock(&mstat_trace_mutex);

	if (mstat_enable)
		mstat_stop_sched_clock = sched_clock();

	if (*pos >= MAX_PERIOD)
		return NULL;

	end_index = (mstat_stop_sched_clock - mstat_start_sched_clock) /
		    period_nsec;

	/* final period may be partial, do not print it */
	if (*pos >= end_index)
		return NULL;

	entry->pos = *pos;

	return entry;
}

static void mstat_stop(struct seq_file *m, void *v)
{
	rt_mutex_unlock(&mstat_trace_mutex);
}

static void *mstat_next(struct seq_file *m, void *v, loff_t *pos)
{
	struct mstat_entry *entry;
	unsigned long long end_index;

	entry    = v;

	if (++(entry->pos) >= MAX_PERIOD)
		return NULL;

	end_index = (mstat_stop_sched_clock - mstat_start_sched_clock) /
		    period_nsec;

	/* final period may be partial, do not print it */
	if (entry->pos >= end_index)
		return NULL;

	(*pos)++;

	return v;
}

static int mstat_show(struct seq_file *m, void *v)
{
	struct mstat_entry *entry;
	int period;
	int cpu;
	int loc;
	unsigned long long period_ts;
	unsigned long long sec;
	unsigned long long msec;
	unsigned long total;
	unsigned long migrate;

	entry = v;
	period = entry->pos;

	if (period >= MAX_PERIOD)
		return 0;


	if (period == 0) {
		seq_printf(m, "# version %s\n", MSTAT_VERSION);
		seq_printf(m, "# period_msec %lld\n",
			   period_nsec / NSEC_PER_MSEC);
		seq_printf(m, "# gettimeofday %ld %ld\n",
			   mstat_start_gtod_tv.tv_sec,
			   mstat_start_gtod_tv.tv_usec);
		seq_printf(m, "# ts_field 1\n");
		for (loc = 1; loc <= MT_EVENT_LOCATION_MAX_CAUSE; loc++) {
			seq_printf(m, "# field %u %s\n", loc + 1,
				   mt_location_func_name[loc]);
		}
		seq_printf(m, "# field %u migrate\n", loc + 1);
	}

	period_ts = period * period_nsec;
	sec  =  period_ts / NSEC_PER_SEC;
#define MOD_BROKEN
#ifdef MOD_BROKEN
	msec = (period_ts - (sec * NSEC_PER_SEC)) / NSEC_PER_MSEC;
#else
	msec = (period_ts % NSEC_PER_SEC) / NSEC_PER_MSEC;
#endif

	seq_printf(m, "%llu.%03llu", sec, msec);

	migrate = 0;
	for (loc = 0; loc < MT_EVENT_LOCATION_MAX_CAUSE; loc++) {
		total = 0;
		for (cpu = 0; cpu < num_possible_cpus(); cpu++)
			total += local_read(&mstat[cpu].cause_cnt[period][loc]);
		migrate += total;
		seq_printf(m, " %ld", total);
	}
	seq_printf(m, " %ld", migrate);
	seq_printf(m, "\n");
	return 0;
}

static const struct seq_operations mstat_seq_op = {
	.start = mstat_start,
	.stop  = mstat_stop,
	.next  = mstat_next,
	.show  = mstat_show
};

static int mstat_seq_open(struct inode *inode, struct file *filp)
{

	return seq_open(filp, &mstat_seq_op);
}

static ssize_t mstat_enable_read(struct file *filp, char __user *ubuf,
				size_t cnt, loff_t *ppos)
{
#define BUFSIZE 32
	char buf[BUFSIZE];
	int ret;

	ret = snprintf(buf, BUFSIZE, "%u\n", mstat_enable);

	return simple_read_from_buffer(ubuf, cnt, ppos, buf, ret);
}

/*
 * mstat_trace_enable() - directly called from within kernel for experiments
 * mstat_enable_write() - called from debugfs
 */

int mstat_trace_enable(int enable)
{
	int ret = 0;

	rt_mutex_lock(&mstat_trace_mutex);
	if (enable == 0) {

		if (mstat_enable) {
			mstat_enable = 0;
			mstat_stop_sched_clock = sched_clock();
		}

	} else if (enable == 1) {

		/*
		 * Do not allow restarting a trace
		 */
		if (mstat_trc_not_empty) {
			ret = -EPERM;
			goto out;
		}

		mstat_start_sched_clock = sched_clock();
		do_gettimeofday(&mstat_start_gtod_tv);

		mstat_trc_not_empty = 1;
		mstat_enable        = 1;

	} else {
		ret = -EINVAL;
		goto out;
	}

out:
	rt_mutex_unlock(&mstat_trace_mutex);

	return ret;
}

static ssize_t mstat_enable_write(struct file *filp, const char __user *ubuf,
				  size_t cnt, loff_t *ppos)
{
	char buf[2];
	int ret = cnt;

	if (cnt > sizeof(buf))
		return -EINVAL;

	rt_mutex_lock(&mstat_trace_mutex);

	if (copy_from_user(&buf, ubuf, cnt)) {
		ret = -EFAULT;
		goto out;
	}

	if (*buf == '0') {

		if (mstat_enable) {
			mstat_enable = 0;
			mstat_stop_sched_clock = sched_clock();
		}

	} else if (*buf == '1') {

		/*
		 * Do not allow restarting a measurement
		 */
		if (mstat_trc_not_empty) {
			ret = -EPERM;
			goto out;
		}

		mstat_start_sched_clock = sched_clock();
		do_gettimeofday(&mstat_start_gtod_tv);

		mstat_trc_not_empty = 1;
		mstat_enable        = 1;

	} else {
		ret = -EINVAL;
		goto out;
	}

out:
	rt_mutex_unlock(&mstat_trace_mutex);

	return ret;
}

static ssize_t mstat_period_msec_read(struct file *filp, char __user *ubuf,
				      size_t cnt, loff_t *ppos)
{
#define BUFSIZE 32
	char buf[BUFSIZE];
	int ret;

	ret = snprintf(buf, BUFSIZE, "%llu\n", period_nsec / NSEC_PER_MSEC);

	return simple_read_from_buffer(ubuf, cnt, ppos, buf, ret);
}

static ssize_t mstat_period_msec_write(struct file *filp,
				       const char __user *ubuf,
				       size_t cnt, loff_t *ppos)
{
	char buf[18];
	unsigned long tmp;
	int ret = cnt;
	int tmp_ret;

	/* "- 1" to leave room to append buf with '\0' */
	if (cnt > sizeof(buf) - 1)
		return -EINVAL;

	if (copy_from_user(&buf, ubuf, cnt))
		return -EFAULT;

	/*
	 * Do not allow changing period for an existing or active measurement
	 */

	rt_mutex_lock(&mstat_trace_mutex);

	if (mstat_trc_not_empty) {
		ret = -EPERM;
		goto out;
	}

	buf[cnt] = '\0';

	tmp_ret = strict_strtoul(buf, 0, &tmp);
	if (tmp_ret) {
		ret = tmp_ret;
		goto out;
	}

	/* avoid divide by zero when period_nsec is a divisor */
	if (tmp == 0) {
		ret = -EINVAL;
		goto out;
	}

	period_nsec = tmp * NSEC_PER_MSEC;

out:
	rt_mutex_unlock(&mstat_trace_mutex);

	return ret;
}

static ssize_t mstat_reset_write(struct file *filp, const char __user *ubuf,
				size_t cnt, loff_t *ppos)
{
	char buf[2];

	if (cnt > sizeof(buf))
		return -EINVAL;

	if (copy_from_user(&buf, ubuf, cnt))
		return -EFAULT;

	if (*buf == '0')
		mstat_init_data();
	else
		return -EINVAL;

	return cnt;
}

static int mstat_ctl_open(struct inode *inode, struct file *filp)
{
	filp->private_data = inode->i_private;

	return 0;
}

static const struct file_operations mstat_stat_ops = {
	/*
	 * If .llseek is desired, then mt_start() needs to be
	 * enhanced.
	 * .llseek = seq_lseek,
	 */
	.read    = seq_read,
	.open    = mstat_seq_open,
	.release = seq_release,
};

static const struct file_operations mstat_enable_fops = {
	.read    = mstat_enable_read,
	.write   = mstat_enable_write,
	.open    = mstat_ctl_open,
};

static const struct file_operations mstat_period_msec_fops = {
	.read    = mstat_period_msec_read,
	.write   = mstat_period_msec_write,
	.open    = mstat_ctl_open,
};

static const struct file_operations mstat_reset_fops = {
	.write   = mstat_reset_write,
	.open    = mstat_ctl_open,
};

static __init int mstat_debugfs_init(void)
{
	struct dentry *dentry;
	struct dentry *dentry_dir;
	char *dir_name = "migration_stat";
	char *file;

	dentry_dir = debugfs_create_dir(dir_name, NULL);
	if (!dentry_dir) {
		pr_warning("create debugfs directory '%s' failed\n",
			   dir_name);
	} else {
		file = "enable";
		dentry = debugfs_create_file(file, 0664, dentry_dir,
					     NULL, &mstat_enable_fops);
		if (!dentry)
			pr_warning("create debugfs file '%s/%s' failed\n",
				   dir_name, file);

		file = "period_msec";
		dentry = debugfs_create_file(file, 0664, dentry_dir,
					     NULL, &mstat_period_msec_fops);
		if (!dentry)
			pr_warning("create debugfs file '%s/%s' failed\n",
				   dir_name, file);

		file = "reset";
		dentry = debugfs_create_file(file, 0664, dentry_dir,
					     NULL, &mstat_reset_fops);
		if (!dentry)
			pr_warning("create debugfs file '%s/%s' failed\n",
				   dir_name, file);

		file = "stat";
		dentry = debugfs_create_file(file, 0444, dentry_dir,
					     NULL, &mstat_stat_ops);
		if (!dentry)
			pr_warning("create debugfs file '%s/%s' failed\n",
				   dir_name, file);
	}

	return 0;
}

static __init int mstat_init(void)
{
	mstat_init_data();

	mstat_debugfs_init();

	return 0;
}
late_initcall(mstat_init);

/*
 * ----- end debug fs related functions
 */


#endif /* CONFIG_SNSC_LITE_MIGRATION_STAT */


/* __________________________________________________________________________ */

#ifdef CONFIG_SNSC_LITE_MIGRATION_TRACE


/*
 * Update MT_VERSION when the trace file format changes.
 * Format of MT_VERSION is major.minor.fix
 *
 * Update fix if the trace file format does not change.
 *
 * Update minor if adding new events or if changes to existing events will
 * not cause post processing tools to be unable to process the * trace file.
 * (Post processing tools might not be able to take advantage of the new events
 * in the trace file or additional information added to existing events, but
 * will still properly process everything else.)
 *
 * Update major if post processing tools _must_ be updated to process the
 * trace file.
 */
#define MT_VERSION "1.0.0"

static DEFINE_RT_MUTEX(mt_trace_mutex);

static int mt_trc_not_empty;


/*
 * All events are aligned to unsigned long long.  Pad is added on the end
 * to maintain the alignment.
 */

#define MT_BUF_SIZE (1024 * CONFIG_SNSC_LITE_MIGRATION_TRACE_BUF_SIZE)

struct mt_trc {
	local_t             next_offset;
	local_t             next_index;
	char                buf[MT_BUF_SIZE];
	unsigned long long  start_sched_clock;
	struct timeval      tv;
} ____cacheline_aligned_in_smp;

static struct mt_trc mt_trc[NR_CPUS] ____cacheline_aligned_in_smp;

#define EVENT_LEN(hdr) (hdr->loc_and_len & MT_EVENT_LEN_MASK);

struct mt_event_header {
	unsigned long long  sched_clock;
	unsigned long       loc_and_len;
	pid_t               pid;
};

struct mt_set_task_comm {
	struct mt_event_header  hdr;
	char                    comm[TASK_COMM_LEN];
};

struct mt_event_migrate {
	struct mt_event_header  hdr;
	int                     old_cpu;
	int                     new_cpu;
};

struct mt_event_mark {
	struct mt_event_header  hdr;
	unsigned long           mark;
};


struct mt_entry {
	struct mt_trc           *mt_trc;
	unsigned long            buf_index;
	struct mt_event_header  *hdr;
	int                      pos_0;
	int                      cpu;
	int                      save_valid;
	int                      save_cpu;
	unsigned int             save_buf_index;
	unsigned long long       save_end_pos;
	struct mt_event_header  *save_hdr;
};

static struct mt_entry mt_entry;


static int mt_enable;

static void mt_get_sched_clock(void *info)
{
	int cpu = raw_smp_processor_id();
	struct mt_trc *__mt_trc = mt_trc + cpu;

	__mt_trc->start_sched_clock = sched_clock();
	do_gettimeofday(&__mt_trc->tv);
}

static void mt_init_data(void)
{
	rt_mutex_lock(&mt_trace_mutex);

	/*
	 * side effect: disables mt trace
	 */
	mt_enable = 0;

	mt_trc_not_empty = 0;

	mt_entry.save_valid = 0;

	memset(&mt_trc, 0, sizeof(mt_trc));

	rt_mutex_unlock(&mt_trace_mutex);
}

/*
 * ASSUMES
 *
 *    parameters are valid:
 *       len <= 0xff
 *       len >= sizeof(struct mt_event_header)
 *       (loc & 0xffff) == 0
 *
 *    There will not be a large number of callers of this function so
 *    inline will not cause code bloat.
 */
static inline void *mt_reserve_buf(unsigned int len, unsigned int loc,
					struct task_struct *p)
{
	int cpu = raw_smp_processor_id();
	struct mt_trc *__mt_trc = mt_trc + cpu;
	struct mt_event_header *hdr;
	long new_offset;

	new_offset = local_add_return(len, &__mt_trc->next_offset);

	if ((new_offset > 0) && (new_offset <= MT_BUF_SIZE)) {
		local_inc(&__mt_trc->next_index);
		hdr = ((void *)__mt_trc->buf) + new_offset - len;
		hdr->sched_clock = sched_clock();
		hdr->loc_and_len = loc | len;
		hdr->pid         = p->pid;
		return hdr;
	} else {
		mt_enable = 0;
		return NULL;
	}

}

void mt_copy_process(struct task_struct *p)
{
	struct mt_set_task_comm *event;
	int event_len = sizeof(*event);

	if (!mt_enable)
		return;

	event = mt_reserve_buf(event_len, MT_EVENT_LOCATION_copy_process, p);
	if (event)
		strlcpy(event->comm, p->comm, sizeof(event->comm));
}

void mt_set_task_comm(struct task_struct *p)
{
	struct mt_set_task_comm *event;
	int event_len = sizeof(*event);

	if (!mt_enable)
		return;

	event = mt_reserve_buf(event_len, MT_EVENT_LOCATION_set_task_comm, p);
	if (event)
		strlcpy(event->comm, p->comm, sizeof(event->comm));
}

void mt_task_list(struct task_struct *p)
{
	struct mt_set_task_comm *event;
	int event_len = sizeof(*event);

	if (!mt_enable)
		return;

	event = mt_reserve_buf(event_len, MT_EVENT_LOCATION_task_list, p);
	if (event)
		strlcpy(event->comm, p->comm, sizeof(event->comm));
}

static void mt_log_comm(void)
{
	struct task_struct *p;

	read_lock(&tasklist_lock);

	for_each_process(p)
		mt_task_list(p);

	read_unlock(&tasklist_lock);
}


/*
 * ----- begin debug fs related functions
 */

static void *mt_start(struct seq_file *m, loff_t *pos)
{
	struct mt_entry *entry = &mt_entry;
	struct mt_event_header *hdr;
	int cpu;
	unsigned int buf_index;
	unsigned long long end_pos;
	unsigned long long next_pos;
	unsigned long long prev_end_pos;


	rt_mutex_lock(&mt_trace_mutex);

	/*
	 * Make sure the trace is stopped.  Once *pos has moved info a second
	 * cpu's mt_trc[]->buf then any new events added to a previous cpu's
	 * mt_trc[]->buf will cause subsequent calls to mt_start() to set
	 * entry to an incorrect location.
	 */
	mt_enable = 0;

	end_pos = 0;
	for (cpu = 0; cpu < num_possible_cpus(); cpu++)
		end_pos += local_read(&mt_trc[cpu].next_index);
	if ((*pos > end_pos) || (end_pos == 0))
		return NULL;

	/*
	 * Scan through trace, by cpu, until get to desired pos.
	 */

	end_pos = 0;
	hdr = 0; /* suppress gcc "may be used unitialized" warning */
	for (cpu = 0; cpu < num_possible_cpus(); cpu++) {
		hdr          = (void *)&mt_trc[cpu].buf;
		buf_index    = 0;
		prev_end_pos = end_pos;
		end_pos     += local_read(&mt_trc[cpu].next_index);
		if (end_pos > *pos) {
			if (entry->save_valid &&
			    (entry->save_cpu     == cpu) &&
			    (entry->save_end_pos <= *pos)
			   ) {
				hdr          = entry->save_hdr;
				buf_index    = entry->save_buf_index;
				prev_end_pos = entry->save_end_pos;
			}
			next_pos = prev_end_pos;
			while (1) {
				if (next_pos == *pos)
					goto for_cpu_break;
				hdr = ((void *)hdr) + EVENT_LEN(hdr);
				next_pos++;
				buf_index++;
			}
		}
	}
for_cpu_break:


	if (cpu == num_possible_cpus())
		return NULL;	/* this should not happen */

	entry->save_cpu       = cpu;
	entry->save_hdr       = hdr;
	entry->save_buf_index = buf_index;
	entry->save_end_pos   = end_pos;
	entry->save_valid     = 1;

	if (*pos == 0)
		entry->pos_0 = 1;
	entry->buf_index  = buf_index;
	entry->hdr        = hdr;
	entry->mt_trc     = &mt_trc[cpu];
	entry->cpu        = cpu;

	return entry;
}

static void mt_stop(struct seq_file *m, void *v)
{
	rt_mutex_unlock(&mt_trace_mutex);
}

static void *mt_next(struct seq_file *m, void *v, loff_t *pos)
{
	struct mt_trc *e_mt_trc;
	struct mt_entry *entry;
	struct mt_event_header *hdr;

	entry    = v;
	e_mt_trc = entry->mt_trc;
	hdr      = entry->hdr;

	if (++(entry->buf_index) >= local_read(&e_mt_trc->next_index)) {
		if (++(entry->cpu) >= num_possible_cpus()) {
			return NULL;
		} else {
			entry->mt_trc = &mt_trc[entry->cpu];
			entry->buf_index = 0;
			entry->hdr = (void *)&mt_trc[entry->cpu].buf;
		}
	} else {
		entry->hdr = ((void *)entry->hdr) + EVENT_LEN(hdr);
	}

	(*pos)++;

	return v;
}

static int mt_show(struct seq_file *m, void *v)
{
	int buf_index;
	int k;
	unsigned long loc;
	struct mt_entry *entry;
	struct mt_trc *mt_trc;
	struct mt_event_header *hdr;

	entry     = v;
	buf_index = entry->buf_index;
	mt_trc    = entry->mt_trc;
	hdr       = entry->hdr;

	if (entry->cpu >= num_possible_cpus())
		goto out;

	if (buf_index == 0) {

		if (entry->pos_0) {
			entry->pos_0 = 0;

			seq_printf(m, "# version %s\n", MT_VERSION);
			seq_printf(m, "# nr_cpus %u\n", num_possible_cpus());
			seq_printf(m, "# ts_field 1\n");

			for (k = 1; k <= MT_EVENT_LOCATION_MAX_CAUSE; k++) {
				seq_printf(m, "# cause %u %s\n", k,
					  mt_location_func_name[k]);
			}

			for (k = MT_EVENT_LOCATION_MAX_CAUSE + 1;
			     k <= MT_EVENT_LOCATION_MAX_EVENT; k++) {
				seq_printf(m, "# loc %u %s\n", k,
					  mt_location_func_name[k]);
			}

		}

		if (mt_trc->start_sched_clock) {
			seq_printf(m, "0x%016llx %u . . . "
				   "start_sched_clock %ld %ld\n",
				   mt_trc->start_sched_clock,
				   entry->cpu, mt_trc->tv.tv_sec,
				   mt_trc->tv.tv_usec);
		}
	}

	if (!local_read(&mt_trc->next_index))
		goto out;

	loc = hdr->loc_and_len & MT_EVENT_LOCATION_MASK;

	/* try_to_wake_up likely most frequent, test first */
	if ((loc == MT_EVENT_LOCATION_try_to_wake_up) ||
	    (loc == MT_EVENT_LOCATION___migrate_task) ||
	    (loc == MT_EVENT_LOCATION_pull_one_task)  ||
	    (loc == MT_EVENT_LOCATION_pull_rt_task)   ||
	    (loc == MT_EVENT_LOCATION_pull_task)      ||
	    (loc == MT_EVENT_LOCATION_push_rt_task)   ||
	    (loc == MT_EVENT_LOCATION_sched_fork)
	   ) {

		struct mt_event_migrate *event = (void *)hdr;

		seq_printf(m, "0x%016llx %u %u %u %u %lu\n",
			hdr->sched_clock,
			entry->cpu,
			event->old_cpu,
			event->new_cpu,
			hdr->pid,
			LOC_NUM(loc));

	} else if ((loc == MT_EVENT_LOCATION_copy_process)  ||
		   (loc == MT_EVENT_LOCATION_set_task_comm) ||
		   (loc == MT_EVENT_LOCATION_task_list)
		  ) {

		struct mt_set_task_comm *event = (void *)hdr;

		seq_printf(m, "0x%016llx %u . . %u %ld : %s\n",
			hdr->sched_clock,
			entry->cpu,
			hdr->pid,
			LOC_NUM(loc),
			event->comm);

	} else if (loc == MT_EVENT_LOCATION_mark) {

		struct mt_event_mark *event = (void *)hdr;

		seq_printf(m, "0x%016llx %u . . %u %lu 0x%08lx\n",
			hdr->sched_clock,
			entry->cpu,
			hdr->pid,
			LOC_NUM(loc),
			event->mark);

	}

out:
	return 0;
}

static const struct seq_operations mt_seq_op = {
	.start = mt_start,
	.stop  = mt_stop,
	.next  = mt_next,
	.show  = mt_show
};

static int mt_seq_open(struct inode *inode, struct file *filp)
{
	/*
	 * Do not allow access while tracing is active.
	 */
	if (mt_enable)
		return -EPERM;

	return seq_open(filp, &mt_seq_op);
}

void mt_trace_mark(unsigned long mark)
{
	struct mt_event_mark *event;
	int event_len = sizeof(*event);

	if (!mt_enable)
		return;

	event = mt_reserve_buf(event_len, MT_EVENT_LOCATION_mark, current);
	if (event)
		event->mark = mark;
}

static ssize_t mt_enable_read(struct file *filp, char __user *ubuf,
				size_t cnt, loff_t *ppos)
{
#define BUFSIZE 32
	char buf[BUFSIZE];
	int ret;

	ret = snprintf(buf, BUFSIZE, "%u\n", mt_enable);

	return simple_read_from_buffer(ubuf, cnt, ppos, buf, ret);
}

/*
 * mt_trace_enable() - directly called from within kernel for experiments
 * mt_enable_write() - called from debugfs
 */

/*
 * Analysis tools assume a continuous trace.  Do not specify allow_restart
 * unless you understand the implications for the analysis tools.
 */
int mt_trace_enable(int enable, int allow_restart)
{
	int ret = 0;


	rt_mutex_lock(&mt_trace_mutex);

	if (enable == 0) {

		mt_enable = 0;

	} else if (enable == 1) {

		if (mt_trc_not_empty && !allow_restart) {
			ret = -EPERM;
			goto out;
		}
		mt_trc_not_empty = 1;

		/*
		 * Put start times in trace.
		 * Get local mt_trc[CPU]->start_sched_clock,
		 * then on each other cpu.
		 */
		mt_get_sched_clock(NULL);

		preempt_disable();
		smp_call_function(mt_get_sched_clock, NULL, 1);
		preempt_enable();

		mt_enable = 1;

		mt_log_comm();

	} else {
		ret = -EINVAL;
		goto out;
	}

out:
	rt_mutex_unlock(&mt_trace_mutex);

	return ret;
}

static ssize_t mt_enable_write(struct file *filp, const char __user *ubuf,
				size_t cnt, loff_t *ppos)
{
	char buf[2];
	int ret = cnt;

	if (cnt > sizeof(buf))
		return -EINVAL;

	if (copy_from_user(&buf, ubuf, cnt))
		return -EFAULT;

	rt_mutex_lock(&mt_trace_mutex);

	if (*buf == '0') {

		mt_enable = 0;

	} else if (*buf == '1') {

		/*
		 * Do not allow restarting a trace
		 */
		if (mt_trc_not_empty) {
			ret = -EPERM;
			goto out;
		}
		mt_trc_not_empty = 1;

		/*
		 * Put start times in trace.
		 * Get local mt_trc[CPU]->start_sched_clock,
		 * then on each other cpu.
		 */
		mt_get_sched_clock(NULL);

		preempt_disable();
		smp_call_function(mt_get_sched_clock, NULL, 1);
		preempt_enable();

		mt_enable = 1;

		mt_log_comm();

	} else {
		ret = -EINVAL;
		goto out;
	}

out:
	rt_mutex_unlock(&mt_trace_mutex);

	return ret;
}

static ssize_t mt_mark_write(struct file *filp, const char __user *ubuf,
				size_t cnt, loff_t *ppos)
{
	char buf[18];
	unsigned long mark;
	int ret;

	/* "- 1" to leave room to append buf with '\0' */
	if (cnt > sizeof(buf) - 1)
		return -EINVAL;

	if (copy_from_user(&buf, ubuf, cnt))
		return -EFAULT;

	buf[cnt] = '\0';

	ret = strict_strtoul(buf, 0, &mark);
	if (ret)
		return ret;

	/*
	 * If trace is not active then mt_trace_mark() will ignore request.
	 * No error is returned to user space.
	 */
	mt_trace_mark(mark);

	return cnt;
}

static ssize_t mt_reset_write(struct file *filp, const char __user *ubuf,
				size_t cnt, loff_t *ppos)
{
	char buf[2];

	if (cnt > sizeof(buf))
		return -EINVAL;

	if (copy_from_user(&buf, ubuf, cnt))
		return -EFAULT;

	if (*buf == '0')
		mt_init_data();
	else
		return -EINVAL;

	return cnt;
}

static int mt_ctl_open(struct inode *inode, struct file *filp)
{
	filp->private_data = inode->i_private;

	return 0;
}

static const struct file_operations mt_trace_fops = {
	/*
	 * If .llseek is desired, then mt_start() needs to be
	 * enhanced.
	 * .llseek = seq_lseek,
	 */
	.read    = seq_read,
	.open    = mt_seq_open,
	.release = seq_release,
};

static const struct file_operations mt_enable_fops = {
	.read    = mt_enable_read,
	.write   = mt_enable_write,
	.open    = mt_ctl_open,
};

static const struct file_operations mt_mark_fops = {
	.write   = mt_mark_write,
	.open    = mt_ctl_open,
};

static const struct file_operations mt_reset_fops = {
	.write   = mt_reset_write,
	.open    = mt_ctl_open,
};

static __init int mt_debugfs_init(void)
{
	struct dentry *dentry;
	struct dentry *dentry_dir;
	char *dir_name = "migration_trace";
	char *file;

	dentry_dir = debugfs_create_dir(dir_name, NULL);
	if (!dentry_dir) {
		pr_warning("create debugfs directory '%s' failed\n",
			   dir_name);
	} else {
		file = "enable";
		dentry = debugfs_create_file(file, 0664, dentry_dir,
						NULL, &mt_enable_fops);
		if (!dentry)
			pr_warning("create debugfs file '%s/%s' failed\n",
				   dir_name, file);

		file = "mark";
		dentry = debugfs_create_file(file, 0664, dentry_dir,
						NULL, &mt_mark_fops);
		if (!dentry)
			pr_warning("create debugfs file '%s/%s' failed\n",
				   dir_name, file);

		file = "reset";
		dentry = debugfs_create_file(file, 0664, dentry_dir,
						NULL, &mt_reset_fops);
		if (!dentry)
			pr_warning("create debugfs file '%s/%s' failed\n",
				   dir_name, file);

		file = "trace";
		dentry = debugfs_create_file(file, 0444, dentry_dir,
						NULL, &mt_trace_fops);
		if (!dentry)
			pr_warning("create debugfs file '%s/%s' failed\n",
				   dir_name, file);
	}

	return 0;
}

static __init int mt_init(void)
{
	mt_init_data();

	mt_debugfs_init();

	return 0;
}
late_initcall(mt_init);

/*
 * ----- end debug fs related functions
 */


#endif /* CONFIG_SNSC_LITE_MIGRATION_TRACE */

/* __________________________________________________________________________ */


#if defined(CONFIG_SNSC_LITE_MIGRATION_STAT) \
 || defined(CONFIG_SNSC_LITE_MIGRATION_TRACE)

/*
 * The functions that add trace data do not use the mutex to protect access
 * to mstat_enable and mt_enable (to avoid the mutex overhead).  This creates
 * two __extremely__ unlikely races:
 *
 *   1) The trace could be stopped while a trace entry is being created.
 *      Although __extremely__ unlikely, mt_show() could then attempt to report
 *      a partially completed event.  This would just result in an incorrectly
 *      reported event.
 *   2) mt_init_data() could reset the trace data structure, the adding of
 *      trace data could continue after the reset completes.  A subsequent
 *      trace start could then attempt to write an entry that overlaps.
 *      This scenario is even more unlikely.
 */

#ifdef CONFIG_SNSC_LITE_MIGRATION_STAT

#define MSTAT_LOCATION(LOC)                                                    \
	if (mstat_enable) {                                                    \
		int cpu = raw_smp_processor_id();                              \
		int now;                                                       \
		unsigned long long sc_now = sched_clock();                     \
		now = (sc_now - mstat_start_sched_clock) / period_nsec;        \
									       \
		if (now < MAX_PERIOD) {                                        \
			if (LOC_NUM(MT_EVENT_LOCATION_##LOC) <=                \
			    MT_EVENT_LOCATION_MAX_CAUSE) {                     \
				int loc = LOC_NUM(MT_EVENT_LOCATION_##LOC) - 1;\
				local_inc(&mstat[cpu].cause_cnt[now][loc]);    \
			}                                                      \
		} else {                                                       \
			mstat_enable = 0;                                      \
		}                                                              \
	}

#else

#define MSTAT_LOCATION(LOC)

#endif


#ifdef CONFIG_SNSC_LITE_MIGRATION_TRACE

#define MT_LOCATION(LOC)                                                       \
	{                                                                      \
	int old_cpu = task_cpu(p);                                    \
	if (mt_enable && (old_cpu != new_cpu)) {                               \
		struct mt_event_migrate *event;                                \
		int event_len = sizeof(*event);                                \
									       \
		event = mt_reserve_buf(event_len, MT_EVENT_LOCATION_##LOC, p); \
		if (event) {                                                   \
			event->old_cpu = old_cpu;                              \
			event->new_cpu = new_cpu;                              \
		}                                                              \
	}                                                                      \
	}

#else

#define MT_LOCATION(LOC)

#endif


#define MSTAT_MT_LOCATION(LOC)                                                 \
void mt_##LOC(struct task_struct *p, int new_cpu)                              \
{                                                                              \
	MSTAT_LOCATION(LOC);                                                   \
	MT_LOCATION(LOC);                                                      \
}

MSTAT_MT_LOCATION(__migrate_task)
MSTAT_MT_LOCATION(pull_one_task)
MSTAT_MT_LOCATION(pull_rt_task)
MSTAT_MT_LOCATION(pull_task)
MSTAT_MT_LOCATION(push_rt_task)
MSTAT_MT_LOCATION(sched_fork)
MSTAT_MT_LOCATION(try_to_wake_up)

#endif /* CONFIG_SNSC_LITE_MIGRATION_STAT || CONFIG_SNSC_LITE_MIGRATION_TRACE */
