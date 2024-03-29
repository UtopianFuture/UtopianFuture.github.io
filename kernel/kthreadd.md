## kthreadd

kthreadd 是 2 号内核进程（kernel thread），用来创建其他内核进程的。这篇文章我们来分析它是怎样创建新的内核进程以及涉及到有意思的地方。

这是创建 kthreadd 的位置，

```c
noinline void __ref rest_init(void)
{
	struct task_struct *tsk;
	int pid;

	...

	numa_default_policy();
	pid = kernel_thread(kthreadd, NULL, CLONE_FS | CLONE_FILES);
	rcu_read_lock();
	kthreadd_task = find_task_by_pid_ns(pid, &init_pid_ns);
	rcu_read_unlock();

	...
}
```

这是 kthread 的代码，不长，我们来分析一下。

```c
int kthreadd(void *unused)
{
	struct task_struct *tsk = current;

	/* Setup a clean context for our children to inherit. */
	set_task_comm(tsk, "kthreadd");z
	ignore_signals(tsk);
	set_cpus_allowed_ptr(tsk, housekeeping_cpumask(HK_FLAG_KTHREAD));
	set_mems_allowed(node_states[N_MEMORY]);

	current->flags |= PF_NOFREEZE;
	cgroup_init_kthreadd();

	for (;;) { // 这个循环就是进程调度的主循环
		set_current_state(TASK_INTERRUPTIBLE);
		if (list_empty(&kthread_create_list)) // 如果没有需要创建的线程，则调度已有的线程执行
			schedule();
		__set_current_state(TASK_RUNNING);

		spin_lock(&kthread_create_lock);
		while (!list_empty(&kthread_create_list)) { // 有需要创建的线程
			struct kthread_create_info *create;

			create = list_entry(kthread_create_list.next, // 将需要创建的线程加入到链表中
					    struct kthread_create_info, list);
			list_del_init(&create->list);
			spin_unlock(&kthread_create_lock);

			create_kthread(create); // 创建线程

			spin_lock(&kthread_create_lock);
		}
		spin_unlock(&kthread_create_lock);
	}

	return 0;
}
```

有必要把 `kthreadd`，`create_thread`，`kernel_thread`，`kthread` 搞懂，太乱了。

这个函数很简单，就是调用 `kernel_thread`，返回 pid 即进程号。

```c
static void create_kthread(struct kthread_create_info *create)
{
	int pid;

#ifdef CONFIG_NUMA
	current->pref_node_fork = create->node;
#endif
	/* We want our own signal handler (we take no signals by default). */
	pid = kernel_thread(kthread, create, CLONE_FS | CLONE_FILES | SIGCHLD);
	if (pid < 0) { // 这应该是创建失败了
		/* If user was SIGKILLed, I release the structure. */
		struct completion *done = xchg(&create->done, NULL);

		if (!done) {
			kfree(create);
			return;
		}
		create->result = ERR_PTR(pid);
		complete(done);
	}
}
```

这几个函数调用都是为了完成创建内核线程这一件事，用语言很难单独描述它们，直接看代码比较清晰，代码都不难。

`kernel_thread` 好像是比较老的接口？创建 `kernel_init` 和 `kthreadd` 线程也是这个函数。

```c
/*
 * Create a kernel thread.
 */
pid_t kernel_thread(int (*fn)(void *), void *arg, unsigned long flags)
{
	struct kernel_clone_args args = {
		.flags		= ((lower_32_bits(flags) | CLONE_VM |
				    CLONE_UNTRACED) & ~CSIGNAL),
		.exit_signal	= (lower_32_bits(flags) & CSIGNAL),
		.stack		= (unsigned long)fn, // 我在某篇 blog 中看到说创建内核线程就是将代码复制到内核空间
		.stack_size	= (unsigned long)arg, // 这是不是就是将 kthread 放到内核栈中
	};

	return kernel_clone(&args);
}
```

