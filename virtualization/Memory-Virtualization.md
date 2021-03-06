## Memory Virtualization

### Guest RAM configuration

The QEMU command-line option `-m [size=]megs[,slots=n,maxmem=size]` specifies the initial guest RAM size as well as the maximum guest RAM size and number of slots for memory chips (DIMMs).

The reason for the maximum size and slots is that QEMU emulates DIMM hotplug so the guest operating system can detect when new memory is added and removed using the same mechanism as on real hardware. This involves plugging or unplugging a DIMM into a slot, just like on a physical machine. In other words, changing the amount of memory available isn't done in byte units, it's done by changing the set of DIMMs plugged into the emulated machine.

The MemoryRegion is the link between guest physical address space and the RAMBlocks containing the memory. Each MemoryRegion has the ram_addr_t offset of the RAMBlock and each RAMBlock has a MemoryRegion pointer.

### QEMU 内存初始化

#### 基本结构

我们先看看整体的结构图（[查看大图](https://raw.githubusercontent.com/UtopianFuture/UtopianFuture.github.io/fd62f363c30f80ffb2711c56f4c7a989eb916195/image/qemu-address-space.svg)）。

![qemu-address-space](https://raw.githubusercontent.com/UtopianFuture/UtopianFuture.github.io/fd62f363c30f80ffb2711c56f4c7a989eb916195/image/qemu-address-space.svg)

#### 32 位系统的地址空间布局

再看看正常的 QEMU 中给 guestos 注册的内存是怎样的，

```plain
memory-region: memory
  0000000000000000-ffffffffffffffff (prio 0, i/o): system
    0000000000000000-00000000bfffffff (prio 0, i/o): alias ram-below-4g @pc.ram 0000000000000000-00000000bfffffff
    0000000000000000-ffffffffffffffff (prio -1, i/o): pci
      00000000000a0000-00000000000bffff (prio 1, i/o): vga-lowmem
      00000000000c0000-00000000000dffff (prio 1, rom): pc.rom
      00000000000e0000-00000000000fffff (prio 1, i/o): alias isa-bios @pc.bios 0000000000020000-000000000003ffff
      00000000fd000000-00000000fdffffff (prio 1, ram): vga.vram
      00000000febc0000-00000000febdffff (prio 1, i/o): e1000-mmio
      00000000febf0000-00000000febf0fff (prio 1, i/o): vga.mmio
        00000000febf0000-00000000febf00ff (prio 0, i/o): edid
        00000000febf0400-00000000febf041f (prio 0, i/o): vga ioports remapped
        00000000febf0500-00000000febf0515 (prio 0, i/o): bochs dispi interface
        00000000febf0600-00000000febf0607 (prio 0, i/o): qemu extended regs
      00000000fffc0000-00000000ffffffff (prio 0, rom): pc.bios
    00000000000a0000-00000000000bffff (prio 1, i/o): alias smram-region @pci 00000000000a0000-00000000000bffff
    00000000000c0000-00000000000c3fff (prio 1, i/o): alias pam-ram @pc.ram 00000000000c0000-00000000000c3fff [disabled]
    00000000000c0000-00000000000c3fff (prio 1, i/o): alias pam-pci @pc.ram 00000000000c0000-00000000000c3fff [disabled]
    00000000000c0000-00000000000c3fff (prio 1, i/o): alias pam-rom @pc.ram 00000000000c0000-00000000000c3fff
    00000000000c0000-00000000000c3fff (prio 1, i/o): alias pam-pci @pci 00000000000c0000-00000000000c3fff [disabled]
    00000000000c4000-00000000000c7fff (prio 1, i/o): alias pam-ram @pc.ram 00000000000c4000-00000000000c7fff [disabled]
    00000000000c4000-00000000000c7fff (prio 1, i/o): alias pam-pci @pc.ram 00000000000c4000-00000000000c7fff [disabled]
    00000000000c4000-00000000000c7fff (prio 1, i/o): alias pam-rom @pc.ram 00000000000c4000-00000000000c7fff
    00000000000c4000-00000000000c7fff (prio 1, i/o): alias pam-pci @pci 00000000000c4000-00000000000c7fff [disabled]
    00000000000c8000-00000000000cbfff (prio 1, i/o): alias pam-ram @pc.ram 00000000000c8000-00000000000cbfff [disabled]
    00000000000c8000-00000000000cbfff (prio 1, i/o): alias pam-pci @pc.ram 00000000000c8000-00000000000cbfff [disabled]
    00000000000c8000-00000000000cbfff (prio 1, i/o): alias pam-rom @pc.ram 00000000000c8000-00000000000cbfff
    00000000000c8000-00000000000cbfff (prio 1, i/o): alias pam-pci @pci 00000000000c8000-00000000000cbfff [disabled]
    00000000000cb000-00000000000cdfff (prio 1000, i/o): alias kvmvapic-rom @pc.ram 00000000000cb000-00000000000cdfff
    00000000000cc000-00000000000cffff (prio 1, i/o): alias pam-ram @pc.ram 00000000000cc000-00000000000cffff [disabled]
    00000000000cc000-00000000000cffff (prio 1, i/o): alias pam-pci @pc.ram 00000000000cc000-00000000000cffff [disabled]
    00000000000cc000-00000000000cffff (prio 1, i/o): alias pam-rom @pc.ram 00000000000cc000-00000000000cffff
    00000000000cc000-00000000000cffff (prio 1, i/o): alias pam-pci @pci 00000000000cc000-00000000000cffff [disabled]
    00000000000d0000-00000000000d3fff (prio 1, i/o): alias pam-ram @pc.ram 00000000000d0000-00000000000d3fff [disabled]
    00000000000d0000-00000000000d3fff (prio 1, i/o): alias pam-pci @pc.ram 00000000000d0000-00000000000d3fff [disabled]
    00000000000d0000-00000000000d3fff (prio 1, i/o): alias pam-rom @pc.ram 00000000000d0000-00000000000d3fff
    00000000000d0000-00000000000d3fff (prio 1, i/o): alias pam-pci @pci 00000000000d0000-00000000000d3fff [disabled]
    00000000000d4000-00000000000d7fff (prio 1, i/o): alias pam-ram @pc.ram 00000000000d4000-00000000000d7fff [disabled]
    00000000000d4000-00000000000d7fff (prio 1, i/o): alias pam-pci @pc.ram 00000000000d4000-00000000000d7fff [disabled]
    00000000000d4000-00000000000d7fff (prio 1, i/o): alias pam-rom @pc.ram 00000000000d4000-00000000000d7fff
    00000000000d4000-00000000000d7fff (prio 1, i/o): alias pam-pci @pci 00000000000d4000-00000000000d7fff [disabled]
    00000000000d8000-00000000000dbfff (prio 1, i/o): alias pam-ram @pc.ram 00000000000d8000-00000000000dbfff [disabled]
    00000000000d8000-00000000000dbfff (prio 1, i/o): alias pam-pci @pc.ram 00000000000d8000-00000000000dbfff [disabled]
    00000000000d8000-00000000000dbfff (prio 1, i/o): alias pam-rom @pc.ram 00000000000d8000-00000000000dbfff
    00000000000d8000-00000000000dbfff (prio 1, i/o): alias pam-pci @pci 00000000000d8000-00000000000dbfff [disabled]
    00000000000dc000-00000000000dffff (prio 1, i/o): alias pam-ram @pc.ram 00000000000dc000-00000000000dffff [disabled]
    00000000000dc000-00000000000dffff (prio 1, i/o): alias pam-pci @pc.ram 00000000000dc000-00000000000dffff [disabled]
    00000000000dc000-00000000000dffff (prio 1, i/o): alias pam-rom @pc.ram 00000000000dc000-00000000000dffff
    00000000000dc000-00000000000dffff (prio 1, i/o): alias pam-pci @pci 00000000000dc000-00000000000dffff [disabled]
    00000000000e0000-00000000000e3fff (prio 1, i/o): alias pam-ram @pc.ram 00000000000e0000-00000000000e3fff [disabled]
    00000000000e0000-00000000000e3fff (prio 1, i/o): alias pam-pci @pc.ram 00000000000e0000-00000000000e3fff [disabled]
00000000000e0000-00000000000e3fff (prio 1, i/o): alias pam-rom @pc.ram 00000000000e0000-00000000000e3fff
00000000000e0000-00000000000e3fff (prio 1, i/o): alias pam-pci @pci 00000000000e0000-00000000000e3fff [disabled]
00000000000e4000-00000000000e7fff (prio 1, i/o): alias pam-ram @pc.ram 00000000000e4000-00000000000e7fff [disabled]
00000000000e4000-00000000000e7fff (prio 1, i/o): alias pam-pci @pc.ram 00000000000e4000-00000000000e7fff [disabled]
00000000000e4000-00000000000e7fff (prio 1, i/o): alias pam-rom @pc.ram 00000000000e4000-00000000000e7fff
00000000000e4000-00000000000e7fff (prio 1, i/o): alias pam-pci @pci 00000000000e4000-00000000000e7fff [disabled]
00000000000e8000-00000000000ebfff (prio 1, i/o): alias pam-ram @pc.ram 00000000000e8000-00000000000ebfff
00000000000e8000-00000000000ebfff (prio 1, i/o): alias pam-pci @pc.ram 00000000000e8000-00000000000ebfff [disabled]
00000000000e8000-00000000000ebfff (prio 1, i/o): alias pam-rom @pc.ram 00000000000e8000-00000000000ebfff [disabled]
00000000000e8000-00000000000ebfff (prio 1, i/o): alias pam-pci @pci 00000000000e8000-00000000000ebfff [disabled]
00000000000ec000-00000000000effff (prio 1, i/o): alias pam-ram @pc.ram 00000000000ec000-00000000000effff
00000000000ec000-00000000000effff (prio 1, i/o): alias pam-pci @pc.ram 00000000000ec000-00000000000effff [disabled]
00000000000ec000-00000000000effff (prio 1, i/o): alias pam-rom @pc.ram 00000000000ec000-00000000000effff [disabled]
00000000000ec000-00000000000effff (prio 1, i/o): alias pam-pci @pci 00000000000ec000-00000000000effff [disabled]
00000000000f0000-00000000000fffff (prio 1, i/o): alias pam-ram @pc.ram 00000000000f0000-00000000000fffff [disabled]
00000000000f0000-00000000000fffff (prio 1, i/o): alias pam-pci @pc.ram 00000000000f0000-00000000000fffff [disabled]
00000000000f0000-00000000000fffff (prio 1, i/o): alias pam-rom @pc.ram 00000000000f0000-00000000000fffff
00000000000f0000-00000000000fffff (prio 1, i/o): alias pam-pci @pci 00000000000f0000-00000000000fffff [disabled]
00000000fec00000-00000000fec00fff (prio 0, i/o): kvm-ioapic
00000000fed00000-00000000fed003ff (prio 0, i/o): hpet
00000000fee00000-00000000feefffff (prio 4096, i/o): kvm-apic-msi
0000000100000000-000000023fffffff (prio 0, i/o): alias ram-above-4g @pc.ram 00000000c0000000-00000001ffffffff
```

然后从基本的数据结构入手，首先介绍 `AddressSpace` 结构体，QEMU 用其表示一个虚拟机所能够访问的所有物理地址。

```c
/**
 * struct AddressSpace: describes a mapping of addresses to #MemoryRegion objects
 */
struct AddressSpace {
    /* private: */
    struct rcu_head rcu;
    char *name;
    MemoryRegion *root; // memory region 是树状的，这个相当于根节点

    /* Accessed via RCU.  */
    struct FlatView *current_map; // 这个是将 memory region 压平了看到的内存模型

    int ioeventfd_nb;
    struct MemoryRegionIoeventfd *ioeventfds;
    QTAILQ_HEAD(, MemoryListener) listeners;
    QTAILQ_ENTRY(AddressSpace) address_spaces_link;
};
```

虽然地址空间是同一块，但是不同设备对这块地址空间的视角不同，而 `AddressSpace` 描述的就是不同设备的视角。

`MemoryRegion` 表示虚拟机的一段内存区域，其是内存模拟的核心结构，**整个内存空间就是 `MemoryRegion` 构成的无环图**。

```c
struct MemoryRegion {
    Object parent_obj;

    /* private: */

    /* The following fields should fit in a cache line */
    bool romd_mode;
    bool ram;
    bool subpage;
    bool readonly; /* For RAM regions */
    bool nonvolatile;
    bool rom_device;
    bool flush_coalesced_mmio;
    uint8_t dirty_log_mask;
    bool is_iommu;
    RAMBlock *ram_block;
    Object *owner;

    const MemoryRegionOps *ops; // 该 mr 对应的回调函数
    void *opaque;
    MemoryRegion *container; // 该 mr 的上一级 mr
    Int128 size;
    hwaddr addr;
    void (*destructor)(MemoryRegion *mr);
    uint64_t align;
    bool terminates;
    bool ram_device;
    bool enabled;
    bool warning_printed; /* For reservations */
    uint8_t vga_logging_count;
    MemoryRegion *alias; // mr 会被分成几部分，alias 就是指向其他的部分
    hwaddr alias_offset;
    int32_t priority; // 该 mr 的优先级，因为 mr 会重叠。priority 只在该 container 内有效
    QTAILQ_HEAD(, MemoryRegion) subregions; // 该 mr 的所有子 mr
    QTAILQ_ENTRY(MemoryRegion) subregions_link; // 同一个 container 的 mr
    QTAILQ_HEAD(, CoalescedMemoryRange) coalesced;
    const char *name;
    unsigned ioeventfd_nb;
    MemoryRegionIoeventfd *ioeventfds;
    RamDiscardManager *rdm; /* Only for RAM */
};
```

当给定一个 as 中的地址时，其根据如下原则找到对应的 mr：

（1）从 as 中找到 root 的所有 subregion，如果地址不在 region 中则放弃考虑 region。

（2）如果地址在一个叶子 mr 中，返回该 mr。

（3）如果该子 mr 也是一个 container，那么在子容器中递归查找。

（4）如果 mr 时一个 alias，那么在 alias 中查找。

（5）如果以上过程都没有找到匹配项，但该 container 本身有自己的 MMIO 或 RAM，那么返回这个 container 本身，否则根据下一个优先级查找。（不懂）

（6）如果都没有找到，return。

#### QEMU 虚拟机内存初始化

首先在 `qemu_init` 中会进行一些初始化工作，

```plain
#0  memory_map_init () at ../softmmu/physmem.c:2664
#1  0x0000555555beb3a6 in cpu_exec_init_all () at ../softmmu/physmem.c:3070
#2  0x0000555555c06c90 in qemu_create_machine (qdict=0x555556819dd0) at ../softmmu/vl.c:2163
#3  0x0000555555c0a7b9 in qemu_init (argc=13, argv=0x7fffffffe278, envp=0x7fffffffe2e8) at ../softmmu/vl.c:3707
#4  0x000055555583b6f5 in main (argc=13, argv=0x7fffffffe278, envp=0x7fffffffe2e8) at ../softmmu/main.c:49
```

`cpu_exec_init_all` 会调用 `io_mem_init` 和 `memory_map_init`。`io_mem_init` 创建包含若干包含所有地址空间的 mr。

```c
static void io_mem_init(void)
{
    memory_region_init_io(&io_mem_unassigned, NULL, &unassigned_mem_ops, NULL,
                          NULL, UINT64_MAX); // UINT64_MAX，所有的地址
}
```

`memory_map_init` 则是一个重要函数。其创建 `address_space_memory` 和 `address_space_io` 两个 as，分别用来表示虚拟机的内存地址空间和 I/O 地址空间。

```c
static void memory_map_init(void)
{
    system_memory = g_malloc(sizeof(*system_memory));

    memory_region_init(system_memory, NULL, "system", UINT64_MAX);
    address_space_init(&address_space_memory, system_memory, "memory");

    system_io = g_malloc(sizeof(*system_io));
    memory_region_init_io(system_io, NULL, &unassigned_io_ops, NULL, "io",
                          65536);
    address_space_init(&address_space_io, system_io, "I/O");
}
```

`system_memory` 和 `system_io` 就是对应的 root mr。它们均为全局变量，在系统中会多次使用。

而如果 guestos 写相同的地址，在 system_memory 的空间和 system_io 空间的效果不同的。

```c
int kvm_cpu_exec(CPUState *cpu)
{
    struct kvm_run *run = cpu->kvm_run;
    int ret, run_ret;

    ...

    do {
        ...

        run_ret = kvm_vcpu_ioctl(cpu, KVM_RUN, 0);

        attrs = kvm_arch_post_run(cpu, run);

        ...

        trace_kvm_run_exit(cpu->cpu_index, run->exit_reason); // KVM 中无法处理的异常则返回到用户态 QEMU 处理
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

        ...

        default:
            DPRINTF("kvm_arch_handle_exit\n");
            ret = kvm_arch_handle_exit(cpu, run);
            break;
        }
    } while (ret == 0);

    cpu_exec_end(cpu);
    qemu_mutex_lock_iothread();

   	...

    return ret;
}
```

```c
void address_space_init(AddressSpace *as, MemoryRegion *root, const char *name)
{
    memory_region_ref(root);
    as->root = root; // address_space_memory 的 root mr 为 system_memory
    as->current_map = NULL;
    as->ioeventfd_nb = 0;
    as->ioeventfds = NULL;
    QTAILQ_INIT(&as->listeners);
    QTAILQ_INSERT_TAIL(&address_spaces, as, address_spaces_link);
    as->name = g_strdup(name ? name : "anonymous");
    address_space_update_topology(as);
    address_space_update_ioeventfds(as);
}
```

进一步的内存的初始化是在 `pc_init1` 中进行的，`pc_init1` 会调用 `pc_memory_init` 进行初始化。

```c
void pc_memory_init(PCMachineState *pcms,
                    MemoryRegion *system_memory,
                    MemoryRegion *rom_memory,
                    MemoryRegion **ram_memory)
{
    int linux_boot, i;
    MemoryRegion *option_rom_mr;
    MemoryRegion *ram_below_4g, *ram_above_4g;
    FWCfgState *fw_cfg;
    MachineState *machine = MACHINE(pcms);
    MachineClass *mc = MACHINE_GET_CLASS(machine);
    PCMachineClass *pcmc = PC_MACHINE_GET_CLASS(pcms);
    X86MachineState *x86ms = X86_MACHINE(pcms);

    assert(machine->ram_size == x86ms->below_4g_mem_size +
                                x86ms->above_4g_mem_size);

    linux_boot = (machine->kernel_filename != NULL);

    /*
     * Split single memory region and use aliases to address portions of it,
     * done for backwards compatibility with older qemus.
     */
    // 这里也就是分配虚拟机的物理内存
    *ram_memory = machine->ram;
    ram_below_4g = g_malloc(sizeof(*ram_below_4g));
    memory_region_init_alias(ram_below_4g, NULL, "ram-below-4g", machine->ram, // 建立一个 region
                             0, x86ms->below_4g_mem_size);
    memory_region_add_subregion(system_memory, 0, ram_below_4g); // 该 region 是 system_memory 的子 region
    e820_add_entry(0, x86ms->below_4g_mem_size, E820_RAM); // 将其地址关系加入到 e820 表中供 bios 使用
    if (x86ms->above_4g_mem_size > 0) { // 内存大于 4g 的部分也要建立映射，它们都是 system_memory 的子 region
        ram_above_4g = g_malloc(sizeof(*ram_above_4g));
        memory_region_init_alias(ram_above_4g, NULL, "ram-above-4g",
                                 machine->ram,
                                 x86ms->below_4g_mem_size,
                                 x86ms->above_4g_mem_size);
        memory_region_add_subregion(system_memory, 0x100000000ULL,
                                    ram_above_4g);
        e820_add_entry(0x100000000ULL, x86ms->above_4g_mem_size, E820_RAM);
    }

    ...

    /* Initialize PC system firmware */
    pc_system_firmware_init(pcms, rom_memory);

    // 建立 rom 的 mr
    option_rom_mr = g_malloc(sizeof(*option_rom_mr));
    memory_region_init_ram(option_rom_mr, NULL, "pc.rom", PC_ROM_SIZE,
                           &error_fatal);
    if (pcmc->pci_enabled) {
        memory_region_set_readonly(option_rom_mr, true);
    }
    memory_region_add_subregion_overlap(rom_memory,
                                        PC_ROM_MIN_VGA,
                                        option_rom_mr,
                                        1);

    // 创建一个 fw_cfg 设备并进行初始化
    fw_cfg = fw_cfg_arch_create(machine,
                                x86ms->boot_cpus, x86ms->apic_id_limit);

    // 将创建的 fw_cfg 设备赋值给全局 fw_cfg 设备
    rom_set_fw(fw_cfg);

    if (pcmc->has_reserved_memory && machine->device_memory->base) {
        uint64_t *val = g_malloc(sizeof(*val));
        PCMachineClass *pcmc = PC_MACHINE_GET_CLASS(pcms);
        uint64_t res_mem_end = machine->device_memory->base;

        if (!pcmc->broken_reserved_end) {
            res_mem_end += memory_region_size(&machine->device_memory->mr);
        }
        *val = cpu_to_le64(ROUND_UP(res_mem_end, 1 * GiB));
        fw_cfg_add_file(fw_cfg, "etc/reserved-memory-end", val, sizeof(*val));
    }

    // 加载 bios
    if (linux_boot) {
        x86_load_linux(x86ms, fw_cfg, pcmc->acpi_data_size,
                       pcmc->pvh_enabled);
    }

    for (i = 0; i < nb_option_roms; i++) {
        rom_add_option(option_rom[i].name, option_rom[i].bootindex);
    }
    x86ms->fw_cfg = fw_cfg;

    /* Init default IOAPIC address space */
    x86ms->ioapic_as = &address_space_memory; // 默认的 ioapic as 就是 address_space_memory

    /* Init ACPI memory hotplug IO base address */
    pcms->memhp_io_base = ACPI_MEMORY_HOTPLUG_BASE;
}
```

总结起来，这个函数主要是分配虚拟机的物理内存(ram_below_4g, ram_above_4g)，初始化相应的 mr，创建 fw_cfg 设备和加载 bios。其中创建 fw_cfg 还需要[详细分析](.)。

#### 分配虚拟机 RAM 过程

虚拟机的 RAM 是通过如下方式分配的，

```plain
#0  qemu_ram_alloc_internal (size=8589934592, max_size=8589934592, resized=0x0, host=0x0, ram_flags=0, mr=0x555556938af0, errp=0x7fffffffde80) at ../softmmu/physmem.c:2156
#1  0x0000555555be9955 in qemu_ram_alloc (size=8589934592, ram_flags=0, mr=0x555556938af0, errp=0x7fffffffde80) at ../softmmu/physmem.c:2187
#2  0x0000555555bf7e91 in memory_region_init_ram_flags_nomigrate (mr=0x555556938af0, owner=0x555556938a90, name=0x555556a44cf0 "pc.ram", size=8589934592, ram_flags=0, errp=0x7fffffffdee0)
    at ../softmmu/memory.c:1553
#3  0x0000555555893a3d in ram_backend_memory_alloc (backend=0x555556938a90, errp=0x7fffffffdee0) at ../backends/hostmem-ram.c:33
#4  0x0000555555894689 in host_memory_backend_memory_complete (uc=0x555556938a90, errp=0x7fffffffdf48) at ../backends/hostmem.c:335
#5  0x0000555555d48c84 in user_creatable_complete (uc=0x555556938a90, errp=0x55555677c100 <error_fatal>) at ../qom/object_interfaces.c:27
#6  0x0000555555c0797a in create_default_memdev (ms=0x55555699a400, path=0x0) at ../softmmu/vl.c:2448
#7  0x0000555555c08060 in qemu_init_board () at ../softmmu/vl.c:2645
#8  0x0000555555c082ad in qmp_x_exit_preconfig (errp=0x55555677c100 <error_fatal>) at ../softmmu/vl.c:2740
#9  0x0000555555c0a936 in qemu_init (argc=13, argv=0x7fffffffe278, envp=0x7fffffffe2e8) at ../softmmu/vl.c:3775
#10 0x000055555583b6f5 in main (argc=13, argv=0x7fffffffe278, envp=0x7fffffffe2e8) at ../softmmu/main.c:49
```

`qemu_ram_alloc_internal` 函数用来分配一个 `RAMBlock` 结构以及虚拟机物理内存对应的 QEMU 进程空间中的虚拟地址。

```c
RAMBlock *qemu_ram_alloc_internal(ram_addr_t size, ram_addr_t max_size,
                                  void (*resized)(const char*,
                                                  uint64_t length,
                                                  void *host),
                                  void *host, uint32_t ram_flags,
                                  MemoryRegion *mr, Error **errp)
{
    RAMBlock *new_block;
    Error *local_err = NULL;

    assert((ram_flags & ~(RAM_SHARED | RAM_RESIZEABLE | RAM_PREALLOC |
                          RAM_NORESERVE)) == 0);
    assert(!host ^ (ram_flags & RAM_PREALLOC));

    size = HOST_PAGE_ALIGN(size);
    max_size = HOST_PAGE_ALIGN(max_size);
    new_block = g_malloc0(sizeof(*new_block));
    new_block->mr = mr;
    new_block->resized = resized;
    new_block->used_length = size;
    new_block->max_length = max_size; // 最大为 16G
    assert(max_size >= size);
    new_block->fd = -1;
    new_block->page_size = qemu_real_host_page_size;
    new_block->host = host;
    new_block->flags = ram_flags;
    ram_block_add(new_block, &local_err);
    if (local_err) {
        g_free(new_block);
        error_propagate(errp, local_err);
        return NULL;
    }
    return new_block;
}
```

`RAMBlock` 结构体表示虚拟机中的一块内存条，其记录了内存条的一些基本信息，如所属的 mr（应该会有多个 mr 使用该内存条），系统的页面大小等。

`ram_block_add` 将一块新的内存条加入到系统中，所有的 `RAMBlock` 会通过 next 域连接到一个链表中，链表头时 `ram_list.blocks` 全局遍历，这里暂时不详细分析这个函数，之后有需要再分析。

而除了 ram 会分配内存外，pc.bios, pc.rom, vga.vram 等虚拟设备的 ROM 也会分配虚拟机的物理内存。

总体来说，MemoryRegion 用于描述一个范围内的映射规则，AddressSpace 用于描述整个地址空间的映射关系。有了映射关系，当 guest 访问到这些地址的时候，就可以知道具体应该进行什么操作了。

但是这里有一个问题，这些 memory region 逐层嵌套，如果不做简化处理，为了确定一个地址到底落到了哪一个 MemoryRegion，每次都需要 从顶层 memory region 逐个找其 child memory region，其中还需要处理 alias 和 priority 的问题，而且到底命中哪一个 memory region 需要这个比较范围。 为此，QEMU 会进行一个平坦化的操作，具体过程下面会分析:

- 将 memory region 压平为 FlatView 中的 FlatRange，避免逐级查询 memory region；
- 将 FlatRange 变为树的查询，将查询从 O(n) 的查询修改为 O(log(N))；

然后还有一个问题：为什么 memory region 重叠，为什么需要制作出现 alias 的概念。 其实，很简单，**这样写代码比较方便**，

- 如果不让 memory region 可以 overlap，**那么一个模块在构建 memory region 的时候需要考虑另一个另一个模块的 memory region 的大小**，
  - **pci 空间是覆盖了 64 bit 的空间，和 ram-below-4g 和 ram-above-4g 是 overlap 的**，而 ram 的大小是动态确定，如果禁止 overlap，那么 pci 的范围也需要动态调整。
- 使用 alias 可以**将一个 memory region 的一部分放到另一个 memory region 上**，就 ram-below-4g 和 ram-above-4g 的例子而言，就是将创建的 ram-below-4g 是 pc.ram 的一部分，然后放到 MemoryRegion system 上。
  - 如果不采用这种方法，而是创建两个分裂的 pc.ram, 那么就要 mmap 两次，之后的种种操作也都需要进行两次。

即 guestos 的内存由于不同的用途，如映射给 PCI 或映射给 RAM 会发生重叠，而在访问的时候需要根据访问的对象来进行平坦操作。

### 内存布局的提交

#### 内存更改通知

address space 和 memory region 构成的无环图描述了在 QEMU 用户空间是怎样构成虚拟机的物理空间的，为了让 EPT 正常工作，每次 QEMU 修改空间布局需要通知 KVM 修改 EPT，这一工作是通过 `MemoryListener` 来实现的。

```c
struct MemoryListener {
    /**
     * @begin:
     *
     * Called at the beginning of an address space update transaction.
     * Followed by calls to #MemoryListener.region_add(),
     * #MemoryListener.region_del(), #MemoryListener.region_nop(),
     * #MemoryListener.log_start() and #MemoryListener.log_stop() in
     * increasing address order.
     *
     * @listener: The #MemoryListener.
     */
    void (*begin)(MemoryListener *listener);

    /**
     * @commit:
     *
     * Called at the end of an address space update transaction,
     * after the last call to #MemoryListener.region_add(),
     * #MemoryListener.region_del() or #MemoryListener.region_nop(),
     * #MemoryListener.log_start() and #MemoryListener.log_stop().
     *
     * @listener: The #MemoryListener.
     */
    void (*commit)(MemoryListener *listener);

    /**
     * @region_add:
     *
     * Called during an address space update transaction,
     * for a section of the address space that is new in this address space
     * space since the last transaction.
     *
     * @listener: The #MemoryListener.
     * @section: The new #MemoryRegionSection.
     */
    void (*region_add)(MemoryListener *listener, MemoryRegionSection *section);

    /**
     * @region_del:
     *
     * Called during an address space update transaction,
     * for a section of the address space that has disappeared in the address
     * space since the last transaction.
     *
     * @listener: The #MemoryListener.
     * @section: The old #MemoryRegionSection.
     */
    void (*region_del)(MemoryListener *listener, MemoryRegionSection *section);

    /**
     * @region_nop:
     *
     * Called during an address space update transaction,
     * for a section of the address space that is in the same place in the address
     * space as in the last transaction.
     *
     * @listener: The #MemoryListener.
     * @section: The #MemoryRegionSection.
     */
    void (*region_nop)(MemoryListener *listener, MemoryRegionSection *section);

    /**
     * @log_start:
     *
     * Called during an address space update transaction, after
     * one of #MemoryListener.region_add(), #MemoryListener.region_del() or
     * #MemoryListener.region_nop(), if dirty memory logging clients have
     * become active since the last transaction.
     *
     * @listener: The #MemoryListener.
     * @section: The #MemoryRegionSection.
     * @old: A bitmap of dirty memory logging clients that were active in
     * the previous transaction.
     * @new: A bitmap of dirty memory logging clients that are active in
     * the current transaction.
     */
    void (*log_start)(MemoryListener *listener, MemoryRegionSection *section,
                      int old, int new);

    unsigned priority;
    const char *name;

    /* private: */
    AddressSpace *address_space;
    QTAILQ_ENTRY(MemoryListener) link; // 将各个 MemoryListener 连接起来
    QTAILQ_ENTRY(MemoryListener) link_as; // 将同一个 as 的 MemoryListener 连接起来
};
```

`MemoryListener` 通过 `memory_listener_register` 函数注册，其主要是将 `MemoryListener` 加入到链表中。`kvm_memory_listener_register` 就是注册对应的处理函数。在之后的分析中我们可以看到，如果虚拟机变更了内存布局，如在 mr 中增加 submr，就会使用 `kvm_region_add` 来通知 KVM 更改映射关系。

```c
void kvm_memory_listener_register(KVMState *s, KVMMemoryListener *kml,
                                  AddressSpace *as, int as_id, const char *name)
{
    int i;

    kml->slots = g_malloc0(s->nr_slots * sizeof(KVMSlot));
    kml->as_id = as_id;

    for (i = 0; i < s->nr_slots; i++) {
        kml->slots[i].slot = i;
    }

    kml->listener.region_add = kvm_region_add;
    kml->listener.region_del = kvm_region_del;
    kml->listener.log_start = kvm_log_start;
    kml->listener.log_stop = kvm_log_stop;
    kml->listener.priority = 10;
    kml->listener.name = name;

    if (s->kvm_dirty_ring_size) {
        kml->listener.log_sync_global = kvm_log_sync_global;
    } else {
        kml->listener.log_sync = kvm_log_sync;
        kml->listener.log_clear = kvm_log_clear;
    }

    memory_listener_register(&kml->listener, as);

    for (i = 0; i < s->nr_as; ++i) {
        if (!s->as[i].as) {
            s->as[i].as = as;
            s->as[i].ml = kml;
            break;
        }
    }
}
```

当 QEMU 进行了创建一个新的 as、调用 `memory_region_add_subregion` 将一个 `MemoryRegion` 添加到另一个 `MemoryRegion` 的 `subregions` 中等操作时就需要通知到各个 `listener`，这个过程叫 `commit`。

```c
void memory_region_transaction_commit(void)
{
    AddressSpace *as;

    assert(memory_region_transaction_depth);
    assert(qemu_mutex_iothread_locked());

    --memory_region_transaction_depth;
    if (!memory_region_transaction_depth) {
        if (memory_region_update_pending) {
            flatviews_reset();

            MEMORY_LISTENER_CALL_GLOBAL(begin, Forward);

            QTAILQ_FOREACH(as, &address_spaces, address_spaces_link) {
                address_space_set_flatview(as);
                address_space_update_ioeventfds(as);
            }
            memory_region_update_pending = false;
            ioeventfd_update_pending = false;
            MEMORY_LISTENER_CALL_GLOBAL(commit, Forward);
        } else if (ioeventfd_update_pending) {
            QTAILQ_FOREACH(as, &address_spaces, address_spaces_link) {
                address_space_update_ioeventfds(as);
            }
            ioeventfd_update_pending = false;
        }
   }
}
```

#### 虚拟机内存平坦化

KVM 中通过 `ioctl(KVM_SET_USER_MEMORY_REGION)` 来设置 QEMU 的虚拟地址与虚拟机物理地址的映射(HVA -> GPA)。虚拟机内存的平坦化指**将 as 根 mr 表示的虚拟机内存地址空间转变成一个平坦的线性地址空间，每一段线性空间的属性和其所属的 `MemoryRegion` 都一致，每一段线性地址空间都与虚拟机的物理地址相关联**。

`FlatView` 表示虚拟机的平坦内存，as 结构体中有一个类型为 `FlatView` 的 `current_map` 成员来表示该 as 对应的平坦视角，

```c
struct FlatView {
    struct rcu_head rcu;
    unsigned ref;
    FlatRange *ranges;
    unsigned nr; // FlatRange 的个数
    unsigned nr_allocated; // 已经分配的个数
    struct AddressSpaceDispatch *dispatch;
    MemoryRegion *root;
};
```

mr 展开后的内存拓扑由 FlatRange 表示，每个 `FlatRange` 对应 as 中的一段空间。

```c
struct FlatRange {
    MemoryRegion *mr;
    hwaddr offset_in_region;
    AddrRange addr;
    uint8_t dirty_log_mask;
    bool romd_mode;
    bool readonly;
    bool nonvolatile;
};
```

而与平坦化相关的函数是 `generate_memory_topology`，

```c
static FlatView *generate_memory_topology(MemoryRegion *mr)
{
    int i;
    FlatView *view;

    view = flatview_new(mr);

    if (mr) {
        render_memory_region(view, mr, int128_zero(),
                             addrrange_make(int128_zero(), int128_2_64()),
                             false, false);
    }
    flatview_simplify(view);

    view->dispatch = address_space_dispatch_new(view);
    for (i = 0; i < view->nr; i++) {
        MemoryRegionSection mrs =
            section_from_flat_range(&view->ranges[i], view);
        flatview_add_to_dispatch(view, &mrs);
    }
    address_space_dispatch_compact(view->dispatch);
    g_hash_table_replace(flat_views, mr, view);

    return view;
}
```

其中 `render_memory_region` 会将 mr 展开并将地址信息记录到 `FlatView` 中，而 `flatview_simplify` 将 `FlatView` 中能够合并的 `FlatRange` 合并。`render_memory_region` 是平坦化的核心函数，我们来详细分析一下，

```c
static void render_memory_region(FlatView *view,  // 该 as 的 FlatView
                                 MemoryRegion *mr, // 需要展开的 mr
                                 Int128 base, // 需要展开的 mr 的开始位置
                                 AddrRange clip, // 一段虚拟机物理地址范围
                                 bool readonly,
                                 bool nonvolatile)
{
    MemoryRegion *subregion;
    unsigned i;
    hwaddr offset_in_region;
    Int128 remain;
    Int128 now;
    FlatRange fr;
    AddrRange tmp;

    if (!mr->enabled) {
        return;
    }

    // 找到该 mr 的 range
    int128_addto(&base, int128_make64(mr->addr));
    readonly |= mr->readonly;
    nonvolatile |= mr->nonvolatile;

    tmp = addrrange_make(base, mr->size);

    if (!addrrange_intersects(tmp, clip)) {
        return;
    }

    clip = addrrange_intersection(tmp, clip);

    // 如果该 mr 是 alias，找到实际的 mr
    if (mr->alias) {
        int128_subfrom(&base, int128_make64(mr->alias->addr));
        int128_subfrom(&base, int128_make64(mr->alias_offset));
        render_memory_region(view, mr->alias, base, clip,
                             readonly, nonvolatile);
        return;
    }

    /* Render subregions in priority order. */
    QTAILQ_FOREACH(subregion, &mr->subregions, subregions_link) {
        render_memory_region(view, subregion, base, clip,
                             readonly, nonvolatile);
    }

    if (!mr->terminates) {
        return;
    }

    offset_in_region = int128_get64(int128_sub(clip.start, base));
    base = clip.start;
    remain = clip.size;

    fr.mr = mr;
    fr.dirty_log_mask = memory_region_get_dirty_log_mask(mr);
    fr.romd_mode = mr->romd_mode;
    fr.readonly = readonly;
    fr.nonvolatile = nonvolatile;

    /* Render the region itself into any gaps left by the current view. */
    // 遍历所有的 FlatRange
    for (i = 0; i < view->nr && int128_nz(remain); ++i) {
        // 如果 mr 的 base 大于该 range 的最后长度，那么该 mr 应该在该 range 的右侧，
        // 不需要插入，直接判断下一个 range。
        if (int128_ge(base, addrrange_end(view->ranges[i].addr))) {
            continue;
        }
        // 这里处理 mr 的 base 小于该 range 的起始地址的情况，
        // 需要将小于起始地址的那部分单独创建一个 range 插入到 flatview 中，
        if (int128_lt(base, view->ranges[i].addr.start)) {
            now = int128_min(remain,
                             int128_sub(view->ranges[i].addr.start, base));
            fr.offset_in_region = offset_in_region;
            fr.addr = addrrange_make(base, now);
            flatview_insert(view, i, &fr);
            ++i;
            int128_addto(&base, now);
            offset_in_region += int128_get64(now);
            int128_subfrom(&remain, now);
        }
        // 这里处理该 mr 超过该 range 的部分。
        // 直接越过该 range，超过的部分下一轮循环处理。
        now = int128_sub(int128_min(int128_add(base, remain),
                                    addrrange_end(view->ranges[i].addr)),
                         base);
        int128_addto(&base, now);
        offset_in_region += int128_get64(now);
        int128_subfrom(&remain, now);
    }
    if (int128_nz(remain)) {
        fr.offset_in_region = offset_in_region;
        fr.addr = addrrange_make(base, remain);
        flatview_insert(view, i, &fr);
    }
}
```

`flatview_simplify` 会根据两个相邻的 range 的属性是否一致来判断是否将两个 range 合并。

虚拟机每次通知 KVM 内修布局修改时都会调用 `generate_memory_topology` 将内存平坦化。

#### 向 KVM 注册内存

首先要了解两个重要的数据结构， `KVMSlot` 是 KVM 中表示内存条的数据结构，而 `kvm_set_user_memory_region` 将其转化成  KVM 中用户态表示虚拟内存条的数据结构 `kvm_userspace_memory_region` 。这里还有一个类似的数据结构 `kvm_memory_region` ，不过没有地方使用它，猜测应该是历史遗留结构。

```c
typedef struct KVMSlot
{
    hwaddr start_addr; // 这块内存条在虚拟机中的物理地址
    ram_addr_t memory_size;
    void *ram; // 虚拟机对应的 QEMU 的进程虚拟地址
    int slot;
    int flags;
    int old_flags;
    /* Dirty bitmap cache for the slot */
    unsigned long *dirty_bmap;
    unsigned long dirty_bmap_size;
    /* Cache of the address space ID */
    int as_id;
    /* Cache of the offset in ram address space */
    ram_addr_t ram_start_offset;
} KVMSlot;
```

```c
struct kvm_userspace_memory_region {
	__u32 slot; // 内存条编号
	__u32 flags;
	__u64 guest_phys_addr; // 这块内存条在虚拟机中的物理地址
	__u64 memory_size; /* bytes */
	__u64 userspace_addr; /* start of the userspace allocated memory */
};
```

经过映射，当虚拟机将 GVA 转换成 GPA 进行访问时，访问的实际上是 QEMU 的用户态进程地址。而该函数最后调用 `ioctl(KVM_SET_USER_MEMORY_REGION)` 来设置虚拟机的物理地址与 QEMU 虚拟地址的映射关系,。

```c
static int kvm_set_user_memory_region(KVMMemoryListener *kml, KVMSlot *slot, bool new)
{
    KVMState *s = kvm_state;
    struct kvm_userspace_memory_region mem;
    int ret;

    mem.slot = slot->slot | (kml->as_id << 16);
    // 虚拟机的物理地址
    mem.guest_phys_addr = slot->start_addr;
    // 虚拟机对应的 QEMU 的进程虚拟地址
    mem.userspace_addr = (unsigned long)slot->ram;
    mem.flags = slot->flags;

    if (slot->memory_size && !new && (mem.flags ^ slot->old_flags) & KVM_MEM_READONLY) {
        /* Set the slot size to 0 before setting the slot to the desired
         * value. This is needed based on KVM commit 75d61fbc. */
        mem.memory_size = 0;
        ret = kvm_vm_ioctl(s, KVM_SET_USER_MEMORY_REGION, &mem);
        if (ret < 0) {
            goto err;
        }
    }
    mem.memory_size = slot->memory_size;
    ret = kvm_vm_ioctl(s, KVM_SET_USER_MEMORY_REGION, &mem);
    slot->old_flags = mem.flags;

    ...

    return ret;
}
```

下面是该函数的调用过程，可以看到初始化 `address_space_memory` 时就会通知 KVM，`memory_region_transaction_commit` 中也会进行内存平坦化操作。

```plain
#0  kvm_set_user_memory_region (kml=0x55555681d200, slot=0x7ffff2354010, new=true) at ../accel/kvm/kvm-all.c:353
#1  0x0000555555d086d5 in kvm_set_phys_mem (kml=0x55555681d200, section=0x7fffffffdbb0, add=true) at ../accel/kvm/kvm-all.c:1430
#2  0x0000555555d088bf in kvm_region_add (listener=0x55555681d200, section=0x7fffffffdbb0) at ../accel/kvm/kvm-all.c:1497
#3  0x0000555555bf68a0 in address_space_update_topology_pass (as=0x55555675ce00 <address_space_memory>, old_view=0x555556a5ace0, new_view=0x555556a83fb0, adding=true)
    at ../softmmu/memory.c:975
#4  0x0000555555bf6ba4 in address_space_set_flatview (as=0x55555675ce00 <address_space_memory>) at ../softmmu/memory.c:1051
#5  0x0000555555bf6d57 in memory_region_transaction_commit () at ../softmmu/memory.c:1103
#6  0x0000555555bfa9b2 in memory_region_update_container_subregions (subregion=0x555556a57480) at ../softmmu/memory.c:2531
#7  0x0000555555bfaa1d in memory_region_add_subregion_common (mr=0x555556822070, offset=0, subregion=0x555556a57480) at ../softmmu/memory.c:2541
#8  0x0000555555bfaa5d in memory_region_add_subregion (mr=0x555556822070, offset=0, subregion=0x555556a57480) at ../softmmu/memory.c:2549
#9  0x0000555555b5fba5 in pc_memory_init (pcms=0x55555699a400, system_memory=0x555556822070, rom_memory=0x555556a57590, ram_memory=0x7fffffffde58) at ../hw/i386/pc.c:826
#10 0x0000555555b441ee in pc_init1 (machine=0x55555699a400, host_type=0x555556061704 "i440FX-pcihost", pci_type=0x5555560616fd "i440FX") at ../hw/i386/pc_piix.c:185
#11 0x0000555555b44b1d in pc_init_v6_2 (machine=0x55555699a400) at ../hw/i386/pc_piix.c:425
#12 0x000055555594b889 in machine_run_board_init (machine=0x55555699a400) at ../hw/core/machine.c:1181
#13 0x0000555555c08082 in qemu_init_board () at ../softmmu/vl.c:2652
#14 0x0000555555c082ad in qmp_x_exit_preconfig (errp=0x55555677c100 <error_fatal>) at ../softmmu/vl.c:2740
#15 0x0000555555c0a936 in qemu_init (argc=13, argv=0x7fffffffe278, envp=0x7fffffffe2e8) at ../softmmu/vl.c:3775
#16 0x000055555583b6f5 in main (argc=13, argv=0x7fffffffe278, envp=0x7fffffffe2e8) at ../softmmu/main.c:49
```

而 KVM 中的处理流程如下：

`kvm_vm_ioctl_set_memory_region` ->

​													`kvm_set_memory_region` ->

​																						`__kvm_set_memory_region`

最后的 `__kvm_set_memory_region` 函数很复杂，展开来又是一大块，之后再分析。

### 内存分派

#### 内存分派表的构建

还是先了解相关的数据结构：

```c
struct PhysPageEntry {
    /* How many bits skip to next level (in units of L2_SIZE). 0 for a leaf. */
    uint32_t skip : 6;
     /* index into phys_sections (!skip) or phys_map_nodes (skip) */
    uint32_t ptr : 26;
};

typedef PhysPageEntry Node[P_L2_SIZE]; // 512

typedef struct PhysPageMap {
    struct rcu_head rcu;

    unsigned sections_nb;
    unsigned sections_nb_alloc;
    unsigned nodes_nb;
    unsigned nodes_nb_alloc;
    Node *nodes; // node 就是一个包含 512 个 entry 的结构
    MemoryRegionSection *sections;
} PhysPageMap;

struct AddressSpaceDispatch {
    MemoryRegionSection *mru_section; // 保存最近一次的 mrs
    /* This is a multi-level map on the physical address space.
     * The bottom level has pointers to MemoryRegionSections.
     */
    PhysPageEntry phys_map; // 指向第一级页表，相当于 x86 的 cr3 寄存器
    PhysPageMap map;
};
```

寻址过程和正常的多级页表寻址类似，`AddressSpaceDispatch` 中的 `phys_map` 就相当于 x86 的 cr3 寄存器，指向第一级页表，`PhysPageMap` 可以理解为一个页表，其中有一些基本信息，还有页表项的指针 `nodes`。而每个页表的页表项就是 `PhysPageEntry` 其可以指向下一级页表，也可以指向最终的 `MemoryRegionSection`。**`MemoryRegionSection` 就是就是 mr，只是增加了一些辅助信息**。

映射的建立流程如下：

`generate_memory_topology`(这个函数在进行映射前会进行平坦化操作) ->

​								`flatview_add_to_dispatch`() ->

​																     `register_subpage` / `register_multipage`(前者映射单个页，后者映射多个页) ->

​																			    	`phys_page_set` ->

​																									   	`phys_page_set_level`(多级页表映射)

```c
static void phys_page_set_level(PhysPageMap *map, PhysPageEntry *lp,
                                hwaddr *index, uint64_t *nb, uint16_t leaf,
                                int level)
{
    PhysPageEntry *p;
    hwaddr step = (hwaddr)1 << (level * P_L2_BITS);

    if (lp->skip && lp->ptr == PHYS_MAP_NODE_NIL) {
        lp->ptr = phys_map_node_alloc(map, level == 0);
    }
    p = map->nodes[lp->ptr];
    lp = &p[(*index >> (level * P_L2_BITS)) & (P_L2_SIZE - 1)];

    while (*nb && lp < &p[P_L2_SIZE]) {
        if ((*index & (step - 1)) == 0 && *nb >= step) {
            lp->skip = 0;
            lp->ptr = leaf;
            *index += step;
            *nb -= step;
        } else {
            phys_page_set_level(map, lp, index, nb, leaf, level - 1);
        }
        ++lp;
    }
}
```

#### 地址分派

映射信息建立完了就可以利用其找到对应的 mr。这里以 MMIO 的写寻址为例分析地址如何查找。

```c
int kvm_cpu_exec(CPUState *cpu)
{
    struct kvm_run *run = cpu->kvm_run;
    int ret, run_ret;

    ...

    do {
        MemTxAttrs attrs;

        if (cpu->vcpu_dirty) {
            kvm_arch_put_registers(cpu, KVM_PUT_RUNTIME_STATE);
            cpu->vcpu_dirty = false;
        }

        kvm_arch_pre_run(cpu, run);

        /* Read cpu->exit_request before KVM_RUN reads run->immediate_exit.
         * Matching barrier in kvm_eat_signals.
         */
        smp_rmb();

        run_ret = kvm_vcpu_ioctl(cpu, KVM_RUN, 0);

        attrs = kvm_arch_post_run(cpu, run);

        ...

        trace_kvm_run_exit(cpu->cpu_index, run->exit_reason);
        switch (run->exit_reason) {
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

            ...

        default:
            DPRINTF("kvm_arch_handle_exit\n");
            ret = kvm_arch_handle_exit(cpu, run);
            break;
        }
    } while (ret == 0);

    ...

    return ret;
}
```

之后的执行流程如下：

`address_space_rw` ->

​			`address_space_write` ->

​						`flatview_write` ->

​									`flatview_write_continue` ->

​												`flatview_translate` ->

​															`flatview_do_translate` ->

​																			`address_space_translate_internal` ->

​																							`address_space_lookup_region`

```c
static MemoryRegionSection *address_space_lookup_region(AddressSpaceDispatch *d,
                                                        hwaddr addr,
                                                        bool resolve_subpage)
{
    MemoryRegionSection *section = qatomic_read(&d->mru_section);
    subpage_t *subpage;

    if (!section || section == &d->map.sections[PHYS_SECTION_UNASSIGNED] ||
        !section_covers_addr(section, addr)) {
        section = phys_page_find(d, addr);
        qatomic_set(&d->mru_section, section);
    }
    if (resolve_subpage && section->mr->subpage) {
        subpage = container_of(section->mr, subpage_t, iomem);
        section = &d->map.sections[subpage->sub_section[SUBPAGE_IDX(addr)]];
    }
    return section;
}
```

这样就完成了虚拟机物理地址的分派，找到了 GPA 对应的 mr，也就找到了对应的 QEMU 中的进程虚拟地址。

### KVM 内存虚拟化

这部分涉及的内容也很多，主要是 EPT 页面的使用，之后有需要再分析。

### MMIO 机制

#### 虚拟设备 MMIO 实现原理

在 X86 下可以通过 PIO 或 MMIO 两种方式访问设备。PIO 即通过专门的访问设备指令进行访问，而 MMIO 是将设备地址映射到内存空间，访问外设和访问内存是一样的，那么 MMIO 是怎样实现的呢？这里先简单介绍 MMIO 的机制，具体的实现在设备虚拟化部分分析。

- **QEMU 申明一段内存作为 MMIO 内存**，这里只是建立一个映射关系，不会立即分配内存空间；

- SeaBIOS 会分配好所有设备 MMIO 对应的基址；

- 当 guestos 第一次访问 MMIO 的地址时，会发生 EPT violation，产生 VM Exit；

- KVM 创建一个 EPT 页表，并设置页表项特殊标志；

- 虚拟机之后再访问对应的 MMIO 地址时会产生 EPT miss，从而产生 VM Exit，退出到 KVM，然后 KVM 发现无法在内核态处理，进入到用户态 QEMU 处理；

#### coalesced MMIO

根据 MMIO 的实现原理，我们知道每次发生 MMIO 都会导致虚拟机退出到 QEMU 应用层，但是往往是**多个 MMIO 一起操作**，这时可以**将多个 MMIO 操作暂存，等到最后一个 MMIO 时再退出到 QEMU 一起处理，这就是 coalesced MMIO**。

### 虚拟机脏页跟踪

脏页跟踪是热迁移的基础。热迁移即在虚拟机运行时将其从一台宿主机迁移到另一台宿主机，但是在迁移过程中，虚拟机还是会不断的写内存，如果虚拟机在 QEMU 迁移了该也之后又对该页写入了新数据，那么 QEMU 就需要重新迁移该页。那么脏页跟踪就是通过一个脏页位图跟踪虚拟机的物理内存有哪些内存被修改了。具体的实现在热迁移部分再分析。

### Reference

[1] http://blog.vmsplice.net/2016/01/qemu-internals-how-guest-physical-ram.html
