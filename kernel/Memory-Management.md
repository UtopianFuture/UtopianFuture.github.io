## Memory Management

很多文章都说内存管理是内核中最复杂的部分、最重要的部分之一，在来实验室之后跟着师兄做项目、看代码的这段时间里，渐渐感觉自己的知识框架是不完整的，底下少了一部分，后来发现这部分就是内核，所以开始学习内核。其实这也不是第一次接触内核，之前也陆陆续续的看过一部分，包括做 RISC-V 操作系统实验，LoongArch 内核的启动部分，但始终没有花时间去肯内存管理，进程调度和文件管理这几个核心模块。而师兄也说过，内核都看的懂，啥代码你看不懂。

### 内存分布

64 位 linux 内核的内存分布

![linux-address-space.png](https://github.com/UtopianFuture/UtopianFuture.github.io/blob/master/image/linux-address-space.png?raw=true)

### 数据结构

内存管理模块涉及到的结构体之间的关系。

![memory-management-structure.png](https://github.com/UtopianFuture/UtopianFuture.github.io/blob/master/image/memory-management-structure.png?raw=true)

### 页框管理

以页框为最小单位分配内存。从 page  作为页描述符记录每个页框当前的状态。

```c
/*
 * Each physical page in the system has a struct page associated with
 * it to keep track of whatever it is we are using the page for at the
 * moment. Note that we have no way to track which tasks are using
 * a page.
 */
struct page {
	page_flags_t flags;	// 保存到 node 和 zone 的链接和页框状态
	atomic_t _count;		/* Usage count */
	atomic_t _mapcount;		/* Count of ptes mapped in mms,
					 * to show when page is mapped
					 * & limit reverse map searches.
					 */
	unsigned long private;		/* Mapping-private opaque data:
					 * usually used for buffer_heads
					 * if PagePrivate set; used for
					 * swp_entry_t if PageSwapCache
					 * When page is free, this indicates
					 * order in the buddy system.
					 */
	struct address_space *mapping;	/* If low bit clear, points to
					 * inode address_space, or NULL.
					 * If page mapped as anonymous
					 * memory, low bit is set, and
					 * it points to anon_vma object:
					 * see PAGE_MAPPING_ANON below.
					 */
	pgoff_t index;			/* Our offset within mapping. */
	struct list_head lru;		/* Pageout list, eg. active_list
					 * protected by zone->lru_lock !
					 */
	/*
	 * On machines where all RAM is mapped into kernel address space,
	 * we can simply calculate the virtual address. On machines with
	 * highmem some memory is mapped into kernel virtual memory
	 * dynamically, so we need a place to store that address.
	 * Note that this field could be 16 bits on x86 ... ;)
	 *
	 * Architectures with slow multiplication can define
	 * WANT_PAGE_VIRTUAL in asm/page.h
	 */
#if defined(WANT_PAGE_VIRTUAL)
	void *virtual;			/* Kernel virtual address (NULL if
					   not kmapped, ie. highmem) */
#endif /* WANT_PAGE_VIRTUAL */
};
```

所有的页描述符存放在 `mem_map` 数组中，用 `virt_to_page`  宏产生线性地址对应的 page 地址，用 `pfn_to_page` 宏产生页框号对应的 page 地址。

```c
#define virt_to_page(kaddr)	pfn_to_page(__pa(kaddr) >> PAGE_SHIFT)
#define pfn_to_page(pfn)	(mem_map + (pfn))
```

#### 内存管理区

由于 NUMA 模型的存在，kernel 将物理内存分为几个节点（node），每个节点有一个类型为 `pg_date_t` 的描述符。

```c
*
 * The pg_data_t structure is used in machines with CONFIG_DISCONTIGMEM
 * (mostly NUMA machines?) to denote a higher-level memory zone than the
 * zone denotes.
 *
 * On NUMA machines, each NUMA node would have a pg_data_t to describe
 * it's memory layout.
 *
 * Memory statistics and page replacement data structures are maintained on a
 * per-zone basis.
 */
typedef struct pglist_data { // 每个节点分为不同的管理区
	struct zone node_zones[MAX_NR_ZONES]; // 管理区描述符数组
	struct zonelist node_zonelists[GFP_ZONETYPES];
	int nr_zones;
	struct page *node_mem_map; // 页描述符数组
	struct bootmem_data *bdata;
	unsigned long node_start_pfn;
	unsigned long node_present_pages; /* total number of physical pages */
	unsigned long node_spanned_pages; /* total size of physical page
					     range, including holes */
	int node_id;
	struct pglist_data *pgdat_next;
	wait_queue_head_t kswapd_wait;
	struct task_struct *kswapd;
	int kswapd_max_order;
} pg_data_t;
```

所有节点的描述符存在一个单向链表中，由 `pgdat_list` 指向。

```c
extern struct pglist_data *pgdat_list;
```

在 UMA 模型中，内核的节点数为 1。

由于实际的计算机体系结构的限制（ISA 总线只能寻址 16 MB 和 32 位计算机只能寻址 4G），每个节点的物理内存又分为 3 个管理区。

- ZONE_DMA：低于 16MB 的内存页框。
- ZONE_NORMAL：高于 16MB 低于 896MB 的内存页框。
- ZONE_HIGHMEM：高于 896MB 的内存页框。

zone 为管理区描述符。

```c
/*
 * On machines where it is needed (eg PCs) we divide physical memory
 * into multiple physical zones. On a PC we have 3 zones:
 *
 * ZONE_DMA	  < 16 MB	ISA DMA capable memory
 * ZONE_NORMAL	16-896 MB	direct mapped by the kernel
 * ZONE_HIGHMEM	 > 896 MB	only page cache and user processes
 */

struct zone {
	/* Fields commonly accessed by the page allocator */
	unsigned long		free_pages;
	unsigned long		pages_min, pages_low, pages_high;
	unsigned long		lowmem_reserve[MAX_NR_ZONES];

	struct per_cpu_pageset	pageset[NR_CPUS];

	/*
	 * free areas of different sizes
	 */
	spinlock_t		lock;
	struct free_area	free_area[MAX_ORDER]; // 伙伴系统的 11 个块

    ...

	wait_queue_head_t	* wait_table;
	unsigned long		wait_table_size;
	unsigned long		wait_table_bits;

	/*
	 * Discontig memory support fields.
	 */
	struct pglist_data	*zone_pgdat;
	struct page		*zone_mem_map; // mem_map 子集，伙伴系统使用
	/* zone_start_pfn == zone_start_paddr >> PAGE_SHIFT */
	unsigned long		zone_start_pfn;

	unsigned long		spanned_pages;	/* total size, including holes */
	unsigned long		present_pages;	/* amount of memory (excluding holes) */

	/*
	 * rarely used fields:
	 */
	char			*name;
} ____cacheline_maxaligned_in_smp;
```

#### 分区页框分配器

管理区分配器搜索能够满足分配请求的管理区，在每个管理区中，伙伴系统负责分配页框，每 CPU 页框高速缓存用来满足单个页框的分配请求。

![memory-management.1.png](https://github.com/UtopianFuture/UtopianFuture.github.io/blob/master/image/memory-management.1.png?raw=true)

#### 管理区分配器

任务：分配一个包含足够空闲页框的 zone 来满足内存请求。我们先了解一下伙伴系统的原理。

##### 伙伴系统算法

即为了解决外碎片的问题，尽量保证有连续的空闲页框块。

伙伴系统的描述网上有很多，这里不啰嗦，我们直接看代码。伙伴系统分配页框和上文的 `alloc_pages` 分配页框有什么不同？

> 把所有的空闲页框分组为 11 个块链表，每个块链表分别包含大小为 1、2、4、8、16、32、64、128、256、512 和 1024 个连续页框的页框块。最大可以申请 1024 个连续页框，也即 4MB 大小的连续空间。
>
> 假设要申请一个 256 个页框的块，先从 256 个页框的链表中查找空闲块，如果没有，就去 512 个页框的链表中找，找到了即将页框分为两个 256 个页框的块，一个分配给应用，另外一个移到 256 个页框的链表中。如果 512 个页框的链表中仍没有空闲块，继续向 1024 个页框的链表查找，如果仍然没有，则返回错误。
>
> 页框块在释放时，会主动将两个连续的页框块合并成一个较大的页框块。
>
> 伙伴算法具有以下一些缺点：
>
> - 一个很小的块往往会阻碍一个大块的合并，一个系统中，对内存块的分配，大小是随机的，一片内存中仅一个小的内存块没有释放，旁边两个大的就不能合并。
> - 算法中有一定的浪费现象，伙伴算法是按 2 的幂次方大小进行分配内存块，当然这样做是有原因的，即为了避免把大的内存块拆的太碎，更重要的是使分配和释放过程迅速。但是他也带来了不利的一面，如果所需内存大小不是 2 的幂次方，就会有部分页面浪费。有时还很严重。比如原来是 1024 个块，申请了 16 个块，再申请 600 个块就申请不到了，因为已经被分割了。
> - 另外拆分和合并涉及到较多的链表和位图操作，开销还是比较大的。
>
> Buddy 算法的释放原理：
>
> 内存的释放是分配的逆过程，也可以看作是伙伴的合并过程。当释放一个块时，先在其对应的链表中考查是否有伙伴存在，如果没有伙伴块，就直接把要释放的块挂入链表头；如果有，则从链表中摘下伙伴，合并成一个大块，然后继续考察合并后的块在更大一级链表中是否有伙伴存在，直到不能合并或者已经合并到了最大的块。
>
> Buddy 算法对伙伴的定义如下：
>
> - 两个块大小相同；
> - 两个块地址连续；
> - 两个块必须是同一个大块中分离出来的；

##### 请求和释放页框

这几个函数之后都需要详细分析。它们是整个内存管理的核心。

有 6 个函数和宏用来请求页框，这里只分析 `alloc_pages`

```c
struct page *alloc_pages(gfp_t gfp, unsigned order) // gfp 表示如何寻找页框， order 表示请求的页框数
{
	struct mempolicy *pol = &default_policy;
	struct page *page;

	if (!in_interrupt() && !(gfp & __GFP_THISNODE))
		pol = get_task_policy(current);

	/*
	 * No reference counting needed for current->mempolicy
	 * nor system default_policy
	 */
	if (pol->mode == MPOL_INTERLEAVE)
		page = alloc_page_interleave(gfp, order, interleave_nodes(pol));
	else if (pol->mode == MPOL_PREFERRED_MANY)
		page = alloc_pages_preferred_many(gfp, order,
				numa_node_id(), pol);
	else
		page = __alloc_pages(gfp, order,
				policy_node(gfp, pol, numa_node_id()),
				policy_nodemask(gfp, pol));

	return page;
}
EXPORT_SYMBOL(alloc_pages);
```

调用 `alloc_pages` 的地方很多。

![memory-management.2.png](https://github.com/UtopianFuture/UtopianFuture.github.io/blob/master/image/memory-management.2.png?raw=true)

`alloc_pages` 主要是调用 `__alloc_pages` 。接下来详细分析相关函数。

1. `__alloc_pages`

   ```c
   /*
    * This is the 'heart' of the zoned buddy allocator.
    */
   struct page *__alloc_pages(gfp_t gfp, unsigned int order, int preferred_nid,
   							nodemask_t *nodemask)
   {
   	struct page *page;
   	unsigned int alloc_flags = ALLOC_WMARK_LOW; // 表示页面分配的行为和属性，这里初始化为 ALLOC_WMARK_LOW
       											// 表示允许分配内存的判断条件为 WMARK_LOW
   	gfp_t alloc_gfp; /* The gfp_t that was actually used for allocation */ // 分配掩码，确定页面属性等
   	struct alloc_context ac = { }; // 用于保存相关参数

   	...

   	gfp &= gfp_allowed_mask;
   	/*
   	 * Apply scoped allocation constraints. This is mainly about GFP_NOFS
   	 * resp. GFP_NOIO which has to be inherited for all allocation requests
   	 * from a particular context which has been marked by
   	 * memalloc_no{fs,io}_{save,restore}. And PF_MEMALLOC_PIN which ensures
   	 * movable zones are not used during allocation.
   	 */
   	gfp = current_gfp_context(gfp);
   	alloc_gfp = gfp;
   	if (!prepare_alloc_pages(gfp, order, preferred_nid, nodemask, &ac,
   			&alloc_gfp, &alloc_flags)) // 从代码来看就是保存 zonelist 等相关信息到 ac 中
   		return NULL;

   	/*
   	 * Forbid the first pass from falling back to types that fragment
   	 * memory until all local zones are considered.
   	 */
   	alloc_flags |= alloc_flags_nofragment(ac.preferred_zoneref->zone, gfp); // 用于内存碎片化方面的优化

   	/* First allocation attempt */
   	page = get_page_from_freelist(alloc_gfp, order, alloc_flags, &ac); // 从空闲链表中分配内存，并返回第一个 page
   	if (likely(page))
   		goto out;

   	alloc_gfp = gfp;
   	ac.spread_dirty_pages = false;

   	/*
   	 * Restore the original nodemask if it was potentially replaced with
   	 * &cpuset_current_mems_allowed to optimize the fast-path attempt.
   	 */
   	ac.nodemask = nodemask;

   	page = __alloc_pages_slowpath(alloc_gfp, order, &ac); // 若分配不成功，则通过慢路径分配

   out:
   	if (memcg_kmem_enabled() && (gfp & __GFP_ACCOUNT) && page &&
   	    unlikely(__memcg_kmem_charge_page(page, gfp, order) != 0)) {
   		__free_pages(page, order);
   		page = NULL;
   	}

   	trace_mm_page_alloc(page, order, alloc_gfp, ac.migratetype);

   	return page;
   }
   EXPORT_SYMBOL(__alloc_pages);
   ```

​	这其中涉及一些概念需要理解清楚。

- 分配掩码

  ```c
  typedef unsigned int __bitwise gfp_t;
  ```

  分配掩码是描述页面分配方法的标志。其大致可以分为如下几类：

  - 内存管理区修饰符（zone modifier）
  - 移动修饰符（mobility and placement modifier）
  - 水位修饰符（watermark modifier）
  - 页面回收修饰符（page reclaim modifier）
  - 行为修饰符（action modifier）

  具体的定义在 gfp.h 中。而要正确使用这么多标志很难，所以定义了一些常用的表示组合——类型标志，如 `GFP_KERNEL`, `GFP_USER` 等，这里就不一一介绍，知道它是干什么的就行了。

- `pglist_data`

  该数据结构用来表示一个内存节点的所有资源。

  ```c
  typedef struct pglist_data {
  	/*
  	 * node_zones contains just the zones for THIS node. Not all of the
  	 * zones may be populated, but it is the full list. It is referenced by
  	 * this node's node_zonelists as well as other node's node_zonelists.
  	 */
  	struct zone node_zones[MAX_NR_ZONES];

  	/*
  	 * node_zonelists contains references to all zones in all nodes.
  	 * Generally the first zones will be references to this node's
  	 * node_zones.
  	 */
  	struct zonelist node_zonelists[MAX_ZONELISTS];

  	int nr_zones; /* number of populated zones in this node */

      ...

  	unsigned long node_start_pfn;
  	unsigned long node_present_pages; /* total number of physical pages */
  	unsigned long node_spanned_pages; /* total size of physical page
  					     range, including holes */
  	int node_id;
  	wait_queue_head_t kswapd_wait;
  	wait_queue_head_t pfmemalloc_wait;
  	struct task_struct *kswapd;	/* Protected by
  					   mem_hotplug_begin/end() */
  	int kswapd_order;
  	enum zone_type kswapd_highest_zoneidx;

  	int kswapd_failures;		/* Number of 'reclaimed == 0' runs */

  	...

  	/* Per-node vmstats */
  	struct per_cpu_nodestat __percpu *per_cpu_nodestats;
  	atomic_long_t		vm_stat[NR_VM_NODE_STAT_ITEMS];
  } pg_data_t;
  ```

  在 NUMA 架构中，整个系统的内存由一个 `pglist_data` 指针 `node_data` 来管理。

  ```c
  extern struct pglist_data *node_data[];
  ```

2. `prepare_alloc_pages`

   这个函数主要用于初始化在页面分配时用到的参数，这些参数临时存放在 `alloc_context` 中。

   ```c
   struct alloc_context {
   	struct zonelist *zonelist; // 每个内存节点对应的 zonelist
   	nodemask_t *nodemask; // 该内存节点的掩码
   	struct zoneref *preferred_zoneref; // 表示首选 zone 的 zoneref
   	int migratetype; // 迁移类型
   	enum zone_type highest_zoneidx; // 表示这个分配掩码允许内存分配的最高 zone (?)
   	bool spread_dirty_pages; // 用于指定是否传播脏页
   };
   ```

   ```c
   static inline bool prepare_alloc_pages(gfp_t gfp_mask, unsigned int order,
   		int preferred_nid, nodemask_t *nodemask,
   		struct alloc_context *ac, gfp_t *alloc_gfp,
   		unsigned int *alloc_flags)
   {
   	ac->highest_zoneidx = gfp_zone(gfp_mask); // 根据分配掩码计算 zone 的 zone_idx
   	ac->zonelist = node_zonelist(preferred_nid, gfp_mask); // 返回首选节点 preferred_nid 对应的 zonelist
   	ac->nodemask = nodemask;
   	ac->migratetype = gfp_migratetype(gfp_mask);

   	...

   	*alloc_flags = gfp_to_alloc_flags_cma(gfp_mask, *alloc_flags);

   	/* Dirty zone balancing only done in the fast path */
   	ac->spread_dirty_pages = (gfp_mask & __GFP_WRITE);

   	/*
   	 * The preferred zone is used for statistics but crucially it is
   	 * also used as the starting point for the zonelist iterator. It
   	 * may get reset for allocations that ignore memory policies.
   	 */
   	ac->preferred_zoneref = first_zones_zonelist(ac->zonelist,
   					ac->highest_zoneidx, ac->nodemask);

   	return true;
   }
   ```

- `zonelist`

  ```c
  struct zonelist {
  	struct zoneref _zonerefs[MAX_ZONES_PER_ZONELIST + 1];
  };

  struct zoneref {
  	struct zone *zone;	/* Pointer to actual zone */
  	int zone_idx;		/* zone_idx(zoneref->zone) */
  };
  ```

  内核使用 zone 来管理内存节点，上文介绍过，一个内存节点可能存在多个 zone，如 `ZONE_DMA,` `ZONE_NORMAL` 等。zonelist 是所有可用的 zone，其中排在第一个的是页面分配器最喜欢的。

  我们假设系统中只有一个内存节点，有两个 zone，分别是 `ZONE_DMA` 和 `ZONE_NORMAL`，那么 `zonelist` 中的相关数据结构的关系如下：

  ![ZONELIST.png](https://github.com/UtopianFuture/UtopianFuture.github.io/blob/master/image/ZONELIST.png?raw=true)

3.  `get_page_from_freelist`

   ```c
   /*
    * get_page_from_freelist goes through the zonelist trying to allocate
    * a page.
    */
   static struct page *
   get_page_from_freelist(gfp_t gfp_mask, unsigned int order, int alloc_flags,
   						const struct alloc_context *ac)
   {
   	struct zoneref *z;
   	struct zone *zone;
   	struct pglist_data *last_pgdat_dirty_limit = NULL;
   	bool no_fallback;

   retry:
   	/*
   	 * Scan zonelist, looking for a zone with enough free.
   	 * See also __cpuset_node_allowed() comment in kernel/cpuset.c.
   	 */
   	no_fallback = alloc_flags & ALLOC_NOFRAGMENT;
   	z = ac->preferred_zoneref;
   	for_next_zone_zonelist_nodemask(zone, z, ac->highest_zoneidx,
   					ac->nodemask) { // 从最喜欢的 zone 开始遍历所有的 zone
   		struct page *page;
   		unsigned long mark;

   		...

   		mark = wmark_pages(zone, alloc_flags & ALLOC_WMARK_MASK); // 计算 zone 中某个水位的页面大小（？）
   		if (!zone_watermark_fast(zone, order, mark, // 计算当前 zone 的空闲页面是否满足 WMARK_LOW
   				       ac->highest_zoneidx, alloc_flags, // 同时根据 order 计算是否有足够大的空闲内存块
   				       gfp_mask)) {
   			int ret;

               ...

               // 返回 false，进行处理
   			/* Checked here to keep the fast path fast */
   			BUILD_BUG_ON(ALLOC_NO_WATERMARKS < NR_WMARK);
   			if (alloc_flags & ALLOC_NO_WATERMARKS)
   				goto try_this_zone;

   			if (!node_reclaim_enabled() ||
   			    !zone_allows_reclaim(ac->preferred_zoneref->zone, zone))
   				continue;

   			ret = node_reclaim(zone->zone_pgdat, gfp_mask, order); // 尝试进行回收
   			switch (ret) {
   			case NODE_RECLAIM_NOSCAN:
   				/* did not scan */
   				continue;
   			case NODE_RECLAIM_FULL:
   				/* scanned but unreclaimable */
   				continue;
   			default:
   				/* did we reclaim enough */
   				if (zone_watermark_ok(zone, order, mark,
   					ac->highest_zoneidx, alloc_flags))
   					goto try_this_zone;

   				continue;
   			}
   		}

   try_this_zone: // 显而易见，从当前 zone 分配内存
   		page = rmqueue(ac->preferred_zoneref->zone, zone, order,
   				gfp_mask, alloc_flags, ac->migratetype); // 从伙伴系统分配页面，核心函数，后面分析
   		if (page) { // 分配成功后设置必要的属性和检查
   			prep_new_page(page, order, gfp_mask, alloc_flags);

   			/*
   			 * If this is a high-order atomic allocation then check
   			 * if the pageblock should be reserved for the future
   			 */
   			if (unlikely(order && (alloc_flags & ALLOC_HARDER)))
   				reserve_highatomic_pageblock(page, zone, order);

   			return page;
   		}
           ...
   	}

   	/*
   	 * It's possible on a UMA machine to get through all zones that are
   	 * fragmented. If avoiding fragmentation, reset and try again.
   	 */
   	if (no_fallback) { // 没有分配成功，尝试重新分配
   		alloc_flags &= ~ALLOC_NOFRAGMENT;
   		goto retry;
   	}

   	return NULL;
   }
   ```

   这里有几点需要注意：

- 首选的 zone 是通过 `gfp_mask` 换算的，具体的换算过程是 `gfp_zone` 和 ` first_zones_zonelist` 宏。
- 大部分情况只需要遍历第一个 zone 就可以成功分配内存。
- 在分配内存之前需要判断 zone 的水位情况以及是否满足分配连续大内存块的需求，这是 `zone_watermark_fast` -> `zone_watermark_ok` -> `__zone_watermark_ok` 的工作。

4. `zone_watermark_fast`

   判断 zone 的水位情况以及是否满足分配连续大内存块的需求。

   ```c
   static inline bool zone_watermark_fast(struct zone *z, unsigned int order,
   				unsigned long mark, int highest_zoneidx,
   				unsigned int alloc_flags, gfp_t gfp_mask)
   {
   	long free_pages;

   	free_pages = zone_page_state(z, NR_FREE_PAGES); //

   	/*
   	 * Fast check for order-0 only. If this fails then the reserves
   	 * need to be calculated.
   	 */
   	if (!order) {
   		long fast_free;

   		fast_free = free_pages;
   		fast_free -= __zone_watermark_unusable_free(z, 0, alloc_flags);
   		if (fast_free > mark + z->lowmem_reserve[highest_zoneidx])
   			return true;
   	}

   	if (__zone_watermark_ok(z, order, mark, highest_zoneidx, alloc_flags,
   					free_pages))
   		return true;

       ...

   	return false;
   }
   ```

5. `rmqueue`

   `rmqueue` 函数会从伙伴系统中获取内存。若没有需要大小的内存块，那么从更大的内存块中“切”内存。如程序要需要 `order = 4` 的内存块，但是伙伴系统中 `order = 4` 的内存块已经分配完了，那么从 `order = 5` 的内存块中“切”一块 `order = 4` 的内存块分配给该程序，同时将剩下的部分添加到 `order = 4` 的空闲链表中。

   ```c
   /*
    * Allocate a page from the given zone. Use pcplists for order-0 allocations.
    */
   static inline struct page *rmqueue(struct zone *preferred_zone,
   			struct zone *zone, unsigned int order,
   			gfp_t gfp_flags, unsigned int alloc_flags,
   			int migratetype)
   {
   	unsigned long flags;
   	struct page *page;

   	if (likely(pcp_allowed_order(order))) {
   		/*
   		 * MIGRATE_MOVABLE pcplist could have the pages on CMA area and
   		 * we need to skip it when CMA area isn't allowed.
   		 */
   		if (!IS_ENABLED(CONFIG_CMA) || alloc_flags & ALLOC_CMA ||
   				migratetype != MIGRATE_MOVABLE) {
   			page = rmqueue_pcplist(preferred_zone, zone, order, // 处理分配单个页面的情况
   					gfp_flags, migratetype, alloc_flags);
   			goto out;
   		}
   	}

   	/*
   	 * We most definitely don't want callers attempting to
   	 * allocate greater than order-1 page units with __GFP_NOFAIL.
   	 */
   	WARN_ON_ONCE((gfp_flags & __GFP_NOFAIL) && (order > 1));
   	spin_lock_irqsave(&zone->lock, flags); // 访问 zone 需要加锁

   	do { // 处理 order > 0 的情况
   		page = NULL;
   		/*
   		 * order-0 request can reach here when the pcplist is skipped
   		 * due to non-CMA allocation context. HIGHATOMIC area is
   		 * reserved for high-order atomic allocation, so order-0
   		 * request should skip it.
   		 */
   		if (order > 0 && alloc_flags & ALLOC_HARDER) {
   			page = __rmqueue_smallest(zone, order, MIGRATE_HIGHATOMIC); // 从 zone->free_area 中分配内存
               												// 从 order 到 MAX_ORDER - 1 遍历
   			if (page)
   				trace_mm_page_alloc_zone_locked(page, order, migratetype);
   		}
   		if (!page) // __rmqueue_smallest 分配失败
   			page = __rmqueue(zone, order, migratetype, alloc_flags); // 从 MAX_ORDER - 1 到 order 遍历
   	} while (page && check_new_pages(page, order)); // 判断分配出来的页面是否合格
   	if (!page)
   		goto failed;

   	__mod_zone_freepage_state(zone, -(1 << order), // 更新该 zone 的 NR_FREE_PAGES
   				  get_pcppage_migratetype(page));
   	spin_unlock_irqrestore(&zone->lock, flags); // 释放锁

   	__count_zid_vm_events(PGALLOC, page_zonenum(page), 1 << order);
   	zone_statistics(preferred_zone, zone, 1);

   out:
   	/* Separate test+clear to avoid unnecessary atomics */
   	if (test_bit(ZONE_BOOSTED_WATERMARK, &zone->flags)) {
   		clear_bit(ZONE_BOOSTED_WATERMARK, &zone->flags);
   		wakeup_kswapd(zone, 0, 0, zone_idx(zone)); // 判断是否需要唤醒 kswapd 线程回收内存
   	}

   	VM_BUG_ON_PAGE(page && bad_range(zone, page), page);
   	return page;

   failed:
   	spin_unlock_irqrestore(&zone->lock, flags);
   	return NULL;
   }
   ```

- `per_cpu_pageset`

  `per_cpu_pageset` 是一个 Per-CPU 变量，即每个 CPU 中都有一个本地的 `per_cpu_pageset` 变量，里面暂存了一部分单个的物理页面。当系统需要单个物理页面时，直接从本地 CPU 的 `per_cpu_pageset` 中获取物理页面即可，这样能减少对 zone 中相关锁的操作。

  ```c
  struct per_cpu_pageset	pageset[NR_CPUS];
  ```

  ```c
  struct per_cpu_pageset {
  	struct per_cpu_pages pcp[2];	/* 0: hot.  1: cold */
  }
  ```

  ```c
  struct per_cpu_pages {
  	int count;		/* number of pages in the list */
  	int high;		/* high watermark, emptying needed */
  	int batch;		/* chunk size for buddy add/remove */
  	short free_factor;	/* batch scaling factor during free */
  	/* Lists of pages, one per migrate type stored on the pcp-lists */
  	struct list_head lists[NR_PCP_LISTS];
  };
  ```



有 4 个函数和宏能够释放页框，这里只分析 `__free_pages`。

```c
void __free_pages(struct page *page, unsigned int order)
{
	if (put_page_testzero(page))
		free_the_page(page, order);
	else if (!PageHead(page))
		while (order-- > 0)
			free_the_page(page + (1 << order), order);
}
EXPORT_SYMBOL(__free_pages);
```

 其实从代码中来看都是调用 `free_the_page`，

```c
static inline void free_the_page(struct page *page, unsigned int order)
{
	if (pcp_allowed_order(order))		/* Via pcp? */
		free_unref_page(page, order); // 释放单个物理页面
	else
		__free_pages_ok(page, order, FPI_NONE); // 释放多个物理页面
}
```

首先来看看 `order = 0` 的情况，

1. `free_unref_page`

   ```c
   /*
    * Free a pcp page
    */
   void free_unref_page(struct page *page, unsigned int order)
   {
   	unsigned long flags;
   	unsigned long pfn = page_to_pfn(page); // 将 page 转换为 pfn
   	int migratetype;

   	if (!free_unref_page_prepare(page, pfn, order)) // 做释放前的准备工作
   		return;

   	...

   	local_lock_irqsave(&pagesets.lock, flags); // 释放过程中避免相应中断
   	free_unref_page_commit(page, pfn, migratetype, order); // 释放单个物理页面到 PCP 链表中
   	local_unlock_irqrestore(&pagesets.lock, flags);
   }
   ```

2. `free_unref_page_commit`

   ```c
   static void free_unref_page_commit(struct page *page, unsigned long pfn,
   				   int migratetype, unsigned int order)
   {
   	struct zone *zone = page_zone(page);
   	struct per_cpu_pages *pcp;
   	int high;
   	int pindex;

   	__count_vm_event(PGFREE);
   	pcp = this_cpu_ptr(zone->per_cpu_pageset);
   	pindex = order_to_pindex(migratetype, order);
   	list_add(&page->lru, &pcp->lists[pindex]); // 添加到 pcp->list
   	pcp->count += 1 << order; // count++
   	high = nr_pcp_high(pcp, zone);
   	if (pcp->count >= high) {
   		int batch = READ_ONCE(pcp->batch);

   		free_pcppages_bulk(zone, nr_pcp_free(pcp, high, batch), pcp);
   	}
   }
   ```

3. `__free_pages_ok` 前期准备工作和 `free_unref_page` 最后还是会调用 `__free_one_page`。该函数不仅可以释放物理页面到伙伴系统，还可以处理空闲页面的合并工作。合并原理下面有介绍。

   ```c
   static inline void __free_one_page(struct page *page,
   		unsigned long pfn,
   		struct zone *zone, unsigned int order,
   		int migratetype, fpi_t fpi_flags)
   {
   	struct capture_control *capc = task_capc(zone);
   	unsigned long buddy_pfn;
   	unsigned long combined_pfn;
   	unsigned int max_order;
   	struct page *buddy;
   	bool to_tail;

   	max_order = min_t(unsigned int, MAX_ORDER - 1, pageblock_order);

   	...

   continue_merging:
   	while (order < max_order) {
   		if (compaction_capture(capc, page, order, migratetype)) {
   			__mod_zone_freepage_state(zone, -(1 << order),
   								migratetype);
   			return;
   		}
   		buddy_pfn = __find_buddy_pfn(pfn, order);
   		buddy = page + (buddy_pfn - pfn); // 计算相邻的内存块

   		if (!page_is_buddy(page, buddy, order)) // 检查该相邻块能否合并
   			goto done_merging;
   		/*
   		 * Our buddy is free or it is CONFIG_DEBUG_PAGEALLOC guard page,
   		 * merge with it and move up one order.
   		 */
   		if (page_is_guard(buddy))
   			clear_page_guard(zone, buddy, order, migratetype);
   		else
   			del_page_from_free_list(buddy, zone, order);
   		combined_pfn = buddy_pfn & pfn;
   		page = page + (combined_pfn - pfn);
   		pfn = combined_pfn;
   		order++;
   	}
   	if (order < MAX_ORDER - 1) {

           ...

   		max_order = order + 1;
   		goto continue_merging;
   	}

   done_merging: // 合并两内存块
   	set_buddy_order(page, order);

   	if (fpi_flags & FPI_TO_TAIL)
   		to_tail = true;
   	else if (is_shuffle_order(order))
   		to_tail = shuffle_pick_tail();
   	else
   		to_tail = buddy_merge_likely(pfn, buddy_pfn, page, order);

   	if (to_tail)
   		add_to_free_list_tail(page, zone, order, migratetype);
   	else
   		add_to_free_list(page, zone, order, migratetype);

   	/* Notify page reporting subsystem of freed page */
   	if (!(fpi_flags & FPI_SKIP_REPORT_NOTIFY))
   		page_reporting_notify_free(order);
   }
   ```

- 判断内存块 B 能够与内存块 A 合并需要符合 3 个条件：

  - 内存块 B 要在伙伴系统中；
  - 内存块 B 的 order 要和 A 的相同；
  - 内存块 B 要和 A 在同一个 zone 中；

  ```c
  static inline bool page_is_buddy(struct page *page, struct page *buddy,
  							unsigned int order)
  {
  	if (!page_is_guard(buddy) && !PageBuddy(buddy)) //（1）
  		return false;

  	if (buddy_order(buddy) != order) //（2）
  		return false;

  	/*
  	 * zone check is done late to avoid uselessly calculating
  	 * zone/node ids for pages that could never merge.
  	 */
  	if (page_zone_id(page) != page_zone_id(buddy)) //（3）
  		return false;

  	VM_BUG_ON_PAGE(page_count(buddy) != 0, buddy);

  	return true;
  }
  ```

### 内存区管理

伙伴系统算法采用页框作为基本内存区，但一个页框一般是 4KB，而程序很多时候都是申请很小的内存，比如几百字节，十几 KB，这时分配一个页会造成很大的浪费，slab 分配器解决的就是对小内存区的请求。

slab 分配器最终还是使用伙伴系统来分配实际的物理页面，只不过 slab 分配器在这些连续的物理页面上实现了自己的管理机制。

下面是 slab 系统涉及到的 slab 描述符、slab 节点、本地对象缓冲池、共享对象缓冲池、3 个 slab 链表、n 个 slab 分配器，以及众多 slab 缓存对象之间的关系。先对整个系统有大概的印象再去了解具体实现就比较容易。

![slab_structure.png](https://github.com/UtopianFuture/UtopianFuture.github.io/blob/master/image/slab_structure.png?raw=true)

#### 创建 slab 描述符

1. `kmem_cache` 是 slab 分配器中的核心数据结构，我们将其称为 slab 描述符。

   ```c
   struct kmem_cache {
   	struct kmem_cache_cpu __percpu *cpu_slab; // 共享对象缓冲池
   	/* Used for retrieving partial slabs, etc. */
   	slab_flags_t flags; // 对象的分配掩码
   	unsigned long min_partial;
   	unsigned int size;	/* The size of an object including metadata */
   	unsigned int object_size;/* The size of an object without metadata */
   	struct reciprocal_value reciprocal_size;
   	unsigned int offset;	/* Free pointer offset */
   	struct kmem_cache_order_objects oo;

   	/* Allocation and freeing of slabs */
   	struct kmem_cache_order_objects max; // 其实就是 slab 描述符中空闲对象的最大最小值
   	struct kmem_cache_order_objects min;
   	gfp_t allocflags;	/* gfp flags to use on each alloc */
   	int refcount;		/* Refcount for slab cache destroy */
   	void (*ctor)(void *); // 构造函数
   	unsigned int inuse;		/* Offset to metadata */
   	unsigned int align;		/* Alignment */
   	unsigned int red_left_pad;	/* Left redzone padding size */
   	const char *name;	/* Name (only for display!) */
   	struct list_head list;	/* List of slab caches */ // 用于把 slab 描述符添加到全局链表 slab_caches 中

       ...

   	unsigned int useroffset;	/* Usercopy region offset */
   	unsigned int usersize;		/* Usercopy region size */

   	struct kmem_cache_node *node[MAX_NUMNODES]; // slab 节点
   };
   ```

2. `kmem_cache_create`

   ` kmem_cache_create` 只是 `kmem_cache_create_usercopy` 的包装，直接看 `kmem_cache_create_usercopy`，

   ```c
   struct kmem_cache *
   kmem_cache_create_usercopy(const char *name,
   		  unsigned int size, unsigned int align,
   		  slab_flags_t flags,
   		  unsigned int useroffset, unsigned int usersize,
   		  void (*ctor)(void *))
   {
   	struct kmem_cache *s = NULL;
   	const char *cache_name;
   	int err;

   	mutex_lock(&slab_mutex);

   	err = kmem_cache_sanity_check(name, size);
   	if (err) {
   		goto out_unlock;
   	}

   	...

   	if (!usersize)
   		s = __kmem_cache_alias(name, size, align, flags, ctor); // 查找是否有现成的 slab 描述符可以用
   	if (s)
   		goto out_unlock;

   	cache_name = kstrdup_const(name, GFP_KERNEL);
   	if (!cache_name) {
   		err = -ENOMEM;
   		goto out_unlock;
   	}

   	s = create_cache(cache_name, size, // 创建 slab 描述符
   			 calculate_alignment(flags, align, size),
   			 flags, useroffset, usersize, ctor, NULL);
   	if (IS_ERR(s)) {
   		err = PTR_ERR(s);
   		kfree_const(cache_name);
   	}

   out_unlock:
   	mutex_unlock(&slab_mutex)
   	return s;
   }
   ```

3. `__kmem_cache_create`

   `create_cache` 只是初步初始化一个 slab 描述符，主要的创建过程在 `__kmem_cache_create` 中。

   ```c
   int __kmem_cache_create(struct kmem_cache *cachep, slab_flags_t flags)
   {
   	size_t ralign = BYTES_PER_WORD;
   	gfp_t gfp;
   	int err;
   	unsigned int size = cachep->size;

   	...

   	if (flags & SLAB_RED_ZONE) {
   		ralign = REDZONE_ALIGN;
   		/* If redzoning, ensure that the second redzone is suitably
   		 * aligned, by adjusting the object size accordingly. */
   		size = ALIGN(size, REDZONE_ALIGN);
   	}

   	/* 3) caller mandated alignment */
   	if (ralign < cachep->align) {
   		ralign = cachep->align;
   	}
   	/* disable debug if necessary */
   	if (ralign > __alignof__(unsigned long long))
   		flags &= ~(SLAB_RED_ZONE | SLAB_STORE_USER);
   	/*
   	 * 4) Store it.
   	 */
   	cachep->align = ralign;
   	cachep->colour_off = cache_line_size(); // 着色区的大小为 l1 cache 的行
   	/* Offset must be a multiple of the alignment. */
   	if (cachep->colour_off < cachep->align)
   		cachep->colour_off = cachep->align;

   	if (slab_is_available())
   		gfp = GFP_KERNEL;
   	else
   		gfp = GFP_NOWAIT;

   	...

   	kasan_cache_create(cachep, &size, &flags);

   	size = ALIGN(size, cachep->align);
   	/*
   	 * We should restrict the number of objects in a slab to implement
   	 * byte sized index. Refer comment on SLAB_OBJ_MIN_SIZE definition.
   	 */
   	if (FREELIST_BYTE_INDEX && size < SLAB_OBJ_MIN_SIZE)
   		size = ALIGN(SLAB_OBJ_MIN_SIZE, cachep->align);

   	...

   	if (set_objfreelist_slab_cache(cachep, size, flags)) { // 设置 slab 管理器的内存布局格式
   		flags |= CFLGS_OBJFREELIST_SLAB;
   		goto done;
   	}

   	if (set_off_slab_cache(cachep, size, flags)) { // 这里对 3 中布局不做详细分析
   		flags |= CFLGS_OFF_SLAB;
   		goto done;
   	}

   	if (set_on_slab_cache(cachep, size, flags)) // 这 3 个函数最终都会调用 calculate_slab_order
   		goto done;

   	return -E2BIG;

   done:
   	cachep->freelist_size = cachep->num * sizeof(freelist_idx_t); // freelist 是 slab 分配器的管理区
   	cachep->flags = flags;
   	cachep->allocflags = __GFP_COMP;
   	if (flags & SLAB_CACHE_DMA)
   		cachep->allocflags |= GFP_DMA;
   	if (flags & SLAB_CACHE_DMA32)
   		cachep->allocflags |= GFP_DMA32;`
   	if (flags & SLAB_RECLAIM_ACCOUNT)
   		cachep->allocflags |= __GFP_RECLAIMABLE;
   	cachep->size = size; // 一个 slab 对象的大小
   	cachep->reciprocal_buffer_size = reciprocal_value(size);

   	...

   	if (OFF_SLAB(cachep)) {
   		cachep->freelist_cache =
   			kmalloc_slab(cachep->freelist_size, 0u);
   	}

   	err = setup_cpu_cache(cachep, gfp); // 继续配置 slab，包括 slab 节点，下面介绍
   	if (err) {
   		__kmem_cache_release(cachep);
   		return err;
   	}

   	return 0;
   }
   ```

#### slab 分配器的内存布局

slab 分配器的内存布局通常由 3 个部分组成，见图 slab_structure：

- 着色区。
- n 个 slab 对象。
- 管理区。管理区可以看作一个 freelist 数组，数组的每个成员大小为 1 字节，每个成员管理一个 slab 对象。

1. `calculate_slab_order`

   这个函数会解决如下 3 个问题：

   - 一个 slab 分配器需要多少个物理页面。
   - 一个 slab 分配器中能够包含多少个 slab 对象。
   - 一个 slab 分配器中包含多少个着色区。

   ```c
   static size_t calculate_slab_order(struct kmem_cache *cachep,
   				size_t size, slab_flags_t flags)
   {
   	size_t left_over = 0;
   	int gfporder;

   	for (gfporder = 0; gfporder <= KMALLOC_MAX_ORDER; gfporder++) { // 从 0 开始计算最合适（最小）的 gfporder
   		unsigned int num;
   		size_t remainder;

   		num = cache_estimate(gfporder, size, flags, &remainder); // 计算该 gfporder 下能容纳多少个 slab 对象
   		if (!num) // 不能容纳对象，显然不行
   			continue;

   		/* Can't handle number of objects more than SLAB_OBJ_MAX_NUM */
   		if (num > SLAB_OBJ_MAX_NUM) // SLAB_OBJ_MAX_NUM 是一个 slab 分配器能容纳的最多的对象数
   			break;

   		if (flags & CFLGS_OFF_SLAB) {
   			struct kmem_cache *freelist_cache;
   			size_t freelist_size;

   			freelist_size = num * sizeof(freelist_idx_t);
   			freelist_cache = kmalloc_slab(freelist_size, 0u);
   			if (!freelist_cache)
   				continue;

   			/*
   			 * Needed to avoid possible looping condition
   			 * in cache_grow_begin()
   			 */
   			if (OFF_SLAB(freelist_cache))
   				continue;

   			/* check if off slab has enough benefit */
   			if (freelist_cache->size > cachep->size / 2)
   				continue;
   		}

   		/* Found something acceptable - save it away */
   		cachep->num = num; // 不大不小，先保存下来，之后有更好的再换
   		cachep->gfporder = gfporder;
   		left_over = remainder; // remainder 是剩余空间

   		/*
   		 * A VFS-reclaimable slab tends to have most allocations
   		 * as GFP_NOFS and we really don't want to have to be allocating
   		 * higher-order pages when we are unable to shrink dcache.
   		 */
   		if (flags & SLAB_RECLAIM_ACCOUNT)
   			break;

   		/*
   		 * Large number of objects is good, but very large slabs are
   		 * currently bad for the gfp()s.
   		 */
   		if (gfporder >= slab_max_order)
   			break;

   		/*
   		 * Acceptable internal fragmentation?
   		 */
   		if (left_over * 8 <= (PAGE_SIZE << gfporder)) // 剩余空间不能太大
   			break;
   	}
   	return left_over;
   }
   ```

#### 配置 slab 描述符

确定了 slab 分配器的内存布局后，调用 `setup_cpu_cache` 继续配置 slab 描述符。主要是配置如下两个变量：

- limit：空闲对象的最大数量，根据对象的大小计算。
- batchcount：表示本地对象缓冲池和共享对象缓冲池之间填充对象的数量，通常是 limit 的一半。

之后再调用 `do_tune_cpucache` 配置对象缓冲池。

1. `do_tune_cpucache`

   ```c
   static int do_tune_cpucache(struct kmem_cache *cachep, int limit,
   			    int batchcount, int shared, gfp_t gfp)
   {
   	struct array_cache __percpu *cpu_cache, *prev;
   	int cpu;

   	cpu_cache = alloc_kmem_cache_cpus(cachep, limit, batchcount); // 配置本地对象缓冲池
   	if (!cpu_cache)
   		return -ENOMEM;

   	prev = cachep->cpu_cache;
   	cachep->cpu_cache = cpu_cache;
   	/*
   	 * Without a previous cpu_cache there's no need to synchronize remote
   	 * cpus, so skip the IPIs.
   	 */
   	if (prev)
   		kick_all_cpus_sync();

   	check_irq_on();
   	cachep->batchcount = batchcount;
   	cachep->limit = limit;
   	cachep->shared = shared;

   	if (!prev)
   		goto setup_node;

   	for_each_online_cpu(cpu) { // 当 slab 描述符之前有本地对象缓冲池时，遍历在线的 cpu，清空本地缓冲池
   		LIST_HEAD(list);
   		int node;
   		struct kmem_cache_node *n;
   		struct array_cache *ac = per_cpu_ptr(prev, cpu);

   		node = cpu_to_mem(cpu);
   		n = get_node(cachep, node);
   		spin_lock_irq(&n->list_lock);
   		free_block(cachep, ac->entry, ac->avail, node, &list); // 清空本地缓冲池
   		spin_unlock_irq(&n->list_lock);
   		slabs_destroy(cachep, &list);
   	}
   	free_percpu(prev);

   setup_node:
   	return setup_kmem_cache_nodes(cachep, gfp); // 遍历系统中所有的内存节点
   }
   ```

   - 本地/共享对象缓冲池

     ```c
     struct array_cache {
     	unsigned int avail;
     	unsigned int limit;
     	unsigned int batchcount;
     	unsigned int touched;
     	void *entry[];	/*
     			 * Must have this definition in here for the proper
     			 * alignment of array_cache. Also simplifies accessing
     			 * the entries.
     			 */
     };
     ```

2. 初始化本地对象缓冲池

   ```c
   static struct array_cache __percpu *alloc_kmem_cache_cpus(
   		struct kmem_cache *cachep, int entries, int batchcount)
   {
   	int cpu;
   	size_t size;
   	struct array_cache __percpu *cpu_cache;

   	size = sizeof(void *) * entries + sizeof(struct array_cache);
   	cpu_cache = __alloc_percpu(size, sizeof(void *)); // 分配

   	if (!cpu_cache)
   		return NULL;

   	for_each_possible_cpu(cpu) {
   		init_arraycache(per_cpu_ptr(cpu_cache, cpu), // 这是干嘛，遍历每个 cpu
   				entries, batchcount); // 初始化 array_cache 中的成员
   	}

   	return cpu_cache;
   }
   ```

3. `setup_kmem_cache_nodes` -> `setup_kmem_cache_node`

	```c
	static int setup_kmem_cache_node(struct kmem_cache *cachep,
				int node, gfp_t gfp, bool force_change)
	{
	int ret = -ENOMEM;
	struct kmem_cache_node *n;
	struct array_cache *old_shared = NULL;
	struct array_cache *new_shared = NULL;
	struct alien_cache **new_alien = NULL;
	LIST_HEAD(list);

	if (use_alien_caches) {
		new_alien = alloc_alien_cache(node, cachep->limit, gfp);
		if (!new_alien)
			goto fail;
	}

	if (cachep->shared) {
		new_shared = alloc_arraycache(node,
			cachep->shared * cachep->batchcount, 0xbaadf00d, gfp);
		if (!new_shared)
			goto fail;
	}

	ret = init_cache_node(cachep, node, gfp); // 分配 kmem_cache_node 节点
	if (ret)
		goto fail;

	n = get_node(cachep, node);
	spin_lock_irq(&n->list_lock);
	if (n->shared && force_change) {
		free_block(cachep, n->shared->entry,
				n->shared->avail, node, &list);
		n->shared->avail = 0;
	}

	if (!n->shared || force_change) {
		old_shared = n->shared;
		n->shared = new_shared;
		new_shared = NULL;
	}

	if (!n->alien) {
		n->alien = new_alien;
		new_alien = NULL;
	}

	spin_unlock_irq(&n->list_lock);
	slabs_destroy(cachep, &list);

	/*
	 * To protect lockless access to n->shared during irq disabled context.
	 * If n->shared isn't NULL in irq disabled context, accessing to it is
	 * guaranteed to be valid until irq is re-enabled, because it will be
	 * freed after synchronize_rcu().
	 */
	if (old_shared && force_change)
		synchronize_rcu();

	return ret;
	}
	```

	- slab 节点

	  `kmem_cache_node` 主要包含 3 个链表。

	  ```c
     struct kmem_cache_node {
       	spinlock_t list_lock;

       #ifdef CONFIG_SLAB
       	struct list_head slabs_partial;	/* partial list first, better asm code */
       	struct list_head slabs_full;
       	struct list_head slabs_free;
       	unsigned long total_slabs;	/* length of all slab lists */
       	unsigned long free_slabs;	/* length of free slab list only */
       	unsigned long free_objects; // 表示 3 个链表中所有的空闲对象
       	unsigned int free_limit;    // 所有 slab 分配器上容许空闲对象的对大数目
       	unsigned int colour_next;	/* Per-node cache coloring */
       	struct array_cache *shared;	/* shared per node */
       	struct alien_cache **alien;	/* on other nodes */
       	unsigned long next_reap;	/* updated without locking */
       	int free_touched;		/* updated without locking */
       #endif

       	atomic_long_t nr_slabs;
       	atomic_long_t total_objects;
       	struct list_head full;
       };
     ```


至此，slab 描述符完成创建。

#### 分配 slab 对象

分配过程对于我这个初学者来说过于复杂，所以先看看图，理理关系，再看具体实现。

![alloc_slab_obj.png](https://github.com/UtopianFuture/UtopianFuture.github.io/blob/master/image/alloc_slab_obj.png?raw=true)

- `kmem_cache_alloc`
  - `slab_alloc`
    - 关中断 `local_irq_save(save_flags);`
    - 调用 `__do_cache_alloc`
    - 开中断
      - `__do_cache_alloc`
        - `____cache_alloc`
          - `cpu_cache_get` 获取该 slab 描述符的本地对象缓冲池
          - 判断本地对象缓冲池中是否有空闲对象，如果有就直接 `objp = ac->entry[--ac->avail];`
          - 没有，调用 `cache_alloc_refill` 分配
            - `cache_alloc_refill`
              - `get_node` 获取该 slab 描述符对应的节点（kmem_cache_node）

1. `____cache_alloc`

   ```c
   static inline void *____cache_alloc(struct kmem_cache *cachep, gfp_t flags)
   {
   	void *objp;
   	struct array_cache *ac;

   	check_irq_off();

   	ac = cpu_cache_get(cachep);
   	if (likely(ac->avail)) { // 判断本地对象缓冲池中是否有空闲对象
   		ac->touched = 1;
   		objp = ac->entry[--ac->avail]; // 直接从本地对象缓冲池中获取
   									   // 第一次 ac->avail 都是 0，需要重新分配
   		STATS_INC_ALLOCHIT(cachep);
   		goto out;
   	}

   	STATS_INC_ALLOCMISS(cachep);
   	objp = cache_alloc_refill(cachep, flags); // 没有，重新分配
   	/*
   	 * the 'ac' may be updated by cache_alloc_refill(),
   	 * and kmemleak_erase() requires its correct value.
   	 */
   	ac = cpu_cache_get(cachep);

       ...

   	return objp;
   }
   ```

2. `cache_alloc_refill`

   ```c
   static void *cache_alloc_refill(struct kmem_cache *cachep, gfp_t flags)
   {
   	int batchcount;
   	struct kmem_cache_node *n;
   	struct array_cache *ac, *shared;
   	int node;
   	void *list = NULL;
   	struct page *page;

   	check_irq_off();
   	node = numa_mem_id();

   	ac = cpu_cache_get(cachep); // 该描述符的本地对象缓冲池
   	batchcount = ac->batchcount;
   	n = get_node(cachep, node); // 该描述符对应的 slab 节点

   	BUG_ON(ac->avail > 0 || !n);
   	shared = READ_ONCE(n->shared); // 共享对象缓冲池
   	if (!n->free_objects && (!shared || !shared->avail)) // slab 节点没有空闲对象
   		goto direct_grow; // 同时共享对象缓冲池为空或共享对象缓冲池中也没有空闲对象，需要分配

   	spin_lock(&n->list_lock);
   	shared = READ_ONCE(n->shared);

   	/* See if we can refill from the shared array */
   	if (shared && transfer_objects(ac, shared, batchcount)) { // 共享对象缓冲池不为空，
   		shared->touched = 1; // 尝试迁移 batchcount 个空闲对象到本地对象缓冲池 ac 中
   		goto alloc_done;
   	}

   	while (batchcount > 0) {
   		/* Get slab alloc is to come from. */
   		page = get_first_slab(n, false); // 检查 slabs_partial 和 slabs_free 链表
   		if (!page)
   			goto must_grow;

   		check_spinlock_acquired(cachep);

   		batchcount = alloc_block(cachep, ac, page, batchcount);
   		fixup_slab_list(cachep, n, page, &list);
   	}

   must_grow:
   	n->free_objects -= ac->avail;
   alloc_done:
   	spin_unlock(&n->list_lock);
   	fixup_objfreelist_debug(cachep, &list);

   direct_grow:
   	if (unlikely(!ac->avail)) { // 整个内存节点中没有 slab 空闲对象，重新分配 slab 分配器
   		/* Check if we can use obj in pfmemalloc slab */
   		if (sk_memalloc_socks()) {
   			void *obj = cache_alloc_pfmemalloc(cachep, n, flags); // 这是啥意思？

   			if (obj)
   				return obj;
   		}

           // 分配一个 slab 分配器，并返回这个 slab 分配器中第一个物理页面的 page
   		page = cache_grow_begin(cachep, gfp_exact_node(flags), node);

   		/*
   		 * cache_grow_begin() can reenable interrupts,
   		 * then ac could change.
   		 */
   		ac = cpu_cache_get(cachep);
   		if (!ac->avail && page) // 从刚分配的 slab 分配器的空闲对象中迁移 batchcoutn 个空闲对象到本地对象缓冲池中。
   			alloc_block(cachep, ac, page, batchcount); // ac 是本地对象缓冲池，
   		cache_grow_end(cachep, page);

   		if (!ac->avail)
   			return NULL;
   	}
   	ac->touched = 1; // 表示刚刚使用过本地对象缓冲池

   	return ac->entry[--ac->avail]; // 返回一个空闲对象
   }
   ```

   - `alloc_block` 整个过程：

     ```c
     static __always_inline int alloc_block(struct kmem_cache *cachep,
     		struct array_cache *ac, struct page *page, int batchcount)
     {
         // cachep->num 应该是描述符中空闲对象的阀值，但问题是我在 kmem_cache 中
         // 每找到这个成员啊！
     	while (page->active < cachep->num && batchcount--) {
     		STATS_INC_ALLOCED(cachep);
     		STATS_INC_ACTIVE(cachep);
     		STATS_SET_HIGH(cachep);

             // 进行迁移，本地对象缓冲池中的空闲对象不断增加
     		ac->entry[ac->avail++] = slab_get_obj(cachep, page);
     	}

     	return batchcount;
     }

     // 这个函数描述了如何从 slab 描述符中获取 slab 对象
     static void *slab_get_obj(struct kmem_cache *cachep, struct page *page)
     {
     	void *objp;

     	objp = index_to_obj(cachep, page, get_free_obj(page, page->active));
     	page->active++; // active 表示活动的对象的索引？

     	return objp;
     }

     // freelist 是管理区，其管理每个对象
     static inline freelist_idx_t get_free_obj(struct page *page, unsigned int idx)
     {
     	return ((freelist_idx_t *)page->freelist)[idx];
     }

     // 将 slab 分配器的第 idx 个空闲对象转化为对象格式。其实就是返回该对象的地址
     static inline void *index_to_obj(struct kmem_cache *cache, struct page *page,
     				 unsigned int idx)
     {
     	return page->s_mem + cache->size * idx;
     }
     ```

   - page 中和 slab 相关的几个成员：

     ```c
     void *s_mem;	/* slab: first object */
     struct kmem_cache *slab_cache; // 指向这个 slab 分配器所属的 slab 描述符？
     unsigned int active； // 表示 slab 分配器中活跃对象的数目。所谓活跃对象就是指对象已经被迁移到对象缓冲池中
         				 // 当其为0 时，表示这个 slab 分配器已经没有活跃对象，可以被销毁。
     void *freelist; // 管理区
     ```

3. 创建 slab 分配器

   ```c
   static struct page *cache_grow_begin(struct kmem_cache *cachep,
   				gfp_t flags, int nodeid)
   {
   	void *freelist;
   	size_t offset;
   	gfp_t local_flags;
   	int page_node;
   	struct kmem_cache_node *n;
   	struct page *page;

   	/*
   	 * Be lazy and only check for valid flags here,  keeping it out of the
   	 * critical path in kmem_cache_alloc().
   	 */
   	if (unlikely(flags & GFP_SLAB_BUG_MASK))
   		flags = kmalloc_fix_flags(flags);

   	WARN_ON_ONCE(cachep->ctor && (flags & __GFP_ZERO));
   	local_flags = flags & (GFP_CONSTRAINT_MASK|GFP_RECLAIM_MASK);

   	check_irq_off();
   	if (gfpflags_allow_blocking(local_flags))
   		local_irq_enable();

   	/*
   	 * Get mem for the objs.  Attempt to allocate a physical page from
   	 * 'nodeid'.
   	 */
   	page = kmem_getpages(cachep, local_flags, nodeid); // 分配 slab 分配器需要的物理页面
   	if (!page)
   		goto failed;

   	page_node = page_to_nid(page);
   	n = get_node(cachep, page_node);

   	/* Get colour for the slab, and cal the next value. */
   	n->colour_next++; // 下一个 slab 分配器应该包括的着色区数目
   	if (n->colour_next >= cachep->colour) // 重新计算
   		n->colour_next = 0;

   	offset = n->colour_next;
   	if (offset >= cachep->colour)
   		offset = 0;

   	offset *= cachep->colour_off; // 这是什么操作？ colour_off 表示高速缓存行的大小

   	...

   	/* Get slab management. */
   	freelist = alloc_slabmgmt(cachep, page, offset, // 计算管理区的起始地址
   			local_flags & ~GFP_CONSTRAINT_MASK, page_node);
   	if (OFF_SLAB(cachep) && !freelist)
   		goto opps1;

   	slab_map_pages(cachep, page, freelist);

   	cache_init_objs(cachep, page); // 初始化 slab 分配器中的对象

   	if (gfpflags_allow_blocking(local_flags))
   		local_irq_disable();

   	return page;
   }
   ```

- 为什么要有着色区？

  着色区让每个 slab 分配器对应不同数量的高速缓存行，着色区的大小为 `colour_next * colour_off`，其中 `colour_next` 时从0 到这个 slab 描述符中计算出来的 colour 最大值，colour_off 为 L1 高速缓存行大小。这样可以**使不同的 slab 分配器上同一个相对位置 slab 对象的起始地址再高速缓存中相互错开（？）**，有利于提高高速缓存行的访问效率。

#### 释放 slab 对象

释放 slab 对象的关键函数是 `___cache_free`，

- `kmem_cache_free`
  - 开关中断
  - `__cache_free`
    - `___cache_free`

```c
void ___cache_free(struct kmem_cache *cachep, void *objp,
		unsigned long caller)
{
	struct array_cache *ac = cpu_cache_get(cachep);

	...

	if (ac->avail < ac->limit) {
		STATS_INC_FREEHIT(cachep); // 什么也不做
	} else {
		STATS_INC_FREEMISS(cachep);
		cache_flusharray(cachep, ac); // 回收 slab 分配器
	}

	...

	__free_one(ac, objp); // 直接将对象释放到本地对象缓冲池中
}
```

#### slab 分配器和伙伴系统的接口函数

slab 分配器创建 slab 对象时会调用伙伴系统的分配物理页面接口函数去分配 2^cachep->gfporder 个页面，调用的函数是 `kmem_getpages`。

```c
static struct page *kmem_getpages(struct kmem_cache *cachep, gfp_t flags,
								int nodeid)
{
	struct page *page;

	flags |= cachep->allocflags;

	page = __alloc_pages_node(nodeid, flags, cachep->gfporder); // 这里调用前面分析的 __alloc_pages
	if (!page) {
		slab_out_of_memory(cachep, flags, nodeid);
		return NULL;
	}

	account_slab_page(page, cachep->gfporder, cachep, flags); // 应该是计算该 slab 分配器占用的物理页面
	__SetPageSlab(page);
	/* Record if ALLOC_NO_WATERMARKS was set when allocating the slab */
	if (sk_memalloc_socks() && page_is_pfmemalloc(page))
		SetPageSlabPfmemalloc(page);

	return page;
}
```

#### 管理区 freelist

上文提到管理区可以看作一个 freelist 数组，数组的每个成员大小为 1 字节，每个成员管理一个 slab 对象。这里我们看看 freelist 是怎样管理 slab 分配器中的对象的。

在 [分配 slab 对象](# 分配 slab 对象)中介绍到 `cache_alloc_refill` 会判断本地/共享对象缓冲池中是否有空闲对象，如果没有的话就需要调用 `cache_grow_begin` 创建一个新的 slab 分配器。而在 `cache_init_objs` 中会把管理区中的 freelist 数组按顺序标号。

![freelist_init_state.png](https://github.com/UtopianFuture/UtopianFuture.github.io/blob/master/image/freelist_init_state.png?raw=true)

```c
static void cache_init_objs(struct kmem_cache *cachep,
			    struct page *page)
{
	int i;
	void *objp;
	bool shuffled;

	...

	for (i = 0; i < cachep->num; i++) {
		objp = index_to_obj(cachep, page, i);
		objp = kasan_init_slab_obj(cachep, objp);

		/* constructor could break poison info */
		if (DEBUG == 0 && cachep->ctor) {
			kasan_unpoison_object_data(cachep, objp);
			cachep->ctor(objp);
			kasan_poison_object_data(cachep, objp);
		}

		if (!shuffled)
			set_free_obj(page, i, i);
	}
}
```

上文介绍的 `alloc_block` 过程根据生成标号将不活跃的对象迁移到本地对象缓冲池中。

#### kmalloc

`kmalloc` 的核心实现就是 slab 机制。类似于伙伴系统，在内存块中按照 2^order 字节来创建多个 slab 描述符，如 16 字节、32 字节、64 字节等，同时系统会创建 `kmalloc-16`, `kmalloc-32`, `kmalloc-64` 等描述符，在系统启动时通过 `create_kmalloc_caches` 函数完成。例如要分配 30 字节的小块内存，可以用 `kmalloc(30, GFP_KERNEL)` 来实现，之后系统会从 kmalloc-32 slab 描述符中分配一个对象。

```c
static __always_inline void *kmalloc(size_t size, gfp_t flags)
{
	if (__builtin_constant_p(size)) {
#ifndef CONFIG_SLOB
		unsigned int index;
#endif
		if (size > KMALLOC_MAX_CACHE_SIZE)
			return kmalloc_large(size, flags);
#ifndef CONFIG_SLOB
		index = kmalloc_index(size); // 可以找到使用哪个 slab 描述符

		if (!index)
			return ZERO_SIZE_PTR;

		return kmem_cache_alloc_trace(
				kmalloc_caches[kmalloc_type(flags)][index],
				flags, size);
#endif
	}
	return __kmalloc(size, flags);
}
```

```c
static __always_inline unsigned int __kmalloc_index(size_t size,
						    bool size_is_constant)
{
	if (!size)
		return 0;

	if (size <= KMALLOC_MIN_SIZE)
		return KMALLOC_SHIFT_LOW;

	if (KMALLOC_MIN_SIZE <= 32 && size > 64 && size <= 96)
		return 1;
	if (KMALLOC_MIN_SIZE <= 64 && size > 128 && size <= 192)
		return 2;
	if (size <=          8) return 3;
	if (size <=         16) return 4;
	if (size <=         32) return 5;
	if (size <=         64) return 6;
	if (size <=        128) return 7;
	if (size <=        256) return 8;
	if (size <=        512) return 9;
	if (size <=       1024) return 10;
	if (size <=   2 * 1024) return 11;
	if (size <=   4 * 1024) return 12;
	if (size <=   8 * 1024) return 13;
	if (size <=  16 * 1024) return 14;
	if (size <=  32 * 1024) return 15;
	if (size <=  64 * 1024) return 16;
	if (size <= 128 * 1024) return 17;
	if (size <= 256 * 1024) return 18;
	if (size <= 512 * 1024) return 19;
	if (size <= 1024 * 1024) return 20;
	if (size <=  2 * 1024 * 1024) return 21;
	if (size <=  4 * 1024 * 1024) return 22;
	if (size <=  8 * 1024 * 1024) return 23;
	if (size <=  16 * 1024 * 1024) return 24;
	if (size <=  32 * 1024 * 1024) return 25;

	/* Will never be reached. Needed because the compiler may complain */
	return -1;
}
```

### vmalloc

这部分只是大概了解 vmalloc 是干什么的和其分配流程，但其详细的实现还不懂。

上面介绍了 `kmalloc` 使用 slab 分配器分配小块的、连续的物理内存，因为 slab 分配器在创建的时候也需要使用伙伴系统分配物理内存页面的接口，所以 slab 分配器建立在一个物理地址连续的大块内存之上。那如果在内核中不需要连续的物理地址，而仅仅需要虚拟地址连续的内存块该如何处理？这就是 `vmalloc` 的工作。

vmalloc 有不同的给外界提供了不同的接口，如 `__vmalloc`, `vmalloc` 等，但它们都是 `__vmalloc_node` 的封装，然后再调用`__vmalloc_node_range`。

```c
void *__vmalloc_node(unsigned long size, unsigned long align,
			    gfp_t gfp_mask, int node, const void *caller)
{
	return __vmalloc_node_range(size, align, VMALLOC_START, VMALLOC_END,
				gfp_mask, PAGE_KERNEL, 0, node, caller);
}
```

vmalloc 分配的空间在 [内存分布](# 内存分布) 小节中的图中有清晰的说明。

1. vmalloc 的核心功能都是在 `__vmalloc_node_range` 函数中实现的。

   ```c
   void *__vmalloc_node_range(unsigned long size, unsigned long align,
   			unsigned long start, unsigned long end, gfp_t gfp_mask,
   			pgprot_t prot, unsigned long vm_flags, int node,
   			const void *caller)
   {
   	struct vm_struct *area;
   	void *addr;
   	unsigned long real_size = size;
   	unsigned long real_align = align;
   	unsigned int shift = PAGE_SHIFT;

   	...

   again:
   	area = __get_vm_area_node(real_size, align, shift, VM_ALLOC |
   				  VM_UNINITIALIZED | vm_flags, start, end, node,
   				  gfp_mask, caller);
   	if (!area) {
   		warn_alloc(gfp_mask, NULL,
   			"vmalloc error: size %lu, vm_struct allocation failed",
   			real_size);
   		goto fail;
   	}

   	addr = __vmalloc_area_node(area, gfp_mask, prot, shift, node); // 分配物理内存，并和 vm_struct 空间
   	if (!addr)                                                     // 建立映射关系
   		goto fail;

   	/*
   	 * In this function, newly allocated vm_struct has VM_UNINITIALIZED
   	 * flag. It means that vm_struct is not fully initialized.
   	 * Now, it is fully initialized, so remove this flag here.
   	 */
   	clear_vm_uninitialized_flag(area);

   	size = PAGE_ALIGN(size);
   	kmemleak_vmalloc(area, size, gfp_mask);

   	return addr;

   fail:
   	if (shift > PAGE_SHIFT) {
   		shift = PAGE_SHIFT;
   		align = real_align;
   		size = real_size;
   		goto again;
   	}

   	return NULL;
   }
   ```

2. `__get_vm_area_node`

   ```c
   static struct vm_struct *__get_vm_area_node(unsigned long size,
   		unsigned long align, unsigned long shift, unsigned long flags,
   		unsigned long start, unsigned long end, int node,
   		gfp_t gfp_mask, const void *caller)
   {
   	struct vmap_area *va;
   	struct vm_struct *area;
   	unsigned long requested_size = size;

   	BUG_ON(in_interrupt());
   	size = ALIGN(size, 1ul << shift);
   	if (unlikely(!size))
   		return NULL;

   	if (flags & VM_IOREMAP) // 如果是用于 IOREMAP，那么默认按 128 个页面对齐（？）
   		align = 1ul << clamp_t(int, get_count_order_long(size),
   				       PAGE_SHIFT, IOREMAP_MAX_ORDER);

   	area = kzalloc_node(sizeof(*area), gfp_mask & GFP_RECLAIM_MASK, node); // 怎么调用 kmalloc_node 了
   	if (unlikely(!area))
   		return NULL;

   	if (!(flags & VM_NO_GUARD))
   		size += PAGE_SIZE;

   	va = alloc_vmap_area(size, align, start, end, node, gfp_mask); // 分配 vmalloc 区域
   	if (IS_ERR(va)) {
   		kfree(area);
   		return NULL;
   	}

   	kasan_unpoison_vmalloc((void *)va->va_start, requested_size);

   	setup_vmalloc_vm(area, va, flags, caller); // 构建一个 vm_struct 空间

   	return area;
   }
   ```

   - `alloc_vmap_area` 负责分配 vmalloc 区域。其在 vmalloc 区域中查找一块大小合适的并且没有使用的空间，这段空间称为缝隙（hole）。
     - 从 vmalloc 区域的起始位置 `VMALLOC_START` 开始，首先从红黑树 `vmap_area_root` 上查找，这棵树存放着系统正在使用的 vmalloc 区域，遍历左叶子节点赵区域地址最小的区域。如果区域的开始地址等于 `VMALLOC_START` ，说明这个区域是第一个 vmalloc 区域；如果红黑树没有一个节点，说明整个 vmalloc 区域都是空的。
     - 从 `VMALLOC_START` 开始查找每个已存在的 vmalloc 区域的缝隙能够容纳目前申请的大小。如果已有的 vmalloc 区域的缝隙不能容纳，那么从最后一块 vmalloc 区域的结束地址开辟一个新的 vmalloc 区域。
     - 找到新的区域缝隙后，调用 `insert_vmap_area` 将其注册到红黑树。

### 进程地址空间

### malloc

### Reference

[1] https://zhuanlan.zhihu.com/p/339800986

[2] 奔跑吧 Linux 内核，卷 1：基础架构

### 些许感想

内存管理的学习是循序渐进的，开始可能只能将书中的内容稍加精简记录下来，随着看的内容越多，难免对前面的内容产生疑惑，这时再回过头去理解，同时补充自己的笔记。这样多来几次也就理解、记住了。所以最开始“抄”的过程对于我来说无法跳过。
