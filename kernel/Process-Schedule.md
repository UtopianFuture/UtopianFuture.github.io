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
    - [关键函数 kernel_clone](#关键函数 kernel_clone)
    - [关键函数 copy_process](#关键函数 copy_process)
    - [关键函数 dup_task_struct](#关键函数 dup_task_struct)
    - [关键函数 copy_mm](#关键函数 copy_mm)
    - [关键函数 dup_mmap](#关键函数 dup_mmap)
    - [关键函数 copy_thread](#关键函数 copy_thread)
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
    - [关键函数 sched_fork](#关键函数 sched_fork)
    - [关键函数 task_fork_fair](#关键函数 task_fork_fair)
    - [关键函数 update_curr](#关键函数 update_curr)
    - [关键函数 place_entiry](#关键函数 place_entiry)
    - [关键函数 sched_slice](#关键函数 sched_slice)
  - [进程加入调度器](#进程加入调度器)
    - [关键函数 wake_up_new_task](#关键函数 wake_up_new_task)
    - [关键函数 enqueue_task_fair](#关键函数 enqueue_task_fair)
    - [关键函数 enqueue_entity](#关键函数 enqueue_entity)
  - [进程调度](#进程调度)
    - [关键函数__schedule](#关键函数__schedule)
    - [关键函数 pick_next_entity](#关键函数 pick_next_entity)
  - [进程切换](#进程切换)
    - [关键函数 context_switch](#关键函数 context_switch)
    - [关键函数 switch_mm](#关键函数 switch_mm)
    - [关键函数 switch_to](#关键函数 switch_to)
    - [关键函数__switch_to](#关键函数__switch_to)
    - [thread_struct](#thread_struct)
    - [关键函数 finish_task_switch](#关键函数 finish_task_switch)
- [PREEMPT_RT](#PREEMPT_RT)
- [负载计算](#负载计算)
  - [量化负载的计算](#量化负载的计算)
  - [实际算力的计算](#实际算力的计算)
  - [PELT 算法](#PELT 算法)
    - [历史累计衰减时间](#历史累计衰减时间)
    - [负载贡献](#负载贡献)
    - [sched_avg](#sched_avg)
- [SMP 负载均衡](#SMP 负载均衡)
- [绿色节能调度器](#绿色节能调度器)
- [创建并运行新进程](#创建并运行新进程)
- [疑问](#疑问)
- [Reference](#Reference)

### 基本概念

这里介绍一些我个人觉得很容易混淆和有趣的概念。

#### 轻量级进程

线程称为轻量级进程，它是操作系统调度的最小单元，通常一个进程可以拥有多个线程，即**线程对应一个进程描述符，而进程对应一个或一组进程描述符**。内核并没有特别的调度算法或定义特别的数据结构来表示线程，线程和进程都使用相同的进程描述符数据结构。内核中使用 `clone` 来创建线程，其工作方式和创建进程的 `fork` 类似。但 `clone` 会确定哪些资源和父进程共享，哪些资源为线程独享。

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
	unsigned int			__state; // 表示进程的状态

    ...

	/*
	 * This begins the randomizable portion of task_struct. Only
	 * scheduling-critical items should be added above here.
	 */
	randomized_struct_fields_start

	void				*stack; // 内核栈的位置么，在哪里初始化的
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

    // 进程的动态优先级。这是调度类考虑的优先级，这就是 nice 值，不就是优先级么
    // 它是 static_prio - MAX_RT_PRIO(100)得到的
	int				prio;
	int				static_prio; // 静态优先级，在进程启动时分配。其范围是 0 ~ 139
	int				normal_prio; // 基于 static_prio 和调度策略计算出来的优先级，子进程初始化时继承该优先级
	unsigned int			rt_priority; // 实时进程的优先级

	const struct sched_class	*sched_class; // 调度类，deadline, rt, cfs 等
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

	unsigned int			policy; // 确定进程的类型，如普通进程还是实时进程等
	int				nr_cpus_allowed; // 确定该进程可以在哪几个 CPU 上运行
	const cpumask_t			*cpus_ptr;
	cpumask_t			*user_cpus_ptr;
	cpumask_t			cpus_mask; // cpu 亲和性相关
	void				*migration_pending;
#ifdef CONFIG_SMP
	unsigned short			migration_disabled;
#endif
	unsigned short			migration_flags;

...

	struct sched_info		sched_info; // 调度相关信息

	struct list_head		tasks; // 所有的 task 放在一个 list 么
#ifdef CONFIG_SMP
	struct plist_node		pushable_tasks;
	struct rb_node			pushable_dl_tasks;
#endif

	struct mm_struct		*mm; // 哈哈，这个就很熟悉了
    // 这个和 mm 有什么区别？
    // 是这样的，对于内核线程来说，没有进程地址空间描述符，但出于进程调度的需要
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

    ...

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

- TASK_RUNNING（可运行态或就绪态或正在运行态）：内核对当前正在运行的进程没有给出一个明确的状态；
- TASK_INTERRUPTIBLE：进程进入睡眠状态来等待某些条件达成或某个资源被释放，一旦条件满足，内核将该状态的进程设置为 TASK_RUNNING 队列；
- TASK_UNINTERRUPTIBLE：一种不可中断的睡眠状态，**不可以被信号打断**，必须等到等待的条件满足时才被唤醒。这个状态通常在进程必须等待时不受干扰或所等待的事件很快发生时使用；
- __TASK_STOPPED：进程已停止运行；
- EXIT_ZOMBIE：进程已消亡，但对应的 `task_struct` 还没有释放。子进程退出时，父进程可以通过 `wait` 和 `waitpid` 来获取子进程消亡的原因。

#### 进程标识

在[轻量级进程](#轻量级进程)中介绍了，内核中没有专门用来描述线程的数据结构，而是**使用线程组来表示多线程的进程**。一个线程组中的线程的 pid 是唯一的表示，而 **tgid 则和该组中第一个进程的 pid 相同**。因为根据 POSIX 标准中的规定，**一个多线程应用程序中所有的线程必须拥有相同的 PID，这样可以把指定信号发送给组里所有的线程，通过 tgid 的方式就可以完成这一规定**。通过如下两个接口可以获取当前线程对应的 pid 和 tgid。

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
	.active_mm	= &init_mm,					\ // 这个也是静态初始化的

	...

	INIT_NUMA_BALANCING(tsk)					\
	INIT_KASAN(tsk)							\
}
```

```c
struct mm_struct init_mm = {
	.mm_mt		= MTREE_INIT_EXT(mm_mt, MM_MT_FLAGS, init_mm.mmap_lock),
	.pgd		= swapper_pg_dir,
	.mm_users	= ATOMIC_INIT(2),
	.mm_count	= ATOMIC_INIT(1),
	.write_protect_seq = SEQCNT_ZERO(init_mm.write_protect_seq),
	MMAP_LOCK_INITIALIZER(init_mm)
	.page_table_lock =  __SPIN_LOCK_UNLOCKED(init_mm.page_table_lock),
	.arg_lock	=  __SPIN_LOCK_UNLOCKED(init_mm.arg_lock),
	.mmlist		= LIST_HEAD_INIT(init_mm.mmlist),
	.user_ns	= &init_user_ns,
	.cpu_bitmap	= CPU_BITS_NONE,
#ifdef CONFIG_IOMMU_SVA
	.pasid		= INVALID_IOASID,
#endif
	INIT_MM_CONTEXT(init_mm)
};

// 在 setup_arch 时调用
void setup_initial_init_mm(void *start_code, void *end_code,
			   void *end_data, void *brk)
{
	init_mm.start_code = (unsigned long)start_code;
	init_mm.end_code = (unsigned long)end_code;
	init_mm.end_data = (unsigned long)end_data;
	init_mm.brk = (unsigned long)brk;
}
```

#### 获取当前进程

内核中通过 `current` 宏来获取当前进程的 `task_struct` 结构指针。

```c
static __always_inline struct task_struct *get_current(void)
{
	unsigned long sp_el0;

    // sp_el0 是系统寄存器，用来存放当前 task 的 task_struct 指针
	asm ("mrs %0, sp_el0" : "=r" (sp_el0));

	return (struct task_struct *)sp_el0;
}

#define current get_current()
```

#### 内核线程

内核线程就是运行在内核地址空间的进程，它**没有独立的进程地址空间，所有的内核线程都共享内核地址空间**，即 `task_struct` 结构中的 `mm_struct` 指针设为 null。但内核线程也和普通进程一样参与系统调度。

### 进程创建与终止

#### 创建进程

内核为进程的创建提供了相应的系统调用，如 `sys_fork`, `sys_vfork` 以及 `sys_clone` 等，C 标准库提供了这些系统调用的封装函数。下面简单介绍这几种系统调用。

##### fork

```c
#ifdef __ARCH_WANT_SYS_FORK
SYSCALL_DEFINE0(fork)
{
	struct kernel_clone_args args = {
		.exit_signal = SIGCHLD, // 其他参数都是默认的，只是需要设置 SIGCHLD 标志位
	};

	return kernel_clone(&args);
}
#endif
```

通过 COW 技术创建子进程，子进程**只会复制父进程的页表，而不会复制页面内容**。当它们开始执行各自的程序时，它们的进程地址空间才开始分道扬镳。

`fork` 函数会有两次返回，一次在父进程中，一次在子进程中。如果返回值为 0，说明这是子进程；如果返回值为正数，说明这是父进程，父进程会返回子进程的 pid；如果返回 -1，表示创建失败。

当然，只复制页表在某些情况下还是会比较慢，后来就有了 `vfork` 和 `clone`。

##### vfork

和  `fork` 类似，但是 `vfork` 的**父进程会一直阻塞**，直到子进程调用 `exit` 或 `execve` 为止，其可以**避免复制父进程的页表项**。

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

什么场景适合使用 vfork？

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

进程创建时会用到很多标志位，这里列出几个常用的：

| 标志位         | 含义                                                         |
| -------------- | ------------------------------------------------------------ |
| CLONE_VM       | 父、子进程共享进程地址空间                                   |
| CLONE_FS       | 父、子进程共享文件系统信息                                   |
| CLONE_FILES    | 父、子进程共享打开的文件                                     |
| CLONE_SIGHAND  | 父、子进程共享信号处理函数以及被阻塞的信号                   |
| CLONE_VFORK    | 在创建子进程时启用内核的完成量机制，wait_for_completion 会使父进程进入睡眠状态，直到子进程调用 execve 或 exit 释放内存 |
| CLONE_IO       | 复制 I/O 上下文                                              |
| CLONE_PTRACE   | 父进程被跟踪、子进程也会被跟踪                               |
| CLONE_PARENT   | 父、子进程拥有同一个父亲                                     |
| CLONE_THREAD   | 父、子进程在同一个线程组                                     |
| CLONE_NEWNS    | 为子进程创建新的命名空间                                     |
| CLONE_UNTRACED | 保证没有进程可以跟踪这个新进程                               |

##### 关键函数 kernel_clone

在 5.15 的内核中，这些创建用户态进程和内核线程的接口最后都是调用 `kernel_clone`，

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
	int cgroup; // 这个是虚拟化支持的一种机制，之后再分析
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

##### 关键函数 copy_process

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

	retval = perf_event_init_task(p, clone_flags);
    
	retval = audit_alloc(p);
    
	/* copy all the process information */
	shm_init_task(p);
	retval = security_task_alloc(p, clone_flags);
    
	retval = copy_semundo(clone_flags, p);
    
	retval = copy_files(clone_flags, p); // 复制父进程打开文件信息
    
    // 复制父进程 fs_struct 数据结构，copy_files 和 copy_fs 有什么区别么
    // 或者说打开文件信息和 fs_struct 有什么关系？
	retval = copy_fs(clone_flags, p);
    
	retval = copy_sighand(clone_flags, p); // 复制父进程的信号处理函数
    
	retval = copy_signal(clone_flags, p); // 复制父进程的信号系统
    
	retval = copy_mm(clone_flags, p); // 复制父进程的页表信息
    
    // 复制父进程的命名空间，命名空间也很重要，需要深入了解
    // 命名空间技术主要用于访问隔离，其原理是针对一类资源进程抽象，并将其封装在一起以供一个容器使用
    // 每个容器都有自己的抽象资源，它们之间彼此之间不可见，因此访问是隔离的
	retval = copy_namespaces(clone_flags, p);
    
	retval = copy_io(clone_flags, p); // I/O 相关
    
	retval = copy_thread(clone_flags, args->stack, args->stack_size, p, args->tls); // 设置内核栈等信息

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
    // 成功分配了 pid，这里是对 pid 进行一系列的操作，不理解，还有 pid_type 也不理解
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
}
```

##### 关键函数 dup_task_struct

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
    
    // 分配内核栈，通过 __vmalloc_node_range 即 vmalloc 分配机制来分配内核栈
	stack = alloc_thread_stack_node(tsk, node);
    
	...

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

##### 关键函数 dup_mm

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

    // 复制父进程的进程地址空间的页表到子进程，继续完成上面的工作
	err = dup_mmap(mm, oldmm);

	mm->hiwater_rss = get_mm_rss(mm);
	mm->hiwater_vm = mm->total_vm;

	...

	return mm;

    ...
}
```

##### 关键函数 dup_mmap

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
        
		retval = vma_dup_policy(mpnt, tmp);
        
		tmp->vm_mm = mm; // 一个个 VMA 添加
		retval = dup_userfaultfd(tmp, &uf);
        
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

##### 关键函数 copy_thread

这里设置些架构相关的寄存器，涉及到 `clone`, `vfork`, `fork` 等系统调用返回用户态的寄存器状态以及返回到哪个处理函数进行系统态 - 用户态的切换，现在知道这个函数是 `ret_from_fork`，当然，这是新建进程开始执行的函数。

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

进程的终止主要有 2 种方法：

- 自愿终止
  - 从 `main` 函数返回，**链接程序会自动的添加 `exit` 系统调用**；
  - 主动调用 `exit` 系统调用；
- 被动终止
  - 进程收到一个自己不能处理的信号；
  - 进程在内核态执行时产生一个异常；
  - 进程收到 SIGKILL （ctrl + c）等终止信号；

当一个进程终止时，内核会释放它所有占用的资源（`task_struct` 视情况而定），并把这个消息传递给父进程。如果父进程先于子进程消亡，那么子进程就变成“孤儿”进程，内核会将它”托孤“给 1 号进程（init）。

### 进程调度

#### 调度策略

进程调度依赖于调度策略，内核将相同的调度策略抽象成调度类，不同类型的进程采用不同的调度策略，目前内核默认实现 5 种调度类。

|  调度类  |                调度策略                 |                   适用范围                   |                             说明                             |
| :------: | :-------------------------------------: | :------------------------------------------: | :----------------------------------------------------------: |
|   stop   |                   无                    | 最高优先级的进程，比 deadline 进程的优先级高 | 『1』可以抢占任何进程；『2』在每个 CPU 上实现一个名为 “migration/N” 的内核线程（这不就是迁移线程么）。该内核线程的优先级最高，可以抢占任何进程的运行，一般用来运行特殊的功能；『3』用于负载均衡机制中的进程迁移、softlockup 检测、CPU 热拔插、RCU 等 |
| deadline |             SCHED_DEADLINE              |      最高优先级的实时进程。优先级为 -1       |     用于调度有严格时间要求的实时进程，如视频编码/译码等      |
| realtime |          SCHED_FIFO、SCHED_RR           |        普通实时进程。优先级为 0 ~ 99         |  用于普通的实时进程，如 IRQ 线程化（应该就是中断上下半部）   |
|   CFS    | SCHED_NORMAL、 SCHED_BATCH、 SCHED_IDLE |         普通进程。优先级为 100 ~ 139         |                        由 CFS 来调度                         |
|   idle   |                   无                    |               最低优先级的进程               | 当继续队列中没有其他进程时进入 idle 调度类。idle 调度类会让 CPU 进入低功耗模式 |

- `SCHED_DEADLINE`：限期进程调度策略，使 task 选择 Deadline 调度器来调度运行；
- `SCHED_FIFO`：实时进程调度策略，先进先出调度没有时间片，没有更高优先级的情况下，只能等待主动让出 CPU；
- `SCHED_RR`：实时进程调度策略，时间片轮转，进程用完时间片后加入优先级对应运行队列的尾部，把 CPU 让给同优先级的其他进程；
- `SCHED_NORMAL`：普通进程调度策略，使 task 选择 CFS 调度器来调度运行；
- `SCHED_BATCH`：普通进程调度策略，批量处理，使 task 选择 CFS 调度器来调度运行；
- `SCHED_IDLE`：普通进程调度策略，使 task 以最低优先级选择 CFS 调度器来调度运行；

#### 经典调度算法

现代操作系统的进程调度器的设计大多受**多级反馈队列算法**的影响。多级反馈队列算法的核心是**把进程按优先级分成多个队列，相同优先级的进程在同一个队列**。它最大的特点在于能够根据判断正在运行的进程属于哪种进程，如 I/O 消耗型或 CPU 消耗型，作出不同的反馈，然后**动态的修改进程的优先级**。

其有如下几条基本规则：

- 如果进程 A 的优先级大于 B，那么调度器选择 A；
- 如果 A 和 B 的优先级一样，那么使用轮转算法来选择；
- 当一个新进程进入调度器时，把它放入优先级最高的队列；
- 若一个进程完全占用了时间片，说明这是一个 CPU 消耗型的进程，需要将其优先级降一级（让出 CPU 么）；
- 若一个进程在时间便结束前放弃 CPU，说明这是一个 I/O 消耗型的进程，那么优先级不变；

这样设计的调度器会存在如下几个问题：

- 如果 I/O 消耗型进程过多，可能会造成 CPU 消耗型进程饥饿；

- 有些进程会欺骗调度器。一个 CPU 消耗型的进程在时间片快用完的时候请求 I/O，那么调度器会将其认定为 I/O 消耗型的进程，从而保留在高优先级的队列。大量这样进程会导致系统的交互性变差；

- 有些进程在生命周期中一会是 I/O 消耗型，一会是 CPU 消耗型，调度器很难判断。

为了解决以上问题，多级反馈队列算法提出了改良方案：

- 每个时间周期 S 后，将系统中所有进程的优先级都提到最高，相当于**每隔一段时间重启一次系统**；
- 当一个进程使用完时间片后，不管它是否在时间片最末位发生 I/O 请求，都将其优先级降低；

虽说算法思想不难，但是在实际工程中最难的是参数如何确定和优化，如时间间隔 S。

### CFS

Completely Fair Scheduler，完全公平调度器，用于内核中普通进程的调度。

CFS 采用了红黑树算法来管理所有的调度实体 `sched_entity`，算法效率为 `O(log(n))`。`CFS` 跟踪调度实体 `sched_entity` 的虚拟运行时间 `vruntime`，平等对待运行队列中的调度实体，**将执行时间少的调度实体排列到红黑树的左边**。调度实体通过 `enqueue_entity` 和 `dequeue_entity` 来进行红黑树的出队入队。每个 `sched_latency` 周期内，根据各个任务的权重值，可以计算出运行时间 `runtime`，运行时间可以转换成虚拟运行时间 `vruntime`。**根据虚拟运行时间的大小**，插入到 CFS 红黑树中，虚拟运行时间少的调度实体放置到左边，在下一次任务调度的时候，**选择虚拟运行时间少的调度实体来运行**。

#### vruntime

内核使用 0 ~ 139 表示进程的优先级，数值越低，优先级越高。优先级 0 ~ 99 分配给实时进程，100 ~ 139 分配给普通进程使用。另外，**在用户空间有一个传统的变量 nice**，它映射到普通进程的优先级，即 100 ~ 139。nice 值的范围是 -20 ~ 19，进程默认的 nice 值为 0，**nice 值越高，优先级越低，权重越低，反之亦然**。如果一个 CPU 密集型的进程 nice 值从 0 增加到 1，那么它相对于其他 nice 值为 0 的进程将减少 10% 的 CPU 时间。为了计算方便，内核约定 nice 值为 0 的进程权重为 1024，其他 nice 值对应的权重值可以通过查表的方式来获取，

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

那优先级 0~99 的实时进程怎么办？实时进程不用 cfs 调度啊，自然不需要这么复杂的设计。

这里有个问题，为什么有了优先级还要使用 nice 值？仅仅是因为优先级是内核态的变量，而 nice 值可以在用户态访问么？为了方便计算吧，改变 nice 值可能直接改变进程获取 CPU 资源的比例。

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

根据 nice 值直接得到 `load_weight->inv_weight`。

前面讲到 CFS 跟踪调度实体 `sched_entity` 的虚拟运行时间 `vruntime`，平等对待运行队列中的调度实体，将执行时间少的调度实体排列到红黑树的左边，在下一次任务调度的时候，选择虚拟运行时间少的调度实体来运行。也就是说 `vruntime` 是决定进程调度次序的关键变量，但为何要设计 `vruntime`，而不是直接使用 `runtime`？因为还要考虑到进程权重啊！

在 CFS 中有一个计算 `vruntime` 的核心函数 `calc_delta_fair`，其调用 `__calc_delta`，

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

从这个函数的计算公式可以得出，nice 越大，计算出来的 `vruntime` 就越大，而**调度器是选择虚拟运行时间少的调度实体来运行**，所以 nice 值越大，优先级越低。随着 `vruntime` 的增长，优先级低的进程也有机会被调度。

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
	struct sched_avg		avg; // 用于负载均衡，这里暂时不分析
#endif
};
```

##### rq

`DECLARE_PER_CPU_SHARED_ALIGNED(struct rq, runqueues);` 这个数据结构是**每个 CPU 通用就绪队列的描述符**。它**包含了多个调度策略的就绪队列，可以理解为一个 CPU 总的就绪队列**，所有相关的信息都可以在里面找到。其中有很多负载均衡方面的变量，暂时不分析。

该结构体在 start_kernel->sched_init 中会通过 `for_each_possible_cpu` 初始化。

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
	struct dl_rq		dl; // 实时进程的就绪队列，deadline 也是实时进程

#ifdef CONFIG_FAIR_GROUP_SCHED
	/* list of leaf cfs_rq on this CPU: */
	struct list_head	leaf_cfs_rq_list;
	struct list_head	*tmp_alone_branch;
#endif /* CONFIG_FAIR_GROUP_SCHED */

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

    // 若一个进程的实际算力大于 CPU 额定算力的 80%，那么这个进程称为不合适的进程（？）
    // 在 ARM 大小核中就存在这种情况
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

	...
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
    // 指向 CFS 运行队列所属的 CPU RQ 运行队列
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

![process_schedule](D:\gitlab\UtopianFuture.github.io\image\process_schedule.svg)

除了这 3 个重要的数据结构，内核还为每种调度类定义了各自的方法集，这里先给出 CFS 的方法集，其定义在 `kernel/sched/fair.c` 中，idle 调度类的方法集在 `kernel/sched/idle.c` 中，其他的应该差不多。

##### fair_sched_class

上面介绍过，内核支持多种调度类，rt, deadline, cfs，每个调度类都有一个函数集。

```c
/*
 * All the scheduling class methods:
 */
const struct sched_class fair_sched_class
	__section("__fair_sched_class") = {
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

```c
| kernel_clone
|	-> copy_process
|		-> sched_fork
|		-> sched_post_fork
|			-> task_fork_fair
|				-> update_curr
|				-> place_entity
|					-> seched_slice
|	-> wake_up_new_task // 创建好后需要加入调度队列开始执行
|		-> activate_task
|			-> enqueue_task
|				-> enqueue_task_fair
|					-> enqueue_entity
|						-> update_curr
|						-> place_entity
|						-> __enqueue_entity
```

[关键函数 copy_process](#关键函数copy_process) 中介绍了通过 `clone`, `vfork`, `fork` 等系统调用创建进程的过程，在创建的过程中也会初始化进程调度相关的数据结构。

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

    ...
}
```

下面分析一下 `sched_fork` 是怎样初始化调度器的。

##### 关键函数 sched_fork

```c
/*
 * fork()/clone()-time setup:
 */
int sched_fork(unsigned long clone_flags, struct task_struct *p)
{
	unsigned long flags;

    // 和之前的情况不同，这个看似关键的函数只是初始化 task_struct 中进程调度相关的数据结构
    // 这里没有初始化优先级
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
	p->prio = current->normal_prio; // 继承优先级

	uclamp_fork(p);

	...

	if (dl_prio(p->prio)) // deadline 进程为什么出错？deadline 进程不允许 fork deadline 类的子进程
		return -EAGAIN;
	else if (rt_prio(p->prio))
		p->sched_class = &rt_sched_class; // 实时进程
	else
		p->sched_class = &fair_sched_class; // 普通进程使用 CFS 调度类

	init_entity_runnable_average(&p->se);

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
	__set_task_cpu(p, smp_processor_id()); // 子进程开始所处的 CPU 应该就是父进程运行的 CPU，后续可能会改变
	if (p->sched_class->task_fork) // 没有该回调函数咋触发调度
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

##### 关键函数 task_fork_fair

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
		update_curr(cfs_rq); // 更新 curr->vruntime, curr->exec_start 等变量
		se->vruntime = curr->vruntime; // 调度的优先级和父进程一样
	}
	place_entity(cfs_rq, se, 1); // 根据情况对进程虚拟时间进行一些惩罚（？）

	...

	se->vruntime -= cfs_rq->min_vruntime; // 后面会加回来
	rq_unlock(rq, &rf);
}
```

这里有两点很难理解[^8]：

- 为什么进程初始化都要对 vruntime 进行惩罚？

  `place_entity` 函数在进程创建以及唤醒的时候都会调用，创建进程的时候传递参数 initial=1。主要目的是更新调度实体得到虚拟时间（se->vruntime 成员）。要和 cfs_rq->min_vruntime 的值保持差别不大，如果非常小的话，岂不是要上天（疯狂占用 cpu 运行）。

- 新进程的 vruntime 为什么需要减去当前 cfs_rq 的 min_vruntim？

> 现在计算进程的 vruntime 是基于当前 cpu 上的 cfs_rq，并且现在还没有加入当前 cfs_rq 的就绪队列上。等到当前进程创建完毕开始唤醒的时候，加入的就绪队列就不一定是现在计算基于的 cpu。所以，在加入就绪队列的函数中会根据情况加上当前就绪队列 cfs_rq->min_vruntime。为什么要“先减后加”处理呢？假设 cpu0 上的 cfs 就绪队列的最小虚拟时间 min_vruntime 的值是 1000000，此时创建进程的时候赋予当前进程虚拟时间是 1000500（继承自父进程）。但是，唤醒此进程加入的就绪队列却是 cpu1 上 CFS 就绪队列，cpu1 上的 cfs 就绪队列的最小虚拟时间 min_vruntime 的值如果是 9000000。如果不采用“先减后加”的方法，那么该进程在 cpu1 上运行肯定是“乐坏”了，疯狂的运行。现在的处理计算得到的调度实体的虚拟时间是 1000500 - 1000000 + 9000000 = 9000500，因此事情就不是那么的糟糕。

##### 关键函数 update_curr

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

    // 现在就更新么，新进程不是还没有放入到调度器中？
    // 看清楚了，这是当前进程的执行时间更新
	curr->exec_start = now;

	schedstat_set(curr->statistics.exec_max,
		      max(delta_exec, curr->statistics.exec_max));

	curr->sum_exec_runtime += delta_exec; // 总的执行时间是一段段计算的
	schedstat_add(cfs_rq->exec_clock, delta_exec);

	curr->vruntime += calc_delta_fair(delta_exec, curr); // 前面有说明，根据实际执行时间和 nice 值计算 vruntime
    // 更新最小 vruntime，
    // 选择 cfs_rq->min_vruntime、当前运行进程的 vruntime、rb 中最左侧的 se->vruntime 中最小的那个
    // min_vruntime 关系到下一次选择哪个进程调度
	update_min_vruntime(cfs_rq);

	account_cfs_rq_runtime(cfs_rq, delta_exec);
}
```

如果 min_vruntime 一直不更新的话，由于 min_vruntime 太小，导致后面创建的新进程根据这个值来初始化新进程的虚拟时间，岂不是新创建的进程有可能再一次疯狂了。这一次可能就是 cpu0 创建，在 cpu0 上面疯狂。

##### 关键函数 place_entity

根据情况对进程虚拟时间进行一些惩罚，

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

	/* sleeps up to a single latency don't count. */
	if (!initial) {
		unsigned long thresh = sysctl_sched_latency;

		/*
		 * Halve their sleep time's effect, to allow
		 * for a gentler effect of sleepers:
		 */
		if (sched_feat(GENTLE_FAIR_SLEEPERS))
			thresh >>= 1;

		vruntime -= thresh;
	}

	/* ensure we never gain time by being placed backwards. */
	se->vruntime = max_vruntime(se->vruntime, vruntime); // 相当于确定新进程的权重
}
```

> 这里主要是针对唤醒的进程，针对睡眠很久的的进程，我们总是期望它很快得到调度执行，毕竟人家睡了那么久。所以这里减去一定的虚拟时间作为补偿。而对于刚刚创建的进程，由于要给睡眠一段时间的进程让路，所以加上一定的虚拟时间作为补偿。
>
> 我们保证调度实体的虚拟时间不能倒退。为何呢？可以想一下，如果一个进程刚睡眠 1ms，然后醒来后你却要奖励 3ms（虚拟时间减去 3ms），然后他竟然赚了 2ms。作为调度器，我们不做亏本生意。你睡眠 100ms，奖励你 3ms，那就是没问题的。

##### 关键函数 sched_slice

`sched_vslice` -> `sched_slice`

该函数计算就绪队列中每个可执行进程能够得到多少虚拟时间。

```c
static u64 sched_slice(struct cfs_rq *cfs_rq, struct sched_entity *se)
{
	unsigned int nr_running = cfs_rq->nr_running; // 当前就绪队列中的可运行进程数
	u64 slice;

	if (sched_feat(ALT_PERIOD))
		nr_running = rq_of(cfs_rq)->cfs.h_nr_running;

	slice = __sched_period(nr_running + !se->on_rq); // 计算就绪队列中一个调度周期的长度

    // 没有组调度的情况下，就是循环一次
	for_each_sched_entity(se) {
		struct load_weight *load;
		struct load_weight lw;

		cfs_rq = cfs_rq_of(se);
		load = &cfs_rq->load; // 就绪队列上所有调度实体权重之和

		...

        // 计算调度实体 se 的权重占整个就绪队列权重的比例
        // 然后乘以调度周期时间即可得到当前调度实体应该运行的时间
        //（参数 weight 传递调度实体 se 权重，参数 lw 传递就绪队列权重 cfs_rq->load ）
        // 例如，就绪队列权重是 3072，当前调度实体 se 权重是 1024
        // 调度周期是 6ms，那么调度实体应该得到的时间是 6*1024/3072=2ms
		slice = __calc_delta(slice, se->load.weight, load);
	}

	if (sched_feat(BASE_SLICE))
		slice = max(slice, (u64)sysctl_sched_min_granularity);

	return slice;
}
```

`__sched_period` 会计算就绪队列中一个调度周期的长度，可以理解为一个调度周期的总时间片，它根据当前运行的进程数量来计算。CFS 有一个默认调度时间片 `sysctl_sched_latency`，而当就绪队列中的进程数大于 8 时，按照进程最小的调度延时 `sysctl_sched_min_granularity` 乘以就绪队列中的进程数来计算调度周期时间片。

```c
static unsigned int sched_nr_latency = 8;
unsigned int sysctl_sched_min_granularity			= 750000ULL;
unsigned int sysctl_sched_latency			= 6000000ULL;
```

```c
/*
 * The idea is to set a period in which each task runs once.
 *
 * When there are too many tasks (sched_nr_latency) we have to stretch
 * this period because otherwise the slices get too small.
 *
 * p = (nr <= nl) ? l : l*nr/nl
 */
static u64 __sched_period(unsigned long nr_running)
{
	if (unlikely(nr_running > sched_nr_latency))
		return nr_running * sysctl_sched_min_granularity;
	else
		return sysctl_sched_latency;
}
```

确定就绪队列的调度周期时间片后 `sched_slice` 就根据当前进程的权重来计算在 CFS 就绪队列总权重中可以分到的调度时间，这个调度时间是物理时间，`sched_vslice` 则根据物理时间计算 `vruntime`。

#### 进程加入 cfs 调度器

整个添加过程是这样的：

```plain
#0  enqueue_entity (cfs_rq=cfs_rq@entry=0xffff888237c29040, se=se@entry=0xffff888100264680, flags=8) at kernel/sched/fair.c:4283
#1  0xffffffff810f0a74 in enqueue_task_fair (rq=0xffff888237c28f40, p=<optimized out>, flags=<optimized out>) at kernel/sched/fair.c:5620
#2  0xffffffff810dcfa4 in enqueue_task (rq=0xffff888237c28f40, p=0xffff8881002645c0, flags=8) at kernel/sched/core.c:1976
#3  0xffffffff810e4099 in activate_task (flags=8, p=0xffff8881002645c0, rq=0xffff888237c28f40) at kernel/sched/core.c:2001
#4  wake_up_new_task (p=p@entry=0xffff8881002645c0) at kernel/sched/core.c:4463
#5  0xffffffff810a2106 in kernel_clone (args=args@entry=0xffffffff82e03e38) at kernel/fork.c:2608
#6  0xffffffff810a2705 in kernel_thread (fn=<optimized out>, arg=arg@entry=0x0 <fixed_percpu_data>, flags=flags@entry=1536)
    at kernel/fork.c:2636
#7  0xffffffff81c0b2b0 in rest_init () at init/main.c:711
#8  0xffffffff831b9f7c in arch_call_rest_init () at init/main.c:886
#9  0xffffffff831ba949 in start_kernel () at init/main.c:1141
#10 0xffffffff831b95a0 in x86_64_start_reservations (
    real_mode_data=real_mode_data@entry=0x2e3a920 <error: Cannot access memory at address 0x2e3a920>) at arch/x86/kernel/head64.c:525
#11 0xffffffff831b962d in x86_64_start_kernel (real_mode_data=0x2e3a920 <error: Cannot access memory at address 0x2e3a920>)
    at arch/x86/kernel/head64.c:506
#12 0xffffffff81000107 in secondary_startup_64 () at arch/x86/kernel/head_64.S:283
```

在[关键函数 kernel_clone](#关键函数kernel_clone) 中我们知道进程创建完后需要将其加入到就绪队列接受调度、运行，这里我们进一步分析 `wake_up_new_task`。

```c
pid_t kernel_clone(struct kernel_clone_args *args)
{
	...

	p = copy_process(NULL, trace, NUMA_NO_NODE, args); // 显而易见，这是关键函数

    ...

	wake_up_new_task(p); // 将新创建的进程加入到就绪队列接受调度、运行

	...

	return nr; // 返回新进程的 pid
}
```

##### 关键函数 wake_up_new_task

```c
void wake_up_new_task(struct task_struct *p)
{
	struct rq_flags rf;
	struct rq *rq;

	raw_spin_lock_irqsave(&p->pi_lock, rf.flags);
	WRITE_ONCE(p->__state, TASK_RUNNING); // 上来先置成 runable
#ifdef CONFIG_SMP
	/*
	 * Fork balancing, do it here and not earlier because:
	 *  - cpus_ptr can change in the fork path
	 *  - any previously selected CPU might disappear through hotplug
	 *
	 * Use __set_task_cpu() to avoid calling sched_class::migrate_task_rq,
	 * as we're not fully set-up yet.
	 */
    // 初始化上次使用的 CPU，不过 p 是新进程，还没有使用，怎么搞？
    // 应该是初始化时设置为父进程使用的 CPU
	p->recent_used_cpu = task_cpu(p);
	rseq_migrate(p);
    // 重新设置子进程将要运行的 CPU
    // 其实 CPU 在 sched_fork 中已经设置过，那为什么要重新设置？
    // 因为在初始化的过程中，cpus_allowed 可能发生变化，即该进程能够运行在哪个 CPU 上
    // 亦或者是之前选择的 CPU 关闭了
    // select_task_rq 选择调度域中最空闲的 CPU，这个很好理解
    __set_task_cpu(p, select_task_rq(p, task_cpu(p), SD_BALANCE_FORK, 0));
#endif
	rq = __task_rq_lock(p, &rf);
	update_rq_clock(rq);
	post_init_entity_util_avg(p);

	activate_task(rq, p, ENQUEUE_NOCLOCK); // 将子进程添加到调度器中，并将进程状态设为可执行
	trace_sched_wakeup_new(p);
    // 检查是否满足抢占当前运行进程的条件，满足的话需要设置 TIF_NEED_RESCHED 标志
    // 这里后续有需要可以深入研究一下
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

##### 关键函数 enqueue_task_fair

`activate_task` -> `enqueue_task`

这个函数是在 [sched_class](#sched_class) 中设置的回调函数，用于将调度实体加入到就绪队列的红黑树中。

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
    // 在没有组调度的情况下，其实就是遍历一个实体
	for_each_sched_entity(se) {
		if (se->on_rq)
			break;
		cfs_rq = cfs_rq_of(se);
		enqueue_entity(cfs_rq, se, flags);

		cfs_rq->h_nr_running++;
        // 为何 idle 数量也要增加
        // 因为前面重新检查了 idle 线程
		cfs_rq->idle_h_nr_running += idle_h_nr_running;

		...

		flags = ENQUEUE_WAKEUP;
	}

    // 为何要遍历 2 次？
    // 而且 h_nr_running，idle_h_nr_running 重复增加了，不会有问题么
    // 并不是遍历 2 次，上面遍历完后 se 就是空指针了，这里不会执行
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

##### 关键函数 enqueue_entity

```c
static void
enqueue_entity(struct cfs_rq *cfs_rq, struct sched_entity *se, int flags)
{
	bool renorm = !(flags & ENQUEUE_WAKEUP) || (flags & ENQUEUE_MIGRATED);
	bool curr = cfs_rq->curr == se; // 当前 cpu 在执行的 se 就是这次要调度的 se

	/*
	 * If we're the current task, we must renormalise before calling
	 * update_curr().
	 */
    // 前面介绍过，先减后加
    // 这样来看，不单单是创建新进程是需要减去 min_vruntime
    // dequeue 也会减
	if (renorm && curr)
		se->vruntime += cfs_rq->min_vruntime;

    // 更新当前进程的 vruntime，这个在 sched_fork 中就已经计算过了，为什么这里还要计算？
    // 不单单是进程创建会走到这里，还有其他很多情况也会调用该函数入队，所以这里要统一计算
	update_curr(cfs_rq);

	/*
	 * Otherwise, renormalise after, such that we're placed at the current
	 * moment in time, instead of some random moment in the past. Being
	 * placed in the past could significantly boost this task to the
	 * fairness detriment of existing tasks.
	 */
	if (renorm && !curr)
		se->vruntime += cfs_rq->min_vruntime; // 选择了一个新的进程执行

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

    // 对于刚唤醒的进程，补偿一些 vruntime
    // 新创建的进程就不会执行
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

我们将整个添加过程用框图的方式将 `enqueue_entity` 整理一下，

![enqueue_entity](D:\gitlab\UtopianFuture.github.io\image\enqueue_entity.svg)

#### 进程调度

以下 3 种情况可能会发起进程调度（有待确认）：

- 在**阻塞操作**中，如使用互斥量（mutex）、信号量（semaphore）、等待队列（waitqueue）等；
- 在**中断返回前和系统调用返回用户空间时**，检查 `TIF_NEED_RESCHED` 标志位以判断是否需要调度。将要被唤醒的进程不会马上调用 `schedule`，而是会将其调度实体被添加到 CFS 的就绪队列中（这在前面就已经分析了），并且设置 `TIF_NEED_RESCHED` 标志位；
- 周期性调度。

而被唤醒的进程加入的调度时机根据内核是否可以被抢占可分成两种情况：

- 内核可抢占
  - 如果唤醒动作（何为唤醒动作，将进程插入到就绪队列么）发生在系统调用或异常处理上下文中，**在下一次调用 `preempt_enable` 时会检查是否需要抢占调度**；
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

	sched_submit_work(tsk); // 好吧，不知道这是干嘛的
	do {
		preempt_disable(); // 关闭内核抢占。为什么关闭、开启都是调用 barrier() 呢？
		__schedule(SM_NONE); // 调度的核心函数
		sched_preempt_enable_no_resched();
	} while (need_resched());
	sched_update_worker(tsk);
}
```

##### 关键函数 __schedule

该函数是所有进程切换的起点，里面有一个函数 `trace_sched_switch`，perfetto 能够根据该 trace 点记录所有的进程切换，很好的设计。

```c
static void __sched notrace __schedule(unsigned int sched_mode)
{
	struct task_struct *prev, *next;
	unsigned long *switch_count;
	unsigned long prev_state;
	struct rq_flags rf;
	struct rq *rq;
	int cpu;

    // 每个 cpu 都会执行 sechedule，可是多核编程又没有好的材料学习
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
            // 在 rt 的 spinlock 等锁时，就会执行到这里（rt 的 spinlock 改成空闲等待了）
            // 否则直接选择下一个执行的进程
            // 该函数与 activate_task 流程类似，只是把入队改成了出队
			deactivate_task(rq, prev, DEQUEUE_SLEEP | DEQUEUE_NOCLOCK);

			if (prev->in_iowait) {
				atomic_inc(&rq->nr_iowait);
				delayacct_blkio_start();
			}
		}
		switch_count = &prev->nvcsw;
	}

	next = pick_next_task(rq, prev, &rf); // 选择下一个执行的进程
	clear_tsk_need_resched(prev); // 清除当前进程的 TIF_NEED_RESCHED 标志位
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

        // 记录所有的进程切换
		trace_sched_switch(sched_mode & SM_MASK_PREEMPT, prev, next);

		/* Also unlocks the rq: */
		rq = context_switch(rq, prev, next, &rf); // 进程上下文切换
	}
    ...
}
```

##### 关键函数 pick_next_task

```c
#define SCHED_DATA				\
	STRUCT_ALIGN();				\
	__begin_sched_classes = .;		\
	*(__idle_sched_class)			\
	*(__fair_sched_class)			\
	*(__rt_sched_class)			\
	*(__dl_sched_class)			\
	*(__stop_sched_class)			\
	__end_sched_classes = .;
```

```c
/*
 * Pick up the highest-prio task:
 */
static inline struct task_struct *
pick_next_task(struct rq *rq, struct task_struct *prev, struct rq_flags *rf)
{
	const struct sched_class *class;
	struct task_struct *p;

	/*
	 * Optimization: we know that if all tasks are in the fair class we can
	 * call that function directly, but only if the @prev task wasn't of a
	 * higher scheduling class, because otherwise those loose the
	 * opportunity to pull in more work from other CPUs.
	 */
	if (likely(prev->sched_class <= &fair_sched_class &&
		   rq->nr_running == rq->cfs.h_nr_running)) {

		p = pick_next_task_fair(rq, prev, rf);
		if (unlikely(p == RETRY_TASK))
			goto restart;

		/* Assumes fair_sched_class->next == idle_sched_class */
		if (!p) {
			put_prev_task(rq, prev);
			p = pick_next_task_idle(rq);
		}

		return p;
	}

restart:
	put_prev_task_balance(rq, prev, rf);

	for_each_class(class) { // 遍历按照 __end_sched_classes -> __begin_sched_classes 顺序
		p = class->pick_next_task(rq);
		if (p)
			return p;
	}

	/* The idle class should always have a runnable task: */
	BUG();
}
```

##### 关键函数 pick_next_entity

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

##### 关键函数 context_switch

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

一个特殊情况是新建进程，第一次执行的切入点在 `copy_thread` 中指定的 `ret_from_fork` 中，因此，**当 `switch_to` 切换到新建进程中时，新进程从 `ret_from_fork` 开始执行**。

##### 关键函数 switch_mm

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

##### 关键函数 switch_to

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

假设进程 A 在 CPU 0 上主动执行 `switch_to` 切换到 B 执行，那么 A 执行了 A0，然后运行了 `switch_to`。在 `switch_to` 中 CPU 0 切换到 B 上硬件上下文，运行 B，A 被换出了，这时 B 直接运行自己的代码段，**而 A1 还没有执行，所以需要 `last` 指向 A**。

那为何不直接使用 `prev` 呢？在 `switch_to` 执行前，`prev` 指向 A，但是  `switch_to` 执行完后，此时内核栈已经从 A 的内核栈切换到 B 的内核栈，读取 `prev` 变成了读取 B 的 `prev` 参数，而不是 A 的 `prev` 参数，所以读出来的 `prev` 不一定指向 A。**那为什么 `__switch_to_asm` 能够返回指向 A 的指针？**因为 `__switch_to` 会返回 `prev`。

经过一段时间，某个 CPU 上的进程 X 主动执行 `switch_to` 切换到 A 执行，即 A 从 CPU 0 切换到 CPU n。这时 X 进入睡眠，而 A 从上次的睡眠点开始运行，也就是说开始运行 A1，而这时 `last` 指向 X。通常 A1 是 `finish_task_switch`，即 **A 重新运行前需要通过这个函数对 X 进行一些清理工作**，而 `last` 就是传给 `finish_task_switch` 的参数。

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

##### 关键函数 finish_task_switch

这个函数是**在 next 进程上下文中为 prev 进程收尾的**，大概了解以下它做了哪些工作。

```C
/**
 * finish_task_switch - clean up after a task-switch
 * @prev: the thread we just switched away from.
 *
 * finish_task_switch must be called after the context switch, paired
 * with a prepare_task_switch call before the context switch.
 * finish_task_switch will reconcile locking set up by prepare_task_switch,
 * and do any other architecture-specific cleanup actions.
 *
 * Note that we may have delayed dropping an mm in context_switch(). If
 * so, we finish that here outside of the runqueue lock. (Doing it
 * with the lock held can cause deadlocks; see schedule() for
 * details.)
 *
 * The context switch have flipped the stack from under us and restored the
 * local variables which were saved when this task called schedule() in the
 * past. prev == current is still correct but we need to recalculate this_rq
 * because prev may have moved to another CPU.
 */
static struct rq *finish_task_switch(struct task_struct *prev)
	__releases(rq->lock)
{
	struct rq *rq = this_rq();
	struct mm_struct *mm = rq->prev_mm;
	long prev_state;

	...

	rq->prev_mm = NULL;

	/*
	 * A task struct has one reference for the use as "current".
	 * If a task dies, then it sets TASK_DEAD in tsk->state and calls
	 * schedule one last time. The schedule call will never return, and
	 * the scheduled task must drop that reference.
	 *
	 * We must observe prev->state before clearing prev->on_cpu (in
	 * finish_task), otherwise a concurrent wakeup can get prev
	 * running on another CPU and we could rave with its RUNNING -> DEAD
	 * transition, resulting in a double drop.
	 */
	prev_state = READ_ONCE(prev->__state);
	vtime_task_switch(prev);
	perf_event_task_sched_in(prev, current);
	finish_task(prev); // 将 on_cpu 置为 0

    ...

	/*
	 * When switching through a kernel thread, the loop in
	 * membarrier_{private,global}_expedited() may have observed that
	 * kernel thread and not issued an IPI. It is therefore possible to
	 * schedule between user->kernel->user threads without passing though
	 * switch_mm(). Membarrier requires a barrier after storing to
	 * rq->curr, before returning to userspace, so provide them here:
	 *
	 * - a full memory barrier for {PRIVATE,GLOBAL}_EXPEDITED, implicitly
	 *   provided by mmdrop(),
	 * - a sync_core for SYNC_CORE.
	 */
	if (mm) {
		membarrier_mm_sync_core_before_usermode(mm);
		mmdrop(mm);
	}

	...

	return rq;
}
```

### RT

rt 调度类使用的是优先级队列的方式。对于优先级 0~99 的 rt 进程，有 100 个队列与之对应。

#### 相关数据结构

##### rt_rq

调度方式也很简单，找到位图中的第一个 se。

```c
struct rt_rq {
	struct rt_prio_array active; // 优先级队列，100 个优先级的链表，并定义了位图，用于快速查询
	unsigned int rt_nr_running; // 在 RT 运行队列中所有活动的任务数
	unsigned int rr_nr_running; // 这个用来干嘛
#if defined CONFIG_SMP || defined CONFIG_RT_GROUP_SCHED
	struct {
		int curr; /* highest queued rt task prio */ // 当前 RT 任务的最高优先级
#ifdef CONFIG_SMP
        // 下一个要运行的 RT 任务的优先级，如果两个任务都有最高优先级，则 curr == next
		int next; /* next highest */
#endif
	} highest_prio;
#endif
#ifdef CONFIG_SMP
	unsigned long rt_nr_migratory; // 任务没有绑定在某个 CPU 上时，这个值会增减，用于任务迁移
	unsigned long rt_nr_total; // 用于 overload 检查
	int overloaded; // RT 运行队列过载，则将任务推送到其他 CPU
	struct plist_head pushable_tasks;   // 优先级列表，用于推送过载任务
#endif /* CONFIG_SMP */
	int rt_queued; // 表示 RT 运行队列已经加入 rq 队列，可以被调度，限流后这把这个变量置为 0

	int rt_throttled; // 用于限流操作
	u64 rt_time; // 累加的运行时长，超出了本地 rt_runtime 时，则进行限制
	u64 rt_runtime; // 分配给该队列的运行时长
	/* Nests inside the rq lock: */
	raw_spinlock_t rt_runtime_lock;

#ifdef CONFIG_RT_GROUP_SCHED
	unsigned long rt_nr_boosted; // 用于优先级翻转问题解决

	struct rq *rq; // 指向运行队列
	struct task_group *tg; // 指向任务组
#endif
};
```

##### rt_prio_array

```c
struct rt_prio_array {
	DECLARE_BITMAP(bitmap, MAX_RT_PRIO+1); /* include 1 bit for delimiter */ // 用来快速找到 se
	struct list_head queue[MAX_RT_PRIO]; // 0~99
};
```

##### sched_rt_entity

```c
struct sched_rt_entity {
	struct list_head		run_list; // 用于加入到优先级队列中
	unsigned long			timeout; // 设置的时间超时
	unsigned long			watchdog_stamp; // 用于记录 jiffies 值
	unsigned int			time_slice; // 时间片，100ms
	unsigned short			on_rq; // 是否入队，入队表示可以执行
	unsigned short			on_list;

	struct sched_rt_entity		*back; // 临时用于从上往下连接 RT 调度实体时使用
#ifdef CONFIG_RT_GROUP_SCHED
	struct sched_rt_entity		*parent; // 指向父 RT 调度实体
	/* rq on which this entity is (to be) queued: */
	struct rt_rq			*rt_rq; // RT 调度实体所属的实时运行队列，被调度
	/* rq "owned" by this entity/group: */
	struct rt_rq			*my_q; // RT 调度实体所拥有的实时运行队列，用于管理子任务或子组任务
#endif
} __randomize_layout;
```

##### rt_sched_class

这些回调函数是了解 rt 调度类的关键。

```c
const struct sched_class rt_sched_class
	__section("__rt_sched_class") = {
    // wake_up_process->activate_task 时调用，将 task 加入到队列
	.enqueue_task		= enqueue_task_rt,
    // __schedule->deactivate_task，主动触发调度时执行，将 task 从 rq 中移除
	.dequeue_task		= dequeue_task_rt,
	.yield_task		= yield_task_rt, // 将对应的 task 重新放到队首/队尾，有点像进程抢占

	.check_preempt_curr	= check_preempt_curr_rt, // 进程抢占

	.pick_next_task		= pick_next_task_rt, // 选择下一个运行的 task
    // 更新负载，然后将可以在多个 CPU 上执行的 task 添加到 rt.pushable_tasks 链表中
	.put_prev_task		= put_prev_task_rt,
	.set_next_task          = set_next_task_rt,

#ifdef CONFIG_SMP
	.balance		= balance_rt, // 从其他 CPU 拉一些进程过来执行
    // wake_up_process 的时候，为了避免当前 CPU 负载过高，尝试选择一个低负载的 CPU 运行
	.select_task_rq		= select_task_rq_rt,
	.set_cpus_allowed       = set_cpus_allowed_common, // 几个调度类都是这个回调函数，设置 CPU 亲和性
	.rq_online              = rq_online_rt,
	.rq_offline             = rq_offline_rt,
    // 从传入的 rq 中推一些任务到其他 rq，推的条件比较严格
    // 唤醒的任务 p 不是 rq 上正在 running 的任务
    // 且当前 rq 也没有设置 resched 标志位(不会马上重新调度)
    // 且 p 也允许在其它 CPU 上运行
    // 且 rq 当前正在运行的任务是 dl 或 rt 任务
    // 且 rq 的当前任务只能在当前 CPU 运行或优先级比 p 更高
	.task_woken		= task_woken_rt,
    // 切换进程所属的调度类，从 rt 调度类切换到其他调度类
    // sched_setscheduler 和修改优先级的时候都会调用
	.switched_from		= switched_from_rt,
	.find_lock_rq		= find_lock_lowest_rq,
#endif

    // 时钟等周期性检查会调用到该函数，会更新 CPU 负载，当前任务运行时间等信息
    // 然后置上 TIF_NEED_RESCHED 位，后续系统调用、中断退出时才会调用 schedule() 函数发生调度
	.task_tick		= task_tick_rt,

	.get_rr_interval	= get_rr_interval_rt,

	.prio_changed		= prio_changed_rt,
	.switched_to		= switched_to_rt, // 切换进程的所属的调度类，从其他调度类切换到 rt 调度类

    // 更新进程执行时间，与负载均衡和带宽控制有关
    // 在有任务 enqueue/dequeue 都会执行
	.update_curr		= update_curr_rt,

#ifdef CONFIG_UCLAMP_TASK
	.uclamp_enabled		= 1,
#endif
};
```

#### 进程加入 rt 调度器

前面分析了进程创建，然后加入到 cfs_rq 的逻辑，这里我们接着分析怎样加入到 rt_rq 中，

```c
| kernel_clone
|	-> copy_process
|		-> sched_fork // 这里根据进程优先级，选择 rt_sched_class
|	-> wake_up_new_task // 创建好后需要加入调度队列开始执行
|		-> __set_task_cpu
|			-> select_task_rq
|				// 不同调度类的回调函数不同
|				// 如果当前 rq 运行的进程是 rt 进程，那么尝试将该进程放到其他 cpu 上
|				// 否则就放在 p->cpu 上执行，这个 p->cpu 应该是继承自父进程
|				-> select_task_rq_rt
|					-> find_lowest_rq
|		-> activate_task
|			-> enqueue_task
|				-> enqueue_task_rt
|		-> check_preempt_curr // 如果新加入的进程优先级高于当前进程，触发 resched
|		-> task_woken_rt // 每次唤醒新的进程，都尝试 push
```

第一个普通进程是 idle 进程，静态初始化的，而第一个实时进程是通过 `sched_setscheduler` 系统调用来配置。

看看实际的调用栈，

```c
#0  enqueue_task_rt (rq=0xffff0001f0ff5b80, p=0xffff0000c089ab80, flags=14) at kernel/sched/rt.c:1384
#1  0xffff8000100b83cc in enqueue_task (flags=<optimized out>, p=0xffff0000c089ab80, rq=0xffff0001f0ff5b80) at kernel/sched/core.c:1651
#2  __sched_setscheduler (p=p@entry=0xffff0000c089ab80, attr=attr@entry=0xffff800011efbd10, user=user@entry=false, pi=pi@entry=true) at kernel/sched/core.c:6136
#3  0xffff8000100b8dc0 in _sched_setscheduler (param=<synthetic pointer>, param=<synthetic pointer>, check=false, policy=1, p=0xffff0000c089ab80 at kernel/sched/core.c:6183
#4  sched_setscheduler_nocheck (param=<synthetic pointer>, policy=1, p=0xffff0000c089ab80) at kernel/sched/core.c:6229
#5  sched_set_fifo (p=0xffff0000c089ab80) at kernel/sched/core.c:6253
#6  0xffff800011607300 in watchdog_dev_init () at drivers/watchdog/watchdog_dev.c:1188 // 原来 watchdog 是在这里设置为 rt
#7  0xffff80001160721c in watchdog_init () at drivers/watchdog/watchdog_core.c:424
#8  0xffff800010014df0 in do_one_initcall (fn=0xffff800011607204 <watchdog_init>) at init/main.c:1202
#9  0xffff8000115b12bc in do_initcall_level (command_line=0xffff0000c01fcc00 "root", level=4) at init/main.c:1275
#10 do_initcalls () at init/main.c:1291
#11 do_basic_setup () at init/main.c:1311
#12 kernel_init_freeable () at init/main.c:1515
#13 0xffff800010e1019c in kernel_init (unused=<optimized out>) at init/main.c:1402
#14 0xffff800010016428 in ret_from_fork () at arch/arm64/kernel/entry.S:1049
```

##### 关键函数 enqueue_task_rt

```c
| enqueue_task_rt
|	-> enqueue_rt_entity
|		// 无论入队/出队，都要先从原来的链表中摘出来
|		// 然后是入队（DEQUEUE_SAVE）就插入到链表头
|		// 出队（ENQUEUE_RESTORE）插入到链表尾
|		-> dequeue_rt_stack
|		-> __enqueue_rt_entity // 都要执行插入操作
|		-> enqueue_top_rt_rq
|	->enqueue_pushable_task
```

```c
static void
enqueue_task_rt(struct rq *rq, struct task_struct *p, int flags)
{
        // 接来下找到 task_struct 对应的 rt_se
        // 这个才是能插入到优先级队列的
        struct sched_rt_entity *rt_se = &p->rt;

        if (flags & ENQUEUE_WAKEUP)
                rt_se->timeout = 0;

        enqueue_rt_entity(rt_se, flags);

        // 该进程能够在多个 CPU 上执行，就可以放到 pushable 队列中
        // 后续可以 push 到其他 CPU 上执行
        if (!task_current(rq, p) && p->nr_cpus_allowed > 1)
                enqueue_pushable_task(rq, p);
}

static void enqueue_rt_entity(struct sched_rt_entity *rt_se, unsigned int flags)
{
        struct rq *rq = rq_of_rt_se(rt_se);

        dequeue_rt_stack(rt_se, flags); // 先从原来的链表中摘出来
        for_each_sched_rt_entity(rt_se)
                __enqueue_rt_entity(rt_se, flags); // 再插入到新链表中
        enqueue_top_rt_rq(&rq->rt);
}
```

```c
static void __enqueue_rt_entity(struct sched_rt_entity *rt_se, unsigned int flags)
{
	struct rt_rq *rt_rq = rt_rq_of_se(rt_se);
	struct rt_prio_array *array = &rt_rq->active;
	struct rt_rq *group_rq = group_rt_rq(rt_se);
	struct list_head *queue = array->queue + rt_se_prio(rt_se); // 找到该优先级对应的队列

	/*
	 * Don't enqueue the group if its throttled, or when empty.
	 * The latter is a consequence of the former when a child group
	 * get throttled and the current group doesn't have any other
	 * active members.
	 */
	if (group_rq && (rt_rq_throttled(group_rq) || !group_rq->rt_nr_running)) {
		if (rt_se->on_list)
			__delist_rt_entity(rt_se, array);
		return;
	}

	if (move_entity(flags)) {
		WARN_ON_ONCE(rt_se->on_list);
		if (flags & ENQUEUE_HEAD)
			list_add(&rt_se->run_list, queue); // 插入即可
		else
			list_add_tail(&rt_se->run_list, queue); // 一般都是插入到队尾

		__set_bit(rt_se_prio(rt_se), array->bitmap);
		rt_se->on_list = 1;
	}
	rt_se->on_rq = 1;

    // 更新 rt_rq 中最高的优先级、rr_nr_running 等统计信息
	inc_rt_tasks(rt_se, rt_rq);
}
```

##### 关键函数 check_preempt_curr_rt

这里会置上了 `TIF_NEED_RESCHED`，会在后续中断返回前和系统调用返回用户空间时进行调度。

```c
static void check_preempt_curr_rt(struct rq *rq, struct task_struct *p, int flags)
{
    // rt 的调度逻辑很简单，优先级高的先执行
    // 因为进程 p 在前面已经添加到优先级队列，重新调度，肯定是它先执行
	if (p->prio < rq->curr->prio) {
		resched_curr(rq);
		return;
	}

#ifdef CONFIG_SMP
    // 这里相同优先级也可能将进程 p 移动到队首，然后触发调度
	if (p->prio == rq->curr->prio && !test_tsk_need_resched(rq->curr))
		check_preempt_equal_prio(rq, p);
#endif
}
```

`resched_curr` 只是将 `TIF_NEED_RESCHED` 位置上，随后的 `task_woken_rt` 函数还会尝试 push 操作，即将本运行队列上最高优先级（不在运行）的实时任务，转移到另一个运行队列。

##### 关键函数 check_preempt_equal_prio

```c
static void check_preempt_equal_prio(struct rq *rq, struct task_struct *p)
{
	/*
	 * Current can't be migrated, useless to reschedule,
	 * let's hope p can move out.
	 */
    // 1. 如果 curr 无法在其他 CPU 上运行，也无法找到一个负载更低的 CPU
    // 这个好理解，当前进程迁移不了，只能看 p 能不能迁移
	if (rq->curr->nr_cpus_allowed == 1 ||
	    !cpupri_find(&rq->rd->cpupri, rq->curr, NULL)) // cpupri_find 后面介绍
		return; // 只能在后面的 task_woken_rt 中尝试能不能将进程 p push 到其他 CPU

	/*
	 * p is migratable, so let's not schedule it and
	 * see if it is pushed or pulled somewhere else.
	 */
    // 2. 进程 p 能够在多个进程上执行，同时能够找到一个负载更低的 CPU
    // 进程 p 能迁移，那就没必要移到队首，与同优先级的进程抢 CPU 了
	if (p->nr_cpus_allowed != 1 &&
	    cpupri_find(&rq->rd->cpupri, p, NULL))
		return; // 后面的 task_woken_rt 会将进程 p push 到其他 CPU

	/*
	 * There appear to be other CPUs that can accept
	 * the current task but none can run 'p', so lets reschedule
	 * to try and push the current task away:
	 */
    // 移到队首，抢占当前 CPU
	requeue_task_rt(rq, p, 1);
	resched_curr(rq);
}
```

#### 进程调度

`__schedule` 是唯一的进程切换入口，在 `for_each_class` 按照 stop->dl->rt->fair->idle 的顺序调用各自的 `pick_next_task` 函数。

```c
| __schedule
|	-> pick_next_task
|		-> put_prev_task_balance
|			-> balance_rt
|				-> pull_rt_task // 从其他 CPU 拉一些进程过来执行
|			-> put_prev_task_rt
|				-> update_curr_rt // 更新 rt_rq 的执行耗时等
|				-> update_rt_rq_load_avg // pelt 相关，暂不关注
|				-> enqueue_pushable_task // 如果可以在多个 CPU 上执行该进程，就可以放到 pushable_tasks 链表里
| 		-> pick_next_task_rt
|			// 检查 rt_queued 是否为 1，如触发 rt throttling
|			// rt_queued 就会被置为 0，表示该 rt_rq 不可调度
|			-> sched_rt_runnable
|			-> _pick_next_task_rt // 选择优先级最高的队列
|				-> pick_next_rt_entity
|			-> set_next_task_rt // 将选中的 task 从 rt.pushable_tasks 中删除，并更新队列中优先级最高的进程的信息
|	-> clear_tsk_need_resched
|	-> clear_preempt_need_resched
|	-> context_switch // 这里也会触发负载均衡
```

##### 关键函数 pick_next_task_rt

```c
static struct sched_rt_entity *pick_next_rt_entity(struct rq *rq,
						   struct rt_rq *rt_rq)
{
	struct rt_prio_array *array = &rt_rq->active;
	struct sched_rt_entity *next = NULL;
	struct list_head *queue;
	int idx;

    // 返回 b[0]，即第一个 task，也就是优先级最高的队列中，最早入队的 task
	idx = sched_find_first_bit(array->bitmap);
	BUG_ON(idx >= MAX_RT_PRIO);

	queue = array->queue + idx;
	next = list_entry(queue->next, struct sched_rt_entity, run_list);

	return next;
}
```

#### 调度算法

RT 调度类有两种调度算法：

- `SCHED_FIFO`：实时进程调度策略，先进先出调度没有时间片，没有更高优先级的情况下，只能等待主动让出 CPU；
- `SCHED_RR`：实时进程调度策略，时间片轮转，进程用完时间片后加入优先级对应运行队列的尾部，把 CPU 让给同优先级的其他进程；

我们可以通过 `sched_setscheduler ` 系统调用来设置进程的调度类和调度策略，

```c
| sched_setscheduler
|	-> do_sched_setscheduler
|		-> sched_setscheduler
|			-> _sched_setscheduler
|				-> __sched_setscheduler // 很复杂的函数，后续再分析
```

```c
SYSCALL_DEFINE3(sched_setscheduler, pid_t, pid, int, policy, struct sched_param __user *, param)
{
	...

	return do_sched_setscheduler(pid, policy, param);
}
```

对于使用 SCHED_RR 调度策略的进程，在定时器到期时会触发调度，

```c
#0  scheduler_tick () at kernel/sched/core.c:4639
#1  0xffffffc0100e4598 in update_process_times (user_tick=0) at kernel/time/timer.c:1754
#2  0xffffffc0100f5e00 in tick_periodic (cpu=cpu@entry=0) at kernel/time/tick-common.c:98
#3  0xffffffc0100f5ee4 in tick_handle_periodic (dev=0xffffff81f0fa7c00) at kernel/time/tick-common.c:110
#4  0xffffffc010a00b6c in timer_handler (evt=<optimized out>, access=1) at drivers/clocksource/arm_arch_timer.c:647
#5  arch_timer_handler_virt (irq=<optimized out>, dev_id=<optimized out>) at drivers/clocksource/arm_arch_timer.c:658
#6  0xffffffc0100be62c in handle_percpu_devid_irq (desc=0xffffff80c010f800) at kernel/irq/chip.c:930
#7  0xffffffc0100b6ca8 in generic_handle_irq_desc (desc=<optimized out>) at ./include/linux/irqdesc.h:152
#8  generic_handle_irq (irq=<optimized out>) at kernel/irq/irqdesc.c:656
#9  __handle_domain_irq (domain=0xffffff80c0180000, hwirq=<optimized out>, lookup=lookup@entry=true, regs=regs@entry=0xffffffc011533db0)
    at kernel/irq/irqdesc.c:693
#10 0xffffffc010010058 in handle_domain_irq (regs=0xffffffc011533db0, hwirq=<optimized out>, domain=<optimized out>)
    at ./include/linux/irqdesc.h:170
#11 gic_handle_irq (regs=0xffffffc011533db0) at drivers/irqchip/irq-gic.c:372
#12 0xffffffc010011b4c in el1_irq () at arch/arm64/kernel/entry.S:687
```

定时器的周期在 `start_kernel` -> `time_init` -> `timer_probe` 时会初始化，最终会在 `tick_setup_device` 中赋值，

```c
# undef HZ
# define HZ		CONFIG_HZ	/* Internal kernel timer frequency */ // 可以在 menuconfig 中配置
# define USER_HZ	100		/* some user interfaces are */
# define CLOCKS_PER_SEC	(USER_HZ)       /* in "ticks" like times() */
#endif /* __ASM_GENERIC_PARAM_H */

static void tick_setup_device(struct tick_device *td,
			      struct clock_event_device *newdev, int cpu,
			      const struct cpumask *cpumask)
{
	
    ...
        
	if (!td->evtdev) {
		/*
		 * If no cpu took the do_timer update, assign it to
		 * this cpu:
		 */
		if (tick_do_timer_cpu == TICK_DO_TIMER_BOOT) {
			tick_do_timer_cpu = cpu;

			tick_next_period = ktime_get();
			tick_period = NSEC_PER_SEC / HZ; // HZ 可以通过 menuconfig 配置，一般配置为 250

            ...
		}

		/*
		 * Startup in periodic mode first.
		 */
		td->mode = TICKDEV_MODE_PERIODIC;
	} else {
		
        ...
            
	}

	...
}
```

配置为 250 的话，那么时钟中断的周期就是 1/250*1000=4ms，RR 的周期就是 4 * 25ms = 100ms。

```c
| hrtick
| scheduler_tick
|	-> task_tick_rt
|		-> update_curr_rt
|		-> update_rt_rq_load_avg
|		-> requeue_task_rt
|		-> resched_curr
```

##### 关键函数 task_tick_rt

```c
static void task_tick_rt(struct rq *rq, struct task_struct *p, int queued)
{
	struct sched_rt_entity *rt_se = &p->rt;

	update_curr_rt(rq); // 带宽控制，后面介绍
	update_rt_rq_load_avg(rq_clock_pelt(rq), rq, 1);

	watchdog(rq, p);

	/*
	 * RR tasks need a special form of timeslice management.
	 * FIFO tasks have no timeslices.
	 */
	if (p->policy != SCHED_RR) // FIFO 调度类不需要调度
		return;

    // 经过了 25 个时钟中断，就会被强制切出去
	if (--p->rt.time_slice)
		return;

	p->rt.time_slice = sched_rr_timeslice;

	/*
	 * Requeue to the end of queue if we (and all of our ancestors) are not
	 * the only element on the queue
	 */
	for_each_sched_rt_entity(rt_se) {
		if (rt_se->run_list.prev != rt_se->run_list.next) { // 有多个 rt 进程
			requeue_task_rt(rq, p, 0); // 放到队尾，这就是 RR，轮着来
			resched_curr(rq);
			return;
		}
	}
}
```

#### RT Throttling

RT Throttling 的核心思想是：限制 CPU 上可运行的 RT 任务在**检测周期**内占用 CPU 的时长不能超过**运行门限**，防止普通进程饿死。rt_period 和 rt_runtime 是 RT 带宽控制的两个可配置参数。这两个参数非常有用，实际生产环境中，如果 rt 进程设置的过多，很容易普通进程由于等锁，等调度导致 hungtask。

```c
/*
 * period over which we measure -rt task CPU usage in us.
 * default: 1s
 */
unsigned int sysctl_sched_rt_period = 1000000;

/*
 * part of the period that we allow rt tasks to run in us.
 * default: 0.95s
 */
int sysctl_sched_rt_runtime = 950000;
```

这两个值可以在用户态通过 `/sys/fs/cgroup/cpu/rt_runtime_us` 和 `/sys/fs/cgroup/cpu/rt_period_us` 来进行设置。

##### 相关数据结构

**rt_bandwidth**

rt 还有带宽控制的特性，用于防止 rt 进程太多，普通进程得不到 CPU 时间，然后饿死的问题。

```c
struct rt_bandwidth {
	/* nests inside the rq lock: */
	raw_spinlock_t		rt_runtime_lock;
	ktime_t			rt_period;      // 时间周期，默认为 1000ms
	u64			rt_runtime;     // 一个时间周期内的运行时间，超过则限流，默认值为 950ms
	struct hrtimer		rt_period_timer;    // 时间周期定时器
	unsigned int		rt_period_active;
};
```

`struct rt_bandwidth def_rt_bandwidth;`，**该变量为所有 CPU 的 rt_rq 公用**。

##### 初始化流程

和 RT Throttling 相关的变量是 `rt_bandwidth->rt_period`, `rt_bandwidth->rt_runtime`, `rt_rq->rt_time`, `rt_rq->rt_runtime`。

其中 `rt_bandwidth->rt_period`, `rt_bandwidth->rt_runtime` 是所有 CPU 共用的，表示系统中设置的 period 是多少，其主要作用是用来设置定时器的到期时间。每个 rt_rq 在初始化的时候，会复制到本身的 `rt_rq->rt_time`, `rt_rq->rt_runtime` 变量，后续使用的是该变量。

```c
| start_kernel
|	-> sched_init
|		-> init_rt_bandwidth // 设置定时器的回调函数 sched_rt_period_timer，到期就会执行
|			-> hrtimer_init
```

```c
void __init sched_init(void)
{
	...

    // 初始化 def_rt_bandwidth
	init_rt_bandwidth(&def_rt_bandwidth, global_rt_period(), global_rt_runtime());
	init_dl_bandwidth(&def_dl_bandwidth, global_rt_period(), global_rt_runtime());

	...

	for_each_possible_cpu(i) {
		struct rq *rq;

		rq = cpu_rq(i);
		
        ...

		rq->rt.rt_runtime = def_rt_bandwidth.rt_runtime; // 后续判断是通过 rq->rt.rt_runtime
		
        ...
	}

	...
}
```

```c
// 传入的就是 sysctl_sched_rt_period, sysctl_sched_rt_runtime
void init_rt_bandwidth(struct rt_bandwidth *rt_b, u64 period, u64 runtime)
{
	rt_b->rt_period = ns_to_ktime(period);
	rt_b->rt_runtime = runtime;

	raw_spin_lock_init(&rt_b->rt_runtime_lock);

	hrtimer_init(&rt_b->rt_period_timer, CLOCK_MONOTONIC,
		     HRTIMER_MODE_REL_HARD);
    // 该回调函数在定时器到期时执行，后面分析
	rt_b->rt_period_timer.function = sched_rt_period_timer;
}
```

##### 带宽控制流程

触发带宽控制的路径如下图所示，

![rt_throttlong](D:\gitlab\UtopianFuture.github.io\image\rt_throttlong.svg)

**关键函数 update_curr_rt**

该函数在 rt_rq 中任务需要变动时会执行，

```c
static void update_curr_rt(struct rq *rq)
{
	struct task_struct *curr = rq->curr;
	struct sched_rt_entity *rt_se = &curr->rt;
	u64 delta_exec;
	u64 now;

	now = rq_clock_task(rq);
	delta_exec = now - curr->se.exec_start; // 和上次统计时的时间间隔

	curr->se.sum_exec_runtime += delta_exec;
	account_group_exec_runtime(curr, delta_exec);

	curr->se.exec_start = now; // 更新统计时间戳
	cgroup_account_cputime(curr, delta_exec);

	if (!rt_bandwidth_enabled())
		return;

	for_each_sched_rt_entity(rt_se) {
		struct rt_rq *rt_rq = rt_rq_of_se(rt_se);
		int exceeded;

		if (sched_rt_runtime(rt_rq) != RUNTIME_INF) {
			raw_spin_lock(&rt_rq->rt_runtime_lock);
			rt_rq->rt_time += delta_exec; // 每次调度都会累加
			exceeded = sched_rt_runtime_exceeded(rt_rq); // 检查是否触发了 rt throttling
			if (exceeded)
				resched_curr(rq); // 标识该队列需要重新调度
			raw_spin_unlock(&rt_rq->rt_runtime_lock);
            // 为什么这里还要调用一次定时器的回调函数 do_sched_rt_period_timer
            // 应该是立刻检查定时器是否到期，如果到期，该 rt_rq 就可以立刻调度
            // 减少 rt 进程的阻塞时间
            // 后面详细介绍该算法
			if (exceeded)
				do_start_rt_bandwidth(sched_rt_bandwidth(rt_rq));
		}
	}
}
```

这里进一步解释一下为什么在 `dequeue_task`, `put_prev_task`, `hrtick`, `scheduler_tick` 时需要调用 `update_curr_rt` 更新进程执行时间。

- dequeue_task：因为当前进程要被调度出去了，后续不知道啥时候才能执行，所以要记录最后一次的执行时间戳，以及该进程执行的时间。下次该进程被选中时，还会在 `set_next_task_rt` 时更新 `curr->se.exec_start`，确保执行时间的统计不会出错；
- put_prev_task：这个函数是用来做负载均衡的，将该进程迁移到其他 CPU 上执行。那么该进程的执行时间就不能再累加到当前 rt_rq 了，所以在迁移之前更新一下执行时间到当前的 rt_rq；
- Hrtick, scheduler_tick：这个毫无疑问，周期性的更新；

**关键函数 sched_rt_runtime_exceeded**

```c
static int sched_rt_runtime_exceeded(struct rt_rq *rt_rq)
{
	u64 runtime = sched_rt_runtime(rt_rq); // rt_rq->rt_runtime 分配给该队列的运行时长

	if (rt_rq->rt_throttled)
		return rt_rq_throttled(rt_rq);

    // rt_rq->rt_runtime >= def_rt_bandwidth.rt_period
    // 可是 rt_runtime 不都是用 rt_period 赋值的么，怎么会大于它呢
    // 应该是在这个周期内，在用户态改变了 period，而 runtime 还没有更新
    // runtime 要等到定时器到期，执行 do_sched_rt_period_timer 回调函数才会更新
	if (runtime >= sched_rt_period(rt_rq))
		return 0;

    // 如果超时了，先尝试从其他 CPU 借一些 cputime 过来
	balance_runtime(rt_rq);
	runtime = sched_rt_runtime(rt_rq);
	if (runtime == RUNTIME_INF)
		return 0;

    // 如果从其他 CPU 借到了，runtime 会增加
    // 借完发现可用的执行时间还是大于已用的执行时间
    // 就会触发 rt throttling
	if (rt_rq->rt_time > runtime) {
		struct rt_bandwidth *rt_b = sched_rt_bandwidth(rt_rq);

		/*
		 * Don't actually throttle groups that have no runtime assigned
		 * but accrue some time due to boosting.
		 */
		if (likely(rt_b->rt_runtime)) {
			rt_rq->rt_throttled = 1; // 这个位置上了，后续 rt_rq 就不会调度了
			static bool once;

			printk_deferred_once("sched: RT throttling activated\n");

			if (!once) {
				// printk RT throttling warning every time
				// once = true;
				dump_throttled_rt_tasks(rt_rq); // 打印 rt_period 等信息
			}
		} else { // rt_runtime 配置为 0 的话，表示不限制 RT 进程运行时间
			rt_rq->rt_time = 0;
		}

		if (rt_rq_throttled(rt_rq)) {
            // rt_rq->rt_queued=0，表示该 rt_rq 没有加入到 rq 队列
            // 自然也就不会调度
            // 那什么时候加回来呢
            // 定时器到期会检查 rt_time 和 rt_runtime，重新调度
			sched_rt_rq_dequeue(rt_rq);
			return 1;
		}
	}

	return 0;
}
```

**关键函数 balance_runtime**

该函数会从所有的 online cpu 上借 runtime。

```c
| balance_runtime
|	-> do_balance_runtime
```

```c
/*
 * We ran out of runtime, see if we can borrow some from our neighbours.
 */
static void do_balance_runtime(struct rt_rq *rt_rq)
{
	struct rt_bandwidth *rt_b = sched_rt_bandwidth(rt_rq);
	struct root_domain *rd = rq_of_rt_rq(rt_rq)->rd;
	int i, weight;
	u64 rt_period;

	weight = cpumask_weight(rd->span);

	raw_spin_lock(&rt_b->rt_runtime_lock);
	rt_period = ktime_to_ns(rt_b->rt_period);
	for_each_cpu(i, rd->span) { // 所有 online cpu
		struct rt_rq *iter = sched_rt_period_rt_rq(rt_b, i);
		s64 diff;

		...
            
		diff = iter->rt_runtime - iter->rt_time; // 该 cpu 还剩多少 rt 可运行的时间
		if (diff > 0) {
			diff = div_u64((u64)diff, weight); // 这些是能借出去的，要还么？
			if (rt_rq->rt_runtime + diff > rt_period)
				diff = rt_period - rt_rq->rt_runtime; // 不用借太多，只要这个周期够用就行
			iter->rt_runtime -= diff;
			rt_rq->rt_runtime += diff; // rt_runtime 变多了
			if (rt_rq->rt_runtime == rt_period) { // 足够了，返回
				raw_spin_unlock(&iter->rt_runtime_lock);
				break;
			}
		}
next:
		raw_spin_unlock(&iter->rt_runtime_lock);
	} // 所以有可能借了一圈也没有借够
	raw_spin_unlock(&rt_b->rt_runtime_lock);
}
```

##### 解除限流状态

如果限制了 RT 进程的执行，那在下一个 rt_period 怎样恢复它的执行呢？

```c
| enqueue_task_rt
|	-> enqueue_rt_entity
|		-> __enqueue_rt_entity
|			-> inc_rt_tasks
|				-> inc_rt_group
|					-> start_rt_bandwidth
|						// 设置一个时间间隔为 0 的定时器，立刻触发回调函数
|						// 触发回调函数只是检查，不一定 period 到期
|						-> do_start_rt_bandwidth
|							-> sched_rt_period_timer
|								-> hrtimer_forward_now
|									-> hrtimer_forward // 没有到期直接返回
|								-> do_sched_rt_period_timer
```

前面分析过，在调度器初始化时，会调用 `init_rt_bandwidth` 设置 rt_period_timer 定时器的回调函数为 `sched_rt_period_timer`。

```c
// 入参为 def_rt_bandwidth
static inline void do_start_rt_bandwidth(struct rt_bandwidth *rt_b)
{
	raw_spin_lock(&rt_b->rt_runtime_lock);
	if (!rt_b->rt_period_active) {
		rt_b->rt_period_active = 1;

        // 定时器立刻到期，执行回调函数 sched_rt_period_timer
		hrtimer_forward_now(&rt_b->rt_period_timer, ns_to_ktime(0));
		hrtimer_start_expires(&rt_b->rt_period_timer,
				      HRTIMER_MODE_ABS_PINNED_HARD);
	}
	raw_spin_unlock(&rt_b->rt_runtime_lock);
}
```

**关键函数 sched_rt_period_timer**

该函数设计的很巧妙。你可以设置一个定时器，到期会执行回调函数；也可以设置到期时间为 0，立刻唤醒它，回调函数会检查 rt_b 是否到期，如果没有到期，就返回。

```c
static enum hrtimer_restart sched_rt_period_timer(struct hrtimer *timer)
{
	struct rt_bandwidth *rt_b =
		container_of(timer, struct rt_bandwidth, rt_period_timer);
	int idle = 0;
	int overrun;

	raw_spin_lock(&rt_b->rt_runtime_lock);
	for (;;) {
        // 重新设置定时器到期时间为 now+period
        // overrun 表示从上一个定时器到期时间到现在，经过了几个 period
        /* last expired               now
              |                        |
         * ---|------|------|------|------|---> time
         *    |-->p<-| p: period
         *    |-------->overrun = 4<------|
         */
		overrun = hrtimer_forward_now(timer, rt_b->rt_period);
		if (!overrun) // 定时器没有到期，不用执行
			break;

        // 定时器到期，处理带宽控制相关逻辑
		raw_spin_unlock(&rt_b->rt_runtime_lock);
		idle = do_sched_rt_period_timer(rt_b, overrun);
		raw_spin_lock(&rt_b->rt_runtime_lock);
	}
    // idle 说明运行队列已经空闲，无需重新启动定时器，清除定时器启动标记
	if (idle)
		rt_b->rt_period_active = 0;
	raw_spin_unlock(&rt_b->rt_runtime_lock);

    // 如果是 idle，则不需要重新开启定时器
    // 不开启定时器，就会出现从上一个定时器到现在，经过了多个 period
	return idle ? HRTIMER_NORESTART : HRTIMER_RESTART;
}
```

```c
| hrtimer_forward_now
|	-> hrtimer_forward
```

```c
u64 hrtimer_forward(struct hrtimer *timer, ktime_t now, ktime_t interval)
{
	u64 orun = 1;
	ktime_t delta;

    // 当前时刻小于到期时间点，说明该定时器还未到期
	delta = ktime_sub(now, hrtimer_get_expires(timer));
	if (delta < 0)
		return 0;

	...
        
	if (unlikely(delta >= interval)) {
		s64 incr = ktime_to_ns(interval);

		orun = ktime_divns(delta, incr); // 这个很简单，整除，向下取整，中间隔了几个 period
		hrtimer_add_expires_ns(timer, incr * orun); // 到期时间也要相应加上
		if (hrtimer_get_expires_tv64(timer) > now)
			return orun;
		/*
		 * This (and the ktime_add() below) is the
		 * correction for exact:
		 */
		orun++;
	}
	hrtimer_add_expires(timer, interval); // 还需要向上取整，不然到期时间还是小于 now

	return orun;
}
```

**关键函数 do_sched_rt_period_timer**

该函数是定时器到期后必做的检查，无论有没有触发 rt throttling。其主要是更新 rt_tq->rt_runtime（可能从用户态改变了） 和 rt_rq->rt_time（每个周期都要更新）。

```c
static int do_sched_rt_period_timer(struct rt_bandwidth *rt_b, int overrun)
{
	int i, idle = 1, throttled = 0;
	const struct cpumask *span;

    // def_rt_bandwidth 是所有 CPU 公用的
    // 所有要处理所有 CPU 的到期情况
	span = sched_rt_period_mask();
	for_each_cpu(i, span) {
		int enqueue = 0;
		struct rt_rq *rt_rq = sched_rt_period_rt_rq(rt_b, i);
		struct rq *rq = rq_of_rt_rq(rt_rq);
		int skip;

		raw_spin_lock(&rt_rq->rt_runtime_lock);
        // 重新配置周期内可执行时间，这个值可以从用户态更改，所以每次都要重新赋值
		if (!sched_feat(RT_RUNTIME_SHARE) && rt_rq->rt_runtime != RUNTIME_INF)
			rt_rq->rt_runtime = rt_b->rt_runtime;
        // 周期内，队列中没有任务运行，无需处理，跳过
		skip = !rt_rq->rt_time && !rt_rq->rt_nr_running;
		raw_spin_unlock(&rt_rq->rt_runtime_lock);
		if (skip)
			continue;

		raw_spin_lock(&rq->lock);
		update_rq_clock(rq);

		if (rt_rq->rt_time) { // 有任务运行
			u64 runtime;

			raw_spin_lock(&rt_rq->rt_runtime_lock);
			if (rt_rq->rt_throttled) // 该队列被限流了
				balance_runtime(rt_rq); // 需要从其他 cpu 借些 runtime
			runtime = rt_rq-> rt_runtime;
            // 这个 overrun*runtime 是啥意思？
            // 表示从上个到期时间到现在经过了几个 period，共有 overrun*runtime 的配置执行时间
            // 如果 rt_rq->rt_time 更大，说明配额运行时间还不够，rt_time 还不为零，这时记录超过的时间
            // 如果过 overrun*runtime 更大，说明额定时间是够的，rt_time 就归零了
			rt_rq->rt_time -= min(rt_rq->rt_time, overrun*runtime);
			if (rt_rq->rt_throttled && rt_rq->rt_time < runtime) {
				rt_rq->rt_throttled = 0; // 这样才能重新调度
				enqueue = 1;

				/*
				 * When we're idle and a woken (rt) task is
				 * throttled check_preempt_curr() will set
				 * skip_update and the time between the wakeup
				 * and this unthrottle will get accounted as
				 * 'runtime'.
				 */
				if (rt_rq->rt_nr_running && rq->curr == rq->idle)
					rq_clock_cancel_skipupdate(rq);
			}
            // rt_time 不为 0，说明实际运行时间要大于额定时间，还有任务要运行，需要重启定时器
			if (rt_rq->rt_time || rt_rq->rt_nr_running)
				idle = 0;
			raw_spin_unlock(&rt_rq->rt_runtime_lock);
		} else if (rt_rq->rt_nr_running) { // 队列中有任务要运行，但是检测周期内没有被调度运行
			idle = 0; // 需要重启定时器
			if (!rt_rq_throttled(rt_rq))
				enqueue = 1;
		}
		if (rt_rq->rt_throttled)
			throttled = 1;

		if (enqueue) // rt_rq->rt_queued = 1，现在可以调度了
			sched_rt_rq_enqueue(rt_rq);
		raw_spin_unlock(&rq->lock);
	}

	if (!throttled && (!rt_bandwidth_enabled() || rt_b->rt_runtime == RUNTIME_INF))
		return 1;

	return idle;
}
```

##### 总结

系统中有一个公共的变量 `struct rt_bandwidth *def_rt_bandwidth`，其中的  rt_period 变量表示统计周期，rt_runtime 变量表示在 period 内 rt 进程可以执行的时间。以及一个定时器，该定时器在 period 到期时会调用回调函数 `do_sched_rt_period_timer` 检查当前系统中 rt 进程的运行时间，根据情况让被 throttling 的 rt_rq 重新加入 rq 被调度。

而有两个地方会重新设置定时器到期的时间，覆盖了所有 rt 进程进/出队列的操作：

- `update_curr_rt`：检查是否达到限流条件，开启 throttling；
- `enqueue_task_rt`：新的任务进来了，检查一下是否处于 throttling 状态，如果处于，赶紧触发定时器，看能不能重新调度；

#### 负载均衡

RT 也要做负载均衡么？RT 调度器负载均衡的目标是什么？**让高优先级进程能够先执行。**

##### 相关数据结构

**root_domain**

实时调度器需要几个全局的或者说系统范围的资源来作出调度决定，以及 CPU 数量的增加而出现的可伸缩性瓶颈（由于锁保护的这些资源的竞争），Root Domain 引入的目的就是为了减少这样的竞争以改善可伸缩性。

```c
struct root_domain {
	atomic_t		refcount; // root domain 的引用计数，当 rd 被运行队列引用时加 1，反之减 1
	atomic_t		rto_count; // 实时任务过载的(rt overload)的 CPU 的数目
	struct rcu_head		rcu;
	cpumask_var_t		span; // 属于该 rd 的 CPU 掩码，即有哪些 CPU 使用了这个 root_domain
	cpumask_var_t		online;

	/*
	 * Indicate pullable load on at least one CPU, e.g:
	 * - More than one runnable task
	 * - Running task is misfit
	 */
	int			overload; // 表明该 rd 有任一 CPU 有多于一个的可运行任务

	/* Indicate one or more cpus over-utilized (tipping point) */
	int			overutilized;

	...

	cpumask_var_t		rto_mask; // 在哪里设置的
	struct cpupri		cpupri; // 后面介绍

	unsigned long		max_cpu_capacity;

	/*
	 * NULL-terminated list of performance domains intersecting with the
	 * CPUs of the rd. Protected by RCU.
	 */
	struct perf_domain __rcu *pd;
};
```

**cpupri**

CPU 优先级管理（CPU Priority Management）跟踪系统中每个 CPU 当前执行进程的优先级，可以让进程迁移的决定更有效率。

```c
struct cpupri_vec {
	atomic_t		count;
	cpumask_var_t		mask;
};

struct cpupri {
    // 这个结构体数组为，idle, normal, rt(99) 准备了一一对应的 102 个元素
    // 0->101 对应的优先级依次降低
    // 表示这个优先级当前有几个 CPU 在运行，以及对应的 cpu mask
	struct cpupri_vec	pri_to_cpu[CPUPRI_NR_PRIORITIES];
    // 每个 CPU 的状态 CPUPRI_INVALID （-1）, CPUPRI_IDLE（0），CPUPRI_NORMAL（1）
    // RT0, ... RT99，共 102 种状态，也即每个 CPU 当前运行的优先级
	int			*cpu_to_pri; // 为每个 CPU 都分配了一个变量
};
```

pri_to_cpu 与进程优先级的关系通过图来表示更清晰，

![cpupri](D:\gitlab\UtopianFuture.github.io\image\cpupri.svg)

##### 初始化

在 `start_kernel` 阶段会初始化一个全局的 def_root_domain，并赋值给每个 rt->rd。

```c
| start_kernel
|	-> sched_init
|		-> init_rt_bandwidth // 前面介绍的带宽限制相关
|		-> init_defrootdomain // 主要就是申请内存，设置引用计数
|		-> rq_attach_root // 将 def_root_domain 赋值到每个 rq，同时将 refcount 加 1
```

```c
void init_defrootdomain(void)
{
	init_rootdomain(&def_root_domain);

	atomic_set(&def_root_domain.refcount, 1);
}

static int init_rootdomain(struct root_domain *rd)
{
	...

	init_dl_bw(&rd->dl_bw);
	if (cpudl_init(&rd->cpudl) != 0)
		goto free_rto_mask;

	if (cpupri_init(&rd->cpupri) != 0)
		goto free_cpudl;
	return 0;
}

int cpupri_init(struct cpupri *cp)
{
	int i;

	for (i = 0; i < CPUPRI_NR_PRIORITIES; i++) {
		struct cpupri_vec *vec = &cp->pri_to_cpu[i];

		atomic_set(&vec->count, 0);
		if (!zalloc_cpumask_var(&vec->mask, GFP_KERNEL))
			goto cleanup;
	}

	cp->cpu_to_pri = kcalloc(nr_cpu_ids, sizeof(int), GFP_KERNEL);
	if (!cp->cpu_to_pri)
		goto cleanup;

	for_each_possible_cpu(i)
		cp->cpu_to_pri[i] = CPUPRI_INVALID;

	return 0;
}
```

##### cpupri 维护

下面一系列的操作，就是为了维护 `struct cpupri` 结构体，方面后续 `cpupri_find` 快速找到这样一个 CPU，即它运行的进程优先级最低，然后又能够运行该优先级的进程，这样 push 过去，就能快速抢占执行。

```c
| enqueue_task_rt
|	-> enqueue_rt_entity
|		-> dequeue_rt_stack
|			-> dequeue_top_rt_rq // 将 rt_rq->rt_queued 置为 0
|			-> __de queue_rt_entity // rt_se->rt = 0
|				-> dec_rt_tasks
|					-> dec_rt_prio
|						-> dec_rt_prio_smp
|							-> cpupri_set
|		-> __enqueue_rt_entity
|			-> inc_rt_tasks
|				-> inc_rt_prio
|					-> inc_rt_prio_smp
|						-> cpupri_set
    
| dequeue_task_rt
|	-> dequeue_rt_entity
|		-> dequeue_rt_stack
|		-> __enqueue_rt_entity // 后续同上
```

```c
// prio 为新入队的进程优先级
// prev_prio 为该进程入队前的 rt_rq->highest_prio.curr
static void inc_rt_prio_smp(struct rt_rq *rt_rq, int prio, int prev_prio)
{
	struct rq *rq = rq_of_rt_rq(rt_rq);

	...
        
    // 要该 rq 是可调度的，然后新入队的进程优先级比当前 rt->rq->highest_prio.curr 高
    // 才能调整
	if (rq->online && prio < prev_prio)
		cpupri_set(&rq->rd->cpupri, rq->cpu, prio);
}

// prio 为新入队的进程优先级
// prev_prio 为当前的 rt_rq->highest_prio.curr
static void dec_rt_prio_smp(struct rt_rq *rt_rq, int prio, int prev_prio)
{
	struct rq *rq = rq_of_rt_rq(rt_rq);

	...
        
    // 同样，该 rq 是可调度的，然后 rt_rq->highest_prio.curr 在 de 期间发生改变
    // 这里还没有完全搞懂
	if (rq->online && rt_rq->highest_prio.curr != prev_prio)
		cpupri_set(&rq->rd->cpupri, rq->cpu, rt_rq->highest_prio.curr);
}
```

总结起来，就是当前 CPU 运行的进程（rt_rq->highest_prio.curr）发生改变，就需要同步维护 cpupri。

```c
void cpupri_set(struct cpupri *cp, int cpu, int newpri)
{
    // 记录进程入队/出队之前 rt_rq->highest_prio.curr 的值
	int *currpri = &cp->cpu_to_pri[cpu];
	int oldpri = *currpri;
	int do_mb = 0;

    // 将普通进程的 nice 值转化成 CPUPRI_NORMAL，1
    // 将 idle 进程转换成 CPUPRI_IDLE，0
    // rt 进程反转一下，排布在 2-101 素中
	newpri = convert_prio(newpri);

	BUG_ON(newpri >= CPUPRI_NR_PRIORITIES);

    // 好理解
	if (newpri == oldpri)
		return;

    // 走到这里说明 rt_rq->highest_prio.curr 发生了变化
    // 需要在这里更新
    // 在新的 rt_rq->highest_prio.curr 对应的 cpupri_vec 标记上该 CPU 也能执行该优先级的进程
    // 然后旧的 rt_rq->highest_prio.curr 对应的 cpupri_vec 标记上该 CPU 无法执行该优先级的进程
	if (likely(newpri != CPUPRI_INVALID)) {
		struct cpupri_vec *vec = &cp->pri_to_cpu[newpri];

        // 这样看来，cpupri_vec 这个结构体的作用就是记录
        // 哪些 CPU 能够执行某一优先级的进程
		cpumask_set_cpu(cpu, vec->mask);

		smp_mb__before_atomic(); // 看来社区在这里吃过亏，上这么多内存屏障
		atomic_inc(&(vec)->count); // 记录该优先级上进程个数
		do_mb = 1;
	}
    
	if (likely(oldpri != CPUPRI_INVALID)) {
		struct cpupri_vec *vec  = &cp->pri_to_cpu[oldpri];

		
		if (do_mb)
			smp_mb__after_atomic();

		atomic_dec(&(vec)->count);
		smp_mb__after_atomic();
        // 该 CPU 不再能够执行这一优先级的进程了
		cpumask_clear_cpu(cpu, vec->mask);
	}
    
	// cp->cpu_to_pri 在初始化时每个 CPU 都是设置为 CPUPRI_INVALID
    // 在这里设置为 rt_rq->highest_prio.curr 转换之后的值
    // 即当前 CPU 运行进程的优先级（都是运行最高优先级的进程）
	*currpri = newpri;
}
```

在入队/出队时维护好 cpupri 后，就可以在负载均衡是通过 `push_rt_task` 与 `pull_rt_task` 函数快速找到哪个 CPU 负载低，保证高优先级进程先执行。

- `push_rt_task` 会在以下时间点被调用：

  - 直接调用：`task_woken_rt` （该函数的作用是从传入的 rq 中推一些任务到其他 rq）中调用 `push_rt_tasks`；


  - 间接调用：进程调度类发生改变时（比如通过 `sched_setscheduler` 系统调用改变调度类，通过 `rt_mutex_setprio` 进行优先级继承）；


  - 间接调用：发生调度时，在 `context_switch` 中（之前分析 `context_switch` 时从没关注到这点）会调用（这时候可能有一个实时进程被更高优先级的实时进程抢占了）；


- `pull_rt_task` 会在以下时间点被调用：

  - 直接调用：`pick_next_task_rt` 选择下一个要调度的实时进程之前，先 balance 一下，当前运行队列的最高优先级比 prev 任务的优先级低，这个时候 pull 一下；


  - 间接调用：进程调度类发生改变时；


  - 间接调用：进程优先级发生变化时；


我们来看看整体的执行流程，

![rt_root_domain](D:\gitlab\UtopianFuture.github.io\image\rt_root_domain.svg)

##### push 推任务[^10]

push 推任务迁移是指**一个 CPU 将本运行队列上最高优先级（不在运行）的实时任务，转移到另一个运行队列的操作**。执行过程如下：

```c
| push_rt_tasks
|	-> push_rt_task
|		-> pick_next_pushable_task
|		-> find_lock_lowest_rq
|			-> find_lowest_rq
|				-> cpupri_find
|				-> cpumask_test_cpu // 通过 bit and 的方式快速找到对应 cpu
|		-> deactivate_task
|		-> set_task_cpu
|		-> activate_task
|		-> resched_curr
```

**关键函数 push_rt_tasks**

```c
static void push_rt_tasks(struct rq *rq)
{
	/* push_rt_task will return true if it moved an RT */
	while (push_rt_task(rq, false))
		;
}

/*
 * If the current CPU has more than one RT task, see if the non
 * running task can migrate over to a CPU that is running a task
 * of lesser priority.
 */
static int push_rt_task(struct rq *rq, bool pull) // 选择一个优先级更低，能够运行该 task 的 CPU
{
	struct task_struct *next_task;
	struct rq *lowest_rq;
	int ret = 0;

    // 如果该队列没有过载，那么没有 rt task 可以被推走
	if (!rq->rt.overloaded)
		return 0;

    // 首先在该队列的可推链表里找有没有合适的任务，如果没有则返回
    // 之前任务入列的时候会通过 enqueue_pushable_task 把可能的任务加入 rt.pushable_tasks 链表
    // pushable_tasks 是按照优先级由高到底进行排序的，所以 first entry 是链表里优先级最高的
	next_task = pick_next_pushable_task(rq);
	if (!next_task)
		return 0;

	...

	/*
	 * It's possible that the next_task slipped in of
	 * higher priority than current. If that's the case
	 * just reschedule current.
	 */
	if (unlikely(next_task->prio < rq->curr->prio)) {
		resched_curr(rq);
		return 0;
	}

	/* We might release rq lock */
	get_task_struct(next_task);

	/* find_lock_lowest_rq locks the rq if found */
	lowest_rq = find_lock_lowest_rq(next_task, rq);
	if (!lowest_rq) {
        ...
	}

    // 这些就很好理解了
    // 将选中的 task 从当前 cpu 的 rq 中 dequeue
    // 设置 p->cpu = lowest_rq->cpu
    // 将选中的 task enqueue 到 lowest_rq 中
    // 设置 lowest_rq 为 need_resched
	deactivate_task(rq, next_task, 0);
	set_task_cpu(next_task, lowest_rq->cpu);
	activate_task(lowest_rq, next_task, 0);
	resched_curr(lowest_rq);
	ret = 1;

	return ret;
}
```

**关键函数 find_lock_lowest_rq**

该函数调用最多调用 `find_lowest_rq` 三次，并确定找到的 `lowest_rq` 是能用的。

```c
/* Will lock the rq it finds */
static struct rq *find_lock_lowest_rq(struct task_struct *task, struct rq *rq)
{
	struct rq *lowest_rq = NULL;
	int tries;
	int cpu;

    // 尝试 find_lowest_rq 3 次
	for (tries = 0; tries < RT_MAX_TRIES; tries++) {
		cpu = find_lowest_rq(task);

		if ((cpu == -1) || (cpu == rq->cpu))
			break;

		lowest_rq = cpu_rq(cpu);

        // 找到的 CPU rt_rq 中的最高优先级进程比需要 push 的高
        // 那肯定没有必要 push，抢不过人家
		if (lowest_rq->rt.highest_prio.curr <= task->prio) {
			lowest_rq = NULL;
			break;
		}

		if (double_lock_balance(rq, lowest_rq)) {
			
            ...
                
		}

		/* If this rq is still suitable use it. */
		if (lowest_rq->rt.highest_prio.curr > task->prio)
			break;

		/* try again */
		double_unlock_balance(rq, lowest_rq);
		lowest_rq = NULL;
	}

	return lowest_rq;
}
```

**关键函数 find_lowest_rq**

```c
static int find_lowest_rq(struct task_struct *task)
{
	struct sched_domain *sd;
    // local_cpu_mask 是一个 per cpu 变量
    // sched_init_smp->init_sched_rt_class 时初始化
    // 其实就是 cpu_possible_mask
	struct cpumask *lowest_mask = this_cpu_cpumask_var_ptr(local_cpu_mask);
	int this_cpu = smp_processor_id();
	int cpu      = task_cpu(task);
	int ret;

	...
        
	/*
	 * If we're on asym system ensure we consider the different capacities
	 * of the CPUs when searching for the lowest_mask.
	 */
	if (static_branch_unlikely(&sched_asym_cpucapacity)) { // asym system 是指什么？
		ret = cpupri_find_fitness(&task_rq(task)->rd->cpupri, // 5.10 版本的内核不走这里
					  task, lowest_mask,
					  rt_task_fits_capacity);
	} else {
        // cpumask_and(lowest_mask, &p->cpus_mask, vec->mask);
        // 通过对 p->cpus_mask 即该 task 可以运行的 cpu 和 rq->rd->cpupri 取交集
        // 得到负载最低的 cpu 集合 lowest_mask
		ret = cpupri_find(&task_rq(task)->rd->cpupri,
				  task, lowest_mask);
	}

    if (!ret) // 遍历所有的优先级，都没有
		return -1; /* No targets found */
    
	...

    // 这里返回的是该 CPU 能够运行在哪些 CPU 上，以及哪些 CPU 能够运行该优先级的进程的交集
    // 首先选择该 task 上次运行过的 cpu，因为该 cpu 可能是 cache-hit
    // 这种直接通过 bit 位的方式确实很方便
	if (cpumask_test_cpu(cpu, lowest_mask))
		return cpu;

	/*
	 * Otherwise, we consult the sched_domains span maps to figure
	 * out which CPU is logically closest to our hot cache data.
	 */
    // 因为任务会反复入队/出队，set_task_cpu
    // 所以 p->cpu（执行该任务的 cpu）和 smp_processor_id 往往不一样
	if (!cpumask_test_cpu(this_cpu, lowest_mask))
		this_cpu = -1; /* Skip this_cpu opt if not among lowest */
	
    ...

	/*
	 * And finally, if there were no matches within the domains
	 * just give the caller *something* to work with from the compatible
	 * locations.
	 */
	if (this_cpu != -1)
		return this_cpu;

	cpu = cpumask_any_distribute(lowest_mask);
	if (cpu < nr_cpu_ids)
		return cpu;

	return -1;
}
```

**关键函数 cpupri_find**

`cpupri_find` 本质也是调用 `cpupri_find_fitness`，只是 `fintness_fn` 为 NULL。

```c
int cpupri_find_fitness(struct cpupri *cp, struct task_struct *p,
		struct cpumask *lowest_mask,
		bool (*fitness_fn)(struct task_struct *p, int cpu))
{
	int task_pri = convert_prio(p->prio);
	int idx, cpu;

	BUG_ON(task_pri >= CPUPRI_NR_PRIORITIES);

    // 从优先级低的元素开始查找
    // 进程优先级经过转换，cp->pri_to_cpu 中从 0~101 元素表示的进程优先级是从小到大的
    // 从 0 开始找，
	for (idx = 0; idx < task_pri; idx++) {

		if (!__cpupri_find(cp, p, lowest_mask, idx))
			continue;

        // 找到一个就行
		if (!lowest_mask || !fitness_fn)
			return 1;

		/* Ensure the capacity of the CPUs fit the task */
		for_each_cpu(cpu, lowest_mask) {
			if (!fitness_fn(p, cpu))
				cpumask_clear_cpu(cpu, lowest_mask);
		}

		/*
		 * If no CPU at the current priority can fit the task
		 * continue looking
		 */
		if (cpumask_empty(lowest_mask))
			continue;

		return 1;
	}

	...

	return 0;
}
```

**关键函数 __cpupri_find**

```c
static inline int __cpupri_find(struct cpupri *cp, struct task_struct *p,
				struct cpumask *lowest_mask, int idx)
{
	struct cpupri_vec *vec  = &cp->pri_to_cpu[idx];
	int skip = 0;

	if (!atomic_read(&(vec)->count)) // 该优先级上没有进程
		skip = 1;
	
	smp_rmb();

	/* Need to do the rmb for every iteration */
	if (skip)
		return 0;

    // 该进程能运行在所有 CPU 上，所有 CPU 都能运行该优先级的进程，就跳过该优先级
	if (cpumask_any_and(&p->cpus_mask, vec->mask) >= nr_cpu_ids)
		return 0;

    // 该进程能够运行在哪些 CPU 上，哪些 CPU 能够运行该优先级进程
    // 取个交集，就得出可以 push 到哪些 CPU 上
	if (lowest_mask) {
		cpumask_and(lowest_mask, &p->cpus_mask, vec->mask);

		...
	}

	return 1;
}
```

##### pull 拉任务[^10]

`pull_rt_task` 算法着眼于一个 root domain 中**所有过载的运行队列**，检查它们是否有一个实时任务能运行在本 CPU 的 rq（本 CPU 在任务的`task->cpus_allowed_mask` 中），**且其优先级高于本 CPU 上将要被调度的任务**。

扫描 root domain 中所有过载的运行队列之后终结。因此，pull 拉操作可能拉多于一个任务到本 CPU 的运行队列。

执行过程如下：

```c
| pull_rt_task
|	-> pick_highest_pushable_task
|	-> deactivate_task
|	-> set_task_cpu
|	-> activate_task
|	-> resched_curr
```

**关键函数 pull_rt_task**

```c
static void pull_rt_task(struct rq *this_rq)
{
	int this_cpu = this_rq->cpu, cpu;
	bool resched = false;
	struct task_struct *p, *push_task;
	struct rq *src_rq;
	int rt_overload_count = rt_overloaded(this_rq);

	...

    // rto_mask 是当该 CPU 上有一个以上的 runnable task，就会置上对应的 bit
    // 这里相当于遍历所有 overloaded cpu
    // 所以可能会从每个 CPU 上 pull 一个进程过来
	for_each_cpu(cpu, this_rq->rd->rto_mask) {
		if (this_cpu == cpu)
			continue;

		src_rq = cpu_rq(cpu);

        // 遍历到的 rq 中次高优先级进程比该 rq 当前执行的进程优先级还低
        // pull 过来也不会快速执行，没必要
		if (src_rq->rt.highest_prio.next >=
		    this_rq->rt.highest_prio.curr)
			continue;

		push_task = NULL;
		double_lock_balance(this_rq, src_rq);

        // 遍历 src_rq->rt.pushable_tasks
        // 看遍历到的 task 是否在运行，以及能够在 this_cpu 上运行
        // 找到一个就返回
		p = pick_highest_pushable_task(src_rq, this_cpu);

		/*
		 * Do we have an RT task that preempts
		 * the to-be-scheduled task?
		 */
		if (p && (p->prio < this_rq->rt.highest_prio.curr)) {
			WARN_ON(p == src_rq->curr);
			WARN_ON(!task_on_rq_queued(p));

			if (p->prio < src_rq->curr->prio)
				goto skip;

			if (is_migration_disabled(p)) {
				trace_sched_migrate_pull_tp(p);
				push_task = get_push_task(src_rq);
			} else {
                // 这个和 push 一样，找到能 pull 的 task
                // 执行出队 -> set_task_cpu -> 入队的操作
				deactivate_task(src_rq, p, 0);
				set_task_cpu(p, this_cpu);
				activate_task(this_rq, p, 0);
				resched = true;
			}
		}

        ...

	if (resched)
		resched_curr(this_rq);
}
```

##### 总结

- RT 调度器 push 的时候为一个进程 p 选核的逻辑就是：找比 p 优先级低的那些 cpu（cpu_to_pri[cpu] 要比 p 优先级低），越低越好，所以从 CPUPRI_IDLE（对应无 RT）开始；有了 cpupri，选核逻辑就很高效；

- RT load balance 的核心逻辑：高优先级 RT 优先运行；

  比如 cpu0 rt_rq 中有三个进程 p1、p2、p3，优先级分别为 0，20 和 30；cpu1 rt_rq 中有两个进程 p4、p5，优先级分别为 1 和 10。当 cpu0 上优先级为 0 的进程 p1 dequeue 时，会执行 `cpupri_set`，更新当前 CPU 执行的进程。然后在触发调度时会执行 push/pull 操作，就会将 cpu1 上优先级为 10 的进程p5拉过来执行，而不是执行自己 rt_rq 上优先级为 20 的进程 p2，这样就能让优先级为 10 的 p5 优先于优先级为 20 的 p2 执行。

### PREEMPT_RT

通过 Preempt_RT 来实现 Linux 实时性的关键点是减少内核中非抢占性（non-preemptible）的代码量，且要尽量减少对实际的代码的修改量。为了减少内核中非抢占性（non-preemptible）的代码量，需要实现对临界区（critical secitons），中断处理例程（interrupt handlers），中断屏蔽代码段（interrupt-disable code sequences）的可抢占性。为了减少对实际的代码的修改量，Preempt_RT patches 充分重用了 kernel 对 SMP 的支持能力，从而避免了对 kernel 的大量重写。

### 负载计算

这章的内容是探究如何更好的描述一个调度实体和就绪队列的工作负载。大部分内容都是从[这里](#http://www.wowotech.net/process_management/450.html)复制过来的，它讲的很清楚，我就省的再敲一遍。然后对于计算公式我能够理解，内核代码也贴出来了，可以说对于 PELT 算法我知道怎样算工作负载，但对于其要怎样使用还不懂，比如  `sched_avg` 中的 `load_avg`, `runnable_avg`, `util_avg` 变量我就不知道怎么用。这里就当个记录吧，之后有需要再深入理解。

#### 量化负载的计算

内核中使用量化负载来衡量 CPU、进程、就绪队列的负载。量化负载说白了就是运行时间除以总时间乘上权重，那么以此可以计算：

- CPU 的负载 = 运行时间 / 总时间 * 就绪队列的总权重

- decay_avg_load = decay_sum_runable_time / decay_sum_period_time * weight

  decay_avg_load 就是量化负载；decay_sum_runable_time 是就绪队列或调度实体在可运行状态下的所有历史累计衰减时间，这个历史衰减时间我认为就是运行时间（好吧，不是这么简单，见下面的分析）；decay_sum_period_time 是就绪队列或调度实体在所有采样周期里累加衰减时间；weight 是调度实体或就绪队列的权重。

当一个进程 decay_sum_runable_time 无限接近 decay_sum_period_time 时，它的量化负载就无限接近权重，这说明这个进程一直在占用 CPU。我们将 CPU 对应的就绪队列中所有进程的量化负载累加起来就得到 CPU 的总负载。

#### 实际算力的计算

处理器有一个计算能力的概念，也就是这个处理器最大的处理能力。在 SMP 架构下，系统中所有处理器的计算能力是一样的，但在 ARM 架构中使用了大小核设计，处理器的处理能力就不一样了。内核同样使用量化计算能力来描述处理器的计算能力，若系统中功能最强大的 CPU 的量化计算能力设置为 1024，那么系统中功能稍弱的 CPU 的量化计算能力就要小于 1024。通常 CPU 的量化计算能力通过设备树或  BIOS 传给内核。[rq](#rq) 中有成员变量 `cpu_capacity_orig` 和 `cpu_capacity` 来描述 CPU 的算力。

CPU 的最大量化计算能力称为额定算力，而一个进程或就绪队列当前计算能力称为实际算力，其计算公式为：

- util_avg = decay_sum_runable_time / decay_sum_period_time * cpu_capacity

  util_avg 可以理解为额定算力的利用率或 CPU 利用率。

  从公式中就可以看出，如果一个调度实体的执行时间越接近采样时间，它的实际算力就越接近额定算力，也可以理解为它的 CPU 利用率更高。

内核中的绿色节能调度器会使用实际算力来进行进程的迁移调度。

#### PELT 算法

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

该数据结构用来描述[调度实体](#sched_entity)或[就绪队列](#cfs_rq)的负载信息。

```c
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

### SMP 负载均衡[^9]

> 负载均衡的复杂性主要和复杂的系统拓扑有关。由于当前 CPU 很忙，我们把之前运行在该 CPU 上的一个任务迁移到新的 CPU 上的时候，如果迁移到新的 CPU 是和原来的 CPU 在不同的 cluster 中，性能会受影响（因为会 cache 会变冷）。但是对于超线程架构，cpu 共享 cache，这时候超线程之间的任务迁移将不会有特别明显的性能影响。NUMA 上任务迁移的影响又不同，我们应该尽量避免不同 NUMA node 之间的任务迁移，除非 NUMA node 之间的均衡达到非常严重的程度。总之，一个好的负载均衡算法必须适配各种 cpu 拓扑结构。为了解决这些问题，linux 内核引入了 sched_domain 的概念。
>

#### 相关数据结构

##### sched_domain

```c
struct sched_domain {
	/* These fields must be setup */
	struct sched_domain __rcu *parent;	/* top domain must be null terminated */
	struct sched_domain __rcu *child;	/* bottom domain must be null terminated */
    // 一个调度域中有若干个调度组，这些调度组形成一个环形链表，groups 成员就是链表头
	struct sched_group *groups;	/* the balancing groups of the domain */
	unsigned long min_interval;	/* Minimum balance interval ms */
	unsigned long max_interval;	/* Maximum balance interval ms */
    // 正常情况下，balance_interval 定义了均衡的时间间隔
    // 如果 cpu 繁忙，那么均衡要时间间隔长一些
    // 即时间间隔定义为 busy_factor x balance_interval
	unsigned int busy_factor;	/* less balancing by factor if busy */
    // 调度域内的不均衡状态达到了一定的程度之后就开始进行负载均衡的操作
    // imbalance_pct 这个成员定义了判定不均衡的门限
	unsigned int imbalance_pct;	/* No balance until over watermark */
	unsigned int cache_nice_tries;	/* Leave cache hot tasks for # tries */

	int nohz_idle;			/* NOHZ IDLE status */
	int flags;			/* See SD_* */
	int level;

	/* Runtime fields. */
    // 上次进行 balance 的时间点
	unsigned long last_balance;	/* init to jiffies. units in jiffies */
	unsigned int balance_interval;	/* initialise to 1. units in ms. */
	unsigned int nr_balance_failed; /* initialise to 0 */

	/* idle_balance() stats */
	u64 max_newidle_lb_cost;
	unsigned long next_decay_max_lb_cost;

	u64 avg_scan_cost;		/* select_idle_sibling */

	...

#ifdef CONFIG_SCHED_DEBUG
	char *name;
#endif
	union {
		void *private;		/* used during construction */
		struct rcu_head rcu;	/* used during destruction */
	};
	struct sched_domain_shared *shared;

	unsigned int span_weight;
	/*
	 * Span of all CPUs in this domain.
	 *
	 * NOTE: this field is variable length. (Allocated dynamically
	 * by attaching extra space to the end of the structure,
	 * depending on how many CPUs the kernel has booted up with)
	 */
	unsigned long span[];
};
```

##### sched_group

```c
struct sched_group {
	struct sched_group	*next;			/* Must be a circular list */
	atomic_t		ref;

	unsigned int		group_weight;
	struct sched_group_capacity *sgc;
	int			asym_prefer_cpu;	/* CPU of highest priority in group */

	/*
	 * The CPUs this group covers.
	 *
	 * NOTE: this field is variable length. (Allocated dynamically
	 * by attaching extra space to the end of the structure,
	 * depending on how many CPUs the kernel has booted up with)
	 */
	unsigned long		cpumask[];
};
```

#### 总结

负载均衡和公司组织何其相像。公司组织也是分层的，为了避免一个部门工作，剩余部门摸鱼的现象，需要根据每个团队的能力设置工作目标。先在小范围调整工作量，因为调整范围越大，代价也就越大，然后逐步达到全公司范围内的工作量平衡。

### 绿色节能调度器

### 创建并运行新进程

最后我们分析一下系统是怎样创建一个新进程并进行进程切换然后运行的。

![create_and_run](D:\gitlab\UtopianFuture.github.io\image\create_and_run.svg)

其中具体流程如下：

- shell 程序会调用系统调用 fork() 来创建一个新的进程
- 使用 do_fork() 创建新进程
  - 创建新进程的 `task_struct` 数据结构
  - 复制父进程的 `task_struct` 数据结构到新进程
  - 复制父进程的相关页表到新进程
  - 设置新进程的内核栈
- 父进程调用 `wake_up_new_task` 尝试唤醒新进程
  - 调用调度类的 `select_task_rq`，为新进程寻找一个负载最轻的 CPU，这里是 CPU1
  - 调度类的 `enqueue_task` 把新进程添加到该 CPU 的就绪队列中
- CPU1 选择合适的进程运行
  - 每次时间片耗尽或在**阻塞操作**中，如使用互斥量（mutex）、信号量（semaphore）、等待队列（waitqueue）等，以及在**中断返回前和系统调用返回用户空间时**会执行 `schedule` 函数来检查是否需要重新调度。`schedule` 会调用 `pick_next_task` 来选择下一个最合适的进程
  - `switch_mm` 切换父进程和新进程的页表
  - `switch_to` 切换新进程
- 运行新进程
  - 新进程第一次运行时会调用 `ret_from_fork`，其他情况是从上次中断位置的下一条指令开始运行。
  - 返回用户空间运行 shell 程序
  - shell 程序调用 exec() 系统调用来运行 test 程序，最后新进程变成了 test 进程

### 疑问

- 为什么要设置优先级、nice 值、权重、实际运行时间（runtime）、虚拟运行时间（vruntime）？它们之间的关系是什么？

  我们来看一个函数，

  ```c
  static void set_load_weight(struct task_struct *p, bool update_load)
  {
  	int prio = p->static_prio - MAX_RT_PRIO; // MAX_RT_PRIO = 100
  	struct load_weight *load = &p->se.load;
  
  	/*
  	 * SCHED_IDLE tasks get minimal weight:
  	 */
  	if (task_has_idle_policy(p)) {
  		load->weight = scale_load(WEIGHT_IDLEPRIO);
  		load->inv_weight = WMULT_IDLEPRIO;
  		return;
  	}
  
  	/*
  	 * SCHED_OTHER tasks have to update their load when changing their
  	 * weight
  	 */
  	if (update_load && p->sched_class == &fair_sched_class) {
  		reweight_task(p, prio);
  	} else {
  		load->weight = scale_load(sched_prio_to_weight[prio]);
  		load->inv_weight = sched_prio_to_wmult[prio];
  	}
  }
  ```

  从这个函数中我们可以清晰的确定 nice 其实就是优先级，而每个优先级对应一个权重，这个对应关系只有普通进程，即优先级 100 ~ 139 的进程才有。也就是说实时进程没有权重这一说法，直接按照优先级顺序执行么？是的。

  在 `task_struct` 中有一个和优先级相关的变量，

  ```c
  struct task_struct {
      ...
  
  	// 进程的动态优先级。这是调度类考虑的优先级，即 nice 值，
      // 它是 static_prio - MAX_RT_PRIO(100)得到的，范围是 100 ~ 139
  	int				prio;
  	int				static_prio; // 静态优先级，在进程启动时分配。它的范围是 0 ~ 139
  	int				normal_prio; // 基于 static_prio 和调度策略计算出来的优先级，子进程初始化时继承该优先级
  	unsigned int	rt_priority; // 实时进程的优先级
  
      ...
  
  }
  ```

  而 `vruntime` 则是根据 `runtime` 和进程权重计算出来的，进程的权重又由 nice（优先级）确定。

- 为什么要根据 vruntime 决定调度顺序？

- 在[关键函数 context_switch](#关键函数context_switch) 中使用 last 参数来达到在 next 进程中能够处理 prev 进程的遗留问题，但是，last 是一个指针，在进程页表都切换了的情况下，prev 能正确指向 prev 的 `task_struct` 么？

- `thread_struct` 数据结构保存进程在上下文切换时的硬件上下文，但怎么和我想象的内容不太一样，很多寄存器没有保存，只保存了 sp 和一些段寄存器，在 X86 中其他的寄存器值都保存在栈里么？还是说就只需要保存这些？

  其实这些信息的保存在[关键函数 copy_thread](#关键函数copy_thread) 中已经保存了。

- [关键函数 copy_thread](#关键函数copy_thread) 进程创建过程还涉及到很多模块的初始化不懂，之后需要不断深入理解。

- 系统中是怎样做到 CFS 和 RR 调度类共存的？

- 这个领域，最常遇到什么问题？

- 正常对 RT 调度类的理解是：如果某个高优先级进程的调度方式为 RR，那么在执行完时间周期后，会调用 `resched_curr` 重新调度，那么下一次就不应该再执行这个进程了。如果 __schedule 还选择该进程执行，那么 RR 就没有意义了，但是我在代码中没有找到能够选择其他进程的逻辑。

### Reference

[1^]: 奔跑吧 Linux 内核，卷 1：基础架构

[2^]: 内核版本：5.15-rc5，commitid: f6274b06e326d8471cdfb52595f989a90f5e888f

[3^]: https://blog.csdn.net/u013982161/article/details/51347944
[4^]: https://www.cnblogs.com/LoyenWang/p/12495319.html
[5^]: http://www.wowotech.net/process_management/PELT.html
[6^]: http://www.wowotech.net/process_management/450.html
[7^]: https://www.cnblogs.com/LoyenWang/p/12584345.html
[8^]: http://www.wowotech.net/process_management/448.html
[9^]: http://www.wowotech.net/process_management/load_balance.html
[10^]: https://juejin.cn/post/7209625823581356088
