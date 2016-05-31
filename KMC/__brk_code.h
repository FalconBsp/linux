/*
 * PARTNER-Jet Linux support patch by KMC
 *
 * A definition of S/W break-point instruction
 *
 * Ver3.0	12.06.10
 *
 */

#ifndef __BRK_CODE_H__
#define __BRK_CODE_H__


#ifdef CONFIG_ARM
 #if defined(CONFIG_CPU_V6) || defined(CONFIG_CPU_V7) || defined(CONFIG_KMC_DEBUG_ON_QEMU)
  #define	__KMC_BRK_CODE()	asm("	.long	0xe1200070")
 #else
  #define	__KMC_BRK_CODE()	asm("	.long	0xdeeedeee")
 #endif
#endif

#ifdef CONFIG_MIPS
 #if defined(CONFIG_CPU_TX49XX)
  #define	__KMC_BRK_CODE()	asm("	.long	0x0000000e")
 #else
  #define	__KMC_BRK_CODE()	asm("	.long	0x7000003f")
 #endif
#endif

#ifdef CONFIG_CPU_SH4
 #define	__KMC_BRK_CODE()					\
					asm("	.align 2");		\
					asm("	.long	0x003b003b")
#endif
#ifdef CONFIG_CPU_SH3
 #define	__KMC_BRK_CODE()	asm("	.long	0x00000000")
#endif

#ifdef CONFIG_MN10300
 #define	__KMC_BRK_CODE()	asm("	.long	0xffffffff")
#endif

#ifndef __KMC_BRK_CODE
 #error	!! __KMC_BRK_CODE is not defined !!
#endif

#endif
