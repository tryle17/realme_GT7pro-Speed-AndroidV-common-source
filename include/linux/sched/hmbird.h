/* SPDX-License-Identifier: GPL-2.0 */
/*
 * HMBIRD scheduler class: Documentation/scheduler/hmbird.rst
 *
 * Copyright (c) 2024 OPlus.
 * Copyright (c) 2024 Dao Huang
 * Copyright (c) 2024 Yuxing Wang
 * Copyright (c) 2024 Taiyu Li
 */
#ifndef _LINUX_SCHED_HMBIRD_H
#define _LINUX_SCHED_HMBIRD_H

#define HMBIRD_TS_IDX 1
#define HMBIRD_OPS_IDX 14
#define HMBIRD_RQ_IDX 15

#define get_hmbird_ts(p)	\
	((struct hmbird_entity *)(p->android_oem_data1[HMBIRD_TS_IDX]))

#define get_hmbird_rq(rq)	\
	((struct hmbird_rq *)(rq->android_oem_data1[HMBIRD_RQ_IDX]))

#define get_hmbird_ops(rq)	\
	((struct hmbird_ops *)(rq->android_oem_data1[HMBIRD_OPS_IDX]))

#define SCHED_PROP_DEADLINE_MASK (0xFF) /* deadline for ext sched class */
/*
 * Every task has a DEADLINE_LEVEL which stands for
 * max schedule latency this task can afford. LEVEL1~5
 * for user-aware tasks, LEVEL6~9 for other tasks.
 */
#define SCHED_PROP_DEADLINE_LEVEL0 (0)
#define SCHED_PROP_DEADLINE_LEVEL1 (1)
#define SCHED_PROP_DEADLINE_LEVEL2 (2)
#define SCHED_PROP_DEADLINE_LEVEL3 (3)
#define SCHED_PROP_DEADLINE_LEVEL4 (4)
#define SCHED_PROP_DEADLINE_LEVEL5 (5)
#define SCHED_PROP_DEADLINE_LEVEL6 (6)
#define SCHED_PROP_DEADLINE_LEVEL7 (7)
#define SCHED_PROP_DEADLINE_LEVEL8 (8)
#define SCHED_PROP_DEADLINE_LEVEL9 (9)
/*
 * Distinguish tasks into periodical tasks which requires
 * low schedule latency and non-periodical tasks which are
 * not sensitive to schedule latency.
 */
#define SCHED_HMBIRD_DSQ_TYPE_PERIOD            (0) /* period dsq of hmbird */
#define SCHED_HMBIRD_DSQ_TYPE_NON_PERIOD        (1) /* non period dsq of hmbird */

#define TOP_TASK_BITS_MASK      (0xFF)
#define TOP_TASK_BITS           (8)
#include <linux/llist.h>

extern atomic_t non_hmbird_task;
extern atomic_t __hmbird_ops_enabled;
#define hmbird_enabled()           atomic_read(&__hmbird_ops_enabled)

enum hmbird_consts {
	HMBIRD_SLICE_DFL		= 1 * NSEC_PER_MSEC,
	HMBIRD_SLICE_ISO		= 8 * HMBIRD_SLICE_DFL,
	HMBIRD_SLICE_INF		= U64_MAX,	/* infinite, implies nohz */
};

/*
 * DSQ (dispatch queue) IDs are 64bit of the format:
 *
 *   Bits: [63] [62 ..  0]
 *         [ B] [   ID   ]
 *
 *    B: 1 for IDs for built-in DSQs, 0 for ops-created user DSQs
 *   ID: 63 bit ID
 *
 * Built-in IDs:
 *
 *   Bits: [63] [62] [61..32] [31 ..  0]
 *         [ 1] [ L] [   R  ] [    V   ]
 *
 *    1: 1 for built-in DSQs.
 *    L: 1 for LOCAL_ON DSQ IDs, 0 for others
 *    V: For LOCAL_ON DSQ IDs, a CPU number. For others, a pre-defined value.
 */
enum hmbird_dsq_id_flags {
	HMBIRD_DSQ_FLAG_BUILTIN	= 1LLU << 63,
	HMBIRD_DSQ_FLAG_LOCAL_ON	= 1LLU << 62,

	HMBIRD_DSQ_INVALID		= HMBIRD_DSQ_FLAG_BUILTIN | 0,
	HMBIRD_DSQ_GLOBAL		= HMBIRD_DSQ_FLAG_BUILTIN | 1,
	HMBIRD_DSQ_LOCAL		= HMBIRD_DSQ_FLAG_BUILTIN | 2,
	HMBIRD_DSQ_LOCAL_ON	= HMBIRD_DSQ_FLAG_BUILTIN | HMBIRD_DSQ_FLAG_LOCAL_ON,
	HMBIRD_DSQ_LOCAL_CPU_MASK	= 0xffffffffLLU,
};

