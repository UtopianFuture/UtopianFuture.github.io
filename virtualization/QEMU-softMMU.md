## softMMU in QEMU

之前只是直到 softMMU 是 QEMU 中用来存储 GVA -> HVA 映射关系的一个优化，但其中细节没有了解。现在 HAMT 项目遇到一个问题就是无法确定哪些 miss 是正常会发生的，所以就不能进一步分析出错的原因是什么。所以这里进一步分析 softMMU，看看其中涉及到哪些有意思的东西。

### 执行流程

softmmu 只有 tcg 才需要，实现基本思路是:

- 所有的访存指令前使用软件进行地址翻译，如果命中，那么获取 GPA 进行访存
- 如果不命中，慢路径，也就是 store_helper

TLB 在 code 中的经典路径:

```
 * Since the addressing of x86 architecture is complex, we
 * remain the original processing that exacts the final
 * x86vaddr from IR1_OPND(x86 opnd).
 * It is usually done by functions load_ireg_from_ir1_mem()
 * and store_ireg_from_ir1_mem().
```

`gen_ldst_softmmu_helper` // 这个是翻译 guestos 是所有的访存指令的翻译

​			-> `__gen_ldst_softmmu_helper_native` // 其中的注释非常清晰，首先查询 TLB，如果不成功，进入慢路径

​						-> `tr_gen_lookup_qemu_tlb` // TLB 比较查询，之后分析

​						->`tr_gen_ldst_slow_path` // 无奈，只能跳转到 slow path 去

​									-> `td_rcd_softmmu_slow_path` // 这个具体是怎么做的？需要分析

`tr_translate_tb` // 翻译函数的入口

​			->`tr_ir2_generate`

​						->`tr_gen_tb_start`

​						->`tr_gen_softmmu_slow_path` // slow path 的代码在每一个 tb 只会生成一次

​									-> `__tr_gen_softmmu_sp_rcd`

​												-> `helper_ret_stb_mmu` // 跳转的入口通过 `helper_ret_stb_mmu` 实现, 当前在 accel/tcg/cputlb.c 中

​															-> `store_helper`

​																		-> `io_writex`

​																					-> `memory_region_dispatch_write`

​						-> `tr_gen_tb_end`

进行 TLB 填充的经典路径：

`store_helper`

​			-> `tlb_fill`

​						-> `x86_cpu_tlb_fill`

​									-> `handle_mmu_fault` // 利用 x86 的页面进行 page walk

​												-> `tlb_set_page_with_attrs` // 设置页面

指令执行的过程，获取指令同样可以触发 TLB refill 操作:

```txt
#0  tlb_set_page_with_attrs (cpu=0x7ffff7ffd9e8 <_rtld_global+2440>, vaddr=32767, paddr=140737354006919, attrs=..., prot=0, mmu_idx=-1, size=4096) at src/tcg/cputlb.c:682
#1  0x0000555555629469 in handle_mmu_fault (cs=0x55555596ea80 <__x86_cpu>, addr=4294967280, size=0, is_write1=2, mmu_idx=2) at src/i386/excp_helper.c:637
#2  0x0000555555629640 in x86_cpu_tlb_fill (cs=0x55555596ea80 <__x86_cpu>, addr=4294967280, size=0, access_type=MMU_INST_FETCH, mmu_idx=2, probe=false, retaddr=0) at src/i386/excp_helper.c:685
#3  0x00005555555d94d8 in tlb_fill (cpu=0x55555596ea80 <__x86_cpu>, addr=4294967280, size=0, access_type=MMU_INST_FETCH, mmu_idx=2, retaddr=0) at src/tcg/cputlb.c:895
#4  0x00005555555d9d88 in get_page_addr_code_hostp (env=0x5555559771f0 <__x86_cpu+34672>, addr=4294967280, hostp=0x0) at src/tcg/cputlb.c:1075
#5  0x00005555555d9f0e in get_page_addr_code (env=0x5555559771f0 <__x86_cpu+34672>, addr=4294967280) at src/tcg/cputlb.c:1106
#6  0x00005555555cd59e in tb_htable_lookup (cpu=0x55555596ea80 <__x86_cpu>, pc=4294967280, cs_base=4294901760, flags=64, cflags=4278190080) at src/tcg/cpu-exec.c:675
#7  0x00005555555cbaab in tb_lookup__cpu_state (cpu=0x55555596ea80 <__x86_cpu>, pc=0x7fffffffd4b8, cs_base=0x7fffffffd4b4, flags=0x7fffffffd4bc, cflags=4278190080) at src/tcg/../../include/exec/tb-lookup.h:44
#8  0x00005555555cc5a1 in tb_find (cpu=0x55555596ea80 <__x86_cpu>, last_tb=0x0, tb_exit=0, cflags=0) at src/tcg/cpu-exec.c:285
#9  0x00005555555cd0a6 in cpu_exec (cpu=0x55555596ea80 <__x86_cpu>) at src/tcg/cpu-exec.c:559
#10 0x000055555561afb8 in tcg_cpu_exec (cpu=0x55555596ea80 <__x86_cpu>) at src/qemu/cpus.c:122
#11 0x000055555561b22c in qemu_tcg_rr_cpu_thread_fn (arg=0x55555596ea80 <__x86_cpu>) at src/qemu/cpus.c:235
```

