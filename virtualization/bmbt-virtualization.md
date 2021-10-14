## bmbt 系统级虚拟化

## 中期目标

1. 在 loongarch 的 qemu 上输出 hello world.
2. 调研 memory managment 相关设计和实现。

### 零、 虚拟化知识

VMM 对物理资源的虚拟可以归结为三个主要任务：处理器虚拟化、内存虚拟化和 I/O 虚拟化。

#### 1. 可虚拟化架构与不可虚拟化架构

系统级的虚拟机由于 VMM 直接运行在 bare metal 上，整体可以分为 3 个部分：

![](https://github.com/UtopianFuture/UtopianFuture.github.io/blob/master/image/bmbt.1.png?raw=true)

在我们的项目中，虚拟机暂时是 ubuntu，虚拟机监控器是 bmbt，硬件是 LoongArch。

敏感指令：操作特权资源的指令，包括修改虚拟机的运行模式或物理机的状态，访存等等。所有的特权指令都是敏感指令，但不是所有的敏感指令都是特权指令。为了 VMM 能够完全控制物理资源，敏感指令必须在 VMM 的监督下才能运行，或者经由 VMM 完成。如果一个系统所有的敏感指令都是特权指令，那么只需要将 VMM 运行在系统的最高特权级上，而 guest 系统运行在非最高特权级上，在执行敏感指令时，陷入到 VMM 中。而如果像 x86 架构一样，不是所有的敏感指令都是特权指令，那么我们就说这种架构存在虚拟化漏洞，是不可虚拟化架构。这种架构需要采用一些辅助方法，如在硬件层面填补虚拟化漏洞——VT；通过软件方法避免使用无法陷入的敏感指令。

问题：BMBT 要怎样解决 x86 的虚拟化漏洞？

#### 2. 处理器虚拟化

##### 2.1 指令模拟

​	guest 运行的架构可能不是原架构，如 ubuntu 只能运行在 x86 架构上，如何使其在 LoongArch 架构的 CPU 上运行却察觉不到和 x86 架构有区别，换句话说，其运行的处理器和 VMM 需要提供与其“期望”的处理器一致的功能和行为。典型的“期望”包括：

（1）指令集和与执行效果。

（2）可用寄存器集合，包括通用寄存器以及各种系统寄存器。

（3）运行模式，如实模式、保护模式和 64 位长模式等。处理器的运行模式决定了指令执行的效果、寻址宽度与限制以及保护粒度等。

（4）地址翻译系统，如页表级数。

（5）保护机制，如分段和分页等。

（6）中断/异常机制，如虚拟处理器必须能够正确模拟真实处理器的行为，在错误的执行条件下，为虚拟机注入一个虚拟的异常。

##### 2.2 中断和异常的模拟及注入

​	VMM 对于异常的虚拟化需要完全按照物理处理器对于各种异常条件的定义，再根据虚拟处理器当时的内容，来判断是否需要模拟出一个虚拟的异常，并注入到 guest 中。

##### 2.3 SMP 模拟

​	底层有 N 个物理 CPU，能够虚拟出 M 个虚拟 CPU。

问题：

1. bmbt 如何达到这一功能，即让 ubuntu 运行在 LoongArch 架构的 CPU 上运行却察觉不到和 x86 架构有区别？仿照 QEMU？
2. 如何解决虚拟寄存器，上下文切换和虚拟处理器？之前看《LINUX 系统虚拟化》了解到，虚拟 CPU 就是定义一个结构体来表示，如 KVM 中的 VMCS，QEMU 中的 X86State 等，之后所有的操作都是对这个结构体进行操作，bmbt 也是这样实现的么？
3. 当 guest 要访问敏感资源时，要陷入到 VMM，如何陷入？
4. 二进制翻译是用 latx 还是用 QEMU 的 tcg？

#### 3. 内存虚拟化(见[linux 系统虚拟化](https://github.com/UtopianFuture/UtopianFuture.github.io/blob/master/linux-note/linux%E7%B3%BB%E7%BB%9F%E8%99%9A%E6%8B%9F%E5%8C%96.md))

问题：

（1）内存分配算法怎样设计？

#### 4. I/O 虚拟化(见[linux 系统虚拟化](https://github.com/UtopianFuture/UtopianFuture.github.io/blob/master/linux-note/linux%E7%B3%BB%E7%BB%9F%E8%99%9A%E6%8B%9F%E5%8C%96.md))

​	现实中 I/O 资源是有限的额，为了满足多个 gues 对 I/O 的访问需求，VMM 必须通过 I/O 虚拟化的方式复用有限的 I/O 资源。

问题：

（1）I/O 虚拟化的设计思想是什么？目前进展怎么样？

### 一、调试 LA 内核

#### 1. 环境搭建

[在 qemu 上调试 LoongArch 内核](https://github.com/UtopianFuture/UtopianFuture.github.io/blob/master/kernel/debug%20loongarch%20kernel%20in%20qemu.md)

#### 2. 内核启动过程

主核的执行入口（PC 寄存器的初始值）是编译内核时决定的，运行时由 BIOS 或者 BootLoader 传递给内核。内核的初始入口是`kernel_entry`。LA 的`kernel_entry`和 mips 的类似，进行.bss 段的清 0（为什么要清 0），保存 a0~a3 等操作。之后就进入到第二入口`start_kernel()`。

通过 gdb 单步调试看 LA 内核是怎样初始化的。但是遇到一个问题，内核使用`-O2`优化项，在单步调试时很多值都是`optimized out`，同时设置断点也不会顺序执行，是跳着执行的，给阅读代码带来困难。后来请教师兄，这是正常的，start_kernel()部分的代码可以直接看源码，不用单步调试。


### 二、源码阅读

#### 1. start_kernel()

`start_kernel()`的一级节点中，架构相关的重要函数有`setup_arch()`, t`rap_init()`, `init_IRQ()`, `time_init()`。代码中涉及的技术都在之后有介绍。

##### 1.1 setup_arch()

架构相关，代码和 mips 类似，下为代码树展开。

```plain
setup_arch()
| -- cpu_probe(); // 探测cpu类型，写入cputype中
|
| -- plat_early_init(); // 解析bios传入的参数
|	| -- fw_init_cmdline(); // 获取参数
|	| -- prom_init_env(); // 根据参数设置环境变量
|	| -- memblock_and_maxpfn_init() // 挂载memblock
|		| -- memblock_add();		// loongson_mem_map和boot_mem_map是什么关系
|
| -- init_initrd(); // 主要是检查initrd_start和initrd_end是否正确，将其映射到虚拟地址
|
| -- prom_init(); // 初始化io空间的基址、ACPI表、loongarch使用的numa存储等
|	| -- set_io_port_base(); // 设置IO空间的基址
|	| -- if(efi_bp){} // efi_bp是在prom_init_env()中用bios传递的_fw_envp赋值的，之后进行ACPI初始化，主要是初始化各种表
|	| -- acpi_table_upgrade(); // 通过CPIO获取或bios收集的数据，对各个表进行初始化
|	| -- acpi_boot_table_init();
|		| -- acpi_initialize_tables(); // Initialize the table manager, get the RSDP and RSDT/XSDT.
|	| -- acpi_boot_init(); // 主要是解析MADT
|	| -- prom_init_numa_memory();
|		| -- numa_mem_init(); // 初始化numa
|			| -- numa_default_distance(); // 初始化numa节点的距离矩阵
|			| -- init_node_memblock(); // 逐个分析内存分布图并将结果通过add_mem_region()保存到loongson_mem_map中
|	| -- loongson_acpi_init(); // ACPI初始化始终是个大问题，需要进一步了解ACPI才能看懂
|
| -- cpu_report(); // 打印一些初始化后CPU的信息
|
| -- arch_mem_init(); // 主要是初始化设备树和bootmem
|	| -- plat_mem_setup(); // detects the memory configuration and
|						   // will record detected memory areas using add_memory_region.
|			| -- early_init_dt_scan_memory(); // 早期读取bios传入的信息，最终通过memblock_add()挂载
|	| -- early_init_dt_scan(); // 早期初始化设备树
|	| -- dt_bootmem_init(); // 建立boot_mem_map内存映射图，boot_mem_map主要给BootMem内存分配器用，只包含系统内存
|							// 这里不是初始化bootmem的地方，而只是确定其上下界，
|							// 然后通过memblock_add_range()（核心函数）将其挂载
|	| -- device_tree_init(); // 用bios传递的信息初始化设备树节点
|		| -- unflatten_and_copy_device_tree();
|			| -- early_init_dt_alloc_memory_arch(); // 先在初始化好的bootmem中分配物理空间
|			| -- unflatten_device_tree(); // create tree of device_nodes from flat blob
|
|	| -- sparse_init(); // 初始化稀疏型内存模型
|
|	| -- plat_swiotlb_setup(); // swiotlb为软件中转站，用于让任意设备能够对任意内存地址发起DMA访问
| 							   // 要保证弱寻址能力的设备能够访问，所有尽早初始化
|
|	| -- resource_init(); // 在已经初始化的bootmem中为code, date, bss段分配空间
|
|	| -- plat_smp_setup(); // smp是多对称处理器，这里先配置主核，主要是主核编号，核间中断等
|
|	| -- prefill_possible_map(); // 建立合理的逻辑CPU的possible值，possible和present的区别是CPU物理热拔插，
|								 // 如果物理上移除一个CPU，present就会减1，默认两者像等
|
|	| -- cpu_cache_init(); // 三级cache初始化，主要是ways, sets, size
|		| -- setup_protection_map(); // 建立进程VMA权限到页表权限的映射表（为什么是16个页表？）
|
|	| -- paging_init(); // 初始化各个内存页面管理区。设置不同的页面管理区是为访问能力有限的设备服务
|		| -- free_area_init_nodes(); // Initialise all pg_data_t and zone data, the start_pfn, end_pfn.
\
```

###### 1.1.1 cpu_probe()

源码分析：

```c
void cpu_probe(void) // probe CPU type, LOONGARCH's processor_id should be 0
{
	struct cpuinfo_loongarch *c = &current_cpu_data; // current_cpu_data指向当前cpu信息
	unsigned int cpu = smp_processor_id(); // 获取当前cpu编号

	/*
	 * Set a default elf platform, cpu probe may later
	 * overwrite it with a more precise value
	 */
	set_elf_platform(cpu, "loongarch");

	c->cputype	= CPU_UNKNOWN; // 初始化当前cpu的信息
	c->processor_id = read_cpucfg(LOONGARCH_CPUCFG0); // 有多个CPUCFG，这些CFG是干嘛用的，同时read_cpucfd()好像返回的都是0，怎么回事
	c->fpu_vers	= (read_cpucfg(LOONGARCH_CPUCFG2) >> 3) & 0x3;
	c->writecombine = _CACHE_SUC;

	c->fpu_csr31	= FPU_CSR_RN;
	c->fpu_msk31	= FPU_CSR_RSVD | FPU_CSR_ABS2008 | FPU_CSR_NAN2008;

	switch (c->processor_id & PRID_COMP_MASK) {
	case PRID_COMP_LOONGSON:
		cpu_probe_loongson(c, cpu); // 通过这个函数探测CPU类型
		break;
	}

	BUG_ON(!__cpu_family[cpu]);
	BUG_ON(c->cputype == CPU_UNKNOWN);

	/*
	 * Platform code can force the cpu type to optimize code
	 * generation. In that case be sure the cpu type is correctly
	 * manually setup otherwise it could trigger some nasty bugs.
	 */
	BUG_ON(current_cpu_type() != c->cputype);

	if (loongarch_fpu_disabled)
		c->options &= ~LOONGARCH_CPU_FPU;

	if (c->options & LOONGARCH_CPU_FPU)
		cpu_set_fpu_opts(c);
	else
		cpu_set_nofpu_opts(c);

	if (cpu_has_lsx)
		elf_hwcap |= HWCAP_LOONGARCH_LSX;

	if (cpu_has_lasx)
		elf_hwcap |= HWCAP_LOONGARCH_LASX;

	if (cpu_has_lvz && IS_ENABLED(CONFIG_KVM_LOONGARCH_VZ)) {
		cpu_probe_lvz(c);
		elf_hwcap |= HWCAP_LOONGARCH_LVZ;
	}

	elf_hwcap |= HWCAP_LOONGARCH_CRC32;

	cpu_probe_vmbits(c);

#ifdef CONFIG_64BIT
	if (cpu == 0)
		__ua_limit = ~((1ull << cpu_vmbits) - 1);
#endif
}
```

###### 1.1.2 plat_early_init()

源码分析：

```c
void __init fw_init_cmdline(void)
{
	int i;

	fw_argc = fw_arg0; // 参数个数
	_fw_argv = (long *)fw_arg1; // 参数的字符串数组
	_fw_envp = (long *)fw_arg2; // 环境变量

	arcs_cmdline[0] = '\0';
	for (i = 1; i < fw_argc; i++) {
		strlcat(arcs_cmdline, fw_argv(i), COMMAND_LINE_SIZE);
		if (i < (fw_argc - 1))
			strlcat(arcs_cmdline, " ", COMMAND_LINE_SIZE);
	}
}
```

```c
void __init prom_init_env(void)
{
	efi_bp = (struct bootparamsinterface *)_fw_envp;

	loongson_regaddr_set(smp_group, 0x800000001fe01000, 16); // 设置smp_gropu寄存器，但不知道为什么要设置这些寄存器

	loongson_sysconf.ht_control_base = 0x80000EFDFB000000;

	loongson_regaddr_set(loongson_chipcfg, 0x800000001fe00180, 16);

	loongson_regaddr_set(loongson_chiptemp, 0x800000001fe0019c, 16);
	loongson_regaddr_set(loongson_freqctrl, 0x800000001fe001d0, 16);

	loongson_regaddr_set(loongson_tempinthi, 0x800000001fe01460, 16);
	loongson_regaddr_set(loongson_tempintlo, 0x800000001fe01468, 16);
	loongson_regaddr_set(loongson_tempintsta, 0x800000001fe01470, 16);
	loongson_regaddr_set(loongson_tempintup, 0x800000001fe01478, 16);

	loongson_sysconf.io_base_irq = LOONGSON_PCH_IRQ_BASE;
	loongson_sysconf.io_last_irq = LOONGSON_PCH_IRQ_BASE + 256;
	loongson_sysconf.msi_base_irq = LOONGSON_PCI_MSI_IRQ_BASE;
	loongson_sysconf.msi_last_irq = LOONGSON_PCI_MSI_IRQ_BASE + 192;
	loongson_sysconf.msi_address_hi = 0;
	loongson_sysconf.msi_address_lo = 0x2FF00000;
	loongson_sysconf.dma_mask_bits = LOONGSON_DMA_MASK_BIT;

	loongson_sysconf.pcie_wake_enabled =
		!(readw(LS7A_PM1_ENA_REG) & ACPI_PCIE_WAKEUP_STATUS);
	if (list_find(efi_bp->extlist))
		printk("Scan bootparm failed\n");
}
```

这个函数本来以为只是解析 bios 传入的参数，但后来看 bootmem 的过程中发现，bootmem 用的是 memblock 实现的，不是之前的位图，所以对这个函数[详细分析](https://github.com/UtopianFuture/UtopianFuture.github.io/blob/master/kernel/Analysis%20memblock_add_range().md)。

重要的数据结构：

```c
// 这个应该就是bootmem的数据结构，书上说是用位图的方式，但这里改用mem_start和mem_size表示内存空间
// 这个是BIOS内存分布图，记录了包括NUMA节点和多种类型在内的内存信息。
struct loongsonlist_mem_map {
	struct	_extention_list_hdr header;	/*{"M", "E", "M"}*/
	u8	map_count;
	struct	_loongson_mem_map {
		u32 mem_type;
		u64 mem_start;
		u64 mem_size;
	}__attribute__((packed))map[LOONGSON3_BOOT_MEM_MAP_MAX];
}__attribute__((packed));
```

```c
void __init memblock_and_maxpfn_init(void)
{
	int i;
	u32 mem_type;
	u64 mem_start, mem_end, mem_size;

	/* parse memory information */
	for (i = 0; i < loongson_mem_map->map_count; i++) { // 将map中的虚拟内存依次挂载

		mem_type = loongson_mem_map->map[i].mem_type; // loongson_mem_map在哪里初始化的？目前没有找到
		mem_start = loongson_mem_map->map[i].mem_start;
		mem_size = loongson_mem_map->map[i].mem_size;
		mem_end = mem_start + mem_size;

		switch (mem_type) {
		case ADDRESS_TYPE_SYSRAM:
			memblock_add(mem_start, mem_size); // 分配物理内存
			if (max_low_pfn < (mem_end >> PAGE_SHIFT))
				max_low_pfn = mem_end >> PAGE_SHIFT;
			break;
		}
	}
	memblock_set_current_limit(PFN_PHYS(max_low_pfn));
}
```

memblock_add_range()就是 bootmem 的 allocator，初始化过程中，所有的内存挂载，物理页的 reserved，都是通过这个函数进行。

```c
/**
 * memblock_add_range - add new memblock region
 * @type: memblock type to add new region into
 * @base: base address of the new region
 * @size: size of the new region
 * @nid: nid of the new region
 * @flags: flags of the new region
 *
 * Add new memblock region [@base, @base + @size) into @type.  The new region
 * is allowed to overlap with existing ones - overlaps don't affect already
 * existing regions.  @type is guaranteed to be minimal (all neighbouring
 * compatible regions are merged) after the addition.
 *
 * Return:
 * 0 on success, -errno on failure.
 */
int __init_memblock memblock_add_range(struct memblock_type *type,
				phys_addr_t base, phys_addr_t size,
				int nid, enum memblock_flags flags)
{
	bool insert = false;
	phys_addr_t obase = base;
	phys_addr_t end = base + memblock_cap_size(base, &size); // 防止溢出
	int idx, nr_new;
	struct memblock_region *rgn; // 每个memblock_region表示一块内存，而不再是一页一页的表示

	if (!size)
		return 0;

	/* special case for empty array */
	if (type->regions[0].size == 0) { // 该种type的第一个region
		WARN_ON(type->cnt != 1 || type->total_size);
		type->regions[0].base = base;
		type->regions[0].size = size;
		type->regions[0].flags = flags;
		memblock_set_region_node(&type->regions[0], nid);
		type->total_size = size;
		return 0;
	}
repeat:
	/*
	 * The following is executed twice.  Once with %false @insert and
	 * then with %true.  The first counts the number of regions needed
	 * to accommodate the new area.  The second actually inserts them.
	 */
	base = obase;
	nr_new = 0;

	for_each_memblock_type(idx, type, rgn) {
		phys_addr_t rbase = rgn->base;
		phys_addr_t rend = rbase + rgn->size; // 内存从低往高增长

		if (rbase >= end) // overlap，直接跳转到if(!insert)
			break;
		if (rend <= base) // 该region的end < base，也就是说会出现碎片，尝试存储在下一个region
			continue;
		/*
		 * @rgn overlaps.  If it separates the lower part of new
		 * area, insert that portion.
		 */
		if (rbase > base) {
#ifdef CONFIG_HAVE_MEMBLOCK_NODE_MAP
			WARN_ON(nid != memblock_get_region_node(rgn));
#endif
			WARN_ON(flags != rgn->flags);
			nr_new++;
			if (insert)
				memblock_insert_region(type, idx++, base,
						       rbase - base, nid,
						       flags);
		}
		/* area below @rend is dealt with, forget about it */
		base = min(rend, end);
	}

	/* insert the remaining portion */
	if (base < end) {
		nr_new++;
		if (insert)
			memblock_insert_region(type, idx, base, end - base,
					       nid, flags);
	}

	if (!nr_new)
		return 0;

	/*
	 * If this was the first round, resize array and repeat for actual
	 * insertions; otherwise, merge and return.
	 */
	if (!insert) {
		while (type->cnt + nr_new > type->max)
			if (memblock_double_array(type, obase, size) < 0)
				return -ENOMEM;
		insert = true;
		goto repeat;
	} else {
		memblock_merge_regions(type);
		return 0;
	}
}
```

###### 1.1.3 prom_init()

源码分析：

```c
void __init prom_init(void)
{
	/* init base address of io space */
	set_io_port_base((unsigned long) // ioremap()获取到io base的物理地址后set_io_port_base将其赋值给全局变量loongarch_io_port_base
		ioremap(LOONGSON_LIO_BASE, LOONGSON_LIO_SIZE));

	if (efi_bp) { // efi_bp是在prom_init_env()中用bios传递的_fw_envp赋值的
		efi_init(); // 为什么要初始化efi，efi和acpi有什么关系？
#if defined(CONFIG_ACPI) && defined(CONFIG_BLK_DEV_INITRD)
		acpi_table_upgrade(); // 这部分初始化看不懂，为什么要从cpio中获取数据。应该是bios将数据保存成这种格式。
#endif
#ifdef CONFIG_ACPI
		acpi_gbl_use_default_register_widths = false;
		acpi_boot_table_init();
		acpi_boot_init();
#endif
		if (!cpu_has_hypervisor)
			loongarch_pci_ops = &ls7a_pci_ops;
		else
			loongarch_pci_ops = &virt_pci_ops;
	}

	if (nr_pch_pics == 0)
		register_pch_pic(0, LS7A_PCH_REG_BASE,
				LOONGSON_PCH_IRQ_BASE);

#ifdef CONFIG_NUMA
	prom_init_numa_memory(); //
#else
	prom_init_memory();
#endif
	if (efi_bp) {
		dmi_scan_machine();
		if (dmi_available) {
			dmi_set_dump_stack_arch_desc();
			smbios_parse();
		}
	}
	pr_info("The BIOS Version: %s\n", b_info.bios_version);

	efi_runtime_init();

	register_smp_ops(&loongson3_smp_ops);
	loongson_acpi_init();
}
```

对[ACPI](#3.4. ACPI（建议浏览一下 ACPI[手册](https://uefi.org/sites/default/files/resources/ACPI_6_3_final_Jan30.pdf)）)进一步分析：

首先分析重要的数据结构 RSDT，RSDT 分为 the header 和 data 两个部分，the header 是所有 SDT 共有的。

```c
struct acpi_table_header {
	// All the ACPI tables have a 4 byte Signature field (except the RSDP which has an 8 byte one).
	// Using the signature, you can determine what table are you working with.
	char signature[ACPI_NAME_SIZE];	/* ASCII table signature */
	u32 length;		/* Length of table in bytes, including this header */
	u8 revision;		/* ACPI Specification minor version number */
	u8 checksum;		/* To make sum of entire table == 0 */
	char oem_id[ACPI_OEM_ID_SIZE];	/* ASCII OEM identification */
	char oem_table_id[ACPI_OEM_TABLE_ID_SIZE];	/* ASCII OEM table identification */
	u32 oem_revision;	/* OEM revision number */
	char asl_compiler_id[ACPI_NAME_SIZE];	/* ASCII ASL compiler vendor ID */
	u32 asl_compiler_revision;	/* ASL compiler version */
};
```

这个函数并不是初始化 RSDT 的，而是初始化所有的 ACPI 表。

```c
void __init acpi_table_upgrade(void)
{
	void *data = (void *)initrd_start;
	size_t size = initrd_end - initrd_start;
	int sig, no, table_nr = 0, total_offset = 0;
	long offset = 0;
	struct acpi_table_header *table;
	char cpio_path[32] = "kernel/firmware/acpi/"; // bios获取到的数据
	struct cpio_data file;

	if (data == NULL || size == 0)
		return;

	for (no = 0; no < NR_ACPI_INITRD_TABLES; no++) {
		file = find_cpio_data(cpio_path, data, size, &offset);
		if (!file.data)
			break;

		data += offset;
		size -= offset;

		if (file.size < sizeof(struct acpi_table_header)) {
			pr_err("ACPI OVERRIDE: Table smaller than ACPI header [%s%s]\n",
				cpio_path, file.name);
			continue;
		}

		table = file.data; // file.data就是table，接下来初始化对应的ACPI表

		for (sig = 0; table_sigs[sig]; sig++) // 找到对应的ACPI表
			if (!memcmp(table->signature, table_sigs[sig], 4))
				break;

		if (!table_sigs[sig]) {
			pr_err("ACPI OVERRIDE: Unknown signature [%s%s]\n",
				cpio_path, file.name);
			continue;
		}
		if (file.size != table->length) {
			pr_err("ACPI OVERRIDE: File length does not match table length [%s%s]\n",
				cpio_path, file.name);
			continue;
		}
		// A 8-bit checksum field of the whole table, inclusive of the header.
		// All bytes of the table summed must be equal to 0 (mod 0x100).
		if (acpi_table_checksum(file.data, table->length)) {
			pr_err("ACPI OVERRIDE: Bad table checksum [%s%s]\n",
				cpio_path, file.name);
			continue;
		}

		pr_info("%4.4s ACPI table found in initrd [%s%s][0x%x]\n",
			table->signature, cpio_path, file.name, table->length);

		all_tables_size += table->length;
		acpi_initrd_files[table_nr].data = file.data; // 记录所有初始化的表信息
		acpi_initrd_files[table_nr].size = file.size;
		table_nr++;
	}
	if (table_nr == 0)
		return;

	acpi_tables_addr = // 为初始化的ACPI表分配物理地址
		memblock_find_in_range(0, ACPI_TABLE_UPGRADE_MAX_PHYS,
				       all_tables_size, PAGE_SIZE);
	if (!acpi_tables_addr) {
		WARN_ON(1);
		return;
	}
	/*
	 * Only calling e820_add_reserve does not work and the
	 * tables are invalid (memory got used) later.
	 * memblock_reserve works as expected and the tables won't get modified.
	 * But it's not enough on X86 because ioremap will
	 * complain later (used by acpi_os_map_memory) that the pages
	 * that should get mapped are not marked "reserved".
	 * Both memblock_reserve and e820__range_add (via arch_reserve_mem_area)
	 * works fine.
	 */
	memblock_reserve(acpi_tables_addr, all_tables_size); // 这里为什么要设为reserve还不清楚
	arch_reserve_mem_area(acpi_tables_addr, all_tables_size);

	/*
	 * early_ioremap only can remap 256k one time. If we map all
	 * tables one time, we will hit the limit. Need to map chunks
	 * one by one during copying the same as that in relocate_initrd().
	 */
	for (no = 0; no < table_nr; no++) { // 这里应该是将分配好的物理空间进行映射
		unsigned char *src_p = acpi_initrd_files[no].data;
		phys_addr_t size = acpi_initrd_files[no].size;
		phys_addr_t dest_addr = acpi_tables_addr + total_offset;
		phys_addr_t slop, clen;
		char *dest_p;

		total_offset += size;

		while (size) {
			slop = dest_addr & ~PAGE_MASK;
			clen = size;
			if (clen > MAP_CHUNK_SIZE - slop)
				clen = MAP_CHUNK_SIZE - slop;
			dest_p = early_memremap(dest_addr & PAGE_MASK,
						clen + slop);
			memcpy(dest_p + slop, src_p, clen);
			early_memunmap(dest_p, clen + slop);
			src_p += clen;
			dest_addr += clen;
			size -= clen;
		}
	}
}
```

按照注释，这个才是获取 RSDT 的，但为什么这里又要初始化一个各种表，和上一个函数有什么区别？

猜想：不是初始化其他表的，而是建立 RSDT 与其他表的关联，因为 RSDT 包含了所有指向其他系统表的指针。

```c
/*******************************************************************************
 *
 * FUNCTION:    acpi_initialize_tables
 *
 * PARAMETERS:  initial_table_array - Pointer to an array of pre-allocated
 *                                    struct acpi_table_desc structures. If NULL, the
 *                                    array is dynamically allocated.
 *              initial_table_count - Size of initial_table_array, in number of
 *                                    struct acpi_table_desc structures
 *              allow_resize        - Flag to tell Table Manager if resize of
 *                                    pre-allocated array is allowed. Ignored
 *                                    if initial_table_array is NULL.
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Initialize the table manager, get the RSDP and RSDT/XSDT.
 *
 * NOTE:        Allows static allocation of the initial table array in order
 *              to avoid the use of dynamic memory in confined environments
 *              such as the kernel boot sequence where it may not be available.
 *
 *              If the host OS memory managers are initialized, use NULL for
 *              initial_table_array, and the table will be dynamically allocated.
 *
 ******************************************************************************/

acpi_status ACPI_INIT_FUNCTION
acpi_initialize_tables(struct acpi_table_desc *initial_table_array,
		       u32 initial_table_count, u8 allow_resize)
{
	acpi_physical_address rsdp_address;
	acpi_status status;

	ACPI_FUNCTION_TRACE(acpi_initialize_tables);

	/*
	 * Setup the Root Table Array and allocate the table array
	 * if requested
	 */
	if (!initial_table_array) {
		status = acpi_allocate_root_table(initial_table_count);
		if (ACPI_FAILURE(status)) {
			return_ACPI_STATUS(status);
		}
	} else {
		/* Root Table Array has been statically allocated by the host */

		memset(initial_table_array, 0,
		       (acpi_size)initial_table_count *
		       sizeof(struct acpi_table_desc));

		acpi_gbl_root_table_list.tables = initial_table_array;
		acpi_gbl_root_table_list.max_table_count = initial_table_count;
		acpi_gbl_root_table_list.flags = ACPI_ROOT_ORIGIN_UNKNOWN;
		if (allow_resize) {
			acpi_gbl_root_table_list.flags |=
			    ACPI_ROOT_ALLOW_RESIZE;
		}
	}

	/* Get the address of the RSDP */

	rsdp_address = acpi_os_get_root_pointer();
	if (!rsdp_address) {
		return_ACPI_STATUS(AE_NOT_FOUND);
	}

	/*
	 * Get the root table (RSDT or XSDT) and extract all entries to the local
	 * Root Table Array. This array contains the information of the RSDT/XSDT
	 * in a common, more useable format.
	 */
	status = acpi_tb_parse_root_table(rsdp_address);
	return_ACPI_STATUS(status);
}
```



```c
static int __init numa_mem_init(int (*init_func)(void))
{
	int i;
	int ret;
	int node;

	for (i = 0; i < CONFIG_NR_CPUS; i++)
		set_cpuid_to_node(i, NUMA_NO_NODE);
	nodes_clear(numa_nodes_parsed); // 初始化前先清0
	nodes_clear(node_possible_map);
	nodes_clear(node_online_map);
	memset(&numa_meminfo, 0, sizeof(numa_meminfo));
	numa_default_distance(); // 初始化节点距离矩阵
	/* Parse SRAT and SLIT if provided by firmware. */
	ret = init_func();
	if (ret < 0)
		return ret;
	node_possible_map = numa_nodes_parsed;
	if (WARN_ON(nodes_empty(node_possible_map)))
		return -EINVAL;
	init_node_memblock(); // 逐个分析内存分布图并将结果通过add_mem_region()保存到loongson_mem_map中
	if (numa_meminfo_cover_memory(&numa_meminfo) == false)
		return -EINVAL;

	for_each_node_mask(node, node_possible_map) { // 建立逻辑CPU和节点的映射关系（CPU拓扑图）
		node_mem_init(node);					  // 描述哪个核属于哪个节点
		node_set_online(node);
		__node_data[(node)]->cpumask = cpus_on_node[node];
	}
	max_low_pfn = PHYS_PFN(memblock_end_of_DRAM());
	return 0;
}
```

###### 1.1.4 arch_mem_init()

源码分析：

```c
/*
 * arch_mem_init - initialize memory management subsystem
 *
 *  o plat_mem_setup() detects the memory configuration and will record detected
 *    memory areas using add_memory_region.
 *
 * At this stage the memory configuration of the system is known to the
 * kernel but generic memory management system is still entirely uninitialized.
 *
 *  o bootmem_init()
 *  o sparse_init()
 *  o paging_init()
 *  o dma_contiguous_reserve()
 *
 * At this stage the bootmem allocator is ready to use.
 *
 * NOTE: historically plat_mem_setup did the entire platform initialization.
 *	 This was rather impractical because it meant plat_mem_setup had to
 * get away without any kind of memory allocator.  To keep old code from
 * breaking plat_setup was just renamed to plat_mem_setup and a second platform
 * initialization hook for anything else was introduced.
 */

static void __init arch_mem_init(char **cmdline_p)
{
	unsigned int node;
	unsigned long start_pfn, end_pfn;
	struct memblock_region *reg;
	extern void plat_mem_setup(void);
#ifdef CONFIG_MACH_LOONGSON64
	bool enable;
#endif

	/* call board setup routine */
	plat_mem_setup(); // 初始化系统控制台——哑控制台，同时通过early_init_dt_scan_nodes()进行早期的FDT校验和初始化
	memblock_set_bottom_up(true);

	early_init_fdt_reserve_self();
	early_init_fdt_scan_reserved_mem();

	if (loongson_fdt_blob)
		dt_bootmem_init(); // 建立boot_mem_map内存映射图
						   // 这里应该不是建立，而是建立好了将其挂载到物理空间，所以关键还是找到boot_mem_map在哪里初始化的
	else
		bootmem_init();

	/*
	 * Prevent memblock from allocating high memory.
	 * This cannot be done before max_low_pfn is detected, so up
	 * to this point is possible to only reserve physical memory
	 * with memblock_reserve; memblock_virt_alloc* can be used
	 * only after this point
	 */
	memblock_set_current_limit(PFN_PHYS(max_low_pfn));

#ifdef CONFIG_PROC_VMCORE
	if (setup_elfcorehdr && setup_elfcorehdr_size) {
		printk(KERN_INFO "kdump reserved memory at %lx-%lx\n",
		       setup_elfcorehdr, setup_elfcorehdr_size);
		memblock_reserve(setup_elfcorehdr, setup_elfcorehdr_size);
	}
#endif

	loongarch_parse_crashkernel();
#ifdef CONFIG_KEXEC
	if (crashk_res.start != crashk_res.end)
		memblock_reserve(crashk_res.start,
				 crashk_res.end - crashk_res.start + 1);
#endif
	for_each_online_node(node) {
		get_pfn_range_for_nid(node, &start_pfn, &end_pfn);
		reserve_crashm_region(node, start_pfn, end_pfn);
		reserve_oldmem_region(node, start_pfn, end_pfn);
	}

	device_tree_init();// 解析和初始化设备树
#ifdef CONFIG_MACH_LOONGSON64
	enable = memblock_bottom_up();
	memblock_set_bottom_up(false);
#endif
	sparse_init(); // 初始化稀疏型内存模型
#ifdef CONFIG_MACH_LOONGSON64
	memblock_set_bottom_up(enable);
#endif
	plat_swiotlb_setup(); // swiotlb是一个纯软件中转站，用于让访存能力有限的IO设备能够访问任意的DMA空间

	dma_contiguous_reserve(PFN_PHYS(max_low_pfn));
	/* Tell bootmem about cma reserved memblock section */
	for_each_memblock(reserved, reg)
		if (reg->size != 0)
			memblock_reserve(reg->base, reg->size);
	reserve_nosave_region();
}
```

初始化设备树可以看[这里](http://sourcelink.top/2019/09/10/dts-unflatten_device_tree/)，分析的很详细。

plat_swiotlb_setup()

```c
void  __init
swiotlb_init(int verbose)
{
	size_t default_size = IO_TLB_DEFAULT_SIZE;
	unsigned char *vstart;
	unsigned long bytes;

	if (!io_tlb_nslabs) { // io_tlb_nslabs表示有多少个slab，一个slab是2K
		io_tlb_nslabs = (default_size >> IO_TLB_SHIFT);
		io_tlb_nslabs = ALIGN(io_tlb_nslabs, IO_TLB_SEGSIZE);
	}

	bytes = io_tlb_nslabs << IO_TLB_SHIFT;

	/* Get IO TLB memory from the low pages */
	vstart = memblock_virt_alloc_low_nopanic(PAGE_ALIGN(bytes), PAGE_SIZE); // 从低地址分配物理空间
	if (vstart && !swiotlb_init_with_tbl(vstart, io_tlb_nslabs, verbose)) // 进一步初始化
		return;

	if (io_tlb_start) { // 分配失败，释放物理内存
		memblock_free_early(io_tlb_start,
				    PAGE_ALIGN(io_tlb_nslabs << IO_TLB_SHIFT));
		io_tlb_start = 0;
	}
	pr_warn("Cannot allocate buffer");
	no_iotlb_memory = true;
}
```

###### 1.1.5 plat_smp_setup()

LoongArch 也使用 loongson3_smp_setup()进行初始化。

源码分析：

```c
const struct plat_smp_ops loongson3_smp_ops = {
	.send_ipi_single = loongson3_send_ipi_single, // 核间通讯
	.send_ipi_mask = loongson3_send_ipi_mask, 	  // 核间通讯
	.smp_setup = loongson3_smp_setup,			  // 主核启动
	.prepare_cpus = loongson3_prepare_cpus,
	.boot_secondary = loongson3_boot_secondary,	  // 辅核启动
	.init_secondary = loongson3_init_secondary,
	.smp_finish = loongson3_smp_finish,
#ifdef CONFIG_HOTPLUG_CPU						  // CPU热拔插
	.cpu_disable = loongson3_cpu_disable,
	.cpu_die = loongson3_cpu_die,
#endif
};
```

```c
static void __init loongson3_smp_setup(void)
{
	int i = 0, num = 0; /* i: physical id, num: logical id */

	if (acpi_disabled) {
		init_cpu_possible(cpu_none_mask);

		while (i < MAX_CPUS) {
			if (loongson_sysconf.reserved_cpus_mask & (0x1UL << i)) { // reserved_cpus_mask非0，该核不用
				/* Reserved physical CPU cores */
				__cpu_number_map[i] = -1;
			} else { // 建立CPU逻辑编号和物理编号的对应关系
				__cpu_number_map[i] = num;
				__cpu_logical_map[num] = i;
				set_cpu_possible(num, true);
				num++;
			}
			i++;
		}
		pr_info("Detected %i available CPU(s)\n", num);

		while (num < MAX_CPUS) {
			__cpu_logical_map[num] = -1;
			num++;
		}
	}

	ipi_method_init(); // ipi（核间中断）初始化
	ipi_set_regs_init();
	ipi_clear_regs_init();
	ipi_status_regs_init();
	ipi_en_regs_init();
	ipi_mailbox_buf_init();

	if (cpu_has_csripi)
		iocsr_writel(0xffffffff, LOONGARCH_IOCSR_IPI_EN);
	else
		xconf_writel(0xffffffff, ipi_en_regs[cpu_logical_map(0)]);

	cpu_set_core(&cpu_data[0],
		     cpu_logical_map(0) % loongson_sysconf.cores_per_package);
	cpu_set_cluster(&cpu_data[0],
		     cpu_logical_map(0) / loongson_sysconf.cores_per_package);
	cpu_data[0].package = cpu_logical_map(0) / loongson_sysconf.cores_per_package; // 确定主核的封装编号和核编号
}
```

###### 1.1.6 paging_init()

源码分析：

```c
void __init free_area_init_nodes(unsigned long *max_zone_pfn)
{
	unsigned long start_pfn, end_pfn;
	int i, nid;

	/* Record where the zone boundaries are */
	memset(arch_zone_lowest_possible_pfn, 0,
				sizeof(arch_zone_lowest_possible_pfn));
	memset(arch_zone_highest_possible_pfn, 0,
				sizeof(arch_zone_highest_possible_pfn));

	start_pfn = find_min_pfn_with_active_regions();

	for (i = 0; i < MAX_NR_ZONES; i++) {
		if (i == ZONE_MOVABLE)
			continue;

		end_pfn = max(max_zone_pfn[i], start_pfn); // an array of max PFNs for each zone
		arch_zone_lowest_possible_pfn[i] = start_pfn;
		arch_zone_highest_possible_pfn[i] = end_pfn;

		start_pfn = end_pfn;
	}

	/* Find the PFNs that ZONE_MOVABLE begins at in each node */
	memset(zone_movable_pfn, 0, sizeof(zone_movable_pfn));
	find_zone_movable_pfns_for_nodes();

	/* Print out the zone ranges */
	pr_info("Zone ranges:\n");
	for (i = 0; i < MAX_NR_ZONES; i++) {
		if (i == ZONE_MOVABLE)
			continue;
		pr_info("  %-8s ", zone_names[i]);
		if (arch_zone_lowest_possible_pfn[i] ==
				arch_zone_highest_possible_pfn[i])
			pr_cont("empty\n"); // If the maximum PFN between two adjacent zones match,
		else					// it is assumed that the zone is empty.
			pr_cont("[mem %#018Lx-%#018Lx]\n",
				(u64)arch_zone_lowest_possible_pfn[i]
					<< PAGE_SHIFT,
				((u64)arch_zone_highest_possible_pfn[i]
					<< PAGE_SHIFT) - 1);
	}

	/* Print out the PFNs ZONE_MOVABLE begins at in each node */
	pr_info("Movable zone start for each node\n");
	for (i = 0; i < MAX_NUMNODES; i++) {
		if (zone_movable_pfn[i])
			pr_info("  Node %d: %#018Lx\n", i,
			       (u64)zone_movable_pfn[i] << PAGE_SHIFT);
	}

	/* Print out the early node map */
	pr_info("Early memory node ranges\n");
	for_each_mem_pfn_range(i, MAX_NUMNODES, &start_pfn, &end_pfn, &nid)
		pr_info("  node %3d: [mem %#018Lx-%#018Lx]\n", nid,
			(u64)start_pfn << PAGE_SHIFT,
			((u64)end_pfn << PAGE_SHIFT) - 1);

	/* Initialise every node */
	mminit_verify_pageflags_layout();
	setup_nr_node_ids();
	zero_resv_unavail();
	for_each_online_node(nid) {
		pg_data_t *pgdat = NODE_DATA(nid);
		free_area_init_node(nid, NULL,
				find_min_pfn_for_node(nid), NULL);

		/* Any memory on that node */
		if (pgdat->node_present_pages)
			node_set_state(nid, N_MEMORY);
		check_for_memory(pgdat, nid);
	}
}
```

##### 1.2 trap_init()

```plain
trap_init()
| -- set_handler(); // 将不同的trap_handler加载到对应的内存位置
|	| -- memcpy(); // 每个handler大小为vec_size，所以要EXCCODE * vec_size
|
| -- cache_error_setup(); // Install uncached CPU exception handler
|	| -- set_merr_handler(); // except_vec_cex就是cache exception handler
```

还有很多异常，如 PSI, HYP, GCM 等，为什么没有设置 handler？

应该是 set_handler()只负责设置 cpu exception handler.

```c
void __init trap_init(void)
{
	unsigned long i;

	/*
	 * Initialise exception handlers
	 */
	for (i = 0; i < 64; i++)
		set_handler(i * vec_size, handle_reserved, vec_size);

	set_handler(EXCCODE_TLBL * vec_size, handle_tlbl, vec_size); // TLB miss on a load
	set_handler(EXCCODE_TLBS * vec_size, handle_tlbs, vec_size); // TLB miss on a store
	set_handler(EXCCODE_TLBI * vec_size, handle_tlbl, vec_size); // TLB miss on a ifetch(what is ifetch)
	set_handler(EXCCODE_TLBM * vec_size, handle_tlbm, vec_size); // TLB modified fault
	set_handler(EXCCODE_TLBRI * vec_size, tlb_do_page_fault_rixi, vec_size); // TLB Read-Inhibit exception
	set_handler(EXCCODE_TLBXI * vec_size, tlb_do_page_fault_rixi, vec_size); // TLB Execution-Inhibit exception
	set_handler(EXCCODE_ADE * vec_size, handle_ade, vec_size); // Address Error
	set_handler(EXCCODE_UNALIGN * vec_size, handle_unalign, vec_size); // Unalign Access
	set_handler(EXCCODE_SYS * vec_size, handle_sys_wrap, vec_size); // System call
	set_handler(EXCCODE_BP * vec_size, handle_bp, vec_size); // Breakpoint
	set_handler(EXCCODE_INE * vec_size, handle_ri, vec_size); // Inst. Not Exist
	set_handler(EXCCODE_IPE * vec_size, handle_ri, vec_size); // Inst. Privileged Error
	set_handler(EXCCODE_FPDIS * vec_size, handle_fpdis, vec_size); // FPU Disabled
	set_handler(EXCCODE_LSXDIS * vec_size, handle_lsx, vec_size); // LSX Disabled
	set_handler(EXCCODE_LASXDIS * vec_size, handle_lasx, vec_size); // LASX Disabled
	set_handler(EXCCODE_FPE * vec_size, handle_fpe, vec_size); // Floating Point Exception
	set_handler(EXCCODE_BTDIS * vec_size, handle_lbt, vec_size); // Binary Trans. Disabled
	set_handler(EXCCODE_WATCH * vec_size, handle_watch, vec_size); // Watch address reference

	cache_error_setup(); // Install uncached CPU exception handler

	local_flush_icache_range(ebase, ebase + 0x400);

	sort_extable(__start___dbe_table, __stop___dbe_table);
}
```

```c
/* Install CPU exception handler */
void set_handler(unsigned long offset, void *addr, unsigned long size)
{
	memcpy((void *)(ebase + offset), addr, size);
	local_flush_icache_range(ebase + offset, ebase + offset + size);
}
```

```c
/*
 * Install uncached CPU exception handler.
 * This is suitable only for the cache error exception which is the only
 * exception handler that is being run uncached.
 */
void set_merr_handler(unsigned long offset, void *addr, unsigned long size)
{
	unsigned long uncached_ebase = TO_UNCAC(__pa(merror_ebase));

	if (!addr)
		panic(panic_null_cerr);

	memcpy((void *)(uncached_ebase + offset), addr, size);
}
```



### 三、相关知识

#### 3.1. [BSS 段清 0](https://www.cnblogs.com/lvzh/p/12079365.html)

BSS 段是保存全局变量和静态局部变量的，因为这两种数据的位置是固定的，所有可以直接保存在 BSS 里，局部变量是保存在栈上。在初始化内核时一次性将 BSS 所有变量初始化为 0 更方便。

#### 3.2. efi

EFI 系统分区（EFI system partition，ESP），是一个[FAT](https://zh.wikipedia.org/wiki/FAT)或[FAT32](https://zh.wikipedia.org/wiki/FAT32)格式的磁盘分区。UEFI 固件可从 ESP 加载 EFI 启动程式或者 EFI 应用程序。

#### 3.3. [cpio](https://unix.stackexchange.com/questions/7276/why-use-cpio-for-initramfs)

cpio 是 UNIX 操作系统的一个文件备份程序及文件格式。

The initial ramdisk needs to be unpacked by the kernel during boot, cpio is used because it is already implemented in kernel code.

All 2.6 Linux kernels **contain a gzipped "cpio" format archive,** which is extracted into rootfs when the kernel boots up.  After extracting, the kernel
checks to see if rootfs contains a file "init", and if so it executes it as PID. If found, this init process is responsible for bringing the system the rest of the way up, including locating and mounting the real root device (if any).  If rootfs does not contain an init program after the embedded cpio archive is extracted into it, the kernel will fall through to the older code to locate and mount a root partition, then exec some variant of /sbin/init
out of that.

#### 3.4. ACPI（建议浏览一下 ACPI[手册](https://uefi.org/sites/default/files/resources/ACPI_6_3_final_Jan30.pdf)）

Advanced Configuration and Power Interface (ACPI). Before the development of ACPI, operating systems (OS) primarily used BIOS (Basic Input/
Output System) interfaces for **power management and device discovery and configuration**.

ACPI can first be understood as an architecture-independent power management and configuration framework that forms a subsystem within the host OS. This framework **establishes a hardware register set to define power states** (sleep, hibernate, wake, etc). The hardware register set can accommodate operations on dedicated hardware and general purpose hardware.

The primary intention of the standard ACPI framework and the hardware register set is to enable power management and system configuration without directly calling firmware natively from the OS. **ACPI serves as an interface layer between the system firmware (BIOS) and the OS**.

There are 2 main parts to ACPI. **The first part** is the tables used by the OS for configuration during boot (these include things like how many CPUs, APIC details, NUMA memory ranges, etc). The second part is the run time ACPI environment, which consists of AML code (a platform independent OOP language that comes from the BIOS and devices) and the ACPI SMM (System Management Mode) code.

关键数据结构：

**RSDT** (Root System Description Table) is a data structure used in the [ACPI](https://wiki.osdev.org/ACPI) programming interface. This table contains pointers to all the other System Description Tables. However there are many kinds of SDT. All the SDT may be split into two parts. One (**the header**) which is common to all the SDT and another (data) which is different for each table. RSDT contains 32-bit physical addresses, XSDT contains 64-bit physical addresses.

#### 3.5. [NUMA](https://zhuanlan.zhihu.com/p/62795773)

NUMA 指的是针对某个 CPU，内存访问的距离和时间是不一样的。其解决了多 CPU 系统下共享 BUS 带来的性能问题（链接中的图很直观）。

NUMA 的特点是：被共享的内存物理上是分布式的，所有这些内存的集合就是全局地址空间。所以处理器访问这些内存的时间是不一样的，显然访问本地内存的速度要比访问全局共享内存或远程访问外地内存要快些。

#### 3.6. initrd

Initrd ramdisk 或者 initrd 是指一个临时文件系统，它在启动阶段被 Linux 内核调用。initrd 主要用于当“根”文件系统被[挂载](https://zh.wikipedia.org/wiki/Mount_(Unix))之前，进行准备工作。

#### 3.7. initramfs

#### 3.8. [设备树（dt）](https://e-mailky.github.io/2019-01-14-dts-1)

Device Tree 由一系列被命名的结点（node）和属性（property）组成，而结点本身可包含子结点。所谓属性， 其实就是成对出现的 name 和 value。在 Device Tree 中，可描述的信息包括（原先这些信息大多被 hard code 到 kernel 中）：

- CPU 的数量和类别
- 内存基地址和大小
- 总线和桥
- 外设连接
- 中断控制器和中断使用情况
- GPIO 控制器和 GPIO 使用情况
- Clock 控制器和 Clock 使用情况

它基本上就是画一棵电路板上 CPU、总线、设备组成的树，**Bootloader 会将这棵树传递给内核**，然后内核可以识别这棵树， 并根据它**展开出 Linux 内核中的**platform_device、i2c_client、spi_device 等**设备**，而这些设备用到的内存、IRQ 等资源， 也被传递给了内核，内核会将这些资源绑定给展开的相应的设备。

是否 Device Tree 要描述系统中的所有硬件信息？答案是否定的。基本上，那些可以动态探测到的设备是不需要描述的， 例如 USB device。不过对于 SOC 上的 usb hostcontroller，它是无法动态识别的，需要在 device tree 中描述。同样的道理， 在 computersystem 中，PCI device 可以被动态探测到，不需要在 device tree 中描述，但是 PCI bridge 如果不能被探测，那么就需要描述之。

设备树和 ACPI 有什么关系？

#### 3.9. [BootMem 内存分配器](https://cloud.tencent.com/developer/article/1376122)

**[Bootmem](https://www.kernel.org/doc/html/v4.19/core-api/boot-time-mm.html#bootmem) is a boot-time physical memory allocator and configurator**.

It is used early in the boot process before the page allocator is set up.

Bootmem is based on the most basic of allocators, a First Fit allocator which uses a bitmap to represent memory. If a bit is 1, the page is allocated and 0 if unallocated. To satisfy allocations of sizes smaller than a page, the allocator records the **Page Frame Number (PFN)** of the last allocation and the offset the allocation ended at. Subsequent small allocations are merged together and stored on the same page.

The information used by the bootmem allocator is represented by `struct bootmem_data`. An array to hold up to `MAX_NUMNODES` such structures is statically allocated and then it is discarded when the system initialization completes. **Each entry in this array corresponds to a node with memory**. For UMA systems only entry 0 is used.

The bootmem allocator is initialized during early architecture specific setup. Each architecture is required to supply a `setup_arch()` function which, among other tasks, is responsible for acquiring the necessary parameters to initialise the boot memory allocator. These parameters define limits of usable physical memory:

- **min_low_pfn** - the lowest PFN that is available in the system
- **max_low_pfn** - the highest PFN that may be addressed by low memory (`ZONE_NORMAL`)
- **max_pfn** - the last PFN available to the system.

After those limits are determined, the `init_bootmem()` or `init_bootmem_node()` function should be called to initialize the bootmem allocator. The UMA case should use the init_bootmem function. It will initialize `contig_page_data` structure that represents the only memory node in the system. In the NUMA case the `init_bootmem_node` function should be called to initialize the bootmem allocator for each node.

Once the allocator is set up, it is possible to use either single node or NUMA variant of the allocation APIs.

现在的 bootmem 初始化是用的 memblock，详细看这个。

LoongArch 的 bootmem 似乎和 x86 的不一样，以下为 x86 的 bootmem 初始化过程。

bootmem_data 结构：

```c
/**
 * struct bootmem_data - per-node information used by the bootmem allocator
 * @node_min_pfn: the starting physical address of the node's memory
 * @node_low_pfn: the end physical address of the directly addressable memory
 * @node_bootmem_map: is a bitmap pointer - the bits represent all physical
 *		      memory pages (including holes) on the node.
 * @last_end_off: the offset within the page of the end of the last allocation;
 *                if 0, the page used is full
 * @hint_idx: the PFN of the page used with the last allocation;
 *            together with using this with the @last_end_offset field,
 *            a test can be made to see if allocations can be merged
 *            with the page used for the last allocation rather than
 *            using up a full new page.
 * @list: list entry in the linked list ordered by the memory addresses
 */
typedef struct bootmem_data {
	unsigned long node_min_pfn;
	unsigned long node_low_pfn;
	void *node_bootmem_map;
	unsigned long last_end_off;
	unsigned long hint_idx;
	struct list_head list;
} bootmem_data_t;
```

bootmem 的需求是简单，因此使用 first fit 的方式。该分配器使用一个位图来管理页，位图中的 bit 数等于物理页数，bit 为 1，表示该页使用；bit 为 0，表示该页未用。在需要分配内存时，bootmem 逐位扫描位图，知道找到一个空间足够大的连续页的位置。这种每次分配都需要从头扫描的方式效率不高，因此内核初始化结束后就转用伙伴系统（连同 slab、slub 或 slob 分配器）。

NUMA 内存体系中，每个节点都要初始化一个 bootmem 分配器。

开始时位图中的 bit 都是 1，根据 BIOS 提供的可用内存区的列表，释放所有可用的内存页。由于 bootmem 需要一些内存页保存位图，必须先调用 reserve_bootmem 分配这些内存页（ACPI 数据和 SMP 启动时的配置也是通过 reserve_bootmem 保存的）。

在停用 bootmem 时，需要扫描位图释放每个未使用的页，释放完后，位图所在的页也要释放。

#### 3.10. [SWIOTLB](https://blog.csdn.net/liuhangtiant/article/details/87825466)

龙芯 3 号的访存能力是 48 位，而龙芯的顶级 IO 总线是 40 位的，部分 PCI 设备的总线只有 32 位，如果系统为其分配了超过 40 位或 32 位总线寻址能力的地址，那么这些设备就不能访问对应的 DMA 数据，为了让访存能力有限的 IO 设备能够访问任意的 DMA 空间，就必须在硬件上设置一个 DMA 地址-物理地址的映射表，或者由内核在设备可访问的地址范围预先准备一款内存做中转站——SWIOTLB。

#### 3.11. IOMMU

 **Input–output memory management unit** (**IOMMU**) is a memory management unit (MMU) that **connects a direct-memory-access–capable (DMA-capable) I/O bus to the main memory**. Like a traditional MMU, which translates CPU-visible virtual addresses to physical addresses, the IOMMU maps device-visible virtual addresses (also called *device addresses* or *I/O addresses* in this context) to physical addresses. Some units also provide memory protection from faulty or malicious devices. It's function is same as SWIOTLB.

#### 3.12. 节点

系统的物理内存被划分为几个节点（node)，每个节点的物理内存又分为一个管理区（zone）:

- ZONE_DMA: 包含低于 16MB 的内存页框；
- ZONE_MORMAL: 包含高于 16MB 且低于 896MB 的内存页框；
- ZONE_HIGHMEM: 包含从 896MB 开始的内存页框。

#### 3.13. LSX 和 LASZ

龙芯架构下的向量扩展指令，其包括向量扩展（Loongson SIMD Extension, LSX）和高级向量扩展（Loongson Advanced SIMD Extension, LASX）。两个扩展部分均采用 SIMD 指令且功能基本一致，区别是 LSX 操作的向量位宽是 128 位，而 LASX 是 256 位。两者的关系类似与 xmm 和 mmx。

问题：

（1）正常在 LA 架构上运行 LA 内核是这样的，那如果在 LA 架构上运行 x86 内核是怎样的，BootLoader 直接传递 x86 内核的入口地址么。bios 要怎样把 LA 内核拉起来。

（2）源码要结合书一起看，而且要多找即本书，对比着看，因为有些内容，如 ACPI，bootmem 不是所有的书都会详细介绍。我用到的参考书有《基于龙芯的 Linux 内核探索解析》、《深入理解 LINUX 内核》、《深入 LINUX 内核架构》。
