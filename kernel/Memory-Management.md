## Memory Management

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

	ZONE_PADDING(_pad1_)

	/* Fields commonly accessed by the page reclaim scanner */
	spinlock_t		lru_lock;
	struct list_head	active_list;
	struct list_head	inactive_list;
	unsigned long		nr_scan_active;
	unsigned long		nr_scan_inactive;
	unsigned long		nr_active;
	unsigned long		nr_inactive;
	unsigned long		pages_scanned;	   /* since last reclaim */
	int			all_unreclaimable; /* All pages pinned */

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

管理区分配器搜索能够满足分配请求的管理区，在每个管理区中，伙伴系统负责分配页框，每 CPU 页框高速缓存用来满足但个页框的分配请求。

![image-20211216093255057](/home/guanshun/.config/Typora/typora-user-images/image-20211216093255057.png)

#### 管理区分配器

任务：分配一个包含足够空闲页框的 zone 来满足内存请求。

##### 请求和释放页框

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

![image-20211216094418595](/home/guanshun/.config/Typora/typora-user-images/image-20211216094418595.png)

`alloc_pages` 主要是调用 `__alloc_pages` 。

```c
/*
 * This is the 'heart' of the zoned buddy allocator.
 */
struct page *__alloc_pages(gfp_t gfp, unsigned int order, int preferred_nid,
							nodemask_t *nodemask)
{
	struct page *page;
	unsigned int alloc_flags = ALLOC_WMARK_LOW;
	gfp_t alloc_gfp; /* The gfp_t that was actually used for allocation */
	struct alloc_context ac = { };

	/*
	 * There are several places where we assume that the order value is sane
	 * so bail out early if the request is out of bound.
	 */
	if (unlikely(order >= MAX_ORDER)) {
		WARN_ON_ONCE(!(gfp & __GFP_NOWARN));
		return NULL;
	}

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
			&alloc_gfp, &alloc_flags))
		return NULL;

	/*
	 * Forbid the first pass from falling back to types that fragment
	 * memory until all local zones are considered.
	 */
	alloc_flags |= alloc_flags_nofragment(ac.preferred_zoneref->zone, gfp);

	/* First allocation attempt */
	page = get_page_from_freelist(alloc_gfp, order, alloc_flags, &ac);
	if (likely(page))
		goto out;

	alloc_gfp = gfp;
	ac.spread_dirty_pages = false;

	/*
	 * Restore the original nodemask if it was potentially replaced with
	 * &cpuset_current_mems_allowed to optimize the fast-path attempt.
	 */
	ac.nodemask = nodemask;

	page = __alloc_pages_slowpath(alloc_gfp, order, &ac);

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

有 4 个函数和宏能够释放页框，这里只分析 `__free_pages`

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

这几个函数之后都需要详细分析。它们是整个内存管理的核心。

#### 高端内存页框的内核映射

64 位机器不存在这个问题，因为地址空间足够访问现有内存。这部分暂时大致了解，之后有需要再分析。

#### 伙伴系统算法

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
> - 另外拆分和合并涉及到 较多的链表和位图操作，开销还是比较大的。
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

##### 数据结构

zone 中的 `struct free_area free_area[MAX_ORDER];` 表示块大小。

```c
struct free_area {
	struct list_head	free_list[MIGRATE_TYPES];
	unsigned long		nr_free; // 大小为 2^k 的空闲块的个数
};
```

`free_area` 数组的第 k 个元素表示所有大小为 2^k 的空闲块。

##### 分配块

`__rmqeuue` 用来在 zone 中找到一个空闲块。

```c
/*
 * Do the hard work of removing an element from the buddy allocator.
 * Call me with the zone->lock already held.
 */
static __always_inline struct page *
__rmqueue(struct zone *zone, unsigned int order, int migratetype,
						unsigned int alloc_flags)
{ // zone 表示 zone 描述符地址， order 表示请求的 page 大小
	struct page *page;

	if (IS_ENABLED(CONFIG_CMA)) {
		/*
		 * Balance movable allocations between regular and CMA areas by
		 * allocating from CMA when over half of the zone's free memory
		 * is in the CMA area.
		 */
		if (alloc_flags & ALLOC_CMA &&
		    zone_page_state(zone, NR_FREE_CMA_PAGES) >
		    zone_page_state(zone, NR_FREE_PAGES) / 2) {
			page = __rmqueue_cma_fallback(zone, order);
			if (page)
				goto out;
		}
	}
retry:
	page = __rmqueue_smallest(zone, order, migratetype);
	if (unlikely(!page)) {
		if (alloc_flags & ALLOC_CMA)
			page = __rmqueue_cma_fallback(zone, order);

		if (!page && __rmqueue_fallback(zone, order, migratetype,
								alloc_flags))
			goto retry;
	}
out:
	if (page)
		trace_mm_page_alloc_zone_locked(page, order, migratetype);
	return page;
}
```

```c
/*
 * Go through the free lists for the given migratetype and remove
 * the smallest available page from the freelists
 */
