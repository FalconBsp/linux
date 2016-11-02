/*
 * Copyright 2014 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 */

#include <linux/device.h>
#include <linux/export.h>
#include <linux/err.h>
#include <linux/fs.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/compat.h>
#include <uapi/linux/kfd_ioctl.h>
#include <linux/time.h>
#include "kfd_priv.h"
#include <linux/mm.h>
#include <uapi/asm-generic/mman-common.h>
#include <asm/processor.h>

/*
 * The primary memory I/O features being added for revisions of gfxip
 * beyond 7.0 (Kaveri) are:
 *
 * Access to ATC/IOMMU mapped memory w/ associated extension of VA to 48b
 *
 * “Flat” shader memory access – These are new shader vector memory
 * operations that do not reference a T#/V# so a “pointer” is what is
 * sourced from the vector gprs for direct access to memory.
 * This pointer space has the Shared(LDS) and Private(Scratch) memory
 * mapped into this pointer space as apertures.
 * The hardware then determines how to direct the memory request
 * based on what apertures the request falls in.
 *
 * Unaligned support and alignment check
 *
 *
 * System Unified Address - SUA
 *
 * The standard usage for GPU virtual addresses are that they are mapped by
 * a set of page tables we call GPUVM and these page tables are managed by
 * a combination of vidMM/driver software components.  The current virtual
 * address (VA) range for GPUVM is 40b.
 *
 * As of gfxip7.1 and beyond we’re adding the ability for compute memory
 * clients (CP/RLC, DMA, SHADER(ifetch, scalar, and vector ops)) to access
 * the same page tables used by host x86 processors and that are managed by
 * the operating system. This is via a technique and hardware called ATC/IOMMU.
 * The GPU has the capability of accessing both the GPUVM and ATC address
 * spaces for a given VMID (process) simultaneously and we call this feature
 * system unified address (SUA).
 *
 * There are three fundamental address modes of operation for a given VMID
 * (process) on the GPU:
 *
 *	HSA64 – 64b pointers and the default address space is ATC
 *	HSA32 – 32b pointers and the default address space is ATC
 *	GPUVM – 64b pointers and the default address space is GPUVM (driver
 *		model mode)
 *
 *
 * HSA64 - ATC/IOMMU 64b
 *
 * A 64b pointer in the AMD64/IA64 CPU architecture is not fully utilized
 * by the CPU so an AMD CPU can only access the high area
 * (VA[63:47] == 0x1FFFF) and low area (VA[63:47 == 0) of the address space
 * so the actual VA carried to translation is 48b.  There is a “hole” in
 * the middle of the 64b VA space.
 *
 * The GPU not only has access to all of the CPU accessible address space via
 * ATC/IOMMU, but it also has access to the GPUVM address space.  The “system
 * unified address” feature (SUA) is the mapping of GPUVM and ATC address
 * spaces into a unified pointer space.  The method we take for 64b mode is
 * to map the full 40b GPUVM address space into the hole of the 64b address
 * space.

 * The GPUVM_Base/GPUVM_Limit defines the aperture in the 64b space where we
 * direct requests to be translated via GPUVM page tables instead of the
 * IOMMU path.
 *
 *
 * 64b to 49b Address conversion
 *
 * Note that there are still significant portions of unused regions (holes)
 * in the 64b address space even for the GPU.  There are several places in
 * the pipeline (sw and hw), we wish to compress the 64b virtual address
 * to a 49b address.  This 49b address is constituted of an “ATC” bit
 * plus a 48b virtual address.  This 49b address is what is passed to the
 * translation hardware.  ATC==0 means the 48b address is a GPUVM address
 * (max of 2^40 – 1) intended to be translated via GPUVM page tables.
 * ATC==1 means the 48b address is intended to be translated via IOMMU
 * page tables.
 *
 * A 64b pointer is compared to the apertures that are defined (Base/Limit), in
 * this case the GPUVM aperture (red) is defined and if a pointer falls in this
 * aperture, we subtract the GPUVM_Base address and set the ATC bit to zero
 * as part of the 64b to 49b conversion.
 *
 * Where this 64b to 49b conversion is done is a function of the usage.
 * Most GPU memory access is via memory objects where the driver builds
 * a descriptor which consists of a base address and a memory access by
 * the GPU usually consists of some kind of an offset or Cartesian coordinate
 * that references this memory descriptor.  This is the case for shader
 * instructions that reference the T# or V# constants, or for specified
 * locations of assets (ex. the shader program location).  In these cases
 * the driver is what handles the 64b to 49b conversion and the base
 * address in the descriptor (ex. V# or T# or shader program location)
 * is defined as a 48b address w/ an ATC bit.  For this usage a given
 * memory object cannot straddle multiple apertures in the 64b address
 * space. For example a shader program cannot jump in/out between ATC
 * and GPUVM space.
 *
 * In some cases we wish to pass a 64b pointer to the GPU hardware and
 * the GPU hw does the 64b to 49b conversion before passing memory
 * requests to the cache/memory system.  This is the case for the
 * S_LOAD and FLAT_* shader memory instructions where we have 64b pointers
 * in scalar and vector GPRs respectively.
 *
 * In all cases (no matter where the 64b -> 49b conversion is done), the gfxip
 * hardware sends a 48b address along w/ an ATC bit, to the memory controller
 * on the memory request interfaces.
 *
 *	<client>_MC_rdreq_atc   // read request ATC bit
 *
 *		0 : <client>_MC_rdreq_addr is a GPUVM VA
 *
 *		1 : <client>_MC_rdreq_addr is a ATC VA
 *
 *
 * “Spare” aperture (APE1)
 *
 * We use the GPUVM aperture to differentiate ATC vs. GPUVM, but we also use
 * apertures to set the Mtype field for S_LOAD/FLAT_* ops which is input to the
 * config tables for setting cache policies. The “spare” (APE1) aperture is
 * motivated by getting a different Mtype from the default.
 * The default aperture isn’t an actual base/limit aperture; it is just the
 * address space that doesn’t hit any defined base/limit apertures.
 * The following diagram is a complete picture of the gfxip7.x SUA apertures.
 * The APE1 can be placed either below or above
 * the hole (cannot be in the hole).
 *
 *
 * General Aperture definitions and rules
 *
 * An aperture register definition consists of a Base, Limit, Mtype, and
 * usually an ATC bit indicating which translation tables that aperture uses.
 * In all cases (for SUA and DUA apertures discussed later), aperture base
 * and limit definitions are 64KB aligned.
 *
 *	<ape>_Base[63:0] = { <ape>_Base_register[63:16], 0x0000 }
 *
 *	<ape>_Limit[63:0] = { <ape>_Limit_register[63:16], 0xFFFF }
 *
 * The base and limit are considered inclusive to an aperture so being
 * inside an aperture means (address >= Base) AND (address <= Limit).
 *
 * In no case is a payload that straddles multiple apertures expected to work.
 * For example a load_dword_x4 that starts in one aperture and ends in another,
 * does not work.  For the vector FLAT_* ops we have detection capability in
 * the shader for reporting a “memory violation” back to the
 * SQ block for use in traps.
 * A memory violation results when an op falls into the hole,
 * or a payload straddles multiple apertures.  The S_LOAD instruction
 * does not have this detection.
 *
 * Apertures cannot overlap.
 *
 *
 *
 * HSA32 - ATC/IOMMU 32b
 *
 * For HSA32 mode, the pointers are interpreted as 32 bits and use a single GPR
 * instead of two for the S_LOAD and FLAT_* ops. The entire GPUVM space of 40b
 * will not fit so there is only partial visibility to the GPUVM
 * space (defined by the aperture) for S_LOAD and FLAT_* ops.
 * There is no spare (APE1) aperture for HSA32 mode.
 *
 *
 * GPUVM 64b mode (driver model)
 *
 * This mode is related to HSA64 in that the difference really is that
 * the default aperture is GPUVM (ATC==0) and not ATC space.
 * We have gfxip7.x hardware that has FLAT_* and S_LOAD support for
 * SUA GPUVM mode, but does not support HSA32/HSA64.
 *
 *
 * Device Unified Address - DUA
 *
 * Device unified address (DUA) is the name of the feature that maps the
 * Shared(LDS) memory and Private(Scratch) memory into the overall address
 * space for use by the new FLAT_* vector memory ops.  The Shared and
 * Private memories are mapped as apertures into the address space,
 * and the hardware detects when a FLAT_* memory request is to be redirected
 * to the LDS or Scratch memory when it falls into one of these apertures.
 * Like the SUA apertures, the Shared/Private apertures are 64KB aligned and
 * the base/limit is “in” the aperture. For both HSA64 and GPUVM SUA modes,
 * the Shared/Private apertures are always placed in a limited selection of
 * options in the hole of the 64b address space. For HSA32 mode, the
 * Shared/Private apertures can be placed anywhere in the 32b space
 * except at 0.
 *
 *
 * HSA64 Apertures for FLAT_* vector ops
 *
 * For HSA64 SUA mode, the Shared and Private apertures are always placed
 * in the hole w/ a limited selection of possible locations. The requests
 * that fall in the private aperture are expanded as a function of the
 * work-item id (tid) and redirected to the location of the
 * “hidden private memory”. The hidden private can be placed in either GPUVM
 * or ATC space. The addresses that fall in the shared aperture are
 * re-directed to the on-chip LDS memory hardware.
 *
 *
 * HSA32 Apertures for FLAT_* vector ops
 *
 * In HSA32 mode, the Private and Shared apertures can be placed anywhere
 * in the 32b space except at 0 (Private or Shared Base at zero disables
 * the apertures). If the base address of the apertures are non-zero
 * (ie apertures exists), the size is always 64KB.
 *
 *
 * GPUVM Apertures for FLAT_* vector ops
 *
 * In GPUVM mode, the Shared/Private apertures are specified identically
 * to HSA64 mode where they are always in the hole at a limited selection
 * of locations.
 *
 *
 * Aperture Definitions for SUA and DUA
 *
 * The interpretation of the aperture register definitions for a given
 * VMID is a function of the “SUA Mode” which is one of HSA64, HSA32, or
 * GPUVM64 discussed in previous sections. The mode is first decoded, and
 * then the remaining register decode is a function of the mode.
 *
 *
 * SUA Mode Decode
 *
 * For the S_LOAD and FLAT_* shader operations, the SUA mode is decoded from
 * the COMPUTE_DISPATCH_INITIATOR:DATA_ATC bit and
 * the SH_MEM_CONFIG:PTR32 bits.
 *
 * COMPUTE_DISPATCH_INITIATOR:DATA_ATC    SH_MEM_CONFIG:PTR32        Mode
 *
 * 1                                              0                  HSA64
 *
 * 1                                              1                  HSA32
 *
 * 0                                              X                 GPUVM64
 *
 * In general the hardware will ignore the PTR32 bit and treat
 * as “0” whenever DATA_ATC = “0”, but sw should set PTR32=0
 * when DATA_ATC=0.
 *
 * The DATA_ATC bit is only set for compute dispatches.
 * All “Draw” dispatches are hardcoded to GPUVM64 mode
 * for FLAT_* / S_LOAD operations.
 */

