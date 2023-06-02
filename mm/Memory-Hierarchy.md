## 内存管理

从社招角度来看，怎样才是一个 NB 的员工，就内存管理领域来看：

- 基础。这个基础是多方面的，语言的精准把握，对整个内核的掌控，例如内存的回收、释放过程，要有深度；
- 项目能力。对做过项目的细节把握，以及结合业务场景的整个解决流程，这是体现能力的地方；
- 眼界。内核、芯片领域的新技术、其他竞对的解决方案。

研一的时候其实看过《计算机体系结构：量化研究方法》，当时看的是英文版，个人感觉收获蛮大，算是我后来一切工作的基础。如今有机会继续从事这方面的工作，很大程度上可以归结为运气好。
现在需要深入研究内核的内存管理模块，所有书中有关内存的只是需要回顾一下，原来的[笔记](https://utopianfuture.github.io/Architecture/A-Quantitative-Approach.html)在这里。略显杂乱，看完之后没有进一步归纳总结，这里算是之前工作的进一步。

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

- 重名(aliasing)问题。多个不同的虚拟地址映射到相同的物理地址，因此会出现多个 cache line 存储同一个物理地址（对应多个虚拟地址）的情况。这样会引发问题。
  - 浪费 cache line；
  - 当执行写操作时，只更新了其中一个虚拟地址对应的 cache line，而其他的虚拟地址对应的高速缓存没有更新，导致之后访问到异常数据；
- 同名(homonyms)问题。相同的虚拟地址对应不同的物理地址（多个进行使用相同的虚拟地址进行映射），而虚拟地址经过 MMU 转换后对应不同的物理地址。在进程切换时可能发生，解决办法是切换时刷新该进程对应的所有 cache line。

#### cache 分类

在查询 cache 时，根据使用虚拟地址还是物理地址来查询 tag 域或 index 域，我们可以将 cache 分成如下几类：

- VIVT：使用虚拟地址的 tag 域和 index 域；
- PIPT：使用物理地址的 tag 域和 index 域；
- VIPT：使用虚拟地址的 index 域和物理地址的 tag 域。虚拟地址同时发送到 cache 和 MMU/TLB 进行处理。在 TLB/MMU 进行地址翻译时，使用虚拟地址的索引域和偏移量来查询高速缓存。当地址翻译介绍，在使用物理标记域来匹配 cache line。VIPT 也存在重名问题。

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

### 思考

- 这些优化方法要了解概念不难，但是没有做过相应的项目，根本无法产生深入的理解，关键的参数也不知道怎么设置；

### Reference

[1] 计算机体系结构：量化研究方法

[2] 奔跑吧 Linux 内核
