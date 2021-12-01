### SLUB

伙伴管理算法（mem_init）初衷是解决**外部碎片**问题，而 slab 算法（kmem_cache_init）则是用于解决**内部碎片**问题，但是内存使用的得不合理终究会产生碎片。碎片问题产生后，申请大块连续内存将可能持续失败，但是实际上内存的空闲空间却是足够的。这时候就引入了不连续页面管理算法（vmalloc_init），即我们常用的 vmalloc 申请分配的内存空间，它主要是用于将不连续的页面，通过内存映射到连续的虚拟地址空间中，提供给申请者使用，由此实现内存的高利用。

This allocator (sitting on top of the low-level page allocator) manages caches of objects of a specific size, allowing for fast and space-efficient allocations.

The SLAB allocator: traditional allocator

The SLOB allocator: for embedded system

The SLUB allocator: default, for large system