接下来进一步分析其中的关键函数，

#### 关键函数 store_helper

这个函数看起来很长，其实大部分代码是处理临界情况的，暂时不用理解，所以关键还在于弄清楚 softTLB 的结构，

```c
static inline void QEMU_ALWAYS_INLINE store_helper(CPUArchState *env,
                                                   target_ulong addr,
                                                   uint64_t val, TCGMemOpIdx oi,
                                                   uintptr_t retaddr,
                                                   MemOp op) {
  uintptr_t mmu_idx = get_mmuidx(oi); // mmu_idx 是当前 mmu 所处的模式
  uintptr_t index = tlb_index(env, mmu_idx, addr); // 这个很好理解
  CPUTLBEntry *entry = tlb_entry(env, mmu_idx, addr);
  target_ulong tlb_addr = tlb_addr_write(entry);
  const size_t tlb_off = offsetof(CPUTLBEntry, addr_write);
  unsigned a_bits = get_alignment_bits(get_memop(oi));
  void *haddr;
  size_t size = memop_size(op);

  ...

  /* If the TLB entry is for a different page, reload and try again.  */
  if (!tlb_hit(tlb_addr, addr)) {
    if (!victim_tlb_hit(env, mmu_idx, index, tlb_off,
                        addr & TARGET_PAGE_MASK)) {
      tlb_fill(env_cpu(env), addr, size, MMU_DATA_STORE, mmu_idx, retaddr);
      index = tlb_index(env, mmu_idx, addr);
      entry = tlb_entry(env, mmu_idx, addr);
    }
    tlb_addr = tlb_addr_write(entry) & ~TLB_INVALID_MASK;
  }

  /* Handle anything that isn't just a straight memory access.  */
  if (unlikely(tlb_addr & ~TARGET_PAGE_MASK)) {
    CPUIOTLBEntry *iotlbentry;
    bool need_swap;

    /* For anything that is unaligned, recurse through byte stores.  */
    if ((addr & (size - 1)) != 0) {
      goto do_unaligned_access;
    }

    iotlbentry = &env_tlb(env)->d[mmu_idx].iotlb[index];

    /* Handle watchpoints.  */
    if (unlikely(tlb_addr & TLB_WATCHPOINT)) {
      /* On watchpoint hit, this will longjmp out.  */
      cpu_check_watchpoint(env_cpu(env), addr, size, iotlbentry->attrs,
                           BP_MEM_WRITE, retaddr);
    }

    need_swap = size > 1 && (tlb_addr & TLB_BSWAP);

    /* Handle I/O access.  */
    if (tlb_addr & TLB_MMIO) {
      io_writex(env, iotlbentry, entry, mmu_idx, val, addr, retaddr,
                op ^ (need_swap * MO_BSWAP));

      return;
    }

    /* Ignore writes to ROM.  */
    if (unlikely(tlb_addr & TLB_DISCARD_WRITE)) {
      return;
    }

    /* Handle clean RAM pages.  */
    if (tlb_addr & TLB_NOTDIRTY) {
      notdirty_write(env_cpu(env), addr, size, iotlbentry, retaddr);
    }

    haddr = (void *)((uintptr_t)addr + entry->addend);

    ...

    store_memop(haddr, val, op);

    return;
  }

  /* Handle slow unaligned access (it spans two pages or IO).  */
  ...

  haddr = (void *)((uintptr_t)addr + entry->addend);
  store_memop(haddr, val, op);

  ...

}
```