#define MAKE_GPUVM_APP_BASE(gpu_num) \
	(((uint64_t)(gpu_num) << 61) + 0x1000000000000L)

#define MAKE_GPUVM_APP_LIMIT(base, size) \
	(((uint64_t)(base) & 0xFFFFFF0000000000UL) + (size) - 1)

#define MAKE_SCRATCH_APP_BASE() \
	(((uint64_t)(0x1UL) << 61) + 0x100000000L)

#define MAKE_SCRATCH_APP_LIMIT(base) \
	(((uint64_t)base & 0xFFFFFFFF00000000UL) | 0xFFFFFFFF)

#define MAKE_LDS_APP_BASE() \
	(((uint64_t)(0x1UL) << 61) + 0x0)

#define MAKE_LDS_APP_LIMIT(base) \
	(((uint64_t)(base) & 0xFFFFFFFF00000000UL) | 0xFFFFFFFF)


#define DGPU_VM_BASE_DEFAULT 0x100000
#define DGPU_IB_BASE_DEFAULT (DGPU_VM_BASE_DEFAULT - PAGE_SIZE)

int kfd_set_process_dgpu_aperture(struct kfd_process_device *pdd,
					uint64_t base, uint64_t limit)
{
	if (base < (pdd->qpd.cwsr_base + pdd->dev->cwsr_size)) {
		pr_err("Set dgpu vm base 0x%llx failed.\n", base);
		return -EINVAL;
	}
	pdd->dgpu_base = base;
	pdd->dgpu_limit = limit;
	return 0;
}

