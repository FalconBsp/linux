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
 */

#ifndef KFD_PRIV_H_INCLUDED
#define KFD_PRIV_H_INCLUDED

#include <linux/hashtable.h>
#include <linux/mmu_notifier.h>
#include <linux/mutex.h>
#include <linux/types.h>
#include <linux/atomic.h>
#include <linux/workqueue.h>
#include <linux/spinlock.h>
#include <linux/idr.h>
#include <linux/kfd_ioctl.h>
#include <kgd_kfd_interface.h>

#define KFD_SYSFS_FILE_MODE 0444

/* GPU ID hash width in bits */
#define KFD_GPU_ID_HASH_WIDTH 16

/* Use upper bits of mmap offset to store KFD driver specific information.
 * BITS[63:62] - Encode MMAP type
 * BITS[61:46] - Encode gpu_id. To identify to which GPU the offset belongs to
 * BITS[45:40] - Reserved. Not Used.
 * BITS[39:0]  - MMAP offset value. Used by TTM.
 *
 * NOTE: struct vm_area_struct.vm_pgoff uses offset in pages. Hence, these
 *  defines are w.r.t to PAGE_SIZE
 */
#define KFD_MMAP_TYPE_SHIFT	(62 - PAGE_SHIFT)
#define KFD_MMAP_TYPE_MASK	(0x3ULL << KFD_MMAP_TYPE_SHIFT)
#define KFD_MMAP_TYPE_DOORBELL	(0x3ULL << KFD_MMAP_TYPE_SHIFT)
#define KFD_MMAP_TYPE_EVENTS	(0x2ULL << KFD_MMAP_TYPE_SHIFT)
#define KFD_MMAP_TYPE_MAP_BO	(0x1ULL << KFD_MMAP_TYPE_SHIFT)
#define KFD_MMAP_TYPE_RESERVED_MEM	(0x0ULL << KFD_MMAP_TYPE_SHIFT)

#define KFD_MMAP_GPU_ID_SHIFT (46 - PAGE_SHIFT)
#define KFD_MMAP_GPU_ID_MASK (((1ULL << KFD_GPU_ID_HASH_WIDTH) - 1) \
				<< KFD_MMAP_GPU_ID_SHIFT)
#define KFD_MMAP_GPU_ID(gpu_id) ((((uint64_t)gpu_id) << KFD_MMAP_GPU_ID_SHIFT)\
				& KFD_MMAP_GPU_ID_MASK)
#define KFD_MMAP_GPU_ID_GET(offset)    ((offset & KFD_MMAP_GPU_ID_MASK) \
				>> KFD_MMAP_GPU_ID_SHIFT)

#define KFD_MMAP_OFFSET_VALUE_MASK	(0xFFFFFFFFFFULL >> PAGE_SHIFT)
#define KFD_MMAP_OFFSET_VALUE_GET(offset) (offset & KFD_MMAP_OFFSET_VALUE_MASK)

/*
 * When working with cp scheduler we should assign the HIQ manually or via
 * the radeon driver to a fixed hqd slot, here are the fixed HIQ hqd slot
 * definitions for Kaveri. In Kaveri only the first ME queues participates
 * in the cp scheduling taking that in mind we set the HIQ slot in the
 * second ME.
 */
#define KFD_CIK_HIQ_PIPE 4
#define KFD_CIK_HIQ_QUEUE 0


/* Macro for allocating structures */
#define kfd_alloc_struct(ptr_to_struct)	\
	((typeof(ptr_to_struct)) kzalloc(sizeof(*ptr_to_struct), GFP_KERNEL))

#define KFD_MAX_NUM_OF_PROCESSES 512
#define KFD_MAX_NUM_OF_QUEUES_PER_PROCESS 1024

/*
 * Kernel module parameter to specify maximum number of supported queues per
 * device
 */
extern int max_num_of_queues_per_device;

#define KFD_MAX_NUM_OF_QUEUES_PER_DEVICE_DEFAULT 4096
#define KFD_MAX_NUM_OF_QUEUES_PER_DEVICE		\
	(KFD_MAX_NUM_OF_PROCESSES *			\
			KFD_MAX_NUM_OF_QUEUES_PER_PROCESS)

