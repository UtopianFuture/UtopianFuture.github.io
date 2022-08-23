## Interrupt Management

之前分析过键盘敲下一个字符到显示在显示器上的经过，[那篇](./kernel/From-keyboard-to-Display.md)文章也涉及到中断的处理，我本以为中断也就那样，但我还是太天真了，面试过程中很多关于中断的问题答的不好，所以再系统的学习一下。

### 数据结构

还是老规矩，先看看关键的数据结构，

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

因为现在的 SoC 内部包含多个中断控制器，并且每个中断控制器管理的中断源很多，为了更好的管理如此复杂的中断设备，内核引入了 `irq_domain` 管理框架。

一个 `irq_domain` 就表示一个中断控制器，每个中断控制器管理多个中断源，其可以按照树或数组的方式管理 `irq_desc`，这个是中断管理的核心，中断处理都是根据 irq 找到对应的 `irq_desc` 之后的事情就好办了。

#### irq_desc

```c
/**
 * struct irq_desc - interrupt descriptor
 */
struct irq_desc {
	struct irq_common_data	irq_common_data;
	struct irq_data		irq_data;
	unsigned int __percpu	*kstat_irqs;
	irq_flow_handler_t	handle_irq;
	struct irqaction	*action;	/* IRQ action list */ // 多个设备共享一个描述符，需要遍历所有的 irqaction
	unsigned int		status_use_accessors;
	unsigned int		core_internal_state__do_not_mess_with_it;
	unsigned int		depth;		/* nested irq disables */
	unsigned int		wake_depth;	/* nested wake enables */
	unsigned int		tot_count;
	unsigned int		irq_count;	/* For detecting broken IRQs */
	unsigned long		last_unhandled;	/* Aging timer for unhandled count */
	unsigned int		irqs_unhandled;
	atomic_t		threads_handled;
	int			threads_handled_last;
	raw_spinlock_t		lock;
	struct cpumask		*percpu_enabled;
	const struct cpumask	*percpu_affinity;
#ifdef CONFIG_SMP
	const struct cpumask	*affinity_hint;
	struct irq_affinity_notify *affinity_notify;
#ifdef CONFIG_GENERIC_PENDING_IRQ
	cpumask_var_t		pending_mask;
#endif
#endif
	unsigned long		threads_oneshot;
	atomic_t		threads_active;
	wait_queue_head_t       wait_for_threads;
#ifdef CONFIG_PM_SLEEP
	unsigned int		nr_actions;
	unsigned int		no_suspend_depth;
	unsigned int		cond_suspend_depth;
	unsigned int		force_resume_depth;
#endif
#ifdef CONFIG_PROC_FS
	struct proc_dir_entry	*dir;
#endif
#ifdef CONFIG_GENERIC_IRQ_DEBUGFS
	struct dentry		*debugfs_file;
	const char		*dev_name;
#endif
#ifdef CONFIG_SPARSE_IRQ
	struct rcu_head		rcu;
	struct kobject		kobj;
#endif
	struct mutex		request_mutex;
	int			parent_irq;
	struct module		*owner;
	const char		*name;
} ____cacheline_internodealigned_in_smp;
```

#### irq_domain

```c
struct irq_domain {
	struct list_head link; // 常规操作，所有的 irq_domain 连接到全局链表上
	const char *name;
	const struct irq_domain_ops *ops; // 这个应该是不同的控制器操作函数不同
	void *host_data;
	unsigned int flags;
	unsigned int mapcount; // 管理的中断源数量

	/* Optional data */
	struct fwnode_handle *fwnode; // 从 DTS 或 ACPI 获取的
	enum irq_domain_bus_token bus_token;
	struct irq_domain_chip_generic *gc;
#ifdef	CONFIG_IRQ_DOMAIN_HIERARCHY
	struct irq_domain *parent; // 这个应该就是多个中断控制器级联成树状结构
#endif

	/* reverse map data. The linear map gets appended to the irq_domain */
	irq_hw_number_t hwirq_max; // 最大支持的中断数量
	unsigned int revmap_size; // 线性映射的大小
	struct radix_tree_root revmap_tree; // 基数树映射的根节点
	struct mutex revmap_mutex;
	struct irq_data __rcu *revmap[]; // 线性映射用到的查找表
};
```

#### 关键函数 __irq_domain_add

内核中使用 `__irq_domain_add` 来初始化一个 `irq_domain`，