#### 关键函数tlb_fill

tcg 的 `tlb_fill` 函数为 `x86_cpu_tlb_fill`。

```c
bool x86_cpu_tlb_fill(CPUState *cs, vaddr addr, int size,
                      MMUAccessType access_type, int mmu_idx,
                      bool probe, uintptr_t retaddr)
{
    X86CPU *cpu = X86_CPU(cs);
    CPUX86State *env = &cpu->env;

	...

    env->retaddr = retaddr;
    if (handle_mmu_fault(cs, addr, size, access_type, mmu_idx)) {
        /* FIXME: On error in get_hphys we have already jumped out.  */
        g_assert(!probe);
        raise_exception_err_ra(env, cs->exception_index,
                               env->error_code, retaddr);
    }
    return true;
#endif
}
```

#### 关键函数handle_mmu_fault

```c
/* return value:
 * -1 = cannot handle fault
 * 0  = nothing more to do
 * 1  = generate PF fault
 */
static int handle_mmu_fault(CPUState *cs, vaddr addr, int size,
                            int is_write1, int mmu_idx)
{
    X86CPU *cpu = X86_CPU(cs);
    CPUX86State *env = &cpu->env;
    uint64_t ptep, pte;
    int32_t a20_mask;
    target_ulong pde_addr, pte_addr;
    int error_code = 0;
    int is_dirty, prot, page_size, is_write, is_user;
    hwaddr paddr;
    uint64_t rsvd_mask = PG_HI_RSVD_MASK;
    uint32_t page_offset;
    target_ulong vaddr;

    is_user = mmu_idx == MMU_USER_IDX;

    is_write = is_write1 & 1;

    a20_mask = x86_get_a20_mask(env);
    if (!(env->cr[0] & CR0_PG_MASK)) { // LATX 居然没有开启分页？不是，而是系统运行一段时间后才会开启
        pte = addr;
        prot = PAGE_READ | PAGE_WRITE | PAGE_EXEC;
        page_size = 4096;
        goto do_mapping;
    }

    if (!(env->efer & MSR_EFER_NXE)) {
        rsvd_mask |= PG_NX_MASK;
    }

    if (env->cr[4] & CR4_PAE_MASK) {

        ...

    } else {
        uint32_t pde; // 遍历页表

        /* page directory entry */
        pde_addr = ((env->cr[3] & ~0xfff) + ((addr >> 20) & 0xffc)) &
            a20_mask;
        pde_addr = get_hphys(cs, pde_addr, MMU_DATA_STORE, NULL);
        pde = x86_ldl_phys(cs, pde_addr);
        if (!(pde & PG_PRESENT_MASK)) {
            goto do_fault;
        }
        ptep = pde | PG_NX_MASK;

        /* if PSE bit is set, then we use a 4MB page */
        if ((pde & PG_PSE_MASK) && (env->cr[4] & CR4_PSE_MASK)) {
            page_size = 4096 * 1024;
            pte_addr = pde_addr;

            /* Bits 20-13 provide bits 39-32 of the address, bit 21 is reserved.
             * Leave bits 20-13 in place for setting accessed/dirty bits below.
             */
            pte = pde | ((pde & 0x1fe000LL) << (32 - 13));
            rsvd_mask = 0x200000;
            goto do_check_protect_pse36;
        }

        if (!(pde & PG_ACCESSED_MASK)) {
            pde |= PG_ACCESSED_MASK;
            x86_stl_phys_notdirty(cs, pde_addr, pde);
        }

        /* page directory entry */
        pte_addr = ((pde & ~0xfff) + ((addr >> 10) & 0xffc)) &
            a20_mask;
        pte_addr = get_hphys(cs, pte_addr, MMU_DATA_STORE, NULL);
        pte = x86_ldl_phys(cs, pte_addr);
        if (!(pte & PG_PRESENT_MASK)) {
            goto do_fault;
        }
        /* combine pde and pte user and rw protections */
        ptep &= pte | PG_NX_MASK;
        page_size = 4096;
        rsvd_mask = 0;
    }

do_check_protect:
    rsvd_mask |= (page_size - 1) & PG_ADDRESS_MASK & ~PG_PSE_PAT_MASK;
do_check_protect_pse36:
    if (pte & rsvd_mask) {
        goto do_fault_rsvd;
    }
    ptep ^= PG_NX_MASK;

    /* can the page can be put in the TLB?  prot will tell us */
    if (is_user && !(ptep & PG_USER_MASK)) {
        goto do_fault_protect;
    }

    prot = 0;
    if (mmu_idx != MMU_KSMAP_IDX || !(ptep & PG_USER_MASK)) {
        prot |= PAGE_READ;
        if ((ptep & PG_RW_MASK) || (!is_user && !(env->cr[0] & CR0_WP_MASK))) {
            prot |= PAGE_WRITE;
        }
    }
    if (!(ptep & PG_NX_MASK) &&
        (mmu_idx == MMU_USER_IDX ||
         !((env->cr[4] & CR4_SMEP_MASK) && (ptep & PG_USER_MASK)))) {
        prot |= PAGE_EXEC;
    }
    if ((env->cr[4] & CR4_PKE_MASK) && (env->hflags & HF_LMA_MASK) &&
        (ptep & PG_USER_MASK) && env->pkru) {
        uint32_t pk = (pte & PG_PKRU_MASK) >> PG_PKRU_BIT;
        uint32_t pkru_ad = (env->pkru >> pk * 2) & 1;
        uint32_t pkru_wd = (env->pkru >> pk * 2) & 2;
        uint32_t pkru_prot = PAGE_READ | PAGE_WRITE | PAGE_EXEC;

        if (pkru_ad) {
            pkru_prot &= ~(PAGE_READ | PAGE_WRITE);
        } else if (pkru_wd && (is_user || env->cr[0] & CR0_WP_MASK)) {
            pkru_prot &= ~PAGE_WRITE;
        }

        prot &= pkru_prot;
        if ((pkru_prot & (1 << is_write1)) == 0) {
            assert(is_write1 != 2);
            error_code |= PG_ERROR_PK_MASK;
            goto do_fault_protect;
        }
    }

    if ((prot & (1 << is_write1)) == 0) {
        goto do_fault_protect;
    }

    /* yes, it can! */
    is_dirty = is_write && !(pte & PG_DIRTY_MASK);
    if (!(pte & PG_ACCESSED_MASK) || is_dirty) {
        pte |= PG_ACCESSED_MASK;
        if (is_dirty) {
            pte |= PG_DIRTY_MASK;
        }
        x86_stl_phys_notdirty(cs, pte_addr, pte);
    }

    if (!(pte & PG_DIRTY_MASK)) {
        /* only set write access if already dirty... otherwise wait
           for dirty access */
        assert(!is_write);
        prot &= ~PAGE_WRITE;
    }

 do_mapping:
    pte = pte & a20_mask;

    /* align to page_size */
    pte &= PG_ADDRESS_MASK & ~(page_size - 1);
    page_offset = addr & (page_size - 1);
    paddr = get_hphys(cs, pte + page_offset, is_write1, &prot); //

    /* Even if 4MB pages, we map only one 4KB page in the cache to
       avoid filling it too fast */
    vaddr = addr & TARGET_PAGE_MASK;
    paddr &= TARGET_PAGE_MASK;

    assert(prot & (1 << is_write1));
    tlb_set_page_with_attrs(cs, vaddr, paddr, cpu_get_mem_attrs(env), // 设置 tlb entry
                            prot, mmu_idx, page_size);
    return 0;

    ... // 各种错误处理
}
```

