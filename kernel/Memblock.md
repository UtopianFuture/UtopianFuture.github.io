## Memblock

[TOC]

### 背景

**在 buddy 系统工作前，内核需要感知物理内存**，并在初始化早期提供内存分配。内核使用 memblock 机制来完成这一工作。

### 大纲

整个内存初始化过程可以分为 memblock 初始化、memblock 映射、buddy 初始化三个部分。

![img](https://raw.githubusercontent.com/UtopianFuture/UtopianFuture.github.io/81305767754ca8df95d18da10465dd730bc093be/image/memblock-outline.svg)

下面是进一步的分析。

```c
| start_kernel // 内核初始化入口，涉及内存，中断等诸多方面
| 	-> setup_arch // 架构相关部分初始化
| 		// 将内核的数据段、代码段等信息写入 init_mm 中，这些信息是在 vmlinux.lds.S 中确定的
| 		-> setup_initial_init_mm
| 		// 建立 FIXADDR_START 虚拟地址相关的页表，会初始化 pgd, p4d, pmd 等页表，
| 		// 但不会将物理地址写入 pte，需要在后续使用时按需写入
| 		-> early_fixmap_init
| 		// __fdt_pointer 是在 head.S 中保存的寄存器值
| 		// 调用 fixmap_remap_fdt 对 __fdt_pointer（dts 的物理地址）进行映射，后续可以通过虚拟地址解析 dts
| 		-> setup_machine_fdt
| 			-> early_init_dt_scan
| 				-> early_init_dt_scan_nodes
| 					// 解析 memory 节点，将内存信息通过 memblock_add 添加到 memblock.memroy 中
| 					-> early_init_dt_scan_memory
| 		// 在解析 dts 前，会对 memblock.memory 做一些调整，删除超出地址范围的信息
|		// 也会将 _stext ~ _end 地址段添加到 memblock.reserved 中
| 		-> arm64_memblock_init
| 			// 遍历 dts，对所有的预留内存节点进行初始化
| 			-> early_init_fdt_scan_reserved_mem
| 				-> __fdt_scan_reserved_mem
| 					-> __reserved_mem_reserve_reg
| 						-> early_init_dt_reserve_memory_arch
| 							// 对于 no-map 类型的节点，会置上 MEMBLCOK_NOMAP 位
|							// 对于 map 类型的节点，添加到 memblock.reserved 中
| 							// 这里很奇怪，因为后面会对 memblock.memory 内存块进行映射
| 							-> memblock_mark_nomap/memblock_reserve
| 						// 所有的预留内存节点信息都添加到 reserved_mem 全局数组中
| 						// 后续 cma 的初始化需要用到这里的信息
| 						-> fdt_reserved_mem_save_node
| 				-> fdt_init_reserved_mem
| 					// 如果是 alloc-ranges 类型的预留内存节点，在这里动态的分配内存，分配过程很复杂，暂不进一步分析
| 					-> __reserved_mem_alloc_size
| 					// cma 类型的内存需要调用 rmem_cma_setup 初始化
| 					-> __reserved_mem_init_node
| 		// 到目前为止，memblock 完成初始化，
|		// 所有的内存信息已经保存在 memblock.memory 和 memblock.reserved 中，之后需要进行映射
| 		-> paging_init
| 			// 对内核的 _stext, _etext 等段进行映射
| 			// swapper_pg_dir 是 init 进程的 pgd
| 			-> map_kernel
| 			// 这里只会对 memblock.memory 内存块进行映射，映射到线性映射区，
| 			// 在遍历时会判断该内存块的 flag，如果置上了 MEMBLOCK_NOMAP，则不会映射。
|			// 同时为了不映射 kernel image 中的 rodata 数据，
| 			// 会先将 kernel_start ~ kernel_end 部分转换为 no-map 类型，
| 			// 等 memblock.memory 映射完了，再取消 no-map
| 			-> map_mem
| 				-> __map_memblock
| 					-> __create_pgd_mapping // 对每个内存段建立映射关系
| 		-> bootmem_init
| 			-> arch_numa_init // 空函数
|			// 当前内存采用的是 sparse 内存模型，即物理内存存在空洞
|			// 并且支持内存热插拔（这是个啥），以 section 为单位进行管理
| 			-> sparse_init
|				// 对 memblock.memory 中的内存执行 memory_present 函数
|				// 每 128MB 为一个 mem_section
|				// 可为啥要这样管理呢？为了热拔插好管理？
|				// 是的，这样每个 mem_section 都是可以热拔插的
| 				-> memblocks_present
|				// 遍历每个 node 中的每个 mem_section
|				// 执行 vmemmap_populate 函数，建立 vmemmap 到 page frame 的页表
| 				-> sparse_init_nid
| 			-> zone_sizes_init
|				// 确定好系统中每个 zone 的地址范围，这些信息会在系统初始化时打印出来
|				// 然后遍历每个 node，初始化对应的 pg_data_t
| 				-> free_area_init
|					-> free_area_init_node
|						-> calculate_node_totalpages // 计算 zone->present_pages 等变量
|						// 最重要的是初始化 zone->free_area 和 zone->free_area[order].free_list[t] 变量
|						-> free_area_init_core
|					// 根据 pfn 找到对应的 page，并进行初始化
|					// 在 sparse_init_nid 函数执行完后，pfn_to_page 就能够使用了
|					// 初始化完后，会将该 page 置成 MIGRATE_MOVABLE 类型（那其他类型的 page 在哪里初始化？）
|					-> memmap_init
| 	// 到目前为止，所有的物理内存资源完成初始化，基本的映射也建立好了，
| 	// 有条件初始化 buddy 等内存管理系统了
|	-> mm_core_init // 初始化 buddy 系统，slab 系统等内存分配器
|		-> mem_init // 将 memblock 管理的内存释放到 buddy 中
| 			->memblock_free_all
|				// 将 memblock.memory 中的内存释放掉，这里有个问题，
| 				// __next_mem_pfn_range 遍历的是 memblock.memory，
|				// 但是循环中 free_memmap 释放的又是 memblock.reserved
| 				-> free_unused_memmap
| 					-> free_memmap // 释放对应的内存块
| 						-> memblock_free
| 							-> memblock_remove_range
| 					-> free_low_memory_core_early // 这里会遍历 memblock.memory, memblock.reserved
```

### 相关数据结构

MEMBLOCK 内存分配器作为早期的内存管理器，维护了两种内存。第一种内存是系统可用的物理内存，即系统实际含有的物理内存，其值从 DTS 中进行配置。第二种内存是内核预留给操作系统的内存（reserved memory），这部分内存作为特殊功能使用，**不能作为共享内存使用**，memblock 通过 dts 信息来初始化这部分内存。

#### memblock

MEMBLOCK 分配器的主体是使用数据结构 struct memblock 进行维护：

```c
/**
* struct memblock - memblock allocator metadata
* @bottom_up: is bottom up direction?
* @current_limit: physical address of the current allocation limit
* @memory: usabe memory regions
* @reserved: reserved memory regions
* @physmem: all physical memory
*/
struct memblock {
 bool bottom_up; /* is bottom up direction? */
 phys_addr_t current_limit; // 指定当前 MEMBLOCK 分配器上限
 struct memblock_type memory; // 可用物理内存，所有的物理内存都在这里面，包括 reserved 类型的
 struct memblock_type reserved; // 操作系统预留的内存
};
```

#### memblock_type

MEMBLOCK 分配器使用数据结构 struct memblock_type 管理不同类型的内存：

```c
/**
 * struct memblock_type - collection of memory regions of certain type
 * @cnt: number of regions
 * @max: size of the allocated array
 * @total_size: size of all regions
 * @regions: array of regions
 * @name: the memory type symbolic name
 */
struct memblock_type {
	unsigned long cnt; // 当前类型的物理内存内存块数量
	unsigned long max; // 最大可管理内存区块的数量
	phys_addr_t total_size; // 已经管理内存块的大小
	struct memblock_region *regions;
	char *name;
};
```

#### memblock_region

数据结构 struct memblock_region 维护一块内存区块：

```c
/**
 * struct memblock_region - represents a memory region
 * @base: physical address of the region
 * @size: size of the region
 * @flags: memory region attributes
 * @nid: NUMA node id
 */
struct memblock_region {
	phys_addr_t base;
	phys_addr_t size;
	enum memblock_flags flags;
#ifdef CONFIG_HAVE_MEMBLOCK_NODE_MAP
	int nid;
#endif
};
```

内核定义了一个 `struct memblock memblock __initdata_memblock` 变量来管理所有的内存结点，之后所有的操作都是围绕这个变量，

```c
static struct memblock_region memblock_memory_init_regions[INIT_MEMBLOCK_REGIONS] __initdata_memblock;
static struct memblock_region memblock_reserved_init_regions[INIT_MEMBLOCK_RESERVED_REGIONS] __initdata_memblock; // 最大支持 128 块内存

struct memblock memblock __initdata_memblock = {
	.memory.regions		= memblock_memory_init_regions,
	.memory.cnt		= 1,	/* empty dummy entry */
	.memory.max		= INIT_MEMBLOCK_REGIONS,
	.memory.name		= "memory",

	.reserved.regions	= memblock_reserved_init_regions,
	.reserved.cnt		= 1,	/* empty dummy entry */
	.reserved.max		= INIT_MEMBLOCK_RESERVED_REGIONS,
	.reserved.name		= "reserved",

	.bottom_up		= false,
	.current_limit		= MEMBLOCK_ALLOC_ANYWHERE,
};
```

### memblock 初始化

我们进一步深入代码分析 memblock 是怎样解析 dts 进行内存初始化的。

#### early_fixmap_init

```c
void __init early_fixmap_init(void)
{
	pgd_t *pgdp;
	p4d_t *p4dp, p4d;
	pud_t *pudp;
	pmd_t *pmdp;
	unsigned long addr = FIXADDR_START; // 这块虚拟地址是固定的，具体定义见 fixed_address，其有多个段

	// 通过 init_mm.pgd 得到
	pgdp = pgd_offset_k(addr);
	p4dp = p4d_offset(pgdp, addr); // 就是 pgd
	p4d = READ_ONCE(*p4dp);
	// bm_pud, bm_pmd 也是静态定义的，之后有需要往页表项中填入即可
	if (CONFIG_PGTABLE_LEVELS > 3 &&
	 !(p4d_none(p4d) || p4d_page_paddr(p4d) == __pa_symbol(bm_pud))) {
		/*
		 * We only end up here if the kernel mapping and the fixmap
		 * share the top level pgd entry, which should only happen on
		 * 16k/4 levels configurations.
		 */
		BUG_ON(!IS_ENABLED(CONFIG_ARM64_16K_PAGES));
		pudp = pud_offset_kimg(p4dp, addr);
	} else {
		if (p4d_none(p4d))
			__p4d_populate(p4dp, __pa_symbol(bm_pud), P4D_TYPE_TABLE);
		pudp = fixmap_pud(addr);
	}
	if (pud_none(READ_ONCE(*pudp)))
		__pud_populate(pudp, __pa_symbol(bm_pmd), PUD_TYPE_TABLE);
	pmdp = fixmap_pmd(addr);
	__pmd_populate(pmdp, __pa_symbol(bm_pte), PMD_TYPE_TABLE);

	...
}
```

建立好的映射如下图所示，之后就可以往 bm_pte 填入物理地址，通过 MMU 使用 va 访问内存，

![img](https://raw.githubusercontent.com/UtopianFuture/UtopianFuture.github.io/81305767754ca8df95d18da10465dd730bc093be/image/early_fixmap_init.svg)

#### setup_machine_fdt

```c
static void __init setup_machine_fdt(phys_addr_t dt_phys) // __fdt_pointer 是在 head.S 中保存的寄存器值
{
	int size;
	void *dt_virt = fixmap_remap_fdt(dt_phys, &size, PAGE_KERNEL); // 对 dts 的物理地址进行映射
	const char *name;

	if (dt_virt)
		memblock_reserve(dt_phys, size); // dts 这块内存保存到 memblock.reserved 中

 	// 调用 early_init_dt_scan_memory 解析 memory 节点，
 	// 将内存信息通过 memblock_add 添加到 memblock.memroy 中
	if (!dt_virt || !early_init_dt_scan(dt_virt)) {
		...
	}

	/* Early fixups are done, map the FDT as read-only now */
	fixmap_remap_fdt(dt_phys, &size, PAGE_KERNEL_RO); // 修改映射为 ro

	...
}
```

#### early_init_dt_scan_memory

```c
int __init early_init_dt_scan_memory(unsigned long node, const char *uname,
				 int depth, void *data)
{
	const char *type = of_get_flat_dt_prop(node, "device_type", NULL);
	const __be32 *reg, *endp;
	int l;
	bool hotpluggable;

 	// 解析 memory 节点，可参考上面的 dts 示例
 	// device_type 有很多种类型，一定要是 memory 类型才行
	/* We are scanning "memory" nodes only */
	if (type == NULL || strcmp(type, "memory") != 0)
		return 0;

	reg = of_get_flat_dt_prop(node, "linux,usable-memory", &l);
	if (reg == NULL)
		reg = of_get_flat_dt_prop(node, "reg", &l);
	if (reg == NULL)
		return 0;

	endp = reg + (l / sizeof(__be32));

	while ((endp - reg) >= (dt_root_addr_cells + dt_root_size_cells)) {
		u64 base, size;

 		// 从代码来看，这里就是解析 reg 指定的 base 和 size，但是经过实验，发现并不是
 		// size 和 qemu 传入的可用内存有关
		base = dt_mem_next_cell(dt_root_addr_cells, &reg);
		size = dt_mem_next_cell(dt_root_size_cells, &reg);

		...

 		// 这里会调用 memblock_add，将 memory 节点的内存添加到 memblock.memory 中
		early_init_dt_add_memory_arch(base, size);

 	...

	}

	return 0;
}
```

#### arm64_memblock_init

在 `setup_machine_fdt` 中我们解析 dts 得到了整个的物理内存空间，下面我们需要解析 reserved-memroy 节点，进一步记录，

```c
void __init arm64_memblock_init(void)
{
	s64 linear_region_size = PAGE_END - _PAGE_OFFSET(vabits_actual);

	...

 	// 下面主要是去除一些 corner case
 	/* Remove memory above our supported physical address size */
	memblock_remove(1ULL << PHYS_MASK_SHIFT, ULLONG_MAX);
	/*
	 * Select a suitable value for the base of physical memory.
	 */
 	// memblock_start_of_DRAM 和后面的 memblock_end_of_DRAM 都是读取 memblock.memory.regions[0] 信息
 	// 得到 start 和 end，而 memblock.memory.regions[0] 就是在 early_init_dt_scan_memory 中
 	// 解析 memory 节点得到的信息写入的
	memstart_addr = round_down(memblock_start_of_DRAM(),
				 ARM64_MEMSTART_ALIGN);
	if ((memblock_end_of_DRAM() - memstart_addr) > linear_region_size)
		pr_warn("Memory doesn't fit in the linear mapping, VA_BITS too small\n");
	/*
	 * Remove the memory that we will not be able to cover with the
	 * linear mapping. Take care not to clip the kernel which may be
	 * high in memory.
	 */
	memblock_remove(max_t(u64, memstart_addr + linear_region_size,
			__pa_symbol(_end)), ULLONG_MAX);
	if (memstart_addr + linear_region_size < memblock_end_of_DRAM()) {
		/* ensure that memstart_addr remains sufficiently aligned */
		memstart_addr = round_up(memblock_end_of_DRAM() - linear_region_size,
					 ARM64_MEMSTART_ALIGN);
		memblock_remove(0, memstart_addr);
	}

 	...

	/*
	 * Register the kernel text, kernel data, initrd, and initial
	 * pagetables with memblock.
	 */
	memblock_reserve(__pa_symbol(_stext), _end - _stext);
	early_init_fdt_scan_reserved_mem(); // 解析 dts 的 reserved-memory 节点
	if (!IS_ENABLED(CONFIG_ZONE_DMA) && !IS_ENABLED(CONFIG_ZONE_DMA32))
		reserve_crashkernel();

    ...
}
```

#### early_init_fdt_scan_reserved_mem
```c
void __init early_init_fdt_scan_reserved_mem(void)
{
	int n;
	u64 base, size;
	if (!initial_boot_params)
		return;
	/* Process header /memreserve/ fields */
	// 这里是解析 /memreserve 节点的，不咋遇到，暂不用关注
	for (n = 0; ; n++) {
		fdt_get_mem_rsv(initial_boot_params, n, &base, &size);
		if (!size)
			break;
		early_init_dt_reserve_memory_arch(base, size, false);
	}
	// 遍历每个子节点，并执行回调函数 __fdt_scan_reserved_mem
	of_scan_flat_dt(__fdt_scan_reserved_mem, NULL);
	// 这里是进一步初始化，如 cma, dma 类型的内存需要调用对应的回调函数初始化
	fdt_init_reserved_mem();
	// 将 elfcorehdr 添加到 memblock.reserved 中
	fdt_reserve_elfcorehdr();
}
```
#### __fdt_scan_reserved_mem
```c
static int __init __fdt_scan_reserved_mem(unsigned long node, const char *uname,
					 int depth, void *data)
{
	static int found;
	int err;
	// 具体的遍历方式暂可不用深究
	if (!found && depth == 1 && strcmp(uname, "reserved-memory") == 0) {
		...
	}
	// 检查 status 属性是否为 ok 或 okay
	if (!of_fdt_device_is_available(initial_boot_params, node))
		return 0;
	// 这里解析 reg 方式的预留内存
	err = __reserved_mem_reserve_reg(node, uname);
	if (err == -ENOENT && of_get_flat_dt_prop(node, "size", NULL))
		// 这里解析 alloc-ranges 方式的预留内存
		// 暂时将 base 和 size 都配置为 0，后续再分配
		fdt_reserved_mem_save_node(node, uname, 0, 0);
	/* scan next node */
	return 0;
}
```
#### __reserved_mem_reserve_reg
```c
static int __init __reserved_mem_reserve_reg(unsigned long node,
					 const char *uname)
{
	...

	prop = of_get_flat_dt_prop(node, "reg", &len);
	if (!prop)
		return -ENOENT;
	// 解析 no-map 属性
	nomap = of_get_flat_dt_prop(node, "no-map", NULL) != NULL;
	while (len >= t_len) {
		base = dt_mem_next_cell(dt_root_addr_cells, &prop);
		size = dt_mem_next_cell(dt_root_size_cells, &prop);
		if (size && // 该函数将 [base, base + size) 添加到 memblock 变量中
		 early_init_dt_reserve_memory_arch(base, size, nomap) == 0)
			pr_debug("Reserved memory: reserved region for node '%s': base %pa, size %lu MiB\n",
				uname, &base, (unsigned long)(size / SZ_1M));
		...

		len -= t_len;
		if (first) {
			// 将预留内存添加到 static struct reserved_mem reserved_mem[MAX_RESERVED_REGIONS]; 中
			// 后续需要遍历该数组做进一步的初始化
			fdt_reserved_mem_save_node(node, uname, base, size);
			first = 0;
		}
	}
	return 0;
}
```
#### early_init_dt_reserve_memory_arch
```c
static int __init early_init_dt_reserve_memory_arch(phys_addr_t base,
					phys_addr_t size, bool nomap)
{
	if (nomap) {
		/*
		 * If the memory is already reserved (by another region), we
		 * should not allow it to be marked nomap, but don't worry
		 * if the region isn't memory as it won't be mapped.
		 */
		if (memblock_overlaps_region(&memblock.memory, base, size) &&
		 memblock_is_region_reserved(base, size))
			return -EBUSY;

		// 配置 MEMBLOCK_NOMAP 标志，注意，这里没有添加到 memblock.memory 变量中
		// 因为所有的内存都在 early_init_dt_scan_memory 添加到 memblock.memory 中
		// 所以这里只是配置标识位
		return memblock_mark_nomap(base, size);
	}

	// 没有 no-map 属性的添加到 memblock.reserved 变量中
	// 这里和我们的理解有些不一样
	return memblock_reserve(base, size);
}
```
#### fdt_init_reserved_mem
```c
void __init fdt_init_reserved_mem(void)
{
	int i;

	/* check for overlapping reserved regions */
	__rmem_check_for_overlap();
	// reserved_mem 就是上面定义的全局变量，所有的预留内存节点都会存储在这里
	for (i = 0; i < reserved_mem_count; i++) {
		struct reserved_mem *rmem = &reserved_mem[i];
		unsigned long node = rmem->fdt_node;

		...

		// 前面说过，alloc-ranges 类型的预留内存节点的 base, size 暂时配置为 0
		// 这里进行分配
		// 分配过程可概述为在 alloc-ranges 指定的范围内寻找一块内存，下次有需要再深入分析
		if (rmem->size == 0)
			err = __reserved_mem_alloc_size(node, rmem->name,
						 &rmem->base, &rmem->size);
		if (err == 0) {
			err = __reserved_mem_init_node(rmem); // 所有都分配完后，进一步初始化

			...
		}
	}
}
```
#### __reserved_mem_init_node
```c
typedef int (*reservedmem_of_init_fn)(struct reserved_mem *rmem);
#define RESERVEDMEM_OF_DECLARE(name, compat, init)			\
	_OF_DECLARE(reservedmem, name, compat, init, reservedmem_of_init_fn)
/* coherent.c */
RESERVEDMEM_OF_DECLARE(dma, "shared-dma-pool", rmem_dma_setup);
/* contiguous.c */
RESERVEDMEM_OF_DECLARE(cma, "shared-dma-pool", rmem_cma_setup);

static int __init __reserved_mem_init_node(struct reserved_mem *rmem)
{
	extern const struct of_device_id __reservedmem_of_table[];
	const struct of_device_id *i;
	int ret = -ENOENT;
	for (i = __reservedmem_of_table; i < &__rmem_of_table_sentinel; i++) {
		// 该类型的回调函数目前只有 dma, cma 类型的预留内存定义了
		reservedmem_of_init_fn initfn = i->data;
		const char *compat = i->compatible;
		if (!of_flat_dt_is_compatible(rmem->fdt_node, compat))
			continue;

		// 执行 rmem_dma_setup 或 rmem_cma_setup 回调函数
		ret = initfn(rmem);
	}

	return ret;
}
```
### 内存分布
经过了上面一系列的构造，我们来看一下现在 memblock 的布局是怎样的，
```c
[ 0.000000] Booting Linux on physical CPU 0x0000000000 [0x411fd070]
[ 0.000000] Linux version 5.15.74-gbd4ffbd0c377-dirty (guanshun@guanshun-ubuntu) (aarch64-linux-gnu-gcc (Linaro GCC 7.5-2019.12) 7.5.0, GNU ld (Linaro_Binutils-2019.12) 2.28.2.20170706) #556 SMP PREEMPT Tue Mar 12 16:48:58 CST 2024
[ 0.000000] setup_initial_init_mm, start_code: 0xffffffc008010000, end_code: 0xffffffc008b80000, end_data: 0xffffffc0092f2a00, brk: 0xffffffc009370000
 // 申请 dts 需要的内存
[ 0.000000] memblock_reserve: [0x0000000049c00000-0x0000000049c0a45d] setup_arch+0xd8/0x5fc
 // 添加 memory 节点信息
[ 0.000000] memblock_add: [0x0000000040000000-0x00000000bfffffff] early_init_dt_add_memory_arch+0x54/0x60
[ 0.000000] OF: fdt: early_init_dt_scan_memory, base: 0x40000000, size: 0x80000000
[ 0.000000] Machine model: linux,dummy-virt
[ 0.000000] efi: UEFI not found.
 // 移除不支持的地址范围
[ 0.000000] memblock_remove: [0x0001000000000000-0x0000fffffffffffe] arm64_memblock_init+0x9c/0x304
[ 0.000000] arm64_memblock_init, linear_region_size: 0x4000000000, memstart_addr: 0x40000000, start: 0x40000000, end: 0xc0000000
 // 移除线性映射不支持的地址范围
[ 0.000000] memblock_remove: [0x0000004040000000-0x000000403ffffffe] arm64_memblock_init+0x12c/0x304
 // 移除 initrd 的内存区域
[ 0.000000] memblock_remove: [0x0000000048000000-0x0000000049be3fff] arm64_memblock_init+0x1fc/0x304
[ 0.000000] memblock_add: [0x0000000048000000-0x0000000049be3fff] arm64_memblock_init+0x208/0x304
[ 0.000000] memblock_reserve: [0x0000000048000000-0x0000000049be3fff] arm64_memblock_init+0x214/0x304
 // 将内核的 _stext ~ _end 保存到 memblock.reserved 中
[ 0.000000] memblock_reserve: [0x0000000040210000-0x000000004156ffff] arm64_memblock_init+0x290/0x304
 // 注意，sec_region 为 no-map 类型的预留内存，没有调用 memblock_add 添加到 memblock 中
 // 而是将其配置上 MEMBLOCK_NOMAP 标识
[ 0.000000] OF: fdt: __reserved_mem_reserve_reg, base: 0x42700000, size: 0x100, t_len: 0x10, len: 0x10
[ 0.000000] OF: reserved mem: fdt_reserved_mem_save_node, count: 1, name: sec_region, base: 0x42700000, size: 0x100
 // test_block1 为 alloc-ranges 类型的预留内存，只会添加到 reserved_mem 数组中，下同
[ 0.000000] OF: reserved mem: fdt_reserved_mem_save_node, count: 2, name: test_block1, base: 0x0, size: 0x0
[ 0.000000] OF: fdt: __reserved_mem_reserve_reg, base: 0x42800000, size: 0x400000, t_len: 0x10, len: 0x10
[ 0.000000] memblock_reserve: [0x0000000042800000-0x0000000042bfffff] __fdt_scan_reserved_mem+0x2bc/0x350
[ 0.000000] OF: reserved mem: fdt_reserved_mem_save_node, count: 3, name: test_block2, base: 0x42800000, size: 0x400000
[ 0.000000] OF: fdt: __reserved_mem_reserve_reg, base: 0x42c00000, size: 0x400000, t_len: 0x10, len: 0x10
[ 0.000000] memblock_reserve: [0x0000000042c00000-0x0000000042ffffff] __fdt_scan_reserved_mem+0x2bc/0x350
[ 0.000000] OF: reserved mem: fdt_reserved_mem_save_node, count: 4, name: test_block3, base: 0x42c00000, size: 0x400000
[ 0.000000] OF: fdt: __reserved_mem_reserve_reg, base: 0x43400000, size: 0x400000, t_len: 0x10, len: 0x10
[ 0.000000] memblock_reserve: [0x0000000043400000-0x00000000437fffff] __fdt_scan_reserved_mem+0x2bc/0x350
[ 0.000000] OF: reserved mem: fdt_reserved_mem_save_node, count: 5, name: test_block4, base: 0x43400000, size: 0x400000
[ 0.000000] OF: fdt: __reserved_mem_reserve_reg, base: 0x43800000, size: 0x400000, t_len: 0x10, len: 0x10
[ 0.000000] memblock_reserve: [0x0000000043800000-0x0000000043bfffff] __fdt_scan_reserved_mem+0x2bc/0x350
[ 0.000000] OF: reserved mem: fdt_reserved_mem_save_node, count: 6, name: test_block5, base: 0x43800000, size: 0x400000
[ 0.000000] OF: fdt: __reserved_mem_reserve_reg, base: 0x44c00000, size: 0x400000, t_len: 0x10, len: 0x10
[ 0.000000] memblock_reserve: [0x0000000044c00000-0x0000000044ffffff] __fdt_scan_reserved_mem+0x2bc/0x350
[ 0.000000] OF: reserved mem: fdt_reserved_mem_save_node, count: 7, name: test_block6, base: 0x44c00000, size: 0x400000
[ 0.000000] OF: fdt: __reserved_mem_reserve_reg, base: 0x4a000000, size: 0x400000, t_len: 0x10
[ 0.000000] memblock_reserve: [0x0000000044c00000-0x0000000044ffffff] __fdt_scan_reserved_mem+0x2bc/0x350
[ 0.000000] OF: reserved mem: fdt_reserved_mem_save_node, count: 7, name: test_block6, base: 0x44c00000, size: 0x400000
[ 0.000000] OF: fdt: __reserved_mem_reserve_reg, base: 0x4a000000, size: 0x400000, t_len: 0x10, len: 0x10
[ 0.000000] memblock_reserve: [0x000000004a000000-0x000000004a3fffff] __fdt_scan_reserved_mem+0x2bc/0x350
[ 0.000000] OF: reserved mem: fdt_reserved_mem_save_node, count: 8, name: test_block7, base: 0x4a000000, size: 0x400000
[ 0.000000] OF: reserved mem: fdt_reserved_mem_save_node, count: 9, name: test_cma, base: 0x0, size: 0x0
 // linux,cma 为 reg 类型的预留内存，会添加到 memblock.reserved 中
[ 0.000000] OF: fdt: __reserved_mem_reserve_reg, base: 0x60000000, size: 0x10000000, t_len: 0x10, len: 0x10
[ 0.000000] memblock_reserve: [0x0000000060000000-0x000000006fffffff] __fdt_scan_reserved_mem+0x2bc/0x350
[ 0.000000] OF: reserved mem: fdt_reserved_mem_save_node, count: 10, name: linux,cma, base: 0x60000000, size: 0x10000000
 // 这里分配 alloc-ranges 类型的预留内存，并全部添加到 memblock.reserved 中
[ 0.000000] OF: reserved mem: __reserved_mem_alloc_size, align: 0x400000
[ 0.000000] memblock_reserve: [0x00000000b8000000-0x00000000bfffffff] memblock_alloc_range_nid+0xdc/0x150
[ 0.000000] Reserved memory: created CMA memory pool at 0x00000000b8000000, size 128 MiB
[ 0.000000] OF: reserved mem: initialized node test_cma, compatible id shared-dma-pool
[ 0.000000] OF: reserved mem: fdt_init_reserved_mem, name: test_cma, base: 0xb8000000, size: 0x8000000
[ 0.000000] OF: reserved mem: __reserved_mem_alloc_size, align: 0x400000
[ 0.000000] memblock_reserve: [0x0000000043c00000-0x0000000043ffffff] memblock_alloc_range_nid+0xdc/0x150
[ 0.000000] Reserved memory: created CMA memory pool at 0x0000000043c00000, size 4 MiB
[ 0.000000] OF: reserved mem: initialized node test_block1, compatible id shared-dma-pool
[ 0.000000] OF: reserved mem: fdt_init_reserved_mem, name: test_block1, base: 0x43c00000, size: 0x400000
[ 0.000000] OF: reserved mem: fdt_init_reserved_mem, name: sec_region, base: 0x42700000, size: 0x100
[ 0.000000] Reserved memory: created CMA memory pool at 0x0000000042800000, size 4 MiB
[ 0.000000] OF: reserved mem: initialized node test_block2, compatible id shared-dma-pool
[ 0.000000] OF: reserved mem: fdt_init_reserved_mem, name: test_block2, base: 0x42800000, size: 0x400000
[ 0.000000] Reserved memory: created CMA memory pool at 0x0000000042c00000, size 4 MiB
[ 0.000000] OF: reserved mem: initialized node test_block3, compatible id shared-dma-pool
[ 0.000000] OF: reserved mem: fdt_init_reserved_mem, name: test_block3, base: 0x42c00000, size: 0x400000
[ 0.000000] Reserved memory: created CMA memory pool at 0x0000000043400000, size 4 MiB
[ 0.000000] OF: reserved mem: initialized node test_block4, compatible id shared-dma-pool
[ 0.000000] OF: reserved mem: fdt_init_reserved_mem, name: test_block4, base: 0x43400000, size: 0x400000
[ 0.000000] Reserved memory: created CMA memory pool at 0x0000000043800000, size 4 MiB
[ 0.000000] OF: reserved mem: initialized node test_block5, compatible id shared-dma-pool
[ 0.000000] OF: reserved mem: fdt_init_reserved_mem, name: test_block5, base: 0x43800000, size: 0x400000
[ 0.000000] Reserved memory: created CMA memory pool at 0x0000000044c00000, size 4 MiB
[ 0.000000] OF: reserved mem: initialized node test_block6, compatible id shared-dma-pool
[ 0.000000] OF: reserved mem: fdt_init_reserved_mem, name: test_block6, base: 0x44c00000, size: 0x400000
[ 0.000000] Reserved memory: created CMA memory pool at 0x000000004a000000, size 4 MiB
[ 0.000000] OF: reserved mem: initialized node test_block7, compatible id shared-dma-pool
[ 0.000000] OF: reserved mem: fdt_init_reserved_mem, name: test_block7, base: 0x4a000000, size: 0x400000
 // cma 类型的内存都要调用 rmem_cma_setup 初始化
[ 0.000000] Reserved memory: created CMA memory pool at 0x0000000060000000, size 256 MiB
[ 0.000000] OF: reserved mem: initialized node linux,cma, compatible id shared-dma-pool
[ 0.000000] OF: reserved mem: fdt_init_reserved_mem, name: linux,cma, base: 0x60000000, size: 0x10000000
 // 到目前为止，所有的内存都添加到 memblock 中，接下来是建立映射关系
 // 因为 kernel_start ~ kernel_end 不需要建立映射，所以这里是到 0x40210000，0x40210000 ~ 0x411d0000 挖掉了
[ 0.000000] map_mem, start: 0x40000000, end: 0x40210000
[ 0.000000] map_mem, start: 0x411d0000, end: 0x42700000
 // 这些内存是在建立映射时为页表申请的
[ 0.000000] memblock_reserve: [0x00000000b7ff0000-0x00000000b7ff0fff] memblock_alloc_range_nid+0xdc/0x150
 // 除了 NO-MAP 类型的内存，其他的都会建立映射关系
[ 0.000000] map_mem, start: 0x42700100, end: 0xc0000000
 // 后面一系列都是建立映射为页表申请的内存
[ 0.000000] memblock_reserve: [0x00000000b7fe0000-0x00000000b7fe0fff] memblock_alloc_range_nid+0xdc/0x150
[ 0.000000] memblock_reserve: [0x00000000b7fd0000-0x00000000b7fd0fff] memblock_alloc_range_nid+0xdc/0x150
...
[ 0.000000] memblock_reserve: [0x00000000b7c00000-0x00000000b7c00fff] memblock_alloc_range_nid+0xdc/0x150
 // 这里是 free init_pg_dir ~ init_pg_end 范围的内存
[ 0.000000] memblock_free: [0x000000004155f000-0x0000000041561fff] paging_init+0x504/0x54c
 // 后面是进一步初始化需要的内存，后面再分析
[ 0.000000] memblock_alloc_try_nid: 63004 bytes align=0x8 nid=-1 from=0x0000000000000000 max_addr=0x0000000000000000 early_init_dt_alloc_memory_arch+0x28/0x58
[ 0.000000] NUMA: No NUMA configuration found
[ 0.000000] memblock_free: [0x00000000b7bee000-0x00000000b7bee0ff] memblock_free_ptr+0x54/0x68
[ 0.000000] NUMA: Faking a node at [mem 0x0000000040000000-0x00000000bfffffff]
[ 0.000000] NUMA: NODE_DATA [mem 0xb7bebb00-0xb7bedfff]
[ 0.000000] memblock_alloc_try_nid: 65536 bytes align=0x40 nid=-1 from=0x0000000000000000 max_addr=0x0000000000000000 sparse_init+0x90/0x324
[ 0.000000] memblock_alloc_try_nid: 4096 bytes align=0x40 nid=0 from=0x0000000000000000 max_addr=0x0000000000000000 sparse_index_alloc+0x50/0x84
[ 0.000000] memblock_alloc_try_nid: 640 bytes align=0x40 nid=0 from=0x0000000000000000 max_addr=0x0000000000000000 sparse_init_nid+0x5c/0x284
[ 0.000000] memblock_alloc_exact_nid_raw: 33554432 bytes align=0x200000 nid=0 from=0x0000000040000000 max_addr=0x0000000000000000 memmap_alloc+0x20/0x34
[ 0.000000] memblock_reserve: [0x00000000b5a00000-0x00000000b79fffff] memblock_alloc_range_nid+0xdc/0x150
[ 0.000000] memblock_alloc_try_nid_raw: 4096 bytes align=0x1000 nid=0 from=0x0000000040000000 max_addr=0x0000000000000000 __earlyonly_bootmem_alloc+0x28/0x34
[ 0.000000] Zone ranges:
[ 0.000000] DMA [mem 0x0000000040000000-0x00000000bfffffff]
[ 0.000000] DMA32 empty
[ 0.000000] Normal empty
[ 0.000000] Movable zone start for each node
[ 0.000000] Early memory node ranges
[ 0.000000] node 0: [mem 0x0000000040000000-0x00000000426fffff]
[ 0.000000] node 0: [mem 0x0000000042701000-0x00000000bfffffff]
[ 0.000000] Initmem setup node 0 [mem 0x0000000040000000-0x00000000bfffffff]
[ 0.000000] On node 0, zone DMA: 1 pages in unavailable ranges
[ 0.000000] MEMBLOCK configuration:
[ 0.000000] memory size = 0x0000000080000000 reserved size = 0x000000001ef712fa
[ 0.000000] memory.cnt = 0x3
[ 0.000000] memory[0x0]	[0x0000000040000000-0x00000000426fffff], 0x0000000002700000 bytes on node 0 flags: 0x0
 // 正常来说，所有的内存都会添加到 memblock.memory 中，但是 sec_region 是 no-map 类型的，所以这里 flags 不一样。同时 no-map 类型的内存不会添加到 memblock.reserve 中
[ 0.000000] memory[0x1]	[0x0000000042700000-0x00000000427000ff], 0x0000000000000100 bytes on node 0 flags: 0x4
[ 0.000000] memory[0x2]	[0x0000000042700100-0x00000000bfffffff], 0x000000007d8fff00 bytes on node 0 flags: 0x0
[ 0.000000] reserved.cnt = 0xf
 // 不断的调用 memblock_resere，导致了下面这些内存块。除 no-map 类型的内存，都会添加到该链表中
[ 0.000000] reserved[0x0]	[0x0000000040210000-0x000000004155efff], 0x000000000134f000 bytes flags: 0x0
[ 0.000000] reserved[0x1]	[0x0000000041562000-0x000000004156ffff], 0x000000000000e000 bytes flags: 0x0
[ 0.000000] reserved[0x2]	[0x0000000042800000-0x0000000042ffffff], 0x0000000000800000 bytes flags: 0x0
[ 0.000000] reserved[0x3]	[0x0000000043400000-0x0000000043ffffff], 0x0000000000c00000 bytes flags: 0x0
[ 0.000000] reserved[0x4]	[0x0000000044c00000-0x0000000044ffffff], 0x0000000000400000 bytes flags: 0x0
[ 0.000000] reserved[0x5]	[0x0000000048000000-0x0000000049be3fff], 0x0000000001be4000 bytes flags: 0x0
[ 0.000000] reserved[0x6]	[0x0000000049c00000-0x0000000049c0a45d], 0x000000000000a45e bytes flags: 0x0
[ 0.000000] reserved[0x7]	[0x000000004a000000-0x000000004a3fffff], 0x0000000000400000 bytes flags: 0x0
[ 0.000000] reserved[0x8]	[0x0000000060000000-0x000000006fffffff], 0x0000000010000000 bytes flags: 0x0
[ 0.000000] reserved[0x9]	[0x00000000b5a00000-0x00000000b79fffff], 0x0000000002000000 bytes flags: 0x0
[ 0.000000] reserved[0xa]	[0x00000000b7bd9000-0x00000000b7bd9fff], 0x0000000000001000 bytes flags: 0x0
[ 0.000000] reserved[0xb]	[0x00000000b7bdab00-0x00000000b7bee0ff], 0x0000000000013600 bytes flags: 0x0
[ 0.000000] reserved[0xc]	[0x00000000b7bee740-0x00000000b7bee9bf], 0x0000000000000280 bytes flags: 0x0
[ 0.000000] reserved[0xd]	[0x00000000b7bee9e0-0x00000000b7bfdffb], 0x000000000000f61c bytes flags: 0x0
[ 0.000000] reserved[0xe]	[0x00000000b7bfe000-0x00000000bfffffff], 0x0000000008402000 bytes flags: 0x0
[ 0.000000] memblock_alloc_try_nid: 192 bytes align=0x40 nid=-1 from=0x0000000000000000 max_addr=0x0000000000000000 setup_arch+0x304/0x5fc
[ 0.000000] psci: probing for conduit method from DT.
[ 0.000000] psci: PSCIv1.1 detected in firmware.
[ 0.000000] psci: Using standard PSCI v0.2 function IDs
[ 0.000000] psci: Trusted OS migration not required
[ 0.000000] psci: SMC Calling Convention v1.0
[ 0.000000] memblock_alloc_try_nid: 56 bytes align=0x40 nid=-1 from=0x0000000000000000 max_addr=0x0000000000000000 start_kernel+0x13c/0x664
[ 0.000000] memblock_alloc_try_nid: 56 bytes align=0x40 nid=-1 from=0x0000000000000000 max_addr=0x0000000000000000 start_kernel+0x15c/0x664
[ 0.000000] memblock_alloc_try_nid: 4096 bytes align=0x1000 nid=-1 from=0x0000000000000000 max_addr=0x0000000000000000 pcpu_embed_first_chunk+0x360/0x7fc
[ 0.000000] memblock_alloc_try_nid: 4096 bytes align=0x40 nid=-1
from=0x0000000000000000 max_addr=0x0000000000000000 pcpu_embed_first_chunk+0x360/0x7fc
[ 0.000000] memblock_alloc_try_nid: 4096 bytes align=0x40 nid=-1 from=0x0000000000000000 max_addr=0x0000000000000000 pcpu_embed_first_chunk+0x51c/0x7fc
[ 0.000000] memblock_alloc_try_nid: 155648 bytes align=0x1000 nid=0 from=0x0000000040000000 max_addr=0x0000000000000000 pcpu_fc_alloc+0x38/0x44
[ 0.000000] memblock_free: [0x00000000b7bc4000-0x00000000b7bc3fff] pcpu_fc_free+0x44/0x50
[ 0.000000] memblock_free: [0x00000000b7bd7000-0x00000000b7bd6fff] pcpu_fc_free+0x44/0x50
[ 0.000000] percpu: Embedded 19 pages/cpu s39384 r8192 d30248 u77824
[ 0.000000] memblock_alloc_try_nid: 8 bytes align=0x40 nid=-1 from=0x0000000000000000 max_addr=0x0000000000000000 pcpu_setup_first_chunk+0x338/0x86c
[ 0.000000] memblock_alloc_try_nid: 8 bytes align=0x40 nid=-1 from=0x0000000000000000 max_addr=0x0000000000000000 pcpu_setup_first_chunk+0x360/0x86c
[ 0.000000] memblock_alloc_try_nid: 8 bytes align=0x40 nid=-1 from=0x0000000000000000 max_addr=0x0000000000000000 pcpu_setup_first_chunk+0x388/0x86c
[ 0.000000] memblock_alloc_try_nid: 16 bytes align=0x40 nid=-1 from=0x0000000000000000 max_addr=0x0000000000000000 pcpu_setup_first_chunk+0x3b0/0x86c
[ 0.000000] pcpu-alloc: s39384 r8192 d30248 u77824 alloc=19*4096
[ 0.000000] pcpu-alloc: [0] 0 [0] 1
[ 0.000000] memblock_alloc_try_nid: 288 bytes align=0x40 nid=-1 from=0x0000000000000000 max_addr=0x0000000000000000 pcpu_setup_first_chunk+0x774/0x86c
[ 0.000000] memblock_alloc_try_nid: 144 bytes align=0x40 nid=-1 from=0x0000000000000000 max_addr=0x0000000000000000 pcpu_alloc_first_chunk+0x7c/0x278
[ 0.000000] memblock_alloc_try_nid: 384 bytes align=0x40 nid=-1 from=0x0000000000000000 max_addr=0x0000000000000000 pcpu_alloc_first_chunk+0xdc/0x278
[ 0.000000] memblock_alloc_try_nid: 392 bytes align=0x40 nid=-1 from=0x0000000000000000 max_addr=0x0000000000000000 pcpu_alloc_first_chunk+0x128/0x278
[ 0.000000] memblock_alloc_try_nid: 96 bytes align=0x40 nid=-1 from=0x0000000000000000 max_addr=0x0000000000000000 pcpu_alloc_first_chunk+0x150/0x278
[ 0.000000] memblock_alloc_try_nid: 144 bytes align=0x40 nid=-1 from=0x0000000000000000 max_addr=0x0000000000000000 pcpu_alloc_first_chunk+0x7c/0x278
[ 0.000000] memblock_alloc_try_nid: 1024 bytes align=0x40 nid=-1 from=0x0000000000000000 max_addr=0x0000000000000000 pcpu_alloc_first_chunk+0xdc/0x278
[ 0.000000] memblock_alloc_try_nid: 1032 bytes align=0x40 nid=-1 from=0x0000000000000000 max_addr=0x0000000000000000 pcpu_alloc_first_chunk+0x128/0x278
[ 0.000000] memblock_alloc_try_nid: 256 bytes align=0x40 nid=-1 from=0x0000000000000000 max_addr=0x0000000000000000 pcpu_alloc_first_chunk+0x150/0x278
[ 0.000000] memblock_free: [0x00000000b7bd8000-0x00000000b7bd8fff] pcpu_free_alloc_info+0x4c/0x58
[ 0.000000] memblock_free: [0x00000000b7bd7000-0x00000000b7bd7fff] pcpu_embed_first_chunk+0x798/0x7fc
[ 0.000000] Detected PIPT I-cache on CPU0
[ 0.000000] CPU features: detected: GIC system register CPU interface
[ 0.000000] CPU features: detected: Spectre-v2
[ 0.000000] CPU features: detected: Spectre-v3a
[ 0.000000] CPU features: detected: Spectre-v4
[ 0.000000] CPU features: detected: Spectre-BHB
[ 0.000000] CPU features: detected: ARM erratum 834220
[ 0.000000] CPU features: detected: ARM erratum 832075
[ 0.000000] CPU features: detected: ARM errata 1165522, 1319367, or 1530923
[ 0.000000] Built 1 zonelists, mobility grouping on. Total pages: 516095
[ 0.000000] Policy zone: DMA
[ 0.000000] Kernel command line: rootwait console=ttyAMA0 init=/init nokaslr loglevel=15
[ 0.000000] memblock_alloc_try_nid: 9 bytes align=0x40 nid=-1 from=0x0000000000000000 max_addr=0x0000000000000000 start_kernel+0x2a8/0x664
[ 0.000000] Unknown kernel command line parameters "nokaslr", will be passed to user space.
[ 0.000000] memblock_free: [0x00000000b7bee100-0x00000000b7bee108] memblock_free_ptr+0x54/0x68
[ 0.000000] memblock_alloc_try_nid: 2097152 bytes align=0x40 nid=-1 from=0x0000000000000000 max_addr=0x0000000000000000 alloc_large_system_hash+0x174/0x2ac
[ 0.000000] memblock_reserve: [0x00000000b5800000-0x00000000b59fffff] memblock_alloc_range_nid+0xdc/0x150
[ 0.000000] Dentry cache hash table entries: 262144 (order: 9, 2097152 bytes, linear)
[ 0.000000] memblock_alloc_try_nid: 1048576 bytes align=0x40 nid=-1 from=0x0000000000000000 max_addr=0x0000000000000000 alloc_large_system_hash+0x174/0x2ac
[ 0.000000] Inode-cache hash table entries: 131072 (order: 8, 1048576 bytes, linear)
[ 0.000000] mem auto-init: stack:off, test alloc:off, test free:off
[ 0.000000] Memory: 1586580K/2097148K available (11712K kernel code, 1162K rwdata, 4384K rodata, 2048K init, 430K bss, 88680K reserved, 421888K cma-reserved)
```

总的来说，对于 no-map 类型的预留内存，只会存在于 memblock.memory 节点中，同时会置上 MEMBLOCK_NOMAP 标志，所以可以在 /sys/kernel/debug/memblock/memory 中看到单独的一个节点；对于 map 类型的节点，会不断调用 memblock_reserve 函数添加到 memblock.reserved 节点中；对于 alloc-ranges 类型的阶段，其 base 和 size 不会在解析 dts 时确定，而是在所有节点解析完后统一分配；对于 cma 内存，会额外执行 rmem_dma_setup 或 rmem_cma_setup 回调函数。

再回到 memblock.memory 和 memblock.reserved 两个链表。memblock.memory 记录了整个 DRAM 内存空间的地址信息，并且额外记录 no-map 类型的地址信息；memblock.reserved 会记录 map , cma 等类型的地址信息。

由此可以总结出 64 位 arm linux 内核的内存分布，
![linux-address-space.png](https://github.com/UtopianFuture/UtopianFuture.github.io/blob/master/image/linux-address-space.png?raw=true)

### memblock 映射
在所有的内存信息都存入 memblock 中后，我们需要对其建立虚实地址映射，后续才能使用 va 通过 MMU 访问物理内存。
#### paging_init
```c
void __init paging_init(void)
{
	// swapper_pg_dir 可以理解为指向 init 进程的 pgd 页表的指针
	pgd_t *pgdp = pgd_set_fixmap(__pa_symbol(swapper_pg_dir));
	map_kernel(pgdp);
	map_mem(pgdp);
	pgd_clear_fixmap();
	// 页表建立好了，将 swapper_pg_dir 写入到 ttbr1 中
	// ttbr1 是内核页表的基地址，ttbr0 是用户进程的页表基地址
	cpu_replace_ttbr1(lm_alias(swapper_pg_dir));
	init_mm.pgd = swapper_pg_dir;
	memblock_free(__pa_symbol(init_pg_dir),
		 __pa_symbol(init_pg_end) - __pa_symbol(init_pg_dir));
	memblock_allow_resize();
}
```
#### map_kernel
```c
static void __init map_kernel(pgd_t *pgdp)
{
	// 每段内核态的内存都有 vm_struct 表示，用户态的内存用 vm_area_struct 表示
	static struct vm_struct vmlinux_text, vmlinux_rodata, vmlinux_inittext,
				vmlinux_initdata, vmlinux_data;
	...
	map_kernel_segment(pgdp, _stext, _etext, text_prot, &vmlinux_text, 0,
			 VM_NO_GUARD);
	map_kernel_segment(pgdp, __start_rodata, __inittext_begin, PAGE_KERNEL,
			 &vmlinux_rodata, NO_CONT_MAPPINGS, VM_NO_GUARD);
	map_kernel_segment(pgdp, __inittext_begin, __inittext_end, text_prot,
			 &vmlinux_inittext, 0, VM_NO_GUARD);
	map_kernel_segment(pgdp, __initdata_begin, __initdata_end, PAGE_KERNEL,
			 &vmlinux_initdata, 0, VM_NO_GUARD);
	map_kernel_segment(pgdp, _data, _end, PAGE_KERNEL, &vmlinux_data, 0, 0);
	...
}
```
#### map_mem
```c
#define for_each_mem_range(i, p_start, p_end) \
	__for_each_mem_range(i, &memblock.memory, NULL, NUMA_NO_NODE,	\ // 这里只遍历 membloc.memory
			 MEMBLOCK_HOTPLUG, p_start, p_end, NULL)
#define __for_each_mem_range(i, type_a, type_b, nid, flags,		\
			 p_start, p_end, p_nid)			\
	for (i = 0, __next_mem_range(&i, nid, flags, type_a, type_b,	\
				 p_start, p_end, p_nid);		\
	 i != (u64)ULLONG_MAX;					\
	 __next_mem_range(&i, nid, flags, type_a, type_b,		\
			 p_start, p_end, p_nid))
void __init_memblock __next_mem_range_rev(u64 *idx, int nid,
					 enum memblock_flags flags,
					 struct memblock_type *type_a,
					 struct memblock_type *type_b,
					 phys_addr_t *out_start,
					 phys_addr_t *out_end, int *out_nid)
{
	...

	for (; idx_a >= 0; idx_a--) {
		struct memblock_region *m = &type_a->regions[idx_a];
		phys_addr_t m_start = m->base;
		phys_addr_t m_end = m->base + m->size;
		int m_nid = memblock_get_region_node(m);

		// 在遍历之前会检查是否有 MEMBLOCK_NOMAP 标识
		if (should_skip_region(type_a, m, nid, flags))
			continue;

		...

	}
	/* signal end of iteration */
	*idx = ULLONG_MAX;
}

static void __init map_mem(pgd_t *pgdp)
{
	static const u64 direct_map_end = _PAGE_END(VA_BITS_MIN);
	phys_addr_t kernel_start = __pa_symbol(_stext);
	phys_addr_t kernel_end = __pa_symbol(__init_begin);
	phys_addr_t start, end;
	int flags = NO_EXEC_MAPPINGS;
	u64 i;

	...

	/*
	 * Take care not to create a writable alias for the
	 * read-only text and rodata sections of the kernel image.
	 * So temporarily mark them as NOMAP to skip mappings in
	 * the following for-loop
	 */
	memblock_mark_nomap(kernel_start, kernel_end - kernel_start);
	...
	/* map all the memory banks */
	for_each_mem_range(i, &start, &end) {
		if (start >= end)
			break;
		/*
		 * The linear map must allow allocation tags reading/writing
		 * if MTE is present. Otherwise, it has the same attributes as
		 * PAGE_KERNEL.
		 */
		// 调用 __create_pgd_mapping 依次建立 4 级页表
		__map_memblock(pgdp, start, end, pgprot_tagged(PAGE_KERNEL),
			 flags);
	}
	/*
	 * Map the linear alias of the [_stext, __init_begin) interval
	 * as non-executable now, and remove the write permission in
	 * mark_linear_text_alias_ro() below (which will be called after
	 * alternative patching has completed). This makes the contents
	 * of the region accessible to subsystems such as hibernate,
	 * but protects it from inadvertent modification or execution.
	 * Note that contiguous mappings cannot be remapped in this way,
	 * so we should avoid them here.
	 */
	__map_memblock(pgdp, kernel_start, kernel_end,
		 PAGE_KERNEL, NO_CONT_MAPPINGS);
	memblock_clear_nomap(kernel_start, kernel_end - kernel_start);

	...

}
```

### memblock 内存管理算法

上面我们介绍的是 memblock 的初始化过程，该过程中，会调用 memblock_add, memblock_reserve 等函数来添加内存快，下面我们深入分析一下 memblock 是怎样管理这些内存块的。。

先梳理一下 memblock 提供的接口：

- memblock_add: 添加内存块到 memblock.memory 中；
- memblock_remove: 从 memblock.memory 中删除指定的内存块；
- memblock_free: 从 memblock.reserved 中删除指定的内存块；
- memblock_reserve: 添加内存块到 memblock.reserved 中；
- memblock_add_node: 添加内存块到 memblock.memory 中指定的 region；

代码树展开：

```plain
memblock_add_range()
| -- memblock_insert_region();
|
| -- memblock_double_array();
|
| -- memblock_merge_regions();
```

#### memblock_add_range()

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
	phys_addr_t end = base + memblock_cap_size(base, &size);
	int idx, nr_new;
	struct memblock_region *rgn;

	if (!size)
		return 0;

	/* special case for empty array */
	if (type->regions[0].size == 0) { // 该type内存的第一个region
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

	for_each_memblock_type(idx, type, rgn) { // 遍历所有的regions
		phys_addr_t rbase = rgn->base;
		phys_addr_t rend = rbase + rgn->size;

		if (rbase >= end)
			break;
		if (rend <= base)
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

如果内存区内已经包含其他的内存区块，那么函数继续执行。函数首先调用 for_each_memblock_type() 函数遍历该内存区内的所有内存区块，每遍历到一个内存区块， 函数会将新的内存区块和该内存区块进行比较，这两个内存区块一共会出现 11 种情况，但函数将这么多的情况分作三种进行统一处理：

**遍历到的内存区块的起始地址大于或等于新内存区块的结束地址，新的内存区块位于遍历到内存区块的前端**

对于这类，会存在两种情况，分别为：

```plain
(1) rbase > end

 base                    end        rbase               rend
 +-----------------------+          +-------------------+
 |                       |          |                   |
 | New region            |          | Exist regions     |
 |                       |          |                   |
 +-----------------------+          +-------------------+

(2) rbase == endi

                         rbase                      rend
                        | <----------------------> |
 +----------------------+--------------------------+
 |                      |                          |
 | New region           | Exist regions            |
 |                      |                          |
 +----------------------+--------------------------+
 | <------------------> |
 base                   end
```

由于于**内存区内的所有内存区块都是按其首地址从低到高排列**（但并不是低地址都已经被分配了），对于这类情况，函数会直接退出 for_each_memblock() 循环，直接进入下一个判断，此时新内 存块的基地址都小于其结束地址，这样函数就会将新的内存块加入到内存区的链表中去。

**遍历到的内存区块的终止地址小于或等于新内存区块的起始地址, 新的内存区块位于遍历到内存区块的后面**

对于这类情况，会存在两种情况，分别为：

```plain
(1) base > rend
 rbase                rend         base                  end
 +--------------------+            +---------------------+
 |                    |            |                     |
 |   Exist regions    |            |      new region     |
 |                    |            |                     |
 +--------------------+            +---------------------+

(2) base == rend
                      base
 rbase                rend                     end
 +--------------------+------------------------+
 |                    |                        |
 |   Exist regions    |       new region       |
 |                    |                        |
 +--------------------+------------------------+
```

对于这类情况，函数会在 for_each_memblock() 中继续循环遍历剩下的节点，直到找到新加的内存区块与已遍历到的内存区块**存在其他类情况**。也可能出现遍历的内存区块是内存区最后一块内存区块，那么函数就会结束 for_each_memblock() 的循环，这样的话新内存区块还是和最后 一块已遍历的内存区块**保持这样的关系**。接着函数检查到新的内存区块的基地址小于其结束地址， 那么函数就将这块内存区块加入到内存区链表内。

**其他情况，两个内存区块存在重叠部分**

剩余的情况中，新的内存区块都与已遍历到的内存区块存在重叠部分，但可以分做两种情况进行处理：

**（1）新内存区块不重叠部分位于已遍历内存区块的前部**

**（2）新内存区块不重叠部分位于已遍历内存区块的后部**

对于第一种情况，典型的模型如下：

```plain
                 rbase     Exist regions        rend
                 | <--------------------------> |
 +---------------+--------+---------------------+
 |               |        |                     |
 |               |        |                     |
 |               |        |                     |
 +---------------+--------+---------------------+
 | <--------------------> |
 base   New region        end
```

当然还有其他几种情况也满足这种情况，但这种情况的显著特征就是不重叠部分位于已遍历的内存区块的前部。对于这种情况，函数在调用 for_each_memblock() 循环的时候，只要探测到这种情况的时候，函数就会直接调用 memblock_insert_region() 函数**将不重叠部分直接加入到内存区链表里**，新加入的部分在内存区链表中位于已遍历内存区块的前面。执行完上面的函数之后， 调用 min 函数重新调整新内存区块的基地址，新调整的内存区块可能 base 与 end 也可能出现 两种情况：

a. base < end

b. base == end

如果 base == end 情况，那么新内存区块在这部分代码段已经执行完成。对于 base 小于 end 的情况，函数继续调用 memblock_insert_region() 函数将剩下的内存区块加入到内存区块链表内。

对于第二种情况，典型的模型如下图：

```plain
rbase                     rend
| <---------------------> |
+----------------+--------+----------------------+
|                |        |                      |
| Exist regions  |        |                      |
|                |        |                      |
+----------------+--------+----------------------+
                 | <---------------------------> |
                 base      new region            end
```

对于这种情况，函数会继续在 for_each_memblock() 中循环，并且每次循环中，都调用 min 函数更新新内存区块的基地址，并不断循环，使其不与已存在的内存区块重叠或出现其他位置。 如果循环结束时，新的内存区块满足 base < end 的情况，那么就调用 memblock_insert_region() 函数将剩下的内存区块加入到内存区块链表里。

接下来的代码片段首先检查 nr_new 参数，这个参数用于**指定有没有新的内存区块需要加入到内存区块链表**。这里对这几个参数有问题：nr_new 和 insert，以及为什 么要 repeat？

设计这部分代码的开发者的基本思路就是：**第一次通过 insert 和 nr_new 变量只检查新的内存区块是否加入到内存区块以及要加入几个内存区块**(在有的一个内存区块由于 与已经存在的内存区块存在重叠被分成了两块，所以这种情况下，一块新的内存区块加入时就需要向内存区块链表中加入两块内存区块)，通过这样的检测之后，函数就在上面的代码中检测现有的内存区是否能存储下这么多的内存区块，**如果不能**，则调用 memblock_double_array() 函数**增加现有内存区块链表的长度**。检测完毕之后，函数就执行真正的加入工作，将新的内存区块都加入到内存区块链表内。执行完以上操作之后，函数最后调用 memblock_merge_regions() 函数将内存区块链表中可以合并的内存区块进行合并。

```c
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
```

#### memblock_insert_region()

该函数的作用就是**将一个内存区块插入到内存区块链表的指定位置**。

函数首先检查内存区块链表是否已经超出最大内存区块数，如果是则报错。接着函数调用 memmove() 函数将内存区块链表中 idx 对应的内存区块以及之后的内存区块都往内存区块链表**后移一个位置**，然后将空出来的位置给新的内存区使用。移动完之后就是更新相关的数据。

```c
/**
 * memblock_insert_region - insert new memblock region
 * @type:	memblock type to insert into
 * @idx:	index for the insertion point
 * @base:	base address of the new region
 * @size:	size of the new region
 * @nid:	node id of the new region
 * @flags:	flags of the new region
 *
 * Insert new memblock region [@base, @base + @size) into @type at @idx.
 * @type must already have extra room to accommodate the new region.
 */
static void __init_memblock memblock_insert_region(struct memblock_type *type,
						   int idx, phys_addr_t base,
						   phys_addr_t size,
						   int nid,
						   enum memblock_flags flags)
{
	struct memblock_region *rgn = &type->regions[idx];

	BUG_ON(type->cnt >= type->max);
	memmove(rgn + 1, rgn, (type->cnt - idx) * sizeof(*rgn));
	rgn->base = base;
	rgn->size = size;
	rgn->flags = flags;
	memblock_set_region_node(rgn, nid);
	type->cnt++;
	type->total_size += size;
}
```

#### memblock_merge_regions()

函数的作用就是**将内存区对应的内存区块链表中能合并的内存区块进行合并**。

函数通过遍历内存区块链表内存的所有内存区块，如果满足两个内存区是连接在一起的，以及 NUMA 号相同，flags 也相同，那么这两块内存区块就可以合并；反之只要其中一个条件不满足， 那么就不能合并。合并两个内存区块就是调用 memmove() 函数，首先将能合并的两个内存区块数据进行更新，将前一块的 size 增加后一块的 size，然后将后一块的下一块开始的 i - 2 块往 前移一个位置，那么合并就完成了。

```c
/**
 * memblock_merge_regions - merge neighboring compatible regions
 * @type: memblock type to scan
 *
 * Scan @type and merge neighboring compatible regions.
 */
static void __init_memblock memblock_merge_regions(struct memblock_type *type)
{
	int i = 0;

	/* cnt never goes below 1 */
	while (i < type->cnt - 1) {
		struct memblock_region *this = &type->regions[i];
		struct memblock_region *next = &type->regions[i + 1];

		if (this->base + this->size != next->base ||
		    memblock_get_region_node(this) !=
		    memblock_get_region_node(next) ||
		    this->flags != next->flags) {
			BUG_ON(this->base + this->size > next->base);
			i++;
			continue;
		}

		this->size += next->size;
		/* move forward from next + 1, index of which is i + 2 */
		memmove(next, next + 1, (type->cnt - (i + 2)) * sizeof(*next));
		type->cnt--;
	}
}
```

### 进入 buddy 系统
#### 内存模型初始化
这部分涉及到的知识比较多，我们一个个分析，
```c
| setup_arch
| 	-> bootmem_init
| 		-> arch_numa_init // 空函数
|		// 当前内存采用的是 sparse 内存模型，即物理内存存在空洞
|		// 并且支持内存热插拔（这是个啥），以 section 为单位进行管理
|		-> kvm_hyp_reserve // 预留 hypervisor 需要使用的内存
| 		-> sparse_init
|			// 对 memblock.memory 中的内存执行 memory_present 函数
|			// 每 128MB 为一个 mem_section
|			// 可为啥要这样管理呢？为了热拔插好管理？
|			// 是的，这样每个 mem_section 都是可以热拔插的
| 			-> memblocks_present
|			// 遍历每个 node 中的每个 mem_section
|			// 执行 vmemmap_populate 函数，建立 vmemmap 到 page frame 的页表
|			// 建立页表就是调用 vmemmap_pgd_populate 等函数
|			// 如果该级页表已经存在，直接获取，如果不存在，申请并写入
| 			-> sparse_init_nid
|				-> __populate_section_memmap
|					-> vmemmap_populate
|						-> vmemmap_populate_address // 创建 4/5 级页表
|				// 初始化 mem_section 结构体
|				-> sparse_init_one_section
| 		-> zone_sizes_init
|			// 确定好系统中每个 zone 的地址范围，这些信息会在系统初始化时打印出来
|			// 然后遍历每个 node，初始化对应的 pg_data_t
| 			-> free_area_init
|				-> free_area_init_node
|					-> calculate_node_totalpages // 计算 zone->present_pages 等变量
|					// 最重要的是初始化 zone->free_area 和 zone->free_area[order].free_list[t] 变量
|					-> free_area_init_core
|				// 根据 pfn 找到对应的 page，并进行初始化
|				// 在 sparse_init_nid 函数执行完后，pfn_to_page 就能够使用了
|				// 初始化完后，会将该 page 置成 MIGRATE_MOVABLE 类型（那其他类型的 page 在哪里初始化？）
|				-> memmap_init
```
##### mem_section
该结构体是 sparse 内存模型的核心，每个 mem_section 表示 128MB(kernel-6.6)，section 内的内存都是连续的，每个 section 都是可插拔的。
```c
struct mem_section {
	unsigned long section_mem_map; // 指向 mem_map，保存 page 结构体
	struct mem_section_usage *usage;
#ifdef CONFIG_PAGE_EXTENSION
	/*
	 * If SPARSEMEM, pgdat doesn't have page_ext pointer. We use
	 * section. (see page_ext.h about this.)
	 */
	struct page_ext *page_ext;
	unsigned long pad;
#endif
};
```
##### sparse_init
初始化 sparse 内存模型（？），就是以 mem_section 为单位管理所有的物理内存（包括预留的），kernel6.6 上每个 mem_section 为 128MB，这个值和 PMD 指向多少个 PTE 有关系。
```c
/*
 * Allocate the accumulated non-linear sections, allocate a mem_map
 * for each and record the physical to section mapping.
 */
void __init sparse_init(void)
{
	unsigned long pnum_end, pnum_begin, map_count = 1;
	int nid_begin;
	memblocks_present(); // 对 memblock.memory 中的内存执行 memory_present 函数
	pnum_begin = first_present_section_nr();
	nid_begin = sparse_early_nid(__nr_to_section(pnum_begin));
	/* Setup pageblock_order for HUGETLB_PAGE_SIZE_VARIABLE */
	set_pageblock_order();
 	// 遍历所有的 mem_section
	for_each_present_section_nr(pnum_begin + 1, pnum_end) {
		int nid = sparse_early_nid(__nr_to_section(pnum_end));
		if (nid == nid_begin) {
			map_count++;
			continue;
		}
		/* Init node with sections in range [pnum_begin, pnum_end) */
		sparse_init_nid(nid_begin, pnum_begin, pnum_end, map_count);
		nid_begin = nid;
		pnum_begin = pnum_end;
		map_count = 1;
	}
	/* cover the last node */
	sparse_init_nid(nid_begin, pnum_begin, pnum_end, map_count);
	vmemmap_populate_print_last();
}
```
##### memory_present
这个函数会以 128MB 为一个 `mem_secton` 进行初始化，然后将该 `mem_section` 置上 `SECTION_MARKED_PRESENT` 位。
```c
/* Record a memory area against a node. */
static void __init memory_present(int nid, unsigned long start, unsigned long end)
{
	unsigned long pfn;
	...
	start &= PAGE_SECTION_MASK;
 	// 防止 page frame 越界
	mminit_validate_memmodel_limits(&start, &end);

 	// 相当于每个 mem_section 2^27 = 128MB
 	// 那就是 memory 中所有的内存都会纳入到 mem_section 中管理，包括预留内存
	for (pfn = start; pfn < end; pfn += PAGES_PER_SECTION) {
		unsigned long section = pfn_to_section_nr(pfn);
		struct mem_section *ms;
		sparse_index_init(section, nid);
		set_section_nid(section, nid);

 		// ms 指向 sparse_index_init 中 alloc 的 mem_section
 		// 该变量已经放到全局变量 mem_section 中
		// 变量命名具有迷惑性，变量类型和变量名都一样
		ms = __nr_to_section(section);
		if (!ms->section_mem_map) {
			ms->section_mem_map = sparse_encode_early_nid(nid) |
							SECTION_IS_ONLINE;
			__section_mark_present(ms, section);
		}
	}
}
```
##### sparse_init_nid
遍历每个 node 中的每个 mem_section，执行 `vmemmap_populate` 函数，建立 vmemmap 到 page frame 的页表，
```c
static void __init sparse_init_nid(int nid, unsigned long pnum_begin,
				 unsigned long pnum_end,
				 unsigned long map_count)
{
	struct mem_section_usage *usage;
	unsigned long pnum;
	struct page *map;
	usage = sparse_early_usemaps_alloc_pgdat_section(NODE_DATA(nid),
			mem_section_usage_size() * map_count);
	...

	sparse_buffer_init(map_count * section_map_size(), nid);
	for_each_present_section_nr(pnum_begin, pnum) {
		unsigned long pfn = section_nr_to_pfn(pnum);
		if (pnum >= pnum_end)
			break;

		// 获取该 section 对应的 page 结构体地址
		// 如果使能了 vmemmap 模型，则地址范围在 vmemmap 区域中，需要建立 vmmemap 到 page frame 的页表。
		map = __populate_section_memmap(pfn, PAGES_PER_SECTION,
				nid, NULL, NULL);
		...

		check_usemap_section_nr(nid, usage);

 		// 将映射的 vmemmap 区域的地址写入到 mem_section->section_mem_map 中
 		// 后续可以通过该变量和 pfn 找到对应的 page
		sparse_init_one_section(__nr_to_section(pnum), pnum, map, usage,
				SECTION_IS_EARLY);
		usage = (void *) usage + mem_section_usage_size();
	}
	sparse_buffer_fini();
	return;
 ...
}
```
##### vmemmap_populate_range
这个操作没看懂。
```
| __populate_section_memmap
| 	-> vmemmap_populate
| 		-> vmemmap_populate_basepages
```
```c
static int __meminit vmemmap_populate_range(unsigned long start,
					 unsigned long end, int node,
					 struct vmem_altmap *altmap,
					 struct page *reuse)
{
	unsigned long addr = start;
	pte_t *pte;

 	// 为 mem_section 中的每个 page 建立映射？
	for (; addr < end; addr += PAGE_SIZE) {
		pte = vmemmap_populate_address(addr, node, altmap, reuse);
		if (!pte)
			return -ENOMEM;
	}
	return 0;
}

static pte_t * __meminit vmemmap_populate_address(unsigned long addr, int node,
					 struct vmem_altmap *altmap,
					 struct page *reuse)
{
	pgd_t *pgd;
	p4d_t *p4d;
	pud_t *pud;
	pmd_t *pmd;
	pte_t *pte;

	pgd = vmemmap_pgd_populate(addr, node);
	if (!pgd)
		return NULL;
	p4d = vmemmap_p4d_populate(pgd, addr, node);
	if (!p4d)
		return NULL;
	pud = vmemmap_pud_populate(p4d, addr, node);
	if (!pud)
		return NULL;
	pmd = vmemmap_pmd_populate(pud, addr, node);
	if (!pmd)
		return NULL;

 	// 最终写入到 init_mm 中
 	// addr 是物理地址，虚拟地址在这个函数里面获取的
	pte = vmemmap_pte_populate(pmd, addr, node, altmap, reuse);
	if (!pte)
		return NULL;
	vmemmap_verify(pte, node, addr, addr + PAGE_SIZE);
	return pte;
}
```
建立映射之后就可以通过如下宏在 page 和 pfn 之间转换，
```c
#define VMEMMAP_START		(-(UL(1) << (VA_BITS - VMEMMAP_SHIFT))) // 该段虚拟地址空间用来存放 struct page 结构体
#define VMEMMAP_END		(VMEMMAP_START + VMEMMAP_SIZE)

// 该段地址空间的大小要能覆盖线性地址映射区
// 即线性地址映射区所有的 page 都能有 struct page 存在这里
#define VMEMMAP_SIZE	((_PAGE_END(VA_BITS_MIN) - PAGE_OFFSET) >> VMEMMAP_SHIFT)
#define vmemmap			((struct page *)VMEMMAP_START - (memstart_addr >> PAGE_SHIFT))
#define __pfn_to_page(pfn)	(vmemmap + (pfn))
#define __page_to_pfn(page)	(unsigned long)((page) - vmemmap)
#define page_to_pfn __page_to_pfn
#define pfn_to_page __pfn_to_page
#define page_to_virt(x)	({						\
	__typeof__(x) __page = x;					\
	u64 __idx = ((u64)__page - VMEMMAP_START) / sizeof(struct page);\
	u64 __addr = PAGE_OFFSET + (__idx * PAGE_SIZE);			\
	(void *)__tag_set((const void *)__addr, page_kasan_tag(__page));\
})

#define virt_to_page(x)	({						\
	u64 __idx = (__tag_reset((u64)x) - PAGE_OFFSET) / PAGE_SIZE;	\
	u64 __addr = VMEMMAP_START + (__idx * sizeof(struct page));	\
	(struct page *)__addr;						\
})

static __always_inline void *lowmem_page_address(const struct page *page)
{
	return page_to_virt(page);
}
#define page_address(page) lowmem_page_address(page)
```
初始化之后的内存布局如下图所示，
![img](https://raw.githubusercontent.com/UtopianFuture/UtopianFuture.github.io/81305767754ca8df95d18da10465dd730bc093be/image/mem_section.svg)

##### free_area_init
该函数确定每个 zone(ZONE_DMA, ZONE_DMA32, ZONE_NORMAL 等) 的地址范围，以及每个 node(NUMA 架构有多个 node，UMA 架构只有一个 node)信息，每个 node 都由 pg_data_t 表示。
```c
| zone_sizes_init
| 	-> free_area_init
```
```c
void __init free_area_init(unsigned long *max_zone_pfn)
{
	unsigned long start_pfn, end_pfn;
	int i, nid, zone;
	bool descending;

	/* Record where the zone boundaries are */
	memset(arch_zone_lowest_possible_pfn, 0,
				sizeof(arch_zone_lowest_possible_pfn));
	memset(arch_zone_highest_possible_pfn, 0,
				sizeof(arch_zone_highest_possible_pfn));
	start_pfn = PHYS_PFN(memblock_start_of_DRAM());
	descending = arch_has_descending_max_zone_pfns();
	for (i = 0; i < MAX_NR_ZONES; i++) {
		if (descending)
			zone = MAX_NR_ZONES - i - 1;
		else
			zone = i;
		if (zone == ZONE_MOVABLE)
			continue;
		end_pfn = max(max_zone_pfn[zone], start_pfn);
		arch_zone_lowest_possible_pfn[zone] = start_pfn;
		arch_zone_highest_possible_pfn[zone] = end_pfn;
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
		pr_info(" %-8s ", zone_names[i]);
		if (arch_zone_lowest_possible_pfn[i] ==
				arch_zone_highest_possible_pfn[i])
			pr_cont("empty\n");
		else
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
			pr_info(" Node %d: %#018Lx\n", i,
			 (u64)zone_movable_pfn[i] << PAGE_SHIFT);
	}

	/*
	 * Print out the early node map, and initialize the
	 * subsection-map relative to active online memory ranges to
	 * enable future "sub-section" extensions of the memory map.
	 */
 	// 这里会打印 memblock.memory 信息
	pr_info("Early memory node ranges\n");
	for_each_mem_pfn_range(i, MAX_NUMNODES, &start_pfn, &end_pfn, &nid) {
		pr_info(" node %3d: [mem %#018Lx-%#018Lx]\n", nid,
			(u64)start_pfn << PAGE_SHIFT,
			((u64)end_pfn << PAGE_SHIFT) - 1);
		subsection_map_init(start_pfn, end_pfn - start_pfn);
	}

	/* Initialise every node */
	mminit_verify_pageflags_layout();
	setup_nr_node_ids();
	set_pageblock_order();
	for_each_node(nid) {
		pg_data_t *pgdat;

		...

		pgdat = NODE_DATA(nid);
		free_area_init_node(nid); // 核心函数，初始化 pglist_data

		/* Any memory on that node */
		if (pgdat->node_present_pages)
			node_set_state(nid, N_MEMORY);
		check_for_memory(pgdat);
	}

	memmap_init();
	/* disable hash distribution for systems with a single node */
	fixup_hashdist();
}
```
##### free_area_init_core
free_area_init_node -> free_area_init_node
```c
/*
 * Set up the zone data structures:
 * - mark all pages reserved
 * - mark all memory queues empty
 * - clear the memory bitmaps
 *
 * NOTE: pgdat should get zeroed by caller.
 * NOTE: this function is only called during early init.
 */
static void __init free_area_init_core(struct pglist_data *pgdat)
{
	enum zone_type j;
	int nid = pgdat->node_id;

	pgdat_init_internals(pgdat);
	pgdat->per_cpu_nodestats = &boot_nodestats;
	for (j = 0; j < MAX_NR_ZONES; j++) {
		struct zone *zone = pgdat->node_zones + j;
		unsigned long size, freesize, memmap_pages;

		... // 初始化 zone 中的 managed_pages, spanned_pages, name 等变量

		/*
		 * Set an approximate value for lowmem here, it will be adjusted
		 * when the bootmem allocator frees pages into the buddy system.
		 * And all highmem pages will be managed by the buddy system.
		 */
		zone_init_internals(zone, j, nid, freesize);
		if (!size)
			continue;
		setup_usemap(zone);
		// 调用 zone_init_free_lists 初始化 free_area 数组
		// 每个 free_area 都有多个链表，对应不同的迁移类型
		// 初始化完后会将 initialized 变量置为 1，表示该 zone 初始化完成
		init_currently_empty_zone(zone, zone->zone_start_pfn, size);
	}
}

static void __meminit zone_init_free_lists(struct zone *zone)
{
	unsigned int order, t;
	for_each_migratetype_order(order, t) {
		INIT_LIST_HEAD(&zone->free_area[order].free_list[t]);
		zone->free_area[order].nr_free = 0;
	}
#ifdef CONFIG_UNACCEPTED_MEMORY
	INIT_LIST_HEAD(&zone->unaccepted_pages);
#endif
}
```
#### buddy 初始化
memblock 始终是初始化阶段的内存管理模块，最终我们还是要转向 buddy 系统。而 memblock.memory 中 no-map 的内存块和 memory.reserved 中所有的内存块，即 dts 中配置的预留内存在该函数中都不会加入到 buddy 系统中。
```c
| mm_core_init
| 	-> mem_init
| 		-> memblock_free_all
```
#### memblock_free_all
```c
/**
 * memblock_free_all - release free pages to the buddy allocator
 */
void __init memblock_free_all(void)
{
	unsigned long pages;

	// 先将 memblock 中的内存信息删除掉
	// 从前面分析可知，memblock.memory 链表保存了所有的内存信息
	// 而 memblock.reserved 链表保存了所有的预留内存信息（不包含 no-map 类型）
	// 经过调试，因为 CONFIG_SPARSEMEM_VMEMMAP 打开了
	// 这个函数没有执行，被优化了
	free_unused_memmap();
	// 将 zone->managed_pages 配置为 0
	// 不懂这个变量有啥用
	reset_all_zones_managed_pages();
	pages = free_low_memory_core_early(); // 将所有的内存添加到 buddy 中管理
	totalram_pages_add(pages); // 增加 _totalram_pages
}
```
#### free_low_memory_core_early
```c
static unsigned long __init free_low_memory_core_early(void)
{
	unsigned long count = 0;
	phys_addr_t start, end;
	u64 i;

	memblock_clear_hotplug(0, -1);
	memmap_init_reserved_pages();

	/*
	 * We need to use NUMA_NO_NODE instead of NODE_DATA(0)->node_id
	 * because in some case like Node0 doesn't have RAM installed
	 * low ram will be on Node1
	 */
	// 遍历的时候会跳过 no-map 类型的内存
	// 同时预留内存也不会进入 buddy 系统管理
	// 还有些类型的内存也不会进入 buddy，如 MEMBLOCK_HOTPLUG 等
	for_each_free_mem_range(i, NUMA_NO_NODE, MEMBLOCK_NONE, &start, &end,
				NULL)

	// 做一些必要的操作
	// 如 ClearReserved，set_page_count
	// 最后调用 __free_pages_ok 释放 page 到 buddy 系统
	// 这个过程中会根据内存块的大小决定放到哪个 order 的 free_list 中
		count += __free_memory_core(start, end);
	return count;
}
```
#### memmap_init_reserved_pages
```c
static void __init memmap_init_reserved_pages(void)
{
	struct memblock_region *region;
	phys_addr_t start, end;
	int nid;

	/*
	 * set nid on all reserved pages and also treat struct
	 * pages for the NOMAP regions as PageReserved
	 */
 	// 遍历 memblock.memory 链表
	for_each_mem_region(region) {
		nid = memblock_get_region_node(region);
		start = region->base;
		end = start + region->size;
 		// 这个循环就干一件事
 		// 找到所有的 no-map 类型的内存
 		// 转换成 page，然后将该 page 置成 reserved
		if (memblock_is_nomap(region))
			reserve_bootmem_region(start, end, nid);
		memblock_set_node(start, end, &memblock.reserved, nid);
	}

	/* initialize struct pages for the reserved regions */
 	// 遍历 memblock.reserved 链表
 	// 和上一个循环一样，为所有的内存初始化 page，然后置成 reserved
	for_each_reserved_mem_region(region) {
		nid = memblock_get_region_node(region);
		start = region->base;
		end = start + region->size;
		if (nid == NUMA_NO_NODE || nid >= MAX_NUMNODES)
			nid = early_pfn_to_nid(PFN_DOWN(start));
		reserve_bootmem_region(start, end, nid);
	}
}
```
我们可以通过 qemu 看看实际结果，
这是在 `free_low_memory_core_early` 函数中增加打印显示的结果，
```c
[ 0.000000] free_low_memory_core_early, start: 0x42f25000, end: 0x42f28000
[ 0.000000] free_low_memory_core_early, start: 0x42f30000, end: 0x48000000
[ 0.000000] free_low_memory_core_early, start: 0x49be5000, end: 0x49c00000
[ 0.000000] free_low_memory_core_early, start: 0x49c0931a, end: 0x4a080000
[ 0.000000] free_low_memory_core_early, start: 0x4ae80000, end: 0x50000000
[ 0.000000] free_low_memory_core_early, start: 0x54000000, end: 0xbd800000
[ 0.000000] free_low_memory_core_early, start: 0xbfa00000, end: 0xbfabc000
[ 0.000000] free_low_memory_core_early, start: 0xbfbe2000, end: 0xbfbe4000
[ 0.000000] free_low_memory_core_early, start: 0xbfbe5000, end: 0xbfbe56c0
[ 0.000000] free_low_memory_core_early, start: 0xbfbe5bc8, end: 0xbfbe5c00
[ 0.000000] free_low_memory_core_early, start: 0xbfbf9590, end: 0xbfbf95c0
[ 0.000000] free_low_memory_core_early, start: 0xbfbf9620, end: 0xbfbf9640
[ 0.000000] free_low_memory_core_early, start: 0xbfbf97c8, end: 0xbfbf9800
[ 0.000000] free_low_memory_core_early, start: 0xbfbf9a10, end: 0xbfbf9a40
[ 0.000000] free_low_memory_core_early, start: 0xbfbf9b60, end: 0xbfbf9b80
[ 0.000000] free_low_memory_core_early, start: 0xbfbf9b90, end: 0xbfbf9bc0
[ 0.000000] free_low_memory_core_early, start: 0xbfbf9bc8, end: 0xbfbf9c00
[ 0.000000] free_low_memory_core_early, start: 0xbfbf9c08, end: 0xbfbf9c40
[ 0.000000] free_low_memory_core_early, start: 0xbfbf9c48, end: 0xbfbf9c80
[ 0.000000] free_low_memory_core_early, start: 0xbfbf9cb8, end: 0xbfbf9cc0
[ 0.000000] free_low_memory_core_early, start: 0xbfc05ffc, end: 0xbfc06000
```
这是 cat /sys/kernel/debug/memblock/reserved 和 cat /sys/kernel/debug/memblock/memory 的结果，
```c
estuary:/$ cat /sys/kernel/debug/memblock/reserved
 0: 0x0000000040000000..0x0000000042f24fff
 1: 0x0000000042f28000..0x0000000042f2ffff
 2: 0x0000000049c00000..0x0000000049c09319
 3: 0x0000000050000000..0x0000000053ffffff
 4: 0x00000000bd800000..0x00000000bf9fffff
 5: 0x00000000bfabc000..0x00000000bfbe1fff
 6: 0x00000000bfbe4000..0x00000000bfbe4fff
 7: 0x00000000bfbe56c0..0x00000000bfbe5bc7
 8: 0x00000000bfbe5c00..0x00000000bfbf958f
 9: 0x00000000bfbf95c0..0x00000000bfbf961f
 10: 0x00000000bfbf9640..0x00000000bfbf97c7
 11: 0x00000000bfbf9800..0x00000000bfbf9a0f
 12: 0x00000000bfbf9a40..0x00000000bfbf9b5f
 13: 0x00000000bfbf9b80..0x00000000bfbf9b8f
 14: 0x00000000bfbf9bc0..0x00000000bfbf9bc7
 15: 0x00000000bfbf9c00..0x00000000bfbf9c07
 16: 0x00000000bfbf9c40..0x00000000bfbf9c47
 17: 0x00000000bfbf9c80..0x00000000bfbf9cb7
 18: 0x00000000bfbf9cc0..0x00000000bfc05ffb
 19: 0x00000000bfc06000..0x00000000bfffffff
estuary:/$ cat /sys/kernel/debug/memblock/memory
 0: 0x0000000040000000..0x000000004a07ffff
 1: 0x000000004a080000..0x000000004a47ffff
 2: 0x000000004a480000..0x00000000bfffffff
```
可以看出，memblock.reserved 链表记录的预留的内存信息，memblock.memory 链表记录的是整个 DRAM 内存空间的信息，包括 no-map 类型的内存信息。而 memblock.reserved 和 memblock.memory 链表中的内存是不会还给 buddy 系统的，从而达到预留的目的。
### 问题
- 为什么需要 memblock，而不直接使用 buddy？
 - 内核需要感知真正的物理内存，buddy 只需要知道有哪些内存即可，不需要维护哪些物理内存是可以使用的，哪些内存的预留的；
- memblock 初始化时机；
 - start_kernel，即内核启动阶段；
- 如何预留内存；
 - 在 dts 的 reserved_memory 节点中配置；
- alloc-ranges（动态分配） 和 reg（静态预留）分别是怎样实现的；
 - alloc-ranges 在 `__reserved_mem_alloc_size` 中会遍历 memblock 分配合适的内存块，reg 就是读取 DTS 的地址信息；
- map 和 no-map 有何区别；
 - 在 `page_init` 建立虚实地址映射时，会检查 memblock 内存块的标志位，如果配置了 MEMBLOC_NOMAP，就不会建立映射；
 - no-map 属性的内存会添加到 memblock.memory 链表中，其他属性的内存（map, cma）会添加到 memblock.reserved 链表中；
- cma 类型的内存时怎样初始化的；
 - 每块预留内存都会存储到 `static struct reserved_mem reserved_mem[MAX_RESERVED_REGIONS];` 数组中，在 `fdt_init_reserved_mem` 会对每块预留内存进行初始化，cma 类型内存定义了如下的回调函数 `rmem_cma_setup`；
 ```c
 #define RESERVEDMEM_OF_DECLARE(name, compat, init) \
 _OF_DECLARE(reservedmem, name, compat, init, reservedmem_of_init_fn)
 RESERVEDMEM_OF_DECLARE(cma, "shared-dma-pool", rmem_cma_setup);
 ```
- 初始化阶段如何进行映射；
 - 先对 fixmap 区域进行映射，使得能够读取 DTS，fixmap 区域的虚拟地址是固定的，物理地址是从 DTS 中读取出来的。这部分映射建立好后，会建立内核代码段、数据段等的映射，然后建立通过 memblock_add 添加到 memblock 中的内存区域的映射；
- 初始化阶段如何申请释放内存；
 - 梳理 memblock 各个申请/释放接口，以及使用用例；
- 各个镜像是如何预留内存的；
 - 在 dts 的 reserved_memory 节点中配置；
- 内存初始化后 layout 是什么样的？线性/非线性映射区，代码段，数据段，bss段如何布局；
 - 见内存布局章节；
- no-map 属性有什么作用；
 - no-map 属性和地址映射相关，**如果没有 no-map 属性，那么 OS 会为这段 memory 创建地址映射，像其他普通内存一样**。但是有 no-map 属性的往往是专用于某个设备驱动，在驱动中会进行 ioremap，如果 OS 已经对这段地址进行了 mapping，而驱动又一次 mapping，这样就有**不同的虚拟地址 mapping 到同一个物理地址上去**，在某些 ARCH 上（ARMv6 之后的 cpu），会造成不可预知的后果；
- memblock 管理的内存怎样转到 buddy 中管理；

### reference

[1] https://biscuitos.github.io/blog/MMU-ARM32-MEMBLOCK-memblock_add_range/

这篇博客确实帮助很大，讲解很详细，而且分析代码的方式也值得学习，一段段代码贴出来，而不是整个函数复制。然后通过简单的作图，更容易理解，我看代码光靠头脑风暴，不太行，之后也可以采用这种方法。
