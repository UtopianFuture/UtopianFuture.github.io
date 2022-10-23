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

这个其实是一个很容易理解的想法，将 SSD 作为 HDD 的 cache，亦或者是将本地硬盘作为远程硬盘的 cache。

而为了做到这一点，bcache 在 block layer 加上 hooks，这样就可以劫持文件系统操作。然后将 cache 设备按照磁盘的分区分成一个个的 buckets，同时还有一个类似于 superblock 的数组单独存储在 cache 设备中。这样就能够使整个 cache 操作对于 os 来说是透明的。

好家伙，内核中有这部分代码，

/linux-5.15/drivers/md/bcache/super.c

```
/*
 * Module hooks
 */
module_exit(bcache_exit);
module_init(bcache_init);

module_param(bch_cutoff_writeback, uint, 0);
MODULE_PARM_DESC(bch_cutoff_writeback, "threshold to cutoff writeback");

module_param(bch_cutoff_writeback_sync, uint, 0);
MODULE_PARM_DESC(bch_cutoff_writeback_sync, "hard threshold to cutoff writeback");

MODULE_DESCRIPTION("Bcache: a Linux block layer cache");
MODULE_AUTHOR("Kent Overstreet <kent.overstreet@gmail.com>");
MODULE_LICENSE("GPL");
```

### [How likely should likely() be?](https://lwn.net/Articles/70473/)

这篇文章主要是介绍 `likely()/unlikely()`。因为分支预测的原因，可以通过这两个“函数”暗示编译器这个条件判断的结果，从而提高性能，但是这两个函数在不同架构上的实现是不一样的，对于那些站在更高角度的 maintainer 来说，这样不是很好，应该用 `probable(condition, percent)` 这样的 macro 来替换。其给出的原因是使用这两个 macro 需要 programmer 去判断条件判断的结果，万一判断错了，带来的代价会很大。

### [The cost of inline functions](https://lwn.net/Articles/82495/)

inline 函数在每一个调用函数中生成一个 copy，从而避免函数跳转带来的性能损失。但是在 socket buffers 中这样做却会带来性能损失，因为在 'SKBs' 中太多 inline 函数，使得整个内核变大，然后 cache miss 增大，性能下降。

### [A filesystem for namespaces](https://lwn.net/Articles/877308/)

namespacefs 是一种内核提供的能够展示运行的 namespaces 之间的层次结构，能够为开发者提供有效的检测、监控所有正在运行的 containers 的方法。

但是这个 patch 在如何表示每个 containers 上遇到了很大的问题，问题的关键在于内核中其实没有 container 这个概念，有的只是一个个 namespace。

其实 namespacefs 这个机制以后也很难合并到内核中，表面上看这方面的工作无法达到预期的目标，但是它能够让大家一起来讨论现有的内核有什么问题，如没有 container 概念，这会让大家进一步解决这方面的问题。

而这篇文章对我的意义是又告诉我一遍：你不懂 namespace :joy:.

### [Namespaces in operation, part 1: namespaces overview](https://lwn.net/Articles/531114/)

首先 namaspace 的提出是为了支持 containers——一种轻量化虚拟机的实现。

按照在内核中的实现的顺序，文章介绍了几种 namespace：

- mount namespaces：其隔离了文件系统的挂载点，使得每个 namespace 的进程看到的文件系统结构是不一样的。然后 mount 和 umount 系统调用操作的也不再是全局变量，而是仅能影响到每个 namespace。同时该 namespace 能够构成一个主从树状结构；
- UTS namespaces: 该 namespace 影响的主要是 uname 系统调用（不知道这个 uname 系统调用和 uname 命令是不是同一个东西，如果是同一个东西，那么就很好理解），其隔离两个系统标识符 `nodename` 和 `domainname`，使得每个 container 能够拥有自己的 hostname 和 [NIS](https://en.wikipedia.org/wiki/Network_Information_Service) domain name；
- IPC namespaces: 隔离 interprocess communication 资源，包括 System V IPC 和 POSIX message queues；
- PID namespaces: 隔离 PID，不同的 namespace 中进程可以有相同的 PID，这样做的好处是能够在 containers 热迁移时继续相同的 PID。同时相同的进程在不同层次的 namespace 是有不同的 PID；
- network namespaces: 每个 network namespace 有它们自己的网络设备（虚拟的）、IP 地址、IP 路由表、端口号等等；
- user namespaces: 没搞懂这个和 PID 有什么区别，user and group ID 是啥？内核中有这个结构么。但是它能实现的功能很有意思，一个在 namespace 外普通特权级（这里说的不准确，那有什么普通特权级，应该是特权级为 3）的进程在 namespace 内能够有成为特权级 0 的进程；

应该还有其他的 namespace 会慢慢开发出来。

### [Namespaces in operation, part 2: the namespaces API](https://lwn.net/Articles/531381/)

主要介绍了基本的 namesapces API：

- clone：创造一个新的 namespace

  ```c
  child_pid =
        clone(childFunc, child_stack + STACK_SIZE, /* Points to start of
                                                      downwardly growing stack */
              CLONE_NEWUTS | SIGCHLD, argv[1]);
  ```

- setns：能够使进程加入一个新的 namespace；

- unshare：使进程离开某个 namespace；

### [Namespaces in operation, part 3: PID namespaces](https://lwn.net/Articles/531419/)
