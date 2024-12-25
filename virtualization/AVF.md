## AVF

[Android Virtualization Framework](https://source.android.com/docs/core/virtualization/whyavf?hl=zh-cn)，这篇文档详细介绍了为什么要开发 avf，而不是使用现有的 kvm。简而言之，就是现有的 ARM64 kvm 实现是基于 VHE 的，其将整个 host(linux kernel) 执行在 el2，这会导致系统的可信计算基 (TCB) 变大，不安全。所以 google 才开发了这套安全的虚拟化框架，让 hypervisor 执行在 el2，让 hostos/guestos 执行在 el1，这样 hostos 也不能访问 guestos 的数据，TCB 就只有 hypervisor，很小很安全。

### 虚拟化架构

在 PKVM 方案中，hostos/guestos/hypervisor 之间的关系是这样的，

### 初始化架构

在使能了 pkvm 的机器上，完整的初始化是这样的，

![pkvm-boot-device.jpg](https://github.com/UtopianFuture/UtopianFuture.github.io/blob/master/image/pkvm-boot-device.jpg?raw=true)

我们依次分析一下这 5 个阶段，

#### Early boot

```c
| __HEAD
|	-> primary_entry // branch to kernel start, magic
|		-> init_kernel_el
|			-> init_el2 // 这之后就是我们熟知的 start_kernel
```

```c
SYM_FUNC_START(init_kernel_el)
	mrs	x1, CurrentEL
	cmp	x1, #CurrentEL_EL2
	b.eq	init_el2
SYM_INNER_LABEL(init_el2, SYM_L_LOCAL)
	msr	elr_el2, lr
	// clean all HYP code to the PoC if we booted at EL2 with the MMU on
	cbz	x0, 0f
	adrp	x0, __hyp_idmap_text_start
	adr_l	x1, __hyp_text_end
	adr_l	x2, dcache_clean_poc // 对 text_start-text_end 范围内的数据进行 cache 操作
	blr	x2
	mov_q	x0, INIT_SCTLR_EL2_MMU_OFF // mmu 在启动早期到底需要进行哪些操作？
	pre_disable_mmu_workaround
	msr	sctlr_el2, x0
	isb
0:
	mov_q	x0, HCR_HOST_NVHE_FLAGS
	msr	hcr_el2, x0 // 配置 NVHE
	isb
	init_el2_state // 初始化 el2 的系统寄存器，如 sctlr_el2 等
	/* Hypervisor stub */
	adr_l	x0, __hyp_stub_vectors
	msr	vbar_el2, x0 // 设置 el2 的初始异常向量表 __hyp_stub_vectors
	isb
	mov_q	x1, INIT_SCTLR_EL1_MMU_OFF
	/*
	 * Fruity CPUs seem to have HCR_EL2.E2H set to RES1,
	 * making it impossible to start in nVHE mode. Is that
	 * compliant with the architecture? Absolutely not!
	 */
	mrs	x0, hcr_el2
	and	x0, x0, #HCR_E2H
	cbz	x0, 1f
	/* Set a sane SCTLR_EL1, the VHE way */
	pre_disable_mmu_workaround
	msr_s	SYS_SCTLR_EL12, x1
	mov	x2, #BOOT_CPU_FLAG_E2H
	b	2f
1:
	pre_disable_mmu_workaround
	msr	sctlr_el1, x1
	mov	x2, xzr
2:
	__init_el2_nvhe_prepare_eret // 如果是 nVHE，通过 eret 返回到 el1 执行
	mov	w0, #BOOT_CPU_MODE_EL2 // 对于 VHE，依旧在 EL2 运行
	orr	x0, x0, x2
	eret
SYM_FUNC_END(init_kernel_el)
```

#### Memory reservation

该阶段主要是分配 hypervisor 中需要使用的内存，如各种数据结构，页表等。

```c
| start_kernel
|	-> setup_arch
|		-> bootmem_init
|			-> kvm_hyp_reserve
```

```c
void __init kvm_hyp_reserve(void)
{
	u64 hyp_mem_pages = 0;
	int ret;

	...

    // 所谓注册就是使用 hyp_memory 和 hyp_memblock_nr_ptr 两个 nvhe 中的全局变量来记录 reserved.memroy 信息
	ret = register_memblock_regions();
    // 和上面类似，不过是使用 moveable_regs 再记录一下，为啥？
	ret = register_moveable_regions();
    // hypervisor 就使用这些内存么
	hyp_mem_pages += hyp_s1_pgtable_pages();
	hyp_mem_pages += host_s2_pgtable_pages(); // 这就是 stage2 页表使用的内存么，怎样计算的呢？
	hyp_mem_pages += hyp_vm_table_pages();
	hyp_mem_pages += hyp_vmemmap_pages(STRUCT_HYP_PAGE_SIZE);
	hyp_mem_pages += hyp_ffa_proxy_pages();

	/*
	 * Try to allocate a PMD-aligned region to reduce TLB pressure once
	 * this is unmapped from the host stage-2, and fallback to PAGE_SIZE.
	 */
	hyp_mem_size = hyp_mem_pages << PAGE_SHIFT;
	hyp_mem_base = memblock_phys_alloc(ALIGN(hyp_mem_size, PMD_SIZE),
					 PMD_SIZE); // 这个就很熟悉了，这部分内存不会释放给 buddy，kernel 使用不了

    ...

}
```

#### Preparing init

这部分要做的工作就多了，我们一个个看，

```c
| kvm_arm_init // module_init，kvm 是一个 ko，使用这种方式加载
|	-> is_hyp_mode_available // 判断当前 kvm 是否支持 pkvm
|	-> kvm_get_mode // 当前内核是否支持 kvm
|	-> kvm_sys_reg_table_init // 没看懂为啥要初始化这些值
|	-> is_kernel_in_hyp_mode // 判断内核是否处于 el2
|	-> kvm_set_ipa_limit // 架构支持的 ipa 位数，会打印出来
|	-> kvm_arm_init_sve // 可扩展向量扩展，SIMD 指令集的扩展，暂不需要关注
|	// vmid 为 8/16 位，由 ID_AA64MMFR1_EL1_VMIDBits_16 寄存器控制
|	// 用 bitmap 来管理 vmid，使用 new_vmid 来获取新的 vmid
|	-> kvm_arm_vmid_alloc_init
|	-> init_hyp_mode // 为什么只有在 el2 下才需要执行该函数
|		-> kvm_mmu_init // 主要是初始化 hypervisor 的页表以及建立 idmap 映射
|			// 申请页表，这个页表是 hypervisor 在 stage1 使用的，stage2 初始化完后会释放
|			-> kvm_pgtable_hyp_init
|			-> kvm_map_idmap_text // 将 hypervisor 镜像的地址信息做好 identity 映射
|				-> __create_hyp_mappings // 传入的 start 和 phys 是一样的，这就是 identity 映射
|		-> create_hyp_mappings
|		-> init_pkvm_host_fp_state
|		-> kvm_hyp_init_symbols
|		-> kvm_hyp_init_protection
|			-> do_pkvm_init // 进入到 el2，执行下一阶段的工作
|				-> __pkvm_init
|	-> kvm_init_vector_slots // 防止 spectre 漏洞
|	-> init_subsystems
|		-> cpu_hyp_init // 配置向量表，以及 kvm_host_data 等关键变量
|		-> hyp_cpu_pm_init
|		-> kvm_vgic_hyp_init // 中断虚拟化相关，暂不分析
|		-> kvm_timer_hyp_init // 时钟虚拟化，暂不分析
|	// 目前 ARM64 KVM 有三种模式，
|	// PKVM，google 在 nVHE 基础上增加的，pKVM 作为 hypervisor 运行在 el2，android 运行在 el1
|	// VHE，kvm + kernel 作为 hypervisor，运行在 EL2，kernel 能访问所有的物理地址
|	// nVHE，hypervisor 运行在 el2，kernel 运行在 el1
| 	// VHE 和 nVHE 都是 ARM 为支持虚拟化开发的硬件特性，PKVM 是在 nVHE 的基础上开发的 hypervisor
|	-> is_protected_kvm_enabled
|	-> kvm_init
|		-> kvm_irqfd_init
|		-> kvm_vfio_ops_init
|	-> finalize_init_hyp_mode
```

##### 关键函数 init_hyp_mode

该函数只有在 nVHE 模式下才会执行，这里的种种操作都是初始化执行在 el2 的 hypervisor。这里面涉及到很多架构相关的操作，还不熟悉。

```c
/* Inits Hyp-mode on all online CPUs */
static int __init init_hyp_mode(void)
{
	u32 hyp_va_bits;
	int cpu;

	/*
	 * The protected Hyp-mode cannot be initialized if the memory pool
	 * allocation has failed.
	 */
    // hyp_mem_base 是全局变量，在 bootmem_init -> kvm_hyp_reserve 从 memblock 中申请内存
    // 从这里就可以一窥 pkvm 的执行流程
    // 系统先是在 el1 执行各种初始化操作，然后通过 hvc 进入到 el2
    // 后面会给出 pkvm 的详细执行流程
	if (is_protected_kvm_enabled() && !hyp_mem_base)

	/*
	 * Allocate Hyp PGD and setup Hyp identity mapping
	 */
	err = kvm_mmu_init(&hyp_va_bits);

	/*
	 * Allocate stack pages for Hypervisor-mode
	 */
    // 给每个 CPU 分配栈内存，这些内存是用来干嘛的
	for_each_possible_cpu(cpu) {
		unsigned long stack_page;
		stack_page = __get_free_page(GFP_KERNEL);
		per_cpu(kvm_arm_hyp_stack_page, cpu) = stack_page;
	}

	/*
	 * Allocate and initialize pages for Hypervisor-mode percpu regions.
	 */
    // 这块内存也不知道干嘛用的
	for_each_possible_cpu(cpu) {
		struct page *page;
		void *page_addr;
		page = alloc_pages(GFP_KERNEL, nvhe_percpu_order());
		page_addr = page_address(page);
		memcpy(page_addr, CHOOSE_NVHE_SYM(__per_cpu_start), nvhe_percpu_size());
		kvm_nvhe_sym(kvm_arm_hyp_percpu_base)[cpu] = (unsigned long)page_addr;
	}

	... // 建立 hypervisor 代码段，数据段等映射

	/*
	 * Map the Hyp stack pages
	 */
    // 这些映射也没搞明白
	for_each_possible_cpu(cpu) {
		struct kvm_nvhe_init_params *params = per_cpu_ptr_nvhe_sym(kvm_init_params, cpu);
		char *stack_page = (char *)per_cpu(kvm_arm_hyp_stack_page, cpu);
		err = create_hyp_stack(__pa(stack_page), &params->stack_hyp_va);

		/*
		 * Save the stack PA in nvhe_init_params. This will be needed
		 * to recreate the stack mapping in protected nVHE mode.
		 * __hyp_pa() won't do the right thing there, since the stack
		 * has been mapped in the flexible private VA space.
		 */
		params->stack_pa = __pa(stack_page);
	}

	for_each_possible_cpu(cpu) {
		char *percpu_begin = (char *)kvm_nvhe_sym(kvm_arm_hyp_percpu_base)[cpu];
		char *percpu_end = percpu_begin + nvhe_percpu_size();

		/* Map Hyp percpu pages */
		err = create_hyp_mappings(percpu_begin, percpu_end, PAGE_HYP);

		/* Prepare the CPU initialization parameters */
		cpu_prepare_hyp_mode(cpu, hyp_va_bits);
	}

	err = init_pkvm_host_fp_state();
	kvm_hyp_init_symbols();
	hyp_trace_init_events();
	if (is_protected_kvm_enabled()) {
		if (IS_ENABLED(CONFIG_ARM64_PTR_AUTH_KERNEL) &&
		 cpus_have_const_cap(ARM64_HAS_ADDRESS_AUTH))
			pkvm_hyp_init_ptrauth();
		init_cpu_logical_map();
		if (!init_psci_relay()) {
		}
		err = kvm_hyp_init_protection(hyp_va_bits);
	}
	return 0;
}
```

#### pKVM init

```c
| handle___pkvm_init // host_hcall 中的回调函数
| 	-> __pkvm_init
| 		-> divide_memory_pool
| 		-> recreate_hyp_mappings
| 		-> hyp_alloc_init
| 		-> update_nvhe_init_params
| 		-> __pkvm_init_switch_pgd
| 			-> __pkvm_init_finalise // 配置 stage2 页表和页表操作函数
| 				-> kvm_host_prepare_stage2
| 				-> __host_enter // 在 hyp/nvhe/host.S 中，前面还有一系列操作
```

##### 关键函数 __pkvm_init

在 el1 中建立好 idmap 之后，通过 hvc 进入到 el2。phys, size 就是 `kvm_hyp_reserve` 中分配的 hyp_mem_base 和 hyp_mem_size。

```c
int __pkvm_init(phys_addr_t phys, unsigned long size, unsigned long nr_cpus,
		unsigned long *per_cpu_base, u32 hyp_va_bits)
{
	void *virt = hyp_phys_to_virt(phys);
	typeof(__pkvm_init_switch_pgd) *fn;
	int ret;

	...

    // 这里就和 kvm_hyp_reserve 中计算 size 的过程一一对应
	ret = divide_memory_pool(virt, size);
    // 对 hypervisor 镜像中的各个段重新映射成不同属性
    // 为啥要重新映射？
    // 很合理，在 kvm_mmu_init 中的映射是 hostos 建立的 el1 映射
    // 这里进入了 el2，需要确保 el1 中的 hostos 无法访问，应该是这样

	ret = recreate_hyp_mappings(phys, size, per_cpu_base, hyp_va_bits);

    // 这里涉及到 hyp_allocator 结构体
    // 猜测一下，这 128MB 就是 hypervisor 能够使用的内存
    // 从注释来看是虚拟地址
    // 只能使用 128MB 的虚拟地址？
	ret = hyp_alloc_init(SZ_128M);

    // 更新 kvm_init_params->pgd_pa
	update_nvhe_init_params();

	/* Jump in the idmap page to switch to the new page-tables */
	fn = (typeof(fn))__hyp_pa(__pkvm_init_switch_pgd);
	fn(this_cpu_ptr(&kvm_init_params), __pkvm_init_finalise);
	unreachable();
}
```

##### 关键函数 __pkvm_init_switch_pgd

这里切换的逻辑很清晰，这才叫好代码。

```c
/*
 * void __pkvm_init_switch_pgd(struct kvm_nvhe_init_params *params,
 * void (*finalize_fn)(void));
 *
 * SYM_TYPED_FUNC_START() allows C to call this ID-mapped function indirectly
 * using a physical pointer without triggering a kCFI failure.
 */
SYM_TYPED_FUNC_START(__pkvm_init_switch_pgd)
	/* Load the inputs from the VA pointer before turning the MMU off */
	ldr	x5, [x0, #NVHE_INIT_PGD_PA]
	ldr	x0, [x0, #NVHE_INIT_STACK_HYP_VA]
	/* Turn the MMU off */
	pre_disable_mmu_workaround
	mrs	x2, sctlr_el2
	bic	x3, x2, #SCTLR_ELx_M
	msr	sctlr_el2, x3
	isb
	tlbi	alle2
	/* Install the new pgtables */
	phys_to_ttbr x4, x5
alternative_if ARM64_HAS_CNP
	orr	x4, x4, #TTBR_CNP_BIT
alternative_else_nop_endif
	msr	ttbr0_el2, x4 // hypervisor 用的是 ttbr0_el2 寄存器
	/* Set the new stack pointer */
	mov	sp, x0
	/* And turn the MMU back on! */
    // 也就是说，MMU 翻译可以指定页表地址，那每次上下文切换也要更换页表
    // 肯定可以啊，每个进程都有自己的页表基地址，存在 mm_struct 中
	set_sctlr_el2	x2
	ret	x1
SYM_FUNC_END(__pkvm_init_switch_pgd)
```

##### 关键函数 __pkvm_init_finalise

切换好页表后执行就是 hypervisor 自己的代码了。

```c
void __noreturn __pkvm_init_finalise(void)
{
    // 该变量在 init_hyp_mode->init_subsystems 中初始化的
	struct kvm_host_data *host_data = this_cpu_ptr(&kvm_host_data);
	struct kvm_cpu_context *host_ctxt = &host_data->host_ctxt;
	unsigned long nr_pages, reserved_pages, pfn;
	int ret;

	/* Now that the vmemmap is backed, install the full-fledged allocator */
	pfn = hyp_virt_to_pfn(hyp_pgt_base); // divide_memory_pool 中申请好的
	nr_pages = hyp_s1_pgtable_pages(); // hypervisor 怎么会管理 stage1 的映射呢？
	reserved_pages = hyp_early_alloc_nr_used_pages();
	ret = hyp_pool_init(&hpool, pfn, nr_pages, reserved_pages); // 维护 hpool 的意图是啥

    // 给 hostos 建立 stage2 的映射
    // 这样来看 stage2 的映射都是 hypervisor 管理的
    // hostos 的种种资源都是在 pkvm 初始化的时候做的
    // 不像其他 vm 是在发起 create vm ioctl 中做的
	ret = kvm_host_prepare_stage2(host_s2_pgt_base);

    // 这些都是 pkvm 修改映射关系需要用到的函数
    // 在后面的 donate/share 操作有不同的类型
    // host/hyp -> host/hyp/guest
    // 在操作不同的页表时，就需要使用对应 pgtable 对应的 mm_ops
    // 当然这里面的操作大同小异
	pkvm_pgtable_mm_ops = (struct kvm_pgtable_mm_ops) {
		.zalloc_page = hyp_zalloc_hyp_page,
		.phys_to_virt = hyp_phys_to_virt,
		.virt_to_phys = hyp_virt_to_phys,
		.get_page = hpool_get_page, // 转换成对应的 hyp_page，然后增加 refcount
		.put_page = hpool_put_page,
		.page_count = hyp_page_count,
	};
	pkvm_pgtable.mm_ops = &pkvm_pgtable_mm_ops;

    // 将 hypervisor 中的 hyp_page.refcount 都 +1
    // 防止被释放
	ret = fix_hyp_pgtable_refcnt();
	ret = hyp_create_pcpu_fixmap();
    // 读取系统寄存器 cntfrq_el0，就是 el0 的时钟频率，即系统的时钟频率
	ret = pkvm_timer_init();
	// 看起来像是将 hypervisor 使用的几块内存 owner 设置为 PKVM_ID_HYP
	ret = fix_host_ownership();
	ret = unmap_protected_regions();
	// pin 操作就是增加 refcount，让系统不会回收这块内存
	ret = pin_host_tables();
	ret = hyp_ffa_init(ffa_proxy_pages);
	pkvm_hyp_vm_table_init(vm_table_base);
out:
	/*
	 * We tail-called to here from handle___pkvm_init() and will not return,
	 * so make sure to propagate the return value to the host.
	 */
	cpu_reg(host_ctxt, 1) = ret;
	__host_enter(host_ctxt);
}
```

##### 关键函数 kvm_host_prepare_stage2

```c
int kvm_host_prepare_stage2(void *pgt_pool_base)
{
	struct kvm_s2_mmu *mmu = &host_mmu.arch.mmu; // 后面映射都是使用该变量
	int ret;

	prepare_host_vtcr(); // host_mmu.arch.vtcr 是干什么的
	hyp_spin_lock_init(&host_mmu.lock);
	mmu->arch = &host_mmu.arch;

    // 初始化 host_mmu.mm_ops
    // 这个 ops 和 pkvm_pgtable_mm_ops 有什么区别
    // 这个很好理解，这里初始化的是 host_mmu
    // 里面这些回调函数自然是在 host 修改页表的时候使用的
	ret = prepare_s2_pool(pgt_pool_base);
	host_s2_pte_ops.force_pte_cb = host_stage2_force_pte;
	host_s2_pte_ops.pte_is_counted_cb = host_stage2_pte_is_counted;

    // 初始化 host_mmu.pgt 变量
    // 这里面 start_level 变量是用来干嘛的
	ret = __kvm_pgtable_stage2_init(&host_mmu.pgt, mmu,
					&host_mmu.mm_ops, KVM_HOST_S2_FLAGS,
					&host_s2_pte_ops);
	mmu->pgd_phys = __hyp_pa(host_mmu.pgt.pgd);
	mmu->pgt = &host_mmu.pgt; // mmu 也可以通过 host_mmu 获取到啊
	atomic64_set(&mmu->vmid.id, 0);
	return prepopulate_host_stage2();
}
```

##### 关键函数 __kvm_pgtable_stage2_init

```c
int __kvm_pgtable_stage2_init(struct kvm_pgtable *pgt, struct kvm_s2_mmu *mmu,
			 struct kvm_pgtable_mm_ops *mm_ops,
			 enum kvm_pgtable_stage2_flags flags,
			 struct kvm_pgtable_pte_ops *pte_ops)
{
	size_t pgd_sz;
	u64 vtcr = mmu->arch->vtcr;
	u32 ia_bits = VTCR_EL2_IPA(vtcr);
	u32 sl0 = FIELD_GET(VTCR_EL2_SL0_MASK, vtcr);
	u32 start_level = VTCR_EL2_TGRAN_SL0_BASE - sl0;
	pgd_sz = kvm_pgd_pages(ia_bits, start_level) * PAGE_SIZE;
	pgt->pgd = (kvm_pteref_t)mm_ops->zalloc_pages_exact(pgd_sz);
	if (!pgt->pgd)
		return -ENOMEM;
	pgt->ia_bits		= ia_bits;
	pgt->start_level	= start_level;
	pgt->mm_ops		= mm_ops; // mem_protect.c:132
	pgt->mmu		= mmu;
	pgt->flags		= flags;
	pgt->pte_ops		= pte_ops;
	/* Ensure zeroed PGD pages are visible to the hardware walker */
	dsb(ishst);
	return 0;
}
```

#### Host deprivileged

这一步是怎样做的呢？可能是 `__host_entry` 完成的，还不确定。

```c
SYM_FUNC_START(__host_exit)
	get_host_ctxt	x0, x1
	/* Store the host regs x2 and x3 */
	stp	x2, x3, [x0, #CPU_XREG_OFFSET(2)]
	/* Retrieve the host regs x0-x1 from the stack */
	ldp	x2, x3, [sp], #16	// x0, x1
	/* Store the host regs x0-x1 and x4-x17 */
	stp	x2, x3, [x0, #CPU_XREG_OFFSET(0)]
	stp	x4, x5, [x0, #CPU_XREG_OFFSET(4)]
	stp	x6, x7, [x0, #CPU_XREG_OFFSET(6)]
	stp	x8, x9, [x0, #CPU_XREG_OFFSET(8)]
	stp	x10, x11, [x0, #CPU_XREG_OFFSET(10)]
	stp	x12, x13, [x0, #CPU_XREG_OFFSET(12)]
	stp	x14, x15, [x0, #CPU_XREG_OFFSET(14)]
	stp	x16, x17, [x0, #CPU_XREG_OFFSET(16)]
	/* Store the host regs x18-x29, lr */
	save_callee_saved_regs x0
	/* Save the host context pointer in x29 across the function call */
	mov	x29, x0
#ifdef CONFIG_ARM64_PTR_AUTH_KERNEL
alternative_if_not ARM64_HAS_ADDRESS_AUTH
b __skip_pauth_save
alternative_else_nop_endif
alternative_if ARM64_KVM_PROTECTED_MODE
	/* Save kernel ptrauth keys. */
	add x18, x29, #CPU_APIAKEYLO_EL1
	ptrauth_save_state x18, x19, x20
	/* Use hyp keys. */
	adr_this_cpu x18, kvm_hyp_ctxt, x19
	add x18, x18, #CPU_APIAKEYLO_EL1
	ptrauth_restore_state x18, x19, x20
	isb
alternative_else_nop_endif
__skip_pauth_save:
#endif /* CONFIG_ARM64_PTR_AUTH_KERNEL */
	bl	handle_trap
__host_enter_restore_full:
	/* Restore kernel keys. */
#ifdef CONFIG_ARM64_PTR_AUTH_KERNEL
alternative_if_not ARM64_HAS_ADDRESS_AUTH
b __skip_pauth_restore
alternative_else_nop_endif
alternative_if ARM64_KVM_PROTECTED_MODE
	add x18, x29, #CPU_APIAKEYLO_EL1
	ptrauth_restore_state x18, x19, x20
alternative_else_nop_endif
__skip_pauth_restore:
#endif /* CONFIG_ARM64_PTR_AUTH_KERNEL */
	/* Restore host regs x0-x17 */
	ldp	x0, x1, [x29, #CPU_XREG_OFFSET(0)]
	ldp	x2, x3, [x29, #CPU_XREG_OFFSET(2)]
	ldp	x4, x5, [x29, #CPU_XREG_OFFSET(4)]
	ldp	x6, x7, [x29, #CPU_XREG_OFFSET(6)]
	/* x0-7 are use for panic arguments */
__host_enter_for_panic:
	ldp	x8, x9, [x29, #CPU_XREG_OFFSET(8)]
	ldp	x10, x11, [x29, #CPU_XREG_OFFSET(10)]
	ldp	x12, x13, [x29, #CPU_XREG_OFFSET(12)]
	ldp	x14, x15, [x29, #CPU_XREG_OFFSET(14)]
	ldp	x16, x17, [x29, #CPU_XREG_OFFSET(16)]
	/* Restore host regs x18-x29, lr */
	restore_callee_saved_regs x29
	/* Do not touch any register after this! */
__host_enter_without_restoring:
	eret
	sb
SYM_FUNC_END(__host_exit)

/*
 * void __noreturn __host_enter(struct kvm_cpu_context *host_ctxt);
 */
SYM_FUNC_START(__host_enter)
	mov	x29, x0
	b	__host_enter_restore_full // 就是恢复 host 的寄存器现场，然后 eret 返回到 el1
SYM_FUNC_END(__host_enter)
```

### 目标

- 内存虚拟化，mmu/smmu 怎样建页表，host/guest 怎样访问内存；

### 问题
- avf 这套框架是怎样处理多核情况的？
 对于虚拟化技术来说，npu, vpu 等 master 都是外设；
- stage1 和 stage2 是什么，分别由谁填充页表？
 - stage1 是内核填充，stage2 是 hypervisor 填充的？
- pcsi 怎样工作的，哪些组件会触发 smc？
- 怎样使能 mmu？
 - kernel 的 start_kernel 之前有一段汇编，使能 mmu，应该只是使能 stage1，在 kvm 初始化的时候，才会使能 stage2；
- hypervisor 可以解析EL1 页表，怎样解析？
- pkvm 是怎样实现共享内存，让 host 和 guest 都能访问？
- VMID 是怎样产生的？
 - kvm 初始化时，有一个函数，`kvm_arm_vmid_alloc_init`，会使用 bitmapt 来管理 vmid，然后有一个 new_vmid 的接口，能够申请 vmid
- pkvm 是怎样进行 hostos/guestos 切换的，以及 vcpu 调度？
