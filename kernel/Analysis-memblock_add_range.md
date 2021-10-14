## 分析内核源码 memblock_add_range()

### 1. memblock 原理

MEMBLOCK 内存分配器作为早期的内存管理器，维护了两种内存。第一种内存是系统可用的物理内存，即系统实际含有的物理内存，其值从 DTS 中进行配置，并通过 uboot 实际探测之 后传入到内核。第二种内存是内核预留给操作系统的内存，这部分内存作为特殊功能使用，不能作为共享内存使用。MEMBLOCK 内存分配器基础框架如下：

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

从上面的逻辑图可以知道，MEMBLOCK 分配器使用一个 struct memblock 结构维护着两种内存， 其中成员 memory 维护着可用物理内存区域；成员 reserved 维护着操作系统预留的内存区域。 每个区域使用数据结构 struct memblock_type 进行管理，其成员 regions 负责维护该类型内 存的所有内存区，每个内存区使用数据结构 struct memblock_region 进行维护。

### 2. 相关数据结构

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

### 3. 源码分析

代码树展开：

```plain
memblock_add_range()
| -- memblock_insert_region();
|
| -- memblock_double_array();
|
| -- memblock_merge_regions();
```

#### 3.1. memblock_add_range()

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

##### 3.1.1. 遍历到的内存区块的起始地址大于或等于新内存区块的结束地址，新的内存区块位于遍历到内存区块的前端

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

##### 3.1.2. 遍历到的内存区块的终止地址小于或等于新内存区块的起始地址, 新的内存区块位于遍历到内存区块的后面

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

##### 3.1.3. 其他情况,两个内存区块存在重叠部分

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

#### 3.2. memblock_insert_region()

该函数的作用就是将一个内存区块插入到内存区块链表的指定位置。

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

#### 3.3. memblock_merge_regions()

函数的作用就是将内存区对应的内存区块链表中能合并的内存区块进行合并。

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

### reference

[1] https://biscuitos.github.io/blog/MMU-ARM32-MEMBLOCK-memblock_add_range/

这篇博客确实帮助很大，讲解很详细，而且分析代码的方式也值的学习，一段段的代码贴出来，而不是整个函数复制。然后通过简单的作图，更容易理解，我看代码光靠头脑风暴，不太行，之后也可以采用这种方法。
