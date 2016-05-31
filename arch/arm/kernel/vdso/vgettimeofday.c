/*
 * Copyright 2014 Mentor Graphics Corporation.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; version 2 of the
 * License.
 *
 */

#include <linux/compiler.h>
#include <linux/hrtimer.h>
#include <linux/time.h>
#include <asm/arch_timer.h>
#include <asm/barrier.h>
#include <asm/page.h>
#include <asm/unistd.h>
#include <asm/vdso_datapage.h>

static struct vdso_data *get_datapage(void)
{
	struct vdso_data *ret;

	/* Hack to perform pc-relative load of data page */
	asm("b 1f\n"
	    ".align 2\n"
	    "2:\n"
	    ".long _vdso_data - .\n"
	    "1:\n"
	    "adr r2, 2b\n"
	    "ldr r3, [r2]\n"
	    "add %0, r2, r3\n" :
	    "=r" (ret) : : "r2", "r3");

	return ret;
}

static u32 seqcnt_acquire(struct vdso_data *vdata)
{
	u32 seq;

	do {
		seq = ACCESS_ONCE(vdata->tb_seq_count);
	} while (seq & 1);

	dmb();

	return seq;
}

static u32 seqcnt_read(struct vdso_data *vdata)
{
	dmb();

	return ACCESS_ONCE(vdata->tb_seq_count);
}

static long clock_gettime_fallback(clockid_t _clkid, struct timespec *_ts)
{
	register struct timespec *ts asm("r1") = _ts;
	register clockid_t clkid asm("r0") = _clkid;
	register long ret asm ("r0");
	register long nr asm("r7") = __NR_clock_gettime;

	asm("swi #0" : "=r" (ret) : "r" (clkid), "r" (ts), "r" (nr) : "memory");

	return ret;
}

static int do_realtime_coarse(struct timespec *ts, struct vdso_data *vdata)
{
	struct timespec copy;
	u32 seq;

	do {
		seq = seqcnt_acquire(vdata);

		copy.tv_sec = vdata->xtime_coarse_sec;
		copy.tv_nsec = vdata->xtime_coarse_nsec;
	} while (seq != seqcnt_read(vdata));

	*ts = copy;

	return 0;
}

static int do_monotonic_coarse(struct timespec *ts, struct vdso_data *vdata)
{
	struct timespec copy;
	struct timespec wtm;
	u32 seq;

	do {
		seq = seqcnt_acquire(vdata);

		copy.tv_sec = vdata->xtime_coarse_sec;
		copy.tv_nsec = vdata->xtime_coarse_nsec;
		wtm.tv_sec = vdata->wtm_clock_sec;
		wtm.tv_nsec = vdata->wtm_clock_nsec;
	} while (seq != seqcnt_read(vdata));

	copy.tv_sec += wtm.tv_sec;
	copy.tv_nsec += wtm.tv_nsec;
	if (copy.tv_nsec >= NSEC_PER_SEC) {
		copy.tv_nsec -= NSEC_PER_SEC;
		copy.tv_sec += 1;
	}

	*ts = copy;

	return 0;
}

#ifdef CONFIG_ARM_ARCH_TIMER

static int do_realtime(struct timespec *ts, struct vdso_data *vdata)
{
	unsigned long sec;
	u32 seq;
	u64 ns;

	do {
		u64 cycles;

		seq = seqcnt_acquire(vdata);

		if (vdata->use_syscall)
			return -1;

		cycles = arch_counter_get_cntvct() - vdata->cs_cycle_last;

		/* The generic timer architecture guarantees only 56 bits */
		cycles &= ~(0xff00ULL << 48);
		ns = (cycles * vdata->cs_mult) >> vdata->cs_shift;

		sec = vdata->xtime_clock_sec;
		ns += vdata->xtime_clock_nsec;

		while (ns >= NSEC_PER_SEC) {
			ns -= NSEC_PER_SEC;
			sec += 1;
		}
	} while (seq != seqcnt_read(vdata));

	ts->tv_sec = sec;
	ts->tv_nsec = ns;

	return 0;
}

