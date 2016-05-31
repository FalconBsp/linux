/*
 * Coherent per-device memory handling.
 * Borrowed from i386
 */
#include <linux/slab.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/dma-mapping.h>
#ifdef CONFIG_ARCH_TEGRA
#include <linux/dma-attrs.h>
#endif

struct dma_coherent_mem {
	void		*virt_base;
	dma_addr_t	device_base;
	phys_addr_t	pfn_base;
	int		size;
	int		flags;
	unsigned long	*bitmap;
};

int dma_declare_coherent_memory(struct device *dev, dma_addr_t bus_addr,
				dma_addr_t device_addr, size_t size, int flags)
{
	void __iomem *mem_base = NULL;
	int pages = size >> PAGE_SHIFT;
	int bitmap_size = BITS_TO_LONGS(pages) * sizeof(long);

#ifdef CONFIG_ARCH_TEGRA
	if ((flags &
		(DMA_MEMORY_MAP | DMA_MEMORY_IO | DMA_MEMORY_NOMAP)) == 0)
#else
	if ((flags & (DMA_MEMORY_MAP | DMA_MEMORY_IO)) == 0)
#endif
		goto out;
	if (!size)
		goto out;
	if (dev->dma_mem)
		goto out;

	/* FIXME: this routine just ignores DMA_MEMORY_INCLUDES_CHILDREN */

#ifndef CONFIG_ARCH_TEGRA
	mem_base = ioremap(bus_addr, size);
	if (!mem_base)
		goto out;
#endif

	dev->dma_mem = kzalloc(sizeof(struct dma_coherent_mem), GFP_KERNEL);
	if (!dev->dma_mem)
		goto out;
	dev->dma_mem->bitmap = kzalloc(bitmap_size, GFP_KERNEL);
	if (!dev->dma_mem->bitmap)
		goto free1_out;

#ifdef CONFIG_ARCH_TEGRA
	if (flags & DMA_MEMORY_NOMAP)
		goto skip_mapping;

	mem_base = ioremap(bus_addr, size);
	if (!mem_base)
		goto out;
#endif
	dev->dma_mem->virt_base = mem_base;

#ifdef CONFIG_ARCH_TEGRA
skip_mapping:
#endif
	dev->dma_mem->device_base = device_addr;
	dev->dma_mem->pfn_base = PFN_DOWN(bus_addr);
	dev->dma_mem->size = pages;
	dev->dma_mem->flags = flags;

	if (flags & DMA_MEMORY_MAP)
		return DMA_MEMORY_MAP;

#ifdef CONFIG_ARCH_TEGRA
	if (flags & DMA_MEMORY_NOMAP)
		return DMA_MEMORY_NOMAP;
#endif

	return DMA_MEMORY_IO;

 free1_out:
	kfree(dev->dma_mem);
 out:
	if (mem_base)
		iounmap(mem_base);
	return 0;
}
EXPORT_SYMBOL(dma_declare_coherent_memory);

void dma_release_declared_memory(struct device *dev)
{
	struct dma_coherent_mem *mem = dev->dma_mem;

	if (!mem)
		return;
	dev->dma_mem = NULL;

#ifdef CONFIG_ARCH_TEGRA
	if (!(mem->flags & DMA_MEMORY_NOMAP))
		iounmap(mem->virt_base);
#else
	iounmap(mem->virt_base);
#endif

	kfree(mem->bitmap);
	kfree(mem);
}
EXPORT_SYMBOL(dma_release_declared_memory);

void *dma_mark_declared_memory_occupied(struct device *dev,
					dma_addr_t device_addr, size_t size)
{
	struct dma_coherent_mem *mem = dev->dma_mem;
	int pos, err;

	size += device_addr & ~PAGE_MASK;

	if (!mem)
		return ERR_PTR(-EINVAL);

	pos = (device_addr - mem->device_base) >> PAGE_SHIFT;
	err = bitmap_allocate_region(mem->bitmap, pos, get_order(size));
	if (err != 0)
		return ERR_PTR(err);
	return mem->virt_base + (pos << PAGE_SHIFT);
}
EXPORT_SYMBOL(dma_mark_declared_memory_occupied);

