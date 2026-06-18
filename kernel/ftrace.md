## ftrace 原理与实现

从没了解过内核的 trace，没想到也这么复杂。对 trace 最初的认识来自于这张图，

![trace-tools.jpg](https://github.com/UtopianFuture/UtopianFuture.github.io/blob/master/image/trace-tools.jpg?raw=true)

搞不清楚种种 trace 工具的底层原理，这里从 ARM64 ftrace 开始分析。

ftrace 的实现很复杂，在分析的时候，只需要抓住它的核心设计本质：采用**静态代码插装技术**，在编译时向内核函数插入 mcount 函数，然后在内核初始化时将 mcount 函数替换成 nop 指令（动态），然后通过 tracefs 向用户态暴露一系列接口，达到按需使用的目的。

### ftrace 的静动态实现

ftrace 是 function trace 的简称，但是function trace只是ftrace里面的一个基本功能（还有哪些进阶功能？）。function trace是基于gcc的`-pg`参数插入`mcount`函数实现的，这部分依赖于处理器架构。Kernel中打开`CONFIG_FUNCTION_TRACER` 后，会增加`-pg`编译选项，这样在每个函数入口处都会插入`bl mcount`跳转指令，函数运行时会进入`mcount`函数。

```makefile
ccflags-remove-$(CONFIG_FUNCTION_TRACER) += -pg
```

mcount的主要任务是根据是否打开了ftrace相关的宏，在每个内核函数入口加入trace代码，跳转到对应的注册函数中，做进一步处理。但是，在每个内核函数入口加入trace代码，必然影响内核的性能（静态），为了减小对内核性能的影响，ftrace支持动态trace功能。下面来一一介绍这两种trace功能是如何实现的。

#### 静态mcount实现

插入到函数入口的 `mcount`会判断函数指针`ftrace_trace_function`是否被注册，默认注册的是空函数`ftrace_stub`，只有打开function tracer后才会注册具体的处理函数`ftrace_trace_function`。

```c
#ifndef CONFIG_DYNAMIC_FTRACE // 静态
/*
 * void _mcount(unsigned long return_address)
 * @return_address: return address to instrumented function
 *
 * This function makes calls, if enabled, to:
 *     - tracer function to probe instrumented function's entry,
 *     - ftrace_graph_caller to set up an exit hook
 */
SYM_FUNC_START(_mcount)
	mcount_enter

	ldr_l	x2, ftrace_trace_function // 后面分析
	adr	x0, ftrace_stub
	cmp	x0, x2			// if (ftrace_trace_function
	b.eq	skip_ftrace_call	//     != ftrace_stub) { 判断 ftrace_trace_function 是否是空函数

	mcount_get_pc	x0		//       function's pc
	mcount_get_lr	x1		//       function's lr (= parent's pc)
	blr	x2			//   (*ftrace_trace_function)(pc, lr);

skip_ftrace_call:			// }
#ifdef CONFIG_FUNCTION_GRAPH_TRACER // 两种特性，trace_function 和 graph_tracer
	ldr_l	x2, ftrace_graph_return
	cmp	x0, x2			//   if ((ftrace_graph_return
	b.ne	ftrace_graph_caller	//        != ftrace_stub)

	ldr_l	x2, ftrace_graph_entry	//     || (ftrace_graph_entry
	adr_l	x0, ftrace_graph_entry_stub //     != ftrace_graph_entry_stub))
	cmp	x0, x2
	b.ne	ftrace_graph_caller	//     ftrace_graph_caller();
#endif /* CONFIG_FUNCTION_GRAPH_TRACER */
	mcount_exit
SYM_FUNC_END(_mcount)
EXPORT_SYMBOL(_mcount)
NOKPROBE(_mcount)

#else /* CONFIG_DYNAMIC_FTRACE */
/*
 * _mcount() is used to build the kernel with -pg option, but all the branch
 * instructions to _mcount() are replaced to NOP initially at kernel start up,
 * and later on, NOP to branch to ftrace_caller() when enabled or branch to
 * NOP when disabled per-function base.
 */
SYM_FUNC_START(_mcount) // 动态
	ret
SYM_FUNC_END(_mcount)
EXPORT_SYMBOL(_mcount)
NOKPROBE(_mcount)
        
SYM_FUNC_START(ftrace_stub)
	ret
SYM_FUNC_END(ftrace_stub)
```

#### 动态 mcount 实现

static ftrace 一旦使能，对kernel中所有的函数(除开notrace、online、其他特殊函数)进行插桩，这带来的性能开销是惊人的，有可能导致人们弃用ftrace功能。

为了解决这个问题，社区推出了 dynamic ftrace，因为调用者一般不需要对所有函数进行追踪，只会对感兴趣的函数进行追踪。如果使能`CONFIG_DYNAMIC_FTRACE`，`mcount` 会被实现成一个空函数（只有一条 `ret` 指令）。在 `start_kernel` 中，调用 `ftrace_init` 函数，将 mcount 替换成 `nop` 指令。需要跟踪某个函数时，再通过 tracefs 打开 tracer，需要跟踪的函数的对应位置会被动态替换成跳转到 `ftrace_caller` 的指令。

这里涉及到两个指令替换，

- 将 mcount 替换成 `nop` 指令；
- 将 `nop` 指令替换成 `ftrace_caller` 指令；

先看第一步替换，

在编译的时候调用 `recordmcount.pl` 搜索所有静态 mcount 实现中的 `bl _mcount` 函数调用点，并且所有的调用点地址保存到 `section __mcount_loc`，

```c
#define MCOUNT_REC()	. = ALIGN(8);				\
			__start_mcount_loc = .;			\
			KEEP(*(__mcount_loc))			\
			KEEP(*(__patchable_function_entries))	\
			__stop_mcount_loc = .;			\
			ftrace_stub_graph = ftrace_stub;
```

在初始化时，遍历 `section __mcount_loc` 的调用点地址，默认给所有 `bl _mcount` 替换成 `nop`。

```c
| start_kernel
|	-> ftrace_init
|		-> ftrace_process_locs
|			-> ftrace_process_locs
|				-> ftrace_update_code
|					-> ftrace_update_code
|						-> ftrace_bug
|							-> ftrace_get_addr_curr
```

```c
void __init ftrace_init(void)
{
	extern unsigned long __start_mcount_loc[];
	extern unsigned long __stop_mcount_loc[];
	unsigned long count, flags;
	int ret;

	...

    // 计算 mcount 的个数
	count = __stop_mcount_loc - __start_mcount_loc;
    ...

	last_ftrace_enabled = ftrace_enabled = 1;

	ret = ftrace_process_locs(NULL,
				  __start_mcount_loc,
				  __stop_mcount_loc);

	pr_info("ftrace: allocated %ld pages with %ld groups\n",
		ftrace_number_of_pages, ftrace_number_of_groups); // 初始化时能看到这个打印

	set_ftrace_early_filters();

	return;
}
```

内核用 `struct ftrace_page` 和 `struct dyn_ftrace` 来跟踪每个 trace 点。

```c
struct ftrace_page {
	struct ftrace_page	*next;
	struct dyn_ftrace	*records;
	int			index;
	int			size;
};
```

```c
struct dyn_ftrace {
	unsigned long		ip; /* address of mcount call-site */
	unsigned long		flags;
	struct dyn_arch_ftrace	arch;
};·
```

```c
static int ftrace_process_locs(struct module *mod,
			       unsigned long *start,
			       unsigned long *end)
{
	struct ftrace_page *start_pg;
	struct ftrace_page *pg;
	struct dyn_ftrace *rec;
	unsigned long count;
	unsigned long *p;
	unsigned long addr;
	unsigned long flags = 0; /* Shut up gcc */
	int ret = -ENOMEM;

	count = end - start;

	sort(start, count, sizeof(*start),
	     ftrace_cmp_ips, NULL); // 为啥还要排序？

	start_pg = ftrace_allocate_pages(count); // 分配
	if (!start_pg)
		return -ENOMEM;

	mutex_lock(&ftrace_lock);

	/*
	 * Core and each module needs their own pages, as
	 * modules will free them when they are removed.
	 * Force a new page to be allocated for modules.
	 */
	if (!mod) {
		WARN_ON(ftrace_pages || ftrace_pages_start);
		/* First initialization */
		ftrace_pages = ftrace_pages_start = start_pg;
	} else {
		if (!ftrace_pages)
			goto out;

		if (WARN_ON(ftrace_pages->next)) {
			/* Hmm, we have free pages? */
			while (ftrace_pages->next)
				ftrace_pages = ftrace_pages->next;
		}

		ftrace_pages->next = start_pg;
	}

	p = start;
	pg = start_pg;
	while (p < end) {
		addr = ftrace_call_adjust(*p++);
		/*
		 * Some architecture linkers will pad between
		 * the different mcount_loc sections of different
		 * object files to satisfy alignments.
		 * Skip any NULL pointers.
		 */
		if (!addr)
			continue;

		if (pg->index == pg->size) {
			/* We should have allocated enough */
			if (WARN_ON(!pg->next))
				break;
			pg = pg->next;
		}

		rec = &pg->records[pg->index++];
		rec->ip = addr;
	}

	/* We should have used all pages */
	WARN_ON(pg->next);

	/* Assign the last page to ftrace_pages */
	ftrace_pages = pg;

	/*
	 * We only need to disable interrupts on start up
	 * because we are modifying code that an interrupt
	 * may execute, and the modification is not atomic.
	 * But for modules, nothing runs the code we modify
	 * until we are finished with it, and there's no
	 * reason to cause large interrupt latencies while we do it.
	 */
	if (!mod)
		local_irq_save(flags);
	ftrace_update_code(mod, start_pg);
	if (!mod)
		local_irq_restore(flags);
	ret = 0;
 out:
	mutex_unlock(&ftrace_lock);

	return ret;
}
```

```c
/*
 * void ftrace_caller(unsigned long return_address)
 * @return_address: return address to instrumented function
 *
 * This function is a counterpart of _mcount() in 'static' ftrace, and
 * makes calls to:
 *     - tracer function to probe instrumented function's entry,
 *     - ftrace_graph_caller to set up an exit hook
 */
SYM_FUNC_START(ftrace_caller)
	mcount_enter

	mcount_get_pc0	x0		//     function's pc
	mcount_get_lr	x1		//     function's lr

SYM_INNER_LABEL(ftrace_call, SYM_L_GLOBAL)	// tracer(pc, lr);
	nop				// This will be replaced with "bl xxx"
					// where xxx can be any kind of tracer.

#ifdef CONFIG_FUNCTION_GRAPH_TRACER
SYM_INNER_LABEL(ftrace_graph_call, SYM_L_GLOBAL) // ftrace_graph_caller();
	nop				// If enabled, this will be replaced
					// "b ftrace_graph_caller"
#endif

	mcount_exit
SYM_FUNC_END(ftrace_caller)
#endif /* CONFIG_DYNAMIC_FTRACE */
```

内核是这样替换的，

```c
// include/linux/ftrace.h
extern void ftrace_call(void);

void ftrace_modify_all_code(int command);

#ifndef FTRACE_ADDR
#define FTRACE_ADDR ((unsigned long)ftrace_caller)
#endif
```

```c
| start_kernel
|	-> ftrace_init
|		-> ftrace_process_locs
|			-> ftrace_process_locs
|				-> ftrace_update_code
|					-> ftrace_update_code
|						-> ftrace_bug
|							-> ftrace_get_addr_curr
```

```c
/**
 * ftrace_get_addr_curr - Get the call address that is already there
 * @rec:  The ftrace record descriptor
 *
 * The FTRACE_FL_REGS_EN is set when the record already points to
 * a function that saves all the regs. Basically the '_EN' version
 * represents the current state of the function.
 *
 * Returns the address of the trampoline that is currently being called
 */
unsigned long ftrace_get_addr_curr(struct dyn_ftrace *rec)
{
	struct ftrace_ops *ops;
	unsigned long addr;

	...

	if (rec->flags & FTRACE_FL_REGS_EN)
		return (unsigned long)FTRACE_REGS_ADDR;
	else
		return (unsigned long)FTRACE_ADDR; // 这个就是 ftrace_caller 的地址
}
```



### function tracer

#### tracer注册

#### tracer使能

##### function tracer的使能

##### function tracer的filter

##### function tracer的filter command

##### function tracer的数据存入

##### function tracer的数据读出

##### function tracer总结

### function graph tracer

#### function graph tracer注册

#### function graph tracer使能

##### function graph tracer的使能

##### function graph tracer的filter

##### function graph tracer的数据写入

##### function graph tracer的数据读出

##### function graph tracer总结

### irqsoff tracer

### Reference

[^1]: https://blog.linuxplumbersconf.org/2014/ocw/system/presentations/1773/original/ftrace-kernel-hooks-2014.pdf
[^2]: https://juejin.cn/post/7356942180387192886