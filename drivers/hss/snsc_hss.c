/*
 * Hierarchical Suspend Set (HSS)
 *
 * This privides the framework for in-kernel drivers so that
 * the driver can suspend/resume in specified order of drivers.
 *
 * Copyright 2009 Sony Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * THIS  SOFTWARE  IS PROVIDED   ``AS  IS'' AND   ANY  EXPRESS OR IMPLIED
 * WARRANTIES,   INCLUDING, BUT NOT  LIMITED  TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN
 * NO  EVENT  SHALL   THE AUTHOR  BE    LIABLE FOR ANY   DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED   TO, PROCUREMENT OF  SUBSTITUTE GOODS  OR SERVICES; LOSS OF
 * USE, DATA,  OR PROFITS; OR  BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 * ANY THEORY OF LIABILITY, WHETHER IN  CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA
 *
 */
#include <linux/version.h>
#include <linux/err.h>
#include <linux/platform_device.h>
#include <linux/suspend.h>
#include <linux/device.h>
#include <linux/pm.h>
#include <linux/snsc_hss.h>
#include <linux/slab.h>
#include <linux/module.h>


#include "snsc_hss_internal.h"

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Sony Corporation");
MODULE_DESCRIPTION("Hierarchical Suspend Set");

#ifdef __NEW_PM_API
struct dev_pm_ops hss_pm_ops = {
	.prepare	= hss_new_prepare,
	.suspend	= hss_new_suspend,
	.suspend_noirq	= hss_new_suspend_noirq,
	.resume_noirq	= hss_new_resume_noirq,
	.resume		= hss_new_resume,
	.complete	= hss_new_complete,
};
#endif /* __NEW_PM_API */

static struct platform_driver hss_platform_driver = {
	.driver		= {
		.name	= HSS_MODULE_NAME,
		.owner	= THIS_MODULE,
#ifdef __NEW_PM_API
		.pm	= &hss_pm_ops,
#endif
	},
#ifndef __NEW_PM_API
	.suspend	= hss_driver_legacy_suspend,
	.suspend_late	= hss_driver_legacy_suspend_late,
	.resume_early	= hss_driver_legacy_resume_early,
	.resume		= hss_driver_legacy_resume,
#endif
};

/*
 * Globals
 */

/* list for all registered nodes */
LIST_HEAD(hss_all_nodes_list);

/* list for nodes that have been successful in suspending */
LIST_HEAD(hss_suceeded_nodes_list);

/*
 * lock for the lists above and it also shows suspend being
 * in progress
 */
DEFINE_MUTEX(hss_list_lock);

static struct platform_device *hss_platform_device;

/*
 * pseudo node that represents the grand parent for all nodes
 */
static struct hss_node *root_node;

/*
 * the code asumes all registered nodes have ops member,
 * so, define empty ops struct for the root node
 */
static struct hss_node_ops root_node_ops;

/*
 * used to check the HSS module added into PM list or not
 */
static int hss_inited;

#ifdef CONFIG_SNSC_HSS_DEBUG
/*
 * define a class for sysdev
 */
#ifdef __NEW_SYSDEV_API
struct bus_type hss_subsys = {
	.name = HSS_MODULE_NAME,
};
#else
struct bus_type hss_subsys = {
	set_kset_name(HSS_MODULE_NAME),
};
#endif

/*
 * sysdev attributes
 */
#ifdef __NEW_SYSDEV_API
static ssize_t hss_show_sysdev_name(struct device *dev,
				    struct device_attribute *attr,
				    char *buf)
#else
static ssize_t hss_show_sysdev_name(struct device *dev, char *buf)
#endif
{
	struct hss_node *node;
	ssize_t ret;

	node = container_of(dev, struct hss_node, dev);
	ret = sprintf(buf, "%s\n", node->name);

	return ret;
};

#ifdef __NEW_SYSDEV_API
static ssize_t hss_show_sysdev_level(struct device *dev,
				     struct device_attribute *attr,
				     char *buf)
