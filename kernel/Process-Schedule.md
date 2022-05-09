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
    - [关键函数context_switch](#关键函数context_switch)
    - [关键函数switch_mm](#关键函数switch_mm)
    - [关键函数switch_to](#关键函数switch_to)
    - [关键函数__switch_to](#关键函数__switch_to)
    - [thread_struct](#thread_struct)
- [负载计算](#负载计算)
  - [量化负载的计算](#量化负载的计算)
  - [实际算力的计算](#实际算力的计算)
  - [PELT算法](#PELT算法)
    - [历史累计衰减时间](#历史累计衰减时间)
    - [负载贡献](#负载贡献)
    - [sched_avg](#sched_avg)

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

    ...

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
	int				on_rq; // 设置进程的状态，on_rq = 1 表示进程处于可运行状态

	int				prio; // 进程的动态优先级。这是调度类考虑的优先级
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
    // 这个和 mm 有什么区别？
    // 是这样的，对于内核线程来说，没有进程地址空间描述符，但处于进程调度的需要
    // 需要借用一个进程的地址空间，所以有了 active_mm
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
	pid_t				tgid; // 进程组 id，和该组第一个进程的 pid 一样

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

    ...

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

    ...

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

    ...

#ifdef CONFIG_CGROUPS
	/* Control Group info protected by css_set_lock: */
	struct css_set __rcu		*cgroups;
	/* cg_list protected by css_set_lock and tsk->alloc_lock: */
	struct list_head		cg_list;
#endif

    ...

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

	...

	/*
	 * Time slack values; these are used to round up poll() and
	 * select() etc timeout values. These are in nanoseconds.
	 */
	u64				timer_slack_ns;
	u64				default_timer_slack_ns;

	...

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

	...

#ifdef CONFIG_MEMCG
	struct mem_cgroup		*memcg_in_oom;
	gfp_t				memcg_oom_gfp_mask;
	int				memcg_oom_order;

	/* Number of pages to reclaim on returning to userland: */
	unsigned int			memcg_nr_pages_over_high;

	/* Used by memcontrol for targeted memcg charge: */
	struct mem_cgroup		*active_memcg;
#endif

    ...

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

    ...

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
- TASK_UNINTERRUPTIBLE：进程在睡眠时不受干扰，对信号不做任何反应（那怎样唤醒它？）。
- __TASK_STOPPED：进程已停止运行。
- EXIT_ZOMBIE：进程已消亡，但对应的 `task_struct` 还没有释放。子进程退出时，父进程可以通过 `wait` 和 `waitpid` 来获取子进程消亡的原因。

#### 进程标识

在[轻量级进程](#轻量级进程)中介绍了，内核中没有专门用来描述线程的数据结构，而是使用线程组来表示多线程的进程。一个线程组中的线程的 pid 是唯一的表示，而 **tgid 则和该组中第一个进程的 pid 相同**。因为根据 POSIX 标准中的规定，**一个多线程应用程序中所有的线程必须拥有相同的 PID，这样可以把指定信号发送给组里所有的线程，通过 tgid 的方式就可以完成这一规定**。通过如下两个接口可以获取当前线程对应的 pid 和 tgid。

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

好吧，这个我不懂。X86 中有专门的指针来指向 `task_struct` 么？

#### 内核线程

内核线程就是运行在内核地址空间的进程，它**没有独立的进程地址空间**，所有的内核线程都共享内核地址空间，即 `task_struct` 结构中的 `mm_struct` 指针设为 null。但内核线程也和普通进程一样参与系统调度。

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

通过 COW 技术创建子进程，子进程**只会复制父进程的页表，而不会复制页面内容**。当它们开始执行各自的程序时，它们的进程地址空间才开始分道扬镳。

`fork` 函数会有两次返回，一次在父进程中，一次在子进程中。如果返回值为 0，说明这是子进程；如果返回值为正数，说明这是父进程，父进程会返回子进程的 pid；如果返回 -1，表示创建失败。

当然，只复制页表在某些情况下还是会比较慢，后来就有了 `vfork` 和 `clone`。

##### vfork

和  `fork` 类似，但是 `vfork` 的**父进程会一直阻塞**，直到子进程调用 `exit` 或 `execve` 为止，其可以避免复制父进程的页表项。

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

##### 关键函数 kernel_clone

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
    // 复制父进程 fs_struct 数据结构，copy_files 和 copy_fs 有什么区别么
    // 或者说打开文件信息和 fs_struct 有什么关系？
	retval = copy_fs(clone_flags, p);
	if (retval)
		goto bad_fork_cleanup_files;
	retval = copy_sighand(clone_flags, p); // 复制父进程的信号处理函数
	if (retval)
		goto bad_fork_cleanup_fs;
	retval = copy_signal(clone_flags, p); // 复制父进程的信号系统
	if (retval)
		goto bad_fork_cleanup_sighand;
	retval = copy_mm(clone_flags, p); // 复制父进程的页表信息
	if (retval)
		goto bad_fork_cleanup_signal;
	retval = copy_namespaces(clone_flags, p); // 复制父进程的命名空间，命名空间也很重要，需要深入了解
	if (retval)
		goto bad_fork_cleanup_mm;
	retval = copy_io(clone_flags, p); // I/O 相关
	if (retval)
		goto bad_fork_cleanup_namespaces;
	retval = copy_thread(clone_flags, args->stack, args->stack_size, p, args->tls); // 设置内核栈等信息
	if (retval)
		goto bad_fork_cleanup_io;

	stackleak_task_init(p);

	if (pid != &init_struct_pid) { // 从 kernel_clone 传入的 pid 是 null，所以这里要分配 pid
		pid = alloc_pid(p->nsproxy->pid_ns_for_children, args->set_tid,
				args->set_tid_size);
		if (IS_ERR(pid)) {
			retval = PTR_ERR(pid);
			goto bad_fork_cleanup_thread;
		}
	}

	...

	/* ok, now we should be set up.. */
	p->pid = pid_nr(pid); // 设置 pid
	if (clone_flags & CLONE_THREAD) { // 不是以当前进程为组长创建一个进程组的子进程
		p->group_leader = current->group_leader; // 而是创建当前进程的兄弟进程
		p->tgid = current->tgid;
	} else {
		p->group_leader = p; // 以当前进程为组长创建子线程
		p->tgid = p->pid; // 这里前面介绍过，tgid 等于进程组的第一个进程的 pid
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

	sched_core_fork(p); // 这些内容暂时不懂

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
    // 成功分配了 pid，这里是对 pid 进行一些列的操作，不理解，还有 pid_type 也不理解
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
#ifdef CONFIG_VMAP_STACK
	tsk->stack_vm_area = stack_vm_area;
#endif

	...

	return NULL;
}
```

##### 关键函数 copy_mm

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

	if (clone_flags & CLONE_VM) { // 如果是 vfork，那么共用地址空间
		mmget(oldmm); // 将 mm->mm_users 计算器加 1
		mm = oldmm; // 共用是怎么个共用法，都是只读么，如果需要修改再复制页表项？COW 的激进版么
	} else { // 不然需要复制父进程的地址空间描述符及页表
		mm = dup_mm(tsk, current->mm);
		if (!mm)
			return -ENOMEM;
	}

	tsk->mm = mm;
	tsk->active_mm = mm; // 可能是内核线程，需要借用 mm_struct
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

这里设置些架构相关的寄存器，涉及到 `clone`, `vfork`, `fork` 等系统调用返回用户态的寄存器状态以及返回到哪个处理函数进行系统态 - 用户态的切换，现在知道这个函数是 `ret_from_fork` 。

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

- `SCHED_DEADLINE`：限期进程调度策略，使 task 选择`Deadline调度器`来调度运行；
- `SCHED_FIFO`：实时进程调度策略，先进先出调度没有时间片，没有更高优先级的情况下，只能等待主动让出 CPU；
- `SCHED_RR`：实时进程调度策略，时间片轮转，进程用完时间片后加入优先级对应运行队列的尾部，把 CPU 让给同优先级的其他进程；
- `SCHED_NORMAL`：普通进程调度策略，使 task 选择`CFS调度器`来调度运行；
- `SCHED_BATCH`：普通进程调度策略，批量处理，使 task 选择`CFS调度器`来调度运行；
- `SCHED_IDLE`：普通进程调度策略，使 task 以最低优先级选择`CFS调度器`来调度运行；

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

还是不理解为什么要引入 `vruntime`。

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

    struct {
		raw_spinlock_t	lock ____cacheline_aligned;
		int		nr;
		unsigned long	load_avg;
		unsigned long	util_avg;
		unsigned long	runnable_avg;
	} removed;

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

```plain
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
		rq = context_switch(rq, prev, next, &rf); // 进程上下文切换
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

进程上下文包括执行该进程有关的各种寄存器、内核栈、`task_struct` 等数据结构，进程切换的核心函数是 `context_switch`。

##### 关键函数context_switch

```c
/*
 * context_switch - switch to the new MM and the new thread's register state.
 */
static __always_inline struct rq *
context_switch(struct rq *rq, struct task_struct *prev,
	       struct task_struct *next, struct rq_flags *rf)
{
	prepare_task_switch(rq, prev, next); // 最重要的应该是 WRITE_ONCE(next->on_cpu, 1);

	/*
	 * For paravirt, this is coupled with an exit in switch_to to
	 * combine the page table reload and the switch backend into
	 * one hypercall.
	 */
	arch_start_context_switch(prev); // 好吧，这个我不知道是干啥的

    // 这些是根据不同的切换要求选择不同的切换方式
    // lazy_tlb 是啥
    // mmgrab 又是啥
	/*
	 * kernel -> kernel   lazy + transfer active
	 *   user -> kernel   lazy + mmgrab() active
	 *
	 * kernel ->   user   switch + mmdrop() active
	 *   user ->   user   switch
	 */
    // 哦，内核线程是没有独立的地址空间的，可以以此判断
	if (!next->mm) {                                // to kernel
		enter_lazy_tlb(prev->active_mm, next); // 注释是说这个仅仅表示切换到内核线程或没有 mm 的执行环境

        // 之前就有这个问题，active_mm 和 mm 有什么区别？前面解释了
        // 这里是借用了前一个进程的用户地址空间，因为前一个进程也可能是内核线程
        // 所以这里借用 active_mm 而不是 mm
		next->active_mm = prev->active_mm;
		if (prev->mm)                           // from user
            // 增加 mm->mm_count 的计数值
            // Make sure that @mm will not get freed even after the owning task exits
            // mm_count: The number of references to &struct mm_struct
			mmgrab(prev->active_mm);
		else									// from kernel
			prev->active_mm = NULL;
	} else {                                        // to user
		membarrier_switch_mm(rq, prev->active_mm, next->mm);
		/*
		 * sys_membarrier() requires an smp_mb() between setting
		 * rq->curr / membarrier_switch_mm() and returning to userspace.
		 *
		 * The below provides this either through switch_mm(), or in
		 * case 'prev->active_mm == next->mm' through
		 * finish_task_switch()'s mmdrop().
		 */
		switch_mm_irqs_off(prev->active_mm, next->mm, next); // 该函数等同于 switch_mm

		if (!prev->mm) {                        // from kernel
			/* will mmdrop() in finish_task_switch(). */
			rq->prev_mm = prev->active_mm;
			prev->active_mm = NULL; // 不需要借用了么，那下次需要切换怎么办？
		}
	}

	rq->clock_update_flags &= ~(RQCF_ACT_SKIP|RQCF_REQ_SKIP);

	prepare_lock_switch(rq, next, rf);

	/* Here we just switch the register state and the stack. */
    // prev 进程被调度出去“睡觉”了
    // 这里利用 %rax 寄存器保存指向 prev 进程的指针
    // 使得在切换到 next 上下文后第 3 个参数 prev 仍然指向 prev 进程
    // 从而在 next 进程中能够给 prev 进程收尾
    // 但还有一个问题，prev 是一个指针，在进程页表都切换了的情况下，prev 能正确指向 prev 的 task_struct 么
    // 可能 task_struct 存储的内存保留下来了？
    // 也就是说，要在不同进程间传递信息除了共享内存，还能使用通用寄存器
	switch_to(prev, next, prev);
	barrier(); // 从这之后就执行 next 进程

	return finish_task_switch(prev); // smp_store_release(&prev->on_cpu, 0);
}
```

`switch_to` 是新旧进程的切换点，完成 next 进程栈切换后开始执行 next 进程。进程执行的处理器相关寄存器等内容保存到进程的硬件上下文数据结构中（没有在 `task_struct` 中找到对应的数据结构，原来是 `mm_struct->context`）。

一个特殊情况是新建进程，第一次执行的切入点在 `copy_thread` 中指定的 `ret_from_fork` 中，因此，当 `switch_to` 切换到新建进程中时，新进程从 `ret_from_fork` 开始执行。

##### 关键函数switch_mm

`switch_mm` 切换进程的地址空间，也就是切换 next 进程的页表到硬件页表中。这里还进行复杂的 tlb flush 操作，需要搞清楚。

好吧，虽然我理解 asid 机制，但还是看不懂这段代码，唯一能理解就是切换 cr3 寄存器。

```c
void switch_mm_irqs_off(struct mm_struct *prev, struct mm_struct *next,
			struct task_struct *tsk)
{
	struct mm_struct *real_prev = this_cpu_read(cpu_tlbstate.loaded_mm);
	u16 prev_asid = this_cpu_read(cpu_tlbstate.loaded_mm_asid);
	bool was_lazy = this_cpu_read(cpu_tlbstate_shared.is_lazy);
	unsigned cpu = smp_processor_id();
	u64 next_tlb_gen;
	bool need_flush;
	u16 new_asid;

	...

	if (was_lazy)
		this_cpu_write(cpu_tlbstate_shared.is_lazy, false);

	/*
	 * The membarrier system call requires a full memory barrier and
	 * core serialization before returning to user-space, after
	 * storing to rq->curr, when changing mm.  This is because
	 * membarrier() sends IPIs to all CPUs that are in the target mm
	 * to make them issue memory barriers.  However, if another CPU
	 * switches to/from the target mm concurrently with
	 * membarrier(), it can cause that CPU not to receive an IPI
	 * when it really should issue a memory barrier.  Writing to CR3
	 * provides that full memory barrier and core serializing
	 * instruction.
	 */
	if (real_prev == next) {
		VM_WARN_ON(this_cpu_read(cpu_tlbstate.ctxs[prev_asid].ctx_id) !=
			   next->context.ctx_id);

		/*
		 * Even in lazy TLB mode, the CPU should stay set in the
		 * mm_cpumask. The TLB shootdown code can figure out from
		 * cpu_tlbstate_shared.is_lazy whether or not to send an IPI.
		 */
		if (WARN_ON_ONCE(real_prev != &init_mm &&
				 !cpumask_test_cpu(cpu, mm_cpumask(next))))
			cpumask_set_cpu(cpu, mm_cpumask(next));

		/*
		 * If the CPU is not in lazy TLB mode, we are just switching
		 * from one thread in a process to another thread in the same
		 * process. No TLB flush required.
		 */
		if (!was_lazy)
			return;

		/*
		 * Read the tlb_gen to check whether a flush is needed.
		 * If the TLB is up to date, just use it.
		 * The barrier synchronizes with the tlb_gen increment in
		 * the TLB shootdown code.
		 */
		smp_mb();
		next_tlb_gen = atomic64_read(&next->context.tlb_gen);
		if (this_cpu_read(cpu_tlbstate.ctxs[prev_asid].tlb_gen) ==
				next_tlb_gen)
			return;

		/*
		 * TLB contents went out of date while we were in lazy
		 * mode. Fall through to the TLB switching code below.
		 */
		new_asid = prev_asid;
		need_flush = true;
	} else {
		/*
		 * Apply process to process speculation vulnerability
		 * mitigations if applicable.
		 */
		cond_mitigation(tsk);

		/*
		 * Stop remote flushes for the previous mm.
		 * Skip kernel threads; we never send init_mm TLB flushing IPIs,
		 * but the bitmap manipulation can cause cache line contention.
		 */
		if (real_prev != &init_mm) {
			VM_WARN_ON_ONCE(!cpumask_test_cpu(cpu,
						mm_cpumask(real_prev)));
			cpumask_clear_cpu(cpu, mm_cpumask(real_prev));
		}

		/*
		 * Start remote flushes and then read tlb_gen.
		 */
		if (next != &init_mm)
			cpumask_set_cpu(cpu, mm_cpumask(next));
		next_tlb_gen = atomic64_read(&next->context.tlb_gen);

		choose_new_asid(next, next_tlb_gen, &new_asid, &need_flush);

		/* Let nmi_uaccess_okay() know that we're changing CR3. */
		this_cpu_write(cpu_tlbstate.loaded_mm, LOADED_MM_SWITCHING);
		barrier();
	}

	if (need_flush) {
		this_cpu_write(cpu_tlbstate.ctxs[new_asid].ctx_id, next->context.ctx_id);
		this_cpu_write(cpu_tlbstate.ctxs[new_asid].tlb_gen, next_tlb_gen);
		load_new_mm_cr3(next->pgd, new_asid, true); // 切换 cr3 寄存器，就是切换页表

		trace_tlb_flush(TLB_FLUSH_ON_TASK_SWITCH, TLB_FLUSH_ALL);
	} else {
		/* The new ASID is already up to date. */
		load_new_mm_cr3(next->pgd, new_asid, false);

		trace_tlb_flush(TLB_FLUSH_ON_TASK_SWITCH, 0);
	}

	/* Make sure we write CR3 before loaded_mm. */
	barrier();

	this_cpu_write(cpu_tlbstate.loaded_mm, next);
	this_cpu_write(cpu_tlbstate.loaded_mm_asid, new_asid);

	if (next != real_prev) {
		cr4_update_pce_mm(next);
		switch_ldt(real_prev, next);
	}
}
```

##### 关键函数switch_to

`switch_to` 切换到 next 进程的内核态和硬件上下文。

```c
#define switch_to(prev, next, last)					\ // last is prev
do {									\
	((last) = __switch_to_asm((prev), (next)));			\
} while (0)
```

这里有两个问题：

- 为什么 `switch_to` 有 3 个参数，`prev` 和 `next` 就足够了，为何还要 `last` ？
- `switch_to` 函数后面的代码，如 `finish_task_switch` 由哪个进程执行？因为 `switch_to` 之后执行的就是 next 进程，如果是新进程从 `ret_from_fork` 开始执行，如果不是，则从上次中断的 pc 开始执行，即 `switch_to` 后的指令。

其实 `context_switch` 代码可以分成两个部分：A0 和 A1，这两部分代码属于同一个进程。

![switch_to.png](https://github.com/UtopianFuture/UtopianFuture.github.io/blob/master/image/switch_to.png?raw=true)

假设进程 A 在 CPU 0 上主动执行 `switch_to` 切换到 B 执行，那么 A 执行了 A0，然后运行了 `switch_to`。在 `switch_to` 中 CPU 0 切换到 B 上硬件上下文，运行 B，A 被换出了，这时 B 直接运行自己的代码段，而 A1 还没有执行，所以需要 `last` 指向 A。

那为何不直接使用 `prev` 呢？在 `switch_to` 执行前，`prev` 指向 A，但是  `switch_to` 执行完后，此时内核栈已经从 A 的内核栈切换到 B 的内核栈，读取 `prev` 变成了读取 B 的 `prev` 参数，而不是 A 的 `prev` 参数，所以读出来的 `prev` 不一定指向 A。**那为什么 `__switch_to_asm` 能够返回指向 A 的指针？**因为 `__switch_to` 会返回 `prev`。

经过一段时间，某个 CPU 上的进程 X 主动执行 `switch_to` 切换到 A 执行，即 A 从 CPU 0 切换到 CPU n。这时 X 进入睡眠，而 A 从上次的睡眠点开始运行，也就是说开始运行 A1，而这时 `last` 执行 X。通常 A1 是 `finish_task_switch`，即 A 重新运行前需要通过这个函数对 X 进行一些清理工作，而 `last` 就是传给 `finish_task_switch` 的参数。

```c
.pushsection .text, "ax"
SYM_FUNC_START(__switch_to_asm)
	/*
	 * Save callee-saved registers
	 * This must match the order in inactive_task_frame
	 */
	pushq	%rbp
	pushq	%rbx
	pushq	%r12
	pushq	%r13
	pushq	%r14
	pushq	%r15

	/* switch stack */
    // %rdi 是函数传参的第 1 个参数，这里是 prev
    // TASK_threadsp 是 task_struct->thread->sp
    // 保存栈指针到 prev 的 task_struct 中
	movq	%rsp, TASK_threadsp(%rdi)
	movq	TASK_threadsp(%rsi), %rsp // %rsi 是第 2 个参数 - next，这就完成了栈指针的切换

#ifdef CONFIG_STACKPROTECTOR
	movq	TASK_stack_canary(%rsi), %rbx // 这是干嘛
	movq	%rbx, PER_CPU_VAR(fixed_percpu_data) + stack_canary_offset
#endif

#ifdef CONFIG_RETPOLINE
	/*
	 * When switching from a shallower to a deeper call stack
	 * the RSB may either underflow or use entries populated
	 * with userspace addresses. On CPUs where those concerns
	 * exist, overwrite the RSB with entries which capture
	 * speculative execution to prevent attack.
	 */
	FILL_RETURN_BUFFER %r12, RSB_CLEAR_LOOPS, X86_FEATURE_RSB_CTXSW
#endif

	/* restore callee-saved registers */
	popq	%r15
	popq	%r14
	popq	%r13
	popq	%r12
	popq	%rbx
	popq	%rbp

	jmp	__switch_to
SYM_FUNC_END(__switch_to_asm)
.popsection
```

##### 关键函数__switch_to

```c
/*
 *	switch_to(x,y) should switch tasks from x to y.
 */
__visible __notrace_funcgraph struct task_struct *
__switch_to(struct task_struct *prev_p, struct task_struct *next_p)
{
	struct thread_struct *prev = &prev_p->thread; // thread_struct 是保存硬件上下文的，不过
	struct thread_struct *next = &next_p->thread;
	struct fpu *prev_fpu = &prev->fpu;
	struct fpu *next_fpu = &next->fpu;
	int cpu = smp_processor_id();

    ...

	/* We must save %fs and %gs before load_TLS() because
	 * %fs and %gs may be cleared by load_TLS().
	 *
	 * (e.g. xen_load_tls())
	 */
	save_fsgs(prev_p); // 这些都很好理解

	/*
	 * Load TLS before restoring any segments so that segment loads
	 * reference the correct GDT entries.
	 */
	load_TLS(next, cpu);

	/*
	 * Leave lazy mode, flushing any hypercalls made here.  This
	 * must be done after loading TLS entries in the GDT but before
	 * loading segments that might reference them.
	 */
	arch_end_context_switch(next_p);

	/* Switch DS and ES.
	 *
	 * Reading them only returns the selectors, but writing them (if
	 * nonzero) loads the full descriptor from the GDT or LDT.  The
	 * LDT for next is loaded in switch_mm, and the GDT is loaded
	 * above.
	 *
	 * We therefore need to write new values to the segment
	 * registers on every context switch unless both the new and old
	 * values are zero.
	 *
	 * Note that we don't need to do anything for CS and SS, as
	 * those are saved and restored as part of pt_regs.
	 */
	savesegment(es, prev->es);
	if (unlikely(next->es | prev->es))
		loadsegment(es, next->es);

	savesegment(ds, prev->ds);
	if (unlikely(next->ds | prev->ds))
		loadsegment(ds, next->ds);

	x86_fsgsbase_load(prev, next);

	x86_pkru_load(prev, next);

	/*
	 * Switch the PDA and FPU contexts.
	 */
	this_cpu_write(current_task, next_p); // 是吧
	this_cpu_write(cpu_current_top_of_stack, task_top_of_stack(next_p));

	switch_fpu_finish(next_fpu);

	/* Reload sp0. */
	update_task_stack(next_p);

	switch_to_extra(prev_p, next_p);

	...

	/* Load the Intel cache allocation PQR MSR. */
	resctrl_sched_in();

	return prev_p; // 哦，原来真的会返回 prev
}
```

##### thread_struct

这个数据结构保存进程在上下文切换时的硬件上下文，但怎么和我想象的内容不太一样，很多寄存器没有保存，只保存了 sp 和一些段寄存器，在 X86 中其他的寄存器值都保存在栈里么？

```c
struct thread_struct {
	/* Cached TLS descriptors: */
    // 这个 TLS 是啥？
    // Thread Local Storage (TLS) are per-thread global variables.
    // 也就是保存线程中的一些全局变量等
	struct desc_struct	tls_array[GDT_ENTRY_TLS_ENTRIES];
#ifdef CONFIG_X86_32
	unsigned long		sp0;
#endif
	unsigned long		sp;
#ifdef CONFIG_X86_32
	unsigned long		sysenter_cs;
#else
	unsigned short		es; // 段寄存器。关于 X86 的段寻址机制还需要深入理解
	unsigned short		ds;
	unsigned short		fsindex;
	unsigned short		gsindex;
#endif

#ifdef CONFIG_X86_64
	unsigned long		fsbase;
	unsigned long		gsbase;
#else
	/*
	 * XXX: this could presumably be unsigned short.  Alternatively,
	 * 32-bit kernels could be taught to use fsindex instead.
	 */
	unsigned long fs;
	unsigned long gs;
#endif

	/* Save middle states of ptrace breakpoints */
	struct perf_event	*ptrace_bps[HBP_NUM];
	/* Debug status used for traps, single steps, etc... */
	unsigned long           virtual_dr6;
	/* Keep track of the exact dr7 value set by the user */
	unsigned long           ptrace_dr7;
	/* Fault info: */
	unsigned long		cr2;
	unsigned long		trap_nr;
	unsigned long		error_code;
#ifdef CONFIG_VM86
	/* Virtual 86 mode info */
	struct vm86		*vm86;
#endif
	/* IO permissions: */
	struct io_bitmap	*io_bitmap;

	/*
	 * IOPL. Privilege level dependent I/O permission which is
	 * emulated via the I/O bitmap to prevent user space from disabling
	 * interrupts.
	 */
	unsigned long		iopl_emul;

	unsigned int		sig_on_uaccess_err:1;

	/*
	 * Protection Keys Register for Userspace.  Loaded immediately on
	 * context switch. Store it in thread_struct to avoid a lookup in
	 * the tasks's FPU xstate buffer. This value is only valid when a
	 * task is scheduled out. For 'current' the authoritative source of
	 * PKRU is the hardware itself.
	 */
	u32			pkru;

	/* Floating point and extended processor state */
	struct fpu		fpu;
	/*
	 * WARNING: 'fpu' is dynamically-sized.  It *MUST* be at
	 * the end.
	 */
};
```

### 负载计算

这章的内容是探究如何更好的描述一个调度实体和就绪队列的工作负载。大部分内容都是从[这里](#http://www.wowotech.net/process_management/450.html)复制过来的，它讲的很清楚，我就省的再敲一遍。然后对于计算公式我能够理解，然后内核代码也贴出来了，可以说对于 PELT 算法我知道怎样算工作负载，但对于其要怎样使用还不懂，比如  `sched_avg` 中的 `load_avg`, `runnable_avg`, `util_avg` 变量我就不知道怎么用。这里就当个记录吧，之后有需要再深入理解。

#### 量化负载的计算

内核中使用量化负载来衡量 CPU、进程、就绪队列的负载。量化负载说白了就是运行时间除以总时间乘上权重，那么以此可以计算：

- CPU 的负载 = 运行时间 / 总时间 * 就绪队列的总权重

- decay_avg_load = decay_sum_runable_time / decay_sum_period_time * weight

  decay_avg_load 就是量化负载；decay_sum_runable_time 是就绪队列或调度实体在可运行状态下的所有历史累计衰减时间，这个历史衰减时间我认为就是运行时间（好吧，不是这么简单，见下面的分析）；decay_sum_period_time 是就绪队列或调度实体在所有采样周期里累加衰减时间；weight 是调度实体或就绪队列的权重。

当一个进程 decay_sum_runable_time 无限接近 decay_sum_period_time 时，它的量化负载就无限接近权重，这说明这个进程一直在占用 CPU。我们将 CPU 对应的就绪队列值哦个所有进程的量化负载累加起来就得到 CPU 的总负载。

#### 实际算力的计算

处理器有一个计算能力的概念，也就是这个处理器最大的处理能力。在 SMP 架构下，系统中所有处理器的计算能力是一样的，但在 ARM 架构中使用了大小核设计，处理器的处理能力就不一样了。内核同样使用量化计算能力来描述处理器的计算能力，若系统中功能最强大的 CPU 的量化计算能力设置为 1024，那么系统中功能稍弱的 CPU 的量化计算能力就要小于 1024。通常 CPU 的量化计算能力通过设备树或  BIOS 传给内核。[rq](#rq)中有成员变量 `cpu_capacity_orig` 和 `cpu_capacity` 来描述 CPU 的算力。

CPU 的最大量化计算能力称为额定算力，而一个进程或就绪队列当前计算能力称为实际算力，其计算公式为：

- util_avg = decay_sum_runable_time / decay_sum_period_time * cpu_capacity

  util_avg 可以理解为额定算力的利用率或 CPU 利用率。

  从公式中就可以看出，如果一个调度实体的执行时间越接近采样时间，它的实际算力就越接近额定算力，也可以理解为它的 CPU 利用率更高。

内核中的绿色节能调度器会使用实际算力来进行进程的迁移调度。

#### PELT算法

##### [历史累计衰减时间](#http://www.wowotech.net/process_management/PELT.html)

PELT 算法为了做到 Per-entity 的负载跟踪，将时间（物理时间，不是虚拟时间）分成了 1024us 的序列，在每一个 1024us 的周期中，一个 entity 对系统负载的贡献可以根据**该实体处于 runnable 状态的时间进行计算**。如果在该周期内，runnable 的时间是 x，那么对系统负载的贡献就是（x/1024）。当然，一个实体在一个计算周期内的负载可能会超过 1024us，这是因为我们会累积在过去周期中的负载，当然，对于过去的负载我们在计算的时候需要乘一个衰减因子。如果我们让 Li 表示在周期 pi 中该调度实体的对系统负载贡献，那么一个调度实体对系统负荷的总贡献可以表示为：

*L = L0 + L1\*y + L2\*y^2 + L3\*y^3 + ...*

其中 y 是衰减因子。通过上面的公式可以看出：

- 调度实体对系统负荷的贡献值是一个序列之和组成；

- 最近的负荷值拥有最大的权重；

- 过去的负荷也会被累计，但是是以递减的方式来影响负载计算。

使用这样序列的好处是计算简单，我们不需要使用数组来记录过去的负荷贡献，只要把上次的总负荷的贡献值乘以 y 再加上新的 L0 负荷值就 OK 了。

在 3.8 版本的代码中，y^32 = 0.5, y = 0.97857206。这样选定的 y 值，一个调度实体的负荷贡献经过 32 个周期（1024us）后，对当前时间的的符合贡献值会衰减一半。

举个例子，如何计算一个 se 的负载贡献。如果有一个 task，从第一次加入 rq 后开始一直运行 4096us 后一直睡眠，那么在 1023us、2047us、3071us、4095us、5119us、6143us、7167us 和 8191us 时间的每一个时刻负载贡献分别是多少呢？

```txt
1023us: L0 = 1023
2047us: L1 = 1023 + 1024 * y                                                 = 1023 + (L0 + 1) * y = 2025
3071us: L2 = 1023 + 1024 * y + 1024 * y2                                     = 1023 + (L1 + 1) * y = 3005
4095us: L3 = 1023 + 1024 * y + 1024 * y2 + 1024 * y3                         = 1023 + (L2 + 1) * y = 3963
5119us: L4 = 0    + 1024 * y + 1024 * y2 + 1024 * y3 + 1024 * y4             =    0 + (L3 + 1) * y = 3877
6143us: L5 = 0    +    0     + 1024 * y2 + 1024 * y3 + 1024 * y4 + 1024 * y5 =          0 + L4 * y = 3792
7167us: L6 = 0    + L5 * y = L4 * y2 = 3709
8191us: L7 = 0    + L6 * y = L5 * y2 = L4 * y3 = 3627
```

##### [负载贡献](http://www.wowotech.net/process_management/450.html)

从上面的计算公式我们也可以看出，经常需要计算 valyn 的值，因此内核提供 `decay_load` 函数用于计算第 n 个周期的衰减值。为了避免浮点数运算，采用移位和乘法运算提高计算速度。`decay_load(val, n) = val * y^n * 2^32 >> 32`。我们将 `y^n * 2^32` 的值提前计算出来保存在数组 `runnable_avg_yN_inv` 中。

```c
static const u32 runnable_avg_yN_inv[] __maybe_unused = {
	0xffffffff, 0xfa83b2da, 0xf5257d14, 0xefe4b99a, 0xeac0c6e6, 0xe5b906e6,
	0xe0ccdeeb, 0xdbfbb796, 0xd744fcc9, 0xd2a81d91, 0xce248c14, 0xc9b9bd85,
	0xc5672a10, 0xc12c4cc9, 0xbd08a39e, 0xb8fbaf46, 0xb504f333, 0xb123f581,
	0xad583ee9, 0xa9a15ab4, 0xa5fed6a9, 0xa2704302, 0x9ef5325f, 0x9b8d39b9,
	0x9837f050, 0x94f4efa8, 0x91c3d373, 0x8ea4398a, 0x8b95c1e3, 0x88980e80,
	0x85aac367, 0x82cd8698,
};
```

根据 `runnable_avg_yN_inv` 数组的值，我们就方便实现 `decay_load` 函数。

```c
/*
 * Approximate:
 *   val * y^n,    where y^32 ~= 0.5 (~1 scheduling period)
 */
static u64 decay_load(u64 val, u64 n)
{
	unsigned int local_n;

	if (unlikely(n > LOAD_AVG_PERIOD * 63))
		return 0;

	/* after bounds checking we can collapse to 32-bit */
	local_n = n;

	/*
	 * As y^PERIOD = 1/2, we can combine
	 *    y^n = 1/2^(n/PERIOD) * y^(n%PERIOD)
	 * With a look-up table which covers y^n (n<PERIOD)
	 *
	 * To achieve constant time decay_load.
	 */
	if (unlikely(local_n >= LOAD_AVG_PERIOD)) {
		val >>= local_n / LOAD_AVG_PERIOD;
		local_n %= LOAD_AVG_PERIOD;
	}

	val = mul_u64_u32_shr(val, runnable_avg_yN_inv[local_n], 32);
	return val;
}
```

经过上面举例，我们可以知道计算当前负载贡献并不需要记录所有历史负载贡献。我们只需要知道**上一刻负载贡献**就可以计算当前负载贡献，这大大降低了代码实现复杂度。

我们继续上面举例问题的思考，我们依然假设一个 task 开始从 0 时刻运行，那么 1022us 后的负载贡献自然就是 1022。当 task 经过 10us 之后，此时（现在时刻是 1032us）的负载贡献又是多少呢？

很简单，10us 中的 2us 和之前的 1022us 可以凑成一个周期 1024us。这个 1024us 需要进行一次衰减，即现在的负载贡献是：1024y + 8 = 1010。2us 属于上一个负载计算时距离一个周期 1024us 的差值，由于 2 是上一个周期的时间，因此也需要衰减一次，8 是当前周期时间，不需要衰减。

又经过了 2124us，此时（现在时刻是 3156us）负载贡献又是多少呢？即：1010y2 + 1016y2 + 1024y + 84 = 3024。2124us 可以分解成 3 部分：1016us 补齐上一时刻不足 1024us 部分，凑成一个周期；1024us 一个整周期；当前时刻不足一个周期的剩余 84us 部分。相当于我们经过了 2 个周期，因此针对上一次的负载贡献需要衰减 2 次，也就是 1010y^2 部分，1016us 是补齐上一次不足一个周期的部分，因此也需要衰减 2 次，所以公式中还有 1016y^2 部分。1024us 部分相当于距离当前时刻是一个周期，所以需要衰减 1 次，最后 84 部分是当前剩余时间，不需要衰减。

针对以上事例，我们可以得到一个更通用情况下的计算公式。假设上一时刻负载贡献是 u，经历 d 时间后的负载贡献如何计算呢？根据上面的例子，我们可以把时间 d 分成 3 和部分：d1 是离当前时间最远（不完整的）period 的剩余部分，d2 是完整 period 时间，而 d3 是（不完整的）当前 period 的剩余部分。假设时间 d 是经过 p 个周期（d=d1+d2+d3, p=1+d2/1024）。d1，d2，d3 的示意图如下：

```plain
      d1          d2           d3
      ^           ^            ^
      |           |            |
    |<->|<----------------->|<--->|
|---x---|------| ... |------|-----x (now)

                            p-1
 u' = (u + d1) y^p + 1024 * \Sum y^n + d3 y^0
                            n=1
                              p-1
    = u y^p + d1 y^p + 1024 * \Sum y^n + d3 y^0
                              n=1
```

上面的例子现在就可以套用上面的公式计算。例如，上一次的负载贡献 u=1010，经过时间 d=2124us，可以分解成 3 部分，d1=1016us，d2=1024，d3=84。经历的周期 p=2。所以当前时刻负载贡献 u'=1010y2 + 1016y2 + 1024y + 84，与上面计算结果一致。

内核中用来实现上述计算的是 `___update_load_sum`，

```c
static __always_inline int
___update_load_sum(u64 now, struct sched_avg *sa,
		  unsigned long load, unsigned long runnable, int running)
{
	u64 delta;

	delta = now - sa->last_update_time;

	delta >>= 10; // 将其转换成微秒

	sa->last_update_time += delta << 10;

	if (!accumulate_sum(delta, sa, load, runnable, running)) // 计算工作负载
		return 0;

	return 1;
}
```

计算结果保存在 `sched_avg` 中的 `load_sum`, `runnable_sum`, `util_sum` 中。

而 `___update_load_avg` 计算量化负载，计算结果保存在 `sched_avg` 中的 `load_avg`, `runnable_avg`, `util_avg` 中。

```c
static __always_inline void
___update_load_avg(struct sched_avg *sa, unsigned long load)
{
	u32 divider = get_pelt_divider(sa);

	/*
	 * Step 2: update *_avg.
	 */
	sa->load_avg = div_u64(load * sa->load_sum, divider);
	sa->runnable_avg = div_u64(sa->runnable_sum, divider);
	WRITE_ONCE(sa->util_avg, sa->util_sum / divider);
}
```

##### sched_avg

该数据结构用来描述[调度实体](#sched_entity)或[就绪队列](#cfs_rq)的负载信息。注释值得一看。

```c
/*
 * The load/runnable/util_avg accumulates an infinite geometric series
 *
 * [load_avg definition]
 *
 *   load_avg = runnable% * scale_load_down(load)
 *
 * [runnable_avg definition]
 *
 *   runnable_avg = runnable% * SCHED_CAPACITY_SCALE
 *
 * [util_avg definition]
 *
 *   util_avg = running% * SCHED_CAPACITY_SCALE
 *
 * where runnable% is the time ratio that a sched_entity is runnable and
 * running% the time ratio that a sched_entity is running.
 *
 * For cfs_rq, they are the aggregated values of all runnable and blocked
 * sched_entities.
 *
 * The load/runnable/util_avg doesn't directly factor frequency scaling and CPU
 * capacity scaling. The scaling is done through the rq_clock_pelt that is used
 * for computing those signals (see update_rq_clock_pelt())
 *
 * N.B., the above ratios (runnable% and running%) themselves are in the
 * range of [0, 1]. To do fixed point arithmetics, we therefore scale them
 * to as large a range as necessary. This is for example reflected by
 * util_avg's SCHED_CAPACITY_SCALE.
 *
 * [Overflow issue]
 *
 * The 64-bit load_sum can have 4353082796 (=2^64/47742/88761) entities
 * with the highest load (=88761), always runnable on a single cfs_rq,
 * and should not overflow as the number already hits PID_MAX_LIMIT.
 *
 * For all other cases (including 32-bit kernels), struct load_weight's
 * weight will overflow first before we do, because:
 *
 *    Max(load_avg) <= Max(load.weight)
 *
 * Then it is the load_weight's responsibility to consider overflow
 * issues.
 */
struct sched_avg {
	u64				last_update_time; // 上一次更新的时间点，用于计算时间间隔
    // 对于调度实体来说，它的统计对象是调度实体在可运行状态下的累计衰减总时间
    // 对于就绪队列来说，它的统计对象是所有进程的累计工作总负载（时间乘权重）
	u64				load_sum;
    // 对于调度实体来说，它的统计对象是调度实体在就绪队列里可运行状态下的累计衰减总时间
    // 对于就绪队列来说，它的统计对象是所有可以运行状态下进程的累计工作总负载（时间乘权重）
	u64				runnable_sum;
    // 对于调度实体来说，它的统计对象是调度实体在正在运行状态下的累计衰减总时间
    // 对于就绪队列来说，它的统计对象是所有正在运行状态进程的累计衰减总时间
	u32				util_sum;
	u32				period_contrib; // 存放上一次时间采样时，不能凑成一个周期（1024us）的剩余时间
    // 对于调度实体来说，它是可运行状态下的量化负载，用来衡量一个进程的负载贡献值
    // 对于就绪队列来说，它是总的量化负载
	unsigned long			load_avg;
    // 对于调度实体来说，它是可运行状态下的量化负载，等于 load_avg
    // 对于就绪队列来说，它是队列里所有可运行状态下进程的总量化负载
    // 在 SMP 负载均衡中使用该成员来比较 CPU 的负载大小
	unsigned long			runnable_avg;
	unsigned long			util_avg; // 实际算力
	struct util_est			util_est;
} ____cacheline_aligned;
```

有几点需要注意：

- 调度实体在就绪队列中的时间包括两部分：
  - 正在运行时间，running；
  - 等待时间，runable，包括正在运行的时间和等待时间；

### SMP负载均衡

这两部分对现在的我来说都过于深入，与其花时间学习这些现在不太可能用到的东西不如先把上面这些基础的知识搞懂。所以这两部分暂时不分析，之后有需要再看。下一步把内存管理和进程调度没有搞懂的地方用 gdb + qemu 深入分析，然后再看看文件系统。

### 绿色节能调度器

### 疑问

1. 为什么要设置优先级、nice 值、权重、实际运行时间（runtime）、虚拟运行时间（vruntime）？它们之间的关系是什么？

2. 为什么要根据 vruntime 决定调度顺序？

3. 在[关键函数context_switch](#关键函数context_switch)中使用 last 参数来达到在 next 进程中能够处理 prev 进程的遗留问题，但是，last 是一个指针，在进程页表都切换了的情况下，prev 能正确指向 prev 的 `task_struct` 么？

4. `thread_struct` 数据结构保存进程在上下文切换时的硬件上下文，但怎么和我想象的内容不太一样，很多寄存器没有保存，只保存了 sp 和一些段寄存器，在 X86 中其他的寄存器值都保存在栈里么？还是说就只需要保存这些？

   其实这些信息的保存在[关键函数copy_thread](#关键函数copy_thread)中已经保存了。

5. [关键函数copy_thread](#关键函数copy_thread)进程创建过程还涉及到很多模块的初始化不懂，之后需要不断深入理解。

### Reference

[1] 奔跑吧 Linux 内核，卷 1：基础架构

[2] 内核版本：5.15-rc5，commitid: f6274b06e326d8471cdfb52595f989a90f5e888f

[3] https://blog.csdn.net/u013982161/article/details/51347944

[4] https://www.cnblogs.com/LoyenWang/p/12495319.html

[5] http://www.wowotech.net/process_management/PELT.html

[6] http://www.wowotech.net/process_management/450.html[]
