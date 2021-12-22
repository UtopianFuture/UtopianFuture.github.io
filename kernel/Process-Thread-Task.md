## Process or Thread or Task

### 进程切换

#### switch_to 宏

这个宏的作用是切换内核态堆栈和硬件上下文。

我们先来看看这个宏的调用情况：

```plain
#0  __switch_to (prev_p=0xffff8881002645c0, next_p=0xffffffff82e1a940 <init_task>) at arch/x86/kernel/process_64.c:559
#1  0xffffffff81c140c4 in context_switch (rf=0xffffffff82e03b38, next=0xffff8881002645c0, prev=0xffffffff82e1a940 <init_task>, rq=0xffff888237c28f40) at kernel/sched/core.c:4940
#2  __schedule (sched_mode=sched_mode@entry=1) at kernel/sched/core.c:6287
#3  0xffffffff81c147a8 in preempt_schedule_common () at kernel/sched/core.c:6459
#4  0xffffffff81c147e2 in __cond_resched () at kernel/sched/core.c:8151
#5  0xffffffff812b8ee1 in alloc_vmap_area (size=size@entry=20480, align=align@entry=16384, vstart=vstart@entry=18446683600570023936, vend=vend@entry=18446718784942112767,
    node=node@entry=-1, gfp_mask=gfp_mask@entry=3520) at mm/vmalloc.c:1528
#6  0xffffffff812b982a in __get_vm_area_node (size=20480, size@entry=16384, align=align@entry=16384, flags=flags@entry=34, start=18446683600570023936, end=18446718784942112767,
    node=node@entry=-1, gfp_mask=3520, caller=0xffffffff810a20ad <kernel_clone+157>, shift=12) at mm/vmalloc.c:2423
#7  0xffffffff812bc784 in __vmalloc_node_range (size=size@entry=16384, align=align@entry=16384, start=<optimized out>, end=<optimized out>, gfp_mask=gfp_mask@entry=3520, prot=...,
    vm_flags=0, node=-1, caller=0xffffffff810a20ad <kernel_clone+157>) at mm/vmalloc.c:3010
#8  0xffffffff810a0cdc in alloc_thread_stack_node (tsk=0xffff888100265d00, tsk=0xffff888100265d00, node=-1) at kernel/fork.c:245
#9  dup_task_struct (node=-1, orig=0xffffffff82e1a940 <init_task>) at kernel/fork.c:887
#10 copy_process (pid=pid@entry=0x0 <fixed_percpu_data>, trace=trace@entry=0, node=node@entry=-1, args=args@entry=0xffffffff82e03e38) at kernel/fork.c:2026
#11 0xffffffff810a20ad in kernel_clone (args=args@entry=0xffffffff82e03e38) at kernel/fork.c:2584
#12 0xffffffff810a2705 in kernel_thread (fn=<optimized out>, arg=arg@entry=0x0 <fixed_percpu_data>, flags=flags@entry=1536) at kernel/fork.c:2636
#13 0xffffffff81c0b2b0 in rest_init () at init/main.c:711
#14 0xffffffff831b9f7c in arch_call_rest_init () at init/main.c:886
#15 0xffffffff831ba949 in start_kernel () at init/main.c:1141
#16 0xffffffff831b95a0 in x86_64_start_reservations (real_mode_data=real_mode_data@entry=0x2e3a920 <error: Cannot access memory at address 0x2e3a920>) at arch/x86/kernel/head64.c:525
#17 0xffffffff831b962d in x86_64_start_kernel (real_mode_data=0x2e3a920 <error: Cannot access memory at address 0x2e3a920>) at arch/x86/kernel/head64.c:506
#18 0xffffffff81000107 in secondary_startup_64 () at arch/x86/kernel/head_64.S:283
#19 0x0000000000000000 in ?? ()
```

