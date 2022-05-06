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
  - [终止进程](#终止进程)
- [进程调度](#进程调度)
  - [调度策略](#调度策略)
  - [经典调度算法](#经典调度算法)
- [CFS](#CFS)
  - [vruntime](#vruntime)
  - [相关数据结构](#相关数据结构)
    - [sched_entity](#sched_entity)
    - [rq](#rq)
    - [cfs_rq](#cfs_rq)
    - [sched_class](#sched_class)
  - [进程创建中的相关初始化](#进程创建中的相关初始化)
    - [关键函数sched_fork](#关键函数sched_fork)
    - [关键函数task_fork_fair](#关键函数task_fork_fair)
    - [关键函数update_curr](#关键函数update_curr)
  - [进程加入调度器](#进程加入调度器)
    - [关键函数wake_up_new_task](#关键函数wake_up_new_task)
    - [关键函数enqueue_task_fair](#关键函数enqueue_task_fair)
    - [关键函数enqueue_entity](#关键函数enqueue_entity)
  - [进程调度](#进程调度)
    - [关键函数__schedule](#关键函数__schedule)
    - [关键函数pick_next_entity](#关键函数pick_next_entity)
  - [进程切换](#进程切换)
- [Reference](#Reference)

### 基本概念

这里介绍一些我个人觉得很容易混淆和有趣的概念。

#### 轻量级进程

线程称为轻量级进程，它是操作系统调度的最小单元，通常一个进程可以拥有多个线程，即线程对应一个进程描述符，而进程对应一个或一组进程描述符。内核并没有特别的调度算法或定义特别的数据结构来表示线程，线程和进程都使用相同的进程描述符数据结构。内核中使用 `clone` 来创建线程，其工作方式和创建进程的 `fork` 类似。但 `clone` 会确定哪些资源和父进程共享，哪些资源为线程独享。

#### task_struct

啊！这个数据结构好长，又不能精简。

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

	void				*stack; // 内核栈的位置么
	refcount_t			usage;
	/* Per task flags (PF_*), defined further below: */
	unsigned int			flags; // 进程属性标志位。如进程退出时会设置 PF_EXITING
	unsigned int			ptrace;

#ifdef CONFIG_SMP
	int				on_cpu; // 表示进程处于运行态
	struct __call_single_node	wake_entry;
#ifdef CONFIG_THREAD_INFO_IN_TASK
	/* Current CPU: */
	unsigned int			cpu; // 进程运行在哪个 CPU
#endif
	unsigned int			wakee_flips; // 用于 wake affine 特性（？）
	unsigned long			wakee_flip_decay_ts; // 记录上一次 wakee_flips 的时间
	struct task_struct		*last_wakee; // 用于记录上一次唤醒的是哪个进程

	/*
	 * recent_used_cpu is initially set as the last CPU used by a task
	 * that wakes affine another task. Waker/wakee relationships can
	 * push tasks around a CPU where each wakeup moves to the next one.
	 * Tracking a recently used CPU allows a quick search for a recently
	 * used CPU that may be idle.
	 */
	int				recent_used_cpu;
	int				wake_cpu; // 进程上次运行在哪个 CPU 上
#endif
	int				on_rq; // 设置进程的状态

	int				prio; // 进程的动态优先级。这是调度类考（？）虑的优先级
	int				static_prio; // 静态优先级，再进程启动时分配
	int				normal_prio; // 基于 static_prio 和调度策略计算出来的优先级，子进程初始化时继承该优先级
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

...

#ifdef CONFIG_PREEMPT_NOTIFIERS
	/* List of struct preempt_notifier: */
	struct hlist_head		preempt_notifiers;
#endif

...

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

...

#ifdef CONFIG_TASKS_TRACE_RCU
	int				trc_reader_nesting; // RCU 到底是啥
	int				trc_ipi_to_cpu;
	union rcu_special		trc_reader_special;
	bool				trc_reader_checked;
	struct list_head		trc_holdout_list;
#endif /* #ifdef CONFIG_TASKS_TRACE_RCU */

	struct sched_info		sched_info; // 调度相关信息

	struct list_head		tasks; // 所有的 task 放在一个 list 么
#ifdef CONFIG_SMP
	struct plist_node		pushable_tasks;
	struct rb_node			pushable_dl_tasks;
#endif

	struct mm_struct		*mm; // 哈哈，这个就很熟悉了
	struct mm_struct		*active_mm; // 这个和 mm 有什么区别

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
	.cpus_allowed	= CPU_MASK_ALL,					\ // 该进程可以在哪个 CPU 上运行
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

内核线程就是运行在内核地址空间的进程，它**没有独立的进程地址空间**，所有的内核线程都共享内核地址空间，即 `task_struct` 结构中的 `mm_struct` 指针设为  null。但内核线程也和普通进程一样参与系统调度。

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
	retval = sched_fork(clone_flags, p); // 初始化与进程调度相关的结构，下面分析
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

#### 终止进程

进程的终止主要有 2 中方法：

- 自愿终止
  - 从 `main` 函数返回，连接程序会自动的添加 `exit` 系统调用
  - 主动调用 `exit` 系统调用
- 被动终止
  - 进程收到一个自己不能处理的信号
  - 进程在内核态执行时产生一个异常
  - 进程收到 SIGKILL 等终止信号

当一个进程终止时，内核会释放它所有占用的资源（`task_struct` 视情况而定），并把这个消息传递给父进程。

### 进程调度

#### 调度策略

进程调度依赖于调度策略，内核将相同的调度策略抽象成调度类，不同类型的进程采用不同的调度策略，目前内核默认实现 5 种调度类。

|  调度类  |                调度策略                 |                   适用范围                   |                             说明                             |
| :------: | :-------------------------------------: | :------------------------------------------: | :----------------------------------------------------------: |
|   stop   |                   无                    | 最高优先级的进程，比 deadline 进程的优先级高 | 『1』可以抢占任何进程；『2』在每个 CPU 上实现一个名为 “migration/N” 的内核线程（这不就是迁移线程么）。该内核线程的优先级最高，可以抢占任何进程的运行，一般用来运行特殊的功能；『3』用于负载均衡机制中的进程迁移、softlockup 检测、CPU 热拔插、RCU 等 |
| deadline |             SCHED_DEADLINE              |      最高优先级的实时进程。优先级为 -1       |     用于调度有严格时间要求的实时进程，如视频编码/译码等      |
| realtime |          SCHED_FIFO、SCHED_RR           |        普通实时进程。优先级为 0 ~ 99         |           用于普通的实时进程，如 IRQ 线程化（？）            |
|   CFS    | SCHED_NORMAL、 SCHED_BATCH、 SCHED_IDLE |         普通进程。优先级为 100 ~ 139         |                        由 CFS 来调度                         |
|   idle   |                   无                    |               最低优先级的进程               | 当继续队列中没有其他进程时进入 idle 调度类。idle 调度类会让 CPU 进入低功耗模式 |

- `SCHED_DEADLINE`：限期进程调度策略，使task选择`Deadline调度器`来调度运行；
- `SCHED_FIFO`：实时进程调度策略，先进先出调度没有时间片，没有更高优先级的情况下，只能等待主动让出CPU；
- `SCHED_RR`：实时进程调度策略，时间片轮转，进程用完时间片后加入优先级对应运行队列的尾部，把CPU让给同优先级的其他进程；
- `SCHED_NORMAL`：普通进程调度策略，使task选择`CFS调度器`来调度运行；
- `SCHED_BATCH`：普通进程调度策略，批量处理，使task选择`CFS调度器`来调度运行；
- `SCHED_IDLE`：普通进程调度策略，使task以最低优先级选择`CFS调度器`来调度运行；

#### 经典调度算法

现代操作系统的进程调度器的设计大多受多级反馈队列算法的影响。多级反馈队列算法的核心是把进程按优先级分成多个队列，相同优先级的进程在同一个队列。它最大的特点再与能够根据判断正在运行的进程属于哪种进程，如 I/O 消耗型或 CPU 消耗型，作出不同的反馈，然后动态的修改进程的优先级。

其有如下几条基本规则：

- 如果进程 A 的优先级大于 B，那么调度器选择 A；
- 如果 A 和 B 的优先级一样，那么使用轮转算法来选择；
- 当一个新进程进入调度器时，把它放入优先级最高的队列；
- 若一个进程完全占用了时间便，说明这是一个 CPU 消耗型的进程，需要将其优先级降一级（让出 CPU 么）；
- 若一个进程在时间便结束前放弃 CPU，说明这是一个 I/O 消耗型的进程，那么优先级不变；

这样设计的调度器会存在如下几个问题：

（1）如果 I/O 消耗型进程过多，可能会造成 CPU 消耗型进程饥饿；

（2）有些进程会欺骗调度器。一个 CPU 消耗型的进程在时间片快用完的时候请求 I/O，那么调度器会将其认定为 I/O 消耗型的进程，从而保留在高优先级的队列。大量这样进程会导致系统的交互性变差。

（3）有些进程在生命周期中一会是 I/O 消耗型，一会是 CPU 消耗型，调度器很难判断。

为了解决以上问题，多级反馈队列算法提出了改良方案：

- 每个时间周期 S 后，将系统中所有进程的优先级都提到最高，相当于每隔一段时间重启一次系统；
- 当一个进程使用完时间片后，不管它是否在时间片最末位发生 I/O 请求，都将其优先级降低；

虽说算法思想不难，但是在实际工程中最难的是参数如何确定和优化，如时间间隔 S。

### CFS

`Completely Fair Scheduler`，完全公平调度器，用于内核中普通进程的调度。

`CFS` 采用了红黑树算法来管理所有的调度实体`sched_entity`，算法效率为 `O(log(n))`。`CFS` 跟踪调度实体 `sched_entity` 的虚拟运行时间 `vruntime`，平等对待运行队列中的调度实体，将执行时间少的调度实体排列到红黑树的左边。调度实体通过 `enqueue_entity()` 和 `dequeue_entity()` 来进行红黑树的出队入队。每个 `sched_latency` 周期内，根据各个任务的权重值，可以计算出运行时间 `runtime`，运行时间可以转换成虚拟运行时间 `vruntime`。根据虚拟运行时间的大小，插入到 CFS 红黑树中，虚拟运行时间少的调度实体放置到左边，在下一次任务调度的时候，**选择虚拟运行时间少的调度实体来运行**。

#### vruntime

内核使用 0 ~ 139 表示进程的优先级，数值越低，优先级越高。优先级 0 ~ 99 分配给实时进程，100 ~ 139 分配给普通进程使用。另外，在用户空间有一个传统的变量 nice，它映射到普通进程的优先级，即 100 ~ 139。nice 值的范围是 -20 ~ 19，进程默认的 nice 值为 0，**nice 值越高，优先级越低，权重越低，反之亦然**。如果一个 CPU 密集型的进程 nice 值从 0 增加到 1，那么它相对于其他 nice 值为 0 的进程将减少 10% 的 CPU 时间。为了计算方便，内核约定 nice 值为 0 的进程权重为 1024，其他 nice 值对应的权重值可以通过查表的方式来获取，

```c
const int sched_prio_to_weight[40] = {
 /* -20 */     88761,     71755,     56483,     46273,     36291,
 /* -15 */     29154,     23254,     18705,     14949,     11916,
 /* -10 */      9548,      7620,      6100,      4904,      3906,
 /*  -5 */      3121,      2501,      1991,      1586,      1277,
 /*   0 */      1024,       820,       655,       526,       423,
 /*   5 */       335,       272,       215,       172,       137,
 /*  10 */       110,        87,        70,        56,        45,
 /*  15 */        36,        29,        23,        18,        15,
};
```

通过这个表定义的权重值就可以达到 nice 值增减 1，占用 CPU 时间增减 10%，这个可以自行计算。

这里有个问题，为什么有了优先级还要使用 nice 值？仅仅是因为优先级是内核态的变量，而 nice 值可以在用户态访问么？

```c
const u32 sched_prio_to_wmult[40] = {
 /* -20 */     48388,     59856,     76040,     92818,    118348,
 /* -15 */    147320,    184698,    229616,    287308,    360437,
 /* -10 */    449829,    563644,    704093,    875809,   1099582,
 /*  -5 */   1376151,   1717300,   2157191,   2708050,   3363326,
 /*   0 */   4194304,   5237765,   6557202,   8165337,  10153587,
 /*   5 */  12820798,  15790321,  19976592,  24970740,  31350126,
 /*  10 */  39045157,  49367440,  61356676,  76695844,  95443717,
 /*  15 */ 119304647, 148102320, 186737708, 238609294, 286331153,
};
```

那么这个表有何用途？

前面讲到 `CFS` 跟踪调度实体 `sched_entity` 的虚拟运行时间 `vruntime`，平等对待运行队列中的调度实体，将执行时间少的调度实体排列到红黑树的左边，在下一次任务调度的时候，选择虚拟运行时间少的调度实体来运行。也就是说 `vruntime` 是决定进程调度次序的关键变量，但为何要设计 `vruntime`，而不是直接使用 `runtime`？

在 `CFS` 中有一个计算 `vruntime` 的核心函数 `calc_delta_fair`，其调用 `__calc_delta`，

```c
/*
 * delta_exec * weight / lw.weight
 *   OR
 * (delta_exec * (weight * lw->inv_weight)) >> WMULT_SHIFT
 *
 * Either weight := NICE_0_LOAD and lw \e sched_prio_to_wmult[], in which case
 * we're guaranteed shift stays positive because inv_weight is guaranteed to
 * fit 32 bits, and NICE_0_LOAD gives another 10 bits; therefore shift >= 22.
 *
 * Or, weight =< lw.weight (because lw.weight is the runqueue weight), thus
 * weight/lw.weight <= 1, and therefore our shift will also be positive.
 */
// 该函数计算 vruntime，其中 delta_exec 表示实际运行时间，weight 表示 nice 值为 0 的权重值，lw 表示该进程的权重值
static u64 __calc_delta(u64 delta_exec, unsigned long weight, struct load_weight *lw)
{
	u64 fact = scale_load_down(weight);
	u32 fact_hi = (u32)(fact >> 32);
	int shift = WMULT_SHIFT;
	int fs;

	__update_inv_weight(lw);

	if (unlikely(fact_hi)) {
		fs = fls(fact_hi);
		shift -= fs;
		fact >>= fs;
	}

	fact = mul_u32_u32(fact, lw->inv_weight);

	fact_hi = (u32)(fact >> 32);
	if (fact_hi) {
		fs = fls(fact_hi);
		shift -= fs;
		fact >>= fs;
	}

	return mul_u64_u32_shr(delta_exec, fact, shift);
}
```

从这个函数的计算公式可以得出，nice 越大，计算出来的 `vruntime` 就越大，而调度器是选择虚拟运行时间少的调度实体来运行，所以 nice 值越大，优先级越低。随着 `vruntime` 的增长，优先级低的进程也有机会被调度。

还是不理解为什么要进入 `vruntime`。

#### 相关数据结构

##### sched_entity

这个数据结构描述了**进程作为调度实体**所需要的所有信息。

```c
struct sched_entity {
	/* For load-balancing: */
	struct load_weight		load; // 调度实体的权重
	struct rb_node			run_node; // 调度实体作为一个节点插入 CFS 的红黑树中
	struct list_head		group_node; // 就绪队列的链表。这种设计和 VMA 类似
	unsigned int			on_rq; // 当进程加入就绪队列时，该变量置 1，否则置 0

	u64				exec_start; // 计算调度实体虚拟时间的起始时间
	u64				sum_exec_runtime; // 调度实体的总运行时间，这是真实时间
	u64				vruntime; // 调度实体的虚拟运行时间
	u64				prev_sum_exec_runtime; // 上一次调度实体的总运行时间

	u64				nr_migrations; // 发生迁移的次数

	struct sched_statistics		statistics; // 统计信息

#ifdef CONFIG_FAIR_GROUP_SCHED
	int				depth;
	struct sched_entity		*parent; // 这也有 parent？
	/* rq on which this entity is (to be) queued: */
	struct cfs_rq			*cfs_rq; // 该调度实体属于哪个队列么
	/* rq "owned" by this entity/group: */
	struct cfs_rq			*my_q; // 指向归属于当前调度实体的 CFS 队列，用于包含子任务或子任务组
	/* cached value of my_q->h_nr_running */
	unsigned long			runnable_weight; // 进程在可运行状态的权重，即进程的权重
#endif

#ifdef CONFIG_SMP
	/*
	 * Per entity load average tracking.
	 *
	 * Put into separate cache line so it does not
	 * collide with read-mostly values above.
	 */
	struct sched_avg		avg;
#endif
};
```

##### rq

这个数据结构是每个 CPU 通用就绪队列的描述符。它包含了多个调度策略的就绪队列，可以理解为一个 CPU 总的就绪队列，所有相关的信息都可以在里面找到。

```c
struct rq {
	/* runqueue lock: */
	raw_spinlock_t		__lock;

	/*
	 * nr_running and cpu_load should be in the same cacheline because
	 * remote CPUs use both these fields when doing load calculation.
	 */
	unsigned int		nr_running; // 就绪队列中可运行进程的数量

    ...

#ifdef CONFIG_SMP
	unsigned int		ttwu_pending;
#endif
	u64			nr_switches; // 进程切换的次数

#ifdef CONFIG_UCLAMP_TASK
	/* Utilization clamp values based on CPU's RUNNABLE tasks */
	struct uclamp_rq	uclamp[UCLAMP_CNT] ____cacheline_aligned;
	unsigned int		uclamp_flags;
#define UCLAMP_FLAG_IDLE 0x01
#endif

	struct cfs_rq		cfs; // CFS 就绪队列
	struct rt_rq		rt; // 实时进程的就绪队列
	struct dl_rq		dl; // 时是进程的就绪队列，deadline 也是实时进程

#ifdef CONFIG_FAIR_GROUP_SCHED
	/* list of leaf cfs_rq on this CPU: */
	struct list_head	leaf_cfs_rq_list;
	struct list_head	*tmp_alone_branch;
#endif /* CONFIG_FAIR_GROUP_SCHED */

	/*
	 * This is part of a global counter where only the total sum
	 * over all CPUs matters. A task can increase this counter on
	 * one CPU and if it got migrated afterwards it may decrease
	 * it on another CPU. Always updated under the runqueue lock:
	 */
	unsigned int		nr_uninterruptible; // 不可中断状态的进程进入就绪队列的数量

	struct task_struct __rcu	*curr; // 正在运行的进程
	struct task_struct	*idle; // idle 进程，也就是 0 号进程
	struct task_struct	*stop; // stop 进程（还有这种进程，是干什么的）
	unsigned long		next_balance; // 下一次做负载均衡的时间（？）
	struct mm_struct	*prev_mm; // 进程切换时指向上一个进程的 mm

	unsigned int		clock_update_flags; // 用于更新就绪队列时钟的标志位
	u64			clock; // 每次时钟节拍到来时会更新这个时钟
	/* Ensure that all clocks are in the same cache line */
	u64			clock_task ____cacheline_aligned; // 计算 vruntime 时使用该时钟
	u64			clock_pelt;
	unsigned long		lost_idle_time;

	atomic_t		nr_iowait;

	...

#ifdef CONFIG_SMP
	struct root_domain		*rd; // 调度域的根（？）
	struct sched_domain __rcu	*sd;

    // CPU 对应普通进程的量化计算能力，系统大约会预留最高计算能力的 80% 给普通进程
    // 预留 20% 给实时进程
	unsigned long		cpu_capacity;
    // CPU 最高的量化计算能力，系统中拥有最强处理器能力的 CPU 通常量化为 1024（？）
	unsigned long		cpu_capacity_orig;

	struct callback_head	*balance_callback;

	unsigned char		nohz_idle_balance;
	unsigned char		idle_balance;

    // 若一个进程的实际算理大于 CPU 额定算力的 80%，那么这个进程称为不合适的进程（？）
	unsigned long		misfit_task_load;

	/* For active balancing */
	int			active_balance;
    // 用于负载均衡，表示迁移的目标 CPU
	int			push_cpu;
	struct cpu_stop_work	active_balance_work;

	/* CPU of this runqueue: */
	int			cpu; // 用于表示就绪队列运行在哪个 CPU 上
	int			online; // 表示 CPU 所处的状态

	struct list_head cfs_tasks; // 可运行状态的调度实体会添加到这个链表头中

	struct sched_avg	avg_rt;
	struct sched_avg	avg_dl;
#ifdef CONFIG_HAVE_SCHED_AVG_IRQ
	struct sched_avg	avg_irq;
#endif
#ifdef CONFIG_SCHED_THERMAL_PRESSURE
	struct sched_avg	avg_thermal;
#endif
	u64			idle_stamp;
	u64			avg_idle;

	unsigned long		wake_stamp;
	u64			wake_avg_idle;

	/* This is used to determine avg_idle's max value */
	u64			max_idle_balance_cost;
#endif /* CONFIG_SMP */

	...

	/* calc_load related fields */
	unsigned long		calc_load_update;
	long			calc_load_active;


#ifdef CONFIG_SCHEDSTATS
	/* latency stats */
	struct sched_info	rq_sched_info;
	unsigned long long	rq_cpu_time;
	/* could above be rq->cfs_rq.exec_clock + rq->rt_rq.rt_runtime ? */

	/* sys_sched_yield() stats */
	unsigned int		yld_count;

	/* schedule() stats */
	unsigned int		sched_count;
	unsigned int		sched_goidle;

	/* try_to_wake_up() stats */
	unsigned int		ttwu_count;
	unsigned int		ttwu_local;
#endif

#ifdef CONFIG_CPU_IDLE
	/* Must be inspected within a rcu lock section */
	struct cpuidle_state	*idle_state;
#endif

#ifdef CONFIG_SMP
	unsigned int		nr_pinned;
#endif
	unsigned int		push_busy;
	struct cpu_stop_work	push_work;

#ifdef CONFIG_SCHED_CORE
	/* per rq */
	struct rq		*core;
	struct task_struct	*core_pick;
	unsigned int		core_enabled;
	unsigned int		core_sched_seq;
	struct rb_root		core_tree;

	/* shared state -- careful with sched_core_cpu_deactivate() */
	unsigned int		core_task_seq;
	unsigned int		core_pick_seq;
	unsigned long		core_cookie;
	unsigned char		core_forceidle;
	unsigned int		core_forceidle_seq;
#endif
};
```

##### cfs_rq

CFS 调度域的就绪队列。

```c
struct cfs_rq {
	struct load_weight	load;
	unsigned int		nr_running; // 可运行状态的进程数量
    // 这个不理解（？）
	unsigned int		h_nr_running;      /* SCHED_{NORMAL,BATCH,IDLE} */
	unsigned int		idle_h_nr_running; /* SCHED_IDLE */

	u64			exec_clock; // 就绪队列的总运行时间
	u64			min_vruntime; // CFS 就绪队列中红黑树里最小的 vruntime 值

    ...

	struct rb_root_cached	tasks_timeline; // 红黑树，用于存放调度实体

	/*
	 * 'curr' points to currently running entity on this cfs_rq.
	 * It is set to NULL otherwise (i.e when none are currently running).
	 */
	struct sched_entity	*curr; // 当前正在运行的进程
	struct sched_entity	*next; // 进程切换的下一个进程
	struct sched_entity	*last; // 用于抢占内核，当其他进程抢占了当前进程是，last 指向这个抢占进程
	struct sched_entity	*skip;

#ifdef CONFIG_SMP
	/*
	 * CFS load tracking
	 */
	struct sched_avg	avg; // 计算负载相关

    ...

#endif /* CONFIG_SMP */

#ifdef CONFIG_FAIR_GROUP_SCHED
    // 指向CFS运行队列所属的 CPU RQ 运行队列
	struct rq		*rq;	/* CPU runqueue to which this cfs_rq is attached */

	/*
	 * leaf cfs_rqs are those that hold tasks (lowest schedulable entity in
	 * a hierarchy). Non-leaf lrqs hold other higher schedulable entities
	 * (like users, containers etc.)
	 *
	 * leaf_cfs_rq_list ties together list of leaf cfs_rq's in a CPU.
	 * This list is used during load balance.
	 */
	int			on_list;
	struct list_head	leaf_cfs_rq_list;
	struct task_group	*tg;	/* group that "owns" this runqueue */

	/* Locally cached copy of our task_group's idle value */
	int			idle;

    ...

#endif /* CONFIG_FAIR_GROUP_SCHED */
};
```

这三个重要的数据结构关系如下图：

![process_schedule.png](https://github.com/UtopianFuture/UtopianFuture.github.io/blob/master/image/process_schedule.png?raw=true)

除了这 3 个重要的数据结构，内核还为每中调度类定义了各自的方法集，这里先给出 CFS 的方法集，其定义在 `kernel/sched/fair.c` 中，idle 调度类的方法集在 	`kernel/sched/idle.c` 中，其他的应该差不多。

##### sched_class

```c
/*
 * All the scheduling class methods:
 */
DEFINE_SCHED_CLASS(fair) = {

	.enqueue_task		= enqueue_task_fair,
	.dequeue_task		= dequeue_task_fair,
	.yield_task		= yield_task_fair,
	.yield_to_task		= yield_to_task_fair,

	.check_preempt_curr	= check_preempt_wakeup,

	.pick_next_task		= __pick_next_task_fair,
	.put_prev_task		= put_prev_task_fair,
	.set_next_task          = set_next_task_fair,

#ifdef CONFIG_SMP
	.balance		= balance_fair,
	.pick_task		= pick_task_fair,
	.select_task_rq		= select_task_rq_fair,
	.migrate_task_rq	= migrate_task_rq_fair,

	.rq_online		= rq_online_fair,
	.rq_offline		= rq_offline_fair,

	.task_dead		= task_dead_fair,
	.set_cpus_allowed	= set_cpus_allowed_common,
#endif

	.task_tick		= task_tick_fair,
	.task_fork		= task_fork_fair,

	.prio_changed		= prio_changed_fair,
	.switched_from		= switched_from_fair,
	.switched_to		= switched_to_fair,

	.get_rr_interval	= get_rr_interval_fair,

	.update_curr		= update_curr_fair,

#ifdef CONFIG_FAIR_GROUP_SCHED
	.task_change_group	= task_change_group_fair,
#endif

#ifdef CONFIG_UCLAMP_TASK
	.uclamp_enabled		= 1,
#endif
};
```

这些回调函数之后都会用到，这里只是做个记录。

#### 进程创建中的相关初始化

[关键函数copy_process](#关键函数copy_process)中介绍了通过 `clone`, `vfork`, `fork` 等系统调用创建进程的过程，在创建的过程中也会初始化进程调度相关的数据结构。

```c
static __latent_entropy struct task_struct *copy_process(
					struct pid *pid,
					int trace,
					int node,
					struct kernel_clone_args *args)
{
	...

	/* Perform scheduler related setup. Assign this task to a CPU. */
	retval = sched_fork(clone_flags, p); // 初始化与进程调度相关的结构，下面分析
	if (retval)
		goto bad_fork_cleanup_policy;

    ...
}
```

下面分析一下 `sched_fork` 是怎样初始化调度器的。

##### 关键函数sched_fork

```c

 * fork()/clone()-time setup:
 */
int sched_fork(unsigned long clone_flags, struct task_struct *p)
{
	unsigned long flags;

     // 和之前的情况不同，这个看似关键的函数只是初始化 task_struct 中进程调度相关的数据结构
	__sched_fork(clone_flags, p);
	/*
	 * We mark the process as NEW here. This guarantees that
	 * nobody will actually run it, and a signal or other external
	 * event cannot wake it up and insert it on the runqueue either.
	 */
	p->__state = TASK_NEW;

	/*
	 * Make sure we do not leak PI boosting priority to the child.
	 */
	p->prio = current->normal_prio; // 设置优先级

	uclamp_fork(p);

	...

	if (dl_prio(p->prio)) // deadline 进程为什么出错？
		return -EAGAIN;
	else if (rt_prio(p->prio))
		p->sched_class = &rt_sched_class; // 实时进程
	else
		p->sched_class = &fair_sched_class; // 普通进程使用 CFS 调度类

	init_entity_runnable_average(&p->se); //

	/*
	 * The child is not yet in the pid-hash so no cgroup attach races,
	 * and the cgroup is pinned to this child due to cgroup_fork()
	 * is ran before sched_fork().
	 *
	 * Silence PROVE_RCU.
	 */
	raw_spin_lock_irqsave(&p->pi_lock, flags);
	rseq_migrate(p);
	/*
	 * We're setting the CPU for the first time, we don't migrate,
	 * so use __set_task_cpu().
	 */
	__set_task_cpu(p, smp_processor_id()); // 子进程开始所处的 CPU 应该就是父进程运行的 CPU
	if (p->sched_class->task_fork)
		p->sched_class->task_fork(p); // 该回调函数为 task_fork_fair，前面有说明
	raw_spin_unlock_irqrestore(&p->pi_lock, flags);

#ifdef CONFIG_SCHED_INFO
	if (likely(sched_info_on()))
		memset(&p->sched_info, 0, sizeof(p->sched_info)); // 统计信息直接复制
#endif
#if defined(CONFIG_SMP)
	p->on_cpu = 0; // on_cpu 表示程序是否处于运行态，这里进程还没有加入调度器开始运行
#endif
	init_task_preempt_count(p);
#ifdef CONFIG_SMP
	plist_node_init(&p->pushable_tasks, MAX_PRIO);
	RB_CLEAR_NODE(&p->pushable_dl_tasks);
#endif
	return 0;
}
```

##### 关键函数task_fork_fair

```c
static void task_fork_fair(struct task_struct *p)
{
	struct cfs_rq *cfs_rq;
	struct sched_entity *se = &p->se, *curr;
	struct rq *rq = this_rq();
	struct rq_flags rf;

	rq_lock(rq, &rf);
	update_rq_clock(rq);

	cfs_rq = task_cfs_rq(current); // 获取当前进程所在的 CFS 就绪队列
	curr = cfs_rq->curr;
	if (curr) {
		update_curr(cfs_rq);
		se->vruntime = curr->vruntime;
	}
	place_entity(cfs_rq, se, 1); // 根据情况对进程虚拟时间进行一些惩罚（？）

	if (sysctl_sched_child_runs_first && curr && entity_before(curr, se)) {
		/*
		 * Upon rescheduling, sched_class::put_prev_task() will place
		 * 'current' within the tree based on its new key value.
		 */
		swap(curr->vruntime, se->vruntime);
		resched_curr(rq);
	}

	se->vruntime -= cfs_rq->min_vruntime;
	rq_unlock(rq, &rf);
}
```

##### 关键函数update_curr

该函数传入的参数为当前进程所在的 CFS 就绪队列，其用于更新进程的 `vruntime`。

```c
/*
 * Update the current task's runtime statistics.
 */
static void update_curr(struct cfs_rq *cfs_rq)
{
	struct sched_entity *curr = cfs_rq->curr;
	u64 now = rq_clock_task(rq_of(cfs_rq));
	u64 delta_exec;

	delta_exec = now - curr->exec_start; // 哦，这才是执行时间

	curr->exec_start = now; // 现在就更新么，新进程不是还没有放入到调度器中？

	schedstat_set(curr->statistics.exec_max,
		      max(delta_exec, curr->statistics.exec_max));

	curr->sum_exec_runtime += delta_exec;
	schedstat_add(cfs_rq->exec_clock, delta_exec);

	curr->vruntime += calc_delta_fair(delta_exec, curr); // 前面有说明，根据实际执行时间和 nice 值计算 vruntime
	update_min_vruntime(cfs_rq);

	account_cfs_rq_runtime(cfs_rq, delta_exec);
}
```

##### 关键函数place_entity

根据情况对进程虚拟时间进行一些惩罚

```c
static void
place_entity(struct cfs_rq *cfs_rq, struct sched_entity *se, int initial)
{
	u64 vruntime = cfs_rq->min_vruntime; // CFS 就绪队列中红黑树里最小的 vruntime 值

	/*
	 * The 'current' period is already promised to the current tasks,
	 * however the extra weight of the new task will slow them down a
	 * little, place the new task so that it fits in the slot that
	 * stays open at the end.
	 */
	if (initial && sched_feat(START_DEBIT))
        // 需要更新最小值，使得低优先级的进程也能被调度
		vruntime += sched_vslice(cfs_rq, se);

	...

	/* ensure we never gain time by being placed backwards. */
	se->vruntime = max_vruntime(se->vruntime, vruntime); // 相当于确定新进程的权重
}
```

上面两个函数都是确定子进程的 `vruntime` 的。

#### 进程加入调度器

在[关键函数kernel_clone](#关键函数kernel_clone)中我们知道进程创建完后需要将其加入到就绪队列接受调度、运行，这里我们进一步分析 `wake_up_new_task`。

```c
pid_t kernel_clone(struct kernel_clone_args *args)
{
	...

	p = copy_process(NULL, trace, NUMA_NO_NODE, args); // 显而易见，这是关键函数

    ...

	wake_up_new_task(p); // 将新创建的进程加入到就绪队列接受调度、运行

	...

	return nr;
}
```

##### 关键函数wake_up_new_task

```c
void wake_up_new_task(struct task_struct *p)
{
	struct rq_flags rf;
	struct rq *rq;

	raw_spin_lock_irqsave(&p->pi_lock, rf.flags);
	WRITE_ONCE(p->__state, TASK_RUNNING);
#ifdef CONFIG_SMP
	/*
	 * Fork balancing, do it here and not earlier because:
	 *  - cpus_ptr can change in the fork path
	 *  - any previously selected CPU might disappear through hotplug
	 *
	 * Use __set_task_cpu() to avoid calling sched_class::migrate_task_rq,
	 * as we're not fully set-up yet.
	 */
    // 初始化为上次使用的 CPU，不过 p 是新进程，还没有使用，怎么搞？
    // 应该是初始化时设置为父进程使用的 CPU
	p->recent_used_cpu = task_cpu(p);
	rseq_migrate(p);
    // 重新设置子进程将要运行的 CPU
    // 其实 CPU 在 sched_fork 中已经设置过，那为什么要重新设置？
    // 因为在初始化的过程中，cpus_allowed 可能发生变化，即该进程能够运行在哪个 CPU 上
    // 亦或者是之前选择的 CPU 关闭了
    // select_task_rq 选择调度域中最空闲的 CPU，这个很好理解
	__set_task_cpu(p, select_task_rq(p, task_cpu(p), WF_FORK));
#endif
	rq = __task_rq_lock(p, &rf);
	update_rq_clock(rq);
	post_init_entity_util_avg(p);

	activate_task(rq, p, ENQUEUE_NOCLOCK); // 将子进程添加到调度器中，并将进程状态设为可执行
	trace_sched_wakeup_new(p);
	check_preempt_curr(rq, p, WF_FORK);
#ifdef CONFIG_SMP
	if (p->sched_class->task_woken) { // 为何在 sched_class 中唯独没有这个回调函数
		/*
		 * Nothing relies on rq->lock after this, so it's fine to
		 * drop it.
		 */
		rq_unpin_lock(rq, &rf);
		p->sched_class->task_woken(rq, p);
		rq_repin_lock(rq, &rf);
	}
#endif
	task_rq_unlock(rq, p, &rf);
}
```

##### 关键函数enqueue_task_fair

`activate_task` -> `enqueue_task`

这个函数是在 sched_class 中设置的回调函数，用于将调度实体加入到就绪队列的红黑树中。

```c
static void
enqueue_task_fair(struct rq *rq, struct task_struct *p, int flags)
{
	struct cfs_rq *cfs_rq;
	struct sched_entity *se = &p->se;
	int idle_h_nr_running = task_has_idle_policy(p);
	int task_new = !(flags & ENQUEUE_WAKEUP);

	...

    // 这个调度实体应该只有一个吧，为何要用 for 循环来遍历？
	for_each_sched_entity(se) {
		if (se->on_rq)
			break;
		cfs_rq = cfs_rq_of(se);
		enqueue_entity(cfs_rq, se, flags);

		cfs_rq->h_nr_running++;
		cfs_rq->idle_h_nr_running += idle_h_nr_running; // 为何 idle 数量也要增加

		...

		flags = ENQUEUE_WAKEUP;
	}

    // 为何要遍历 2 次？
    // 貌似上面那次是插入到红黑树中，这里是开始调度，根据 se_update_runnable 猜的
	for_each_sched_entity(se) {
		cfs_rq = cfs_rq_of(se);

		update_load_avg(cfs_rq, se, UPDATE_TG);
		se_update_runnable(se);
		update_cfs_group(se);

		cfs_rq->h_nr_running++;
		cfs_rq->idle_h_nr_running += idle_h_nr_running;

		...
	}

	/* At this point se is NULL and we are at root level*/
	add_nr_running(rq, 1);

	...

	hrtick_update(rq);
}
```

##### 关键函数enqueue_entity

```c
static void
enqueue_entity(struct cfs_rq *cfs_rq, struct sched_entity *se, int flags)
{
	bool renorm = !(flags & ENQUEUE_WAKEUP) || (flags & ENQUEUE_MIGRATED);
	bool curr = cfs_rq->curr == se;

	/*
	 * If we're the current task, we must renormalise before calling
	 * update_curr().
	 */
	if (renorm && curr)
		se->vruntime += cfs_rq->min_vruntime;

    // 更新当前进程的 vruntime，这个在 sched_fork 中就已经计算过了，为什么这里还要计算？
	update_curr(cfs_rq);

	/*
	 * Otherwise, renormalise after, such that we're placed at the current
	 * moment in time, instead of some random moment in the past. Being
	 * placed in the past could significantly boost this task to the
	 * fairness detriment of existing tasks.
	 */
	if (renorm && !curr)
		se->vruntime += cfs_rq->min_vruntime;

	/*
	 * When enqueuing a sched_entity, we must:
	 *   - Update loads to have both entity and cfs_rq synced with now.
	 *   - Add its load to cfs_rq->runnable_avg
	 *   - For group_entity, update its weight to reflect the new share of
	 *     its group cfs_rq
	 *   - Add its new weight to cfs_rq->load.weight
	 */
	update_load_avg(cfs_rq, se, UPDATE_TG | DO_ATTACH); // 如注释解释的，更新负载
	se_update_runnable(se);
	update_cfs_group(se);
	account_entity_enqueue(cfs_rq, se);

	if (flags & ENQUEUE_WAKEUP)
		place_entity(cfs_rq, se, 0);

	check_schedstat_required();
	update_stats_enqueue(cfs_rq, se, flags);
	check_spread(cfs_rq, se);
	if (!curr)
		__enqueue_entity(cfs_rq, se); // 将调度实体插入到红黑树中
	se->on_rq = 1; // 到这里进程才真正可执行

	/*
	 * When bandwidth control is enabled, cfs might have been removed
	 * because of a parent been throttled but cfs->nr_running > 1. Try to
	 * add it unconditionally.
	 */
	if (cfs_rq->nr_running == 1 || cfs_bandwidth_used())
		list_add_leaf_cfs_rq(cfs_rq);

	if (cfs_rq->nr_running == 1)
		check_enqueue_throttle(cfs_rq);
}
```

我们将整个添加过程用框图的方式整理一下，

![enqueue_entity.png](https://github.com/UtopianFuture/UtopianFuture.github.io/blob/master/image/enqueue_entity.png?raw=true)

#### 进程调度

以下 3 种情况可能会发起进程调度：

- 在阻塞操作中，如使用互斥量（mutex）、信号量（semaphore）、等待队列（waitqueue）等；
- 在中断返回前和系统调用返回用户空间时，检查 `TIF_NEED_RESCHED` 标志位以判断是否需要调度；
- 将要被唤醒的进程不会马上调用 `schedule`，而是会将其调度实体被添加到 CFS 的就绪队列中（这在前面就已经分析了），并且设置 `TIF_NEED_RESCHED` 标志位；

而被唤醒的进程的调度时机根据内核是否可以被抢占可分成两种情况：

- 内核可抢占
  - 如果唤醒动作（何为唤醒动作，将进程插入到就绪队列么）发生在系统调用或异常处理上下文中，在下一次调用 `preempt_enable` 时会检查是否需要抢占调度；
  - 如果唤醒动作发生在硬中断处理上下文中，硬件中断处理返回前会检查是否要抢占当前进程；
- 内核不可抢占
  - 当前进程调用 `cond_resched` 时会检查是否要调度；
  - 主动调用 `schedule`；

主动调用 `schedule` 的地方很多，

![call_schedule.png](https://github.com/UtopianFuture/UtopianFuture.github.io/blob/master/image/call_schedule.png?raw=true)

这是另一个例子，应该是内核线程创建子进程后返回，申请进程调用。

```
#0  schedule () at kernel/sched/core.c:6360
#1  0xffffffff810cc802 in kthreadd (unused=<optimized out>) at kernel/kthread.c:673
#2  0xffffffff81004572 in ret_from_fork () at arch/x86/entry/entry_64.S:295
```

`schedule` 函数如下：

```c
asmlinkage __visible void __sched schedule(void)
{
	struct task_struct *tsk = current;

	sched_submit_work(tsk); // 好吧，不知道这时干嘛的
	do {
		preempt_disable(); // 关闭内核抢占。为什么关闭、开启都是调用 barrier() 呢？
		__schedule(SM_NONE); // 调度的核心函数
		sched_preempt_enable_no_resched();
	} while (need_resched());
	sched_update_worker(tsk);
}
```

##### 关键函数__schedule

```c
static void __sched notrace __schedule(unsigned int sched_mode)
{
	struct task_struct *prev, *next;
	unsigned long *switch_count;
	unsigned long prev_state;
	struct rq_flags rf;
	struct rq *rq;
	int cpu;

	cpu = smp_processor_id();
	rq = cpu_rq(cpu);
	prev = rq->curr;

    // 判断当前进程是否处于 atomic 上下文中
    // 所谓 atomic 上下文包含硬件中断上下文、软中断上下文等
    // 因为 schedule 是一个可睡眠函数，在 atomic 中调用可能会导致中断无法返回，造成死锁
	schedule_debug(prev, !!sched_mode);

    ... // 这里是申请各种锁

	/* Promote REQ to ACT */
	rq->clock_update_flags <<= 1;
	update_rq_clock(rq);

	switch_count = &prev->nivcsw;

	/*
	 * We must load prev->state once (task_struct::state is volatile), such
	 * that:
	 *
	 *  - we form a control dependency vs deactivate_task() below.
	 *  - ptrace_{,un}freeze_traced() can change ->state underneath us.
	 */
	prev_state = READ_ONCE(prev->__state);
    // 如果本次调度不是抢占调度且当前进程处于非运行状态
	if (!(sched_mode & SM_MASK_PREEMPT) && prev_state) {
		if (signal_pending_state(prev_state, prev)) {
			WRITE_ONCE(prev->__state, TASK_RUNNING);
		} else {
			prev->sched_contributes_to_load =
				(prev_state & TASK_UNINTERRUPTIBLE) &&
				!(prev_state & TASK_NOLOAD) &&
				!(prev->flags & PF_FROZEN);

			if (prev->sched_contributes_to_load)
				rq->nr_uninterruptible++;

            // 如果当前进程主动调用 schedule，将其移出就绪队列
            // 否则直接选择下一个执行的进程
			deactivate_task(rq, prev, DEQUEUE_SLEEP | DEQUEUE_NOCLOCK);

			if (prev->in_iowait) {
				atomic_inc(&rq->nr_iowait);
				delayacct_blkio_start();
			}
		}
		switch_count = &prev->nvcsw;
	}

	next = pick_next_task(rq, prev, &rf); // 选择下一个执行的进程
	clear_tsk_need_resched(prev); // 清楚当前进程的 TIF_NEED_RESCHED 标志位
	clear_preempt_need_resched();
#ifdef CONFIG_SCHED_DEBUG
	rq->last_seen_need_resched_ns = 0;
#endif

	if (likely(prev != next)) {
		rq->nr_switches++;
		/*
		 * RCU users of rcu_dereference(rq->curr) may not see
		 * changes to task_struct made by pick_next_task().
		 */
		RCU_INIT_POINTER(rq->curr, next);

		++*switch_count;

		migrate_disable_switch(rq, prev);
		psi_sched_switch(prev, next, !task_on_rq_queued(prev));

		trace_sched_switch(sched_mode & SM_MASK_PREEMPT, prev, next);

		/* Also unlocks the rq: */
		rq = context_switch(rq, prev, next, &rf); // 进程上下文切换，下面分析
	}
    ...
}
```

##### 关键函数pick_next_entity

`pick_next_task_fair` 是 CFS 的选择函数，不同的调度类的选择函数不同，选择策略应该也不同。其会调用 `pick_next_entity`。CFS 选择红黑树中最左侧的调度实体。

```c
static struct sched_entity *
pick_next_entity(struct cfs_rq *cfs_rq, struct sched_entity *curr)
{
	struct sched_entity *left = __pick_first_entity(cfs_rq);
	struct sched_entity *se;

	/*
	 * If curr is set we have to see if its left of the leftmost entity
	 * still in the tree, provided there was anything in the tree at all.
	 */
	if (!left || (curr && entity_before(curr, left)))
		left = curr;

	se = left; /* ideally we run the leftmost entity */

	/*
	 * Avoid running the skip buddy, if running something else can
	 * be done without getting too unfair.
	 */
	if (cfs_rq->skip && cfs_rq->skip == se) {
		struct sched_entity *second;

		if (se == curr) {
			second = __pick_first_entity(cfs_rq);
		} else {
			second = __pick_next_entity(se);
			if (!second || (curr && entity_before(curr, second)))
				second = curr;
		}

		if (second && wakeup_preempt_entity(second, left) < 1)
			se = second;
	}

	if (cfs_rq->next && wakeup_preempt_entity(cfs_rq->next, left) < 1) {
		/*
		 * Someone really wants this to run. If it's not unfair, run it.
		 */
		se = cfs_rq->next;
	} else if (cfs_rq->last && wakeup_preempt_entity(cfs_rq->last, left) < 1) {
		/*
		 * Prefer last buddy, try to return the CPU to a preempted task.
		 */
		se = cfs_rq->last;
	}

	return se;
}
```

#### 进程切换

#### 调度节拍

#### 组调度机制

### Reference

[1] 奔跑吧 Linux 内核，卷 1：基础架构

[2] 内核版本：5.15-rc5，commitid: f6274b06e326d8471cdfb52595f989a90f5e888f

[3] https://blog.csdn.net/u013982161/article/details/51347944

[4] https://www.cnblogs.com/LoyenWang/p/12495319.html
