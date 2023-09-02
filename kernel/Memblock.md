## Memblock

[TOC]

### memblock 原理

MEMBLOCK 内存分配器作为早期的内存管理器，维护了两种内存。第一种内存是系统可用的物理内存，即系统实际含有的物理内存，其值从 DTS 中进行配置，并通过 uboot 实际探测之 后传入到内核。第二种内存是内核预留给操作系统的内存（reserved memory），这部分内存作为特殊功能使用，不能作为共享内存使用，memblock 通过 dts 信息来初始化这部分内存。MEMBLOCK 内存分配器基础框架如下：

```plain
MEMBLOCK


                                         struct memblock_region
                       struct            +------+------+--------+------+
                       memblock_type     |      |      |        |      |
                       +----------+      | Reg0 | Reg1 | ...    | Regn |
                       |          |      |      |      |        |      |
                       | regions -|----->+------+------+--------+------+
                       | cnt      |      [memblock_memory_init_regions]
                       |          |
 struct           o--->+----------+
 memblock         |
 +-----------+    |
 |           |    |
 | memory   -|----o
 | reserved -|----o
 |           |    |                      struct memblock_region
 +-----------+    |    struct            +------+------+--------+------+
                  |    memblock_type     |      |      |        |      |
                  o--->+----------+      | Reg0 | Reg1 | ...    | Regn |
                       |          |      |      |      |        |      |
                       | regions -|----->+------+------+--------+------+
                       | cnt      |      [memblock_reserved_init_regions]
                       |          |
                       +----------+
```

从上面的逻辑图可以知道，MEMBLOCK 分配器使用一个 struct memblock 结构维护着两种内存， 其中成员 memory 维护着可用物理内存区域；成员 reserved 维护着操作系统预留的内存区域。 每个区域使用数据结构 struct memblock_type 进行管理，其成员 regions 负责维护该类型内存的所有内存区，每个内存区使用数据结构 struct memblock_region 进行维护。

### 相关数据结构

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
        bool bottom_up;  /* is bottom up direction? */
        phys_addr_t current_limit; // 指定当前 MEMBLOCK 分配器在上限
        struct memblock_type memory; // 可用物理内存
        struct memblock_type reserved; // 操作系统预留的内存
#ifdef CONFIG_HAVE_MEMBLOCK_PHYS_MAP
        struct memblock_type physmem;
#endif
};
```

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
	unsigned long cnt; // 当前类型的物理内存管理内存区块的数量
	unsigned long max; // 最大可管理内存区块的数量
	phys_addr_t total_size; // 已经管理内存区块的总体积
	struct memblock_region *regions;
	char *name;
};
```

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

### memblock 源码分析

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

### reserved memory 源码分析

上面介绍到，memblock 管理的内存分为两个部分：系统可用的物理内存和内核预留给操作系统的内存（reserved memory），这里我们进一步分析一下 memblock 是怎样通过解析设备树来构建内存信息的。

#### 建立 memory type 的内存块

setup_arch--->setup_machine_fdt--->early_init_dt_scan--->early_init_dt_scan_nodes--->memblock_add

setup_arch 是架构相关的初始化，在这里解析设备树，

```c
void __init __no_sanitize_address setup_arch(char **cmdline_p)
{
	setup_initial_init_mm(_stext, _etext, _edata, _end);

	*cmdline_p = boot_command_line;

	...

	setup_machine_fdt(__fdt_pointer); // 这个地址是 dts 的地址，在 head.S 中赋值

	...

	arm64_memblock_init(); // 所有的内存都以 memblock->memory region 的方式管理

	...
}
```

setup_machine_fdt->early_init_dt_scan->early_init_dt_scan_nodes，在这个函数中解析设备树，但有个问题，解析出来的信息怎么没有一个全局变量保存？其实是有保存的，以 {size, address} 为例，dt_root_size_cells 和 dt_root_addr_cells 就是用来保存这两个值的，其他的也一样。

```c
void __init early_init_dt_scan_nodes(void)
{
	int rc = 0;

    // of_scan_flat_dt 遍历所有的 node，对每个 node 调用对应的回调函数
	/* Initialize {size,address}-cells info */
	of_scan_flat_dt(early_init_dt_scan_root, NULL);

	/* Retrieve various information from the /chosen node */
	rc = of_scan_flat_dt(early_init_dt_scan_chosen, boot_command_line);
	if (!rc)
		pr_warn("No chosen node found, continuing without\n");

	/* Setup memory, calling early_init_dt_add_memory_arch */
	of_scan_flat_dt(early_init_dt_scan_memory, NULL);

	/* Handle linux,usable-memory-range property */
    // 这种属性的内存我们之后遇到再分析
	early_init_dt_check_for_usable_mem_range();
}
```