fork 系统调用也是使用 `kernel_clone` 来创建进程，晚点再分析。

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

	/*
	 * For legacy clone() calls, CLONE_PIDFD uses the parent_tid argument
	 * to return the pidfd. Hence, CLONE_PIDFD and CLONE_PARENT_SETTID are
	 * mutually exclusive. With clone3() CLONE_PIDFD has grown a separate
	 * field in struct clone_args and it still doesn't make sense to have
	 * them both point at the same memory location. Performing this check
	 * here has the advantage that we don't need to have a separate helper
	 * to check for legacy clone().
	 */
	if ((args->flags & CLONE_PIDFD) &&
	    (args->flags & CLONE_PARENT_SETTID) &&
	    (args->pidfd == args->parent_tid))
		return -EINVAL;

	/*
	 * Determine whether and which event to report to ptracer.  When
	 * called from kernel_thread or CLONE_UNTRACED is explicitly
	 * requested, no event is reported; otherwise, report if the event
	 * for the type of forking is enabled.
	 */
	if (!(clone_flags & CLONE_UNTRACED)) {
		if (clone_flags & CLONE_VFORK)
			trace = PTRACE_EVENT_VFORK;
		else if (args->exit_signal != SIGCHLD)
			trace = PTRACE_EVENT_CLONE;
		else
			trace = PTRACE_EVENT_FORK;

		if (likely(!ptrace_event_enabled(current, trace)))
			trace = 0;
	}

	p = copy_process(NULL, trace, NUMA_NO_NODE, args);
	add_latent_entropy();

	if (IS_ERR(p))
		return PTR_ERR(p);

	/*
	 * Do this prior waking up the new thread - the thread pointer
	 * might get invalid after that point, if the thread exits quickly.
	 */
	trace_sched_process_fork(current, p);

	pid = get_task_pid(p, PIDTYPE_PID);
	nr = pid_vnr(pid);

	if (clone_flags & CLONE_PARENT_SETTID)
		put_user(nr, args->parent_tid);

	if (clone_flags & CLONE_VFORK) {
		p->vfork_done = &vfork;
		init_completion(&vfork);
		get_task_struct(p);
	}

	wake_up_new_task(p);

	/* forking complete and child started to run, tell ptracer */
	if (unlikely(trace))
		ptrace_event_pid(trace, pid);

	if (clone_flags & CLONE_VFORK) {
		if (!wait_for_vfork_done(p, &vfork))
			ptrace_event_pid(PTRACE_EVENT_VFORK_DONE, pid);
	}

	put_pid(pid);
	return nr;
}
```

`kthread` 应该就表示要创建的内核线程。在 `kernel_clone` 创建完后就会回到 `kthreadd` 中继续执行，然后在 `if (list_empty(&kthread_create_list))` 中判断，由于需要创建的内核线程已经创建完毕，所以这时候可以使用 `schedule` 调度了。这时候才会执行 `kthread` 。有个问题，在 `kernel_thread` 中只是指明了需要创建线程的地址，即 `kthread`，但并没有给它传入实参，那它的实参哪里来的呢？这个其实还是 `kernel_thread` 完成的。

```c
	struct kernel_clone_args args = {
		.flags		= ((lower_32_bits(flags) | CLONE_VM |
				    CLONE_UNTRACED) & ~CSIGNAL),
		.exit_signal	= (lower_32_bits(flags) & CSIGNAL),
		.stack		= (unsigned long)fn,
		.stack_size	= (unsigned long)arg,
	};
