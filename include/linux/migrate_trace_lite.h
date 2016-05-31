#ifndef _LINUX_MIGRATE_TRACE_LITE_H
#define _LINUX_MIGRATE_TRACE_LITE_H


/*
 * Functions to trace process migration
 */


#if defined(CONFIG_SNSC_LITE_MIGRATION_TRACE)

extern void mt_copy_process(struct task_struct *p);
extern void mt_set_task_comm(struct task_struct *p);

#else

#define mt_copy_process(p)             do {} while (0)
#define mt_set_task_comm(p)            do {} while (0)

#endif


#if defined(CONFIG_SNSC_LITE_MIGRATION_TRACE) \
 || defined(CONFIG_SNSC_LITE_MIGRATION_STAT)

extern void mt___migrate_task(struct task_struct *p, int new_cpu);
extern void mt_migrate_task(struct task_struct *p, int new_cpu);
extern void mt_pull_one_task(struct task_struct *p, int new_cpu);
extern void mt_pull_rt_task(struct task_struct *p, int new_cpu);
extern void mt_pull_task(struct task_struct *p, int new_cpu);
extern void mt_push_rt_task(struct task_struct *p, int new_cpu);
extern void mt_sched_fork(struct task_struct *p, int new_cpu);
extern void mt_try_to_wake_up(struct task_struct *p, int new_cpu);

/*
 * Functions for experiments, not normally used by any code.
 */

extern void mt_trace_mark(unsigned long mark);
extern int mt_trace_enable(int enable, int allow_restart);


#else

#define mt___migrate_task(p, new_cpu)  do {} while (0)
#define mt_migrate_task(p, new_cpu)    do {} while (0)
#define mt_pull_one_task(p, new_cpu)   do {} while (0)
#define mt_pull_rt_task(p, new_cpu)    do {} while (0)
#define mt_pull_task(p, new_cpu)       do {} while (0)
#define mt_push_rt_task(p, new_cpu)    do {} while (0)
#define mt_sched_fork(p, new_cpu)      do {} while (0)
#define mt_try_to_wake_up(p, new_cpu)  do {} while (0)

#define mt_trace_mark(mark)                     do {} while (0)
#define mt_trace_enable(enable, allow_restart)  do {} while (0)

#endif


#endif /* _LINUX_MIGRATE_TRACE_LITE_H */