#define KFD_KERNEL_QUEUE_SIZE 2048

/* Kernel module parameter to specify the scheduling policy */
extern int sched_policy;

extern int cwsr_enable;

/*
 * Kernel module parameter to specify the maximum process
 * number per HW scheduler
 */
extern int hws_max_conc_proc;

/*
 * Kernel module parameter to specify whether to send sigterm to HSA process on
 * unhandled exception
 */
extern int send_sigterm;

/**
 * enum kfd_sched_policy
 *
 * @KFD_SCHED_POLICY_HWS: H/W scheduling policy known as command processor (cp)
 * scheduling. In this scheduling mode we're using the firmware code to
 * schedule the user mode queues and kernel queues such as HIQ and DIQ.
 * the HIQ queue is used as a special queue that dispatches the configuration
 * to the cp and the user mode queues list that are currently running.
 * the DIQ queue is a debugging queue that dispatches debugging commands to the
 * firmware.
 * in this scheduling mode user mode queues over subscription feature is
 * enabled.
 *
 * @KFD_SCHED_POLICY_HWS_NO_OVERSUBSCRIPTION: The same as above but the over
 * subscription feature disabled.
 *
 * @KFD_SCHED_POLICY_NO_HWS: no H/W scheduling policy is a mode which directly
 * set the command processor registers and sets the queues "manually". This
 * mode is used *ONLY* for debugging proposes.
 *
 */
enum kfd_sched_policy {
	KFD_SCHED_POLICY_HWS = 0,
	KFD_SCHED_POLICY_HWS_NO_OVERSUBSCRIPTION,
	KFD_SCHED_POLICY_NO_HWS
};

enum cache_policy {
	cache_policy_coherent,
	cache_policy_noncoherent
};

enum asic_family_type {
	CHIP_KAVERI = 0,
	CHIP_CARRIZO,
	CHIP_TONGA,
	CHIP_FIJI
};

#define KFD_IS_VI(chip) ((chip) >= CHIP_CARRIZO && (chip) <= CHIP_FIJI)
#define KFD_IS_DGPU(chip) ((chip) >= CHIP_TONGA && (chip) <= CHIP_FIJI)

struct kfd_event_interrupt_class {
	bool (*interrupt_isr)(struct kfd_dev *dev, const uint32_t *ih_ring_entry);
	void (*interrupt_wq)(struct kfd_dev *dev, const uint32_t *ih_ring_entry);
};

struct kfd_device_info {
	unsigned int asic_family;
	const struct kfd_event_interrupt_class *event_interrupt_class;
	unsigned int max_pasid_bits;
	unsigned int max_no_of_hqd;
	size_t ih_ring_entry_size;
	uint8_t num_of_watch_points;
	uint16_t mqd_size_aligned;
	bool is_need_iommu_device;
};

struct kfd_mem_obj {
	uint32_t range_start;
	uint32_t range_end;
	uint64_t gpu_addr;
	uint32_t *cpu_ptr;
};

struct kfd_vmid_info {
	uint32_t first_vmid_kfd;
	uint32_t last_vmid_kfd;
	uint32_t vmid_num_kfd;
};

struct kfd_dev {
	struct kgd_dev *kgd;

	const struct kfd_device_info *device_info;
	struct pci_dev *pdev;

	unsigned int id;		/* topology stub index */

	phys_addr_t doorbell_base;	/* Start of actual doorbells used by
					 * KFD. It is aligned for mapping
					 * into user mode
					 */
	size_t doorbell_id_offset;	/* Doorbell offset (from KFD doorbell
					 * to HW doorbell, GFX reserved some
					 * at the start)
					 */
	size_t doorbell_process_limit;	/* Number of processes we have doorbell
					 * space for.
					 */
	u32 __iomem *doorbell_kernel_ptr; /* This is a pointer for a doorbells
					   * page used by kernel queue
					   */

