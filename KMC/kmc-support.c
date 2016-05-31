/*
 * PARTNER-Jet Linux support patch by KMC
 *
 * 2012.06.10 ver3.0-beta
 */

#include <asm/unistd.h>

#include "__brk_code.h"

#define __val0(x)	#x
#define __val1(x)	__val0(x)

#if defined(__arm__)

#if defined(__ARM_EABI__)			/* add 07.02.07 support eabi */
 #define        __BL_SCHED_YIELD()                                      \
		__asm__("       mov     r7,#" __val1(__NR_sched_yield));        \
		__asm__("       swi     0")
 #define        __BL_GETTID()                                           \
		__asm__("       mov     r7,#" __val1(__NR_gettid));     \
		__asm__("       swi     0")
#else
 #define	__BL_SCHED_YIELD()					\
			__asm__("	swi	" __val1(__NR_sched_yield))
 #define	__BL_GETTID()						\
			__asm__("	swi	" __val1(__NR_gettid))
#endif	// __ARM_EABI__


 #define	__KMC_BRK_CODE__()	__KMC_BRK_CODE()

 #define	__KMC_ALIGN()		__asm__("	.align	4")
 #define	__KMC_NOAT()

 #define	__KMC_SLEEP_THREAD()					\
	__asm__("	b	___ksup_sleep_thread");			\
	__asm__("	b	___ksup_mem_hit");			\
	__asm__("	b  	__kmc_start_debuger");			\
	__asm__("	.long	0x014c434d")

 #define	__KSUP_SLEEP_THREAD()					\
	__asm__("	stmfd	sp!, {r0-r12,r14}");			\
	__asm__("	mrs	r0,cpsr");				\
	__asm__("	stmfd	sp!, {r0}");				\
	__asm__(".L4_arm:");						\
	__BL_SCHED_YIELD();						\
	__BL_GETTID();							\
	__asm__("	cmp	r4, r0");				\
	__asm__("	bne	.L4_arm");				\
	__asm__("	ldmfd	sp!, {r0}");				\
	__asm__("	msr	cpsr, r0");				\
	__asm__("	ldmfd	sp!, {r0-r12,r14}");			\
	__asm__("	b	___ksup_sleep_thread_brk")

 #define	__KSUP_MEM_HIT()					\
	__asm__("	ldrb	r0, [r0, #0]");				\
	__asm__("	b	___ksup_sleep_thread_brk")

#if 0
 #define	__KMC_START_DEBUGER()					\
	__asm__("	stmfd	sp!, {r0, lr}");			\
	__asm__("	bl	getpid");				\
	__asm__("	ldmfd	sp!, {r1, lr}");			\
	__asm__("	ldr	r2, .start");				\
	__asm__("	b	___ksup_start_debuger_brk")
#endif

#if 1
 #define	__KMC_START_DEBUGER()					\
	__asm__("	b	___ksup_start_debuger_brk")
#endif

#ifndef __KMC_STATIC_SUPPORT
 #define	__KMC_START()						\
	__asm__("	.align	4");					\
	__asm__(".start:");						\
	__asm__("	.long	0x0000849c")
#else
 #define	__KMC_START()						\
	__asm__("	.align	4");					\
	__asm__(".start:");						\
	__asm__("	.long	_start")
#endif



