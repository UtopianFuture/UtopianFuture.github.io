## Process Schedule

### 目录

- [基本概念](#基本概念)
  - [轻量级进程](#轻量级进程)
  - [task_struct](#task_struct)
  - [进程的生命周期](#进程的生命周期)
  - [进程标识](#进程标识)
  - [进程间的关系](#进程间的关系)
  - [获取当前进程](#获取当前进程)
  - [内核线程](#内核线程)
- [进程创建与终止](#进程创建与终止)
  - [创建进程](#创建进程)
    - [fork](#fork)
    - [vfork](#vfork)
    - [clone](#clone)
    - [关键函数kernel_clone](#关键函数kernel_clone)
    - [关键函数copy_process](#关键函数copy_process)
    - [关键函数dup_task_struct](#关键函数dup_task_struct)
    - [关键函数copy_mm](#关键函数copy_mm)
    - [关键函数dup_mmap](#关键函数dup_mmap)
    - [关键函数copy_thread](#关键函数copy_thread)

  - [进程创建后的返回](#进程创建后的返回)
  - [终止进程](#终止进程)

- [进程调度](#进程调度)
- [Reference](#Reference)

### 基本概念

这里介绍一些我个人觉得很容易混淆和有趣的概念。

#### 轻量级进程

线程称为轻量级进程，它是操作系统调度的最小单元，通常一个进程可以拥有多个线程，即线程对应一个进程描述符，而进程对应一个或一组进程描述符。内核并没有特别的调度算法或定义特别的数据结构来表示线程，线程和进程都使用相同的进程描述符数据结构。内核中使用 `clone` 来创建线程，其工作方式和创建进程的 `fork` 类似。但 `clone` 会确定哪些资源和父进程共享，哪些资源为线程独享。

#### task_struct

```c
struct task_struct {
#ifdef CONFIG_THREAD_INFO_IN_TASK
	/*
	 * For reasons of header soup (see current_thread_info()), this
	 * must be the first element of task_struct.
	 */
	struct thread_info		thread_info; // 这个好像还有些复杂，之后再分析
#endif
	unsigned int			__state // 表示进程的状态

#ifdef CONFIG_PREEMPT_RT
	/* saved state for "spinlock sleepers" */
	unsigned int			saved_state;
#endif

	/*
	 * This begins the randomizable portion of task_struct. Only
	 * scheduling-critical items should be added above here.
	 */
	randomized_struct_fields_start

	void				*stack;
	refcount_t			usage;
	/* Per task flags (PF_*), defined further below: */
	unsigned int			flags; // 进程属性标志位。如进程退出时会设置 PF_EXITING
	unsigned int			ptrace;

#ifdef CONFIG_SMP
	int				on_cpu;
	struct __call_single_node	wake_entry;
#ifdef CONFIG_THREAD_INFO_IN_TASK
	/* Current CPU: */
	unsigned int			cpu;
#endif
	unsigned int			wakee_flips;
	unsigned long			wakee_flip_decay_ts;
	struct task_struct		*last_wakee;

	/*
	 * recent_used_cpu is initially set as the last CPU used by a task
	 * that wakes affine another task. Waker/wakee relationships can
	 * push tasks around a CPU where each wakeup moves to the next one.
	 * Tracking a recently used CPU allows a quick search for a recently
	 * used CPU that may be idle.
	 */
	int				recent_used_cpu;
	int				wake_cpu;
#endif
	int				on_rq;

	int				prio; // 进程的动态优先级。这是调度类考（？）虑的优先级
	int				static_prio; // 静态优先级，再进程启动时分配
	int				normal_prio; // 基于 static_prio 和调度策略计算出来的优先级
	unsigned int			rt_priority; // 实时进程的优先级

	const struct sched_class	*sched_class; // 调度类
	struct sched_entity		se; // 普通进程的调度实体
	struct sched_rt_entity		rt; // 实时进程的调度实体
	struct sched_dl_entity		dl; // deadline 进程的调度实体

#ifdef CONFIG_SCHED_CORE
	struct rb_node			core_node;
	unsigned long			core_cookie;
	unsigned int			core_occupation;
#endif

#ifdef CONFIG_CGROUP_SCHED
	struct task_group		*sched_task_group;
#endif

#ifdef CONFIG_UCLAMP_TASK
	/*
	 * Clamp values requested for a scheduling entity.
	 * Must be updated with task_rq_lock() held.
	 */
	struct uclamp_se		uclamp_req[UCLAMP_CNT];
	/*
	 * Effective clamp values used for a scheduling entity.
	 * Must be updated with task_rq_lock() held.
	 */
	struct uclamp_se		uclamp[UCLAMP_CNT];
#endif

#ifdef CONFIG_PREEMPT_NOTIFIERS
	/* List of struct preempt_notifier: */
	struct hlist_head		preempt_notifiers;
#endif

#ifdef CONFIG_BLK_DEV_IO_TRACE
	unsigned int			btrace_seq;
#endif

	unsigned int			policy; // 确定进程的类型，如普通进程还是实时进程等
	int				nr_cpus_allowed; // 确定该进程可以在哪几个 CPU 上运行
	const cpumask_t			*cpus_ptr;
	cpumask_t			*user_cpus_ptr;
	cpumask_t			cpus_mask;
	void				*migration_pending;
#ifdef CONFIG_SMP
	unsigned short			migration_disabled;
#endif
	unsigned short			migration_flags;

#ifdef CONFIG_PREEMPT_RCU
	int				rcu_read_lock_nesting;
	union rcu_special		rcu_read_unlock_special;
	struct list_head		rcu_node_entry;
	struct rcu_node			*rcu_blocked_node;
#endif /* #ifdef CONFIG_PREEMPT_RCU */

#ifdef CONFIG_TASKS_RCU
	unsigned long			rcu_tasks_nvcsw;
	u8				rcu_tasks_holdout;
	u8				rcu_tasks_idx;
	int				rcu_tasks_idle_cpu;
	struct list_head		rcu_tasks_holdout_list;
#endif /* #ifdef CONFIG_TASKS_RCU */

#ifdef CONFIG_TASKS_TRACE_RCU
	int				trc_reader_nesting;
	int				trc_ipi_to_cpu;
	union rcu_special		trc_reader_special;
	bool				trc_reader_checked;
	struct list_head		trc_holdout_list;
#endif /* #ifdef CONFIG_TASKS_TRACE_RCU */

	struct sched_info		sched_info;

	struct list_head		tasks;
#ifdef CONFIG_SMP
	struct plist_node		pushable_tasks;
	struct rb_node			pushable_dl_tasks;
#endif

	struct mm_struct		*mm; // 哈哈，这个就很熟悉了
	struct mm_struct		*active_mm;

	/* Per-thread vma caching: */
	struct vmacache			vmacache;

#ifdef SPLIT_RSS_COUNTING
	struct task_rss_stat		rss_stat;
#endif
	int				exit_state;
	int				exit_code; // 存放进程退出值和终止信号，这样父进程可以知道子进程退出的原因
	int				exit_signal;
	/* The signal sent when the parent dies: */
	int				pdeath_signal;
	/* JOBCTL_*, siglock protected: */
	unsigned long			jobctl;

	/* Used for emulating ABI behavior of previous Linux versions: */
	unsigned int			personality;

	/* Scheduler bits, serialized by scheduler locks: */
	unsigned			sched_reset_on_fork:1;
	unsigned			sched_contributes_to_load:1;
	unsigned			sched_migrated:1;
#ifdef CONFIG_PSI
	unsigned			sched_psi_wake_requeue:1;
#endif

	/* Force alignment to the next boundary: */
	unsigned			:0;

	/* Unserialized, strictly 'current' */

	/*
	 * This field must not be in the scheduler word above due to wakelist
	 * queueing no longer being serialized by p->on_cpu. However:
	 *
	 * p->XXX = X;			ttwu()
	 * schedule()			  if (p->on_rq && ..) // false
	 *   smp_mb__after_spinlock();	  if (smp_load_acquire(&p->on_cpu) && //true
	 *   deactivate_task()		      ttwu_queue_wakelist())
	 *     p->on_rq = 0;			p->sched_remote_wakeup = Y;
	 *
	 * guarantees all stores of 'current' are visible before
	 * ->sched_remote_wakeup gets used, so it can be in this word.
	 */
	unsigned			sched_remote_wakeup:1;

	/* Bit to tell LSMs we're in execve(): */
	unsigned			in_execve:1;
	unsigned			in_iowait:1;
#ifndef TIF_RESTORE_SIGMASK
	unsigned			restore_sigmask:1;
#endif
#ifdef CONFIG_MEMCG
	unsigned			in_user_fault:1;
#endif
#ifdef CONFIG_COMPAT_BRK
	unsigned			brk_randomized:1;
#endif
#ifdef CONFIG_CGROUPS
	/* disallow userland-initiated cgroup migration */
	unsigned			no_cgroup_migration:1;
	/* task is frozen/stopped (used by the cgroup freezer) */
	unsigned			frozen:1;
#endif
#ifdef CONFIG_BLK_CGROUP
	unsigned			use_memdelay:1;
#endif
#ifdef CONFIG_PSI
	/* Stalled due to lack of memory */
	unsigned			in_memstall:1;
#endif
#ifdef CONFIG_PAGE_OWNER
	/* Used by page_owner=on to detect recursion in page tracking. */
	unsigned			in_page_owner:1;
#endif
#ifdef CONFIG_EVENTFD
	/* Recursion prevention for eventfd_signal() */
	unsigned			in_eventfd_signal:1;
#endif

	unsigned long			atomic_flags; /* Flags requiring atomic access. */

	struct restart_block		restart_block;

	pid_t				pid; // 进程 id
	pid_t				tgid; // 那这个表示啥

#ifdef CONFIG_STACKPROTECTOR
	/* Canary value for the -fstack-protector GCC feature: */
	unsigned long			stack_canary;
#endif
	/*
	 * Pointers to the (original) parent process, youngest child, younger sibling,
	 * older sibling, respectively.  (p->father can be replaced with
	 * p->real_parent->pid)
	 */

	/* Real parent process: */
	struct task_struct __rcu	*real_parent; // 该进程的父进程

	/* Recipient of SIGCHLD, wait4() reports: */
	struct task_struct __rcu	*parent; // 和上一个有什么区别么

	/*
	 * Children/sibling form the list of natural children:
	 */
	struct list_head		children;
	struct list_head		sibling;
	struct task_struct		*group_leader; // 进程组的组长。上面介绍了，进程对应一个或多个线程

	/*
	 * 'ptraced' is the list of tasks this task is using ptrace() on.
	 *
	 * This includes both natural children and PTRACE_ATTACH targets.
	 * 'ptrace_entry' is this task's link on the p->parent->ptraced list.
	 */
	struct list_head		ptraced;
	struct list_head		ptrace_entry;

	/* PID/PID hash table linkage. */
	struct pid			*thread_pid;
	struct hlist_node		pid_links[PIDTYPE_MAX];
	struct list_head		thread_group;
	struct list_head		thread_node;

	struct completion		*vfork_done;

	/* CLONE_CHILD_SETTID: */
	int __user			*set_child_tid;

	/* CLONE_CHILD_CLEARTID: */
	int __user			*clear_child_tid;

	/* PF_IO_WORKER */
	void				*pf_io_worker;

	u64				utime;
	u64				stime;
#ifdef CONFIG_ARCH_HAS_SCALED_CPUTIME
	u64				utimescaled;
	u64				stimescaled;
#endif
	u64				gtime;
	struct prev_cputime		prev_cputime;
#ifdef CONFIG_VIRT_CPU_ACCOUNTING_GEN
	struct vtime			vtime;
#endif

#ifdef CONFIG_NO_HZ_FULL
	atomic_t			tick_dep_mask;
#endif
	/* Context switch counts: */
	unsigned long			nvcsw;
	unsigned long			nivcsw;

	/* Monotonic time in nsecs: */
	u64				start_time;

	/* Boot based time in nsecs: */
	u64				start_boottime;

	/* MM fault and swap info: this can arguably be seen as either mm-specific or thread-specific: */
	unsigned long			min_flt;
	unsigned long			maj_flt;

	/* Empty if CONFIG_POSIX_CPUTIMERS=n */
	struct posix_cputimers		posix_cputimers;

#ifdef CONFIG_POSIX_CPU_TIMERS_TASK_WORK
	struct posix_cputimers_work	posix_cputimers_work;
#endif

	/* Process credentials: */

	/* Tracer's credentials at attach: */
	const struct cred __rcu		*ptracer_cred;

	/* Objective and real subjective task credentials (COW): */
	const struct cred __rcu		*real_cred;

	/* Effective (overridable) subjective task credentials (COW): */
	const struct cred __rcu		*cred;

#ifdef CONFIG_KEYS
	/* Cached requested key. */
	struct key			*cached_requested_key;
#endif

	/*
	 * executable name, excluding path.
	 *
	 * - normally initialized setup_new_exec()
	 * - access it with [gs]et_task_comm()
	 * - lock it with task_lock()
	 */
	char				comm[TASK_COMM_LEN]; // 可执行程序的名称

	struct nameidata		*nameidata;

#ifdef CONFIG_SYSVIPC
	struct sysv_sem			sysvsem;
	struct sysv_shm			sysvshm;
#endif
#ifdef CONFIG_DETECT_HUNG_TASK
	unsigned long			last_switch_count;
	unsigned long			last_switch_time;
#endif
	/* Filesystem information: */
	struct fs_struct		*fs; // 保存一个指向文件系统信息的指针

	/* Open file information: */
	struct files_struct		*files; // 指向进程的文件描述符的指针（一个进程应该可以打开多个文件，不应该是指针的指针么）

#ifdef CONFIG_IO_URING
	struct io_uring_task		*io_uring;
#endif

	/* Namespaces: */
	struct nsproxy			*nsproxy;

	/* Signal handlers: */
	struct signal_struct		*signal;
	struct sighand_struct __rcu		*sighand;
	sigset_t			blocked;
	sigset_t			real_blocked;
	/* Restored if set_restore_sigmask() was used: */
	sigset_t			saved_sigmask;
	struct sigpending		pending;
	unsigned long			sas_ss_sp;
	size_t				sas_ss_size;
	unsigned int			sas_ss_flags;

	struct callback_head		*task_works;

#ifdef CONFIG_AUDIT
#ifdef CONFIG_AUDITSYSCALL
	struct audit_context		*audit_context;
#endif
	kuid_t				loginuid;
	unsigned int			sessionid;
#endif
	struct seccomp			seccomp;
	struct syscall_user_dispatch	syscall_dispatch;

	/* Thread group tracking: */
	u64				parent_exec_id;
	u64				self_exec_id;

	/* Protection against (de-)allocation: mm, files, fs, tty, keyrings, mems_allowed, mempolicy: */
	spinlock_t			alloc_lock;

	/* Protection of the PI data structures: */
	raw_spinlock_t			pi_lock;

	struct wake_q_node		wake_q;

#ifdef CONFIG_RT_MUTEXES
	/* PI waiters blocked on a rt_mutex held by this task: */
	struct rb_root_cached		pi_waiters;
	/* Updated under owner's pi_lock and rq lock */
	struct task_struct		*pi_top_task;
	/* Deadlock detection and priority inheritance handling: */
	struct rt_mutex_waiter		*pi_blocked_on;
#endif

#ifdef CONFIG_DEBUG_MUTEXES
	/* Mutex deadlock detection: */
	struct mutex_waiter		*blocked_on;
#endif

#ifdef CONFIG_DEBUG_ATOMIC_SLEEP
	int				non_block_count;
#endif

#ifdef CONFIG_TRACE_IRQFLAGS
	struct irqtrace_events		irqtrace;
	unsigned int			hardirq_threaded;
	u64				hardirq_chain_key;
	int				softirqs_enabled;
	int				softirq_context;
	int				irq_config;
#endif
#ifdef CONFIG_PREEMPT_RT
	int				softirq_disable_cnt;
#endif

#ifdef CONFIG_LOCKDEP
# define MAX_LOCK_DEPTH			48UL
	u64				curr_chain_key;
	int				lockdep_depth;
	unsigned int			lockdep_recursion;
	struct held_lock		held_locks[MAX_LOCK_DEPTH];
#endif

#if defined(CONFIG_UBSAN) && !defined(CONFIG_UBSAN_TRAP)
	unsigned int			in_ubsan;
#endif

	/* Journalling filesystem info: */
	void				*journal_info;

	/* Stacked block device info: */
	struct bio_list			*bio_list;

#ifdef CONFIG_BLOCK
	/* Stack plugging: */
	struct blk_plug			*plug;
#endif

	/* VM state: */
	struct reclaim_state		*reclaim_state;

	struct backing_dev_info		*backing_dev_info;

	struct io_context		*io_context;

#ifdef CONFIG_COMPACTION
	struct capture_control		*capture_control;
#endif
	/* Ptrace state: */
	unsigned long			ptrace_message;
	kernel_siginfo_t		*last_siginfo;

	struct task_io_accounting	ioac;
#ifdef CONFIG_PSI
	/* Pressure stall state */
	unsigned int			psi_flags;
#endif
#ifdef CONFIG_TASK_XACCT
	/* Accumulated RSS usage: */
	u64				acct_rss_mem1;
	/* Accumulated virtual memory usage: */
	u64				acct_vm_mem1;
	/* stime + utime since last update: */
	u64				acct_timexpd;
#endif
#ifdef CONFIG_CPUSETS
	/* Protected by ->alloc_lock: */
	nodemask_t			mems_allowed;
	/* Sequence number to catch updates: */
	seqcount_spinlock_t		mems_allowed_seq;
	int				cpuset_mem_spread_rotor;
	int				cpuset_slab_spread_rotor;
#endif
#ifdef CONFIG_CGROUPS
	/* Control Group info protected by css_set_lock: */
	struct css_set __rcu		*cgroups;
	/* cg_list protected by css_set_lock and tsk->alloc_lock: */
	struct list_head		cg_list;
#endif
#ifdef CONFIG_X86_CPU_RESCTRL
	u32				closid;
	u32				rmid;
#endif
#ifdef CONFIG_FUTEX
	struct robust_list_head __user	*robust_list;
#ifdef CONFIG_COMPAT
	struct compat_robust_list_head __user *compat_robust_list;
#endif
	struct list_head		pi_state_list;
	struct futex_pi_state		*pi_state_cache;
	struct mutex			futex_exit_mutex;
	unsigned int			futex_state;
#endif
#ifdef CONFIG_PERF_EVENTS
	struct perf_event_context	*perf_event_ctxp[perf_nr_task_contexts];
	struct mutex			perf_event_mutex;
	struct list_head		perf_event_list;
#endif
#ifdef CONFIG_DEBUG_PREEMPT
	unsigned long			preempt_disable_ip;
#endif
#ifdef CONFIG_NUMA
	/* Protected by alloc_lock: */
	struct mempolicy		*mempolicy;
	short				il_prev;
	short				pref_node_fork;
#endif
#ifdef CONFIG_NUMA_BALANCING
	int				numa_scan_seq;
	unsigned int			numa_scan_period;
	unsigned int			numa_scan_period_max;
	int				numa_preferred_nid;
	unsigned long			numa_migrate_retry;
	/* Migration stamp: */
	u64				node_stamp;
	u64				last_task_numa_placement;
	u64				last_sum_exec_runtime;
	struct callback_head		numa_work;

	/*
	 * This pointer is only modified for current in syscall and
	 * pagefault context (and for tasks being destroyed), so it can be read
	 * from any of the following contexts:
	 *  - RCU read-side critical section
	 *  - current->numa_group from everywhere
	 *  - task's runqueue locked, task not running
	 */
	struct numa_group __rcu		*numa_group;

	/*
	 * numa_faults is an array split into four regions:
	 * faults_memory, faults_cpu, faults_memory_buffer, faults_cpu_buffer
	 * in this precise order.
	 *
	 * faults_memory: Exponential decaying average of faults on a per-node
	 * basis. Scheduling placement decisions are made based on these
	 * counts. The values remain static for the duration of a PTE scan.
	 * faults_cpu: Track the nodes the process was running on when a NUMA
	 * hinting fault was incurred.
	 * faults_memory_buffer and faults_cpu_buffer: Record faults per node
	 * during the current scan window. When the scan completes, the counts
	 * in faults_memory and faults_cpu decay and these values are copied.
	 */
	unsigned long			*numa_faults;
	unsigned long			total_numa_faults;

	/*
	 * numa_faults_locality tracks if faults recorded during the last
	 * scan window were remote/local or failed to migrate. The task scan
	 * period is adapted based on the locality of the faults with different
	 * weights depending on whether they were shared or private faults
	 */
	unsigned long			numa_faults_locality[3];

	unsigned long			numa_pages_migrated;
#endif /* CONFIG_NUMA_BALANCING */

#ifdef CONFIG_RSEQ
	struct rseq __user *rseq;
	u32 rseq_sig;
	/*
	 * RmW on rseq_event_mask must be performed atomically
	 * with respect to preemption.
	 */
	unsigned long rseq_event_mask;
#endif

	struct tlbflush_unmap_batch	tlb_ubc;

	union {
		refcount_t		rcu_users;
		struct rcu_head		rcu;
	};

	/* Cache last used pipe for splice(): */
	struct pipe_inode_info		*splice_pipe;

	struct page_frag		task_frag;

#ifdef CONFIG_TASK_DELAY_ACCT
	struct task_delay_info		*delays;
#endif

#ifdef CONFIG_FAULT_INJECTION
	int				make_it_fail;
	unsigned int			fail_nth;
#endif
	/*
	 * When (nr_dirtied >= nr_dirtied_pause), it's time to call
	 * balance_dirty_pages() for a dirty throttling pause:
	 */
	int				nr_dirtied;
	int				nr_dirtied_pause;
	/* Start of a write-and-pause period: */
	unsigned long			dirty_paused_when;

#ifdef CONFIG_LATENCYTOP
	int				latency_record_count;
	struct latency_record		latency_record[LT_SAVECOUNT];
#endif
	/*
	 * Time slack values; these are used to round up poll() and
	 * select() etc timeout values. These are in nanoseconds.
	 */
	u64				timer_slack_ns;
	u64				default_timer_slack_ns;

#if defined(CONFIG_KASAN_GENERIC) || defined(CONFIG_KASAN_SW_TAGS)
	unsigned int			kasan_depth;
#endif

#ifdef CONFIG_KCSAN
	struct kcsan_ctx		kcsan_ctx;
#ifdef CONFIG_TRACE_IRQFLAGS
	struct irqtrace_events		kcsan_save_irqtrace;
#endif
#endif

#if IS_ENABLED(CONFIG_KUNIT)
	struct kunit			*kunit_test;
#endif

#ifdef CONFIG_FUNCTION_GRAPH_TRACER
	/* Index of current stored address in ret_stack: */
	int				curr_ret_stack;
	int				curr_ret_depth;

	/* Stack of return addresses for return function tracing: */
	struct ftrace_ret_stack		*ret_stack;

	/* Timestamp for last schedule: */
	unsigned long long		ftrace_timestamp;

	/*
	 * Number of functions that haven't been traced
	 * because of depth overrun:
	 */
	atomic_t			trace_overrun;

	/* Pause tracing: */
	atomic_t			tracing_graph_pause;
#endif

#ifdef CONFIG_TRACING
	/* State flags for use by tracers: */
	unsigned long			trace;

	/* Bitmask and counter of trace recursion: */
	unsigned long			trace_recursion;
#endif /* CONFIG_TRACING */

#ifdef CONFIG_KCOV
	/* See kernel/kcov.c for more details. */

	/* Coverage collection mode enabled for this task (0 if disabled): */
	unsigned int			kcov_mode;

	/* Size of the kcov_area: */
	unsigned int			kcov_size;

	/* Buffer for coverage collection: */
	void				*kcov_area;

	/* KCOV descriptor wired with this task or NULL: */
	struct kcov			*kcov;

	/* KCOV common handle for remote coverage collection: */
	u64				kcov_handle;

	/* KCOV sequence number: */
	int				kcov_sequence;

	/* Collect coverage from softirq context: */
	unsigned int			kcov_softirq;
#endif

#ifdef CONFIG_MEMCG
	struct mem_cgroup		*memcg_in_oom;
	gfp_t				memcg_oom_gfp_mask;
	int				memcg_oom_order;

	/* Number of pages to reclaim on returning to userland: */
	unsigned int			memcg_nr_pages_over_high;

	/* Used by memcontrol for targeted memcg charge: */
	struct mem_cgroup		*active_memcg;
#endif

#ifdef CONFIG_BLK_CGROUP
	struct request_queue		*throttle_queue;
#endif

#ifdef CONFIG_UPROBES
	struct uprobe_task		*utask;
#endif
#if defined(CONFIG_BCACHE) || defined(CONFIG_BCACHE_MODULE)
	unsigned int			sequential_io;
	unsigned int			sequential_io_avg;
#endif
	struct kmap_ctrl		kmap_ctrl;
#ifdef CONFIG_DEBUG_ATOMIC_SLEEP
	unsigned long			task_state_change;
# ifdef CONFIG_PREEMPT_RT
	unsigned long			saved_state_change;
# endif
#endif
	int				pagefault_disabled;
#ifdef CONFIG_MMU
	struct task_struct		*oom_reaper_list;
#endif
#ifdef CONFIG_VMAP_STACK
	struct vm_struct		*stack_vm_area;
#endif
#ifdef CONFIG_THREAD_INFO_IN_TASK
	/* A live task holds one reference: */
	refcount_t			stack_refcount;
#endif
#ifdef CONFIG_LIVEPATCH
	int patch_state;
#endif
#ifdef CONFIG_SECURITY
	/* Used by LSM modules for access restriction: */
	void				*security;
#endif
#ifdef CONFIG_BPF_SYSCALL
	/* Used by BPF task local storage */
	struct bpf_local_storage __rcu	*bpf_storage;
	/* Used for BPF run context */
	struct bpf_run_ctx		*bpf_ctx;
#endif

#ifdef CONFIG_GCC_PLUGIN_STACKLEAK
	unsigned long			lowest_stack;
	unsigned long			prev_lowest_stack;
#endif

#ifdef CONFIG_X86_MCE
	void __user			*mce_vaddr;
	__u64				mce_kflags;
	u64				mce_addr;
	__u64				mce_ripv : 1,
					mce_whole_page : 1,
					__mce_reserved : 62;
	struct callback_head		mce_kill_me;
	int				mce_count;
#endif

#ifdef CONFIG_KRETPROBES
	struct llist_head               kretprobe_instances;
#endif

#ifdef CONFIG_ARCH_HAS_PARANOID_L1D_FLUSH
	/*
	 * If L1D flush is supported on mm context switch
	 * then we use this callback head to queue kill work
	 * to kill tasks that are not running on SMT disabled
	 * cores
	 */
	struct callback_head		l1d_flush_kill;
#endif

	/*
	 * New fields for task_struct should be added above here, so that
	 * they are included in the randomized portion of task_struct.
	 */
	randomized_struct_fields_end

	/* CPU-specific state of this task: */
	struct thread_struct		thread;

	/*
	 * WARNING: on x86, 'thread_struct' contains a variable-sized
	 * structure.  It *MUST* be at the end of 'task_struct'.
	 *
	 * Do not put anything below here!
	 */
};
```

#### 进程的生命周期

内核中对进程生命周期的定义和之前学的经典操作系统的定义略有不同：

- TASK_RUNNING（可运行态或就绪态或正在运行态）：内核对当前正在运行的进程没有给出一个明确的状态。
- TASK_INTERRUPTIBLE：进程进入睡眠状态来等待某些条件达成或某个资源被释放，一旦条件满足，内核将该状态的进程设置为 TASK_RUNNING 队列。
- TASK_UNINTERRUPTIBLE：进程在睡眠时不受干扰，对信号不做任何反映。
- __TASK_STOPPED：进程已停止运行。
- EXIT_ZOMBIE：进程已消亡，但对应的 `task_struct` 还没有释放。子进程退出时，父进程可以通过 `wait` 和 `waitpid` 来获取子进程消亡的原因。

#### 进程标识

在[轻量级进程](#轻量级进程)中介绍了，内核中没有专门用来描述线程的数据结构，而是使用线程组来表示多线程的进程。一个线程组中的线程的 pid 表示唯一的表示，而 tgid 则和该组中第一个进程的 pid 相同。因为根据 POSIX 标准中的规定，一个多线程应用程序中所有的线程必须拥有相同的 PID，这样可以把指定信号发送给组里所有的线程，通过 tgid 的方式就可以完成这一规定。通过如下两个接口可以获取当前线程对应的 pid 和 tgid。

```c
/* Get the process ID of the calling process.  */
extern __pid_t getpid (void) __THROW;

/* Return the kernel thread ID (TID) of the current thread.  */
extern __pid_t gettid (void) __THROW;
```

#### 进程间的关系

关于 0 号、1 号、2 号进程在这篇[文章](#https://github.com/UtopianFuture/UtopianFuture.github.io/blob/master/kernel/kernel_init.md)中已经分析过了，这里补充一个有意思的点，即 0 号进程是怎样初始化的。之前只知道 0 号进程负责最开始的系统初始化，但没有仔细想过它是怎样初始化的。

```c
asmlinkage __visible void __init start_kernel(void)
{
	...

	set_task_stack_end_magic(&init_task);

	...
}
```

在 `start_kernel` 的最开始 `init_task` 进程就开始工作了。它是通过一个宏来预先静态赋值的。

```c
/*
 *  INIT_TASK is used to set up the first task table, touch at
 * your own risk!. Base=0, limit=0x1fffff (=2MB)
 */
#define INIT_TASK(tsk)	\
{									\
	.state		= 0,						\
	.stack		= &init_thread_info,				\
	.usage		= ATOMIC_INIT(2),				\
	.flags		= PF_KTHREAD,					\
	.prio		= MAX_PRIO-20,					\
	.static_prio	= MAX_PRIO-20,					\
	.normal_prio	= MAX_PRIO-20,					\
	.policy		= SCHED_NORMAL,					\
	.cpus_allowed	= CPU_MASK_ALL,					\
	.nr_cpus_allowed= NR_CPUS,					\
	.mm		= NULL,						\
	.active_mm	= &init_mm,					\

	...

	INIT_NUMA_BALANCING(tsk)					\
	INIT_KASAN(tsk)							\
}
```

#### 获取当前进程

内核中通过 `current` 宏来获取当前进程的 `task_struct` 结构指针。

```c
static __always_inline struct task_struct *get_current(void)
{
	return this_cpu_read_stable(current_task);
}

#define current get_current()
```

好吧，这个我不懂。

#### 内核线程

内核线程就是运行在内核地址空间的进程，它没有独立的进程地址空间，所有的内核线程都共享内核地址空间，即 `task_struct` 结构中的 `mm_struct` 指针设为  null。但内核线程也和普通进程一样参与系统调度。

### 进程创建与终止

#### 创建进程

内核为进程的创建提供了相应的系统调用，如 `sys_fork`, `sys_vfork` 以及 `sys_clone` 等，C 标准库提供了这些系统调用的封装函数。下面简单介绍这几种系统调用。

##### fork

```c
#ifdef __ARCH_WANT_SYS_FORK
SYSCALL_DEFINE0(fork)
{
#ifdef CONFIG_MMU
	struct kernel_clone_args args = {
		.exit_signal = SIGCHLD, // 其他参数都是默认的，只是需要设置 SIGCHLD 标志位
	};

	return kernel_clone(&args);
#else
	/* can not support in nommu mode */
	return -EINVAL;
#endif
}
#endif
```

通过 COW 技术创建子进程，子进程只会复制父进程的页表，而不会复制页面内容。当它们开始执行各自的程序时，它们的进程地址空间才开始分道扬镳。

`fork` 函数会有两次返回，一次在父进程中，一次在子进程中。如果返回值为 0，说明这是子进程；如果返回值为正数，说明这是父进程，父进程会返回子进程的 pid；如果返回 -1，表示创建失败。

当然，只复制页表在某些情况下还是会比较慢，后来就有了 `vfork` 和 `clone`。

##### vfork

和  `fork` 类似，但是 `vfork` 的父进程会一直阻塞，知道子进程调用 `exit` 或 `execve` 为止，其可以避免复制父进程的页表项。

```c
#ifdef __ARCH_WANT_SYS_VFORK
SYSCALL_DEFINE0(vfork)
{
	struct kernel_clone_args args = {
        // CLONE_VFORK 表示父进程会被挂起，直至子进程释放虚拟内存
        // CLONE_VM 表示父、子进程执行在相同的进程地址空间中
		.flags		= CLONE_VFORK | CLONE_VM,
		.exit_signal	= SIGCHLD,
	};

	return kernel_clone(&args);
}
#endif
```

##### clone

`clone` 可以传递众多参数，有选择的继承父进程的资源。

```c
SYSCALL_DEFINE5(clone, unsigned long, clone_flags, unsigned long, newsp,
		 int __user *, parent_tidptr,
		 int __user *, child_tidptr,
		 unsigned long, tls)
{
	struct kernel_clone_args args = {
		.flags		= (lower_32_bits(clone_flags) & ~CSIGNAL),
		.pidfd		= parent_tidptr,
		.child_tid	= child_tidptr,
		.parent_tid	= parent_tidptr,
		.exit_signal	= (lower_32_bits(clone_flags) & CSIGNAL),
		.stack		= newsp,
		.tls		= tls,
	};

	return kernel_clone(&args);
}
```

##### 关键函数kernel_clone

在 5.15 的内核中，这些创建用户态进程和内核线程的接口最后都是调用 `kernel_clone`，只是传入的参数不一样。和书中介绍的不一样，5.15 的内核传入 `kernel_clone` 的参数是 `kernel_clone_args`，而不是之前的多个型参。

```c
struct kernel_clone_args {
	u64 flags; // 创建进程的标志位
	int __user *pidfd;
	int __user *child_tid;
	int __user *parent_tid; // 父子进程的 tid
	int exit_signal;
	unsigned long stack; // 用户栈的起始地址
	unsigned long stack_size; // 用户栈的大小
	unsigned long tls; // 线程本地存储（？）
	pid_t *set_tid;
	/* Number of elements in *set_tid */
	size_t set_tid_size;
	int cgroup; // 这个应该是虚拟化支持的一种机制，之后再分析
	int io_thread;
	struct cgroup *cgrp;
	struct css_set *cset;
};
```

```c
/*
 *  Ok, this is the main fork-routine.
 *
 * It copies the process, and if successful kick-starts
 * it and waits for it to finish using the VM if required.
 *
 * args->exit_signal is expected to be checked for sanity by the caller.
 */
pid_t kernel_clone(struct kernel_clone_args *args)
{
	u64 clone_flags = args->flags;
	struct completion vfork;
	struct pid *pid;
	struct task_struct *p;
	int trace = 0;
	pid_t nr;

	...

	p = copy_process(NULL, trace, NUMA_NO_NODE, args); // 显而易见，这是关键函数
	add_latent_entropy();

	/*
	 * Do this prior waking up the new thread - the thread pointer
	 * might get invalid after that point, if the thread exits quickly.
	 */
	trace_sched_process_fork(current, p);

	pid = get_task_pid(p, PIDTYPE_PID);
	nr = pid_vnr(pid); // 从当前命名空间看到的 PID（？）

	if (clone_flags & CLONE_PARENT_SETTID)
		put_user(nr, args->parent_tid);

	if (clone_flags & CLONE_VFORK) {
		p->vfork_done = &vfork; // vfork 创建子进程时要保证子进程先运行
		init_completion(&vfork); // 这里使用一个 completion 变量来达到扣留父进程的目的
		get_task_struct(p); // init_completion 负责初始化该变量
	}

	wake_up_new_task(p); // 将新创建的进程加入到就绪队列接受调度、运行

	if (clone_flags & CLONE_VFORK) {
		if (!wait_for_vfork_done(p, &vfork)) // 对于 vfork，等待子进程调用 exec 或 exit
			ptrace_event_pid(PTRACE_EVENT_VFORK_DONE, pid);
	}

	put_pid(pid);
    // 现在子进程已经创建成功了，父、子进程都需要返回到用户空间执行
    // 而两者都会从该函数的返回处开始执行，所以 fork, vfork, clone 等系统调用都有 2 个返回值
    // 这里返回 nr，为子进程的 pid，所以其为父进程
    // 而另一个返回值为子进程被调度执行后的返回
	return nr;
}
```

##### 关键函数copy_process

这个函数很长，涉及的东西非常多，所以只关注目前认为更加重要的信息。

```c
/*
 * This creates a new process as a copy of the old one,
 * but does not actually start it yet.
 *
 * It copies the registers, and all the appropriate
 * parts of the process environment (as per the clone
 * flags). The actual kick-off is left to the caller.
 */
static __latent_entropy struct task_struct *copy_process(
					struct pid *pid,
					int trace,
					int node,
					struct kernel_clone_args *args)
{
	int pidfd = -1, retval;
	struct task_struct *p;
	struct multiprocess_signals delayed;
	struct file *pidfile = NULL;
	u64 clone_flags = args->flags;
	struct nsproxy *nsp = current->nsproxy;

	...

	retval = -ENOMEM;
	p = dup_task_struct(current, node); // 分配 task_struct
	if (!p)
		goto fork_out;
	if (args->io_thread) {
		/*
		 * Mark us an IO worker, and block any signal that isn't
		 * fatal or STOP
		 */
		p->flags |= PF_IO_WORKER;
		siginitsetinv(&p->blocked, sigmask(SIGKILL)|sigmask(SIGSTOP));
	}

	...

	retval = copy_creds(p, clone_flags); // 复制父进程的证书（？）

	/*
	 * If multiple threads are within copy_process(), then this check
	 * triggers too late. This doesn't hurt, the check is only there
	 * to stop root fork bombs.
	 */
	retval = -EAGAIN;
    // nr_threads 表示系统已经创建的进程数，max_threads 表示系统最多可以拥有的进程数
	if (data_race(nr_threads >= max_threads))
		goto bad_fork_cleanup_count;

	delayacct_tsk_init(p);	/* Must remain after dup_task_struct() */
	p->flags &= ~(PF_SUPERPRIV | PF_WQ_WORKER | PF_IDLE | PF_NO_SETAFFINITY); // 设置子进程的标志位
	p->flags |= PF_FORKNOEXEC; // 该标志表示这个进程暂时还不能执行
	INIT_LIST_HEAD(&p->children);
	INIT_LIST_HEAD(&p->sibling);
	rcu_copy_process(p);
	p->vfork_done = NULL;
	spin_lock_init(&p->alloc_lock);

	init_sigpending(&p->pending);

	...

	/* Perform scheduler related setup. Assign this task to a CPU. */
	retval = sched_fork(clone_flags, p); // 初始化与进程调度相关的结构
	if (retval)
		goto bad_fork_cleanup_policy;

	retval = perf_event_init_task(p, clone_flags);
	if (retval)
		goto bad_fork_cleanup_policy;
	retval = audit_alloc(p);
	if (retval)
		goto bad_fork_cleanup_perf;
	/* copy all the process information */
	shm_init_task(p);
	retval = security_task_alloc(p, clone_flags);
	if (retval)
		goto bad_fork_cleanup_audit;
	retval = copy_semundo(clone_flags, p);
	if (retval)
		goto bad_fork_cleanup_security;
	retval = copy_files(clone_flags, p); // 复制父进程打开文件信息
	if (retval)
		goto bad_fork_cleanup_semundo;
	retval = copy_fs(clone_flags, p); // 复制父进程 fs_struct 数据结构
	if (retval)
		goto bad_fork_cleanup_files;
	retval = copy_sighand(clone_flags, p); // 复制父进程的信号处理函数（？）
	if (retval)
		goto bad_fork_cleanup_fs;
	retval = copy_signal(clone_flags, p); // 复制父进程的信号系统
	if (retval)
		goto bad_fork_cleanup_sighand;
	retval = copy_mm(clone_flags, p); // 复制父进程的页表信息
	if (retval)
		goto bad_fork_cleanup_signal;
	retval = copy_namespaces(clone_flags, p); // 复制父进程的命名空间
	if (retval)
		goto bad_fork_cleanup_mm;
	retval = copy_io(clone_flags, p); // I/O 相关
	if (retval)
		goto bad_fork_cleanup_namespaces;
	retval = copy_thread(clone_flags, args->stack, args->stack_size, p, args->tls); // 设置内核栈等信息
	if (retval)
		goto bad_fork_cleanup_io;

	stackleak_task_init(p);

	if (pid != &init_struct_pid) {
		pid = alloc_pid(p->nsproxy->pid_ns_for_children, args->set_tid,
				args->set_tid_size);
		if (IS_ERR(pid)) {
			retval = PTR_ERR(pid);
			goto bad_fork_cleanup_thread;
		}
	}

	...

	/* ok, now we should be set up.. */
	p->pid = pid_nr(pid);
	if (clone_flags & CLONE_THREAD) {
		p->group_leader = current->group_leader;
		p->tgid = current->tgid;
	} else {
		p->group_leader = p;
		p->tgid = p->pid;
	}

	p->nr_dirtied = 0;
	p->nr_dirtied_pause = 128 >> (PAGE_SHIFT - 10);
	p->dirty_paused_when = 0;

	p->pdeath_signal = 0;
	INIT_LIST_HEAD(&p->thread_group);
	p->task_works = NULL;

	...

	p->start_time = ktime_get_ns();
	p->start_boottime = ktime_get_boottime_ns();

	/*
	 * Make it visible to the rest of the system, but dont wake it up yet.
	 * Need tasklist lock for parent etc handling!
	 */
	write_lock_irq(&tasklist_lock);

	/* CLONE_PARENT re-uses the old parent */
	if (clone_flags & (CLONE_PARENT|CLONE_THREAD)) {
		p->real_parent = current->real_parent;
		p->parent_exec_id = current->parent_exec_id;
		if (clone_flags & CLONE_THREAD)
			p->exit_signal = -1;
		else
			p->exit_signal = current->group_leader->exit_signal;
	} else {
		p->real_parent = current;
		p->parent_exec_id = current->self_exec_id;
		p->exit_signal = args->exit_signal;
	}

	klp_copy_process(p);

	sched_core_fork(p);

	spin_lock(&current->sighand->siglock);

	/*
	 * Copy seccomp details explicitly here, in case they were changed
	 * before holding sighand lock.
	 */
	copy_seccomp(p);

	rseq_fork(p, clone_flags);

	...

	/* Let kill terminate clone/fork in the middle */
	if (fatal_signal_pending(current)) {
		retval = -EINTR;
		goto bad_fork_cancel_cgroup;
	}

	/* past the last point of failure */
	if (pidfile)
		fd_install(pidfd, pidfile);

    // 这段代码不懂
	init_task_pid_links(p);
	if (likely(p->pid)) {
		ptrace_init_task(p, (clone_flags & CLONE_PTRACE) || trace);

		init_task_pid(p, PIDTYPE_PID, pid);
		if (thread_group_leader(p)) {
			init_task_pid(p, PIDTYPE_TGID, pid);
			init_task_pid(p, PIDTYPE_PGID, task_pgrp(current));
			init_task_pid(p, PIDTYPE_SID, task_session(current));

			if (is_child_reaper(pid)) {
				ns_of_pid(pid)->child_reaper = p;
				p->signal->flags |= SIGNAL_UNKILLABLE;
			}
			p->signal->shared_pending.signal = delayed.signal;
			p->signal->tty = tty_kref_get(current->signal->tty);
			/*
			 * Inherit has_child_subreaper flag under the same
			 * tasklist_lock with adding child to the process tree
			 * for propagate_has_child_subreaper optimization.
			 */
			p->signal->has_child_subreaper = p->real_parent->signal->has_child_subreaper ||
							 p->real_parent->signal->is_child_subreaper;
			list_add_tail(&p->sibling, &p->real_parent->children);
			list_add_tail_rcu(&p->tasks, &init_task.tasks);
			attach_pid(p, PIDTYPE_TGID);
			attach_pid(p, PIDTYPE_PGID);
			attach_pid(p, PIDTYPE_SID);
			__this_cpu_inc(process_counts);
		} else {
			current->signal->nr_threads++;
			atomic_inc(&current->signal->live);
			refcount_inc(&current->signal->sigcnt);
			task_join_group_stop(p);
			list_add_tail_rcu(&p->thread_group,
					  &p->group_leader->thread_group);
			list_add_tail_rcu(&p->thread_node,
					  &p->signal->thread_head);
		}
		attach_pid(p, PIDTYPE_PID);
		nr_threads++;
	}
	total_forks++;
	hlist_del_init(&delayed.node);
	spin_unlock(&current->sighand->siglock);
	syscall_tracepoint_update(p);
	write_unlock_irq(&tasklist_lock);

	proc_fork_connector(p);
	sched_post_fork(p);
	cgroup_post_fork(p, args);
	perf_event_fork(p);

	trace_task_newtask(p, clone_flags);
	uprobe_copy_process(p, clone_flags);

	copy_oom_score_adj(clone_flags, p);

	return p;

    ...
}
```

##### 关键函数dup_task_struct

`dup_task_struct` 为新进程分配一个进程描述符和内核栈。

```c
static struct task_struct *dup_task_struct(struct task_struct *orig, int node)
{
	struct task_struct *tsk;
	unsigned long *stack;
	struct vm_struct *stack_vm_area __maybe_unused;
	int err;

	if (node == NUMA_NO_NODE)
		node = tsk_fork_get_node(orig);
    // 分配 task_struct
    // 哈，调用 kmem_cache_alloc_node，即 slab 分配器
    // 之前的文章中就介绍过，slab 分配器分配小块内存，且分配的内存按照 cache 行对齐
	tsk = alloc_task_struct_node(node);
	if (!tsk)
		return NULL;

    // 分配内核栈，通过 __vmalloc_node_range 即 vmalloc 分配机制来分配内核栈
	stack = alloc_thread_stack_node(tsk, node);
	if (!stack)
		goto free_tsk;

	if (memcg_charge_kernel_stack(tsk))
		goto free_stack;

	stack_vm_area = task_stack_vm_area(tsk); // 这个就是该进程内核栈内存区域对应的空间描述符 -- vm_struct

	err = arch_dup_task_struct(tsk, orig); // 将父进程的 task_struct 直接复制到子进程的 task_struct

	/*
	 * arch_dup_task_struct() clobbers the stack-related fields.  Make
	 * sure they're properly initialized before using any stack-related
	 * functions again.
	 */
	tsk->stack = stack;

	...

	return NULL;
}
```

##### 关键函数copy_mm

这个函数就很好理解了。

```c
static int copy_mm(unsigned long clone_flags, struct task_struct *tsk)
{
	struct mm_struct *mm, *oldmm;

	tsk->min_flt = tsk->maj_flt = 0;
	tsk->nvcsw = tsk->nivcsw = 0;
#ifdef CONFIG_DETECT_HUNG_TASK
	tsk->last_switch_count = tsk->nvcsw + tsk->nivcsw;
	tsk->last_switch_time = 0;
#endif

	tsk->mm = NULL;
	tsk->active_mm = NULL;

	/*
	 * Are we cloning a kernel thread?
	 *
	 * We need to steal a active VM for that..
	 */
	oldmm = current->mm; // 获取父进程的进程地址空间描述符
	if (!oldmm)
		return 0;

	/* initialize the new vmacache entries */
	vmacache_flush(tsk);

	if (clone_flags & CLONE_VM) { // 如果是 fork，那么共用地址空间
		mmget(oldmm);
		mm = oldmm;
	} else { // 不然需要复制父进程的地址空间描述符及页表
		mm = dup_mm(tsk, current->mm);
		if (!mm)
			return -ENOMEM;
	}

	tsk->mm = mm;
	tsk->active_mm = mm;
	return 0;
}
```

##### 关键函数dup_mm

这个函数涉及到很多内存管理的知识，应该仔细分析，把它搞懂。

```c
/**
 * dup_mm() - duplicates an existing mm structure
 * @tsk: the task_struct with which the new mm will be associated.
 * @oldmm: the mm to duplicate.
 *
 * Allocates a new mm structure and duplicates the provided @oldmm structure
 * content into it.
 *
 * Return: the duplicated mm or NULL on failure.
 */
static struct mm_struct *dup_mm(struct task_struct *tsk,
				struct mm_struct *oldmm)
{
	struct mm_struct *mm;
	int err;

	mm = allocate_mm(); // 调用 slab 分配器创建一个 mm_struct
	if (!mm)
		goto fail_nomem;

    // 直接复制（？）
    // 复制数据结构的内容，而不是复制内存，有什么区别（？）
    // 区别在与只是 mm_struct 中的内容，它只是描述符，而不是真正的内容
    // 如重要的页表项还没有复制，这里只是复制的页表项的地址等等
	memcpy(mm, oldmm, sizeof(*mm));

    // 这里为什么又需要配置 mm，不是直接复制么
    // 还需要分配 pgd。pgd 指向该进程一级页表的基地址
    // 好吧，pgd 的分配也需要调用伙伴系统分配页面
    // 因为子进程的 mm_struct 不应该和父进程的完全一样，这里需要重新设置
	if (!mm_init(mm, tsk, mm->user_ns))
		goto fail_nomem;

    // 复制父进程的进程地址空间的页表到子进程，继续完成上面的工作
	err = dup_mmap(mm, oldmm);
	if (err)
		goto free_pt;

	mm->hiwater_rss = get_mm_rss(mm);
	mm->hiwater_vm = mm->total_vm;

	if (mm->binfmt && !try_module_get(mm->binfmt->module))
		goto free_pt;

	return mm;

    ...
}
```

##### 关键函数dup_mmap

这个函数复制父进程的进程地址空间的**页表**到子进程。

```c
#ifdef CONFIG_MMU
static __latent_entropy int dup_mmap(struct mm_struct *mm,
					struct mm_struct *oldmm)
{
	struct vm_area_struct *mpnt, *tmp, *prev, **pprev;
	struct rb_node **rb_link, *rb_parent;
	int retval;
	unsigned long charge;
	LIST_HEAD(uf);

	...

	/* No ordering required: file already has been exposed. */
	dup_mm_exe_file(mm, oldmm);

	mm->total_vm = oldmm->total_vm;
	mm->data_vm = oldmm->data_vm;
	mm->exec_vm = oldmm->exec_vm;
	mm->stack_vm = oldmm->stack_vm;

	rb_link = &mm->mm_rb.rb_node;
	rb_parent = NULL;
	pprev = &mm->mmap;

    ...

    // 遍历父进程所有的 VMA，将这些 VMA 逐个添加到子进程的 mm->mmap 中
	prev = NULL;
	for (mpnt = oldmm->mmap; mpnt; mpnt = mpnt->vm_next) {
		struct file *file;

        // 这应该是不需要复制的 VMA -- DONTCOPY
		if (mpnt->vm_flags & VM_DONTCOPY) {
			vm_stat_account(mm, mpnt->vm_flags, -vma_pages(mpnt));
			continue;
		}
		charge = 0;

        ...

		if (mpnt->vm_flags & VM_ACCOUNT) {
			unsigned long len = vma_pages(mpnt);

			if (security_vm_enough_memory_mm(oldmm, len)) /* sic */
				goto fail_nomem;
			charge = len;
		}
        // 为子进程创建新的 VMA
		tmp = vm_area_dup(mpnt);
		if (!tmp)
			goto fail_nomem;
		retval = vma_dup_policy(mpnt, tmp);
		if (retval)
			goto fail_nomem_policy;
		tmp->vm_mm = mm; // 一个个 VMA 添加
		retval = dup_userfaultfd(tmp, &uf);
		if (retval)
			goto fail_nomem_anon_vma_fork;
		if (tmp->vm_flags & VM_WIPEONFORK) {
			/*
			 * VM_WIPEONFORK gets a clean slate in the child.
			 * Don't prepare anon_vma until fault since we don't
			 * copy page for current vma.
			 */
			tmp->anon_vma = NULL;
		} else if (anon_vma_fork(tmp, mpnt)) // 创建子进程的 anon_vma，并通过枢纽 avc 连接父子进程
			goto fail_nomem_anon_vma_fork;
		tmp->vm_flags &= ~(VM_LOCKED | VM_LOCKONFAULT);
		file = tmp->vm_file;
		if (file) { // 该 vma 对应的 file
			struct address_space *mapping = file->f_mapping;

			get_file(file);
			i_mmap_lock_write(mapping);
			if (tmp->vm_flags & VM_SHARED)
				mapping_allow_writable(mapping);
			flush_dcache_mmap_lock(mapping);
			/* insert tmp into the share list, just after mpnt */
			vma_interval_tree_insert_after(tmp, mpnt,
					&mapping->i_mmap);
			flush_dcache_mmap_unlock(mapping);
			i_mmap_unlock_write(mapping);
		}

		...

		/*
		 * Link in the new vma and copy the page table entries.
		 */
		*pprev = tmp;
		pprev = &tmp->vm_next;
		tmp->vm_prev = prev;
		prev = tmp;

		__vma_link_rb(mm, tmp, rb_link, rb_parent); // 插入 vma
		rb_link = &tmp->vm_rb.rb_right;
		rb_parent = &tmp->vm_rb;

		mm->map_count++;
		if (!(tmp->vm_flags & VM_WIPEONFORK))
			retval = copy_page_range(tmp, mpnt); // 复制 4/5 级页表

		if (tmp->vm_ops && tmp->vm_ops->open)
			tmp->vm_ops->open(tmp);

		if (retval)
			goto out;
	}
	/* a new mm has just been created */
	retval = arch_dup_mmap(oldmm, mm);

    ...
}
```

##### 关键函数copy_thread

这里设置些架构相关的寄存器，涉及到 clone, vfork, fork 等系统调用返回用户态的寄存器状态以及返回到哪个处理函数进行系统态 - 用户态的切换，现在知道这个函数是 `ret_from_fork` 。

```c
int copy_thread(unsigned long clone_flags, unsigned long sp, unsigned long arg,
		struct task_struct *p, unsigned long tls)
{
	struct inactive_task_frame *frame;
	struct fork_frame *fork_frame; // 内核栈么
	struct pt_regs *childregs; // 这是 X86 所有的通用寄存器
	int ret = 0;

	childregs = task_pt_regs(p);
	fork_frame = container_of(childregs, struct fork_frame, regs);
	frame = &fork_frame->frame;

	frame->bp = encode_frame_pointer(childregs);
	frame->ret_addr = (unsigned long) ret_from_fork; // 在这里设置 clone,fork,vfork 等的返回地址
	p->thread.sp = (unsigned long) fork_frame;
	p->thread.io_bitmap = NULL;
	memset(p->thread.ptrace_bps, 0, sizeof(p->thread.ptrace_bps));

#ifdef CONFIG_X86_64
	current_save_fsgs();
	p->thread.fsindex = current->thread.fsindex;
	p->thread.fsbase = current->thread.fsbase;
	p->thread.gsindex = current->thread.gsindex;
	p->thread.gsbase = current->thread.gsbase;

	savesegment(es, p->thread.es);
	savesegment(ds, p->thread.ds);
#else
	p->thread.sp0 = (unsigned long) (childregs + 1);
	/*
	 * Clear all status flags including IF and set fixed bit. 64bit
	 * does not have this initialization as the frame does not contain
	 * flags. The flags consistency (especially vs. AC) is there
	 * ensured via objtool, which lacks 32bit support.
	 */
	frame->flags = X86_EFLAGS_FIXED;
#endif

	/* Kernel thread ? */
	if (unlikely(p->flags & PF_KTHREAD)) {
		p->thread.pkru = pkru_get_init_value();
		memset(childregs, 0, sizeof(struct pt_regs));
		kthread_frame_init(frame, sp, arg);
		return 0;
	}

	/*
	 * Clone current's PKRU value from hardware. tsk->thread.pkru
	 * is only valid when scheduled out.
	 */
	p->thread.pkru = read_pkru();

	frame->bx = 0;
	*childregs = *current_pt_regs();
    // 这里设置 rax 寄存器为 0，而 rax 是保存返回值的寄存器
    // 所以 fork,clone,vfork 系统调用子进程的返回值是 0
	childregs->ax = 0;
	if (sp)
		childregs->sp = sp;

#ifdef CONFIG_X86_32
	task_user_gs(p) = get_user_gs(current_pt_regs());
#endif

	...

	return ret;
}
```

#### 进程创建后的返回



#### 终止进程

进程的终止主要有 2 中方法：

- 自愿终止
  - 从 `main` 函数返回，连接程序会自动的添加 `exit` 系统调用
  - 主动调用 `exit` 系统调用
- 被动终止
  - 进程受到一个自己不能处理的信号
  - 进程在内核态执行时产生一个异常
  - 进程收到 SIGKILL 等终止信号

当一个进程终止时，内核会释放它所有占用的资源（`task_struct` 视情况而定），并把这个消息传递给父进程。

### 进程调度

### Reference
