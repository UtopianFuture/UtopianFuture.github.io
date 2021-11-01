## CPU Virtualization

### 1. QEMU 侧虚拟机的建立

### 2. KVM 侧虚拟机的建立

`kvm_dev_ioctl` 向用户层 QEMU 提供 `ioctl` ，根据不同类型的请求提供不同的服务，如果是 `KVM_CREATE_VM` 就调用 `kvm_dev_ioctl_create_vm` 创建一个虚拟机，然后返回一个文件描述符到用户态，QEMU 用该描述符操作虚拟机。

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

该函数的主要任务是调用 `kvm_create_vm` 创建虚拟机实例，每一个虚拟机实例用 kvm 结构体表示。

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
	r = kvm_coalesced_mmio_init(kvm);
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

	/*
	 * Don't call kvm_put_kvm anymore at this point; file->f_op is
	 * already set, with ->release() being kvm_vm_release().  In error
	 * cases it will be called by the final fput(file) and will take
	 * care of doing kvm_put_kvm(kvm).
	 */
	if (kvm_create_vm_debugfs(kvm, r) < 0) {
		put_unused_fd(r);
		fput(file);
		return -ENOMEM;
	}
	kvm_uevent_notify_change(KVM_EVENT_CREATE_VM, kvm);

	fd_install(r, file);
	return r;

put_kvm:
	kvm_put_kvm(kvm);
	return r;
}
```

### 3. QEMU CPU 的创建

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

    if (xcc->host_cpuid_required) {
        if (!accel_uses_host_cpuid()) {
            g_autofree char *name = x86_cpu_class_get_model_name(xcc);
            error_setg(&local_err, "CPU model '%s' requires KVM", name);
            goto out;
        }
    }

    ...

    x86_cpu_expand_features(cpu, &local_err);
    if (local_err) {
        goto out;
    }

    x86_cpu_filter_features(cpu, cpu->check_cpuid || cpu->enforce_cpuid);

    if (cpu->enforce_cpuid && x86_cpu_have_filtered_features(cpu)) {
        error_setg(&local_err,
                   accel_uses_host_cpuid() ?
                       "Host doesn't support requested features" :
                       "TCG doesn't support requested features");
        goto out;
    }

    ...

    /* Process Hyper-V enlightenments */
    x86_cpu_hyperv_realize(cpu);

    cpu_exec_realizefn(cs, &local_err);
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

    mce_init(cpu);

	...

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

```
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

```
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

另外 `X86CPUClass` 是静态变量，其中的变量也是所有 CPU 共有的。

`x86_cpu_expand_features` 根据 QEMU 的命令行参数解析出来的 CPU 特性对 CPU 实例中的属性设置 TRUE 或 FALSE 。`x86_cpu_filter_features` 用来检测宿主机 CPU 特性能否支持创建的 CPU 对象。`x86_cpu_hyperv_realize` 用来初始化 CPU 实例中的 hyperv 相关的变量。`cpu_exec_realizefn` 调用 `cpu_list_add` 将正在初始化的 CPU 对象添加到一个全局链表 cpus 上。接下来的重要函数是 `qemu_init_vcpu` ，它会根据 QEMU 使用用的加速器 `cpus_accel->create_vcpu_thread(cpu);` 来决定执行哪个初始化函数，加速方式在 accel 目录下。

```c
void qemu_init_vcpu(CPUState *cpu)
{
    MachineState *ms = MACHINE(qdev_get_machine());

    cpu->nr_cores = ms->smp.cores;
    cpu->nr_threads =  ms->smp.threads;
    cpu->stopped = true;
    cpu->random_seed = qemu_guest_random_seed_thread_part1();

    if (!cpu->as) {
        /* If the target cpu hasn't set up any address spaces itself,
         * give it the default one.
         */
        cpu->num_ases = 1;
        cpu_address_space_init(cpu, 0, "cpu-memory", cpu->memory);
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

该函数通过 `kvm_vcpu_thread_fn` 创建 VCPU 线程并运行。

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
            r = kvm_cpu_exec(cpu);
            if (r == EXCP_DEBUG) {
                cpu_handle_guest_debug(cpu);
            }
        }
        qemu_wait_io_event(cpu);
    } while (!cpu->unplug || cpu_can_run(cpu));

    kvm_destroy_vcpu(cpu);
    cpu_thread_signal_destroyed(cpu);
    qemu_mutex_unlock_iothread();
    rcu_unregister_thread();
    return NULL;
}
```

这个函数首先调用 `kvm_init_vcpu` ，用于在 KVM 中创建 VCPU，这里 `KVMState` 是 QEMU 中用来表示 KVM 相关的数据结构，操作虚拟机的 fd 就保存在这里。

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
                        cpu->kvm_fd, 0);
    ...

    ret = kvm_arch_init_vcpu(cpu);

    ...

err:
    return ret;
}
```

