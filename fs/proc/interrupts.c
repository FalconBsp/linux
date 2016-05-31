#include <linux/fs.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/irqnr.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/rt_trace_lite.h>
#include <linux/rt_trace_lite_irq.h>

/*
 * /proc/interrupts
 * /proc/irq_stat
 */
static void *int_seq_start(struct seq_file *f, loff_t *pos)
{
	return (*pos <= nr_irqs) ? pos : NULL;
}

static void *int_seq_next(struct seq_file *f, void *v, loff_t *pos)
{
	(*pos)++;
	if (*pos > nr_irqs)
		return NULL;
	return pos;
}

static void int_seq_stop(struct seq_file *f, void *v)
{
	/* Nothing to do */
}

static const struct seq_operations int_seq_ops = {
	.start = int_seq_start,
	.next  = int_seq_next,
	.stop  = int_seq_stop,
	.show  = show_interrupts
};

static int interrupts_open(struct inode *inode, struct file *filp)
{
	return seq_open(filp, &int_seq_ops);
}

static const struct file_operations proc_interrupts_operations = {
	.open		= interrupts_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= seq_release,
};

#ifdef CONFIG_SNSC_DEBUG_IRQ_DURATION
static void *irq_stat_seq_next(struct seq_file *f, void *v, loff_t *pos)
{
	(*pos)++;
	if (*pos > NR_IRQS_EXT)
		return NULL;
	return pos;
}

extern int show_irq_stat(struct seq_file *f, void *v); /* In arch code */
static struct seq_operations irq_stat_seq_ops = {
	.start = int_seq_start,
	.next  = irq_stat_seq_next,
	.stop  = int_seq_stop,
	.show  = show_irq_stat,
};

static int irq_stat_open(struct inode *inode, struct file *filp)
{
	return seq_open(filp, &irq_stat_seq_ops);
}

static const struct file_operations proc_irq_stat_operations = {
	.open		= irq_stat_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= seq_release,
	.write		= irq_stat_write,
};
#endif

static int __init proc_interrupts_init(void)
{
	proc_create("interrupts", 0, NULL, &proc_interrupts_operations);
#ifdef CONFIG_SNSC_DEBUG_IRQ_DURATION
	proc_create("irq_stat", 0, NULL, &proc_irq_stat_operations);
#endif
	return 0;
}
module_init(proc_interrupts_init);