`secondary_startup_64` 应该是辅核的启动过程，从这里可以清楚的看到之前我们分析的大部分内容。`start_kernel` 开始初始化，这里的初始化是 0 号进程做的，然后调用 `rest_init`，在这里创建 1 号线程（`kernel_init`），2 号线程 （`kthreadd`），而 `kernel_thread` 就是在 `kthreadd` 还未创建的时候创建内核线程的函数。之后 `kernel_clone` 复制父进程的进程空间到子进程。`copy_process` 创建进程描述符以及子进程执行所需要的所有其他数据结构。`dup_task_struct` 之后一系列函数只能从命名上推测其功能，但具体不知道是怎么工作的，`__schedule` 是进程调度，调度要进程切换 `context_switch`。

下面的代码比较复杂，这里只是做个记录，知道是在这里进行的进程切换即可，之后有需要再详细分析。

```c
#define switch_to(prev, next, last)					\
do {									\
	((last) = __switch_to_asm((prev), (next)));			\
} while (0)
```

```c
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
	movq	%rsp, TASK_threadsp(%rdi)
	movq	TASK_threadsp(%rsi), %rsp

#ifdef CONFIG_STACKPROTECTOR
	movq	TASK_stack_canary(%rsi), %rbx
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
```

```c
/*
 *	switch_to(x,y) should switch tasks from x to y.
 *
 * This could still be optimized:
 * - fold all the options into a flag word and test it with a single test.
 * - could test fs/gs bitsliced
 *
 * Kprobes not supported here. Set the probe on schedule instead.
 * Function graph tracer not supported too.
 */
__visible __notrace_funcgraph struct task_struct *
__switch_to(struct task_struct *prev_p, struct task_struct *next_p)
{
	struct thread_struct *prev = &prev_p->thread;
	struct thread_struct *next = &next_p->thread;
	struct fpu *prev_fpu = &prev->fpu;
	struct fpu *next_fpu = &next->fpu;
	int cpu = smp_processor_id();

	WARN_ON_ONCE(IS_ENABLED(CONFIG_DEBUG_ENTRY) &&
		     this_cpu_read(hardirq_stack_inuse));

	if (!test_thread_flag(TIF_NEED_FPU_LOAD))
		switch_fpu_prepare(prev_fpu, cpu);

	/* We must save %fs and %gs before load_TLS() because
	 * %fs and %gs may be cleared by load_TLS().
	 *
	 * (e.g. xen_load_tls())
	 */
	save_fsgs(prev_p);

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
	this_cpu_write(current_task, next_p);
	this_cpu_write(cpu_current_top_of_stack, task_top_of_stack(next_p));

	switch_fpu_finish(next_fpu);

	/* Reload sp0. */
	update_task_stack(next_p);

	switch_to_extra(prev_p, next_p);

	if (static_cpu_has_bug(X86_BUG_SYSRET_SS_ATTRS)) {
		/*
		 * AMD CPUs have a misfeature: SYSRET sets the SS selector but
		 * does not update the cached descriptor.  As a result, if we
		 * do SYSRET while SS is NULL, we'll end up in user mode with
		 * SS apparently equal to __USER_DS but actually unusable.
		 *
		 * The straightforward workaround would be to fix it up just
		 * before SYSRET, but that would slow down the system call
		 * fast paths.  Instead, we ensure that SS is never NULL in
		 * system call context.  We do this by replacing NULL SS
		 * selectors at every context switch.  SYSCALL sets up a valid
		 * SS, so the only way to get NULL is to re-enter the kernel
		 * from CPL 3 through an interrupt.  Since that can't happen
		 * in the same task as a running syscall, we are guaranteed to
		 * context switch between every interrupt vector entry and a
		 * subsequent SYSRET.
		 *
		 * We read SS first because SS reads are much faster than
		 * writes.  Out of caution, we force SS to __KERNEL_DS even if
		 * it previously had a different non-NULL value.
		 */
		unsigned short ss_sel;
		savesegment(ss, ss_sel);
		if (ss_sel != __KERNEL_DS)
			loadsegment(ss, __KERNEL_DS);
	}

	/* Load the Intel cache allocation PQR MSR. */
	resctrl_sched_in();

	return prev_p;
}
```