enum hmbird_exit_type {
	HMBIRD_EXIT_NONE,
	HMBIRD_EXIT_DONE,

	HMBIRD_EXIT_UNREG = 64,	/* BPF unregistration */
	HMBIRD_EXIT_SYSRQ,		/* requested by 'S' sysrq */

	HMBIRD_EXIT_ERROR = 1024,	/* runtime error, error msg contains details */
	HMBIRD_EXIT_ERROR_STALL,	/* watchdog detected stalled runnable tasks */
};

/*
 * Dispatch queue (dsq) is a simple FIFO which is used to buffer between the
 * scheduler core and the BPF scheduler. See the documentation for more details.
 */
struct hmbird_dispatch_q {
	raw_spinlock_t		lock;
	struct list_head	fifo;	/* processed in dispatching order */
	struct rb_root_cached	priq;
	u32			nr;
	u64			id;
	struct llist_node	free_node;
	struct rcu_head		rcu;
	u64                     last_consume_at;
	bool                    is_timeout;
};

/* hmbird_entity.flags */
enum hmbird_ent_flags {
	HMBIRD_TASK_QUEUED		= 1 << 0, /* on hmbird runqueue */
	HMBIRD_TASK_BAL_KEEP	= 1 << 1, /* balance decided to keep current */
	HMBIRD_TASK_ENQ_LOCAL	= 1 << 2, /* used by hmbird_select_cpu_dfl, set HMBIRD_ENQ_LOCAL */

	HMBIRD_TASK_OPS_PREPPED	= 1 << 8, /* prepared for BPF scheduler enable */
	HMBIRD_TASK_OPS_ENABLED	= 1 << 9, /* task has BPF scheduler enabled */

	HMBIRD_TASK_WATCHDOG_RESET = 1 << 16, /* task watchdog counter should be reset */
	HMBIRD_TASK_DEQD_FOR_SLEEP	= 1 << 17, /* last dequeue was for SLEEP */

	HMBIRD_TASK_CURSOR		= 1 << 31, /* iteration cursor, not a task */
};

/* hmbird_entity.dsq_flags */
enum hmbird_ent_dsq_flags {
	HMBIRD_TASK_DSQ_ON_PRIQ	= 1 << 0, /* task is queued on the priority queue of a dsq */
};

#define RAVG_HIST_SIZE 5
struct hmbird_sched_task_stats {
	u64				mark_start;
	u64				window_start;
	u32				sum;
	u32				sum_history[RAVG_HIST_SIZE];
	int				cidx;

	u32				demand;
	u16				demand_scaled;
	void			*sdsq;
};

struct hmbird_sched_rq_stats {
	u64		window_start;
	u64		latest_clock;
	u32		prev_window_size;
	u64		task_exec_scale;
	u64		prev_runnable_sum;
	u64		curr_runnable_sum;
	int		*sched_ravg_window_ptr;
};

/*
 * The following is embedded in task_struct and contains all fields necessary
 * for a task to be scheduled by HMBIRD.
 */
struct hmbird_entity {
	struct hmbird_dispatch_q	*dsq;
	struct {
		struct list_head	fifo;	/* dispatch order */
		struct rb_node		priq;
	} dsq_node;
	struct list_head	watchdog_node;
	u32			flags;		/* protected by rq lock */
	u32			dsq_flags;	/* protected by dsq lock */
	u32			weight;
	s32			sticky_cpu;
	s32			holding_cpu;
	u32			kf_mask;	/* see hmbird_kf_mask above */
	struct task_struct	*kf_tasks[2];	/* see HMBIRD_CALL_OP_TASK() */
	atomic64_t		ops_state;
	unsigned long		runnable_at;
	u64			slice;
	u64			dsq_vtime;
	bool			disallow;	/* reject switching into HMBIRD */
	u16			demand_scaled;

	/* cold fields */
	struct list_head	tasks_node;
	struct task_struct	*task;
	const struct sched_class *sched_class;
	unsigned long		sched_prop;
	unsigned long		top_task_prop;
	struct hmbird_sched_task_stats sts;
	unsigned long           running_at;
	int                     gdsq_idx;

	s32			critical_affinity_cpu;
	int			dsq_sync_ux;
};

struct hmbird_ops {
	bool (*scx_enable)(void);
	bool (*check_non_task)(void);
	void (*do_sched_yield_before)(long *skip);
	void (*window_rollover_run_once)(struct rq *rq);
};

void hmbird_free(struct task_struct *p);

enum DSQ_SYNC_UX_FLAG {
	DSQ_SYNC_UX_NONE = 0,
	DSQ_SYNC_STATIC_UX = 1,
	DSQ_SYNC_INHERIT_UX = 1 << 1,
};

#endif	/* _LINUX_SCHED_HMBIRD_H */
