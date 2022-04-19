## Memory Management

### 目录

- [内存分布](#内存分布)
- [数据结构](#数据结构)
- [页框管理](#页框管理)
  - [page](#page)
  - [内存管理区](#内存管理区)
    - [pglist_data](#pglist_data)
    - [zone](#zone)
    - [zonelist](#zonelist)

  - [分区页框分配器](#分区页框分配器)
  - [管理区分配器](#管理区分配器)
    - [伙伴系统算法](#伙伴系统算法)
    - [请求页框](#请求页框)
    - [释放页框](#释放页框)

- [内存区管理](#内存区管理)
  - [创建slab描述符](#创建slab描述符)
    - [kmem_cache](#kmem_cache)
  - [slab分配器的内存布局](#slab分配器的内存布局)
  - [配置slab描述符](#配置slab描述符)
  - [分配slab对象](#分配slab对象)
  - [释放slab对象](#释放slab对象)
  - [slab分配器和伙伴系统的接口函数](#slab分配器和伙伴系统的接口函数)
  - [管理区freelist](#管理区freelist)
  - [kmalloc](#kmalloc)
- [vmalloc](#vmalloc)
- [进程地址空间](#进程地址空间)
  - [mm_struct](#mm_struct)
  - [VMA](#VMA)
  - [VMA相关操作](#VMA相关操作)
- [malloc](#malloc)
- [mmap](#mmap)
- [缺页异常处理](#缺页异常处理)
  - [关键函数do_user_addr_fault](#关键函数do_user_addr_fault)
  - [关键函数__handle_mm_fault](#关键函数__handle_mm_fault)
  - [关键函数handle_pte_fault](#关键函数handle_pte_fault)
  - [匿名页面缺页中断](#匿名页面缺页中断)
  - [文件映射缺页中断](#文件映射缺页中断)
  - [写时复制（COW）](#写时复制（COW）)
- [补充知识点](#补充知识点)
- [Reference](#Reference)
- [些许感想](#些许感想)

很多文章都说内存管理是内核中最复杂的部分、最重要的部分之一，在来实验室之后跟着师兄做项目、看代码的这段时间里，渐渐感觉自己的知识框架是不完整的，底下少了一部分，后来发现这部分就是内核，所以开始学习内核。其实这也不是第一次接触内核，之前也陆陆续续的看过一部分，包括做 RISC-V 操作系统实验，LoongArch 内核的启动部分，但始终没有花时间去肯内存管理，进程调度和文件管理这几个核心模块。而师兄也说过，内核都看的懂，啥代码你看不懂。

### 内存分布

64 位 arm linux 内核的内存分布

![linux-address-space.png](https://github.com/UtopianFuture/UtopianFuture.github.io/blob/master/image/linux-address-space.png?raw=true)

### 数据结构

内存管理模块涉及到的结构体之间的关系。

![memory-management-structure.png](https://github.com/UtopianFuture/UtopianFuture.github.io/blob/master/image/memory-management-structure.png?raw=true)

这里涉及多个结构之间的转换，先记录一下。

```c
/*
 * Convert a physical address to a Page Frame Number and back
 */
#define	__phys_to_pfn(paddr)	PHYS_PFN(paddr)
#define	__pfn_to_phys(pfn)	PFN_PHYS(pfn)

#define page_to_pfn __page_to_pfn
#define pfn_to_page __pfn_to_page

/* memmap is virtually contiguous.  */
#define __pfn_to_page(pfn)	(vmemmap + (pfn))
#define __page_to_pfn(page)	(unsigned long)((page) - vmemmap)

#define PFN_ALIGN(x)	(((unsigned long)(x) + (PAGE_SIZE - 1)) & PAGE_MASK)
#define PFN_UP(x)	(((x) + PAGE_SIZE-1) >> PAGE_SHIFT)
#define PFN_DOWN(x)	((x) >> PAGE_SHIFT)
#define PFN_PHYS(x)	((phys_addr_t)(x) << PAGE_SHIFT)
#define PHYS_PFN(x)	((unsigned long)((x) >> PAGE_SHIFT))
```

### 页框管理

内核中以页框（大多数情况为 4K）为最小单位分配内存。page  作为页描述符记录每个页框当前的状态。

##### page

```c
struct page {
	unsigned long flags; // 保存到 node 和 zone 的链接和页框状态
	/*
	 * Five words (20/40 bytes) are available in this union.
	 * WARNING: bit 0 of the first word is used for PageTail(). That
	 * means the other users of this union MUST NOT use the bit to
	 * avoid collision and false-positive PageTail().
	 */
	union {
        // 管理匿名页面/文件映射页面
		struct {	/* Page cache and anonymous pages */
			struct list_head lru;
			/* See page-flags.h for PAGE_MAPPING_FLAGS */
            // 页面所指向的地址空间
            // 内核中的地址空间通常有两个不同的地址空间，
            // 一个用于文件映射页面，如在读取文件时，地址空间用于
            // 将文件的内容与存储介质区关联起来；
            // 另一个用于匿名映射（？）
			struct address_space *mapping;
			pgoff_t index;		/* Our offset within mapping. */ // 为什么需要有这个？
			/**
			 * @private: Mapping-private opaque data.
			 * Usually used for buffer_heads if PagePrivate.
			 * Used for swp_entry_t if PageSwapCache.
			 * Indicates order in the buddy system if PageBuddy.
			 */
			unsigned long private; // 指向私有数据的指针
		};
		struct {	/* page_pool used by netstack */
			/**
			 * @pp_magic: magic value to avoid recycling non
			 * page_pool allocated pages.
			 */
			unsigned long pp_magic;
			struct page_pool *pp;
			unsigned long _pp_mapping_pad;
			unsigned long dma_addr;
			union {
				/**
				 * dma_addr_upper: might require a 64-bit
				 * value on 32-bit architectures.
				 */
				unsigned long dma_addr_upper;
				/**
				 * For frag page support, not supported in
				 * 32-bit architectures with 64-bit DMA.
				 */
				atomic_long_t pp_frag_count;
			};
		};
		struct {	/* slab, slob and slub */
			union {
				struct list_head slab_list; // slab 链表节点
				struct {	/* Partial pages */
					struct page *next;

					int pages;	/* Nr of pages left */
					int pobjects;	/* Approximate count */

				};
			};
			struct kmem_cache *slab_cache; /* not slob */
			/* Double-word boundary */
			void *freelist;		/* first free object */ // 管理区
			union {
				void *s_mem;	/* slab: first object */
				unsigned long counters;		/* SLUB */
				struct {			/* SLUB */
					unsigned inuse:16;
					unsigned objects:15;
					unsigned frozen:1;
				};
			};
		};
		struct {	/* Tail pages of compound page */
			unsigned long compound_head;	/* Bit zero is set */

			/* First tail page only */
			unsigned char compound_dtor;
			unsigned char compound_order;
			atomic_t compound_mapcount;
			unsigned int compound_nr; /* 1 << compound_order */
		};
		struct {	/* Second tail page of compound page */
			unsigned long _compound_pad_1;	/* compound_head */
			atomic_t hpage_pinned_refcount;
			/* For both global and memcg */
			struct list_head deferred_list;
		};
        // 管理页表
		struct {	/* Page table pages */
			unsigned long _pt_pad_1;	/* compound_head */
			pgtable_t pmd_huge_pte; /* protected by page->ptl */
			unsigned long _pt_pad_2;	/* mapping */
			union {
				struct mm_struct *pt_mm; /* x86 pgds only */
				atomic_t pt_frag_refcount; /* powerpc */
			};
		struct {	/* ZONE_DEVICE pages */
			/** @pgmap: Points to the hosting device page map. */
			struct dev_pagemap *pgmap;
			void *zone_device_data;
		};

		/** @rcu_head: You can use this to free a page by RCU. */
		struct rcu_head rcu_head;
	};

	union {		/* This union is 4 bytes in size. */
		/*
		 * If the page can be mapped to userspace, encodes the number
		 * of times this page is referenced by a page table.
		 */
		atomic_t _mapcount; // 页面被进程映射的个数，即已经映射了多少个用户 PTE

		/*
		 * If the page is neither PageSlab nor mappable to userspace,
		 * the value stored here may help determine what this page
		 * is used for.  See page-flags.h for a list of page types
		 * which are currently stored here.
		 */
		unsigned int page_type;

		unsigned int active;		/* SLAB */ // slab 分配器中活跃对象的数量
		int units;			/* SLOB */
	};

	/* Usage count. *DO NOT USE DIRECTLY*. See page_ref.h */
	atomic_t _refcount; // 内核中引用该页面的次数，用于跟踪页面的使用情况

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
	void *virtual;			/* Kernel virtual address (NULL if not kmapped, ie. highmem) */
#endif /* WANT_PAGE_VIRTUAL */

#ifdef LAST_CPUPID_NOT_IN_PAGE_FLAGS
	int _last_cpupid;
#endif
} _struct_page_alignment;
```

所有的页描述符存放在 `mem_map` 数组中，用 `virt_to_page`  宏产生线性地址对应的 page 地址，用 `pfn_to_page` 宏产生页框号对应的 page 地址。

```c
#define virt_to_page(kaddr)	pfn_to_page(__pa(kaddr) >> PAGE_SHIFT) // _pa() 宏用来将虚拟地址转换为物理地址
#define pfn_to_page(pfn)	(mem_map + (pfn)) // 页框号不就是 page 的地址么。好吧，应该不是，所有的页描述符存放在 												  // mem_map 数组中，所以 mem_map 中存放的才是 page 地址
```

#### 内存管理区

由于 NUMA 模型的存在，kernel 将物理内存分为几个节点（node），每个节点有一个类型为 `pg_date_t` 的描述符。

##### pglist_data

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
	struct zonelist node_zonelists[GFP_ZONETYPES]; // 所有节点的 zone
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

  如果 Linux 物理内存小于 1G 的空间，通常将线性地址空间和物理空间一一映射，这样可以提供访问速度。

- ZONE_HIGHMEM：高于 896MB 的内存页框。

  高端内存的基本思想：借用一段虚拟地址空间，建立临时地址映射（页表），使用完之后就释放，以此达到这段地址空间可以循环使用，访问所有的物理内存。

##### zone

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

##### zonelist

```c
struct zonelist {
	struct zoneref _zonerefs[MAX_ZONES_PER_ZONELIST + 1];
};

struct zoneref {
	struct zone *zone;	/* Pointer to actual zone */
	int zone_idx;		/* zone_idx(zoneref->zone) */
};
```

内核使用 zone 来管理内存节点，上文介绍过，一个内存节点可能存在多个 zone，如 `ZONE_DMA,` `ZONE_NORMAL` 等。`zonelist` 是所有可用的 zone，其中排在第一个的是页面分配器最喜欢的。

我们假设系统中只有一个内存节点，有两个 zone，分别是 `ZONE_DMA` 和 `ZONE_NORMAL`，那么 `zonelist` 中的相关数据结构的关系如下：

![ZONELIST.png](https://github.com/UtopianFuture/UtopianFuture.github.io/blob/master/image/ZONELIST.png?raw=true)

#### 分区页框分配器

管理区分配器搜索能够满足分配请求的管理区，在每个管理区中，伙伴系统负责分配页框，每 CPU 页框高速缓存用来满足单个页框的分配请求。

![zone-menagement.png](https://github.com/UtopianFuture/UtopianFuture.github.io/blob/master/image/zone-menagement.png?raw=true)

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

##### 请求页框

页框的请求和释放是整个内存管理的核心，如 malloc 等函数都需要调用伙伴系统的这些接口来分配内存。

有多个函数和宏用来请求页框，这里只分析 `alloc_pages`，

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
   	page = get_page_from_freelist(alloc_gfp, order, alloc_flags, &ac); // 从空闲链表中分配内存，并返回第一个 																	   // page 的地址
   	if (likely(page))
   		goto out;

   	alloc_gfp = gfp;
   	ac.spread_dirty_pages = false;

   	/*
   	 * Restore the original nodemask if it was potentially replaced with
   	 * &cpuset_current_mems_allowed to optimize the fast-path attempt.
   	 */
   	ac.nodemask = nodemask;

   	page = __alloc_pages_slowpath(alloc_gfp, order, &ac); // 若分配不成功，则通过慢路径分配，这个之后再分析

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

  具体的定义在 gfp.h 中。而要正确使用这么多标志很难，所以定义了一些常用的表示组合——类型标志，如 `GFP_KERNEL`, `GFP_USER` 等，这里就不一一介绍，知道它是干什么的就行了，之后真正要用再查

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

   - `gfp_zone` 该函数用来确定使用哪个 zone 来分配内存，

     ```c
     static inline enum zone_type gfp_zone(gfp_t flags)
     {
     	enum zone_type z;
     	int bit = (__force int) (flags & GFP_ZONEMASK);

     	z = (GFP_ZONE_TABLE >> (bit * GFP_ZONES_SHIFT)) &
     					 ((1 << GFP_ZONES_SHIFT) - 1);
     	VM_BUG_ON((GFP_ZONE_BAD >> bit) & 1);
     	return z;
     }
     ```

     内存管理去修饰符使用 gfp_mask 的低 4 位来表示，

     ```c
     enum zone_type {
     #ifdef CONFIG_ZONE_DMA
     	ZONE_DMA, // 从 ZONE_DMA 中分配内存
     #endif
     #ifdef CONFIG_ZONE_DMA32
     	ZONE_DMA32,
     #endif

     	ZONE_NORMAL,
     #ifdef CONFIG_HIGHMEM
     	ZONE_HIGHMEM, // 优先从 ZONE_HIGHMEM 中分配内存
     #endif

     	ZONE_MOVABLE, // 页面可以被迁移或者回收，如用于内存规整机制
     #ifdef CONFIG_ZONE_DEVICE
     	ZONE_DEVICE,
     #endif
     	__MAX_NR_ZONES // 应该是 ZONE 数量
     };
     ```

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

   try_this_zone: // 显而易见，从当前 zone 分配内存（应该就是第一个 zone）
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

       // zone->vm_stat[item] 中存放了该 zone 的各种页面统计数据，包括空闲页面数量，不活跃的匿名页面数量等，
       // 该函数获取空闲页面的数量
   	free_pages = zone_page_state(z, NR_FREE_PAGES);

   	/*
   	 * Fast check for order-0 only. If this fails then the reserves
   	 * need to be calculated.
   	 */
   	if (!order) {
   		long fast_free;

   		fast_free = free_pages;
   		fast_free -= __zone_watermark_unusable_free(z, 0, alloc_flags);
           // lowmem_reserve 是每个 zone 预留的内存，防止高端 zone 在内存不足时过度使用低端内存，
           // 相当于一个缓冲区，如果使用到这块内存的话说明该 zone 内存不足，需要做回收操作
   		if (fast_free > mark + z->lowmem_reserve[highest_zoneidx])
   			return true;
   	}

       // 分配多个页的情况
   	if (__zone_watermark_ok(z, order, mark, highest_zoneidx, alloc_flags,
   					free_pages))
   		return true;

       ...

   	return false;
   }
   ```

5. `__zone_watermark_ok` 这个代码很好理解。

   ```c
   bool __zone_watermark_ok(struct zone *z, unsigned int order, unsigned long mark,
   			 int highest_zoneidx, unsigned int alloc_flags,
   			 long free_pages)
   {
   	long min = mark;
   	int o;
   	const bool alloc_harder = (alloc_flags & (ALLOC_HARDER|ALLOC_OOM));

   	/* free_pages may go negative - that's OK */
   	free_pages -= __zone_watermark_unusable_free(z, order, alloc_flags);

       // ALLOC_HIGH 表示什么情况？
   	if (alloc_flags & ALLOC_HIGH)
   		min -= min / 2;

   	...

   	/*
   	 * Check watermarks for an order-0 allocation request. If these
   	 * are not met, then a high-order request also cannot go ahead
   	 * even if a suitable page happened to be free.
   	 */
   	if (free_pages <= min + z->lowmem_reserve[highest_zoneidx])
   		return false;

   	/* If this is an order-0 request then the watermark is fine */
   	if (!order)
   		return true;

   	/* For a high-order request, check at least one suitable page is free */
   	for (o = order; o < MAX_ORDER; o++) {
   		struct free_area *area = &z->free_area[o];
   		int mt;

           // 没有空闲页
   		if (!area->nr_free)
   			continue;

           // 该 order 的块链表中有空闲页，可以分配内存，返回 true
   		for (mt = 0; mt < MIGRATE_PCPTYPES; mt++) {
   			if (!free_area_empty(area, mt))
   				return true;
   		}

           ...

           // alloc_harder 不知道是干啥的
           // MIGRATE_HIGHATOMIC 表示可迁移类型页面？
   		if (alloc_harder && !free_area_empty(area, MIGRATE_HIGHATOMIC))
   			return true;
   	}
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
               // 从 zone->free_area 中分配内存，从 order 到 MAX_ORDER - 1 遍历
               // zone->free_area 就是伙伴系统的 11 个块
   			page = __rmqueue_smallest(zone, order, MIGRATE_HIGHATOMIC);
   			if (page)
   				trace_mm_page_alloc_zone_locked(page, order, migratetype);
   		}
   		if (!page) // __rmqueue_smallest 分配失败
               // 从 MAX_ORDER - 1 到 order 遍历，其还是调用 __rmqueue_smallest
   			page = __rmqueue(zone, order, migratetype, alloc_flags);
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

6. `__rmqueue_smallest`

   ```c
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
   		page = get_page_from_free_area(area, migratetype); // 从该块中获取物理页面
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

这里我们总结一下伙伴系统通过快路径分配物理页面的流程。

首先物理页面都是存放在 zone 中的 `free_area` 中，伙伴系统将所有的空闲页框分组为 11 个块链表，每个块链表分别包含大小为 1、2、4、8、16、32、64、128、256、512 和 1024 个连续页框的页框块。内核可以使用多个接口来申请物理页面，这些接口最后都是调用 `__alloc_pages`。`__alloc_pages` 首先会进行分配前的准备工作，比如设置第一个 zone（大部分情况下从第一个 zone 中分配页面），设置分配掩码等等，而后调用 `get_page_from_freelist`。`get_page_from_freelist` 会遍历所有 zone，检查该 zone 的 watermark 是否满足要求，watermark 是伙伴系统用来提高分配效率的机制，每个 zone 都有 3 个 watermark，根据这些 watermark 在适当的时候做页面回收等工作。如果该 zone 的 watermark 满足要求，就调用 `rmqueue` 去分配物理页面。这里又根据需要分配页面的大小采用不同的分配策略。如果 `order == 0`，即只需要分配 1 个页面（内核大部分情况是这样的），那么直接调用 `rmqueue_pcplist` -> `__rmqueue_pcplist` 使用 `per_cpu_pages` 中的每 CPU 页框高速缓存来分配，这样就不需要使用 zone 的锁，提高效率。当然，如果每 CPU 页框高速缓存如果也没有物理页面了，那么还是先需要通过 `rmqueue_bulk` -> `__rmqueue` 来增加页面列表的。如果需要分配多个物理页面，那么就需要通过 `__rmqueue_smallest` 来分配页面。这个分配流程就很简单了，从 `free_area` 的第 `order`  个块链表开始遍历， 知道找到符合条件的块链表。

我觉得整个分配流程到不难，难的是要考虑到各种应用场景，理解如果设置分配掩码，理解如何根据 zone 的 watermark 去进行空间回收。不过这些现在还没有时间去学习，之后有需要再进一步分析。

##### 释放页框

同样有多个函数和宏能够释放页框，这里只分析 `__free_pages`。

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

   	local_lock_irqsave(&pagesets.lock, flags); // 释放过程中避免响应中断
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

slab 机制是基于对象进行管理的，所谓的对象就是内核中的数据结构（例如：`task_struct`, `file_struct` 等）。相同类型的对象归为一类，每当要申请这样一个对象时，就从对应的 slab 描述符的本地对象缓冲池、共享对象缓冲池或 slab 链表中分配一个这样大小的单元出去，而当要释放时，将其重新保存在该 slab 描述符中，而不是直接返回给伙伴系统，从而避免内部碎片。slab 机制并不丢弃已经分配的对象，而是释放并把它们保存在内存中。slab 分配对象时，会使用最近释放的对象的内存块，因此其驻留在 cpu 高速缓存中的概率会大大提高（soga）。

slab 分配器最终还是使用伙伴系统来分配实际的物理页面，只不过 slab 分配器在这些连续的物理页面上实现了自己的管理机制。

在开始分析 slab 的时候理解出现了偏差，即很多书中说的 slab 其实是一个广义的概念，泛指 slab 机制，其实现有 3 种：适用于小型嵌入式系统的 slob，slab 和适用于大型系统的 slub，我开始认为我们日常使用的内核用的都是 slab，但其实不是，目前大多数内核都是默认使用 slub 实现，slub 是 slab 的优化。那么这里还是继续分析 slab 的实现，因为设计原理都是一样的，然后再看看 slab 和 slub 的实现有何不同。

下面是 slab 系统涉及到的 slab 描述符、slab 节点、本地对象缓冲池、共享对象缓冲池、3 个 slab 链表、n 个 slab 分配器，以及众多 slab 缓存对象之间的关系。先对整个系统有大概的印象再去了解具体实现就比较容易。

![slab_structure.png](https://github.com/UtopianFuture/UtopianFuture.github.io/blob/master/image/slab_structure.png?raw=true)

简单总结一下各个结构体之间的关系。

首先我们需要了解 slab 描述符，类似于伙伴系统，内核在内存块中按照 2^order 字节（对象大小）来创建多个 slab 描述符，如 16 字节、32 字节、64 字节等，`kmem_cache` 保存了一些必要的信息。slab 描述符拥有众多的 slab 对象，这些对象按照一定的关系保存在本地对象缓冲池，共享对象缓冲池，slab 节点的 3 个链表中。让人感到迷惑的是这个 slab 链表和 slab 分配器有什么关系？其实每个 slab 分配器就是一个或多个物理页，这些物理页按照一个 slab 的大小可以存储多个 slab 对象，通过也有用于管理的管理区（freelist）和提高缓存效率的着色区（colour）。创建好的 slab 分配器会根据其中空闲对象的数量插入 3 个链表之一。

当本地对象缓冲池中空闲对象数量不够，则从共享对象缓冲池中迁移 batchcount 个空闲对象到本地对象缓冲池；当本地和共享对象缓冲池都没有空闲对象，那么从 slab 节点的 slabs_partial 或 slabs_free 链表中迁移空闲对象到本地对象缓冲池；如果以上 3 个地方都没有空闲对象，那么就需要创建新的 slab 分配器，即通过伙伴系统的接口请求物理内存，然后再将新建的 slab 分配器所在的 page 插入到链表中。

#### 创建slab描述符

`kmem_cache` 是 slab 分配器中的核心数据结构，我们将其称为 slab 描述符。

##### kmem_cache

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

##### kmem_cache_node

其主要包含 3 个链表（slab 描述符和 slab 节点之间的关系是怎样的）。

```c
struct kmem_cache_node {
  	spinlock_t list_lock;

  #ifdef CONFIG_SLAB
    // 这里需要理解，slab 分配器其实就是一个 page，上面也分析过，page 结构体中有专门支持 slab 机制的变量
    // 所谓满的、部分满的、空的 slab 分配器，其实就是该 page 中的空闲对象的数量
    // 新创建的 slab 分配器都需要插入这 3 个链表之一
    // 而这 3 个链表又属于某一个 slab 描述的 slab 节点
    // 所以说一个 slab 描述符有很多的 slab 对象
  	struct list_head slabs_partial;	/* partial list first, better asm code */ // 部分满的 slab 分配器
  	struct list_head slabs_full; // 满的 slab 分配器
  	struct list_head slabs_free; // 全空的 slab 分配器
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

1. `kmem_cache_create`

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
           // 遍历 slab_caches 查找是否有现成的 slab 描述符可以用，如果有则将找到的 slab 描述符 refcount++
   		s = __kmem_cache_alias(name, size, align, flags, ctor);
   	if (s)
   		goto out_unlock;

   	...

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

   这里有个问题，为什么在 `vmalloc_init` 中会调用该函数，

   ```c
   void __init vmalloc_init(void)
   {
   	struct vmap_area *va;
   	struct vm_struct *tmp;
   	int i;

   	/*
   	 * Create the cache for vmap_area objects.
   	 */
       // 这个变量很熟悉，但忘记是干啥的了，猜测应该是 slab 描述符信息需要
       // 保存在 vmalloc 区域，这里是初始化该区域
   	vmap_area_cachep = KMEM_CACHE(vmap_area, SLAB_PANIC);

   	...

   	/* Import existing vmlist entries. */
   	for (tmp = vmlist; tmp; tmp = tmp->next) {
   		va = kmem_cache_zalloc(vmap_area_cachep, GFP_NOWAIT);
   		if (WARN_ON_ONCE(!va))
   			continue;

   		va->va_start = (unsigned long)tmp->addr;
   		va->va_end = va->va_start + tmp->size;
   		va->vm = tmp;
   		insert_vmap_area(va, &vmap_area_root, &vmap_area_list);
   	}

   	/*
   	 * Now we can initialize a free vmap space.
   	 */
   	vmap_init_free_space();
   	vmap_initialized = true;
   }
   ```

   ```
   #0  kmem_cache_create_usercopy (name=name@entry=0xffffffff825bc870 "vmap_area", size=size@entry=64,
       align=align@entry=8, flags=flags@entry=262144, useroffset=useroffset@entry=0, usersize=usersize@entry=0,
       ctor=0x0 <fixed_percpu_data>) at mm/slab_common.c:315
   #1  0xffffffff8128bbd6 in kmem_cache_create (name=name@entry=0xffffffff825bc870 "vmap_area", size=size@entry=64,
       align=align@entry=8, flags=flags@entry=262144, ctor=ctor@entry=0x0 <fixed_percpu_data>)
       at mm/slab_common.c:422
   #2  0xffffffff831f500a in vmalloc_init () at mm/vmalloc.c:2336
   #3  0xffffffff831ba69c in mm_init () at init/main.c:854
   #4  start_kernel () at init/main.c:988
   #5  0xffffffff831b95a0 in x86_64_start_reservations (
       real_mode_data=real_mode_data@entry=0x2e3a920 <error: Cannot access memory at address 0x2e3a920>)
       at arch/x86/kernel/head64.c:525
   #6  0xffffffff831b962d in x86_64_start_kernel (
       real_mode_data=0x2e3a920 <error: Cannot access memory at address 0x2e3a920>) at arch/x86/kernel/head64.c:506
   #7  0xffffffff81000107 in secondary_startup_64 () at arch/x86/kernel/head_64.S:283
   ```

2. `__kmem_cache_create`

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
   	cachep->colour_off = cache_line_size(); // 着色区的大小为 l1 cache 的行大小
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

   	if (set_off_slab_cache(cachep, size, flags)) { // 这里对 3 种布局不做详细分析
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

#### slab分配器的内存布局

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

       // 找到合适的 gfporder，这就解决了第一个问题
   	for (gfporder = 0; gfporder <= KMALLOC_MAX_ORDER; gfporder++) { // 从 0 开始计算最合适（最小）的 gfporder
   		unsigned int num;
   		size_t remainder;

           // 2^gfporder / size，这就解决了第二个问题
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
       // 剩下的空间用于着色
       // cachep->colour = left / cachep->colour_off;
   	return left_over;
   }
   ```

   上面只是确定了 slab 分配器的内存布局，但是还没有确定内容。

#### 配置slab描述符

确定了 slab 分配器的内存布局后，调用 `setup_cpu_cache` 继续配置 slab 描述符。主要是配置如下两个变量（这两个变量有什么用呢？）：

- limit：空闲对象的最大数量，根据对象的大小计算。

  ```c
  if (cachep->size > 131072)
  		limit = 1;
  	else if (cachep->size > PAGE_SIZE)
  		limit = 8;
  	else if (cachep->size > 1024)
  		limit = 24;
  	else if (cachep->size > 256)
  		limit = 54;
  	else
  		limit = 120;
  ```

- batchcount：表示本地对象缓冲池和共享对象缓冲池之间填充对象的数量，通常是 limit 的一半。

  ```c
  batchcount = (limit + 1) / 2;
  ```

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

   	for_each_online_cpu(cpu) { // 当 slab 描述符之前有本地对象缓冲池时，遍历在线的 cpu，清空本地缓冲池（？）
   		LIST_HEAD(list);
   		int node;
   		struct kmem_cache_node *n;
   		struct array_cache *ac = per_cpu_ptr(prev, cpu);

   		node = cpu_to_mem(cpu); // numa 中的内存节点，uma 应该返回定值
   		n = get_node(cachep, node); // slab 节点，kmem_cache_node
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

2. 初始化本地对象缓冲池（没懂）

   ```c
   static struct array_cache __percpu *alloc_kmem_cache_cpus(
   		struct kmem_cache *cachep, int entries, int batchcount)
   {
   	int cpu;
   	size_t size;
   	struct array_cache __percpu *cpu_cache; // 对象缓冲池

   	size = sizeof(void *) * entries + sizeof(struct array_cache);
   	cpu_cache = __alloc_percpu(size, sizeof(void *)); // 分配，好复杂

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

	遍历系统中所有的内存节点以初始化和内存节点相关的 slab 信息。

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

		ret = init_cache_node(cachep, node, gfp);
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

	fail:
	kfree(old_shared);
		kfree(new_shared);
	free_alien_cache(new_alien);

	return ret;
	}
   ```

至此，slab 描述符完成创建。

#### 分配slab对象

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
   		goto alloc_done; // 前面有说过 batchcount 表示本地对象缓冲池和共享对象缓冲池之间填充对象的数量
   	}

   	while (batchcount > 0) {
   		/* Get slab alloc is to come from. */
           // 共享对象缓冲池中没有空闲对象，检查 slabs_partial 和 slabs_free 链表
   		page = get_first_slab(n, false);
   		if (!page)
   			goto must_grow;

   		check_spinlock_acquired(cachep);

           // 从 slab 分配器中迁移 batchcount 个空闲对象到本地对象缓冲池
   		batchcount = alloc_block(cachep, ac, page, batchcount);
           // 将该 slab 分配器的的 slab_list 加入到 slabs_partial 或 slabs_free 链表（？）
           // 不是本来就是从 slabs_partial 和 slabs_free 链表找到的 page 么？
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
         // 没找到这个成员啊！
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
     struct kmem_cache *slab_cache; // 指向这个 slab 分配器所属的 slab 描述符。page 在 3 个链表中
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

  着色区让每个 slab 分配器对应不同数量的高速缓存行，着色区的大小为 `colour_next * colour_off`，其中 `colour_next` 是从 0 到这个 slab 描述符中计算出来的 colour 最大值，colour_off 为 L1 高速缓存行大小。这样可以**使不同的 slab 分配器上同一个相对位置 slab 对象的起始地址在高速缓存中相互错开（？）**，有利于提高 cache 的访问效率。这篇文章解释的很[清楚](https://codeantenna.com/a/MTCbuWKjCr)。

#### 释放slab对象

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

#### slab分配器和伙伴系统的接口函数

slab 分配器创建 slab 对象时会调用伙伴系统的分配物理页面接口函数去分配 `2^cachep->gfporder` 个页面，调用的函数是 `kmem_getpages`。

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

#### 管理区freelist

上文提到管理区可以看作一个 freelist 数组，数组的每个成员大小为 1 字节，每个成员管理一个 slab 对象。这里我们看看 freelist 是怎样管理 slab 分配器中的对象的。

在 [分配 slab 对象](#分配slab对象)中介绍到 `cache_alloc_refill` 会判断本地/共享对象缓冲池中是否有空闲对象，如果没有的话就需要调用 `cache_grow_begin` 创建一个新的 slab 分配器。而在 `cache_init_objs` 中会把管理区中的 `freelist` 数组按顺序标号。

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

`kmalloc` 的核心实现就是 slab 机制。类似于伙伴系统，在内存块中按照 2^order 字节来创建多个 slab 描述符，如 16 字节、32 字节、64 字节等，同时系统会创建 `kmalloc-16`, `kmalloc-32`, `kmalloc-64` 等描述符，在系统启动时通过 `create_kmalloc_caches` 函数完成。例如要分配 30 字节的小块内存，可以用 `kmalloc(30, GFP_KERNEL)` 来实现，之后系统会从 `kmalloc-32 slab` 描述符中分配一个对象。

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

上面介绍了 `kmalloc` 使用 slab 分配器分配小块的、连续的物理内存，因为 slab 分配器在创建的时候也需要使用伙伴系统分配物理内存页面的接口，所以 **slab 分配器建立在一个物理地址连续的大块内存之上**（理解这点很重要）。那如果在内核中不需要连续的物理地址，而**仅仅需要虚拟地址连续的内存块**该如何处理？这就是 `vmalloc` 的工作。

vmalloc 映射区的**映射方式与用户空间完全相同**，内核可以通过调用 vmalloc 函数在内核地址空间的 vmalloc 区域获得内存。这个函数的功能相当于用户空间的 malloc 函数，所提供的虚拟地址空间是连续的， 但不保证物理地址是连续的。

还是先看看 vmalloc 相关的数据结构，内核中用 `vm_struct` 来表示一块 vmalloc 分配的区域。

```c
struct vm_struct {
	struct vm_struct	*next;
	void			*addr;
	unsigned long		size;
	unsigned long		flags;
	struct page		**pages;
#ifdef CONFIG_HAVE_ARCH_HUGE_VMALLOC
	unsigned int		page_order;
#endif
	unsigned int		nr_pages;
	phys_addr_t		phys_addr;
	const void		*caller;
};

// vm_struct 和 vmap_area 分别用来干嘛
// 从代码来看 vmap_area 就是表示一块 vmalloc 的起始和结束地址，以及所有块组成的链表和红黑树
struct vmap_area {
	unsigned long va_start;
	unsigned long va_end;

	struct rb_node rb_node;         /* address sorted rbtree */ // 传统做法
	struct list_head list;          /* address sorted list */

	/*
	 * The following two variables can be packed, because
	 * a vmap_area object can be either:
	 *    1) in "free" tree (root is vmap_area_root)
	 *    2) or "busy" tree (root is free_vmap_area_root)
	 */
	union {
		unsigned long subtree_max_size; /* in "free" tree */
		struct vm_struct *vm;           /* in "busy" tree */
	};
};
```

vmalloc 有不同的给外界提供了不同的接口，如 `__vmalloc`, `vmalloc` 等，但它们都是 `__vmalloc_node` 的封装，然后再调用`__vmalloc_node_range`。

```c
void *__vmalloc_node(unsigned long size, unsigned long align,
			    gfp_t gfp_mask, int node, const void *caller)
{
	return __vmalloc_node_range(size, align, VMALLOC_START, VMALLOC_END,
				gfp_mask, PAGE_KERNEL, 0, node, caller);
}
```

vmalloc 分配的空间在 [内存分布](# 内存分布) 小节中的图中有清晰的说明（不同架构的内存布局是不一样的，因为这篇文章的时间跨度较大，参考多本书籍，所以混合了 arm 内核、Loongarch 内核和 x86 内核的源码，这是个问题，之后要想想怎么解决。在 64 位 x86 内核中，该区域为 `0xffffc90000000000 ~ 0xffffe8ffffffffff`）。

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
   	area = __get_vm_area_node(real_size, align, shift, VM_ALLOC | // 分配一块连续的虚拟内存空间
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

这里有个疑问，为什么 vmalloc 最后申请空间也要调用到 slab 分配器？

```c
#0  slab_alloc_node (orig_size=64, addr=18446744071581699837, node=<optimized out>, gfpflags=3264,
    s=0xffff88810004f600) at mm/slub.c:3120
#1  kmem_cache_alloc_node (s=0xffff88810004f600, gfpflags=gfpflags@entry=3264, node=node@entry=-1)
    at mm/slub.c:3242
#2  0xffffffff812b8efd in alloc_vmap_area (size=size@entry=20480, align=align@entry=16384,
    vstart=vstart@entry=18446683600570023936, vend=vend@entry=18446718784942112767, node=node@entry=-1,
    gfp_mask=3264, gfp_mask@entry=3520) at mm/vmalloc.c:1531
#3  0xffffffff812b982a in __get_vm_area_node (size=20480, size@entry=16384, align=align@entry=16384,
    flags=flags@entry=34, start=18446683600570023936, end=18446718784942112767, node=node@entry=-1,
    gfp_mask=3520, caller=0xffffffff810a20ad <kernel_clone+157>, shift=12) at mm/vmalloc.c:2423
#4  0xffffffff812bc784 in __vmalloc_node_range (size=size@entry=16384, align=align@entry=16384,
    start=<optimized out>, end=<optimized out>, gfp_mask=gfp_mask@entry=3520, prot=..., vm_flags=0, node=-1,
    caller=0xffffffff810a20ad <kernel_clone+157>) at mm/vmalloc.c:3010
```

### 进程地址空间

还是先看看进程的地址空间布局

![process_address_space.png](https://github.com/UtopianFuture/UtopianFuture.github.io/blob/master/image/process_address_space.png?raw=true)

- 数据段和代码段就不再介绍，就是程序存储的位置。
- 用户进程栈。通常位于用户地址空间的最高地址，从上往下延伸。其包含栈帧，里面包含了局部变量和函数调用参数等。注意，不要和内核栈混淆，进程的内核栈独立存在并由内核维护，**主要用于上下文切换**。用户栈和内核栈的作用可以[看这里](https://github.com/UtopianFuture/UtopianFuture.github.io/blob/master/linux-note/others.md#%E7%94%A8%E6%88%B7%E6%A0%88)，其区别可以[看这里](https://github.com/UtopianFuture/UtopianFuture.github.io/blob/master/linux-note/others.md#%E7%94%A8%E6%88%B7%E6%A0%88%E5%92%8C%E5%86%85%E6%A0%B8%E6%A0%88%E7%9A%84%E5%8C%BA%E5%88%AB)。
- 堆映射区域。malloc 分配的进程虚拟地址就是这段区域。

每个进程都有一套页表，在进程切换是需要切换 cr3 寄存器，这样每个进程的地址空间就是隔离的。

#### mm_struct

`mm_struct` 是内核管理用户进程的内存区域和其页表映射的数据结构。进程控制块（PCB）中有指向 `mm_struct` 的指针 mm。

```c
struct mm_struct {
	struct {
		struct vm_area_struct *mmap; // 进程中所有的 VMA 形成单链表的表头
		struct rb_root mm_rb; // VMA 红黑树的根节点
		u64 vmacache_seqnum;                   /* per-thread vmacache */
#ifdef CONFIG_MMU
		unsigned long (*get_unmapped_area) (struct file *filp, // 用于判断虚拟地址空间是否有足够的空间
				unsigned long addr, unsigned long len, // 返回一段没有映射过的空间的起始地址
				unsigned long pgoff, unsigned long flags);
#endif
		unsigned long mmap_base;	/* base of mmap area */
		unsigned long mmap_legacy_base;	/* base of mmap area in bottom-up allocations */

		unsigned long task_size;	/* size of task vm space */
		unsigned long highest_vm_end;	/* highest vma end address */
		pgd_t * pgd; // 指向进程的一级页表（PGD）

		atomic_t mm_users; // The number of users(thread) including userspace
		atomic_t mm_count; // The number of references to &struct mm_struct

#ifdef CONFIG_MMU
		atomic_long_t pgtables_bytes;	/* PTE page table pages */
#endif
		int map_count;			/* number of VMAs */

		struct list_head mmlist; // 所有的 mm_task 都连接到一个双向链表中，该链表的表头是 init_mm 内存描述符
								 // 即 init 进程的地址空间

		unsigned long hiwater_rss; /* High-watermark of RSS usage */
		unsigned long hiwater_vm;  /* High-water virtual memory usage */

		unsigned long total_vm;	   /* Total pages mapped */
		unsigned long locked_vm;   /* Pages that have PG_mlocked set */
		atomic64_t    pinned_vm;   /* Refcount permanently increased */
		unsigned long data_vm;	   /* VM_WRITE & ~VM_SHARED & ~VM_STACK */
		unsigned long exec_vm;	   /* VM_EXEC & ~VM_WRITE & ~VM_STACK */
		unsigned long stack_vm;	   /* VM_STACK */
		unsigned long def_flags;

        // 代码段、数据段的起始、结束地址
		unsigned long start_code, end_code, start_data, end_data;
        // 堆空间的起始地址、当前堆中的 VMA 的结束地址（？）、栈空间的起始地址
        // 堆空间应该就是 malloc 可用的空间，随着内存的使用逐渐往上增长
		unsigned long start_brk, brk, start_stack;
		unsigned long arg_start, arg_end, env_start, env_end; //（？）

		unsigned long saved_auxv[AT_VECTOR_SIZE]; /* for /proc/PID/auxv */

#ifdef CONFIG_MEMCG
		struct task_struct __rcu *owner; // owner of this mm_struct
#endif
		struct user_namespace *user_ns;

        ...

	} __randomize_layout;

	/*
	 * The mm_cpumask needs to be at the end of mm_struct, because it
	 * is dynamically sized based on nr_cpu_ids.
	 */
	unsigned long cpu_bitmap[];
};
```

#### VMA

VMA 描述的是进程用 mmap，malloc 等函数分配的地址空间，或者说它描述的是进程地址空间的一个区间。所有的 `vm_area_struct` 构成一个单链表和一颗红黑树，通过 `mm_struct` 就可以找到所有的 `vm_area_struct`，再结合虚拟地址就可以找到对应的 `vm_area_struct`。

有个问题，VMA 如何和物理地址建立映射？

```c
struct vm_area_struct {
	/* The first cache line has the info for VMA tree walking. */ //（？）

	unsigned long vm_start;		/* Our start address within vm_mm. */
	unsigned long vm_end;		/* The first byte after our end address within vm_mm. */

	/* linked list of VM areas per task, sorted by address */
	struct vm_area_struct *vm_next, *vm_prev;

	struct rb_node vm_rb; // VMA 作为一个节点插入红黑树

	/*
	 * Largest free memory gap in bytes to the left of this VMA.
	 * Either between this VMA and vma->vm_prev, or between one of the
	 * VMAs below us in the VMA rbtree and its ->vm_prev. This helps
	 * get_unmapped_area find a free area of the right size.
	 */
	unsigned long rb_subtree_gap;

	/* Second cache line starts here. */ //（？）

	struct mm_struct *vm_mm;	/* The address space we belong to. */

	pgprot_t vm_page_prot; // Access permissions of this VMA
	unsigned long vm_flags;		/* Flags, see mm.h. */

	struct list_head anon_vma_chain; // 这两个变量用于管理反向映射（RMAP），后面有介绍
	struct anon_vma *anon_vma;

	/* Function pointers to deal with this struct. */
	const struct vm_operations_struct *vm_ops;

	/* Information about our backing store: */
	unsigned long vm_pgoff;		/* Offset (within vm_file) in PAGE_SIZE units */ // 指定文件映射的偏移量
	struct file * vm_file;		/* File we map to (can be NULL). */ // 指向一个 file 实例，描述一个被映射的文件
	void * vm_private_data;		/* was vm_pte (shared mem) */

#ifdef CONFIG_SWAP
	atomic_long_t swap_readahead_info;
#endif
#ifndef CONFIG_MMU
	struct vm_region *vm_region;	/* NOMMU mapping region */
#endif
#ifdef CONFIG_NUMA
	struct mempolicy *vm_policy;	/* NUMA policy for the VMA */
#endif
	struct vm_userfaultfd_ctx vm_userfaultfd_ctx;
} __randomize_layout;
```

将 `mm_struct` 和 `vm_area_struct` 结合起来看。

![mm_strct-VMA.png](https://github.com/UtopianFuture/UtopianFuture.github.io/blob/master/image/mm_strct-VMA.png?raw=true)

这里有个问题，X86 CPU 中不是有 cr3 寄存器指向一级页表么，为什么这里还要设置一个 pgd，难道 cr3 中的数据是从这里来的？而且图虽然清晰，但是很多细节还需要仔细分析，如 VMA 和物理页面建立映射关系等等。

#### VMA相关操作

有个问题，为什么要合并 VMA，合并了那么两个 VMA 的内容进程要怎样区分呢？它们应该是有不同的用途。

哦，原来不是所有的 VMA 都可以合并，带有 `VM_SPECIAL` 标志位的 VMA 就不能合并。`VM_SPECIAL` 主要是指包含（`VM_IO | VM_DONTEXPAND | VM_PFNMAP | VM_MIXEDMAP`）标志位的 VMA。

- 查找 VMA。调用 `find_vma` 通过虚拟地址查找对应的 VMA。`find_vma` 根据给定的 vaddr 查找满足如下条件之一的 VMA。
  - vaddr 在 VMA 空间范围内，即 vma -> vm_start <= vaddr <= vma -> vm_end；
  - 距离 vaddr 最近并且 VMA 的结束地址大于 vaddr 的 VMA。
- 插入 VMA。`insert_vm_struct` 向 VMA 链表和红黑树中插入一个新的 VMA。
- 合并 VMA。在新的 VMA 插入到进程的地址空间后，内核会检查它是否可以和现有的 VMA 合并。`vma_merge` 完成这个功能。合并中常见如下 3 中情况：
  - 新的 VMA 的起始地址和 prev 节点结束地址重叠；
  - 新的 VMA 的结束地址和 next 节点起始地址重叠；
  - 新的 VMA 和 prev, next 节点正好衔接上。

### malloc

malloc 函数是标准 C 库封装的一个核心函数，C 标准库最终会调用内核的 brk 系统调用。brk 系统调用展开后变成 `__do_sys_brk`。《奔跑吧内核》关于 brk 系统调用和 malloc 函数的解释很形象。

> 我们习惯使用的是 malloc 函数，而不太熟悉 brk 系统调用，这是因为很少直接使用 brk 系统调用。如果把 malloc 函数想象成零售商，那么 brk 就是代理商，malloc 为用户进程维护一个本地小仓库，当进程需要使用更多内存时，就向这个小仓库要货；当小仓库存量不足时，就通过代理商 brk 向内核批发。

1. brk 系统调用

   系统调用的定义是通过 `SYSCALL_DEFINEx` 宏来实现的，其中 `x` 表示参数个数，这个宏的定义如下：

   ```c
   #define SYSCALL_DEFINE1(name, ...) SYSCALL_DEFINEx(1, _##name, __VA_ARGS__)
   #define SYSCALL_DEFINE2(name, ...) SYSCALL_DEFINEx(2, _##name, __VA_ARGS__)
   #define SYSCALL_DEFINE3(name, ...) SYSCALL_DEFINEx(3, _##name, __VA_ARGS__)
   #define SYSCALL_DEFINE4(name, ...) SYSCALL_DEFINEx(4, _##name, __VA_ARGS__)
   #define SYSCALL_DEFINE5(name, ...) SYSCALL_DEFINEx(5, _##name, __VA_ARGS__)
   #define SYSCALL_DEFINE6(name, ...) SYSCALL_DEFINEx(6, _##name, __VA_ARGS__)

   #define SYSCALL_DEFINE_MAXARGS	6

   #define SYSCALL_DEFINEx(x, sname, ...)				\
   	SYSCALL_METADATA(sname, x, __VA_ARGS__)			\
   	__SYSCALL_DEFINEx(x, sname, __VA_ARGS__)
   ```

   所有的系统调用展开后的地址都会保存在系统调用表 sys_call_table 中。

   brk 系统调用最后展开成 `__do_sys_brk`，

   ```
   #0  __do_sys_brk (brk=0) at mm/mmap.c:207
   #1  __se_sys_brk (brk=0) at mm/mmap.c:194
   #2  __x64_sys_brk (regs=<optimized out>) at mm/mmap.c:194
   #3  0xffffffff81c0711b in do_syscall_x64 (nr=<optimized out>, regs=0xffffc90000dd7f58)
       at arch/x86/entry/common.c:50
   #4  do_syscall_64 (regs=0xffffc90000dd7f58, nr=<optimized out>) at arch/x86/entry/common.c:80
   #5  0xffffffff81e0007c in entry_SYSCALL_64 () at arch/x86/entry/entry_64.S:113
   ```

   ```c
   SYSCALL_DEFINE1(brk, unsigned long, brk)
   {
   	unsigned long newbrk, oldbrk, origbrk;
   	struct mm_struct *mm = current->mm;
   	struct vm_area_struct *next;
   	unsigned long min_brk;
   	bool populate;
   	bool downgraded = false;
   	LIST_HEAD(uf); // 内部临时用的链表

   	if (mmap_write_lock_killable(mm))
   		return -EINTR;

   	origbrk = mm->brk;

   	min_brk = mm->start_brk;

   	if (brk < min_brk) // 出错了
   		goto out;

   	newbrk = PAGE_ALIGN(brk); // 结合上面的图就很容易理解，新的 brk 上界
   	oldbrk = PAGE_ALIGN(mm->brk); // 原来的上界
   	if (oldbrk == newbrk) { // 新分配的上界和原来的上界一样，直接分配成功
   		mm->brk = brk;
   		goto success;
   	}

   	/*
   	 * Always allow shrinking brk.
   	 * __do_munmap() may downgrade mmap_lock to read.
   	 */
   	if (brk <= mm->brk) { // 这表示进程请求释放空间，调用 __do_munmap 释放这一部分空间
   		int ret;

   		/*
   		 * mm->brk must to be protected by write mmap_lock so update it
   		 * before downgrading mmap_lock. When __do_munmap() fails,
   		 * mm->brk will be restored from origbrk.
   		 */
   		mm->brk = brk;
   		ret = __do_munmap(mm, newbrk, oldbrk-newbrk, &uf, true);
   		if (ret < 0) {
   			mm->brk = origbrk;
   			goto out;
   		} else if (ret == 1) {
   			downgraded = true;
   		}
   		goto success;
   	}

   	/* Check against existing mmap mappings. */
   	next = find_vma(mm, oldbrk);
       // 以旧边界地址开始的地址空间已经在使用，不需要再寻找（？）
   	if (next && newbrk + PAGE_SIZE > vm_start_gap(next))
   		goto out;

   	/* Ok, looks good - let it rip. */
   	if (do_brk_flags(oldbrk, newbrk-oldbrk, 0, &uf) < 0) // 没有找到一块已经存在的 VMA，继续分配 VMA
   		goto out;
   	mm->brk = brk;

   success:
   	populate = newbrk > oldbrk && (mm->def_flags & VM_LOCKED) != 0; // 判断进程是否使用 mlockall 系统调用
   	if (downgraded)
   		mmap_read_unlock(mm);
   	else
   		mmap_write_unlock(mm);
   	userfaultfd_unmap_complete(mm, &uf);
   	if (populate) // 进程使用 mlockall 系统调用，
   		mm_populate(oldbrk, newbrk - oldbrk); // 需要马上分配物理内存
   	return brk;

   out:
   	mmap_write_unlock(mm);
   	return origbrk;
   }
   ```

2. `do_brk_flags`

   该函数会调用 `vm_area_alloc` 来创建一个新的 VMA。

   ```c
   static int do_brk_flags(unsigned long addr, unsigned long len, unsigned long flags, struct list_head *uf)
   {
   	struct mm_struct *mm = current->mm;
   	struct vm_area_struct *vma, *prev;
   	struct rb_node **rb_link, *rb_parent;
   	pgoff_t pgoff = addr >> PAGE_SHIFT;
   	int error;
   	unsigned long mapped_addr;

   	/* Until we need other flags, refuse anything except VM_EXEC. */
   	if ((flags & (~VM_EXEC)) != 0)
   		return -EINVAL;
   	flags |= VM_DATA_DEFAULT_FLAGS | VM_ACCOUNT | mm->def_flags;

   	mapped_addr = get_unmapped_area(NULL, addr, len, 0, MAP_FIXED); // 找到一段未使用的线性地址空间

       ...

   	/* Clear old maps, set up prev, rb_link, rb_parent, and uf */
   	if (munmap_vma_range(mm, addr, len, &prev, &rb_link, &rb_parent, uf))
   		return -ENOMEM;

   	/* Check against address space limits *after* clearing old maps... */
   	if (!may_expand_vm(mm, flags, len >> PAGE_SHIFT))
   		return -ENOMEM;

   	if (mm->map_count > sysctl_max_map_count)
   		return -ENOMEM;

   	if (security_vm_enough_memory_mm(mm, len >> PAGE_SHIFT))
   		return -ENOMEM;

   	/* Can we just expand an old private anonymous mapping? */
   	vma = vma_merge(mm, prev, addr, addr + len, flags, // 尝试合并 VMA
   			NULL, NULL, pgoff, NULL, NULL_VM_UFFD_CTX);
   	if (vma)
   		goto out;

   	/*
   	 * create a vma struct for an anonymous mapping
   	 */
   	vma = vm_area_alloc(mm); // 合并不成功，新建一个 VMA
   	if (!vma) {
   		vm_unacct_memory(len >> PAGE_SHIFT);
   		return -ENOMEM;
   	}

   	vma_set_anonymous(vma);
   	vma->vm_start = addr; // 初始化各种信息
   	vma->vm_end = addr + len;
   	vma->vm_pgoff = pgoff;
   	vma->vm_flags = flags;
   	vma->vm_page_prot = vm_get_page_prot(flags);
   	vma_link(mm, vma, prev, rb_link, rb_parent);
   out:
   	perf_event_mmap(vma); // 更新 mm_struct 信息
   	mm->total_vm += len >> PAGE_SHIFT;
   	mm->data_vm += len >> PAGE_SHIFT;
   	if (flags & VM_LOCKED)
   		mm->locked_vm += (len >> PAGE_SHIFT);
   	vma->vm_flags |= VM_SOFTDIRTY;
   	return 0;
   }
   ```

   这个有个疑问，`vm_area_alloc` 创建新的 VMA 为什么还会调用到 slab 分配器？

   ```
   #0  slab_alloc_node (orig_size=200, addr=18446744071579497582, node=-1, gfpflags=3264, s=0xffff8881001d5600)  at mm/slub.c:3120
   #1  slab_alloc (orig_size=200, addr=18446744071579497582, gfpflags=3264, s=0xffff8881001d5600) at mm/slub.c:3214
   #2  kmem_cache_alloc (s=0xffff8881001d5600, gfpflags=gfpflags@entry=3264) at mm/slub.c:3219
   #3  0xffffffff8109f46e in vm_area_alloc (mm=mm@entry=0xffff888100292640) at kernel/fork.c:349
   #4  0xffffffff812ab044 in do_brk_flags (addr=addr@entry=94026372730880, len=len@entry=135168,
       flags=<optimized out>, flags@entry=0, uf=uf@entry=0xffffc9000056bef0) at mm/mmap.c:3067
   #5  0xffffffff812ab6cc in __do_sys_brk (brk=94026372866048) at mm/mmap.c:271
   #6  __se_sys_brk (brk=94026372866048) at mm/mmap.c:194
   #7  __x64_sys_brk (regs=<optimized out>) at mm/mmap.c:194
   #8  0xffffffff81c0711b in do_syscall_x64 (nr=<optimized out>, regs=0xffffc9000056bf58)
       at arch/x86/entry/common.c:50
   #9  do_syscall_64 (regs=0xffffc9000056bf58, nr=<optimized out>) at arch/x86/entry/common.c:80
   #10 0xffffffff81e0007c in entry_SYSCALL_64 () at arch/x86/entry/entry_64.S:113
   ```

3. `mm_populate` 为该进程分配物理内存。通常用户进程很少使用 `VM_LOCKED` 分配掩码（果然很少用，设置断点都跑不到，那就分析代码看怎样建立映射吧），所以 brk 系统调用不会马上为这个进程分配物理内存，而是一直延迟到用户进程需要访问这些虚拟页面并发生缺页中断时才会分配物理内存，并和虚拟地址建立映射关系。

   ```c
   int __mm_populate(unsigned long start, unsigned long len, int ignore_errors)
   {
   	struct mm_struct *mm = current->mm;
   	unsigned long end, nstart, nend;
   	struct vm_area_struct *vma = NULL;
   	int locked = 0;
   	long ret = 0;

   	end = start + len;

   	for (nstart = start; nstart < end; nstart = nend) {
   		/*
   		 * We want to fault in pages for [nstart; end) address range.
   		 * Find first corresponding VMA.
   		 */
   		if (!locked) {
   			locked = 1;
   			mmap_read_lock(mm);
   			vma = find_vma(mm, nstart); // 找到对应的 VMA
   		} else if (nstart >= vma->vm_end)
   			vma = vma->vm_next;
   		if (!vma || vma->vm_start >= end)
   			break;
   		/*
   		 * Set [nstart; nend) to intersection of desired address
   		 * range with the first VMA. Also, skip undesirable VMA types.
   		 */
   		nend = min(end, vma->vm_end);
   		if (vma->vm_flags & (VM_IO | VM_PFNMAP))
   			continue;
   		if (nstart < vma->vm_start) // 重叠
   			nstart = vma->vm_start;
   		/*
   		 * Now fault in a range of pages. populate_vma_page_range()
   		 * double checks the vma flags, so that it won't mlock pages
   		 * if the vma was already munlocked.
   		 */
   		ret = populate_vma_page_range(vma, nstart, nend, &locked); // 人为的制造缺页并完成映射

           ...

   		nend = nstart + ret * PAGE_SIZE;
   		ret = 0;
   	}
   	if (locked)
   		mmap_read_unlock(mm);
   	return ret;	/* 0 or negative error code */
   }
   ```

4. `populate_vma_page_range` 调用 `__get_user_pages` 来分配物理内存并建立映射关系。

   `__get_user_pages` 主要用于**锁住内存**，即保证用户空间分配的内存不会被释放。很多驱动程序使用这个接口函数来为用户态程序分配物理内存。

   ```c
   static long __get_user_pages(struct mm_struct *mm,
   		unsigned long start, unsigned long nr_pages,
   		unsigned int gup_flags, struct page **pages,
   		struct vm_area_struct **vmas, int *locked)
   {
   	long ret = 0, i = 0;
   	struct vm_area_struct *vma = NULL;
   	struct follow_page_context ctx = { NULL };

   	...

   	do {
   		struct page *page;
   		unsigned int foll_flags = gup_flags;
   		unsigned int page_increm;

   		/* first iteration or cross vma bound */
   		if (!vma || start >= vma->vm_end) {
   			vma = find_extend_vma(mm, start); // 查找 VMA

   			...
   		}
   retry:
   		...

   		page = follow_page_mask(vma, start, foll_flags, &ctx); // 判断 VMA 中的虚页是否已经分配了物理内存
           													   // 其中涉及了页表遍历等操作，之后再分析吧
   		if (!page) { // 没有分配
   			ret = faultin_page(vma, start, &foll_flags, locked); // 人为的触发缺页异常，后面详细分析

               ...
   		}
           ...

   		if (pages) {
   			pages[i] = page;
   			flush_anon_page(vma, page, start); // 刷新这些页面对应的高速缓存
   			flush_dcache_page(page);
   			ctx.page_mask = 0;
   		}
   next_page:
   		if (vmas) {
   			vmas[i] = vma;
   			ctx.page_mask = 0;
   		}
   		page_increm = 1 + (~(start >> PAGE_SHIFT) & ctx.page_mask);
   		if (page_increm > nr_pages)
   			page_increm = nr_pages;
   		i += page_increm;
   		start += page_increm * PAGE_SIZE;
   		nr_pages -= page_increm;
   	} while (nr_pages);
   out:
   	if (ctx.pgmap)
   		put_dev_pagemap(ctx.pgmap);
   	return i ? i : ret;
   }
   ```

5. `follow_page_mask` 主要用于遍历页表并返回物理页面的 page 数据结构，这个应该比较复杂，但也是核心函数，之后再分析。这里有个问题，就是遍历页表不是由 MMU 做的，为什么这里还要用软件遍历？

简单总结一下 malloc 的操作流程。标准 C 库函数 malloc 最后使用的系统调用是 brk，传入的参数只有 brk 的结束地址，用这个地址和`mm -> brk` 比较，确定是释放内存还是分配内存。而需要分配内存的大小为 `newbrk-oldbrk`，这里 `newbrk` 就是传入的参数，`oldbrk` 是` mm -> brk`。brk 系统调用申请的内存空间貌似都是 `0x21000`。同时根据传入的 brk 在 VMA 的红黑树中寻找是否存在已经分配的内存块，如果有的话那么就不需要从新分配，否则就调用 `do_brk_flags` 分配新的 VMA，然后进行初始化，更新该进程的 `mm`。这样来看就是创建一个 VMA嘛，物理空间都是发生 #PF 才分配的。

### mmap

mmap 就是将文件映射到进程的物理地址空间，使得进程访问文件和正常的访问内存一样。

![mmap.png](https://github.com/UtopianFuture/UtopianFuture.github.io/blob/master/image/mmap.png?raw=true)

根据文件关联性和映射区域是否共享等属性，mmap 映射可以分为 4 类：

- 私有匿名映射。匿名映射即没有映射对应的相关文件，内存区域的内容会被初始化为 0。
- 共享匿名映射。
- 私有文件映射。
- 共享文件映射。这种映射有两种使用场景：
  - 读写文件；
  - 进程间通讯。进程之间的地址空间是相互隔离的，如果多个进程同时映射到一个文件，就实现了多进程间的共享内存通信。

mmap 机制在内核中的实现和 brk 类似，但其和缺页中断机制结合后会复杂很多。如内存漏洞 [Dirty COW](https://blog.csdn.net/hbhgyu/article/details/106245182) 就利用了 mmap 和缺页中断的相关漏洞。这里只记录 mmap 的执行流程，之后有需要再详细分析。

![mmap-implement.png](https://github.com/UtopianFuture/UtopianFuture.github.io/blob/master/image/mmap-implement.png?raw=true)

### 缺页异常处理

前面介绍到 malloc 和 mmap 都是只分配了虚拟地址，但是没有分配物理内存，也没有建立虚拟地址和物理地址之间的映射。当进程访问这些还没有建立映射关系的虚拟地址时，CPU 自动触发一个缺页异常。缺页异常的处理是内存管理的重要部分，需要考虑多种情况以及实现细节，包括匿名页面、KSM 页面、页面高速缓存、写时复制（COW）、私有映射和共享映射等等。这里先看看大概的执行流程，然后再详细分析每种情况的实现。

![page_fault](/home/guanshun/gitlab/UFuture.github.io/image/page_fault.png)

从触发缺页异常到 CPU 根据中断号跳转到对应的处理函数这个过程在之前做项目时已经跟踪过，就不在分析，这里主要分析 `handle_mm_fault` 相关的处理函数。

- `DEFINE_IDTENTRY_RAW_ERRORCODE(exc_page_fault)`
  - `unsigned long address = read_cr2();` // 在 X86 架构中，cr2 寄存器保存着访存时引发 #PF 异常的线性地址，cr3 寄存器提供整个也转化结构表的基地址。
  - `handle_page_fault`
    - `do_kern_addr_fault` 内核地址空间引发的中断；
    - `do_user_addr_fault` 用户地址空间引发的中断；

#### 关键函数do_user_addr_fault

该函数的功能是根据发生异常的 addr 找到对应的 VMA，然后判断 VMA 是否有问题，平时变成地址越界之类的错误应该都是在这里定义的。之后对异常处理的结果进行判断，抛出错误。

```c
static inline void do_user_addr_fault(struct pt_regs *regs,
			unsigned long error_code, unsigned long address)
{
	struct vm_area_struct *vma;
	struct task_struct *tsk;
	struct mm_struct *mm;
	vm_fault_t fault;
	unsigned int flags = FAULT_FLAG_DEFAULT;

	tsk = current;
	mm = tsk->mm;

    // 在获取 VMA 之前做了诸多检查，如异常发生时是否正在执行一些关键路径代码、当前进程是否为内核线程、
    // reserved bits 是否置位了、是否在用户态等等，这里为了减小篇幅代码就不放了，我们的关注点在缺页处理上
	...

	vma = find_vma(mm, address);
	if (unlikely(!vma)) { // 从上面的图上可以看到，bad_area 就会报错
		bad_area(regs, error_code, address); // 这个应该是访问没有分配的地址
		return;
	}
	if (likely(vma->vm_start <= address) // 找到出错地址所在的 VMA
		goto good_area;
	if (unlikely(!(vma->vm_flags & VM_GROWSDOWN))) { // 这个是啥问题？
		bad_area(regs, error_code, address); // VMA 属性问题
		return;
	}
	if (unlikely(expand_stack(vma, address))) { // 这些应该都是编程时常犯的错误，堆栈越界
		bad_area(regs, error_code, address);
		return;
	}

good_area:
	fault = handle_mm_fault(vma, address, flags, regs);

	if (fault_signal_pending(fault, regs)) { // 下面这些都是错误情况的处理
		/*
		 * Quick path to respond to signals.  The core mm code
		 * has unlocked the mm for us if we get here.
		 */
		if (!user_mode(regs))
			kernelmode_fixup_or_oops(regs, error_code, address,
						 SIGBUS, BUS_ADRERR,
						 ARCH_DEFAULT_PKEY);
		return;
	}

    ...

	mmap_read_unlock(mm);
	if (likely(!(fault & VM_FAULT_ERROR)))
		return;

	if (fatal_signal_pending(current) && !user_mode(regs)) {
		kernelmode_fixup_or_oops(regs, error_code, address,
					 0, 0, ARCH_DEFAULT_PKEY);
		return;
	}

	if (fault & VM_FAULT_OOM) {
		/* Kernel mode? Handle exceptions or die: */
		if (!user_mode(regs)) {
			kernelmode_fixup_or_oops(regs, error_code, address,
						 SIGSEGV, SEGV_MAPERR,
						 ARCH_DEFAULT_PKEY);
			return;
		}

		/*
		 * We ran out of memory, call the OOM killer, and return the
		 * userspace (which will retry the fault, or kill us if we got
		 * oom-killed):
		 */
		pagefault_out_of_memory();
	} else {
		if (fault & (VM_FAULT_SIGBUS|VM_FAULT_HWPOISON|
			     VM_FAULT_HWPOISON_LARGE))
			do_sigbus(regs, error_code, address, fault);
		else if (fault & VM_FAULT_SIGSEGV)
			bad_area_nosemaphore(regs, error_code, address);
		else
			BUG();
	}
}
```

#### 关键函数__handle_mm_fault

 `handle_mm_fault` 只是 `__handle_mm_fault` 的封装。这个函数的主要功能就是遍历页表。

```c
static vm_fault_t __handle_mm_fault(struct vm_area_struct *vma,
		unsigned long address, unsigned int flags)
{
	struct vm_fault vmf = { // vm_fault 是内核为缺页异常定义的数据结构，其常用于填充相应的参数
		.vma = vma,			// 并传递给进程地址空间的 fault() 回调函数
		.address = address & PAGE_MASK,
		.flags = flags,
		.pgoff = linear_page_index(vma, address), // 在 VMA 中的偏移量
		.gfp_mask = __get_fault_gfp_mask(vma),
	};
	unsigned int dirty = flags & FAULT_FLAG_WRITE;
	struct mm_struct *mm = vma->vm_mm;
	pgd_t *pgd; // 页全局目录，也就是第一级目录
	p4d_t *p4d; // 页 4 级目录，也就是第二级目录
	vm_fault_t ret;

	pgd = pgd_offset(mm, address); // 通过虚拟地址获取对应的页表项
	p4d = p4d_alloc(mm, pgd, address);
	if (!p4d)
		return VM_FAULT_OOM;

	vmf.pud = pud_alloc(mm, p4d, address); // 一级一级的找
	if (!vmf.pud) // 一级没找到就错误
		return VM_FAULT_OOM;
retry_pud:

    ...

	vmf.pmd = pmd_alloc(mm, vmf.pud, address);
	if (!vmf.pmd)
		return VM_FAULT_OOM;

	...
	}

	return handle_pte_fault(&vmf); // 最后处理 PTE，为什么要通过 PTE 才能确定后面的一系列操作（上图），
								   // 而其他的页表不能呢？
}
```

- 这里 pgd, p4d 等是 5 级页表的名称。五级分页每级命名分别为页全局目录(PGD)、页 4 级目录(P4D)、页上级目录(PUD)、页中间目录(PMD)、页表(PTE)。

#### 关键函数handle_pte_fault

这就就是处理各种 VMA，匿名映射，文件映射，VMA 的属性是否可读可写等等。

好多种情况啊！真滴复杂。

```c
static vm_fault_t handle_pte_fault(struct vm_fault *vmf)
{
	pte_t entry;

	if (unlikely(pmd_none(*vmf->pmd))) {
		vmf->pte = NULL;
	} else {

        ...

		vmf->pte = pte_offset_map(vmf->pmd, vmf->address);
		vmf->orig_pte = *vmf->pte;

		/*
		 * some architectures can have larger ptes than wordsize,
		 * e.g.ppc44x-defconfig has CONFIG_PTE_64BIT=y and
		 * CONFIG_32BIT=y, so READ_ONCE cannot guarantee atomic
		 * accesses.  The code below just needs a consistent view
		 * for the ifs and we later double check anyway with the
		 * ptl lock held. So here a barrier will do.
		 */
		barrier(); // 保证正确读取了 PTE 的内容
		if (pte_none(vmf->orig_pte)) { // ？没看懂
			pte_unmap(vmf->pte);
			vmf->pte = NULL;
		}
	}

	if (!vmf->pte) { // pte 为空
		if (vma_is_anonymous(vmf->vma)) // 判断是否为匿名映射
			return do_anonymous_page(vmf); // 后面分析
		else
			return do_fault(vmf); // 不是匿名映射，是文件映射。后面分析
	}

	if (!pte_present(vmf->orig_pte)) // present 为 0，说明页面不在内存中，即真正的缺页（？）
		return do_swap_page(vmf); // 请求从交换区读回页面

	if (pte_protnone(vmf->orig_pte) && vma_is_accessible(vmf->vma)) // 页面被设置为 NUMA 调度页面（？）
		return do_numa_page(vmf);

	vmf->ptl = pte_lockptr(vmf->vma->vm_mm, vmf->pmd);
	spin_lock(vmf->ptl);
	entry = vmf->orig_pte;
	if (unlikely(!pte_same(*vmf->pte, entry))) { // 判断这段时间内 PTE 是否修改了，若修改了，说明出现了异常情况
		update_mmu_tlb(vmf->vma, vmf->address, vmf->pte);
		goto unlock;
	}
	if (vmf->flags & FAULT_FLAG_WRITE) { // 因为写内存而触发缺页异常
		if (!pte_write(entry)) // PTE 是只读的
			return do_wp_page(vmf); // 处理写时复制的缺页中断
		entry = pte_mkdirty(entry); // PTE 是可写的，设置 _PAGE_DIRTY 和 _PAGE_SOFT_DIRTY 位
	}
	entry = pte_mkyoung(entry);
	if (ptep_set_access_flags(vmf->vma, vmf->address, vmf->pte, entry,
				vmf->flags & FAULT_FLAG_WRITE)) { // 判断 PTE 是否发生变化，如果发生变化就需要敬爱嗯新的内容
		update_mmu_cache(vmf->vma, vmf->address, vmf->pte); // 写入 PTE 中，并刷新对应的 TLB 和 cache
	} else {
		/* Skip spurious TLB flush for retried page fault */
		if (vmf->flags & FAULT_FLAG_TRIED)
			goto unlock;
		/*
		 * This is needed only for protection faults but the arch code
		 * is not yet telling us if this is a protection fault or not.
		 * This still avoids useless tlb flushes for .text page faults
		 * with threads.
		 */
		if (vmf->flags & FAULT_FLAG_WRITE)
			flush_tlb_fix_spurious_fault(vmf->vma, vmf->address);
	}
unlock:
	pte_unmap_unlock(vmf->pte, vmf->ptl);
	return 0;
}
```

#### 匿名页面缺页中断

`do_anonymous_page` 匿名页面缺页的处理。没有关联对应文件的映射称为匿名映射。

```c
static vm_fault_t do_anonymous_page(struct vm_fault *vmf)
{
	struct vm_area_struct *vma = vmf->vma;
	struct page *page;
	vm_fault_t ret = 0;
	pte_t entry;

	/* File mapping without ->vm_ops ? */
	if (vma->vm_flags & VM_SHARED) // 防止共享的 VMA 进入匿名页面的中断处理
		return VM_FAULT_SIGBUS;

	...

	/* Use the zero-page for reads */
	if (!(vmf->flags & FAULT_FLAG_WRITE) && // 该匿名页面只有只读属性
			!mm_forbids_zeropage(vma->vm_mm)) {
		entry = pte_mkspecial(pfn_pte(my_zero_pfn(vmf->address),
						vma->vm_page_prot)); // 将 PTE 设置为指向系统零页
		vmf->pte = pte_offset_map_lock(vma->vm_mm, vmf->pmd,
				vmf->address, &vmf->ptl);
		if (!pte_none(*vmf->pte)) {
			update_mmu_tlb(vma, vmf->address, vmf->pte);
			goto unlock;
		}

        ...

		goto setpte;
	}

	/* Allocate our own private page. */
    // 处理 VMA 属性为可写的情况
	if (unlikely(anon_vma_prepare(vma)))
		goto oom;
	page = alloc_zeroed_user_highpage_movable(vma, vmf->address); // 分配物理页面，会调用到 alloc_pages_vma
	if (!page) // page 就是分配的物理页面
		goto oom;

	...

	entry = mk_pte(page, vma->vm_page_prot); // 基于分配的物理页面设置新的 PTE
	entry = pte_sw_mkyoung(entry);
	if (vma->vm_flags & VM_WRITE)
		entry = pte_mkwrite(pte_mkdirty(entry));

	vmf->pte = pte_offset_map_lock(vma->vm_mm, vmf->pmd, vmf->address,
			&vmf->ptl); // 获取刚刚分配的 PTE
	if (!pte_none(*vmf->pte)) { // 失败了，出错了
		update_mmu_cache(vma, vmf->address, vmf->pte);
		goto release;
	}

	...

	inc_mm_counter_fast(vma->vm_mm, MM_ANONPAGES); // 增加进程匿名页面的数目
	page_add_new_anon_rmap(page, vma, vmf->address, false); // 添加到 RMAP 系统中（？）
	lru_cache_add_inactive_or_unevictable(page, vma); // 将匿名页面添加到 LRU 链表中
setpte:
	set_pte_at(vma->vm_mm, vmf->address, vmf->pte, entry); // 设置到硬件页表中。这就很好理解了，
    							   					// 正常的 page table walk 是硬件来做的。但是当发生异常，
    							  				    // 就需要软件遍历、构造、写入到硬件，之后硬件就可以正常使用了
	/* No need to invalidate - it was non-present before */
	update_mmu_cache(vma, vmf->address, vmf->pte);
unlock:
	pte_unmap_unlock(vmf->pte, vmf->ptl);
	return ret;
release:
	put_page(page);
	goto unlock;
oom_free_page:
	put_page(page);
oom:
	return VM_FAULT_OOM;
}
```

#### 文件映射缺页中断

内核用 `do_fault` 来处理文件映射的缺页中断。

```c
static vm_fault_t do_fault(struct vm_fault *vmf)
{
	struct vm_area_struct *vma = vmf->vma;
	struct mm_struct *vm_mm = vma->vm_mm;
	vm_fault_t ret;

	/*
	 * The VMA was not fully populated on mmap() or missing VM_DONTEXPAND
	 */
	if (!vma->vm_ops->fault) { // 有些内核模块或驱动的 mmap 函数没有实现 fault 回调函数

        // 脑子不够用，这种情况暂时不分析。
        ...

	} else if (!(vmf->flags & FAULT_FLAG_WRITE)) // 这次缺页是由读内存导致的
		ret = do_read_fault(vmf);
	else if (!(vma->vm_flags & VM_SHARED)) // 由写内存导致的并且 VMA 没有设置 VM_SHARED
		ret = do_cow_fault(vmf);
	else								 // 该 VMA 属于共享内存且是由写内存导致的异常
		ret = do_shared_fault(vmf);

	/* preallocated pagetable is unused: free it */
	if (vmf->prealloc_pte) {
		pte_free(vm_mm, vmf->prealloc_pte);
		vmf->prealloc_pte = NULL;
	}
	return ret;
}
```

1. `do_read_fault`

   ```c
   static vm_fault_t do_read_fault(struct vm_fault *vmf)
   {
   	struct vm_area_struct *vma = vmf->vma;
   	vm_fault_t ret = 0;

   	// map_pages 函数可以在缺页异常地址附近提前建立尽可能多的和页面高速缓存的映射，这样可以减少发生缺页异常的次数
       // 但其只是建立映射，而没有建立页面高速缓存（这个页面高速缓存时不是就是 cache），
       // 那这个做法是不是就是之前学过的根据空间局部性原理将发生异常的地址周围的数据也加载到 cache 中，
       // 减少缺页次数。应该就是这个意思。
       // fault_around_bytes 是全局变量，默认为 16 个页面
   	if (vma->vm_ops->map_pages && fault_around_bytes >> PAGE_SHIFT > 1) {
   		if (likely(!userfaultfd_minor(vmf->vma))) {
   			ret = do_fault_around(vmf); // 这个函数就是调用 map_pages 建立映射的
   			if (ret)
   				return ret;
   		}
   	}

   	ret = __do_fault(vmf); // 通过调用 vma->vm_ops->fault(vmf); 将文件内容读取到 page
   	if (unlikely(ret & (VM_FAULT_ERROR | VM_FAULT_NOPAGE | VM_FAULT_RETRY)))
   		return ret;

   	ret |= finish_fault(vmf); // 取回缺页异常对应的物理页面，调用 dodo_set_pte 建立物理页面和虚拟地址的映射关系
   	unlock_page(vmf->page);
   	if (unlikely(ret & (VM_FAULT_ERROR | VM_FAULT_NOPAGE | VM_FAULT_RETRY)))
   		put_page(vmf->page);
   	return ret;
   }
   ```

2. `do_cow_fault`

   ```c
   static vm_fault_t do_cow_fault(struct vm_fault *vmf)
   {
   	struct vm_area_struct *vma = vmf->vma;
   	vm_fault_t ret;

   	...

       // 调用 alloc_pages_vma 优先使用高端内存为 cow_page 分配一个新的物理页面
   	vmf->cow_page = alloc_page_vma(GFP_HIGHUSER_MOVABLE, vma, vmf->address);
   	if (!vmf->cow_page)
   		return VM_FAULT_OOM;

   	ret = __do_fault(vmf); // 这个和下面的 finish_fault 函数应该和上面的 do_read_fault 是一样的
   	if (unlikely(ret & (VM_FAULT_ERROR | VM_FAULT_NOPAGE | VM_FAULT_RETRY)))
   		goto uncharge_out;
   	if (ret & VM_FAULT_DONE_COW)
   		return ret;

   	ret |= finish_fault(vmf);
   	unlock_page(vmf->page);
   	put_page(vmf->page);

       ...

   	return ret;
   }
   ```

3. `do_shared_fault`

   ```c
   static vm_fault_t do_shared_fault(struct vm_fault *vmf)
   {
   	struct vm_area_struct *vma = vmf->vma;
   	vm_fault_t ret, tmp;

       // 不需要像上面那样分配新的物理页，因为这个共享页，内容已经存在了，只需要将其读取到 vmf->page 中即可
   	ret = __do_fault(vmf);
   	if (unlikely(ret & (VM_FAULT_ERROR | VM_FAULT_NOPAGE | VM_FAULT_RETRY)))
   		return ret;

   	/*
   	 * Check if the backing address space wants to know that the page is
   	 * about to become writable
   	 */
   	if (vma->vm_ops->page_mkwrite) { // 通知进程，页面将变成可写
   		unlock_page(vmf->page);
   		tmp = do_page_mkwrite(vmf);
   		if (unlikely(!tmp ||
   				(tmp & (VM_FAULT_ERROR | VM_FAULT_NOPAGE)))) {
   			put_page(vmf->page);
   			return tmp;
   		}
   	}

   	ret |= finish_fault(vmf);

       ...

   	ret |= fault_dirty_shared_page(vmf);
   	return ret;
   }
   ```

#### 写时复制（COW）

当进程试图修改只有只读属性的页面时，CPU 触发异常，在 `do_wp_page` 中尝试处理异常，通常的做法是分配一个新的页面并且复制据页面内容到新的页面中，这个新分配的页面具有可写属性。

`do_wp_page` 处理非常多且非常复杂的情况，如页面可以分成特殊页面、单身匿名页面、非单身匿名页面、KSM 页面以及文件映射页面等等，这里只了解 COW 的原理，具体实现先不做分析，之后有需要再分析。

### RMAP

因为目前的项目没有用到这个机制的地方，加上精力有限，故只了解其原理、主要数据结构和执行流程，具体的实现不做分析。

一个物理页面可以被多个进程的虚拟内存通过 PTE 映射。有的页面需要迁移，有的页面长时间不用需要交换到磁盘，在交换之前，必须找出哪些进程使用这个页面，然后解除这些映射的用户 PTE。

在 2.4 版本的内核中，为了确定某个页面是否被某个进程映射，必须遍历每个进程的页表，因此效率很低，在 2.5 版本的内核中，使用了反向映射（Reverse Mapping）。

RMAP 的主要目的是从物理页面的 page 数据结构中找到有哪些用户进程的 PTE，这样就可以快速解除所有的 PTE 并回收这个页面。

##### anon_vma

这个数据结构和 VMA 有相似之处啊。

```c
struct anon_vma {
	struct anon_vma *root;		/* Root of this anon_vma tree */
	struct rw_semaphore rwsem;	/* W: modification, R: walking the list */ // 互斥信号
	/*
	 * The refcount is taken on an anon_vma when there is no
	 * guarantee that the vma of page tables will exist for
	 * the duration of the operation. A caller that takes
	 * the reference is responsible for clearing up the
	 * anon_vma if they are the last user on release
	 */
	atomic_t refcount; // 引用计数

	/*
	 * Count of child anon_vmas and VMAs which points to this anon_vma.
	 *
	 * This counter is used for making decision about reusing anon_vma
	 * instead of forking new one. See comments in function anon_vma_clone.
	 */
	unsigned degree;

	struct anon_vma *parent;	/* Parent of this anon_vma */

	/*
	 * NOTE: the LSB of the rb_root.rb_node is set by
	 * mm_take_all_locks() _after_ taking the above lock. So the
	 * rb_root must only be read/written after taking the above lock
	 * to be sure to see a valid next pointer. The LSB bit itself
	 * is serialized by a system wide lock only visible to
	 * mm_take_all_locks() (mm_all_locks_mutex).
	 */

	/* Interval tree of private "related" vmas */
	struct rb_root_cached rb_root; // 所有的 va 构成一颗红黑树
};
```

##### anon_vma_chain

其可以连接父子进程的 `anon_vma` 数据结构。

```c
struct anon_vma_chain {
	struct vm_area_struct *vma; // 可以指向父进程的 VMA，也可以指向子进程的 VMA
	struct anon_vma *anon_vma; // 可以指向父进程的 anon_vma，也可以指向子进程的 anon_vma
	struct list_head same_vma;   /* locked by mmap_lock & page_table_lock */
	struct rb_node rb;			/* locked by anon_vma->rwsem */ // 红黑树
	unsigned long rb_subtree_last;
#ifdef CONFIG_DEBUG_VM_RB
	unsigned long cached_vma_start, cached_vma_last;
#endif
};
```



### 补充知识点

#### MMU

MMU是 CPU 中的一个硬件单元，通常每个核有一个 MMU。MMU由两部分组成：TLB(Translation Lookaside Buffer)和 table walk unit。

TLB 很熟悉了，就不再分析。主要介绍一下 table walk unit。

首先对于硬件 page table walk 的理解是有些问题的，即内核中有维护一套进程页表，为什么还要硬件来做呢？两者有何区别？第一个问题很好理解，效率嘛，第二个问题就是我们要分析的。

如果发生 TLB miss，就需要查找当前进程的 page table，接下来就是 table walk unit 的工作。而使用 table walk unit硬件单元来查找page table的方式被称为hardware TLB miss handling，通常被CISC架构的处理器（比如IA-32）所采用。如果在page table中查找不到，出现page fault，那么就会交由软件（操作系统）处理，之后就是我们熟悉的 PF。

好吧，从找到的资料看 page table walk 和我理解的内核的 4/5 级页表转换没有什么不同。但是依旧有一个问题，即 mmu 访问的这些 page table 是不是就是内核访问的 page table，都是存放在内存中的。

### 疑问

1. `vm_area_alloc` 创建新的 VMA 为什么还会调用到 slab 分配器？
2. vmalloc 中的`vm_struct` 和 `vmap_area` 分别用来干嘛？
3. 为什么 vmalloc 最后申请空间也要调用到 slab 分配器？（这样看来 `slab_alloc_node` 才是真核心啊）？

### Reference

[1] 奔跑吧 Linux 内核，卷 1：基础架构

[2] 内核版本：5.15-rc5，commitid: f6274b06e326d8471cdfb52595f989a90f5e888f

[3] https://zhuanlan.zhihu.com/p/65298260

[4] https://biscuitos.github.io/blog/MMU-Linux4x-VMALLOC/

### 些许感想

内存管理的学习是循序渐进的，开始可能只能将书中的内容稍加精简记录下来，随着看的内容越多，难免对前面的内容产生疑惑，这时再回过头去理解，同时补充自己的笔记。这样多来几次也就理解、记住了。所以最开始“抄”的过程对于我来说无法跳过。
