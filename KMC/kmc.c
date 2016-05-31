/*
 * PARTNER-Jet Linux support patch by KMC
 *
 * 2012.06.10 ver3.0-beta
 */

#include <linux/pagemap.h>
#include <linux/spinlock.h>
#include <linux/version.h>
#include <linux/sched.h>
#include <linux/mman.h>
#include <linux/syscalls.h>
#include <asm/uaccess.h>
#include <asm/param.h>
#include <asm/page.h>

#include <linux/kmc.h>

#include "kmc-evtr.h"
#include "__brk_code.h"



#ifdef noinline
#undef noinline
#define noinline        noinline
#endif
#define	__KMC_DEBUG_FUNC_ATTR	__attribute__ ((noinline))
// #define	__KMC_DEBUG_FUNC_ATTR	__attribute__ ((aligned (4), noinline))


__KMC_DEBUG_FUNC_ATTR
void __kmc_do_exit(void)
{
	__asm__("nop");

}

#ifdef CONFIG_KMC_PARTNER_COLLECT_THREAD_OFF

#define __KMC_MAX_PT_COUNT	32

struct task_struct *__kmc_tss_list[__KMC_MAX_PT_COUNT + 1];

void __kmc_do_exit__(struct task_struct *tsk)
{
	int	i;

	__KMC_LINUX_EVENT_SET_LOG_T(KMC_LINUX_EVENTID_EXIT, tsk);

	if (__kmc_tss_list[0] != 0) {
		for (i = 1; i < (__KMC_MAX_PT_COUNT + 1); ++i) {
			if (__kmc_tss_list[i] == tsk) {
				__kmc_tss_list[0] = (struct task_struct *)i;
				__kmc_do_exit();
			}
		}
	}
	return;
}
#else

#define	__KMC_MAX_THREAD_COUNT	256
#define	__KMC_MAX_PT_COUNT	4

struct task_struct *__kmc_tss_list_array[__KMC_MAX_PT_COUNT + 1][__KMC_MAX_THREAD_COUNT];

void __kmc_do_exit__(struct task_struct *tsk)
{
	int	i;

	__KMC_LINUX_EVENT_SET_LOG_T(KMC_LINUX_EVENTID_EXIT, tsk);

	if (__kmc_tss_list_array[0][0] != 0) {
#ifdef CONFIG_SMP
		static DEFINE_RAW_SPINLOCK(__kmc_do_exit_lock);
		raw_spin_lock(&__kmc_do_exit_lock);
#endif
		for (i = 1; i < (__KMC_MAX_PT_COUNT + 1); ++i) {
			int	j;
			struct task_struct **tpp;
			struct task_struct *tp;

			tpp = __kmc_tss_list_array[i];
			for (j = 0; j < __KMC_MAX_THREAD_COUNT; ++j) {
				tp = *(tpp++);
				if (tp == 0) break;
				if (tp == tsk) {
					while (j < (__KMC_MAX_THREAD_COUNT - 1)) {
						if (*tpp == 0) break;
						*(tpp - 1) = *tpp;
						*tpp = tsk;
						++tpp;
						++j;
					}
					__kmc_tss_list_array[0][0] = (struct task_struct *)i;
					__kmc_tss_list_array[0][1] = (struct task_struct *)j;
					__kmc_do_exit();
					i = (__KMC_MAX_PT_COUNT + 1);
					break;
				}
			}
		}
#ifdef CONFIG_SMP
		raw_spin_unlock(&__kmc_do_exit_lock);
#endif
	}
	return;
}
#endif



#define KMC_MAX_SHEDULE_LIST    1024

#ifdef CONFIG_SMP

#define	CORE_SMP_NUMBER	CONFIG_NR_CPUS
int __kmc_schedules_list_pid_smp[CORE_SMP_NUMBER][KMC_MAX_SHEDULE_LIST];
int __kmc_schedules_index_smp[CORE_SMP_NUMBER];
int __kmc_schedules_index_max = KMC_MAX_SHEDULE_LIST;

#else

int __kmc_schedules_index_max = KMC_MAX_SHEDULE_LIST;
int __kmc_schedules_index;
int __kmc_schedules_list_pid[KMC_MAX_SHEDULE_LIST];

#endif

#ifdef CONFIG_MN10300
int __kmc_am33_trace = 0;
#endif

#ifdef CONFIG_KMC_USE_BT
int  __kmc_schedule_trace_point;
void __kmc_code_data(int);
#ifdef CONFIG_ARM
#include	"kmc_dt_arm.c"
#endif
#ifdef  CONFIG_MIPS
#include	"kmc_dt_mips.c"
#endif
#ifdef CONFIG_SUPERH
#include	"kmc_dt_sh.c"
#endif
#ifdef CONFIG_MN10300
#include	"kmc_dt_am33.c"
#endif
#endif

#ifdef CONFIG_KMC_USE_BT
static DEFINE_RAW_SPINLOCK(__kmc_linux_schedule_lock);
#endif

