/*
 *  Boot time analysis
 *
 *  Copyright 2001-2009 Sony Corporation
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  version 2 of the  License.
 *
 *  THIS  SOFTWARE  IS PROVIDED   ``AS  IS'' AND   ANY  EXPRESS OR IMPLIED
 *  WARRANTIES,   INCLUDING, BUT NOT  LIMITED  TO, THE IMPLIED WARRANTIES OF
 *  MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN
 *  NO  EVENT  SHALL   THE AUTHOR  BE    LIABLE FOR ANY   DIRECT, INDIRECT,
 *  INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 *  NOT LIMITED   TO, PROCUREMENT OF  SUBSTITUTE GOODS  OR SERVICES; LOSS OF
 *  USE, DATA,  OR PROFITS; OR  BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 *  ANY THEORY OF LIABILITY, WHETHER IN  CONTRACT, STRICT LIABILITY, OR TORT
 *  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 *  THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *  You should have received a copy of the  GNU General Public License along
 *  with this program; if not, write  to the Free Software Foundation, Inc.,
 *  51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA.
 */

#include <linux/kernel.h>
#include <linux/proc_fs.h>
#include <linux/module.h>
#include <linux/seq_file.h>
#include <linux/dma-mapping.h>
#include <linux/snsc_boot_time.h>
#include <linux/slab.h>

#include <linux/memblock.h>

#ifdef CONFIG_SNSC_NBLARGS
#include <linux/snsc_nblargs.h>
#endif
#include <linux/sched.h>
#include <asm/io.h>
#include <asm/string.h>
#include <asm/uaccess.h>
#include <asm/pgalloc.h>

#undef DEBUG_BOOT_TIME

#ifndef CONFIG_SNSC_BOOT_TIME_VERSION_1
#if (CONFIG_SNSC_BOOT_TIME_MAX_COMMENT & 3)
#error CONFIG_BOOT_TIME_MAX_COMMENT should be 4 bytes aligned value.
#endif
#endif

#define BOOT_TIME_BASE		CONFIG_SNSC_DEFAULT_BOOT_TIME_BASE
#define BOOT_TIME_SIZE		CONFIG_SNSC_DEFAULT_BOOT_TIME_SIZE

#define BOOT_TIME_MAGIC_BASE	0x4E554355

#if defined CONFIG_SNSC_BOOT_TIME_VERSION_1
#define BOOT_TIME_VERSION	1
#elif defined CONFIG_SNSC_BOOT_TIME_VERSION_2
#define BOOT_TIME_VERSION	2
#endif
#define BOOT_TIME_MAGIC		(BOOT_TIME_MAGIC_BASE + BOOT_TIME_VERSION)

#define BOOT_TIME_CLEAR_KEY	"CLEAR"

#ifdef CONFIG_SNSC_BOOT_TIME_USE_NBLARGS
#define BOOT_TIME_NBLARGS_KEY	"boottime"
#endif

#ifdef CONFIG_SNSC_BOOT_TIME_VERSION_1
#define MAX_COMMENT		24
#else
#define MAX_COMMENT		(boot_time_max_size)
#endif

struct boot_time_entry {
	u32	count_lo;
	u32	count_hi;
	char	comment[];
};

struct boot_time {
	u32	magic;
	u32	offHead;
	u32	offNext;
	u32	offMax;
#ifndef CONFIG_SNSC_BOOT_TIME_VERSION_1
	u32	max_comment;
	u32     numWritten;
#endif
	struct boot_time_entry first_entry;
};

static u32 boottime_bufsize;
static u32 boottime_nr_entry;

static struct boot_time *pBootTime = NULL;
#ifndef CONFIG_SNSC_BOOT_TIME_VERSION_1
static int boot_time_max_size;
#endif

#ifdef CONFIG_SNSC_BOOT_TIME_RINGBUFFER
static DEFINE_SPINLOCK(boot_time_lock);
#endif

static atomic_t boot_time_offset = ATOMIC_INIT(0);
static DEFINE_SEMAPHORE(proc_mutex);

#define BOOT_TIME_ENTRY_SIZE	(sizeof(struct boot_time_entry) + MAX_COMMENT)
#define PENTRY(off)		((struct boot_time_entry *)((unsigned long)pBootTime + off))