```c
struct irq_domain *__irq_domain_add(struct fwnode_handle *fwnode, unsigned int size,
				    irq_hw_number_t hwirq_max, int direct_max,
				    const struct irq_domain_ops *ops,
				    void *host_data)
{
	struct irqchip_fwid *fwid;
	struct irq_domain *domain;

	static atomic_t unknown_domains;

	...

  	// 看过前面的内存管理这个就很好理解了
	domain = kzalloc_node(struct_size(domain, revmap, size),
			      GFP_KERNEL, of_node_to_nid(to_of_node(fwnode)));

    // 一直不理解这个 fwnode 到底是啥，其实它表示的是 DTS 或 ACPI 的一个 entry
    // 这里应该是解析这个 entry
	if (is_fwnode_irqchip(fwnode)) {
		fwid = container_of(fwnode, struct irqchip_fwid, fwnode);

		switch (fwid->type) {
		case IRQCHIP_FWNODE_NAMED:
		case IRQCHIP_FWNODE_NAMED_ID:
			domain->fwnode = fwnode;
			domain->name = kstrdup(fwid->name, GFP_KERNEL);
			if (!domain->name) {
				kfree(domain);
				return NULL;
			}
			domain->flags |= IRQ_DOMAIN_NAME_ALLOCATED;
			break;
		default:
			domain->fwnode = fwnode;
			domain->name = fwid->name;
			break;
		}
	} else if (is_of_node(fwnode) || is_acpi_device_node(fwnode) ||
		   is_software_node(fwnode)) {

		... // 这两个差不多

	}

	...

	fwnode_handle_get(fwnode);
	fwnode_dev_initialized(fwnode, true);

	/* Fill structure */
	INIT_RADIX_TREE(&domain->revmap_tree, GFP_KERNEL); // irq_desc 的组织形式
	mutex_init(&domain->revmap_mutex);
	domain->ops = ops;
	domain->host_data = host_data;
	domain->hwirq_max = hwirq_max;

	if (direct_max) {
		size = direct_max;
		domain->flags |= IRQ_DOMAIN_FLAG_NO_MAP;
	}

	domain->revmap_size = size;

	irq_domain_check_hierarchy(domain);

	mutex_lock(&irq_domain_mutex);
	debugfs_add_domain_dir(domain);
	list_add(&domain->link, &irq_domain_list); // 最后要将所有的 irq_domain 插入链表
	mutex_unlock(&irq_domain_mutex);

	pr_debug("Added domain %s\n", domain->name);
	return domain;
}
```

通过调试内核可知，在 X86 的机器上，有如下中断控制器会调用 `__irq_domain_add` 进行初始化：

- `x86_vector_domain` 不清楚这个是干什么的，其调用路径为 `start_kernel` -> `early_irq_init` -> `arch_early_irq_init` -> `irq_domain_create_tree` ->  `__irq_domain_add`。貌似它是所有 `irq_domain` 的 parent。
- APIC，其调用路径为 `start_kernel` -> `x86_late_time_init` -> `apic_intr_mode_init` -> `apic_bsp_setup` -> `setup_IO_APIC` -> `mp_irqdomain_create` -> `irq_domain_create_linear` -> `__irq_domain_add`，这个应该就是 APIC 的初始化，但很奇怪，跟 `time_init` 有什么关系呢？
- pci, `x86_create_pci_msi_domain`；
- hpet, `hpet_create_irq_domain`；

虽然创建了 `irq_domain`，但还没有建立 hwirq 和 irq 的映射，`__irq_domain_alloc_irqs` 用来建立 hwirq 和 irq 之间的映射，其会调用 `irq_domain_alloc_descs`，

```c
int irq_domain_alloc_descs(int virq, unsigned int cnt, irq_hw_number_t hwirq,
			   int node, const struct irq_affinity_desc *affinity)
{
	unsigned int hint;

	if (virq >= 0) {
		virq = __irq_alloc_descs(virq, virq, cnt, node, THIS_MODULE,
					 affinity);
	} else {
		hint = hwirq % nr_irqs;
		if (hint == 0)
			hint++;
		virq = __irq_alloc_descs(-1, hint, cnt, node, THIS_MODULE,
					 affinity);
		if (virq <= 0 && hint > 1) {
			virq = __irq_alloc_descs(-1, 1, cnt, node, THIS_MODULE,
						 affinity);
		}
	}

	return virq;
}
```

最后将 hwirq 和 irq 都写入 `irq_data` 中即完成了中断映射。

我去，怎么 X86 的内核不是这样的！在用 GDB 实际调试内核时发现并不会调用 `__irq_domain_alloc_irqs` 来建立映射，而是通过下面这个方式，

