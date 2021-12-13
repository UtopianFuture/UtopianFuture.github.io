## Signal

信号是用于通知进程特定事件的发生的一个整数。

与信号有关的数据结构

```c
struct task_struct {
	/* Signal handlers: */
	struct signal_struct		*signal; // 指向进程的信号描述符指针
	struct sighand_struct __rcu		*sighand; // 进程的信号处理程序
	sigset_t			blocked; // 被阻塞信号的掩码
	sigset_t			real_blocked; // 被阻塞信号的临时掩码
	/* Restored if set_restore_sigmask() was used: */
	sigset_t			saved_sigmask;
	struct sigpending		pending; // 私有挂起信号
	unsigned long			sas_ss_sp; // 信号处理程序备用栈地址
	size_t				sas_ss_size; // xxx大小
	unsigned int			sas_ss_flags;
	...
}
```

```c
struct signal_struct {
	refcount_t		sigcnt;

	...

	/* shared signal handling: */
	struct sigpending	shared_pending;

	...

} __randomize_layout;
```

```c
struct sighand_struct {
	spinlock_t		siglock;
	refcount_t		count;
	wait_queue_head_t	signalfd_wqh;
	struct k_sigaction	action[_NSIG];
};
```

它们之间的关系如下图：

![image-20211213220235601](/home/guanshun/.config/Typora/typora-user-images/image-20211213220235601.png)



### Reference

[1] https://wushifublog.com/2020/05/16/%E6%B7%B1%E5%85%A5%E7%90%86%E8%A7%A3Linux%E5%86%85%E6%A0%B8%E2%80%94%E2%80%94signals/

[2] 深入理解 LINUX 内核