#ifndef CONFIG_SNSC_BOOT_TIME_RINGBUFFER
#define INC_OFFSET(head, off, max)		do { (off) += BOOT_TIME_ENTRY_SIZE; } while(0)
#else
#define INC_OFFSET(head, off, max)                             \
	do {                                                   \
		if (((off) + BOOT_TIME_ENTRY_SIZE) >= (max)) { \
			(off) = (head);                        \
		} else {                                       \
			(off) += BOOT_TIME_ENTRY_SIZE;         \
		}                                              \
	} while(0)
#endif

#define WORD_ALIGNED(addr)	(((unsigned long)(addr) & (4 - 1)) == 0)

#ifdef CONFIG_SNSC_BOOT_TIME_RINGBUFFER
static int
inc_offset_and_return_current_offset(int head, int max)
{
	unsigned long flags;
	int current_offset;

	spin_lock_irqsave(&boot_time_lock, flags);
	current_offset = atomic_read(&boot_time_offset);
	if ((current_offset + BOOT_TIME_ENTRY_SIZE) >= max) {
		atomic_set(&boot_time_offset, pBootTime->offHead);
	} else {
		atomic_add_return(BOOT_TIME_ENTRY_SIZE, &boot_time_offset);
	}
	spin_unlock_irqrestore(&boot_time_lock, flags);
	return current_offset;
}
#endif

__attribute__((weak)) unsigned long long notrace
boot_time_cpu_clock(int cpu)
{
	return cpu_clock(cpu);
}

static void
boot_time_count_set(struct boot_time_entry *pEntry)
{
	unsigned long long t;

	t = boot_time_cpu_clock(raw_smp_processor_id());
	if (!t) {
		t = (unsigned long long)(jiffies - INITIAL_JIFFIES) *
			(1000000000 / HZ);
	}

	pEntry->count_lo = (u32)t;
	pEntry->count_hi = (u32)(t >> 32);
}

/* add new measurement point */
void
boot_time_add(char *comment)
{
	int offset, len = 0;
#ifndef CONFIG_SNSC_BOOT_TIME_VERSION_1
	int nr_entry;
#endif

	struct boot_time_entry *p;

	if (pBootTime == NULL) {
		return;
	}

#ifndef CONFIG_SNSC_BOOT_TIME_RINGBUFFER
	if (pBootTime->offNext >= pBootTime->offMax) {
		return;
	}
#endif
	/*
	 * First get the next offset and then count the current offset
	 */
#ifndef CONFIG_SNSC_BOOT_TIME_RINGBUFFER
	offset = atomic_add_return(BOOT_TIME_ENTRY_SIZE, &boot_time_offset);
	offset -= BOOT_TIME_ENTRY_SIZE;
#else
	offset = inc_offset_and_return_current_offset(pBootTime->offHead,
						      pBootTime->offMax);
#endif

#ifndef CONFIG_SNSC_BOOT_TIME_VERSION_1
	nr_entry = (pBootTime->offMax - pBootTime->offHead) /
		   BOOT_TIME_ENTRY_SIZE;

	if (pBootTime->numWritten < nr_entry) {
		pBootTime->numWritten++;
	}
#endif

#ifndef CONFIG_SNSC_BOOT_TIME_RINGBUFFER
	if (offset >= pBootTime->offMax) {
		return;
	}
#endif

	p = PENTRY(offset);
	boot_time_count_set(p);
	if (comment) {
#ifdef CONFIG_SMP
		unsigned int cpu = get_cpu();
		put_cpu();
		len = snprintf(p->comment, MAX_COMMENT, "%d: ", cpu);
#endif
		strncpy(&(p->comment[len]), comment, MAX_COMMENT - len);
	} else {
		p->comment[0] = '\0';
	}

	pBootTime->offNext = atomic_read(&boot_time_offset);
#ifndef CONFIG_SNSC_BOOT_TIME_RINGBUFFER
	if (pBootTime->offNext > pBootTime->offMax) {
		pBootTime->offNext = pBootTime->offMax;
	}
#endif
	return ;
}

#ifdef CONFIG_SNSC_BOOT_TIME_RINGBUFFER
/* (*pos)
 *   Upper 32bit is the offset when seq_start.
 *   Lower 32bit is the position.
 */
