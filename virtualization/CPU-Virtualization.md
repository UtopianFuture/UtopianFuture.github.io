## CPU Virtualization

### QEMU 侧虚拟机的建立

QEMU 侧创建虚拟机比较简单，主要是在 `kvm_init` 中调用 `ioctl(KVM_CREATE_VM)` ，保存一个返回句柄 fd 在 `KVM_State->vmfd` 中，这样在其他地方也能使用这个句柄。QEMU 中使用 `KVM_State` 表示 KVM 相关的数据结构。

#### KVMState

老规矩，我们来看看这个重要的数据结构，

```c
struct KVMState
{
    AccelState parent_obj; // 因为 QEMU 能够支持 tcg, kvm 等不同加速方式，所以有个父类

    int nr_slots; // 内存条，这些都是从 KVM 中获取的
    int fd; // 表示内核中的 /dev/kvm 模块，内核要支持 KVM 才能进行下一步操作
    int vmfd; // 这个句柄表示的就是 KVM 中创建的虚拟机
    int coalesced_mmio; // 这个就是在 KVM 中分配页面专门用来做 mmio，访问到这个地址就会触发异常
    int coalesced_pio;
    struct kvm_coalesced_mmio_ring *coalesced_mmio_ring;
    bool coalesced_flush_in_progress;
    int vcpu_events;
    int robust_singlestep;
    int debugregs;
#ifdef KVM_CAP_SET_GUEST_DEBUG
    QTAILQ_HEAD(, kvm_sw_breakpoint) kvm_sw_breakpoints;
#endif
    int max_nested_state_len;
    int many_ioeventfds;
    int intx_set_mask;
    int kvm_shadow_mem;
    bool kernel_irqchip_allowed;
    bool kernel_irqchip_required;
    OnOffAuto kernel_irqchip_split;
    bool sync_mmu;
    uint64_t manual_dirty_log_protect;
    /* The man page (and posix) say ioctl numbers are signed int, but
     * they're not.  Linux, glibc and *BSD all treat ioctl numbers as
     * unsigned, and treating them as signed here can break things */
    unsigned irq_set_ioctl;
    unsigned int sigmask_len;
    GHashTable *gsimap;
#ifdef KVM_CAP_IRQ_ROUTING
    struct kvm_irq_routing *irq_routes;
    int nr_allocated_irq_routes;
    unsigned long *used_gsi_bitmap;
    unsigned int gsi_count;
    QTAILQ_HEAD(, KVMMSIRoute) msi_hashtab[KVM_MSI_HASHTAB_SIZE];
#endif
    KVMMemoryListener memory_listener;
    QLIST_HEAD(, KVMParkedVcpu) kvm_parked_vcpus;

    /* For "info mtree -f" to tell if an MR is registered in KVM */
    int nr_as;
    struct KVMAs {
        KVMMemoryListener *ml;
        AddressSpace *as;
    } *as; // 该虚拟机的注册的内存可以用这里开始找到
    // 不知道啥是dirty ring,j
    uint64_t kvm_dirty_ring_bytes;  /* Size of the per-vcpu dirty ring */
    uint32_t kvm_dirty_ring_size;   /* Number of dirty GFNs per ring */
    struct KVMDirtyRingReaper reaper;
};
```

这是调用过程，

```c
#0  kvm_init (ms=0x55555699a400) at ../accel/kvm/kvm-all.c:2307
#1  0x0000555555af387b in accel_init_machine (accel=0x55555681c140, ms=0x55555699a400) at ../accel/accel-softmmu.c:39
#2  0x0000555555c0752b in do_configure_accelerator (opaque=0x7fffffffda0d, opts=0x555556a448e0, errp=0x55555677c100 <error_fatal>) at ../softmmu/vl.c:2348
#3  0x0000555555f01307 in qemu_opts_foreach (list=0x5555566a18c0 <qemu_accel_opts>, func=0x555555c07401 <do_configure_accelerator>, opaque=0x7fffffffda0d, errp=0x55555677c100 <error_fatal>)
    at ../util/qemu-option.c:1135
#4  0x0000555555c07790 in configure_accelerators (progname=0x7fffffffe0c5 "/home/guanshun/gitlab/qemu-newest/build/qemu-system-x86_64") at ../softmmu/vl.c:2414
#5  0x0000555555c0a834 in qemu_init (argc=13, argv=0x7fffffffdcc8, envp=0x7fffffffdd38) at ../softmmu/vl.c:3724 // qemu 的参数解析就是在这里完成的
#6  0x000055555583b6f5 in main (argc=13, argv=0x7fffffffdcc8, envp=0x7fffffffdd38) at ../softmmu/main.c:49
```

总的来说 `kvm_init` 在使用 KVM 的情况下是同时 `ioctl` 来确定 KVM 能够使用哪些功能，当然，最重要的还是确定 `vmfd`。

### KVM 侧虚拟机的建立

`kvm_dev_ioctl` 向用户层 QEMU 提供 `ioctl` ，根据不同类型的请求提供不同的服务，如果是 `KVM_CREATE_VM` 就调用 `kvm_dev_ioctl_create_vm` 创建一个虚拟机，然后返回一个文件描述符到用户态，QEMU 用该描述符操作虚拟机。

KVM 侧的实现之前没有详细分析，现在进一步分析。

```c
static long kvm_dev_ioctl(struct file *filp,
			  unsigned int ioctl, unsigned long arg)
{
	long r = -EINVAL;

	switch (ioctl) {
	case KVM_GET_API_VERSION:
		if (arg)
			goto out;
		r = KVM_API_VERSION;
		break;
	case KVM_CREATE_VM:
		r = kvm_dev_ioctl_create_vm(arg);
		break;
	case KVM_CHECK_EXTENSION:
		r = kvm_vm_ioctl_check_extension_generic(NULL, arg);
		break;
	case KVM_GET_VCPU_MMAP_SIZE:
		if (arg)
			goto out;
		r = PAGE_SIZE;     /* struct kvm_run */
#ifdef CONFIG_X86
		r += PAGE_SIZE;    /* pio data page */
#endif
#ifdef CONFIG_KVM_MMIO
		r += PAGE_SIZE;    /* coalesced mmio ring page */
#endif
		break;
	case KVM_TRACE_ENABLE:
	case KVM_TRACE_PAUSE:
	case KVM_TRACE_DISABLE:
		r = -EOPNOTSUPP;
		break;
	default:
		return kvm_arch_dev_ioctl(filp, ioctl, arg);
	}
out:
	return r;
}
```

该函数的主要任务是调用 `kvm_create_vm` 创建虚拟机实例，每一个虚拟机实例用 `kvm` 结构体表示。

#### kvm

这里又涉及各种各样的情况，先记录下来，之后一点点补。

```c
struct kvm {

    ... // 各种锁

	struct mm_struct *mm; /* userspace tied to this vm */
	struct kvm_memslots __rcu *memslots[KVM_ADDRESS_SPACE_NUM];
	struct kvm_vcpu *vcpus[KVM_MAX_VCPUS]; // 每个虚拟机都有一个或多个 VCPU

	/* Used to wait for completion of MMU notifiers.  */
	spinlock_t mn_invalidate_lock;
	unsigned long mn_active_invalidate_count;
	struct rcuwait mn_memslots_update_rcuwait;

	/*
	 * created_vcpus is protected by kvm->lock, and is incremented
	 * at the beginning of KVM_CREATE_VCPU.  online_vcpus is only
	 * incremented after storing the kvm_vcpu pointer in vcpus,
	 * and is accessed atomically.
	 */
	atomic_t online_vcpus;
	int created_vcpus;
	int last_boosted_vcpu;
	struct list_head vm_list;
	struct mutex lock;
	struct kvm_io_bus __rcu *buses[KVM_NR_BUSES];
#ifdef CONFIG_HAVE_KVM_EVENTFD
	struct {
		spinlock_t        lock;
		struct list_head  items;
		struct list_head  resampler_list;
		struct mutex      resampler_lock;
	} irqfds;
	struct list_head ioeventfds;
#endif
	struct kvm_vm_stat stat;
	struct kvm_arch arch;
	refcount_t users_count;
#ifdef CONFIG_KVM_MMIO
	struct kvm_coalesced_mmio_ring *coalesced_mmio_ring;
	spinlock_t ring_lock;
	struct list_head coalesced_zones;
#endif

	struct mutex irq_lock;
#ifdef CONFIG_HAVE_KVM_IRQCHIP
	/*
	 * Update side is protected by irq_lock.
	 */
	struct kvm_irq_routing_table __rcu *irq_routing;
#endif
#ifdef CONFIG_HAVE_KVM_IRQFD
	struct hlist_head irq_ack_notifier_list;
#endif

#if defined(CONFIG_MMU_NOTIFIER) && defined(KVM_ARCH_WANT_MMU_NOTIFIER)
	struct mmu_notifier mmu_notifier;
	unsigned long mmu_notifier_seq;
	long mmu_notifier_count;
	unsigned long mmu_notifier_range_start;
	unsigned long mmu_notifier_range_end;
#endif
	struct list_head devices;
	u64 manual_dirty_log_protect;
	struct dentry *debugfs_dentry;
	struct kvm_stat_data **debugfs_stat_data;
	struct srcu_struct srcu;
	struct srcu_struct irq_srcu;
	pid_t userspace_pid;
	unsigned int max_halt_poll_ns;
	u32 dirty_ring_size;
	bool vm_bugged;

#ifdef CONFIG_HAVE_KVM_PM_NOTIFIER
	struct notifier_block pm_notifier;
#endif
	char stats_id[KVM_STATS_NAME_SIZE];
};
```

这里主要是调用 `kvm_create_vm` 函数来初始化各种结构体。