#elif defined(__mips__)

 #define	__BL_SCHED_YIELD()					\
		__asm__("	li	$2,"__val1(__NR_sched_yield));	\
		__asm__("	syscall")
 #define	__BL_GETTID()						\
		__asm__("	li	$2,"__val1(__NR_gettid));	\
		__asm__("	syscall")


 #define	__KMC_BRK_CODE__()	__KMC_BRK_CODE()

 #define	__KMC_ALIGN()		__asm__("	.align	2")
 #define	__KMC_NOAT()		__asm__("	.set	noat")


 #define	__KMC_SLEEP_THREAD()					\
	__asm__("	b	___ksup_sleep_thread");			\
	__asm__("	nop");						\
	__asm__("	b	___ksup_mem_hit");			\
	__asm__("	nop");						\
	__asm__("	b  	__kmc_start_debuger");			\
	__asm__("	nop");						\
	__asm__("	.long	0x014c434d")

 #define	__KSUP_SLEEP_THREAD()					\
	__asm__("	addiu	$sp,$sp,-0x90");			\
	__asm__("	sw	$1,0x4($sp)");				\
	__asm__("	sw	$2,0x8($sp)");				\
	__asm__("	sw	$3,0xc($sp)");				\
	__asm__("	sw	$4,0x10($sp)");				\
	__asm__("	sw	$5,0x14($sp)");				\
	__asm__("	sw	$6,0x18($sp)");				\
	__asm__("	sw	$7,0x1c($sp)");				\
	__asm__("	sw	$8,0x20($sp)");				\
	__asm__("	sw	$9,0x24($sp)");				\
	__asm__("	sw	$10,0x28($sp)");			\
	__asm__("	sw	$11,0x2c($sp)");			\
	__asm__("	sw	$12,0x30($sp)");			\
	__asm__("	sw	$13,0x34($sp)");			\
	__asm__("	sw	$14,0x38($sp)");			\
	__asm__("	sw	$15,0x3c($sp)");			\
	__asm__("	sw	$16,0x40($sp)");			\
	__asm__("	sw	$17,0x44($sp)");			\
	__asm__("	sw	$18,0x48($sp)");			\
	__asm__("	sw	$19,0x4c($sp)");			\
	__asm__("	sw	$20,0x50($sp)");			\
	__asm__("	sw	$21,0x54($sp)");			\
	__asm__("	sw	$22,0x58($sp)");			\
	__asm__("	sw	$23,0x5c($sp)");			\
	__asm__("	sw	$24,0x60($sp)");			\
	__asm__("	sw	$25,0x64($sp)");			\
	__asm__("	sw	$26,0x68($sp)");			\
	__asm__("	sw	$27,0x6c($sp)");			\
	__asm__("	sw	$28,0x70($sp)");			\
	__asm__("	sw	$30,0x74($sp)");			\
	__asm__("	sw	$31,0x78($sp)");			\
	__asm__("	mfhi	$1");					\
	__asm__("	sw	$1,0x7c($sp)");				\
	__asm__("	mflo	$1");					\
	__asm__("	sw	$1,0x80($sp)");				\
	__asm__(".L4_mips:");						\
	__BL_SCHED_YIELD();						\
	__BL_GETTID();							\
	__asm__("	bne	$4,$2,.L4_mips");			\
	__asm__("	nop");						\
	__asm__("	lw	$2,0x8($sp)");				\
	__asm__("	lw	$3,0xc($sp)");				\
	__asm__("	lw	$4,0x10($sp)");				\
	__asm__("	lw	$5,0x14($sp)");				\
	__asm__("	lw	$6,0x18($sp)");				\
	__asm__("	lw	$7,0x1c($sp)");				\
	__asm__("	lw	$8,0x20($sp)");				\
	__asm__("	lw	$9,0x24($sp)");				\
	__asm__("	lw	$10,0x28($sp)");			\
	__asm__("	lw	$11,0x2c($sp)");			\
	__asm__("	lw	$12,0x30($sp)");			\
	__asm__("	lw	$13,0x34($sp)");			\
	__asm__("	lw	$14,0x38($sp)");			\
	__asm__("	lw	$15,0x3c($sp)");			\
	__asm__("	lw	$16,0x40($sp)");			\
	__asm__("	lw	$17,0x44($sp)");			\
	__asm__("	lw	$18,0x48($sp)");			\
	__asm__("	lw	$19,0x4c($sp)");			\
	__asm__("	lw	$20,0x50($sp)");			\
	__asm__("	lw	$21,0x54($sp)");			\
	__asm__("	lw	$22,0x58($sp)");			\
	__asm__("	lw	$23,0x5c($sp)");			\
	__asm__("	lw	$24,0x60($sp)");			\
	__asm__("	lw	$25,0x64($sp)");			\
	__asm__("	lw	$26,0x68($sp)");			\
	__asm__("	lw	$27,0x6c($sp)");			\
	__asm__("	lw	$28,0x70($sp)");			\
	__asm__("	lw	$30,0x74($sp)");			\
	__asm__("	lw	$31,0x78($sp)");			\
	__asm__("	lw	$1,0x7c($sp)");				\
	__asm__("	mthi	$1");					\
	__asm__("	lw	$1,0x80($sp)");				\
	__asm__("	mtlo	$1");					\
	__asm__("	lw	$1,0x4($sp)");				\
	__asm__("	addiu	$sp,$sp,0x90");				\
	__asm__("	beq	$0,$0,___ksup_sleep_thread_brk");	\
	__asm__("	nop")

 #define	__KSUP_MEM_HIT()					\
	__asm__("	lb	$1,0($1)");				\
	__asm__("	beq	$0,$0,___ksup_sleep_thread_brk");	\
	__asm__("	nop")

 #define	__KMC_START_DEBUGER()					\
	__asm__("	beq	$0,$0,___ksup_start_debuger_brk");	\
	__asm__("	nop")

 #define	__KMC_START()