#else
static ssize_t hss_show_sysdev_level(struct device *dev, char *buf)
#endif
{
	struct hss_node *node;
	ssize_t ret;

	node = container_of(dev, struct hss_node, dev);
	ret = sprintf(buf, "%d\n", node->level);

	return ret;
};

#ifdef __NEW_SYSDEV_API
static ssize_t hss_show_sysdev_num_children(struct device *dev,
					    struct device_attribute *attr,
					    char *buf)
#else
static ssize_t hss_show_sysdev_num_children(struct device *dev,
					    char *buf)
#endif
{
	struct hss_node *node;
	ssize_t ret;

	node = container_of(dev, struct hss_node, dev);
	ret = sprintf(buf, "%d\n", node->num_children);

	return ret;
};

DEVICE_ATTR(name, 0444, hss_show_sysdev_name, NULL);
DEVICE_ATTR(level, 0444, hss_show_sysdev_level, NULL);
DEVICE_ATTR(num_children, 0444, hss_show_sysdev_num_children, NULL);

static struct subsys_interface hss_interface = {
	.name    = "hss_interface",
	.subsys  = &hss_subsys,
};

#endif /* CONFIG_SNSC_HSS_DEBUG */

/*
 * allocate an hss node
 * name: ASCIIZ string.
 */
struct hss_node *hss_alloc_node(const char *name)
{
	struct hss_node *node;
	unsigned int name_len;

	pr_debug("%s:<-\n", __func__);
	if (name == NULL)
		return NULL;

	name_len = strlen(name);

	if (name_len == 0)
		return NULL;

	node = kzalloc(sizeof(struct hss_node) + name_len, GFP_KERNEL);
	if (node != NULL) {
		memcpy(node->name, name, name_len);
		node->name[name_len] = '\0';
		node->name_len = name_len;
		/*
		 * list_head initialization will be done while registration
		INIT_LIST_HEAD(&node->next);
		INIT_LIST_HEAD(&node->processed);
		 */
	}
	pr_debug("%s:->\n", __func__);
	return node;
}
EXPORT_SYMBOL(hss_alloc_node);

/*
 * free the allocated node
 */
void hss_free_node(struct hss_node *node)
{
	kfree(node);
}
EXPORT_SYMBOL(hss_free_node);

/*
 * register a node to this framework
 */
int hss_register_node(struct hss_node *node,
		      struct hss_node_ops *ops,
		      const char *parent_name)
{
	int error = 0;
	struct list_head *tmp;
	struct hss_node *p;

	if (ops == NULL)
		return -EINVAL;

	if (parent_name == NULL)
		parent_name = HSS_ROOT_NODE_NAME;

	/*
	 * If suspending is in progress, fails
	 */
	if (!mutex_trylock(&hss_list_lock)) {
		pr_info("%s: suspend in prorgress or concurrently called "
			"for node %s\n",
			__func__, node->name);
		return -EBUSY;
	}

	/*
	 * find the parent and insert the passed node after it
	 * so that every children is placed after its parent in
	 * the list
	 */
	list_for_each(tmp, &hss_all_nodes_list) {
		p = list_entry(tmp, struct hss_node, next);

		if (strncmp(p->name, parent_name, p->name_len + 1) != 0)
			continue;

		/* found the parent, insert it after the parent */
		node->level = p->level + 1;
		node->parent = p;
		node->ops = ops;
		INIT_LIST_HEAD(&node->next);
		INIT_LIST_HEAD(&node->processed);

		/* increment the parent's counter */
		p->num_children++;

		list_add(&node->next, tmp);
		break;
	}

	/*
	 * parent found?
	 */
	if (tmp == &hss_all_nodes_list) {
		/*
		 * For now, if the specified parent is not found we
		 * assume it is an error.
		 * Possible other implementation: treat this case as
		 * if no parent specified.
		 */
		pr_info("%s: no parent found p=%s\n", __func__,
			parent_name);
		error = -ENOENT;
		goto done;
	}

done:
	mutex_unlock(&hss_list_lock);
	return error;
}
EXPORT_SYMBOL(hss_register_node);

/*
 * Unregister the node from this framework.
 */
