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

再看看正常的 QEMU 中给 guestos 注册的内存是怎样的，整个文件很长，可以看[这里](https://github.com/UtopianFuture/UtopianFuture.github.io/blob/master/virtualization/QEMU-mtree.md)。

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
    RAMBlock *ram_block; // 表示实际分配的物理内存
    Object *owner;

    const MemoryRegionOps *ops; // 该 mr 对应的回调函数
    void *opaque;
    MemoryRegion *container; // 该 mr 的上一级 mr
    Int128 size;
    hwaddr addr;
    void (*destructor)(MemoryRegion *mr);
    uint64_t align;
    bool terminates; // 是否是叶子节点
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

常见的 MemoryRegion 有以下几种：

- RAM： host 上一段实际分配给 guest 作为物理内存的虚拟内存；
- MMIO：guest 上的一段内存，但是在 host 上没有对应的虚拟内存，而是截获对这个区域的访问，调用对应读写函数访问虚拟设备；
- ROM：与 RAM 类似，但是该类型的内存是只读；
- ROM device：其在读方面类型 RAM，能直接读，但在写方面类似于 MMIO，调用对应读写函数访问虚拟设备；
- container：包含若干个 mr，每个 mr 在这个 container 的偏移都不一样，其主要将多个 mr 合并成一个；
- alias：mr 的其他部分；

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

而如果 guestos 写相同的地址，在 system_memory 的空间和 system_io 空间的效果不同的。写 system_io 的话 KVM 无法处理，需要返回到 QEMU，

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
    memory_region_add_subregion(system_memory, 0, ram_below_4g); // 该 region 是 system_memory 的 subregion
    e820_add_entry(0, x86ms->below_4g_mem_size, E820_RAM); // 将其地址关系加入到 e820 表中供 bios 使用
    if (x86ms->above_4g_mem_size > 0) { // 内存大于 4g 的部分也要建立映射，它们都是 system_memory 的 subregion
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

`ram_block_add` 首先会在 host 中为 guest 分配一块物理内存空间，然后将这个新的内存条加入到系统中，所有的 `RAMBlock` 会通过 next 域连接到一个链表中，即全局变量 `ram_list`。

```c
static void ram_block_add(RAMBlock *new_block, Error **errp)
{
    const bool noreserve = qemu_ram_is_noreserve(new_block);
    const bool shared = qemu_ram_is_shared(new_block);
    RAMBlock *block;
    RAMBlock *last_block = NULL;
    ram_addr_t old_ram_size, new_ram_size;
    Error *err = NULL;

    old_ram_size = last_ram_page();

    qemu_mutex_lock_ramlist();
    new_block->offset = find_ram_offset(new_block->max_length);

    if (!new_block->host) {

        ...

        } else {
            new_block->host = qemu_anon_ram_alloc(new_block->max_length, // 调用 qemu_ram_mmap
                                                  &new_block->mr->align,
                                                  shared, noreserve);
            ...

            memory_try_enable_merging(new_block->host, new_block->max_length);
        }
    }

    new_ram_size = MAX(old_ram_size,
              (new_block->offset + new_block->max_length) >> TARGET_PAGE_BITS);
	...

    if (block) {
        QLIST_INSERT_BEFORE_RCU(block, new_block, next);
    } else if (last_block) {
        QLIST_INSERT_AFTER_RCU(last_block, new_block, next);
    } else { /* list is empty */
        QLIST_INSERT_HEAD_RCU(&ram_list.blocks, new_block, next);
    }
    ram_list.mru_block = NULL;

    ...

    if (new_block->host) {

        ...

        ram_block_notify_add(new_block->host, new_block->used_length,
                             new_block->max_length);
    }
}
```

而除了 ram 会分配内存外，pc.bios, pc.rom, vga.vram 等虚拟设备的 ROM 也会分配虚拟机的物理内存。但是 MMIO 就不会分配物理内存，而是截获对 MMIO 的访问，调用相应的虚拟设备。

ok，上文涉及到的用来做内存虚拟化的结构体很多：RAMBlock，MemoryRegion，AddressSpace，MemoryRegionSection，KVMSlot 和 kvm_userspace_memory_region，我们这里总结一下：

ROMBlock 才是真正分配了 host 内存的地方，如果把它直接理解成一个内存条也是非常合适的，但实际上不仅仅如此，还有设备自有内存，显存。它的主要元素就是 mr，host, offset 和 used_length。

```c
struct RAMBlock {
    struct rcu_head rcu;
    struct MemoryRegion *mr; // 每个 ramblock 都有唯一一个 mr 对应，但不是所有的 mr 都有 ramblock 对应，如 mmio
    uint8_t *host; // hva
    uint8_t *colo_cache; /* For colo, VM's ram cache */
    ram_addr_t offset;
    ram_addr_t used_length;
    ram_addr_t max_length;
    void (*resized)(const char*, uint64_t length, void *host);
    uint32_t flags;
    /* Protected by iothread lock.  */
    char idstr[256];
    /* RCU-enabled, writes protected by the ramlist lock */
    QLIST_ENTRY(RAMBlock) next;
    QLIST_HEAD(, RAMBlockNotifier) ramblock_notifiers;
    int fd;
    size_t page_size;
    /* dirty bitmap used during migration */
    unsigned long *bmap;
    /* bitmap of already received pages in postcopy */
    unsigned long *receivedmap;
    unsigned long *clear_bmap;
    uint8_t clear_bmap_shift;
    ram_addr_t postcopy_length;
};
```

而 MemoryRegion 用于描述一个范围内的映射规则，AddressSpace 用于描述整个地址空间的映射关系。有了映射关系，当 guest 访问到这些地址的时候，就可以知道具体应该进行什么操作了。

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

首先明确虚拟机中 mmu 的功能，当虚拟机内部进行内存访问的是后，mmu 会根据 guest 的页表将 GVA 转化为 GPA，然后根据 EPT 页表将 GPA 转换成 HPA，这就是所谓的两级地址转换，即代码中出现的 tdp(two dimission page)。在将 GVA 转化为 GPA 的过程中如果发生缺页异常，这个异常会由 guest 处理，不需要 vm exit，即 guest 的缺页异常处理函数负责分配一个客户物理页面，将该页面物理地址回填，建立客户页表结构。而在 GPA 转换成 HPA 过程中发生缺页异常，则会以 `EXIT_REASON_EPT_VIOLATION` 退出到 host，然后使用 `[EXIT_REASON_EPT_VIOLATION] = handle_ept_violation,` 进行处理。映射建立完后中断返回，切换到非根模式继续运行。

这个大概的流程，我们需要搞懂 mmu 是怎样初始化、使用、处理缺页的，ept 是怎样初始化、使用、处理缺页的。下面就按照这个步骤进行分析。

#### 虚拟机 MMU 初始化

在 KVM 初始化的时候会调用架构相关的 `hardware_setup`，其会调用 `setup_vmcs_config`，在启动读取 `MSR_IA32_VMX_PROCBASED_CTLS` 寄存器，其控制大部分虚拟机执行特性，这个寄存器的功能可以看手册的 Volume 3 A.3.2 部分，其中有全面的介绍。这里我们只关注和内存虚拟化相关的部分。

其实按照之前的习惯，应该先分析数据结构的，但是 `kvm_mmu` 太大了，而且大部分都不理解，就不水字数了，之后有机会再补充。

```c
static __init int hardware_setup(void)
{
	unsigned long host_bndcfgs;
	struct desc_ptr dt;
	int r, ept_lpage_level;

	store_idt(&dt);
	host_idt_base = dt.address;

	vmx_setup_user_return_msrs();

	if (setup_vmcs_config(&vmcs_config, &vmx_capability) < 0)
		return -EIO;

	...

	if (!cpu_has_vmx_vpid() || !cpu_has_vmx_invvpid() ||
	    !(cpu_has_vmx_invvpid_single() || cpu_has_vmx_invvpid_global()))
		enable_vpid = 0;

    // 这些函数其实都是读取全局变量 vmcs_config，其在 setup_vmcs_config 中以及设置好了
	if (!cpu_has_vmx_ept() ||
	    !cpu_has_vmx_ept_4levels() ||
	    !cpu_has_vmx_ept_mt_wb() ||
	    !cpu_has_vmx_invept_global())
		enable_ept = 0; // 和 mmu, ept 相关的

	/* NX support is required for shadow paging. */
	if (!enable_ept && !boot_cpu_has(X86_FEATURE_NX)) {
		pr_err_ratelimited("kvm: NX (Execute Disable) not supported\n");
		return -EOPNOTSUPP;
	}

	if (!cpu_has_vmx_ept_ad_bits() || !enable_ept)
		enable_ept_ad_bits = 0;

	...

	if (enable_ept)
		kvm_mmu_set_ept_masks(enable_ept_ad_bits,
				      cpu_has_vmx_ept_execute_only());

	if (!enable_ept)
		ept_lpage_level = 0;
	else if (cpu_has_vmx_ept_1g_page())
		ept_lpage_level = PG_LEVEL_1G;
	else if (cpu_has_vmx_ept_2m_page())
		ept_lpage_level = PG_LEVEL_2M;
	else
		ept_lpage_level = PG_LEVEL_4K;
	kvm_configure_mmu(enable_ept, 0, vmx_get_max_tdp_level(),
			  ept_lpage_level);

	/*
	 * Only enable PML when hardware supports PML feature, and both EPT
	 * and EPT A/D bit features are enabled -- PML depends on them to work.
	 */
	if (!enable_ept || !enable_ept_ad_bits || !cpu_has_vmx_pml())
		enable_pml = 0;

	...

	vmx_set_cpu_caps();

	r = alloc_kvm_area();
	if (r)
		nested_vmx_hardware_unsetup();
	return r;
}
```

主要就是通过 `rdmsr` 读取物理寄存器，然后进行初始化。

```c
static __init int adjust_vmx_controls(u32 ctl_min, u32 ctl_opt,
				      u32 msr, u32 *result)
{
	u32 vmx_msr_low, vmx_msr_high;
	u32 ctl = ctl_min | ctl_opt;

	rdmsr(msr, vmx_msr_low, vmx_msr_high);

	ctl &= vmx_msr_high; /* bit == 0 in high word ==> must be zero */
	ctl |= vmx_msr_low;  /* bit == 1 in low word  ==> must be one  */

	/* Ensure minimum (required) set of control bits are supported. */
	if (ctl_min & ~ctl)
		return -EIO;

	*result = ctl;
	return 0;
}
```

##### 关键函数 kvm_mmu_create

KVM 在创建 VCPU 时会调用 `kvm_mmu_create` 创建虚拟机 MMU，从数据结构来看每个 VCPU 都有一个 MMU。

KVM 部分不同内核版本差别还挺大的，索性将所有的初始化相关函数都给出来，一个个分析，

`kvm_vm_ioctl` -> `kvm_vm_ioctl_create_vcpu` -> `kvm_arch_vcpu_create` -> `kvm_mmu_create` / `kvm_init_mmu`

```c
int kvm_mmu_create(struct kvm_vcpu *vcpu)
{
	int ret;

    // 这些变量都是干什么用的？
	vcpu->arch.mmu_pte_list_desc_cache.kmem_cache = pte_list_desc_cache;
	vcpu->arch.mmu_pte_list_desc_cache.gfp_zero = __GFP_ZERO;

	vcpu->arch.mmu_page_header_cache.kmem_cache = mmu_page_header_cache;
	vcpu->arch.mmu_page_header_cache.gfp_zero = __GFP_ZERO;

	vcpu->arch.mmu_shadow_page_cache.gfp_zero = __GFP_ZERO;

	vcpu->arch.mmu = &vcpu->arch.root_mmu; // 这几个都是一个意思
	vcpu->arch.walk_mmu = &vcpu->arch.root_mmu;

	vcpu->arch.nested_mmu.translate_gpa = translate_nested_gpa;

    // 这里创建了两个 mmu，怎么运行的？
    // 看 struct kvm_vcpu_arch 的注释，这个好像是嵌套虚拟化用的
	ret = __kvm_mmu_create(vcpu, &vcpu->arch.guest_mmu);

	ret = __kvm_mmu_create(vcpu, &vcpu->arch.root_mmu);

    ...

	return ret;
}
```

```c
static int __kvm_mmu_create(struct kvm_vcpu *vcpu, struct kvm_mmu *mmu)
{
	struct page *page;
	int i;

	mmu->root_hpa = INVALID_PAGE; // ept 页表中第一级页表的物理地址
	mmu->root_pgd = 0;
	mmu->translate_gpa = translate_gpa;
	for (i = 0; i < KVM_MMU_NUM_PREV_ROOTS; i++)
		mmu->prev_roots[i] = KVM_MMU_ROOT_INFO_INVALID;

    // PAE 是地址扩展的意思，将 32 位线性地址转换成 52 位物理地址，但不清楚为何需要下面这些操作
	/*
	 * When using PAE paging, the four PDPTEs are treated as 'root' pages,
	 * while the PDP table is a per-vCPU construct that's allocated at MMU
	 * creation.  When emulating 32-bit mode, cr3 is only 32 bits even on
	 * x86_64.  Therefore we need to allocate the PDP table in the first
	 * 4GB of memory, which happens to fit the DMA32 zone.  TDP paging
	 * generally doesn't use PAE paging and can skip allocating the PDP
	 * table.  The main exception, handled here, is SVM's 32-bit NPT.  The
	 * other exception is for shadowing L1's 32-bit or PAE NPT on 64-bit
	 * KVM; that horror is handled on-demand by mmu_alloc_shadow_roots().
	 */
	if (tdp_enabled && kvm_mmu_get_tdp_level(vcpu) > PT32E_ROOT_LEVEL)
		return 0;

	page = alloc_page(GFP_KERNEL_ACCOUNT | __GFP_DMA32);
	if (!page)
		return -ENOMEM;

	mmu->pae_root = page_address(page);

	/*
	 * CR3 is only 32 bits when PAE paging is used, thus it's impossible to
	 * get the CPU to treat the PDPTEs as encrypted.  Decrypt the page so
	 * that KVM's writes and the CPU's reads get along.  Note, this is
	 * only necessary when using shadow paging, as 64-bit NPT can get at
	 * the C-bit even when shadowing 32-bit NPT, and SME isn't supported
	 * by 32-bit kernels (when KVM itself uses 32-bit NPT).
	 */
	if (!tdp_enabled)
		set_memory_decrypted((unsigned long)mmu->pae_root, 1);
	else
		WARN_ON_ONCE(shadow_me_mask);

	for (i = 0; i < 4; ++i)
		mmu->pae_root[i] = INVALID_PAE_ROOT;

	return 0;
}
```

##### 关键函数 kvm_init_mmu

这个函数根据不同的使用场景调用不同的初始化函数，这里主要分析 `init_kvm_tdp_mmu`，

```c
void kvm_init_mmu(struct kvm_vcpu *vcpu)
{
	if (mmu_is_nested(vcpu))
		init_kvm_nested_mmu(vcpu);
	else if (tdp_enabled)
		init_kvm_tdp_mmu(vcpu); // tdp - two dimission page，这个是需要用到的
	else
        // 我一直以为 softmmu 是 qemu 中才有的优化，没想到在 kvm 中也有
        // 并不是，这是影子页表的初始化
		init_kvm_softmmu(vcpu);
}
```

这里是设置 mmu 的几个回调函数，之后根据 VCPU 所处的模式设置 `gva_to_gpa`，

```c
static void init_kvm_tdp_mmu(struct kvm_vcpu *vcpu)
{
	struct kvm_mmu *context = &vcpu->arch.root_mmu;
	struct kvm_mmu_role_regs regs = vcpu_to_role_regs(vcpu);
	union kvm_mmu_role new_role =
		kvm_calc_tdp_mmu_root_page_role(vcpu, &regs, false);

	if (new_role.as_u64 == context->mmu_role.as_u64)
		return;

	context->mmu_role.as_u64 = new_role.as_u64;
	context->page_fault = kvm_tdp_page_fault; // 缺页处理
	context->sync_page = nonpaging_sync_page; // 没啥用，返回 0
	context->invlpg = NULL; // 不需要处理么，我看 handle_exit 中有 invlpg 的处理函数
	context->shadow_root_level = kvm_mmu_get_tdp_level(vcpu);
	context->direct_map = true; // 直接映射？
	context->get_guest_pgd = get_cr3;
	context->get_pdptr = kvm_pdptr_read;
	context->inject_page_fault = kvm_inject_page_fault;
	context->root_level = role_regs_to_root_level(&regs);

	if (!is_cr0_pg(context))
		context->gva_to_gpa = nonpaging_gva_to_gpa;
	else if (is_cr4_pae(context))
		context->gva_to_gpa = paging64_gva_to_gpa;
	else
		context->gva_to_gpa = paging32_gva_to_gpa;

	reset_guest_paging_metadata(vcpu, context);
	reset_tdp_shadow_zero_bits_mask(vcpu, context);
}
```

#### 虚拟机物理地址的设置

虚拟机的物理内存是通过 `ioctl(KVM_SET_USER_MEMORY_REGION)` 实现的。

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

	case KVM_SET_USER_MEMORY_REGION: {
		struct kvm_userspace_memory_region kvm_userspace_mem;

		r = -EFAULT;
		if (copy_from_user(&kvm_userspace_mem, argp, // 获取从用户态传来的数据
						sizeof(kvm_userspace_mem)))
			goto out;

        // 现在 userspace_addr 和 memory_size 应该已经分配好了
		r = kvm_vm_ioctl_set_memory_region(kvm, &kvm_userspace_mem);
		break;
	}

    ...

	default:
		r = kvm_arch_vm_ioctl(filp, ioctl, arg);
	}
out:
	return r;
}
```

这其中 `kvm_userspace_memory_region` 是用户态传过来的参数类型，用来表示虚拟机的一段物理地址，

```c
/* for KVM_SET_USER_MEMORY_REGION */
struct kvm_userspace_memory_region {
	__u32 slot; // ID，包括 AddressSpace 的 ID 和本身的 ID
	__u32 flags; // 该段内存的属性
	__u64 guest_phys_addr; // 虚拟机的物理内存
	__u64 memory_size; /* bytes */ // 大小
	__u64 userspace_addr; /* start of the userspace allocated memory */ // 用户态分配的地址
};
```

##### kvm

KVM 中有几个相似的数据结构，我们来分析一下它们的区别：

首先是表示虚拟机的 `struct kvm`，

```c
struct kvm {