# define BOOTTIME_GET_POS(x)	(lower_32_bits(x))
# define BOOTTIME_GET_START(x)	(upper_32_bits(x))
# define BOOTTIME_SET_START(x)	((u64)(x) << 32)
#endif

static void *
boot_time_seq_start(struct seq_file *m, loff_t *pos)
{
#ifdef CONFIG_ARM
	u_int32_t off;
#else
	u_int64_t off;
#endif
	loff_t n = *pos;
#ifdef CONFIG_SNSC_BOOT_TIME_RINGBUFFER
	int nr_entry;
	u32 start;
	s32 diff;
#endif

	if (down_interruptible(&proc_mutex))
		return NULL;

	if (pBootTime == NULL) {
		return NULL;
	}

#ifdef DEBUG_BOOT_TIME
	printk(KERN_ERR "seq_start: 0x%llx, ", n);
#endif
#ifndef CONFIG_SNSC_BOOT_TIME_RINGBUFFER
	off = pBootTime->offHead;
	if (off == pBootTime->offNext)
		return NULL;
#else
	nr_entry = (pBootTime->offMax - pBootTime->offHead) /
		   BOOT_TIME_ENTRY_SIZE;

	if (pBootTime->numWritten >= nr_entry) {
		off = pBootTime->offNext;
	} else {
		off = pBootTime->offHead;
	}
	diff = 0;
	if (!n) {
		/* save the offset into upper 32bit. */
		*pos = BOOTTIME_SET_START(off);
	} else {
		start = BOOTTIME_GET_START(n);
		n = BOOTTIME_GET_POS(n);
		if ((diff = off - start) < 0) {
			diff += boottime_bufsize;
		}
		diff /= BOOT_TIME_ENTRY_SIZE;
		if (n < diff) {
			/* catch up (leap) */
			n = diff;
			*pos = BOOTTIME_SET_START(start) | diff;
		}
	}
	if (n > pBootTime->numWritten || pBootTime->numWritten == 0)
		return NULL;

	/* adjust (relative to current off) */
	n -= diff;
#endif
	while (n--) {
		INC_OFFSET(pBootTime->offHead, off, pBootTime->offMax);
		if (off == pBootTime->offNext)
			return NULL;
	}
#ifdef DEBUG_BOOT_TIME
	printk(KERN_ERR "off=0x%x\n", off);
#endif
	return (void *)off;
}

static void
boot_time_seq_stop(struct seq_file *m, void *v)
{
#ifdef DEBUG_BOOT_TIME
	printk(KERN_ERR "seq_stop: off=%p\n", v);
#endif
	up(&proc_mutex);
}

static void *
boot_time_seq_next(struct seq_file *m, void *v, loff_t *pos)
{
#ifdef CONFIG_ARM
	u_int32_t off = (u_int32_t)v;
#else
	u_int64_t off = (u_int64_t)v;
#endif

	(*pos)++;
	INC_OFFSET(pBootTime->offHead, off, pBootTime->offMax);
#ifdef CONFIG_SNSC_BOOT_TIME_RINGBUFFER
	if (BOOTTIME_GET_POS(*pos) > boottime_nr_entry) {
		/* to avoid infinity loop */
		return NULL;
	}
#endif
	return (off == pBootTime->offNext) ? NULL : (void *)off;
}

static int
boot_time_seq_show(struct seq_file *m, void *v)
{
#ifdef CONFIG_ARM
        u_int32_t off = (u_int32_t)v;
#else
	u_int64_t off = (u_int64_t)v;
#endif
	u_int32_t start;
#ifdef CONFIG_SNSC_BOOT_TIME_RINGBUFFER
	int nr_entry;
#endif
	struct boot_time_entry *p;
	int i;
	int space = 0;
	u64 dividend;

#ifndef CONFIG_SNSC_BOOT_TIME_RINGBUFFER
	start = pBootTime->offHead;
#else
	nr_entry =  (pBootTime->offMax - pBootTime->offHead) /
			BOOT_TIME_ENTRY_SIZE;

	if (pBootTime->numWritten >= nr_entry) {
		start = pBootTime->offNext;
	} else {
		start = pBootTime->offHead;
	}
#endif
	if (off == start) {
		seq_printf(m, "boot time (%p)\n", pBootTime);
	}

	p = PENTRY(off);
	for(i =0; i < MAX_COMMENT; i++) {
		if (space || p->comment[i] == '\0' || p->comment[i] == '\n') {
			seq_putc(m, ' ');
			space = 1;
		} else {
			seq_printf(m, "%c", p->comment[i]);
		}
	}

	dividend = ((u64)p->count_hi << 32) | p->count_lo;
	do_div(dividend, 1000000);	/* sched_clock() returns nano-sec. */
	seq_printf(m, " : %5u [ms]\n", (u32)dividend);

	return 0;
}