/**
#ifdef CONFIG_ARCH_TEGRA
 * dma_alloc_from_coherent_attr() - try to allocate memory from the per-device
 * coherent area
#else
 * dma_alloc_from_coherent() - try to allocate memory from the per-device coherent area
#endif
 *
 * @dev:	device from which we allocate memory
 * @size:	size of requested memory area
 * @dma_handle:	This will be filled with the correct dma handle
 * @ret:	This pointer will be filled with the virtual address
 *		to allocated area.
 *
#ifdef CONFIG_ARCH_TEGRA
 * @attrs:	DMA Attribute
#endif
 * This function should be only called from per-arch dma_alloc_coherent()
 * to support allocation from per-device coherent memory pools.
 *
#ifdef CONFIG_ARCH_TEGRA
 * Returns 0 if dma_alloc_coherent_attr should continue with allocating from
#else
 * Returns 0 if dma_alloc_coherent should continue with allocating from
#endif
 * generic memory areas, or !0 if dma_alloc_coherent should return @ret.
 */
#ifdef CONFIG_ARCH_TEGRA
int dma_alloc_from_coherent_attr(struct device *dev, ssize_t size,
				       dma_addr_t *dma_handle, void **ret,
				       struct dma_attrs *attrs)
#else
int dma_alloc_from_coherent(struct device *dev, ssize_t size,
				       dma_addr_t *dma_handle, void **ret)
#endif
{
	struct dma_coherent_mem *mem;
	int order = get_order(size);
	int pageno;
#ifdef CONFIG_ARCH_TEGRA
	unsigned int count;
	unsigned long align;
#endif

	if (!dev)
		return 0;
	mem = dev->dma_mem;
	if (!mem)
		return 0;

	*ret = NULL;

	if (unlikely(size > (mem->size << PAGE_SHIFT)))
		goto err;

#ifdef CONFIG_ARCH_TEGRA
	if (dma_get_attr(DMA_ATTR_ALLOC_EXACT_SIZE, attrs)) {
		align = 0;
		count = PAGE_ALIGN(size) >> PAGE_SHIFT;
	} else  {
		align = (1 << order) - 1;
		count = 1 << order;
	}

	pageno = bitmap_find_next_zero_area(mem->bitmap, mem->size,
			0, count, align);

	if (pageno >= mem->size)
#else
	pageno = bitmap_find_free_region(mem->bitmap, mem->size, order);
	if (unlikely(pageno < 0))
#endif
		goto err;

#ifdef CONFIG_ARCH_TEGRA
	bitmap_set(mem->bitmap, pageno, count);
#endif

	/*
	 * Memory was found in the per-device area.
	 */
	*dma_handle = mem->device_base + (pageno << PAGE_SHIFT);
#ifdef CONFIG_ARCH_TEGRA
	if (!(mem->flags & DMA_MEMORY_NOMAP)) {
		*ret = mem->virt_base + (pageno << PAGE_SHIFT);
		memset(*ret, 0, size);
	}
#else
	*ret = mem->virt_base + (pageno << PAGE_SHIFT);
	memset(*ret, 0, size);
#endif

	return 1;

err:
	/*
	 * In the case where the allocation can not be satisfied from the
	 * per-device area, try to fall back to generic memory if the
	 * constraints allow it.
	 */
	return mem->flags & DMA_MEMORY_EXCLUSIVE;
}
#ifdef CONFIG_ARCH_TEGRA
EXPORT_SYMBOL(dma_alloc_from_coherent_attr);
#else
EXPORT_SYMBOL(dma_alloc_from_coherent);
#endif

/**
#ifdef CONFIG_ARCH_TEGRA
 * dma_release_from_coherent_attr() - try to free the memory allocated from
 * per-device coherent memory pool
#else
 * dma_release_from_coherent() - try to free the memory allocated from per-device coherent memory pool
#endif
 * @dev:	device from which the memory was allocated
#ifdef CONFIG_ARCH_TEGRA
 * @size:	size of the memory area to free
#else
 * @order:	the order of pages allocated
#endif
 * @vaddr:	virtual address of allocated pages
#ifdef CONFIG_ARCH_TEGRA
 * @attrs:	DMA Attribute
#endif
 *
 * This checks whether the memory was allocated from the per-device
 * coherent memory pool and if so, releases that memory.
 *
 * Returns 1 if we correctly released the memory, or 0 if
#ifdef CONFIG_ARCH_TEGRA
 * dma_release_coherent_attr() should proceed with releasing memory from
#else
 * dma_release_coherent() should proceed with releasing memory from
#endif
 * generic pools.
 */