__KMC_DEBUG_FUNC_ATTR
void __kmc_schedule(struct task_struct *prev,struct task_struct *next)
{
	int	index_next;
#ifdef CONFIG_SMP
    	int	cpu;

	cpu = raw_smp_processor_id();
	index_next=__kmc_schedules_index_smp[cpu] & (KMC_MAX_SHEDULE_LIST-1);
	++__kmc_schedules_index_smp[cpu];
	#ifdef CONFIG_KMC_USE_BT
		__kmc_code_data(__kmc_schedule_trace_point=__kmc_schedules_list_pid_smp[cpu][index_next]=next->pid);
	#else
		__kmc_schedules_list_pid_smp[cpu][index_next]=next->pid;
	#endif
#else

	index_next=__kmc_schedules_index & (KMC_MAX_SHEDULE_LIST-1);
	++__kmc_schedules_index;

	#ifdef CONFIG_KMC_USE_BT
    		__kmc_code_data(__kmc_schedule_trace_point=__kmc_schedules_list_pid[index_next]=next->pid);
	#else
		__kmc_schedules_list_pid[index_next]=next->pid;
	#endif
#endif
#ifdef CONFIG_MN10300
	if(__kmc_am33_trace==7){
		*(char *)(0xC00001C3) |= 0x01;
	}
#endif
}

void (*__kmc_schedule_call)(struct task_struct *,struct task_struct *)=__kmc_schedule;

void __kmc_schedule__(struct task_struct *prev,struct task_struct *next)
{
#ifdef CONFIG_KMC_USE_BT
	unsigned long flags;

	raw_spin_lock_irqsave(&__kmc_linux_schedule_lock, flags);
#endif

	__kmc_schedule_call(prev, next);

	__KMC_LINUX_EVENT_SET_LOG_I( KMC_LINUX_EVENTID_TASKSWITCH, prev->pid, next->pid );

#ifdef CONFIG_KMC_USE_BT
	raw_spin_unlock_irqrestore(&__kmc_linux_schedule_lock, flags);
#endif
}



/* This code is for thread debugging to realize delayed attach */
void __kmc_delayed_attach(void) {
	asm(".global __kmc_thr_debug_area");
	asm("__kmc_thr_debug_area:");
	__KMC_BRK_CODE();
	asm (" .long  0x4c434dad");
}
void (*__kmc_delayed_attach_tmp)(void) = __kmc_delayed_attach;

#ifdef CONFIG_KMC_NO_USER_PATCH
#define __KMC_EXEC(a, b, c)	__kmc_exec(a, b, c)
#define	__KMC_INJECT_SUPCODE(a)	__kmc_inject_supcode(a)
#else
#define __KMC_EXEC(a, b, c)
#define	__KMC_INJECT_SUPCODE(a)
#endif

#if defined(CONFIG_KMC_EVENTTRACKER_SUPPORT) || defined(CONFIG_KMC_NO_USER_PATCH)

#ifdef CONFIG_KMC_NO_USER_PATCH
#define TARGET_TABLE_MAX	16+1
char __kmc_target_process_name[TARGET_TABLE_MAX][128];

__KMC_DEBUG_FUNC_ATTR
void ___kmc_exec(char *comm, struct task_struct *taskp) {
	__KMC_BRK_CODE();
	asm (" .long  0x4c434daa");
	asm (" nop");
}

__KMC_DEBUG_FUNC_ATTR
void ___kmc_thread_start(struct task_struct *taskp, char *name) {
	__KMC_BRK_CODE();
	asm (" .long  0x4c434d11");
	asm (" nop");
}

extern void __kmc_start_debuger(struct task_struct *, char *);

void __kmc_exec(char *comm, struct task_struct *taskp, int isthread)
{
    int i;

	if (isthread) {		// isthread:1 -> NPTL, isthread:2 -> linuxthreads
		for (i = 1; i < __KMC_MAX_PT_COUNT; i++) {
			struct task_struct *t;
#ifdef CONFIG_KMC_PARTNER_COLLECT_THREAD_OFF
			r = __kmc_tss_list[i];
#else
			t = __kmc_tss_list_array[i][0];
#endif
			if (NULL != t && (struct task_struct *)0xffffffff != t) {
				if (1 == isthread) {	// NPTL
					if (taskp->tgid == t->tgid) {
						___kmc_thread_start(taskp, NULL);
						break;
					}
				} else {		// linuxthreads
					if (taskp->mm == t->mm) {
						___kmc_thread_start(taskp, NULL);
						break;
					}
				}
			}
		}
	} else {
    		for (i = 0; i < TARGET_TABLE_MAX; i++) {
        		if (strncmp(__kmc_target_process_name[i], comm, strlen(comm)) == 0) {
				unsigned long start_code = (unsigned long)taskp->mm->start_code;
				unsigned long end_code = (unsigned long)taskp->mm->end_code;
				char dummy;
				while (end_code > start_code) {
		    			volatile char __user *hit = (char *)start_code;
		    			dummy = *hit;
		    			start_code += PAGE_SIZE;
				}
				___kmc_exec(comm, taskp);
				break;
			}
		}
	}
}