int hss_unregister_node(struct hss_node *node)
{
	int error = 0;
	struct list_head *tmp __attribute__((unused));
	struct hss_node *p __attribute__((unused));

	/*
	 * If suspending is in progress, fail
	 */
	if (!mutex_trylock(&hss_list_lock))
		return -EBUSY;

#ifdef DEBUG
	/*
	 * See if it is really in the list
	 */
	list_for_each(tmp, &all_nodes_list) {
		p = list_entry(tmp, struct hss_node, next);
		if (p == node)
			break;
	}
	if (tmp == &all_nodes_list) {
		pr_debug("%s: node is not in the list\n", __func__);
		error = -ENOENT;
		goto done;
	}
#endif
	/*
	 * if it has children, refuse to be removed
	 */
	if (node->num_children != 0) {
		pr_debug("%s: node %s has children %d\n", __func__,
			 node->name, node->num_children);
		error = -EBUSY;
		goto done;
	}

#ifdef DEBUG
	if (node->parent == NULL)
		BUG();
#endif
	node->parent->num_children--;

	list_del(&node->next);
done:
	mutex_unlock(&hss_list_lock);
	return error;
}
EXPORT_SYMBOL(hss_unregister_node);

/*
 * Get/Set the private data
 * To keep the struct opaque to the user, do not inline
 * the function...
 */
void hss_set_private(struct hss_node *node, void *private)
{
	node->private = private;
}
EXPORT_SYMBOL(hss_set_private);

void *hss_get_private(const struct hss_node *node)
{
	return node->private;
}
EXPORT_SYMBOL(hss_get_private);

/*
 * callback result helper functions
 */
void hss_set_invocation(struct hss_node *node,
			enum hss_callbacks callback,
			int result)
{
	node->results[callback] = result;
	set_bit(callback, &node->callback_invoked);
}

/*
 * caller must held hss_list_lock
 */
void hss_clear_invocation_flag_all(void)
{
	struct list_head *tmp;
	struct hss_node *p;

	list_for_each(tmp, &hss_all_nodes_list) {
		p = list_entry(tmp, struct hss_node, next);
		p->callback_invoked = 0;
	}
};

/*
 * Find specified node
 */
int hss_find_node(const char *node_name, struct hss_node **node)
{
	struct list_head *tmp;
	struct hss_node *p;
	int ret;

#ifdef DEBUG
	BUG_ON(!mutex_is_locked(&hss_list_lock));
#endif

	ret = -ENOENT;
	mutex_lock(&hss_list_lock);
	list_for_each(tmp, &hss_all_nodes_list) {
		p = list_entry(tmp, struct hss_node, next);
		if (strncmp(p->name, node_name, p->name_len + 1) == 0) {
			*node = p;
			ret = 0;
			break;
		}
	}
	mutex_unlock(&hss_list_lock);

	return ret;
}
EXPORT_SYMBOL(hss_find_node);

/*
 * Get the result of specified callback
 */
int hss_get_callback_result(const struct hss_node *node,
			    enum hss_callbacks callback,
			    int *result)
{
	void *fn = NULL;

	if (!node->ops)
		return -ENOENT;

	switch (callback) {
	case HSS_CALLBACK_PREPARE:
		fn = node->ops->prepare;
		break;
	case HSS_CALLBACK_SUSPEND:
		fn = node->ops->suspend;
		break;
	case HSS_CALLBACK_SUSPEND_NOIRQ:
		fn = node->ops->suspend_noirq;
		break;
	case HSS_CALLBACK_RESUME_NOIRQ:
		fn = node->ops->resume_noirq;
		break;
	case HSS_CALLBACK_RESUME:
		fn = node->ops->resume;
		break;
	case HSS_CALLBACK_COMPLETE:
		fn = node->ops->complete;
		break;
	case HSS_NUM_CALLBACK:
		/* to supress warings, use HSS_NUM_CALLBACK */
		fn = NULL;
		break;
	};

	if (!fn)
		return -ENOENT;

	if (test_bit(callback, &node->callback_invoked))
		*result = node->results[callback];
	else
		return -EAGAIN;

	return 0;
};
EXPORT_SYMBOL(hss_get_callback_result);