	struct kgd2kfd_shared_resources shared_resources;
	struct kfd_vmid_info vm_info;

	const struct kfd2kgd_calls *kfd2kgd;
	struct mutex doorbell_mutex;
	unsigned long doorbell_available_index[DIV_ROUND_UP(
		KFD_MAX_NUM_OF_QUEUES_PER_PROCESS, BITS_PER_LONG)];

	void *gtt_mem;
	uint64_t gtt_start_gpu_addr;
	void *gtt_start_cpu_ptr;
	void *gtt_sa_bitmap;
	struct mutex gtt_sa_lock;
	unsigned int gtt_sa_chunk_size;
	unsigned int gtt_sa_num_of_chunks;

	/* QCM Device instance */
	struct device_queue_manager *dqm;

	bool init_complete;

	/* Interrupts */
	void *interrupt_ring;
	size_t interrupt_ring_size;
	atomic_t interrupt_ring_rptr;
	atomic_t interrupt_ring_wptr;
	struct work_struct interrupt_work;
	spinlock_t interrupt_lock;

	/*
	 * Interrupts of interest to KFD are copied
	 * from the HW ring into a SW ring.
	 */
	bool interrupts_active;

	/* Debug manager */
	struct kfd_dbgmgr *dbgmgr;

	/* MEC firmware version*/
	uint16_t mec_fw_version;

	/* Maximum process number mapped to HW scheduler */
	unsigned int max_proc_per_quantum;

	/* cwsr */
	bool cwsr_enabled;
	struct page *cwsr_pages;
	uint32_t cwsr_size;
	uint32_t tma_offset;  /*Offset for TMA from the  start of cwsr_mem*/
};

/* KGD2KFD callbacks */
void kgd2kfd_exit(void);
struct kfd_dev *kgd2kfd_probe(struct kgd_dev *kgd,
			struct pci_dev *pdev, const struct kfd2kgd_calls *f2g);
bool kgd2kfd_device_init(struct kfd_dev *kfd,
			const struct kgd2kfd_shared_resources *gpu_resources);
void kgd2kfd_device_exit(struct kfd_dev *kfd);

enum kfd_mempool {
	KFD_MEMPOOL_SYSTEM_CACHEABLE = 1,
	KFD_MEMPOOL_SYSTEM_WRITECOMBINE = 2,
	KFD_MEMPOOL_FRAMEBUFFER = 3,
};

/* Character device interface */
int kfd_chardev_init(void);
void kfd_chardev_exit(void);
struct device *kfd_chardev(void);

/**
 * enum kfd_preempt_type_filter
 *
 * @KFD_PREEMPT_TYPE_FILTER_SINGLE_QUEUE: Preempts single queue.
 *
 * @KFD_PRERMPT_TYPE_FILTER_ALL_QUEUES: Preempts all queues in the
 *						running queues list.
 *
 * @KFD_PRERMPT_TYPE_FILTER_BY_PASID: Preempts queues that belongs to
 *						specific process.
 *
 */
enum kfd_preempt_type_filter {
	KFD_PREEMPT_TYPE_FILTER_SINGLE_QUEUE,
	KFD_PREEMPT_TYPE_FILTER_ALL_QUEUES,
	KFD_PREEMPT_TYPE_FILTER_DYNAMIC_QUEUES,
	KFD_PREEMPT_TYPE_FILTER_BY_PASID
};

enum kfd_preempt_type {
	KFD_PREEMPT_TYPE_WAVEFRONT,
	KFD_PREEMPT_TYPE_WAVEFRONT_RESET
};

/**
 * enum kfd_queue_type
 *
 * @KFD_QUEUE_TYPE_COMPUTE: Regular user mode queue type.
 *
 * @KFD_QUEUE_TYPE_SDMA: Sdma user mode queue type.
 *
 * @KFD_QUEUE_TYPE_HIQ: HIQ queue type.
 *
 * @KFD_QUEUE_TYPE_DIQ: DIQ queue type.
 */
