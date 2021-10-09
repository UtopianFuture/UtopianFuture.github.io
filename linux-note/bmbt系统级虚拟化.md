## bmbt系统级虚拟化

### 零、虚拟化知识

VMM对物理资源的虚拟可以归结为三个主要任务：处理器虚拟化、内存虚拟化和I/O虚拟化。

#### 1. 可虚拟化架构与不可虚拟化架构

系统级的虚拟机由于VMM直接运行在bare metal上，整体可以分为3个部分：

![image-20211007102918303](/home/guanshun/.config/Typora/typora-user-images/image-20211007102918303.png)

在我们的项目中，虚拟机暂时是ubuntu，虚拟机监控器是bmbt，硬件是LoongArch。

敏感指令：操作特权资源的指令，包括修改虚拟机的运行模式或物理机的状态，访存等等。所有的特权指令都是敏感指令，但不是所有的敏感指令都是特权指令。为了VMM能够完全控制物理资源，敏感指令必须在VMM的监督下才能运行，或者经由VMM完成。如果一个系统所有的敏感指令都是特权指令，那么只需要将VMM运行在系统的最高特权级上，而guest系统运行在非最高特权级上，在执行敏感指令时，陷入到VMM中。而如果像x86架构一样，不是所有的敏感指令都是特权指令，那么我们就说这种架构存在虚拟化漏洞，是不可虚拟化架构。这种架构需要采用一些辅助方法，如在硬件层面填补虚拟化漏洞——VT；通过软件方法避免使用无法陷入的敏感指令。

问题：BMBT要怎样解决x86的虚拟化漏洞？

#### 2. 处理器虚拟化

##### 2.1 指令模拟

​	guest运行的架构可能不是原架构，如ubuntu只能运行在x86架构上，如何使其在LoongArch架构的CPU上运行却察觉不到和x86架构有区别，换句话说，其运行的处理器和VMM需要提供与其“期望”的处理器一致的功能和行为。典型的“期望”包括：

（1）指令集和与执行效果。

（2）可用寄存器集合，包括通用寄存器以及各种系统寄存器。

（3）运行模式，如实模式、保护模式和64位长模式等。处理器的运行模式决定了指令执行的效果、寻址宽度与限制以及保护粒度等。

（4）地址翻译系统，如页表级数。

（5）保护机制，如分段和分页等。

（6）中断/异常机制，如虚拟处理器必须能够正确模拟真实处理器的行为，在错误的执行条件下，为虚拟机注入一个虚拟的异常。

##### 2.2 中断和异常的模拟及注入

​	VMM对于异常的虚拟化需要完全按照物理处理器对于各种异常条件的定义，再根据虚拟处理器当时的内容，来判断是否需要模拟出一个虚拟的异常，并注入到guest中。

##### 2.3 SMP模拟

​	底层有N个物理CPU，能够虚拟出M个虚拟CPU。

问题：

1. bmbt如何达到这一功能，即让ubuntu运行在LoongArch架构的CPU上运行却察觉不到和x86架构有区别？仿照QEMU？
2. 如何解决虚拟寄存器，上下文切换和虚拟处理器？之前看《LINUX系统虚拟化》了解到，虚拟CPU就是定义一个结构体来表示，如KVM中的VMCS，QEMU中的X86State等，之后所有的操作都是对这个结构体进行操作，bmbt也是这样实现的么？
3. 当guest要访问敏感资源时，要陷入到VMM，如何陷入？
4. 二进制翻译是用latx还是用QEMU的tcg？

#### 3. 内存虚拟化(见[linux系统虚拟化](https://github.com/UtopianFuture/UtopianFuture.github.io/blob/master/linux-note/linux%E7%B3%BB%E7%BB%9F%E8%99%9A%E6%8B%9F%E5%8C%96.md))

问题：

（1）内存分配算法怎样设计？

#### 4. I/O虚拟化(见[linux系统虚拟化](https://github.com/UtopianFuture/UtopianFuture.github.io/blob/master/linux-note/linux%E7%B3%BB%E7%BB%9F%E8%99%9A%E6%8B%9F%E5%8C%96.md))

​	现实中I/O资源是有限的额，为了满足多个gues对I/O的访问需求，VMM必须通过I/O虚拟化的方式复用有限的I/O资源。

问题：

（1）I/O虚拟化的设计思想是什么？目前进展怎么样？

### 一、调试LA内核

#### 1. 内核启动过程

主核的执行入口（PC寄存器的初始值）是编译内核时决定的，运行时由BIOS或者BootLoader传递给内核。内核的初始入口是`kernel_entry`。LA的`kernel_entry`和mips的类似，进行.bss段的清0（为什么要清0），保存a0~a3等操作。之后就进入到第二入口`start_kernel()`。

通过gdb单步调试看LA内核是怎样初始化的。但是遇到一个问题，内核使用`-O2`优化项，在单步调试时很多值都是`optimized out`，同时设置断点也不会顺序执行，是跳着执行的，给阅读代码带来困难。后来请教师兄，这是正常的，start_kernel()部分的代码可以直接看源码，不用单步调试。

2. 

### 二、源码阅读

#### 1. start_kernel()

start_kernel()中一级节点中，先从架构相关的`setup_arch()`开始看。代码中涉及的技术都在之后有介绍。

##### 1.1 setup_arch()

架构相关，代码和mips类似，下为代码树展开。

```
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
|	| -- set_io_port_base();
|	| -- if(efi_bp){} // efi_bp是在prom_init_env()中用bios传递的_fw_envp赋值的，之后进行ACPI初始化
|	| -- prom_init_numa_memory();
|
|
|
|
|
|
|
```



###### 1.1.1 cpu_probe()

源码分析：

```
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

```
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

```
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

```
void __init prom_init(void)
{
	/* init base address of io space */
	set_io_port_base((unsigned long) // ioremap()获取到io base的物理地址后set_io_port_base将其赋值给全局变量loongarch_io_port_base
		ioremap(LOONGSON_LIO_BASE, LOONGSON_LIO_SIZE));

	if (efi_bp) { // efi_bp是在prom_init_env()中用bios传递的_fw_envp赋值的
		efi_init(); // 为什么要初始化efi，efi和acpi有什么关系？
#if defined(CONFIG_ACPI) && defined(CONFIG_BLK_DEV_INITRD)
		acpi_table_upgrade(); // don't understand
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
	prom_init_numa_memory();
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



### 三、相关知识

1. [BSS段清0](https://www.cnblogs.com/lvzh/p/12079365.html)

   BSS段是保存全局变量和静态局部变量的，因为这两种数据的位置是固定的，所有可以直接保存在BSS里，局部变量是保存在栈上。在初始化内核时一次性将BSS所有变量初始化为0更方便。

2. efi

   EFI系统分区（EFI system partition，ESP），是一个[FAT](https://zh.wikipedia.org/wiki/FAT)或[FAT32](https://zh.wikipedia.org/wiki/FAT32)格式的磁盘分区。UEFI固件可从ESP加载EFI启动程式或者EFI应用程序。

3. NUMA

4. initrd

   Initrd ramdisk或者initrd是指一个临时文件系统，它在启动阶段被Linux内核调用。initrd主要用于当“根”文件系统被[挂载](https://zh.wikipedia.org/wiki/Mount_(Unix))之前，进行准备工作。

5. 设备树（fds）

6. SWIOTLB

7. IOMMU

8. 

问题：

（1）正常在LA架构上运行LA内核是这样的，那如果在LA架构上运行x86内核是怎样的，BootLoader直接传递x86内核的入口地址么。bios要怎样把LA内核拉起来。
