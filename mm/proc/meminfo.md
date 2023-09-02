MemTotal:       32382760 kB // 系统总内存大小，包括用于内核的内存和用户进程使用的内存。
MemFree:        11530244 kB // 系统未被使用的内存大小，即空闲内存大小。
MemAvailable:   27304420 kB // 系统当前可用的内存大小，与 MemFree 不同，MemAvailable 还考虑了缓存和页面回收等因素。
Buffers:         4446936 kB // 用于缓存块设备的内存大小，包括文件系统元数据和页面缓存。
Cached:          4647868 kB // 用于文件系统缓存的内存大小，包括文件数据和目录项缓存。
SwapCached:            0 kB // 用于 swap 缓存的内存大小，可以快速回收而不必写回磁盘。
Active:          6279372 kB // 活跃的内存大小，即正在被使用或最近被使用的内存大小。
Inactive:        5615984 kB // 不活跃的内存大小，即最近没有被使用的内存大小。
Active(anon):      14220 kB // 活跃的匿名内存大小，即没有被映射到文件的内存大小。
Inactive(anon):  3321760 kB // 不活跃的匿名内存大小，即最近没有被映射到文件的内存大小。
Active(file):    6265152 kB // 活跃的文件缓存内存大小，即被映射到文件的内存大小。
Inactive(file):  2294224 kB //
Unevictable:      472044 kB
Mlocked:              80 kB
SwapTotal:      10000380 kB // swap 总容量大小。
SwapFree:       10000380 kB
Dirty:                 8 kB // 已被修改但还未写回磁盘的页面大小。
Writeback:             0 kB // 正在被写回磁盘的页面大小。
AnonPages:       3272708 kB // 被映射到进程私有地址空间的页面大小，包括栈和堆等。
Mapped:          1111428 kB // 被映射到文件或共享库的页面大小。
Shmem:            546164 kB // 被共享内存或 tmpfs 等文件系统使用的页面大小。
KReclaimable:    7684548 kB
Slab:            8103988 kB
SReclaimable:    7684548 kB
SUnreclaim:       419440 kB
KernelStack:       17760 kB
PageTables:        49612 kB
NFS_Unstable:          0 kB
Bounce:                0 kB
WritebackTmp:          0 kB
CommitLimit:    26191760 kB
Committed_AS:   11162460 kB
VmallocTotal:   34359738367 kB
VmallocUsed:       53288 kB
VmallocChunk:          0 kB
Percpu:            23360 kB
HardwareCorrupted:     0 kB // no
AnonHugePages:         0 kB // no
ShmemHugePages:        0 kB // no
ShmemPmdMapped:        0 kB // no
FileHugePages:         0 kB // no
FilePmdMapped:         0 kB // no
HugePages_Total:       0 // no
HugePages_Free:        0 // no
HugePages_Rsvd:        0 // no
HugePages_Surp:        0 // no
Hugepagesize:       2048 kB // no
Hugetlb:               0 kB // no
DirectMap4k:      456764 kB
DirectMap2M:    10786816 kB
DirectMap1G:    22020096 kB