enum kfd_queue_type  {
	KFD_QUEUE_TYPE_COMPUTE,
	KFD_QUEUE_TYPE_SDMA,
	KFD_QUEUE_TYPE_HIQ,
	KFD_QUEUE_TYPE_DIQ
};

enum kfd_queue_format {
	KFD_QUEUE_FORMAT_PM4,
	KFD_QUEUE_FORMAT_AQL
};

/**
 * struct queue_properties
 *
 * @type: The queue type.
 *
 * @queue_id: Queue identifier.
 *
 * @queue_address: Queue ring buffer address.
 *
 * @queue_size: Queue ring buffer size.
 *
 * @priority: Defines the queue priority relative to other queues in the
 * process.
 * This is just an indication and HW scheduling may override the priority as
 * necessary while keeping the relative prioritization.
 * the priority granularity is from 0 to f which f is the highest priority.
 * currently all queues are initialized with the highest priority.
 *
 * @queue_percent: This field is partially implemented and currently a zero in
 * this field defines that the queue is non active.
 *
 * @read_ptr: User space address which points to the number of dwords the
 * cp read from the ring buffer. This field updates automatically by the H/W.
 *
 * @write_ptr: Defines the number of dwords written to the ring buffer.
 *
 * @doorbell_ptr: This field aim is to notify the H/W of new packet written to
 * the queue ring buffer. This field should be similar to write_ptr and the user
 * should update this field after he updated the write_ptr.
 *
 * @doorbell_off: The doorbell offset in the doorbell pci-bar.
 *
 * @is_interop: Defines if this is a interop queue. Interop queue means that the
 * queue can access both graphics and compute resources.
 *
 * @is_active: Defines if the queue is active or not.
 *
 * @vmid: If the scheduling mode is no cp scheduling the field defines the vmid
 * of the queue.
 *
 * This structure represents the queue properties for each queue no matter if
 * it's user mode or kernel mode queue.
 *
 */
struct queue_properties {
	enum kfd_queue_type type;
	enum kfd_queue_format format;
	unsigned int queue_id;
	uint64_t queue_address;
	uint64_t  queue_size;
	uint32_t priority;
	uint32_t queue_percent;
	uint32_t *read_ptr;
	uint32_t *write_ptr;
	uint32_t __iomem *doorbell_ptr;
	uint32_t doorbell_off;
	bool is_interop;
	bool is_active;
	/* Not relevant for user mode queues in cp scheduling */
	unsigned int vmid;
	/* Relevant only for sdma queues*/
	uint32_t sdma_engine_id;
	uint32_t sdma_queue_id;
	uint32_t sdma_vm_addr;
	/* Relevant only for VI */
	uint64_t eop_ring_buffer_address;
	uint32_t eop_ring_buffer_size;
	uint64_t ctx_save_restore_area_address;
	uint32_t ctx_save_restore_area_size;
	uint32_t ctl_stack_size;
	uint64_t tba_addr;
	uint64_t tma_addr;
	/* Relevant for CU */
	uint32_t cu_mask;
};

/**
 * struct queue
 *
 * @list: Queue linked list.
 *
 * @mqd: The queue MQD.
 *
 * @mqd_mem_obj: The MQD local gpu memory object.
 *
 * @gart_mqd_addr: The MQD gart mc address.
 *
 * @properties: The queue properties.
 *
 * @mec: Used only in no cp scheduling mode and identifies to micro engine id
 * that the queue should be execute on.
 *
 * @pipe: Used only in no cp scheduling mode and identifies the queue's pipe id.
 *
 * @queue: Used only in no cp scheduliong mode and identifies the queue's slot.
 *
 * @process: The kfd process that created this queue.
 *
 * @device: The kfd device that created this queue.
 *
 * This structure represents user mode compute queues.
 * It contains all the necessary data to handle such queues.
 *
 */

struct queue {
	struct list_head list;
	void *mqd;
	struct kfd_mem_obj *mqd_mem_obj;
	uint64_t gart_mqd_addr;
	struct queue_properties properties;
	bool evicted; /* true -> queue is evicted */