static __always_inline
struct page *__rmqueue_smallest(struct zone *zone, unsigned int order,
						int migratetype)
{
	unsigned int current_order;
	struct free_area *area;
	struct page *page;

	/* Find a page of the appropriate size in the preferred list */
	for (current_order = order; current_order < MAX_ORDER; ++current_order) {
		area = &(zone->free_area[current_order]);
		page = get_page_from_free_area(area, migratetype);
		if (!page)
			continue;
		del_page_from_free_list(page, zone, current_order);
		expand(zone, page, order, current_order, migratetype);
		set_pcppage_migratetype(page, migratetype);
		return page;
	}

	return NULL;
}
```

##### 释放块

```c
/*
 * Freeing function for a buddy system allocator.
 *
 * The concept of a buddy system is to maintain direct-mapped table
 * (containing bit values) for memory blocks of various "orders".
 * The bottom level table contains the map for the smallest allocatable
 * units of memory (here, pages), and each level above it describes
 * pairs of units from the levels below, hence, "buddies".
 * At a high level, all that happens here is marking the table entry
 * at the bottom level available, and propagating the changes upward
 * as necessary, plus some accounting needed to play nicely with other
 * parts of the VM system.
 * At each level, we keep a list of pages, which are heads of continuous
 * free pages of length of (1 << order) and marked with PageBuddy.
 * Page's order is recorded in page_private(page) field.
 * So when we are allocating or freeing one, we can derive the state of the
 * other.  That is, if we allocate a small block, and both were
 * free, the remainder of the region must be split into blocks.
 * If a block is freed, and its buddy is also free, then this
 * triggers coalescing into a block of larger size.
 *
 * -- nyc
 */

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

	VM_BUG_ON(!zone_is_initialized(zone));
	VM_BUG_ON_PAGE(page->flags & PAGE_FLAGS_CHECK_AT_PREP, page);

	VM_BUG_ON(migratetype == -1);
	if (likely(!is_migrate_isolate(migratetype)))
		__mod_zone_freepage_state(zone, 1 << order, migratetype);

	VM_BUG_ON_PAGE(pfn & ((1 << order) - 1), page);
	VM_BUG_ON_PAGE(bad_range(zone, page), page);

continue_merging:
	while (order < max_order) {
		if (compaction_capture(capc, page, order, migratetype)) {
			__mod_zone_freepage_state(zone, -(1 << order),
								migratetype);
			return;
		}
		buddy_pfn = __find_buddy_pfn(pfn, order);
		buddy = page + (buddy_pfn - pfn);

		if (!page_is_buddy(page, buddy, order))
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
		/* If we are here, it means order is >= pageblock_order.
		 * We want to prevent merge between freepages on isolate
		 * pageblock and normal pageblock. Without this, pageblock
		 * isolation could cause incorrect freepage or CMA accounting.
		 *
		 * We don't want to hit this code for the more frequent
		 * low-order merging.
		 */
		if (unlikely(has_isolate_pageblock(zone))) {
			int buddy_mt;

			buddy_pfn = __find_buddy_pfn(pfn, order);
			buddy = page + (buddy_pfn - pfn);
			buddy_mt = get_pageblock_migratetype(buddy);

			if (migratetype != buddy_mt
					&& (is_migrate_isolate(migratetype) ||
						is_migrate_isolate(buddy_mt)))
				goto done_merging;
		}
		max_order = order + 1;
		goto continue_merging;
	}

done_merging:
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

#### 每 CPU 页框高速缓存

内核经常请求和释放单个页框，因此每个 zone 定义了一个 “每 CPU ”页框高速缓存，其包括一些预先分配的页框。主要的数据结构是在 zone 的 pageset。

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
	int low;		/* low watermark, refill needed */
	int high;		/* high watermark, emptying needed */
	int batch;		/* chunk size for buddy add/remove */
	struct list_head list;	/* the list of pages */
};
```

每 CPU 高速缓存通过 `buffered_rmqueue` 在指定的 zone 中分配页框，使用 `free_hot_code_page` 释放页框到每 CPU 高速缓存。



### 内存区管理

### 非连续内存区管理

### Reference

[1] https://www.zhihu.com/search?type=content&q=%E5%86%85%E6%A0%B8%E5%86%85%E5%AD%98%E7%AE%A1%E7%90%86
