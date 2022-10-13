## LWN Note

长期以来我都沉浸在自己的世界，一个人学内核，主要是考虑到基础很薄弱，与其花很多时间去看内核方面的新闻、文章，不如先把内核基础的内存管理、进程调度、中断控制、并发同步和文件系统这些子模块搞清楚，以对整个内核有一定的了解，这样去看别人讨论或和别人讨论才能有底气，现在基础知识掌握的还行，该看看 LWN 和别人的博客，学些更有意思的东西。另一方面是最近和朋友聊天，我想到我们做技术的，不能总在国内卷，要出去和外国人竞争，所以也需要加强这方面的能力。

### [Why printk() is so complicated (and how to fix it)](https://lwn.net/Articles/800946/)

首先 printk 不像我们我们想象的那么简单，它有很长的发展历史，同时因为它在内核中无处不在，它需要考虑不同的场景，例如它会工作在 atomic context、NMIs 中等等，这就是它为什么这么复杂。可以将矛盾点分为两个：

- 非交互性：对于不重要的信息，我们可以将 printk 设计成完成可抢占的，这样对系统性能影响最小；
- 可靠性：对于重要的信息，例如 kernel panic，developer 需要快速，全面的了解原因，这类就涉及成 "atomic consoles"；

然后 printk 的实现似乎是涉及到 ring buffer，我以为就是简单从 serial 输出。其实不对，messages 都从 console 输出，但是其可能加载到 serial, graphic adapters or nexwork connections。

或许应该去看看这个 ring buffer 是什么东西。

### [Why RISC-V doesn't (yet) support KVM](https://lwn.net/Articles/856685/)

总的来说，RISC-V 的 KVM 支持其实已经完成了，并且在 hardware 上工作良好，但是 RISC-V 分支的代码合并策略（patch acceptance policy）有些问题，总在变化，导致 RISC-V KVM 系列 patch 一直处于 staging 状态，没有 frozen，不能合并和 mainline 中。

### [VMX virtualization runs afoul of split-lock detection](https://lwn.net/Articles/816918/)

这篇文章提到 split-lock（分体锁）的使用会影响到 VMX 的正确性。首先分体锁的使用是有一定意义的，它能够避开潜在的 denial-of-service attack vector（不清楚是啥），同时也能摆脱延迟密集型负载的性能问题。但是使用分体锁会导致 guest 需要处理对其检查异常。

这里涉及到到的分体锁之后去了解，同时对 x86 的非对齐访问也需要看看。

### [Detecting and handling split locks](https://lwn.net/Articles/790464/)

非对齐访问可以看看[这篇](https://www.sunxidong.com/532.html)文章，或者[这篇](https://www.kernel.org/doc/Documentation/unaligned-memory-access.txt#:~:text=Unaligned%20memory%20accesses%20occur%20when,be%20an%20unaligned%20memory%20access.)英文文章，当然这篇 lwn 的文章也介绍了什么是非对齐访问。总的来说，CPU 在硬件设计上只能进行对其访问，但是 x86 内核能够支持非对齐访问，一般的做法是多次读取对齐内存，然后进行数据拼接，从而实现非对齐访问。

而非对齐访问会导致分t锁问题，因为一个原子操作可能会被分成访问多个 cacheline，然后根据 cache 一致性协议，需要在多个 CPU 的 cache 中保持数据的正确性，这会导致不可预料的性能和安全问题。因此嵌入式和高性能设备应该避免使用非对齐访问（平时开发也尽量避免吧，不然要是遇到 bug 都要怀疑人生）。

目前 intel 的做法是提供了一个异常标志位 "Alignment Check"(`#AC`)，来表示硬件检测到非对其访问。当发生非对齐访问时，CPU 会锁住整个 memory bus，这样在访问多个 cacheline 时就不会有其他 CPU 的访存操作来影响 cache，这样来解决 cache 一致性问题。但是这样又会导致一系列的问题，比如当一个 guestos 发生非对齐访问时，其他的 guestos 都不能运行。

更好的解决方案还在路上。

### [Support for Intel's Linear Address Masking](https://lwn.net/Articles/902094/)

因为 64 位系统中，地址肯定用不完，所以可以将高位部分用来存储一些信息。Intel 提供两种方式：

- `LAM_U57` allows six bits of metadata in bits 62 to 57.
- `LAM_U48` allows 15 bits of metadata in bits 62 to 48.

这种方式和 AMD 的做法略有区别，AMD 是将整个高 7 位用来做额外用处，但因为内核是通过最高位地址是否为 1 来判断是内核地址还是用户地址，所以他的这种做法会导致很多问题。

### [BPF: the universal in-kernel virtual machine](https://lwn.net/Articles/599755/)

这篇文章主要是介绍的 BPF 的，借着这个契机，开始系统的学习 BPF。

### [Bcache: Caching beyond just RAM](https://lwn.net/Articles/394672/)

### MISC

- Spectre: it is a subset of security [vulnerabilities](https://en.wikipedia.org/wiki/Vulnerability_(computing)) within the class of vulnerabilities known as microarchitectural timing [side-channel attacks](https://en.wikipedia.org/wiki/Side-channel_attacks). 一个安全漏洞的 patch
- Zen 4: is the [codename](https://en.wikipedia.org/wiki/Codename) for a [CPU](https://en.wikipedia.org/wiki/CPU) [microarchitecture](https://en.wikipedia.org/wiki/Microarchitecture) by [AMD](https://en.wikipedia.org/wiki/AMD), released on September 27, 2022. It is the successor to [Zen 3](https://en.wikipedia.org/wiki/Zen_3) and uses [TSMC](https://en.wikipedia.org/wiki/TSMC)'s [5 nm](https://en.wikipedia.org/wiki/5_nm_process) process. Zen 4 powers [Ryzen 7000](https://en.wikipedia.org/wiki/Ryzen_7000) mainstream desktop processors (codenamed "Raphael") and will be used in high-end mobile processors (codenamed "Dragon Range"), thin & light mobile processors (codenamed "Phoenix"), as well as [Epyc](https://en.wikipedia.org/wiki/Epyc) 7004 server processors (codenamed "Genoa" and "Bergamo"). CPU 微架构