	uint32_t mec;
	uint32_t pipe;
	uint32_t queue;

	unsigned int sdma_id;

	struct kfd_process	*process;
	struct kfd_dev		*device;
};

/*
 * Please read the kfd_mqd_manager.h description.
 */
enum KFD_MQD_TYPE {
	KFD_MQD_TYPE_COMPUTE = 0,	/* for no cp scheduling */
	KFD_MQD_TYPE_HIQ,		/* for hiq */
	KFD_MQD_TYPE_CP,		/* for cp queues and diq */
	KFD_MQD_TYPE_SDMA,		/* for sdma queues */
	KFD_MQD_TYPE_MAX
};

struct scheduling_resources {
	unsigned int vmid_mask;
	enum kfd_queue_type type;
	uint64_t queue_mask;
	uint64_t gws_mask;
	uint32_t oac_mask;
	uint32_t gds_heap_base;
	uint32_t gds_heap_size;
};

struct process_queue_manager {
	/* data */
	struct kfd_process	*process;
	unsigned int		num_concurrent_processes;
	struct list_head	queues;
	unsigned long		*queue_slot_bitmap;

	/*cwsr trap handler isa memory*/
	int64_t		tba_addr;
	int64_t		tma_addr;
};

struct qcm_process_device {
	/* The Device Queue Manager that owns this data */
	struct device_queue_manager *dqm;
	struct process_queue_manager *pqm;
	/* Queues list */
	struct list_head queues_list;
	struct list_head priv_queue_list;

	unsigned int queue_count;
	unsigned int vmid;
	bool is_debug;
	bool evicted; /* true -> process device is evicted,and can't be active*/
	/*
	 * All the memory management data should be here too
	 */
	uint64_t gds_context_area;
	uint32_t sh_mem_config;
	uint32_t sh_mem_bases;
	uint32_t sh_mem_ape1_base;
	uint32_t sh_mem_ape1_limit;
	uint32_t page_table_base;
	uint32_t gds_size;
	uint32_t num_gws;
	uint32_t num_oac;
	uint32_t sh_hidden_private_base;
};

/*8 byte handle containing GPU ID in the most significant 4 bytes and
 * idr_handle in the least significant 4 bytes*/
#define MAKE_HANDLE(gpu_id, idr_handle) (((uint64_t)(gpu_id) << 32) + idr_handle)
#define GET_GPU_ID(handle) (handle >> 32)
#define GET_IDR_HANDLE(handle) (handle & 0xFFFFFFFF)

/* Data that is per-process-per device. */
struct kfd_process_device {
	/*
	 * List of all per-device data for a process.
	 * Starts from kfd_process.per_device_data.
	 */
	struct list_head per_device_list;

	/* The device that owns this data. */
	struct kfd_dev *dev;


	/* per-process-per device QCM data structure */
	struct qcm_process_device qpd;

	/*Apertures*/
	uint64_t lds_base;
	uint64_t lds_limit;
	uint64_t gpuvm_base;
	uint64_t gpuvm_limit;
	uint64_t scratch_base;
	uint64_t scratch_limit;
	uint64_t dgpu_base;
	uint64_t dgpu_limit;

	uint64_t sh_hidden_private_base_vmid;
	/* Is this process/pasid bound to this device? (amd_iommu_bind_pasid) */
	bool bound;

	/* VM context for GPUVM allocations */
	void *vm;

	/* GPUVM allocations storage */
	struct idr alloc_idr;

	/* This flag tells if we should reset all
	 * wavefronts on process termination
	 */
	bool reset_wavefronts;
};

#define qpd_to_pdd(x) container_of(x, struct kfd_process_device, qpd)

/* Process data */
struct kfd_process {
	/*
	 * kfd_process are stored in an mm_struct*->kfd_process*
	 * hash table (kfd_processes in kfd_process.c)
	 */
	struct hlist_node kfd_processes;

	struct mm_struct *mm;

	struct mutex mutex;

	/*
	 * In any process, the thread that started main() is the lead
	 * thread and outlives the rest.
	 * It is here because amd_iommu_bind_pasid wants a task_struct.
	 */
	struct task_struct *lead_thread;

