## CPU Virtualization

在 [Interrupt-Virtualization](https://github.com/UtopianFuture/UtopianFuture.github.io/blob/master/virtualization/Interrupt-Virtualization.md) 中已经介绍了 CPU 的类型初始化，对象实例初始化，但还需要对 CPU 对象进行具现化才能让 CPU 对象可以用。我对具现化的理解就是在 VCPU 上运行 GuestOS 。这一工作通过调用 `x86_cpu_realizefn` 来完成。

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

    if (cpu->max_features && accel_uses_host_cpuid()) {
        if (enable_cpu_pm) {
            host_cpuid(5, 0, &cpu->mwait.eax, &cpu->mwait.ebx,
                       &cpu->mwait.ecx, &cpu->mwait.edx);
            env->features[FEAT_1_ECX] |= CPUID_EXT_MONITOR;
            if (kvm_enabled() && kvm_has_waitpkg()) {
                env->features[FEAT_7_0_ECX] |= CPUID_7_0_ECX_WAITPKG;
            }
        }
        if (kvm_enabled() && cpu->ucode_rev == 0) {
            cpu->ucode_rev = kvm_arch_get_supported_msr_feature(kvm_state,
                                                                MSR_IA32_UCODE_REV);
        }
    }

    if (cpu->ucode_rev == 0) {
        /* The default is the same as KVM's.  */
        if (IS_AMD_CPU(env)) {
            cpu->ucode_rev = 0x01000065;
        } else {
            cpu->ucode_rev = 0x100000000ULL;
        }
    }

    /* mwait extended info: needed for Core compatibility */
    /* We always wake on interrupt even if host does not have the capability */
    cpu->mwait.ecx |= CPUID_MWAIT_EMX | CPUID_MWAIT_IBE;

    if (cpu->apic_id == UNASSIGNED_APIC_ID) {
        error_setg(errp, "apic-id property was not initialized properly");
        return;
    }

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

    /* On AMD CPUs, some CPUID[8000_0001].EDX bits must match the bits on
     * CPUID[1].EDX.
     */
    if (IS_AMD_CPU(env)) {
        env->features[FEAT_8000_0001_EDX] &= ~CPUID_EXT2_AMD_ALIASES;
        env->features[FEAT_8000_0001_EDX] |= (env->features[FEAT_1_EDX]
           & CPUID_EXT2_AMD_ALIASES);
    }

    /* For 64bit systems think about the number of physical bits to present.
     * ideally this should be the same as the host; anything other than matching
     * the host can cause incorrect guest behaviour.
     * QEMU used to pick the magic value of 40 bits that corresponds to
     * consumer AMD devices but nothing else.
     */
    if (env->features[FEAT_8000_0001_EDX] & CPUID_EXT2_LM) {
        if (accel_uses_host_cpuid()) {
            uint32_t host_phys_bits = x86_host_phys_bits();
            static bool warned;

            /* Print a warning if the user set it to a value that's not the
             * host value.
             */
            if (cpu->phys_bits != host_phys_bits && cpu->phys_bits != 0 &&
                !warned) {
                warn_report("Host physical bits (%u)"
                            " does not match phys-bits property (%u)",
                            host_phys_bits, cpu->phys_bits);
                warned = true;
            }

            if (cpu->host_phys_bits) {
                /* The user asked for us to use the host physical bits */
                cpu->phys_bits = host_phys_bits;
                if (cpu->host_phys_bits_limit &&
                    cpu->phys_bits > cpu->host_phys_bits_limit) {
                    cpu->phys_bits = cpu->host_phys_bits_limit;
                }
            }
        }
        if (cpu->phys_bits &&
            (cpu->phys_bits > TARGET_PHYS_ADDR_SPACE_BITS ||
            cpu->phys_bits < 32)) {
            error_setg(errp, "phys-bits should be between 32 and %u "
                             " (but is %u)",
                             TARGET_PHYS_ADDR_SPACE_BITS, cpu->phys_bits);
            return;
        }
        /* 0 means it was not explicitly set by the user (or by machine
         * compat_props or by the host code above). In this case, the default
         * is the value used by TCG (40).
         */
        if (cpu->phys_bits == 0) {
            cpu->phys_bits = TCG_PHYS_ADDR_BITS;
        }
    } else {
        /* For 32 bit systems don't use the user set value, but keep
         * phys_bits consistent with what we tell the guest.
         */
        if (cpu->phys_bits != 0) {
            error_setg(errp, "phys-bits is not user-configurable in 32 bit");
            return;
        }

        if (env->features[FEAT_1_EDX] & CPUID_PSE36) {
            cpu->phys_bits = 36;
        } else {
            cpu->phys_bits = 32;
        }
    }

    /* Cache information initialization */
    if (!cpu->legacy_cache) {
        if (!xcc->model || !xcc->model->cpudef->cache_info) {
            g_autofree char *name = x86_cpu_class_get_model_name(xcc);
            error_setg(errp,
                       "CPU model '%s' doesn't support legacy-cache=off", name);
            return;
        }
        env->cache_info_cpuid2 = env->cache_info_cpuid4 = env->cache_info_amd =
            *xcc->model->cpudef->cache_info;
    } else {
        /* Build legacy cache information */
        env->cache_info_cpuid2.l1d_cache = &legacy_l1d_cache;
        env->cache_info_cpuid2.l1i_cache = &legacy_l1i_cache;
        env->cache_info_cpuid2.l2_cache = &legacy_l2_cache_cpuid2;
        env->cache_info_cpuid2.l3_cache = &legacy_l3_cache;

        env->cache_info_cpuid4.l1d_cache = &legacy_l1d_cache;
        env->cache_info_cpuid4.l1i_cache = &legacy_l1i_cache;
        env->cache_info_cpuid4.l2_cache = &legacy_l2_cache;
        env->cache_info_cpuid4.l3_cache = &legacy_l3_cache;

        env->cache_info_amd.l1d_cache = &legacy_l1d_cache_amd;
        env->cache_info_amd.l1i_cache = &legacy_l1i_cache_amd;
        env->cache_info_amd.l2_cache = &legacy_l2_cache_amd;
        env->cache_info_amd.l3_cache = &legacy_l3_cache;
    }

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

#ifndef CONFIG_USER_ONLY
    if (tcg_enabled()) {
        cpu->cpu_as_mem = g_new(MemoryRegion, 1);
        cpu->cpu_as_root = g_new(MemoryRegion, 1);

        /* Outer container... */
        memory_region_init(cpu->cpu_as_root, OBJECT(cpu), "memory", ~0ull);
        memory_region_set_enabled(cpu->cpu_as_root, true);

        /* ... with two regions inside: normal system memory with low
         * priority, and...
         */
        memory_region_init_alias(cpu->cpu_as_mem, OBJECT(cpu), "memory",
                                 get_system_memory(), 0, ~0ull);
        memory_region_add_subregion_overlap(cpu->cpu_as_root, 0, cpu->cpu_as_mem, 0);
        memory_region_set_enabled(cpu->cpu_as_mem, true);

        cs->num_ases = 2;
        cpu_address_space_init(cs, 0, "cpu-memory", cs->memory);
        cpu_address_space_init(cs, 1, "cpu-smm", cpu->cpu_as_root);

        /* ... SMRAM with higher priority, linked from /machine/smram.  */
        cpu->machine_done.notify = x86_cpu_machine_done;
        qemu_add_machine_init_done_notifier(&cpu->machine_done);
    }
#endif

    qemu_init_vcpu(cs);

    /*
     * Most Intel and certain AMD CPUs support hyperthreading. Even though QEMU
     * fixes this issue by adjusting CPUID_0000_0001_EBX and CPUID_8000_0008_ECX
     * based on inputs (sockets,cores,threads), it is still better to give
     * users a warning.
     *
     * NOTE: the following code has to follow qemu_init_vcpu(). Otherwise
     * cs->nr_threads hasn't be populated yet and the checking is incorrect.
     */
    if (IS_AMD_CPU(env) &&
        !(env->features[FEAT_8000_0001_ECX] & CPUID_EXT3_TOPOEXT) &&
        cs->nr_threads > 1 && !ht_warned) {
            warn_report("This family of AMD CPU doesn't support "
                        "hyperthreading(%d)",
                        cs->nr_threads);
            error_printf("Please configure -smp options properly"
                         " or try enabling topoext feature.\n");
            ht_warned = true;
    }

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

`x86_cpu_expand_features` 根据 QEMU 的命令行参数解析出来的 CPU 特性对 CPU 实例中的属性设置 TRUE 或 FALSE 。`x86_cpu_filter_features` 用来检测宿主机 CPU 特性能否支持创建的 CPU 对象。`x86_cpu_hyperv_realize` 用来初始化 CPU 实例中的 hyperv 相关的变量。`cpu_exec_realizefn` 调用 `cpu_list_add` 将正在初始化的 CPU 对象添加到一个全局链表 cpus 上。接下来的重要函数是 qemu_init_vcpu ，它会根据 QEMU 使用用的加速器 `cpus_accel->create_vcpu_thread(cpu)` 来决定执行哪个初始化函数

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

`qemu_init_vcpu` 会记录 CPU 的核数和线程数，然后创建地址空间。如果使用 KVM 加速，就会调用 `kvm_start_vcpu_thread`

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

该函数通过 `kvm_vcpu_thread_fn` 创建 VCPU 线程

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

这个函数首先调用 `kvm_init_vcpu` ，用于在 KVM 中创建 VCPU ，之后会详细介绍。然后调用 `kvm_init_cpu_signals` 初始化 CPU 的信号处理，使 CPU 线程能够处理 IPI 中断。

接下来的 `do while` 循环是最重要的，通过 `cpu_can_run` 判断 CPU 是否能够运行，如果可以则调用 `kvm_cpu_exec` ，该函数会调用 `kvm_vcpu_ioctl` ，即 KVM 提供的 `ioctl(KVM_RUN)` ，让 VCPU 在物理 CPU 上运行起来。然后应用层就阻塞在这里，当 guestOS 产生 `VM Exit` 时内核再根据退出原因进行处理，然后再循环运行，完成 CPU 的虚拟化。