```c
#0  bitmap_find_next_zero_area_off (map=map@entry=0xffff88810007dd30, size=size@entry=236,
    start=start@entry=32, nr=nr@entry=1, align_mask=align_mask@entry=0,
    align_offset=align_offset@entry=0) at lib/bitmap.c:412
#1  0xffffffff8112a302 in bitmap_find_next_zero_area (align_mask=0, nr=1, start=32,
    size=236, map=0xffff88810007dd30) at ./include/linux/bitmap.h:197
#2  matrix_alloc_area (m=m@entry=0xffff88810007dd00, cm=cm@entry=0xffff888237caef00,
    managed=managed@entry=false, num=1) at kernel/irq/matrix.c:118
#3  0xffffffff8112ab78 in irq_matrix_alloc (m=0xffff88810007dd00,
    msk=msk@entry=0xffff888100063958, reserved=reserved@entry=true,
    mapped_cpu=mapped_cpu@entry=0xffffc90000013b64) at kernel/irq/matrix.c:395
#4  0xffffffff8107026c in assign_vector_locked (irqd=irqd@entry=0xffff88810007b0c0,
    dest=0xffff888100063958) at arch/x86/kernel/apic/vector.c:248
#5  0xffffffff81070ce0 in assign_irq_vector_any_locked (irqd=irqd@entry=0xffff88810007b0c0)
    at arch/x86/kernel/apic/vector.c:279
#6  0xffffffff81070e03 in activate_reserved (irqd=0xffff88810007b0c0)
    at arch/x86/kernel/apic/vector.c:393
#7  x86_vector_activate (dom=<optimized out>, irqd=0xffff88810007b0c0,
    reserve=<optimized out>) at arch/x86/kernel/apic/vector.c:462
#8  0xffffffff81124fc8 in __irq_domain_activate_irq (irqd=0xffff88810007b0c0,
    reserve=reserve@entry=false) at kernel/irq/irqdomain.c:1763
#9  0xffffffff81124fa8 in __irq_domain_activate_irq (irqd=irqd@entry=0xffff8881001d5c28,
    reserve=reserve@entry=false) at kernel/irq/irqdomain.c:1760
#10 0xffffffff811264a9 in irq_domain_activate_irq (
    irq_data=irq_data@entry=0xffff8881001d5c28, reserve=reserve@entry=false)
    at kernel/irq/irqdomain.c:1786
#11 0xffffffff81122415 in irq_activate (desc=desc@entry=0xffff8881001d5c00)
    at kernel/irq/chip.c:294
#12 0xffffffff8111f94e in __setup_irq (irq=irq@entry=9, desc=desc@entry=0xffff8881001d5c00,
    new=new@entry=0xffff888100c65980) at kernel/irq/manage.c:1708
#13 0xffffffff8111fec4 in request_threaded_irq (irq=9,
    handler=handler@entry=0xffffffff816711a0 <acpi_irq>,
    thread_fn=thread_fn@entry=0x0 <fixed_percpu_data>, irqflags=irqflags@entry=128,
    devname=devname@entry=0xffffffff82610986 "acpi",
    dev_id=dev_id@entry=0xffffffff816711a0 <acpi_irq>) at kernel/irq/manage.c:2172
#14 0xffffffff816715ff in request_irq (dev=0xffffffff816711a0 <acpi_irq>,
    name=0xffffffff82610986 "acpi", flags=128, handler=0xffffffff816711a0 <acpi_irq>,
    irq=<optimized out>) at ./include/linux/interrupt.h:168

	...

#23 do_initcalls () at init/main.c:1392
#24 do_basic_setup () at init/main.c:1411
#25 kernel_init_freeable () at init/main.c:1614
#26 0xffffffff81c0b31a in kernel_init (unused=<optimized out>) at init/main.c:1505
#27 0xffffffff81004572 in ret_from_fork () at arch/x86/entry/entry_64.S:295
#28 0x0000000000000000 in ?? ()
```

看来映射的建立和架构强相关。X86 架构比我想象的复杂，记得之前分析 LA 架构挺简单的，我还是把之前的东西捡起来吧。

#### irq_data

```c
struct irq_data {
	u32			mask;
	unsigned int		irq;
	unsigned long		hwirq;
	struct irq_common_data	*common;
	struct irq_chip		*chip;
	struct irq_domain	*domain;
#ifdef	CONFIG_IRQ_DOMAIN_HIERARCHY
	struct irq_data		*parent_data;
#endif
	void			*chip_data;
};
```

#### irq_chip

这个结构是指硬件中断控制器底层操作相关的方法集合。

```c
struct irq_chip {
	struct device	*parent_device;
	const char	*name;
	unsigned int	(*irq_startup)(struct irq_data *data);
	void		(*irq_shutdown)(struct irq_data *data);
	void		(*irq_enable)(struct irq_data *data); // 打开该中断控制器中的一个中断
	void		(*irq_disable)(struct irq_data *data);

	void		(*irq_ack)(struct irq_data *data);
	void		(*irq_mask)(struct irq_data *data);
	void		(*irq_mask_ack)(struct irq_data *data);
	void		(*irq_unmask)(struct irq_data *data);
	void		(*irq_eoi)(struct irq_data *data);

	...

	void		(*ipi_send_single)(struct irq_data *data, unsigned int cpu);
	void		(*ipi_send_mask)(struct irq_data *data, const struct cpumask *dest);

	int		(*irq_nmi_setup)(struct irq_data *data);
	void		(*irq_nmi_teardown)(struct irq_data *data);

	unsigned long	flags;
};
```

#### irqdata

中断注册时还需要初始化 `irqaction` 结构，

```c
/**
 * struct irqaction - per interrupt action descriptor
 */
struct irqaction {
	irq_handler_t		handler;
	void			*dev_id;
	void __percpu		*percpu_dev_id;
	struct irqaction	*next;
	irq_handler_t		thread_fn;
	struct task_struct	*thread;
	struct irqaction	*secondary;
	unsigned int		irq;
	unsigned int		flags;
	unsigned long		thread_flags;
	unsigned long		thread_mask;
	const char		*name;
	struct proc_dir_entry	*dir;
} ____cacheline_internodealigned_in_smp; // 这个标志表示什么？
```

### 注册中断

