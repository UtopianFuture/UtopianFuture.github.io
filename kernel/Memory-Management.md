## Memory Management

### 编程思想

写代码不能拿到代码就开始跑，开始调，这不是正确的写代码的方式，会让你陷入到很 trivial 的细节当中。当拿到一份代码，或者写一段代码，你要完全理解这段代码，对于你认为可能会出错的地方，加上很多措施去加固它，这样才能增强你对代码的洞察力。当代码跑出问题了，这时候不应该立刻动手调试，gdb 打断点谁不会，而是坐下来想，哪怕想一天，让代码在你脑子里跑，然后你意识到问题在哪里。是的，这样做开始很慢，但之后出的问题都在掌握之中。而我往往等不了，心急，跑起来再说，最后效率并不高。

### 目录

[TOC]

很多文章都说内存管理是内核中最复杂的部分、最重要的部分之一，在来实验室之后跟着师兄做项目、看代码的这段时间里，渐渐感觉自己的知识框架是不完整的，底下少了一部分，后来发现这部分就是内核，所以开始学习内核。其实这也不是第一次接触内核，之前也陆陆续续的看过一部分，包括做 RISC-V 操作系统实验，LoongArch 内核的启动部分，但始终没有花时间去啃内存管理，进程调度和文件管理这几个核心模块，而师兄也说过，内核都看的懂，啥代码你看不懂，所以分析一下内存管理模块。

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

##### page

内核中以页框（大多数情况为 4K）为最小单位分配内存（但是在实际开发过程中也遇到过很多大页，如 4M，1G 等，那这些页要怎样管理？）。page  作为页描述符记录每个页框当前的状态。

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
            // 另一个用于匿名映射
			struct address_space *mapping;
			pgoff_t index;		/* Our offset within mapping. */ // 为什么需要有这个？文件偏移量么？
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
		atomic_t _mapcount; // 页面被进程映射的个数，即已经映射了多少个用户 PTE，在 RMAP 时维护

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

所有的页描述符（page 数据结构）存放在 `mem_map` 数组中，用 `virt_to_page`  宏产生线性地址对应的 page 地址，用 `pfn_to_page` 宏产生页框号对应的 page 地址。

```c
#define virt_to_page(kaddr)	pfn_to_page(__pa(kaddr) >> PAGE_SHIFT) // _pa() 宏用来将虚拟地址转换为物理地址
#define pfn_to_page(pfn)	(mem_map + (pfn)) // 页框号不就是 page 的地址么。好吧，应该不是，所有的页描述符存放在 												  // mem_map 数组中，所以 mem_map 中存放的才是 page 地址
```

这里有一点需要注意，page 和 page frame，一个是描述内核地址空间的，一个是描述物理内存的，

- page 线性地址被分成以固定长度为单位的组，称为页，比如典型的 4K 大小，页内部连续的线性地址被映射到连续的物理地址中；
- page frame 内存被分成固定长度的存储区域，称为页框，也叫**物理页**。每一个页框会包含一个页，页框的长度和一个页的长度是一致的，在内核中使用 `struct page` 来关联物理页。

看这个图就明白了，

