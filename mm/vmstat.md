```
// 1.
nr_free_pages 2795692 // 当前系统中空闲页面的数量
nr_zone_inactive_anon 848472 // 当前系统 zone 中的不活跃的匿名页数量
nr_zone_active_anon 3148
nr_zone_inactive_file 712553
nr_zone_active_file 1479174
nr_zone_unevictable 143860 // 不可驱逐的页面数量
nr_zone_write_pending 31 // 等待写回磁盘的页面数量

// 2.
nr_mlock 4 // 被用户程序锁定的页                                                                                                       // no
nr_bounce 0 // 用于 Bounce buffer 的页                                                                                                 // no
nr_zspages 0 // Zswap 用于压缩的页面数                                                                                                 // no
nr_free_cma 0 // 空闲的连续内存区域数                                                                                                  // no

//
nr_inactive_anon 848472 // 当前系统该 node 中已经不活跃的匿名页数量
nr_active_anon 3148 // 当前系统中活跃的匿名页数量
nr_inactive_file 712553 // 当前系统中已经不活跃的文件页数量
nr_active_file 1479174 // 当前系统中活跃的文件页数量
nr_unevictable 143860 // 当前系统中无法被驱逐的页数量

// 3.
nr_slab_reclaimable 1908835 // 可回收 SLAB 的页面数
nr_slab_unreclaimable 105270 // 不可回收 SLAB 的页面数
nr_isolated_anon 0 // 被隔离的匿名内存页面数的变量                                                                                     // no
nr_isolated_file 0 // 与文件页池隔离的页数                                                                                             // no
nr_anon_pages 818482 // 匿名页数量
nr_mapped 266459 // 当前内存中映射到进程地址空间的内存页的数量。
				 // 这些内存页通常是代码、库和共享对象等可执行文件中的页面，以及共享内存和匿名映射。
				 // 当进程执行映射到其地址空间时，内核会将这些页面加载到物理内存中。
nr_file_pages 2368456 // 文件缓存中已经缓存的文件页数
nr_dirty 31 // 当前的脏页数量                                                                                                          // no
nr_writeback 0 // 正在回写的页数                                                                                                       // no
nr_writeback_temp 0 // 临时回写到内存的页数                                                                                            // no
nr_shmem 178207 // 共享内存页数
nr_shmem_hugepages 0 // 共享大页数                                                                                                     // no
nr_shmem_pmdmapped 0 // PMD 映射的共享内存页数                                                                                         // no
nr_file_hugepages 0                                                                                                                    // no
nr_file_pmdmapped 0                                                                                                                    // no
nr_anon_transparent_hugepages 0 // 匿名透明大页数                                                                                      // no
nr_vmscan_write 0 // 被 vmstat 扫描并写回的页面数                                                                                      // no
nr_vmscan_immediate_reclaim 0 // 被 vmstat 扫描并立即回收的页面数                                                                      // no
nr_dirtied 2938440 // 最近一段时间内被标记为脏页的页面数量
nr_written 2166576 // 写回磁盘的页面数
nr_kernel_misc_reclaimable 0 // 可回收的内核内存页面的数量                                                                             // no
nr_foll_pin_acquired 0                                                                                                                 // no
nr_foll_pin_released 0                                                                                                                 // no
nr_kernel_stack 19056 // 内核栈占用的内存页数
nr_page_table_pages 15227 // 分配到页表的页数
nr_swapcached 0
nr_dirty_threshold 983743 // 脏页的阈值                                                                                                // no
nr_dirty_background_threshold 491271 // 脏页的后台阈值                                                                                 // no

// 5.
workingset_nodes 0 // 当前正在使用的工作集节点数                                                                                       // no
// workingset_nodes 变量表示当前使用的工作集节点数，即内核维护工作集的节点数。                                                         // no
// 当这个值很大时，说明内核在维护进程的工作集时需要花费更多的时间和资源，可能会影响系统的性能。                                        // no
// 因此，这个变量对于诊断系统内存使用问题非常有用。                                                                                    // no
workingset_refault_anon 0                                                                                                              // no
workingset_refault_file 0                                                                                                              // no
workingset_activate_anon 0                                                                                                             // no
workingset_activate_file 0                                                                                                             // no
workingset_restore_anon 0                                                                                                              // no
workingset_restore_file 0                                                                                                              // no
workingset_nodereclaim 0                                                                                                               // no

// 6. （放到 1. 中？）
pgpgin 7306865 // 从启动到现在从磁盘交换分区中读入到物理内存中的页面数量
pgpgout 10157029 // 从启动到现在从物理内存中写出到磁盘交换分区中的页面数量
// 当系统的物理内存不足时，操作系统会将一部分不常用的页面移动到磁盘上的交换分区中，以释放内存空间，
// 这个过程就是页面交换。如果 pswpin 和 pswpout 的值比较高，说明系统物理内存不足，需要进行频繁的页面交换，这会降低系统的性能。
pswpin 0 // 每秒从磁盘交换区读入的页面数
pswpout 0 // 每秒写回磁盘交换区的页面数

// 7.
pgalloc_dma 1024 // 从启动到现在 DMA 存储区分配的页数
pgalloc_dma32 1026
pgalloc_normal 34634712
pgalloc_movable 0

// 8.
// 内核已经尝试为DMA分配内存但失败了，这可能是因为系统中可用的DMA内存不足。
// 较高的 allocstall_dma 值可能意味着系统需要更多的DMA内存，或者可能暗示系统存在其他问题，例如设备驱动程序的错误。
allocstall_dma 0 // 内核在尝试为DMA（直接内存访问）分配内存时被阻塞的次数
allocstall_dma32 0
allocstall_normal 0
allocstall_movable 0

// 9.
// 在进行 DMA 操作时，操作系统通常会将需要访问的页面锁定在内存中，
// 以确保这些页面不会被其他进程或操作系统回收。如果 DMA 操作中需要跳过一些页面，
// 这些页面就不会被锁定在内存中，因此可能会被其他进程或操作系统回收，从而导致 DMA 操作失败或发生错误。
pgskip_dma 0 // 在 DMA 操作中跳过的页面数量
pgskip_dma32 0
pgskip_normal 0
pgskip_movable 0

// 10.
pgfree 38862450 // 从启动到现在释放的页数
pgactivate 1421961 // 从启动到现在激活的页数
// 当系统内存紧张时，操作系统会试图将不常用的页面移动到磁盘交换空间（swap space）或文件系统缓存中，
// 以释放物理内存供其他进程使用。pgdeactivate记录了这个过程中已经成功移动的页面数量，
// 它可以用来评估系统的内存压力和页面淘汰效率。
pgdeactivate 0 // 自系统启动以来，已经将活跃页面（active page）移动到非活跃页面（inactive page）的次数                                // no
pglazyfree 3861 // 已标记为“需要释放”的懒惰页面的数量。                                                                               // no
				// 懒惰页面是指已经被分配给某个进程，但当前未被使用，
				// 且在系统内存紧张的情况下可能会被回收以释放内存的页面。
				// 当这些页面被回收并释放时，pglazyfreed 的计数器就会增加。
pgfault 20517165 // 从启动到现在次 page fault 数
pgmajfault 8859 // 从启动到现在主 page fault 数
pglazyfreed 0                                                                                                                         // no
pgrefill 0                                                                                                                            // no
// pgrefill 记录了在页面缺失时，系统从何处获取页面。
// 具体地说，当进程访问一个尚未在内存中的页面时，会发生缺页中断。此时，系统会从下面几个地方获取一个页面并分配给该进程：
// (1) 从用户进程的堆栈区或内核栈区中分配
// (2) 从页缓存中读取
// (3) 从匿名页面的区域中分配
// (4) 从文件映射的区域中读取
// 如果页面是从匿名页面或文件映射的区域中分配的，那么 pgrefill 的计数器就会增加。
pgreuse 2001935                                                                                                                       // no
// 而 pgreuse 记录的是在页面释放时，系统将页面加入哪个列表中，以便后续可以再次被使用。
// 具体地说，当进程释放一个页面时，该页面可能被添加到下面几个列表中的一个：
// (1) LRU 活动链表：表示该页面最近被使用过，系统认为该页面有较高的概率再次被使用。
// (2) LRU 非活动链表：表示该页面已经很久没有被使用过，但是还未被回收，系统认为该页面仍有一定的概率再次被使用。
// (3) 其他内存池或者页面回收器的缓存：一些内存池或者回收器会自行维护自己的空闲页面列表，用于快速分配和回收页面。
// 如果页面被添加到 LRU 活动链表或者 LRU 非活动链表中，那么 pgreuse 的计数器就会增加。
pgsteal_kswapd 0 // 从进程驻留内存中回收的页面数，由 kswapd 进程执行                                                                  // no
pgsteal_direct 0 // 从进程驻留内存中回收的页面数，直接回收                                                                            // no
pgdemote_kswapd 0                                                                                                                     // no
// 表示 kswapd 进程在执行页面置换时，将页面从 LRU 活跃列表中降级（demote）到 LRU 不活跃列表中的页面数量。
// kswapd 进程是负责后台执行页面置换的守护进程，当系统内存不足时，它会尝试将不常用的页面换出到磁盘上，以释放内存。
// pgdemote_kswapd 的值越高，说明 kswapd 进程需要降级更多的页面，也就意味着系统内存不足的情况更加频繁。
pgdemote_direct 0                                                                                                                     // no
// 表示在执行页面置换时，直接从 LRU 活跃列表中降级到不活跃列表中的页面数量。
// 和 pgdemote_kswapd 不同的是，pgdemote_direct 统计的是由进程直接引起的页面置换，而不是由 kswapd 进程引起的。
// pgdemote_direct 的值越高，说明进程频繁将不常用的页面释放，也就意味着系统内存不足的情况更加频繁。
pgscan_kswapd 0 // 从启动到现在 kswapd 守护进程扫描的页面数                                                                           // no
pgscan_direct 0 // 直接回收页面时扫描的页面数量                                                                                       // no
pgscan_direct_throttle 0 // 直接回收页面时被限制的页面扫描数。                                                                        // no
						 // 当系统内存不足时，Linux会限制直接回收页面的速度，
						 // 以避免在短时间内回收过多的页面，影响系统性能。
						 // 该变量记录了因限制直接回收速度而导致的被限制的页面扫描数量。
pgscan_anon 0 // 匿名页面扫描的页面数                                                                                                 // no
pgscan_file 0                                                                                                                         // no
pgsteal_anon 0                                                                                                                        // no
pgsteal_file 0 // 从文件页中回收的页面数                                                                                              // no
zone_reclaim_failed 0                                                                                                                 // no
pginodesteal 0 // 从启动到现在通过释放 i 节点回收的页面数                                                                             // no
slabs_scanned 0 // 从启动到现在被扫面的 slab 数(?)                                                                                    // no
kswapd_inodesteal 0                                                                                                                   // no
kswapd_low_wmark_hit_quickly 0
// 一个内核日志消息，表示系统中的内存压力已经导致内核的 kswapd 守护进程在不久前被启动，
// 而且已经接近或达到了系统的最小可用空闲内存阈值（low water mark）。
// 当系统的可用内存降至此水平以下时，kswapd 将开始在内存中移动页面，以尝试回收内存并避免出现内存不足的情况。
// 如果 kswapd 在不久前就被启动了，并且可用内存已接近最小阈值，这意味着内存压力很高，系统可能正在经历内存不足的情况。
// 如果这种情况经常发生，可能需要增加系统的可用内存或优化应用程序和内核的内存使用情况。
kswapd_high_wmark_hit_quickly 0
pageoutrun 0
pgrotated 8
drop_pagecache 0 //
drop_slab 0 //
oom_kill 0 // oom_kill 执行次数

//
numa_pte_updates 0
numa_huge_pte_updates 0
numa_hint_faults 0
numa_hint_faults_local 0
numa_pages_migrated 0

//
pgmigrate_success 0
pgmigrate_fail 0
thp_migration_success 0 // 透明大页合并成功次数
thp_migration_fail 0
thp_migration_split 0

//
compact_migrate_scanned 0 // 在做 compact 时已扫描以可移动页面的物理页面数量
compact_free_scanned 0
compact_isolated 0
compact_stall 0
compact_fail 0
compact_success 0
compact_daemon_wake 0
compact_daemon_migrate_scanned 0
compact_daemon_free_scanned 0

//
htlb_buddy_alloc_success 0 // 分配一个HugeTLB页面时成功分配的次数
htlb_buddy_alloc_fail 0

//
unevictable_pgs_culled 1753878 // 已经被清除的无法删除页的计数器(?)
unevictable_pgs_scanned 1556189
unevictable_pgs_rescued 1556198
unevictable_pgs_mlocked 182
unevictable_pgs_munlocked 178
unevictable_pgs_cleared 0
unevictable_pgs_stranded 0

//
thp_fault_alloc 6
thp_fault_fallback 0
thp_fault_fallback_charge 0
thp_collapse_alloc 0
thp_collapse_alloc_failed 0
thp_file_alloc 0
thp_file_fallback 0
thp_file_fallback_charge 0
thp_file_mapped 0
thp_split_page 0
thp_split_page_failed 0
thp_deferred_split_page 1
thp_split_pmd 1
thp_split_pud 0
thp_zero_page_alloc 0
thp_zero_page_alloc_failed 0
thp_swpout 0
thp_swpout_fallback 0

//
// 在虚拟化环境中，balloon driver 是一种内存管理技术，用于调整虚拟机的内存大小。
// 当虚拟机需要更多内存时，balloon driver 会从宿主机上释放未使用的内存给虚拟机使用，
// 而当虚拟机释放内存时，balloon driver 会将内存返回给宿主机。
balloon_inflate 0 // balloon driver 请求更多内存的次数
balloon_deflate 0 // balloon driver 释放内存的次数
balloon_migrate 0 // balloon driver 将内存从一个节点移到另一个节点的次数

//
swap_ra 0 //  指操作系统在请求访问交换分区时，触发读取数据到内存的请求数量（即读取操作的请求数量）
swap_ra_hit 0 // 指从交换分区中读取数据到内存时，从系统自带的缓存中命中的数量（即命中缓存的请求数量）
// Direct Mapping Area 是内核在初始化的时候，直接映射到物理内存的部分虚拟地址空间，以方便内核访问物理内存。
// 由于 Direct Mapping Area 是一个固定的映射，因此随着系统中物理内存的增加或减少，
// 内核需要动态地重新分配 Direct Mapping Area，以便能够访问所有物理内存。
// 这就会导致 Direct Mapping Area 的大小变化，如果 Direct Mapping Area 的大小发生变化，内核需要对其进行重新分裂。

// direct_map_level2_splits 表示第二级 Direct Mapping Area 被分裂的次数，
// direct_map_level3_splits 表示第三级 Direct Mapping Area 被分裂的次数。
// 们通常用于调优 Direct Mapping 区域的大小，以及保证内核使用的虚拟地址空间不会超过一定的限制。
direct_map_level2_splits 297
direct_map_level3_splits 17
nr_unstable 0 // 不稳定页(?)
```
