## workqueue

### 问题

- 面临什么问题内核需要使用 workqueue？
- 内核怎样使用 workqueue？
- workqueue 涉及到哪些设计思想？

Workqueue 是内核里面很重要的一个机制，特别是内核驱动，一般的小型任务 (work) 都不会自己起一个线程来处理，而是扔到 Workqueue 中处理。Workqueue 的主要工作就是**用进程上下文来处理内核中大量的小任务**。

所以 Workqueue 的主要设计思想：一个是并行，多个 work 不要相互阻塞；另外一个是节省资源，多个 work 尽量共享资源 ( 进程、调度、内存 )，不要造成系统过多的资源浪费。

为了实现的设计思想，workqueue 的设计实现也更新了很多版本。最新的 workqueue 实现叫做 CMWQ(Concurrency Managed Workqueue)，也就是用更加智能的算法来实现“并行和节省”。

workqueue 允许内核函数被激活，挂起，稍后**由 worker thread 的特殊内核线程来执行**。workqueue 中的函数运行在进程上下文中，

这部份涉及到几个关键的数据结构：

`workqueue_struct`，`worker_pool`，`pool_workqueue`，`work_struct`，`worker`，有必要把它们之间的关系搞懂。还有就是 runqueue 和 workqueue 有什么关系。**runqueue 中放的是 process，用来作负载均衡的，而 workqueue 中放的是可以延迟执行的内核函数**。

从代码中推测 `workqueue_struct` 表示一个工作队列；`pool_workqueue` 负责建立起 `workqueue` 和 `worker_pool` 之间的关系，`workqueue` 和 pwq 是一对多的关系，pwq 和 `worker_pool` 是一对一的关系；`work_struct` 表示挂起的函数，`worker` 是执行挂起函数的内核线程，一个 `worker` 对应一个 `work_thread`；`worker_pool` 表示所有用来执行 work 的 worker。

可以看一下它们之间的拓扑图。



### workqueue init

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
	int std_nice[NR_STD_WORKER_POOLS] = { 0, HIGHPRI_NICE_LEVEL };
	int hk_flags = HK_FLAG_DOMAIN | HK_FLAG_WQ;
	int i, cpu;

	BUILD_BUG_ON(__alignof__(struct pool_workqueue) < __alignof__(long long));

	BUG_ON(!alloc_cpumask_var(&wq_unbound_cpumask, GFP_KERNEL));
	cpumask_copy(wq_unbound_cpumask, housekeeping_cpumask(hk_flags));

	pwq_cache = KMEM_CACHE(pool_workqueue, SLAB_PANIC);

	/* initialize CPU pools */
	for_each_possible_cpu(cpu) {
		struct worker_pool *pool; // 每个 cpu 一个 worker pool

		i = 0;
		for_each_cpu_worker_pool(pool, cpu) { // cpu0 创建 2 个 worker pool
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

	system_wq = alloc_workqueue("events", 0, 0); // 这些 work queue 分别负责什么
	system_highpri_wq = alloc_workqueue("events_highpri", WQ_HIGHPRI, 0);
	system_long_wq = alloc_workqueue("events_long", 0, 0);
	system_unbound_wq = alloc_workqueue("events_unbound", WQ_UNBOUND,
					    WQ_UNBOUND_MAX_ACTIVE);
	system_freezable_wq = alloc_workqueue("events_freezable",
					      WQ_FREEZABLE, 0);
	system_power_efficient_wq = alloc_workqueue("events_power_efficient",
					      WQ_POWER_EFFICIENT, 0);
	system_freezable_power_efficient_wq = alloc_workqueue("events_freezable_power_efficient",
					      WQ_FREEZABLE | WQ_POWER_EFFICIENT,
					      0);
	BUG_ON(!system_wq || !system_highpri_wq || !system_long_wq ||
	       !system_unbound_wq || !system_freezable_wq ||
	       !system_power_efficient_wq ||
	       !system_freezable_power_efficient_wq);
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

### 执行场景

我们用类似甘特图的方式来描述 worker 在不同配置下的执行过程。

 w(ork)0、w1、w2 被排到同一个 CPU 上的一个绑定的 wq q0 上。w0 消耗 CPU 5ms，然后睡眠 10ms，然后在完成之前再次消耗 CPU 5ms。忽略所有其他的任务、工作和处理开销，并假设简单的 FIFO 调度，下面是一个高度简化的原始 workqueue 的可能的执行序列。

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



### Reference

[1] https://lwn.net/Articles/403891/

[2] https://kernel.meizu.com/linux-workqueue.html
