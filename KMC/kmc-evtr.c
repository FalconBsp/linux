/*
 * PARTNER-Jet Linux support patch by KMC
 *
 * 2012.06.10 ver3.0-beta
 */

#include <linux/spinlock.h>
#include <linux/version.h>
#include <linux/sched.h>
#include <linux/mman.h>
#include <linux/kmc.h>
#include <linux/module.h>
#include <linux/vmalloc.h>
#include <linux/printk.h>
#include <linux/io.h>
#include <asm/io.h>
#include <asm/delay.h>

#include "kmc-evtr.h"

#define KMC_LINUX_EVENTLOG_VERSION	(0x00000003)


#ifdef CONFIG_KMC_EVENTTRACKER_RTM
#define	KMC_LINUX_EVENTLOG_MAXLOG	0
#else
#define KMC_LINUX_EVENTLOG_MAXLOG	(CONFIG_KMC_EVENTTRACKER_LOG_SIZE * 1024)	// must be 2^n
#endif
#define KMC_LINUX_EVENTLOG_MAXPNAME	(CONFIG_KMC_EVENTTRACKER_PNAME_SIZE * 256)	// must be 2^n
#define KMC_LINUX_EVENTLOG_MAXPNAMELENGTH	(32)
#define	KMC_LINUX_EVENTLOG_MAXTHREADINFO	(1024)	// must be 2^n

typedef struct {
	u64 evtTime;
	int evtId;
	int evtPid;
	int evtParam;
#ifdef CONFIG_SMP
	int cpuId;
#endif
} T_KMC_LINUX_EVENTLOG;

typedef struct {
	int	pid;
	char	name[KMC_LINUX_EVENTLOG_MAXPNAMELENGTH];
} T_KMC_LINUX_PNAME;

typedef struct {
	unsigned short	thid;
	unsigned short	pid;
} T_KMC_LINUX_THINFO;

typedef struct {
	int	version;
	int	ticks_per_10msec;

	/* EventLog Info. */
	int	log_offset;
	int	log_max;
	int	log_size;
	int	log_index;

	/* Process Name Table */
	int	pname_offset;
	int	pname_max;
	int	pname_size;
	int	pname_index;

	/* Thread Info Table */
	int	thinfo_offset;
	int	thinfo_max;
	int	thinfo_size;
	int	thinfo_index;

	/* EventLog */
	T_KMC_LINUX_EVENTLOG	log[KMC_LINUX_EVENTLOG_MAXLOG];

	/* Process Name Table */
	T_KMC_LINUX_PNAME	pname[KMC_LINUX_EVENTLOG_MAXPNAME];

	/* Thread Info Table */
	T_KMC_LINUX_THINFO	thinfo[KMC_LINUX_EVENTLOG_MAXTHREADINFO];

#ifdef CONFIG_KMC_EVENTTRACKER_THNAME
	/* Thread Name Table */
	T_KMC_LINUX_THNAME	thname[KMC_LINUX_EVENTLOG_MAXTHNAME];
#endif
} T_KMC_LINUX_LOGAREA;

static DEFINE_RAW_SPINLOCK(__kmc_linux_event_lock);

T_KMC_LINUX_LOGAREA	*__kmc_linux_event_data = NULL;
T_KMC_LINUX_LOGAREA    *__kmc_linux_event_allocdata = NULL;
unsigned long		__kmc_linux_event_data_size = sizeof(T_KMC_LINUX_LOGAREA);
int __kmc_linux_event_errcount;

#ifdef CONFIG_KMC_EVT_USE_ARM_GLOBAL_TIMER
static void __iomem *arm_a9_timer_base = 0;
#define	GLOBAL_TIMER_COUNTER_LOW	0x0
#define	GLOBAL_TIMER_COUNTER_HIGH	0x4
#define	GLOBAL_TIMER_CONTROL		0x8

static inline unsigned long armv7_read_configbase(void)
{
	u32	val;
	asm volatile("mrc p15, 4, %0, c15, c0, 0" : "=r"(val));
	return val;
}