```

`arg` 就是要传入的参数，`kthread_clone` 会将 `arg` 的地址传给 `kthread`。

现在来分析一下 `kthread` 到底做了些什么。发现还是太复杂了，分析不了，会涉及到进程调度。那这个先跳过，我们看看 `worker_thread` 是怎样处理的。

```c
static int kthread(void *_create)
{
	/* Copy data: it's on kthread's stack */
	struct kthread_create_info *create = _create;
	int (*threadfn)(void *data) = create->threadfn;
	void *data = create->data;
	struct completion *done;
	struct kthread *self;
	int ret;

	set_kthread_struct(current);
	self = to_kthread(current);

	/* If user was SIGKILLed, I release the structure. */
	done = xchg(&create->done, NULL);
	if (!done) {
		kfree(create);
		do_exit(-EINTR);
	}

	if (!self) {
		create->result = ERR_PTR(-ENOMEM);
		complete(done);
		do_exit(-ENOMEM);
	}

	self->threadfn = threadfn;
	self->data = data;
	init_completion(&self->exited);
	init_completion(&self->parked);
	current->vfork_done = &self->exited;

	/* OK, tell user we're spawned, wait for stop or wakeup */
	__set_current_state(TASK_UNINTERRUPTIBLE);
	create->result = current;
	/*
	 * Thread is going to call schedule(), do not preempt it,
	 * or the creator may spend more time in wait_task_inactive().
	 */
	preempt_disable();
	complete(done);
	schedule_preempt_disabled();
	preempt_enable();

	ret = -EINTR;
	if (!test_bit(KTHREAD_SHOULD_STOP, &self->flags)) {
		cgroup_kthread_ready();
		__kthread_parkme(self);
		ret = threadfn(data);
	}
	do_exit(ret);
}
```

发现一个也是创建线程的宏 `kthread_create`，类似的命名，类似的功能，这个会返回创建线程的 `task_struct`，而 `create_kthread` 返回创建线程的 pid，要搞清楚它们之间的区别。

### kthread_create 和 create_kthread

`kthread_create` 是一个宏，最后会调用到这个函数，这看起来就是一个正常的创建线程的函数，有 kmalloc，首先其创建方法就和 `create_kthread` 不一样。

```c
struct task_struct *__kthread_create_on_node(int (*threadfn)(void *data),
						    void *data, int node,
						    const char namefmt[],
						    va_list args)
{
	DECLARE_COMPLETION_ONSTACK(done);
	struct task_struct *task;
	struct kthread_create_info *create = kmalloc(sizeof(*create),
						     GFP_KERNEL);

	if (!create)
		return ERR_PTR(-ENOMEM);
	create->threadfn = threadfn;
	create->data = data;
	create->node = node;
	create->done = &done;

	spin_lock(&kthread_create_lock);
	list_add_tail(&create->list, &kthread_create_list); // 每个创建的 task 都要将 create 添加到
    													// kthread_create_list，之后 task 执行会用到
	spin_unlock(&kthread_create_lock);

	wake_up_process(kthreadd_task);

    ...

	task = create->result;
	if (!IS_ERR(task)) {
		static const struct sched_param param = { .sched_priority = 0 };
		char name[TASK_COMM_LEN];

		/*
		 * task is already visible to other tasks, so updating
		 * COMM must be protected.
		 */
		vsnprintf(name, sizeof(name), namefmt, args);
		set_task_comm(task, name);
		/*
		 * root may have changed our (kthreadd's) priority or CPU mask.
		 * The kernel thread should not inherit these properties.
		 */
		sched_setscheduler_nocheck(task, SCHED_NORMAL, &param);
		set_cpus_allowed_ptr(task,
				     housekeeping_cpumask(HK_FLAG_KTHREAD));
	}
	kfree(create);
	return task;
}
```

在两个函数（宏）设置断点，看看调用情况即可。

```plain
guanshun@guanshun-ubuntu ~> ps -eo pid,ppid,command
    PID    PPID COMMAND
      1       0 /sbin/init splash
      2       0 [kthreadd]
      3       2 [rcu_gp]
      4       2 [rcu_par_gp]
      6       2 [kworker/0:0H-kblockd]
      9       2 [mm_percpu_wq]
```

3(rcu_gp)，4(rcu_par_gp)，5, 6, 7(kworker), 8(mm_percpu_wq), 9(rcu_tasks_rude_), 10(rcu_tasks_trace) 等等内核线程都是 `kthread_create` 创建的。

```plain
(gdb) p task->pid
$24 = 17
(gdb) bt
#0  __kthread_create_on_node (threadfn=threadfn@entry=0xffffffff81c0ef75 <devtmpfsd>, data=data@entry=0xffffc90000013e48, node=node@entry=-1,
    namefmt=namefmt@entry=0xffffffff8264a6bb "kdevtmpfs", args=args@entry=0xffffc90000013de8) at kernel/kthread.c:413
#1  0xffffffff810cbb19 in kthread_create_on_node (threadfn=threadfn@entry=0xffffffff81c0ef75 <devtmpfsd>, data=data@entry=0xffffc90000013e48, node=node@entry=-1,
    namefmt=namefmt@entry=0xffffffff8264a6bb "kdevtmpfs") at kernel/kthread.c:453
#2  0xffffffff8321c117 in devtmpfs_init () at drivers/base/devtmpfs.c:463
#3  0xffffffff8321be53 in driver_init () at drivers/base/init.c:23
#4  0xffffffff831bab25 in do_basic_setup () at init/main.c:1408
#5  kernel_init_freeable () at init/main.c:1614
#6  0xffffffff81c0b31a in kernel_init (unused=<optimized out>) at init/main.c:1505
#7  0xffffffff81004572 in ret_from_fork () at arch/x86/entry/entry_64.S:295
#8  0x0000000000000000 in ?? ()
```

然后 `create_kthread` 只有开头执行了一次就没有执行过了。

初始化先分析到这里，之后有时间再进一步分析。