/*
 * Initialize the framework
 * Root node part
 */
static int __init hss_init_root_node(void)
{
	int error;

	pr_debug("%s: start\n", __func__);

#ifdef CONFIG_SNSC_HSS_DEBUG
	error = subsys_system_register(&hss_subsys, NULL);
	if (error)
		goto fail_sysdev_class;

	error = subsys_interface_register(&hss_interface);
	if (error)
		goto fail_sysdev_driver;
#endif
	/*
	 * root node
	 */
	root_node = hss_alloc_node(HSS_ROOT_NODE_NAME);

	if (root_node == NULL) {
		error = -ENOMEM;
		goto fail_root_node_alloc;
	}

	INIT_LIST_HEAD(&root_node->next);
	INIT_LIST_HEAD(&root_node->processed);
	root_node->ops = &root_node_ops;
	list_add(&root_node->next, &hss_all_nodes_list);

	pr_debug("%s: done\n", __func__);
	return 0;

fail_root_node_alloc:
#ifdef CONFIG_SNSC_HSS_DEBUG
	subsys_interface_unregister(&hss_interface);
fail_sysdev_driver:
	bus_unregister(&hss_subsys);
fail_sysdev_class:
#endif

	pr_debug("%s: error\n", __func__);
	return error;
}
core_initcall(hss_init_root_node);

/*
 * Initialize the framework
 * Platform device part
 */
static int hss_init_platform(void)
{
	int error;

	pr_debug("%s: start\n", __func__);

	if (root_node == NULL)
		return -ENODEV;

#ifndef __NEW_PM_API
	error = register_pm_notifier(&hss_nb);
	if (error)
		return error;
#endif

	/*
	 * platform_device for hss
	 */
	error = platform_driver_register(&hss_platform_driver);

	if (error)
		goto fail_platform_driver;

	hss_platform_device = platform_device_alloc(HSS_MODULE_NAME, 0);

	if (hss_platform_device == NULL) {
		error = -ENOMEM;
		goto fail_platform_device_alloc;
	}

	error = platform_device_add(hss_platform_device);

	if (error)
		goto fail_platform_device_add;

	hss_inited = 1;
	pr_debug("%s: done\n", __func__);
	return 0;

fail_platform_device_add:
	platform_device_put(hss_platform_device);
fail_platform_device_alloc:
	platform_driver_unregister(&hss_platform_driver);
fail_platform_driver:
#ifndef __NEW_PM_API
	unregister_pm_notifier(&hss_nb);
#endif
	pr_debug("%s: error\n", __func__);
	return error;
}

int hss_suspend_prepare(void)
{
	pr_debug("%s: start\n", __func__);

	if (hss_inited == 1) {
		/*
		 * if HSS module added into PM list already
		 * un-register it first, and then re-register
		 * to ensure the HSS module is the last entry
		 */
		platform_device_del(hss_platform_device);
		platform_device_put(hss_platform_device);
		platform_driver_unregister(&hss_platform_driver);
#ifndef __NEW_PM_API
		unregister_pm_notifier(&hss_nb);
#endif
		hss_inited = 0;
	}

	return hss_init_platform();
}

#ifdef CONFIG_SNSC_HSS_MODULE
/*
 * This function is never used nor called as
 * this framework is always built into the kernel statically.
 * Written this as the reference for disposing resources
 */
static void __exit hss_exit(void)
{
	pr_debug("%s: start\n", __func__);

	struct list_head *tmp, *n;
	struct hss_node *p;

	/*
	 * platform_device
	 */
	platform_device_del(hss_platform_device);
	platform_device_put(hss_platform_device);
	platform_driver_unregister(&hss_platform_driver);

	list_for_each_safe(tmp, n, &hss_all_nodes_list) {
		p = list_entry(tmp, struct hss_node, next);
		kfree(p);
	}

#ifdef CONFIG_SNSC_HSS_DEBUG
	bus_unregister(&hss_subsys);
#endif

#ifndef __NEW_PM_API
	unregister_pm_notifier(&hss_nb);
#endif

	pr_debug("%s: done\n", __func__);
}
module_exit(hss_exit);
#endif
