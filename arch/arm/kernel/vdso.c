/*
 * Adapted from arm64 version.
 *
 * Copyright (C) 2012 ARM Limited
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/err.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/timekeeper_internal.h>
#include <linux/vmalloc.h>

#include <asm/cacheflush.h>
#include <asm/page.h>
#include <asm/vdso.h>
#include <asm/vdso_datapage.h>
#include <asm/mmu_context.h>

static unsigned long vdso_pages;
static struct page **vdso_pagelist;

static union {
	struct vdso_data	data;
	u8			page[PAGE_SIZE];
} vdso_data_store __page_aligned_data;
struct vdso_data *vdso_data = &vdso_data_store.data;

/*
 * The vDSO data page.
 */

static int __init vdso_init(void)
{
	struct page *pg;
	char *vbase;
	int i, ret = 0;

	vdso_pages = (&vdso_end - &vdso_start) >> PAGE_SHIFT;
	pr_info("vdso: %ld pages (%ld code, %ld data) at base %p\n",
		vdso_pages + 1, vdso_pages, 1L, &vdso_start);

	/* Allocate the vDSO pagelist, plus a page for the data. */
	vdso_pagelist = kzalloc(sizeof(struct page *) * (vdso_pages + 1),
				GFP_KERNEL);
	if (vdso_pagelist == NULL) {
		pr_err("Failed to allocate vDSO pagelist!\n");
		return -ENOMEM;
	}

	/* Grab the vDSO code pages. */
	for (i = 0; i < vdso_pages; i++) {
		pg = virt_to_page(&vdso_start + i*PAGE_SIZE);
		ClearPageReserved(pg);
		get_page(pg);
		vdso_pagelist[i] = pg;
	}

	/* Sanity check the shared object header. */
	vbase = vmap(vdso_pagelist, 1, 0, PAGE_KERNEL);
	if (vbase == NULL) {
		pr_err("Failed to map vDSO pagelist!\n");
		return -ENOMEM;
	} else if (memcmp(vbase, "\177ELF", 4)) {
		pr_err("vDSO is not a valid ELF object!\n");
		ret = -EINVAL;
		goto unmap;
	}

	/* Grab the vDSO data page. */
	pg = virt_to_page(vdso_data);
	get_page(pg);
	vdso_pagelist[i] = pg;

unmap:
	vunmap(vbase);
	return ret;
}
arch_initcall(vdso_init);

/* assumes mmap_sem is write-locked */
void arm_install_vdso(void)
{
	struct mm_struct *mm = current->mm;
	unsigned long vdso_base, vdso_mapping_len;
	int ret;

	/* Be sure to map the data page */
	vdso_mapping_len = (vdso_pages + 1) << PAGE_SHIFT;

	vdso_base = get_unmapped_area(NULL, 0, vdso_mapping_len, 0, 0);
	if (IS_ERR_VALUE(vdso_base)) {
		pr_notice_once("%s: get_unapped_area failed (%ld)\n",
			       __func__, (long)vdso_base);
		ret = vdso_base;
		return;
	}
	mm->context.vdso = vdso_base;

	ret = install_special_mapping(mm, vdso_base, vdso_mapping_len,
				      VM_READ|VM_EXEC|
				      VM_MAYREAD|VM_MAYWRITE|VM_MAYEXEC,
				      vdso_pagelist);
	if (ret) {
		pr_notice_once("%s: install_special_mapping failed (%d)\n",
			       __func__, ret);
		mm->context.vdso = 0;
		return;
	}
}

/**
 * update_vsyscall - update the vdso data page
 *
 * Increment the sequence counter, making it odd, indicating to
 * userspace that an update is in progress.  Update the fields used
 * for coarse clocks, and, if the architected system timer is in use,
 * the fields used for high precision clocks.  Increment the sequence
 * counter again, making it even, indicating to userspace that the
 * update is finished.
 *
 * Userspace is expected to sample tb_seq_count before reading any
 * other fields from the data page.  If tb_seq_count is odd, userspace
 * is expected to wait until it becomes even.  After copying data from
 * the page, userspace must sample tb_seq_count again; if it has
 * changed from its previous value, userspace must retry the whole
 * sequence.
 *
 * Calls to update_vsyscall are serialized by the timekeeping core.
 */
void update_vsyscall(struct timekeeper *tk)
{
	struct timespec xtime_coarse;
	struct timespec wall_time = tk_xtime(tk);
	struct timespec *wtm = &tk->wall_to_monotonic;
	u32 use_syscall = strcmp(tk->clock->name, "arch_sys_counter");

	++vdso_data->tb_seq_count;
	smp_wmb();

	xtime_coarse = __current_kernel_time();
	vdso_data->use_syscall			= use_syscall;
	vdso_data->xtime_coarse_sec		= xtime_coarse.tv_sec;
	vdso_data->xtime_coarse_nsec		= xtime_coarse.tv_nsec;
	vdso_data->wtm_clock_sec		= wtm->tv_sec;
	vdso_data->wtm_clock_nsec		= wtm->tv_nsec;

	if (!use_syscall) {
		vdso_data->cs_cycle_last	= tk->clock->cycle_last;
		vdso_data->xtime_clock_sec	= wall_time.tv_sec;
		vdso_data->xtime_clock_nsec	= wall_time.tv_nsec;
		vdso_data->cs_mult		= tk->mult;
		vdso_data->cs_shift		= tk->shift;
	}

	smp_wmb();
	++vdso_data->tb_seq_count;
	flush_dcache_page(virt_to_page(vdso_data));
}

void update_vsyscall_tz(void)
{
	vdso_data->tz_minuteswest	= sys_tz.tz_minuteswest;
	vdso_data->tz_dsttime		= sys_tz.tz_dsttime;
	flush_dcache_page(virt_to_page(vdso_data));
}
