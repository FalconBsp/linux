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

#ifndef __SNSC_HSS_H__
#define __SNSC_HSS_H__
#ifdef __KERNEL__

struct hss_node;

struct hss_node_ops {
	int (*prepare)(struct hss_node *node);
	int (*suspend)(struct hss_node *node);
	int (*suspend_noirq)(struct hss_node *node);
	int (*resume_noirq)(struct hss_node *node);
	int (*resume)(struct hss_node *node);
	int (*complete)(struct hss_node *node);
};

enum hss_callbacks {
	HSS_CALLBACK_PREPARE,
	HSS_CALLBACK_SUSPEND,
	HSS_CALLBACK_SUSPEND_NOIRQ,
	HSS_CALLBACK_RESUME_NOIRQ,
	HSS_CALLBACK_RESUME,
	HSS_CALLBACK_COMPLETE,
	HSS_NUM_CALLBACK
};

#ifdef CONFIG_SNSC_HSS
struct hss_node *hss_alloc_node(const char *name);
void hss_free_node(struct hss_node *node);

int hss_register_node(struct hss_node *node,
		      struct hss_node_ops *ops,
		      const char *parent_name);
int hss_unregister_node(struct hss_node *node);

void hss_set_private(struct hss_node *node, void *private);
void *hss_get_private(const struct hss_node *node);
int hss_find_node(const char *node_name, struct hss_node **node);
int hss_get_callback_result(const struct hss_node *node,
			    enum hss_callbacks callback,
			    int *result);
int hss_suspend_prepare(void);
#else
inline void *hss_alloc_node(const char *name)
{
	return NULL;
};

inline int hss_free_node(struct hss_node *node)
{
	return 0;
};

inline int hss_register_node(struct hss_node *node,
			     struct hss_node_ops *ops,
			     const char *parent_name)
{
	return -EBUSY;
};

inline int hss_unregister_node(struct hss_node *node)
{
	return -EBUSY;
};

inline void hss_set_private(struct hss_node *node, void *private)
{
};

inline void *hss_get_private(struct hss_node *node)
{
	return NULL;
};

inline int hss_find_node(const char *node_name, struct hss_node **node)
{
	return -ENOENT;
};

inline int hss_get_callback_result(const struct hss_node *node,
				   enum hss_callbacks callback,
				   int *result)
{
	return -ENOENT;
};
#endif /* CONFIG_SNSC_HSS */

#endif /* __KERNEL__ */
#endif /* __SNSC_HSS_H_ */
