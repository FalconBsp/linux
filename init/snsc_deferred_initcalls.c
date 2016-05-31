/*
 *  deferred initcalls
 *
 *  Copyright 2009 Sony Corporation
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
#include <linux/init.h>
#include <linux/uaccess.h>
#include <linux/workqueue.h>
#include <linux/proc_fs.h>
#include <asm/page.h>

#define DEFD_MSG_PFX		"deferred_initcalls: "
#define DEFD_PROC_NAME		"snsc_deferred_initcalls"
#define DEFD_NUM_GROUPS		32

typedef struct deferred_group {
	struct list_head list;
	struct semaphore sem;
	int done;
} deferred_group_t;

static deferred_group_t defd_groups[DEFD_NUM_GROUPS];
static DEFINE_SEMAPHORE(defd_free_sem);
static atomic_t defd_total = ATOMIC_INIT(0);

extern void free_initmem(void);

static int
do_free_initmem(void)
{
	static int freed = 0;
	deferred_group_t *group;
	int idx, ret = 0;

	if (down_interruptible(&defd_free_sem)) {
		return -ERESTARTSYS;
	}

	/* check if already freed */
	if (freed) {
		goto up_free_sem;
	}

	/* aquire all group's semaphores */
	group = defd_groups;
	for (idx = 0; idx < DEFD_NUM_GROUPS; idx++, group++) {
		if (down_interruptible(&group->sem)) {
			ret = -ERESTARTSYS;
			goto up_group_sem;
		}
	}

	/* flush initcall's works */
	flush_scheduled_work();

	/* free init sections */
	free_initmem();
	freed = 1;

	/* set the total count to zero */
	atomic_set(&defd_total, 0);

 up_group_sem:
	/* release already acquired group's semaphores */
	for (idx--, group--; idx >= 0; idx--, group--) {
		up(&group->sem);
	}

 up_free_sem:
	up(&defd_free_sem);

	return ret;
}

static int
do_deferred_initcalls(int idx)
{
	deferred_group_t *group = &defd_groups[idx];
	deferred_initcall_t *call;
	int ret;

	/* acquire specified group's semaphore */
	if (down_interruptible(&group->sem)) {
		return -ERESTARTSYS;
	}

	/* check if already finished */
	if (atomic_read(&defd_total) == 0) {
		up(&group->sem);
		return do_free_initmem();
	}

	/* check group status */
	if (list_empty(&group->list) || group->done) {
		up(&group->sem);
		return 0;
	}

	/* call deferred initcalls */
	list_for_each_entry(call, &group->list, list) {
		ret = call->func();
		if (ret < 0) {
			pr_err(DEFD_MSG_PFX "%s failed: %d\n",
			       call->name, ret);
		} else {
			pr_debug(DEFD_MSG_PFX "%s done\n",
				 call->name);
		}
	}
	group->done = 1;

	/* decrement total count */
	atomic_dec(&defd_total);

	/* release group's semaphore */
	up(&group->sem);

	/* free init sections if no deferred initcalls exists */
	if (atomic_read(&defd_total) == 0) {
		return do_free_initmem();
	}

	return 0;
}

static ssize_t
deferred_initcalls_proc_read(struct file *file, char __user *buf,
			     size_t count, loff_t *off)
{
	deferred_group_t *group;
	deferred_initcall_t *call;
	int idx;
	int limit = PAGE_SIZE - 96;
	char tmp[50];
	size_t tmp_size = 0;

	if (down_interruptible(&defd_free_sem)) {
		return -ERESTARTSYS;
	}

	/* check if already finished */
	if (atomic_read(&defd_total) == 0) {
		goto up_free_sem;
	}

	/* output deferred initcall list */
	group = defd_groups;
	for (idx = 0; idx < DEFD_NUM_GROUPS; idx++, group++) {
		if (list_empty(&group->list)) {
			continue;
		}
		tmp_size = sprintf(tmp, "Group %02u:\n", idx);
		if (tmp_size > limit) {
			goto output_end;
		}
		list_for_each_entry(call, &group->list, list) {
			tmp_size = sprintf(tmp,
				       "  %-30s : %s\n", call->name,
				       group->done ? "done" : "deferred");
			if (tmp_size > limit) {
				goto output_end;
			}
		}
	}

 output_end:
	/* check buffer full */
	if (idx < DEFD_NUM_GROUPS) {
		tmp_size = sprintf(tmp, "<truncated>\n");
	}

 up_free_sem:
	up(&defd_free_sem);

//	return tmp_size;
	return simple_read_from_buffer(buf, count, off, tmp, tmp_size);
}

static ssize_t
deferred_initcalls_proc_write(struct file *file, const char __user *buffer,
			      size_t count, loff_t *off)
{
	char buf[16];
	int idx, ret;

	/* check string length */
	if (count >= sizeof(buf)) {
		return -EINVAL;
	}

	/* get string from user */
	if (copy_from_user(buf, buffer, count)) {
		return -EFAULT;
	}
	buf[count] = '\0';
	if (buf[count - 1] == '\n') {
		buf[count - 1] = '\0';
	}

	/* for 'free' operation */
	if (strcmp(buf, "free") == 0) {
		ret = do_free_initmem();
		if (ret < 0) {
			return ret;
		}
		return count;
	}

	/* for 'all' operation */
	if (strcmp(buf, "all") == 0) {
		for (idx = 0; idx < DEFD_NUM_GROUPS; idx++) {
			ret = do_deferred_initcalls(idx);
			if (ret < 0) {
				return ret;
			}
		}
		return count;
	}

	/* for user specified group */
	idx = simple_strtoul(buf, NULL, 0);
	if (idx >= DEFD_NUM_GROUPS) {
		pr_err(DEFD_MSG_PFX "invalid group: %d\n", idx);
		return -EINVAL;
	}
	ret = do_deferred_initcalls(idx);
	if (ret < 0) {
		return ret;
	}

	return count;
}

static const struct file_operations proc_deferred_initcalls_fops = {
	.owner               = THIS_MODULE,
        .read                = deferred_initcalls_proc_read,
        .write               = deferred_initcalls_proc_write,
};

static int __init
deferred_initcalls_init(void)
{
	struct proc_dir_entry *entry;
	deferred_group_t *group;
	deferred_initcall_t *call;
	int idx;

	/* initialize groups */
	group = defd_groups;
	for (idx = 0; idx < DEFD_NUM_GROUPS; idx++, group++) {
		INIT_LIST_HEAD(&group->list);
		sema_init(&group->sem,1);
	}

	/* parse deferred initcall table */
	for (call = __deferred_initcall_start;
	     call < __deferred_initcall_end; call++) {
		idx = call->group;
		if (idx < 0 || idx >= DEFD_NUM_GROUPS) {
			pr_err(DEFD_MSG_PFX "%s has invalid group %d\n",
			       call->name, idx);
		} else {
			group = &defd_groups[idx];
			if (list_empty(&group->list)) {
				atomic_inc(&defd_total);
			}
			list_add_tail(&call->list, &group->list);
		}
	}

	/* create proc entry */
	entry = proc_create(DEFD_PROC_NAME, S_IRUGO | S_IWUSR, NULL, &proc_deferred_initcalls_fops);
	if (!entry) {
		return -ENOMEM;
	}
	return 0;
}

late_initcall(deferred_initcalls_init);