中断处理程序最基本的工作是通知硬件设备中断已经被接收。有些中断处理程序需要完成的工作很多，为了满足实时性的要求，中断处理程序需要快速完成并且退出中断，因此将中断分成上下半部。

内核中外设驱动需要注册中断，其接口如下：

```c
static inline int __must_check
request_irq(unsigned int irq, irq_handler_t handler, unsigned long flags,
	    const char *name, void *dev)
{
	return request_threaded_irq(irq, handler, NULL, flags, name, dev);
}
```

`request_irq` 是较老的接口，2.6.30 内核中新增加了线程化的中断注册接口 `request_threaded_irq`。

这里记录一下常用的中断标志位，因为之后的初始化需要根据不同的中断类型进行不同的初始化，

| 中断标志位        | 描述                                                         |
| ----------------- | ------------------------------------------------------------ |
| IRQF_TRIGGER_*    | 中断触发的类型                                               |
| IRQF_SHARED       | 多个设备共享一个中断号                                       |
| IRQF_PROBE_SHARED | 中断处理程序允许出现共享中断部匹配的情况                     |
| IRQF_TIMER        | 标记一个时间中断                                             |
| IRQF_PERCPU       | 属于特定 CPU 的中断                                          |
| IRQF_NOBALANCING  | 禁止多 CPU 间的中断均衡（中断也可以均衡？）                  |
| IRQF_IRQPOLL      | 中断被用作轮询                                               |
| IRQF_ONESHOT      | 表示一次性触发的中断，不能嵌套。在中断线程化中也保持中断关闭状态 |
| IRQF_NO_SUSPEND   | 在系统睡眠过程中不要关闭该中断                               |
| IRQF_FORCE_RESUME | 在系统唤醒时必须强制打开该中断                               |
| IRQF_NO_THREAD    | 表示该中断不会被线程化                                       |

#### 关键函数 request_threaded_irq

```c
int request_threaded_irq(unsigned int irq, irq_handler_t handler,
			 irq_handler_t thread_fn, unsigned long irqflags, // thread_fn 是中断线程化的处理程序
			 const char *devname, void *dev_id) // irqflags 用来表示该设备申请的中断的状态
{
	struct irqaction *action;
	struct irq_desc *desc;
	int retval;

	...

	desc = irq_to_desc(irq); // 根据 irq 找到对应的中断描述符
	if (!desc)
		return -EINVAL;

	if (!irq_settings_can_request(desc) || // 有些中断描述符是系统预留的，外设不可使用
	    WARN_ON(irq_settings_is_per_cpu_devid(desc)))
		return -EINVAL;

	if (!handler) {
		if (!thread_fn) // 主处理函数和 thread_fn 不能同时为空
			return -EINVAL;
        // 默认的中断处理函数，该函数直接返回 IRQ_WAKE_THREAD，表示唤醒中断线程
		handler = irq_default_primary_handler;
	}

	action = kzalloc(sizeof(struct irqaction), GFP_KERNEL);
	if (!action)
		return -ENOMEM;

	action->handler = handler;
	action->thread_fn = thread_fn;
	action->flags = irqflags;
	action->name = devname;
	action->dev_id = dev_id;

	retval = irq_chip_pm_get(&desc->irq_data);
	if (retval < 0) {
		kfree(action);
		return retval;
	}

	retval = __setup_irq(irq, desc, action); // 继续注册

	if (retval) {
		irq_chip_pm_put(&desc->irq_data);
		kfree(action->secondary);
		kfree(action);
	}

	...

	return retval;
}
```

从这个调用栈我们可以观察到几个事情：

- 这是在内核态执行的，看变量的地址空间就知道了；
- 这个 APIC 申请 irq，且该设备没有 thread_fn；
- 到目前为止 0 号进程的工作已经完成了，现在应该是 1 号进程 kthead 在进行初始化工作；
-  `ret_from_fork` 说明 1 号进程是新 fork 出来的进程，没有经过 `schedule` 调度；
- 外设的初始化都是在 `do_initcalls` 完成的。

```
#0  request_threaded_irq (irq=9, handler=handler@entry=0xffffffff816711a0 <acpi_irq>,
    thread_fn=thread_fn@entry=0x0 <fixed_percpu_data>, irqflags=irqflags@entry=128,
    devname=devname@entry=0xffffffff82610986 "acpi",
    dev_id=dev_id@entry=0xffffffff816711a0 <acpi_irq>) at kernel/irq/manage.c:2115
#1  0xffffffff816715ff in request_irq (dev=0xffffffff816711a0 <acpi_irq>,
    name=0xffffffff82610986 "acpi", flags=128, handler=0xffffffff816711a0 <acpi_irq>,
    irq=<optimized out>) at ./include/linux/interrupt.h:168
#2  acpi_os_install_interrupt_handler (gsi=9,
    handler=handler@entry=0xffffffff8169ac07 <acpi_ev_sci_xrupt_handler>,
    context=0xffff8881008729a0) at drivers/acpi/osl.c:586
#3  0xffffffff8169ad1e in acpi_ev_install_sci_handler () at drivers/acpi/acpica/evsci.c:156
#4  0xffffffff81696598 in acpi_ev_install_xrupt_handlers ()
    at drivers/acpi/acpica/evevent.c:94
#5  0xffffffff8320fc95 in acpi_enable_subsystem (flags=flags@entry=2)
    at drivers/acpi/acpica/utxfinit.c:184
#6  0xffffffff8320d0dc in acpi_bus_init () at drivers/acpi/bus.c:1230
#7  acpi_init () at drivers/acpi/bus.c:1323
#8  0xffffffff81003928 in do_one_initcall (fn=0xffffffff8320d02d <acpi_init>)
    at init/main.c:1303
#9  0xffffffff831babeb in do_initcall_level (command_line=0xffff888100059de0 "console",
    level=4) at init/main.c:1376
#10 do_initcalls () at init/main.c:1392
#11 do_basic_setup () at init/main.c:1411
#12 kernel_init_freeable () at init/main.c:1614
#13 0xffffffff81c0b31a in kernel_init (unused=<optimized out>) at init/main.c:1505
#14 0xffffffff81004572 in ret_from_fork () at arch/x86/entry/entry_64.S:295
#15 0x0000000000000000 in ?? ()
```