![img](https://raw.githubusercontent.com/UtopianFuture/UtopianFuture.github.io/81305767754ca8df95d18da10465dd730bc093be/image/mem_section.svg)

总结一下，首先客观存在的是物理内存，4G/16G 等等，为了管理这些物理内存，内核使用 page 这个结构体，这个结构体里面记录了某块 4K 的是用情况。但 page 毕竟只是一个数据结构，怎样和物理地址联系起来，不能在里面增加一个地址指针吧，那太 low 了！内核的做法是在虚拟地址空间抠出一块来，vmemmap 空间，因为是 64bits 地址，肯定够用。这块空间一般来说是 2048GB，很难有机器有这么大的 DDR 吧，那就是用 vmemmap+n 表示第 n 个物理页，n 是 4KB 粒度，**这个 n 也就是物理地址**。然后 vmemmap+n 就是指向内核中用来管理这 4KB 物理内存的 page 数据结构地址。

这就是所谓的线性映射。内核中都是是用 page 来操作物理内存，这样内核访问物理内存直接通过 page 的指针加 offset 就行，十分高效。现在能够理解 `virt_to_page` 和 ` pfn_to_page` 在干嘛了吧！其他接口 `phys_to_virt` 等都是类似的。

这里对 _refcount 和 _mapcount 做进一步的解释：

- `_refcount`：用于跟踪页面的引用计数，表示内核中有多少个指针指向该页面；
 - alloc_pages 分配成功会 +1；
 - page 添加到 lru 链表时会 +1；
 - 加入到 address_space 时（？）；
 - page 被映射到其他用户进程 PTE 时，会 +1；
 - 对于 PG_swapable 页面，_add_to_swap_cache 会 +1；
 - 还有[其他](https://blog.csdn.net/GetNextWindow/article/details/131905827)内存路径也会 +1;
- `_mapcount`：用于跟踪页面在页表中的映射计数，表示有多少个页表项映射到该页面；
 - 该变量在建立 rmap 映射时 +1，在解除 rmap 映射时 -1；

#### 内存管理区

由于 NUMA 模型的存在，kernel 将物理内存分为几个节点（node），每个节点有一个类型为 `pg_date_t` 的描述符。

##### pglist_data

在 `bootmem_init` 中会初始化 node_zones 变量，而 node_zonelists 变量要等到 `mm_core_init->build_all_zonelists` 才会赋值。

```c
/*
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

pg_data_t 在 `bootmem_init` 时就会完成初始化。

**由于实际的计算机体系结构的限制**（ISA 总线只能寻址 16 MB 和 32 位计算机只能寻址 4G），每个节点的物理内存又分为 3 个管理区。

- ZONE_DMA：低于 16MB 的内存页框；

- ZONE_DMA32：在 64 位机器上为低 4G 地址范围；

- ZONE_NORMAL：高于 16MB 低于 896MB 的内存页框，

  如果 Linux 物理内存小于 1G 的空间，通常将线性地址空间和物理空间一一映射，这样可以提供访问速度；

- ZONE_HIGHMEM：高于 896MB 的内存页框。

  高端内存的基本思想：借用一段虚拟地址空间，建立临时地址映射（页表），使用完之后就释放，以此达到这段地址空间可以循环使用，访问所有的物理内存。

我们进一步分析为什么要分 zone 区域，内核必须处理 80x86 体系结构的两种硬件约束，

- ISA 总线的直接内存存储 DMA 处理器有一个严格的限制 : 他们只能对 RAM 的前 16MB 进行寻址；
- 在具有大容量 RAM 的现代 32 位计算机中，**CPU 不能直接访问所有的物理地址**，因为线性地址空间太小，内核不可能直接映射所有物理内存到线性地址空间，所以需要 ZONE_HIGH 映射物理地址到内核地址空间；

因此内核对不同区域的内存需要采用不同的管理方式和映射方式，即分成不同的 zone。

##### zone

zone 为管理区描述符，在 `bootmem_init` 中初始化。

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

##### free_area

```c
struct free_area {
    // 伙伴系统的每个块都分为多个链表，每个链表存储一个类型的内存块，
    // 如不可移动类型、可回收类型、可移动类型等
    // 这样做是为了页面迁移方便，属于反碎片化的优化。后面有分析
	struct list_head	free_list[MIGRATE_TYPES];
	unsigned long		nr_free;
};
```

内核使用 zone 来管理内存节点，上文介绍过，一个内存节点可能存在多个 zone，如 `ZONE_DMA,` `ZONE_NORMAL` 等。`zonelist` 是所有可用的 zone，其中**排在第一个的是页面分配器最喜欢的**。

我们假设系统中只有一个内存节点，有两个 zone，分别是 `ZONE_DMA` 和 `ZONE_NORMAL`，那么 `zonelist` 中的相关数据结构的关系如下：

![ZONELIST.png](https://github.com/UtopianFuture/UtopianFuture.github.io/blob/master/image/ZONELIST.png?raw=true)

#### 分区页框分配器

管理区分配器搜索能够满足分配请求的 zone，在每个 zone 中，伙伴系统负责分配页框，每 CPU 页框高速缓存用来满足**单个页框**的分配请求。

![zone-menagement.png](https://github.com/UtopianFuture/UtopianFuture.github.io/blob/master/image/zone-menagement.png?raw=true)

从图中可以看出，每个 ZONE 都有伙伴系统结构(free_area)。

#### 管理区分配器

任务：分配一个包含足够空闲页框的 zone 来满足内存请求。我们先了解一下伙伴系统的原理。

##### 伙伴系统算法

即为了解决外碎片的问题，尽量保证有连续的空闲页框块。

伙伴系统的描述网上有很多，这里不啰嗦，我们直接看代码。伙伴系统分配页框和下文的 `alloc_pages` 分配页框有什么不同（没有不同，一个东西）？

> 把所有的空闲页框分组为 11 个块链表，每个块链表分别包含大小为 1、2、4、8、16、32、64、128、256、512 和 1024 个连续页框的页框块。最大可以申请 1024 个连续页框，也即 4MB 大小的连续空间。
>
> 假设要申请一个 256 个页框的块，先从 256 个页框的链表中查找空闲块，如果没有，就去 512 个页框的链表中找，找到了即将页框分为两个 256 个页框的块，一个分配给应用，另外一个移到 256 个页框的链表中。如果 512 个页框的链表中仍没有空闲块，继续向 1024 个页框的链表查找，如果仍然没有，则返回错误。
>
> 页框块在释放时，会主动将**两个连续的页框块合并成一个较大的页框块**。
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
    // 表示页面分配的行为和属性，这里初始化为 ALLOC_WMARK_LOW
    // 表示允许分配内存的判断条件为 WMARK_LOW
    // 只有内存水位低于 LOW 的时候，才会进入到慢速路径
   	unsigned int alloc_flags = ALLOC_WMARK_LOW;
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
   EXPORT_SYMBOL(__alloc_pages); // 该函数可被驱动使用
   ```

这其中涉及一些概念需要理解清楚。

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

  具体的定义在 gfp.h 中。而要正确使用这么多标志很难，所以定义了一些常用的表示组合——类型标志，如 `GFP_KERNEL`, `GFP_USER` 等，这里就不一一介绍，知道它是干什么的就行了，之后真正要用再查。下面给出一下常用的标志的含义：

  - `GFP_KERNEL`，其是最常见的内存分配掩码之一，主要用于分配内核使用的的内存，需要注意的是**分配过程中会引起睡眠**，这在中断上下文以及不能睡眠的内核路径里调用该分配掩码需要特别警惕，因为会引起死锁或者其他系统异常；
  - `GFP_ATOMIC`，这个标志位正好和 `GFP_KERNEL` 相反，它**可以使用在不能睡眠的内存分配路径上**，比如中断处理程序、软中断以及 tasklet 等。`GFP_KERNEL` 可以**让调用者睡眠等待系统页面回收来释放一些内存**，但是 `GFP_ATOMIC` 不可以，所以**有可能会分配失败**；
  - `GFP_USER`、`GFP_HIGHUSER` 和 `GFP_HIGHUSER_MOVEABLE`，**这三个标志位都是为用户空间进程分配内存的**。不同之处在于，`GFP_HIGHUSER` 首先使用高端内存，`GFP_HIGHUSER_MOVEABLE` 首先使用高端内存并且分配的内存具有可迁移性；
  - `GFP_NOIN`、`GFP_NOFS`，这两个标志位都会产生阻塞，它们用来避免某些其他的操作。
    `GFP_NOIO` 表示分配过程中绝不会启动任何磁盘 I/O 的操作。
    `GFP_NOFS` 表示分配过程中绝不会启动文件系统的相关操作。
    举个例子，假设进程 A 在执行打开文件的操作中需要分配内存，这时内存短缺了，那么进程 A 会睡眠等待，系统的 OOM Killer 机制会选择一个进程杀掉。假设选择了进程 B，而进程 B 退出时需要执行一些文件系统的操作，这些操作可能会去申请锁，而恰巧进程 A 持有这个锁，那么就是发生死锁。

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

     内存管理区修饰符使用 `gfp_mask` 的低 4 位来表示，

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

        	// 计算 zone 中某个水位的页面大小（？）
        	// 计算当前 zone 的空闲页面是否满足 WMARK_LOW
        	// 同时根据 order 计算是否有足够大的空闲内存块
        	// 对内存管理进一步分析之后，水位描述符是为了防止内存碎片化
   		mark = wmark_pages(zone, alloc_flags & ALLOC_WMARK_MASK);
   		if (!zone_watermark_fast(zone, order, mark,
   				       ac->highest_zoneidx, alloc_flags,
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

- 首选的 zone 是通过 `gfp_mask` 换算的，具体的换算过程是 `gfp_zone` 和 `first_zones_zonelist` 宏;
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
       // vm_stat 在其他关键数据结构中也有，是为了更好的了解内存使用情况，后面会分析
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
       // 同样是为了检查该页块的其他页面类型的链表是否有足够的内存满足分配
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
           // MIGRATE_PCPTYPES 是一种页面类型
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

6. `rmqueue`

   `rmqueue` 函数会从伙伴系统中获取内存。若没有需要大小的内存块，那么**从更大的内存块中“切”内存**。如程序要需要 `order=4` 的内存块，但是伙伴系统中 `order=4` 的内存块已经分配完了，那么从 `order=5` 的内存块中“切”一块 `order=4` 的内存块分配给该程序，同时将剩下的部分添加到 `order=4` 的空闲链表中。

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
       // ZONE_BOOSTED_WATERMARK 表示发生了碎片化，临时提高水位以提前触发 kswapd 内核线程进行内存回收
   	if (test_bit(ZONE_BOOSTED_WATERMARK, &zone->flags)) {
   		clear_bit(ZONE_BOOSTED_WATERMARK, &zone->flags);
   		wakeup_kswapd(zone, 0, 0, zone_idx(zone)); // 判断是否需要唤醒 kswapd 内核线程回收内存
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

  `per_cpu_pageset` 是一个 Per-CPU 变量，即每个 CPU 中都有一个本地的 `per_cpu_pageset` 变量，里面**暂存了一部分单个的物理页面**。当系统需要单个物理页面时，直接从本地 CPU 的 `per_cpu_pageset` 中获取物理页面即可，这样能减少对 zone 中相关锁的操作。

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

8. `__rmqueue`

   这个函数同样是调用 `__rmqueue_smallest`，但是如果 `__rmqueue_smallest` 分配失败，其会调用 `__rmqueue_fallback`。这个函数会**从其他类型的页块中挪用页块**，这是基于页面迁移类型做的优化，这里就不做进一步分析。

   ```c
   static __always_inline struct page *
   __rmqueue(struct zone *zone, unsigned int order, int migratetype,
   						unsigned int alloc_flags)
   {
   	struct page *page;

   	...

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

这里我们总结一下伙伴系统通过快路径分配物理页面的流程。

首先物理页面都是存放在 zone 中的 `free_area` 中，伙伴系统将所有的空闲页框分组为 11 个块链表，每个块链表分别包含大小为 1、2、4、8、16、32、64、128、256、512 和 1024 个连续页框的页框块。内核可以使用多个接口来申请物理页面，这些接口最后都是调用 `__alloc_pages`。`__alloc_pages` 首先会进行分配前的准备工作，比如设置第一个 zone（大部分情况下从第一个 zone 中分配页面），设置分配掩码等等，而后调用 `get_page_from_freelist`。`get_page_from_freelist` 会遍历所有 zone，检查该 zone 的 watermark 是否满足要求，**watermark 是伙伴系统用来提高分配效率的机制**，每个 zone 都有 3 个 watermark，根据这些 watermark 在适当的时候做页面回收等工作。如果该 zone 的 watermark 满足要求，就调用 `rmqueue` 去分配物理页面。这里又根据需要分配页面的大小采用不同的分配策略。如果 `order == 0`，即只需要分配 1 个页面（内核大部分情况是这样的），那么直接调用 `rmqueue_pcplist` -> `__rmqueue_pcplist` 使用 `per_cpu_pages` 中的每 CPU 页框高速缓存来分配，这样就不需要使用 zone 的锁，提高效率。当然，如果每 CPU 页框高速缓存如果也没有物理页面了，那么还是先需要通过 `rmqueue_bulk` -> `__rmqueue` 来增加页面列表的。如果需要分配多个物理页面，那么就需要通过 `__rmqueue_smallest` 来分配页面。这个分配流程就很简单了，从 `free_area` 的第 `order`  个块链表开始遍历， 知道找到符合条件的块链表。

我觉得整个分配流程到不难，**难的是要考虑到各种应用场景，理解如何设置分配掩码**，理解如何根据 zone 的 watermark 去进行空间回收。不过这些现在还没有时间去学习，之后有需要再进一步分析。

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

伙伴系统算法采用页框作为基本内存区，但一个页框一般是 4KB，而程序很多时候都是申请很小的内存，比如几百字节，十几 KB，这时分配一个页会造成很大的浪费，**slab 分配器解决的就是对小内存区的请求**。

slab 机制是基于对象进行管理的，**所谓的对象就是内核中的数据结构（例如：`task_struct`, `file_struct` 等）**（这个理解是不全面的，对象应该是分配的内存块，只是在内核中更多的是分配各种数据数据结构）。相同类型的对象归为一类，每当要申请这样一个对象时，就从对应的 slab 描述符的本地对象缓冲池、共享对象缓冲池或 slab 链表中分配一个这样大小的单元出去，而当要释放时，**将其重新保存在该 slab 描述符中，而不是直接返回给伙伴系统**，从而避免内部碎片。slab 机制并不丢弃已经分配的对象，而是释放并把它们保存在内存中（只有在内存不足时才会回收 slab 对象，通过 slab shrinker 机制）。slab 分配对象时，会使用最近释放的对象的内存块，**因此其驻留在 cpu 高速缓存中的概率会大大提高**（soga）。同时 slab 机制为每个 CPU 都设计了本地对象缓冲池，避免了多核之间的锁争用的问题。

slab 分配器最终还是使用伙伴系统来分配实际的物理页面，只不过 slab 分配器**在这些连续的物理页面上实现了自己的管理机制**。

在开始分析 slab 的时候理解出现了偏差，即很多书中说的 slab 其实是一个广义的概念，泛指 slab 机制，其实现有 3 种：适用于小型嵌入式系统的 slob，slab 和适用于大型系统的 slub，我开始认为我们日常使用的内核用的都是 slab，但其实不是，**目前大多数内核都是默认使用 slub 实现，slub 是 slab 的优化**。那么这里还是继续分析 slab 的实现，因为设计原理都是一样的，然后再看看 slab 和 slub 的实现有何不同。

下面是 slab 系统涉及到的 slab 描述符、slab 节点、本地对象缓冲池、共享对象缓冲池、3 个 slab 链表、n 个 slab 分配器，以及众多 slab 缓存对象之间的关系。先对整个系统有大概的印象再去了解具体实现就比较容易。

![slab_structure.png](https://github.com/UtopianFuture/UtopianFuture.github.io/blob/master/image/slab_structure.png?raw=true)

简单总结一下各个结构体之间的关系。

首先我们需要了解 slab 描述符，类似于伙伴系统，内核在内存块中按照 2^order 字节（对象大小）来创建多个 slab 描述符，如 16 字节、32 字节、64 字节等，`kmem_cache` 保存了一些必要的信息。**slab 描述符拥有众多的 slab 对象（因为内核中很多数据结构大小一样，所以放在一个 slab 描述符中）**，这些对象按照一定的关系保存在本地对象缓冲池，共享对象缓冲池，slab 节点的 3 个链表中。让人感到迷惑的是这个 slab 链表和 slab 分配器有什么关系？其实每个 slab 分配器就是一个或多个物理页，这些物理页按照一个 slab 的大小可以存储多个 slab 对象，通过也有用于管理的管理区（freelist）和提高缓存效率的着色区（colour）。创建好的 slab 对象会根据其中空闲对象的数量插入 3 个链表之一。

当本地对象缓冲池中空闲对象数量不够，则**从共享对象缓冲池中迁移 batchcount 个空闲对象到本地对象缓冲池**；当本地和共享对象缓冲池都没有空闲对象，那么从 slab 节点的 `slabs_partial` 或 `slabs_free` 链表中迁移空闲对象到本地对象缓冲池；如果以上 3 个地方都没有空闲对象，那么就需要创建新的 slab 分配器，即通过伙伴系统的接口请求物理内存，然后再将新建的 slab 分配器所在的 page 插入到链表中（这种设计太牛了）。注意是 slab 分配器，不是 slab 描述符，某个大小的 slab 描述符应该只有一个，因为从代码上来看可以直接通过需要需要分配到大小确定对应的 slab 描述符。

#### 创建 slab 描述符

`kmem_cache` 是 slab 分配器中的核心数据结构，我们将其称为 slab 描述符。

##### kmem_cache

```c
struct kmem_cache {
	struct kmem_cache_cpu __percpu *cpu_slab; // 共享对象缓冲池（这应该是本地对象缓冲池）
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
	void (*ctor)(void *); // 构造函数（这个构造函数不知道有什么用）
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

其主要包含 3 个链表（slab 描述符和 slab 节点之间的关系是怎样的，每个 slab 描述符都有多个 slab 节点，节点拥有更多的空闲对象可供分配，但是**访问节点中的对象要比访问本地缓冲池慢**）。

```c
struct kmem_cache_node {
  	spinlock_t list_lock;

  #ifdef CONFIG_SLAB
    // 这里需要理解，slab 分配器其实就是一个或多个 page，上面也分析过，page 结构体中有专门支持 slab 机制的变量
    // 所谓满的、部分满的、空的 slab 分配器，其实就是该 page 中的空闲对象的数量
    // 新创建的 slab 分配器都需要插入这 3 个链表之一
    // 而这 3 个链表又属于某一个 slab 描述符的 slab 节点
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

##### kmem_cache_cpu

本地对象缓冲池，这里怎么有个 `struct slab` 结构，之前没注意。

原来是新的 patch，但是从 commit message 上看不出为何这样修改。

> mm/slub: Convert most struct page to struct slab by spatch
>
>  The majority of conversion from struct page to struct slab in SLUB
>  internals can be delegated to a coccinelle semantic patch. This includes
>  renaming of variables with 'page' in name to 'slab', and similar.

```c
struct kmem_cache_cpu {
	void **freelist;	/* Pointer to next available object */
	unsigned long tid;	/* Globally unique transaction id */
	struct slab *slab;	/* The slab from which we are allocating */
#ifdef CONFIG_SLUB_CPU_PARTIAL
	struct slab *partial;	/* Partially allocated frozen slabs */
#endif
	local_lock_t lock;	/* Protects the fields above */
#ifdef CONFIG_SLUB_STATS
	unsigned stat[NR_SLUB_STAT_ITEMS];
#endif
};
```

##### kmem_cache_create

`kmem_cache_create` 只是 `kmem_cache_create_usercopy` 的包装，直接看 `kmem_cache_create_usercopy`，

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

这里有个问题，为什么在 `vmalloc_init` 中会调用该函数？很简单，`vmap_area` 也是一个对象。

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

```plain
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

       // 找到合适的 gfporder，这就解决了第一个问题
   	for (gfporder = 0; gfporder <= KMALLOC_MAX_ORDER; gfporder++) { // 从 0 开始计算最合适（最小）的 gfporder
   		unsigned int num;
   		size_t remainder;

           // 2^gfporder/size，这就解决了第二个问题
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

#### 配置 slab 描述符

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


至此，slab 描述符完成创建。

#### 分配 slab 对象

分配过程对于我这个初学者来说过于复杂，所以先看看图，理理关系，再看具体实现。

![alloc_slab_obj.png](https://github.com/UtopianFuture/UtopianFuture.github.io/blob/master/image/alloc_slab_obj.png?raw=true)

发现在很多操作前都需要关闭中断，例如这里分配对象前关中断，这应该是有特定的技巧在其中的。比如在中断处理时可能也需要分配 slab 对象，如果不关中断可能导致死锁？

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
              - `get_node` 获取该 slab 描述符对应的节点（`kmem_cache_node`）

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
	} else { // 主动释放 bacthcount 个对象
		STATS_INC_FREEMISS(cachep);
		cache_flusharray(cachep, ac); // 回收 slab 分配器
	}

	...

	__free_one(ac, objp); // 直接将对象释放到本地对象缓冲池中
}
```

当系统所有空闲对象数目大于系统空闲对象数目阀值并且这个 slab 分配器没有活跃对象时，系统就是销毁这个 slab 分配器，从而回收内存。并且 slab 分配器还注册了一个定时器，定时的扫面所有的 slab 描述符，回收一部分内存，达到条件的 slab 分配器会被销毁（也就是回收一部分物理页）。

#### slab 分配器和伙伴系统的接口函数

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

#### 管理区 freelist

上文提到管理区可以看作一个 freelist 数组，数组的每个成员大小为 1 字节，每个成员管理一个 slab 对象。这里我们看看 freelist 是怎样管理 slab 分配器中的对象的。

在[分配 slab 对象](#分配slab对象)中介绍到 `cache_alloc_refill` 会判断本地/共享对象缓冲池中是否有空闲对象，如果没有的话就需要调用 `cache_grow_begin` 创建一个新的 slab 分配器。而在 `cache_init_objs` 中会把管理区中的 `freelist` 数组按顺序标号。

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

这部分只是大概了解 `vmalloc` 是干什么的和分配流程，详细的实现还不懂。

上面介绍了 `kmalloc` 使用 slab 分配器分配小块的、连续的物理内存，因为 slab 分配器在创建的时候也需要使用伙伴系统分配物理内存页面的接口，所以 **slab 分配器建立在一个物理地址连续的大块内存之上**（理解这点很重要）。那如果在内核中不需要连续的物理地址，而**仅仅需要内核空间的虚拟地址是连续的内存块**该如何处理？这就是 `vmalloc` 的工作。

后来发现 `vmalloc` 的用途不止于此，**在创建子进程时需要为子进程分配内核栈，这时就需要用到 `vmalloc`**。那是不是说需要在内核地址空间分配内存时就需要用到 `vmalloc`，这个有待验证。

vmalloc 映射区的**映射方式与用户空间完全相同**，内核可以通过调用 vmalloc 函数在内核地址空间的 vmalloc 区域获得内存。这个函数的功能相当于用户空间的 malloc 函数，所提供的虚拟地址空间是连续的， 但不保证物理地址是连续的。

#### vm_struct

还是先看看 `vmalloc` 相关的数据结构，**内核中用 `vm_struct` 来表示一块 vmalloc 分配的区域**，注意不要和用户态的 VMA 搞混了。

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
// 而 vm_struct 则更详细的记录了 page 信息
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
    // 主要就是分配虚拟地址，然后构造 vm_struct 这个结构体
	return __vmalloc_node_range(size, align, VMALLOC_START, VMALLOC_END,
				gfp_mask, PAGE_KERNEL, 0, node, caller);
}
```

`vmalloc` 分配的空间在[内存分布](# 内存分布)小节中的图中有清晰的说明（不同架构的内存布局是不一样的，因为这篇文章的时间跨度较大，参考多本书籍，所以混合了 arm 内核、Loongarch 内核和 x86 内核的源码，这是个问题，之后要想想怎么解决。在 64 位 x86 内核中，该区域为 `0xffffc90000000000 ~ 0xffffe8ffffffffff`）。

`vm_struct` 会区分多种类型的映射区域，

```c
/* bits in flags of vmalloc's vm_struct below */
#define VM_IOREMAP		0x00000001	/* ioremap() and friends */
#define VM_ALLOC		0x00000002	/* vmalloc() */
#define VM_MAP			0x00000004	/* vmap()ed pages */
#define VM_USERMAP		0x00000008	/* suitable for remap_vmalloc_range */
#define VM_DMA_COHERENT		0x00000010	/* dma_alloc_coherent */
#define VM_UNINITIALIZED	0x00000020	/* vm_struct is not fully initialized */
#define VM_NO_GUARD		0x00000040      /* don't add guard page */
#define VM_KASAN		0x00000080      /* has allocated kasan shadow memory */
#define VM_FLUSH_RESET_PERMS	0x00000100	/* reset direct map and flush TLB on unmap, can't be freed in atomic context */
#define VM_MAP_PUT_PAGES	0x00000200	/* put pages and free array in vfree */
#define VM_NO_HUGE_VMAP		0x00000400	/* force PAGE_SIZE pte mapping */
```

这些映射类型在后续的学习工作中都会遇到。

#### __vmalloc_node_range

vmalloc 的核心功能都是在 `__vmalloc_node_range` 函数中实现的。分配内存套路都是一样的，先分配虚拟地址，再根据需要决定是否要分配物理内存，最后建立映射。

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

    // 分配物理内存，并和 vm_struct 空间建立映射关系
	addr = __vmalloc_area_node(area, gfp_mask, prot, shift, node);
	if (!addr)
		goto fail;

	/*
	 * In this function, newly allocated vm_struct has VM_UNINITIALIZED
	 * flag. It means that vm_struct is not fully initialized.
	 * Now, it is fully initialized, so remove this flag here.
	 */
	clear_vm_uninitialized_flag(area);

	size = PAGE_ALIGN(size);
	kmemleak_vmalloc(area, size, gfp_mask); // 检查内存泄漏（？）

	return addr;
}
```

#### 分配虚拟内存

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

    // 怎么调用 kmalloc_node 了？
    // 这是因为需要分配一个新的数据结构，而数据结构往往就是几十个字节
    // 所以使用 slab 分配器。这种分配方式之后会遇到很多
	area = kzalloc_node(sizeof(*area), gfp_mask & GFP_RECLAIM_MASK, node);
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

- `alloc_vmap_area` 负责分配 vmalloc 区域。其**在 vmalloc 区域中查找一块大小合适的并且没有使用的空间**，这段空间称为缝隙（hole）。
  - 从 vmalloc 区域的起始位置 `VMALLOC_START` 开始，首先从红黑树 `vmap_area_root` 上查找，这棵树存放着系统正在使用的 vmalloc 区域，遍历左叶子节点寻找区域地址最小的区域。如果区域的开始地址等于 `VMALLOC_START` ，说明这个区域是第一个 vmalloc 区域；如果红黑树没有一个节点，说明整个 vmalloc 区域都是空的。
  - 从 `VMALLOC_START` 开始查找每个已存在的 vmalloc 区域的缝隙能够容纳目前申请的大小。如果已有的 vmalloc 区域的缝隙不能容纳，那么从最后一块 vmalloc 区域的结束地址开辟一个新的 vmalloc 区域。
  - 找到新的区域缝隙后，调用 `insert_vmap_area` 将其注册到红黑树。

这里有个疑问，为什么 vmalloc 最后申请空间也要调用到 slab 分配器？（上面已经解释了）

```plain
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

#### 分配物理地址

```c
static void *__vmalloc_area_node(struct vm_struct *area, gfp_t gfp_mask,
				 pgprot_t prot, unsigned int page_shift,
				 int node)
{
	const gfp_t nested_gfp = (gfp_mask & GFP_RECLAIM_MASK) | __GFP_ZERO;
	unsigned long addr = (unsigned long)area->addr;
	unsigned long size = get_vm_area_size(area);
	unsigned long array_size;
	unsigned int nr_small_pages = size >> PAGE_SHIFT;
	unsigned int page_order;

	array_size = (unsigned long)nr_small_pages * sizeof(struct page *);
	gfp_mask |= __GFP_NOWARN;
	if (!(gfp_mask & (GFP_DMA | GFP_DMA32))) // 确定使用哪个 ZONE
		gfp_mask |= __GFP_HIGHMEM; // 不过在 64 位系统中已经没有 HIGHMEM 了

	/* Please note that the recursion is strictly bounded. */
    // 哈，这个就很好理解了，大于一个 page 的调用伙伴系统，否则调用 slab 分配器
    // 不过这里只是分配指向所有 page 的指针，真正的物理内存不是在这里分配
	if (array_size > PAGE_SIZE) {
		area->pages = __vmalloc_node(array_size, 1, nested_gfp, node,
					area->caller);
	} else {
		area->pages = kmalloc_node(array_size, nested_gfp, node);
	}

	...

	set_vm_area_page_order(area, page_shift - PAGE_SHIFT);
    // 计算需要分配多少个 page
	page_order = vm_area_page_order(area);

    // 这里应该是分配物理页面
	area->nr_pages = vm_area_alloc_pages(gfp_mask, node,
		page_order, nr_small_pages, area->pages);

	atomic_long_add(area->nr_pages, &nr_vmalloc_pages);

	/*
	 * If not enough pages were obtained to accomplish an
	 * allocation request, free them via __vfree() if any.
	 */
	if (area->nr_pages != nr_small_pages) {
		warn_alloc(gfp_mask, NULL,
			"vmalloc error: size %lu, page order %u, failed to allocate pages",
			area->nr_pages * PAGE_SIZE, page_order);
		goto fail;
	}

	if (vmap_pages_range(addr, addr + size, prot, area->pages,
			page_shift) < 0) {
		warn_alloc(gfp_mask, NULL,
			"vmalloc error: size %lu, failed to map pages",
			area->nr_pages * PAGE_SIZE);
		goto fail;
	}

	return area->addr;

fail:
	__vfree(area->addr);
	return NULL;
}
```

### vmap

vmap 函数完成的工作是，**在 vmalloc 虚拟地址空间中找到一个空闲区域**，然后将 page 页面数组对应的物理内存映射到该区域，最终返回映射的虚拟起始地址。

vmalloc 和 vmap 的操作，大部分的逻辑操作是一样的，比如从 `VMALLOC_START ~ VMALLOC_END` 区域之间查找并分配 `vmap_area`， 比如对虚拟地址和物理页框进行映射关系的建立。**不同之处在于 vmap 建立映射时，page 是函数传入进来的，而 vmalloc 是通过调用 `alloc_page` 接口向 buddy 申请的**，一个建立映射，一个是申请内存。

```c
| vmap
|	-> get_vm_area_caller
|		-> __get_vm_area_node
|			-> alloc_vmap_area
|				-> __alloc_vmap_area // 从 VMALLOC 区域申请内存，使用 rb tree 管理 VMALLOC 虚拟地址空间
|			-> setup_vmalloc_vm // 写入到 vm_struct 中
|	-> vmap_pages_range
|		-> vmap_pages_range_noflush
|			-> __vmap_pages_range_noflush
|				-> vmap_range_noflush // 最终会调用到 vmap_pte_range，同样执行 set_pte_at 函数
|		-> flush_cache_vmap // 正常来说解映射之后需要 clean cache，防止后续 dirty data 导致 DDR 中数据被踩，但这里是空函数
```

### CMA[^7]

CMA(Contiguous Memory Allocator)负责**物理地址连续的内存分配**。一般系统会在启动过程中，从整个 memory 中配置一段连续内存用于 CMA，然后内核其他的模块可以通过 CMA 的接口 API 进行连续内存的分配。CMA 的核心并不是设计精巧的算法来管理地址连续的内存块，实际上它的**底层还是依赖内核伙伴系统这样的内存管理机制**。

Linux 内核中已经提供了各种内存分配的接口，为何还要建立 CMA 这种连续内存分配的机制呢？

最主要原因是功能上需要。很多嵌入式设备没有 IOMMU/SMMU，也不支持 scatter-getter，因此这类设备的驱动程序需要操作连续的物理内存空间才能提供服务。早期，这些设备包括相机、硬件音视频解码器和编码器等，它们通常服务于多媒体业务。当前虽然很多设备已经配置了 IOMMU/SMMU，但是依然有诸多嵌入式设备没有 IOMMU/SMMU，需要使用物理地址连续的内存。

此外，物理地址连续的内存 cache 命中率更高，访问速度更优，对业务性能有优势。

在系统启动之初，伙伴系统中的大块内存比较大，也许分配大块很容易，但是随着系统的运行，内存不断的分配、释放，大块内存不断的裂解，再裂解，这时候，内存碎片化导致分配地址连续的大块内存就不是那么容易。

在 CMA 被提出来之前有两个选择：

- 在启动时分配用于视频采集的 DMA buffer：缺点是当照相机不使用时（大多数时间内 camera 其实都是空闲的），预留的那些 DMA BUFFER 的内存实际上被浪费了；
- 另外一个方案是当实际使用 camere 设备的时候分配 DMA buffer：这种方式不会浪费内存，但是不可靠，随着内存碎片化，大的、连续的内存分配变得越来越困难，一旦内存分配失败，camera 就无法使用。

Michal Nazarewicz 的 CMA 补丁能够解决这一问题。对于 CMA 内存，**当前驱动没有分配使用的时候，这些 memory 可以内核的被其他的模块使用（当然有一定的要求）**，而当驱动分配 CMA 内存后，那些被其他模块使用的内存需要吐出来，形成物理地址连续的大块内存，给具体的驱动来使用。

#### CMA 初始化

##### dts 配置

配置 CMA 内存区有两种方法，一种是通过 dts 的 reserved memory，另外一种是通过 command line 参数和内核配置参数。

device tree 中可以包含 reserved-memory node，在该节点的 child node 中，可以定义各种预留内存的信息。可以参考如下示例来配置 cma 内存，

```c
        default_cma: linux,cma {
            compatible = "shared-dma-pool";
            reg = <0X0 0X0 0X0 0Xxxx>; // base + size
            reusable;
            linux,cma-default;
        };
        test_cma1: test_cma1 {
            compatible = "shared-dma-pool";
            alloc-ranges = <0X0 0X0 0XFFFFFFFF 0XFFFFFFFF>;
            size = <0X0 0Xxxx>; // size
            reusable;
        };
```

配置 cma 内存必须要有 `compatible = "shared-dma-pool"` 和 `reusable` 属性，不能有 `no-map` 属性，具有 `linux, cma-default` 属性的节点会成为默认的 cma 内存，之后 `cma_alloc` 都是走该节点申请内存。不具有 `linux,cma-default` 可以成为 for per device CMA area。

通过命令行参数也可以建立 cma area，这种方式用的少，不再介绍。

##### 内核初始化

初始化阶段可以分成：解析 dts、预留内存初始化和 buddy 初始化，在 [Memblock](./Memblock.md) 中我们分析了前两步，这里我们继续分析内存怎样从 cma 到 buddy。

```c
| setup_arch
| 	-> mm_init
|		-> mem_init
| 			-> free_all_bootmem
| 				-> free_low_memory_core_early
| 					-> __free_memory_core
```

CMA 是一块连续的物理内存，用户是通过 cma heap 的方式来使用它的，具体使用流程可以参考[这篇](https://utopianfuture.github.io/mm/DMA-heap.html)文章。在上面的过程中，free memory 被释放到伙伴系统中，而 reserved memory 不会进入伙伴系统，对于 CMA area，我们之前说过，最终被伙伴系统管理，因此，在初始化的过程中，CMA area 的内存会全部导入伙伴系统（方便其他应用可以通过伙伴系统分配内存）。具体代码如下：

```c
| start_kernel
| 	-> arch_call_rest_init
| 		-> rest_init
|			-> kernel_init
| 				-> kernel_init_freeable
| 					-> do_basic_setup
| 						-> do_initcalls
| 							-> do_initcall_level // 遍历 initcall_levels
| 								-> do_one_initcall // 该函数会调用到 core_initcall
```

最终执行 `cma_init_reserved_areas` 将 CMA 转入 buddy 系统管理，

```c
core_initcall(cma_init_reserved_areas);
static int __init cma_init_reserved_areas(void)
{
	int i;

    // 这里 cma_area_count 是在 rmem_cma_setup 中调用 cma_init_reserved_mem 进行初始化的
    // 即在 arm64_memblock_init 进行 memblock 初始化的时候
	for (i = 0; i < cma_area_count; i++)
		cma_activate_area(&cma_areas[i]); // 每个 cma_area 都初始化，这是一个全局变量

	return 0;
}
```

`cma_activate_area` 主要检查该 CMA 是否所有 page 都在一个 zone 以及调用 `init_cma_reserved_pageblock` 将所有的内存以 page 为单位释放给 buddy。

```c
static void __init cma_activate_area(struct cma *cma)
{
	unsigned long base_pfn = cma->base_pfn, pfn;
	struct zone *zone;

    // 在通过 cma_alloc 申请时，需要通过 bitmap 管理
    // 但是通过 buddy 申请时，又没有改变 bitmap，不会有问题么
	cma->bitmap = bitmap_zalloc(cma_bitmap_maxno(cma), GFP_KERNEL);

	/*
	 * alloc_contig_range() requires the pfn range specified to be in the
	 * same zone. Simplify by forcing the entire CMA resv range to be in the
	 * same zone.
	 */
	WARN_ON_ONCE(!pfn_valid(base_pfn));
	zone = page_zone(pfn_to_page(base_pfn));
	for (pfn = base_pfn + 1; pfn < base_pfn + cma->count; pfn++) {
		WARN_ON_ONCE(!pfn_valid(pfn));
		if (page_zone(pfn_to_page(pfn)) != zone)
			goto not_in_zone;
	}

	for (pfn = base_pfn; pfn < base_pfn + cma->count;
	 pfn += pageblock_nr_pages) // pageblock_nr_pages == 512
		init_cma_reserved_pageblock(pfn_to_page(pfn));
	spin_lock_init(&cma->lock);

 ...

	return;
}
```

注释写的很清楚，关于**为什么需要将 page type 设置为 `MIGRATE_CMA`** 后面有介绍，

```c
/* Free whole pageblock and set its migration type to MIGRATE_CMA. */
void __init init_cma_reserved_pageblock(struct page *page)
{
	unsigned i = pageblock_nr_pages;
	struct page *p = page;

	do {
		__ClearPageReserved(p); // 将页面已经设置的 reserved 标志位清除掉
		set_page_count(p, 0);
	} while (++p, --i);

	set_pageblock_migratetype(page, MIGRATE_CMA); // 将 migratetype 设置为 MIGRATE_CMA
	if (pageblock_order >= MAX_ORDER) {
		i = pageblock_nr_pages;
		p = page;

		do {
			set_page_refcounted(p);
			__free_pages(p, MAX_ORDER - 1); // 将 CMA 区域中所有的页面都释放到 buddy 系统中
			p += MAX_ORDER_NR_PAGES;
		} while (i -= MAX_ORDER_NR_PAGES);
	} else {
		set_page_refcounted(page);
		__free_pages(page, pageblock_order);
	}

	adjust_managed_page_count(page, pageblock_nr_pages); // 更新伙伴系统管理的内存数量
	page_zone(page)->cma_pages += pageblock_nr_pages; // 这里涉及到 pageblock 和 zone，是个好的切入点
}
```

至此，所有的 CMA area 的内存进入伙伴系统[^9]。

**注意，上面的 `RESERVEDMEM_OF_DECLARE(cma, "shared-dma-pool", rmem_cma_setup);` 是 CMA 的初始化，这里是将 CMA 导入到 buddy 系统。**

#### CMA 使用

当从伙伴系统请求内存的时候，我们需要提供了一个 gfp_mask 的参数。它有多种类型，不过**在 CMA 这个场景，它用来指定请求页面的迁移类型（migrate type）**。migrate type 有很多中，其中有一个是 `MIGRATE_MOVABLE` 类型，被标记为 `MIGRATE_MOVABLE` 的 page 说明该页面上的数据是**可以迁移的**。

```c
enum migratetype {
	MIGRATE_UNMOVABLE,
	MIGRATE_MOVABLE,
	MIGRATE_RECLAIMABLE,
#ifdef CONFIG_CMA
	MIGRATE_CMA,
#endif
	MIGRATE_PCPTYPES, /* the number of types on the pcp lists */
	MIGRATE_HIGHATOMIC = MIGRATE_PCPTYPES,
#ifdef CONFIG_MEMORY_ISOLATION
	MIGRATE_ISOLATE,	/* can't allocate from here */
#endif
	MIGRATE_TYPES
};
```

伙伴系统不会跟踪每一个 page frame 的迁移类型，实际上它是按照 pageblock 为单位进行管理的，memory zone 中会有一个 bitmap，指明该 zone 中每一个 pageblock 的 migrate type。在处理内存分配请求的时候，一般会首先从和请求相同 migrate type（gfp_mask）的 pageblock 中分配页面。如果分配不成功，不同 migrate type 的 pageblock 中也会考虑（这点在 alloc_pages 函数中很明确）。这意味着一个 unmovable 页面请求也可以从 migrate type 是 movable 的 pageblock 中分配。这一点 CMA 是不能接受的，所以引入了一个新的 migrate type：MIGRATE_CMA。这种迁移类型具有一个重要性质：**只有可移动的页面可以从 MIGRATE_CMA 的 pageblock 中分配**。

##### 分配连续内存

设备可以通过如下三种途径申请 cma 内存：

- dma heap：cma heap, carveout heap 等都是通过 cma_alloc 申请 cma 内存；
- dma_alloc_coherent：正常的设备可以使用该函数来申请 CMA 内存，其有三条路径，我们下面来详细分析一下这个函数；
- buddy 系统 fallback 到 cma 内存；

**dma heap**

以 cma heap 为例，

```c
static struct dma_buf *cma_heap_allocate(struct dma_heap *heap,
					 unsigned long len,
					 unsigned long fd_flags,
					 unsigned long heap_flags)
{
	...

	if (align > CONFIG_CMA_ALIGNMENT)
		align = CONFIG_CMA_ALIGNMENT;

	cma_pages = cma_alloc(cma_heap->cma, pagecount, align, false);
	if (!cma_pages)
		goto free_buffer;

	...

	buffer->pages = kmalloc_array(pagecount, sizeof(*buffer->pages), GFP_KERNEL);
	if (!buffer->pages) {
		ret = -ENOMEM;
		goto free_cma;
	}

	for (pg = 0; pg < pagecount; pg++)
		buffer->pages[pg] = &cma_pages[pg];
	buffer->cma_pages = cma_pages;
	buffer->heap = cma_heap;
	buffer->pagecount = pagecount;

	...

	return dmabuf;
}
```

cma heap 是申请 default cma 内存的，我们也可以创建设备专用的 cma 内存，然后创建一个 heap 给专门的设备/场景使用。

**dma_alloc_coherent**

这部分的详细分析可以看这篇[文档](../mm/Memory-Hierarchy.md)，

```c
static inline void *dma_alloc_coherent(struct device *dev, size_t size,
		dma_addr_t *dma_handle, gfp_t gfp)
{
	return dma_alloc_attrs(dev, size, dma_handle, gfp,
			(gfp & __GFP_NOWARN) ? DMA_ATTR_NO_WARN : 0);
}
```

`dma_alloc_attrs` 有 3 条分配路径，对应 3 种使用情况，

```c
void *dma_alloc_attrs(struct device *dev, size_t size, dma_addr_t *dma_handle,
		gfp_t flag, unsigned long attrs)
{
	const struct dma_map_ops *ops = get_dma_ops(dev);
	void *cpu_addr;

	WARN_ON_ONCE(!dev->coherent_dma_mask);
	if (dma_alloc_from_dev_coherent(dev, size, dma_handle, &cpu_addr)) // 私有的 dma 内存
		return cpu_addr;

	/* let the implementation decide on the zone to allocate from: */
	flag &= ~(__GFP_DMA | __GFP_DMA32 | __GFP_HIGHMEM);
	if (dma_alloc_direct(dev, ops)) // default cma
		cpu_addr = dma_direct_alloc(dev, size, dma_handle, flag, attrs);
	else if (ops->alloc) // 设置了 iommu_dma_ops 的情况
		cpu_addr = ops->alloc(dev, size, dma_handle, flag, attrs);
	else
		return NULL;

	return cpu_addr;
}
EXPORT_SYMBOL(dma_alloc_attrs);

```
上述 3 条路径中，`dma_direct_alloc` 和 `ops->alloc` 可能会走到 `cma_alloc` 函数，其他情况需要根据情况详细分析。我们来看一下 `cma_alloc` 是怎样从 default cma 中分配连续的物理内存。

##### cma_alloc

cma_alloc 用来从指定的 CMA area 上分配 count 个连续的 page frame，按照 align 对齐。具体过程就是从 bitmap 上搜索 free page 的过程，一旦搜索到，就调用 alloc_contig_range 向伙伴系统申请内存。需要注意的是，**CMA 内存分配过程是一个比较“重”的操作，可能涉及页面迁移、页面回收等操作，因此不适合用于 atomic context**。整个分配过程如下图所示：

```c
| cma_alloc
|	-> bitmap_find_next_zero_area_off
|	-> bitmap_set
|	-> alloc_contig_range
|		// 虽然是连续的一段物理内存，但是它们可能在不同 free_list 中
|		-> start_isolate_page_range
|			// 先隔离头尾两个 pageblock
|			// 先是调用 set_migratetype_isolate 配置 MIGRATE_ISOLATE
|			// 然后对范围内的 page 进行调整，如果这些 page 是一个 page order 的一部分
|			// 那么需要将这些 page 拆开来
|			// 对 compound_page 的处理会复杂一点，之后再分析
|			-> isolate_single_pageblock
|				-> set_migratetype_isolate
|					// 如果范围内有 page 是 reserved 等情况，就是不可移动的
|					// 在这个过程中，大页，复合页都是要特殊处理的，之后有时间再分析
|					-> has_unmovable_pages
|					-> set_pageblock_migratetype
|					-> move_freepages_block
|						-> move_freepages // 为什么要将这些 pages 移走
|			-> set_migratetype_isolate // 隔离剩下的 pageblock
|		// 从注释来看，执行该函数是因为 allocator 和 isolate 之间没有同步操作
|		// 可能这边把 page 配置为 isolate 了，该 page 又释放了
|		// 执行该函数可以 flush most of them?
|		-> drain_all_pages
|		-> __alloc_contig_migrate_range
|			-> isolate_migratepages_range
|				-> isolate_migratepages_block // 处理各种情况的 folios，最后添加到 cc->migratepges 链表中
|			-> reclaim_clean_pages_from_list
|				-> shrink_folio_list // 根据各种 folio 的情况，调整 clean_list 中的 folios 位置，确定是否要回收
|			-> migrate_pages
|				-> alloc_migration_target
|				-> migrate_pages_batch/migrate_pages_sync
|					-> migrate_pages_batch
|						-> migrate_folio_unmap
|						-> migrate_folio_move
|		-> test_pages_isolated
|		// 这里为什么还要 isolate 一次
|		// 其实不是 isolate，而是 isolate 完后，将这些 pages 从 buddy 里扣掉
|		// 这样下次就不会再申请到这些 pages
|		-> isolate_freepages_range
|			-> isolate_freepages_block
|				-> __isolate_free_page
|					-> del_page_from_free_list
|		-> undo_isolate_page_range
|			-> unset_migratetype_isolate
|	-> cma_clear_bitmap
```

![img](https://raw.githubusercontent.com/UtopianFuture/UtopianFuture.github.io/81305767754ca8df95d18da10465dd730bc093be/image/cma_alloc.svg)

```c
/**
 * cma_alloc() - allocate pages from contiguous area
 * @cma: Contiguous memory region for which the allocation is performed.
 * @count: Requested number of pages.
 * @align: Requested alignment of pages (in PAGE_SIZE order).
 * @no_warn: Avoid printing message about failed allocation
 *
 * This function allocates part of contiguous memory on specific
 * contiguous memory area.
 */
struct page *cma_alloc(struct cma *cma, unsigned long count,
		 unsigned int align, bool no_warn) // 预留的 cma 内存是 4MB 对齐，cma_alloc 申请是 align 对齐
{
	unsigned long mask, offset;
	unsigned long pfn = -1;
	unsigned long start = 0;
	unsigned long bitmap_maxno, bitmap_no, bitmap_count;
	unsigned long i;
	struct page *page = NULL;

    ...

    // 获取 bimap 最大的可用 bit 数(bitmap_maxno)，此次分配需要多大的 bitmap(bitmap_count)等
	mask = cma_bitmap_aligned_mask(cma, align);
	offset = cma_bitmap_aligned_offset(cma, align);
	bitmap_maxno = cma_bitmap_maxno(cma);
	bitmap_count = cma_bitmap_pages_to_bits(cma, count);

	...

	for (;;) {
		spin_lock_irq(&cma->lock); // 该 spinlock 是用来保护 cma->bitmap 的

        // 从 bitmap 中找到一块 align 对齐，size > count 的空闲块
        // 遍历是从 start = 0 开始，一直到 bitmap_maxno
        // 这里 bitmap 只会记录 cma_alloc 分配的内存，不会记录 buddy 分配的 cma 内存
        // 感觉这样分配势必带来效率低下
        // 如果 alloc_contig_range 返回失败，说明这块内存被 buddy 分配出去了
        // 那么后面调用 cma_alloc 也就无需尝试这块内存了
		bitmap_no = bitmap_find_next_zero_area_off(cma->bitmap,
				bitmap_maxno, start, bitmap_count, mask,
				offset);
		if (bitmap_no >= bitmap_maxno) { // 分配失败
			if ((num_attempts < max_retries) && (ret == -EBUSY)) {
				spin_unlock_irq(&cma->lock);
				if (fatal_signal_pending(current))
					break;
				...

				start = 0;
				ret = -ENOMEM;
				schedule_timeout_killable(msecs_to_jiffies(100));
				num_attempts++;
				continue;
			} else {
				spin_unlock_irq(&cma->lock);
				break;
			}
		}

        // 将要分配的页面的对应 bitmap 先置位为 1，表示已经分配了
		bitmap_set(cma->bitmap, bitmap_no, bitmap_count);

		/*
		 * It's safe to drop the lock here. We've marked this region for
		 * our exclusive use. If the migration fails we will take the
		 * lock again and unmark it.
		 */
		spin_unlock_irq(&cma->lock);
		pfn = cma->base_pfn + (bitmap_no << cma->order_per_bit);
		mutex_lock(&cma_mutex); // 这里为什么又用 mutex?

        // 关键函数，分配物理内存，下面分析
        // 这个函数主要是 migrate_page，如果迁移成功，返回 success，说明这块内存可以用，后续直接使用即可
        // 如果迁移失败，说明无法使用
		ret = alloc_contig_range(pfn, pfn + count, MIGRATE_CMA,
				 GFP_KERNEL | (no_warn ? __GFP_NOWARN : 0));
		mutex_unlock(&cma_mutex);
		if (ret == 0) { // 分配成功
			page = pfn_to_page(pfn); // pfn 是从 cma->base_pfn 来的，不是 alloc_contig_range 分配出来的
			break;
		}

		cma_clear_bitmap(cma, pfn, count); // 分配失败，清除掉 bitmap
		if (ret != -EBUSY)
			break;

		...

	}

	...

	return page;
}
EXPORT_SYMBOL_GPL(cma_alloc);
```

`cma_alloc` 分配过程中可能存在 schedule，不适合用于 atomic context。

**alloc_contig_range**

原来在分配前还要将该块内存配置成 `MIGRATE_ISOLATE` 状态。这里面的 hook 点能否做些文章呢？

```c
int alloc_contig_range(unsigned long start, unsigned long end,
		 unsigned migratetype, gfp_t gfp_mask)
{
	unsigned long outer_start, outer_end;
	unsigned int order;
	int ret = 0;
	bool skip_drain_all_pages = false;

	...

    // 将目标内存块的 pageblock 的迁移类型由 MIGRATE_CMA 变更为 MIGRATE_ISOLATE
    // 因为 buddy 系统不会从 MIGRATE_ISOLATE 迁移类型的 pageblock 分配页面
    // 可以防止在 cma 分配过程中，这些页面又被人从 buddy 分走
	ret = start_isolate_page_range(pfn_max_align_down(start),
				 pfn_max_align_up(end), migratetype, 0);
	trace_android_vh_cma_drain_all_pages_bypass(migratetype,
						&skip_drain_all_pages); // 这个 hook 点能用来干嘛
	if (!skip_drain_all_pages)
		drain_all_pages(cc.zone); // 回收 pcp(?)

    // 将目标内存块已使用的页面进行迁移处理，迁移过程就是将页面内容复制到其他内存区域，并更新对该页面的引用
	ret = __alloc_contig_migrate_range(&cc, start, end);

    ...

done:
	undo_isolate_page_range(pfn_max_align_down(start), // 取消 isolate 状态
				pfn_max_align_up(end), migratetype);
	return ret;
}
EXPORT_SYMBOL(alloc_contig_range);
```

**start_isolate_page_range**

如何将 page 隔离出来，什么情况下会失败，

```c
int start_isolate_page_range(unsigned long start_pfn, unsigned long end_pfn,
			 int migratetype, int flags, gfp_t gfp_flags) // migratetype = MIGRATE_CMA
{
	unsigned long pfn;
	struct page *page;

	/* isolation is done at page block granularity */
	unsigned long isolate_start = pageblock_start_pfn(start_pfn);
	unsigned long isolate_end = pageblock_align(end_pfn);
	int ret;
	bool skip_isolation = false;

	/* isolate [isolate_start, isolate_start + pageblock_nr_pages) pageblock */
	ret = isolate_single_pageblock(isolate_start, flags, gfp_flags, false,
			skip_isolation, migratetype);

	...

	/* isolate [isolate_end - pageblock_nr_pages, isolate_end) pageblock */
	ret = isolate_single_pageblock(isolate_end, flags, gfp_flags, true,
			skip_isolation, migratetype);

	...

	/* skip isolated pageblocks at the beginning and end */
	for (pfn = isolate_start + pageblock_nr_pages;
	 pfn < isolate_end - pageblock_nr_pages;
	 pfn += pageblock_nr_pages) {
		page = __first_valid_page(pfn, pageblock_nr_pages);
		if (page && set_migratetype_isolate(page, migratetype, flags,
					start_pfn, end_pfn)) {
			undo_isolate_page_range(isolate_start, pfn, migratetype);
			unset_migratetype_isolate(
				pfn_to_page(isolate_end - pageblock_nr_pages),
				migratetype);

			return -EBUSY;
		}
	}

	return 0;
}
```

**isolate_single_pageblock**

```c
static int isolate_single_pageblock(unsigned long boundary_pfn, int flags,
			gfp_t gfp_flags, bool isolate_before, bool skip_isolation,
			int migratetype)
{
	unsigned long start_pfn;
	unsigned long isolate_pageblock;
	unsigned long pfn;
	struct zone *zone;
	int ret;

	VM_BUG_ON(!pageblock_aligned(boundary_pfn));

	if (isolate_before)
		isolate_pageblock = boundary_pfn - pageblock_nr_pages;
	else
		isolate_pageblock = boundary_pfn;

	/*
	 * scan at the beginning of MAX_ORDER_NR_PAGES aligned range to avoid
	 * only isolating a subset of pageblocks from a bigger than pageblock
	 * free or in-use page. Also make sure all to-be-isolated pageblocks
	 * are within the same zone.
	 */
	zone = page_zone(pfn_to_page(isolate_pageblock));
	start_pfn = max(ALIGN_DOWN(isolate_pageblock, MAX_ORDER_NR_PAGES),
				 zone->zone_start_pfn);
	if (skip_isolation) {

		...

	} else { // 为什么是先设置 ISOLATE，下面再拆分，那岂不是可能有些范围外的 page 也给配置上了
		ret = set_migratetype_isolate(pfn_to_page(isolate_pageblock), migratetype,
				flags, isolate_pageblock, isolate_pageblock + pageblock_nr_pages);
	}

	...

	for (pfn = start_pfn; pfn < boundary_pfn;) {
		struct page *page = __first_valid_page(pfn, boundary_pfn - pfn);

		VM_BUG_ON(!page);
		pfn = page_to_pfn(page);

		/*
		 * start_pfn is MAX_ORDER_NR_PAGES aligned, if there is any
		 * free pages in [start_pfn, boundary_pfn), its head page will
		 * always be in the range.
		 */
		if (PageBuddy(page)) {
			int order = buddy_order(page);
			if (pfn + (1UL << order) > boundary_pfn) {
				/* free page changed before split, check it again */
				if (split_free_page(page, order, boundary_pfn - pfn)) // 拆开来
					continue;
			}

			pfn += 1UL << order;
			continue;
		}

		/*
		 * migrate compound pages then let the free page handling code
		 * above do the rest. If migration is not possible, just fail.
		 */
		if (PageCompound(page)) {

			... // 这种情况很复杂，下次再分析

		}

		pfn++;

	}

	return 0;

failed:
	/* restore the original migratetype */
	if (!skip_isolation)
		unset_migratetype_isolate(pfn_to_page(isolate_pageblock), migratetype);

	return -EBUSY; // 没隔离成功
}
```

**set_migratetype_isolate**

```c
static int set_migratetype_isolate(struct page *page, int migratetype, int isol_flags,
			unsigned long start_pfn, unsigned long end_pfn)
{
	struct zone *zone = page_zone(page);
	struct page *unmovable;
	unsigned long flags;
	unsigned long check_unmovable_start, check_unmovable_end;

	...

	check_unmovable_start = max(page_to_pfn(page), start_pfn);
	check_unmovable_end = min(pageblock_end_pfn(page_to_pfn(page)),
				 end_pfn);

	unmovable = has_unmovable_pages(check_unmovable_start, check_unmovable_end,
			migratetype, isol_flags);
	if (!unmovable) {
		unsigned long nr_pages;
		int mt = get_pageblock_migratetype(page);

		set_pageblock_migratetype(page, MIGRATE_ISOLATE);
		zone->nr_isolate_pageblock++;
		nr_pages = move_freepages_block(zone, page, MIGRATE_ISOLATE,
									NULL);
		__mod_zone_freepage_state(zone, -nr_pages, mt);
		spin_unlock_irqrestore(&zone->lock, flags);

		return 0;
	}

	spin_unlock_irqrestore(&zone->lock, flags);
	if (isol_flags & REPORT_FAILURE) {
		/*
		 * printk() with zone->lock held will likely trigger a
		 * lockdep splat, so defer it here.
		 */
		dump_page(unmovable, "unmovable page");
	}

	return -EBUSY;
}
```

**__alloc_contig_migrate_range**

看一下页面是怎样迁移的。

```c
/* [start, end) must belong to a single zone. */
static int __alloc_contig_migrate_range(struct compact_control *cc,
					unsigned long start, unsigned long end)
{
	/* This function is based on compact_zone() from compaction.c. */
	unsigned int nr_reclaimed;
	unsigned long pfn = start;
	unsigned int tries = 0;
	int ret = 0;
	struct migration_target_control mtc = {
		.nid = zone_to_nid(cc->zone),
		.gfp_mask = GFP_USER | __GFP_MOVABLE | __GFP_RETRY_MAYFAIL,
	};

    // 将准备添加到 LRU 链表上，却还未加入 LRU 的页面(还待在 LRU 缓存)添加到 LRU 上，并关闭 LRU 缓存功能
	lru_cache_disable();
	while (pfn < end || !list_empty(&cc->migratepages)) {
		if (fatal_signal_pending(current)) {
			ret = -EINTR;
			break;
		}

		if (list_empty(&cc->migratepages)) {
			cc->nr_migratepages = 0;

            // 隔离要分配区域已经被 buddy 使用的 page，存放到 cc 的链表中
            // 返回的是最后扫描并处理的页框号
            // 这里隔离主要是防止后续迁移过程，page 被释放或者被 LRU 回收路径使用
			ret = isolate_migratepages_range(cc, pfn, end);
			if (ret && ret != -EAGAIN)
				break;

			pfn = cc->migrate_pfn;
			tries = 0;
		} else if (++tries == 5) {
			ret = -EBUSY;
			break;
		}

        // 对于干净的文件页，直接回收即可
		nr_reclaimed = reclaim_clean_pages_from_list(cc->zone,
							&cc->migratepages);
		cc->nr_migratepages -= nr_reclaimed;

        // 页面迁移在内核态的主要接口，内核中涉及到页面迁移的功能大都会调到
        // 它把可移动的物理页迁移到一个新分配的页面
        // 这里应该很复杂，见下面的“页面迁移”章节
		ret = migrate_pages(&cc->migratepages, alloc_migration_target,
			NULL, (unsigned long)&mtc, cc->mode, MR_CONTIG_RANGE, NULL);
	}

	lru_cache_enable();

 ...

	return 0;
}
```

**undo_isolate_page_range**

**buddy fallback 使用**

调用 alloc_pages 从 buddy 申请内存时，如果带上了 ALLOC_CMA 标识，可能会从 cma 中分配内存，

```c
static __always_inline struct page *
__rmqueue(struct zone *zone, unsigned int order, int migratetype,
						unsigned int alloc_flags)
{
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

            // 如果有 ALLOC_CMA 标识位并且 CMA_PAGES > FREE_PAGE / 2，
            // 尝试从 MIGRATE_CMA 申请，
            // 上述标志位都没有，才从指定的 migratetype 中申请
            // 也就是说，如果申请者配置了 ALLOC_CMA 标志位
            // 就能优先从 cma 中申请内存
			page = __rmqueue_cma_fallback(zone, order);
			if (page)
				return page;
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

	return page;
}
```

```c
static __always_inline struct page *__rmqueue_cma_fallback(struct zone *zone,
					unsigned int order)
{
	return __rmqueue_smallest(zone, order, MIGRATE_CMA);
}

static __always_inline
struct page *__rmqueue_smallest(struct zone *zone, unsigned int order,
						int migratetype)
{
	unsigned int current_order;
	struct free_area *area;
	struct page *page;

	/* Find a page of the appropriate size in the preferred list */
	for (current_order = order; current_order <= MAX_ORDER; ++current_order) {
		area = &(zone->free_area[current_order]);
		page = get_page_from_free_area(area, migratetype); // 就是从 MIGRATE_CMA 中申请内存
		if (!page)
			continue;

		del_page_from_free_list(page, zone, current_order);
		expand(zone, page, order, current_order, migratetype);
		set_pcppage_migratetype(page, migratetype);
		trace_mm_page_alloc_zone_locked(page, order, migratetype,
				pcp_allowed_order(order) &&
				migratetype < MIGRATE_PCPTYPES);
		return page;
	}

	return NULL;
}
```

cma 存在两个问题：

- CMA 区域内存利用率低。假设物理内存中 CMA、Movable、其他（Unmovable 和 Reclaimable）的量分别是 X、Y、Z，通常情况下 X < Y 且 X<Z。显然，当用户通过 buddy 系统先申请 X 数量的 Movable 类型内存，这时申请的是 free_list[MIGRATE_Movable]，再申请 Y+Z 数量的 Unmovable 时，就会出现：CMA 区域内存充足，但 Unmovable 内存申请不到内存的问题，因为 Unmovalbe 类型无法从 CMA 中申请；

 可以采用 Movable 申请优先从 CMA 内存中申请，但是这样会导致 cma_alloc 变慢，因为需要迁移的 page 变多了。

- 部分 Movable 内存无法迁移导致 cma_alloc 失败。部分 Movable 内存在某些情况下无法迁移，如果这类内存处于 CMA 区域，会导致 cma_alloc 失败。[文章](https://lwn.net/Articles/636234/)中首先提出该问题，该问题是 CMA 区域的 Movable 内存被长期 pin 住，导致这些内存会变得“不可迁移”；

##### 释放连续内存

分配连续内存的逆过程，除了 bitmap 的操作之外，最重要的就是调用 `free_contig_range`，将指定的 pages 返回伙伴系统。

```c
bool cma_release(struct cma *cma, const struct page *pages,
		 unsigned long count)
{
	unsigned long pfn;

	if (!cma_pages_valid(cma, pages, count))
		return false;

	pr_debug("%s(page %p, count %lu)\n", __func__, (void *)pages, count);

	pfn = page_to_pfn(pages);

	VM_BUG_ON(pfn + count > cma->base_pfn + cma->count);

	free_contig_range(pfn, count); // 这里调用 __free_page 释放回 buddy
	cma_clear_bitmap(cma, pfn, count);
	trace_cma_release(cma->name, pfn, pages, count);

	return true;
}
EXPORT_SYMBOL_GPL(cma_release);
```

##### **问题**

- cma 如何初始化；
  先在 memblock 初始化时通过 RESERVEDMEM_OF_DECLARE(cma, "shared-dma-pool", rmem_cma_setup); 回调到 rmem_cma_setup 进行初始化，然后在 rest_init 函数中通过 core_initcall(cma_init_reserved_areas); 将每一块 CMA 内存释放到 buddy 系统，后续可以从 buddy 中 fallback 申请；

- dts 如何配置，有什么特殊要求；
  配置 cma 内存必须要有 compatible = "shared-dma-pool" 和 reusable 属性，不能有 no-map 属性，具有 linux, cma-default 属性的节点会成为默认的 cma 内存，之后 cma_alloc 都是走该节点申请内存。不具有 linux,cma-default 可以成为 for per device CMA area。

- cma 初始化时如何和 buddy 关联；
  在 rest_init 函数中通过 core_initcall(cma_init_reserved_areas); 将每一块 CMA 内存释放到 buddy 系统，后续可以从 buddy 中 fallback 申请；

- cma 内存被 buddy 申请后如何迁移；
  在 cma_alloc 函数中会调用 migrate_page 进行内存迁移；

- 如何申请使用 cma；
  - dma heap：cma heap, carveout heap 等都是通过 cma_alloc 申请 cma 内存；
  - buddy 系统 fallback 到 cma 内存；

- MIGRATE_MOVABLE, MIGRATE_CMA 和 MIGRATE_UNMOVABLE 有什么区别，分别适用于哪种场景；
  - 在调用 alloc_pages 申请内存时，如果配置了 MIGRATE_MOVABLE 属性，那么在 prepare_alloc_pages 中会将 ALLOC_CMA 也置上；
  - ALLOC_CMA 又是在什么场景下使用的；

- MIGRATE_CMA 是一个 free_list，alloc_pages 是调用 get_page_from_free_area 从 free_list 中划走，但是 cma__alloc 还维护了一个 bitmap，两者之间信息怎样交互？
  没有交互，在 bitmap 中到合适的内存块就尝试迁移，如果迁移成功，表示可以使用，否则继续遍历；

- 当前哪些场景使用了 CMA 内存；
  媒体组件的镜像加载，安全内存场景；

- DEBUG
  调用 cma_alloc 申请 cma 内存失败，返回 EBUSY，怎么办？
  首先返回 EBUSY 的情况有以下几种：
    - 有些 page 是 unmovable 的，无法配置为 MIGRATE_ISOLATE 属性;
    - 有些 page 已经配置为 MIGRATE_ISOLATE 属性；
    - 迁移超过 5 次还未将所有的 page 迁移走；

    可以加日志确定是哪种情况导致的失败，然后 cma 内存是有固定范围的，在 memblock 阶段就可以确定下来。我们可以针对不同的情况加打印：
    - 在所有会配置 unmovable 属性的地方加打印，输出 page_own；
    - cma_alloc 函数的使用者是已知的，在配置 MIGRATE_ISOLATE 的地方把 PID 打印出来；
    - 这个情况比较复杂，还需要分析；

### 进程地址空间

还是先看看进程的地址空间布局。

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
        // 堆空间就是 malloc 可用的空间，随着内存的使用逐渐往上增长
		unsigned long start_brk, brk, start_stack;
		unsigned long arg_start, arg_end, env_start, env_end; //（？）

		unsigned long saved_auxv[AT_VECTOR_SIZE]; /* for /proc/PID/auxv */

        /*
		 * Special counters, in some configurations protected by the
		 * page_table_lock, in other configurations by being atomic.
		 */
		struct mm_rss_stat rss_stat;

		struct linux_binfmt *binfmt;

		/* Architecture-specific MM context */
		mm_context_t context; // 保存硬件上下文

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

VMA 描述的是进程用 mmap，malloc 等函数分配的地址空间，或者说它**描述的是进程地址空间的一个区间**。所有的 `vm_area_struct` 构成一个单链表和一颗红黑树，通过 `mm_struct` 就可以找到所有的 `vm_area_struct`，再结合虚拟地址就可以找到对应的 `vm_area_struct`。

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

这里有个问题，X86 CPU 中不是有 cr3 寄存器指向一级页表么，为什么这里还要设置一个 pgd，难道 cr3 中的数据是从这里来的？而且图虽然清晰，但是很多细节还需要仔细分析，如 VMA 和物理页面建立映射关系等等，建立映射关系其实就是分配 page，返回的 page 就是物理地址。

#### VMA 相关操作

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

#### brk 系统调用

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

所有的系统调用展开后的地址都会保存在系统调用表 `sys_call_table` 中。

brk 系统调用最后展开成 `__do_sys_brk`，

```plain
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

#### 创建 VMA

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

这个有个疑问，`vm_area_alloc` 创建新的 VMA 为什么还会调用到 slab 分配器？`vm_area_alloc` 本身作为一个结构也需要占用内存，不过它的大小肯定小于 4K，所以使用 slab 来分配。

```plain
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

#### 分配物理内存

`mm_populate` 为该进程分配物理内存。通常用户进程很少使用 `VM_LOCKED` 分配掩码（果然很少用，设置断点都跑不到，那就分析代码看怎样建立映射吧），所以 brk 系统调用不会马上为这个进程分配物理内存，而是**一直延迟到用户进程需要访问这些虚拟页面并发生缺页中断时才会分配物理内存，并和虚拟地址建立映射关系**。

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

#### 建立映射关系

`populate_vma_page_range` 调用 `__get_user_pages` 来分配物理内存并建立映射关系。

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

`follow_page_mask` 主要用于遍历页表并返回物理页面的 page 数据结构，这个应该比较复杂，但也是核心函数，之后再分析。这里有个问题，就是遍历页表不是由 MMU 做的，为什么这里还要用软件遍历？这里并不是访存，所以不会用到 MMU。

简单总结一下 malloc 的操作流程。标准 C 库函数 malloc 最后使用的系统调用是 brk，传入的参数只有 brk 的结束地址，用这个地址和`mm -> brk` 比较，确定是释放内存还是分配内存。而需要分配内存的大小为 `newbrk-oldbrk`，这里 `newbrk` 就是传入的参数，`oldbrk` 是`mm -> brk`。brk 系统调用申请的内存空间貌似都是 `0x21000`。同时根据传入的 brk 在 VMA 的红黑树中寻找是否存在已经分配的内存块，如果有的话那么就不需要从新分配，否则就调用 `do_brk_flags` 分配新的 VMA，然后进行初始化，更新该进程的 `mm`。这样来看就是创建一个 VMA 嘛，物理空间都是发生 #PF 才分配的。

### mmap/munmap[^11]

mmap 就是将文件映射到进程的物理地址空间，使得进程访问文件和正常的访问内存一样（由于 linux 一切皆文件的思想，这里文件可以指申请的一块内存，一个外设，一个文件，所以 mmap 的使用非常广泛，意味着它要处理非常多的情况）。

![mmap.png](https://github.com/UtopianFuture/UtopianFuture.github.io/blob/master/image/mmap.png?raw=true)

根据文件关联性和映射区域是否共享等属性，mmap 映射可以分为 4 类：
- 私有匿名映射。匿名映射即没有映射对应的相关文件，内存区域的内容会被初始化为 0；
- 共享匿名映射。共享匿名映射让相关进程共享一块内存区域，通常用于父、子进程之间的通信；
- 私有文件映射。该映射的场景是加载动态共享库；
- 共享文件映射。这种映射有两种使用场景：
 - 读写文件；
 - 进程间通讯。进程之间的地址空间是相互隔离的，如果多个进程同时映射到一个文件，就实现了多进程间的共享内存通信。

mmap 机制在内核中的实现和 brk 类似，但其和缺页中断机制结合后会复杂很多。如内存漏洞 [Dirty COW](https://blog.csdn.net/hbhgyu/article/details/106245182) 就利用了 mmap 和缺页中断的相关漏洞。

![mmap-implement.png](https://github.com/UtopianFuture/UtopianFuture.github.io/blob/master/image/mmap-implement.png?raw=true)

在面试的时候被问到这样一个问题：“mmap 映射文件是怎样和磁盘联系”，所以进一步分析一下 mmap 是怎样完成的。

用户态接口，

```c
/**
 * @offset: 该参数表示将 fd 指定的 file 的从 [offset, offset + length) 范围的数据映射到 addr 开始的地址范围，
 * 默认为 0
 */
void *mmap(void *addr, size_t length, int prot, int flags, int fd, off_t offset);
```

以 dma_buf 中的 cma_heap 为例看一下 mmap 的内核调用流程：

```
#0 cma_heap_mmap (dmabuf=0xffffff8005504e00, vma=0xffffff80055efb40) at drivers/dma-buf/multi-heap/cma_heap.c:207
#1 0xffffffc008be5924 in dma_buf_mmap_internal (file=0xffffff8005504e00, vma=0x200000) at drivers/dma-buf/dma-buf.c:168
#2 0xffffffc00835867c in call_mmap (vma=<optimized out>, file=<optimized out>) at ./include/linux/fs.h:2092
#3 mmap_region (file=0xffffff8005504e00, addr=547939028992, len=18446743524043845824, vm_flags=251, pgoff=18446743798978460176, uf=0x0) at mm/mmap.c:1791
#4 0xffffffc008359380 in do_mmap (file=0xffffff800552af00, addr=18446743524043848512, len=18446743799011098624, prot=3, flags=1, pgoff=0, populate=0xffffffc00afebd50, uf=0x7f93b61000) at mm/mmap.c:1575
#5 0xffffffc00830c9f0 in vm_mmap_pgoff (file=0xffffff8005504e00, addr=0, len=2097152, prot=3, flag=1, pgoff=0) at mm/util.c:551
#6 0xffffffc008353e98 in ksys_mmap_pgoff (addr=0, len=2097152, prot=3, flags=1, fd=18446743798978460176, pgoff=0) at mm/mmap.c:1624
#7 0xffffffc008027f18 in __do_sys_mmap (off=<optimized out>, fd=<optimized out>, flags=<optimized out>, prot=<optimized out>, len=<optimized out>,
 addr=<optimized out>) at arch/arm64/kernel/sys.c:28
#8 __se_sys_mmap (off=<optimized out>, fd=<optimized out>, flags=<optimized out>, prot=<optimized out>, len=<optimized out>, addr=<optimized out>)
 at arch/arm64/kernel/sys.c:21
#9 __arm64_sys_mmap (regs=0xffffff8005504e00) at arch/arm64/kernel/sys.c:21
#10 0xffffffc008037644 in __invoke_syscall (syscall_fn=<optimized out>, regs=<optimized out>) at arch/arm64/kernel/syscall.c:38
#11 invoke_syscall (regs=0xffffffc00afebeb0, scno=90110784, sc_nr=179453952, syscall_table=<optimized out>) at arch/arm64/kernel/syscall.c:52
#12 0xffffffc008037864 in el0_svc_common (sc_nr=<optimized out>, syscall_table=<optimized out>, scno=89148928, regs=<optimized out>)
 at arch/arm64/kernel/syscall.c:142
#13 do_el0_svc (regs=0xffffff8005504e00) at arch/arm64/kernel/syscall.c:181
#14 0xffffffc0095ad568 in el0_svc (regs=0xffffffc00afebeb0) at arch/arm64/kernel/entry-common.c:595
#15 0xffffffc0095ad960 in el0t_64_sync_handler (regs=0xffffff8005504e00) at arch/arm64/kernel/entry-common.c:613
#16 0xffffffc0080115cc in el0t_64_sync () at arch/arm64/kernel/entry.S:584
```

应该是版本问题，执行流程和上面的图略有些不一样，`sys_mmap_pgoff` 是系统调用的处理函数，其会调用 `ksys_mmap_pgoff`，

```c
// fd 就是 alloc_fd 中分配的，通过它可以找到 file
// pgoff 就是 mmap 接口中的 offset 参数
unsigned long ksys_mmap_pgoff(unsigned long addr, unsigned long len,
			 unsigned long prot, unsigned long flags,
			 unsigned long fd, unsigned long pgoff)
{
	struct file *file = NULL;
	unsigned long retval;

	if (!(flags & MAP_ANONYMOUS)) { // 判断是否是文件映射
		audit_mmap_fd(fd, flags);
		file = fget(fd); // 这里主要是根据文件描述符获取对应的文件
		if (!file)
			return -EBADF;
		if (is_file_hugepages(file)) {
			len = ALIGN(len, huge_page_size(hstate_file(file)));
		} else if (unlikely(flags & MAP_HUGETLB)) {
			retval = -EINVAL;
			goto out_fput;
		}
	} else if (flags & MAP_HUGETLB) { // 按理来说不是文件映射就是匿名映射，为什么是判断 HUGETLB

		...

	}

    // 匿名页不用判断，file 就是 NULL
	retval = vm_mmap_pgoff(file, addr, len, prot, flags, pgoff);

out_fput:
	if (file)
		fput(file);
	return retval;
}
```

`vm_mmap_pgoff` 不单单是 mmap 系统调用会使用，内核其他部分也会使用，

```c
unsigned long vm_mmap_pgoff(struct file *file, unsigned long addr,
	unsigned long len, unsigned long prot,
	unsigned long flag, unsigned long pgoff)
{
	unsigned long ret;
	struct mm_struct *mm = current->mm;
	unsigned long populate;

	LIST_HEAD(uf);
	ret = security_mmap_file(file, prot, flag); // 这个不知道是干啥的。主要是检查文件权限是否正确等
	if (!ret) {
		if (mmap_write_lock_killable(mm))
			return -EINTR;
		ret = do_mmap(file, addr, len, prot, flag, pgoff, &populate, // 这个才是关键函数
			 &uf);
		mmap_write_unlock(mm);
		userfaultfd_unmap_complete(mm, &uf);
		if (populate)
            // 如果 mmap 传入了 MAP_POPULATE/MAP_LOCKED 标志，就需要做该操作
            // 即对 vma 中的每个 page 触发 page fault，提前分配物理内存
			mm_populate(ret, populate);
	}
	return ret;
}
```

#### 关键函数 do_mmap

这个函数的核心功能就是找到空闲的虚拟内存地址，并根据不同的文件打开方式设置不同的 vm 标志位 flag！

```c
unsigned long do_mmap(struct file *file, unsigned long addr,
			unsigned long len, unsigned long prot,
			unsigned long flags, unsigned long pgoff,
			unsigned long *populate, struct list_head *uf)
{
	struct mm_struct *mm = current->mm;
	vm_flags_t vm_flags;
	int pkey = 0;
	*populate = 0;

	if (!len) // 对于用户态 mmap，len 不能为 0
		return -EINVAL;
	/*
	 * Does the application expect PROT_READ to imply PROT_EXEC?
	 *
	 * (the exception is when the underlying filesystem is noexec
	 * mounted, in which case we dont add PROT_EXEC.)
	 */

    // 还没遇到过这种情况
	if ((prot & PROT_READ) && (current->personality & READ_IMPLIES_EXEC))
		if (!(file && path_noexec(&file->f_path)))
			prot |= PROT_EXEC;

	/* force arch specific MAP_FIXED handling in get_unmapped_area */
	if (flags & MAP_FIXED_NOREPLACE)
		flags |= MAP_FIXED;
	if (!(flags & MAP_FIXED))
		addr = round_hint_to_min(addr);

	/* Careful about overflows.. */
	len = PAGE_ALIGN(len);
	if (!len)
		return -ENOMEM;

	/* offset overflow? */
	if ((pgoff + (len >> PAGE_SHIFT)) < pgoff)
		return -EOVERFLOW;

    // 原来对于一个进程来说，mmap 的次数是有上限的
	/* Too many mappings? */
	if (mm->map_count > sysctl_max_map_count)
		return -ENOMEM;
	/* Obtain the address to map to. we verify (or select) it and ensure
	 * that it represents a valid section of the address space.
	 */

    // 从当前进程的用户空间获取一个未被映射区间的起始地址
    // 该函数中会调用 get_unmapped_area 回调函数
    // 不同的映射方式回调函数不同，但做的事情是类似的
    // 从 mmap 地址空间找一块空闲的内存
    // 从当前的调用栈来看，大部情况是调用 arch_get_unmapped_area_topdown 函数
	addr = get_unmapped_area(file, addr, len, pgoff, flags);
	if (IS_ERR_VALUE(addr)) // 错误地址不应该处理一下么
		return addr;

    // 又是各种权限的检查
	if (flags & MAP_FIXED_NOREPLACE) {
		if (find_vma_intersection(mm, addr, addr + len))
			return -EEXIST;
	}

	...

	if (file) {
		struct inode *inode = file_inode(file);
		unsigned long flags_mask;

        // 检查 len 和 pgoff 是否符合 file 的长度要求
		if (!file_mmap_ok(file, inode, pgoff, len))
			return -EOVERFLOW;

		flags_mask = LEGACY_MAP_MASK | file->f_op->mmap_supported_flags;
		switch (flags & MAP_TYPE) { // 不同的映射类型
		case MAP_SHARED:
			/*
			 * Force use of MAP_SHARED_VALIDATE with non-legacy
			 * flags. E.g. MAP_SYNC is dangerous to use with
			 * MAP_SHARED as you don't know which consistency model
			 * you will get. We silently ignore unsupported flags
			 * with MAP_SHARED to preserve backward compatibility.
			 */
			flags &= LEGACY_MAP_MASK;
			fallthrough; // 这是编译器支持的一个 attribute，没啥具体操作，进入到下一个 case 执行
		case MAP_SHARED_VALIDATE:
			if (flags & ~flags_mask)
				return -EOPNOTSUPP;
			if (prot & PROT_WRITE) {
				if (!(file->f_mode & FMODE_WRITE))
					return -EACCES;
				if (IS_SWAPFILE(file->f_mapping->host))
					return -ETXTBSY;
			}
			/*
			 * Make sure we don't allow writing to an append-only
			 * file..
			 */
			if (IS_APPEND(inode) && (file->f_mode & FMODE_WRITE))
				return -EACCES;
			vm_flags |= VM_SHARED | VM_MAYSHARE;
			if (!(file->f_mode & FMODE_WRITE))
				vm_flags &= ~(VM_MAYWRITE | VM_SHARED);
			fallthrough;
		case MAP_PRIVATE:
			if (!(file->f_mode & FMODE_READ))
				return -EACCES;
			if (path_noexec(&file->f_path)) {
				if (vm_flags & VM_EXEC)
					return -EPERM;
				vm_flags &= ~VM_MAYEXEC;
			}
			if (!file->f_op->mmap)
				return -ENODEV;
			if (vm_flags & (VM_GROWSDOWN|VM_GROWSUP))
				return -EINVAL;
			break;
		default:
			return -EINVAL;
		}
	} else {
		switch (flags & MAP_TYPE) { // 哦，原来是在这里处理匿名映射
		case MAP_SHARED:
			if (vm_flags & (VM_GROWSDOWN|VM_GROWSUP))
				return -EINVAL;
			/*
			 * Ignore pgoff.
			 */
			pgoff = 0;
			vm_flags |= VM_SHARED | VM_MAYSHARE;
			break;
		case MAP_PRIVATE:
			/*
			 * Set pgoff according to addr for anon_vma.
			 */
			pgoff = addr >> PAGE_SHIFT; // 为什么私有匿名映射要设置 pgoff
			break;
		default:
			return -EINVAL;
		}
	}

	...

    // 好吧，原来上面种种操作都是设置标志位，这里才是关键
	addr = mmap_region(file, addr, len, vm_flags, pgoff, uf);
	if (!IS_ERR_VALUE(addr) &&
	 ((vm_flags & VM_LOCKED) ||
	 (flags & (MAP_POPULATE | MAP_NONBLOCK)) == MAP_POPULATE))
		*populate = len;

	return addr;
}
```

#### 关键函数 unmapped_area

该函数是从进程地址空间的 mmap 范围内申请一块空闲的虚拟地址。

```
| arch_get_unmapped_area_topdown
| 	-> vm_unmapped_area
| 		-> arch_get_unmapped_area
| 			-> vm_unmapped_area
| 				-> unmapped_area
```

```c
/**
 * unmapped_area() - Find an area between the low_limit and the high_limit with
 * the correct alignment and offset, all from @info. Note: current->mm is used
 * for the search.
 *
 * @info: The unmapped area information including the range [low_limit -
 * high_limit), the alignment offset and mask.
 *
 * Return: A memory address or -ENOMEM.
 */
static unsigned long unmapped_area(struct vm_unmapped_area_info *info)
{
	... // 没看懂，之后再分析

}
```

#### 关键函数 mmap_region

其核心功能是创建 vma 和初始化虚拟内存区域，并加入红黑树节点进行管理，这个看上面的图更容易了解。

```c
unsigned long mmap_region(struct file *file, unsigned long addr,
		unsigned long len, vm_flags_t vm_flags, unsigned long pgoff,
		struct list_head *uf)
{
	struct mm_struct *mm = current->mm;
	struct vm_area_struct *vma, *prev, *merge;
	int error;
	struct rb_node **rb_link, *rb_parent;
	unsigned long charged = 0;

	/* Check against address space limit. */
	if (!may_expand_vm(mm, vm_flags, len >> PAGE_SHIFT)) { // 检查申请的内存空间是否超过限制

        ..

	}

	/* Clear old maps, set up prev, rb_link, rb_parent, and uf */
    // 检查 [addr, addr + len] 范围内是否已经有映射了
    // 如果有，那么需要将旧的映射关系清除
	if (munmap_vma_range(mm, addr, len, &prev, &rb_link, &rb_parent, uf))
		return -ENOMEM;

	/*
	 * Private writable mapping: check memory availability
	 */
	if (accountable_mapping(file, vm_flags)) {
		charged = len >> PAGE_SHIFT;
		if (security_vm_enough_memory_mm(mm, charged))
			return -ENOMEM;
		vm_flags |= VM_ACCOUNT;
	}

	/*
	 * Can we just expand an old mapping?
	 */
    // 这个之前也遇到过，合并 vma
	vma = vma_merge(mm, prev, addr, addr + len, vm_flags,
			NULL, file, pgoff, NULL, NULL_VM_UFFD_CTX);
	if (vma)
		goto out;

    // 申请新的 VMA
	vma = vm_area_alloc(mm);
	if (!vma) {
		error = -ENOMEM;
		goto unacct_error;
	}

    // 初始化，每次 mmap 都有一个 vma 对应
	vma->vm_start = addr;
	vma->vm_end = addr + len;
	vma->vm_flags = vm_flags;
	vma->vm_page_prot = vm_get_page_prot(vm_flags);
	vma->vm_pgoff = pgoff;
	if (file) { // 文件映射
		if (vm_flags & VM_SHARED) { // 共享，可写
			error = mapping_map_writable(file->f_mapping);
			if (error)
				goto free_vma;
		}
		vma->vm_file = get_file(file); // 这里会增加该 file 的引用次数

        // 调用该文件系统指定的 mmap 函数，这里还可以进一步分析
        // 这个函数最终就是建立页表
		error = call_mmap(file, vma);
		if (error)
			goto unmap_and_free_vma;

		/* Can addr have changed??
		 *
		 * Answer: Yes, several device drivers can do it in their
		 * f_op->mmap method. -DaveM
		 * Bug: If addr is changed, prev, rb_link, rb_parent should
		 * be updated for vma_link()
		 */
		WARN_ON_ONCE(addr != vma->vm_start);
		addr = vma->vm_start;

		...

		vm_flags = vma->vm_flags;
	} else if (vm_flags & VM_SHARED) {
		error = shmem_zero_setup(vma); // 共享匿名映射
		if (error)
			goto free_vma;
	} else { // 私有匿名映射，vm_ops = NULL
		vma_set_anonymous(vma);
	}

	/* Allow architectures to sanity-check the vm_flags */
	if (!arch_validate_flags(vma->vm_flags)) {
		error = -EINVAL;
		if (file)
			goto unmap_and_free_vma;
		else
			goto free_vma;
	}
	vma_link(mm, vma, prev, rb_link, rb_parent); // 常规操作

    ...

}
```

mmap 映射时可能会有物理内存来建立映射，如文件页映射，系统已经申请好了文件页，dma-buf 之类的；也可能是匿名页，这时没有分配物理内存，之后访问到这块内存会触发缺页中断，在缺页中断中才会实际分配内存。

#### [cache 问题](https://stackoverflow.com/questions/75277872/mmap-and-instruction-data-cache-coherency-why-can-we-copy-and-run-shared-libr)

在读代码的时候发现一个很奇怪的问题，以文件页映射为例，在 `call_mmap` 中会调用 `file->f_op->mmap`，后续可能会调用到 `remap_pfn_range` 建立映射。而在 `set_pte_at` 中会进行 cache 操作，范围是到 pou，这是为啥？

```c
| remap_pfn_range
| 	-> remap_pfn_range_notrack
|		-> flush_cache_range // 空函数
|		-> remap_p4d_range
```

```c
int remap_pfn_range_notrack(struct vm_area_struct *vma, unsigned long addr,
		unsigned long pfn, unsigned long size, pgprot_t prot)
{
	pgd_t *pgd;
	unsigned long next;
	unsigned long end = addr + PAGE_ALIGN(size);
	struct mm_struct *mm = vma->vm_mm;
	int err;
	...

	vm_flags_set(vma, VM_IO | VM_PFNMAP | VM_DONTEXPAND | VM_DONTDUMP);
	BUG_ON(addr >= end);
	pfn -= addr >> PAGE_SHIFT;
	pgd = pgd_offset(mm, addr);
	flush_cache_range(vma, addr, end); // 空函数，很奇怪
	do {
		next = pgd_addr_end(addr, end);
		err = remap_p4d_range(mm, pgd, addr, next, // 建立 3/4/5 级页表
				pfn + (addr >> PAGE_SHIFT), prot);
		if (err)
			return err;
	} while (pgd++, addr = next, addr != end);
	return 0;
}
```

最终会调用到 `remap_pte_range` 将页表写入到 DDR 中，

```c
static int remap_pte_range(struct mm_struct *mm, pmd_t *pmd,
			unsigned long addr, unsigned long end,
			unsigned long pfn, pgprot_t prot)
{
	pte_t *pte, *mapped_pte;
	spinlock_t *ptl;
	int err = 0;
	mapped_pte = pte = pte_alloc_map_lock(mm, pmd, addr, &ptl); // 分配 pte 的内存
	if (!pte)
		return -ENOMEM;
	arch_enter_lazy_mmu_mode();
	do {
		BUG_ON(!pte_none(ptep_get(pte)));
		if (!pfn_modify_allowed(pfn, prot)) {
			err = -EACCES;
			break;
		}
		set_pte_at(mm, addr, pte, pte_mkspecial(pfn_pte(pfn, prot)));
		pfn++;
	} while (pte++, addr += PAGE_SIZE, addr != end);
	arch_leave_lazy_mmu_mode();
	pte_unmap_unlock(mapped_pte, ptl);
	return err;
}
```

```c
#define set_pte_at(mm, addr, ptep, pte) set_ptes(mm, addr, ptep, pte, 1)

static __always_inline void set_ptes(struct mm_struct *mm, unsigned long addr,
				pte_t *ptep, pte_t pte, unsigned int nr)
{
	pte = pte_mknoncont(pte);
	if (likely(nr == 1)) {
		contpte_try_unfold(mm, addr, ptep, __ptep_get(ptep));
		__set_ptes(mm, addr, ptep, pte, 1);
		contpte_try_fold(mm, addr, ptep, pte);
	} else {
		contpte_set_ptes(mm, addr, ptep, pte, nr);
	}
}

static inline void __set_ptes(struct mm_struct *mm,
			 unsigned long __always_unused addr,
			 pte_t *ptep, pte_t pte, unsigned int nr)
{
	page_table_check_ptes_set(mm, ptep, pte, nr);
	__sync_cache_and_tags(pte, nr); // 在这里进行 cache 操作
	for (;;) {
		__check_safe_pte_update(mm, ptep, pte);
		__set_pte(ptep, pte);
		if (--nr == 0)
			break;
		ptep++;
		pte = pte_advance_pfn(pte, 1);
	}
}

static inline void __sync_cache_and_tags(pte_t pte, unsigned int nr_pages)
{
    // pte 存在才需要 flush cache
    // 不存在的话说明没人用过这块物理内存，不会存在 cache 问题
    // pte_user_exec 判断页表项是否允许用户执行（user exec）
    // pte_special 判断页表项是否是特殊类型（special）
	if (pte_present(pte) && pte_user_exec(pte) && !pte_special(pte))
		__sync_icache_dcache(pte);

	/*
	 * If the PTE would provide user space access to the tags associated
	 * with it then ensure that the MTE tags are synchronised. Although
	 * pte_access_permitted() returns false for exec only mappings, they
	 * don't expose tags (instruction fetches don't check tags).
	 */
	if (system_supports_mte() && pte_access_permitted(pte, false) &&
	 !pte_special(pte) && pte_tagged(pte))
		mte_sync_tags(pte, nr_pages);
}

void __sync_icache_dcache(pte_t pte)
{
	struct folio *folio = page_folio(pte_page(pte));
	if (!test_bit(PG_dcache_clean, &folio->flags)) { // 如果没有配置 dcache_clean，则需要 sync
		sync_icache_aliases((unsigned long)folio_address(folio),
				 (unsigned long)folio_address(folio) +
					 folio_size(folio));

        // 同步完置上 dcache_clean 位，表明该 page 在 cache 中也是 clean 的，不需要再 clean cache
		set_bit(PG_dcache_clean, &folio->flags);
	}
}

void sync_icache_aliases(unsigned long start, unsigned long end)
{
	if (icache_is_aliasing()) {
		dcache_clean_pou(start, end);
		icache_inval_all_pou();
	} else {
		/*
		 * Don't issue kick_all_cpus_sync() after I-cache invalidation
		 * for user mappings.
		 */
		caches_clean_inval_pou(start, end); // Cache.S 中的汇编实现
	}
}
```

借此机会，我们来详细研究一下 [ARM 的 cache 操作](#../mm/Memory-Hierarchy.md/ARM cache)。

但这里都是刷到 pou 点，跟 DMA 设备访存没关系。

**小结**

- mmap 接口有非常多的 flags，需要逐渐理清楚这些标志位的作用；

### ioremap

ioremap 的变体众多，但底层实现是类似的，都是调用 `__ioremap_caller` 函数，我们来逐个分析。

目前 ioremap 有如下变体（可能还有其他的，遇到了再分析）：

- ioremap 使用场景为映射 device memory 类型内存；
- ioremap_cache，使用场景为映射 normal memory 类型内存，且映射后的虚拟内存支持 cache（但不是所有的系统都实现了）；
- ioremap_wc & ioremap_wt 实现相同，使用场景为映射 normal memory 类型内存，且映射后的虚拟内存不支持 cache，一种是 writecombine，一种是 writethrogh；
- memremap(pbase, size, MEMREMAP_WB)；
- memremap(pbase, size, MEMREMAP_WC)；

```c
// 都是同一个接口，只是配置的属性不同
#define ioremap(addr, size)		__ioremap((addr), (size), __pgprot(PROT_DEVICE_nGnRE))
#define ioremap_wc(addr, size)		__ioremap((addr), (size), __pgprot(PROT_NORMAL_NC))
#define ioremap_np(addr, size)		__ioremap((addr), (size), __pgprot(PROT_DEVICE_nGnRnE))

extern void __iomem *ioremap_cache(phys_addr_t phys_addr, size_t size);
__weak void __iomem *ioremap_cache(resource_size_t offset, unsigned long size)
{
	return ioremap(offset, size);
}

#ifndef ioremap_wt
#define ioremap_wt ioremap
#endif

// memremap 支持多种属性
// MEMREMAP_WB = 1 << 0,
// MEMREMAP_WT = 1 << 1,
// MEMREMAP_WC = 1 << 2,
// MEMREMAP_ENC = 1 << 3,
// MEMREMAP_DEC = 1 << 4,
void *memremap(resource_size_t offset, size_t size, unsigned long flags);
void memunmap(void *addr);
```

从代码上来看，ioremap, ioremap_cache, ioremap_wt 的底层实现都是 ioremap，即将虚拟地址映射为 device memory 类型的内存。而 memremap 也是 ioremap 的封装。

#### memremap

我们先来看一下 memremap 的实现，

```c
/**
 * memremap() - remap an iomem_resource as cacheable memory
 * @offset: iomem resource start address
 * @size: size of remap
 * @flags: any of MEMREMAP_WB, MEMREMAP_WT, MEMREMAP_WC,
 *		  MEMREMAP_ENC, MEMREMAP_DEC
 *
 * memremap() is "ioremap" for cases where it is known that the resource
 * being mapped does not have i/o side effects and the __iomem
 * annotation is not applicable. In the case of multiple flags, the different
 * mapping types will be attempted in the order listed below until one of
 * them succeeds.
 *
 * 也就是说 MEMREMAP_WB 适用于 system RAM, cached 映射
 * MEMREMAP_WB - matches the default mapping for System RAM on
 * the architecture.  This is usually a read-allocate write-back cache.
 * Moreover, if MEMREMAP_WB is specified and the requested remap region is RAM
 * memremap() will bypass establishing a new mapping and instead return
 * a pointer into the direct map.
 *
 * MEMREMAP_WT 不带 cache，映射为 device memory
 * MEMREMAP_WT - establish a mapping whereby writes either bypass the
 * cache or are written through to memory and never exist in a
 * cache-dirty state with respect to program visibility.  Attempts to
 * map System RAM with this mapping type will fail.
 *
 * MEMREMAP_WC 不带 cache，映射为 device memory
 * writecombine 意思是将能够将多笔写操作合并为一个写入 memory 或 buffer
 * MEMREMAP_WC - establish a writecombine mapping, whereby writes may
 * be coalesced together (e.g. in the CPU's write buffers), but is otherwise
 * uncached. Attempts to map System RAM with this mapping type will fail.
 */
void *memremap(resource_size_t offset, size_t size, unsigned long flags)
{
	int is_ram = region_intersects(offset, size,
				       IORESOURCE_SYSTEM_RAM, IORES_DESC_NONE);
	void *addr = NULL;

	if (!flags)
		return NULL;

    // 这些类型不懂，是不是根据 dts 中配置的属性为 device_type = "memory" 来判断这段内存是不是 system RAM
    // 这种内存视图是在哪里构建的呢？
	if (is_ram == REGION_MIXED) {
		WARN_ONCE(1, "memremap attempted on mixed range %pa size: %#lx\n",
				&offset, (unsigned long) size);
		return NULL;
	}

	/* Try all mapping types requested until one returns non-NULL */
	if (flags & MEMREMAP_WB) {
		/*
		 * MEMREMAP_WB is special in that it can be satisfied
		 * from the direct map.  Some archs depend on the
		 * capability of memremap() to autodetect cases where
		 * the requested range is potentially in System RAM.
		 */
		if (is_ram == REGION_INTERSECTS)
			addr = try_ram_remap(offset, size, flags); // 好像大部分情况走这里
		if (!addr)
			addr = arch_memremap_wb(offset, size); // 底层实现是 ioremap_cache
	}

	/*
	 * If we don't have a mapping yet and other request flags are
	 * present then we will be attempting to establish a new virtual
	 * address mapping.  Enforce that this mapping is not aliasing
	 * System RAM.
	 */
    // 如果映射的是 system RAM，但没用 MEMREMAP_WB 就会报错
	if (!addr && is_ram == REGION_INTERSECTS && flags != MEMREMAP_WB) {
		WARN_ONCE(1, "memremap attempted on ram %pa size: %#lx\n",
				&offset, (unsigned long) size);
		return NULL;
	}

    // 从这里可以看出，如果系统没有实现 iormap_wt 和 ioremap_wc
    // 那么其实现还是 ioremap
    // 在本文分析的环境中，是支持 ioremap_wc 的
	if (!addr && (flags & MEMREMAP_WT))
		addr = ioremap_wt(offset, size);

	if (!addr && (flags & MEMREMAP_WC))
		addr = ioremap_wc(offset, size);

	return addr;
}
EXPORT_SYMBOL(memremap);

void memunmap(void *addr)
{
	if (is_ioremap_addr(addr))
		iounmap((void __iomem *) addr);
}
EXPORT_SYMBOL(memunmap);
```

从上面的分析来看，映射最关键的属性有两个：

- device memory&normal memory；

- cache&uncache；

#### __ioremap_caller

我们再来看一下 `__ioremap_caller` 是怎样建立映射以及怎样处理这两种属性。

```c
static void __iomem *__ioremap_caller(phys_addr_t phys_addr, size_t size,
				      pgprot_t prot, void *caller)
{
	unsigned long last_addr;
	unsigned long offset = phys_addr & ~PAGE_MASK;
	int err;
	unsigned long addr;
	struct vm_struct *area;

	... // 映射前的检查

    /*
	 * Don't allow RAM to be mapped.
	 */
    // 在这里检查映射的是否是 RAM 内存
    // 这些 RAM 内存就是在 dts 配置的
    // 所以如果是 DDR 空间使用 ioremap 映射肯定会出问题
	if (WARN_ON(pfn_is_map_memory(__phys_to_pfn(phys_addr))))
		return NULL;

	area = get_vm_area_caller(size, VM_IOREMAP, caller); // 在这里就分配好虚拟地址了，vmalloc 地址范围内分配
	if (!area)
		return NULL;
	addr = (unsigned long)area->addr;
	area->phys_addr = phys_addr;

    // 建立多级页表映射，pgd->p4d->pud->pmd->pte
	err = ioremap_page_range(addr, addr + size, phys_addr, prot);
	if (err) {
		vunmap((void *)addr);
		return NULL;
	}

	return (void __iomem *)(offset + addr); // addr 只是页地址，要加上页内偏移
}
```

最后的 pte 设置是这样的，

```c
/*** Page table manipulation functions ***/
static int vmap_pte_range(pmd_t *pmd, unsigned long addr, unsigned long end,
			phys_addr_t phys_addr, pgprot_t prot,
			unsigned int max_page_shift, pgtbl_mod_mask *mask)
{
	pte_t *pte;
	u64 pfn;
	unsigned long size = PAGE_SIZE;

	pfn = phys_addr >> PAGE_SHIFT;
	pte = pte_alloc_kernel_track(pmd, addr, mask);
	if (!pte)
		return -ENOMEM;
	do {
		...

        // prot 在这里已经写到 pte 中了
        // 然后 set_pte
		set_pte_at(&init_mm, addr, pte, pfn_pte(pfn, prot));
		pfn++;
	} while (pte += PFN_DOWN(size), addr += size, addr != end);
	*mask |= PGTBL_PTE_MODIFIED;
	return 0;
}
```

### 缺页异常处理

前面介绍到 malloc 和 mmap 都是只分配了虚拟地址，但是**没有分配物理内存，也没有建立虚拟地址和物理地址之间的映射**。当进程访问这些还没有建立映射关系的虚拟地址时，CPU 自动触发一个缺页异常。缺页异常的处理是内存管理的重要部分，需要考虑多种情况以及实现细节，包括匿名页面、KSM 页面、页面高速缓存、写时复制（COW）、私有映射和共享映射等等，需要了解各个标志位的意义。这里先看看大概的执行流程，然后再详细分析每种情况的实现。
![page_fault.png](https://github.com/UtopianFuture/UtopianFuture.github.io/blob/master/image/page_fault.png?raw=true)
从触发缺页异常到 CPU 根据中断号跳转到对应的处理函数这个过程在之前做项目时已经跟踪过，就不再分析，这里主要分析 `handle_mm_fault` 相关的处理函数。先来看看调用栈（该调用栈为 ARM64 架构)，

```c
#0 cma_heap_vm_fault (vmf=0xffffffc00afebcd0) at drivers/dma-buf/multi-heap/cma_heap.c:190
#1 0xffffffc0083408e8 in __do_fault (vmf=0xffffffc00afebcd0) at mm/memory.c:3871
#2 0xffffffc008345414 in do_shared_fault (vmf=<optimized out>) at mm/memory.c:4261
// 接下来的处理流程就是根据页面类型来的
#3 do_fault (vmf=<optimized out>) at mm/memory.c:4339
#4 handle_pte_fault (vmf=<optimized out>) at mm/memory.c:4594
#5 __handle_mm_fault (vma=0xffffffc00afebcd0, address=148, flags=177396832) at mm/memory.c:4729
#6 0xffffffc008346124 in handle_mm_fault (vma=0xffffff8005576540, address=547919622144, flags=597, regs=0xffffffc00afebeb0) at mm/memory.c:4827
#7 0xffffffc00804de8c in __do_page_fault (regs=<optimized out>, vm_flags=<optimized out>, mm_flags=<optimized out>, addr=<optimized out>, mm=<optimized out>)
 at arch/arm64/mm/fault.c:500
#8 do_page_fault (far=547919622144, esr=2449473606, regs=0xffffffc00afebeb0) at arch/arm64/mm/fault.c:600
#9 0xffffffc00804e3ac in do_translation_fault (far=18446743799016111312, esr=146815840, regs=0xffffffc00a92b8e0 <__gcov0.arch_local_irq_restore>)
 at arch/arm64/mm/fault.c:681
#10 0xffffffc00804e6f4 in do_mem_abort (far=547919622144, esr=2449473606, regs=0xffffffc00afebeb0) at arch/arm64/mm/fault.c:814
#11 0xffffffc0095ac84c in el0_da (regs=0xffffffc00afebeb0, esr=18446743798978460512) at arch/arm64/kernel/entry-common.c:481
#12 0xffffffc0095ad938 in el0t_64_sync_handler (regs=0xffffffc00afebcd0) at arch/arm64/kernel/entry-common.c:616
#13 0xffffffc0080115cc in el0t_64_sync () at arch/arm64/kernel/entry.S:584
```

#### 关键函数 do_mem_abort

```c
static const struct fault_info fault_info[] = {
	{ do_bad,		SIGKILL, SI_KERNEL,	"ttbr address size fault"	},
	{ do_bad,		SIGKILL, SI_KERNEL,	"level 1 address size fault"	},
	{ do_bad,		SIGKILL, SI_KERNEL,	"level 2 address size fault"	},
	{ do_bad,		SIGKILL, SI_KERNEL,	"level 3 address size fault"	},
	{ do_translation_fault,	SIGSEGV, SEGV_MAPERR,	"level 0 translation fault"	},
	{ do_translation_fault,	SIGSEGV, SEGV_MAPERR,	"level 1 translation fault"	},
	{ do_translation_fault,	SIGSEGV, SEGV_MAPERR,	"level 2 translation fault"	},
    // 若发生在用户态的 translation_fault，则还是调用 do_page_fault
	{ do_translation_fault,	SIGSEGV, SEGV_MAPERR,	"level 3 translation fault"	},
	{ do_page_fault,	SIGSEGV, SEGV_ACCERR,	"level 1 access flag fault"	},
	{ do_page_fault,	SIGSEGV, SEGV_ACCERR,	"level 2 access flag fault"	},
	{ do_page_fault,	SIGSEGV, SEGV_ACCERR,	"level 3 access flag fault"	},

	...

};

static inline const struct fault_info *esr_to_fault_info(unsigned int esr)
{
	return fault_info + (esr & ESR_ELx_FSC); // 这里 fault_info 是全局变量，根据异常类型选择处理函数
}

void do_mem_abort(unsigned long far, unsigned int esr, struct pt_regs *regs)
{
	const struct fault_info *inf = esr_to_fault_info(esr);
	unsigned long addr = untagged_addr(far);

	if (!inf->fn(far, esr, regs))
		return;
	...

}
```

#### 关键函数 do_user_addr_fault

该函数的功能是根据发生异常的 addr 找到对应的 VMA，然后判断 VMA 是否有问题，平时编程地址越界之类的错误应该都是在这里定义的。之后对异常处理的结果进行判断，抛出错误。

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
								   // 而其他的页表不能呢？通过 PTE 才能确定物理页地址
}
```

- 这里 pgd, p4d 等是 5 级页表的名称。五级分页每级命名分别为页全局目录(PGD)、页 4 级目录(P4D)、页上级目录(PUD)、页中间目录(PMD)、页表(PTE)；
- 页表级数增加，页表本身占用的内存就少了；
- 寻址范围不单单要关注 DDR，还要关注整个系统的地址排布，一般要比 DDR 容量大很多；

#### 关键函数 handle_pte_fault

这个函数主要处理各种 VMA，匿名映射，文件映射，VMA 的属性是否可读可写等等。

好多种情况啊！真滴复杂。目前先只分析匿名页面、文件映射和写时复制的缺页中断，其他类型目前的项目用不到，暂时不分析。

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
		if (vma_is_anonymous(vmf->vma)) // 判断是否为匿名映射，当 vma->vm_ops 没有实现方法集时，即为匿名映射
			return do_anonymous_page(vmf); // 后面分析
		else
			return do_fault(vmf); // 不是匿名映射，是文件映射。后面分析
	}

	if (!pte_present(vmf->orig_pte)) // present 为 0，说明页面不在内存中，即真正的缺页，需要从交换区或外存中读取
		return do_swap_page(vmf); // 请求从交换区读回页面

	if (pte_protnone(vmf->orig_pte) && vma_is_accessible(vmf->vma)) // 页面被设置为 NUMA 调度页面（？）
		return do_numa_page(vmf);

	vmf->ptl = pte_lockptr(vmf->vma->vm_mm, vmf->pmd); // 获取进程的内存描述符 mm 中定义的一个自旋锁
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
				vmf->flags & FAULT_FLAG_WRITE)) { // 判断 PTE 是否发生变化，如果发生变化就需要将新的内容
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

`do_anonymous_page` 匿名页面缺页的处理。**没有关联对应文件的映射称为匿名映射**。

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

	if (pte_alloc(vma->vm_mm, vmf->pmd)) // 分配一个 PTE 并将其设置到对应的 PMD 页表项中
		return VM_FAULT_OOM;

	/* See comment in handle_pte_fault() */
	if (unlikely(pmd_trans_unstable(vmf->pmd)))
		return 0;

	/* Use the zero-page for reads */
    // 为什么要这样设置？
	if (!(vmf->flags & FAULT_FLAG_WRITE) && // 该匿名页面只有只读属性
			!mm_forbids_zeropage(vma->vm_mm)) {
		entry = pte_mkspecial(pfn_pte(my_zero_pfn(vmf->address), // 将 PTE 设置为指向系统零页
						vma->vm_page_prot));// pte_mkspecial 设置 PTE 中的 PTE_SPECIAL
		vmf->pte = pte_offset_map_lock(vma->vm_mm, vmf->pmd, // 重新获取 PTE 确保正确的写入了
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
	if (unlikely(anon_vma_prepare(vma))) // RMAP 初始化
		goto oom;
	page = alloc_zeroed_user_highpage_movable(vma, vmf->address); // 分配物理页面，会调用到 alloc_pages_vma
	if (!page) // page 就是分配的物理页面
		goto oom;

	...

    // 基于分配的物理页面设置新的 PTE，这里就是建立物理地址和虚拟地址的映射关系
	entry = mk_pte(page, vma->vm_page_prot);
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
	page_add_new_anon_rmap(page, vma, vmf->address, false); // 添加到 RMAP 系统中，后面有介绍
	lru_cache_add_inactive_or_unevictable(page, vma); // 将匿名页面添加到 LRU 链表中
setpte:
    // 设置到硬件页表中。这就很好理解了，
    // 正常的 page table walk 是硬件来做的。但是当发生异常，
    // 就需要软件遍历、构造、写入到硬件，之后硬件就可以正常使用了
	set_pte_at(vma->vm_mm, vmf->address, vmf->pte, entry);
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

- `alloc_zeroed_user_highpage_movable` 的执行流程如下：

  ```c
  #define alloc_zeroed_user_highpage_movable(vma, vaddr) \
  	alloc_page_vma(GFP_HIGHUSER_MOVABLE | __GFP_ZERO, vma, vaddr)

  #define alloc_page_vma(gfp_mask, vma, addr)			\
  	alloc_pages_vma(gfp_mask, 0, vma, addr, numa_node_id(), false)

  // alloc_pages_vma - Allocate a page for a VMA.
  struct page *alloc_pages_vma(gfp_t gfp, int order, struct vm_area_struct *vma,
  		unsigned long addr, int node, bool hugepage)
  {
  	...

  	page = __alloc_pages(gfp, order, preferred_nid, nmask);
  	...

  }
  ```

  即最后调用伙伴系统的 `__alloc_pages`，这是上文分析过的。

#### 文件映射缺页中断

内核用 `do_fault` 来处理文件映射的缺页中断。这个中断处理不仅仅是下面几个函数简单的处理 PTE，寻找物理页面那么简单，之后需要进一步分析。

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

   	// map_pages 函数可以在缺页异常地址附近提前建立尽可能多的和页面高速缓存的映射，这样可以减少发生缺页异常的次数，
       // 但其只是建立映射，而没有建立页面高速缓存（这个页面高速缓存是不是就是 cache），
       // 下面的 __do_fault 才是分配物理页面。
       // 那这个做法是不是就是之前学过的根据空间局部性原理将发生异常的地址周围的数据也加载到 cache 中，
       // 减少缺页次数。应该就是这个意思。
       // fault_around_bytes 是全局变量，默认为 16 个页面
   	if (vma->vm_ops->map_pages && fault_around_bytes >> PAGE_SHIFT > 1) {
   		if (likely(!userfaultfd_minor(vmf->vma))) {
               // do_fault_around 以当前缺页异常地址为中心，尽量取 16 个页面对齐的地址，
               // 通过调用 map_pages 为这些页面建立 PTE 映射，当然这个函数需要完成寻找物理地址的工作
               // map_pages 对应的回调函数很多，这里只举一个例子：filemap_map_pages，后面分析
   			ret = do_fault_around(vmf);
   			if (ret)
   				return ret;
   		}
   	}

   	ret = __do_fault(vmf); // 通过调用 vma->vm_ops->fault(vmf); 将文件内容读取到 page
   	if (unlikely(ret & (VM_FAULT_ERROR | VM_FAULT_NOPAGE | VM_FAULT_RETRY)))
   		return ret;

   	ret |= finish_fault(vmf); // 取回缺页异常对应的物理页面，调用 do_set_pte 建立物理页面和虚拟地址的映射关系
   	unlock_page(vmf->page);
   	if (unlikely(ret & (VM_FAULT_ERROR | VM_FAULT_NOPAGE | VM_FAULT_RETRY)))
   		put_page(vmf->page);
   	return ret;
   }
   ```

   - `__do_fault` 分配对应的物理内存（考虑到上面的 map_pages，这里应该会分配多个物理页面）

     ```c
     static vm_fault_t __do_fault(struct vm_fault *vmf)
     {
     	struct vm_area_struct *vma = vmf->vma;
     	vm_fault_t ret;

     	/*
     	 * Preallocate pte before we take page_lock because this might lead to
     	 * deadlocks for memcg reclaim which waits for pages under writeback:
     	 *				lock_page(A)
     	 *				SetPageWriteback(A)
     	 *				unlock_page(A)
     	 * lock_page(B)
     	 *				lock_page(B)
     	 * pte_alloc_one
     	 *   shrink_page_list
     	 *     wait_on_page_writeback(A)
     	 *				SetPageWriteback(B)
     	 *				unlock_page(B)
     	 *				# flush A, B to clear the writeback
     	 */
     	if (pmd_none(*vmf->pmd) && !vmf->prealloc_pte) {
     		vmf->prealloc_pte = pte_alloc_one(vma->vm_mm);
     		if (!vmf->prealloc_pte)
     			return VM_FAULT_OOM;
     		smp_wmb(); /* See comment in __pte_alloc() */
     	}

     	ret = vma->vm_ops->fault(vmf);
     	if (unlikely(ret & (VM_FAULT_ERROR | VM_FAULT_NOPAGE | VM_FAULT_RETRY |
     			    VM_FAULT_DONE_COW)))
     		return ret;

     	...

     	return ret;
     }
     ```

   - `filemap_map_pages` 感觉寻址这块还可以继续分析，而且对理解系统很有帮助。

     ```c
     vm_fault_t filemap_map_pages(struct vm_fault *vmf,
     			     pgoff_t start_pgoff, pgoff_t end_pgoff)
     {
     	struct vm_area_struct *vma = vmf->vma;
     	struct file *file = vma->vm_file; // 该 vma 对应的 file
     	struct address_space *mapping = file->f_mapping;
     	pgoff_t last_pgoff = start_pgoff;
     	unsigned long addr;
     	XA_STATE(xas, &mapping->i_pages, start_pgoff);
     	struct page *head, *page;
     	unsigned int mmap_miss = READ_ONCE(file->f_ra.mmap_miss);
     	vm_fault_t ret = 0;

     	rcu_read_lock();
     	head = first_map_page(mapping, &xas, end_pgoff); // 该 file 映射到内存的第一个 page 地址？
     	if (!head)
     		goto out;

     	if (filemap_map_pmd(vmf, head)) {
     		ret = VM_FAULT_NOPAGE;
     		goto out;
     	}

     	addr = vma->vm_start + ((start_pgoff - vma->vm_pgoff) << PAGE_SHIFT); // 虚拟地址
     	vmf->pte = pte_offset_map_lock(vma->vm_mm, vmf->pmd, addr, &vmf->ptl);
     	do {
     		page = find_subpage(head, xas.xa_index); // 找到对应的 page
     		if (PageHWPoison(page))
     			goto unlock;

     		if (mmap_miss > 0)
     			mmap_miss--;

     		addr += (xas.xa_index - last_pgoff) << PAGE_SHIFT; // 加上该 page 的偏移量
     		vmf->pte += xas.xa_index - last_pgoff; // pte 也要更新为该 page 对应的
     		last_pgoff = xas.xa_index;

     		if (!pte_none(*vmf->pte))
     			goto unlock;

     		/* We're about to handle the fault */
     		if (vmf->address == addr)
     			ret = VM_FAULT_NOPAGE;

     		do_set_pte(vmf, page, addr); // 建立映射
     		/* no need to invalidate: a not-present page won't be cached */
     		update_mmu_cache(vma, addr, vmf->pte);
     		unlock_page(head);
     		continue;
     unlock:
     		unlock_page(head);
     		put_page(head);
     	} while ((head = next_map_page(mapping, &xas, end_pgoff)) != NULL);
     	pte_unmap_unlock(vmf->pte, vmf->ptl);
     out:
     	rcu_read_unlock();
     	WRITE_ONCE(file->f_ra.mmap_miss, mmap_miss);
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

当进程试图修改只有只读属性的页面时，CPU 触发异常，在 `do_wp_page` 中尝试处理异常，通常的做法是**分配一个新的页面并且复制据页面内容到新的页面中，这个新分配的页面具有可写属性**。

`do_wp_page` 处理非常多且非常复杂的情况，如页面可以分成特殊页面、单身匿名页面、非单身匿名页面、KSM 页面以及文件映射页面等等，这里只了解 COW 的原理，具体实现先不做分析，之后有需要再分析。

### RMAP

一个物理页面可以被多个进程的虚拟内存通过 PTE 映射。有的页面需要迁移，有的页面长时间不用需要交换到磁盘，在交换之前，必须找出哪些进程使用这个页面，然后解除这些映射的用户 PTE。

在 2.4 版本的内核中，为了确定某个页面是否被某个进程映射，必须遍历每个进程的页表，因此效率很低，在 2.5 版本的内核中，提出了反向映射（Reverse Mapping）的概念。

RMAP 的主要目的是**从物理页面的 page 数据结构中找到有哪些用户进程的 PTE**，这样就可以快速解除所有的 PTE 并回收这个页面，为了实现这一需求，RMAP 机制使用 `anon_vma` 和 `anon_vma_chain` 结构体构造了一个非常复杂的结构。为何要设计的这么复杂，能否直接在 page 中加入一个链表，存储所有的 PTE。粗略的看，占用空间太多？是的，在 2.6 内核使用过 pte_chain 方案，但是它需要在 struct page 中内置一个成员，当时系统是 32 位的，内存小，page 中加一个成员变量对内存占用大。

#### 数据结构

##### anon_vma

其主要**用于连接物理页面的 page 数据结构和 VMA**，这个数据结构和 VMA 有相似之处啊。

```c
struct anon_vma {
	struct anon_vma *root;		/* Root of this anon_vma tree */
	struct rw_semaphore rwsem;	/* W: modification, R: walking the list */ // 互斥信号
	atomic_t refcount; // 引用计数
	unsigned degree;
	struct anon_vma *parent;	/* Parent of this anon_vma */
	/* Interval tree of private "related" vmas */
	struct rb_root_cached rb_root; // 所有相关的 avc 都会插入到该红黑树
};
```

##### anon_vma_chain

其可以连接父子进程的 `anon_vma` 数据结构。

```c
struct anon_vma_chain {
	struct vm_area_struct *vma; // 可以指向父进程的 VMA，也可以指向子进程的 VMA
	struct anon_vma *anon_vma; // 可以指向父进程的 anon_vma，也可以指向子进程的 anon_vma
	struct list_head same_vma; /* locked by mmap_lock & page_table_lock */ // 链表
	struct rb_node rb;			/* locked by anon_vma->rwsem */ // 红黑树
	unsigned long rb_subtree_last;
#ifdef CONFIG_DEBUG_VM_RB
	unsigned long cached_vma_start, cached_vma_last;
#endif
};
```

##### vm_area_struct

```c
struct vm_area_struct {
	...

	struct rb_node vm_rb;
	struct mm_struct *vm_mm;	/* The address space we belong to. */
	struct list_head anon_vma_chain; /* Serialized by mmap_lock & page_table_lock */
    // page->mapping while point to the pointer
	struct anon_vma *anon_vma;	/* Serialized by page_table_lock */

	...

} __randomize_layout;
```

##### page

```c
struct page {
	union {
		struct {	/* Page cache and anonymous pages */
			/**
			 * @lru: Pageout list, eg. active_list protected by
			 * lruvec->lru_lock. Sometimes used as a generic list
			 * by the page owner.
			 */
			struct list_head lru;
			/* See page-flags.h for PAGE_MAPPING_FLAGS */
            // point to anon_vma if it's is anonymous page, point to address_space if it's file page
            // point to vma->anon_vma
			struct address_space *mapping;
			pgoff_t index;		/* Our offset within mapping. */
			unsigned long private;
		};
	};

 ...

	/* Usage count. *DO NOT USE DIRECTLY*. See page_ref.h */
	atomic_t _refcount;

	...

} _struct_page_alignment;
```

我们从进程的创建过程来分析 RMAP 机制如何建立起一个能够快速找到所有使用了该 page 的进程的结构。

#### 父进程产生匿名页面

一般来说，一个进程的地址空间内不会把两个虚拟地址 mapping 到一个 page frame 上去，如果有多个 mapping，那么多半是这个 page 被多个进程共享。我们从一个 page 第一次被映射来分析，最简单的例子就是采用 COW 的进程 fork，在进程没有写的动作之前，内核是不会分配新的 page frame 的，因此父子进程共享一个物理页面。当子进程对这个 page 产生一个写请求时，会触发一个匿名/文件页的缺页中断，分配一个新的 page，并建立映射，下面我们以匿名页为例来分析这种情况。

在上文分析[匿名页面缺页中断](#匿名页面缺页中断)的关键函数 `do_anonymous_page` 中涉及到 RMAP 的两个重要函数：`anon_vma_prepare` 和 `page_add_new_anon_rmap`，下面来详细分析一下这两个函数。

```c
| do_anonymous_page
| 	-> anon_vma_prepare // 建立下图的结构
|		-> __anon_vma_prepare
| 			-> anon_vma_chain_link
| 	-> page_add_new_anon_rmap // page->mapping == anon_vma
```

##### do_anonymous_page

```c
static vm_fault_t do_anonymous_page(struct vm_fault *vmf)
{
	...

	/* Allocate our own private page. */
    // 处理 VMA 属性为可写的情况
	if (unlikely(anon_vma_prepare(vma))) // RMAP 初始化
		goto oom;

    // 分配物理页面，会调用到 alloc_pages_vma
	page = alloc_zeroed_user_highpage_movable(vma, vmf->address);

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
	page_add_new_anon_rmap(page, vma, vmf->address, false); // 将 page->mapping 指向 av
	lru_cache_add_inactive_or_unevictable(page, vma); // 将匿名页面添加到 LRU 链表中

     ...

}
```

##### anon_vma_prepare

`anon_vma_prepare` -> `__anon_vma_prepare`，这个函数功能很简单，就是为 VMA 分配一个 `anon_vma`，同时创建一个新的 `anon_vma_chain_alloc`。

```c
int __anon_vma_prepare(struct vm_area_struct *vma)
{
	struct mm_struct *mm = vma->vm_mm;
	struct anon_vma *anon_vma, *allocated;
	struct anon_vma_chain *avc;

	might_sleep();

	avc = anon_vma_chain_alloc(GFP_KERNEL); // 通过 slab 分配机制分配 avc
	anon_vma = find_mergeable_anon_vma(vma); // 寻找是否有可以复用的 av
	allocated = NULL;
	if (!anon_vma) { // 没有的话分配一个新的 av
		anon_vma = anon_vma_alloc(); // 同样通过 slab 机制分配 av
		if (unlikely(!anon_vma))
			goto out_enomem_free_avc;
		allocated = anon_vma;
	}

	anon_vma_lock_write(anon_vma);

	/* page_table_lock to protect against threads */
	spin_lock(&mm->page_table_lock); // 自旋锁
	if (likely(!vma->anon_vma)) {
		vma->anon_vma = anon_vma; // 首先进行赋值
		anon_vma_chain_link(vma, avc, anon_vma); // 看下面的代码
		/* vma reference or self-parent link for new root */
		anon_vma->degree++;
		allocated = NULL;
		avc = NULL;
	}
	spin_unlock(&mm->page_table_lock);
	anon_vma_unlock_write(anon_vma);

	...

}
```

##### anon_vma_chain_link

这个函数是建立 AVC 连接关系的关键函数，注意入参分别表示什么。

```c
static void anon_vma_chain_link(struct vm_area_struct *vma,
				struct anon_vma_chain *avc,
				struct anon_vma *anon_vma)
{
	avc->vma = vma; // 指向自身的 vma
	avc->anon_vma = anon_vma; // 指向父进程的 av
	list_add(&avc->same_vma, &vma->anon_vma_chain); // 将 avc 添加到 vma 的 avc 链表中
	anon_vma_interval_tree_insert(avc, &anon_vma->rb_root); // 将 avc 插入到 av 内部的红黑树中
}
```

##### page_add_new_anon_rmap

`page_add_new_anon_rmap`->`__page_set_anon_rmap`，注意，到这里已经通过 `alloc_zeroed_user_highpage_movable` 为该匿名页面缺页中断分配了物理页面 page。

```c
static void __page_set_anon_rmap(struct page *page,
	struct vm_area_struct *vma, unsigned long address, int exclusive)
{
	struct anon_vma *anon_vma = vma->anon_vma;

	...

    // anon_vma 指针的值加上 PAGE_MAPPING_ANON 后将指针的值赋给 page->mapping
    // 前面分析 page 数据结构的时候提到 mapping 表示页面所指向的地址空间
    // 内核中的地址空间通常有两个不同的地址空间，一个用于文件映射页面，如在读取文件时，
    // 地址空间用于将文件的内容与存储介质区关联起来；另一个用于匿名映射
    // mapping 的低 2 位用于判断指向匿名映射或 KSM 页面的地址空间（？），
    // 如果 mapping 成员中的第 0 位不为 0，
    // 那么 mapping 成员指向匿名页面的地址空间数据结构 anon_vma（好吧，没懂）
    anon_vma = (void *) anon_vma + PAGE_MAPPING_ANON;

    // 在这里将 page->mapping 指向 vma->anon_vma
    WRITE_ONCE(page->mapping, (struct address_space *) anon_vma);

    // 计算 address 在 vma 的第几个 page，然后将其设置到 page->index，
    // 这个成员变量之前提到过，但也不懂
	page->index = linear_page_index(vma, address);
}
```

我们以图的方式来直观的展示这种结构，VMA-P0 表示父进程的 VMA 结构体，`AVC-P0` 表示父进程的 AVC 结构体。

![img](https://raw.githubusercontent.com/UtopianFuture/UtopianFuture.github.io/81305767754ca8df95d18da10465dd730bc093be/image/RMAP-parent-process.svg)

#### 根据父进程创建子进程

父进程通过 fork 系统调用创建子进程时，**子进程会复制父进程的 VMA 数据结构**，并且会复制父进程的 PTE 内容到子进程的页表中，以实现父子进程共享页表，这样，多个子进程的虚拟页面就会映射到同一个物理页面。那么在创建子进程时就需要在父进程的 `anon_vma` 和 `anon_vma_chain` 结构的基础上进行延申，以便通过父进程的 rb_root 指针就可以快速找到所有使用该 page 的进程。

创建子进程的过程很复杂，`SYSCALL_DEFINE0(fork)` -> `kernel_clone` -> `copy_process`（这个函数非常复杂） -> `copy_mm` -> `dup_mm` -> `dup_mmap`，我们这里只关注地址空间部分。`dup_mmap` 可以极大的加深对进程地址空间管理的理解，但是也很复杂，这里只分析 RMAP 相关的部分。

```c
| dup_mmap
| 	-> anon_vma_fork // for each vma in mm_struct
| 		-> anon_vma_clone // for each avc in vma, attach the new VMA to the parent VMA's anon_vmas
| 			-> anon_vma_chain_link
|		-> anon_vma_alloc // alloc own anon_vma
| 		-> anon_vma_chain_alloc
| 		-> anon_vma_chain_link
```

我们来一步步看各个进程之间是怎样建立联系的。

##### dup_mmap

```c
static __latent_entropy int dup_mmap(struct mm_struct *mm,
					struct mm_struct *oldmm)
{
	struct vm_area_struct *mpnt, *tmp, *prev, **pprev;
	struct rb_node **rb_link, *rb_parent;
	int retval;
	unsigned long charge;
	LIST_HEAD(uf);

	...

    // 遍历父进程所有的 VMA
	for (mpnt = oldmm->mmap; mpnt; mpnt = mpnt->vm_next) {

         ....

        // 复制父进程 VMA 到 tmp
		tmp = vm_area_dup(mpnt);
		tmp->vm_mm = mm;
        // 为子进程创建相应的 anon_vma 数据结构
        anon_vma_fork(tmp, mpnt);
        // 把 tmp 添加到子进程的红黑树中
        __vma_link_rb(mm, tmp, rb_link, rb_parent);
        // 复制父进程的 PTE 到子进程页表中
        copy_page_range(mm, oldmm, mpnt);
	}

    ...

}
```

##### anon_vma_fork

该函数首先会调用 `anon_vma_clone`，将子进程的 VMA 绑定到 `anon_vma` 中，然后创建自己的 `anon_vma`。

```c
int anon_vma_fork(struct vm_area_struct *vma, struct vm_area_struct *pvma) // 子进程 VMA，父进程 VMA
{
	struct anon_vma_chain *avc;
	struct anon_vma *anon_vma;
	int error;

	/* Drop inherited anon_vma, we'll reuse existing or allocate new. */
	vma->anon_vma = NULL; // 因为复制了父进程的 VMA，所以这个 anon_vma 应该是父进程的

	/*
	 * First, attach the new VMA to the parent VMA's anon_vmas,
	 * so rmap can find non-COWed pages in child processes.
	 */
	error = anon_vma_clone(vma, pvma); // 将子进程的 VMA 绑定到父进程的 VMA 对应的 anon_vma 中

	/* An existing anon_vma has been reused, all done then. */
	if (vma->anon_vma) // 绑定完成，如果没有成功，那么就和创建父进程的 av 是一样的
		return 0;

    ...

}
```

##### anon_vma_clone

这里是创建一个”枢纽 avc“，**该 avc 会插入到*父进程每一个 avc->anon_vma->rb_root 中***，同时会插入到该 vma 的 anon_vma_chain 中。

```c
int anon_vma_clone(struct vm_area_struct *dst, struct vm_area_struct *src) // 子进程、父进程
{
	struct anon_vma_chain *avc, *pavc;
	struct anon_vma *root = NULL;
    // 遍历父进程 VMA 中的 avc 链表寻找 avc 实例，即在上面 __anon_vma_prepare 中创建的 avc
	list_for_each_entry_reverse(pavc, &src->anon_vma_chain, same_vma) {
		struct anon_vma *anon_vma;
    	// 创建一个新的 avc，这里称为 avc 枢纽
		avc = anon_vma_chain_alloc(GFP_NOWAIT | __GFP_NOWARN);
 ...

		anon_vma = pavc->anon_vma;
		root = lock_anon_vma_root(root, anon_vma);
    	// 将 avc 枢纽挂入子进程 VMA 的 avc 链表中，
   		// 同时将 avc 枢纽添加到父进程 anon_vma->rb_root 红黑树中，
    	// 使子进程和父进程的 VMA 之间有一个纽带 avc
		anon_vma_chain_link(dst, avc, anon_vma);
		/*
		 * Reuse existing anon_vma if its degree lower than two,
		 * that means it has no vma and only one anon_vma child.
		 *
		 * Do not chose parent anon_vma, otherwise first child
		 * will always reuse it. Root anon_vma is never reused:
		 * it has self-parent reference and at least one child.
		 */
		if (!dst->anon_vma && src->anon_vma &&
		 anon_vma != src->anon_vma && anon_vma->degree < 2)
			dst->anon_vma = anon_vma;
	}
	if (dst->anon_vma)
		dst->anon_vma->degree++;
	unlock_anon_vma_root(root);
	return 0;
 ...

}
```

在使用该 vma 对应的枢纽 anon_vma_chain 建立好该 vma 与各个父进程的 anon_vma 之间的联系之后，还需要构建 vma 自身的 av。

```c
int anon_vma_fork(struct vm_area_struct *vma, struct vm_area_struct *pvma) // 子进程 VMA，父进程 VMA
{
	struct anon_vma_chain *avc;
	struct anon_vma *anon_vma;
	int error;

	...

	/* Then add our own anon_vma. */
	anon_vma = anon_vma_alloc();
	avc = anon_vma_chain_alloc(GFP_KERNEL);

	/*
	 * The root anon_vma's rwsem is the lock actually used when we
	 * lock any of the anon_vmas in this anon_vma tree.
	 */
	anon_vma->root = pvma->anon_vma->root;
	anon_vma->parent = pvma->anon_vma;

	/*
	 * With refcounts, an anon_vma can stay around longer than the
	 * process it belongs to. The root anon_vma needs to be pinned until
	 * this anon_vma is freed, because the lock lives in the root.
	 */
	get_anon_vma(anon_vma->root);

	/* Mark this anon_vma as the one where our new (COWed) pages go. */
	vma->anon_vma = anon_vma; // 组装子进程的 av
	anon_vma_lock_write(anon_vma);

    // 将 avc 挂入子进程的 vma->avc 链表和红黑树中
	anon_vma_chain_link(vma, avc, anon_vma); // 这个过程和父进程是一样的
	anon_vma->parent->degree++;
	anon_vma_unlock_write(anon_vma);

	return 0;
}
```

还是以图的方式来展示这一结构，`VMA-P1` 表示子进程的 VMA 结构体。创建子进程时会遍历父进程 P0 的 anon_vma_chain 链表，然后根据 AVC-P0 构建“枢纽 AVC-link_01"。“枢纽 AVC-link_01"会插入到 P0 的 `AV-P0->rb_root` 指向的红黑树中。

![img](https://raw.githubusercontent.com/UtopianFuture/UtopianFuture.github.io/81305767754ca8df95d18da10465dd730bc093be/image/RMAP-child-process.svg)



#### 子进程创建孙进程

我们在创建子进程的基础上进一步创建孙进程，构建整个 RMAP 管理结构。创建的过程是一样的，这里主要关注”枢纽 AVC“是怎样连接”父子进程“、”父孙进程”。

`AVC-P1` 表示父进程本身，`AVC-P2` 表示孙进程，创建孙进程会遍历 `VMA-P1->anon_vma_chain` 链表，然后对每一个节点都会创建一个“枢纽 AVC”。对于 `AVC-P1` 节点，创建 AVC_link_12，因为 `AVC-P1->anon_vma = AV-P1`，所以 AVC_link_12 会插入到 `AV-P1->rb_root` 指向的红黑树中。对于 `AVC-link_01` 节点，创建 AVC_link_02，因为 `AVC-link_01->anon_vma = AV-P0`，所以 AVC_link_02 会插入到 `AV-P0->rb_root` 指向的红黑树中。

![img](https://raw.githubusercontent.com/UtopianFuture/UtopianFuture.github.io/81305767754ca8df95d18da10465dd730bc093be/image/RMAP-grandson-process.svg)

当需要找到某个 page 对应的所有 VMA 时，只需要**通过 `page->mapping` 找到父进程的 av 结构，然后扫描 av 指向的 av 红黑树**。而子进程的的 VMA 通过插入到父进程的枢纽 avc 也可以遍历到，如通过 `AV-P0->rb_root` 指向的红黑树可以找到所有共享该 page 的子进程，这样就可以快速解除所有进程的映射。这里是每个 VMA 都有对应的 av，所以不要遍历所有的 VMA。

#### RMAP 的应用

RMAP 的典型应用场景如下：
- kswapd 内核线程为了回收页面，需要断开所有映射到该匿名页面的用户 PTE。
- 页面迁移时，需要断开所有映射到匿名页面的用户 PTE。

##### try_to_unmap

RMAP 的核心函数是 `try_to_unmap`，内核中其他模块会调用此函数来断开一个 page 的所有引用。

```c
void try_to_unmap(struct page *page, enum ttu_flags flags)
{
    // rwc 结构体能够根据应用场景配置不同的回调函数
    // 在 shrink_list 中，配置 page_referenced_one，用来计算 page->_mapcount
    // 然后决定是否要移到其他 lru 链表上
    // 这里是配置成 unmap，在内存回收时使用
	struct rmap_walk_control rwc = { // 统一管理 unmap 操作
		.rmap_one = try_to_unmap_one, // 断开某个 VMA 上映射的 PTE
		.arg = (void *)flags,
		.done = page_not_mapped, // 判断一个 page 是否成功断开
		.anon_lock = page_lock_anon_vma_read,
	};

	if (flags & TTU_RMAP_LOCKED)
		rmap_walk_locked(page, &rwc);
	else
		rmap_walk(page, &rwc); // 这两个函数都是调用 rmap_walk_anon 和 rmap_walk_file
}
```

内核中有 3 种页面需要做 unmap 操作，即 KSM 页面、匿名页面和文件映射页面，这一点 `rmap_walk` 很清晰。

```c
void rmap_walk(struct page *page, struct rmap_walk_control *rwc)
{
	if (unlikely(PageKsm(page)))
		rmap_walk_ksm(page, rwc);
	else if (PageAnon(page)) // 判断该 page 是否为匿名页面，就是通过上文讲的 mapping 的最后一位来判断
        // 匿名页持有 down_read(&anon_vma->root->rwsem); 锁
		rmap_walk_anon(page, rwc, false);
	else
        // 文件页持有 down_read(&mapping->i_mmap_rwsem); 锁，确保 rb_root 中的数据是正确的
		rmap_walk_file(page, rwc, false);
}
```

这里只分析 `rmap_walk_anon`，即匿名页面怎样断开所有的 PTE，

```c
static void rmap_walk_anon(struct page *page, struct rmap_walk_control *rwc,
		bool locked)
{
	struct anon_vma *anon_vma;
	pgoff_t pgoff_start, pgoff_end;
	struct anon_vma_chain *avc;

    // 这里会调用 down_read(&anon_vma->root->rwsem);
    // 持有读者锁，由于整个 RMAP 过程耗时，所以此处持有锁可能会导致其他内核路径等待锁
	if (locked) {
		anon_vma = page_anon_vma(page); // page->mapping

		/* anon_vma disappear under us? */
		VM_BUG_ON_PAGE(!anon_vma, page);
	} else {
		anon_vma = rmap_walk_anon_lock(page, rwc);

	if (!anon_vma)
		return;

	pgoff_start = page_to_pgoff(page);
	pgoff_end = pgoff_start + thp_nr_pages(page) - 1;

    // 遍历 anon_vma->rb_root 中的 avc，从 avc 中可以得到对应的 vma
    // 如果该 page->_mapcount 很大的话，那么 anon_vma->rb_root 中会由很多个节点
    // 遍历整颗树是个耗时的操作
	anon_vma_interval_tree_foreach(avc, &anon_vma->rb_root,
			pgoff_start, pgoff_end) {
		struct vm_area_struct *vma = avc->vma;
		unsigned long address = vma_address(page, vma);
		VM_BUG_ON_VMA(address == -EFAULT, vma);
		cond_resched();
		if (rwc->invalid_vma && rwc->invalid_vma(vma, rwc->arg))
			continue;

        // 解除用户 PTE，在这里回调函数为 try_to_unmap_one
        // 有一个进程解除失败就取消解映射
		if (!rwc->rmap_one(page, vma, address, rwc->arg))
			break;

        // 检查该 page 是否还有映射
		if (rwc->done && rwc->done(page))
			break;
	}

	if (!locked)
		anon_vma_unlock_read(anon_vma); // 在遍历完后 unlock
}
```

##### try_to_unmap_one

在 vma 中解除对应的 page 成功返回 true，否则返回 false。

```c
/*
 * @arg: enum ttu_flags will be passed to this argument
 */
static bool try_to_unmap_one(struct page *page, struct vm_area_struct *vma,
		 unsigned long address, void *arg)
{
	struct mm_struct *mm = vma->vm_mm;
	struct page_vma_mapped_walk pvmw = {
		.page = page,
		.vma = vma,
		.address = address,
	};
	pte_t pteval;
	struct page *subpage;
	bool ret = true;
	struct mmu_notifier_range range;
	enum ttu_flags flags = (enum ttu_flags)(long)arg;

    ...

	while (page_vma_mapped_walk(&pvmw)) { // 检查 page 是否在 vma 管理范围内

    ...

		subpage = page - page_to_pfn(page) + pte_pfn(*pvmw.pte);
		address = pvmw.address;

		...

		/* Update high watermark before we lower rss */
		update_hiwater_rss(mm);
		if (PageHWPoison(page) && !(flags & TTU_IGNORE_HWPOISON)) {

            ...

		} else if (PageAnon(page)) {
			swp_entry_t entry = { .val = page_private(subpage) };
			pte_t swp_pte;

			/*
			 * Store the swap location in the pte.
			 * See handle_pte_fault() ...
			 */
			if (unlikely(PageSwapBacked(page) != PageSwapCache(page))) {
				WARN_ON_ONCE(1);
				ret = false;

				/* We have to invalidate as we cleared the pte */
				mmu_notifier_invalidate_range(mm, address,
							address + PAGE_SIZE);
				page_vma_mapped_walk_done(&pvmw);
				break;
			}

            ...

			dec_mm_counter(mm, MM_ANONPAGES);
			inc_mm_counter(mm, MM_SWAPENTS);
			swp_pte = swp_entry_to_pte(entry);
			if (pte_soft_dirty(pteval))
				swp_pte = pte_swp_mksoft_dirty(swp_pte);
			if (pte_uffd_wp(pteval))
				swp_pte = pte_swp_mkuffd_wp(swp_pte);

            // 将配置好的 pte 写入到页表中
			set_pte_at(mm, address, pvmw.pte, swp_pte);

			/* Invalidate as we cleared the pte */
			mmu_notifier_invalidate_range(mm, address,
						 address + PAGE_SIZE);
		} else {

            ...

		}

discard:
		/*
		 * No need to call mmu_notifier_invalidate_range() it has be
		 * done above for all cases requiring it to happen under page
		 * table lock before mmu_notifier_invalidate_range_end()
		 *
		 * See Documentation/vm/mmu_notifier.rst
		 */
        // 对 page->_mapcount 减 1，当 _mapcount 为 -1 时，表示此页已经没有页表项映射了
		page_remove_rmap(subpage, PageHuge(page));
		put_page(page);
	}

	return ret;
}
```

### MGLRU

内核在 6.1 版本用 MGLRU(Multi generation LRU)替换了 LRU。LRU 回收是内存回收的基础，这里只分析 MGLRU 的实现，其和内存回收的接口下一章节再分析。

提出 MGLRU 替换 LRU 的原因在于：
> - 当前 LRU 主要是匿名页和文件页的 active lru 和 inactive lru，决策太粗糙；
> - Page 经常被放到不合适的 LRU 链表上，尤其是在使用 cgroup 后各 cgroup 独立 LRU，很难比较不同 cgroup lru 间页面的冷热程度（为何要比较不同 cgroup lru 的冷热程度呢，cgroup 设计的初衷不就是资源隔离么。那 mglru 是怎样处理不同 cgroup lru 呢）；
> - 它可以在文件页和匿名页内部进行页面活跃程度的排序，但它**难以评估文件页和匿名页之间谁更冷谁更热**；
> - 内核长期以来**偏向于优先回收文件页**，这可能会导致一些包含有用的可执行文件的页面被回收而空闲无用的匿名页仍在内存中；
> - 内存回收扫描成本高。在页表回收时需要通过反向映射（RMAP）找到所有的 PTE，再通过 PTE 中 reference bit 识别页面是否是 young（如果硬件访问过这个页面，reference bit 会被置位，页被标记为 young）。整个过程非常漫长，而且切换进程页表导致的 cache miss 更是让性能成本变更加高；
> MGLRU 的核心思想在于：
> 将内存分为若干个 bucket，称为 "generations （世代）"。一个 page 的 generation 值就反映了它的 "年龄"，即距离该 page 被最后一次访问有多长时间了。这些 page 的管理是由 "两个表针的时钟" 的机制完成的。aging 这个指标会扫描 page 的 accessed bit，看它们自上次扫描以来是否被使用过。被使用过的 page 就被标记好等待搬移到最年轻的一个 generation（这个操作在哪里做的）。eviction 指标则实际上会将 page 移到正确的 generation；那些最终进入最古老的 generation 的 page 是最少访问到的，可以考虑进行回收；

#### lru_gen_folio

该结构体为 mglru 的核心数据结构，

```c
/*
 * The youngest generation number is stored in max_seq for both anon and file
 * types as they are aged on an equal footing. The oldest generation numbers are
 * stored in min_seq[] separately for anon and file types as clean file pages
 * can be evicted regardless of swap constraints.
 *
 * Normally anon and file min_seq are in sync. But if swapping is constrained,
 * e.g., out of swap space, file min_seq is allowed to advance and leave anon
 * min_seq behind.
 *
 * The number of pages in each generation is eventually consistent and therefore
 * can be transiently negative when reset_batch_size() is pending.
 */
struct lru_gen_folio {
	/* the aging increments the youngest generation number */
	unsigned long max_seq;
	/* the eviction increments the oldest generation numbers */
	unsigned long min_seq[ANON_AND_FILE];
	/* the birth time of each generation in jiffies */
	unsigned long timestamps[MAX_NR_GENS];
	/* the multi-gen LRU lists, lazily sorted on eviction */
    // max_seq 和 min_seq 是不断增长的
    // 而 folios 只有 4 * 2 * 3 个元素
	struct list_head folios[MAX_NR_GENS][ANON_AND_FILE][MAX_NR_ZONES];
	/* the multi-gen LRU sizes, eventually consistent */
	long nr_pages[MAX_NR_GENS][ANON_AND_FILE][MAX_NR_ZONES];
	/* the exponential moving average of refaulted */
	unsigned long avg_refaulted[ANON_AND_FILE][MAX_NR_TIERS];
	/* the exponential moving average of evicted+protected */
	unsigned long avg_total[ANON_AND_FILE][MAX_NR_TIERS];
	/* the first tier doesn't need protection, hence the minus one */
	unsigned long protected[NR_HIST_GENS][ANON_AND_FILE][MAX_NR_TIERS - 1];
	/* can be modified without holding the LRU lock */
	atomic_long_t evicted[NR_HIST_GENS][ANON_AND_FILE][MAX_NR_TIERS];
	atomic_long_t refaulted[NR_HIST_GENS][ANON_AND_FILE][MAX_NR_TIERS];
	/* whether the multi-gen LRU is enabled */
	bool enabled;
#ifdef CONFIG_MEMCG
	/* the memcg generation this lru_gen_folio belongs to */
	u8 gen;
	/* the list segment this lru_gen_folio belongs to */
	u8 seg;
	/* per-node lru_gen_folio list for global reclaim */
	struct hlist_nulls_node list;
#endif
	ANDROID_KABI_RESERVE(1);
	ANDROID_KABI_RESERVE(2);
};
```

#### ctrl_ops

这个貌似也是 mglru 的核心设计思想，没懂。

```c
/******************************************************************************
 * PID controller
 ******************************************************************************/
/*
 * A feedback loop based on Proportional-Integral-Derivative (PID) controller.
 *
 * The P term is refaulted/(evicted+protected) from a tier in the generation
 * currently being evicted; the I term is the exponential moving average of the
 * P term over the generations previously evicted, using the smoothing factor
 * 1/2; the D term isn't supported.
 *
 * The setpoint (SP) is always the first tier of one type; the process variable
 * (PV) is either any tier of the other type or any other tier of the same
 * type.
 *
 * The error is the difference between the SP and the PV; the correction is to
 * turn off protection when SP>PV or turn on protection when SP<PV.
 *
 * For future optimizations:
 * 1. The D term may discount the other two terms over time so that long-lived
 * generations can resist stale information.
 */
struct ctrl_pos {
	unsigned long refaulted;
	unsigned long total;
	int gain;
};
```

内核在如下函数中进行了拦截调用 `lru_gen_enabled`，从这些函数中可以一窥 MGLRU 的实现（这种拦截方式未来开发过程中也是可以参考的）。
- memcontrol.c: mem_cgroup_update_tree, mem_cgroup_soft_limit_reclaim;
- rmap.c: folio_referenced_one;
- swap.c: folio_mark_accessed, **folio_add_lru**, lru_deactivate_fn, folio_deactivate;
- vmscan.c: **shrink_folio_list**, prepare_scan_count, lru_gen_change_state, lru_gen_init_lruvec, shrink_lruvec, **shrink_node**, snapshot_refaults, kswapd_age_node;
- workingset.c: workingset_eviction, workingset_test_recent, workingset_refault;

既然该机制是用来回收内存的，那么可以从申请/释放两个方面来分析。

#### 申请

申请内存的时候会按照如下步骤将申请到的 folio 添加到 lrugen 中。在 page fault 和 migrate 中路径中都会调用该函数。

```c
| folio_add_lru
| 	-> folio_batch_add_and_move
|		// 为了防止频繁的申请 lruvec->lru_lock 锁(spinlock)，先将 folio 写入到 folio_batch 中
|		// 相当于 per-cpu cache，等 cache 满了(PAGEVEC_SIZE)再写入到全局的 lruvec 中
|		// 6.6 上 PAGEVEC_SIZE 为 15，也就说可以放 15 个 folio
|		// 这个 cache 其实就是一个 folio 指针数组
|		-> folio_batch_add
| 		-> folio_batch_move_lru // fbatch 满了，将其中的每个 folio 写入到 lruvec 中
| 			-> lru_add_fn
|				-> lruvec_add_folio
| 					-> lru_gen_add_folio
| 					-> update_lru_size
| 					-> list_add
```

`lru_gen_add_folio` 是将 folio 添加到 `lruvec->lrugen` 中的核心函数，

##### lru_gen_add_folio

该函数的功能是计算传入的 folio 的 gen, type, zone，然后 `list_add(&folio->lru, &lrugen->folios[gen][type][zone]);`；

```c
static inline bool lru_gen_add_folio(struct lruvec *lruvec, struct folio *folio, bool reclaiming)
{
	unsigned long seq;
	unsigned long flags;
	int gen = folio_lru_gen(folio);
	int type = folio_is_file_lru(folio);
	int zone = folio_zonenum(folio);
	struct lru_gen_folio *lrugen = &lruvec->lrugen;

	VM_WARN_ON_ONCE_FOLIO(gen != -1, folio);

	if (folio_test_unevictable(folio) || !lrugen->enabled)
		return false;

	/*
	 * There are four common cases for this page:
	 * 1. If it's hot, i.e., freshly faulted in, add it to the youngest
	 * generation, and it's protected over the rest below.
	 * 2. If it can't be evicted immediately, i.e., a dirty page pending
	 * writeback, add it to the second youngest generation.
	 * 3. If it should be evicted first, e.g., cold and clean from
	 * folio_rotate_reclaimable(), add it to the oldest generation.
	 * 4. Everything else falls between 2 & 3 above and is added to the
	 * second oldest generation if it's considered inactive, or the
	 * oldest generation otherwise. See lru_gen_is_active().
	 */
    // 这 4 种情况注释解释的很清楚
    // 还是要结合使用场景来理解这些情况
    // folio_test_xxx 类函数都是读取 folio_flag
	if (folio_test_active(folio))
		seq = lrugen->max_seq;
	else if ((type == LRU_GEN_ANON && !folio_test_swapcache(folio)) ||
		 (folio_test_reclaim(folio) &&
		 (folio_test_dirty(folio) || folio_test_writeback(folio))))
		seq = lrugen->max_seq - 1;
	else if (reclaiming || lrugen->min_seq[type] + MIN_NR_GENS >= lrugen->max_seq)
		seq = lrugen->min_seq[type];
	else
		seq = lrugen->min_seq[type] + 1;

	gen = lru_gen_from_seq(seq);
	flags = (gen + 1UL) << LRU_GEN_PGOFF;
	/* see the comment on MIN_NR_GENS about PG_active */
	set_mask_bits(&folio->flags, LRU_GEN_MASK | BIT(PG_active), flags);

    // 更新统计信息
	lru_gen_update_size(lruvec, folio, -1, gen);

	/* for folio_rotate_reclaimable() */
	if (reclaiming)
		list_add_tail(&folio->lru, &lrugen->folios[gen][type][zone]);
	else
		list_add(&folio->lru, &lrugen->folios[gen][type][zone]); // 这就很好理解了

	return true;
}
```

申请的逻辑很简单，先写入到 fbatch 中，如果 fbatch 满了，那么计算 gen, type, zone，写入到对应 lrugen->folios 中。

#### 回收

`shrink_node` 是直接内存回收的关键函数，调用 `shrink_node` 的路径在[触发页面回收](#触发页面回收)。我们从这里开始分析，先看看整体架构。

``` c
| shrink_node
| 	-> lru_gen_enabled&root_reclaim
| 	-> (Y)lru_gen_shrink_node
|		// 在回收之前需要将 per-cpu cache 即 fbatch 中的 folio
|		// 调用 folio_batch_move_lru 写回到 lruvec->lrugen 中
|		// 如果没有写回会有问题么
|		-> lru_add_drain
|		-> set_mm_walk // 这里面的 lru_gen_mm_walk 是做什么的
|		-> set_initial_priority
|		-> shrink_one/shrink_many
|			-> try_to_shrink_lruvec
|				// 是否需要老化是根据每个 gen 中的 folio 数量解决的
|				// 是否需要回收 folio 是根据所有 gen 中的 total folio 数量是否大于 sc->priority 决定的
|				-> get_nr_to_scan
|					// 根据每个链表中的 page 数量以及 min_seq 和 max_seq 之间的距离,
|					// 判断是否要进行 generation 老化
|					-> should_run_aging
|					-> try_to_inc_max_seq // generation 转换
|						-> should_walk_mmu // 检查 AF 标志位
|						// lru_gen_mm_list 是用来记录该 memcg 中所有 mm 的
|						// 如果没有使能 memcg，那么就 mm_list 就只有一个 mm
|						// 在 fork 的时候会将 mm 添加到 mm_list 中
|						// 该函数会遍历 mm_list 中所有的 mm
|						// 但是没看出来画这么大的功夫遍历 pte 目的是啥
|						-> iterate_mm_list
|						-> walk_mm
|						-> inc_max_seq
|				// 老化完再回收
|				// 这里会构造一个 list，将需要回收的 folios 添加到该 list 中的
|				// 用 gdb 调试的时候，申请匿名页把内存占满也没有触发 evict_folios，为啥
|				-> evict_folios
|					-> isolate_folios
|						// 根据 swapiness 决定回收文件页还是匿名页
|						// 回收的类型也跟 tier 有关
|						-> get_type_to_scan
|							// 读取 refaulted 率和 tier
|							-> read_ctrl_pos
|						// 在该函数中调用 list_add 将 folio 添加到 list 中
|						-> scan_folios
|							// 对不同种类的 folios 调用 lru_gen_del_folio
|							// 和 lruvec_add_folio 重排
|							-> sort_folio
|							// 进行必要的检查就调用 lru_gen_del_folio
|							// 会将 referenced 和 reclaimed 位都清掉
|							-> isolate_folio
|								-> lru_gen_del_folio // 将 folio 从 lrugen 中删除
|					-> try_to_inc_min_seq // 回收完 min 链表后看是否需要增加计数
|					// 遍历上面构造的 folio_list，针对各个 folio 的情况（PG_swapbacked, PG_swapbacked 等等）
|					// 修改 folio 的状态，然后回收
|					-> shrink_folio_list
|				-> should_abort_scan // 检查水位是否 OK，决定是否结束回收
|			-> shrink_slab // 遍历 shrinker_list，执行所有组件注册的 shrink 函数
|				-> do_shrink_slab
|	-> (N)shrink_node_memcgs
|	-> flush_reclaim_state
|	-> should_continue_reclaim
```

总结一下，整个回收操作可以分配几步：
- 检查 lru_gen 的max_seq 和 min_seq 的代差以及其中的 folios 数量是否符合要求；
- 遍历页表（？）
- 将需要回收的 folios 隔离出来，添加到一个 list 中；
- 对 list 中的 folios 进行回收；
- 执行 `shrink_slab`，调用各个组件注册的 shrink 回调函数；

```c
static void shrink_node(pg_data_t *pgdat, struct scan_control *sc)
{
	unsigned long nr_reclaimed, nr_scanned, nr_node_reclaimed;
	struct lruvec *target_lruvec;
	bool reclaimable = false;

	if (lru_gen_enabled() && root_reclaim(sc)) {
		lru_gen_shrink_node(pgdat, sc); // 需要对比一下该函数和 shrink_node 有什么区别
		return;
	}

	...

}
```

##### 关键函数 try_to_shrink_lruvec

```c
static bool try_to_shrink_lruvec(struct lruvec *lruvec, struct scan_control *sc)
{
	long nr_to_scan;
	unsigned long scanned = 0;
	int swappiness = get_swappiness(lruvec, sc); // 控制匿名页和文件页回收的比例？没搞懂咋控制的

	/* clean file folios are more likely to exist */
	if (swappiness && !(sc->gfp_mask & __GFP_IO))
		swappiness = 1;
	while (true) {
		int delta;

        // nr_to_scan 可为 -1，0，遍历 lrugen 中每个链表的 folio 的数量
        // 其有什么作用呢
		nr_to_scan = get_nr_to_scan(lruvec, sc, swappiness);
		if (nr_to_scan <= 0)
			break;

        // 从 lruvec->lrugen 中回收的关键逻辑
		delta = evict_folios(lruvec, sc, swappiness);
		if (!delta)
			break;

        // 回收的 > 扫描的？这个命名有点奇怪啊
		scanned += delta;
		if (scanned >= nr_to_scan)
			break;

        // 是否应该终止回收，条件有哪些
		if (should_abort_scan(lruvec, sc))
			break;
		cond_resched();
	}

	/* whether this lruvec should be rotated */
	return nr_to_scan < 0;
}
```

##### 关键函数 get_nr_to_scan

```c
static long get_nr_to_scan(struct lruvec *lruvec, struct scan_control *sc, bool can_swap)
{
	unsigned long nr_to_scan;

    // mem_cgroup 中有 mem_cgroup_per_node 变量
    // mem_cgroup_per_node 中有 struct lruvec *
    // 通过 container_of 得到 mem_cgroup_per_node 指针
	struct mem_cgroup *memcg = lruvec_memcg(lruvec);
	DEFINE_MAX_SEQ(lruvec);

    // memcg 不懂啊
	if (mem_cgroup_below_min(sc->target_mem_cgroup, memcg))
		return -1;

    // nr_to_scan 就表示扫描的页面数量
    // 而这个扫描的页面数量是 lruvec->lrugen 中的总的 page 数量
    // 或者是总的 page 数量右移 sc->priority 位
    // 没懂
	if (!should_run_aging(lruvec, max_seq, sc, can_swap, &nr_to_scan))
		return nr_to_scan;

	/* skip the aging path at the default priority */
	if (sc->priority == DEF_PRIORITY)
		return nr_to_scan;

	/* skip this lruvec as it's low on cold folios */
    // nr_to_scan 为 -1/0 的话，本次 shrink_one 就直接返回，相当于没有执行回收操作
	return try_to_inc_max_seq(lruvec, max_seq, sc, can_swap, false) ? -1 : 0;
}
```

##### 关键函数 should_run_aging

根据当前各个 generation 的状态判断是否要进行老化，即增加 max_seq，

```c
static bool should_run_aging(struct lruvec *lruvec, unsigned long max_seq,
			 struct scan_control *sc, bool can_swap, unsigned long *nr_to_scan)
{
	int gen, type, zone;
	unsigned long old = 0;
	unsigned long young = 0;
	unsigned long total = 0;
	struct lru_gen_folio *lrugen = &lruvec->lrugen;
	struct mem_cgroup *memcg = lruvec_memcg(lruvec);
	DEFINE_MIN_SEQ(lruvec);

	/* whether this lruvec is completely out of cold folios */
    // 如果最老的 gen 和最新的 gen 不足 MIN_NR_GENS（没有 cold folios?）
    // 那么说明需要老化，后续会在 try_to_inc_max_seq 中增加 max_seq
    // 同时该 node 不需要做回收
	if (min_seq[!can_swap] + MIN_NR_GENS > max_seq) {
		*nr_to_scan = 0;
		return true;
	}

	for (type = !can_swap; type < ANON_AND_FILE; type++) {
		unsigned long seq;
		for (seq = min_seq[type]; seq <= max_seq; seq++) {
			unsigned long size = 0;
			gen = lru_gen_from_seq(seq);
			for (zone = 0; zone < MAX_NR_ZONES; zone++)
				size += max(READ_ONCE(lrugen->nr_pages[gen][type][zone]), 0L);
			total += size; // 表示 lrugen 中总的 page 数量
			if (seq == max_seq)
				young += size; // 表示最新的 gen 中的 page 数量
			else if (seq + MIN_NR_GENS == max_seq)
				old += size; // 表示最老的 gen 中的 page 数量
		}
	}

	/* try to scrape all its memory if this memcg was deleted */
	if (!mem_cgroup_online(memcg)) {
		*nr_to_scan = total;
		return false;
	}
    // 为什么要做这个偏移呢
	*nr_to_scan = total >> sc->priority;

	/*
	 * The aging tries to be lazy to reduce the overhead, while the eviction
	 * stalls when the number of generations reaches MIN_NR_GENS. Hence, the
	 * ideal number of generations is MIN_NR_GENS+1.
	 */
    // gen 是足够的，不需要老化
	if (min_seq[!can_swap] + MIN_NR_GENS < max_seq)
		return false;

	/*
	 * It's also ideal to spread pages out evenly, i.e., 1/(MIN_NR_GENS+1)
	 * of the total number of pages for each generation. A reasonable range
	 * for this average portion is [1/MIN_NR_GENS, 1/(MIN_NR_GENS+2)]. The
	 * aging cares about the upper bound of hot pages, while the eviction
	 * cares about the lower bound of cold pages.
	 */
    // 最新的 gen 中的 page 数量太多也要老化
	if (young * MIN_NR_GENS > total)
		return true;
    // 最老的 gen 中的 page 数量太少也要老化
    // 看来 mglru 的设计思想就是各个 gen 的 folio 数量达到一个均衡的状态
	if (old * (MIN_NR_GENS + 2) < total)
		return true;
	return false;
}
```

##### 关键函数 try_to_inc_max_seq

前面判断了需要进行老化，看一下是怎样进行老化的。这里又涉及到另外两个结构体，`lru_gen_mm_list` 和 `lru_gen_mm_state`。

老化势必涉及到 folio 的迁移，这个怎么做的

```c
static bool try_to_inc_max_seq(struct lruvec *lruvec, unsigned long max_seq,
			 struct scan_control *sc, bool can_swap, bool force_scan)
{
	bool success;
	struct lru_gen_mm_walk *walk;
	struct mm_struct *mm = NULL;
	struct lru_gen_folio *lrugen = &lruvec->lrugen;

	VM_WARN_ON_ONCE(max_seq > READ_ONCE(lrugen->max_seq));
	/* see the comment in iterate_mm_list() */
	if (max_seq <= READ_ONCE(lruvec->mm_state.seq)) {
		success = false;
		goto done;
	}

	/*
	 * If the hardware doesn't automatically set the accessed bit, fallback
	 * to lru_gen_look_around(), which only clears the accessed bit in a
	 * handful of PTEs. Spreading the work out over a period of time usually
	 * is less efficient, but it avoids bursty page faults.
	 */
    // 这里会检查 AF 表示位
    // 没有使能 ARM64_HW_AFDBM 或者 AF 标志位没有置上
    // 都不会遍历页表
    // 不遍历页表就执行 iterate_mm_list_nowalk，否则执行 iterate_mm_list
    // 为什么要有这样的区别呢
	if (!should_walk_mmu()) {
		success = iterate_mm_list_nowalk(lruvec, max_seq); // 这个函数属实没看懂
		goto done;
	}
	walk = set_mm_walk(NULL, true);
	if (!walk) {
		success = iterate_mm_list_nowalk(lruvec, max_seq);
		goto done;
	}

    // 这个 walk 是用来干嘛的
	walk->lruvec = lruvec;
	walk->max_seq = max_seq;
	walk->can_swap = can_swap;
	walk->force_scan = force_scan;
	do {
    	// 该函数不断获取 mm，walk_mm 去遍历
    	// 涉及到 lru_gen_mm_state，没搞懂
		success = iterate_mm_list(lruvec, walk, &mm);
		if (mm)
			walk_mm(lruvec, mm, walk); // 这个函数也很复杂，没懂
	} while (mm);
done:
	if (success)
    // 从命名来看，这里只是增加 max_seq 的
    // 但从代码实现来看，这里会将 min_seq 和 max_seq 都 +1
    // 然后更新 lru 的统计信息
		inc_max_seq(lruvec, can_swap, force_scan);
	return success;
}
```

总的来说逻辑很简单，遍历 lruvec->mm_state，然后执行 walk_mm 函数，执行完后就 `inc_max_seq`。

##### 关键函数 walk_mm

为什么要执行该函数呢？

```c
static void walk_mm(struct lruvec *lruvec, struct mm_struct *mm, struct lru_gen_mm_walk *walk)
{
	static const struct mm_walk_ops mm_walk_ops = {
		.test_walk = should_skip_vma,
		.p4d_entry = walk_pud_range,
		.walk_lock = PGWALK_RDLOCK,
	};
	int err;
	struct mem_cgroup *memcg = lruvec_memcg(lruvec);

	walk->next_addr = FIRST_USER_ADDRESS;
	do {
		DEFINE_MAX_SEQ(lruvec);
		err = -EBUSY;
		/* another thread might have called inc_max_seq() */
		if (walk->max_seq != max_seq)
			break;
		/* folio_update_gen() requires stable folio_memcg() */
		if (!mem_cgroup_trylock_pages(memcg))
			break;
		/* the caller might be holding the lock for write */
		if (mmap_read_trylock(mm)) {
			err = walk_page_range(mm, walk->next_addr, ULONG_MAX, &mm_walk_ops, walk);
			mmap_read_unlock(mm);
		}
		mem_cgroup_unlock_pages();
		if (walk->batched) {
			spin_lock_irq(&lruvec->lru_lock);
			reset_batch_size(lruvec, walk);
			spin_unlock_irq(&lruvec->lru_lock);
		}
		cond_resched();
	} while (err == -EAGAIN);
}
```

##### 关键函数 evict_folios

这里面哪个函数是做回收的？

```c
static int evict_folios(struct lruvec *lruvec, struct scan_control *sc, int swappiness)
{
	int type;
	int scanned;
	int reclaimed;
	LIST_HEAD(list);
	LIST_HEAD(clean);
	struct folio *folio;
	struct folio *next;
	enum vm_event_item item;
	struct reclaim_stat stat;
	struct lru_gen_mm_walk *walk;
	bool skip_retry = false;
	struct mem_cgroup *memcg = lruvec_memcg(lruvec);
	struct pglist_data *pgdat = lruvec_pgdat(lruvec);

	spin_lock_irq(&lruvec->lru_lock);

    // 计算好哪些 folio 可以回收
	scanned = isolate_folios(lruvec, sc, swappiness, &type, &list);
	scanned += try_to_inc_min_seq(lruvec, swappiness);
	if (get_nr_gens(lruvec, !swappiness) == MIN_NR_GENS)
		scanned = 0;
	spin_unlock_irq(&lruvec->lru_lock);
	if (list_empty(&list))
		return scanned;

retry:
	reclaimed = shrink_folio_list(&list, pgdat, sc, &stat, false);
	sc->nr_reclaimed += reclaimed; // 这个参数有啥用
	list_for_each_entry_safe_reverse(folio, next, &list, lru) {
		if (!folio_evictable(folio)) {
			list_del(&folio->lru);
			folio_putback_lru(folio);
			continue;
		}

		if (folio_test_reclaim(folio) &&
		 (folio_test_dirty(folio) || folio_test_writeback(folio))) {
			/* restore LRU_REFS_FLAGS cleared by isolate_folio() */
			if (folio_test_workingset(folio))
				folio_set_referenced(folio);
			continue;
		}

		if (skip_retry || folio_test_active(folio) || folio_test_referenced(folio) ||
		 folio_mapped(folio) || folio_test_locked(folio) ||
		 folio_test_dirty(folio) || folio_test_writeback(folio)) {
			/* don't add rejected folios to the oldest generation */
			set_mask_bits(&folio->flags, LRU_REFS_MASK | LRU_REFS_FLAGS,
				 BIT(PG_active));
			continue;
		}

		/* retry folios that may have missed folio_rotate_reclaimable() */
		list_move(&folio->lru, &clean);
		sc->nr_scanned -= folio_nr_pages(folio);
	}
	spin_lock_irq(&lruvec->lru_lock);
	move_folios_to_lru(lruvec, &list);
	walk = current->reclaim_state->mm_walk;
	if (walk && walk->batched)
		reset_batch_size(lruvec, walk);
	...

	spin_unlock_irq(&lruvec->lru_lock);
	...

	return scanned;
}
```

##### 关键函数 isolate_folios

swappiness数值越大，Swap 的积极程度越高，越倾向回收匿名页；swappiness数值越小，Swap 的积极程度越低，越倾向回收文件页。

```c
static int isolate_folios(struct lruvec *lruvec, struct scan_control *sc, int swappiness,
			 int *type_scanned, struct list_head *list)
{
	int i;
	int type;
	int scanned;
	int tier = -1;
	DEFINE_MIN_SEQ(lruvec);

	/*
	 * Try to make the obvious choice first. When anon and file are both
	 * available from the same generation, interpret swappiness 1 as file
	 * first and 200 as anon first.
	 */
    // 原来 swappiness 是在这里控制回收文件页还是匿名页
	if (!swappiness) // 为 0 就全回收文件页
		type = LRU_GEN_FILE;
	else if (min_seq[LRU_GEN_ANON] < min_seq[LRU_GEN_FILE]) // 匿名页的老化速度慢了，加快一点
		type = LRU_GEN_ANON;
	else if (swappiness == 1) // 1 也是全回收文件页
		type = LRU_GEN_FILE;
	else if (swappiness == 200) // 这个值在哪里配置呢
		type = LRU_GEN_ANON;
	else
		type = get_type_to_scan(lruvec, swappiness, &tier); // 这里计算就复杂了，下次再分析
	for (i = !swappiness; i < ANON_AND_FILE; i++) {
		if (tier < 0)
			tier = get_tier_idx(lruvec, type);
    	// 根据上面确定的 type，这里会遍历该 type 的 min 数组
    	// 并将能够 isolate 的 folio 添加到 list 中
    	// 判断能否 isolate 的条件很复杂
    	// 从这里就能明白，回收只发生在 min_seq 中
		scanned = scan_folios(lruvec, sc, type, tier, list);
		if (scanned)
			break;
		type = !type;
		tier = -1;
	}
	*type_scanned = type;
	return scanned;
}
```

##### 关键函数 scan_folios

```c
static int scan_folios(struct lruvec *lruvec, struct scan_control *sc,
		 int type, int tier, struct list_head *list)
{
	int i;
	int gen;
	enum vm_event_item item;
	int sorted = 0;
	int scanned = 0;
	int isolated = 0;
	int remaining = MAX_LRU_BATCH;
	struct lru_gen_folio *lrugen = &lruvec->lrugen;
	struct mem_cgroup *memcg = lruvec_memcg(lruvec);

	if (get_nr_gens(lruvec, type) == MIN_NR_GENS)
		return 0;
	gen = lru_gen_from_seq(lrugen->min_seq[type]);

    // 遍历所有的链表
	for (i = MAX_NR_ZONES; i > 0; i--) {
		LIST_HEAD(moved);
		int skipped = 0;
		int zone = (sc->reclaim_idx + i) % MAX_NR_ZONES;
		struct list_head *head = &lrugen->folios[gen][type][zone];
		while (!list_empty(head)) {
			struct folio *folio = lru_to_folio(head);
			int delta = folio_nr_pages(folio);
			scanned += delta;

			// 如果不符合回收标准，只是调整一下
			if (sort_folio(lruvec, folio, sc, tier))
				sorted += delta;

			// 将 folios 从原来的链表中删除
			// 添加到 list 中
			else if (isolate_folio(lruvec, folio, sc)) {
				list_add(&folio->lru, list);
				isolated += delta;
			} else {
			// 这个不知道什么意思
				list_move(&folio->lru, &moved);
				skipped += delta;
			}
			if (!--remaining || max(isolated, skipped) >= MIN_LRU_BATCH)
				break;
		}

		if (skipped) {
			list_splice(&moved, head);
			__count_zid_vm_events(PGSCAN_SKIP, zone, skipped);
		}

		if (!remaining || isolated >= MIN_LRU_BATCH)
			break;
	}
	...

	return isolated || !remaining ? scanned : 0;
}
```

##### 关键函数 shrink_folio_list

这个函数本质上还是链表操作，难就难在内存的复杂性，导致要处理多种多样的 folio 情况。

需要深入了解各种 folio 的使用场景及标志位的判断，暂时还做不到。

```c
/*
 * shrink_folio_list() returns the number of reclaimed pages
 */
static unsigned int shrink_folio_list(struct list_head *folio_list,
		struct pglist_data *pgdat, struct scan_control *sc,
		struct reclaim_stat *stat, bool ignore_references)
{
	LIST_HEAD(ret_folios);
	LIST_HEAD(free_folios);
	LIST_HEAD(demote_folios);
	unsigned int nr_reclaimed = 0;
	unsigned int pgactivate = 0;
	bool do_demote_pass;
	struct swap_iocb *plug = NULL;

	memset(stat, 0, sizeof(*stat));
	cond_resched();
	do_demote_pass = can_demote(pgdat->node_id, sc);

retry:
    // 整体就是这个循环
    // folio_list 是前面构造的可以回收的 folio
    // 直到所有的 folio 都 delete 掉，才结束循环
	while (!list_empty(folio_list)) {
		struct address_space *mapping;
		struct folio *folio;
		enum folio_references references = FOLIOREF_RECLAIM;
		bool dirty, writeback;
		unsigned int nr_pages;

		cond_resched();

		// 内核的链表操作也要学习一下
		folio = lru_to_folio(folio_list);
		list_del(&folio->lru);
		if (!folio_trylock(folio))
			goto keep;

		// folio_test_active 是怎样跳转到 PAGEFLAG(Active, active, PF_HEAD) 宏的
		// 走到这个流程的不能是 active folio
		VM_BUG_ON_FOLIO(folio_test_active(folio), folio);
		nr_pages = folio_nr_pages(folio);

		/* Account the number of base pages */
		sc->nr_scanned += nr_pages;

    	// 这里做了很多条件检查
		// folio 是 unevictable 有两种情况：
		// folio mapping 被标志为 unevictable，这种情况很少
		// folio 中的某个 page 在 mlocked VMA 中
		if (unlikely(!folio_evictable(folio)))
			goto activate_locked;

		// mapcount >= 0，说明用户还在使用，不能释放
		if (!sc->may_unmap && folio_mapped(folio))
			goto keep_locked;

		// 从代码来看，和上面的没去啊
		// 注释没看懂，folio_update_gen 和这里有什么关系
		/* folio_update_gen() tried to promote this page? */
		if (lru_gen_enabled() && !ignore_references &&
		 folio_mapped(folio) && folio_test_referenced(folio))
			goto keep_locked;

		/*
		 * The number of dirty pages determines if a node is marked
		 * reclaim_congested. kswapd will stall and start writing
		 * folios if the tail of the LRU is all dirty unqueued folios.
		 */
		 // 匿名页没有 dirty 和 writeback 之分
		 // 都是检查 PAGEFLAG
		 // 10 多种 pageflag，要考虑全面得多难
		folio_check_dirty_writeback(folio, &dirty, &writeback);
		if (dirty || writeback)
			stat->nr_dirty += nr_pages;

		// 是 dirtry，但是不需要写回，什么情况下会这样
		if (dirty && !writeback)
			stat->nr_unqueued_dirty += nr_pages;

		/*
		 * Treat this folio as congested if folios are cycling
		 * through the LRU so quickly that the folios marked
		 * for immediate reclaim are making it to the end of
		 * the LRU a second time.
		 */
		if (writeback && folio_test_reclaim(folio))
			stat->nr_congested += nr_pages;

		/*
		 * If a folio at the tail of the LRU is under writeback, there
		 * are three cases to consider.
		 *
		 * 这些细节考验功力
		 * 1) If reclaim is encountering an excessive number
		 * of folios under writeback and this folio has both
		 * the writeback and reclaim flags set, then it
		 * indicates that folios are being queued for I/O but
		 * are being recycled through the LRU before the I/O
		 * can complete. Waiting on the folio itself risks an
		 * indefinite stall if it is impossible to writeback
		 * the folio due to I/O error or disconnected storage
		 * so instead note that the LRU is being scanned too
		 * quickly and the caller can stall after the folio
		 * list has been processed.
		 *
		 * 2) Global or new memcg reclaim encounters a folio that is
		 * not marked for immediate reclaim, or the caller does not
		 * have __GFP_FS (or __GFP_IO if it's simply going to swap,
		 * not to fs). In this case mark the folio for immediate
		 * reclaim and continue scanning.
		 *
		 * Require may_enter_fs() because we would wait on fs, which
		 * may not have submitted I/O yet. And the loop driver might
		 * enter reclaim, and deadlock if it waits on a folio for
		 * which it is needed to do the write (loop masks off
		 * __GFP_IO|__GFP_FS for this reason); but more thought
		 * would probably show more reasons.
		 *
		 * 3) Legacy memcg encounters a folio that already has the
		 * reclaim flag set. memcg does not have any dirty folio
		 * throttling so we could easily OOM just because too many
		 * folios are in writeback and there is nothing else to
		 * reclaim. Wait for the writeback to complete.
		 *
		 * In cases 1) and 2) we activate the folios to get them out of
		 * the way while we continue scanning for clean folios on the
		 * inactive list and refilling from the active list. The
		 * observation here is that waiting for disk writes is more
		 * expensive than potentially causing reloads down the line.
		 * Since they're marked for immediate reclaim, they won't put
		 * memory pressure on the cache working set any longer than it
		 * takes to write them to disk.
		 */
		 // 这里通过 PAGEFLAG 宏来检查非常常见，需要搞懂是怎样检查的
		 // 这些 bit 是在 pte 里面的么
		 // PG_writeback 该页正在写入磁盘
		if (folio_test_writeback(folio)) {
			/* Case 1 above */
			// 立刻回收
			if (current_is_kswapd() &&
			 folio_test_reclaim(folio) &&
			 test_bit(PGDAT_WRITEBACK, &pgdat->flags)) {
				stat->nr_immediate += nr_pages;
				goto activate_locked;
			/* Case 2 above */
			} else if (writeback_throttling_sane(sc) ||
			 !folio_test_reclaim(folio) ||
			 !may_enter_fs(folio, sc->gfp_mask)) {
				/*
				 * This is slightly racy -
				 * folio_end_writeback() might have
				 * just cleared the reclaim flag, then
				 * setting the reclaim flag here ends up
				 * interpreted as the readahead flag - but
				 * that does not matter enough to care.
				 * What we do want is for this folio to
				 * have the reclaim flag set next time
				 * memcg reclaim reaches the tests above,
				 * so it will then wait for writeback to
				 * avoid OOM; and it's also appropriate
				 * in global reclaim.
				 */
				folio_set_reclaim(folio);
				stat->nr_writeback += nr_pages;
				goto activate_locked;
			/* Case 3 above */
			} else {
				folio_unlock(folio);
				folio_wait_writeback(folio);
				/* then go back and try same folio again */
				list_add_tail(&folio->lru, folio_list);
				continue;
			}
		}

    	// 这里会进行 look_around 操作
    	// 这个 mglru 一个很重要的特性
    	// 需要调用 rmap 的接口，计算该 folio 的引用次数
    	// 将 PG_reference 标志位去掉
    	// 最后会调用到 lru_gen_look_around->folio_activate
    	// 将该 folio 周围的几个 folios 进行 promote
    	// 这里 promote 只是更改 flag，实际移动 folio 要等到下一次 evict_folios->shrink_folio_list
		if (!ignore_references)
			references = folio_check_references(folio, sc);
		switch (references) {
		case FOLIOREF_ACTIVATE:
			goto activate_locked;
		case FOLIOREF_KEEP:
			stat->nr_ref_keep += nr_pages;
			goto keep_locked;
		case FOLIOREF_RECLAIM:
		case FOLIOREF_RECLAIM_CLEAN:
			; /* try to reclaim the folio below */
		}

		/*
		 * Before reclaiming the folio, try to relocate
		 * its contents to another node.
		 */
		if (do_demote_pass &&
		 (thp_migration_supported() || !folio_test_large(folio))) {
			list_add(&folio->lru, &demote_folios);
			folio_unlock(folio);
			continue;
		}

        /*
		 * Anonymous process memory has backing store?
		 * Try to allocate it some swap space here.
		 * Lazyfree folio could be freed directly
		 */
		if (folio_test_anon(folio) && folio_test_swapbacked(folio)) {
			if (!folio_test_swapcache(folio)) {
				if (!(sc->gfp_mask & __GFP_IO))
					goto keep_locked;
				if (folio_maybe_dma_pinned(folio))
					goto keep_locked;
				if (folio_test_large(folio)) {
					/* cannot split folio, skip it */
					if (!can_split_folio(folio, NULL))
						goto activate_locked;
					/*
					 * Split partially mapped folios right away.
					 * We can free the unmapped pages without IO.
					 */
					if (data_race(!list_empty(&folio->_deferred_list)) &&
					 split_folio_to_list(folio, folio_list))
						goto activate_locked;
				}

    			// 将该 page 加入到 swapcache 中
				if (!add_to_swap(folio)) {
					int __maybe_unused order = folio_order(folio);
					if (!folio_test_large(folio))
						goto activate_locked_split;
					/* Fallback to swap normal pages */
					if (split_folio_to_list(folio, folio_list))
						goto activate_locked;
#ifdef CONFIG_TRANSPARENT_HUGEPAGE
					if (nr_pages >= HPAGE_PMD_NR) {
						count_vm_event(THP_SWPOUT_FALLBACK);
					}
					count_mthp_stat(order, MTHP_STAT_ANON_SWPOUT_FALLBACK);
#endif
					if (!add_to_swap(folio))
						goto activate_locked_split;
				}
			}
		} else if (folio_test_swapbacked(folio) &&
			 folio_test_large(folio)) {
			/* Split shmem folio */
			if (split_folio_to_list(folio, folio_list))
				goto keep_locked;
		}

		/*
		 * If the folio was split above, the tail pages will make
		 * their own pass through this function and be accounted
		 * then.
		 */
		if ((nr_pages > 1) && !folio_test_large(folio)) {
			sc->nr_scanned -= (nr_pages - 1);
			nr_pages = 1;
		}

		/*
		 * The folio is mapped into the page tables of one or more
		 * processes. Try to unmap it here.
		 */
		if (folio_mapped(folio)) {
			enum ttu_flags flags = TTU_BATCH_FLUSH;
			bool was_swapbacked = folio_test_swapbacked(folio);
			if (folio_test_pmd_mappable(folio))
				flags |= TTU_SPLIT_HUGE_PMD;
			/*
			 * Without TTU_SYNC, try_to_unmap will only begin to
			 * hold PTL from the first present PTE within a large
			 * folio. Some initial PTEs might be skipped due to
			 * races with parallel PTE writes in which PTEs can be
			 * cleared temporarily before being written new present
			 * values. This will lead to a large folio is still
			 * mapped while some subpages have been partially
			 * unmapped after try_to_unmap; TTU_SYNC helps
			 * try_to_unmap acquire PTL from the first PTE,
			 * eliminating the influence of temporary PTE values.
			 */
			if (folio_test_large(folio) && list_empty(&folio->_deferred_list))
				flags |= TTU_SYNC;
			try_to_unmap(folio, flags);
			if (folio_mapped(folio)) {
				stat->nr_unmap_fail += nr_pages;
				if (!was_swapbacked &&
				 folio_test_swapbacked(folio))
					stat->nr_lazyfree_fail += nr_pages;
				goto activate_locked;
			}
		}

		/*
		 * Folio is unmapped now so it cannot be newly pinned anymore.
		 * No point in trying to reclaim folio if it is pinned.
		 * Furthermore we don't want to reclaim underlying fs metadata
		 * if the folio is pinned and thus potentially modified by the
		 * pinning process as that may upset the filesystem.
		 */
		if (folio_maybe_dma_pinned(folio))
			goto activate_locked;
		mapping = folio_mapping(folio);
		if (folio_test_dirty(folio)) {
			/*
			 * Only kswapd can writeback filesystem folios
			 * to avoid risk of stack overflow. But avoid
			 * injecting inefficient single-folio I/O into
			 * flusher writeback as much as possible: only
			 * write folios when we've encountered many
			 * dirty folios, and when we've already scanned
			 * the rest of the LRU for clean folios and see
			 * the same dirty folios again (with the reclaim
			 * flag set).
			 */
			if (folio_is_file_lru(folio) &&
			 (!current_is_kswapd() ||
			 !folio_test_reclaim(folio) ||
			 !test_bit(PGDAT_DIRTY, &pgdat->flags))) {
				/*
				 * Immediately reclaim when written back.
				 * Similar in principle to folio_deactivate()
				 * except we already have the folio isolated
				 * and know it's dirty
				 */
				node_stat_mod_folio(folio, NR_VMSCAN_IMMEDIATE,
						nr_pages);
				folio_set_reclaim(folio);
				goto activate_locked;
			}
			if (references == FOLIOREF_RECLAIM_CLEAN)
				goto keep_locked;
			if (!may_enter_fs(folio, sc->gfp_mask))
				goto keep_locked;
			if (!sc->may_writepage)
				goto keep_locked;

			/*
			 * Folio is dirty. Flush the TLB if a writable entry
			 * potentially exists to avoid CPU writes after I/O
			 * starts and then write it out here.
			 */
			try_to_unmap_flush_dirty();
			switch (pageout(folio, mapping, &plug)) {
			case PAGE_KEEP:
				goto keep_locked;
			case PAGE_ACTIVATE:
				goto activate_locked;
			case PAGE_SUCCESS:
				stat->nr_pageout += nr_pages;
				if (folio_test_writeback(folio))
					goto keep;
				if (folio_test_dirty(folio))
					goto keep;

				/*
				 * A synchronous write - probably a ramdisk. Go
				 * ahead and try to reclaim the folio.
				 */
				if (!folio_trylock(folio))
					goto keep;
				if (folio_test_dirty(folio) ||
				 folio_test_writeback(folio))
					goto keep_locked;
				mapping = folio_mapping(folio);
				fallthrough;
			case PAGE_CLEAN:
				; /* try to free the folio below */
			}
		}

		/*
		 * If the folio has buffers, try to free the buffer
		 * mappings associated with this folio. If we succeed
		 * we try to free the folio as well.
		 *
		 * We do this even if the folio is dirty.
		 * filemap_release_folio() does not perform I/O, but it
		 * is possible for a folio to have the dirty flag set,
		 * but it is actually clean (all its buffers are clean).
		 * This happens if the buffers were written out directly,
		 * with submit_bh(). ext3 will do this, as well as
		 * the blockdev mapping. filemap_release_folio() will
		 * discover that cleanness and will drop the buffers
		 * and mark the folio clean - it can be freed.
		 *
		 * Rarely, folios can have buffers and no ->mapping.
		 * These are the folios which were not successfully
		 * invalidated in truncate_cleanup_folio(). We try to
		 * drop those buffers here and if that worked, and the
		 * folio is no longer mapped into process address space
		 * (refcount == 1) it can be freed. Otherwise, leave
		 * the folio on the LRU so it is swappable.
		 */
		if (folio_needs_release(folio)) {
			if (!filemap_release_folio(folio, sc->gfp_mask))
				goto activate_locked;
			if (!mapping && folio_ref_count(folio) == 1) {
				folio_unlock(folio);
				if (folio_put_testzero(folio))
					goto free_it;
				else {
					/*
					 * rare race with speculative reference.
					 * the speculative reference will free
					 * this folio shortly, so we may
					 * increment nr_reclaimed here (and
					 * leave it off the LRU).
					 */
					nr_reclaimed += nr_pages;
					continue;
				}
			}
		}
		if (folio_test_anon(folio) && !folio_test_swapbacked(folio)) {
			/* follow __remove_mapping for reference */
			if (!folio_ref_freeze(folio, 1))
				goto keep_locked;

			/*
			 * The folio has only one reference left, which is
			 * from the isolation. After the caller puts the
			 * folio back on the lru and drops the reference, the
			 * folio will be freed anyway. It doesn't matter
			 * which lru it goes on. So we don't bother checking
			 * the dirty flag here.
			 */
			count_vm_events(PGLAZYFREED, nr_pages);
			count_memcg_folio_events(folio, PGLAZYFREED, nr_pages);
		} else if (!mapping || !__remove_mapping(mapping, folio, true,
							 sc->target_mem_cgroup))
			goto keep_locked;
		folio_unlock(folio);

free_it:
		/*
		 * Folio may get swapped out as a whole, need to account
		 * all pages in it.
		 */
		nr_reclaimed += nr_pages;

		/*
		 * Is there need to periodically free_folio_list? It would
		 * appear not as the counts should be low
		 */
		if (unlikely(folio_test_large(folio)))
			destroy_large_folio(folio);
		else
			list_add(&folio->lru, &free_folios);
		continue;

		// 进入到这些分支的都是不回收的
activate_locked_split:
		/*
		 * The tail pages that are failed to add into swap cache
		 * reach here. Fixup nr_scanned and nr_pages.
		 */
		if (nr_pages > 1) {
			sc->nr_scanned -= (nr_pages - 1);
			nr_pages = 1;
		}
activate_locked:
		/* Not a candidate for swapping, so reclaim swap space. */
		if (folio_test_swapcache(folio) &&
		 (mem_cgroup_swap_full(folio) || folio_test_mlocked(folio)))
			folio_free_swap(folio); // 这里才是真正的释放
		VM_BUG_ON_FOLIO(folio_test_active(folio), folio);
		if (!folio_test_mlocked(folio)) {
			int type = folio_is_file_lru(folio);
			folio_set_active(folio);
			stat->nr_activate[type] += nr_pages;
			count_memcg_folio_events(folio, PGACTIVATE, nr_pages);
		}
keep_locked:
		folio_unlock(folio);
keep:
		list_add(&folio->lru, &ret_folios);
		VM_BUG_ON_FOLIO(folio_test_lru(folio) ||
				folio_test_unevictable(folio), folio);
	}

	/* 'folio_list' is always empty here */
	/* Migrate folios selected for demotion */
	nr_reclaimed += demote_folio_list(&demote_folios, pgdat);
	/* Folios that could not be demoted are still in @demote_folios */
	if (!list_empty(&demote_folios)) {
		/* Folios which weren't demoted go back on @folio_list */
		list_splice_init(&demote_folios, folio_list);

		/*
		 * goto retry to reclaim the undemoted folios in folio_list if
		 * desired.
		 *
		 * Reclaiming directly from top tier nodes is not often desired
		 * due to it breaking the LRU ordering: in general memory
		 * should be reclaimed from lower tier nodes and demoted from
		 * top tier nodes.
		 *
		 * However, disabling reclaim from top tier nodes entirely
		 * would cause ooms in edge scenarios where lower tier memory
		 * is unreclaimable for whatever reason, eg memory being
		 * mlocked or too hot to reclaim. We can disable reclaim
		 * from top tier nodes in proactive reclaim though as that is
		 * not real memory pressure.
		 */
		if (!sc->proactive) {
			do_demote_pass = false;
			goto retry;
		}
	}

    pgactivate = stat->nr_activate[0] + stat->nr_activate[1];
	mem_cgroup_uncharge_list(&free_folios);
	try_to_unmap_flush();
    // 添加到 free_folios 中的才是真正要释放的 folios
	free_unref_page_list(&free_folios);
	list_splice(&ret_folios, folio_list);
	count_vm_events(PGACTIVATE, pgactivate);
	if (plug)
		swap_write_unplug(plug);
	return nr_reclaimed;
}
```

### 页面回收

好吧，这块真看不懂，关键函数一个套一个，战略搁置！！！

在内核中，内存回收分为两个层面[^5]：整机和 memory cgroup（control group 的子系统）[^6]。

#### 整机层面

设置了三条 watermark：min、low、high。当系统 free 内存降到 low 水线以下时，系统会唤醒 kswapd 线程进行异步内存回收，一直回收到 high 水线为止，这种情况不会阻塞正在进行内存分配的进程；但如果 free 内存降到了 min 水线以下，就需要阻塞内存分配进程进行回收，不然就有 OOM 的风险，这种情况下被阻塞进程的内存分配延迟就会提高，从而感受到卡顿。如[图](https://github.com/UtopianFuture/UtopianFuture.github.io/blob/master/image/zone_wmark.png?raw=true)所示。

#### memory cgroup

关于 memory cgroup 的使用之后需要详细的分析，这里先做个简单的记录。其有如下功能：

1. Isolate an application or a group of applications. Memory-hungry applications can be isolated and limited to a smaller amount of memory.
2. Create a cgroup with a limited amount of memory. This can be used as a good alternative to booting with mem=XXXX.

3. Virtualization solutions can control the amount of memory they want to assign to a virtual machine instance.
4. A CD/DVD burner could control the amount of memory used by the rest of the system to ensure that burning does not fail due to lack of available memory.

内核中的页交换算法主要使用 LRU 链表算法和第二次机会法。

#### LRU 链表法

#### 第二次机会法

#### 触发页面回收

内核中触发页面回收的机制大致有 3 个。

- 直接页面回收机制。在内核态中调用 `__alloc_pages` 分配物理页面时，由于系统内存短缺，不能满足分配需求，此时内核会直接陷入到页面回收机制，尝试回收内存。这种情况下执行页面回收的是请求内存的进程本身，为同步回收，因此调用者进程的执行会被阻塞；
- 周期性回收内存机制。这是内核线程 kswapd 的工作。当调用 `__alloc_pages` 时发现当前 watermark 不能满足分配请求，那么唤醒 kswapd 线程来异步回收内存；
- slab 收割机（slab shrinker）机制。这是**用来回收 slab 对象的**（slab 对象的回收难道不是 slab 描述符的计数器为 0 则回收么）。当内存短缺时，直接页面回收机制和周期性回收内存机制都会调用 slab shrinker 来回收 slab 对象。

整个回收机制如下图，这些函数每一个展开都无比复杂，

![page_reclaim](/home/guanshun/gitlab/UFuture.github.io/image/page_reclaim.png)

#### kswapd 内核线程

**内核线程 kswapd 负责在内存不足时回收页面**。kswapd 内核线程在初始化时会为系统中每个内存节点创建一个 "kswapd%d" 的内核线程。从调用栈可以看出 kswapd 是通过 1 号内核线程 `kernel_init` 创建的。

```plain
#0  kswapd_run (nid=0) at mm/vmscan.c:4435
#1  0xffffffff831f2e3b in kswapd_init () at mm/vmscan.c:4470
#2  0xffffffff81003928 in do_one_initcall (fn=0xffffffff831f2df7 <kswapd_init>) at init/main.c:1303
#3  0xffffffff831babeb in do_initcall_level (command_line=0xffff888100052cf0 "console", level=6)
    at init/main.c:1376
#4  do_initcalls () at init/main.c:1392
#5  do_basic_setup () at init/main.c:1411
#6  kernel_init_freeable () at init/main.c:1614
#7  0xffffffff81c0b31a in kernel_init (unused=<optimized out>) at init/main.c:1505
#8  0xffffffff81004572 in ret_from_fork () at arch/x86/entry/entry_64.S:295
```

而 `kswapd` 就是回收函数，其在创建后会睡眠并让出 CPU 控制权，之后需要唤醒它。

```c
void kswapd_run(int nid)
{
	pg_data_t *pgdat = NODE_DATA(nid); // 表示该内存节点的内存布局，上面有分析

	if (pgdat->kswapd)
		return;

	pgdat->kswapd = kthread_run(kswapd, pgdat, "kswapd%d", nid);
	if (IS_ERR(pgdat->kswapd)) {
		/* failure at boot is fatal */
		BUG_ON(system_state < SYSTEM_RUNNING);
		pr_err("Failed to start kswapd on node %d\n", nid);
		pgdat->kswapd = NULL;
	}
}
```

在[上面](#请求页框)分析的物理页面分配关键函数 `rmqueue` 中会检查 `ZONE_BOOSTED_WATERMARK` 标志位，通过这个标志位判断是否需要唤醒 kswapd 内核线程来进行页面回收。

```c
/*
 * Allocate a page from the given zone. Use pcplists for order-0 allocations.
 */
static inline struct page *rmqueue(struct zone *preferred_zone,
			struct zone *zone, unsigned int order,
			gfp_t gfp_flags, unsigned int alloc_flags,
			int migratetype)
{
	...

	/* Separate test+clear to avoid unnecessary atomics */
	if (test_bit(ZONE_BOOSTED_WATERMARK, &zone->flags)) {
		clear_bit(ZONE_BOOSTED_WATERMARK, &zone->flags);
		wakeup_kswapd(zone, 0, 0, zone_idx(zone)); // 判断是否需要唤醒 kswapd 线程回收内存
	}

	...

}
```

接下来分析 `kswapd` 函数，

```c
static int kswapd(void *p) // 这个参数为内存节点描述符 pg_data_t
{
	unsigned int alloc_order, reclaim_order;
	unsigned int highest_zoneidx = MAX_NR_ZONES - 1;
	pg_data_t *pgdat = (pg_data_t *)p;
	struct task_struct *tsk = current;
	const struct cpumask *cpumask = cpumask_of_node(pgdat->node_id);

	if (!cpumask_empty(cpumask))
		set_cpus_allowed_ptr(tsk, cpumask);

	/*
	 * Tell the memory management that we're a "memory allocator",
	 * and that if we need more memory we should get access to it
	 * regardless (see "__alloc_pages()"). "kswapd" should
	 * never get caught in the normal page freeing logic.
	 *
	 * (Kswapd normally doesn't need memory anyway, but sometimes
	 * you need a small amount of memory in order to be able to
	 * page out something else, and this flag essentially protects
	 * us from recursively trying to free more memory as we're
	 * trying to free the first piece of memory in the first place).
	 */
    // 设置 kswapd 进程描述符的标志位
	tsk->flags |= PF_MEMALLOC | PF_SWAPWRITE | PF_KSWAPD;
	set_freezable();

	WRITE_ONCE(pgdat->kswapd_order, 0); // 指定需要回收的页面数量
	WRITE_ONCE(pgdat->kswapd_highest_zoneidx, MAX_NR_ZONES); // 可以扫描和回收的最高 zone
	for ( ; ; ) {
		bool ret;

		alloc_order = reclaim_order = READ_ONCE(pgdat->kswapd_order);
		highest_zoneidx = kswapd_highest_zoneidx(pgdat,
							highest_zoneidx);

kswapd_try_sleep:
		kswapd_try_to_sleep(pgdat, alloc_order, reclaim_order,
					highest_zoneidx); // kswapd 进入睡眠

		/* Read the new order and highest_zoneidx */
		alloc_order = READ_ONCE(pgdat->kswapd_order);
		highest_zoneidx = kswapd_highest_zoneidx(pgdat,
							highest_zoneidx);
		WRITE_ONCE(pgdat->kswapd_order, 0);
		WRITE_ONCE(pgdat->kswapd_highest_zoneidx, MAX_NR_ZONES);

		ret = try_to_freeze();

        ...

		trace_mm_vmscan_kswapd_wake(pgdat->node_id, highest_zoneidx,
						alloc_order);
		reclaim_order = balance_pgdat(pgdat, alloc_order, // 哦！这才是真正的页面回收
						highest_zoneidx);
		if (reclaim_order < alloc_order)
			goto kswapd_try_sleep;
	}

	tsk->flags &= ~(PF_MEMALLOC | PF_SWAPWRITE | PF_KSWAPD);

	return 0;
}
```

##### 关键函数 balance_pgdat

这是一个巨长的函数，涉及了很多东西，我目前无法完全理解，所以只是按照书上总结出一个回收框架。关于这个函数的作用，注释写的很清楚。

```c
/*
 * For kswapd, balance_pgdat() will reclaim pages across a node from zones
 * that are eligible for use by the caller until at least one zone is
 * balanced.
 *
 * Returns the order kswapd finished reclaiming at.
 *
 * kswapd scans the zones in the highmem->normal->dma direction.  It skips
 * zones which have free_pages > high_wmark_pages(zone), but once a zone is
 * found to have free_pages <= high_wmark_pages(zone), any page in that zone
 * or lower is eligible for reclaim until at least one usable zone is
 * balanced.
 */
static int balance_pgdat(pg_data_t *pgdat, int order, int highest_zoneidx)
{
	int i;
	unsigned long nr_soft_reclaimed;
	unsigned long nr_soft_scanned;
	unsigned long pflags;
	unsigned long nr_boost_reclaim;
	unsigned long zone_boosts[MAX_NR_ZONES] = { 0, };
	bool boosted;
	struct zone *zone;
	struct scan_control sc = { // 页面回收的参数
		.gfp_mask = GFP_KERNEL,
		.order = order,
		.may_unmap = 1,
	};

	...

	/*
	 * Account for the reclaim boost. Note that the zone boost is left in
	 * place so that parallel allocations that are near the watermark will
	 * stall or direct reclaim until kswapd is finished.
	 */
	nr_boost_reclaim = 0; // 优化外碎片化的机制
	for (i = 0; i <= highest_zoneidx; i++) {
		zone = pgdat->node_zones + i;
		if (!managed_zone(zone))
			continue;

		nr_boost_reclaim += zone->watermark_boost;
		zone_boosts[i] = zone->watermark_boost;
	}
	boosted = nr_boost_reclaim;

restart:
	set_reclaim_active(pgdat, highest_zoneidx);
	sc.priority = DEF_PRIORITY; // 页面扫描的优先级，优先级越小，扫描的页面就越多
	do {
		unsigned long nr_reclaimed = sc.nr_reclaimed;
		bool raise_priority = true;
		bool balanced;
		bool ret;

		sc.reclaim_idx = highest_zoneidx;

		...

		/*
		 * If the pgdat is imbalanced then ignore boosting and preserve
		 * the watermarks for a later time and restart. Note that the
		 * zone watermarks will be still reset at the end of balancing
		 * on the grounds that the normal reclaim should be enough to
		 * re-evaluate if boosting is required when kswapd next wakes.
		 */
        // 检查这个内存节点是否有合格的 zone
        // 所谓合格的 zone，就是其 watermask 高于 WMARK_HIGH
        // 并且可以分配出 2^order 个连续的物理页面
		balanced = pgdat_balanced(pgdat, sc.order, highest_zoneidx);
		if (!balanced && nr_boost_reclaim) {
			nr_boost_reclaim = 0;
			goto restart; // 重新扫描
		}

		/*
		 * If boosting is not active then only reclaim if there are no
		 * eligible zones. Note that sc.reclaim_idx is not used as
		 * buffer_heads_over_limit may have adjusted it.
		 */
		if (!nr_boost_reclaim && balanced) // 符合条件（？）
			goto out;

		...

		/*
		 * Do some background aging of the anon list, to give
		 * pages a chance to be referenced before reclaiming. All
		 * pages are rotated regardless of classzone as this is
		 * about consistent aging.
		 */
		age_active_anon(pgdat, &sc); // 对匿名页面的活跃 LRU 链表进行老化（？）

		/*
		 * There should be no need to raise the scanning priority if
		 * enough pages are already being scanned that that high
		 * watermark would be met at 100% efficiency.
		 */
        // 页面回收的核心函数
		if (kswapd_shrink_node(pgdat, &sc))
			raise_priority = false;

		...

		if (raise_priority || !nr_reclaimed) // 不断加大扫描粒度
			sc.priority--;
	} while (sc.priority >= 1);

out:
	clear_reclaim_active(pgdat, highest_zoneidx);

	/* If reclaim was boosted, account for the reclaim done in this pass */
	if (boosted) {
		unsigned long flags;

		for (i = 0; i <= highest_zoneidx; i++) {
			if (!zone_boosts[i])
				continue;

			/* Increments are under the zone lock */
			zone = pgdat->node_zones + i;
			spin_lock_irqsave(&zone->lock, flags);
			zone->watermark_boost -= min(zone->watermark_boost, zone_boosts[i]);
			spin_unlock_irqrestore(&zone->lock, flags);
		}

		/*
		 * As there is now likely space, wakeup kcompact to defragment
		 * pageblocks.
		 */
		wakeup_kcompactd(pgdat, pageblock_order, highest_zoneidx); // 外碎片化优化，通过这个函数来做
	}

    ...

	/*
	 * Return the order kswapd stopped reclaiming at as
	 * prepare_kswapd_sleep() takes it into account. If another caller
	 * entered the allocator slow path while kswapd was awake, order will
	 * remain at the higher level.
	 */
	return sc.order; // 返回回收的页面数
}
```

##### 关键函数 shrink_node

`balance_pgdat` -> `kswapd_shrink_node` -> `shrink_node`
该函数用于扫描内存节点中所有可回收的页面，直接内存回收和 kswapd 都会调用该函数回收内存。
![img](https://raw.githubusercontent.com/UtopianFuture/UtopianFuture.github.io/81305767754ca8df95d18da10465dd730bc093be/image/shrink_node.svg)
整个操作可以归纳为两种情况：

- 使能了 mglru：
 - mglru 回收的核心函数，`try_to_shrink_lruvec`，首先执行 `get_nr_to_scan` 判断是否要进行老化，在 `evict_folios` 中将需要回收的 folios 放到 list 中，然后回收这些 folios，`should_abort_scan` 检查 watermark 是否 OK，决定是否结束回收；
 - 调用 shrink_slab；
- 没使能 mglru:
 - 遍历 active 和 inactive lru 进行内存回收，这一操作和 rmap 机制紧密相关，需要借助 rmap 机制来解除某个 page 的所有映射；
 - shrink_active_list 用于扫描 active lru 链表，包括匿名页和文件页，将最近一直没有访问的页面添加到 inactive lru 链表中。这里调用 `page_referenced` 是为了计算所有共享该页面的进程中，该页面最近访问、引用 PTE 的个数(page->_mapcount)，若个数为 0，说明最近没有访问、引用，可以进行下一步操作；
 - shrink_inactive_list 用于扫描 inactive lru 链表以尝试回收页面，并返回回收页面的数量。这里调用 `page_check_references` 同样用于计算、维护在所有共享该页面的的进程中的引用 PTE 的用户数(page->_mapcount)，以此确实是否可以回收；
 - 调用 shrink_slab。`shrinker_list` 是一个全局变量，内核组件可以通过 `register_shrinker` 注册自己的 shrink_slab 回调函数，然后执行 scan_objects 函数，返回回收的页面数量；
我们进一步分析 shrink_list 以及 shrink_slab 是怎样回收内存的。

##### shrink_active_list&shrink_inactive_list

这个是 lru 的回收方式，现在内核使用的都是 mglru，这个以后有需要再分析吧！

#### 回收页面类型

那么接下来考虑一个问题，哪些页面会被回收？

属于内核的大部分页框是不能回收的，包括内核栈，内核的代码段，内核数据段等，**kernel 主要对进程使用的内存页进行回收**，主要包括如下页面：

- 进程堆、栈、数据段使用的匿名页： 存放到 swap 分区中；
- 进程代码段映射的可执行文件的文件页： 直接释放；
- 打开文件进行读写使用的文件页： 如果页中数据与文件数据不一致，则进行回写到磁盘对应文件中，如果一致，则直接释放；
- 进行文件映射 mmap 共享内存时使用的页： 如果页中数据与文件数据不一致，则进行回写到磁盘对应文件中，如果一致，则直接释放；
- 进行匿名 mmap 共享内存时使用的页： 存放到 swap 分区中；
- 进行 shmem 共享内存时使用的页：存放到 swap 分区中；

可以看出，内存回收的时候，会筛选出一些不经常使用的文件页或匿名页，针对上述不同的内存页，有两种处理方式：

- 直接释放。 如进程代码段的页，这些页是只读的，干净的文件页（文件页中保存的内容与磁盘中文件对应内容一致）；

- 将页回写后再释放。匿名页直接将它们写入到 swap 分区中；脏页（文件页保存的数据与磁盘中文件对应的数据不一致）需要先将此文件页回写到磁盘中对应数据所在位置上。

  由此可见，如果系统没有配置 swap 分区，那么只有文件页能被回收。

### 页面迁移

内核为页面迁移提供了一个系统调用，

```c
__SYSCALL(256, sys_migrate_pages)
```

这个系统调用最初是为了**在 NUMA 系统中迁移一个进程的所有页面到指定内存节点上**，但目前其他模块也可以使用页面迁移机制，如内存规整和内存热拔插等。因此，页面迁移机制支持两大类的内存页面：

- 传统 LRU 页面，如匿名页面和文件映射页面（看来 LRU 链表还是要了解啊）
- 非 LRU 页面，如 zsmalloc 或 virtio-ballon 页面（这是内核引入的新特性）

#### 关键函数__unmap_and_move

`migrate_pages` -> `unmap_and_move` -> `__unmap_and_move`

和前面类似，`migrate_pages` 是外部封装函数，主体是一个循环，迁移 `from` 链表中的每一个页，对各种迁移结果进行处理；`unmap_and_move` 会调用 `get_new_page` 分配一个新的页面，然后也处理各种迁移结果。

```c
static int __unmap_and_move(struct page *page, struct page *newpage,
				int force, enum migrate_mode mode) // from, to
{
	int rc = -EAGAIN;
	bool page_was_mapped = false;
	struct anon_vma *anon_vma = NULL;
	bool is_lru = !__PageMovable(page);

	if (!trylock_page(page)) { // 尝试为需要迁移的 page 加锁
		if (!force || mode == MIGRATE_ASYNC)
			goto out;

		if (current->flags & PF_MEMALLOC)
			goto out;

		lock_page(page); // 若加锁失败，除了上面两种情况，需要等待其他进程释放锁
	}

	if (PageWriteback(page)) { // 处理写回页面
		/*
		 * Only in the case of a full synchronous migration is it
		 * necessary to wait for PageWriteback. In the async case,
		 * the retry loop is too short and in the sync-light case,
		 * the overhead of stalling is too much
		 */
		switch (mode) {
		case MIGRATE_SYNC:
		case MIGRATE_SYNC_NO_COPY:
			break;
		default:
			rc = -EBUSY;
			goto out_unlock;
		}
		if (!force)
			goto out_unlock;
		wait_on_page_writeback(page);
	}

	if (PageAnon(page) && !PageKsm(page)) // 处理匿名和 KSM 页面
		anon_vma = page_get_anon_vma(page);

	...

    if (unlikely(!is_lru)) { // 第 2 中情况，非 LRU 页面
		rc = move_to_new_page(newpage, page, mode); // 调用驱动注册的 migratepage 函数来进行迁移
		goto out_unlock_both;
	}

	/*
	 * Corner case handling:
	 * 1. When a new swap-cache page is read into, it is added to the LRU
	 * and treated as swapcache but it has no rmap yet.
	 * Calling try_to_unmap() against a page->mapping==NULL page will
	 * trigger a BUG.  So handle it here.
	 * 2. An orphaned page (see truncate_cleanup_page) might have
	 * fs-private metadata. The page can be picked up due to memory
	 * offlining.  Everywhere else except page reclaim, the page is
	 * invisible to the vm, so the page can not be migrated.  So try to
	 * free the metadata, so the page can be freed.
	 */
	if (!page->mapping) { // 上面的注释很清楚
		VM_BUG_ON_PAGE(PageAnon(page), page);
		if (page_has_private(page)) {
			try_to_free_buffers(page);
			goto out_unlock_both;
		}
	} else if (page_mapped(page)) { // 判断 _mapcount 是否大于等于 0
		/* Establish migration ptes */
		VM_BUG_ON_PAGE(PageAnon(page) && !PageKsm(page) && !anon_vma,
				page);
		try_to_migrate(page, 0); // 应该是和 move_to_new_page 一样完成迁移，但是它会先调用 rmap_walk 解除 PTE
		page_was_mapped = true;
	}

	if (!page_mapped(page)) // 该 page 不存在 PTE，可以直接迁移
		rc = move_to_new_page(newpage, page, mode);

	if (page_was_mapped) // 删除迁移完的页面的 PTE
		remove_migration_ptes(page,
			rc == MIGRATEPAGE_SUCCESS ? newpage : page, false);

	...

	return rc;
}
```

#### 关键函数 move_to_new_page

`move_to_new_page` 用于迁移旧页面到新页面，当然，这个函数还不是最终完成迁移的，不同情况对应不同的迁移函数，不过到这里就打住吧，再深入我怕人没了，之后有能力有需求再分析。

```c
static int move_to_new_page(struct page *newpage, struct page *page,
				enum migrate_mode mode)
{
	struct address_space *mapping;
	int rc = -EAGAIN;
	bool is_lru = !__PageMovable(page);

    // page 数据结构中有 mapping 成员变量
    // 对于匿名页面并且分配了交换缓存，那么 mapping 指向交换缓存
    // 对于匿名页面但没有分配交换缓存，那么 mapping 指向 anon_vma，这是前面分析过的
    // 对于文件映射页面，那么 mapping 会指向文件映射对应的地址空间
    // 这在缺页异常处理中寻找文件映射 page 对应的物理地址时遇到过
	mapping = page_mapping(page);

    // 处理过程很清晰，LRU 页面和非 LRU 页面
	if (likely(is_lru)) {
		if (!mapping) // 匿名页面但没有分配交换缓存
            // 额，这里貌似更加核心？不想深入了，简直是个无底洞！！！
			rc = migrate_page(mapping, newpage, page, mode);
		else if (mapping->a_ops->migratepage)
			/*
			 * Most pages have a mapping and most filesystems
			 * provide a migratepage callback. Anonymous pages
			 * are part of swap space which also has its own
			 * migratepage callback. This is the most common path
			 * for page migration.
			 */
			rc = mapping->a_ops->migratepage(mapping, newpage,
							page, mode);
		else
			rc = fallback_migrate_page(mapping, newpage, // 文件映射页面（？）
							page, mode);
	} else { // 非 LRU 页面
		/*
		 * In case of non-lru page, it could be released after
		 * isolation step. In that case, we shouldn't try migration.
		 */
		VM_BUG_ON_PAGE(!PageIsolated(page), page);
		if (!PageMovable(page)) { // 好吧，又是特殊情况，这些应该都是遇到 bug 后加的补丁吧！
			rc = MIGRATEPAGE_SUCCESS;
			__ClearPageIsolated(page);
			goto out;
		}

		rc = mapping->a_ops->migratepage(mapping, newpage,
						page, mode);
		WARN_ON_ONCE(rc == MIGRATEPAGE_SUCCESS &&
			!PageIsolated(page));
	}

	/*
	 * When successful, old pagecache page->mapping must be cleared before
	 * page is freed; but stats require that PageAnon be left as PageAnon.
	 */
	if (rc == MIGRATEPAGE_SUCCESS) {
		if (__PageMovable(page)) { // 用于分辨 page 是传统 LRU 页面还是非 LRU 页面
			VM_BUG_ON_PAGE(!PageIsolated(page), page);

			/*
			 * We clear PG_movable under page_lock so any compactor
			 * cannot try to migrate this page.
			 */
			__ClearPageIsolated(page);
		}

		/*
		 * Anonymous and movable page->mapping will be cleared by
		 * free_pages_prepare so don't reset it here for keeping
		 * the type to work PageAnon, for example.
		 */
		if (!PageMappingFlags(page))
			page->mapping = NULL;

		if (likely(!is_zone_device_page(newpage)))
			flush_dcache_page(newpage);

	}
out:
	return rc;
}
```

总结一下，页面迁移的本质是将一系列的 page 迁移到新的 page，那么这其中自然会涉及到**物理页的分配、断开旧页面的 PTE、复制旧物理页的内容到新页面、设置新的 PTE，最后释放旧的物理页**，这其中根据页面的属性又有不同的处理方式，跟着这个思路走就可以理解页面迁移（大致应该是这样，不过我还没有对细节了如指掌，时间啊！）。

![page_migrate.png](https://github.com/UtopianFuture/UtopianFuture.github.io/blob/master/image/page_migrate.png?raw=true)

### 内存规整

#### 基本原理

内存规整是为了解决内核碎片化出现的一个功能，当物理设备需要大段的连续的物理内存，而内核无法满足，则会发生内核错误，因此需要**将多个小空闲内存块重新整理以凑出大块连续的物理内存**。

内存规整的核心思想是**将内存页面按照可移动、可回收、不和移动等特性进行分类。**

内核按照可移动性将内存页分为如下三种类型：

- UNMOVABLE：在内存中的位置固定，不能随意移动。内核态分配的内存基本属于这个类型；
- RECLAIMABLE：不可移动，但可以删除回收。例如文件页映射内存；
- MOVABLE：可以随意移动，用户空间的内存基本属于这个类型。

其运行流程总结起来很好理解（但是实现又是另一回事:joy:）。有两个方向的扫描者：一个从 zone 的头部向 zone 的尾部方向扫描，查找哪些页面是可移动的；另一个从 zone 的尾部向 zone 的头部方向扫描，查找哪些页面是空闲页面。当这两个扫描者在 zone 中间相遇或已经满足分配大块内存的需求（能分配处所需要的大块内存并且满足最低的水位要求）时，就可以退出扫描。

![memory_compact.png](https://github.com/UtopianFuture/UtopianFuture.github.io/blob/master/image/memory_compact.png?raw=true)

内核中触发内存规整有 3 个途径：

- 直接内存规整。当伙伴系统发现 zone 的 watermask 无法满足页面分配时，会进入慢路径。在慢路径中，除了唤醒 kswapd 内核线程，还会调用 `__alloc_pages_direct_compact` 尝试整理出大块内存。

- kcompactd 内核线程。和页面回收 kswapd 内核线程一样，每个内存节点都会创建一个 `kcompactd%d` 内核线程（何时唤醒？）。

- 手动触发。通过写 1 到 /proc/sys/vm/compact_memory 节点，会手动触发内存规整，内核会扫描所有的内存节点上的 zone，对每个 zone 做一次内存规整。

#### try_to_compact_pages

直接内存规整，需要分析入参对规整效率的影响

```c
enum compact_result try_to_compact_pages(gfp_t gfp_mask, unsigned int order,
		unsigned int alloc_flags, const struct alloc_context *ac,
		enum compact_priority prio, struct page **capture)
{
	int may_perform_io = (__force int)(gfp_mask & __GFP_IO);
	struct zoneref *z;
	struct zone *zone;
	enum compact_result rc = COMPACT_SKIPPED;

	/*
	 * Check if the GFP flags allow compaction - GFP_NOIO is really
	 * tricky context because the migration might require IO
	 */
	if (!may_perform_io)
		return COMPACT_SKIPPED;
	trace_mm_compaction_try_to_compact_pages(order, gfp_mask, prio);

	/* Compact each zone in the list */
	for_each_zone_zonelist_nodemask(zone, z, ac->zonelist,
					ac->highest_zoneidx, ac->nodemask) {
		enum compact_result status;
		if (prio > MIN_COMPACT_PRIORITY
					&& compaction_deferred(zone, order)) {
			rc = max_t(enum compact_result, COMPACT_DEFERRED, rc);
			continue;
		}

		status = compact_zone_order(zone, order, gfp_mask, prio,
				alloc_flags, ac->highest_zoneidx, capture);
		rc = max(status, rc);

		/* The allocation should succeed, stop compacting */
		if (status == COMPACT_SUCCESS) {
			/*
			 * We think the allocation will succeed in this zone,
			 * but it is not certain, hence the false. The caller
			 * will repeat this with true if allocation indeed
			 * succeeds in this zone.
			 */
			compaction_defer_reset(zone, order, false);
			break;
		}

		if (prio != COMPACT_PRIO_ASYNC && (status == COMPACT_COMPLETE ||
					status == COMPACT_PARTIAL_SKIPPED))

			/*
			 * We think that allocation won't succeed in this zone
			 * so we defer compaction there. If it ends up
			 * succeeding after all, it will be reset.
			 */
			defer_compaction(zone, order);

		/*
		 * We might have stopped compacting due to need_resched() in
		 * async compaction, or due to a fatal signal detected. In that
		 * case do not try further zones
		 */
		if ((prio == COMPACT_PRIO_ASYNC && need_resched())
					|| fatal_signal_pending(current))
			break;
	}
	return rc;
}
```

- gfp_mask：该标识是用来确定迁移类型的，迁移类型在后续 `isolate_migratepages` 中需要作为判断条件，具体可以看 `suitable_migration_source` 函数，这里配置成 GFP_KERNEL；

- order：该路径 order 就是申请的 order，后续规整时，会以该 order 为粒度遍历 zone，如果是用户态触发的，order = -1。后续可以对比一下正常的 order 和 order = -1 有什么区别；

- alloc_flags：分配掩码，会影响后续判断是否适合规整，慢路径设置的是 ALLOC_WMARK_MIN，快路径设置的是 ALLOC_WMARK_LOW。下面几种路径可以加断点跟一下看配置的是什么；

- const struct alloc_context *ac: 需要指定 zonelist, highest_zoneidx 等参数，和 compact_nodes 不同，try_to_compact_pages 之后遍历 ac->zonelist 中的 zone。zonelist 可以通过 `node_zonelist(preferred_nid, gfp_mask);` 获取，其中 preferred_nid 为 0 就行，因为 `node_zonelist` 是直接获取全局变量 `contig_page_data`，不需要 nid；而 `highest_zoneidx` 可以通过 `gfp_zone(gfp_mask)` 获取；

- enum compact_priority prio：决定内存规整的模式，可以配置为 MIGRATE_ASYNC，其他几种场景是怎样配置的可以加断点跟一下；

- struct page **capture：返回值，设置为 NULL；

#### kcompactd 内核线程

整个过程和 kswapd 内核线程类似，创建、睡眠、唤醒。

```plain
#0  kcompactd_run (nid=nid@entry=0) at mm/compaction.c:2979
#1  0xffffffff831f4d5b in kcompactd_init () at mm/compaction.c:3046
#2  0xffffffff81003928 in do_one_initcall (fn=0xffffffff831f4ce7 <kcompactd_init>) at init/main.c:1303
#3  0xffffffff831babeb in do_initcall_level (command_line=0xffff888100052cf0 "console", level=4)
    at init/main.c:1376
#4  do_initcalls () at init/main.c:1392
#5  do_basic_setup () at init/main.c:1411
#6  kernel_init_freeable () at init/main.c:1614
#7  0xffffffff81c0b31a in kernel_init (unused=<optimized out>) at init/main.c:1505
#8  0xffffffff81004572 in ret_from_fork () at arch/x86/entry/entry_64.S:295
```

```c
int kcompactd_run(int nid)
{
	pg_data_t *pgdat = NODE_DATA(nid);
	int ret = 0;

	if (pgdat->kcompactd)
		return 0;

	pgdat->kcompactd = kthread_run(kcompactd, pgdat, "kcompactd%d", nid);
	if (IS_ERR(pgdat->kcompactd)) {
		pr_err("Failed to start kcompactd on node %d\n", nid);
		ret = PTR_ERR(pgdat->kcompactd);
		pgdat->kcompactd = NULL;
	}
	return ret;
}
```

先看看 kcompactd 内核线程的实现，

```c
static int kcompactd(void *p)
{
	pg_data_t *pgdat = (pg_data_t *)p;
	struct task_struct *tsk = current;
	long default_timeout = msecs_to_jiffies(HPAGE_FRAG_CHECK_INTERVAL_MSEC);
	long timeout = default_timeout;

	const struct cpumask *cpumask = cpumask_of_node(pgdat->node_id);

	if (!cpumask_empty(cpumask))
		set_cpus_allowed_ptr(tsk, cpumask);

	set_freezable();

	pgdat->kcompactd_max_order = 0;
	pgdat->kcompactd_highest_zoneidx = pgdat->nr_zones - 1;

	while (!kthread_should_stop()) {
		unsigned long pflags;

		/*
		 * Avoid the unnecessary wakeup for proactive compaction
		 * when it is disabled.
		 */
		if (!sysctl_compaction_proactiveness)
			timeout = MAX_SCHEDULE_TIMEOUT;
		trace_mm_compaction_kcompactd_sleep(pgdat->node_id);
		if (wait_event_freezable_timeout(pgdat->kcompactd_wait,
			kcompactd_work_requested(pgdat), timeout) &&
			!pgdat->proactive_compact_trigger) {

			psi_memstall_enter(&pflags);
			kcompactd_do_work(pgdat);
			psi_memstall_leave(&pflags);
			/*
			 * Reset the timeout value. The defer timeout from
			 * proactive compaction is lost here but that is fine
			 * as the condition of the zone changing substantionally
			 * then carrying on with the previous defer interval is
			 * not useful.
			 */
			timeout = default_timeout;
			continue;
		}

		/*
		 * Start the proactive work with default timeout. Based
		 * on the fragmentation score, this timeout is updated.
		 */
		timeout = default_timeout;
		if (should_proactive_compact_node(pgdat)) {
			unsigned int prev_score, score;

			prev_score = fragmentation_score_node(pgdat);
			proactive_compact_node(pgdat);
			score = fragmentation_score_node(pgdat);
			/*
			 * Defer proactive compaction if the fragmentation
			 * score did not go down i.e. no progress made.
			 */
			if (unlikely(score >= prev_score))
				timeout =
				   default_timeout << COMPACT_MAX_DEFER_SHIFT;
		}
		if (unlikely(pgdat->proactive_compact_trigger))
			pgdat->proactive_compact_trigger = false;
	}

	return 0;
}
```

#### sysctl_compaction_handler

响应 sysctl，逻辑很简单，就是调用 compact_nodes，

```c
static int sysctl_compaction_handler(struct ctl_table *table, int write,
			void *buffer, size_t *length, loff_t *ppos)
{
	int ret;
	ret = proc_dointvec(table, write, buffer, length, ppos);
	if (ret)
		return ret;
	if (sysctl_compact_memory != 1)
		return -EINVAL;
	if (write)
		compact_nodes();
	return 0;
}

static void compact_nodes(void)
{
	int nid;
	/* Flush pending updates to the LRU lists */
	lru_add_drain_all();
	for_each_online_node(nid)
		compact_node(nid); // 后续就是调用 compact_zone
}
```

#### 关键函数 compact_zone

以上三中路径最终都是调用到 `compact_zone`，完成上面描述的扫描 zone （不过最新的内核不再以 zone 为扫描单元，而是以内存节点）的任务，找出可以迁移的页面和空闲页面，最终整理出大块内存。

在分析该函数前，我们需要了解一下规整相关的数据结构以规整模式，

##### compact_control

`struct compact_control` 结构体是规整控制描述符，其中每个变量都对规整过程有影响，

```c
/*
 * compact_control is used to track pages being migrated and the free pages
 * they are being migrated to during memory compaction. The free_pfn starts
 * at the end of a zone and migrate_pfn begins at the start. Movable pages
 * are moved to the end of a zone during a compaction run and the run
 * completes when free_pfn <= migrate_pfn
 */
struct compact_control {
	struct list_head freepages;	/* List of free pages to migrate to */
	struct list_head migratepages;	/* List of pages being migrated */
	unsigned int nr_freepages;	/* Number of isolated free pages */
	unsigned int nr_migratepages;	/* Number of pages to migrate */
	unsigned long free_pfn;		/* isolate_freepages search base */
	/*
	 * Acts as an in/out parameter to page isolation for migration.
	 * isolate_migratepages uses it as a search base.
	 * isolate_migratepages_block will update the value to the next pfn
	 * after the last isolated one.
	 */
	unsigned long migrate_pfn;
	unsigned long fast_start_pfn;	/* a pfn to start linear scan from */
	struct zone *zone;
	unsigned long total_migrate_scanned;
	unsigned long total_free_scanned;
	unsigned short fast_search_fail;/* failures to use free list searches */
	short search_order;		/* order to start a fast search at */
	const gfp_t gfp_mask;		/* gfp mask of a direct compactor */
	int order;			/* order a direct compactor needs */
	int migratetype;		/* migratetype of direct compactor */
    // 直观的来讲，就是该次分配要到什么程度
    // 其定义在 common/mm/internal.h 中
    // ALLOC_CMA 就是一种 alloc_flags，表示可以从 CMA 区域分配
    // ALLOC_NON_BLOCK 表示该次分配不能被阻塞
	const unsigned int alloc_flags;	/* alloc flags of a direct compactor */
	const int highest_zoneidx;	/* zone index of a direct compactor */
	enum migrate_mode mode;		/* Async or sync migration mode */
	bool ignore_skip_hint;		/* Scan blocks even if marked skip */
	bool no_set_skip_hint;		/* Don't mark blocks for skipping */
	bool ignore_block_suitable;	/* Scan blocks considered unsuitable */
	bool direct_compaction;		/* False from kcompactd or /proc/... */
	bool proactive_compaction;	/* kcompactd proactive compaction */
	bool whole_zone;		/* Whole zone should/has been scanned */
	bool contended;			/* Signal lock contention */
	bool finish_pageblock;		/* Scan the remainder of a pageblock. Used
					 * when there are potentially transient
					 * isolation or migration failures to
					 * ensure forward progress.
					 */
	bool alloc_contig;		/* alloc_contig_range allocation */
};
```

##### migrate_mode

内存规整有如下几种模式，

```c
/*
 * MIGRATE_ASYNC means never block
 * MIGRATE_SYNC_LIGHT in the current implementation means to allow blocking
 *	on most operations but not ->writepage as the potential stall time
 *	is too significant
 * MIGRATE_SYNC will block when migrating pages
 * MIGRATE_SYNC_NO_COPY will block when migrating pages but will not copy pages
 *	with the CPU. Instead, page copy happens outside the migratepage()
 *	callback and is likely using a DMA engine. See migrate_vma() and HMM
 *	(mm/hmm.c) for users of this mode.
 */
enum migrate_mode {
	MIGRATE_ASYNC, // 异步模式，当进程需要调度时，退出内存规整
	MIGRATE_SYNC_LIGHT, // 同步模式，允许调用者被阻塞
	MIGRATE_SYNC, // 同步模式，会被阻塞（？）。用户态触发就是这种模式
	MIGRATE_SYNC_NO_COPY, // HMM 机制使用这种模式
};
```

##### compact_result

```c
enum compact_result {
	COMPACT_NOT_SUITABLE_ZONE, // 字面意思，没有合适的 ZONE
	COMPACT_SKIPPED, // 不满足规整条件，退出
	COMPACT_DEFERRED, // 错误导致退出
	COMPACT_NO_SUITABLE_PAGE, // 字面意思，没有合适的 PAGE
	COMPACT_CONTINUE, // 可以在下一个 pageblock 中进行规整
	COMPACT_COMPLETE, // 完成一轮扫描，但是没能满足页面分配需求
	COMPACT_PARTIAL_SKIPPED, // 根据直接页面回收机制以及扫描了 zone 中的部分页面，但没有找到可以规整的页面
	COMPACT_CONTENDED, // 处于某些锁竞争的原因退出规整
	COMPACT_SUCCESS, // 满足页面分配请求，退出直接内存规整
};
```

##### compact_priority

```c
enum compact_priority {
	COMPACT_PRIO_SYNC_FULL, // 最高优先级，压缩和迁移以同步的方式完成；
	MIN_COMPACT_PRIORITY = COMPACT_PRIO_SYNC_FULL,
	COMPACT_PRIO_SYNC_LIGHT,
	MIN_COMPACT_COSTLY_PRIORITY = COMPACT_PRIO_SYNC_LIGHT,
	DEF_COMPACT_PRIORITY = COMPACT_PRIO_SYNC_LIGHT, // 中优先级，压缩以同步方式处理，迁移以异步方式处理；
	COMPACT_PRIO_ASYNC,
	INIT_COMPACT_PRIORITY = COMPACT_PRIO_ASYNC // 最低优先级，压缩和迁移以异步方式处理
};
```

有个问题，同步和异步在性能上分别有什么影响？
先看看整体的规整流程图，

![img](https://raw.githubusercontent.com/UtopianFuture/UtopianFuture.github.io/81305767754ca8df95d18da10465dd730bc093be/image/compact.svg)

对于 alloc_flags 需要分如下场景，
- kcompactd 内核线程，alloc_flags 被初始化为 0，采用最低警戒水位作为判断条件，因为 WMARK_MIN 为 0；
- 用户态触发，compaction_suitable 返回 COMPACT_CONTINUE（？）；
- 慢速路径，alloc_flags 为最低警戒水位；

```c
static enum compact_result
compact_zone(struct compact_control *cc, struct capture_control *capc)
{
	enum compact_result ret;
	unsigned long start_pfn = cc->zone->zone_start_pfn; // 该 zone 的起始页帧号
	unsigned long end_pfn = zone_end_pfn(cc->zone); // 结束页帧号
	unsigned long last_migrated_pfn;
	const bool sync = cc->mode != MIGRATE_ASYNC; // 是否支持异步模式
	bool update_cached;
	unsigned int nr_succeeded = 0;

	/*
	 * These counters track activities during zone compaction. Initialize
	 * them before compacting a new zone.
	 */
	cc->total_migrate_scanned = 0;
	cc->total_free_scanned = 0;
	cc->nr_migratepages = 0;
	cc->nr_freepages = 0;
	INIT_LIST_HEAD(&cc->freepages);
	INIT_LIST_HEAD(&cc->migratepages);

    // 迁移类型，MIGRATE_UNMOVABLE, MEGRATE_MOVABLE 等
    // MIGRATE_UNMOVABLE 是不能被迁移的
	cc->migratetype = gfp_migratetype(cc->gfp_mask);

    // order == -1
    // 为什么要这样判断
    // 用户态触发的话，order 就是设置为 -1
    // 为何用户态触发的要这样检查
	if (!is_via_compact_memory(cc->order)) {
		unsigned long watermark;

		/* Allocation can already succeed, nothing to do */
		watermark = wmark_pages(cc->zone,
					cc->alloc_flags & ALLOC_WMARK_MASK);
		if (zone_watermark_ok(cc->zone, cc->order, watermark,
				 cc->highest_zoneidx, cc->alloc_flags))
			return COMPACT_SUCCESS; // 原来水位达到分配要求（alloc_flags）就可以直接返回 SUCCESS 了

		/* Compaction is likely to fail */
    	// 简单来说，就是检查 zone 里的 freepages 是否大于要求的 freepages
		if (!compaction_suitable(cc->zone, cc->order,
					 cc->highest_zoneidx))
			return COMPACT_SKIPPED;
	}

	/*
	 * Clear pageblock skip if there were failures recently and compaction
	 * is about to be retried after being deferred.
	 */
    // compact_defer 是个很重要的机制
    // 为什么要设计这种机制？
    // zone 结构体中有 3 个 compact 相关的成员变量
    // 需要搞清楚分别是干什么的
	if (compaction_restarting(cc->zone, cc->order))
		__reset_isolation_suitable(cc->zone);

	/*
	 * Setup to move all movable pages to the end of the zone. Used cached
	 * information on where the scanners should start (unless we explicitly
	 * want to compact the whole zone), but check that it is initialised
	 * by ensuring the values are within zone boundaries.
	 */
	cc->fast_start_pfn = 0;
    // 这个很好理解，如果是整个 zone 规整
    // 那么从头部开始遍历需要迁移的 page
    // 从尾部开始遍历空闲 page
	if (cc->whole_zone) {
		cc->migrate_pfn = start_pfn;
		cc->free_pfn = pageblock_start_pfn(end_pfn - 1);
	} else {
    	// compact_cached_migrate_pfn 记录的是上次扫描中可迁移页面的位置
    	// 这个方案对规整的性能有多大的改进
		cc->migrate_pfn = cc->zone->compact_cached_migrate_pfn[sync];
		cc->free_pfn = cc->zone->compact_cached_free_pfn;
		if (cc->free_pfn < start_pfn || cc->free_pfn >= end_pfn) {
			cc->free_pfn = pageblock_start_pfn(end_pfn - 1);
			cc->zone->compact_cached_free_pfn = cc->free_pfn;
		}
		if (cc->migrate_pfn < start_pfn || cc->migrate_pfn >= end_pfn) {
			cc->migrate_pfn = start_pfn;
			cc->zone->compact_cached_migrate_pfn[0] = cc->migrate_pfn;
			cc->zone->compact_cached_migrate_pfn[1] = cc->migrate_pfn;
		}
		if (cc->migrate_pfn <= cc->zone->compact_init_migrate_pfn)
			cc->whole_zone = true;
	}
	last_migrated_pfn = 0;

	/*
	 * Migrate has separate cached PFNs for ASYNC and SYNC* migration on
	 * the basis that some migrations will fail in ASYNC mode. However,
	 * if the cached PFNs match and pageblocks are skipped due to having
	 * no isolation candidates, then the sync state does not matter.
	 * Until a pageblock with isolation candidates is found, keep the
	 * cached PFNs in sync to avoid revisiting the same blocks.
	 */
    // 这些优化暂时不深究
	update_cached = !sync &&
		cc->zone->compact_cached_migrate_pfn[0] == cc->zone->compact_cached_migrate_pfn[1];
	trace_mm_compaction_begin(cc, start_pfn, end_pfn, sync);

	/* lru_add_drain_all could be expensive with involving other CPUs */
	lru_add_drain();

    // 内存规整结束的条件有两个
    // cc->migrate_pfn 和 cc->free_pfn 两个指针相遇
    // zone 中 order 对那个的迁移类型的空闲链表是否有成员
    // 如果没有，可以从其他迁移类型“借”一些空闲块（原来在这里借的）
	while ((ret = compact_finished(cc)) == COMPACT_CONTINUE) {
		int err;
		unsigned long iteration_start_pfn = cc->migrate_pfn;

		/*
		 * Avoid multiple rescans of the same pageblock which can
		 * happen if a page cannot be isolated (dirty/writeback in
		 * async mode) or if the migrated pages are being allocated
		 * before the pageblock is cleared. The first rescan will
		 * capture the entire pageblock for migration. If it fails,
		 * it'll be marked skip and scanning will proceed as normal.
		 */
		cc->finish_pageblock = false;
		if (pageblock_start_pfn(last_migrated_pfn) ==
		 pageblock_start_pfn(iteration_start_pfn)) {
			cc->finish_pageblock = true;
		}
rescan:
		switch (isolate_migratepages(cc)) {
		case ISOLATE_ABORT:
			ret = COMPACT_CONTENDED;
			putback_movable_pages(&cc->migratepages);
			cc->nr_migratepages = 0;
			goto out;
		case ISOLATE_NONE:
			if (update_cached) {
				cc->zone->compact_cached_migrate_pfn[1] =
					cc->zone->compact_cached_migrate_pfn[0];
			}

      /*
			 * We haven't isolated and migrated anything, but
			 * there might still be unflushed migrations from
			 * previous cc->order aligned block.
			 */
			goto check_drain;
		case ISOLATE_SUCCESS:
			update_cached = false;
			last_migrated_pfn = max(cc->zone->zone_start_pfn,
				pageblock_start_pfn(cc->migrate_pfn - 1));
		}

    	// 该函数上面分析过
		err = migrate_pages(&cc->migratepages, compaction_alloc,
				compaction_free, (unsigned long)cc, cc->mode,
				MR_COMPACTION, &nr_succeeded);
		trace_mm_compaction_migratepages(cc, nr_succeeded);

		/* All pages were either migrated or will be released */
		cc->nr_migratepages = 0;
		if (err) {
			putback_movable_pages(&cc->migratepages); // 迁移失败的话需要将页面放回去
 ...

		}
		/* Stop if a page has been captured */
		if (capc && capc->page) {
			ret = COMPACT_SUCCESS;
			break;
		}

check_drain: // 这块没懂
		/*
		 * Has the migration scanner moved away from the previous
		 * cc->order aligned block where we migrated from? If yes,
		 * flush the pages that were freed, so that they can merge and
		 * compact_finished() can detect immediately if allocation
		 * would succeed.
		 */
		if (cc->order > 0 && last_migrated_pfn) {
			unsigned long current_block_start =
				block_start_pfn(cc->migrate_pfn, cc->order);

			if (last_migrated_pfn < current_block_start) {
				lru_add_drain_cpu_zone(cc->zone);
				/* No more flushing until we migrate again */
				last_migrated_pfn = 0;
			}
		}
	}

out: // 这块没懂
	/*
	 * Release free pages and update where the free scanner should restart,
	 * so we don't leave any returned pages behind in the next attempt.
	 */
	if (cc->nr_freepages > 0) {
		unsigned long free_pfn = release_freepages(&cc->freepages);

		cc->nr_freepages = 0;
		VM_BUG_ON(free_pfn == 0);

		/* The cached pfn is always the first in a pageblock */
		free_pfn = pageblock_start_pfn(free_pfn);

		/*
		 * Only go back, not forward. The cached pfn might have been
		 * already reset to zone end in compact_finished()
		 */
		if (free_pfn > cc->zone->compact_cached_free_pfn)
			cc->zone->compact_cached_free_pfn = free_pfn;
	}
	...

	return ret;
}
````

#### 关键函数 isolate_migratepages

该函数用于扫面 zone 中可迁移的页面，可迁移的 page 会被添加到 cc->migratepages 链表中，

```c
/*
 * Isolate all pages that can be migrated from the first suitable block,
 * starting at the block pointed to by the migrate scanner pfn within
 * compact_control.
 */
static isolate_migrate_t isolate_migratepages(struct compact_control *cc)
{
	unsigned long block_start_pfn;
	unsigned long block_end_pfn;
	unsigned long low_pfn;
	struct page *page;
	const isolate_mode_t isolate_mode = // 表示是否支持异步分离
		(sysctl_compact_unevictable_allowed ? ISOLATE_UNEVICTABLE : 0) |
		(cc->mode != MIGRATE_SYNC ? ISOLATE_ASYNC_MIGRATE : 0);
	bool fast_find_block;

	/*
	 * Start at where we last stopped, or beginning of the zone as
	 * initialized by compact_zone(). The first failure will use
	 * the lowest PFN as the starting point for linear scanning.
	 */
	low_pfn = fast_find_migrateblock(cc); // 从上一次分配的地方开始遍历
	block_start_pfn = pageblock_start_pfn(low_pfn);
	if (block_start_pfn < cc->zone->zone_start_pfn)
		block_start_pfn = cc->zone->zone_start_pfn;

	/*
	 * fast_find_migrateblock() has already ensured the pageblock is not
	 * set with a skipped flag, so to avoid the isolation_suitable check
	 * below again, check whether the fast search was successful.
	 */
	fast_find_block = low_pfn != cc->migrate_pfn && !cc->fast_search_fail;

	/* Only scan within a pageblock boundary */
	block_end_pfn = pageblock_end_pfn(low_pfn);

	/*
	 * Iterate over whole pageblocks until we find the first suitable.
	 * Do not cross the free scanner.
	 */
	for (; block_end_pfn <= cc->free_pfn; // 结束条件之一
			fast_find_block = false,
			cc->migrate_pfn = low_pfn = block_end_pfn,
			block_start_pfn = block_end_pfn,
			block_end_pfn += pageblock_nr_pages) {

		...

    	// 返回 pageblock 的第一个 page
		page = pageblock_pfn_to_page(block_start_pfn,
						block_end_pfn, cc->zone);
 		...

		/*
		 * For async direct compaction, only scan the pageblocks of the
		 * same migratetype without huge pages. Async direct compaction
		 * is optimistic to see if the minimum amount of work satisfies
		 * the allocation. The cached PFN is updated as it's possible
		 * that all remaining blocks between source and target are
		 * unsuitable and the compaction scanners fail to meet.
		 */

    	// 判断是否可以迁移
    	// 对于异步类型的内存规整，只迁移 MIGRATE_MOVABLE 类型的 pageblock
    	// 对于同步类型的内存规整，只迁移类型和申请类型相同的 pageblock
		if (!suitable_migration_source(cc, page)) {
			update_cached_migrate(cc, block_end_pfn);
			continue;
		}

		/* Perform the isolation */
    	// 巨复杂的函数，功能就是执行分离操作
    	// 以 2^cc->order 粒度扫描整个 pageblock
    	// 如果该 page 还在 buddy 系统中，那么不适合迁移
    	// 混合页面也不适合迁移，如 THP 和 hugetlbfs 页面
    	// 不在 LRU 链表中的 page 不适合迁移
    	// 匿名页面锁在内存中，不适合迁移
    	// 去除上述 page 后，将能够迁移的 page 加到 cc->migratepages 链表中
		if (isolate_migratepages_block(cc, low_pfn, block_end_pfn,
						isolate_mode))
			return ISOLATE_ABORT;

		/*
		 * Either we isolated something and proceed with migration. Or
		 * we failed and compact_zone should decide if we should
		 * continue or not.
		 */
		break;
	}
	return cc->nr_migratepages ? ISOLATE_SUCCESS : ISOLATE_NONE;
}
```

#### 关键函数 isolate_migratepages_block

该函数需要处理内核中的各种 page，之后有需要再详细分析。

去掉不适合规整的 page，适合规整的 page 有如下两种：
- 传统的 LRU 页面，如匿名页和文件页；
- 非 LRU 页面，即特殊的可迁移页面，如根据 zsmalloc 机制和 virtio-balloon 机制分配的页面（？）；

### KSM[^13]

KSM 指 Kernel SamePage Merging，即内核同页合并，用于合并内容相同的页面。KSM 的出现是**为了优化虚拟化中产生的冗余页面**。KSM 允许合并同一个进程或不同进程之间内容相同的匿名页面，这对应用程序来说是不可见的。把这些相同的页面合并成一个只读的页面，从而释放出多余的物理页面，当应用程序需要改变页面内容时，**会发生写时复制**。

### SWAP

在外存中分配一块内存作为 swap 分区，在内存不足时，将使用频率较低的匿名页先写入到 swapcache，然后再交换 swap 分区，从而达到节省内存的目的。工作流程可以分为两步：

#### swapout

内核中的内存回收流程，最终都会走到 `shrink_page_list` 中，该函数对 page_list 链表中的内存依次处理，回收满足条件的内存。匿名页回收如下图所示，其回收需要经过两次 shrink。
- 第一次 shrink 时，内存页会通过 add_to_swap 分配到对应的 swap slot，设置为脏页并进行回写，最后将该 page 加入到 swapcache 中，但不进行回收；
- 第二次 shrink 时，若脏页已经回写完成，则将该 page 从 swapcache 中删除并回收。

#### swapin

当换出的内存产生缺页异常时，会通过 `do_swap_page` 函数查找到磁盘上的 slot，并将数据读回。

#### 关键函数 compact_zone

`compact_zone` 就是完成上面描述的扫描 zone （不过最新的内核不再以 zone 为扫描单元，而是以内存节点）的任务，找出可以迁移的页面和空闲页面，最终整理出大块内存。

### 慢路径分配

在[请求页框](#请求页框)介绍了快路径分配物理内存，

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
	...

	/* First allocation attempt */
    // 从空闲链表中分配内存，并返回第一个 page 的地址
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

	page = __alloc_pages_slowpath(alloc_gfp, order, &ac); // 若分配不成功，则通过慢路径分配

out:
	...

	return page;
}
EXPORT_SYMBOL(__alloc_pages);
```

但如果当前系统内存的水位在最低水位之下，那么 `__alloc_pages` 就会进入到慢路径分配内存，`__alloc_pages_slowpath` 也比较复杂，我们先看看整个的执行流程。

![alloc_pages_slowpath.png](https://github.com/UtopianFuture/UtopianFuture.github.io/blob/master/image/alloc_pages_slowpath.png?raw=true)

这其中涉及了众多内存优化机制，前面都有分析，这里我们结合代码再看一遍。

#### 关键函数__alloc_pages_slowpath

```C
static inline struct page *
__alloc_pages_slowpath(gfp_t gfp_mask, unsigned int order,
						struct alloc_context *ac)
{
	bool can_direct_reclaim = gfp_mask & __GFP_DIRECT_RECLAIM; // 该标志表示是否允许直接使用页面回收机制
    // 该标志表示会形成一定的内存分配压力，PAGE_ALLOC_COSTLY_ORDER 宏定义为 3
    // 那么当 order > 3 时机会给伙伴系统的分配带来一定的压力
	const bool costly_order = order > PAGE_ALLOC_COSTLY_ORDER;
	struct page *page = NULL;
	unsigned int alloc_flags;
	unsigned long did_some_progress;
	enum compact_priority compact_priority;
	enum compact_result compact_result;
	int compaction_retries;
	int no_progress_loops;
	unsigned int cpuset_mems_cookie;
	int reserve_flags;

	/*
	 * We also sanity check to catch abuse of atomic reserves being used by
	 * callers that are not in atomic context.
	 */
	if (WARN_ON_ONCE((gfp_mask & (__GFP_ATOMIC|__GFP_DIRECT_RECLAIM)) ==
				(__GFP_ATOMIC|__GFP_DIRECT_RECLAIM))) // 检查是否在非中断上下文中滥用 _GFP_ATOMIC 标志
		gfp_mask &= ~__GFP_ATOMIC; // 该标志表示允许访问部分系统预留内存

retry_cpuset:
	compaction_retries = 0;
	no_progress_loops = 0;
	compact_priority = DEF_COMPACT_PRIORITY;
	cpuset_mems_cookie = read_mems_allowed_begin();

	/*
	 * The fast path uses conservative alloc_flags to succeed only until
	 * kswapd needs to be woken up, and to avoid the cost of setting up
	 * alloc_flags precisely. So we do that now.
	 */
    // 重新设置分配掩码，因为在快路径中使用的是 ALLOC_WMARK_LOW 标志
    // 但是在 ALLOC_WMARK_LOW 下无法分配内存，所以才需要进入慢路径
    // 这里使用 ALLOC_WMARK_MIN 标志
    // 还有其他的标志位需要设置，如 __GFP_HIGH，__GFP_ATOMIC，__GFP_NOMEMALLOC 等等
	alloc_flags = gfp_to_alloc_flags(gfp_mask);

	/*
	 * We need to recalculate the starting point for the zonelist iterator
	 * because we might have used different nodemask in the fast path, or
	 * there was a cpuset modification and we are retrying - otherwise we
	 * could end up iterating over non-eligible zones endlessly.
	 */
	ac->preferred_zoneref = first_zones_zonelist(ac->zonelist,
					ac->highest_zoneidx, ac->nodemask);
	if (!ac->preferred_zoneref->zone)
		goto nopage;

	if (alloc_flags & ALLOC_KSWAPD) // 如果此次分配使用了 __GFP_NOMEMALLOC 标志，则唤醒 kswapd 内核线程
		wake_all_kswapds(order, gfp_mask, ac);

	/*
	 * The adjusted alloc_flags might result in immediate success, so try
	 * that first
	 */
    // 和 _alloc_pages 一样，不过这里使用的是最低水位 ALLOC_WMARK_MIN 分配
	page = get_page_from_freelist(gfp_mask, order, alloc_flags, ac);
	if (page) // 如果分配成功，则返回，不然进一步整理内存
		goto got_pg;

	/*
	 * For costly allocations, try direct compaction first, as it's likely
	 * that we have enough base pages and don't need to reclaim. For non-
	 * movable high-order allocations, do that as well, as compaction will
	 * try prevent permanent fragmentation by migrating from blocks of the
	 * same migratetype.
	 * Don't try this for allocations that are allowed to ignore
	 * watermarks, as the ALLOC_NO_WATERMARKS attempt didn't yet happen.
	 */
    // 如果使用最低水位还不能分配成功，那么当满足如下 3 个条件时考虑尝试使用直接内存规整机制来分配物理页面
    // 1. 能够调用直接内存回收机制
    // 2. costly_order
    // 3. 不能访问系统预留内存，原来 gfp_pfmemalloc_allowed 表示系统预留内存
	if (can_direct_reclaim && (costly_order ||
			   (order > 0 && ac->migratetype != MIGRATE_MOVABLE))
			&& !gfp_pfmemalloc_allowed(gfp_mask)) {
        // 调用 try_to_compact_pages 进行内存规整，然后再使用 get_page_from_freelist 尝试分配
		page = __alloc_pages_direct_compact(gfp_mask, order,
						alloc_flags, ac, INIT_COMPACT_PRIORITY,
						&compact_result);
		if (page)
			goto got_pg;

		...

	}

retry:
	/* Ensure kswapd doesn't accidentally go to sleep as long as we loop */
	if (alloc_flags & ALLOC_KSWAPD)
		wake_all_kswapds(order, gfp_mask, ac);

	reserve_flags = __gfp_pfmemalloc_flags(gfp_mask);
	if (reserve_flags)
		alloc_flags = gfp_to_alloc_flags_cma(gfp_mask, reserve_flags);

	/*
	 * Reset the nodemask and zonelist iterators if memory policies can be
	 * ignored. These allocations are high priority and system rather than
	 * user oriented.
	 */
	if (!(alloc_flags & ALLOC_CPUSET) || reserve_flags) {
		ac->nodemask = NULL;
		ac->preferred_zoneref = first_zones_zonelist(ac->zonelist,
					ac->highest_zoneidx, ac->nodemask);
	}

	/* Attempt with potentially adjusted zonelist and alloc_flags */
	page = get_page_from_freelist(gfp_mask, order, alloc_flags, ac);
	if (page)
		goto got_pg;

	/* Caller is not willing to reclaim, we can't balance anything */
	if (!can_direct_reclaim)
		goto nopage;

	/* Avoid recursion of direct reclaim */
	if (current->flags & PF_MEMALLOC)
		goto nopage;

	/* Try direct reclaim and then allocating */
	page = __alloc_pages_direct_reclaim(gfp_mask, order, alloc_flags, ac,
							&did_some_progress);
	if (page)
		goto got_pg;

	/* Try direct compaction and then allocating */
	page = __alloc_pages_direct_compact(gfp_mask, order, alloc_flags, ac,
					compact_priority, &compact_result);
	if (page)
		goto got_pg;

	/* Do not loop if specifically requested */
    // 已经尝试了使用系统预留内存、内存回收、内存重整但都没有成功，那么还可以重试几次
	if (gfp_mask & __GFP_NORETRY)
		goto nopage;

	/*
	 * Do not retry costly high order allocations unless they are
	 * __GFP_RETRY_MAYFAIL
	 */
	if (costly_order && !(gfp_mask & __GFP_RETRY_MAYFAIL)) // 又是一种失败情况
		goto nopage;

	if (should_reclaim_retry(gfp_mask, order, ac, alloc_flags, // 应该重试
				 did_some_progress > 0, &no_progress_loops))
		goto retry;

	/*
	 * It doesn't make any sense to retry for the compaction if the order-0
	 * reclaim is not able to make any progress because the current
	 * implementation of the compaction depends on the sufficient amount
	 * of free memory (see __compaction_suitable)
	 */
	if (did_some_progress > 0 &&
			should_compact_retry(ac, order, alloc_flags,
				compact_result, &compact_priority, &compaction_retries))
		goto retry;


	/* Deal with possible cpuset update races before we start OOM killing */
	if (check_retry_cpuset(cpuset_mems_cookie, ac))
		goto retry_cpuset;

	/* Reclaim has failed us, start killing things */
    // 调用 OOM Killer 机制来终止占用内存较多的进程，从而释放内存（内核考虑的真周全啊！）
	page = __alloc_pages_may_oom(gfp_mask, order, ac, &did_some_progress);
	if (page)
		goto got_pg;

	/* Avoid allocations with no watermarks from looping endlessly */
	if (tsk_is_oom_victim(current) && // 被释放的是当前进程（这不就尴尬了么）
	    (alloc_flags & ALLOC_OOM || (gfp_mask & __GFP_NOMEMALLOC)))
		goto nopage;

	/* Retry as long as the OOM killer is making progress */
	if (did_some_progress) { // 表示释放了一些内存，尝试重新分配
		no_progress_loops = 0;
		goto retry;
	}

nopage:
	/* Deal with possible cpuset update races before we fail */
	if (check_retry_cpuset(cpuset_mems_cookie, ac))
		goto retry_cpuset;

	...

		/*
		 * Help non-failing allocations by giving them access to memory
		 * reserves but do not use ALLOC_NO_WATERMARKS because this
		 * could deplete whole memory reserves which would just make
		 * the situation worse
		 */
		page = __alloc_pages_cpuset_fallback(gfp_mask, order, ALLOC_HARDER, ac);
		if (page)
			goto got_pg;

		cond_resched();
		goto retry;
	}
fail:
	warn_alloc(gfp_mask, ac->nodemask,
			"page allocation failure: order:%u", order);
got_pg:
	return page;
}
```

所以说内核为了能成功分配，简直“无所不用其极”，试了所有可能的方法，如果还是分配不出来，它也无能为力，只能 `warn_alloc` 。

#### 水位管理和分配优先级

伙伴系统通过 zone 的水位来管理内存。zone 的水位分为 3 个等级：高水位（WMARK_HIGH）、低水位（WMARK_LOW）和最低警戒水位（WMARK_MIN）。最低警戒水位以下是系统预留的内存，通常情况下普通优先级的分配请求是不能访问这些内存的。伙伴系统通过分配掩码的不同来访问最低警戒水位以下的内存，如 `__GFP_HIGH`、`__GFP_ATOMIC`、`__GFP_MEMALLOC` 等。

| 分配请求的优先级            | 判断条件                                          | 分配行为                                                     |
| --------------------------- | ------------------------------------------------- | ------------------------------------------------------------ |
| 正常                        | 如 GFP_KERNEL 或者 GFP_USER 等分配掩码            | 不能访问系统预留内存，只能使用最低警戒水位来判断是否完成本次分配请求 |
| 高（ALLOC_HIGH）            | __GFP_HIGH                                        | 表示这是一次优先级比较高的分配请求。可以访问最低警戒水位以下 1/2 的内存 |
| 艰难（ALLOC_HARDER)         | __GFP_ATOMIC 或实时进程                           | 表示需要分配页面的进程不能睡眠并且优先级较高。可以访问最低警戒水位以下 5/8 的内存 |
| OOM 进程（ALLOC_OOM）       | 若线程组有线程被 OOM 进程终止，就做适当补偿       | 用于补偿 OOM 进程或线程。可以访问最低警戒水位以下 3/4 的内存 |
| 紧急（ALLOC_NO_WATERMARKS） | __GFP_MEMALLOC 或者进程设置了 PF_MEMMALLOC 标志位 | 可以访问系统中所有的内存，包括全部的系统预留内存             |

整个内存管理流程如下图所示，前面没有介绍的是随着 kswapd 内核线程不断的回收内存，zone 中的空闲内存会越来越多，当 zone 水位重新返回高水位时，zone 的水位平衡了，kswapd 内核线程会进入睡眠状态。

![zone_wmark.png](https://github.com/UtopianFuture/UtopianFuture.github.io/blob/master/image/zone_wmark.png?raw=true)

### THP

### Mem cgroup[^12]

memcg 是 control group 的 subsys 之一，也是容器底层技术基石之一，其实现了内存资源的隔离与限制功能。这里主要分析 memcg 的计数(charge/uncharge)，进程迁移，softlimit reclaim，memcg reclaim的内核实现。

#### 数据结构

##### mem_cgroup

```c
/*
 * The memory controller data structure. The memory controller controls both
 * page cache and RSS per cgroup. We would eventually like to provide
 * statistics based on the statistics developed by Rik Van Riel for clock-pro,
 * to help the administrator determine what knobs to tune.
 */
struct mem_cgroup {
	struct cgroup_subsys_state css;
	/* Private memcg ID. Used to ID objects that outlive the cgroup */
	struct mem_cgroup_id id;
	/* Accounted resources */
	struct page_counter memory;		/* Both v1 & v2 */
	union {
		struct page_counter swap;	/* v2 only */
		struct page_counter memsw;	/* v1 only */
	};
	/* Range enforcement for interrupt charges */
	struct work_struct high_work;
#if defined(CONFIG_MEMCG_KMEM) && defined(CONFIG_ZSWAP)
	unsigned long zswap_max;
#endif
	unsigned long soft_limit;
	/* vmpressure notifications */
	struct vmpressure vmpressure;
	/*
	 * Should the OOM killer kill all belonging tasks, had it kill one?
	 */
	bool oom_group;
	/* protected by memcg_oom_lock */
	bool		oom_lock;
	int		under_oom;
	int	swappiness;
	/* OOM-Killer disable */
	int		oom_kill_disable;
	/* memory.events and memory.events.local */
	struct cgroup_file events_file;
	struct cgroup_file events_local_file;
	/* handle for "memory.swap.events" */
	struct cgroup_file swap_events_file;
	/* protect arrays of thresholds */
	struct mutex thresholds_lock;
	/* thresholds for memory usage. RCU-protected */
	struct mem_cgroup_thresholds thresholds;
	/* thresholds for mem+swap usage. RCU-protected */
	struct mem_cgroup_thresholds memsw_thresholds;
	/* For oom notifier event fd */
	struct list_head oom_notify;
	/*
	 * Should we move charges of a task when a task is moved into this
	 * mem_cgroup ? And what type of charges should we move ?
	 */
	unsigned long move_charge_at_immigrate;
	/* taken only while moving_account > 0 */
	spinlock_t		move_lock;
	unsigned long		move_lock_flags;
	CACHELINE_PADDING(_pad1_);
	/* memory.stat */
	struct memcg_vmstats	*vmstats;
	/* memory.events */
	atomic_long_t		memory_events[MEMCG_NR_MEMORY_EVENTS];
	atomic_long_t		memory_events_local[MEMCG_NR_MEMORY_EVENTS];
	/*
	 * Hint of reclaim pressure for socket memroy management. Note
	 * that this indicator should NOT be used in legacy cgroup mode
	 * where socket memory is accounted/charged separately.
	 */
	unsigned long		socket_pressure;
	/* Legacy tcp memory accounting */
	bool			tcpmem_active;
	int			tcpmem_pressure;
	...

	CACHELINE_PADDING(_pad2_);
	/*
	 * set > 0 if pages under this cgroup are moving to other cgroup.
	 */
	atomic_t		moving_account;
	struct task_struct	*move_lock_task;
	struct memcg_vmstats_percpu __percpu *vmstats_percpu;
	/* List of events which userspace want to receive */
	struct list_head event_list;
	spinlock_t event_list_lock;
	...

#ifdef CONFIG_LRU_GEN
	/* per-memcg mm_struct list */
	struct lru_gen_mm_list mm_list; // 分析 mglru 的时候经常遇到这个变量
#endif
	struct mem_cgroup_per_node *nodeinfo[];
};
```

##### memory_cgrp_subsys

```c
struct cgroup_subsys memory_cgrp_subsys = {
	.css_alloc = mem_cgroup_css_alloc,
	.css_online = mem_cgroup_css_online,
	.css_offline = mem_cgroup_css_offline,
	.css_released = mem_cgroup_css_released,
	.css_free = mem_cgroup_css_free,
	.css_reset = mem_cgroup_css_reset,
	.css_rstat_flush = mem_cgroup_css_rstat_flush,
	.can_attach = mem_cgroup_can_attach,
	.attach = mem_cgroup_attach,
	.cancel_attach = mem_cgroup_cancel_attach,
	.post_attach = mem_cgroup_move_task,
	.dfl_cftypes = memory_files,
	.legacy_cftypes = mem_cgroup_legacy_files,
	.early_init = 0,
};
```

每个 `cgroup_subsys` 都会在 `cgroup_init_early->cgroup_init_subsys` 中调用 `css_alloc` 回调函数进行初始化。在 `mem_cgroup_css_alloc` 函数中会调用 `mem_cgroup_alloc` 创建 `mem_cgroup` 变量，对应的在 `mem_cgroup_css_free` 中释放。创建的 mem_cgroup 变量后续可以通过 `cgroup_subsys_state` 和 `container_of` 来获取到。

#### charge/uncharge

```c
static int try_charge_memcg(struct mem_cgroup *memcg, gfp_t gfp_mask,
			unsigned int nr_pages)
{
	unsigned int batch = max(MEMCG_CHARGE_BATCH, nr_pages);
	int nr_retries = MAX_RECLAIM_RETRIES;
	struct mem_cgroup *mem_over_limit;
	struct page_counter *counter;
	unsigned long nr_reclaimed;
	bool passed_oom = false;
	unsigned int reclaim_options = MEMCG_RECLAIM_MAY_SWAP;
	bool drained = false;
	bool raised_max_event = false;
	unsigned long pflags;

	...

    // PSI 是统计系统运行信息的机制
    // 也就是说，每次触发 try_charge 都会回收内存
	psi_memstall_enter(&pflags);
	nr_reclaimed = try_to_free_mem_cgroup_pages(mem_over_limit, nr_pages,
						 gfp_mask, reclaim_options);
	psi_memstall_leave(&pflags);
	if (mem_cgroup_margin(mem_over_limit) >= nr_pages)
		goto retry;

	...

	/*
	 * keep retrying as long as the memcg oom killer is able to make
	 * a forward progress or bypass the charge if the oom killer
	 * couldn't make any progress.
	 */
	if (mem_cgroup_oom(mem_over_limit, gfp_mask,
			 get_order(nr_pages * PAGE_SIZE))) {
		passed_oom = true;
		nr_retries = MAX_RECLAIM_RETRIES;
		goto retry;
	}
	...

	return 0;
}
```
可以在这个函数上加断点，看哪些路径会调用到这里。

#### 进程迁移

#### softlimit reclaim

#### memcg reclaim

### 其他内存管理知识

#### PSI

Pressure Stall Information 提供了一种评估系统资源压力的方法。系统有三个基础资源：CPU、Memory 和 IO，无论这些资源配置如何增加，似乎永远无法满足软件的需求。一旦产生资源竞争，就有可能带来延迟增大，使用户体验到卡顿。

Android PSI 由三部分组成，lmkd、epoll 和 PSI 模块。
整个框架从系统启动开始流程为：

- PSI模块初始化。
- lmkd初始化 --- 上图中 ① 创建 epoll 队列；
- lmkd初始化 --- 上图中 ②③④ 组合循环三次注册三个阈值；
- lmkd初始化 --- ⑤ 等待事件通知；
初始化完成后 PSI 模块自行运作，内存相关信息的更新时机为调用 `psi_memstall_enter` 接口，如上图中灰色部分，主要在回收、规整、迁移流程中调用。

#### Huge page

页的默认大小一般为 4K，随着应用程序越来越庞大，使用的内存越来越多，内存的分配与地址翻译对性能的影响越加明显。试想，每次访问新的 4K 页面都会触发 Page Fault，**2M 的页面就需要触发 512 次才能完成分配**。

另外 TLB cache 的大小有限，过多的映射关系势必会产生 cacheline 的冲刷，被冲刷的虚拟地址下次访问时又会产生 TLB miss，又需要遍历页表才能获取物理地址。

对此，**Linux 内核提供了大页机制**。其采用 PMD entry 直接映射物理页，一次 Page Fault 可以直接分配并映射 2M 的大页，并且只需要一个 TLB entry 即可存储这 2M 内存的映射关系，**这样可以大幅提升内存分配与地址翻译的速度**。

因此，**一般推荐占用大内存应用程序使用大页机制分配内存**。当然大页也会有弊端：比如初始化耗时高，进程内存占用可能变高等。

可以使用 perf 工具对比进程使用大页前后的 PageFault 次数的变化：

```
perf stat -e page-faults -p-- sleep 5
```

目前内核提供了两种大页机制，一种是需要提前预留的静态大页形式，另一种是透明大页(THP, Transparent Huge Page) 形式。

##### 静态大页

首先来看静态大页，也叫做 HugeTLB。静态大页可以设置 cmdline 参数在系统启动阶段预留，比如指定大页 size 为 2M，一共预留 512 个这样的大页：

```
hugepagesz=2M hugepages=512
```

还可以在系统运行时动态预留，但该方式可能因为系统中没有足够的连续内存而预留失败。

```
echo 20 > /proc/sys/vm/nr_hugepages // 预留默认 size（可以通过 cmdline 参数 default_hugepagesz=指定size）的大页
echo 5 > /sys/kernel/mm/hugepages/hugepages-*/nr_hugepages // 预留特定 size 的大页
echo 5 > /sys/devices/system/node/node*/hugepages/hugepages-*/nr_hugepages // 预留特定 node 上的大页
```

当预留的大页个数小于已存在的个数，则会释放多余大页（前提是未被使用）。编程中可以使用 mmap(MAP_HUGETLB) 申请内存。详细使用可以参考[内核文档](https://www.kernel.org/doc/Documentation/admin-guide/mm/hugetlbpage.rst)。这种大页的优点是一旦预留成功，就可以满足进程的分配请求，还避免该部分内存被回收；缺点是：
- 需要用户显式地指定预留的大小和数量；
- 需要应用程序适配，比如：
 - mmap、shmget 时指定 MAP_HUGETLB；
 - 挂载 hugetlbfs，然后 open 并 mmap；
- 预留太多大页内存后，free 内存大幅减少，容易触发系统内存回收甚至 OOM；

##### 透明大页

透明大页机制在 THP always 模式下，会在 Page Fault 过程中，为符合要求的 vma 尽量分配大页进行映射。如果此时分配大页失败，比如整机物理内存碎片化严重，无法分配出连续的大页内存，那么就会 fallback 到普通的 4K 进行映射，但会记录下该进程的地址空间 mm_struct；**然后 THP 会在后台启动 khugepaged 线程，定期扫描这些记录的 mm_struct，并进行合页操作**。因为此时可能已经能分配出大页内存了，那么就可以将此前 fallback 的 4K 小页映射转换为大页映射，以提高程序性能。**整个过程完全不需要用户进程参与，对用户进程是透明的，因此称为透明大页**。

虽然透明大页使用起来非常方便、智能，但也有一定的代价：
- 进程内存占用可能远大所需，因为每次Page Fault 都尽量分配大页，即使此时应用程序只读写几KB；
- 可能造成性能抖动：
 - 在第 1 种进程内存占用可能远大所需的情况下，可能造成系统 free 内存更少，更容易触发内存回收，系统内存也更容易碎片化；
 - khugepaged 线程合页时，容易触发页面规整甚至内存回收，该过程费时费力，容易造成 sys cpu 上升；
 - mmap lock 本身是目前内核的一个性能瓶颈，应当尽量避免 write lock 的持有，但 THP 合页等操作都会持有写锁，且耗时较长（数据拷贝等），容易激化 mmap lock 锁竞争，影响性能；

因此 THP 还支持 madvise 模式，该模式需要**应用程序指定使用大页的地址范围，内核只对指定的地址范围做 THP 相关的操作**。这样可以更加针对性、更加细致地优化特定应用程序的性能，又不至于造成反向的负面影响。

可以通过 cmdline 参数和 sysfs 接口设置 THP 的模式：

cmdline 参数：

```
transparent_hugepage=madvise
```

sysfs 接口：

```
echo madvise > /sys/kernel/mm/transparent_hugepage/enabled
```

详细使用可以参考[内核文档](https://www.kernel.org/doc/Documentation/admin-guide/mm/transhuge.rst)。
HugeTLB 和 THP 最大的区别是 HugeTLB 是预分配的方式，而 THP 则是动态分配的方式。[Compound page](#Compound Page) 小结介绍了一些 compound page 的基础知识，接下来看一下如何利用 compound page 机制来实现 THP。

#### mmap_lock 锁[^5]

锁是内存管理中的一把知名的大锁，保护了诸如 mm_struct 结构体成员、 vm_area_struct 结构体成员、页表释放等很多变量与操作。

**mmap_lock 的实现是读写信号量，** 当写锁被持有时，所有的其他读锁与写锁路径都会被阻塞。Linux 内核已经尽可能减少了写锁的持有场景以及时间，但不少场景还是不可避免的需要持有写锁，比如 mmap 以及 munmap 路径、mremap 路径和 THP 转换大页映射路径等场景。

**应用程序应该避免频繁的调用会持有 mmap_lock 写锁的系统调用 (syscall)**， 比如有时可以使用 madvise（MADV_DONTNEED）释放物理内存，该参数下，madvise 相比 munmap 只持有 mmap_lock 的读锁，并且只释放物理内存，不会释放 VMA 区域，因此可以再次访问对应的虚拟地址范围，而不需要重新调用 mmap 函数。

**另外对于 MADV_DONTNEED，再次访问还是会触发 Page Fault 分配物理内存并填充页表，该操作也有一定的性能损耗。** 如果想进一步减少这部分损耗，可以改为 MADV_FREE 参数，该参数也只会持有 mmap_lock 的读锁，区别在于不会立刻释放物理内存，会等到内存紧张时才进行释放，如果在释放之前再次被访问则无需再次分配内存，进而提高内存访问速度。

一般 mmap_lock 锁竞争激烈会导致很多 D 状态进程（TASK_UNINTERRUPTIBLE），这些 D 进程都是进程组的其他线程在等待写锁释放。因此可以打印出所有 D 进程的调用栈，看是否有大量 mmap_lock 的等待。

```bash
for i in `ps -aux | grep " D" | awk '{ print $2}'`; do echo $i; cat /proc/$i/stack; done
```

内核社区专门封装了 mmap_lock 相关函数，并在其中[增加了 tracepoint](https://link.juejin.cn?target=https%3A%2F%2Fgithub.com%2Ftorvalds%2Flinux%2Fcommit%2F2b5067a8143e34aa3fa57a20fb8a3c40d905f942)，这样可以使用 bpftrace 等工具统计持有写锁的进程、调用栈等，方便排查问题，确定优化方向。

```ini
bpftrace -e 'tracepoint:mmap_lock:mmap_lock_start_locking /args->write == true/{ @[comm, kstack] = count();}'
```

#### 跨 numa 内存访问

#### footprint

#### zsmalloc/zRAM[^8]

当系统内存紧张的时候，会将文件页丢弃或写回磁盘（如果是脏页），还可能会触发 LMK 杀进程进行内存回收。这些被回收的内存如果再次使用都需要重新从磁盘读取，而这个过程涉及到较多的 IO 操作。频繁地做 IO 操作，会影响 flash 使用寿命和系统性能。内存压缩能够尽量减少由于内存紧张导致的 IO，提升性能。
目前内核主流的内存压缩技术主要有 3 种：zSwap, zRAM, zCache，这里主要介绍 zRAM。

##### 原理

zRAM 是 memory reclaim 的一部分，它的本质是时间换空间，通过 CPU 压缩、解压缩的开销换取更大的可用空间。

进行内存压缩的时机：

- Kswapd 场景：kswapd 是内核内存回收线程， 当内存 watermark 低于 low 水线时会被唤醒工作，其到内存 watermark 不小于 high 水线；

- Direct reclaim 场景：内存分配过程进入 slowpath，进行直接行内存回收。

#### Compound Page

复合页（Compound Page）就是将物理上连续的两个或多个页看成一个独立的大页，它可以用来创建 hugetlbfs 中使用的大页（hugepage）， 也可以用来创建透明大页（transparent huge page）子系统。但是它不能用在页缓存（page cache）中，这是因为页缓存中管理的都是单个页。

##### 使用

当 `alloc_pages` 或 `__get_free_pages` 等 buddy 系统分配内存接口的分配标志 `gfp_flags` 指定了 `__GFP_COMP`，那么内核必须将这些页组合成复合页 compound page。compound page 的尺寸要远大于当前分页系统支持的页面大小。并且一定是 `2^order * PAGE_SIZE` 大小。Compound page 的信息都存储在 `struct page` 中，

```c
struct page {
	unsigned long flags;		/* Atomic flags, some possibly
					 * updated asynchronously */
	union {
		...

		struct {	/* Tail pages of compound page */
			unsigned long compound_head;	/* Bit zero is set */
			/* First tail page only */
			unsigned char compound_dtor;
			unsigned char compound_order; // order
			atomic_t compound_mapcount;
			unsigned int compound_nr; /* 1 << compound_order */
		};
		struct {	/* Second tail page of compound page */
			unsigned long _compound_pad_1;	/* compound_head */
			atomic_t hpage_pinned_refcount;
			/* For both global and memcg */
			struct list_head deferred_list;
		};
		struct {	/* Page table pages */
			unsigned long _pt_pad_1;	/* compound_head */
			pgtable_t pmd_huge_pte; /* protected by page->ptl */
			unsigned long _pt_pad_2;	/* mapping */
			union {
				struct mm_struct *pt_mm; /* x86 pgds only */
				atomic_t pt_frag_refcount; /* powerpc */
			};
#if ALLOC_SPLIT_PTLOCKS
			spinlock_t *ptl;
#else
			spinlock_t ptl;
#endif
		};
	...

	};
	atomic_t _refcount;
};
```

`struct page` 中的 flag 标记用来识别复合页。在复合页中，打头的第一个普通页称为“head page”，用 `PG_head` 标记，而后面的所有页被称为“tail pages”，在 `compound_head` 变量中表示，同时这个 `compound_head` 还是一个指向 "head page" 的指针。

```c
void prep_compound_page(struct page *page, unsigned int order)
{
	int i;
	int nr_pages = 1 << order;
	__SetPageHead(page); // 用 gdb 看汇编代码，这里置上的是 0x10000 位，即 PG_head
	for (i = 1; i < nr_pages; i++) {
		struct page *p = page + i;
		p->mapping = TAIL_MAPPING;
		set_compound_head(p, page); // 剩余的 page 都是 tail page
	}
	set_compound_page_dtor(page, COMPOUND_PAGE_DTOR);
	set_compound_order(page, order);
	atomic_set(compound_mapcount_ptr(page), -1);
	if (hpage_pincount_available(page))
		atomic_set(compound_pincount_ptr(page), 0);
}

static __always_inline void set_compound_head(struct page *page, struct page *head)
{
    // compound_head 表示 head page，+1 是为了后面判断该 page 是否为 tail page
	WRITE_ONCE(page->compound_head, (unsigned long)head + 1);
}
```

可以使用 `PageCompound` 函数来检测一个页是否是复合页，另外函数 `PageHead` 和函数 `PageTail` 用来检测一个页是否是页头或者页尾。在每个尾页的 `struct page` 中都包含一个指向头页的指针 - first_page，可以使用 `compound_head` 宏获得。

```c
#define compound_head(page)	((typeof(page))_compound_head(page))
static inline unsigned long _compound_head(const struct page *page)
{
	unsigned long head = READ_ONCE(page->compound_head);
	if (unlikely(head & 1))
		return head - 1; // 在 set_compound_head 中 +1 了，为了获取正确的值，这里 -1，表示 head page 的地址
	return (unsigned long)page;
}

static __always_inline int PageTail(struct page *page)
{
	return READ_ONCE(page->compound_head) & 1; // 判断该 page 是否为 tail page
}

static __always_inline int PageCompound(struct page *page)
{
	return test_bit(PG_head, &page->flags) || PageTail(page); // 判断该 page 是否为 compound page
}
```

hugetlb（经典大页）就是通过 compound page 来实现的，我们可以看一下怎么使用的，

```c
static bool prep_compound_gigantic_page(struct page *page, unsigned int order)
{
	int i, j;
	int nr_pages = 1 << order;
	struct page *p = page + 1;
	/* we rely on prep_new_huge_page to set the destructor */
	set_compound_order(page, order); // 设置 compound_order
	__ClearPageReserved(page);
	__SetPageHead(page); // 第一个 page 为 head page
	for (i = 1; i < nr_pages; i++, p = mem_map_next(p, page, i)) {
		__ClearPageReserved(p);
		if (!page_ref_freeze(p, 1)) {
			pr_warn("HugeTLB page can not be used due to unexpected inflated ref count\n");
			goto out_error;
		}
		set_page_count(p, 0);
		set_compound_head(p, page); // 和上面的一样
	}
	atomic_set(compound_mapcount_ptr(page), -1);
	atomic_set(compound_pincount_ptr(page), 0);

	return true;
	...

}
```

##### 释放

compound page 的释放很简单，因为 `struct page` 中用 `compound_order` 记录了页面梳理，所以调用 `compound_order` 函数即可，

```c
void free_compound_page(struct page *page)
{
	mem_cgroup_uncharge(page); // memcg 计数
	free_the_page(page, compound_order(page)); // buddy 的内存释放接口
}

static inline unsigned int compound_order(struct page *page)
{
	if (!PageHead(page))
		return 0; // 不是 compound page
	return page[1].compound_order;
}
```

### 疑问

- `vm_area_alloc` 创建新的 VMA 为什么还会调用到 slab 分配器？

  上面已经解释过了，分配 `vm_area_alloc` 对象；

- vmalloc 中的 `vm_struct` 和 `vmap_area` 分别用来干嘛？

- 为什么 vmalloc 最后申请空间也要调用到 slab 分配器？（这样看来 `slab_alloc_node` 才是真核心啊）？

  使用 slab 申请小块的内存；

- 在处理匿名页面缺页异常时，为什么当需要分配的页面是只读属性时会将创建的 PTE 指向系统零页？

  因为这是匿名映射，没有指定对应的文件，所有没有内容，当进程再对该页进行写操作时，触发匿名页面的写操作异常，这时再通过 `alloc_zeroed_user_highpage_movable` 调用伙伴系统的接口分配新的页面。

- 以 `filemap_map_pages` 为切入点，应该就可以进一步分析文件管理部分的内容，这个之后再分析。

- 页面回收为何如此复杂？

- 如果在回收没有需要将 per-cpu cache 即 fbatch 中的 folio 调用 `folio_batch_move_lru` 写回到 lruvec->lrugen 中会有问题么？

  猜想是没问题的，在 `try_to_shrink_lruvec` 中也会调用，所以其他地方不调用应该问题不大。

### 内核编程技巧

- 获取不到资源就再尝试一次，说不定就可以了，在 alloc_pages, spin_lock, mutex 等核心模块都有这样干；

### Reference

[^1]: 奔跑吧 Linux 内核，卷 1：基础架构
[^2]: 内核版本：5.15-rc5，commitid: f6274b06e326d8471cdfb52595f989a90f5e888f
[^3]: https://zhuanlan.zhihu.com/p/65298260
[^4]: https://biscuitos.github.io/blog/MMU-Linux4x-VMALLOC/
[^5]: https://mp.weixin.qq.com/s/S0sc2aysc6aZ5kZCcpMVTw
[^6]: https://blog.csdn.net/tanzhe2017/article/details/81001507
[^7]: [CMA](http://www.wowotech.net/memory_management/cma.html)
[^8]: [zRAM](http://www.wowotech.net/memory_management/zram.html)
[^9]: [cma alloc](https://zhuanlan.zhihu.com/p/613016541)
[^10]: [arm64 memory](https://www.kernel.org/doc/Documentation/arm64/memory.rst)
[^11]: [mmap](https://www.cnblogs.com/binlovetech/p/17754173.html)
[^12]: [memcg](https://blog.csdn.net/bin_linux96/article/details/84328294)
[^13]: [ksm](https://lwn.net/Articles/953141/)

### 些许感想

1. 内存管理的学习是循序渐进的，开始可能只能将书中的内容稍加精简记录下来，随着看的内容越多，难免对前面的内容产生疑惑，这时再回过头去理解，同时补充自己的笔记。这样多来几次也就理解、记住了。所以最开始“抄”的过程对于我来说无法跳过；
2. 内存管理实在的复杂，要对这一模块进行优化势必要对其完全理解，不然跑着跑着一个 bug 都摸不到头脑，就像我现在调试 TLB 一样，不过要做到这一点何其困难；