    ...

	struct mm_struct *mm; /* userspace tied to this vm */
    // 为 0 表示普通虚拟机用的 RAM，为 1 表示 SMM 的模拟
	struct kvm_memslots __rcu *memslots[KVM_ADDRESS_SPACE_NUM];
	struct kvm_vcpu *vcpus[KVM_MAX_VCPUS];

	...
};
```

##### kvm_memslots

从代码上来看，整个虚拟机就两个 `kvm_memslots`，然后 `kvm_memory_slot` 应该才是具体的内存区域，

```c
/*
 * Note:
 * memslots are not sorted by id anymore, please use id_to_memslot()
 * to get the memslot by its id.
 */
struct kvm_memslots {
	u64 generation;
	/* The mapping table from slot id to the index in memslots[]. */
	short id_to_index[KVM_MEM_SLOTS_NUM]; // slot id 和 index 有什么区别么
	atomic_t last_used_slot;
	int used_slots;
	struct kvm_memory_slot memslots[];
};
```

##### kvm_memory_slot

这个就很合理了，一个内存条该有的信息，

```c
struct kvm_memory_slot {
	gfn_t base_gfn; // 该 slot 对应虚拟机页框的起点
	unsigned long npages; // 该 slot 中有几个 page
	unsigned long *dirty_bitmap; // 脏页的 bitmap
	struct kvm_arch_memory_slot arch;
	unsigned long userspace_addr; // 对应 HVA 的地址
	u32 flags;
	short id;
	u16 as_id;
};
```

##### kvm_arch_memory_slot

`kvm_arch_memory_slot` 表示内存条中架构相关的信息，

```c
struct kvm_arch_memory_slot {
    // rmap 保存 gfn 与对应页表项的 map
    // KVM_NR_PAGE_SIZES 表示页表项的种类，目前为 3
    // 分别表示 4KB,2MB,1GB 的页面，后两者称为大页
	struct kvm_rmap_head *rmap[KVM_NR_PAGE_SIZES];
    // 保存大页信息，所以个数比 rmap 少 1
	struct kvm_lpage_info *lpage_info[KVM_NR_PAGE_SIZES - 1];
	unsigned short *gfn_track[KVM_PAGE_TRACK_MAX]; // 这个又是啥呢
};
```

##### 关键函数 __kvm_set_memory_region

该函数主要负责建立映射（哪个到哪个的映射？）

```c
/*
 * Allocate some memory and give it an address in the guest physical address space.
 *
 * Discontiguous memory is allowed, mostly for framebuffers.
 *
 * Must be called holding kvm->slots_lock for write.
 */