#### 关键函数 __setup_irq

情况很复杂，没有全部搞懂。

```c
static int
__setup_irq(unsigned int irq, struct irq_desc *desc, struct irqaction *new)
{
	struct irqaction *old, **old_ptr;
	unsigned long flags, thread_mask = 0;
	int ret, nested, shared = 0;

	if (desc->irq_data.chip == &no_irq_chip) // 没有正确初始化中断控制器
		return -ENOSYS;

    ...

	new->irq = irq;

	/*
	 * Check whether the interrupt nests into another interrupt thread.
	 */
	nested = irq_settings_is_nested_thread(desc);
	if (nested) {
		if (!new->thread_fn) {
			ret = -EINVAL;
			goto out_mput;
		}
		/*
		 * Replace the primary handler which was provided from
		 * the driver for non nested interrupt handling by the
		 * dummy function which warns when called.
		 */
		new->handler = irq_nested_primary_handler;
	} else {
		if (irq_settings_can_thread(desc)) { // 判断该中断是否可以线程化，即判断 _IRQ_NOTHREAD 标志位
			ret = irq_setup_forced_threading(new); // 进行线程化
			if (ret)
				goto out_mput;
		}
	}

	/*
	 * Create a handler thread when a thread function is supplied
	 * and the interrupt does not nest into another interrupt
	 * thread.
	 */
	if (new->thread_fn && !nested) {
		ret = setup_irq_thread(new, irq, false); // 创建一个内核线程
		if (ret)
			goto out_mput;
		if (new->secondary) {
			ret = setup_irq_thread(new->secondary, irq, true);
			if (ret)
				goto out_thread;
		}
	}

    ...

	/* First installed action requests resources. */
	if (!desc->action) {
		ret = irq_request_resources(desc);
		if (ret) {
			pr_err("Failed to request resources for %s (irq %d) on irqchip %s\n",
			       new->name, irq, desc->irq_data.chip->name);
			goto out_bus_unlock;
		}
	}

	/*
	 * The following block of code has to be executed atomically
	 * protected against a concurrent interrupt and any of the other
	 * management calls which are not serialized via
	 * desc->request_mutex or the optional bus lock.
	 */
	raw_spin_lock_irqsave(&desc->lock, flags);
	old_ptr = &desc->action;
	old = *old_ptr;
	if (old) { // 已经有中断添加到 irq_desc 中，是共享中断

        ...

	}

	/*
	 * Setup the thread mask for this irqaction for ONESHOT. For
	 * !ONESHOT irqs the thread mask is 0 so we can avoid a
	 * conditional in irq_wake_thread().
	 */
	if (new->flags & IRQF_ONESHOT) { // 不允许嵌套的中断

        ...

	} else if (new->handler == irq_default_primary_handler &&
		   !(desc->irq_data.chip->flags & IRQCHIP_ONESHOT_SAFE)) {

        ...

	}

	if (!shared) {
		init_waitqueue_head(&desc->wait_for_threads);

		/* Setup the type (level, edge polarity) if configured: */
		if (new->flags & IRQF_TRIGGER_MASK) {
			ret = __irq_set_trigger(desc,
						new->flags & IRQF_TRIGGER_MASK);

			if (ret)
				goto out_unlock;
		}

		/*
		 * Activate the interrupt. That activation must happen
		 * independently of IRQ_NOAUTOEN. request_irq() can fail
		 * and the callers are supposed to handle
		 * that. enable_irq() of an interrupt requested with
		 * IRQ_NOAUTOEN is not supposed to fail. The activation
		 * keeps it in shutdown mode, it merily associates
		 * resources if necessary and if that's not possible it
		 * fails. Interrupts which are in managed shutdown mode
		 * will simply ignore that activation request.
		 */
		ret = irq_activate(desc);
		if (ret)
			goto out_unlock;

		desc->istate &= ~(IRQS_AUTODETECT | IRQS_SPURIOUS_DISABLED | \
				  IRQS_ONESHOT | IRQS_WAITING);
		irqd_clear(&desc->irq_data, IRQD_IRQ_INPROGRESS);

		if (new->flags & IRQF_PERCPU) {
			irqd_set(&desc->irq_data, IRQD_PER_CPU);
			irq_settings_set_per_cpu(desc);
			if (new->flags & IRQF_NO_DEBUG)
				irq_settings_set_no_debug(desc);
		}

		if (noirqdebug)
			irq_settings_set_no_debug(desc);

		if (new->flags & IRQF_ONESHOT)
			desc->istate |= IRQS_ONESHOT;

		/* Exclude IRQ from balancing if requested */
		if (new->flags & IRQF_NOBALANCING) {
			irq_settings_set_no_balancing(desc);
			irqd_set(&desc->irq_data, IRQD_NO_BALANCING);
		}

		if (!(new->flags & IRQF_NO_AUTOEN) &&
		    irq_settings_can_autoenable(desc)) {
			irq_startup(desc, IRQ_RESEND, IRQ_START_COND);
		} else {
			/*
			 * Shared interrupts do not go well with disabling
			 * auto enable. The sharing interrupt might request
			 * it while it's still disabled and then wait for
			 * interrupts forever.
			 */
			WARN_ON_ONCE(new->flags & IRQF_SHARED);
			/* Undo nested disables: */
			desc->depth = 1;
		}

	} else if (new->flags & IRQF_TRIGGER_MASK) {
		unsigned int nmsk = new->flags & IRQF_TRIGGER_MASK;
		unsigned int omsk = irqd_get_trigger_type(&desc->irq_data);

		if (nmsk != omsk)
			/* hope the handler works with current  trigger mode */
			pr_warn("irq %d uses trigger mode %u; requested %u\n",
				irq, omsk, nmsk);
	}

	*old_ptr = new;

	irq_pm_install_action(desc, new);

	/* Reset broken irq detection when installing new handler */
	desc->irq_count = 0;
	desc->irqs_unhandled = 0;

	/*
	 * Check whether we disabled the irq via the spurious handler
	 * before. Reenable it and give it another chance.
	 */
	if (shared && (desc->istate & IRQS_SPURIOUS_DISABLED)) {
		desc->istate &= ~IRQS_SPURIOUS_DISABLED;
		__enable_irq(desc);
	}

	raw_spin_unlock_irqrestore(&desc->lock, flags);
	chip_bus_sync_unlock(desc);
	mutex_unlock(&desc->request_mutex);

	irq_setup_timings(desc, new);

	/*
	 * Strictly no need to wake it up, but hung_task complains
	 * when no hard interrupt wakes the thread up.
	 */
	if (new->thread)
		wake_up_process(new->thread); // 唤醒中断线程
	if (new->secondary)
		wake_up_process(new->secondary->thread);

	register_irq_proc(irq, desc);
	new->dir = NULL;
	register_handler_proc(irq, new);
	return 0;

	...

	return ret;
}
```