这里面 early_init_dt_scan_memory 需要详细看一下，它是用来解析 memory node 的。

```c
/*
 * early_init_dt_scan_memory - Look for and parse memory nodes
 */
int __init early_init_dt_scan_memory(unsigned long node, const char *uname,
				     int depth, void *data)
{
    // 这里和 dts 中的 memory node 对上了
    // memory node 的 device_type 就是 memory
	const char *type = of_get_flat_dt_prop(node, "device_type", NULL);
	const __be32 *reg, *endp;
	int l;
	bool hotpluggable;

	/* We are scanning "memory" nodes only */
	if (type == NULL || strcmp(type, "memory") != 0)
		return 0;

    // 目前我看到的没有这一属性
    // 使用的是 reg 指定内存区域
	reg = of_get_flat_dt_prop(node, "linux,usable-memory", &l);
	if (reg == NULL)
		reg = of_get_flat_dt_prop(node, "reg", &l);
	if (reg == NULL)
		return 0;

	endp = reg + (l / sizeof(__be32));
    // 这个属性也没看到，看来在 dts 就支持内存条热拔插
	hotpluggable = of_get_flat_dt_prop(node, "hotpluggable", NULL);

	pr_debug("memory scan node %s, reg size %d,\n", uname, l);

    // 解析 reg
	while ((endp - reg) >= (dt_root_addr_cells + dt_root_size_cells)) {
		u64 base, size;

        // ddr 的物理内存可能不是整个连续的映射到虚拟地址空间，中间存在空洞
        // 这些空洞是其他 master 使用的
		base = dt_mem_next_cell(dt_root_addr_cells, &reg);
		size = dt_mem_next_cell(dt_root_size_cells, &reg);

		...

        // 在这里调用 memblock_add 加入到 memory 部分的 memory region 中管理
        // memblock_add 中做必要的范围检查，没问题就可以调用 memblock_add_range
        // 剩下的就回到开头介绍的
        // 当然这里只是解析了整个 ddr 内存空间，对于 reserved memory 还没有处理
		early_init_dt_add_memory_arch(base, size);

		...
	}

	return 0;
}
```

#### 建立 reserved type 的内存块

setup_arch--->arm64_memblock_init--->early_init_fdt_scan_reserved_mem--->__fdt_scan_reserved_mem--->memblock_reserve

这里解析所有的 reserved memory node，

```c
/*
 * __fdt_scan_reserved_mem() - scan a single FDT node for reserved memory
 */
static int __init __fdt_scan_reserved_mem(unsigned long node, const char *uname,
					  int depth, void *data)
{
	static int found;
	int err;

	if (!found && depth == 1 && strcmp(uname, "reserved-memory") == 0) {
		if (__reserved_mem_check_root(node) != 0) {
			pr_err("Reserved memory: unsupported node format, ignoring\n");
			/* break scan */
			return 1;
		}
		found = 1;
		/* scan next node */
		return 0;
	} else if (!found) {
		/* scan next node */
		return 0;
	} else if (found && depth < 2) {
		/* scanning of /reserved-memory has been finished */
		return 1;
	}

    // 解析 status 字段
	if (!of_fdt_device_is_available(initial_boot_params, node))
		return 0;

	err = __reserved_mem_reserve_reg(node, uname);
	if (err == -ENOENT && of_get_flat_dt_prop(node, "size", NULL))
		fdt_reserved_mem_save_node(node, uname, 0, 0);

	/* scan next node */
	return 0;
}
```

解析 reserved memory node 的 reg, no-map 等属性，

```c
/*
 * __reserved_mem_reserve_reg() - reserve all memory described in 'reg' property
 */
static int __init __reserved_mem_reserve_reg(unsigned long node,
					     const char *uname)
{
	int t_len = (dt_root_addr_cells + dt_root_size_cells) * sizeof(__be32);
	phys_addr_t base, size;
	int len;
	const __be32 *prop;
	int first = 1;
	bool nomap;

	prop = of_get_flat_dt_prop(node, "reg", &len);

    ...

	nomap = of_get_flat_dt_prop(node, "no-map", NULL) != NULL;

	while (len >= t_len) {
		base = dt_mem_next_cell(dt_root_addr_cells, &prop);
		size = dt_mem_next_cell(dt_root_size_cells, &prop);

        // 在这里设置 reserved memory
        // 对于 reserved map node，调用 memblock_reserve->memblock_add_range 保存到 memblock.reserved 中
        // 对于 reserved no-map node，调用 memblock_mark_nomap->memblock_setclr_flag 设置属性
        // 还需要了解一下 memblock 是怎么将这些内存给 buddy 管理的
		if (size &&
		    early_init_dt_reserve_memory_arch(base, size, nomap) == 0)
			pr_debug("Reserved memory: reserved region for node '%s': base %pa, size %lu MiB\n",
				uname, &base, (unsigned long)(size / SZ_1M));
		else
			pr_info("Reserved memory: failed to reserve memory for node '%s': base %pa, size %lu MiB\n",
				uname, &base, (unsigned long)(size / SZ_1M));

		len -= t_len;
		if (first) {
			fdt_reserved_mem_save_node(node, uname, base, size);
			first = 0;
		}
	}
	return 0;
}
```