	/* We want to receive a notification when the mm_struct is destroyed */
	struct mmu_notifier mmu_notifier;

	/* Use for delayed freeing of kfd_process structure */
	struct rcu_head	rcu;

	unsigned int pasid;

	/*
	 * List of kfd_process_device structures,
	 * one for each device the process is using.
	 */
	struct list_head per_device_data;

	struct process_queue_manager pqm;

	/* The process's queues. */
	size_t queue_array_size;

	/* Size is queue_array_size, up to MAX_PROCESS_QUEUES. */
	struct kfd_queue **queues;

	unsigned long allocated_queue_bitmap[DIV_ROUND_UP(KFD_MAX_NUM_OF_QUEUES_PER_PROCESS, BITS_PER_LONG)];

	/*Is the user space process 32 bit?*/
	bool is_32bit_user_mode;

	/* Event-related data */
	struct mutex event_mutex;
	/* All events in process hashed by ID, linked on kfd_event.events. */
	DECLARE_HASHTABLE(events, 4);
	struct list_head signal_event_pages;	/* struct slot_page_header.event_pages */
	u32 next_nonsignal_event_id;
	size_t signal_event_count;
	size_t debug_event_count;
};

/**
 * Ioctl function type.
 *
 * \param filep pointer to file structure.
 * \param p amdkfd process pointer.
 * \param data pointer to arg that was copied from user.
 */
typedef int amdkfd_ioctl_t(struct file *filep, struct kfd_process *p,
				void *data);

struct amdkfd_ioctl_desc {
	unsigned int cmd;
	int flags;
	amdkfd_ioctl_t *func;
	unsigned int cmd_drv;
	const char *name;
};

void kfd_process_create_wq(void);
void kfd_process_destroy_wq(void);
struct kfd_process *kfd_create_process(const struct task_struct *);
struct kfd_process *kfd_get_process(const struct task_struct *);
struct kfd_process *kfd_lookup_process_by_pasid(unsigned int pasid);

struct kfd_process_device *kfd_bind_process_to_device(struct kfd_dev *dev,
							struct kfd_process *p);
void kfd_unbind_process_from_device(struct kfd_dev *dev, unsigned int pasid);
struct kfd_process_device *kfd_get_process_device_data(struct kfd_dev *dev,
							struct kfd_process *p);
struct kfd_process_device *kfd_create_process_device_data(struct kfd_dev *dev,
							struct kfd_process *p);

int kfd_reserved_mem_mmap(struct kfd_process *process, struct vm_area_struct *vma);

/* KFD process API for creating and translating handles */
int kfd_process_device_create_obj_handle(struct kfd_process_device *p, void *mem);
void *kfd_process_device_translate_handle(struct kfd_process_device *p, int handle);
void kfd_process_device_remove_obj_handle(struct kfd_process_device *p, int handle);

/* Process device data iterator */
struct kfd_process_device *kfd_get_first_process_device_data(struct kfd_process *p);
struct kfd_process_device *kfd_get_next_process_device_data(struct kfd_process *p,
						struct kfd_process_device *pdd);
bool kfd_has_process_device_data(struct kfd_process *p);

/* PASIDs */
int kfd_pasid_init(void);
void kfd_pasid_exit(void);
bool kfd_set_pasid_limit(unsigned int new_limit);
unsigned int kfd_get_pasid_limit(void);
unsigned int kfd_pasid_alloc(void);
void kfd_pasid_free(unsigned int pasid);

/* Doorbells */
void kfd_doorbell_init(struct kfd_dev *kfd);
int kfd_doorbell_mmap(struct kfd_process *process, struct vm_area_struct *vma);
u32 __iomem *kfd_get_kernel_doorbell(struct kfd_dev *kfd,
					unsigned int *doorbell_off);
