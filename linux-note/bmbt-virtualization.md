## bmbt 系统级虚拟化

### 零、虚拟化知识

VMM 对物理资源的虚拟可以归结为三个主要任务：处理器虚拟化、内存虚拟化和 I/O 虚拟化。

#### 1. 可虚拟化架构与不可虚拟化架构

系统级的虚拟机由于 VMM 直接运行在 bare metal 上，整体可以分为 3 个部分：

![image-20211007102918303](/home/guanshun/.config/Typora/typora-user-images/image-20211007102918303.png)

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

[在 qemu 上调试 LoongArch 内核](https://github.com/UtopianFuture/UtopianFuture.github.io/blob/master/linux-note/%E5%9C%A8qemu%E4%B8%8A%E8%B0%83%E8%AF%95loongson%E5%86%85%E6%A0%B8.md)

#### 2. 内核启动过程

主核的执行入口（PC 寄存器的初始值）是编译内核时决定的，运行时由 BIOS 或者 BootLoader 传递给内核。内核的初始入口是`kernel_entry`。LA 的`kernel_entry`和 mips 的类似，进行.bss 段的清 0（为什么要清 0），保存 a0~a3 等操作。之后就进入到第二入口`start_kernel()`。

通过 gdb 单步调试看 LA 内核是怎样初始化的。但是遇到一个问题，内核使用`-O2`优化项，在单步调试时很多值都是`optimized out`，同时设置断点也不会顺序执行，是跳着执行的，给阅读代码带来困难。后来请教师兄，这是正常的，start_kernel()部分的代码可以直接看源码，不用单步调试。


### 二、源码阅读

#### 1. start_kernel()

start_kernel()中一级节点中，先从架构相关的`setup_arch()`开始看。代码中涉及的技术都在之后有介绍。

##### 1.1 setup_arch()

架构相关，代码和 mips 类似，下为代码树展开。

```plain
setup_arch()
| -- cpu_probe(); // 探测cpu类型，写入cputype中
|
| -- plat_early_init(); // 解析bios传入的参数
|	| -- fw_init_cmdline(); // 获取参数
|	| -- prom_init_env(); // 根据参数设置环境变量
|
| -- init_initrd(); // 主要是检查initrd_start和initrd_end是否正确，将其映射到虚拟地址
|
| -- prom_init(); // 初始化io空间的基址、ACPI表、loongarch使用的numa存储等
|	| -- set_io_port_base(); // 设置IO空间的基址
|	| -- if(efi_bp){} // efi_bp是在prom_init_env()中用bios传递的_fw_envp赋值的，之后进行ACPI初始化
|	| -- prom_init_numa_memory();
|		| -- numa_mem_init(); // 初始化numa
|			| -- numa_default_distance(); // 初始化numa节点的距离矩阵
|			| -- init_node_memblock(); // 逐个分析内存分布图并将结果通过add_mem_region()保存到loongson_mem_map中
|	| -- loongson_acpi_init(); // ACPI初始化始终是个大问题，需要进一步了解ACPI才能看懂
|
| -- cpu_report(); // 打印一些初始化后CPU的信息
|
| -- arch_mem_init(); //主要是初始化设备树和bootmem
|	| -- early_init_dt_scan(); // 早期初始化设备树
|	| -- dt_bootmem_init(); // 建立boot_mem_map内存映射图，boot_mem_map主要给BootMem内存分配器用，只包含系统内存
|	| -- device_tree_init(); // 用bios传递的信息初始化设备树节点
|		| -- unflatten_and_copy_device_tree();
|			| -- early_init_dt_alloc_memory_arch(); // 先在初始化好的bootmem中分配物理空间
|			| -- unflatten_device_tree(); // create tree of device_nodes from flat blob
|
|	| -- sparse_init(); // 初始化稀疏型内存模型
|
|	| -- plat_swiotlb_setup(); // swiotlb为软件中转站，用于让任意设备能够对任意内存地址发起DMA访问。
| 							   // 要保证弱寻址能力的设备能够访问，所有尽早初始化
|
|	| -- resource_init(); // 在已经初始化的bootmem中为code, date, bss段分配空间
|
|	| -- plat_smp_setup(); // smp是多对称处理器，这里先配置祝贺
|
|
|
|
|
\
```

###### 1.1.1 cpu_probe()

源码分析：

```plain
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

```plain
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

```plain
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

###### 1.1.3 prom_init()

源码分析：

```plain
void __init prom_init(void)
{
	/* init base address of io space */
	set_io_port_base((unsigned long) // ioremap()获取到io base的物理地址后set_io_port_base将其赋值给全局变量loongarch_io_port_base
		ioremap(LOONGSON_LIO_BASE, LOONGSON_LIO_SIZE));

	if (efi_bp) { // efi_bp是在prom_init_env()中用bios传递的_fw_envp赋值的
		efi_init(); // 为什么要初始化efi，efi和acpi有什么关系？
#if defined(CONFIG_ACPI) && defined(CONFIG_BLK_DEV_INITRD)
		acpi_table_upgrade(); // 这部分初始化看不懂，为什么要从cpio中获取数据
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

```plain
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

```plain
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

```
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



### 三、相关知识

#### 1. [BSS 段清 0](https://www.cnblogs.com/lvzh/p/12079365.html)

BSS 段是保存全局变量和静态局部变量的，因为这两种数据的位置是固定的，所有可以直接保存在 BSS 里，局部变量是保存在栈上。在初始化内核时一次性将 BSS 所有变量初始化为 0 更方便。

#### 2. efi

EFI 系统分区（EFI system partition，ESP），是一个[FAT](https://zh.wikipedia.org/wiki/FAT)或[FAT32](https://zh.wikipedia.org/wiki/FAT32)格式的磁盘分区。UEFI 固件可从 ESP 加载 EFI 启动程式或者 EFI 应用程序。

#### 3. [cpio](https://unix.stackexchange.com/questions/7276/why-use-cpio-for-initramfs)

cpio 是 UNIX 操作系统的一个文件备份程序及文件格式。

The initial ramdisk needs to be unpacked by the kernel during boot, cpio is used because it is already implemented in kernel code.

All 2.6 Linux kernels **contain a gzipped "cpio" format archive,** which is extracted into rootfs when the kernel boots up.  After extracting, the kernel
checks to see if rootfs contains a file "init", and if so it executes it as PID. If found, this init process is responsible for bringing the system the rest of the way up, including locating and mounting the real root device (if any).  If rootfs does not contain an init program after the embedded cpio archive is extracted into it, the kernel will fall through to the older code to locate and mount a root partition, then exec some variant of /sbin/init
out of that.

#### 4. ACPI（建议浏览一下 ACPI[手册](https://uefi.org/sites/default/files/resources/ACPI_6_3_final_Jan30.pdf)）

Advanced Configuration and Power Interface (ACPI). Before the development of ACPI, operating systems (OS) primarily used BIOS (Basic Input/
Output System) interfaces for **power management and device discovery and configuration**.

ACPI can first be understood as an architecture-independent power management and configuration framework that forms a subsystem within the host OS. This framework **establishes a hardware register set to define power states** (sleep, hibernate, wake, etc). The hardware register set can accommodate operations on dedicated hardware and general purpose hardware.

The primary intention of the standard ACPI framework and the hardware register set is to enable power management and system configuration without directly calling firmware natively from the OS. **ACPI serves as an interface layer between the system firmware (BIOS) and the OS**.

#### 5. [NUMA](https://zhuanlan.zhihu.com/p/62795773)

NUMA 指的是针对某个 CPU，内存访问的距离和时间是不一样的。其解决了多 CPU 系统下共享 BUS 带来的性能问题（链接中的图很直观）。

NUMA 的特点是：被共享的内存物理上是分布式的，所有这些内存的集合就是全局地址空间。所以处理器访问这些内存的时间是不一样的，显然访问本地内存的速度要比访问全局共享内存或远程访问外地内存要快些。

#### 6. initrd

Initrd ramdisk 或者 initrd 是指一个临时文件系统，它在启动阶段被 Linux 内核调用。initrd 主要用于当“根”文件系统被[挂载](https://zh.wikipedia.org/wiki/Mount_(Unix))之前，进行准备工作。

#### 7. initramfs

#### 8. [设备树（dt）](https://e-mailky.github.io/2019-01-14-dts-1)

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

#### 9. [BootMem 内存分配器](https://cloud.tencent.com/developer/article/1376122)

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

#### 10. [SWIOTLB](https://blog.csdn.net/liuhangtiant/article/details/87825466)

龙芯3号的访存能力是48位，而龙芯的顶级IO总线是40位的，部分PCI设备的总线只有32位，如果系统为其分配了超过40位或32位总线寻址能力的地址，那么这些设备就不能访问对应的DMA数据，为了让访存能力有限的IO设备能够访问任意的DMA空间，就必须在硬件上设置一个DMA地址-物理地址的映射表，或者由内核在设备可访问的地址范围预先准备一款内存做中转站——SWIOTLB。

#### 11. IOMMU

#### 12. 节点

问题：

（1）正常在 LA 架构上运行 LA 内核是这样的，那如果在 LA 架构上运行 x86 内核是怎样的，BootLoader 直接传递 x86 内核的入口地址么。bios 要怎样把 LA 内核拉起来。