```c
static int kvm_dev_ioctl_create_vm(unsigned long type)
{
	int r;
	struct kvm *kvm;
	struct file *file;

	kvm = kvm_create_vm(type);
	if (IS_ERR(kvm))
		return PTR_ERR(kvm);
#ifdef CONFIG_KVM_MMIO
	r = kvm_coalesced_mmio_init(kvm); // 分配页面作为 mmio
	if (r < 0)
		goto put_kvm;
#endif
	r = get_unused_fd_flags(O_CLOEXEC);
	if (r < 0)
		goto put_kvm;

	snprintf(kvm->stats_id, sizeof(kvm->stats_id),
			"kvm-%d", task_pid_nr(current));

	file = anon_inode_getfile("kvm-vm", &kvm_vm_fops, kvm, O_RDWR);
	if (IS_ERR(file)) {
		put_unused_fd(r);
		r = PTR_ERR(file);
		goto put_kvm;
	}

	...

	return r;

put_kvm:
	kvm_put_kvm(kvm); // 创建失败，直接销毁
	return r;
}
```

#### kvm_create_vm

这个创建虚拟机的关键函数，包括相关成员变量的初始化以及开启 vmx 模式。当然不同架构部分变量不一样，所以还有 `kvm_arch_init_vm` 来初始化架构相关的部分。

```c
static struct kvm *kvm_create_vm(unsigned long type)
{
	struct kvm *kvm = kvm_arch_alloc_vm(); // 调用 vmalloc 分配 kvm 结构体内存

    ... // 初始化各种锁

	refcount_set(&kvm->users_count, 1);
	for (i = 0; i < KVM_ADDRESS_SPACE_NUM; i++) {
		struct kvm_memslots *slots = kvm_alloc_memslots(); // 为虚拟机分配内存槽

		if (!slots)
			goto out_err_no_arch_destroy_vm;
		/* Generations must be different for each address space. */
		slots->generation = i;
		rcu_assign_pointer(kvm->memslots[i], slots);
	}

	for (i = 0; i < KVM_NR_BUSES; i++) {
		rcu_assign_pointer(kvm->buses[i],
			kzalloc(sizeof(struct kvm_io_bus), GFP_KERNEL_ACCOUNT));
		if (!kvm->buses[i])
			goto out_err_no_arch_destroy_vm;
	}

	kvm->max_halt_poll_ns = halt_poll_ns;

	r = kvm_arch_init_vm(kvm, type); // 架构相关的初始化
	if (r)
		goto out_err_no_arch_destroy_vm;

	r = hardware_enable_all(); // 最终开启 VMX 模式
	if (r)
		goto out_err_no_disable;

#ifdef CONFIG_HAVE_KVM_IRQFD
	INIT_HLIST_HEAD(&kvm->irq_ack_notifier_list);
#endif

	r = kvm_init_mmu_notifier(kvm);
	if (r)
		goto out_err_no_mmu_notifier;

	r = kvm_arch_post_init_vm(kvm);
	if (r)
		goto out_err;

	...

	return kvm;

    ...
}
```

```c
static int hardware_enable(void)
{
	int cpu = raw_smp_processor_id();
	u64 phys_addr = __pa(per_cpu(vmxarea, cpu));
	int r;

	if (cr4_read_shadow() & X86_CR4_VMXE)
		return -EBUSY;

	/*
	 * This can happen if we hot-added a CPU but failed to allocate
	 * VP assist page for it.
	 */
	if (static_branch_unlikely(&enable_evmcs) &&
	    !hv_get_vp_assist_page(cpu))
		return -EFAULT;

	intel_pt_handle_vmx(1);

	r = kvm_cpu_vmxon(phys_addr); // 设置 CR4_VMXE 位，同时调用 vmxon 开启 vmx
	if (r) {
		intel_pt_handle_vmx(0);
		return r;
	}

	if (enable_ept) // 应该是打开 ept
		ept_sync_global();

	return 0;
}
```

### QEMU CPU 的创建

前面两节讲的是整个虚拟机的建立，而虚拟机又可以使用多个 VCPU ，接下来分析 VCPU 的建立。

