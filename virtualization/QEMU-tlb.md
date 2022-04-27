## TLB in QEMU

在做 HAMT 优化时经常遇到 TLB 没有 flush 导致程序跑崩了的问题，这类 bug 很难调，因为程序出错往往是有一个 tlb entry 没有 flush，但是程序出错可以要到几万条指令之后即访问了这个地址才会出错，你要找到是哪个地址出错了相当于大海捞针，这也不是正常的调试方法，而 QEMU 模拟的 TLB 又和硬件不一样，所以这里分析一下 QEMU 是怎样模拟 TLB 的，然后才好根据实际情况在正确的地方 flush。

### 数据结构

老规矩，先看看数据结构。

```c
/*
 * The entire softmmu tlb, for all MMU modes.
 * The meaning of each of the MMU modes is defined in the target code.
 * Since this is placed within CPUNegativeOffsetState, the smallest
 * negative offsets are at the end of the struct.
 */

typedef struct CPUTLB {
    CPUTLBCommon c;
    CPUTLBDesc d[NB_MMU_MODES];
    CPUTLBDescFast f[NB_MMU_MODES];
} CPUTLB;
```

好吧，这个注释我没看懂，`CPUNegativeOffsetState` 第一次见。根据注释，`CPUTLB` 就是整个 softmmu tlb，应该是每个 CPU 对应一个 softmmu，那 MMU  modes 是什么？