static unsigned long __init_arm_global_timer(void)
{
	unsigned long	tmL1, tmL2, tmH1, tmH2, ctl, ticks_per_10msec;
	unsigned long	arm_a9_timer_base_phy;

	arm_a9_timer_base_phy = armv7_read_configbase();
	if (0 != arm_a9_timer_base_phy) {
		arm_a9_timer_base = ioremap_nocache(arm_a9_timer_base_phy + 0x200, 0x100);
	} else {
		arm_a9_timer_base = 0;
	}

	if (arm_a9_timer_base) {
		ctl = __raw_readl(arm_a9_timer_base + GLOBAL_TIMER_CONTROL);
		if (0 == (ctl & 1)) {
			__raw_writel(ctl | 1, arm_a9_timer_base + GLOBAL_TIMER_CONTROL);
		}
		tmH1 = __raw_readl(arm_a9_timer_base + GLOBAL_TIMER_COUNTER_HIGH);
		tmL1 = __raw_readl(arm_a9_timer_base + GLOBAL_TIMER_COUNTER_LOW);
		udelay(100);
		tmL2 = __raw_readl(arm_a9_timer_base + GLOBAL_TIMER_COUNTER_LOW);
		tmH2 = __raw_readl(arm_a9_timer_base + GLOBAL_TIMER_COUNTER_HIGH);
		if (tmH2 != tmH1) {
			unsigned long tmH = tmH2 - tmH1;
			ticks_per_10msec = tmL2 + (0xffffffff - tmL1) + (0xffffffff * (tmH - 1));
			ticks_per_10msec = ticks_per_10msec * 100;
		} else {
			ticks_per_10msec = (tmL2- tmL1) * 100;
		}
		printk(KERN_INFO "KMC: EventTracker, ARM global timer enable, addr=0x%pK, ticks/10ms=%lu(0x%lx)\n", arm_a9_timer_base, ticks_per_10msec, ticks_per_10msec);
	} else {
#ifdef CONFIG_HZ
		ticks_per_10msec = CONFIG_HZ / 100;
#else
		ticks_per_10msec = 10;
#endif
	}

	return ticks_per_10msec;
}

static inline u64 __get_arm_global_timer(void)
{
	u64	evtTime;

	if (arm_a9_timer_base) {
		evtTime = __raw_readl(arm_a9_timer_base + GLOBAL_TIMER_COUNTER_HIGH);
		evtTime = evtTime << 32;
		evtTime = evtTime | __raw_readl(arm_a9_timer_base + GLOBAL_TIMER_COUNTER_LOW);
	} else {
		evtTime = jiffies_64;
	}

	return evtTime;
}
#endif

#include <linux/module.h>
#include <linux/init.h>

static int __init allocate_init(void)
{
	int retval=0;
	__kmc_linux_event_allocdata = vmalloc(sizeof(T_KMC_LINUX_LOGAREA));
	if(__kmc_linux_event_allocdata == NULL)
	{
		printk(KERN_ERR "Failed to allocate memory for KMC event data \n");
		retval = -ENOMEM;
	}
	return retval;
}
subsys_initcall(allocate_init);

void __kmc_linux_event_log_init(void)
{
#ifdef CONFIG_SMP
	unsigned long flags;
	raw_spin_lock_irqsave(&__kmc_linux_event_lock, flags);
	if (!__kmc_linux_event_data) {
#endif

#if defined(CONFIG_CPU_SUBTYPE_SH7757)
	__kmc_linux_event_data = kmalloc(sizeof(T_KMC_LINUX_LOGAREA), GFP_ATOMIC);
#else
	__kmc_linux_event_data = __kmc_linux_event_allocdata;
#endif
	if (__kmc_linux_event_data) {
		memset(__kmc_linux_event_data, 0, sizeof(T_KMC_LINUX_LOGAREA));
		__kmc_linux_event_data->version = KMC_LINUX_EVENTLOG_VERSION;
#if defined CONFIG_KMC_EVT_USE_ARM_GLOBAL_TIMER
		__kmc_linux_event_data->ticks_per_10msec = __init_arm_global_timer();
#elif defined CONFIG_MACH_MX3KZ
		__kmc_linux_event_data->ticks_per_10msec = 105472;		/* LATCH */
#else
#ifdef CONFIG_HZ
		__kmc_linux_event_data->ticks_per_10msec = CONFIG_HZ / 100;	/* jiffies */
#else
		__kmc_linux_event_data->ticks_per_10msec = 10000000;		/* nsec to 10ms */
#endif
#endif
		__kmc_linux_event_data->log_offset 	= (int)&__kmc_linux_event_data->log[0] - (int)&__kmc_linux_event_data->version;
		__kmc_linux_event_data->log_max		= KMC_LINUX_EVENTLOG_MAXLOG;
		__kmc_linux_event_data->log_size	= (int)&__kmc_linux_event_data->log[1] - (int)&__kmc_linux_event_data->log[0];
#ifdef CONFIG_KMC_EVENTTRACKER_RTM
		__kmc_linux_event_data->log_index	= 1;
#endif
		__kmc_linux_event_data->pname_offset	= (int)&__kmc_linux_event_data->pname[0] - (int)&__kmc_linux_event_data->version;
		__kmc_linux_event_data->pname_max	= KMC_LINUX_EVENTLOG_MAXPNAME;
		__kmc_linux_event_data->pname_size	= (int)&__kmc_linux_event_data->pname[1] - (int)&__kmc_linux_event_data->pname[0];
		__kmc_linux_event_data->pname_index     = KMC_LINUX_EVENTLOG_MAXPNAME;

		__kmc_linux_event_data->thinfo_offset	= (int)&__kmc_linux_event_data->thinfo[0] - (int)&__kmc_linux_event_data->version;
		__kmc_linux_event_data->thinfo_max	= KMC_LINUX_EVENTLOG_MAXTHREADINFO;
		__kmc_linux_event_data->thinfo_size	= (int)&__kmc_linux_event_data->thinfo[1] - (int)&__kmc_linux_event_data->thinfo[0];
	}

#ifdef CONFIG_SMP
	}
	raw_spin_unlock_irqrestore(&__kmc_linux_event_lock, flags);
#endif

	return;
}