int __kvm_set_memory_region(struct kvm *kvm,
			    const struct kvm_userspace_memory_region *mem)
{
	struct kvm_memory_slot old, new;
	struct kvm_memory_slot *tmp;
	enum kvm_mr_change change;
	int as_id, id;
	int r;

	r = check_memory_region_flags(mem);
	if (r)
		return r;

	as_id = mem->slot >> 16; // 前面说了 slot 表示 AddressSpace 的 ID 和该内存条本身的 ID
	id = (u16)mem->slot;

	/* General sanity checks */

    ...

	/*
	 * Make a full copy of the old memslot, the pointer will become stale
	 * when the memslots are re-sorted by update_memslots(), and the old
	 * memslot needs to be referenced after calling update_memslots(), e.g.
	 * to free its resources and for arch specific behavior.
	 */
	tmp = id_to_memslot(__kvm_memslots(kvm, as_id), id); // 这个应该是理解上面几个数据结构之间关系的关键
	if (tmp) { // 该内存条之前已经存在
		old = *tmp;
		tmp = NULL;
	} else {
		memset(&old, 0, sizeof(old));
		old.id = id;
	}

	if (!mem->memory_size)
		return kvm_delete_memslot(kvm, mem, &old, as_id);

	new.as_id = as_id;
	new.id = id;
	new.base_gfn = mem->guest_phys_addr >> PAGE_SHIFT;
	new.npages = mem->memory_size >> PAGE_SHIFT;
	new.flags = mem->flags;
	new.userspace_addr = mem->userspace_addr;

	if (new.npages > KVM_MEM_MAX_NR_PAGES)
		return -EINVAL;

	if (!old.npages) { // 该内存条之前不存在
		change = KVM_MR_CREATE; // 所以状态为 create
		new.dirty_bitmap = NULL;
		memset(&new.arch, 0, sizeof(new.arch));
	} else { /* Modify an existing slot. */
		if ((new.userspace_addr != old.userspace_addr) ||
		    (new.npages != old.npages) || // 不是要修改么，为啥不能相同？
		    ((new.flags ^ old.flags) & KVM_MEM_READONLY))
			return -EINVAL;

        // 哦，原来只能该映射到 gpa 的地址，整个内存条的大小和位置都不能变
		if (new.base_gfn != old.base_gfn)
			change = KVM_MR_MOVE;
		else if (new.flags != old.flags)
			change = KVM_MR_FLAGS_ONLY;
		else /* Nothing to change. */
			return 0;

		/* Copy dirty_bitmap and arch from the current memslot. */
		new.dirty_bitmap = old.dirty_bitmap; // 都映射到新的 gfa 了，为啥 dirty bitmap 还要继承？
		memcpy(&new.arch, &old.arch, sizeof(new.arch));
	}

	if ((change == KVM_MR_CREATE) || (change == KVM_MR_MOVE)) {
		/* Check for overlaps */
		kvm_for_each_memslot(tmp, __kvm_memslots(kvm, as_id)) {
			if (tmp->id == id)
				continue;
            // 检查和现有的 memslots 是否重叠
            // 因为这些都是 gpa，即 guest 看到的物理地址，不能重叠
			if (!((new.base_gfn + new.npages <= tmp->base_gfn) ||
			      (new.base_gfn >= tmp->base_gfn + tmp->npages)))
				return -EEXIST;
		}
	}

	/* Allocate/free page dirty bitmap as needed */
	if (!(new.flags & KVM_MEM_LOG_DIRTY_PAGES)) // 好吧，对于 dirty page 我没有完全搞懂
		new.dirty_bitmap = NULL;
	else if (!new.dirty_bitmap && !kvm->dirty_ring_size) {
		r = kvm_alloc_dirty_bitmap(&new);
		if (r)
			return r;

		if (kvm_dirty_log_manual_protect_and_init_set(kvm))
			bitmap_set(new.dirty_bitmap, 0, new.npages);
	}

	r = kvm_set_memslot(kvm, mem, &old, &new, as_id, change); // 修改还是创建都是在这里完成的
	if (r)
		goto out_bitmap;

	if (old.dirty_bitmap && !new.dirty_bitmap)
		kvm_destroy_dirty_bitmap(&old);
	return 0;

out_bitmap:
	if (new.dirty_bitmap && !old.dirty_bitmap)
		kvm_destroy_dirty_bitmap(&new);
	return r;
}
```

为什么要经过这样的转换？

```c
static inline
struct kvm_memory_slot *id_to_memslot(struct kvm_memslots *slots, int id)
{
	int index = slots->id_to_index[id];
	struct kvm_memory_slot *slot;

