#ifndef __ASM_VDSO_H
#define __ASM_VDSO_H

#ifdef __KERNEL__

#ifndef __ASSEMBLY__

#include <linux/mm_types.h>
#include <asm/mmu.h>

static inline bool vma_is_vdso(struct vm_area_struct *vma)
{
	if (vma->vm_mm && vma->vm_start == vma->vm_mm->context.vdso)
		return true;
	return false;
}

void arm_install_vdso(void);

extern char vdso_start, vdso_end;

#endif /* __ASSEMBLY__ */

#define VDSO_LBASE	0x0

#endif /* __KERNEL__ */

#endif /* __ASM_VDSO_H */
