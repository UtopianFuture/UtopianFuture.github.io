## Memory Management

分析内存管理为了解决一个很简单的问题：用户进程从发起存储空间的请求开始到得到该内存空间需要经过哪些步骤？

首先用户进程发起请求，这个请求肯定是由内核函数来响应，然后调用内核的内存分配函数来分配空间。

### 数据结构

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

   	cpu_cache = alloc_kmem_cache_cpus(cachep, limit, batchcount);
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

   	for_each_online_cpu(cpu) {
   		LIST_HEAD(list);
   		int node;
   		struct kmem_cache_node *n;
   		struct array_cache *ac = per_cpu_ptr(prev, cpu);

   		node = cpu_to_mem(cpu);
   		n = get_node(cachep, node);
   		spin_lock_irq(&n->list_lock);
   		free_block(cachep, ac->entry, ac->avail, node, &list);
   		spin_unlock_irq(&n->list_lock);
   		slabs_destroy(cachep, &list);
   	}
   	free_percpu(prev);

   setup_node:
   	return setup_kmem_cache_nodes(cachep, gfp);
   }
   ```

#### 分配 slab 对象

#### 释放 slab 对象

#### slab 分配器和伙伴系统的接口函数

#### 管理区 freelist

#### kmalloc

### 非连续内存区管理

### Reference

[1] https://zhuanlan.zhihu.com/p/339800986

[2] 奔跑吧 Linux 内核，卷 1：基础架构
