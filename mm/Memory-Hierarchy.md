内存管理

从社招角度来看，怎样才是一个 NB 的员工，就内存管理领域来看：

- 基础。这个基础是多方面的，语言的精准把握，对整个内核的掌控，例如内存的回收、释放过程，要有深度；
- 项目能力。对做过项目的细节把握，以及结合业务场景的整个解决流程，这是体现能力的地方；
- 眼界。内核、芯片领域的新技术、其他竞对的解决方案。

研一的时候其实看过《计算机体系结构：量化研究方法》，当时看的是英文版，个人感觉收获蛮大，算是我后来一切工作的基础。如今有机会继续从事这方面的工作，很大程度上可以归结为运气好。
现在需要深入研究内核的内存管理模块，所有书中有关内存的只是需要回顾一下，原来的[笔记](https://utopianfuture.github.io/Architecture/A-Quantitative-Approach.html)在这里。略显杂乱，看完之后没有进一步归纳总结，这里算是之前工作的进一步。

### 内存属性

ARM 架构处理器提供两种类型的内存，

- normal memory：没有额外的约束，提供最高的内存访问性能。通常代码段、数据段及其他数据都会放在 normal memory 中，其可以做很多优化，如分支预测、数据预取、cacheline 预取和填充、乱序加载等；
- device memory：不能进行预测访问等，严格按照执行顺序来执行，通常留给设备来访问。ARM 架构定义了多种设备内存的属性：
 - Device-nGnRnE;
 - Device-nGnRE;
 - Device-nGRE;
 - Device-GRE;
 这些内存属性有特殊的含义：
 - G 和 nG：表示聚合（Gathering）与不聚合（non Gathering）。聚合表示在同一个内存属性的区域中允许把多次访问内存的操作合并成一次总线传输；如果一个内存地址被标记为 nG 属性，其会严格按照访问内存的次数和大小来访问内存，不会做合并优化；
 - R 和 nR：表示指令重排（Re-ordering）与不重排（non Re-ordering）；
 - E 和 nE：表示提前写应答（Early Write Acknowledgement）和不提前写应答（non Early Write Acknowledgement）。往外部设备写数据时，处理器先把数据写入写缓冲区（write buffer），若使能了提前写应答，则数据到达写缓冲区时会发送写应答，否则数据达到外设时才会发送写应答。

ARM64 提供如下几种内存属性：

```c
#define PROT_DEVICE_nGnRnE	(PROT_DEFAULT | PTE_PXN | PTE_UXN | PTE_WRITE | PTE_ATTRINDX(MT_DEVICE_nGnRnE))
#define PROT_DEVICE_nGnRE	(PROT_DEFAULT | PTE_PXN | PTE_UXN | PTE_WRITE | PTE_ATTRINDX(MT_DEVICE_nGnRE))
#define PROT_NORMAL_NC		(PROT_DEFAULT | PTE_PXN | PTE_UXN | PTE_WRITE | PTE_ATTRINDX(MT_NORMAL_NC))
#define PROT_NORMAL		(PROT_DEFAULT | PTE_PXN | PTE_UXN | PTE_WRITE | PTE_ATTRINDX(MT_NORMAL))
#define PROT_NORMAL_TAGGED	(PROT_DEFAULT | PTE_PXN | PTE_UXN | PTE_WRITE | PTE_ATTRINDX(MT_NORMAL_TAGGED))
#define PROT_SECT_DEVICE_nGnRE	(PROT_SECT_DEFAULT | PMD_SECT_PXN | PMD_SECT_UXN | PMD_ATTRINDX(MT_DEVICE_nGnRE))
#define PROT_SECT_NORMAL	(PROT_SECT_DEFAULT | PMD_SECT_PXN | PMD_SECT_UXN | PMD_ATTRINDX(MT_NORMAL))
#define PROT_SECT_NORMAL_EXEC	(PROT_SECT_DEFAULT | PMD_SECT_UXN | PMD_ATTRINDX(MT_NORMAL))
#define _PAGE_DEFAULT		(_PROT_DEFAULT | PTE_ATTRINDX(MT_NORMAL))
#define PAGE_KERNEL		__pgprot(PROT_NORMAL)
#define PAGE_KERNEL_RO		__pgprot((PROT_NORMAL & ~PTE_WRITE) | PTE_RDONLY)
#define PAGE_KERNEL_ROX		__pgprot((PROT_NORMAL & ~(PTE_WRITE | PTE_PXN)) | PTE_RDONLY)
#define PAGE_KERNEL_EXEC	__pgprot(PROT_NORMAL & ~PTE_PXN)
#define PAGE_KERNEL_EXEC_CONT	__pgprot((PROT_NORMAL & ~PTE_PXN) | PTE_CONT)
```

当需要映射内存给设备使用时，通常会使用 DEVICE 相关的属性，如内核中使用 ioremap 相关接口将外部设备的内存映射到内核地址空间中，
- ioremap 使用场景为映射 device memory 类型内存；
- ioremap_cache，使用场景为映射 normal memory 类型内存，且映射后的虚拟内存支持 cache（但不是所有的系统都实现了）；
- ioremap_wc & ioremap_wt 实现相同，使用场景为映射 normal memory 类型内存，且映射后的虚拟内存不支持 cache，一种是 writecombine，一种是 writethrogh；
- memremap(pbase, size, MEMREMAP_WB)；
- memremap(pbase, size, MEMREMAP_WC)；

```c
// 都是同一个接口，只是配置的属性不同
#define ioremap(addr, size)		__ioremap((addr), (size), __pgprot(PROT_DEVICE_nGnRE))

// 对于预留内存来说，如果是 map 属性，使用这个接口会有问题，因为 __ioremap 只能映射 no-map 属性的内存
// 其会做一个检查，pfn_is_map_memory

#define ioremap_wc(addr, size)		__ioremap((addr), (size), __pgprot(PROT_NORMAL_NC))
#define ioremap_np(addr, size)		__ioremap((addr), (size), __pgprot(PROT_DEVICE_nGnRnE))

// map 属性使用这个接口没问题，因为 map 属性内核已经为其建立好映射（在 MMU 中），所以这里直接转化为虚拟地址即可
// 这个接口映射的就是 NORMAL 属性，表示 cacheable
void __iomem *ioremap_cache(phys_addr_t phys_addr, size_t size)
{
	/* For normal memory we already have a cacheable mapping. */
	if (pfn_is_map_memory(__phys_to_pfn(phys_addr)))
		return (void __iomem *)__phys_to_virt(phys_addr);
	return __ioremap_caller(phys_addr, size, __pgprot(PROT_NORMAL),
				__builtin_return_address(0));
}

#ifndef ioremap_wt
#define ioremap_wt ioremap
#endif

// memremap 支持多种属性
// MEMREMAP_WB = 1 << 0,
// MEMREMAP_WT = 1 << 1,
// MEMREMAP_WC = 1 << 2,z
// MEMREMAP_ENC = 1 << 3,
// MEMREMAP_DEC = 1 << 4,
void *memremap(resource_size_t offset, size_t size, unsigned long flags);
void memunmap(void *addr);
```

从代码上来看，ioremap, ioremap_cache, ioremap_wt 的底层实现都是 ioremap，即将虚拟地址映射为 device memory 类型的内存。而 memremap 也是 ioremap 的封装。

#### cache 的共享属性

cache 可以分为如下共享属性：
- 不可共享的(non-shareable)：表示这个内存区域只能被一个处理器访问，其他处理器无法访问这个内存区域；
- 内部共享的(inner-shareable)：表示它可以被多个处理器访问和共享，但是系统中其他能够访问内存的硬件就不能访问了，如 DMA 设备、GPU 等；
- 外部共享的(outer-shareable)：表示系统中很多访问内存的单元（DMA，GPU 等）都可以和处理器一样访问这个内存区域。

### cache 结构

#### 直接映射

#### 组相联映射

#### 全相连映射

#### PIPT 和 VIVT 的区别

在查询 cache 时，使用的是虚拟地址还是物理地址？不同处理器使用的不一样。

##### 物理高速缓存

当处理器查询 MMU 和 TLB 并得到物理地址之后，使用物理地址查询 cache，这种 cache 称为物理高速缓存。使用这种 cache 的缺点就是慢，因为需要等 MMU 和 TLB 完成地址转化后才能查询 cache。

##### 虚拟高速缓存

即使用虚拟地址来寻址。但是这会导致如下问题：

- 重名(aliasing)问题。多个不同的虚拟地址映射到相同的物理地址，因此会出现多个 cacheline 存储同一个物理地址（对应多个虚拟地址）的情况。这样会引发问题。
 - 浪费 cacheline；
 - 当执行写操作时，只更新了其中一个虚拟地址对应的 cacheline，而其他的虚拟地址对应的高速缓存没有更新，导致之后访问到异常数据；
- 同名(homonyms)问题。相同的虚拟地址对应不同的物理地址（多个进程使用相同的虚拟地址进行映射），而虚拟地址经过 MMU 转换后对应不同的物理地址。在进程切换时可能发生，解决办法是**切换时刷新该进程对应的所有 cacheline**。

#### cache 分类

在查询 cache 时，根据使用虚拟地址还是物理地址来查询 tag 域或 index 域，我们可以将 cache 分成如下几类：
- VIVT：使用虚拟地址的 tag 域和 index 域；
- PIPT：使用物理地址的 tag 域和 index 域；
- VIPT：使用虚拟地址的 index 域和物理地址的 tag 域。虚拟地址同时发送到 cache 和 MMU/TLB 进行处理。在 TLB/MMU 进行地址翻译时，使用虚拟地址的索引域和偏移量来查询高速缓存。当地址翻译结束，再使用物理标记域来匹配 cacheline。VIPT 也存在重名问题。

### MESI 协议

目前处理器的 cache 结构一般是 L1 D-cache, L1-Icache, L2-cache 和共享的 L3-cache，在这些 cache 上会存在同一个数据的多个副本，因此需要 cache 一致性协议去维护不同数据副本之间的一致。

实现一个 cache 协议的关键就是跟中每一块共享内存的状态，这些状态用 bit 位表示，而这些 bit 位也保存在内存块中。cache 一致性协议可分为两类：

- 监听协议(snooping protocol)，每个 cache 都要被监听或监听其他 cache 的总线活动（适合 UMA 架构）。目前业界实现的监听协议都是 write invalidate protocol，即当本地 CPU 更改了 cache line，所有其他 CPU 持有的该 cache line 都会被置为 invalidate。与之对应的是 write update protocol，但是这种协议对总线带宽要求高，不怎么使用。而实现一种 write invalidate 的 cache 一致性协议的关键是使用好 bus，或者其他广播介质（目前应该只有总线）。CPU 只负责申请总线使用权限，并总线发起 invalidate 请求。如果多个 CPU 同时对一个 cache line 发起访问，那么需要总线进行仲裁；
- 目录协议(directory protocol)，将所有的 cache line status 保存在 directory 中，全局统一管理 cache 状态（和 snooping protocol 结合，用于 NUMA 架构），directory 可以放置在最外层 cache；

关于这两种协议的介绍，网上的资料大多不准确，建议看《量化研究方法》第五章。

目前广泛使用的是 MESI(Modified, Exclusive, Shared, Invalid)协议，一种总线监听协议。协议中的 4 中状态（这些状态是不是存储在 cache line 中）：

| 状态 | 说明                                                         |
| ---- | ------------------------------------------------------------ |
| M    | 该 cache line 有效，数据已被修改，和内存中的数据不一致，数据只存在于该 cache line 中 |
| E    | 该 cache line 有效，数据和内存中的一致，只存在于该 cache line 中 |
| S    | 该 cache line 有效，数据和内存中的一致，多个 cache line 持有该数据副本 |
| I    | 该 cache line 无效                                           |

MESI 协议在总线上的操作分为本地读写和总线操作。本地读写指的是本地 CPU 读写自己私有的 cache，总线读写是指 CPU 发送请求到总线上，所有的 CPU 都可以收到这个请求。MESI 的操作类型如下所示：

| 操作类型               | 描述                                                         |
| ---------------------- | ------------------------------------------------------------ |
| 本地读（PrRd）         | 本地 CPU 读 cache line                                       |
| 本地写（PrWr）         | 本地 CPU 写 cache line                                       |
| 总线读（BusRd）        | 总线监听到一个来自其他 CPU 的读 cache line 请求。收到信号的 CPU 先检查自己的 cache line 中是否有该数据，然后广播应答信号 |
| 总线写（BusRdX）       | 总线监听到一个来自其他 CPU 的写 cache line 请求。收到信号的 CPU 先检查自己的 cache line 中是否有该数据，然后广播应答信号 |
| 总线更新（BusUpge）    | 总线监听到更新请求，请求其他 CPU 做一些操作。其他 CPU 收到请求后，若自己的 cache line 中是有该数据，则需要做无效 cache line 等操作 |
| 刷新（Flush）          | 总线监听到刷新请求。收到请求的 CPU 把自己的 cache line 数据写回 mm 中 |
| 刷新到总线（FlushOpt） | 收到该请求的 CPU 会将 cache line 数据发送到总线上，这样发送请求的 CPU 就可以获取该 cache line 内容 |

下图为 MESI 协议中各个状态的转换关系。

![img](https://raw.githubusercontent.com/UtopianFuture/UtopianFuture.github.io/3a55d7431904d9d6b3e16058f5652b405460a161/image/MESI.svg)

### cache 优化

#### 3 种缺失率

一般采用缺失率来衡量不同缓存组织方式的优劣，可以将缺失率分为三个类别：

- 强制(Compulsory)：对一个数据块的第一次访问，这个块不可能在缓存中，必然会导致强制缺失；
- 容量(Capacity)：缓存容量不足，无法容纳进程运行的全部块，就需要选择块写回内存，并写入新的块；
- 冲突(Conflict)：直接映射和组相连映射都会导致多个块映射到同一个缓存行中，需要进行替换；

#### 6 种基本优化方法

- 增大块以降低缺失率。最简单的方法，能够降低强制缺失，并且减少了标记数目，能够略微降低静态功耗（？），但是会增大冲突缺失；
- 增大缓存。能够降低容量缺失，但是增大缓存命中时间（需要在遍历一个组中的所有块），同时增加成本和功耗；
- 提高相连程度。这个极端就是全相连映射，不会有冲突缺失，但是命中时间长，同时也会增加功耗（？）；
- 采用多级缓存。现在架构都是 L1 I-cache, L1 D-cache, L2 cache, L3 cache；
- 读取缺失优先级高于写入操作。写入操作应该是指从 write buffer 写入到内存，而写缓冲区中可能会存在读取操作需要的数据。如果 read miss 优先级高于写入操作，那么在 read miss 时，先检查 write buffer，这样能够降低 read miss penatly，但是需要检查 write buffer 和内存数据是否有冲突；
- 在缓存索引期间避免地址转换。现在索引都是使用虚拟地址，但需要处理别名、同名问题，后面详细介绍。

#### 10 种高级优化方法

首先可以将这 10 中优化方法分为 5 类：

- 减少命中时间。小而简单的一级 cache 和路预测算法，两种方法都能降低能量消耗；
- 增加 cache 带宽。缓存访问的流水线化，multibanked caches 和无阻塞 cache，这几种方法都对功耗有不同程度的影响；
- 降低 miss 代价。关键字优先和合并缓冲区，它们对功耗也有些许影响；
- 降低 miss 率。寄存器优化，在编译时的优化能够极大的降低功耗；
- 通过并行降低 miss 代价或 miss 率。这个主要是硬件预取和编译器预取，但是这回增加功耗，尤其是在预取的数据无效时。

##### 1. 小而简单的一级 cache 降低命中时间和功耗

L1 cache 的关键是快，要尽可能接近 CPU 的频率，同时考虑到功率的影响，L1 cache 不可能做的很大。下面两个图分别是 cache 大小，相连程度同相对访问时间的关系与 cache 大小，相连程度同耗能之间的关系。

![quantitative-1.jpg](https://github.com/UtopianFuture/UtopianFuture.github.io/blob/master/image/quantitative-1.jpg?raw=true)

![quantitative-2.jpg](https://github.com/UtopianFuture/UtopianFuture.github.io/blob/master/image/quantitative-2.jpg?raw=true)

##### 2. 采用路预测缩短命中时间

路预测算法在 cache 中使用额外的 bit 来预测下一次 cache 访问的路。多路选择器会提前选择需要访问的 block，在访问时之做简单的 tag 比较。如果预测成功，那么下次访问的 cache 访问延迟就是快速命中时间（？），如果没有成功，那么多路选择器将会比较其他的 block，并修改路预测器，这需要额外的一个时钟周期。

模拟器的测试结果表明，在 2 路组相联中，路预测的成功率超过 90%，在 4 路组相连中超过 80%，并且在 I-cache 中的成功率高于 D-cache。对于高性能的处理器，设计预测失败时一个时钟周期的暂停时间是关键。

##### 3. cache 访问的流水线化和 multibanks cache 以增加带宽

缓存访问的流水线化能够增加 cache 带宽，多组 cache 能够在一个时钟周期内进行多个访问。这两项优化技术最初是用在 L1 cache 上的，能够增加指令带宽，现在多组 cache 也用在 L2 和 L3 cache 上了，主要作为能耗管理（？）。

L1 cache 访问的流水线化可以支持高时钟频率（也就是将一次访问 cache 分成多个阶段，适用于多级流水线的处理器），但是也会增加访问延迟。例如采用了 I-cache 流水线化访问的奔腾系列处理器，在 90 年代中期一次访问是 1 个时钟周期，到 2000 年的奔腾 2 处理器，需要 2 个时钟周期，而现在的 Core i7 处理器，则需要 4 个时钟周期。但是高时钟频率会导致分支预测失败的代价非常大。目前大部分高性能处理器采用 3-4 级 cache 访问流水线。

处理 I-cache 访问流水线化要比 D-cache 简单，I-cache 可以依靠高效的分支预测技术一次读取多条指令，但是 D-cache 会有读、写同时进行的问题，所以将 cache 分成多个不同的 bank，每个 bank 都可以独立的响应访存操作。这一技术最开始是用在 DRAM 上的（确实，包括 SSD 都是分成多个 die 之类的，每个可以独立操作）。

在 L2 和 L3 cache 中，也会使用 multibanks 技术，这样如果没有在访问 bank 时造成冲突，就能够同时处理多个 L1 miss，这是支持下一种优化方法的关键技术。

##### 4. 无阻塞 cache 以增加带宽

对于乱序多发射处理器而言，处理器需要能够在发生 D-cache miss 时继续执行，而不是阻塞。无阻塞 cache 就能够做到在发生 D-cache 时继续 cache hit，这样一方面能够减少 D-cache miss 时发生的性能损失（ CPU 阻塞），一方面降低了能耗。一般将这种优化称为 "hit under miss"。

更进一步的方式是 "hit under multiple miss" 或 "miss under miss"，大多数高性能处理器支持这些优化，如 Core 系列处理器。

通常来说，乱序发射处理器能够处理 L1 D-cache miss 而 L2 cache hit 的情况，但是无法处理很多低等级 cache miss 的情况。处理器处理 miss 次数的能力取决于如下几种情况：

- cache miss 之间的时间局部性和空间局部性，这决定了是否能够发起一次新的低等级 cache 或内存访问；
- cache 或内存的带宽；
- 内存系统的访问延迟；

虽然无阻塞 cache 能够提高性能，但是它们实现起来比较困难。有两个点需要注意：

- 解决 hit 和 miss 之间的竞争（？）。hit 可能和发生 miss 后从下一级 cache 返回的内容冲突（要先满足哪一个么）；如果处理器支持多次 miss，那么 miss 之间也可能冲突。这些冲突可以使用两种方法来解决：
  - hit 的优先级高于 miss；
  - 对所有的 miss 排序；
- 跟踪所有未完成的 miss 便于将来处理。在阻塞 cache 中每次返回值我们都知道对应哪个 miss，但是在无阻塞 cache 中，由于各个 miss 的处理时间不一样，处理之后的返回值不是顺序的。所以处理器需要知道返回值对应的哪个 miss，才不会发生数据访问错误。目前的处理器大多将这些信息保存在一组寄存器中，通常将其称为 Miss Status Handling Registers(MSHRs)。有 n 组未完成的 miss 就有 n 组 MSHR 寄存器，每组寄存器中存放 miss 信息要存在在哪个 cache line，对应的 tag 信息，以及是 load 还是 store 导致的这次 miss。这样在 miss 信息返回后就知道将其放置在哪个 cache line 并判断其是否正确，之后通知 load/store 单元”需要的数据准备好了，你可以进行进一步的处理了“。

所有无阻塞 cache 能够提高带宽，减小 miss 后的阻塞时间，因此能够降低执行时间和能耗，但是需要额外的逻辑单元，增加了硬件的复杂性。

##### 5. 关键字优先和提前重启降低缺失代价

这一优化基于一个现象：处理器每次通常只需要一个 block(cache line?) 中的一个 word(16/32 bits)。因此这一优化做的是不等块中所有的信息都 load 到 cache 中就将请求的 word 发送给处理器并让其恢复工作。这里有两种具体的做法：

- 关键字优先：先 load 处理器请求的 word 并让其尽快到达 cache，在剩余的 words 到达 cache 的过程中就让处理器继续执行；
- 提前重启：还是按照正常的顺序 load，但是一旦处理器请求的 word 到达 cache 就通知处理器让其继续执行。

这种优化方式通常只在 large cache 设计中有效。

##### 6. 合并缓冲区以降低缺失代价

无论采用写直法还是写通法，都需要用到 write buffer，在这两种方法中，数据都是先写入到 write buffer 中，然后由 write buffer 写入到内存或下一级 cache 中。

合并缓冲区的设计思想如下图所示：

![quantitative-3.jpg](https://github.com/UtopianFuture/UtopianFuture.github.io/blob/master/image/quantitative-3.jpg?raw=true)

当 write buffer 中包含 modified block 时，在写入 buffer 前就可以检查新的数据地址是否符合原有的有效的 buffer entry。如果符合的话，将新的 block 与该 entry 合并。

这一优化技术能够提高内存利用率，因为 multiword writes 比 one word at a time 要快，而且这样 write buffer 能够存储更多的数据，降低延迟。

##### 7. 编译优化降低缺失率

这是一种软件优化方法，硬件设计人员的最爱！目前有如下优化方式在编译器中应用广泛。

- Loop Interchange：这种方法是依据程序的空间局部性原则来优化的。程序中有很多嵌套的循环，它们在访问内存的时候并不是按照数据排布的顺序访问，简单的调整循环嵌套的顺序能够是的其按照数据存储的顺序访问数据。

  ```c
  /* Before */
  for (j = 0; j < 100; j = j + 1)
  	for (i = 0; i < 5000; i = i + 1)
  		x[i][j] = 2 * x[i][j];

  /* After */
  for (i = 0; i < 5000; i = i + 1)
  	for (j = 0; j < 100; j = j + 1)
  		x[i][j] = 2 * x[i][j];
  ```

- Blocking：这一方式是提高程序的时间局部性来降低缺失率的。在访问多维数组（或同时访问多个数组）时，存在行、列交叉访问的情况，将数据按行或列排布也无法提高性能，因为每行、列在每次迭代中都会被访问到，可以将其理解为按块访问。

  ```c
  /* Before */
  for (i = 0; i < N; i = i + 1){
  	for (j = 0; j < N; j = j + 1){
          r = 0;
  		for (k = 0; k < N; k = k + 1){
  			r = r + y[i][k]*z[k][j];
  			x[i][j] = r;
          }
  	}
  }

  /* After */
  for (jj = 0; jj < N; jj + jj + B){
  	for (kk = 0; kk < N; kk = kk + B){
  		for (i = 0; i < N; i = i + 1){
  			for (j = jj; j < min(jj + B,N); j = j + 1){
                  r = 0;
  				for (k = kk; k < min(kk + B,N); k = k + 1){
  					r = r + y[i][k]*z[k][j];
  					x[i][j] = x[i][j] + r;
                  }
              }
          }
      }
  }
  ```

  从图中可以看出，

![quantitative-4.jpg](https://github.com/UtopianFuture/UtopianFuture.github.io/blob/master/image/quantitative-4.jpg?raw=true)

![quantitative-5.jpg](https://github.com/UtopianFuture/UtopianFuture.github.io/blob/master/image/quantitative-5.jpg?raw=true)

##### 8. 硬件预取指令和数据降低 miss penalty 和缺失率

指令预取是由 cache 之外的硬件完成的。通常处理器在发生 cache miss 后会取两个 block（这里的 block 应该是指 cache line），一个是请求的 block，放置在 I-cache 中；一个是顺序的下一个 block，放置在 instruction stream buffer 中。当然，如果预取成功率低，那么会对性能造成消极的影响。

##### 9. 编译器控制预取降低 miss penalty 和缺失率

和硬件预取类似，这个编译器完成的。编译器插入预取指令，其可分为两种方式：

- 寄存器预取：将数据 load 到寄存器中；
- cache 预取：只将数据 load 到 cache 而不是寄存器中；

这两种预取方式都可以分为 "faulting" 和 "nonfaulting"，也就是在预取时会不会发生虚拟地址异常或触发保护错。"nonfaulting" 预取就是到预取错误时不走异常流程，而是不进行任何操作（也就是说这条 load 指令变成了一条 no-ops 指令）。

最高效的预取是对于程序来说，预取操作是透明的，其不会改变寄存器和内存的内容，也不会导致异常。

控制预取能够生效的条件是 cache 在进行预取操作时能够继续向处理器发送指令和数据。我们来看一个简单的编译器插入预取指令的例子：

首先我们假设我们有一个 8KB 大小的直接映射的 cache，cache line 是 16 bytes（共有 512 个 cache line），采用写回法。下面的变量 a, b 都是 8 bytes 的双精度浮点类型。我们来计算一下总共会发生多少次 miss。

```c
for (i = 0; i < 3; i = i + 1)
	for (j = 0; j < 100; j = j + 1)
		a[i][j] = b[j][0] * b[j + 1][0];
```

首先对数组 a  的访问会导致 miss。对 a 总共读取 3*100 次，因为每个 cache line 能够存放两个元素，且对 a  的访问是行相关的，那么会发生 3 * (100 / 2) = 150 次。对数组 b 的访问是列相关的，且每次访问 j, j+1 两个元素，所以会发生 100 + 1 = 101 次访问（另外 + 1 是因为第一次访问 `b[0][0]` 和 `b[0][1]` 都发生了 miss）（画图解释更清晰）。总共发生了 251 次 miss。

再来通过编译器预取优化一下：

```c
for (j = 0; j < 100; j = j + 1) {
	prefetch(b[j + 7][0]);
	/* b(j,0) for 7 iterations later */
	prefetch(a[0][j + 7]);
	/* a(0,j) for 7 iterations later */
	a[0][j] = b[j][0] * b[j + 1][0];
}

for (i = 1; i < 3; i = i + 1){
	for (j = 0; j < 100; j = j + 1) {
		prefetch(a[i][j + 7]);
		/* a(i,j) for + 7 iterations */
		a[i][j] = b[j][0] * b[j + 1][0];
    }
}
```

这样的话，我们预取了从 `a[0][7]` 到 `a[2][99]` 和 `b[7][0]` 到 `b[99][0]` 的数据到 cache 中。剩下的元素：

- `b[0][0]` 到 `b[6][0]` 7 次访问会导致 miss；
- `a[0][0]` 到 `a[0][6]` 会导致 4 次 miss；
- `a[1][0]` 到 `a[1][6]` 会导致 4 次 miss；
- `a[2][0]` 到 `a[2][6]` 会导致 4 次 miss；

所以最终的结果是用额外的 400 条预取指令减少了 232 次 cache miss。

然后我们来考虑一下每种指令的执行时间。

首先假设预取指令和处理器执行是重合的，不额外消耗时间。

优化前的循环每次循环为 7 个时钟周期，需要 3 * 100 * 7 = 2100 个时钟周期。每次 cache miss 耗时 100 个时钟周期，需要 251 * 100 = 25100 个时钟周期，共需要 27200 个时钟周期。

优化后的循环，第一个循环每次循环为 9 个时钟周期，需要 9 * 100 = 900 个时钟周期。产生了 7 + 4 = 11 次 cache miss，需要 11 * 100 = 1100 个时钟周期。第二个循环每次循环为 8 个时钟周期，需要 8 * 200 = 1600 个时钟周期，产生了 4 + 4 = 8 次 cache miss，需要 8 * 100 = 800 个始终周期。共需要 900 + 1100 + 1600 + 800 = 4400 个时钟周期，相比于优化前快了 27200 / 4400 = 6.2 倍。

这只是一个简单的用来理解编译器优化的例子，实际情况比这复杂。

##### 10. 使用 HBM 扩展存储层次

目前大部分服务器采用 HBM packaging 的方式来使用大容量的 L4 cache。L4 cache 的介质为 DRAM，容量在 128 MB 到 1 GB 之间。但是使用如此大容量的 DRAM 来作为 L4 cache 存在一个问题，如果 block（这个 block 可以理解为最小存取单元） 太小，tag 占用的空间太大；block 太大，空间利用率低，cache miss 高。这就引入了 [HBM(High Bandwidth Memory)](https://en.wikipedia.org/wiki/High_Bandwidth_Memory)。

**HBM（High Bandwidth Memory）是一种高带宽内存技术**，由英特尔和 SK 海力士等公司联合开发，旨在解决现代计算机系统中的内存瓶颈问题。

HBM 内存使用 3D 堆叠技术，将多个内存芯片垂直堆叠在一起，通过微细间隔的层层排列，实现了高度集成和高带宽的数据传输。同时，HBM 内存还采用了异步时序、多通道和错误修复等技术，提高了内存的性能和可靠性。

与传统的 DDR SDRAM 相比，HBM 内存具有以下优点：

1. 更高的带宽：HBM 内存可以提供更高的带宽，以满足 GPU、FPGA 等高性能计算设备的需求。
2. 更小的封装尺寸：HBM 内存采用 3D 堆叠技术，可以将多个内存芯片集成到较小的封装中，提高系统的集成度和可扩展性。
3. 更低的能耗：HBM 内存使用异步时序技术，可以根据实际数据传输需求动态调整电压和频率，以节约能源。
4. 更低的延迟：HBM 内存采用多通道设计和高速串行连接技术，可以实现更低的延迟和更高的吞吐量。

HBM 内存主要用于高性能计算、人工智能、数据中心等领域，为计算机系统提供更高效、更可靠、更节能的内存解决方案。

它通常用来连接高性能图形加速器，网络设备，在高性能数据中心连接各种专用加速器或者在超级计算机中连接各种 FPGA。所以 HBM 只是用来扩展存储层次的手段。

为了解决上述 block 大小的问题，现在有如下几种方式：

- LH-cache：将 tag 和 data 存储在 HBM SDRAM 的同一列，同一列不同行的访问时间只有不同列的 1/3；
- SRAM-Tags：调整 L4 HBM cache 的组织方式，使得每个 SDRAM 列包好一组 tag 和 29 个数据段，也就是说构造一个 29 路组相联的 cache；
- Alloy cache：将 tag 和 data 通过直接映射的方式组合在一起，这样能降低访问时间；

- summary

  这里总结一下每种优化方法的优化点、带来的硬件复杂度以及目前的使用情况。

  | Technique                                     | Hit time | Band-width | Miss penalty | Miss rate | Power consumption | Hardware cost/ complexity | comment                                                      |
  | --------------------------------------------- | -------- | ---------- | ------------ | --------- | ----------------- | ------------------------- | ------------------------------------------------------------ |
  | Small and simple caches                       | +        |            |              | -         | +                 | 0                         | Trivial; widely used                                         |
  | Way-predicting caches                         | +        |            |              |           | +                 | 1                         | Used in Pentium 4                                            |
  | Pipelined & banked caches                     | -        | +          |              |           |                   | 1                         | Widely used                                                  |
  | Nonblocking caches                            |          | +          | +            |           |                   | 3                         | Widely used                                                  |
  | Critical word first and early restart         |          |            | +            |           |                   | 2                         | Widely used                                                  |
  | Merging write buffer                          |          |            | +            |           |                   | 1                         | Widely used with write through                               |
  | Compiler techniques to reduce cache misses    |          |            |              | +         |                   | 0                         | Software is a challenge, but many compilers handle common linear algebra calculations |
  | Hardware prefetching of instructions and data |          |            | +            | +         | -                 | 2 instr, 3 data           | Most provide prefetch instructions; modern highend processors also automatically prefetch in hardware |
  | Compiler-controlled prefetching               |          |            | +            | +         |                   | 3                         | Needs nonblocking cache; possible instruction overhead; in many CPUs |
  | HBM as additional level of cache              |          | +/-        | -            | +         | +                 | 3                         | Depends on new packaging technology. Effects depend heavily on hit rate improvements |

### ARM cache

下面我们来实际看看 ARM 结构的 cache 操作是怎样的。

在 ARM 架构中，normal memory 是带 cache 的，通常我们使用 clean 操作来刷缓存。但是**刷缓存本身是个模糊的概念**，缓存存在多级，有些在处理器内，有些在总线之后，到底刷到哪里算是终结呢？还有，为了保证一致性，刷的时候是不是需要通知别的处理器和缓存？为了把这些问题规范化，ARM 引入了 Point of Unification/Coherency，Inner/Outer Cacheable 和 System/Inner/Outer/Non Shareable 的概念。

![img](https://raw.githubusercontent.com/UtopianFuture/UtopianFuture.github.io/81305767754ca8df95d18da10465dd730bc093be/image/pou-poc.svg)

- PoU，**对于某一个核 Master**，附属于它的 ICACHE, DCACHE和 TLB，如果在某一点上，它们**能看到一致的内容**，那么这个点就是 PoU。如上图右侧，MasterB 包含了指令，数据缓存和 TLB，还有二级缓存。指令，数据缓存和 TLB 的数据交换都建立在二级缓存，此时二级缓存就成了 PoU。而对于上图左侧的 MasterA，由于没有二级缓存，指令，数据缓存和 TLB 的数据交换都建立在内存上，所以内存成了 PoU。还有一种情况，就是指令缓存可以去监听数据缓存，此时，不需要二级缓存也能保持数据一致，那一级数据缓存就变成了 PoU；

- PoC，**对于系统中所有能够访存的 Master**（注意是所有的，而不是某个核），如果存在某个点，它们的 ICACHE, DCACHE 和 TLB 能看到同一个源，那么这个点就是 PoC。如上图右侧，二级缓存此时不能作为 PoC，因为 MasterB 在它的范围之外，直接访问内存。所以此时内存是 PoC。在左图，由于只有一个 Master，所以内存是 PoC。

  再进一步，如果我们把右图的内存换成三级缓存，把内存接在三级缓存后面，那 PoC 就变成了三级缓存。

  有了这两个定义，我们就**可以指定 TLB 和缓存操作指令到底发到哪个范围**。比如在下图的系统上，有两组 A15，每组四个核，组内含二级缓存。系统的 PoC 在内存，而 A15 的 PoU 分别在它们自己组内的二级缓存上。在某个 A15 上执行 Clean 清指令缓存，范围指定 PoU。显然，所有四个 A15 的一级指令缓存都会被清掉。那么其他的各个 Master 是不是受影响？那就要用到 Inner/Outer/Non Shareable。

  ![img](https://picx.zhimg.com/80/v2-bc56bfd07974e7252a3c8f8fa705b292_1440w.webp?source=d16d100b)

- Shareable 的很容易理解，就是某个地址的可能被别人使用。我们在定义某个页属性的时候会给出。Non-Shareable 就是只有自己使用。当然，定义成 Non-Shareable 不表示别人不可以用，也就是 non-cacheable；

- 对于 Inner 和 Outer Shareable，有个简单的的理解，就是认为他们都是一个东西；

说了这么多概念，你可能会想这有什么用处？回到上文的 Clean 指令，PoU 使得四个 A15 的指令缓存中对应的行都被清掉。由于是指令缓存操作，Inner Shareable 属性使得这个操作被扩散到总线；

要这么复杂的定义有什么用？用处是，**精确定义 TLB/缓存维护和读写指令的范围**。如果我们改变一下，总线不支持 Inner/Outer Shareable 的广播，那么就只有 A7 处理器组会清缓存行。显然这么做在逻辑上不对，因为 A7/A15 可能运行同一行代码。并且，我们之前提到过，如果把读写属性设成 Non-Shareable，那么总线就不会去监听其他 master，减少访问延迟，这样可以非常灵活的提高性能。它们的关系是这样的，

![img](https://raw.githubusercontent.com/UtopianFuture/UtopianFuture.github.io/81305767754ca8df95d18da10465dd730bc093be/image/inner-outer-shareable.svg)

再回到前面的问题，刷某行缓存的时候，怎么知道数据是否最终写到了内存中？对不起，非常抱歉，还是没法知道。你只能做到把范围设成 PoC。如果 PoC 是三级缓存，那么最终刷到三级缓存，如果是内存，那就刷到内存。不过这在逻辑上没有错，按照定义，所有 Master 如果都在三级缓存统一数据的话，那就不必刷到内存了。

简而言之，**PoU/PoC 定义了指令和命令的所能抵达的缓存或内存，在到达了指定地点后，Inner/Outer Shareable 定义了它们被广播的范围**。

再来看看 Inner/Outer Cacheable，这个就简单了，仅仅是一个缓存的前后界定。一级缓存一定是 Inner Cacheable 的，而最外层的缓存，比如三级，可能是 Outer Cacheable，也可能是 Inner Cacheable。他们的用处在于，在定义内存页属性的时候，可以在不同层的缓存上有不同的处理策略。

在 ARM 的处理器和总线手册中，还会出现几个 PoS(Point of Serialization)。它的意思是，在总线中，所有主设备来的各类请求，都必须由控制器检查地址和类型，如果存在竞争，那就会进行串行化。这个概念和其他几个没什么关系。

ARM 的 cache 操作都放在 cache.S 中，我们逐个函数来分析，

```c
/*
 *	MM Cache Management
 *	===================
 *
 *	The arch/arm64/mm/cache.S implements these methods.
 *
 *	Start addresses are inclusive and end addresses are exclusive; start
 *	addresses should be rounded down, end addresses up.
 *
 *	See Documentation/core-api/cachetlb.rst for more information. Please note that
 *	the implementation assumes non-aliasing VIPT D-cache and (aliasing)
 *	VIPT I-cache.
 *
 *	All functions below apply to the interval [start, end)
 *		- start - virtual start address (inclusive)
 *		- end - virtual end address (exclusive)
 *
 *	caches_clean_inval_pou(start, end)
 *
 *		Ensure coherency between the I-cache and the D-cache region to
 *		the Point of Unification.
 *
 *	caches_clean_inval_user_pou(start, end)
 *
 *		Ensure coherency between the I-cache and the D-cache region to
 *		the Point of Unification.
 *		Use only if the region might access user memory.
 *
 *	icache_inval_pou(start, end)
 *
 *		Invalidate I-cache region to the Point of Unification.
 *
 *	dcache_clean_inval_poc(start, end)
 *
 *		Clean and invalidate D-cache region to the Point of Coherency.
 *
 *	dcache_inval_poc(start, end)
 *
 *		Invalidate D-cache region to the Point of Coherency.
 *
 *	dcache_clean_poc(start, end)
 *
 *		Clean D-cache region to the Point of Coherency.
 *
 *	dcache_clean_pop(start, end)
 *
 *		Clean D-cache region to the Point of Persistence.
 *
 *	dcache_clean_pou(start, end)
 *
 *		Clean D-cache region to the Point of Unification.
 */
extern void caches_clean_inval_pou(unsigned long start, unsigned long end);
extern void icache_inval_pou(unsigned long start, unsigned long end);
extern void dcache_clean_inval_poc(unsigned long start, unsigned long end);
extern void dcache_inval_poc(unsigned long start, unsigned long end);
extern void dcache_clean_poc(unsigned long start, unsigned long end);
extern void dcache_clean_pop(unsigned long start, unsigned long end);
extern void dcache_clean_pou(unsigned long start, unsigned long end);
extern long caches_clean_inval_user_pou(unsigned long start, unsigned long end);
extern void sync_icache_aliases(unsigned long start, unsigned long end);
```
借此机会分析一下 ARM 架构的 cache 指令，
```c
/*
 *	caches_clean_inval_pou_macro(start,end) [fixup]
 *
 *	Ensure that the I and D caches are coherent within specified region.
 *	This is typically used when code has been written to a memory region,
 *	and will be executed.
 *
 *	- start - virtual start address of region
 *	- end - virtual end address of region
 *	- fixup - optional label to branch to on user fault
 */
.macro	caches_clean_inval_pou_macro, fixup
alternative_if ARM64_HAS_CACHE_IDC // 该宏没开
	// dsb 为数据同步屏障指令，确保在下一个指令执行前所有存储器访问都已经完成；
	// ishst: Inner Shareable is the required shareability domain, writes are the required access type
	dsb ishst
	b .Ldc_skip_\@
alternative_else_nop_endif
	mov x2, x0
	mov x3, x1
	// dcache_by_line_op 是微指令，定义在 <arm64/include/asm/assembler.h> 中
	// 执行 dc cvau, dsb
	// Clean data cache by address to Point of Unification
	// Inner Shareable is the required shareability domain, reads and writes are the required access types
	dcache_by_line_op cvau, ish, x2, x3, x4, x5, \fixup
.Ldc_skip_\@:
alternative_if ARM64_HAS_CACHE_DIC
	isb // 指令同步屏障指令，清空流水线，确保在执行新的指令前，之前所有的指令都已完成；
	b	.Lic_skip_\@
alternative_else_nop_endif
	// 微指令，执行 ic ivau, dsb ish, isb
	// Invalidate instruction cache by address to Point of Unification
	invalidate_icache_by_line x0, x1, x2, x3, \fixup
.Lic_skip_\@:
.endm

/*
 *	caches_clean_inval_pou(start,end)
 *
 *	Ensure that the I and D caches are coherent within specified region.
 *	This is typically used when code has been written to a memory region,
 *	and will be executed.
 *
 *	- start - virtual start address of region
 *	- end - virtual end address of region
 */
SYM_FUNC_START(caches_clean_inval_pou)
	caches_clean_inval_pou_macro
	ret
SYM_FUNC_END(caches_clean_inval_pou)
SYM_FUNC_ALIAS(__pi_caches_clean_inval_pou, caches_clean_inval_pou)

/*
 *	caches_clean_inval_user_pou(start,end)
 *
 *	Ensure that the I and D caches are coherent within specified region.
 *	This is typically used when code has been written to a memory region,
 *	and will be executed.
 *
 *	- start - virtual start address of region
 *	- end - virtual end address of region
 */
SYM_FUNC_START(caches_clean_inval_user_pou) // 没遇到过，暂时不分析
	uaccess_ttbr0_enable x2, x3, x4
	caches_clean_inval_pou_macro 2f
	mov	x0, xzr
1:
	uaccess_ttbr0_disable x1, x2
	ret
2:
	mov	x0, #-EFAULT
	b	1b
SYM_FUNC_END(caches_clean_inval_user_pou)

/*
 *	icache_inval_pou(start,end)
 *
 *	Ensure that the I cache is invalid within specified region.
 *
 *	- start - virtual start address of region
 *	- end - virtual end address of region
 */
SYM_FUNC_START(icache_inval_pou)
alternative_if ARM64_HAS_CACHE_DIC
	isb
	ret
alternative_else_nop_endif
	// 微指令，执行 ic ivau, dsb ish, isb
	// Invalidate instruction cache by virtual address to Point of Unification
	invalidate_icache_by_line x0, x1, x2, x3
	ret
SYM_FUNC_END(icache_inval_pou)

/*
 *	dcache_clean_inval_poc(start, end)
 *
 *	Ensure that any D-cache lines for the interval [start, end)
 *	are cleaned and invalidated to the PoC.
 *
 *	- start - virtual start address of region
 *	- end - virtual end address of region
 */
SYM_FUNC_START(__pi_dcache_clean_inval_poc)
	// 微指令，执行 dc civau, dsb
	// Clean and invalidate by virtual address to Point of Coherency
	dcache_by_line_op civac, sy, x0, x1, x2, x3
	ret
SYM_FUNC_END(__pi_dcache_clean_inval_poc)
SYM_FUNC_ALIAS(dcache_clean_inval_poc, __pi_dcache_clean_inval_poc)

/*
 *	dcache_clean_pou(start, end)
 *
 * 	Ensure that any D-cache lines for the interval [start, end)
 * 	are cleaned to the PoU.
 *
 *	- start - virtual start address of region
 *	- end - virtual end address of region
 */
SYM_FUNC_START(dcache_clean_pou)
alternative_if ARM64_HAS_CACHE_IDC
	dsb	ishst
	ret
alternative_else_nop_endif
	// 微指令，执行 dc cvau, dsb
	// Clean data cache by address to Point of Unification.
	dcache_by_line_op cvau, ish, x0, x1, x2, x3
	ret
SYM_FUNC_END(dcache_clean_pou)

/*
 *	dcache_inval_poc(start, end)
 *
 * 	Ensure that any D-cache lines for the interval [start, end)
 * 	are invalidated. Any partial lines at the ends of the interval are
 *	also cleaned to PoC to prevent data loss.
 *
 *	- start - kernel start address of region
 *	- end - kernel end address of region
 */
SYM_FUNC_START(__pi_dcache_inval_poc) // 注意，下面两个操作是将数据同步到 PoU
	dcache_line_size x2, x3
	sub	x3, x2, #1
	tst	x1, x3				// end cache line aligned?
	bic	x1, x1, x3
	b.eq	1f
	dc	civac, x1			// clean & invalidate D / U line
1:	tst	x0, x3				// start cache line aligned?
	bic	x0, x0, x3
	b.eq	2f
	dc	civac, x0			// clean & invalidate D / U line
	b	3f
2:	dc	ivac, x0			// invalidate D / U line
3:	add	x0, x0, x2
	cmp	x0, x1
	b.lo	2b
	dsb	sy
	ret
SYM_FUNC_END(__pi_dcache_inval_poc)
SYM_FUNC_ALIAS(dcache_inval_poc, __pi_dcache_inval_poc)

/*
 *	dcache_clean_poc(start, end)
 *
 * 	Ensure that any D-cache lines for the interval [start, end)
 * 	are cleaned to the PoC.
 *
 *	- start - virtual start address of region
 *	- end - virtual end address of region
 */
SYM_FUNC_START(__pi_dcache_clean_poc)
	dcache_by_line_op cvac, sy, x0, x1, x2, x3
	ret
SYM_FUNC_END(__pi_dcache_clean_poc)
SYM_FUNC_ALIAS(dcache_clean_poc, __pi_dcache_clean_poc)

/*
 *	dcache_clean_pop(start, end)
 *
 * 	Ensure that any D-cache lines for the interval [start, end)
 * 	are cleaned to the PoP.
 *
 *	- start - virtual start address of region
 *	- end - virtual end address of region
 */
SYM_FUNC_START(__pi_dcache_clean_pop)
	alternative_if_not ARM64_HAS_DCPOP
	b	dcache_clean_poc
	alternative_else_nop_endif
	dcache_by_line_op cvap, sy, x0, x1, x2, x3
	ret
SYM_FUNC_END(__pi_dcache_clean_pop)
SYM_FUNC_ALIAS(dcache_clean_pop, __pi_dcache_clean_pop)
```

在 6.6.30 版本的内核上，只有 DMA 同步接口 `arch_sync_dma_for_device/arch_sync_dma_for_cpu` 使用了 `dcache_clean_poc/dcache_inval_poc` 函数，既将数据同步到 PoC。而 set_pte 使用的是 `caches_clean_inval_pou` 函数，同步到 PoU。

因此，就存在这种可能。当前内核在 mmap 之后，会执行 dc cvau 命令（在哪里执行的？），这时 L3 中的数据相比于 DDR 还是 dirty 的。在 mmap 之后，DMA 向这块内存中写了数据，写到 DDR 中。之后由于 L3 cache 发生了 cache evict，将这块内存的 dirty cache line 数据写入到 DDR 中，覆盖了 DMA 写入的数据。所以如果有 DMA 和 ACPU 共享内存，DMA 访问之前一定要 flush cache 到 POC。

#### flush cache all

上面的操作比较简单，再来看看有难度的，下面是 ARM 提供的 flush cache all 的实现。

flush cache all 设计的出发点是系统中的 cache 容量是有限的，当需要刷 cache 的大小超过了 cache 容量，那么理论上来说直接 flush cache all 会更加高效。

```c
		MRS		X0, CLIDR_EL1
		AND 	W3, W0, #0x07000000		// Get 2 x Level of Coherence
		LSR 	W3, W3, #23
		CBZ 	W3, Finished			// if loc is 0, then no need to clean
		MOV 	W10, #0					// W10 = 2 x cache level, start clean at cache level 0
		MOV 	W8, #1					// W8 = constant 0b1
Loop1:	ADD		W2, W10, W10, LSR #1	// Calculate 3 x cache level
		LSR 	W1, W0, W2				// extract 3-bit cache type for this level
		AND 	W1, W1, #0x7
		CMP 	W1, #2
		B.LT	Skip					// No data or unified cache at this level
		MSR		CSSELR_EL1, X10			// Select this cache level
		ISB								// Synchronize change of CSSELR
		MRS		X1, CCSIDR_EL1			// Read CCSIDR
		AND 	W2, W1, #7				// W2 = log2(linelen)-4
		ADD 	W2, W2, #4 				// W2 = log2(linelen)
		UBFX	W4, W1, #3, #10			// W4 = max way number, right aligned
		CLZ		W5, W4					// W5 = 32-log2(ways), bit position of way in DC operand
		LSL 	W9, W4, W5				// W9 = max way number, aligned to position in DC operand
		LSL 	W16, W8, W5				// W16 = amount to decrement way number per iteration
Loop2:	UBFX	W7, W1, #13, #15		// W7 = max set number, right aligned
		LSL		W7, W7, W2				// W7 = max set number, aligned to position in DC operand
		LSL 	W17, W8, W2				// W17 = amount to decrement set number per iteration
Loop3:	ORR 	W11, W10, W9			// W11 = combine way number and cache number ...
		ORR		W11, W11, W7			// ... and set number for DC operand
		DC		CSW, X11				// Do data cache clean by set and way
		SUBS	W7, W7, W17				// Decrement set number
		B.GE 	Loop3
		SUBS 	X9, X9, X16				// Decrement way number
		B.GE 	Loop2
Skip:	ADD		W10, W10, #2			// Increment 2 x cache level
		CMP 	W3, W10
		DSB								// Ensure completion of previous cache maintenance instruction
		B.GT	Loop1
Finished:
```

- MRS: Move System Register **allows the PE to read** an AArch64 System register into a general-purpose register;
- MSR: Move general-purpose register to System Register **allows the PE to write** an AArch64 System register from a general-purpose register;
- CLIDR_EL1(Cache Level ID Register): **Identifies the type of cache, or caches**, that are implemented at each level and can be managed using the architected cache maintenance instructions that operate by set/way, up to a maximum of seven levels. Also **identifies the Level of Coherence (LoC) and Level of Unification (LoU)** for the cache hierarchy;
- CCSIDR_EL1(Current Cache Size ID Register): Provides information about the architecture of the currently selected cache;
- CSSELR_EL1(Cache Size Selection Register): Selects the current Cache Size ID Register, CCSIDR_EL1, by specifying the required cache level and the cache type (either instruction or data cache);
- CBNZ/CBZ: Compare and Branch on Nonzero and Compare and Branch on Zero compare the value in a register with zero, and conditionally branch forward a constant value;
- UBFX: 从寄存器中任意位置提取任意数量的相邻位，将其零扩展为 32 位，并将结果写入目标寄存器;

 ```
 UBFX Wd, Wn, #lsb, #width ; 32-bit
 UBFX Xd, Xn, #lsb, #width ; 64-bit
 ```

 从 Wn 寄存器的第 lsb 位开始，提取 width 位到 Wd 寄存器，剩余高位用 0 填充；

- CLZ: 对源寄存器值中第一个二进制 1 位之前的二进制零位进行计数，并将结果写入目标寄存器
- ORR: 用于在两个操作数上进行逻辑或运算，并把结果放置到目的寄存器中；
- DC CSW: Clean data cache by set/way;

#### [L1 / L2 cache 替换策略](https://www.eet-china.com/mp/a166558.html)

我们先看一下 DynamIQ 架构中的cache中新增的几个概念：

- **strictly inclusive**: 所有存在 L1 cache 中的数据，必然也存在 L2 cache 中，L2 被替换出去了，L1 中的数据也会被 invalidate；
- **weakly inclusive**: 当 miss 的时候，数据会被同时缓存到 L1 和 L2，但在之后，L2 中的数据可能会被 invalidate，不过这不影响 L1；
- **stcritly exclusive**: 当 miss 的时候，数据只会缓存到 L1，换句话说，在 L1 中的数据肯定不会在 L2 中；

综上总结：inclusive/exclusive 描述的仅仅是 L1和 L2 之间的替换策略。

#### core cache / DSU cache / memory 替换策略

我们知道 MMU 的页表中的表项中，管理者每一块内存的属性，其实就是 cache 属性，也就是缓存策略。其中就有 cacheable 和 shareable、Inner 和 Outer 的概念。如下是针对 DynamIQ 架构做出的总结，

- **如果将 block 的内存属性配置成 Non-cacheable**，那么数据就不会被缓存到 cache，那么所有 observer 看到的内存是一致的，也就说此时也相当于 Outer Shareable；
- **如果将 block 的内存属性配置成 write-through cacheable 或 write-back cacheable**，那么数据会被缓存 cache 中。write-through 和 write-back 是缓存策略；
- **如果将 block 的内存属性配置成 non-shareable**, 那么 core0 访问该内存时，数据缓存的到 Core0 的 L1 D-cache / L2 cache （将 L1/L2 看做一个整体，直接说数据会缓存到 core0 的 private cache 更好），不会缓存到其它 cache 中；
- **如果将 block 的内存属性配置成 inner-shareable**, 那么 core0 访问该内存时，数据只会缓存到 core 0 的 L1 D-cache / L2 cache 和 DSU L3 cache，不会缓存到 System Level Cache 中(当然如果有 system cache 的话 ) 。此时 core0 的 cache TAG 中的 MESI 状态是 E， 接着如果这个时候 core1 也去读该数据，那么数据也会被缓存 core1 的 L1 D-cache / L2 cache， 此时 core0 和 core1 的 MESI 状态都是 S；
- **如果将 block 的内存属性配置成 outer-shareable**, 那么 core0 访问该内存时，数据会缓存到 core 0 的 L1 D-cache / L2 cache 、cluster0 的 DSU L3 cache 、 System Level Cache 中，core0 的 MESI 状态为 E。如果 core1 再去读的话，则也会缓存到 core1的 L1 D-cache / L2 cache，此时 core0 和 core1 的 MESI 都是 S。这个时候，如果 core7 也去读的话，数据还会被缓存到 cluster1 的 DSU L3 cache。至于 DSU0 和 DSU1 之间的一致性，非 MESI 维护，具体怎么维护的请看 DSU 手册，本文不展开讨论。

### DMA 与 cache 一致性

**DMA 不能访问 cache，那么 DMA 在内存和外设之间搬数据的时候会出现内存与 cache 中的数据不一致的问题（如果都是以 CPU 的视角或者 DMA 设备的视角来看，就不存在这一问题）**。解决这一问题有三种方法：

#### Coherent DMA buffers 一致性

DMA 需要的内存由内核去申请，内核可能需要对这段内存重新做一遍映射，**特点是映射的时候标记这些页是 uncacheable**，这个特性存放在页表里面。

上面说“可能”需要重新做映射，如果内核在 highmem 映射区申请内存并将这个地址通过 vmap 映射到 vmalloc 区域，则需要修改相应页表项并将页面设置为 uncacheable，而如果内核从 lowmem 申请内存，我们知道这部分是**已经线性映射好了**，因此不需要修改页表，只需修改相应页表项为 uncacheable 即可。

相关的接口就是 `dma_alloc_coherent` 和 `dma_free_coherent`。我们来看一下 `dma_alloc_coherent` 是怎样判断内存属性的。

```c
| dma_alloc_coherent
| 	-> dma_alloc_attrs
| 		-> dma_alloc_from_dev_coherent // 设备有自己的 coherent memory pools
| 		-> dma_direct_alloc // 没有 iommu 设备
| 		-> ops->alloc(dma_map_ops->alloc = iommu_dma_alloc) // 该设备有 iommu 设备支持
```

`dma_alloc_coherent` 在面对不同场景有三种选择，下面一一分析，

##### per-device coherent memory

```c
int dma_alloc_from_dev_coherent(struct device *dev, ssize_t size,
		dma_addr_t *dma_handle, void **ret)
{
	struct dma_coherent_mem *mem = dev_get_coherent_memory(dev); // 这里会判断该设备是否有自己的内存
	if (!mem)
		return 0;
	*ret = __dma_alloc_from_coherent(dev, mem, size, dma_handle);
	return 1;
}
```

这个 `dev->dma_mem` 是在 `shared-dma-pool` 类型的预留内存初始化的时候赋值的，

```c
static inline struct dma_coherent_mem *dev_get_coherent_memory(struct device *dev)
{
	if (dev && dev->dma_mem)
		return dev->dma_mem;
	return NULL;
}
```

初始化的路径是这样的，

```c
| RESERVEDMEM_OF_DECLARE(dma, "shared-dma-pool", rmem_dma_setup);
| 	-> rmem->ops = &rmem_dma_ops;
| 		-> rmem_dma_device_init // 在初始化的时候会将这块内存通过 memremap 接口配置成 MEMREMAP_WC 属性，即 uncached
| 			-> dma_assign_coherent_memory
```

而需要调用到 `rmem_dma_setup` 中配置的初始化函数，需要使用 `of_reserved_mem_device_init_by_idx` 接口，

```c
int of_reserved_mem_device_init_by_idx(struct device *dev,
				 struct device_node *np, int idx)
{
	...

	target = of_parse_phandle(np, "memory-region", idx);
	rmem = __find_rmem(target);
	of_node_put(target);
	ret = rmem->ops->device_init(rmem, dev);
	...
	return ret;
}
```

在初始化的时候，会调用 `memremap(MEMREMAP_WC)` 接口将这块预留内存映射成 device memory, uncacheable 属性。

```C
static struct dma_coherent_mem *dma_init_coherent_memory(phys_addr_t phys_addr,
 dma_addr_t device_addr, size_t size, bool use_dma_pfn_offset)
{
    ...

    mem_base = memremap(phys_addr, size, MEMREMAP_WC); // 映射为 writecombine, uncacheable
    dma_mem = kzalloc(sizeof(struct dma_coherent_mem), GFP_KERNEL);
    dma_mem->bitmap = kzalloc(bitmap_size, GFP_KERNEL);
    dma_mem->virt_base = mem_base;
    dma_mem->device_base = device_addr;
    dma_mem->pfn_base = PFN_DOWN(phys_addr);
    dma_mem->size = pages;
    dma_mem->use_dev_dma_pfn_offset = use_dma_pfn_offset;
    return dma_mem;
}
```

最后配置一下，

```C
static int dma_assign_coherent_memory(struct device *dev,
 struct dma_coherent_mem *mem)
{
 	...

    dev->dma_mem = mem;
    return 0;
}
```

这样就将这块预留内存配置该设备的独有的设备内存。我们再来看一下是怎样分配内存的，

```c
static void *__dma_alloc_from_coherent(struct device *dev,
				 struct dma_coherent_mem *mem,
				 ssize_t size, dma_addr_t *dma_handle)
{
    ...

	pageno = bitmap_find_free_region(mem->bitmap, mem->size, order); // 很简单，通过 bitmap 来管理

	/*
	 * Memory was found in the coherent area.
	 */
	*dma_handle = dma_get_device_base(dev, mem) +
			((dma_addr_t)pageno << PAGE_SHIFT); // dma_handle 就是物理地址加页偏移
	ret = mem->virt_base + ((dma_addr_t)pageno << PAGE_SHIFT);
	memset(ret, 0, size);
	return ret;
}
```

所以这种分配方式就是通过将内存映射为 `MEMREMAP_WC` 属性(uncached)来保证内存一致性的。

##### direct alloc

```c
void *dma_direct_alloc(struct device *dev, size_t size,
		dma_addr_t *dma_handle, gfp_t gfp, unsigned long attrs)
{
	struct page *page;
	void *ret;
	size = PAGE_ALIGN(size);

	...

	/* we always manually zero the memory once we are done */
    // 这里还是调用 dma_alloc_contiguous 从 default cma 中申请
	page = __dma_direct_alloc_pages(dev, size, gfp & ~__GFP_ZERO, true);
	if ((IS_ENABLED(CONFIG_DMA_DIRECT_REMAP) &&
	 !dev_is_dma_coherent(dev)) || // 这个表示硬件是否支持 cache 一致性，如果支持，那么就不需要设置为 uncacheable
	 (IS_ENABLED(CONFIG_DMA_REMAP) && PageHighMem(page))) {
		/* remove any dirty cache lines on the kernel alias */
		arch_dma_prep_coherent(page, size); // 这里调用 __dma_flush_area 刷 cache

        // 如果 dev 不是 dma coherent 的，那么需要 remap，保证 coherent
        // 这里 dma_pgprot 对于不是 dma coherent 的情况，配置的是 NORMAL_NC 属性
        // remap 会调用 vmap 配置页表项，同时 vmap 的 flag 也是 VM_DMA_COHERENT
        // 这个 flag 应该也是用来保证一致性的
		/* create a coherent mapping */
		ret = dma_common_contiguous_remap(page, size,
				dma_pgprot(dev, PAGE_KERNEL, attrs),
				__builtin_return_address(0));

        memset(ret, 0, size);
		goto done; // 如果从 highmem 映射区申请内存，那么只需要刷 cache，不需要置为 uncached
	}

	...

	ret = page_address(page);
	if (dma_set_decrypted(dev, ret, size))
		goto out_free_pages;

	memset(ret, 0, size);
	if (IS_ENABLED(CONFIG_ARCH_HAS_DMA_SET_UNCACHED) &&
	 !dev_is_dma_coherent(dev)) {
		arch_dma_prep_coherent(page, size);
		ret = arch_dma_set_uncached(ret, size); // 将该虚拟地址设置成 uncached 属性
		if (IS_ERR(ret))
			goto out_encrypt_pages;
	}

done:
	*dma_handle = phys_to_dma_direct(dev, page_to_phys(page));
	return ret;
}

/* kernel/dma/mapping.c */
pgprot_t dma_pgprot(struct device *dev, pgprot_t prot, unsigned long attrs)
{
    if (force_dma_unencrypted(dev))
    prot = pgprot_decrypted(prot);

    if (dev_is_dma_coherent(dev)) // 如果 dev 是 dma coherent 的，那么直接返回 prot，不需要额外的 cache 操作
        return prot;
#ifdef CONFIG_ARCH_HAS_DMA_WRITE_COMBINE
    if (attrs & DMA_ATTR_WRITE_COMBINE)
        return pgprot_writecombine(prot); // 如果架构支持 write combine，那么设置为 writecombine，即uncached
#endif
    return pgprot_dmacoherent(prot);
}

#define pgprot_dmacoherent(prot) \
	__pgprot_modify(prot, PTE_ATTRINDX_MASK, \
			PTE_ATTRINDX(MT_NORMAL_NC) | PTE_PXN | PTE_UXN) // 这里设置了 NC 属性
```

这个函数中的 `dev_is_dma_coherent` 是判断该 DMA 设备是否在硬件上支持 DMA cache 一致性，如果硬件自身就能完成 cache 同步的操作，那么就不用从软件层面去更改页表，刷 cache。如果在 dts 中配置了 `dma-coherent` 属性，就表示该设备硬件支持 cache 一致性。这一系列的配置在 `platform_driver_register` 中完成的。

##### dma_map_ops->alloc

```c
static void *iommu_dma_alloc(struct device *dev, size_t size,
		dma_addr_t *handle, gfp_t gfp, unsigned long attrs)
{
	bool coherent = dev_is_dma_coherent(dev);
	int ioprot = dma_info_to_prot(DMA_BIDIRECTIONAL, coherent, attrs);
	struct page *page = NULL;
	void *cpu_addr;

	gfp |= __GFP_ZERO;
	if (IS_ENABLED(CONFIG_DMA_REMAP) && gfpflags_allow_blocking(gfp) &&
	 !(attrs & DMA_ATTR_FORCE_CONTIGUOUS)) { // 这里分配是走 buddy 系统，不保证连续
		return iommu_dma_alloc_remap(dev, size, handle, gfp,
				dma_pgprot(dev, PAGE_KERNEL, attrs), attrs); // 这里同样是 dma_pgprot，将内存配置为 uncached
	}

	if (IS_ENABLED(CONFIG_DMA_DIRECT_REMAP) &&
	 !gfpflags_allow_blocking(gfp) && !coherent) // 这种情况还没遇到，不知道是干什么的
		page = dma_alloc_from_pool(dev, PAGE_ALIGN(size), &cpu_addr, // 从代码中来看，有三种 pool
					 gfp, NULL);
	else
    // 通过 cma_alloc 分配，并将内存通过 dma_gpport 配置成 uncached
    // 从 default cma 中分配
		cpu_addr = iommu_dma_alloc_pages(dev, size, &page, gfp, attrs);
	if (!cpu_addr)
		return NULL;

    // 分配 iova
	*handle = __iommu_dma_map(dev, page_to_phys(page), size, ioprot,
			dev->coherent_dma_mask);
	if (*handle == DMA_MAPPING_ERROR) {
		__iommu_dma_free(dev, size, cpu_addr);
		return NULL;
	}
	return cpu_addr;
}
```

综上所述， `dma_alloc_coherent` 就是通过将内存配置成 uncached 来保证 DMA cache 一致性。这里有个技巧可以学习，就是内核代码都是在更改页表属性之前去刷 cache。

#### DMA Streaming Mapping 流式 DMA 映射

**一致性缓存的方式是内核专门申请好一块内存给 DMA 用**。而有时驱动并没这样做，而是让 DMA 引擎直接在上层传下来的内存里做事情。

例如从协议栈里发下来的一个包，想通过网卡发送出去。但是协议栈并不知道这个包要往哪里走，因此分配内存的时候并没有特殊对待，这个包所在的内存通常都是 cacheable。

这时，内存在给 DMA 使用之前，就要调用一次 `dma_map_sg` 或 `dma_map_single`，取决于 DMA 引擎是否支持聚集散列（DMA scatter-gather），支持就用 `dma_map_sg`，不支持就用 `dma_map_single`。DMA 用完之后要调用对应的 unmap 接口。

##### 映射接口

DMA 框架提供一系列的映射接口，适用于不同的场景，这些接口在映射完后都会刷 cache，

```c
dma_addr_t dma_map_page_attrs(struct device *dev, struct page *page, // 使用 page 刷
		size_t offset, size_t size, enum dma_data_direction dir,
		unsigned long attrs){} // 调用 arch_sync_dma_for_device 或 dma_map_ops 的 map_page
void dma_unmap_page_attrs(struct device *dev, dma_addr_t addr, size_t size,
		enum dma_data_direction dir, unsigned long attrs) // 调用 dma_map_ops 的 unmap_page
unsigned int dma_map_sg_attrs(struct device *dev, struct scatterlist *sg, // 使用 scatterlist 刷
		 int nents, enum dma_data_direction dir, unsigned long attrs){} // 同上
void dma_unmap_sg_attrs(struct device *dev, struct scatterlist *sg,
				 int nents, enum dma_data_direction dir,
				 unsigned long attrs){} // 调用 dma_map_ops 的 unmap_sg
int dma_map_sgtable(struct device *dev, struct sg_table *sgt, // 使用 sg_table 刷
		enum dma_data_direction dir, unsigned long attrs); // 同上
```

##### 同步接口

另外，还提供了单独的刷 cache 的接口，

```c
void dma_sync_single_for_cpu(struct device *dev, dma_addr_t addr, size_t size, // 使用 dma address 刷
		enum dma_data_direction dir);
void dma_sync_single_for_device(struct device *dev, dma_addr_t addr,
		size_t size, enum dma_data_direction dir);
void dma_sync_sg_for_cpu(struct device *dev, struct scatterlist *sg, // 使用 scatterlist 刷
		 int nelems, enum dma_data_direction dir);
void dma_sync_sg_for_device(struct device *dev, struct scatterlist *sg,
		 int nelems, enum dma_data_direction dir);
// 使用 sg_table 刷，其实还是调用 dma_sync_sg_for_cpu
static inline void dma_sync_sgtable_for_cpu(struct device *dev,
		struct sg_table *sgt, enum dma_data_direction dir);
static inline void dma_sync_sgtable_for_device(struct device *dev,
		struct sg_table *sgt, enum dma_data_direction dir);
```

上面的这些接口在映射/解映射前都会调用 `arch_sync_dma_for_device`/ `arch_sync_dma_for_cpu` 来做 cache 和 DMA 设备之间的同步，这两个函数的实现是 cache.S 中的汇编。

##### arch_sync_dma_for_cpu

在映射前，或 CPU 读之前，invalid 指定范围的 cacheline，

```c
void arch_sync_dma_for_cpu(phys_addr_t paddr, size_t size,
		enum dma_data_direction dir)
{
	__dma_unmap_area(phys_to_virt(paddr), size, dir);
}

/*
 *	__dma_unmap_area(start, size, dir)
 *	- start	- kernel virtual start address
 *	- size	- size of region
 *	- dir	- DMA direction
 */
SYM_FUNC_START_PI(__dma_unmap_area) // for_cpu
	add	x1, x0, x1
	cmp	w2, #DMA_TO_DEVICE // 从这里可以看出来，只有 direction == DMA_TO_CPU 才会 invalid cache
	b.ne	__dma_inv_area
	ret
SYM_FUNC_END_PI(__dma_unmap_area)

 /*
 *	dcache_inval_poc(start, end)
 *
 * 	Ensure that any D-cache lines for the interval [start, end)
 * 	are invalidated. Any partial lines at the ends of the interval are
 *	also cleaned to PoC to prevent data loss.
 *
 *	- start - kernel start address of region
 *	- end - kernel end address of region
 */
SYM_FUNC_START_LOCAL(__dma_inv_area)
SYM_FUNC_START_PI(dcache_inval_poc)
	/* FALLTHROUGH */

/*
 *	__dma_inv_area(start, end)
 *	- start - virtual start address of region
 *	- end - virtual end address of region
 */
	dcache_line_size x2, x3
	sub	x3, x2, #1
	tst	x1, x3				// end cache line aligned?
	bic	x1, x1, x3
	b.eq	1f
	dc	civac, x1			// clean & invalidate D / U line
1:	tst	x0, x3				// start cache line aligned?
	bic	x0, x0, x3
	b.eq	2f
	dc	civac, x0			// clean & invalidate D / U line
	b	3f
2:	dc	ivac, x0			// invalidate D / U line
3:	add	x0, x0, x2
	cmp	x0, x1
	b.lo	2b
	dsb	sy
	ret
SYM_FUNC_END_PI(dcache_inval_poc)
SYM_FUNC_END(__dma_inv_area)
```

这里有一点需要注意，在 invalidate cache 时需要判断 [start, end) 段内存是否是 cache line 对齐。如果不对齐，那么需要对该 cache line 执行 clean & invalidate cache 操作，额外的 clean cache 操作是因为 cache 操作是以 cache line 为单位的。不对齐的情况下，直接 invalidate cache 会将该 cache line 中其他进程的数据 invalidate 掉，导致其他进程 cache 数据异常。所以要先 clean，将该 cache line 中其他进程的数据写回到 DDR 中，防止丢失。

##### arch_sync_dma_for_device

在 CPU 写完后，需要将数据刷到 DDR 中，

```c
void arch_sync_dma_for_device(phys_addr_t paddr, size_t size,
		enum dma_data_direction dir)
{
	__dma_map_area(phys_to_virt(paddr), size, dir);
}

/*
 *	__dma_map_area(start, size, dir)
 *	- start	- kernel virtual start address
 *	- size	- size of region
 *	- dir	- DMA direction
 */
SYM_FUNC_START_PI(__dma_map_area) // for_device
	add	x1, x0, x1
	b	__dma_clean_area
SYM_FUNC_END_PI(__dma_map_area)

/*
 *	dcache_clean_poc(start, end)
 *
 * 	Ensure that any D-cache lines for the interval [start, end)
 * 	are cleaned to the PoC.
 *
 *	- start - virtual start address of region
 *	- end - virtual end address of region
 */
SYM_FUNC_START_LOCAL(__dma_clean_area)
SYM_FUNC_START_PI(dcache_clean_poc)
	/* FALLTHROUGH */

/*
 *	__dma_clean_area(start, end)
 *	- start - virtual start address of region
 *	- end - virtual end address of region
 */
	dcache_by_line_op cvac, sy, x0, x1, x2, x3
	ret
SYM_FUNC_END_PI(dcache_clean_poc)
SYM_FUNC_END(__dma_clean_area)
```

由于协议栈下来的包的数据有可能还在 cache 里面，调用 `dma_map_single` 后，CPU 就会做一次 flush cache，将 cache 的数据刷到内存，这样 DMA 去读内存就读到新的数据了。

注意，在 map 的时候要指定一个参数，来指明数据的方向是从外设到内存还是从内存到外设：

- DMA_TO_DEVICE：从内存到外设，CPU 会做 cache 的 flush 操作，将 cache 中新的数据刷到内存。这个同步操作必须在软件最后一次修改完这块内存区域之后，在将数据从内存写到设备之前做；
- DMA_FROM_DEVICE：从外设到内存，CPU 将 cache 置无效，这样 CPU 读的时候不命中，就会从内存去读新的数据。这个同步操作必须在驱动访问某块可能被设备更改的内存之前；
- DMA_BIDIRECTIONAL：这个属性需要特殊的处理，它意味着驱动不确定数据在写入设备前是否被修改了，也不确定设备是否也修改了这部分数据。所以需要做两次同步操作，一次是在数据从内存写入到设备前，保证所有的修改都从 CPU 中写入到内存中；一次是在设备使用这块内存之后，访问之前，保证 CPU 中 cacheline 的数据是设备更改后最新的。

这个参数在 `dma_buf_ioctl` 中会确认，

```c
static long dma_buf_ioctl(struct file *file,
			 unsigned int cmd, unsigned long arg)
{
	...

	switch (cmd) {
	case DMA_BUF_IOCTL_SYNC:
		if (copy_from_user(&sync, (void __user *) arg, sizeof(sync)))
			return -EFAULT;
		switch (sync.flags & DMA_BUF_SYNC_RW) {
		case DMA_BUF_SYNC_READ:
			direction = DMA_FROM_DEVICE;
			break;
		case DMA_BUF_SYNC_WRITE:
			direction = DMA_TO_DEVICE;
			break;
		case DMA_BUF_SYNC_RW:
			direction = DMA_BIDIRECTIONAL;
			break;
		default:
			return -EINVAL;
		}

        // 这里会调用到各个 heap 的 begin_cpu_access/end_cpu_access 函数
		if (sync.flags & DMA_BUF_SYNC_END)
			ret = dma_buf_end_cpu_access(dmabuf, direction);
		else
			ret = dma_buf_begin_cpu_access(dmabuf, direction);
		return ret;
	}
}
```

还要注意，这几个接口都是一次性的，每次操作数据都要调用一次 map 和 unmap。并且在 map 期间，CPU 不能去操作这段内存，因此如果 CPU 去写，就又不一致了。

我们来分析一下 DMA HEAP 的代码，看哪些情况需要刷 cache，以及为什么需要刷 cache。

DMA HEAP 框架的使用流程一般为申请到一块 dma buf 后，任何组件拿到该 dma buf 对应的 fd，都可以调用 attach, attachment 接口来使用这块 dma buf。在调用 attachment 将 iova/pa 写入到 sg->dma_address 后，需要调用 `arch_sync_dma_for_device` clean cache。

```c
| dma_buf_map_attachment
| 	-> __map_dma_buf
| 		-> system_heap_map_dma_buf
| 			-> dma_map_sgtable
| 				-> __dma_map_sg_attrs
| 					-> dma_direct_map_sg // no <iommus> attribute in dts
| 						-> dma_direct_map_page // 该接口在建立映射前调用
| 							-> arch_sync_dma_for_device
| 								-> __dma_map_area
| 					-> iommu_dma_map_sg // has <iommus> attribute in dts
| 						-> iommu_dma_sync_sg_for_device
| 							-> arch_sync_dma_for_device // 该接口在建立映射前调用
```
同样，在 unmap_attachment 函数中，在解除映射前也会做一次 invalid cache 操作。

在 attachment 建立映射前需要 clean cache 是为了确保 cache 中没有该块内存的 dirty 数据。如果 cache 存在该块内存的 dirty 数据，当 DMA 设备向 DDR 中写入了新的数据，而 CPU 在读取之前会调用 begin_cpu_access 做一次 invalidate cache，该操作可能会导致 cache 中该内存的 dirty 数据写回到 DDR，覆盖了 DMA 设备新写入的数据，导致数据异常。

在 unmap_attachment 解除映射前需要 invalidate cache？确保 cache 中没有该 va 对应的缓存，如果有的话，下次该 va 又分配出去了，访问的时候直接 cache hit，就会访问到错误的数据。而 mmap/vmap 等映射接口也会做这件事，不过在做之前，会先判断一下，该 pte 是否存在。

#### 其他方式

上面说的是常规 DMA，有些 SoC 可以用硬件做 CPU 和外设的 cache coherence，例如在 SoC 中集成了叫做“Cache Coherent interconnect”的硬件，它可以做到让 DMA 访问到 CPU 的 cache 或者帮忙做 cache 的刷新。这样的话，dma_alloc_coherent 申请的内存就没必要是非 cache 的。

**需要注意的是，使用上述接口刷 cache 的耗时和 cacheline 中数据的 dirty 情况有关。**

### 各种接口内存分配粒度

#### dma_alloc_coherent

- dma_alloc_coherent（第一条路径）：2^n 对齐。如申请 26MB，分配 32MB，另外，分配时从申请内存的 n 倍位置开始寻找连续的，符合要求的内存块，如该块内存总共有 38MB，第一次分配了 32MB，剩余 6MB，如果直接申请 6MB，会分配失败，因为算法会从 6*n 位置开始寻找，0，6，12...36，这样无法找到连续的 6MB 内存空间；
- dma_alloc_coherent（第二条路径）：
- dma_alloc_coherent（第三条路径）：

### set_pte

- set_pte 做了什么？
 ```c
 static inline void __set_pte_at(struct mm_struct *mm,
 				unsigned long __always_unused addr,
 				pte_t *ptep, pte_t pte, unsigned int nr)
 {
 	__sync_cache_and_tags(pte, nr); // 检查是否要 invalidate cache
 	__check_safe_pte_update(mm, ptep, pte); // 检查能够更改 pte，是否是 dirty，或者 readonly 之类的
 	__set_pte(ptep, pte); // WRITE_ONCE
 }
 ```

- 软件页表和硬件页表分别是怎样的？
 - 页表都是存储在 DDR 中的，使用中需要加载到 MMU 中，通过更改 CR3/TTBR 寄存器来做到这一点；
- mmu 查找页表的流程是怎样的？

### 思考

- 这些优化方法要了解概念不难，但是没有做过相应的项目，根本无法产生深入的理解，关键的参数也不知道怎么设置；

### Reference

[1] 计算机体系结构：量化研究方法

[2] 奔跑吧 Linux 内核