void kfd_release_kernel_doorbell(struct kfd_dev *kfd, u32 __iomem *db_addr);
u32 read_kernel_doorbell(u32 __iomem *db);
void write_kernel_doorbell(u32 __iomem *db, u32 value);
unsigned int kfd_queue_id_to_doorbell(struct kfd_dev *kfd,
					struct kfd_process *process,
					unsigned int queue_id);

/* GTT Sub-Allocator */

int kfd_gtt_sa_allocate(struct kfd_dev *kfd, unsigned int size,
			struct kfd_mem_obj **mem_obj);

int kfd_gtt_sa_free(struct kfd_dev *kfd, struct kfd_mem_obj *mem_obj);

extern struct device *kfd_device;

/* Topology */
int kfd_topology_init(void);
void kfd_topology_shutdown(void);
int kfd_topology_add_device(struct kfd_dev *gpu);
int kfd_topology_remove_device(struct kfd_dev *gpu);
struct kfd_dev *kfd_device_by_id(uint32_t gpu_id);
struct kfd_dev *kfd_device_by_pci_dev(const struct pci_dev *pdev);
int kfd_topology_enum_kfd_devices(uint8_t idx, struct kfd_dev **kdev);

/* Interrupts */
int kfd_interrupt_init(struct kfd_dev *dev);
void kfd_interrupt_exit(struct kfd_dev *dev);
void kgd2kfd_interrupt(struct kfd_dev *kfd, const void *ih_ring_entry);
bool enqueue_ih_ring_entry(struct kfd_dev *kfd,	const void *ih_ring_entry);
bool interrupt_is_wanted(struct kfd_dev *dev, const uint32_t *ih_ring_entry);

/* Power Management */
void kgd2kfd_suspend(struct kfd_dev *kfd);
int kgd2kfd_resume(struct kfd_dev *kfd);

/* amdkfd Apertures */
int kfd_init_apertures(struct kfd_process *process);
void kfd_set_process_dgpu_aperture(uint32_t node_id,
		struct kfd_process *process, uint64_t base, uint64_t limit);

/* Queue Context Management */
inline uint32_t lower_32(uint64_t x);
inline uint32_t upper_32(uint64_t x);

int init_queue(struct queue **q, struct queue_properties properties);
void uninit_queue(struct queue *q);
void print_queue_properties(struct queue_properties *q);
void print_queue(struct queue *q);

struct mqd_manager *mqd_manager_init(enum KFD_MQD_TYPE type,
					struct kfd_dev *dev);
struct mqd_manager *mqd_manager_init_cik(enum KFD_MQD_TYPE type,
		struct kfd_dev *dev);
struct mqd_manager *mqd_manager_init_vi(enum KFD_MQD_TYPE type,
		struct kfd_dev *dev);
struct mqd_manager *mqd_manager_init_vi_tonga(enum KFD_MQD_TYPE type,
		struct kfd_dev *dev);
struct device_queue_manager *device_queue_manager_init(struct kfd_dev *dev);
void device_queue_manager_uninit(struct device_queue_manager *dqm);
struct kernel_queue *kernel_queue_init(struct kfd_dev *dev,
					enum kfd_queue_type type);
void kernel_queue_uninit(struct kernel_queue *kq);

/* Process Queue Manager */
struct process_queue_node {
	struct queue *q;
	struct kernel_queue *kq;
	struct list_head process_queue_list;
};

int pqm_init(struct process_queue_manager *pqm, struct kfd_process *p);
void pqm_uninit(struct process_queue_manager *pqm);
int pqm_create_queue(struct process_queue_manager *pqm,
			    struct kfd_dev *dev,
			    struct file *f,
			    struct queue_properties *properties,
			    unsigned int flags,
			    enum kfd_queue_type type,
			    unsigned int *qid);
int pqm_destroy_queue(struct process_queue_manager *pqm, unsigned int qid);
int pqm_update_queue(struct process_queue_manager *pqm, unsigned int qid,
			struct queue_properties *p);
int pqm_set_cu_mask(struct process_queue_manager *pqm, unsigned int qid,
			struct queue_properties *p);
struct kernel_queue *pqm_get_kernel_queue(struct process_queue_manager *pqm,
						unsigned int qid);

