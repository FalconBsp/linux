/*
 * PARTNER-Jet Linux support patch by KMC
 *
 * 2012.06.10 ver3.0-beta
 */

#include <linux/sched.h>
#include <linux/module.h>

extern void __kmc_do_exit__(struct task_struct *);
extern void __kmc_schedule__(struct task_struct *, struct task_struct *);

#ifdef CONFIG_KMC_MODULE_DEBUG
extern void __kmc_mod_init_brfore__(struct module *);
extern void __kmc_mod_init_after__(struct module *, int);
extern void __kmc_mod_exit__(struct module *);
extern void __kmc_chk_mod_sec__(struct module *, char *, void *);
extern void __kmc_set_mod_sec(struct module *);
#endif

#ifdef CONFIG_KMC_NO_USER_PATCH
extern void __kmc_copy_supcode(unsigned long);
#endif

#if defined(CONFIG_KMC_EVENTTRACKER_SUPPORT) || defined(CONFIG_KMC_NO_USER_PATCH)
extern void __kmc_make_fork_tbl(struct task_struct *, int , unsigned long);
#endif

#ifdef CONFIG_KMC_NOTIFY_UPDATE_CONTEXTID
extern void __kmc_notify_update_contextid(struct mm_struct *, u64);
#endif


#ifdef CONFIG_KMC_EVENTTRACKER_SUPPORT

extern void __kmc_linux_event_set_logI(int evtid, int pid, int param);
extern void __kmc_linux_event_set_logS(int evtid, int pid, const char* pname);
extern void __kmc_linux_event_set_logT(int evtid, struct task_struct* t);
extern void __kmc_linux_event_set_Pname(int pid, const char* pname);

#define	__KMC_LINUX_EVENT_SET_LOG_I(a, b, c)	__kmc_linux_event_set_logI(a, b, c)
#define	__KMC_LINUX_EVENT_SET_LOG_S(a, b, c)	__kmc_linux_event_set_logS(a, b, c)
#define	__KMC_LINUX_EVENT_SET_LOG_T(a, b)	__kmc_linux_event_set_logT(a, b)
#define	__KMC_LINUX_EVENT_SET_PNAME(a, b)	__kmc_linux_event_set_Pname(a, b)

#else

#define __KMC_LINUX_EVENT_SET_LOG_I(a, b, c)
#define __KMC_LINUX_EVENT_SET_LOG_S(a, b, c)
#define	__KMC_LINUX_EVENT_SET_LOG_T(a, b)
#define	__KMC_LINUX_EVENT_SET_PNAME(a, b)

#endif