我们来看看 memblock 是怎样将 reserved memroy 设置为 nomap 的。

early_init_dt_reserve_memory_arch--->memblock_mark_nomap--->memblock_setclr_flag

```c
/**
 * memblock_setclr_flag - set or clear flag for a memory region
 * @base: base address of the region
 * @size: size of the region
 * @set: set or clear the flag
 * @flag: the flag to update
 *
 * This function isolates region [@base, @base + @size), and sets/clears flag
 *
 * Return: 0 on success, -errno on failure.
 */
static int __init_memblock memblock_setclr_flag(phys_addr_t base,
				phys_addr_t size, int set, int flag)
{
	struct memblock_type *type = &memblock.memory; // 为什么还是 memblock.memory，不是 memblock.reserved
	int i, ret, start_rgn, end_rgn;

    // 将需要设置为 no-map 的内存段单独“扣”出来
	ret = memblock_isolate_range(type, base, size, &start_rgn, &end_rgn);
	if (ret)
		return ret;

	for (i = start_rgn; i < end_rgn; i++) {
		struct memblock_region *r = &type->regions[i];

		if (set)
			r->flags |= flag; // 每个 memory region 配置属性
		else
			r->flags &= ~flag;
	}

	memblock_merge_regions(type); // 再规整一下
	return 0;
}
```

#### reserved memory 初始化

完成上面的初始化之后，memblock 模块已经通过 device tree 构建了整个系统的内存全貌：哪些是普通内存区域，哪些是保留内存区域。对于那些 reserved memory，我们还需要进行初始化。

setup_arch--->arm64_memblock_init--->early_init_fdt_scan_reserved_mem--->fdt_init_reserved_mem->__reserved_mem_init_node

```c
/**
 * fdt_init_reserved_mem() - allocate and init all saved reserved memory regions
 */
void __init fdt_init_reserved_mem(void)
{
	int i;

	/* check for overlapping reserved regions */
	__rmem_check_for_overlap();

	for (i = 0; i < reserved_mem_count; i++) {
		struct reserved_mem *rmem = &reserved_mem[i];
		unsigned long node = rmem->fdt_node;
		int len;
		const __be32 *prop;
		int err = 0;
		bool nomap;

		nomap = of_get_flat_dt_prop(node, "no-map", NULL) != NULL;
		prop = of_get_flat_dt_prop(node, "phandle", &len);
		if (!prop)
			prop = of_get_flat_dt_prop(node, "linux,phandle", &len);
		if (prop)
			rmem->phandle = of_read_number(prop, len/4);

		if (rmem->size == 0)
			err = __reserved_mem_alloc_size(node, rmem->name,
						 &rmem->base, &rmem->size);
		if (err == 0) {
			err = __reserved_mem_init_node(rmem);
			if (err != 0 && err != -ENOENT) {
				pr_info("node %s compatible matching fail\n",
					rmem->name);
				if (nomap)
					memblock_clear_nomap(rmem->base, rmem->size);
				else
					memblock_free(rmem->base, rmem->size);
			}
		}
	}
}
```

#### 进入 buddy 系统

memblock 始终是初始化阶段的内存管理模块，最终我们还是要转向 buddy 系统。

start_kernel--->mm_init--->mem_init--->free_all_bootmem--->free_low_memory_core_early--->__free_memory_core

在上面的过程中，free memory 被释放到伙伴系统中，而 reserved memory 不会进入伙伴系统），对于 CMA area，我们之前说过，最终由伙伴系统管理，因此，在初始化的过程中，CMA area 的内存会全部导入伙伴系统（方便其他应用可以通过伙伴系统分配内存）。具体代码如下：

```c
core_initcall(cma_init_reserved_areas);
```

至此，所有的 CMA area 的内存进入伙伴系统。

### reference

[1] https://biscuitos.github.io/blog/MMU-ARM32-MEMBLOCK-memblock_add_range/

这篇博客确实帮助很大，讲解很详细，而且分析代码的方式也值得学习，一段段代码贴出来，而不是整个函数复制。然后通过简单的作图，更容易理解，我看代码光靠头脑风暴，不太行，之后也可以采用这种方法。
