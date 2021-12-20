## kthreadd

kthread 是 2 号内核进程（kernel thread），用来创建其他内核进程的。这篇文章我们来分析它是怎样创建新的内核进程以及涉及到有意思的地方。

这是创建 kthread 的位置。

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
	set_task_comm(tsk, "kthreadd");
	ignore_signals(tsk);
	set_cpus_allowed_ptr(tsk, housekeeping_cpumask(HK_FLAG_KTHREAD));
	set_mems_allowed(node_states[N_MEMORY]);

	current->flags |= PF_NOFREEZE;
	cgroup_init_kthreadd();

	for (;;) {
		set_current_state(TASK_INTERRUPTIBLE);
		if (list_empty(&kthread_create_list))
			schedule();
		__set_current_state(TASK_RUNNING);

		spin_lock(&kthread_create_lock);
		while (!list_empty(&kthread_create_list)) {
			struct kthread_create_info *create;

			create = list_entry(kthread_create_list.next,
					    struct kthread_create_info, list);
			list_del_init(&create->list);
			spin_unlock(&kthread_create_lock);

			create_kthread(create);

			spin_lock(&kthread_create_lock);
		}
		spin_unlock(&kthread_create_lock);
	}

	return 0;
}
```

有必要把 `kthreadd`，`create_thread`，`kernel_thread`，`kthread` 搞懂，太乱了。

```c
static void create_kthread(struct kthread_create_info *create)
{
	int pid;

#ifdef CONFIG_NUMA
	current->pref_node_fork = create->node;
#endif
	/* We want our own signal handler (we take no signals by default). */
	pid = kernel_thread(kthread, create, CLONE_FS | CLONE_FILES | SIGCHLD);
	if (pid < 0) {
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