static int do_monotonic(struct timespec *ts, struct vdso_data *vdata)
{
	unsigned long sec;
	u32 seq;
	u64 ns;

	do {
		u64 cycles;

		seq = seqcnt_acquire(vdata);

		if (vdata->use_syscall)
			return -1;

		cycles = arch_counter_get_cntvct() - vdata->cs_cycle_last;

		/* The generic timer architecture guarantees only 56 bits */
		cycles &= ~(0xff00ULL << 48);
		ns = (cycles * vdata->cs_mult) >> vdata->cs_shift;

		sec = vdata->xtime_clock_sec;
		ns += vdata->xtime_clock_nsec;

		sec += vdata->wtm_clock_sec;
		ns += vdata->wtm_clock_nsec;

		while (ns >= NSEC_PER_SEC) {
			ns -= NSEC_PER_SEC;
			sec += 1;
		}
	} while (seq != seqcnt_read(vdata));

	ts->tv_sec = sec;
	ts->tv_nsec = ns;

	return 0;
}

#else /* CONFIG_ARM_ARCH_TIMER */

static int do_realtime(struct timespec *ts, struct vdso_data *vdata)
{
	return -1;
}

static int do_monotonic(struct timespec *ts, struct vdso_data *vdata)
{
	return -1;
}

#endif /* CONFIG_ARM_ARCH_TIMER */

int __kernel_clock_gettime(clockid_t clkid, struct timespec *ts)
{
	struct vdso_data *vdata;
	int ret = -1;

	vdata = get_datapage();

	switch (clkid) {
	case CLOCK_REALTIME_COARSE:
		ret = do_realtime_coarse(ts, vdata);
		break;
	case CLOCK_MONOTONIC_COARSE:
		ret = do_monotonic_coarse(ts, vdata);
		break;
	case CLOCK_REALTIME:
		ret = do_realtime(ts, vdata);
		break;
	case CLOCK_MONOTONIC:
		ret = do_monotonic(ts, vdata);
		break;
	default:
		break;
	}

	if (ret)
		ret = clock_gettime_fallback(clkid, ts);

	return ret;
}

static long clock_getres_fallback(clockid_t _clkid, struct timespec *_ts)
{
	register struct timespec *ts asm("r1") = _ts;
	register clockid_t clkid asm("r0") = _clkid;
	register long ret asm ("r0");
	register long nr asm("r7") = __NR_clock_getres;

	asm volatile(
		"swi #0" :
		"=r" (ret) :
		"r" (clkid), "r" (ts), "r" (nr) :
		"memory");

	return ret;
}

int __kernel_clock_getres(clockid_t clkid, struct timespec *ts)
{
	int ret;

	switch (clkid) {
	case CLOCK_REALTIME:
	case CLOCK_MONOTONIC:
		if (ts) {
			ts->tv_sec = 0;
			ts->tv_nsec = MONOTONIC_RES_NSEC;
		}
		ret = 0;
		break;
	case CLOCK_REALTIME_COARSE:
	case CLOCK_MONOTONIC_COARSE:
		if (ts) {
			ts->tv_sec = 0;
			ts->tv_nsec = LOW_RES_NSEC;
		}
		ret = 0;
		break;
	default:
		ret = clock_getres_fallback(clkid, ts);
		break;
	}

	return ret;
}

static long gettimeofday_fallback(struct timeval *_tv, struct timezone *_tz)
{
	register struct timezone *tz asm("r1") = _tz;
	register struct timeval *tv asm("r0") = _tv;
	register long ret asm ("r0");
	register long nr asm("r7") = __NR_gettimeofday;

	asm("swi #0" : "=r" (ret) : "r" (tv), "r" (tz), "r" (nr) : "memory");

	return ret;
}

int __kernel_gettimeofday(struct timeval *tv, struct timezone *tz)
{
	struct timespec ts;
	struct vdso_data *vdata;
	int ret;

	vdata = get_datapage();

	ret = do_realtime(&ts, vdata);
	if (ret)
		return gettimeofday_fallback(tv, tz);

	if (tv) {
		tv->tv_sec = ts.tv_sec;
		tv->tv_usec = ts.tv_nsec / 1000;
	}
	if (tz) {
		tz->tz_minuteswest = vdata->tz_minuteswest;
		tz->tz_dsttime = vdata->tz_dsttime;
	}

	return ret;
}