extern char	*__kmc_sup_start;
extern int	__kmc_sup_size;

void __kmc_inject_supcode(struct task_struct *task)
{

	char *usrcode = (char *)(task->mm->start_brk - PAGE_SIZE);
	access_process_vm(task, (unsigned long)usrcode, (void *)__kmc_sup_start, __kmc_sup_size, 1);

	return;
}

void __kmc_copy_supcode(unsigned long elf_brk)
{
	char __user *usrcode;
	int	ret;

	if (__kmc_sup_size) {
		usrcode = (char *)(PAGE_ALIGN(elf_brk) - PAGE_SIZE);
		ret = copy_to_user(usrcode, __kmc_sup_start, __kmc_sup_size);
		flush_icache_range((unsigned long)usrcode, (unsigned long)(usrcode + __kmc_sup_size));
		sys_mlock((unsigned long)usrcode, PAGE_SIZE);
		sys_mprotect((unsigned long)usrcode, PAGE_SIZE, PROT_READ | PROT_WRITE | PROT_EXEC);
	}
	return;

}

asmlinkage void __kmc_mlock(void)
{
	if (current->mm) {
		if (current->mm->start_brk) {
			sys_mlock(current->mm->start_brk - PAGE_SIZE, PAGE_SIZE);
			sys_mprotect(current->mm->start_brk - PAGE_SIZE, PAGE_SIZE, PROT_READ | PROT_WRITE | PROT_EXEC);
		}
	}
}

#endif // CONFIG_KMC_NO_USER_PATCH

// mode;		0.. fork,clone , 1... execv
// param;		mode = 0, clone_flags, mode = 1, filename
void __kmc_make_fork_tbl(struct task_struct *task_p, int mode, unsigned long param)
{
	if(mode==0){
		/* fork,clone */
		if (task_p->mm == 0) {
			// kernel thread
			__KMC_LINUX_EVENT_SET_LOG_I( KMC_LINUX_EVENTID_NEWTHREAD, task_p->pid, 0 );
		} else if ((param & CSIGNAL) != SIGCHLD) {
			// user thread
			int thread_mode = 2;		// 1 = NPTL, 2 = linuxthreads
			if (task_p == task_p->group_leader) {
				thread_mode = 2;	// linux_threads
			} else {
				thread_mode = 1;	// NPTL
			}
			if (1 == thread_mode) {
				// NPTL
				__KMC_LINUX_EVENT_SET_LOG_I( KMC_LINUX_EVENTID_NEWTHREAD, task_p->pid, task_p->group_leader->pid );
			} else {
				// linuxthreads
				__KMC_LINUX_EVENT_SET_LOG_I( KMC_LINUX_EVENTID_NEWTHREAD, task_p->pid, task_p->parent->pid );
			}
			__KMC_EXEC(task_p->comm, task_p, thread_mode);
		} else {
			// user process
			__KMC_LINUX_EVENT_SET_LOG_S( KMC_LINUX_EVENTID_FORK, task_p->pid, task_p->comm );
 			__KMC_INJECT_SUPCODE(task_p);
		}
	} else{
		/* execv */
		__KMC_LINUX_EVENT_SET_LOG_S( KMC_LINUX_EVENTID_EXEC, current->pid, (char *)param);
		__KMC_EXEC(task_p->comm, task_p, 0);
	}
	return;
}
#endif	// defined(CONFIG_KMC_EVENTTRACKER_SUPPORT) || defined(CONFIG_KMC_NO_USER_PATCH)

#ifdef CONFIG_KMC_NOTIFY_UPDATE_CONTEXTID

#ifdef CONFIG_SMP
#define __KMC_MAX_CORE_COUNT            CONFIG_NR_CPUS
#else
#define __KMC_MAX_CORE_COUNT            1
#endif
#define __KMC_MAX_HBRK_CID_COUNT        8

volatile unsigned int __kmc_hbrk_cid[__KMC_MAX_CORE_COUNT][__KMC_MAX_HBRK_CID_COUNT];

void __kmc_notify_update_contextid(struct mm_struct *mm, u64 asid)
{
	int	i, j, flg = 0;

	for (i = 0; i < __KMC_MAX_CORE_COUNT; i++) {
		for (j = 0; j < __KMC_MAX_HBRK_CID_COUNT; j++) {
			if (__kmc_hbrk_cid[i][j] == 0) {
				break;
			} else {
				u64	old_asid;
				old_asid = atomic64_read(&mm->context.id);
				if (__kmc_hbrk_cid[i][j] == (int)old_asid) {
					__kmc_hbrk_cid[i][j] = (int)asid;
					flg = 1;
				}
			}
		}
	}
	if (flg) {
		__KMC_BRK_CODE();
		asm(" .long   0x4c434df0");
		asm(" nop");
	}
#if 0
	if (atomic64_read(&mm->context.id) != 0) {
		printk("contextid changed: %08llx -> %08llx\n",atomic64_read(&mm->context.id), asid);
	}
#endif

	return;
}

#endif