static void
do_cache_sync(const void *addr, size_t size, int rw)
{
#ifdef CONFIG_ARM
	dma_addr_t handle;

	/*
	 * To ensure that the CPU can regain ownership for buffer,
	 * we call dma_unmap_single().
	 */
	handle = dma_map_single(NULL, (void*)addr, size, rw);
	dma_unmap_single(NULL, handle, size, rw);
#elif defined(CONFIG_PPC) || defined(CONFIG_MIPS) || defined(CONFIG_X86)
	dma_cache_sync(NULL, (void*)addr, size, rw);
#else
#error "Please define do_cache_sync() for your arch."
#endif
}

/* clear boot-time region */
static void
boot_time_clear(void)
{
	if (pBootTime == NULL) {
		return;
	}

	pBootTime->offNext = pBootTime->offHead;
 	atomic_set(&boot_time_offset, pBootTime->offNext);
#ifndef CONFIG_SNSC_BOOT_TIME_VERSION_1
	pBootTime->numWritten = 0;
#endif
	do_cache_sync(pBootTime,
		      (unsigned long)pBootTime->offMax,
		      DMA_TO_DEVICE);
}

static ssize_t
proc_boot_time_write(struct file *file, const char *buffer, size_t count, loff_t *ppos)
{
	unsigned long count0 = count;
	int len1;
	char *comment;

	comment = kmalloc(MAX_COMMENT + 1, GFP_KERNEL);
	if (comment == NULL) {
		printk(KERN_INFO
		    "Unable to allocate memory for boot_time proc buffer\n");
		return -ENOMEM;
	}
	if (down_interruptible(&proc_mutex)) {
		kfree(comment);
		return -EINTR;
	}
	if (count0 > MAX_COMMENT) {
		len1 = MAX_COMMENT;
	} else {
		len1 = count0;
	}
	count0 -= len1;
	if (copy_from_user(comment, buffer, len1)) {
		kfree(comment);
		return -EFAULT;
	}
	comment[len1] = '\0';

	if (strncmp(comment, BOOT_TIME_CLEAR_KEY, strlen(BOOT_TIME_CLEAR_KEY)) == 0)
		boot_time_clear();
	else
		boot_time_add(comment);

	up(&proc_mutex);
	kfree(comment);

	return count;
}

static struct seq_operations proc_boot_time_seqop = {
	.start = boot_time_seq_start,
	.next  = boot_time_seq_next,
	.stop  = boot_time_seq_stop,
	.show  = boot_time_seq_show
};

static int proc_boot_time_open(struct inode *inode, struct file *file)
{
	return seq_open(file, &proc_boot_time_seqop);
}

static struct file_operations proc_boot_time_operations = {
	.open    = proc_boot_time_open,
	.read    = seq_read,
	.write   = proc_boot_time_write,
	.llseek  = seq_lseek,
	.release = seq_release,
};


static int
reserve_default_boottime_region(void)
{
	int err = 0;
	err = memblock_find_in_range(BOOT_TIME_BASE, BOOT_TIME_BASE +
				BOOT_TIME_SIZE, BOOT_TIME_SIZE, PAGE_SIZE);
	if (err) {
  		printk(KERN_ERR "cannot reserve memory at 0x%08x size 0x%x "
  		       "it is already reserved\n",
  		       BOOT_TIME_BASE, BOOT_TIME_SIZE);
  		return err;
  	}
	printk(KERN_INFO "boottime: reserved memory at 0x%08x size 0x%x\n",
	       BOOT_TIME_BASE, BOOT_TIME_SIZE);
	return 0;
}