	if (index < 0)
		return NULL;

	slot = &slots->memslots[index];

	WARN_ON(slot->id != id);
	return slot;
}
```

##### 关键函数 kvm_set_memslot

```c
static int kvm_set_memslot(struct kvm *kvm,
			   const struct kvm_userspace_memory_region *mem,
			   struct kvm_memory_slot *old,
			   struct kvm_memory_slot *new, int as_id,
			   enum kvm_mr_change change)
{
	struct kvm_memory_slot *slot;
	struct kvm_memslots *slots;
	int r;

	...

    // 为何要 duplicate？
	slots = kvm_dup_memslots(__kvm_memslots(kvm, as_id), change);
	if (!slots) {
		mutex_unlock(&kvm->slots_arch_lock);
		return -ENOMEM;
	}

	if (change == KVM_MR_DELETE || change == KVM_MR_MOVE) {
		/*
		 * Note, the INVALID flag needs to be in the appropriate entry
		 * in the freshly allocated memslots, not in @old or @new.
		 */
		slot = id_to_memslot(slots, old->id);
		slot->flags |= KVM_MEMSLOT_INVALID;

		/*
		 * We can re-use the memory from the old memslots.
		 * It will be overwritten with a copy of the new memslots
		 * after reacquiring the slots_arch_lock below.
		 */
		slots = install_new_memslots(kvm, as_id, slots); // 主要是更新 generation 的，不清楚这个变量是干嘛的

		/* From this point no new shadow pages pointing to a deleted,
		 * or moved, memslot will be created.
		 *
		 * validation of sp->gfn happens in:
		 *	- gfn_to_hva (kvm_read_guest, gfn_to_pfn)
		 *	- kvm_is_visible_gfn (mmu_check_root)
		 */
		kvm_arch_flush_shadow_memslot(kvm, slot);

		/* Released in install_new_memslots. */
		mutex_lock(&kvm->slots_arch_lock);

		/*
		 * The arch-specific fields of the memslots could have changed
		 * between releasing the slots_arch_lock in
		 * install_new_memslots and here, so get a fresh copy of the
		 * slots.
		 */
		kvm_copy_memslots(slots, __kvm_memslots(kvm, as_id));
	}

