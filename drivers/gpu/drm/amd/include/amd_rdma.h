/*
 * Copyright 2015 Advanced Micro Devices, Inc.
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
 */

/* @file This file defined kernel interfaces to communicate with amdkfd */

#ifndef AMD_RDMA_H_
#define AMD_RDMA_H_


/**
 * Structure describing information needed to P2P access from another device
 * to specific location of GPU memory
 */
struct amd_p2p_page_table {
	uint64_t	va;		/**< Specify user virt. address
					  * which this page table
					  * described
					  */
	uint64_t	size;		/**< Specify total size of
					  * allocation
					  */
	int		pid;		/**< Specify process id to which
					  * virtual address is belongs
					  */
	struct sg_table *pages;		/**< Specify DMA/Bus addresses */
	struct device  *dma_device;	/** DMA device requested access
					 */
	void		*priv;		/**< Pointer set by AMD kernel
					  * driver
					  */
};



/**
 * Structure providing function pointers to support rdma/p2p requirements.
 * to specific location of GPU memory
 */
struct amd_rdma_interface {
	int (*get_pages)(uint64_t address, uint64_t length, struct pid *pid,
				struct device *dma_device,
				struct amd_p2p_page_table  **page_table,
				void  *(free_callback)(struct amd_p2p_page_table
								*page_table,
							void *client_priv),
				void  *client_priv);
	int (*put_pages)(struct amd_p2p_page_table *page_table);
	int (*is_gpu_address)(uint64_t address, struct pid *pid);
	int (*get_page_size)(uint64_t address, uint64_t length, struct pid *pid,
				unsigned long *page_size);
};


int amdkfd_query_rdma_interface(const struct amd_rdma_interface **rdma);


#endif /* AMD_RDMA_H_ */

