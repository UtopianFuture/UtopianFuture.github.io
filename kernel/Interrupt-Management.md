## Interrupt Management

这篇文章还是基于 arm 架构的，之后有机会再看看 x86 的 apic 实现有什么区别。之前分析过键盘敲下一个字符到显示在显示器上的经过，[那篇](./kernel/From-keyboard-to-Display.md)文章也涉及到中断的处理，我本以为中断也就那样，但我还是太天真了，面试过程中很多关于中断的问题答的不好，所以再系统的学习一下。

### 数据结构

![interrupt_management.png](https://github.com/UtopianFuture/UtopianFuture.github.io/blob/master/image/interrupt_management.png?raw=true)

简单总结一下各个结构体之间的关系。

- 内核的中断处理，围绕着中断描述符结构 `struct irq_desc` 展开，内核提供了两种中断描述符组织形式：

  - 打开 `CONFIG_SPARSE_IRQ` 宏（中断编号不连续），中断描述符以 `radix-tree` 来组织，用户在初始化时进行动态分配，然后再插入 `radix-tree` 中；

  - 关闭 `CONFIG_SPARSE_IRQ` 宏（中断编号连续），中断描述符以数组的形式组织，并且已经分配好；

  不管哪种形式，最终都可以通过 irq 号来找到对应的中断描述符；

- 图的左侧紫色部分，**主要在中断控制器驱动中进行初始化设置**，包括各个结构中函数指针的指向等，其中 `struct irq_chip` 用于对中断控制器的硬件操作，`struct irq_domain` 与中断控制器对应，完成的工作是硬件中断号到  irq 的映射；

- 图的上侧绿色部分，中断描述符的创建（这里指 `CONFIG_SPARSE_IRQ`），主要在获取设备中断信息的过程中完成的，从而让设备树中的中断能与具体的中断描述符 `irq_desc` 匹配；

- 图中剩余部分，在设备申请注册中断的过程中进行设置，比如 `struct irqaction` 中 `handler` 的设置，这个用于指向我们设备驱动程序中的中断处理函数；

### 中断控制器

内核中的中断处理可以分为 4 层，

- 硬件层，如 CPU 和中断控制器的连接。这个作为软件开发者只需要直到怎么读控制器的寄存器就可以吧；
- 处理器架构管理层，如 CPU 中断异常处理，这个在 x86 中应该是是现在 genex.S 中；
- 中断控制器管理层，如 IRQ 号的映射，中断控制器的初始化等等；
- 内核通用中断处理器层，如中断注册和中断处理；

中断触发的方式，这个在 QEMU 中体现的很明显。

- 边沿触发（edge-triggered）：当中断源产生一个上升沿或下降沿时，触发一个中断；
- 电平触发（level0sensitive）：当中断信号先产生一个高电平或低电平时，触发一个中断；

#### 中断流程

这个涉及 GIC，APIC 等中断控制器的物理实现，就先不分析了。

### hwirq 和 irq 的映射

hwirq 是外设发起中断时用的中断号，是 CPU 设计的时候就制定好了，如 LoongArch 中有 11 个硬件中断。而内核中使用的软中断号，故两者之间要做一个映射。

### 注册中断

### 底层中断处理

### 高层中断处理

### 软中断和 tasklets

### workqueue

#### 问题

- 面临什么问题内核需要使用 workqueue？
- 内核怎样使用 workqueue？
- workqueue 涉及到哪些设计思想？

#### 数据结构

workqueue 是内核里面很重要的一个机制，特别是内核驱动，**一般的小型任务 (work) 都不会自己起一个线程来处理，而是扔到 workqueue 中处理**。workqueue 的主要工作就是**用进程上下文来处理内核中大量的小任务**。

其是除了软中断和 tasklet 以外最常用的一种下半部机制。工作队列的基本原理就是将 work（需要推迟执行的函数）交由内核线程来执行，它总是在进程上下文中执行，因此它允许重新调度和睡眠，是异步执行的进程上下文。另外，其还能解决软中断和 tasklet 执行时间过长导致的系统实时性下降等问题。

所以 workqueue 的主要设计思想为：

- 并行，多个 work 不要相互阻塞；
- 节省资源，多个 work 尽量共享资源 ( 进程、调度、内存 )，不要造成系统过多的资源浪费。

为了实现的设计思想，workqueue 的设计实现也更新了很多版本。最新的 workqueue 实现叫做 CMWQ(Concurrency Managed Workqueue)，也就是用更加智能的算法来实现“并行和节省”。

workqueue 允许内核函数被激活，挂起，稍后**由 worker thread 的特殊内核线程来执行**。workqueue 中的函数运行在进程上下文中，

这部份涉及到几个关键的数据结构：

`workqueue_struct`，`worker_pool`，`pool_workqueue`，`work_struct`，`worker`，有必要把它们之间的关系搞懂。还有就是 `runqueue` 和 `workqueue` 有什么关系。**runqueue 中放的是 process，用来作负载均衡的，而 workqueue 中放的是可以延迟执行的内核函数**。

从代码中推测 `workqueue_struct` 表示一个工作队列；`pool_workqueue` 负责建立起 `workqueue` 和 `worker_pool` 之间的关系，`workqueue` 和 pwq 是一对多的关系，pwq 和 `worker_pool` 是一对一的关系；`work_struct` 表示挂起的函数，`worker` 是执行挂起函数的内核线程，一个 `worker` 对应一个 `work_thread`；`worker_pool` 表示所有用来执行 work 的 worker。

先看看它们之间的拓扑图。

![workqueue.png](https://github.com/UtopianFuture/UtopianFuture.github.io/blob/master/image/workqueue.png?raw=true)

再分析这些数据结构：

##### work_struct

```c
struct work_struct {
	atomic_long_t data;
	struct list_head entry;
	work_func_t func;
#ifdef CONFIG_LOCKDEP
	struct lockdep_map lockdep_map;
#endif
};
```

##### worker

```c
struct worker {
	/* on idle list while idle, on busy hash table while busy */
	union {
		struct list_head	entry;	/* L: while idle */
		struct hlist_node	hentry;	/* L: while busy */
	};

	struct work_struct	*current_work;	/* L: work being processed */
	work_func_t		current_func;	/* L: current_work's fn */
	struct pool_workqueue	*current_pwq;	/* L: current_work's pwq */
	unsigned int		current_color;	/* L: current_work's color */
	struct list_head	scheduled;	/* L: scheduled works */

	/* 64 bytes boundary on 64bit, 32 on 32bit */

	struct task_struct	*task;		/* I: worker task */
	struct worker_pool	*pool;		/* A: the associated pool */
						/* L: for rescuers */
	struct list_head	node;		/* A: anchored at pool->workers */
						/* A: runs through worker->node */

	unsigned long		last_active;	/* L: last active timestamp */
	unsigned int		flags;		/* X: flags */
	int			id;		/* I: worker id */
	int			sleeping;	/* None */

	/*
	 * Opaque string set with work_set_desc().  Printed out with task
	 * dump for debugging - WARN, BUG, panic or sysrq.
	 */
	char			desc[WORKER_DESC_LEN];

	/* used only by rescuers to point to the target workqueue */
	struct workqueue_struct	*rescue_wq;	/* I: the workqueue to rescue */

	/* used by the scheduler to determine a worker's last known identity */
	work_func_t		last_func;
};
```

##### worker_pool

```c
struct worker_pool {
	raw_spinlock_t		lock;		/* the pool lock */
	int			cpu;		/* I: the associated cpu */
	int			node;		/* I: the associated node ID */
	int			id;		/* I: pool ID */
	unsigned int		flags;		/* X: flags */

	unsigned long		watchdog_ts;	/* L: watchdog timestamp */

	struct list_head	worklist;	/* L: list of pending works */

	int			nr_workers;	/* L: total number of workers */
	int			nr_idle;	/* L: currently idle workers */

	struct list_head	idle_list;	/* X: list of idle workers */
	struct timer_list	idle_timer;	/* L: worker idle timeout */
	struct timer_list	mayday_timer;	/* L: SOS timer for workers */

	/* a workers is either on busy_hash or idle_list, or the manager */
	DECLARE_HASHTABLE(busy_hash, BUSY_WORKER_HASH_ORDER);
						/* L: hash of busy workers */

	struct worker		*manager;	/* L: purely informational */
	struct list_head	workers;	/* A: attached workers */
	struct completion	*detach_completion; /* all workers detached */

	struct ida		worker_ida;	/* worker IDs for task name */

	struct workqueue_attrs	*attrs;		/* I: worker attributes */
	struct hlist_node	hash_node;	/* PL: unbound_pool_hash node */
	int			refcnt;		/* PL: refcnt for unbound pools */

	/*
	 * The current concurrency level.  As it's likely to be accessed
	 * from other CPUs during try_to_wake_up(), put it in a separate
	 * cacheline.
	 */
	atomic_t		nr_running ____cacheline_aligned_in_smp;

	/*
	 * Destruction of pool is RCU protected to allow dereferences
	 * from get_work_pool().
	 */
	struct rcu_head		rcu;
} ____cacheline_aligned_in_smp;
```

CMWQ 对 worker_pool 分成两类：

- normal worker_pool，给通用的 workqueue 使用；
- unbound worker_pool，给 WQ_UNBOUND 类型的的 workqueue 使用；

##### pool_workqueue

```c
struct pool_workqueue {
	struct worker_pool	*pool;		/* I: the associated pool */
	struct workqueue_struct *wq;		/* I: the owning workqueue */
	int			work_color;	/* L: current color */
	int			flush_color;	/* L: flushing color */
	int			refcnt;		/* L: reference count */
	int			nr_in_flight[WORK_NR_COLORS];
						/* L: nr of in_flight works */

	/*
	 * nr_active management and WORK_STRUCT_INACTIVE:
	 *
	 * When pwq->nr_active >= max_active, new work item is queued to
	 * pwq->inactive_works instead of pool->worklist and marked with
	 * WORK_STRUCT_INACTIVE.
	 *
	 * All work items marked with WORK_STRUCT_INACTIVE do not participate
	 * in pwq->nr_active and all work items in pwq->inactive_works are
	 * marked with WORK_STRUCT_INACTIVE.  But not all WORK_STRUCT_INACTIVE
	 * work items are in pwq->inactive_works.  Some of them are ready to
	 * run in pool->worklist or worker->scheduled.  Those work itmes are
	 * only struct wq_barrier which is used for flush_work() and should
	 * not participate in pwq->nr_active.  For non-barrier work item, it
	 * is marked with WORK_STRUCT_INACTIVE iff it is in pwq->inactive_works.
	 */
	int			nr_active;	/* L: nr of active works */
	int			max_active;	/* L: max active works */
	struct list_head	inactive_works;	/* L: inactive works */
	struct list_head	pwqs_node;	/* WR: node on wq->pwqs */
	struct list_head	mayday_node;	/* MD: node on wq->maydays */

	/*
	 * Release of unbound pwq is punted to system_wq.  See put_pwq()
	 * and pwq_unbound_release_workfn() for details.  pool_workqueue
	 * itself is also RCU protected so that the first pwq can be
	 * determined without grabbing wq->mutex.
	 */
	struct work_struct	unbound_release_work;
	struct rcu_head		rcu;
} __aligned(1 << WORK_STRUCT_FLAG_BITS);

/*
 * Structure used to wait for workqueue flush.
 */
struct wq_flusher {
	struct list_head	list;		/* WQ: list of flushers */
	int			flush_color;	/* WQ: flush color waiting for */
	struct completion	done;		/* flush completion */
};
```

#### normal worker_pool

默认 work 是在 normal worker_pool 中处理的。系统的规划是每个 CPU 创建两个 normal worker_pool：一个 normal 优先级 (nice=0)、一个高优先级 (nice=HIGHPRI_NICE_LEVEL)，对应创建出来的 worker 的进程 nice 不一样。

每个 worker 对应一个 `worker_thread()` 内核线程，一个 worker_pool 包含一个或者多个 worker，worker_pool 中 worker 的数量是根据 worker_pool 中 work 的负载来动态增减的。下面就是一个 work 执行键盘输入任务的过程，

```plain
(gdb) p p
$22 = (unsigned char *) 0xffff88810431f429 "a"
(gdb) bt
#0  receive_buf (count=<optimized out>, head=0xffff88810431f400, port=0xffff888100a80000) at drivers/tty/tty_buffer.c:493
#1  flush_to_ldisc (work=0xffff888100a80008) at drivers/tty/tty_buffer.c:543
#2  0xffffffff810c4a49 in process_one_work (worker=worker@entry=0xffff88810401ea80, work=0xffff888100a80008) at kernel/workqueue.c:2297
#3  0xffffffff810c4c3d in worker_thread (__worker=0xffff88810401ea80) at kernel/workqueue.c:2444
#4  0xffffffff810cc32a in kthread (_create=0xffff88810400aec0) at kernel/kthread.c:319
// 中断返回后，接下来的任务由 worker 执行
#5  0xffffffff81004572 in ret_from_fork () at arch/x86/entry/entry_64.S:295
#6  0x0000000000000000 in ?? ()
```

我们可以通过 `ps -eo pid,ppid,command | grep kworker` 命令来查看所有 worker 对应的内核线程。

```plain
	  6       2 [kworker/0:0H-events_highpri]  // cpu0 的第 0 个高优先级 worker
	  7       2 [kworker/0:1-events]		   // cpu0 的第 1 个 normal worker
	  22       2 [kworker/1:0H-events_highpri] // cpu1 的第 0 个高优先级 worker
	  28       2 [kworker/2:0H-events_highpri]
	  33       2 [kworker/3:0-events]
	  34       2 [kworker/3:0H-events_highpri]
	  40       2 [kworker/4:0H-events_highpri]
	  46       2 [kworker/5:0H-events_highpri]
	  52       2 [kworker/6:0H-events_highpri]
	  58       2 [kworker/7:0H-events_highpri]
	  64       2 [kworker/8:0H-events_highpri]
	  70       2 [kworker/9:0H-events_highpri]
	  76       2 [kworker/10:0H-events_highpri]
	  82       2 [kworker/11:0H-events_highpri]
	  143       2 [kworker/1:1-events]
	  146       2 [kworker/5:1-events]
```

下面是每个结构体之间的详细关系：

![worker_pool.png](https://github.com/UtopianFuture/UtopianFuture.github.io/blob/master/image/worker_pool.png?raw=true)

对应的拓扑图为：

![worker.png](https://github.com/UtopianFuture/UtopianFuture.github.io/blob/master/image/worker.png?raw=true)

现在通过代码看看 normal worker_pool 是怎样初始化的。

```c
/**
 * workqueue_init_early - early init for workqueue subsystem
 *
 * This is the first half of two-staged workqueue subsystem initialization
 * and invoked as soon as the bare basics - memory allocation, cpumasks and
 * idr are up.  It sets up all the data structures and system workqueues
 * and allows early boot code to create workqueues and queue/cancel work
 * items.  Actual work item execution starts only after kthreads can be
 * created and scheduled right before early initcalls.
 */
void __init workqueue_init_early(void)
{
	int std_nice[NR_STD_WORKER_POOLS] = { 0, HIGHPRI_NICE_LEVEL }; // 初始化两个 worker_pool
	int hk_flags = HK_FLAG_DOMAIN | HK_FLAG_WQ;
	int i, cpu;

	...

	/* initialize CPU pools */
	for_each_possible_cpu(cpu) {
		struct worker_pool *pool; // 每个 cpu 一个 worker pool

		i = 0;
		for_each_cpu_worker_pool(pool, cpu) { // cpu0 创建 2 个 worker pool，normal 和 high priority
			BUG_ON(init_worker_pool(pool));   // smp 架构现在只有一个 boot cpu 能用
			pool->cpu = cpu;
			cpumask_copy(pool->attrs->cpumask, cpumask_of(cpu));
			pool->attrs->nice = std_nice[i++]; // 指定进程优先级
			pool->node = cpu_to_node(cpu);

			/* alloc pool ID */
			mutex_lock(&wq_pool_mutex);
			BUG_ON(worker_pool_assign_id(pool));
			mutex_unlock(&wq_pool_mutex);
		}
	}

	/* create default unbound and ordered wq attrs */
	for (i = 0; i < NR_STD_WORKER_POOLS; i++) {
		struct workqueue_attrs *attrs; // 这个 attrs 有什么用呢？

		BUG_ON(!(attrs = alloc_workqueue_attrs()));
		attrs->nice = std_nice[i];
		unbound_std_wq_attrs[i] = attrs;

		/*
		 * An ordered wq should have only one pwq as ordering is
		 * guaranteed by max_active which is enforced by pwqs.
		 * Turn off NUMA so that dfl_pwq is used for all nodes.
		 */
		BUG_ON(!(attrs = alloc_workqueue_attrs()));
		attrs->nice = std_nice[i];
		attrs->no_numa = true;
		ordered_wq_attrs[i] = attrs;
	}

    ...

}
```

`alloc_workqueue` 是重构之后的接口，原来的接口是 `create_workqueue`，

```c
#define create_workqueue(name)						\
	alloc_workqueue("%s", __WQ_LEGACY | WQ_MEM_RECLAIM, 1, (name))
```

从代码中我们看到 `alloc_workqueue` 需要 3 个参数，第一个参数是 workqueue 的名字，但和原来的接口不同，这个创建对应的执行线程时不会再使用这个名字，第二个参数是 flag，表示在该 workqueue 的 work 会如何执行。

- WQ_UNBOUND：workqueue 设计的是默认运行在 task 提交的 cpu 上，这样能够或和更好的内存局部性（cache 命中率更高）。而这个选项就是允许 task 运行在任何一个 cpu 上，unbround workqueue 让 work 尽早开始执行，而这会牺牲部分局部性。
- WQ_FREEZABLE：设置这个选项的 workqueue 在系统 suspended 时将会 frozen。
- WQ_MEM_RECLAIM：All wq which might be used in the memory reclaim paths **MUST**  have this flag set.  The wq is guaranteed to have at least one execution context regardless of memory pressure.（不懂）
- WQ_HIGHPRI：高优先级的 task 不会等待 cpu 空闲，它们会抢占 cpu ，立刻执行，所以这种 workqueue 的 tasks 会竞争 cpu。
- WQ_CPU_INTENSIVE：这个很好理解，cpu 密集型 task。如果 cpu 已经被其他的 task 占用，那么这种 workqueue 的 task 就会被延迟。
- WQ_SYSFS
- WQ_POWER_EFFICIENT：降低能耗。当 `wq_power_efficient` 选项打开时，flag 变成 `WQ_UNBOUND`。目前只有 `events_power_efficient` 和 `events_freezable_power_efficient` 两个 workqueue 使用了这个选项。
- __WQ_DRAINING
- __WQ_ORDERED
- __WQ_LEGACY：`create_workqueu` 使用这个选项。
- __WQ_ORDERED_EXPLICIT

第三个参数 max_active，表示该 cpu 上能够分配给每个 workqueue 上的 work 的上下文执行数量。

接下来我们看看初始化 workqueue 的第二阶段。

```c
/**
 * workqueue_init - bring workqueue subsystem fully online
 *
 * This is the latter half of two-staged workqueue subsystem initialization
 * and invoked as soon as kthreads can be created and scheduled.
 * Workqueues have been created and work items queued on them, but there
 * are no kworkers executing the work items yet.  Populate the worker pools
 * with the initial workers and enable future kworker creations.
 */
void __init workqueue_init(void)
{
	struct workqueue_struct *wq;
	struct worker_pool *pool;
	int cpu, bkt;

	wq_numa_init();

	mutex_lock(&wq_pool_mutex);

	for_each_possible_cpu(cpu) {
		for_each_cpu_worker_pool(pool, cpu) {
			pool->node = cpu_to_node(cpu);
		}
	}

	list_for_each_entry(wq, &workqueues, list) {
		wq_update_unbound_numa(wq, smp_processor_id(), true);
		WARN(init_rescuer(wq),
		     "workqueue: failed to create early rescuer for %s",
		     wq->name);
	}

	mutex_unlock(&wq_pool_mutex);

	/* create the initial workers */
	for_each_online_cpu(cpu) { // 给每个 worker pool 创建第一个 worker
		for_each_cpu_worker_pool(pool, cpu) {
			pool->flags &= ~POOL_DISASSOCIATED;
			BUG_ON(!create_worker(pool));
		}
	}

	hash_for_each(unbound_pool_hash, bkt, pool, hash_node)
		BUG_ON(!create_worker(pool));

	wq_online = true;
	wq_watchdog_init();
}
```

`workqueue` 涉及到一个非常重要的数据结构的初始化，我也是在之后的调试中才发现的。

### __kthread_create_on_node

这是它的调用过程。

```plain
#0  __kthread_create_on_node (threadfn=threadfn@entry=0xffffffff810c4fe0 <rescuer_thread>, data=data@entry=0xffff8881001d8900, node=node@entry=-1,
    namefmt=namefmt@entry=0xffffffff826495f9 "%s", args=args@entry=0xffffc90000013dd0) at kernel/kthread.c:361
#1  0xffffffff810cbb19 in kthread_create_on_node (threadfn=threadfn@entry=0xffffffff810c4fe0 <rescuer_thread>, data=data@entry=0xffff8881001d8900, node=node@entry=-1,
    namefmt=namefmt@entry=0xffffffff826495f9 "%s") at kernel/kthread.c:453
#2  0xffffffff810c260e in init_rescuer (wq=wq@entry=0xffff888100066e00) at kernel/workqueue.c:4273
#3  0xffffffff831e6fb9 in init_rescuer (wq=0xffff888100066e00) at kernel/workqueue.c:4265
#4  workqueue_init () at kernel/workqueue.c:6081
#5  0xffffffff831baad2 in kernel_init_freeable () at init/main.c:1598
#6  0xffffffff81c0b31a in kernel_init (unused=<optimized out>) at init/main.c:1505
#7  0xffffffff81004572 in ret_from_fork () at arch/x86/entry/entry_64.S:295
#8  0x0000000000000000 in ?? ()
```

我们来看看 `kthread_create_list` 到底有什么用，为什么之后一系列的函数都要用到它。

```c
struct task_struct *__kthread_create_on_node(int (*threadfn)(void *data),
						    void *data, int node,
						    const char namefmt[],
						    va_list args)
{
	DECLARE_COMPLETION_ONSTACK(done);
	struct task_struct *task;
	struct kthread_create_info *create = kmalloc(sizeof(*create), // 最重要的就是分配了 create
						     GFP_KERNEL);

	if (!create)
		return ERR_PTR(-ENOMEM);
	create->threadfn = threadfn;
	create->data = data;
	create->node = node;
	create->done = &done;

	spin_lock(&kthread_create_lock);
	list_add_tail(&create->list, &kthread_create_list); // 将 create 信息添加到 kthread_create_list 中
    													// 之后进程调度就从 kthread_create_list 中获取 task
	spin_unlock(&kthread_create_lock);

	wake_up_process(kthreadd_task);

    ...

	task = create->result;

	...

	kfree(create);
	return task;
}
```

#### 执行场景

我们用类似甘特图的方式来描述 worker 在不同配置下的执行过程。

 work0、w1、w2 被排到同一个 CPU 上的一个绑定的 wq q0 上。w0 消耗 CPU 5ms，然后睡眠 10ms，然后在完成之前再次消耗 CPU 5ms。忽略所有其他的任务、工作和处理开销，并假设简单的 FIFO 调度，下面是一个高度简化的原始 workqueue 的可能的执行序列。

```plain
 TIME IN MSECS	EVENT
 0		w0 starts and burns CPU
 5		w0 sleeps
 15		w0 wakes up and burns CPU
 20		w0 finishes
 20		w1 starts and burns CPU
 25		w1 sleeps
 35		w1 wakes up and finishes
 35		w2 starts and burns CPU
 40		w2 sleeps
 50		w2 wakes up and finishes
```

当 `max_active` >= 3 时，

```plain
TIME IN MSECS	EVENT
 0		w0 starts and burns CPU
 5		w0 sleeps
 5		w1 starts and burns CPU
 10		w1 sleeps
 10		w2 starts and burns CPU
 15		w2 sleeps
 15		w0 wakes up and burns CPU
 20		w0 finishes
 20		w1 wakes up and finishes
 25		w2 wakes up and finishes
```

当 `max_active` == 2 时，

```plain
 TIME IN MSECS	EVENT
 0		w0 starts and burns CPU
 5		w0 sleeps
 5		w1 starts and burns CPU
 10		w1 sleeps
 15		w0 wakes up and burns CPU
 20		w0 finishes
 20		w1 wakes up and finishes
 20		w2 starts and burns CPU
 25		w2 sleeps
 35		w2 wakes up and finishes
```

### 中断案例分析

这是测试 LA 内核的中断的小程序。

运行环境：qemu_la（在 la 机器上直接 sudo apt install qemu） + 3a5000
运行命令：

```plain
qemu-system-loongarch64 -nographic -m 2G -cpu Loongson-3A5000 -serial mon:stdio -bios ~/research/bmbt/qemu-la/pc-bios/loongarch_bios.bin -M loongson7a,kernel_irqchip=off -kernel ~/gitlab/timer-interrupt/hello_period.elf
```

调试命令：

```plain
qemu-system-loongarch64 -nographic -m 2G -cpu Loongson-3A5000 -serial mon:stdio -bios ~/research/bmbt/qemu-la/pc-bios/loongarch_bios.bin -M loongson7a,kernel_irqchip=off -kernel ~/gitlab/timer-interrupt/hello_period.elf -gdb tcp::5678 -S
```

正常的话会周期性的打印 "say_hi!!" 和 "timer interrupt coming!!"

目前遇到的问题是无法在 LA 的机器上编译。
会出现 'fatal error: no match insn: sym_func_start(__cpu_wait)' 的报错，
`SYM_FUNC_START` 是内核实现的 macro，其能够将汇编指令转化成类似于 C 函数。具体解释在[这里](https://www.kernel.org/doc/html/latest/asm-annotations.html#assembler-annotations)

loongarch 的中断处理过程如下。

```plain
#0  serial8250_tx_chars (up=0x9000000001696850 <serial8250_ports>) at drivers/tty/serial/8250/8250_port.c:1805
#1  0x90000000009f1198 in serial8250_handle_irq (port=0x9000000001696850 <serial8250_ports>, iir=194) at drivers/tty/serial/8250/8250_port.c:1924
#2  0x90000000009f1320 in serial8250_handle_irq (iir=<optimized out>, port=<optimized out>) at drivers/tty/serial/8250/8250_port.c:1897
#3  serial8250_default_handle_irq (port=0x9000000001696850 <serial8250_ports>) at drivers/tty/serial/8250/8250_port.c:1940
#4  0x90000000009ece10 in serial8250_interrupt (irq=19, dev_id=0x900000027da36300) at drivers/tty/serial/8250/8250_core.c:125
#5  0x9000000000283c70 in __handle_irq_event_percpu (desc=0x9000000001696850 <serial8250_ports>, flags=0x6) at kernel/irq/handle.c:149
#6  0x9000000000283ed0 in handle_irq_event_percpu (desc=0x900000027d14da00) at kernel/irq/handle.c:189
#7  0x9000000000283f7c in handle_irq_event (desc=0x900000027d14da00) at kernel/irq/handle.c:206
#8  0x9000000000288348 in handle_level_irq (desc=0x900000027d14da00) at kernel/irq/chip.c:650
#9  0x9000000000282aac in generic_handle_irq_desc (desc=<optimized out>) at include/linux/irqdesc.h:155
#10 generic_handle_irq (irq=<optimized out>) at kernel/irq/irqdesc.c:639
#11 0x90000000008f97ac in extioi_irq_dispatch (desc=<optimized out>) at drivers/irqchip/irq-loongson-extioi.c:305
#12 0x9000000000282aac in generic_handle_irq_desc (desc=<optimized out>) at include/linux/irqdesc.h:155
#13 generic_handle_irq (irq=<optimized out>) at kernel/irq/irqdesc.c:639
#14 0x90000000010153f8 in do_IRQ (irq=<optimized out>) at arch/loongarch/kernel/irq.c:103
#15 0x9000000000203674 in except_vec_vi_handler () at arch/loongarch/kernel/genex.S:92
```

`except_vec_vi_handler` 执行完后会跳转到 `do_vi` ，根据 irq 对应的 action 执行相应的回调函数。对于时间中断来说是 `plat_irq_dispatch`。

```c
void do_vi(int irq)
{
	vi_handler_t	action;

	action = ip_handlers[irq];
	if (action)
		action(irq);
	else
		pr_err("vi handler[%d] is not installed\n", irq);
}
```

`struct irq_domain`与中断控制器对应，完成的工作是硬件中断号到 `Linux irq` 的映射，时钟中断的硬件中断号为 11，`linux irq` 为 61。

```c
asmlinkage void __weak plat_irq_dispatch(int irq)
{
	unsigned int virq;

	virq = irq_linear_revmap(irq_domain, irq);
	do_IRQ(virq);
}
```

中断号 irq 是用 a0 寄存器传递的，发现在进入 `except_vec_vi_handler` 时中断号就已经是 11 了，所以 `except_vec_vi_handler` 还不是最开始处理中断的地方。发现在 `except_vec_vi_handler` 可以使用 backtrace 。

```plain
#0  0x90000000002035c8 in except_vec_vi_handler () at arch/loongarch/kernel/genex.S:45
#1  0x90000000010154a4 in csr_xchgl (reg=<optimized out>, mask=<optimized out>, val=<optimized out>) at arch/loongarch/include/asm/loongarchregs.h:341
#2  arch_local_irq_enable () at arch/loongarch/include/asm/irqflags.h:22
#3  __do_softirq () at kernel/softirq.c:276
```

![image-20211228192240982](https://github.com/UtopianFuture/UtopianFuture.github.io/blob/master/image/bmbt-virtualization.1.png?raw=true)

手册说的很清楚，中断信号存储在 CSR.ESTA.IS 域，但不清楚是不是 cpu 会自动将 IS 域与 CSR.ECFG.LIT 域相与，得到 int_vec，需要测试。同时经过调试发现只要将 CSR.CRMD.IE 置为 1，就会执行中断，没有发现有指令来进行按位相于的操作。还有一个问题，为什么将 CSR.CRMD.IE 置为 1 后会立刻跳转到 `except_vec_vi_handler`，从哪里跳转过去的。

其实还是在 `trap_init` 中初始化的，

```c
void __init trap_init(void)
{
	...

	/* set interrupt vector handler */
	for (i = EXCCODE_INT_START; i < EXCCODE_INT_END; i++) {
		vec_start = vector_table[i - EXCCODE_INT_START];
		set_handler(i * VECSIZE, vec_start, VECSIZE);
	}

    ...

	cache_error_setup();
}
```

这里 `vector_talbe` 就是中断向量表。

```c
        .globl  vector_table
vector_table:
	PTR	handle_vi_0
	PTR	handle_vi_1
	PTR	handle_vi_2
	PTR	handle_vi_3
	PTR	handle_vi_4
	PTR	handle_vi_5
	PTR	handle_vi_6
	PTR	handle_vi_7
	PTR	handle_vi_8
	PTR	handle_vi_9
	PTR	handle_vi_10
	PTR	handle_vi_11
	PTR	handle_vi_12
	PTR	handle_vi_13
```

当然 `handle_vi_` 只是一个跳转地址，指向真正的处理函数，

```c
/*
 * Macro helper for vectored interrupt handler.
 */
	.macro	BUILD_VI_HANDLER num
	.align	5
SYM_FUNC_START(handle_vi_\num)
	csrwr	t0, LOONGARCH_CSR_KS0
	csrwr	t1, LOONGARCH_CSR_KS1
	SAVE_SOME #docfi=1
	addi.d	v0, zero, \num
	la.abs	v1, except_vec_vi_handler
	jirl	zero, v1, 0
SYM_FUNC_END(handle_vi_\num)
```

然后就能跳转到 `except_vec_vi_handler` 执行。

ok，再看看 `set_handler` 是怎样注册的。

```c
/* Install CPU exception handler */
void set_handler(unsigned long offset, void *addr, unsigned long size)
{
	memcpy((void *)(ebase + offset), addr, size);
	local_flush_icache_range(ebase + offset, ebase + offset + size);
}
```

`ebase` 是一个全局变量，其在 `start_kernel` -> `setup_arch` -> `cpu_probe` -> `per_cpu_trap_init` -> `configure_exception_vector` 就已经初始化好了。

```c
static void configure_exception_vector(void)
{
	ebase		= (unsigned long)exception_handlers;
	refill_ebase	= (unsigned long)exception_handlers + 80*VECSIZE;

	csr_writeq(ebase, LOONGARCH_CSR_EBASE);
	csr_writeq(refill_ebase, LOONGARCH_CSR_TLBREBASE);
	csr_writeq(ebase, LOONGARCH_CSR_ERREBASE);
}
```

而 `LOONGARCH_CSR_EBASE` 就是 loongson cpu 的中断入口寄存器。

发现在设置 `LOONGARCH_CSR_EBASE` 时 ebase 的值明明是 20070 ，但写入到 `LOONGARCH_CSR_EBASE` 的值却是 30070，用于写入的寄存器不一样。
下面是内核的设置过程，是正常的，记录下来做个对比。

```plain
0x900000000020a438 <per_cpu_trap_init+16>       pcaddu12i $r14,5110(0x13f6)
0x900000000020a43c <per_cpu_trap_init+20>       addi.d $r14,$r14,-1072(0xbd0)
0x900000000020a440 <per_cpu_trap_init+24>       lu12i.w $r13,10(0xa)
0x900000000020a444 <per_cpu_trap_init+28>       stptr.d $r12,$r14,0
0x900000000020a448 <per_cpu_trap_init+32>       add.d  $r13,$r12,$r13
0x900000000020a44c <per_cpu_trap_init+36>       pcaddu12i $r14,5110(0x13f6)
0x900000000020a450 <per_cpu_trap_init+40>       addi.d $r14,$r14,-1100(0xbb4)
0x900000000020a454 <per_cpu_trap_init+44>       stptr.d $r13,$r14,0
0x900000000020a45c <per_cpu_trap_init+52>       csrwr  $r14,0xc
0x900000000020a460 <per_cpu_trap_init+56>       csrwr  $r13,0x88
0x900000000020a464 <per_cpu_trap_init+60>       csrwr  $r12,0x93
```

### 注意

1. 在时间中断的处理函数中需要将 `LOONGARCH_CSR_TINTCLR` 的 `CSR_TINTCLR_TI` 位置为 0，不然会一直处理时间中断。

```c
void do_vi(int irq) {
  if (irq == 0) { // time interrupt
    clear_time_intr();
    timer_interrupt();
  }
  // irq_exit();
  return;
}
```

```c
void clear_time_intr() {
  unsigned long val = CSR_TINTCLR_TI;
  asm volatile("csrwr %0, %1\n\t" : "=r"(val) : "i"(LOONGARCH_CSR_TINTCLR) :);
}
```

2. 内核注册中断入口是这样的：

```c
/* set interrupt vector handler */
for (i = EXCCODE_INT_START; i < EXCCODE_INT_END; i++) {
  vec_start = vector_table[i - EXCCODE_INT_START];
  set_handler(i * VECSIZE, vec_start, VECSIZE);
}
```

但是自己按照这种方式设置会有问题，即中断来了不能正常跳转到注册的位置：`ebase + offset` 只能跳转到 `ebase` ，为了暂时跳过这个问题，按照如下方式设置中断入口：

```c
for (i = 0; i < EXCCODE_INT_END - EXCCODE_INT_START; i++) {
    vec_start = vector_table[i];
    set_handler(i * VECSIZE, vec_start, VECSIZE);
}
```

3. 系统态开发的正确方法应该是多看手册和内核。在遇到无法解决的问题时根据手册内容多尝试。
4. 在检查值是否写入了特定的寄存器时，一定要先写，再读取，看读取的值是否正确。

```c
  asm volatile("csrxchg %0, %1, %2\n\t"
               : "=r"(val)
               : "r"(CSR_ECFG_VS), "i"(LOONGARCH_CSR_ECFG)
               :);

  asm volatile("csrrd %0, %1\n\t" : "=r"(ecfg_val) : "i"(LOONGARCH_CSR_ECFG));
```

现在遇到的问题是无法接收到 serial 中断和 keyboard 中断。
在看书的过程中发现 la 有 8 个硬中断，这些硬中断的中断源来自处理器核外部，其直接来源是核外的中断控制器。也就是说 serial 发起的中断并不是直接将 cpu 的硬中断之一拉高，而是发送到中断控制器，如 8259 就是 pin1 作为 keyboard 中断，pin3, pin4 都是 serial 中断。那么是不是我没有设置中断控制器的映射从而导致无法接收到 serial 中断。
定时器中断能够响应是因为 cpu 中有一个线中断： TI 作为定时器中断。

### 总结

- 原来轮询机制再网络吞吐量大的应用场景下，网卡驱动采用轮询机制比中断机制效率要高。

### Reference

[1] https://lwn.net/Articles/403891/

[2] https://kernel.meizu.com/linux-workqueue.html

[3] https://www.cnblogs.com/LoyenWang/p/13052677.html