	r = kvm_arch_prepare_memory_region(kvm, new, mem, change); // 这里主要是设置 rmap 和 lpage_info 的
	if (r)
		goto out_slots;

	update_memslots(slots, new, change); // 将新建的 memslot 插入 memslots 中，它的作用注释已经很清楚了
	slots = install_new_memslots(kvm, as_id, slots);

	kvm_arch_commit_memory_region(kvm, mem, old, new, change); // 提交内存布局（向谁提交？）

	kvfree(slots);
	return 0;

    ...
}
```

修改内存布局，并重新排列。重新排列是按照 `slots->memslots` 中每个 gfn 从大到小排列的，这样能够根据 gfn 快速的通过二分查找找到对应的 slot，

```c
/*
 *
 *  - When deleting a memslot, the deleted memslot simply needs to be moved to
 *    the end of the array.
 *
 *  - When creating a memslot, the algorithm "inserts" the new memslot at the
 *    end of the array and then it forward to its correct location.
 *
 *  - When moving a memslot, the algorithm first moves the updated memslot
 *    backward to handle the scenario where the memslot's GFN was changed to a
 *    lower value.  update_memslots() then falls through and runs the same flow
 *    as creating a memslot to move the memslot forward to handle the scenario
 *    where its GFN was changed to a higher value.
 */
static void update_memslots(struct kvm_memslots *slots,
			    struct kvm_memory_slot *memslot,
			    enum kvm_mr_change change)
{
	int i;

	if (change == KVM_MR_DELETE) {
		kvm_memslot_delete(slots, memslot);
	} else {
		if (change == KVM_MR_CREATE)
			i = kvm_memslot_insert_back(slots);
		else
			i = kvm_memslot_move_backward(slots, memslot);
		i = kvm_memslot_move_forward(slots, memslot, i);

		/*
		 * Copy the memslot to its new position in memslots and update
		 * its index accordingly.
		 */
		slots->memslots[i] = *memslot;
		slots->id_to_index[memslot->id] = i;
	}
}
```

```c
void kvm_arch_commit_memory_region(struct kvm *kvm,
				const struct kvm_userspace_memory_region *mem,
				struct kvm_memory_slot *old,
				const struct kvm_memory_slot *new,
				enum kvm_mr_change change)
{
	if (!kvm->arch.n_requested_mmu_pages)
        // 使计算出来的 nr_mmu_pages 生效，即将其写入 kvm->arch.n_max_mmu_pages 中
		kvm_mmu_change_mmu_pages(kvm,
                // 计算当前虚拟机所需要的页数 nr_mmu_pages，即将每个 kvm_memslots 的每个 kvm_memory_slot
                // 所需的 npages 加起来
                // 所需页数最小值是 64
				kvm_mmu_calculate_default_mmu_pages(kvm));

