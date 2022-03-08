## Memory Virtualization

### Guest RAM configuration

The QEMU command-line option `-m [size=]megs[,slots=n,maxmem=size]` specifies the initial guest RAM size as well as the maximum guest RAM size and number of slots for memory chips (DIMMs).

The reason for the maximum size and slots is that QEMU emulates DIMM hotplug so the guest operating system can detect when new memory is added and removed using the same mechanism as on real hardware. This involves plugging or unplugging a DIMM into a slot, just like on a physical machine. In other words, changing the amount of memory available isn't done in byte units, it's done by changing the set of DIMMs plugged into the emulated machine.

The MemoryRegion is the link between guest physical address space and the RAMBlocks containing the memory. Each MemoryRegion has the ram_addr_t offset of the RAMBlock and each RAMBlock has a MemoryRegion pointer.

### QEMU 内存初始化

#### 基本结构

还是从基本的数据结构入手，首先介绍 AddressSpace 结构体，QEMU 用其表示一个虚拟机所能够访问的所有物理地址。

```c
/**
 * struct AddressSpace: describes a mapping of addresses to #MemoryRegion objects
 */
struct AddressSpace {
    /* private: */
    struct rcu_head rcu;
    char *name;
    MemoryRegion *root; //

    /* Accessed via RCU.  */
    struct FlatView *current_map;

    int ioeventfd_nb;
    struct MemoryRegionIoeventfd *ioeventfds;
    QTAILQ_HEAD(, MemoryListener) listeners;
    QTAILQ_ENTRY(AddressSpace) address_spaces_link;
};
```

虽然地址空间是同一块，但是不同设备对这块地址空间的视角不同，而 AddressSpace 描述的就是不同设备的视角。

MemoryRegion 表示虚拟机的一段内存区域，其是内存模拟的核心结构，整个内存空间就是 MemoryRegion 构成的无环图。

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

当给定一个 as 中的地址时，其根据如下原则需要对应的 mr：

（1）从 as 中找到 root 的所有 subregion，如果地址不在 region 中则放弃考虑 region。

（2）如果地址在一个叶子 mr 中，返回该 mr。

（3）如果该子 mr 也是一个 container，那么在子容器中递归查找。

（4）如果 mr 时一个 alias，那么在 alias 中查找。

（5）如果以上过程都没有找到匹配项，但该 container 本身有自己的 MMIO 或 RAM，那么返回这个 container 本身，否则根据下一个优先级查找。（不懂）

（6）如果都没有找到，return。

#### QEMU 虚拟机内存初始化

首先在 qemu_init 中会进行一些初始化工作，

```plain
#0  memory_map_init () at ../softmmu/physmem.c:2664
#1  0x0000555555beb3a6 in cpu_exec_init_all () at ../softmmu/physmem.c:3070
#2  0x0000555555c06c90 in qemu_create_machine (qdict=0x555556819dd0) at ../softmmu/vl.c:2163
#3  0x0000555555c0a7b9 in qemu_init (argc=13, argv=0x7fffffffe278, envp=0x7fffffffe2e8) at ../softmmu/vl.c:3707
#4  0x000055555583b6f5 in main (argc=13, argv=0x7fffffffe278, envp=0x7fffffffe2e8) at ../softmmu/main.c:49
```

cpu_exec_init_all 会调用 `io_mem_init` 和 `memory_map_init`。io_mem_init 创建包含若干包含所有地址空间的 mr。

```c
static void io_mem_init(void)
{
    memory_region_init_io(&io_mem_unassigned, NULL, &unassigned_mem_ops, NULL,
                          NULL, UINT64_MAX); // UINT64_MAX，所有的地址
}
```

memory_map_init 则是一个重要函数。其创建 address_space_memory 和 address_space_io 两个 as，分别用来表示虚拟机的内存地址空间和 I/O 地址空间。

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

system_memory 和 system_io 就是对应的 root mr。它们均为全局变量，在系统中会多次使用。

```c
void address_space_init(AddressSpace *as, MemoryRegion *root, const char *name)
{
    memory_region_ref(root);
    as->root = root;
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

进一步的内存的初始化是在 pc_init1 中进行的，pc_init1 会调用 pc_memory_init 进行初始化。

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
    *ram_memory = machine->ram;
    ram_below_4g = g_malloc(sizeof(*ram_below_4g));
    memory_region_init_alias(ram_below_4g, NULL, "ram-below-4g", machine->ram, // 建立一个 region
                             0, x86ms->below_4g_mem_size);
    memory_region_add_subregion(system_memory, 0, ram_below_4g); // 该 region 是 system_memory 的子 region
    e820_add_entry(0, x86ms->below_4g_mem_size, E820_RAM); // 将其地址关系加入到 e820 表中供 bios 使用
    if (x86ms->above_4g_mem_size > 0) { // 内存大于 4g 的部分也要建立映射
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

    fw_cfg = fw_cfg_arch_create(machine,
                                x86ms->boot_cpus, x86ms->apic_id_limit);

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

    if (linux_boot) {
        x86_load_linux(x86ms, fw_cfg, pcmc->acpi_data_size,
                       pcmc->pvh_enabled);
    }

    for (i = 0; i < nb_option_roms; i++) {
        rom_add_option(option_rom[i].name, option_rom[i].bootindex);
    }
    x86ms->fw_cfg = fw_cfg;

    /* Init default IOAPIC address space */
    x86ms->ioapic_as = &address_space_memory;

    /* Init ACPI memory hotplug IO base address */
    pcms->memhp_io_base = ACPI_MEMORY_HOTPLUG_BASE;
}
```

### Reference

[1] http://blog.vmsplice.net/2016/01/qemu-internals-how-guest-physical-ram.html