#### 关键函数tlb_set_page_with_attrs

```c
/* Add a new TLB entry. At most one entry for a given virtual address
 * is permitted. Only a single TARGET_PAGE_SIZE region is mapped, the
 * supplied size is only used by tlb_flush_page.
 *
 * Called from TCG-generated code, which is under an RCU read-side
 * critical section.
 */
void tlb_set_page_with_attrs(CPUState *cpu, target_ulong vaddr, hwaddr paddr,
                             MemTxAttrs attrs, int prot, int mmu_idx,
                             target_ulong size) {
  CPUArchState *env = cpu->env_ptr;
  CPUTLB *tlb = env_tlb(env);
  CPUTLBDesc *desc = &tlb->d[mmu_idx];
  MemoryRegion *mr;
  unsigned int index;
  target_ulong address;
  target_ulong write_address;
  uintptr_t addend;
  CPUTLBEntry *te, tn;
  hwaddr iotlb, xlat, sz, paddr_page;
  target_ulong vaddr_page;
  int asidx = cpu_asidx_from_attrs(cpu, attrs);
  int wp_flags;
  bool is_ram, is_romd;

  assert_cpu_is_self(cpu);

  if (size <= TARGET_PAGE_SIZE) {
    sz = TARGET_PAGE_SIZE;
  } else {
    tlb_add_large_page(env, mmu_idx, vaddr, size);
    sz = size;
  }
  vaddr_page = vaddr & TARGET_PAGE_MASK;
  paddr_page = paddr & TARGET_PAGE_MASK;

  mr = address_space_translate_for_iotlb(cpu, asidx, paddr_page, &xlat, &sz,
                                         attrs, &prot);
  assert(sz >= TARGET_PAGE_SIZE);

  tlb_debug("vaddr=" TARGET_FMT_lx " paddr=0x" TARGET_FMT_plx
            " prot=%x idx=%d\n",
            vaddr, paddr, prot, mmu_idx);

  address = vaddr_page;

  is_ram = memory_region_is_ram(mr);
  // is_romd = memory_region_is_romd(section->mr);
  is_romd = false;

  if (is_ram || is_romd) {
    /* RAM and ROMD both have associated host memory. */
    addend = (uintptr_t)memory_region_get_ram_ptr(mr) + xlat;
  } else {
    /* I/O does not; force the host address to NULL. */
    // [interface 34]
    assert(is_iotlb_mr(mr));
    addend = paddr;
  }

  write_address = address;
  if (is_ram) {
    iotlb = memory_region_get_ram_addr(mr) + xlat;
    /*
     * Computing is_clean is expensive; avoid all that unless
     * the page is actually writable.
     */
    if (prot & PAGE_WRITE) {
      if (mr->readonly) {
        write_address |= TLB_DISCARD_WRITE;
      } else if (cpu_physical_memory_is_clean(iotlb)) {
        write_address |= TLB_NOTDIRTY;
      }
    }
  } else {
    // [interface 34]
    /* I/O or ROMD */
    assert(is_iotlb_mr(mr));
    iotlb = IOTLB_MAGIC;
    /*
     * Writes to romd devices must go through MMIO to enable write.
     * Reads to romd devices go through the ram_ptr found above,
     * but of course reads to I/O must go through MMIO.
     */
    write_address |= TLB_MMIO;
    if (!is_romd) {
      address = write_address;
    }
  }

  wp_flags = cpu_watchpoint_address_matches(cpu, vaddr_page, TARGET_PAGE_SIZE);

  index = tlb_index(env, mmu_idx, vaddr_page);
  te = tlb_entry(env, mmu_idx, vaddr_page);

  /*
   * Hold the TLB lock for the rest of the function. We could acquire/release
   * the lock several times in the function, but it is faster to amortize the
   * acquisition cost by acquiring it just once. Note that this leads to
   * a longer critical section, but this is not a concern since the TLB lock
   * is unlikely to be contended.
   */
  qemu_spin_lock(&tlb->c.lock);

  /* Note that the tlb is no longer clean.  */
  tlb->c.dirty |= 1 << mmu_idx;

  /* Make sure there's no cached translation for the new page.  */
  tlb_flush_vtlb_page_locked(env, mmu_idx, vaddr_page);

  /*
   * Only evict the old entry to the victim tlb if it's for a
   * different page; otherwise just overwrite the stale data.
   */
  if (!tlb_hit_page_anyprot(te, vaddr_page) && !tlb_entry_is_empty(te)) {
    unsigned vidx = desc->vindex++ % CPU_VTLB_SIZE;
    CPUTLBEntry *tv = &desc->vtable[vidx];

    /* Evict the old entry into the victim tlb.  */
    copy_tlb_helper_locked(tv, te);
    desc->viotlb[vidx] = desc->iotlb[index];
    tlb_n_used_entries_dec(env, mmu_idx);
  }

  /* refill the tlb */
  /*
   * At this point iotlb contains a physical section number in the lower
   * TARGET_PAGE_BITS, and either
   *  + the ram_addr_t of the page base of the target RAM (RAM)
   *  + the offset within section->mr of the page base (I/O, ROMD)
   * We subtract the vaddr_page (which is page aligned and thus won't
   * disturb the low bits) to give an offset which can be added to the
   * (non-page-aligned) vaddr of the eventual memory access to get
   * the MemoryRegion offset for the access. Note that the vaddr we
   * subtract here is that of the page base, and not the same as the
   * vaddr we add back in io_readx()/io_writex()/get_page_addr_code().
   */
  desc->iotlb[index].addr = iotlb - vaddr_page;
  desc->iotlb[index].attrs = attrs;

  /* Now calculate the new entry */
  tn.addend = addend - vaddr_page;
  if (prot & PAGE_READ) {
    tn.addr_read = address;
    if (wp_flags & BP_MEM_READ) {
      tn.addr_read |= TLB_WATCHPOINT;
    }
  } else {
    tn.addr_read = -1;
  }

  if (prot & PAGE_EXEC) {
    tn.addr_code = address;
  } else {
    tn.addr_code = -1;
  }

  tn.addr_write = -1;
  if (prot & PAGE_WRITE) {
    tn.addr_write = write_address;
    if (prot & PAGE_WRITE_INV) {
      tn.addr_write |= TLB_INVALID_MASK;
    }
    if (wp_flags & BP_MEM_WRITE) {
      tn.addr_write |= TLB_WATCHPOINT;
    }
  }

  copy_tlb_helper_locked(te, &tn);
  tlb_n_used_entries_inc(env, mmu_idx);
  qemu_spin_unlock(&tlb->c.lock);
}
```