/*
 *  Initialize boottime
 */
void __init
boot_time_init(void)
{
#ifdef CONFIG_SNSC_BOOT_TIME_USE_NBLARGS
	struct nblargs_entry na;
#endif
	static int first = 1;
	u32 size, header_size, nr_entry;
	int err = 0;

	if (!first)
		return;
	first = 0;

#ifdef CONFIG_SNSC_BOOT_TIME_USE_NBLARGS
	if (nblargs_get_key(BOOT_TIME_NBLARGS_KEY, &na) < 0) {
		printk(KERN_INFO "NBLArgs key \"" BOOT_TIME_NBLARGS_KEY
		       "\" not found, using default(0x%08x)\n",
		       BOOT_TIME_BASE);

		err = reserve_default_boottime_region();
	       	if (err) {
 			pBootTime = NULL;
 			return;
 		}
		pBootTime = (struct boot_time *)__va(BOOT_TIME_BASE);
		size = BOOT_TIME_SIZE;
	} else {
		printk(KERN_INFO "NBLArgs key \"" BOOT_TIME_NBLARGS_KEY
		       "\" found(0x%08x)\n", na.addr);

		/* specified boottime region is already reserved by
		   nblargs_init() */
		pBootTime = (struct boot_time *)nbl_to_va(na.addr);
		size = na.size;
	}
#else
	err = reserve_default_boottime_region();
 	if (err) {
 		pBootTime = NULL;
 		return;
 	}
	pBootTime = (struct boot_time *)__va(BOOT_TIME_BASE);
	size = BOOT_TIME_SIZE;
#endif
	header_size = (unsigned long)&(pBootTime->first_entry) -
		      (unsigned long)pBootTime;

#ifndef CONFIG_SNSC_BOOT_TIME_VERSION_1
	if (pBootTime->magic == BOOT_TIME_MAGIC) {
		boot_time_max_size = pBootTime->max_comment;
	} else {
		boot_time_max_size = CONFIG_SNSC_BOOT_TIME_MAX_COMMENT;
	}
#endif

	/* force offMax align to entry start address */
	nr_entry = (u32)((size - header_size) / BOOT_TIME_ENTRY_SIZE);
	pBootTime->offMax = (u32)(header_size +
				  nr_entry * BOOT_TIME_ENTRY_SIZE);
	boottime_bufsize = pBootTime->offMax - pBootTime->offHead;
	boottime_nr_entry = nr_entry;
#ifdef DEBUG_BOOT_TIME
	printk(KERN_ERR "boottime_bufsize=%u\n", boottime_bufsize);
	printk(KERN_ERR "boottime_nr_entry=%u\n", boottime_nr_entry);
#endif

	if (size < (u32)(header_size + BOOT_TIME_ENTRY_SIZE)) {
		printk(KERN_INFO
		       "too small boot time save area(0x%x)\n", size);
		pBootTime = NULL;
		return;
	}

	if ((pBootTime->magic != BOOT_TIME_MAGIC)
	    || ((u_int32_t)pBootTime->offHead > (u_int32_t)pBootTime->offMax)
	    || ((u_int32_t)pBootTime->offNext > (u_int32_t)pBootTime->offMax)
	    || (!WORD_ALIGNED(pBootTime->offHead))
	    || (!WORD_ALIGNED(pBootTime->offNext))) {
		/* initialize boot-time data region */
		pBootTime->magic = BOOT_TIME_MAGIC;
		pBootTime->offHead = header_size;
		pBootTime->offNext = pBootTime->offHead;
#ifndef CONFIG_SNSC_BOOT_TIME_VERSION_1
		pBootTime->max_comment = boot_time_max_size;
		pBootTime->numWritten = 0;
#endif
	}

	/* initialize the atomic offset */
	atomic_set(&boot_time_offset, (u32)pBootTime->offNext);
}

/* initialize /proc for boot time */
static int
boot_time_proc_init(void)
{
	struct proc_dir_entry *entry;

	entry = proc_create("snsc_boot_time", 0666, NULL, &proc_boot_time_operations);

	return 0;
}
late_initcall(boot_time_proc_init);


EXPORT_SYMBOL(boot_time_add);