> The [Intel architecture](https://www.sciencedirect.com/topics/computer-science/intel-architecture) supports three different modes for the MMU paging, namely, 32-bit, Physical Address Extension (allows a 32-bit CPU to access more than 4 GB of memory), and 64-bit modes.

所以困扰了我很久的 `mmu_idx` 就是 mmu modes，大部分情况应该都是定值。

```c
/*
 * Data elements that are shared between all MMU modes.
 */
typedef struct CPUTLBCommon {
    /* Serialize updates to f.table and d.vtable, and others as noted. */
    QemuSpin lock;
    /*
     * Within dirty, for each bit N, modifications have been made to
     * mmu_idx N since the last time that mmu_idx was flushed.
     * Protected by tlb_c.lock.
     */
    uint16_t dirty;
    /*
     * Statistics.  These are not lock protected, but are read and
     * written atomically.  This allows the monitor to print a snapshot
     * of the stats without interfering with the cpu.
     */
    size_t full_flush_count;
    size_t part_flush_count;
    size_t elide_flush_count;
} CPUTLBCommon;
```

`CPUTLBCommon` 只是保存一些统计信息，真正的映射信息保存在 `CPUTLBDesc`。

```c
/*
 * Data elements that are per MMU mode, minus the bits accessed by
 * the TCG fast path.
 */
typedef struct CPUTLBDesc {
    /*
     * Describe a region covering all of the large pages allocated
     * into the tlb.  When any page within this region is flushed,
     * we must flush the entire tlb.  The region is matched if
     * (addr & large_page_mask) == large_page_addr.
     */
    target_ulong large_page_addr;
    target_ulong large_page_mask;
    /* host time (in ns) at the beginning of the time window */
    int64_t window_begin_ns;
    /* maximum number of entries observed in the window */
    size_t window_max_entries;
    size_t n_used_entries;
    /* The next index to use in the tlb victim table.  */
    size_t vindex;
    /* The tlb victim table, in two parts.  */
    CPUTLBEntry vtable[CPU_VTLB_SIZE]; // 为什么才 8 个 TLB entry
    CPUIOTLBEntry viotlb[CPU_VTLB_SIZE];
    /* The iotlb.  */
    CPUIOTLBEntry *iotlb;
} CPUTLBDesc;
```

```c
typedef struct CPUTLBDescFast {
    /* Contains (n_entries - 1) << CPU_TLB_ENTRY_BITS */
    uintptr_t mask;
    /* The array of tlb entries itself. */
    CPUTLBEntry *table; // 貌似这才是真正的 tlb entry
} CPUTLBDescFast QEMU_ALIGNED(2 * sizeof(void *));
```

`CPUTLBDesc` 和 `CPUTLBDescFast` 有什么区别？

```c
typedef struct CPUTLBEntry {
    /* bit TARGET_LONG_BITS to TARGET_PAGE_BITS : virtual address
       bit TARGET_PAGE_BITS-1..4  : Nonzero for accesses that should not
                                    go directly to ram.
       bit 3                      : indicates that the entry is invalid
       bit 2..0                   : zero
    */
    union {
        struct {
            target_ulong addr_read; // 这 3 个应该都是虚拟地址，一样的
            target_ulong addr_write;
            target_ulong addr_code;
            /* Addend to virtual address to get host address.  IO accesses
               use the corresponding iotlb value.  */
            uintptr_t addend; // 物理地址
        };
        /* padding to get a power of two size */
        uint8_t dummy[1 << CPU_TLB_ENTRY_BITS];
    };
} CPUTLBEntry;
```

```c
typedef struct CPUIOTLBEntry {
    /*
     * @addr contains:
     *  - in the lower TARGET_PAGE_BITS, a physical section number
     *  - with the lower TARGET_PAGE_BITS masked off, an offset which
     *    must be added to the virtual address to obtain:
     *     + the ram_addr_t of the target RAM (if the physical section
     *       number is PHYS_SECTION_NOTDIRTY or PHYS_SECTION_ROM)
     *     + the offset within the target MemoryRegion (otherwise)
     */
    hwaddr addr;
    MemTxAttrs attrs;
} CPUIOTLBEntry;
```

`CPUIOTLBEntry` 似乎和 `CPUTLBEntry` 不太一样，怎么没有对应的虚拟地址？

### 主要的刷新函数

接下来分析主要的刷新函数，以及在什么情况下需要刷新。

首先从代码上来看有如下 3 个函数通过 `memset` 实际改变了 tlb entry，

`env_tlb(env)->f[mmu_idx].table` 应该是 tlb entry 的链表头。所以这里对应到硬件 tlb 上要刷新整个 tlb。

```c
static inline void tlb_table_flush_by_mmuidx(CPUArchState *env, int mmu_idx)
{
    tlb_mmu_resize_locked(env, mmu_idx);
    memset(env_tlb(env)->f[mmu_idx].table, -1, sizeof_tlb(env, mmu_idx));
    env_tlb(env)->d[mmu_idx].n_used_entries = 0;
}
```

刷新 mmuidx 对应的整个 tlb，注意这里页调用了 `tlb_flush_one_mmuidx_locked` 刷新 `CPUTLBDescFast` 中的 tlb entry 链表头。这里也是，要刷新整个 tlb。

```c
static void tlb_flush_one_mmuidx_locked(CPUArchState *env, int mmu_idx)
{
    tlb_table_flush_by_mmuidx(env, mmu_idx);
    env_tlb(env)->d[mmu_idx].large_page_addr = -1;
    env_tlb(env)->d[mmu_idx].large_page_mask = -1;
    env_tlb(env)->d[mmu_idx].vindex = 0;
    memset(env_tlb(env)->d[mmu_idx].vtable, -1,
           sizeof(env_tlb(env)->d[0].vtable));
}
```

刷新一个 entry。这里对应硬件 tlb 就只要刷新 page 对应的 tlb entry，这样性能提升才能达到最大。

```c
static inline bool tlb_flush_entry_locked(CPUTLBEntry *tlb_entry,
                                          target_ulong page)
{
    if (tlb_hit_page_anyprot(tlb_entry, page)) {
        memset(tlb_entry, -1, sizeof(*tlb_entry));
        return true;
    }
    return false;
}
```

那么接下来要看看在哪些情况下需要刷新 tlb，这是问题的关键，实现 HAMT 的过程中就是有些该刷新的地方没有刷新。但是总不能加上断点一行行的跑吧，那也太慢了。

先从上述 3 个函数为入手点，看哪里调用了它们。
