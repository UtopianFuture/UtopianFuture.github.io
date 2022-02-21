## Create and Run Virtual Machine

前面有两篇文章可以作为理解 [kvm](https://github.com/UtopianFuture/UtopianFuture.github.io/blob/master/virtualization/kvm.md) 和[虚拟化](https://github.com/UtopianFuture/UtopianFuture.github.io/blob/master/virtualization/linux-system-virtualization.md)的入门资料，接下来结合 kvm 源码分析在 kvm 端虚拟机是怎样创建和运行的。

代码：

kvm: linux-5.15(commit id: f6274b06e326d8471cdfb52595f989a90f5e888f)

qemu: qemu-6.2.0-rc4(commit id: a3607def89f9cd68c1b994e1030527df33aa91d0)

qemu - kvm

QEMU 和 KVM 是通过 IOCTL 进行配合的，qemu 通过 `kvm_ioctl` `kvm_vm_ioctl`、`kvm_vcpu_ioctl`、`kvm_device_ioctl`等函数向 kvm 发起请求。

```c
int kvm_ioctl(KVMState *s, int type, ...)
{
    int ret;
    void *arg;
    va_list ap;

    va_start(ap, type);
    arg = va_arg(ap, void *);
    va_end(ap);

    trace_kvm_ioctl(type, arg);
    ret = ioctl(s->fd, type, arg);
    if (ret == -1) {
        ret = -errno;
    }
    return ret;
}
```

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

```c
int kvm_vcpu_ioctl(CPUState *cpu, int type, ...)
{
    int ret;
    void *arg;
    va_list ap;

    va_start(ap, type);
    arg = va_arg(ap, void *);
    va_end(ap);

    trace_kvm_vcpu_ioctl(cpu->cpu_index, type, arg);
    ret = ioctl(cpu->kvm_fd, type, arg);
    if (ret == -1) {
        ret = -errno;
    }
    return ret;
}
```

```c
int kvm_device_ioctl(int fd, int type, ...)
{
    int ret;
    void *arg;
    va_list ap;

    va_start(ap, type);
    arg = va_arg(ap, void *);
    va_end(ap);

    trace_kvm_device_ioctl(fd, type, arg);
    ret = ioctl(fd, type, arg);
    if (ret == -1) {
        ret = -errno;
    }
    return ret;
}
```

QEMU 和 KVM 的配合流程如下：

```c
						qemu <---> kvm
          kvm_init 初始化 vm    |    KVM_GET_API_VERSION
               创建 vm          |    KVM_CREATE_VM
          pc_init_pci 初始化    |
     pc_cpus_init 初始化 cpu    |    KVM_CREATE_VCPU
   pc_memory_init 初始化 mem    |    KVM_SET_USER_MEMORY_REGION
           main_loop vm 运行    |    KVM_RUN
```

接下来我们一一分析 qemu 中是如何调用的，然后 kvm 中是如何实现的。

### kvm_dev_ioctl

qemu 在 `kvm_init` 中创建虚拟机，调用过程如下：

```plain
#0  kvm_ioctl (s=0x55555681c140, type=44544) at ../accel/kvm/kvm-all.c:2981
#1  0x0000555555d0a975 in kvm_init (ms=0x555556a2f000) at ../accel/kvm/kvm-all.c:2352
#2  0x0000555555af387b in accel_init_machine (accel=0x55555681c140, ms=0x555556a2f000) at ../accel/accel-softmmu.c:39
#3  0x0000555555c0752b in do_configure_accelerator (opaque=0x7fffffffdfbd, opts=0x555556a473e0, errp=0x55555677c100 <error_fatal>) at ../softmmu/vl.c:2348
#4  0x0000555555f01307 in qemu_opts_foreach (list=0x5555566a18c0 <qemu_accel_opts>, func=0x555555c07401 <do_configure_accelerator>, opaque=0x7fffffffdfbd, errp=0x55555677c100 <error_fatal>)
    at ../util/qemu-option.c:1135
#5  0x0000555555c07790 in configure_accelerators (progname=0x7fffffffe68b "/home/guanshun/gitlab/qemu-newest/build/x86_64-softmmu/qemu-system-x86_64") at ../softmmu/vl.c:2414
#6  0x0000555555c0a834 in qemu_init (argc=16, argv=0x7fffffffe278, envp=0x7fffffffe300) at ../softmmu/vl.c:3724
#7  0x000055555583b6f5 in main (argc=16, argv=0x7fffffffe278, envp=0x7fffffffe300) at ../softmmu/main.c:49
```

```c
static int kvm_init(MachineState *ms)
{
    MachineClass *mc = MACHINE_GET_CLASS(ms);

    ...

    qemu_mutex_init(&kml_slots_lock);

    s = KVM_STATE(ms->accelerator);

    ...

    s->fd = qemu_open_old("/dev/kvm", O_RDWR);
    if (s->fd == -1) {
        fprintf(stderr, "Could not access KVM kernel module: %m\n");
        ret = -errno;
        goto err;
    }

    ret = kvm_ioctl(s, KVM_GET_API_VERSION, 0);
    if (ret < KVM_API_VERSION) {
        if (ret >= 0) {
            ret = -EINVAL;
        }
        fprintf(stderr, "kvm version too old\n");
        goto err;
    }

    if (ret > KVM_API_VERSION) {
        ret = -EINVAL;
        fprintf(stderr, "kvm version not supported\n");
        goto err;
    }

    ...

    do {
        ret = kvm_ioctl(s, KVM_CREATE_VM, type);
    } while (ret == -EINTR);

    if (ret < 0) {
        fprintf(stderr, "ioctl(KVM_CREATE_VM) failed: %d %s\n", -ret,
                strerror(-ret));

    soft_vcpus_limit = kvm_recommended_vcpus(s);
    hard_vcpus_limit = kvm_max_vcpus(s);

    while (nc->name) {
        if (nc->num > soft_vcpus_limit) {
            warn_report("Number of %s cpus requested (%d) exceeds "
                        "the recommended cpus supported by KVM (%d)",
                        nc->name, nc->num, soft_vcpus_limit);

            if (nc->num > hard_vcpus_limit) {
                fprintf(stderr, "Number of %s cpus requested (%d) exceeds "
                        "the maximum cpus supported by KVM (%d)\n",
                        nc->name, nc->num, hard_vcpus_limit);
                exit(1);
            }
        }
        nc++;
    }

    missing_cap = kvm_check_extension_list(s, kvm_required_capabilites);
    if (!missing_cap) {
        missing_cap =
            kvm_check_extension_list(s, kvm_arch_required_capabilities);
    }
    if (missing_cap) {
        ret = -EINVAL;
        fprintf(stderr, "kvm does not support %s\n%s",
                missing_cap->name, upgrade_note);
        goto err;
    }

    s->coalesced_mmio = kvm_check_extension(s, KVM_CAP_COALESCED_MMIO);
    s->coalesced_pio = s->coalesced_mmio &&
                       kvm_check_extension(s, KVM_CAP_COALESCED_PIO);


    ...

    return ret;
}
```

这里有点奇怪，虽然 `KVM_GET_API_VERSION` 和 `KVM_CREATE_VM` 在 qemu 中是通过 `kvm_ioctl` 调用的，但是在 kvm 中是通过 `kvm_dev_ioctl` 响应的。

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

这里 `kvm_dev_ioctl_create_vm` 和 `kvm_arch_dev_ioctl` 两个函数需要详细分析。

`kvm_dev_ioctl_create_vm` 是在 kvm 中创建虚拟机的，会调用到 `kvm_create_vm`，而在分析 `kvm_create_vm` 函数前需要了解一个关键的数据结构—— `struct kvm` ，`struct kvm` 是内核中用来表示一个虚拟机的数据结构，一个虚拟机包含多个 vcpu 和多个内存条，在 `struct kvm` 分别用 `memslots` 和 `vcpus` 表示，还有诸多的总线，外设，都在这个结构体中表示。

```c
struct kvm {

    ...

	struct mm_struct *mm; /* userspace tied to this vm */
	struct kvm_memslots __rcu *memslots[KVM_ADDRESS_SPACE_NUM];
	struct kvm_vcpu *vcpus[KVM_MAX_VCPUS]; // vcpu list

	...

	/*
	 * created_vcpus is protected by kvm->lock, and is incremented
	 * at the beginning of KVM_CREATE_VCPU.  online_vcpus is only
	 * incremented after storing the kvm_vcpu pointer in vcpus,
	 * and is accessed atomically.
	 */
	atomic_t online_vcpus;
	int created_vcpus;
	int last_boosted_vcpu;
	struct list_head vm_list; // vm list in host
	struct mutex lock;
	struct kvm_io_bus __rcu *buses[KVM_NR_BUSES];
	...
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

	...
};
```

接下来我们再来看看 `kvm_crate_vm` ，这个函数就是初始化一些关键的数据结构。

```c
static struct kvm *kvm_create_vm(unsigned long type)
{
	struct kvm *kvm = kvm_arch_alloc_vm();
	int r = -ENOMEM;
	int i;

	if (!kvm)
		return ERR_PTR(-ENOMEM);

	KVM_MMU_LOCK_INIT(kvm);
	mmgrab(current->mm);
	kvm->mm = current->mm;
	kvm_eventfd_init(kvm);
	mutex_init(&kvm->lock);
	mutex_init(&kvm->irq_lock);
	mutex_init(&kvm->slots_lock);
	mutex_init(&kvm->slots_arch_lock);
	spin_lock_init(&kvm->mn_invalidate_lock);
	rcuwait_init(&kvm->mn_memslots_update_rcuwait);

	INIT_LIST_HEAD(&kvm->devices);

	BUILD_BUG_ON(KVM_MEM_SLOTS_NUM > SHRT_MAX);

    ...

	refcount_set(&kvm->users_count, 1);
	for (i = 0; i < KVM_ADDRESS_SPACE_NUM; i++) {
		struct kvm_memslots *slots = kvm_alloc_memslots();

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

	r = kvm_arch_init_vm(kvm, type);
	if (r)
		goto out_err_no_arch_destroy_vm;

	r = hardware_enable_all();
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

	mutex_lock(&kvm_lock);
	list_add(&kvm->vm_list, &vm_list);
	mutex_unlock(&kvm_lock);

	preempt_notifier_inc();
	kvm_init_pm_notifier(kvm);

	return kvm;

	...
}
```

接下来是 `kvm_vm_ioctl`

### kvm_vm_ioctl

qeme 中调用 `kvm_vm_ioctl` 的函数很多，就不一一列举，还是看看 `kvm_init` 中的例子。

```plain
#0  kvm_vm_ioctl (s=0x55555681c140, type=44547) at ../accel/kvm/kvm-all.c:2999
#1  0x0000555555d07d57 in kvm_vm_check_extension (s=0x55555681c140, extension=9) at ../accel/kvm/kvm-all.c:1155
#2  0x0000555555d0a696 in kvm_recommended_vcpus (s=0x55555681c140) at ../accel/kvm/kvm-all.c:2279
#3  0x0000555555d0ac02 in kvm_init (ms=0x555556a2f000) at ../accel/kvm/kvm-all.c:2423
#4  0x0000555555af387b in accel_init_machine (accel=0x55555681c140, ms=0x555556a2f000) at ../accel/accel-softmmu.c:39
#5  0x0000555555c0752b in do_configure_accelerator (opaque=0x7fffffffdfbd, opts=0x555556a473e0, errp=0x55555677c100 <error_fatal>) at ../softmmu/vl.c:2348
#6  0x0000555555f01307 in qemu_opts_foreach (list=0x5555566a18c0 <qemu_accel_opts>, func=0x555555c07401 <do_configure_accelerator>, opaque=0x7fffffffdfbd, errp=0x55555677c100 <error_fatal>)
    at ../util/qemu-option.c:1135
#7  0x0000555555c07790 in configure_accelerators (progname=0x7fffffffe68b "/home/guanshun/gitlab/qemu-newest/build/x86_64-softmmu/qemu-system-x86_64") at ../softmmu/vl.c:2414
#8  0x0000555555c0a834 in qemu_init (argc=16, argv=0x7fffffffe278, envp=0x7fffffffe300) at ../softmmu/vl.c:3724
#9  0x000055555583b6f5 in main (argc=16, argv=0x7fffffffe278, envp=0x7fffffffe300) at ../softmmu/main.c:49
```

`kvm_vm_ioctl` 需要处理的 case 很多，这里先分析几个关键的 case。

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
	case KVM_ENABLE_CAP: {
		struct kvm_enable_cap cap;

		r = -EFAULT;
		if (copy_from_user(&cap, argp, sizeof(cap)))
			goto out;
		r = kvm_vm_ioctl_enable_cap_generic(kvm, &cap);
		break;
	}
	case KVM_SET_USER_MEMORY_REGION: {
		struct kvm_userspace_memory_region kvm_userspace_mem;

		r = -EFAULT;
		if (copy_from_user(&kvm_userspace_mem, argp,
						sizeof(kvm_userspace_mem)))
			goto out;

		r = kvm_vm_ioctl_set_memory_region(kvm, &kvm_userspace_mem);
		break;
	}
#ifdef CONFIG_KVM_MMIO
	case KVM_REGISTER_COALESCED_MMIO: {
		struct kvm_coalesced_mmio_zone zone;

		r = -EFAULT;
		if (copy_from_user(&zone, argp, sizeof(zone)))
			goto out;
		r = kvm_vm_ioctl_register_coalesced_mmio(kvm, &zone);
		break;
	}
	case KVM_UNREGISTER_COALESCED_MMIO: {
		struct kvm_coalesced_mmio_zone zone;

		r = -EFAULT;
		if (copy_from_user(&zone, argp, sizeof(zone)))
			goto out;
		r = kvm_vm_ioctl_unregister_coalesced_mmio(kvm, &zone);
		break;
	}
#endif
	case KVM_IRQFD: {
		struct kvm_irqfd data;

		r = -EFAULT;
		if (copy_from_user(&data, argp, sizeof(data)))
			goto out;
		r = kvm_irqfd(kvm, &data);
		break;
	}
	case KVM_IOEVENTFD: {
		struct kvm_ioeventfd data;

		r = -EFAULT;
		if (copy_from_user(&data, argp, sizeof(data)))
			goto out;
		r = kvm_ioeventfd(kvm, &data);
		break;
	}
#ifdef CONFIG_HAVE_KVM_MSI
	case KVM_SIGNAL_MSI: {
		struct kvm_msi msi;

		r = -EFAULT;
		if (copy_from_user(&msi, argp, sizeof(msi)))
			goto out;
		r = kvm_send_userspace_msi(kvm, &msi);
		break;
	}
#endif
#ifdef __KVM_HAVE_IRQ_LINE
	case KVM_IRQ_LINE_STATUS:
	case KVM_IRQ_LINE: {
		struct kvm_irq_level irq_event;

		r = -EFAULT;
		if (copy_from_user(&irq_event, argp, sizeof(irq_event)))
			goto out;

		r = kvm_vm_ioctl_irq_line(kvm, &irq_event,
					ioctl == KVM_IRQ_LINE_STATUS);
		if (r)
			goto out;

		r = -EFAULT;
		if (ioctl == KVM_IRQ_LINE_STATUS) {
			if (copy_to_user(argp, &irq_event, sizeof(irq_event)))
				goto out;
		}

		r = 0;
		break;
	}
#endif
#ifdef CONFIG_HAVE_KVM_IRQ_ROUTING
	case KVM_SET_GSI_ROUTING: {
		struct kvm_irq_routing routing;
		struct kvm_irq_routing __user *urouting;
		struct kvm_irq_routing_entry *entries = NULL;

		r = -EFAULT;
		if (copy_from_user(&routing, argp, sizeof(routing)))
			goto out;
		r = -EINVAL;
		if (!kvm_arch_can_set_irq_routing(kvm))
			goto out;
		if (routing.nr > KVM_MAX_IRQ_ROUTES)
			goto out;
		if (routing.flags)
			goto out;
		if (routing.nr) {
			urouting = argp;
			entries = vmemdup_user(urouting->entries,
					       array_size(sizeof(*entries),
							  routing.nr));
			if (IS_ERR(entries)) {
				r = PTR_ERR(entries);
				goto out;
			}
		}
		r = kvm_set_irq_routing(kvm, entries, routing.nr,
					routing.flags);
		kvfree(entries);
		break;
	}
#endif /* CONFIG_HAVE_KVM_IRQ_ROUTING */
	case KVM_CREATE_DEVICE: {
		struct kvm_create_device cd;

		r = -EFAULT;
		if (copy_from_user(&cd, argp, sizeof(cd)))
			goto out;

		r = kvm_ioctl_create_device(kvm, &cd);
		if (r)
			goto out;

		r = -EFAULT;
		if (copy_to_user(argp, &cd, sizeof(cd)))
			goto out;

		r = 0;
		break;
	}
	case KVM_CHECK_EXTENSION:
		r = kvm_vm_ioctl_check_extension_generic(kvm, arg);
		break;
	default:
		r = kvm_arch_vm_ioctl(filp, ioctl, arg);
	}