`kvm_get_vcpu` 首先会从已有的 VCPU 队列中查询是否有对应 `vcpu_id` 的 VCPU ，如果没有的话就用 `ioctl` 向 KVM 发起一个 `KVM_CREATE_VCPU` 的请求，并把对应的 `vcpu_id` 传进去。

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

接下来的 `do while` 循环是最重要的，通过 `cpu_can_run` 判断 CPU 是否能够运行，如果可以则调用 `kvm_cpu_exec` ，该函数会调用 `kvm_vcpu_ioctl` ，即 KVM 提供的 `ioctl(KVM_RUN)` ，让 VCPU 在物理 CPU 上运行起来。然后应用层就阻塞在这里，当 guestOS 产生 `VM Exit` 时内核再根据退出原因进行处理，然后再循环运行，完成 CPU 的虚拟化。

### 4. KVM CPU 的创建

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

其主要操作也是 `ioctl` ，对应的函数是 `kvm_vm_ioctl` ，这个函数是处理虚拟机级别的 `ioctl` 入口，前面的 `kvm_dev_ioctl` 是设备级别的处理入口，不要搞混了。`kvm_vm_ioctl`  会处理各种请求，刚刚我们分析的 QEMU 中的 `kvm_vm_ioctl` 请求就是在这里进行处理（注意一个是 QEMU 中的函数，一个是kernel KVM 中的函数，不要搞混了），这里先分析 `KVM_CREATE_VCPU` 即创建 VCPU 的情况。

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

```c
static int kvm_vm_ioctl_create_vcpu(struct kvm *kvm, u32 id)
{
	int r;
	struct kvm_vcpu *vcpu;
	struct page *page;

	if (id >= KVM_MAX_VCPU_ID)
		return -EINVAL;

	mutex_lock(&kvm->lock);
	if (kvm->created_vcpus == KVM_MAX_VCPUS) {
		mutex_unlock(&kvm->lock);
		return -EINVAL;
	}

	kvm->created_vcpus++;
	mutex_unlock(&kvm->lock);

	r = kvm_arch_vcpu_precreate(kvm, id);
	if (r)
		goto vcpu_decrement;

	vcpu = kmem_cache_zalloc(kvm_vcpu_cache, GFP_KERNEL_ACCOUNT);
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
	vcpu->run = page_address(page);

	kvm_vcpu_init(vcpu, kvm, id);

	r = kvm_arch_vcpu_create(vcpu);
	if (r)
		goto vcpu_free_run_page;

	if (kvm->dirty_ring_size) {
		r = kvm_dirty_ring_alloc(&vcpu->dirty_ring,
					 id, kvm->dirty_ring_size);
		if (r)
			goto arch_vcpu_destroy;
	}

	mutex_lock(&kvm->lock);
	if (kvm_get_vcpu_by_id(kvm, id)) {
		r = -EEXIST;
		goto unlock_vcpu_destroy;
	}

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

	kvm->vcpus[vcpu->vcpu_idx] = vcpu;

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

这里代码很清楚，`kvm_vcpu_init` 和 `kvm_arch_vcpu_create` 初始化 VCPU ，然后将其保存到 kvm 中的 vcpus 结构中。

```c
kvm->vcpus[vcpu->vcpu_idx] = vcpu;
```

### 5. QEMU 与 KVM 之间的共享数据

### 6. VCPU CPUID 构造

这两节等之后有需要了再详细分析。

### 7. VCPU 的运行

首先每个 VCPU 都会对应一个 VMCS ，VMCS 对于 VCPU 的作用类似与进程描述符对于进程的作用。其用来管理 VMX non-root Operation 的转换以及控制 VCPU 的行为。操作 VMCS 的指令包括 VMCLEAR、VMPTRLD、VMREAD 和 VMWRITE。VMCS 的区域大小为 4KB，VMM 通过它的 64 位地址进行访问。

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

调用 `cpu_can_run` 检查当前 cpu 是否可运行。这里有个问题，为什么用的是 `CPUState`，而不是 `X86CPU`。

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

当在 main 中执行 `vm_start` -> `resume_all_vcpus` -> `qemu_cpu_kick` 时，qemu_cpu_kick 会将 VCPU 唤醒。

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

接下来分析 `kvm_cpu_exec`

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

        run_ret = kvm_vcpu_ioctl(cpu, KVM_RUN, 0);

        attrs = kvm_arch_post_run(cpu, run);

		...

        trace_kvm_run_exit(cpu->cpu_index, run->exit_reason);
        switch (run->exit_reason) {
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
    } while (ret == 0);

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

`kvm_arch_pre_run` 首先做一些运行前的准备工作，如 `nmi` 和 `smi` 的[中断注入](https://github.com/UtopianFuture/UtopianFuture.github.io/blob/master/virtualization/Interrupt-Virtualization.md)，之后使用 `ioctl(KVM_RUN)` 系统调用通知 KVM 使该 KVM 运行起来。KVM 模块在处理该 ioctl 时会执行对应的 vmx 指令，将 VCPU 运行的物理 CPU 从 `VMX root` 转换成 `VMX non-root` ，开始运行虚拟机中的代码。虚拟机内部如果产生 `VM Exit` ，就会退出到 KVM ，如果 KVM 无法处理就会分发到 QEMU ，也就是在 `ioctl(KVM_RUN)`  返回的时候调用 `kvm_arch_post_run` 进行初步的处理。然后根据共享内存 `kvm_run` （这个是第 5 节的内容，没有分析）中的数据来判断退出原因，并进行处理。之后的 `switch` 就是处理 KVM 不能处理的问题。

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
out:
	mutex_unlock(&vcpu->mutex);
	kfree(fpu);
	kfree(kvm_sregs);
	return r;
}
```