中断线程化是内核新增加的特性，这个特性的具体实现应该就是 workqueue。那我们考虑一下为什么需要中断线程化。

在内核中，中断具有最高的优先级，内核会在每一条指令执行完后检查是否有中断发生，如果有，那么内核会进行上下文切换从而执行中断处理函数，等到所有的中断和软中断处理完毕后才会执行进程调度，因此这个过程会导致实时任务得不到及时响应。中断上下文总是抢占进程上下文，中断上下文不仅包括中断处理程序，还包括 softirq, tasklet 等。如果一个高优先级任务和一个中断同时触发，那么内核总是先执行中断处理程序，中断处理程序完成后可能触发软中断，也可能有一些 tasklet 要执行或有新的中断发生，这个高优先级任务的延迟变得不可预测。中断线程化的目的是把中断处理中一些繁重的任务作为内核线程运行，实时进程可以比中断线程拥有更高的优先级。这样高优先级的实时进程就可以优先得到处理。当然，不是所有的中断都可以线程化，如时钟中断。

### 底层中断处理

之前学习的都是 X86 架构的中断，所以对 X86 的汇编比较熟悉，但面试了几场发现现在用的多的是 ARM，所以接着个机会也学学 ARM 架构的硬件中断处理流程和 ARM 指令。当然 X86 的流程也会补充近来。

ARM64 支持多个异常等级（在 X86 中就是特权级），其中 EL0 是用户模式，EL1 是内核模式，EL2 是虚拟化监管模式，EL3 是安全世界模式（这个是用来干嘛的）。

时间比较紧张，之后再分析。

### 高层中断处理

由于手头没有 ARM 的机器，这里就先看看 X86 的中断执行流程吧。

这是串口中断的过程。