#ifdef CONFIG_ARCH_TEGRA
int dma_release_from_coherent_attr(struct device *dev, size_t size, void *vaddr,
				struct dma_attrs *attrs)
#else
int dma_release_from_coherent(struct device *dev, int order, void *vaddr)
#endif
{
	struct dma_coherent_mem *mem = dev ? dev->dma_mem : NULL;
#ifdef CONFIG_ARCH_TEGRA
	void *mem_addr;
	unsigned int count;
	unsigned int pageno;

	if (!mem)
		return 0;

	if (mem->flags & DMA_MEMORY_NOMAP)
		mem_addr =  (void *)mem->device_base;
	else
		mem_addr =  mem->virt_base;


	if (mem && vaddr >= mem_addr &&
	    vaddr - mem_addr < mem->size << PAGE_SHIFT) {

		pageno = (vaddr - mem_addr) >> PAGE_SHIFT;

		if (dma_get_attr(DMA_ATTR_ALLOC_EXACT_SIZE, attrs))
			count = PAGE_ALIGN(size) >> PAGE_SHIFT;
		else
			count = 1 << get_order(size);

		bitmap_clear(mem->bitmap, pageno, count);
#else

	if (mem && vaddr >= mem->virt_base && vaddr <
		   (mem->virt_base + (mem->size << PAGE_SHIFT))) {
		int page = (vaddr - mem->virt_base) >> PAGE_SHIFT;

		bitmap_release_region(mem->bitmap, page, order);
#endif

		return 1;
	}
	return 0;
}
#ifdef CONFIG_ARCH_TEGRA
EXPORT_SYMBOL(dma_release_from_coherent_attr);
#else
EXPORT_SYMBOL(dma_release_from_coherent);
#endif

/**
 * dma_mmap_from_coherent() - try to mmap the memory allocated from
 * per-device coherent memory pool to userspace
 * @dev:	device from which the memory was allocated
 * @vma:	vm_area for the userspace memory
 * @vaddr:	cpu address returned by dma_alloc_from_coherent
 * @size:	size of the memory buffer allocated by dma_alloc_from_coherent
 * @ret:	result from remap_pfn_range()
 *
 * This checks whether the memory was allocated from the per-device
 * coherent memory pool and if so, maps that memory to the provided vma.
 *
 * Returns 1 if we correctly mapped the memory, or 0 if the caller should
 * proceed with mapping memory from generic pools.
 */
int dma_mmap_from_coherent(struct device *dev, struct vm_area_struct *vma,
			   void *vaddr, size_t size, int *ret)
{
	struct dma_coherent_mem *mem = dev ? dev->dma_mem : NULL;
#ifdef CONFIG_ARCH_TEGRA
	void *mem_addr;

	if (!mem)
		return 0;

	if (mem->flags & DMA_MEMORY_NOMAP)
		mem_addr =  (void *)mem->device_base;
	else
		mem_addr =  mem->virt_base;
#endif

#ifdef CONFIG_ARCH_TEGRA
	if (mem && vaddr >= mem_addr && vaddr + size <=
		   (mem_addr + (mem->size << PAGE_SHIFT))) {
#else
	if (mem && vaddr >= mem->virt_base && vaddr + size <=
		   (mem->virt_base + (mem->size << PAGE_SHIFT))) {
#endif
		unsigned long off = vma->vm_pgoff;
#ifdef CONFIG_ARCH_TEGRA
		int start = (vaddr - mem_addr) >> PAGE_SHIFT;
#else
		int start = (vaddr - mem->virt_base) >> PAGE_SHIFT;
#endif
		int user_count = (vma->vm_end - vma->vm_start) >> PAGE_SHIFT;
		int count = size >> PAGE_SHIFT;

		*ret = -ENXIO;
		if (off < count && user_count <= count - off) {
			unsigned pfn = mem->pfn_base + start + off;
			*ret = remap_pfn_range(vma, vma->vm_start, pfn,
					       user_count << PAGE_SHIFT,
					       vma->vm_page_prot);
		}
		return 1;
	}
	return 0;
}
EXPORT_SYMBOL(dma_mmap_from_coherent);
