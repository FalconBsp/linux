/*
* helper.c - kernel helper functions
*
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
*
* Authors: AMD
*
*/

#include "helper.h"

#define INVALID_ORDER		((u32)(-1))

static inline void *getpagestart(void *addr)
{
	return (void *)(((u64)(addr)) & PAGE_MASK);
}

static inline u32 getoffsetinpage(void *addr)
{
	return (u32)(((u64)(addr)) & (~PAGE_MASK));
}

static inline u32 getnrofpagesforbuffer(void *addrStart,
		u32 len)
{
	return (getoffsetinpage(addrStart) + len + PAGE_SIZE-1) / PAGE_SIZE;
}

u32 sizetoorder(u32 size)
{
	u32 order = INVALID_ORDER;

	if (0 != size) {
		order = __builtin_clz(getnrofpagesforbuffer(NULL, size));
		/* there is a size overflow in getnrofpagesforbuffer when
		   the size is too large */
		if (unlikely(order > 31))
			return INVALID_ORDER;
		order = 31 - order;
		/* above algorithm rounds down: clz(5)=2 instead of 3
		   quick correction to fix it: */
		if (((1<<order)*PAGE_SIZE) < size)
			order++;
	}
	return order;
}

u32 addrtopfn(void *addr)
{
	return (u32)((u64)(addr) >> PAGE_SHIFT);
}

void flush_buffer(void *addr, u32 size)
{
	struct page *page;
	void *page_start = getpagestart(addr);
	int i;

	for (i = 0; i < getnrofpagesforbuffer(addr, size); i++) {
		page = virt_to_page(page_start);
		flush_dcache_page(page);
		page_start += PAGE_SIZE;
	}
}

void invalidate_buffer(void *addr, u32 size)
{

}