```
#0  serial8250_start_tx (port=0xffffffff836d1c60 <serial8250_ports>) at drivers/tty/serial/8250/8250_port.c:1654
#1  0xffffffff817440ab in __uart_start (tty=<optimized out>) at drivers/tty/serial/serial_core.c:127
#2  0xffffffff817453d2 in uart_start (tty=0xffff88810666e800) at drivers/tty/serial/serial_core.c:137
#3  0xffffffff8174543e in uart_flush_chars (tty=<optimized out>) at drivers/tty/serial/serial_core.c:549
#4  0xffffffff81728ec9 in __receive_buf (count=<optimized out>, fp=<optimized out>, cp=<optimized out>, tty=0xffff88810666e800) at drivers/tty/n_tty.c:1581
#5  n_tty_receive_buf_common (tty=<optimized out>, cp=<optimized out>, fp=<optimized out>, count=<optimized out>, flow=flow@entry=1) at drivers/tty/n_tty.c:1674
#6  0xffffffff81729d54 in n_tty_receive_buf2 (tty=<optimized out>, cp=<optimized out>, fp=<optimized out>, count=<optimized out>) at drivers/tty/n_tty.c:1709
#7  0xffffffff8172bc22 in tty_ldisc_receive_buf (ld=ld@entry=0xffff8881027f49c0, p=p@entry=0xffff88810671e428 "d", f=f@entry=0x0 <fixed_percpu_data>, count=count@entry=1)
    at drivers/tty/tty_buffer.c:471
#8  0xffffffff8172c592 in tty_port_default_receive_buf (port=<optimized out>, p=0xffff88810671e428 "d", f=0x0 <fixed_percpu_data>, count=1) at drivers/tty/tty_port.c:39
#9  0xffffffff8172bfd1 in receive_buf (count=<optimized out>, head=0xffff88810671e400, port=0xffff888100a80000) at drivers/tty/tty_buffer.c:491
#10 flush_to_ldisc (work=0xffff888100a80008) at drivers/tty/tty_buffer.c:543
#11 0xffffffff810c4a49 in process_one_work (worker=worker@entry=0xffff888100a2b0c0, work=0xffff888100a80008) at kernel/workqueue.c:2297
#12 0xffffffff810c4c3d in worker_thread (__worker=0xffff888100a2b0c0) at kernel/workqueue.c:2444
#13 0xffffffff810cc32a in kthread (_create=0xffff888100a5a280) at kernel/kthread.c:319
#14 0xffffffff81004572 in ret_from_fork () at arch/x86/entry/entry_64.S:295
#15 0x0000000000000000 in ?? ()
```

这只是键盘键入字符到字符显示在显示器上的一部分，下半部分是串口发起中断，

```
serial8250_tx_chars (up=up@entry=0xffffffff836d1c60 <serial8250_ports>) at drivers/tty/serial/8250/8250_port.c:1819
#1  0xffffffff8174ccf4 in serial8250_handle_irq (port=port@entry=0xffffffff836d1c60 <serial8250_ports>, iir=<optimized out>) at drivers/tty/serial/8250/8250_port.c:1932
#2  0xffffffff8174ce11 in serial8250_handle_irq (iir=<optimized out>, port=0xffffffff836d1c60 <serial8250_ports>) at drivers/tty/serial/8250/8250_port.c:1905
#3  serial8250_default_handle_irq (port=0xffffffff836d1c60 <serial8250_ports>) at drivers/tty/serial/8250/8250_port.c:1949
#4  0xffffffff817490e8 in serial8250_interrupt (irq=4, dev_id=0xffff8881058fd1a0) at drivers/tty/serial/8250/8250_core.c:126
#5  0xffffffff8111d892 in __handle_irq_event_percpu (desc=desc@entry=0xffff8881001cda00, flags=flags@entry=0xffffc90000003f54) at kernel/irq/handle.c:156
#6  0xffffffff8111d9e3 in handle_irq_event_percpu (desc=desc@entry=0xffff8881001cda00) at kernel/irq/handle.c:196
#7  0xffffffff8111da6b in handle_irq_event (desc=desc@entry=0xffff8881001cda00) at kernel/irq/handle.c:213
#8  0xffffffff81121ef3 in handle_edge_irq (desc=0xffff8881001cda00) at kernel/irq/chip.c:822
#9  0xffffffff810395a3 in generic_handle_irq_desc (desc=0xffff8881001cda00) at ./include/linux/irqdesc.h:158
#10 handle_irq (regs=<optimized out>, desc=0xffff8881001cda00) at arch/x86/kernel/irq.c:231
#11 __common_interrupt (regs=<optimized out>, vector=39) at arch/x86/kernel/irq.c:250
#12 0xffffffff81c085d8 in common_interrupt (regs=0xffffc9000064fc08, error_code=<optimized out>) at arch/x86/kernel/irq.c:240
#13 0xffffffff81e00cde in asm_common_interrupt () at ./arch/x86/include/asm/idtentry.h:629
```

当然中断并不是这么简单，现在的内核还加入了中断线程化，还需要在唤醒中断注册时的 `thread_fn` 线程，然后进行调度，这就是为什么串口中断的处理会从 `ret_from_fork` 开始，然后使用 `worker_thread` 执行。

内核在跳转到中断向量表对应的处理函数处理中断之前都是要保护现场的，在 X86 中使用 `SAVE_ALL` 宏，在 ARM 中使用 `irq_stack_entry` 宏。注意，在中断发生时，中断上下文会以栈帧的形式保存在中断进程的内核栈中，然后需要切换到中断栈。当中断处理完成后，`irq_stack_exit` 宏把中断栈切换回中断进程的内核栈，然后恢复上下文，并退出中断。每个 CPU 都有对应的中断栈 `irq_stack`。