    // 主要是设置 dirty bitmap
	kvm_mmu_slot_apply_flags(kvm, old, new, change);

	/* Free the arrays associated with the old memslot. */
	if (change == KVM_MR_MOVE)
		kvm_arch_free_memslot(kvm, old);
}
```

这部分源码看完了，但还是有个问题，不知道怎么调试 KVM，所以虚拟机的物理地址的设置只能通过在 QEMU 的 `ioctl(KVM_SET_USER_MEMORY_REGION)` 来间接的看会设置哪些 mr。

#### EPT 页表的构建

没有虚拟机的情况下，页表是通过处理 page fault 来构建的，ept 页表也是这样构建的，下面就来分析一下这个过程。

在 [CPU 虚拟化](https://github.com/UtopianFuture/UtopianFuture.github.io/blob/master/virtualization/CPU-Virtualization.md)部分分析过虚拟机的运行过程，我们知道在 `vcpu_enter_guest` 会完成 vm entry 和 vm exit，退回到 root 态会调用架构相关的 `handle_exit`，

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

		...
	}

	return r;
}
```

```c
/*
 * Returns 1 to let vcpu_run() continue the guest execution loop without
 * exiting to the userspace.  Otherwise, the value will be returned to the
 * userspace.
 */
static int vcpu_enter_guest(struct kvm_vcpu *vcpu)
{
	int r;
	bool req_int_win =
		dm_request_for_irq_injection(vcpu) &&
		kvm_cpu_accept_dm_intr(vcpu);
	fastpath_t exit_fastpath;

	bool req_immediate_exit = false;

	... // vm entry 前的各种中断、异常注入

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

	static_call(kvm_x86_handle_exit_irqoff)(vcpu);

	...

	r = static_call(kvm_x86_handle_exit)(vcpu, exit_fastpath); // intel 对应 vmx_handle_exit

    ...

	return r;
}
```

##### 关键函数 handle_ept_violation

EPT 异常会调用 `handle_ept_violation`，我们看看是怎么处理的，

```c
static int handle_ept_violation(struct kvm_vcpu *vcpu)
{
	unsigned long exit_qualification;
	gpa_t gpa;
	u64 error_code;

	exit_qualification = vmx_get_exit_qual(vcpu); // vmcs_readl(EXIT_QUALIFICATION) 读取退出原因

	/*
	 * EPT violation happened while executing iret from NMI,
	 * "blocked by NMI" bit has to be set before next VM entry.
	 * There are errata that may cause this bit to not be set:
	 * AAK134, BY25.
	 */
	if (!(to_vmx(vcpu)->idt_vectoring_info & VECTORING_INFO_VALID_MASK) &&
			enable_vnmi &&
			(exit_qualification & INTR_INFO_UNBLOCK_NMI))
		vmcs_set_bits(GUEST_INTERRUPTIBILITY_INFO, GUEST_INTR_STATE_NMI);

	gpa = vmcs_read64(GUEST_PHYSICAL_ADDRESS); // 发生 EPT 异常的地址
	trace_kvm_page_fault(gpa, exit_qualification);

	/* Is it a read fault? */
	error_code = (exit_qualification & EPT_VIOLATION_ACC_READ)
		     ? PFERR_USER_MASK : 0;
	/* Is it a write fault? */
	error_code |= (exit_qualification & EPT_VIOLATION_ACC_WRITE)
		      ? PFERR_WRITE_MASK : 0;
	/* Is it a fetch fault? */
	error_code |= (exit_qualification & EPT_VIOLATION_ACC_INSTR)
		      ? PFERR_FETCH_MASK : 0;
	/* ept page table entry is present? */
	error_code |= (exit_qualification &
		       (EPT_VIOLATION_READABLE | EPT_VIOLATION_WRITABLE |
			EPT_VIOLATION_EXECUTABLE))
		      ? PFERR_PRESENT_MASK : 0;

	error_code |= (exit_qualification & EPT_VIOLATION_GVA_TRANSLATED) != 0 ?
	       PFERR_GUEST_FINAL_MASK : PFERR_GUEST_PAGE_MASK;

	vcpu->arch.exit_qualification = exit_qualification;

	/*
	 * Check that the GPA doesn't exceed physical memory limits, as that is
	 * a guest page fault.  We have to emulate the instruction here, because
	 * if the illegal address is that of a paging structure, then
	 * EPT_VIOLATION_ACC_WRITE bit is set.  Alternatively, if supported we
	 * would also use advanced VM-exit information for EPT violations to
	 * reconstruct the page fault error code.
	 */
	if (unlikely(allow_smaller_maxphyaddr && kvm_vcpu_is_illegal_gpa(vcpu, gpa)))
		return kvm_emulate_instruction(vcpu, 0);

	return kvm_mmu_page_fault(vcpu, gpa, error_code, NULL, 0);
}
```

##### 关键函数 direct_page_fault

`kvm_mmu_page_fault` -> `kvm_mmu_do_page_fault` -> `kvm_tdp_page_fault` -> `direct_page_fault`

这个函数需要处理很多种情况，但很多都没有搞懂。