### softTLB

在做 HAMT 优化时经常遇到 TLB 没有 flush 导致程序跑崩了的问题，这类 bug 很难调，因为程序出错往往是有一个 tlb entry 没有 flush，但是程序出错可以要到几万条指令之后即访问了这个地址才会出错，你要找到是哪个地址出错了相当于大海捞针，这也不是正常的调试方法，而 QEMU 模拟的 TLB 又和硬件不一样，所以这里分析一下 QEMU 是怎样模拟 TLB 的，然后才好根据实际情况在正确的地方 flush。

#### 数据结构

老规矩，先看看数据结构。

##### CPUTLB

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

##### CPUTLBCommon

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

##### CPUTLBDesc

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

##### CPUTLBDescFast

```c
typedef struct CPUTLBDescFast {
    /* Contains (n_entries - 1) << CPU_TLB_ENTRY_BITS */
    uintptr_t mask;
    /* The array of tlb entries itself. */
    CPUTLBEntry *table; // 貌似这才是真正的 tlb entry
} CPUTLBDescFast QEMU_ALIGNED(2 * sizeof(void *));
```

`CPUTLBDesc` 和 `CPUTLBDescFast` 有什么区别？

##### CPUTLBEntry

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
            target_ulong addr_write; // 设置 3 个是因为有可读、可写、可执行 3 中权限
            target_ulong addr_code; // 哪个地址是有效值，说明该地址具有该权限
            /* Addend to virtual address to get host address.  IO accesses
               use the corresponding iotlb value.  */
            uintptr_t addend; // 物理地址
        };
        /* padding to get a power of two size */
        uint8_t dummy[1 << CPU_TLB_ENTRY_BITS];
    };
} CPUTLBEntry;
```

##### CPUIOTLBEntry

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

对此需要解释一些问题:

- 快速路径访问的是 CPUTLBEntry，而慢速路径访问 victim TLB 和 CPUIOTLBEntry

- victim TLB 的大小是固定的，而正常的 TLB 的大小是动态调整的

- CPUTLBEntry 的说明:

  - `addr_read` / `addr_write` / `addr_code` 都是 GVA

  - 分别创建出来三个 `addr_read` / `addr_write` / `addr_code` 是为了快速比较，两者相等就是命中，不相等就是不命中，如果向操作系统中的 page table 将 page entry 插入 flag 描述权限，这个比较就要使用更多的指令了(移位/掩码之后比较)

  - addend: GVA + addend 等于 HVA

- CPUIOTLBEntry 的说明:

  - 如果不是落入 RAM : `TARGET_PAGE_BITS` 内可以放 `AddressSpaceDispatch::PhysPageMap::MemoryRegionSection` 数组中的偏移，之外的位置放 MemoryRegion 内偏移。通过这个可以迅速获取 MemoryRegionSection 进而获取 MemoryRegion。

  - 如果是落入 RAM，可以得到 ram addr。

#### 刷新函数

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

### Reference

[1] https://martins3.github.io/qemu/softmmu.html#fnref:1