#elif defined(__sh__)

 #define	__BL_SCHED_YIELD()					\
	__asm__("	mov	#" __val1(__NR_sched_yield) ",r3");	\
	__asm__("	extu.b	r3,r3");				\
	__asm__("	trapa	#0x10")
 #define	__BL_GETTID()						\
	__asm__("	mov	#" __val1(__NR_gettid) ",r3");		\
	__asm__("	extu.b	r3,r3");				\
	__asm__("	trapa	#0x10");				\


 #define	__KMC_BRK_CODE__()	__KMC_BRK_CODE()

 #define	__KMC_ALIGN()		__asm__("	.align	2")
 #define	__KMC_NOAT()

 #define	__KMC_SLEEP_THREAD()					\
	__asm__("	bra	___ksup_sleep_thread");			\
	__asm__("	nop");						\
	__asm__("	nop");						\
	__asm__("	nop");						\
	__asm__("	bra	___ksup_mem_hit");			\
	__asm__("	nop");						\
	__asm__("	nop");						\
	__asm__("	nop");						\
	__asm__("	bra	__kmc_start_debuger");			\
	__asm__("	nop");						\
	__asm__("	nop");						\
	__asm__("	nop");						\
	__asm__("	.long	0x014c434d")

 #define	__KSUP_SLEEP_THREAD()					\
	__asm__("	mov.l	r0,@-r15");				\
	__asm__("	mov.l	r1,@-r15");				\
	__asm__("	mov.l	r2,@-r15");				\
	__asm__("	mov.l	r3,@-r15");				\
	__asm__("	mov.l	r4,@-r15");				\
	__asm__("	mov.l	r5,@-r15");				\
	__asm__("	mov.l	r6,@-r15");				\
	__asm__("	mov.l	r7,@-r15");				\
	__asm__("	mov.l	r8,@-r15");				\
	__asm__("	mov.l	r9,@-r15");				\
	__asm__("	mov.l	r10,@-r15");				\
	__asm__("	mov.l	r11,@-r15");				\
	__asm__("	mov.l	r12,@-r15");				\
	__asm__("	mov.l	r13,@-r15");				\
	__asm__("	mov.l	r14,@-r15");				\
	__asm__("	sts.l	pr,@-r15");				\
	__asm__("	sts.l	macl,@-r15");				\
	__asm__("	sts.l	mach,@-r15");				\
	__asm__(".L4_sh:");						\
	__BL_SCHED_YIELD();						\
	__BL_GETTID();							\
	__asm__("	cmp/eq	r4,r0");				\
	__asm__("	bf	.L4_sh");				\
	__asm__("	nop");						\
	__asm__("	lds.l	@r15+,mach");				\
	__asm__("	lds.l	@r15+,macl");				\
	__asm__("	lds.l	@r15+,pr");				\
	__asm__("	mov.l	@r15+,r14");				\
	__asm__("	mov.l	@r15+,r13");				\
	__asm__("	mov.l	@r15+,r12");				\
	__asm__("	mov.l	@r15+,r11");				\
	__asm__("	mov.l	@r15+,r10");				\
	__asm__("	mov.l	@r15+,r9");				\
	__asm__("	mov.l	@r15+,r8");				\
	__asm__("	mov.l	@r15+,r7");				\
	__asm__("	mov.l	@r15+,r6");				\
	__asm__("	mov.l	@r15+,r5");				\
	__asm__("	mov.l	@r15+,r4");				\
	__asm__("	mov.l	@r15+,r3");				\
	__asm__("	mov.l	@r15+,r2");				\
	__asm__("	mov.l	@r15+,r1");				\
	__asm__("	mov.l	@r15+,r0");				\
	__asm__("	bra	___ksup_sleep_thread_brk");		\
	__asm__("	nop")

 #define	__KSUP_MEM_HIT()					\
	__asm__("	mov.b	@r8,r9");				\
	__asm__("	bra	___ksup_sleep_thread_brk");		\
	__asm__("	nop")

 #define	__KMC_START_DEBUGER()					\
	__asm__("	mov.l	.start,r6");				\
	__asm__("	bra	___ksup_start_debuger_brk");		\
	__asm__("	nop")

#ifndef __KMC_STATIC_SUPPORT
 #define	__KMC_START()						\
	__asm__("	.align	2");					\
	__asm__(".start:");						\
	__asm__("	.long	0x0000849c")
#else
 #define	__KMC_START()						\
	__asm__("	.align	2");					\
	__asm__(".start:");						\
	__asm__("	.long	_start")
#endif