```c
static int direct_page_fault(struct kvm_vcpu *vcpu, gpa_t gpa, u32 error_code,
			     bool prefault, int max_level, bool is_tdp)
{
	bool is_tdp_mmu_fault = is_tdp_mmu(vcpu->arch.mmu);
	bool write = error_code & PFERR_WRITE_MASK;
	bool map_writable;

	gfn_t gfn = gpa >> PAGE_SHIFT;
	unsigned long mmu_seq;
	kvm_pfn_t pfn;
	hva_t hva;
	int r;

	if (page_fault_handle_page_track(vcpu, error_code, gfn))
		return RET_PF_EMULATE;

    // 何为 fast?
    // 只有当 EPT 页表存在且是由写保护产生的 EPT 异常才会进行快速处理
    // 这个也不懂，需要进一步分析
	r = fast_page_fault(vcpu, gpa, error_code);
	if (r != RET_PF_INVALID)
		return r;

    // 保证 3 个 cache （前面有提到每个 VCPU 都有 3 个 cache）有足够多的空间
    // 空间不足的话需要分配，这块还不懂
	r = mmu_topup_memory_caches(vcpu, false);
	if (r)
		return r;

	mmu_seq = vcpu->kvm->mmu_notifier_seq;
	smp_rmb();

	if (kvm_faultin_pfn(vcpu, prefault, gfn, gpa, &pfn, &hva, // 会调用 __gfn_to_pfn_memslot
			 write, &map_writable, &r))
		return r;

	if (handle_abnormal_pfn(vcpu, is_tdp ? 0 : gpa, gfn, pfn, ACC_ALL, &r))
		return r;

	...

	if (!is_noslot_pfn(pfn) && mmu_notifier_retry_hva(vcpu->kvm, mmu_seq, hva))
		goto out_unlock;
	r = make_mmu_pages_available(vcpu);
	if (r)
		goto out_unlock;

	if (is_tdp_mmu_fault)
		r = kvm_tdp_mmu_map(vcpu, gpa, error_code, map_writable, max_level,
				    pfn, prefault);
	else
		r = __direct_map(vcpu, gpa, error_code, map_writable, max_level, pfn, // 建立 gpa 到 hpa 的映射
				 prefault, is_tdp);

    ...

	return r;
}
```

这里还有一个地方值得注意一下，在 `kvm_faultin_pfn` 中会调用 `__gfn_to_pfn_memslot`，我不清楚 `kvm_faultin_pfn` 是干什么用的，但是 gfn 到 pfn 的转换需要分析一下，

```c
// 这个 slot 是在 fast_page_fault 中通过 kvm_vcpu_gfn_to_memslot 函数根据 gfn 在 kvm_memslots 中找到的
kvm_pfn_t __gfn_to_pfn_memslot(struct kvm_memory_slot *slot, gfn_t gfn,
			       bool atomic, bool *async, bool write_fault,
			       bool *writable, hva_t *hva)
{
	unsigned long addr = __gfn_to_hva_many(slot, gfn, NULL, write_fault); // 转化为 hva？

	...

	return hva_to_pfn(addr, atomic, async, write_fault,
			  writable);
}
```

其实转换很简单，`__gfn_to_hva_many` -> `__gfn_to_hva_memslot`

```c
static inline unsigned long
__gfn_to_hva_memslot(const struct kvm_memory_slot *slot, gfn_t gfn)
{
	unsigned long offset = gfn - slot->base_gfn;
	offset = array_index_nospec(offset, slot->npages);
	return slot->userspace_addr + offset * PAGE_SIZE; // 前面说过，userspace_addr 表示 qemu 中的地址，即 hva
}
```

然后将 hva 转换成 hpa，

```c
static kvm_pfn_t hva_to_pfn(unsigned long addr, bool atomic, bool *async,
			bool write_fault, bool *writable)
{
	struct vm_area_struct *vma;
	kvm_pfn_t pfn = 0;
	int npages, r;

    // 看着很熟悉，但实现起来比我想象的复杂
	if (hva_to_pfn_fast(addr, write_fault, writable, &pfn))
		return pfn;

    // 快慢路径，之后有需要再分析
	npages = hva_to_pfn_slow(addr, async, write_fault, writable, &pfn);
	if (npages == 1)
		return pfn;

	...

retry:
	vma = vma_lookup(current->mm, addr); // 哈哈，这个！传统技能了

	if (vma == NULL) // 地址错误
		pfn = KVM_PFN_ERR_FAULT;
	else if (vma->vm_flags & (VM_IO | VM_PFNMAP)) {
		r = hva_to_pfn_remapped(vma, addr, async, write_fault, writable, &pfn);
		if (r == -EAGAIN)
			goto retry;
		if (r < 0)
			pfn = KVM_PFN_ERR_FAULT;
	} else {
		if (async && vma_is_valid(vma, write_fault))
			*async = true;
		pfn = KVM_PFN_ERR_FAULT;
	}
exit:
	mmap_read_unlock(current->mm);
	return pfn;
}
```

到这里就找到 gfn 对应的 pfn 了，可以填入到 EPT 的表项中了。

##### 关键汉斯 __direct_map

这个函数主要是构建 EPT 页表的，

```c
static int __direct_map(struct kvm_vcpu *vcpu, gpa_t gpa, u32 error_code,
			int map_writable, int max_level, kvm_pfn_t pfn,
			bool prefault, bool is_tdp)
{
	bool nx_huge_page_workaround_enabled = is_nx_huge_page_enabled();
	bool write = error_code & PFERR_WRITE_MASK;
	bool exec = error_code & PFERR_FETCH_MASK;
	bool huge_page_disallowed = exec && nx_huge_page_workaround_enabled;
	struct kvm_shadow_walk_iterator it;
	struct kvm_mmu_page *sp;
	int level, req_level, ret;
	gfn_t gfn = gpa >> PAGE_SHIFT;
	gfn_t base_gfn = gfn;

    // 应该是检查是否是 4k 页，并不是， PG_LEVEL_4K == 1
    // 这个表示页表的级数
	level = kvm_mmu_hugepage_adjust(vcpu, gfn, max_level, &pfn,
					huge_page_disallowed, &req_level);

	trace_kvm_mmu_spte_requested(gpa, level, pfn);
	for_each_shadow_entry(vcpu, gpa, it) { // 这里是 4 级页表的构建，比较复杂
		/*
		 * We cannot overwrite existing page tables with an NX
		 * large page, as the leaf could be executable.
		 */
		if (nx_huge_page_workaround_enabled)
			disallowed_hugepage_adjust(*it.sptep, gfn, it.level,
						   &pfn, &level);

		base_gfn = gfn & ~(KVM_PAGES_PER_HPAGE(it.level) - 1);
		if (it.level == level)
			break;

		drop_large_spte(vcpu, it.sptep); // it.sptep 表示该级页表的基地址
		if (is_shadow_present_pte(*it.sptep)) // 判断当前的页表是否存在
			continue; // 存在就不需要创建页表了，后面就是创建页表的过程

		sp = kvm_mmu_get_page(vcpu, base_gfn, it.addr, // 分配一个 mmu 内存页
				      it.level - 1, true, ACC_ALL); // 并插入到 vcpu->kvm->arch.mmu_page_hash 中

        // 将分配的内存页跟当前目录项连接起来
        // 即让当前的 iterator->sptep 指向 sp
        // 这样一级一级的创建，就能构建一个完整的 4 级 EPT 页表
		link_shadow_page(vcpu, it.sptep, sp);
		if (is_tdp && huge_page_disallowed &&
		    req_level >= it.level)
			account_huge_nx_page(vcpu->kvm, sp);
	}

    // 现在是最后一级页表，其页表项要指向 pfn
    // 而 pfn 在前面通过 __gfn_to_hva_memslot 和 hva_to_pfn 已经确定了
	ret = mmu_set_spte(vcpu, it.sptep, ACC_ALL,
			   write, level, base_gfn, pfn, prefault,
			   map_writable);
	if (ret == RET_PF_SPURIOUS)
		return ret;

	direct_pte_prefetch(vcpu, it.sptep);
	++vcpu->stat.pf_fixed;
	return ret;
}
```

