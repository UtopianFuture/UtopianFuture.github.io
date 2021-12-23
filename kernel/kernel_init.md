## Kernel_init

这篇文章我们来分析一下内核启动的第二阶段，第一阶段的分析[这里](https://github.com/UtopianFuture/UtopianFuture.github.io/blob/master/virtualization/bmbt-virtualization.md)。

在 `start_kerenl` 函数中，进行了系统启动过程中几乎所有重要的初始化(有一部分在 boot 中初始化，有一部分在 `start_kernel` 之前的汇编代码进行初始化)，包括内存、页表、必要数据结构、信号、调度器、硬件设备等。而这些初始化是由谁来负责的？就是由`init_task` 进程。`init_task` 是**静态定义的一个进程**，也就是说当内核被放入内存时，它就已经存在，它没有自己的用户空间，一直处于内核空间中运行，并且也只处于内核空间运行（是不是可以理解为是一个 kernel thread）。当它执行到最后，将 `start_kernel`中所有的初始化执行完成后，会在调用 `rest_init` 创建 `kernel_init` 内核线程和一个 `kthreadd` 内核线程，`kernel_init` 内核线程执行到最后会通过 `execve` 系统调用执行转变为我们所熟悉的 **`init` 进程**，而 `kthreadd` 内核线程是内核用于**管理调度其他的内核线程的守护线程**。在最后 `init_task` 将变成一个 idle 进程，用于在 CPU 没有进程运行时运行它，它在此时仅仅用于空转。

`init_task` 在 `sched_init` 完成后化身为 idle 进程，但是它还会继续执行初始化工作，相当于这里只是给 `init_task` 挂个 idle 进程的名号，它其实还是 `init_task` 进程，只有在 `arch_call_rest_init` 中调用 `rest_init` ，`rest_init` 创建 1 号和 2 号进程，然后才化身为真正的 idle 进程，后续的系统启动由 1 号进程接管，1 号进程的执行就是 `kernel_init`。我们先来分析 `sched_init`。

内核线程是在内核态执行周期性任务（如刷新磁盘高速缓存，交换出不用的页框，维护网络连接等等）的进程，其只运行在内核态，不需要进行上下文切换，但也只能使用大于 PAGE_OFFSET（传统的 x86_32 上是 3G）的地址空间。也可以称其为**内核守护进程**。

我看可以使用 `ps -eo pid,ppid,command` 来看一下线程。

```shell
guanshun@guanshun-ubuntu ~> ps -eo pid,ppid,command
    PID    PPID COMMAND
      1       0 /sbin/init splash
      2       0 [kthreadd]
      3       2 [rcu_gp]
      4       2 [rcu_par_gp]
      6       2 [kworker/0:0H-kblockd]
      9       2 [mm_percpu_wq]
```

### sched_init

在`start_kernel`中对调度器进行初始化的函数就是 `sched_init`，其主要工作为

- 对相关数据结构分配内存
- 初始化`root_task_group`
- 初始化每个 CPU 的 rq 队列(包括其中的 cfs 队列和实时进程队列)
- 将 `init_task` 进程转变为 idle 进程

```c
void __init sched_init(void)
{
	unsigned long ptr = 0;
	int i;

	wait_bit_init(); // bit_wait_table 是进程的 wait/wake 队列

	...

#ifdef CONFIG_FAIR_GROUP_SCHED
		root_task_group.se = (struct sched_entity **)ptr;
		ptr += nr_cpu_ids * sizeof(void **); // 每个 CPU 上的调度实体指针 se

		root_task_group.cfs_rq = (struct cfs_rq **)ptr;
		ptr += nr_cpu_ids * sizeof(void **); // 每个 CPU 上的 CFS 运行队列指针

		root_task_group.shares = ROOT_TASK_GROUP_LOAD;
		init_cfs_bandwidth(&root_task_group.cfs_bandwidth);
#endif /* CONFIG_FAIR_GROUP_SCHED */
	}
#ifdef CONFIG_CPUMASK_OFFSTACK
	for_each_possible_cpu(i) {
		per_cpu(load_balance_mask, i) = (cpumask_var_t)kzalloc_node(
			cpumask_size(), GFP_KERNEL, cpu_to_node(i));
		per_cpu(select_idle_mask, i) = (cpumask_var_t)kzalloc_node(
			cpumask_size(), GFP_KERNEL, cpu_to_node(i));
	}
#endif /* CONFIG_CPUMASK_OFFSTACK */

    // 初始化 realtime process 的带宽限制，用于设置 realtime process 在 cpu 中的占用比
	// 不懂
	init_rt_bandwidth(&def_rt_bandwidth, global_rt_period(), global_rt_runtime());
	init_dl_bandwidth(&def_dl_bandwidth, global_rt_period(), global_rt_runtime());

#ifdef CONFIG_SMP
     // 初始化默认的调度域，调度域包含一个或多个 CPU，负载均衡是在调度域内执行的
	init_defrootdomain();
#endif

#ifdef CONFIG_CGROUP_SCHED
	task_group_cache = KMEM_CACHE(task_group, 0); // 分配内存

	list_add(&root_task_group.list, &task_groups); // 将 root_task_group 加入到 task_groups
	INIT_LIST_HEAD(&root_task_group.children);
	INIT_LIST_HEAD(&root_task_group.siblings);
    // 自动分组初始化，每个tty(控制台)动态的创建进程组，这样就可以降低高负载情况下的桌面延迟
    // 这个不理解？？？
	autogroup_init(&init_task);
#endif /* CONFIG_CGROUP_SCHED */

	for_each_possible_cpu(i) { // 初始化每个 CPU 的 rq 队列
		struct rq *rq;

		rq = cpu_rq(i); // 获取当前 CPU 的 rq 队列
		raw_spin_lock_init(&rq->__lock);
		rq->nr_running = 0; // rq 队列中调度实体为 0 个
		rq->calc_load_active = 0;
		rq->calc_load_update = jiffies + LOAD_FREQ;
		init_cfs_rq(&rq->cfs);
		init_rt_rq(&rq->rt);
		init_dl_rq(&rq->dl);
#ifdef CONFIG_FAIR_GROUP_SCHED
		INIT_LIST_HEAD(&rq->leaf_cfs_rq_list);
		rq->tmp_alone_branch = &rq->leaf_cfs_rq_list;
		// 初始化CFS的带宽限制，用于设置普通进程在 CPU 中所占用比
		init_tg_cfs_entry(&root_task_group, &rq->cfs, NULL, i, NULL);
#endif /* CONFIG_FAIR_GROUP_SCHED */

		rq->rt.rt_runtime = def_rt_bandwidth.rt_runtime;
#ifdef CONFIG_SMP
        // 这些都是负载均衡使用的参数
		rq->sd = NULL;
		rq->rd = NULL;

        ...

		INIT_LIST_HEAD(&rq->cfs_tasks);

        // 将该 rq 加入到默认调度域中
		rq_attach_root(rq, &def_root_domain);
#ifdef CONFIG_NO_HZ_COMMON
		rq->last_blocked_load_update_tick = jiffies;
		atomic_set(&rq->nohz_flags, 0);

		INIT_CSD(&rq->nohz_csd, nohz_csd_func, rq);
#endif
#ifdef CONFIG_HOTPLUG_CPU
		rcuwait_init(&rq->hotplug_wait);
#endif
#endif /* CONFIG_SMP */
        // rq 定时器
		hrtick_rq_init(rq);
		atomic_set(&rq->nr_iowait, 0);
	}

    // 设置 init_task 进程的权重
	set_load_weight(&init_task, false);

	...

    // 将当前进程设置为 idle 进程，被调度时只是空转
	init_idle(current, smp_processor_id());

	calc_load_update = jiffies + LOAD_FREQ;

#ifdef CONFIG_SMP
	idle_thread_set_boot_cpu();
	balance_push_set(smp_processor_id(), false);
#endif
	init_sched_fair_class();

	psi_init();

	init_uclamp();

    // 这里只是标记调度器开始运行，但是此时系统只有 init_task(idle) 进程，
    // 并且定时器都还没启动，并不会调度到其他进程，也没有其他进程可供调度
	scheduler_running = 1;
}
```

接下来我们分析看看 `rest_init` 是怎样创建 1，2 号进程。

```c
noinline void __ref rest_init(void)
{
	struct task_struct *tsk;
	int pid;

	rcu_scheduler_starting();
	/*
	 * We need to spawn init first so that it obtains pid 1, however
	 * the init task will end up wanting to create kthreads, which, if
	 * we schedule it before we create kthreadd, will OOPS.
	 */
	pid = kernel_thread(kernel_init, NULL, CLONE_FS); // 创建 kernel_init 进程，该线程完成接下来的初始化工作

    ...

	numa_default_policy();
    // 创建 kthreadd 线程，该线程负责创建其他的 kernal thread
	pid = kernel_thread(kthreadd, NULL, CLONE_FS | CLONE_FILES);
	rcu_read_lock();
	kthreadd_task = find_task_by_pid_ns(pid, &init_pid_ns);
	rcu_read_unlock();

	system_state = SYSTEM_SCHEDULING;

	complete(&kthreadd_done);

	/*
	 * The boot idle thread must execute schedule()
	 * at least once to get things moving:
	 */
	schedule_preempt_disabled(); // 调用 schedule 开始内核调度，系统可以运转
	/* Call into cpu_idle with preempt disabled */
	cpu_startup_entry(CPUHP_ONLINE); // init_task 真正成为 idle 线程，但貌似 idle 的实现也蛮复杂的
}
```

`kernel_thread` 就是负责创建 kernel thread 的，这里暂时不分析它是怎么创建的。我们关注 1 号进程——`kernel_init` 和 2 号进程 `kthreadd` 是怎样工作的。

了解了一些关于 `kernel_thread` 的历史信息：

`kernel_thread` 是一个古老的接口，内核中的有些地方仍然在使用该方法，将一个函数直接传递给内核来创建内核线程，创建的进程运行在内核空间，并且与其他进程线程共享内核虚拟地址空间。但这种方法效率不高，于是 linux-3.x 下之后, 有了更好的实现, 那就是延后内核的创建工作, 将内核线程的创建工作交给一个内核线程来做, 即 `kthreadd` 2 号进程，但是在 `kthreadd` 还没创建之前, 我们只能通过 `kernel_thread` 这种方式去创建。

### kernel_init

`kernel_init` 的生命周期分为内核态和用户态。在内核态的主要工作是执行 `kernel_init_freeable`，我们之后会分析。然后就是要图找到用户态下的那个 init 程序，原因是 `kernel_init` **要完成从内核态到用户态的转变就必须去运行一个用户态的应用程序**，而内核源代码中的程序都是属于内核态的，所以这个应用程序必须不属于内核源代码，这样才能保证自己是用户态，所以这个应用程序就的是由另外一份文件提供，即根文件系统。

```c
static int __ref kernel_init(void *unused)
{
	int ret;

	/*
	 * Wait until kthreadd is all set-up.
	 */
	wait_for_completion(&kthreadd_done);

	kernel_init_freeable();
	/* need to finish all async __init code before freeing the memory */
	async_synchronize_full();
	kprobe_free_init_mem();
	ftrace_free_init_mem();
	kgdb_free_init_mem();
	exit_boot_config();
	free_initmem();
	mark_readonly();

	/*
	 * Kernel mappings are now finalized - update the userspace page-table
	 * to finalize PTI.
	 */
	pti_finalize();

	system_state = SYSTEM_RUNNING;
	numa_default_policy();

	rcu_end_inkernel_boot();

	do_sysctl_args();

	if (ramdisk_execute_command) { // 第一优先级的用户态程序
		ret = run_init_process(ramdisk_execute_command);
		if (!ret)
			return 0;
		pr_err("Failed to execute %s (error %d)\n",
		       ramdisk_execute_command, ret);
	}

	/*
	 * We try each of these until one succeeds.
	 *
	 * The Bourne shell can be used instead of init if we are
	 * trying to recover a really broken machine.
	 */
	if (execute_command) { // 第二优先级的用户态程序
		ret = run_init_process(execute_command); // 装入用户态 init 程序，kernel_init 变成用户态进程
		if (!ret)
			return 0;
		panic("Requested init %s failed (error %d).",
		      execute_command, ret);
	}

	if (CONFIG_DEFAULT_INIT[0] != '\0') {
		ret = run_init_process(CONFIG_DEFAULT_INIT);
		if (ret)
			pr_err("Default init %s failed (error %d)\n",
			       CONFIG_DEFAULT_INIT, ret);
		else
			return 0;
	}

	if (!try_to_run_init_process("/sbin/init") ||
	    !try_to_run_init_process("/etc/init") ||
	    !try_to_run_init_process("/bin/init") ||
	    !try_to_run_init_process("/bin/sh")) // 前 2 个都没找到，按照预定的路径启动
		return 0;

	panic("No working init found.  Try passing init= option to kernel. "
	      "See Linux Documentation/admin-guide/init.rst for guidance."); // 没有指定 init 程序，kernel panic
}
```

我们先来看看`kernel_init_freeable` 函数

```c
kernel_init_freeable
| -- smp_prepare_cpus
| 	| -- native_smp_prepare_cpus // 初始化 smp cpu，会打印出 cpu 信息，这里很多不懂
|
| -- workqueue_init // bring workqueue subsystem fully online
| 					// 初始化 kworker 线程，下面有详细分析
| -- init_mm_internals
|
| -- rcu_init_tasks_generic
| -- do_pre_smp_initcalls
|
| -- smp_init // boot cpu activate the rest cpu
|			  // 通过 idle_threads_init 给每个辅核创建 0 号进程
| -- sched_init_smp
|	| -- sched_init_numa // 根据 NUMA 结构建立节点间的拓扑信息
|	| -- sched_init_domains // 节点内的拓扑信息，根调度域有关
|							// 所有的辅核都已经启动，内核进入并行状态
| -- padata_init // 下面这些函数之后有需要再分析
| -- page_alloc_init_late
| -- /* Initialize page ext after all struct pages are initialized. */
| -- page_ext_init
|
| -- do_basic_setup
|
| -- kunit_run_all_tests
|
| -- wait_for_initramfs
| -- console_on_rootfs
| -- prepare_namespace
|
| -- integrity_load_keys
\
```

接下来通过一系列的调用将 `kernel_init` 转换成用户态进程。

```plain
| -- run_init_process
|	| -- kernel_execve
|		| -- bprm_execve // 看不懂，之后再分析
```

```c
/*
 * sys_execve() executes a new program.
 */
static int bprm_execve(struct linux_binprm *bprm,
		       int fd, struct filename *filename, int flags)
{
	struct file *file;
	int retval;

	retval = prepare_bprm_creds(bprm);
	if (retval)
		return retval;

	check_unsafe_exec(bprm);
	current->in_execve = 1;

	file = do_open_execat(fd, filename, flags);
	retval = PTR_ERR(file);
	if (IS_ERR(file))
		goto out_unmark;

	sched_exec();

	bprm->file = file;
	/*
	 * Record that a name derived from an O_CLOEXEC fd will be
	 * inaccessible after exec.  This allows the code in exec to
	 * choose to fail when the executable is not mmaped into the
	 * interpreter and an open file descriptor is not passed to
	 * the interpreter.  This makes for a better user experience
	 * than having the interpreter start and then immediately fail
	 * when it finds the executable is inaccessible.
	 */
	if (bprm->fdpath && get_close_on_exec(fd))
		bprm->interp_flags |= BINPRM_FLAGS_PATH_INACCESSIBLE;

	/* Set the unchanging part of bprm->cred */
	retval = security_bprm_creds_for_exec(bprm);
	if (retval)
		goto out;

	retval = exec_binprm(bprm);
	if (retval < 0)
		goto out;

	/* execve succeeded */
	current->fs->in_exec = 0;
	current->in_execve = 0;
	rseq_execve(current);
	acct_update_integrals(current);
	task_numa_free(current, false);
	return retval;

out:
	/*
	 * If past the point of no return ensure the code never
	 * returns to the userspace process.  Use an existing fatal
	 * signal if present otherwise terminate the process with
	 * SIGSEGV.
	 */
	if (bprm->point_of_no_return && !fatal_signal_pending(current))
		force_sigsegv(SIGSEGV);

out_unmark:
	current->fs->in_exec = 0;
	current->in_execve = 0;

	return retval;
}
```

### workqueue

 workqueue 允许内核函数被激活，挂起，稍后由 worker thread 的特殊内核线程来执行。workqueue 中的函数运行在进程上下文中，

这部份涉及到几个关键的数据结构：

`workqueue_struct`，`worker_pool`，`pool_workqueue`，`work_struct`，`worker`，有必要把它们之间的关系搞懂。还有就是 runqueue 和 workqueue 有什么关系。runqueue 中放的是 process，用来作负载均衡的，而 workqueue 中放的是可以延迟执行的内核函数。

从代码中推测 `workqueue_struct` 表示一个工作队列，`pool_workqueue` 表示每 CPU 中的 work，`work_struct` 表示挂起的函数，`worker` 是执行挂起函数的内核线程，`worker_pool` 表示所有用来执行 work 的 worker。我们看看是怎样初始化 workqueue 的。

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
	for_each_online_cpu(cpu) {
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

这里涉及到一个非常重要的数据结构的初始化，我也是在之后的调试中才发现的。

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

### Reference

[1] https://0xax.gitbooks.io/linux-insides/content/Initialization/linux-initialization-8.html

[2] https://www.cnblogs.com/tolimit/p/4311404.html

[3] https://www.cnblogs.com/Oude/p/12588325.html