对于 `KVM_RUN` 的情况，会调用 `kvm_arch_vcpu_ioctl_run` 进行处理。

```c
int kvm_arch_vcpu_ioctl_run(struct kvm_vcpu *vcpu)
{
	struct kvm_run *kvm_run = vcpu->run;
	int r;

	vcpu_load(vcpu);
	kvm_sigset_activate(vcpu);
	kvm_run->flags = 0;
	kvm_load_guest_fpu(vcpu);

	...

	if (kvm_run->immediate_exit)
		r = -EINTR;
	else
		r = vcpu_run(vcpu);

out:
	kvm_put_guest_fpu(vcpu);
	if (kvm_run->kvm_valid_regs)
		store_regs(vcpu);
	post_kvm_run_save(vcpu);
	kvm_sigset_deactivate(vcpu);

	vcpu_put(vcpu);
	return r;
}
```

`kvm_arch_vcpu_ioctl_run` 主要调用 `vcpu_run` 。

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

		if (r <= 0)
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

`kvm_vcpu_running` 还要通过读取 vcpu 中的 `hflags` 和 `HF_GUEST_MASK` 判断当前 CPU 是否处于 guest mode

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

	/* Forbid vmenter if vcpu dirty ring is soft-full */
	if (unlikely(vcpu->kvm->dirty_ring_size &&
		     kvm_dirty_ring_soft_full(&vcpu->dirty_ring))) {
		vcpu->run->exit_reason = KVM_EXIT_DIRTY_RING_FULL;
		trace_kvm_dirty_ring_exit(vcpu);
		r = 0;
		goto out;
	}

	if (kvm_request_pending(vcpu)) {
		if (kvm_check_request(KVM_REQ_VM_BUGGED, vcpu)) {
			r = -EIO;
			goto out;
		}
		if (kvm_check_request(KVM_REQ_GET_NESTED_STATE_PAGES, vcpu)) {
			if (unlikely(!kvm_x86_ops.nested_ops->get_nested_state_pages(vcpu))) {
				r = 0;
				goto out;
			}
		}
		...
        r = inject_pending_event(vcpu, &req_immediate_exit);
	}
    r = kvm_mmu_reload(vcpu);
		if (unlikely(r)) {
		goto cancel_injection;
	}

	preempt_disable();

	static_call(kvm_x86_prepare_guest_switch)(vcpu);

	/*
	 * Disable IRQs before setting IN_GUEST_MODE.  Posted interrupt
	 * IPI are then delayed after guest entry, which ensures that they
	 * result in virtual interrupt delivery.
	 */
	local_irq_disable();
	vcpu->mode = IN_GUEST_MODE;

	srcu_read_unlock(&vcpu->kvm->srcu, vcpu->srcu_idx);

	/*
	 * 1) We should set ->mode before checking ->requests.  Please see
	 * the comment in kvm_vcpu_exiting_guest_mode().
	 *
	 * 2) For APICv, we should set ->mode before checking PID.ON. This
	 * pairs with the memory barrier implicit in pi_test_and_set_on
	 * (see vmx_deliver_posted_interrupt).
	 *
	 * 3) This also orders the write to mode from any reads to the page
	 * tables done while the VCPU is running.  Please see the comment
	 * in kvm_flush_remote_tlbs.
	 */
	smp_mb__after_srcu_read_unlock();

	/*
	 * This handles the case where a posted interrupt was
	 * notified with kvm_vcpu_kick.
	 */
	if (kvm_lapic_enabled(vcpu) && vcpu->arch.apicv_active)
		static_call(kvm_x86_sync_pir_to_irr)(vcpu);

	if (kvm_vcpu_exit_request(vcpu)) {
		vcpu->mode = OUTSIDE_GUEST_MODE;
		smp_wmb();
		local_irq_enable();
		preempt_enable();
		vcpu->srcu_idx = srcu_read_lock(&vcpu->kvm->srcu);
		r = 1;
		goto cancel_injection;
	}

	if (req_immediate_exit) {
		kvm_make_request(KVM_REQ_EVENT, vcpu);
		static_call(kvm_x86_request_immediate_exit)(vcpu);
	}

	fpregs_assert_state_consistent();
	if (test_thread_flag(TIF_NEED_FPU_LOAD))
		switch_fpu_return();

	if (unlikely(vcpu->arch.switch_db_regs)) {
		set_debugreg(0, 7);
		set_debugreg(vcpu->arch.eff_db[0], 0);
		set_debugreg(vcpu->arch.eff_db[1], 1);
		set_debugreg(vcpu->arch.eff_db[2], 2);
		set_debugreg(vcpu->arch.eff_db[3], 3);
	} else if (unlikely(hw_breakpoint_active())) {
		set_debugreg(0, 7);
	}

	for (;;) {
		exit_fastpath = static_call(kvm_x86_run)(vcpu);
		if (likely(exit_fastpath != EXIT_FASTPATH_REENTER_GUEST))
			break;

        if (unlikely(kvm_vcpu_exit_request(vcpu))) {
			exit_fastpath = EXIT_FASTPATH_EXIT_HANDLED;
			break;
		}

		if (vcpu->arch.apicv_active)
			static_call(kvm_x86_sync_pir_to_irr)(vcpu);
    }

	...

	vcpu->mode = OUTSIDE_GUEST_MODE;
	smp_wmb();

	static_call(kvm_x86_handle_exit_irqoff)(vcpu);

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

	r = static_call(kvm_x86_handle_exit)(vcpu, exit_fastpath);
	return r;

cancel_injection:
	if (req_immediate_exit)
		kvm_make_request(KVM_REQ_EVENT, vcpu);
	static_call(kvm_x86_cancel_injection)(vcpu);
	if (unlikely(vcpu->arch.apic_attention))
		kvm_lapic_sync_from_vapic(vcpu);
out:
	return r;
}
```

将这些 `request` 处理完并 `inject_pending_event` 之后就调用 `kvm_mmu_reload` 处理内存虚拟化相关的东西。然后就是 `kvm_x86_prepare_guest_switch` ，保存宿主机状态到 `VMCS` ，使虚拟机下次退出后能正常运行。

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

	vmx->exit_reason.full = vmcs_read32(VM_EXIT_REASON);
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

`vmx_vcpu_enter_exit` 最终会 `call vmx_vmenter` ，在这个汇编里使用 `vmlaunch` 指令切换到 guest mode

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

如果 `kvm_vcpu_running` 判断出该 cpu 不能执行，那么就会调用 `vcpu_block` ，最终调用 `schedule` 请求调度，让出物理 cpu 。

### 8. VCPU 的调度

待续。。。