在 [QOM](https://github.com/UtopianFuture/UtopianFuture.github.io/blob/master/virtualization/QOM.md) 中已经介绍了 CPU 的类型初始化，对象实例初始化，但还需要对 CPU 对象进行具现化才能让 CPU 对象可以用。我对具现化的理解就是在 VCPU 上运行 GuestOS 。这一工作通过调用 `x86_cpu_realizefn` 来完成。

```c
static void x86_cpu_realizefn(DeviceState *dev, Error **errp)
{
    CPUState *cs = CPU(dev);
    X86CPU *cpu = X86_CPU(dev);
    X86CPUClass *xcc = X86_CPU_GET_CLASS(dev);
    CPUX86State *env = &cpu->env;
    Error *local_err = NULL;
    static bool ht_warned;

    ...

    // 根据 QEMU 的命令行参数解析出来的 CPU 特性对 CPU 实例中的属性设置 TRUE 或 FALSE
    x86_cpu_expand_features(cpu, &local_err);
    if (local_err) {
        goto out;
    }

    // 用来检测宿主机 CPU 特性能否支持创建的 CPU 对象
    x86_cpu_filter_features(cpu, cpu->check_cpuid || cpu->enforce_cpuid);

    ...

    /* Process Hyper-V enlightenments */
    x86_cpu_hyperv_realize(cpu); // 用来初始化 CPU 实例中的 hyperv 相关的变量

    cpu_exec_realizefn(cs, &local_err); // 调用 cpu_list_add 将正在初始化的 CPU 对象添加到一个全局链表 cpus 上
    if (local_err != NULL) {
        error_propagate(errp, local_err);
        return;
    }

#ifndef CONFIG_USER_ONLY
    MachineState *ms = MACHINE(qdev_get_machine());
    qemu_register_reset(x86_cpu_machine_reset_cb, cpu);

    if (cpu->env.features[FEAT_1_EDX] & CPUID_APIC || ms->smp.cpus > 1) {
        x86_cpu_apic_create(cpu, &local_err);
        if (local_err != NULL) {
            goto out;
        }
    }
#endif

    mce_init(cpu); // 目前还不清楚这个机制是干嘛的

	...

    // 根据 QEMU 使用的加速器 cpus_accel->create_vcpu_thread(cpu);
    // 来决定执行哪个初始化函数，加速方式在 accel 目录下
    qemu_init_vcpu(cs);

    ...

    x86_cpu_apic_realize(cpu, &local_err);
    if (local_err != NULL) {
        goto out;
    }
    cpu_reset(cs);

    xcc->parent_realize(dev, &local_err);

out:
    if (local_err != NULL) {
        error_propagate(errp, local_err);
        return;
    }
}
```

具体的调用过程如下：

```plain
#0  qemu_thread_create (thread=0x5555569aa130, name=0x7fffffffd5e0 "CPU 0/KVM", start_routine=0x555555c84991 <kvm_vcpu_thread_fn>, arg=0x55555699d430, mode=0)
    at ../util/qemu-thread-posix.c:529
#1  0x0000555555c84b8b in kvm_start_vcpu_thread (cpu=0x55555699d430) at ../accel/kvm/kvm-accel-ops.c:73
#2  0x0000555555c513b4 in qemu_init_vcpu (cpu=0x55555699d430) at ../softmmu/cpus.c:628
#3  0x0000555555b74759 in x86_cpu_realizefn (dev=0x55555699d430, errp=0x7fffffffd6e0) at ../target/i386/cpu.c:6910
#4  0x0000555555dfec89 in device_set_realized (obj=0x55555699d430, value=true, errp=0x7fffffffd7e8) at ../hw/core/qdev.c:761
#5  0x0000555555de5a4e in property_set_bool (obj=0x55555699d430, v=0x5555569a9f20, name=0x55555608c221 "realized", opaque=0x55555675d100, errp=0x7fffffffd7e8) at ../qom/object.c:2257
#6  0x0000555555de3a6f in object_property_set (obj=0x55555699d430, name=0x55555608c221 "realized", v=0x5555569a9f20, errp=0x5555566e9a98 <error_fatal>) at ../qom/object.c:1402
#7  0x0000555555ddf2a0 in object_property_set_qobject (obj=0x55555699d430, name=0x55555608c221 "realized", value=0x55555699c6d0, errp=0x5555566e9a98 <error_fatal>)
    at ../qom/qom-qobject.c:28
#8  0x0000555555de3de7 in object_property_set_bool (obj=0x55555699d430, name=0x55555608c221 "realized", value=true, errp=0x5555566e9a98 <error_fatal>) at ../qom/object.c:1472
#9  0x0000555555dfdca9 in qdev_realize (dev=0x55555699d430, bus=0x0, errp=0x5555566e9a98 <error_fatal>) at ../hw/core/qdev.c:389
#10 0x0000555555b27071 in x86_cpu_new (x86ms=0x5555568b9de0, apic_id=0, errp=0x5555566e9a98 <error_fatal>) at ../hw/i386/x86.c:111
#11 0x0000555555b27144 in x86_cpus_init (x86ms=0x5555568b9de0, default_cpu_version=1) at ../hw/i386/x86.c:138
#12 0x0000555555b2047a in pc_init1 (machine=0x5555568b9de0, host_type=0x555556008544 "i440FX-pcihost", pci_type=0x55555600853d "i440FX") at ../hw/i386/pc_piix.c:159
#13 0x0000555555b20efa in pc_init_v6_0 (machine=0x5555568b9de0) at ../hw/i386/pc_piix.c:427
#14 0x0000555555a8c6e9 in machine_run_board_init (machine=0x5555568b9de0) at ../hw/core/machine.c:1232
#15 0x0000555555c77b73 in qemu_init_board () at ../softmmu/vl.c:2514
#16 0x0000555555c77d52 in qmp_x_exit_preconfig (errp=0x5555566e9a98 <error_fatal>) at ../softmmu/vl.c:2588
#17 0x0000555555c7a45f in qemu_init (argc=5, argv=0x7fffffffdd98, envp=0x7fffffffddc8) at ../softmmu/vl.c:3611
#18 0x0000555555818f05 in main (argc=5, argv=0x7fffffffdd98, envp=0x7fffffffddc8) at ../softmmu/main.c:49
```

关于为什么在 `x86_cpu_new` 之后会产生接下来的一系列调用之后再分析，这里先分析 `x86_cpu_realizefn` 。

首先是开始的几个变量声明

```c
CPUState *cs = CPU(dev);
X86CPU *cpu = X86_CPU(dev);
X86CPUClass *xcc = X86_CPU_GET_CLASS(dev);
CPUX86State *env = &cpu->env;
```

它们的关系如下：

```plain
       / 		     +---------+
       |   CPUState  | env_ptr |---  所有 CPU 都有的数据
	   | 		     +---------+  |
	   |			 | 		   |  |
	   |			 +---------+<--
X86CPU |  			 |		   |
	   | CPUX86State |   env   |     x86cpu 特有的数据
	   |			 |		   |
	   |			 +---------+
	   |			 |		   |     cpu 虚拟化需要的数据
	   \			 +---------+
```

另外 `X86CPUClass` 是**静态变量**，其中的变量也是所有 X86 CPU 共有的。

接下来进一步分析 `qemu_init_vcpu`，

```c
void qemu_init_vcpu(CPUState *cpu)
{
    MachineState *ms = MACHINE(qdev_get_machine());

    cpu->nr_cores = ms->smp.cores; // 这些都很好理解
    cpu->nr_threads =  ms->smp.threads;
    cpu->stopped = true;
    cpu->random_seed = qemu_guest_random_seed_thread_part1();

    if (!cpu->as) {
        /* If the target cpu hasn't set up any address spaces itself,
         * give it the default one.
         */
        cpu->num_ases = 1;
        cpu_address_space_init(cpu, 0, "cpu-memory", cpu->memory); // 这里只是创建变量，还没有初始化
    }

    /* accelerators all implement the AccelOpsClass */
    g_assert(cpus_accel != NULL && cpus_accel->create_vcpu_thread != NULL);
    cpus_accel->create_vcpu_thread(cpu);

    while (!cpu->created) {
        qemu_cond_wait(&qemu_cpu_cond, &qemu_global_mutex);
    }
}
```

首先记录 CPU 的核数和线程数，然后创建地址空间。如果使用 KVM 加速，就会调用 `kvm_start_vcpu_thread`

```c
static void kvm_start_vcpu_thread(CPUState *cpu)
{
    char thread_name[VCPU_THREAD_NAME_SIZE];

    cpu->thread = g_malloc0(sizeof(QemuThread));
    cpu->halt_cond = g_malloc0(sizeof(QemuCond));
    qemu_cond_init(cpu->halt_cond);
    snprintf(thread_name, VCPU_THREAD_NAME_SIZE, "CPU %d/KVM",
             cpu->cpu_index);
    qemu_thread_create(cpu->thread, thread_name, kvm_vcpu_thread_fn,
                       cpu, QEMU_THREAD_JOINABLE);
}
```

该函数通过 `kvm_vcpu_thread_fn` 创建 VCPU 线程并**运行**。

```c
static void *kvm_vcpu_thread_fn(void *arg)
{
    CPUState *cpu = arg;
    int r;

    rcu_register_thread();

    qemu_mutex_lock_iothread();
    qemu_thread_get_self(cpu->thread);
    cpu->thread_id = qemu_get_thread_id();
    cpu->can_do_io = 1;
    current_cpu = cpu;

    r = kvm_init_vcpu(cpu, &error_fatal);
    kvm_init_cpu_signals(cpu);

    /* signal CPU creation */
    cpu_thread_signal_created(cpu);
    qemu_guest_random_seed_thread_part2(cpu->random_seed);

    do {
        if (cpu_can_run(cpu)) {
            r = kvm_cpu_exec(cpu); // 运行该 cpu，然后应用层就阻塞在这里
            if (r == EXCP_DEBUG) {
                cpu_handle_guest_debug(cpu);
            }
        }
        qemu_wait_io_event(cpu); // 因为 QEMU 分为主线程和 I/O 线程，I/O 线程是用来处理设备读写的
    } while (!cpu->unplug || cpu_can_run(cpu));

    kvm_destroy_vcpu(cpu); // 退出循环就直接销毁
    cpu_thread_signal_destroyed(cpu);
    qemu_mutex_unlock_iothread();
    rcu_unregister_thread();
    return NULL;
}
```

这个函数首先调用 `kvm_init_vcpu` ，用于在 KVM 中创建 VCPU，这里 `KVMState` 是 QEMU 中用来表示 KVM 相关的数据结构，操作虚拟机的 vmfd 就保存在这里，**注意这里有个容易搞混的地方，KVM 里是通过 `kvm` 结构来表示虚拟机的**。

```c
int kvm_init_vcpu(CPUState *cpu, Error **errp)
{
    KVMState *s = kvm_state;
    long mmap_size;
    int ret;

    trace_kvm_init_vcpu(cpu->cpu_index, kvm_arch_vcpu_id(cpu));

    ret = kvm_get_vcpu(s, kvm_arch_vcpu_id(cpu));

    ...

    cpu->kvm_fd = ret;
    cpu->kvm_state = s;
    cpu->vcpu_dirty = true;

    mmap_size = kvm_ioctl(s, KVM_GET_VCPU_MMAP_SIZE, 0);
    if (mmap_size < 0) {
        ret = mmap_size;
        error_setg_errno(errp, -mmap_size,
                         "kvm_init_vcpu: KVM_GET_VCPU_MMAP_SIZE failed");
        goto err;
    }

    cpu->kvm_run = mmap(NULL, mmap_size, PROT_READ | PROT_WRITE, MAP_SHARED,
                        cpu->kvm_fd, 0); // 将 KVM 中 kvm 结构映射到 QEMU 的地址空间中，数据共享
    ...

    ret = kvm_arch_init_vcpu(cpu); // 构建 cpuid 然后传给 kvm?

    ...

err:
    return ret;
}
```

`kvm_get_vcpu` 首先会**从已有的 VCPU 队列中查询是否有对应 `vcpu_id` 的 VCPU** ，如果没有的话就用 `ioctl` 向 KVM 发起一个 `KVM_CREATE_VCPU` 的请求，并把对应的 `vcpu_id` 传进去。

```c
static int kvm_get_vcpu(KVMState *s, unsigned long vcpu_id)
{
    struct KVMParkedVcpu *cpu;

    QLIST_FOREACH(cpu, &s->kvm_parked_vcpus, node) {
        if (cpu->vcpu_id == vcpu_id) {
            int kvm_fd;

            QLIST_REMOVE(cpu, node);
            kvm_fd = cpu->kvm_fd;
            g_free(cpu);
            return kvm_fd;
        }
    }

    return kvm_vm_ioctl(s, KVM_CREATE_VCPU, (void *)vcpu_id);
}
```

`kvm_vm_ioctl` 底层就是一个 `ioctl` 系统调用，这个系统调用会被 KVM 捕获进行分析。

```c
int kvm_vm_ioctl(KVMState *s, int type, ...)
{
    int ret;
    void *arg;
    va_list ap;

    va_start(ap, type);
    arg = va_arg(ap, void *);
    va_end(ap);

    trace_kvm_vm_ioctl(type, arg);
    ret = ioctl(s->vmfd, type, arg);
    if (ret == -1) {
        ret = -errno;
    }
    return ret;
}
```

然后调用 `kvm_init_cpu_signals` 初始化 CPU 的信号处理，使 CPU 线程能够处理 IPI 中断。

接下来的 `do while` 循环是最重要的，通过 `cpu_can_run` 判断 CPU 是否能够运行，如果可以则调用 `kvm_cpu_exec` 。该函数会调用 `kvm_vcpu_ioctl` ，即 KVM 提供的 `ioctl(KVM_RUN)` ，让 VCPU 在物理 CPU 上运行起来，然后应用层就阻塞在这里，当 guestOS 产生 `VM Exit` 时内核**再根据退出原因进行处理**，然后再循环运行，完成 CPU 的虚拟化。

### KVM CPU 的创建

[前面](#2. KVM 侧虚拟机的建立)讲到 `kvm_dev_ioctl_create_vm` 会创建一个匿名的文件，用来表示一台虚拟机，并且返回 fd 到用户态， QEMU 通过这个文件描述符来操作虚拟机，即下面这行代码：

```c
file = anon_inode_getfile("kvm-vm", &kvm_vm_fops, kvm, O_RDWR);
```

该匿名文件的定义如下：

```c
static struct file_operations kvm_vm_fops = {
	.release        = kvm_vm_release,
	.unlocked_ioctl = kvm_vm_ioctl,
	.llseek		= noop_llseek,
	KVM_COMPAT(kvm_vm_compat_ioctl),
};
```

其主要操作也是 `ioctl` ，对应的函数是 `kvm_vm_ioctl` ，这个函数是处理虚拟机级别的 `ioctl` 入口，前面的 `kvm_dev_ioctl` 是设备级别的处理入口，不要搞混了。`kvm_vm_ioctl`  会处理各种请求，刚刚我们分析的 QEMU 中的 `kvm_vm_ioctl` 请求就是在这里进行处理（注意一个是 QEMU 中的函数，一个是 kernel KVM 中的函数，不要搞混了），这里先分析 `KVM_CREATE_VCPU` 即创建 VCPU 的情况。

```c
static long kvm_vm_ioctl(struct file *filp,
			   unsigned int ioctl, unsigned long arg)
{
	struct kvm *kvm = filp->private_data;
	void __user *argp = (void __user *)arg;
	int r;

	if (kvm->mm != current->mm || kvm->vm_bugged)
		return -EIO;
	switch (ioctl) {
	case KVM_CREATE_VCPU:
		r = kvm_vm_ioctl_create_vcpu(kvm, arg);
		break;
	...

	default:
		r = kvm_arch_vm_ioctl(filp, ioctl, arg);
	}
out:
	return r;
}
```

#### kvm_vm_ioctl_create_vcpu

```c
static int kvm_vm_ioctl_create_vcpu(struct kvm *kvm, u32 id)
{
	int r;
	struct kvm_vcpu *vcpu;
	struct page *page;

	...

	kvm->created_vcpus++;
	mutex_unlock(&kvm->lock);

	r = kvm_arch_vcpu_precreate(kvm, id);
	if (r)
		goto vcpu_decrement;

	vcpu = kmem_cache_zalloc(kvm_vcpu_cache, GFP_KERNEL_ACCOUNT); // 分配结构体
	if (!vcpu) {
		r = -ENOMEM;
		goto vcpu_decrement;
	}

	BUILD_BUG_ON(sizeof(struct kvm_run) > PAGE_SIZE);
	page = alloc_page(GFP_KERNEL_ACCOUNT | __GFP_ZERO);
	if (!page) {
		r = -ENOMEM;
		goto vcpu_free;
	}
	vcpu->run = page_address(page); // 这个信息是运行时信息？和 QEMU 共享的么

	kvm_vcpu_init(vcpu, kvm, id); // 初始化 vcpu 中的变量

	r = kvm_arch_vcpu_create(vcpu); // 架构相关
	if (r)
		goto vcpu_free_run_page;

	...

	vcpu->vcpu_idx = atomic_read(&kvm->online_vcpus);
	BUG_ON(kvm->vcpus[vcpu->vcpu_idx]);

	/* Fill the stats id string for the vcpu */
	snprintf(vcpu->stats_id, sizeof(vcpu->stats_id), "kvm-%d/vcpu-%d",
		 task_pid_nr(current), id);

	/* Now it's all set up, let userspace reach it */
	kvm_get_kvm(kvm);
	r = create_vcpu_fd(vcpu);
	if (r < 0) {
		kvm_put_kvm_no_destroy(kvm);
		goto unlock_vcpu_destroy;
	}

	kvm->vcpus[vcpu->vcpu_idx] = vcpu; // 将创建的 vcpu 保存到对应的虚拟机中

	/*
	 * Pairs with smp_rmb() in kvm_get_vcpu.  Write kvm->vcpus
	 * before kvm->online_vcpu's incremented value.
	 */
	smp_wmb();
	atomic_inc(&kvm->online_vcpus);

	mutex_unlock(&kvm->lock);
	kvm_arch_vcpu_postcreate(vcpu);
	kvm_create_vcpu_debugfs(vcpu);
	return r;

	...

	return r;
}
```

这里代码很清楚，`kvm_vcpu_init` 和 `kvm_arch_vcpu_create` 初始化 VCPU ，`kvm_vcpu_init` 就是初始化一些架构无关变量，最重要的应该是 `vcpu->kvm = kvm;` 和 `vcpu->vcpu_id = id;`，`kvm_arch_vcpu_create` 进行进一步的初始化，这其中涉及到很多关于 CPU 的性质的初始化，不懂，有需要再进一步了解。

#### kvm_arch_vcpu_create

```c
int kvm_arch_vcpu_create(struct kvm_vcpu *vcpu)
{
	struct page *page;
	int r;

	...

	if (!irqchip_in_kernel(vcpu->kvm) || kvm_vcpu_is_reset_bsp(vcpu))
		vcpu->arch.mp_state = KVM_MP_STATE_RUNNABLE;
	else
		vcpu->arch.mp_state = KVM_MP_STATE_UNINITIALIZED;

    // 初始化 mmu，如 pte_list 之类的
	r = kvm_mmu_create(vcpu); // 这个需要深入分析
	if (r < 0)
		return r;

    // lapic
	if (irqchip_in_kernel(vcpu->kvm)) {
		r = kvm_create_lapic(vcpu, lapic_timer_advance_ns);
		if (r < 0)
			goto fail_mmu_destroy;
		if (kvm_apicv_activated(vcpu->kvm))
			vcpu->arch.apicv_active = true;
	} else
		static_branch_inc(&kvm_has_noapic_vcpu);

	r = -ENOMEM;

	page = alloc_page(GFP_KERNEL_ACCOUNT | __GFP_ZERO);
	if (!page)
		goto fail_free_lapic;
	vcpu->arch.pio_data = page_address(page);

	vcpu->arch.mce_banks = kzalloc(KVM_MAX_MCE_BANKS * sizeof(u64) * 4,
				       GFP_KERNEL_ACCOUNT);
	if (!vcpu->arch.mce_banks)
		goto fail_free_pio_data;
	vcpu->arch.mcg_cap = KVM_MAX_MCE_BANKS;

	...

    // 这里会调用到回调函数 vmx_create_vcpu，这个函数很关键
	r = static_call(kvm_x86_vcpu_create)(vcpu);
	if (r)
		goto free_guest_fpu;

	vcpu->arch.arch_capabilities = kvm_get_arch_capabilities();
	vcpu->arch.msr_platform_info = MSR_PLATFORM_INFO_CPUID_FAULT;
	kvm_vcpu_mtrr_init(vcpu);
	vcpu_load(vcpu);
	kvm_set_tsc_khz(vcpu, max_tsc_khz);
	kvm_vcpu_reset(vcpu, false);
	kvm_init_mmu(vcpu); // 内存虚拟化的初始化，需要进一步分析
	vcpu_put(vcpu);
	return 0;

	...
	return r;
}
```

除了做进一步初始化的工作，`kvm_arch_vcpu_create` 在 X86 下会调用 `vmx_create_vcpu`，架构相关的数据都是在这里进行初始化，包括 VMCS 的设置。

#### vmx_create_vcpu

```c
static int vmx_create_vcpu(struct kvm_vcpu *vcpu)
{
	struct vmx_uret_msr *tsx_ctrl;
    // vmx 的 VCPU 用该结构体表示，因为内核需要支持多种架构
    // kvm_vcpu 相当于一个父类，子类继承
	struct vcpu_vmx *vmx;
	int i, cpu, err;

	BUILD_BUG_ON(offsetof(struct vcpu_vmx, vcpu) != 0);
	vmx = to_vmx(vcpu); // kvm_vcpu 表示的是通用层面的 vcpu

	err = -ENOMEM;

    // 每个 vcpu 与 vpid 相关联
    // vpid 用于开启 ept 的情况，当进行 VCPU 的切换时就可以不用将 tlb 中的信息全部 flash

	vmx->vpid = allocate_vpid();

	...

	for (i = 0; i < kvm_nr_uret_msrs; ++i) {
		vmx->guest_uret_msrs[i].data = 0;
		vmx->guest_uret_msrs[i].mask = -1ull;
	}
	if (boot_cpu_has(X86_FEATURE_RTM)) {
		/*
		 * TSX_CTRL_CPUID_CLEAR is handledj in the CPUID interception.
		 * Keep the host value unchanged to avoid changing CPUID bits
		 * under the host kernel's feet.
		 */
		tsx_ctrl = vmx_find_uret_msr(vmx, MSR_IA32_TSX_CTRL);
		if (tsx_ctrl)
			tsx_ctrl->mask = ~(u64)TSX_CTRL_CPUID_CLEAR;
	}

	err = alloc_loaded_vmcs(&vmx->vmcs01);
	if (err < 0)
		goto free_pml;

	/* The MSR bitmap starts with all ones */
	bitmap_fill(vmx->shadow_msr_intercept.read, MAX_POSSIBLE_PASSTHROUGH_MSRS);
	bitmap_fill(vmx->shadow_msr_intercept.write, MAX_POSSIBLE_PASSTHROUGH_MSRS);

	vmx_disable_intercept_for_msr(vcpu, MSR_IA32_TSC, MSR_TYPE_R);

    ...

    // loaded_vmcs 指向当前 VCPU 对应的 VMCS 区域， vmcs01 表示普通虚拟化，
    // 如果是嵌套虚拟化，对应的是其他值。
	vmx->loaded_vmcs = &vmx->vmcs01;
    // 获取当前 cpu
	cpu = get_cpu();
    // 将 vcpu 与当前 cpu 绑定
	vmx_vcpu_load(vcpu, cpu);
	vcpu->cpu = cpu;
	init_vmcs(vmx); // 这两个函数是从 guest 切换到 host 的关键函数，如果想进一步了解 VMCS 可以从这里入手
	vmx_vcpu_put(vcpu);
	put_cpu();
	if (cpu_need_virtualize_apic_accesses(vcpu)) {
		err = alloc_apic_access_page(vcpu->kvm);
		if (err)
			goto free_vmcs;
	}

	if (enable_ept && !enable_unrestricted_guest) {
		err = init_rmode_identity_map(vcpu->kvm);
		if (err)
			goto free_vmcs;
	}

	...

	return 0;

	...
	return err;
}
```

完成初始化后将其保存到 kvm 中的 vcpus 结构中。

```c
kvm->vcpus[vcpu->vcpu_idx] = vcpu;
```

### QEMU 与 KVM 之间的共享数据

QEMU 和 KVM 之间经常需要共享数据，如 KVM 将 VM Exit 的信息放到共享内存中， QEMU 可以通过共享内存去获取这些数据。在 QEMU 通知 KVM 创建 VCPU 的起点函数 `kvm_vcpu_thread_fn` 中，会调用 `kvm_init_vcpu`，其会查询 QEMU 的 VCPU 列表，看是否有符合条件的 VCPU，如果没有则使用 ioctl 通知 KVM 创建一个。然后调用 `ioctl(KVM_GET_VCPU_MMAP_SIZE)` ，该接口返回 KVM 和 QEMU 共享内存的大小，代码如下：

```c
int kvm_init_vcpu(CPUState *cpu, Error **errp)
{
    KVMState *s = kvm_state;
    long mmap_size;
    int ret;

    trace_kvm_init_vcpu(cpu->cpu_index, kvm_arch_vcpu_id(cpu));

    ret = kvm_get_vcpu(s, kvm_arch_vcpu_id(cpu));

    ...

    cpu->kvm_fd = ret;
    cpu->kvm_state = s;
    cpu->vcpu_dirty = true;
    cpu->dirty_pages = 0;

    // 共享内存大小
    mmap_size = kvm_ioctl(s, KVM_GET_VCPU_MMAP_SIZE, 0);

   	...

    // kvm_fd: vCPU file descriptor for KVM.
    cpu->kvm_run = mmap(NULL, mmap_size, PROT_READ | PROT_WRITE, MAP_SHARED,
                        cpu->kvm_fd, 0);
    ...

    return ret;
}
```

`ioctl(KVM_GET_VCPU_MMAP_SIZE)` 返回的页存储的内容如下：

```c
	case KVM_GET_VCPU_MMAP_SIZE:
		if (arg)
			goto out;
		r = PAGE_SIZE;     /* struct kvm_run */
#ifdef CONFIG_X86
		r += PAGE_SIZE;    /* pio data page */
#endif
#ifdef CONFIG_KVM_MMIO
		r += PAGE_SIZE;    /* coalesced mmio ring page */
#endif
		break;
```

获取到内存大小后，QEMU 调用 mmap 将 KVM 的中的虚拟机句柄 fd 映射到 `cpu->kvm_run` 和 `cpu->kvm_dirty_gfns`。也就是说如果在 QEMU 访问 `cpu->kvm_run` 实际上访问的是 KVM 中的 `kvm_run` 信息。

### VCPU CPUID 构造

`kvm_init_vcpu` 最后调用 `kvm_arch_init_vcpu` 完成 VCPU 架构相关的初始化，大部分工作是构造虚拟机 VCPU 的 CPUID。通过 CPUID 虚拟机可以获得 CPU 的型号，具体性能参数等信息。

QEMU 命令行中指定 CPU 类型及其增加的或去掉的 CPU 特性，**QEMU 通过这些特性构造出一个 `cpuid_data`**，然后调用 VCPU 的`ioctl(KVM_SET_CPUID2)` 将构造的 CPUID 数据传到 KVM 中的 VCPU 相关的数据结构中，之后虚拟机内部执行 CPUID 指令会导致 VM Exit，然后陷入 KVM，KVM 会把数据返回给虚拟机。因为 CPUID 是特权指令，必须有 root 态下的内核进行处理。

### VCPU 的运行

首先**每个 VCPU 都会对应一个 VMCS，VMCS 对于 VCPU 的作用类似与进程描述符对于进程的作用**。其用来管理 VMX non-root Operation 的转换以及控制 VCPU 的行为。操作 VMCS 的指令包括 VMCLEAR、VMPTRLD、VMREAD 和 VMWRITE。VMCS 的区域大小为 4KB，VMM 通过它的 64 位地址进行访问。

[前面](#3. QEMU CPU 的创建)讲到 `kvm_vcpu_thread_fn` 创建 VCPU 线程并运行，并且跟踪了 QEMU 是怎样通过 `kvm_init_vcpu` 让 KVM 创建 VCPU 的，现在我们继续分析重要的 `do while` 循环。

```c
	do {
        if (cpu_can_run(cpu)) {
            r = kvm_cpu_exec(cpu);
            if (r == EXCP_DEBUG) {
                cpu_handle_guest_debug(cpu);
            }
        }
        qemu_wait_io_event(cpu);
    } while (!cpu->unplug || cpu_can_run(cpu));
```

调用 `cpu_can_run` 检查当前 cpu 是否可运行。这里有个问题，为什么用的是 `CPUState`，而不是 `X86CPU`，应该是 `CPUState` 中的数据就足够了。

```c
bool cpu_can_run(CPUState *cpu)
{
    if (cpu->stop) {
        return false;
    }
    if (cpu_is_stopped(cpu)) {
        return false;
    }
    return true;
}
```

如果不可运行，则调用 `qemu_wait_io_event` 将 cpu 挂在 `cpu->halt_cond` 条件上，带了全局锁 `qemu_global_mutex` 。

```C
void qemu_wait_io_event(CPUState *cpu)
{
    bool slept = false;

    while (cpu_thread_is_idle(cpu)) {
        if (!slept) {
            slept = true;
            qemu_plugin_vcpu_idle_cb(cpu);
        }
        qemu_cond_wait(cpu->halt_cond, &qemu_global_mutex);
    }
    if (slept) {
        qemu_plugin_vcpu_resume_cb(cpu);
    }
    qemu_wait_io_event_common(cpu);
}
```

当在 main 中执行 `vm_start` -> `resume_all_vcpus` -> `qemu_cpu_kick` 时，`qemu_cpu_kick` 会将 VCPU 唤醒。

```C
void qemu_cpu_kick(CPUState *cpu)
{
    qemu_cond_broadcast(cpu->halt_cond);
    if (cpus_accel->kick_vcpu_thread) {
        cpus_accel->kick_vcpu_thread(cpu);
    } else { /* default */
        cpus_kick_thread(cpu);
    }
}
```

接下来分析 `kvm_cpu_exec`，当然这个函数是 QEMU + KVM 的执行函数，tcg 的执行函数不一样。

```c
int kvm_cpu_exec(CPUState *cpu)
{
    struct kvm_run *run = cpu->kvm_run;
    int ret, run_ret;

    ...

    qemu_mutex_unlock_iothread();
    cpu_exec_start(cpu);

    do {
        MemTxAttrs attrs;

        if (cpu->vcpu_dirty) {
            kvm_arch_put_registers(cpu, KVM_PUT_RUNTIME_STATE);
            cpu->vcpu_dirty = false;
        }

        kvm_arch_pre_run(cpu, run);

        ...

        /* Read cpu->exit_request before KVM_RUN reads run->immediate_exit.
         * Matching barrier in kvm_eat_signals.
         */
        smp_rmb();

        run_ret = kvm_vcpu_ioctl(cpu, KVM_RUN, 0); // 在这里进入 kvm 执行，如果返回了，说明出现了 KVM 无法处理的
												   // 情况，需要返回到 QEMU 处理
        attrs = kvm_arch_post_run(cpu, run);

		...

        trace_kvm_run_exit(cpu->cpu_index, run->exit_reason); // exit_reason 是 KVM 写入的
        switch (run->exit_reason) { // 这里处理的都是 KVM 无法处理的情况
        case KVM_EXIT_IO:
            DPRINTF("handle_io\n");
            /* Called outside BQL */
            kvm_handle_io(run->io.port, attrs,
                          (uint8_t *)run + run->io.data_offset,
                          run->io.direction,
                          run->io.size,
                          run->io.count);
            ret = 0;
            break;
        case KVM_EXIT_MMIO:
            DPRINTF("handle_mmio\n");
            /* Called outside BQL */
            address_space_rw(&address_space_memory,
                             run->mmio.phys_addr, attrs,
                             run->mmio.data,
                             run->mmio.len,
                             run->mmio.is_write);
            ret = 0;
            break;
        case KVM_EXIT_IRQ_WINDOW_OPEN:
        case KVM_EXIT_SHUTDOWN:
        case KVM_EXIT_UNKNOWN:
        case KVM_EXIT_INTERNAL_ERROR:
        case KVM_EXIT_SYSTEM_EVENT:
            switch (run->system_event.type) {
            case KVM_SYSTEM_EVENT_SHUTDOWN:
            case KVM_SYSTEM_EVENT_RESET:
            case KVM_SYSTEM_EVENT_CRASH
            default:
                DPRINTF("kvm_arch_handle_exit\n");
                ret = kvm_arch_handle_exit(cpu, run);
                break;
            }
            break;
        default:
            DPRINTF("kvm_arch_handle_exit\n");
            ret = kvm_arch_handle_exit(cpu, run);
            break;
        }
    } while (ret == 0); // 循环执行，除非退出

    cpu_exec_end(cpu);
    qemu_mutex_lock_iothread();

    if (ret < 0) {
        cpu_dump_state(cpu, stderr, CPU_DUMP_CODE);
        vm_stop(RUN_STATE_INTERNAL_ERROR);
    }

    qatomic_set(&cpu->exit_request, 0);
    return ret;
}
```

`kvm_arch_pre_run` 首先做一些运行前的准备工作，如 `nmi` 和 `smi` 的[中断注入](https://github.com/UtopianFuture/UtopianFuture.github.io/blob/master/virtualization/Interrupt-Virtualization.md)，之后使用 `ioctl(KVM_RUN)` 系统调用通知 KVM 使该 KVM 运行起来。KVM 模块在处理该 ioctl 时会执行对应的 vmx 指令，将 VCPU 运行的物理 CPU 从 `VMX root` 转换成 `VMX non-root` ，开始运行虚拟机中的代码。虚拟机内部如果产生 `VM Exit`（哪些情况会导致 vm exit），就会退出到 KVM ，如果 KVM 无法处理就会分发到 QEMU ，也就是在 `ioctl(KVM_RUN)`  返回的时候调用 `kvm_arch_post_run` 进行初步的处理。然后根据共享内存 `kvm_run` （这个是第 5 节的内容）中的数据来判断退出原因，并进行处理。之后的 `switch` 就是处理 KVM 不能处理的问题。

接下来分析 `ioctl(KVM_RUN)` 是怎样在 kernel 中运行的。这个 `ioctl` 是在 `kvm_vcpu_ioctl` 中处理的，这个函数是专门处理 `VCPU ioctl` 的，和之前的两个处理函数不同（一个是处理 dev ，一个是处理 vm ）。

```c
static long kvm_vcpu_ioctl (struct file *filp,
			   unsigned int ioctl, unsigned long arg)
{
	struct kvm_vcpu *vcpu = filp->private_data;
	void __user *argp = (void __user *)arg;
	int r;
	struct kvm_fpu *fpu = NULL;
	struct kvm_sregs *kvm_sregs = NULL;

	...

	switch (ioctl) {
	case KVM_RUN: {
		struct pid *oldpid;
		r = -EINVAL;
		if (arg)
			goto out;
		oldpid = rcu_access_pointer(vcpu->pid);
		if (unlikely(oldpid != task_pid(current))) {
			/* The thread running this VCPU changed. */
			struct pid *newpid;

			r = kvm_arch_vcpu_run_pid_change(vcpu);
			if (r)
				break;

			newpid = get_task_pid(current, PIDTYPE_PID);
			rcu_assign_pointer(vcpu->pid, newpid);
			if (oldpid)
				synchronize_rcu();
			put_pid(oldpid);
		}
		r = kvm_arch_vcpu_ioctl_run(vcpu);
		trace_kvm_userspace_exit(vcpu->run->exit_reason, r);
		break;
	}
	case KVM_GET_REGS:
	case KVM_SET_REGS:
	case KVM_GET_SREGS:
	case KVM_SET_SREGS:
	case KVM_SET_MP_STATE:
	case KVM_SET_GUEST_DEBUG:
	case KVM_SET_SIGNAL_MASK:
	case KVM_GET_FPU:
	case KVM_SET_FPU:
	case KVM_GET_STATS_FD:
	default:
		r = kvm_arch_vcpu_ioctl(filp, ioctl, arg);
	}

	...

	return r;
}
```

对于 `KVM_RUN` 的情况，会调用 `kvm_arch_vcpu_ioctl_run` 进行处理。

```c
int kvm_arch_vcpu_ioctl_run(struct kvm_vcpu *vcpu)
{
	struct kvm_run *kvm_run = vcpu->run;
	int r;

	vcpu_load(vcpu); // 将 vcpu 中的 vmcs 数据加载到物理 cpu 中
	kvm_sigset_activate(vcpu);
	kvm_run->flags = 0;
	kvm_load_guest_fpu(vcpu);

	...

	if (kvm_run->immediate_exit)
		r = -EINTR;
	else
		r = vcpu_run(vcpu);

	...j

	vcpu_put(vcpu); // 恢复 host 现场
	return r;
}
```

`kvm_arch_vcpu_ioctl_run` 主要调用 `vcpu_run` ，

```c
static int vcpu_run(struct kvm_vcpu *vcpu)
{
	int r;
	struct kvm *kvm = vcpu->kvm;

	vcpu->srcu_idx = srcu_read_lock(&kvm->srcu);
	vcpu->arch.l1tf_flush_l1d = true;

	for (;;) {
		if (kvm_vcpu_running(vcpu)) {
			r = vcpu_enter_guest(vcpu);
		} else {
			r = vcpu_block(kvm, vcpu);
		}

		if (r <= 0) // 根据返回值决定是否要返回 QEMU，如果 KVM 能够处理，就进入下一次循环
			break;

		kvm_clear_request(KVM_REQ_UNBLOCK, vcpu);
		if (kvm_cpu_has_pending_timer(vcpu))
			kvm_inject_pending_timer_irqs(vcpu);

		if (dm_request_for_irq_injection(vcpu) &&
			kvm_vcpu_ready_for_interrupt_injection(vcpu)) {
			r = 0;
			vcpu->run->exit_reason = KVM_EXIT_IRQ_WINDOW_OPEN;
			++vcpu->stat.request_irq_exits;
			break;
		}

		if (__xfer_to_guest_mode_work_pending()) {
			srcu_read_unlock(&kvm->srcu, vcpu->srcu_idx);
			r = xfer_to_guest_mode_handle_work(vcpu);
			if (r)
				return r;
			vcpu->srcu_idx = srcu_read_lock(&kvm->srcu);
		}
	}

	srcu_read_unlock(&kvm->srcu, vcpu->srcu_idx);

	return r;
}
```

`vcpu_run` 和 QEMU 一样，首先通过调用 `kvm_vcpu_running` 判断当前 CPU 是否可运行

```c
static inline bool kvm_vcpu_running(struct kvm_vcpu *vcpu)
{
	if (is_guest_mode(vcpu))
		kvm_check_nested_events(vcpu);

	return (vcpu->arch.mp_state == KVM_MP_STATE_RUNNABLE &&
		!vcpu->arch.apf.halted);
}
```

`kvm_vcpu_running` 还要通过读取 vcpu 中的 `hflags` 和 `HF_GUEST_MASK` 判断当前 CPU 是否处于 guest mode，

```c
static inline bool is_guest_mode(struct kvm_vcpu *vcpu)
{
	return vcpu->arch.hflags & HF_GUEST_MASK;
}
```

如果可以运行，就会调用 `vcpu_enter_guest` 进入虚拟机，在进入虚拟机前会通过 `kvm_check_request` 对 `vcpu -> request` 上的请求进行处理。这个函数是 enter guestos, run guestos,  exit guestos 的重要函数，之后有需要还要再分析一下。

```c
static int vcpu_enter_guest(struct kvm_vcpu *vcpu)
{
	int r;
	bool req_int_win =
		dm_request_for_irq_injection(vcpu) &&
		kvm_cpu_accept_dm_intr(vcpu);
	fastpath_t exit_fastpath;

	bool req_immediate_exit = false;

	...

	preempt_disable();

    // 将 host 的状态保存到 vmcs 中
	static_call(kvm_x86_prepare_guest_switch)(vcpu);

	/*
	 * Disable IRQs before setting IN_GUEST_MODE.  Posted interrupt
	 * IPI are then delayed after guest entry, which ensures that they
	 * result in virtual interrupt delivery.
	 */
	local_irq_disable();
	vcpu->mode = IN_GUEST_MODE;

	...

	for (;;) {
        // 这里的回调函数为 vmx_vcpu_run
		exit_fastpath = static_call(kvm_x86_run)(vcpu);
		if (likely(exit_fastpath != EXIT_FASTPATH_REENTER_GUEST))
			break;

        if (unlikely(kvm_vcpu_exit_request(vcpu))) { // 检查是否有需要注入 guest 的事件
			exit_fastpath = EXIT_FASTPATH_EXIT_HANDLED;
			break;
		}

		if (vcpu->arch.apicv_active)
			static_call(kvm_x86_sync_pir_to_irr)(vcpu);
    }

	...

	vcpu->mode = OUTSIDE_GUEST_MODE;
	smp_wmb();

	static_call(kvm_x86_handle_exit_irqoff)(vcpu); // 先处理外部中断

	/*
	 * Consume any pending interrupts, including the possible source of
	 * VM-Exit on SVM and any ticks that occur between VM-Exit and now.
	 * An instruction is required after local_irq_enable() to fully unblock
	 * interrupts on processors that implement an interrupt shadow, the
	 * stat.exits increment will do nicely.
	 */
	kvm_before_interrupt(vcpu);
	local_irq_enable();
	++vcpu->stat.exits;
	local_irq_disable();
	kvm_after_interrupt(vcpu);

	...

    // 总的中断处理函数，在这里进行分发，下面分析，同时也给出了所有的中断处理函数
	r = static_call(kvm_x86_handle_exit)(vcpu, exit_fastpath);
	return r; // r <= 0 就需要返回到用户态进行处理

	...
}
```

将这些 `request` 处理完并 `inject_pending_event` 之后就调用 `kvm_mmu_reload` 处理内存虚拟化相关的东西。然后就是 `kvm_x86_prepare_guest_switch`，保存宿主机状态到 `VMCS` ，使虚拟机下次退出后能正常运行。

之后就是 vmx 的 run 回调 —— `vmx_vcpu_run` ，这是一个巨长的函数，我没有搞懂，只关注目前有用的部分。

```c
static fastpath_t vmx_vcpu_run(struct kvm_vcpu *vcpu)
{
	struct vcpu_vmx *vmx = to_vmx(vcpu);
	unsigned long cr3, cr4;

	...

	/* The actual VMENTER/EXIT is in the .noinstr.text section. */
	vmx_vcpu_enter_exit(vcpu, vmx);

	...

	vmx_register_cache_reset(vcpu);

	pt_guest_exit(vmx);

	kvm_load_host_xsave_state(vcpu);

	if (is_guest_mode(vcpu)) {
		/*
		 * Track VMLAUNCH/VMRESUME that have made past guest state
		 * checking.
		 */
		if (vmx->nested.nested_run_pending &&
		    !vmx->exit_reason.failed_vmentry)
			++vcpu->stat.nested_run;

		vmx->nested.nested_run_pending = 0;
	}

	vmx->idt_vectoring_info = 0;

	if (unlikely(vmx->fail)) {
		vmx->exit_reason.full = 0xdead;
		return EXIT_FASTPATH_NONE;
	}

	vmx->exit_reason.full = vmcs_read32(VM_EXIT_REASON); // 读取退出原因
	if (unlikely((u16)vmx->exit_reason.basic == EXIT_REASON_MCE_DURING_VMENTRY))
		kvm_machine_check();

	if (likely(!vmx->exit_reason.failed_vmentry))
		vmx->idt_vectoring_info = vmcs_read32(IDT_VECTORING_INFO_FIELD);

	trace_kvm_exit(vmx->exit_reason.full, vcpu, KVM_ISA_VMX);

	if (unlikely(vmx->exit_reason.failed_vmentry))
		return EXIT_FASTPATH_NONE;

	vmx->loaded_vmcs->launched = 1;

	vmx_recover_nmi_blocking(vmx);
	vmx_complete_interrupts(vmx);

	if (is_guest_mode(vcpu))
		return EXIT_FASTPATH_NONE;

	return vmx_exit_handlers_fastpath(vcpu);
}
```

`vmx_vcpu_enter_exit` 最终会 `call vmx_vmenter` ，在这个汇编里使用 `vmlaunch` 指令切换到 guest mode,

```c
/**
 * vmx_vmenter - VM-Enter the current loaded VMCS
 *
 * %RFLAGS.ZF:	!VMCS.LAUNCHED, i.e. controls VMLAUNCH vs. VMRESUME
 *
 * Returns:
 *	%RFLAGS.CF is set on VM-Fail Invalid
 *	%RFLAGS.ZF is set on VM-Fail Valid
 *	%RFLAGS.{CF,ZF} are cleared on VM-Success, i.e. VM-Exit
 *
 * Note that VMRESUME/VMLAUNCH fall-through and return directly if
 * they VM-Fail, whereas a successful VM-Enter + VM-Exit will jump
 * to vmx_vmexit.
 */
SYM_FUNC_START_LOCAL(vmx_vmenter)
	/* EFLAGS.ZF is set if VMCS.LAUNCHED == 0 */
	je 2f

1:	vmresume
	ret

2:	vmlaunch
	ret

3:	cmpb $0, kvm_rebooting
	je 4f
	ret
4:	ud2

	_ASM_EXTABLE(1b, 3b)
	_ASM_EXTABLE(2b, 3b)

SYM_FUNC_END(vmx_vmenter)
```

同时从 guest mode 中退出也是在 `vmx_vcpu_run` 中处理的，即使用 `vmcs_read32` 读取退出原因。

```c
vmx->exit_reason.full = vmcs_read32(VM_EXIT_REASON);
```

当 `vmx_vcpu_run` 执行完返回时，已经完成了一次 `VM Exit` 和 `VM Entry` 。

退出之后同样回调函数 `vmx_handle_exit` 处理外部中断。

```c
static struct kvm_x86_ops vmx_x86_ops __initdata = {
	...

	.run = vmx_vcpu_run,
	.handle_exit = vmx_handle_exit,

	...

};
```

最后处理完中断后根据 `vcpu_enter_guest` 的返回值判断是否需要返回到 QEMU ，如果 `vcpu_enter_guest` 返回值小于等于 0 ，会导致退出循环，进而该 `ioctl` 返回到用户态 QEMU ，如果返回 1 ，则 KVM 能处理，继续下一轮执行。

我们进一步分析有哪些原因导致 guest 退出，同时是怎样处理的，VMX 的中断处理回调函数为 `vmx_handle_exit`，

```c
/*
 * The guest has exited.  See if we can fix it or if we need userspace
 * assistance.
 */
static int __vmx_handle_exit(struct kvm_vcpu *vcpu, fastpath_t exit_fastpath)
{
	struct vcpu_vmx *vmx = to_vmx(vcpu);
	union vmx_exit_reason exit_reason = vmx->exit_reason;
	u32 vectoring_info = vmx->idt_vectoring_info;
	u16 exit_handler_index;

	/*
	 * Flush logged GPAs PML buffer, this will make dirty_bitmap more
	 * updated. Another good is, in kvm_vm_ioctl_get_dirty_log, before
	 * querying dirty_bitmap, we only need to kick all vcpus out of guest
	 * mode as if vcpus is in root mode, the PML buffer must has been
	 * flushed already.  Note, PML is never enabled in hardware while
	 * running L2.
	 */
	if (enable_pml && !is_guest_mode(vcpu))
		vmx_flush_pml_buffer(vcpu);

	/*
	 * We should never reach this point with a pending nested VM-Enter, and
	 * more specifically emulation of L2 due to invalid guest state (see
	 * below) should never happen as that means we incorrectly allowed a
	 * nested VM-Enter with an invalid vmcs12.
	 */
	if (KVM_BUG_ON(vmx->nested.nested_run_pending, vcpu->kvm))
		return -EIO;

	/* If guest state is invalid, start emulating */
	if (vmx->emulation_required)
		return handle_invalid_guest_state(vcpu);

	if (is_guest_mode(vcpu)) {
		/*
		 * PML is never enabled when running L2, bail immediately if a
		 * PML full exit occurs as something is horribly wrong.
		 */
		if (exit_reason.basic == EXIT_REASON_PML_FULL)
			goto unexpected_vmexit;

		/*
		 * The host physical addresses of some pages of guest memory
		 * are loaded into the vmcs02 (e.g. vmcs12's Virtual APIC
		 * Page). The CPU may write to these pages via their host
		 * physical address while L2 is running, bypassing any
		 * address-translation-based dirty tracking (e.g. EPT write
		 * protection).
		 *
		 * Mark them dirty on every exit from L2 to prevent them from
		 * getting out of sync with dirty tracking.
		 */
		nested_mark_vmcs12_pages_dirty(vcpu);

		if (nested_vmx_reflect_vmexit(vcpu))
			return 1;
	}

	if (exit_reason.failed_vmentry) {
		dump_vmcs(vcpu);
		vcpu->run->exit_reason = KVM_EXIT_FAIL_ENTRY;
		vcpu->run->fail_entry.hardware_entry_failure_reason
			= exit_reason.full;
		vcpu->run->fail_entry.cpu = vcpu->arch.last_vmentry_cpu;
		return 0;
	}

	if (unlikely(vmx->fail)) {
		dump_vmcs(vcpu);
		vcpu->run->exit_reason = KVM_EXIT_FAIL_ENTRY;
		vcpu->run->fail_entry.hardware_entry_failure_reason
			= vmcs_read32(VM_INSTRUCTION_ERROR);
		vcpu->run->fail_entry.cpu = vcpu->arch.last_vmentry_cpu;
		return 0;
	}

	/*
	 * Note:
	 * Do not try to fix EXIT_REASON_EPT_MISCONFIG if it caused by
	 * delivery event since it indicates guest is accessing MMIO.
	 * The vm-exit can be triggered again after return to guest that
	 * will cause infinite loop.
	 */
	if ((vectoring_info & VECTORING_INFO_VALID_MASK) &&
	    (exit_reason.basic != EXIT_REASON_EXCEPTION_NMI &&
	     exit_reason.basic != EXIT_REASON_EPT_VIOLATION &&
	     exit_reason.basic != EXIT_REASON_PML_FULL &&
	     exit_reason.basic != EXIT_REASON_APIC_ACCESS &&
	     exit_reason.basic != EXIT_REASON_TASK_SWITCH)) {
		int ndata = 3;

		vcpu->run->exit_reason = KVM_EXIT_INTERNAL_ERROR;
		vcpu->run->internal.suberror = KVM_INTERNAL_ERROR_DELIVERY_EV;
		vcpu->run->internal.data[0] = vectoring_info;
		vcpu->run->internal.data[1] = exit_reason.full;
		vcpu->run->internal.data[2] = vcpu->arch.exit_qualification;
		if (exit_reason.basic == EXIT_REASON_EPT_MISCONFIG) {
			vcpu->run->internal.data[ndata++] =
				vmcs_read64(GUEST_PHYSICAL_ADDRESS);
		}
		vcpu->run->internal.data[ndata++] = vcpu->arch.last_vmentry_cpu;
		vcpu->run->internal.ndata = ndata;
		return 0;
	}

	if (unlikely(!enable_vnmi &&
		     vmx->loaded_vmcs->soft_vnmi_blocked)) {
		if (!vmx_interrupt_blocked(vcpu)) {
			vmx->loaded_vmcs->soft_vnmi_blocked = 0;
		} else if (vmx->loaded_vmcs->vnmi_blocked_time > 1000000000LL &&
			   vcpu->arch.nmi_pending) {
			/*
			 * This CPU don't support us in finding the end of an
			 * NMI-blocked window if the guest runs with IRQs
			 * disabled. So we pull the trigger after 1 s of
			 * futile waiting, but inform the user about this.
			 */
			printk(KERN_WARNING "%s: Breaking out of NMI-blocked "
			       "state on VCPU %d after 1 s timeout\n",
			       __func__, vcpu->vcpu_id);
			vmx->loaded_vmcs->soft_vnmi_blocked = 0;
		}
	}

	if (exit_fastpath != EXIT_FASTPATH_NONE)
		return 1;

	if (exit_reason.basic >= kvm_vmx_max_exit_handlers)
		goto unexpected_vmexit;
#ifdef CONFIG_RETPOLINE
	if (exit_reason.basic == EXIT_REASON_MSR_WRITE)
		return kvm_emulate_wrmsr(vcpu);
	else if (exit_reason.basic == EXIT_REASON_PREEMPTION_TIMER)
		return handle_preemption_timer(vcpu);
	else if (exit_reason.basic == EXIT_REASON_INTERRUPT_WINDOW)
		return handle_interrupt_window(vcpu);
	else if (exit_reason.basic == EXIT_REASON_EXTERNAL_INTERRUPT)
		return handle_external_interrupt(vcpu);
	else if (exit_reason.basic == EXIT_REASON_HLT)
		return kvm_emulate_halt(vcpu);
	else if (exit_reason.basic == EXIT_REASON_EPT_MISCONFIG)
		return handle_ept_misconfig(vcpu);
#endif

	exit_handler_index = array_index_nospec((u16)exit_reason.basic,
						kvm_vmx_max_exit_handlers);
	if (!kvm_vmx_exit_handlers[exit_handler_index])
		goto unexpected_vmexit;

	return kvm_vmx_exit_handlers[exit_handler_index](vcpu); // 为什么上面的要单独列出来？

unexpected_vmexit:
	vcpu_unimpl(vcpu, "vmx: unexpected exit reason 0x%x\n",
		    exit_reason.full);
	dump_vmcs(vcpu);
	vcpu->run->exit_reason = KVM_EXIT_INTERNAL_ERROR;
	vcpu->run->internal.suberror =
			KVM_INTERNAL_ERROR_UNEXPECTED_EXIT_REASON;
	vcpu->run->internal.ndata = 2;
	vcpu->run->internal.data[0] = exit_reason.full;
	vcpu->run->internal.data[1] = vcpu->arch.last_vmentry_cpu;
	return 0;
}
```

这里是所有的中断处理函数，

```c
/*
 * The exit handlers return 1 if the exit was handled fully and guest execution
 * may resume.  Otherwise they set the kvm_run parameter to indicate what needs
 * to be done to userspace and return 0.
 */
static int (*kvm_vmx_exit_handlers[])(struct kvm_vcpu *vcpu) = {
	[EXIT_REASON_EXCEPTION_NMI]           = handle_exception_nmi,
	[EXIT_REASON_EXTERNAL_INTERRUPT]      = handle_external_interrupt,
	[EXIT_REASON_TRIPLE_FAULT]            = handle_triple_fault,
	[EXIT_REASON_NMI_WINDOW]	      = handle_nmi_window,
	[EXIT_REASON_IO_INSTRUCTION]          = handle_io,
	[EXIT_REASON_CR_ACCESS]               = handle_cr,
	[EXIT_REASON_DR_ACCESS]               = handle_dr,
	[EXIT_REASON_CPUID]                   = kvm_emulate_cpuid,
	[EXIT_REASON_MSR_READ]                = kvm_emulate_rdmsr,
	[EXIT_REASON_MSR_WRITE]               = kvm_emulate_wrmsr,
	[EXIT_REASON_INTERRUPT_WINDOW]        = handle_interrupt_window,
	[EXIT_REASON_HLT]                     = kvm_emulate_halt,
	[EXIT_REASON_INVD]		      = kvm_emulate_invd,
	[EXIT_REASON_INVLPG]		      = handle_invlpg,
	[EXIT_REASON_RDPMC]                   = kvm_emulate_rdpmc,
	[EXIT_REASON_VMCALL]                  = kvm_emulate_hypercall,
	[EXIT_REASON_VMCLEAR]		      = handle_vmx_instruction,
	[EXIT_REASON_VMLAUNCH]		      = handle_vmx_instruction,
	[EXIT_REASON_VMPTRLD]		      = handle_vmx_instruction,
	[EXIT_REASON_VMPTRST]		      = handle_vmx_instruction,
	[EXIT_REASON_VMREAD]		      = handle_vmx_instruction,
	[EXIT_REASON_VMRESUME]		      = handle_vmx_instruction,
	[EXIT_REASON_VMWRITE]		      = handle_vmx_instruction,
	[EXIT_REASON_VMOFF]		      = handle_vmx_instruction,
	[EXIT_REASON_VMON]		      = handle_vmx_instruction,
	[EXIT_REASON_TPR_BELOW_THRESHOLD]     = handle_tpr_below_threshold,
	[EXIT_REASON_APIC_ACCESS]             = handle_apic_access,
	[EXIT_REASON_APIC_WRITE]              = handle_apic_write,
	[EXIT_REASON_EOI_INDUCED]             = handle_apic_eoi_induced,
	[EXIT_REASON_WBINVD]                  = kvm_emulate_wbinvd,
	[EXIT_REASON_XSETBV]                  = kvm_emulate_xsetbv,
	[EXIT_REASON_TASK_SWITCH]             = handle_task_switch,
	[EXIT_REASON_MCE_DURING_VMENTRY]      = handle_machine_check,
	[EXIT_REASON_GDTR_IDTR]		      = handle_desc,
	[EXIT_REASON_LDTR_TR]		      = handle_desc,
	[EXIT_REASON_EPT_VIOLATION]	      = handle_ept_violation,
	[EXIT_REASON_EPT_MISCONFIG]           = handle_ept_misconfig,
	[EXIT_REASON_PAUSE_INSTRUCTION]       = handle_pause,
	[EXIT_REASON_MWAIT_INSTRUCTION]	      = kvm_emulate_mwait,
	[EXIT_REASON_MONITOR_TRAP_FLAG]       = handle_monitor_trap,
	[EXIT_REASON_MONITOR_INSTRUCTION]     = kvm_emulate_monitor,
	[EXIT_REASON_INVEPT]                  = handle_vmx_instruction,
	[EXIT_REASON_INVVPID]                 = handle_vmx_instruction,
	[EXIT_REASON_RDRAND]                  = kvm_handle_invalid_op,
	[EXIT_REASON_RDSEED]                  = kvm_handle_invalid_op,
	[EXIT_REASON_PML_FULL]		      = handle_pml_full,
	[EXIT_REASON_INVPCID]                 = handle_invpcid,
	[EXIT_REASON_VMFUNC]		      = handle_vmx_instruction,
	[EXIT_REASON_PREEMPTION_TIMER]	      = handle_preemption_timer,
	[EXIT_REASON_ENCLS]		      = handle_encls,
	[EXIT_REASON_BUS_LOCK]                = handle_bus_lock_vmexit,
};
```

如果 `kvm_vcpu_running` 判断出该 cpu 不能执行，那么就会调用 `vcpu_block` ，最终调用 `schedule` 请求调度，**让出物理 cpu** 。

我们再来看看 tcg 是怎样让 vcpu 运行的。

`rr_start_vcpu_thread` / `mttcg_start_vcpu_thread` ->

​			`rr_cpu_thread_fn`（单线程）/ `mttcg_cpu_thread_fn`（多线程）->

​						`tcg_cpus_exec` ->

​									`cpu_exec` ->

​												`cpu_exec_enter`

```c
static void *mttcg_cpu_thread_fn(void *arg)
{
    ...

    /* process any pending work */
    cpu->exit_request = 1;

    // 同样是一个 while 循环
    do {
        if (cpu_can_run(cpu)) {
            int r;
            qemu_mutex_unlock_iothread();
            r = tcg_cpus_exec(cpu);
            qemu_mutex_lock_iothread();
            switch (r) {
            case EXCP_DEBUG:
                cpu_handle_guest_debug(cpu);
                break;
            case EXCP_HALTED:
                /*
                 * during start-up the vCPU is reset and the thread is
                 * kicked several times. If we don't ensure we go back
                 * to sleep in the halted state we won't cleanly
                 * start-up when the vCPU is enabled.
                 *
                 * cpu->halted should ensure we sleep in wait_io_event
                 */
                g_assert(cpu->halted);
                break;
            case EXCP_ATOMIC:
                qemu_mutex_unlock_iothread();
                cpu_exec_step_atomic(cpu);
                qemu_mutex_lock_iothread();
            default:
                /* Ignore everything else? */
                break;
            }
        }

        qatomic_mb_set(&cpu->exit_request, 0);
        qemu_wait_io_event(cpu);
    } while (!cpu->unplug || cpu_can_run(cpu));

    tcg_cpus_destroy(cpu);
    qemu_mutex_unlock_iothread();
    rcu_remove_force_rcu_notifier(&force_rcu.notifier);
    rcu_unregister_thread();
    return NULL;
}
```

### VCPU 的调度

虚拟机的**每个 VCPU 都对应宿主机中的一个线程，通过宿主及内核调度器进行统一调度管理**。如果不将虚拟机的 VCPU 线程绑定到物理 CPU 上，那么 VCPU 线程可能每次运行时被调度到不同的物理 CPU 上。每个物理 CPU 都有一个指向当前 VMCS 的指针——`current_vmcs`。而 VCPU 调度的本质就是**将物理 CPU 的 per_current 指向需要调度的 VCPU 的 VMCS**。这里涉及到两个重要的函数：

`vcpu_load` 负责将 VCPU 状态加载到物理 CPU 上，`vcpu_put` 负责将当前物理 CPU 上运行的 VCPU 的 VMCS 调度出去并保存。

```c
/*
 * Switches to specified vcpu, until a matching vcpu_put()
 */
void vcpu_load(struct kvm_vcpu *vcpu)
{
	int cpu = get_cpu(); // 获取当前 CPU ID

	__this_cpu_write(kvm_running_vcpu, vcpu);
	preempt_notifier_register(&vcpu->preempt_notifier);
	kvm_arch_vcpu_load(vcpu, cpu); // 不同架构的加载函数，将 VMCS 加载到 cpu 中，X86 对应的是 vmx_vcpu_load
	put_cpu(); // 开启抢占
}
EXPORT_SYMBOL_GPL(vcpu_load);
```

```c
void vcpu_put(struct kvm_vcpu *vcpu)
{
	preempt_disable();
	kvm_arch_vcpu_put(vcpu);
	preempt_notifier_unregister(&vcpu->preempt_notifier);
	__this_cpu_write(kvm_running_vcpu, NULL);
	preempt_enable();
}
EXPORT_SYMBOL_GPL(vcpu_put);
```

`kvm_arch_vcpu_ioctl_run`  是 `ioctl(KVM_RUN)` 的处理函数，它在函数开始和结束时会调用 `vcpu_load` 和 `vcpu_put`。

```c
int kvm_arch_vcpu_ioctl_run(struct kvm_vcpu *vcpu)
{
	struct kvm_run *kvm_run = vcpu->run;
	int r;

    // here
	vcpu_load(vcpu);
	kvm_sigset_activate(vcpu);
	kvm_run->flags = 0;
	kvm_load_guest_fpu(vcpu);

	...

	if (kvm_run->immediate_exit)
		r = -EINTR;
	else // cpu run
		r = vcpu_run(vcpu);

	...

    // here
	vcpu_put(vcpu);
	return r;
}
```
