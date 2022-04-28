## Kernel_init

这篇文章我们来分析一下内核启动的第二阶段，第一阶段的分析[这里](https://github.com/UtopianFuture/UtopianFuture.github.io/blob/master/virtualization/bmbt-virtualization.md)。

这里有个事情应该说明一下，第一阶段的分析是基于 loongarch 的，这里是基于 x86 内核的，初始化过程略有不同，下面是 x86 内核调用 `start_kernel` 的过程。

```plain
#0  start_kernel () at init/main.c:931
#1  0xffffffff831b95a0 in x86_64_start_reservations (real_mode_data=real_mode_data@entry=0x2e3a920 <error: Cannot access memory at address 0x2e3a920>) at arch/x86/kernel/head64.c:525
#2  0xffffffff831b962d in x86_64_start_kernel (real_mode_data=0x2e3a920 <error: Cannot access memory at address 0x2e3a920>) at arch/x86/kernel/head64.c:506
#3  0xffffffff81000107 in secondary_startup_64 () at arch/x86/kernel/head_64.S:283
#4  0x0000000000000000 in ?? ()
```

内核启动的入口函数不是 `kernel_entry`，而是 `secondary_startup_64`，我一直以为这个辅核的启动过程，这里做个记录。

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

### do_execve

接下来通过 `execve` 系统调用来装入 `init` 程序，之后 `kernel_init` 就变成用户态进程了。

```plain
| -- run_init_process
|	| -- do_execve
|		| -- do_execveat_common
```

这里仔细分析一下 `do_execve` 是怎样加载用户态程序的。

##### linux_binprm

内核使用 `linux_binprm` 结构体表示一个可执行文件。

```c
/*
 * This structure is used to hold the arguments that are used when loading binaries.
 */
struct linux_binprm {
	char buf[BINPRM_BUF_SIZE]; // 保存可执行文件的前 128 个字节
#ifdef CONFIG_MMU
	struct vm_area_struct *vma; // 哈，这个就很熟悉了
	unsigned long vma_pages;
#else
# define MAX_ARG_PAGES	32
	struct page *page[MAX_ARG_PAGES];
#endif
	struct mm_struct *mm; // 进程的地址空间描述符
	unsigned long p; /* current top of mem */ // 当前内存页最高地址（？）
	unsigned int
		cred_prepared:1,/* true if creds already prepared (multiple
				 * preps happen for interpreters) */
		cap_effective:1;/* true if has elevated effective capabilities,
				 * false if not; except for init which inherits
				 * its parent's caps anyway */

	unsigned int recursion_depth; /* only for search_binary_handler() */
	struct file * file; // 要执行的文件
	struct cred *cred;	/* new credentials */
	int unsafe;		/* how unsafe this exec is (mask of LSM_UNSAFE_*) */
	unsigned int per_clear;	/* bits to clear in current->personality */
	int argc, envc; // 命令行参数和环境变量数目
	const char * filename;	/* Name of binary as seen by procps */
	const char * interp;	/* Name of the binary really executed. Most
				   of the time same as filename, but could be
				   different for binfmt_{misc,script} */
	unsigned interp_flags;
	unsigned interp_data;
	unsigned long loader, exec;
};
```

##### linux_binfmt

内核对所支持的每种可执行的程序类型都用 `linux_binfmt` 表示，

```c
/*
 * This structure defines the functions that are used to load the binary formats that
 * linux accepts.
 */
struct linux_binfmt {
	struct list_head lh;
	struct module *module;
	int (*load_binary)(struct linux_binprm *);
	int (*load_shlib)(struct file *);
	int (*core_dump)(struct coredump_params *cprm);
	unsigned long min_coredump;	/* minimal dump size */
};
```

并提供了 3 种方法来加载和执行可执行程序：

- load_binary

通过读存放在可执行文件中的信息为当前进程建立一个新的执行环境；

- load_shlib

用于动态的把一个共享库捆绑到一个已经在运行的进程，这是由 `uselib` 系统调用激活的；

- core_dump

在名为 core 的文件中，存放当前进程的执行上下文。这个文件通常是在进程接收到一个缺省操作为”dump”的信号时被创建的，其格式取决于被执行程序的可执行类型；

所有的 `linux_binfmt` 对象都处于一个链表中，可以通过调用 `register_binfmt` 和 `unregister_binfmt` 函数在链表中插入和删除元素。在系统启动期间，为每个编译进内核的可执行文件都执行 `registre_fmt` 函数。当实现了一个新的可执行格式的模块正被装载时，也执行这个函数，当模块被卸载时，执行`unregister_binfmt` 函数。

#### 关键函数do_execveat_common

```c
/*
 * sys_execve() executes a new program.
 */
static int do_execveat_common(int fd, struct filename *filename,
			      struct user_arg_ptr argv,
			      struct user_arg_ptr envp,
			      int flags)
{
	char *pathbuf = NULL;
	struct linux_binprm *bprm;
	struct file *file;
	struct files_struct *displaced;
	int retval;

	...

	/* We're below the limit (still or again), so we don't want to make
	 * further execve() calls fail. */
	current->flags &= ~PF_NPROC_EXCEEDED;

	retval = unshare_files(&displaced); // 为进程复制一份文件表

	retval = -ENOMEM;
	bprm = kzalloc(sizeof(*bprm), GFP_KERNEL); // 分配一个 linux_binprm 结构体

	retval = prepare_bprm_creds(bprm);

	check_unsafe_exec(bprm);
	current->in_execve = 1;

	file = do_open_execat(fd, filename, flags); // 查找并打开二进制文件
	retval = PTR_ERR(file);

	sched_exec(); // 找到最小负载的CPU，用来执行该二进制文件

    // 配置 linux_binprm 结构体中的 file、filename、interp 成员
	bprm->file = file;
	if (fd == AT_FDCWD || filename->name[0] == '/') {
		bprm->filename = filename->name;
	} else {
		if (filename->name[0] == '\0')
			pathbuf = kasprintf(GFP_TEMPORARY, "/dev/fd/%d", fd);
		else
			pathbuf = kasprintf(GFP_TEMPORARY, "/dev/fd/%d/%s",
					    fd, filename->name);
		if (!pathbuf) {
			retval = -ENOMEM;
			goto out_unmark;
		}
		/*
		 * Record that a name derived from an O_CLOEXEC fd will be
		 * inaccessible after exec. Relies on having exclusive access to
		 * current->files (due to unshare_files above).
		 */
		if (close_on_exec(fd, rcu_dereference_raw(current->files->fdt)))
			bprm->interp_flags |= BINPRM_FLAGS_PATH_INACCESSIBLE;
		bprm->filename = pathbuf;
	}
	bprm->interp = bprm->filename;

    // 创建进程的内存地址空间，为新程序初始化内存管理，并调用 init_new_context
    // 检查当前进程是否使用自定义的局部描述符表；如果是，那么分配和准备一个新的 LDT
	retval = bprm_mm_init(bprm);

    // 配置 argc、envc 成员
	bprm->argc = count(argv, MAX_ARG_STRINGS);

	bprm->envc = count(envp, MAX_ARG_STRINGS);

    // 检查该二进制文件的可执行权限
	retval = prepare_binprm(bprm);

    // 从内核空间获取二进制文件的路径名称
	retval = copy_strings_kernel(1, &bprm->filename, bprm);

    // 从用户空间拷贝环境变量和命令行参数
	bprm->exec = bprm->p;
	retval = copy_strings(bprm->envc, envp, bprm);

	retval = copy_strings(bprm->argc, argv, bprm);

	would_dump(bprm, bprm->file);

    // 至此，二进制文件已经被打开，linux_binprm 中也记录了重要信息
    // 内核开始调用 exec_binprm 执行可执行程序
	retval = exec_binprm(bprm);

	/* execve succeeded */
	current->fs->in_exec = 0;
	current->in_execve = 0;
	acct_update_integrals(current);
	task_numa_free(current);
	free_bprm(bprm);
	kfree(pathbuf);
	putname(filename);
	if (displaced)
		put_files_struct(displaced);
	return retval;

	...
}
```

内核中实际执行 `execv` 或 `execve` 系统调用的程序是 `do_execve`，这个函数先打开目标映像文件，读取目标文件的 ELF 头，然后调用另一个函数 `search_binary_handler`，在此函数里面，它会搜索我们上面提到的内核支持的可执行文件类型队列，让各种可执行程序的处理程序前来认领和处理。如果类型匹配，则调用 `load_binary` 函数指针所指向的处理函数来处理目标映像文件。在ELF文件格式中，处理函数是 `load_elf_binary` 函数，下面主要就是分析 `load_elf_binary` 函数的执行过程。

#### 关键函数exec_binprm

该函数用来执行可执行文件，不过它的主要任务还是调用 `search_binary_handler`。

```c
static int exec_binprm(struct linux_binprm *bprm)
{
	pid_t old_pid, old_vpid;
	int ret;

	/* Need to fetch pid before load_binary changes it */
	old_pid = current->pid;
	rcu_read_lock();
	old_vpid = task_pid_nr_ns(current, task_active_pid_ns(current->parent));
	rcu_read_unlock();

	ret = search_binary_handler(bprm);
	if (ret >= 0) {
		audit_bprm(bprm);
		trace_sched_process_exec(current, old_pid, bprm);
		ptrace_event(PTRACE_EVENT_EXEC, old_vpid);
		proc_exec_connector(current);
	}

	return ret;
}
```

### Reference

[1] https://0xax.gitbooks.io/linux-insides/content/Initialization/linux-initialization-8.html

[2] https://www.cnblogs.com/tolimit/p/4311404.html

[3] https://www.cnblogs.com/Oude/p/12588325.html

[4] https://blog.csdn.net/gatieme/article/details/51594439