接下来考虑一个事情，为什么在中断上下文中不能睡眠？睡眠就是调用 `schedule` 让当前进程让出 CPU，调度器选择另一个进程继续执行。前面我们提到现在的内核使用的是一个单独的中断栈，而不是使用被中断进程的内核栈。因此在中断上下文中即无法获得当前进程的栈，也无法获取 `thread_info` 数据结构。因此这时调用 `schedule` 之后无法回到该中断上下文，未完成的中断也不能继续完成。此外中断控制器也等不到中断处理完的信息，导致无法响应同类型的中断。

### 软中断和 tasklet

因为软中断和 tasklet 平时的项目中用不到，所以这里只记录一下它们的原理，不再进一步分析。

中断管理中的上下半部是很重要的设计理念。硬件和汇编代码处理的跳转到中断向量表和中断上下文保存属于上半部，而软中断，tasklet，工作队列属于下半部请求。中断上半部的设计理念是尽快完成并从硬件中断返回。因为硬件中断处理程序是在关中断的情况下做的，本地 CPU 不能继续响应中断，若不能及时开中断，其他对时间敏感的中断可能会出问题。

#### 软中断

软中断是预留给系统中对时间要求比较严格和重要的下半部使用的，目前驱动中只有块设备和网络子系统使用了软中断。

```c
/* PLEASE, avoid to allocate new softirqs, if you need not _really_ high
   frequency threaded job scheduling. For almost all the purposes
   tasklets are more than enough. F.e. all serial device BHs et
   al. should be converted to tasklets, not to softirqs.
 */

enum
{
	HI_SOFTIRQ=0, // 优先级为 0
	TIMER_SOFTIRQ, // 1，定时器的软中断（定时器也会用软中断么？）
	NET_TX_SOFTIRQ, // 2，发送网络数据包的软中断
	NET_RX_SOFTIRQ, // 3，接受网络数据包的软中断
	BLOCK_SOFTIRQ, // 4，块设备使用
	IRQ_POLL_SOFTIRQ, // 5，块设备使用
	TASKLET_SOFTIRQ, // 6，tasklet
	SCHED_SOFTIRQ, // 7，进程调度和负载均衡
	HRTIMER_SOFTIRQ, // 8，高精度定时器
	RCU_SOFTIRQ,    /* Preferable RCU should always be the last softirq */

	NR_SOFTIRQS
};

// 软件中断描述符，只包含一个handler函数指针
struct softirq_action {
	void	(*action)(struct softirq_action *);
};

// 软中断描述符表，实际上就是一个全局的数组
static struct softirq_action softirq_vec[NR_SOFTIRQS] __cacheline_aligned_in_smp;

// CPU软中断状态描述，当某个软中断触发时，__softirq_pending会置位对应的bit
typedef struct {
	unsigned int __softirq_pending;
	unsigned int ipi_irqs[NR_IPI];
} ____cacheline_aligned irq_cpustat_t;

// 每个CPU都会维护一个状态信息结构
irq_cpustat_t irq_stat[NR_CPUS] ____cacheline_aligned;

// 内核为每个CPU都创建了一个软中断处理内核线程
DEFINE_PER_CPU(struct task_struct *, ksoftirqd);
```

这个是系统静态定义的软中断类型。

而软中断的执行需要结合上下文来考虑：

- 在中断返回时，CPU 会检查 `__softirq_pending` 成员的位，如果不为 0，说明有软中断需要处理，处理完后才会返回到中断进程；
- 在进程上下文，需要调用 `wakeup_softirqd` 来唤醒 `ksoftirqd` 线程来处理。

这里总结一下软中断需要注意的地方：

- 软中断的回调函数在开中断的环境下执行，能够被其他中断抢占，但不能被进程抢占；
- 同一类型的软中断可能在多个 CPU 上并行执行。
- 软中断是在中断返回前，即退出硬中断上下文时，执行的，所以其还是执行在中断上下文，不能睡眠。

#### tasklet

tasklet 在内核中使用 `tasklet_struct` 表示，

```c
struct tasklet_struct
{
	struct tasklet_struct *next;
	unsigned long state;
	atomic_t count;
	bool use_callback;
	union {
		void (*func)(unsigned long data);
		void (*callback)(struct tasklet_struct *t);
	};
	unsigned long data;
};
```

从上文中分析可以看出，`tasklet` 是软中断的一种类型，那么两者有何区别：

- 软中断类型内核中都是静态分配，不支持动态分配，而 `tasklet` 支持动态和静态分配，也就是驱动程序中能比较方便的进行扩展；
- 软中断可以在多个 CPU 上并行运行，因此需要考虑可重入问题，而 `tasklet ` 是串行执行，其会绑定在某个 CPU 上运行，运行完后再解绑，不要求重入问题，当然它的性能也就会下降一些。

软中断上下文的优先级高于进程上下文，如果执行软中断和 tasklet 时间很长，那么高优先级的进程就长时间得不到运行，会影响系统的实时性，所以引入了 workqueue。

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