out:
	return r;
}
```

首先最重要的就是 `case KVM_CREATE_VCPU`，这个 case 是创建 virtual cpu，在内核中用 `struct kvm_vcpu` 来表示 vcpu。

```c
struct kvm_vcpu {
	struct kvm *kvm; // 该 cpu 所属的 vm
#ifdef CONFIG_PREEMPT_NOTIFIERS
	struct preempt_notifier preempt_notifier;
#endif
	int cpu;
	int vcpu_id; /* id given by userspace at creation */
	int vcpu_idx; /* index in kvm->vcpus array */
	int srcu_idx;
	int mode;
	u64 requests;
	unsigned long guest_debug;

	int pre_pcpu;
	struct list_head blocked_vcpu_list; // vcpu 队列

	struct mutex mutex;
	struct kvm_run *run; // cpu 运行时环境

	struct rcuwait wait;
	struct pid __rcu *pid;
	int sigset_active;
	sigset_t sigset;
	unsigned int halt_poll_ns;
	bool valid_wakeup;

#ifdef CONFIG_HAS_IOMEM
	int mmio_needed;
	int mmio_read_completed;
	int mmio_is_write;
	int mmio_cur_fragment;
	int mmio_nr_fragments;
	struct kvm_mmio_fragment mmio_fragments[KVM_MAX_MMIO_FRAGMENTS];
#endif

	...

	bool preempted;
	bool ready;
	struct kvm_vcpu_arch arch; // cpu 状态，包括各种寄存器
	struct kvm_vcpu_stat stat;
	char stats_id[KVM_STATS_NAME_SIZE];
	struct kvm_dirty_ring dirty_ring;

	/*
	 * The index of the most recently used memslot by this vCPU. It's ok
	 * if this becomes stale due to memslot changes since we always check
	 * it is a valid slot.
	 */
	int last_used_slot;
};
```