#ifdef CONFIG_KMC_EVENTTRACKER_RTM
void __kmc_linux_event_set_logI(int evtid, int pid, int param)
{
	if (!__kmc_linux_event_data) __kmc_linux_event_log_init();

	if ((__kmc_linux_event_data)&&(evtid == KMC_LINUX_EVENTID_NEWTHREAD)) {
		unsigned long flags;
		int thindex;

		raw_spin_lock_irqsave(&__kmc_linux_event_lock, flags);
		thindex = (__kmc_linux_event_data->thinfo_index++) & (KMC_LINUX_EVENTLOG_MAXTHREADINFO - 1);
		raw_spin_unlock_irqrestore(&__kmc_linux_event_lock, flags);
		__kmc_linux_event_data->thinfo[thindex].thid = (unsigned short)pid;
		__kmc_linux_event_data->thinfo[thindex].pid  = (unsigned short)param;
	}
}
#else
void __kmc_linux_event_set_logI(int evtid, int pid, int param)
{
	if (!__kmc_linux_event_data) __kmc_linux_event_log_init();

	if (__kmc_linux_event_data) {
		T_KMC_LINUX_EVENTLOG *pLog;
		unsigned long flags;
		int	index;
		u64	evtTime;
		int	thindex;
		unsigned int cpuid;

		thindex = KMC_LINUX_EVENTLOG_MAXTHREADINFO;

		cpuid = raw_smp_processor_id();
		raw_spin_lock_irqsave(&__kmc_linux_event_lock, flags);

#if defined CONFIG_KMC_EVT_USE_ARM_GLOBAL_TIMER
		evtTime = __get_arm_global_timer();
#elif defined CONFIG_MACH_MX3KZ
		evtTime = __raw_readl(MXC_GPT_GPTCNT);
#else
#if LINUX_VERSION_CODE >= 0x020600
		evtTime = sched_clock();
#else
		evtTime = jiffies;
#endif
#endif
		index = (__kmc_linux_event_data->log_index++) & (KMC_LINUX_EVENTLOG_MAXLOG - 1);
		if (evtid == KMC_LINUX_EVENTID_NEWTHREAD) {
			thindex = (__kmc_linux_event_data->thinfo_index++) & (KMC_LINUX_EVENTLOG_MAXTHREADINFO - 1);
		}

		raw_spin_unlock_irqrestore(&__kmc_linux_event_lock, flags);

		pLog = &(__kmc_linux_event_data->log[index]);

		pLog->evtTime = evtTime;
		pLog->evtId = evtid;
		pLog->evtPid = pid;
		pLog->evtParam = param;
#ifdef CONFIG_SMP
		pLog->cpuId = cpuid;
#endif
		if (thindex < KMC_LINUX_EVENTLOG_MAXTHREADINFO) {
			__kmc_linux_event_data->thinfo[thindex].thid = (unsigned short)pid;
			__kmc_linux_event_data->thinfo[thindex].pid  = (unsigned short)param;
		}
	}
}
#endif
EXPORT_SYMBOL(__kmc_linux_event_set_logI);

static int RegisterPname(int pid, const char *pname)
{
	int ret = -1;

	if (!__kmc_linux_event_data) __kmc_linux_event_log_init();

	if (__kmc_linux_event_data) {
		int i;
		unsigned long flags;

		raw_spin_lock_irqsave(&__kmc_linux_event_lock, flags);

		i = (pid % KMC_LINUX_EVENTLOG_MAXPNAME);
		__kmc_linux_event_data->pname[i].pid = pid;

		raw_spin_unlock_irqrestore(&__kmc_linux_event_lock, flags);

		strncpy(__kmc_linux_event_data->pname[i].name, pname, KMC_LINUX_EVENTLOG_MAXPNAMELENGTH-1);
		ret = i;
	}
	return ret;
}

void __kmc_linux_event_set_Pname(int pid, const char* pname)
{
	RegisterPname(pid, pname);
}

void __kmc_linux_event_set_logS(int evtid, int pid, const char* pname)
{
	__kmc_linux_event_set_logI(evtid, pid, RegisterPname(pid, pname));
}

void __kmc_linux_event_set_logT(int evtid, struct task_struct* t)
{
	__kmc_linux_event_set_logI(evtid, t->pid, 0);
}

asmlinkage void __kmc_event_post(int id, int param1, int param2)
{
	__kmc_linux_event_set_logI(id, param1, param2);
}