#elif defined(__mn10300__)

 #define	__BL_SCHED_YIELD()					\
	__asm__("	mov	" __val1(__NR_sched_yield) ",d0");	\
	__asm__("	syscall 0")
 #define	__BL_GETTID()						\
	__asm__("	mov	" __val1(__NR_gettid) ",d0");		\
	__asm__("	syscall 0")


 #define	__KMC_BRK_CODE__()	__KMC_BRK_CODE()

 #define	__KMC_ALIGN()		__asm__("	.align	2")
 #define	__KMC_NOAT()

 #define	__KMC_SLEEP_THREAD()					\
	__asm__("	jmp ___ksup_sleep_thread");			\
	__asm__("	nop");						\
	__asm__("	nop");						\
	__asm__("	jmp ___ksup_mem_hit");				\
	__asm__("	nop");						\
	__asm__("	nop")

 #define	__KSUP_SLEEP_THREAD()					\
	__asm__("	movm	[d2,d3,a2,a3,other,exreg0,exreg1,exother],(sp)");	\
	__asm__(".L4_mn103:");						\
	__BL_SCHED_YIELD();						\
	__BL_GETTID();							\
	__asm__("	cmp	d0,d3");				\
	__asm__("	bne	.L4_mn103");				\
	__asm__("	movm	(sp),[d2,d3,a2,a3,other,exreg0,exreg1,exother]");	\
	__asm__("	jmp	___ksup_sleep_thread_brk")

 #define	__KSUP_MEM_HIT()					\
	__asm__("	movbu	(a0),d0");				\
	__asm__("	jmp	___ksup_sleep_thread_brk")

 #define	__KMC_START_DEBUGER()					\
	__asm__("	mov	(.start),d2");				\
	__asm__("	jmp	___ksup_start_debuger_brk")

#ifndef __KMC_STATIC_SUPPORT
 #define	__KMC_START()						\
	__asm__("	.align	2");					\
	__asm__(".start:");						\
	__asm__("	.long	0x0000849c")
#else
 #define	__KMC_START()						\
	__asm__("	.align	2");					\
	__asm__(".start:");						\
	__asm__("	.long	_start")
#endif

#else

 #define	__KMC_BRK_CODE__()
 #define	__KMC_ALIGN()
 #define	__KMC_NOAT()

 #define	__KMC_SLEEP_THREAD()
 #define	__KSUP_SLEEP_THREAD()
 #define	__KSUP_MEM_HIT()
 #define	__KMC_START_DEBUGER()
 #define	__KMC_START()

 #error "not support CPU"
#endif




/*** ’Ç‰Á Sato ***/
__asm__("	.globl	__kmc_sup_start");
__asm__("	.equ	__kmc_sup_start, __kmc_user_debug_end");

__asm__("	.globl	__kmc_sup_size");
__asm__("	.equ	__kmc_sup_size, __kmc_user_debug_end + 4");

__asm__("	.globl	__kmc_sleep_thr_offs");
__asm__("	.equ	__kmc_sleep_thr_offs, __kmc_user_debug_end + 8");
/****/


__asm__("	.text");
__KMC_ALIGN();
/*** Sato ***/
__asm__("__kmc_user_debug:");
__asm__("__kmc_user_debug_tmp_start:");
/******/
__asm__("	.long	0x4c434d02");	// magic code 'KMC' + function no

__asm__("___ksup_start_debuger_brk:");
__KMC_BRK_CODE__();			// r0.. pid , r1.. arg[0](app filename)
__asm__("	.long	0x4c434d01");	// magic code 'KMC' + function no

__asm__("___ksup_sleep_thread_brk:");
__KMC_BRK_CODE__();			// r0.. pid , r1.. arg[0](app filename)
__asm__("	.long	0x4c434d00");	// magic code 'KMC' + function no

__KMC_NOAT();
__asm__("	.global	__kmc_sleep_thread");
__asm__("__kmc_sleep_thread:");
__KMC_SLEEP_THREAD();

__asm__("___ksup_sleep_thread:");
__KSUP_SLEEP_THREAD();


__asm__("___ksup_mem_hit:");
__KSUP_MEM_HIT();

#ifndef	__KMC_DYNAMIC_SUPPORT
__asm__("	.global	__kmc_start_debuger");
#endif
__asm__("__kmc_start_debuger:");
__KMC_START_DEBUGER();


__KMC_START();

/*** Sato ***/
__asm__("__kmc_user_debug_end:");
__asm__("	.long	__kmc_user_debug");	/* kmcsup code start address */
__asm__("	.long	__kmc_user_debug_end - __kmc_user_debug"); /* kmcsup code size */
__asm__("	.long	__kmc_sleep_thread - __kmc_user_debug"); /* __kmc_sleep_thread offset */
__asm__("");
/*****/


