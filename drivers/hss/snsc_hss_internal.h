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

/*
 * Check the design of the LDM PM callbacks
 * and sysdev
 */
#if KERNEL_VERSION(2, 6, 24) <= LINUX_VERSION_CODE
#define __NEW_PM_API
#define __NEW_SYSDEV_API
#else
#undef  __NEW_PM_API
#undef  __NEW_SYSDEV_API
#endif

/*
 * The name used for sysfs class name etc.
 */
#define HSS_MODULE_NAME "hss"

/*
 * The name used for the pseudo root node
 */
#define HSS_ROOT_NODE_NAME "__root"

/*
 * HSS node
 * all members are opaque to users
 */
struct hss_node {
	struct list_head next;
	struct list_head processed;
	struct hss_node *parent;
	struct hss_node_ops *ops;
	int level;
	int num_children;
	unsigned int name_len;
	unsigned long callback_invoked;
	int results[HSS_NUM_CALLBACK];
#ifdef CONFIG_SNSC_HSS_DEBUG
	struct device dev;
#endif
	void *private;
	char name[1];
};

/*
 * Function prototypes
 */
int hss_driver_probe(struct platform_device *dev);
int hss_driver_remove(struct platform_device *dev);
void hss_driver_shutdown(struct platform_device *dev);

#ifndef __NEW_PM_API
int hss_driver_legacy_suspend(struct platform_device *dev,
			      pm_message_t state);
int hss_driver_legacy_suspend_late(struct platform_device *dev,
				   pm_message_t state);
int hss_driver_legacy_resume_early(struct platform_device *dev);
int hss_driver_legacy_resume(struct platform_device *dev);
#else
#define hss_driver_legacy_suspend	NULL
#define hss_driver_legacy_suspend_late	NULL
#define hss_driver_legacy_resume_early	NULL
#define hss_driver_legacy_resume	NULL
#endif


#ifdef __NEW_PM_API
int hss_new_prepare(struct device *dev);
int hss_new_suspend(struct device *dev);
int hss_new_suspend_noirq(struct device *dev);
int hss_new_resume_noirq(struct device *dev);
int hss_new_resume(struct device *dev);
void hss_new_complete(struct device *dev);
#else
#define hss_new_prepare		NULL
#define hss_new_suspend		NULL
#define hss_new_suspend_noirq	NULL
#define hss_new_resume_noirq	NULL
#define hss_new_resume		NULL
#define hss_new_complete	NULL
#define hss_pm_ops		NULL
#endif /* __NEW_PM_API */

/*
 * Helper functions
 */
extern void hss_clear_invocation_flag_all(void);
extern void hss_set_invocation(struct hss_node *node,
			       enum hss_callbacks callback,
			       int result);
/*
 * Global variables
 */

extern struct mutex hss_list_lock;
extern struct list_head hss_all_nodes_list;
extern struct list_head hss_suceeded_nodes_list;

#ifndef __NEW_PM_API
extern struct notifier_block hss_nb;
#endif


#ifdef CONFIG_SNSC_HSS_VERBOSE_OUTPUT
#define HSS_VERBOSE_PRINT(x, arg...) pr_info(x, ##arg)
#else
#define HSS_VERBOSE_PRINT(x, arg...) do {} while (0)
#endif