/* Packet Manager */

#define KFD_HIQ_TIMEOUT (500)

#define KFD_FENCE_COMPLETED (100)
#define KFD_FENCE_INIT   (10)
#define KFD_UNMAP_LATENCY (40)

struct packet_manager_firmware;

struct packet_manager {
	struct device_queue_manager *dqm;
	struct kernel_queue *priv_queue;
	struct mutex lock;
	bool allocated;
	struct kfd_mem_obj *ib_buffer_obj;

	struct packet_manager_firmware *pmf;
};

struct packet_manager_firmware {
	/* Support different firmware versions for map process packet */
	int (*map_process)(struct packet_manager *pm, uint32_t *buffer,
				struct qcm_process_device *qpd);
	int (*get_map_process_packet_size)(void);
};

int pm_init(struct packet_manager *pm, struct device_queue_manager *dqm,
		uint16_t fw_ver);
void pm_uninit(struct packet_manager *pm);
int pm_send_set_resources(struct packet_manager *pm,
				struct scheduling_resources *res);
int pm_send_runlist(struct packet_manager *pm, struct list_head *dqm_queues);
int pm_send_query_status(struct packet_manager *pm, uint64_t fence_address,
				uint32_t fence_value);

int pm_send_unmap_queue(struct packet_manager *pm, enum kfd_queue_type type,
			enum kfd_preempt_type_filter mode,
			uint32_t filter_param, bool reset,
			unsigned int sdma_engine);

void pm_release_ib(struct packet_manager *pm);

uint64_t kfd_get_number_elems(struct kfd_dev *kfd);
phys_addr_t kfd_get_process_doorbells(struct kfd_dev *dev,
					struct kfd_process *process);
int amdkfd_fence_wait_timeout(unsigned int *fence_addr,
				unsigned int fence_value,
				unsigned long timeout);

/* Events */
extern const struct kfd_event_interrupt_class event_interrupt_class_cik;
extern const struct kfd_device_global_init_class device_global_init_class_cik;

enum kfd_event_wait_result {
	KFD_WAIT_COMPLETE,
	KFD_WAIT_TIMEOUT,
	KFD_WAIT_ERROR
};

void kfd_event_init_process(struct kfd_process *p);
void kfd_event_free_process(struct kfd_process *p);
int kfd_event_mmap(struct kfd_process *process, struct vm_area_struct *vma);
int kfd_wait_on_events(struct kfd_process *p,
		       uint32_t num_events, void __user *data,
		       bool all, uint32_t user_timeout_ms,
		       enum kfd_event_wait_result *wait_result);
void kfd_signal_event_interrupt(unsigned int pasid, uint32_t partial_id, uint32_t valid_id_bits);
void kfd_signal_iommu_event(struct kfd_dev *dev,
		unsigned int pasid, unsigned long address,
		bool is_write_requested, bool is_execute_requested);
void kfd_signal_hw_exception_event(unsigned int pasid);
int kfd_set_event(struct kfd_process *p, uint32_t event_id);
int kfd_reset_event(struct kfd_process *p, uint32_t event_id);
int kfd_event_create(struct file *devkfd, struct kfd_process *p,
	     uint32_t event_type, bool auto_reset, uint32_t node_id,
	     uint32_t *event_id, uint32_t *event_trigger_data,
	     uint64_t *event_page_offset, uint32_t *event_slot_index,
	     void *kern_addr);
int kfd_event_destroy(struct kfd_process *p, uint32_t event_id);
void kfd_free_signal_page_dgpu(struct kfd_process *p, uint64_t handle);

void radeon_flush_tlb(struct kfd_dev *dev, uint32_t pasid);

int dbgdev_wave_reset_wavefronts(struct kfd_dev *dev, struct kfd_process *p);

#define KFD_SCRATCH_CZ_FW_VER 600
#define KFD_SCRATCH_KV_FW_VER 413
#define KFD_MULTI_PROC_MAPPING_HWS_SUPPORT 600
#define KFD_CWSR_CZ_FW_VER 625

#endif