int kfd_init_apertures(struct kfd_process *process)
{
	uint8_t id  = 0;
	struct kfd_dev *dev;
	struct kfd_process_device *pdd;

	/*Iterating over all devices*/
	while (kfd_topology_enum_kfd_devices(id, &dev) == 0) {
		if (!dev) {
			id++; /* Skip non GPU devices */
			continue;
		}

		pdd = kfd_create_process_device_data(dev, process);
		if (pdd == NULL) {
			pr_err("Failed to create process device data\n");
			goto err;
		}
		/*
		 * For 64 bit process aperture will be statically reserved in
		 * the x86_64 non canonical process address space
		 * amdkfd doesn't currently support apertures for 32 bit process
		 */
		if (process->is_32bit_user_mode) {
			pdd->lds_base = pdd->lds_limit = 0;
			pdd->gpuvm_base = pdd->gpuvm_limit = 0;
			pdd->scratch_base = pdd->scratch_limit = 0;
		} else {
			/*
			 * node id couldn't be 0 - the three MSB bits of
			 * aperture shoudn't be 0
			 */
			pdd->lds_base = MAKE_LDS_APP_BASE();

			pdd->lds_limit = MAKE_LDS_APP_LIMIT(pdd->lds_base);

			pdd->gpuvm_base = MAKE_GPUVM_APP_BASE(id + 1);

			pdd->gpuvm_limit = MAKE_GPUVM_APP_LIMIT(
				pdd->gpuvm_base,
				dev->shared_resources.gpuvm_size);

			pdd->scratch_base = MAKE_SCRATCH_APP_BASE();

			pdd->scratch_limit =
				MAKE_SCRATCH_APP_LIMIT(pdd->scratch_base);

			if (KFD_IS_DGPU(dev->device_info->asic_family)) {
				pdd->qpd.cwsr_base = DGPU_VM_BASE_DEFAULT;
				pdd->qpd.ib_base = DGPU_IB_BASE_DEFAULT;
			}
		}

		dev_dbg(kfd_device, "node id %u\n", id);
		dev_dbg(kfd_device, "gpu id %u\n", pdd->dev->id);
		dev_dbg(kfd_device, "lds_base %llX\n", pdd->lds_base);
		dev_dbg(kfd_device, "lds_limit %llX\n", pdd->lds_limit);
		dev_dbg(kfd_device, "gpuvm_base %llX\n", pdd->gpuvm_base);
		dev_dbg(kfd_device, "gpuvm_limit %llX\n", pdd->gpuvm_limit);
		dev_dbg(kfd_device, "scratch_base %llX\n", pdd->scratch_base);
		dev_dbg(kfd_device, "scratch_limit %llX\n", pdd->scratch_limit);

		id++;
	}

	return 0;

err:
	return -1;
}

void radeon_flush_tlb(struct kfd_dev *dev, uint32_t pasid)
{
	uint8_t vmid;
	int first_vmid_to_scan = 8;
	int last_vmid_to_scan = 15;

	const struct kfd2kgd_calls *f2g = dev->kfd2kgd;
	/* Scan all registers in the range ATC_VMID8_PASID_MAPPING .. ATC_VMID15_PASID_MAPPING
	 * to check which VMID the current process is mapped to
	 * and flush TLB for this VMID if found*/
	for (vmid = first_vmid_to_scan; vmid <= last_vmid_to_scan; vmid++) {
		if (f2g->get_atc_vmid_pasid_mapping_valid(
			dev->kgd, vmid)) {
			if (f2g->get_atc_vmid_pasid_mapping_pasid(
				dev->kgd, vmid) == pasid) {
				dev_dbg(kfd_device,
					"TLB of vmid %u", vmid);
				f2g->write_vmid_invalidate_request(
					dev->kgd, vmid);
				break;
			}
		}
	}
}