接下来我们看看是怎样构建 EPT 的 4 级页表，之前只是知道大概的流程。`for_each_shadow_entry` 是一个宏，负责遍历，

```c
#define for_each_shadow_entry(_vcpu, _addr, _walker)            \
	for (shadow_walk_init(&(_walker), _vcpu, _addr);	\
	     shadow_walk_okay(&(_walker));			\
	     shadow_walk_next(&(_walker)))
```

`shadow_walk_init` -> `shadow_walk_init_using_root`，从代码中可以看出，其主要是初始化 `kvm_shadow_walk_iterator *iterator`，

```c
static void shadow_walk_init_using_root(struct kvm_shadow_walk_iterator *iterator,
					struct kvm_vcpu *vcpu, hpa_t root,
					u64 addr)
{
	iterator->addr = addr; // gpa
	iterator->shadow_addr = root; // 页表的基地址，遍历每一级页表都需要更新，由 shadow_walk_next 更新
	iterator->level = vcpu->arch.mmu->shadow_root_level; // EPT 的级数，为 4

	if (iterator->level == PT64_ROOT_4LEVEL &&
	    vcpu->arch.mmu->root_level < PT64_ROOT_4LEVEL &&
	    !vcpu->arch.mmu->direct_map)
		--iterator->level;

	if (iterator->level == PT32E_ROOT_LEVEL) {
		/*
		 * prev_root is currently only used for 64-bit hosts. So only
		 * the active root_hpa is valid here.
		 */
		BUG_ON(root != vcpu->arch.mmu->root_hpa);

		iterator->shadow_addr
			= vcpu->arch.mmu->pae_root[(addr >> 30) & 3];
		iterator->shadow_addr &= PT64_BASE_ADDR_MASK;
		--iterator->level;
		if (!iterator->shadow_addr)
			iterator->level = 0;
	}
}
```

for 循环嘛，`shadow_walk_okay` 表示结束条件，

```c
static bool shadow_walk_okay(struct kvm_shadow_walk_iterator *iterator)
{
	if (iterator->level < PG_LEVEL_4K) // 小于 1，遍历结束
		return false;

    // index 表示该地址在相应页表中的 index
    // iterator->addr 表示 gpa，不要搞混了
    // 这个过程其实我们都知道，48 位的 gpa，每级页表对应 9 位，4*9 = 36，最后 12 位是页内偏移
	iterator->index = SHADOW_PT_INDEX(iterator->addr, iterator->level);
	iterator->sptep	= ((u64 *)__va(iterator->shadow_addr)) + iterator->index; // 当前页表项的指针
	return true;
}
```

最后是 `__shadow_walk_next`，

```c
static void __shadow_walk_next(struct kvm_shadow_walk_iterator *iterator,
			       u64 spte)
{
	if (is_last_spte(spte, iterator->level)) {
		iterator->level = 0;
		return;
	}

	iterator->shadow_addr = spte & PT64_BASE_ADDR_MASK; // 我去，搞的这么绕干啥，spte 就是 okey 里的 sptep
	--iterator->level; // 层数减 1
}
```

##### 关键函数 mmu_set_spte

这个函数是真正填 EPT 表项的，所有的 4 级页表的填充都是这个函数完成，

```c
static int mmu_set_spte(struct kvm_vcpu *vcpu, u64 *sptep,
			unsigned int pte_access, bool write_fault, int level,
			gfn_t gfn, kvm_pfn_t pfn, bool speculative,
			bool host_writable)
{
	int was_rmapped = 0;
	int rmap_count;
	int set_spte_ret;
	int ret = RET_PF_FIXED;
	bool flush = false;

	pgprintk("%s: spte %llx write_fault %d gfn %llx\n", __func__,
		 *sptep, write_fault, gfn);

	if (unlikely(is_noslot_pfn(pfn))) {
		mark_mmio_spte(vcpu, sptep, gfn, pte_access);
		return RET_PF_EMULATE;
	}

    // 为何这里还要判断一次页表是否存在？
	if (is_shadow_present_pte(*sptep)) {

        ...

	}

	set_spte_ret = set_spte(vcpu, sptep, pte_access, level, gfn, pfn,
				speculative, true, host_writable); // 在这里填入

    ...

	return ret;
}
```

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

### 说明

KVM 内存虚拟化那节因为没有用 gdb 调试，所以就只是将源码放上去，粗略的分析一下，对整个过程的细节把控还是不够的，之后有需要还需进一步分析。

### Reference

[1] http://blog.vmsplice.net/2016/01/qemu-internals-how-guest-physical-ram.html

[2] https://blog.csdn.net/xelatex_kvm/article/details/17685123

[3] https://android.googlesource.com/kernel/msm/+/android-msm-flo-3.4-kitkat-mr1/Documentation/virtual/kvm/mmu.txt

[4] https://oenhan.com/qemu-memory-struct
