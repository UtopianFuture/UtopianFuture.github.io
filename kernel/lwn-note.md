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

PID namespace 能够创建不同的 PID，那么会产生一个问题，新的 namespace 中子进程的父进程是什么？因为在新的 namespace 中不能再看到原先 namespace 的进程，这里 clone 将新进程的 ppid 统一设置为 0。

同时 pid namespaces 也能够嵌套使用，子 namespace 能够看到（看到意味着能够发送信号给这些进程）所有在该 namespace 中的进程，包括嵌套在其中的进程，但不能看见父 namespace 的进程。

### [Namespaces in operation, part 4: more on PID namespaces](https://lwn.net/Articles/532748/)

这个系列的文章非常硬核，需要花更多的时间去理解，和这篇博客的出发点不太合适，所以单独列出来成为[一篇](https://github.com/UtopianFuture/UtopianFuture.github.io/blob/master/kernel/Namespaces.md)博客。

### [CMA and ARM](https://lwn.net/Articles/450286/)

CMA(contiguous memory allocator) 能够为驱动提供大块的、连续的 DMA 缓存而不需要内存再来处理这一请求。但是这种做法有一个隐含的问题——cache conflicting。CMA 分配的 cache coherent DMA buffer 必须是 “uncached”，但是如果其他的具有不同 cache 属性的内存映射到同一位置，那么就会产生冲突。内核在 boot 阶段为了访存性能会创建一个 "linear mapping"，即在内核空间能够访问所有的内存地址，在 32 位系统中这一工作都是在低地址空间完成的（ZONE_HIGH），而这些低地址空间是能够使用 cache 的，这些 cache 就会和 CMA 的地址产生冲突。

而 ARM 的 maintainer 认为 cache conflicting 在 ARM CPU 中有发生的风险，拒绝 merge。

解决的方法有两个：

- CMA 只分配高地址空间。高地址空间不会参与 cache
- 将用做 coherent DMA buffer 的内存从内核的线性地址空间移除，直到该 buffer 不再使用；

好吧，CMA 和 CAM 不是同一个东西。

### [AMD memory encryption technologies](https://lwn.net/Articles/699820/)

在云计算环境中，能够通过 "user-aeecss attacks" 和 "physical-access attacks" 来从一个 guest 访问到其他 guest 的内存或者在 host 中访问所有 guest 的内存。该加密技术能够解决这一问题，其实现是一个 AMD Secure Processor，其中包含两个特性：SME(Secure Memory Encryption) 和 SEV(Secure Encrypted Virtualization)，目前将一个单独的 32-bit ARM Cortex A5 集成到 SoC 中。

- SME 比较简单。其在 boot 阶段产生一个随机数来透明的加密在页表中所有被标记为 "encrypted" 的页，OS 或 hypervisor 管理哪些页会被加密，这个标记其实就是一个比特位。该特性是为了防范 "physical-access attacks" 的；
- SVE 更加复杂。它有多个 encryption keys 去保护所有的 guest 内存不受其他 guest 和 hypervisor 的攻击。在 boot 阶段，hypervisor 会在 guest 和 scure processor 之间创建一个 secure channel，然后为每个 guest 分配一个 ASID，根据 ASID 生成一个ie key，这个 key 就是访问对应 guest 内存的关键。

虽然听起来不错，但 hypervisor 肯定还是有办法 hack 到 guest 的内存吧。

### [Supporting Intel/AMD memory encryption](https://lwn.net/Articles/752683/)

Intel 和 AMD 都有自己的内存加密方式：

- intel 是将地址高 6 位作为加密位；
- AMD 只是将最高位作为加密位；

但是地址加密会带来一个问题，同样内容的 page 但是不同的 key 会在 cache 中同时存在，这就会导致性能的下降。maintainer 提出的一种方法是记住最后使用的 page，这样如果如果新分配的 page 和原来的 page 具有相同的 key，那么就不需要刷新任何 old cache。

这需要包装一下现有的 page allocator（也就是 buddy system）。设想是将 key 存储在 `page` 中，但其他子系统的 maintainer 认为这样会增加 page allocator 的复杂性，尤其是内存加密可能只在未来的 CPU 中能更好的工作，当前的工作没有必要以及其他不同意见。目前的做法是将 key 存储在 `anon_vma` 中，但是这意味着只有匿名页面能使用这一特性。

内存加密这一技术还在完善中。

哈哈哈，不过评论很有意思”可能未来有人会问 Intel 为啥要花费前 10% 的比特位来作为地址加密的 ID“。不过目前来看地址位是够用的。

### [Saving frequency scaling in the data center](https://lwn.net/Articles/820872/)

Frequency scaling，频率调整，能够在低工作负载的情况下调整 CPU 的频率，从而节能。但是在 data center 中这一功能似乎没啥用，因为 data center 不在乎能耗，更关注能不能获得最好的硬件性能。

### [Bringing kgdb into 2.6](https://lwn.net/Articles/70465/)

看了 Linus 关于 debugger 的[邮件](https://lwn.net/2000/0914/a/lt-debugger.php3)，很有意思，收获很大。确实，不用 debugger 会迫使你从其他的角度去思考你的代码，从而真正理解你的代码。另一方面是面对 bug 的态度，不应该感到沮丧，而是以后要加倍小心。但这在实际的工作环境中是否合适，会不会效率太低被淘汰？

回到 kgdb 的问题，Linus 的态度是内核不会在 mainline 中支持 kgdb，但其他开发者对其持开放态度。

### [KVM, QEMU, and kernel project management](https://lwn.net/Articles/379869/)

好吧，这篇文章每咋看懂，阅读能力还需要增强。开发者在讨论要不要将 qemu 合并到 kernel 中，因为 qemu 和 kvm 是强相关的，合并会使整个开发进度更快。但是 qemu 的开发者不这样认为，qemu 不仅仅是 kvm 在用（这个确实，很多基于 qemu 的二次开发，比如我们），合并会使得 qemu 的开发者和用户减少。内核开发是非常严肃的事情，有事说事，我现在的心态、习惯不太适合，需要改变。

### [On saying "no"](https://lwn.net/Articles/571995/)

几个核心的 maintainer 在抱怨他们对代码的审核能力不足——“that kernel maintainers are getting old and just don't have the energy to fight against substandard code like they once used to"，很多非必要的代码合入了内核，导致例如"Linux security modules debacle" 的问题。内核开发者也面临着退休和人手不足的问题，很多代码没人继续跟进。解决的方法是搞一个 "graybeards@" 的列表，所有 Kernel Summit 参与者都要订阅，否则会被视为对核心 issues 不感兴趣，不想参加 Kernel Summit。不理解，这是为了筛选 maintainer 么？

### [Security requirements for new kernel features](https://lwn.net/Articles/902466/)

首先是内核文件系统新支持的 io_uring 子系统没有经过内核安全社区足够的检查，即在 io_uring 代码中没有安全或审查 hooking，这使得 io_uring 的各种操作在安全模块的控制之外。之后各个 maintainer 聊到这不仅仅是 io_uring 一个 feature 的问题，而是 LSM(Linux Security Module) 没有足够的人再去维护了，这些 maintainer 希望有更多的公司允许开发者花时间去 review 这 LSM 方面的 patch。

### [Linux kernel design patterns - part 1](https://lwn.net/Articles/336224/)

这个系列的文章探究内核中的设计模式。设计模式在本科的时候学过，不过基本都忘记了。而内核中的设计模式主要是为了提高代码质量：例如检查代码格式、检查锁的使用、控制进程对未分配内存的访问等等。

设计模式可以简单理解为对一类问题的描述，以及能够高效解决这类问题的方法。而 developer 或 reviewer 使用这类术语能够高效的工作。

这篇文章首先介绍 reference count。其实就是使用一个计数器来管理某个对象的生存周期，但是简单的加加减减操作在一些临界情况会出问题，所以抽象出一个设计模式来对其进行总结。

内核中常用的引用可以分为两种："external" 和 "internal"。

- external：这类引用通常是通过 "get" 和 "put" 之类的操作来在其他子系统中使用当前子系统中定义的对象；
- internal：这类引用通常不用做计数，在当前子系统内部使用；

#### The "kref" style

```c
if (atomic_dec_and_test(&obj->refcnt)) { ... do stuff ... }
```

这种就是 "kref" style，其适用于对象的生存周期不会超过它最后一次引用（？）。但它貌似就是正常的对计数器进行操作，

```c
struct kref {
	refcount_t refcount;
};

/**
 * kref_get - increment refcount for object.
 * @kref: object.
 */
static inline void kref_get(struct kref *kref)
{
	refcount_inc(&kref->refcount);
}
```

#### The "kcref" style

在内核中没有 "kcref" 结构（到目前也没有）。多了个 'c' 表示 "cached"，表示引用的对象在 cache 中经常使用。

```c
if (atomic_dec_and_lock(&obj->refcnt, &subsystem_lock)) {
    ..... do stuff ....
	spin_unlock(&subsystem_lock);
}
```

#### The "plain" style

这个就是正常的计数器操作，

```c
static inline void put_bh(struct buffer_head *bh)
{
    smp_mb__before_atomic_dec();
    atomic_dec(&bh->b_count);
}
```

### [Linux kernel design patterns - part 2](Linux kernel design patterns - part 2)

这篇文章讨论复杂数据结构方面的设计模式。

首先是为什么内核中需要使用抽象数据类型，例如 "Linked Listd", "RB-trees", "Radix tree"。

- 封装细节，提供更加简介的代码隔离，程序员不需要了解具体的实现也可能使用；
- 为了性能（？）；

#### Linked Lists

```c
struct list_head {
	struct list_head *next, *prev;
};
```

需要仔细研究一下内核的 "Linked Lists" 是怎么样实现的。

通过 "Linked Lists" 的实现，可以总结出两个有价值的模式

- **Embedded Anchor**: A good way to include generic objects in a data structure is to embed an anchor in them and build the data structure around the anchors. The object can be found from the anchor using container_of();
- **Broad Interfaces**: Don't fall for the trap of thinking that "one size fits all". While having 20 or more macros that all do much the same thing is uncommon, it can be a very appropriate way of dealing with the complexity of finding the optimal solution. Trying to squeeze all possibilities into one narrow interface can be inefficient and choosing not to provide for all possibilities is counter-productive.

#### RB-trees

对于红黑树的特性还不了解，

```c
struct rb_node {
	unsigned long  __rb_parent_color;
	struct rb_node *rb_right;
	struct rb_node *rb_left;
} __attribute__((aligned(sizeof(long))));
```

- **Tool Box**: Sometimes it is best not to provide a complete solution for a generic service, but rather to provide a suite of tools that can be used to build custom solutions.

#### Radix tree

### [Linux kernel design patterns - part 3](https://lwn.net/Articles/336262/)

这篇文章主要讨论 "midlayer" 这一设计思想。在我的认知中，这一思想很早就出现了吧，忘记在哪本书看到这样一句话“计算机科学领域的任何问题都可以通过增加一个间接的中间层来解决“，这句话几乎概括了计算机软件体系结构的设计要点.整个体系从上到下都是按照严格的层级结构设计的，而这个中间层通过映射来连接上下文。

- **Midlayer Mistake**: When services need to be provided to a number of low-level drivers, provide them with a library rather than imposing them with a midlayer.

这个 pattern 不懂。

### [Trees I: Radix trees](https://lwn.net/Articles/175432/)

理解 radix tree 看这张图就可以了，

![[big radix tree]](https://static.lwn.net/images/ns/kernel/radix-tree-2.png)

通过一个 64 bit 的 integer key 就可以找到是哪个 node。

关键的数据结构如下：

```c
#define radix_tree_root		xarray
struct xarray {
	spinlock_t	xa_lock;
/* private: The rest of the data structure is not to be used directly. */
	gfp_t		xa_flags;
	void __rcu *	xa_head;
};
```

Radix trees 的声明有两种方式：

```c
#define RADIX_TREE_INIT(name, mask)	XARRAY_INIT(name, mask)

#define RADIX_TREE(name, mask) \ // 使用 name 声明和初始化一个 radix tree
	struct radix_tree_root name = RADIX_TREE_INIT(name, mask)

#define INIT_RADIX_TREE(root, mask) xa_init_flags(root, mask) // 运行时初始化
```

radix trees 最常用的是在内存管理部分，用于跟踪后备存储的 address_space 结构包含一个跟踪与该映射相关的核心页面（？）的 radix tree。同时，address space 中的这棵 radix tree 也能让内存管理代码能够快速找到脏页和写回页。

后面有需要再进一步分析。

### [Trees II: red-black trees](https://lwn.net/Articles/184495/)

这篇文章没有介绍 red-black tree 的原理，主要是介绍其使用场景和使用原语。

- The anticipatory, deadline, and CFQ I/O schedulers all employ rbtrees to track requests;
- the packet CD/DVD driver does the same;
- The high-resolution timer code uses an rbtree to organize outstanding timer requests;
- The ext3 filesystem tracks directory entries in a red-black tree;
- Virtual memory areas (VMAs) are tracked with red-black trees, as are epoll file descriptors, cryptographic keys, and network packets in the "hierarchical token bucket" scheduler.

其原理可以看[这里](https://www.jianshu.com/p/e136ec79235c)。

### [A retry-based AIO infrastructure](https://lwn.net/Articles/73847/)

这段话是对 AIO 的最佳描述：

> The architecture implemented by these patches is based on **retries**. When an asynchronous file operation is requested, the code gets things started and goes as far as it can until something would block; at that point it makes a note and returns to the caller. Later, when the roadblock has been taken care of, **the operation is retried until the next blocking point is hit**. Eventually, all the work gets done and user space can be notified that the requested operation is complete. The initial work is done in the context of the process which first requested the operation; **the retries are handled out of a workqueue**.

关于 Async [这里](https://dev.to/thibmaek/explain-coroutines-like-im-five-2d9)的解释更加直接。

**The problem async trying to solve is that IOs are slow**, but suffer very little performance hit when parallelized. So if you have 10 IOs that take one second each, running them synchronously one after the other will take 10 seconds:

```pseudocode
for io in ios:  # 10 iterations
    io.start()  # so fast we are not going to measure it
    io.wait()  # takes 1 second
    process_result(io.result)  # so fast we are not going to measure it
# Overall - 10 sconds
```

But - if you can perform all of them **in parallel** it will take one second **total**:

```pseudocode
for io in ios:
    io.start()
    pending.add(io)

while pending:
    io = pending.poll_one_that_is_ready()
    process_result(io.result)
```

The `for` loop is immediate - all it does is start the IOs(doesn't block) and add them to a data structure. In the second loop, the first iteration will take one second on `poll_one_that_is_ready()`. But during that second, the other 9 IOs were also running so they are also ready - and in the following iterations `poll_one_that_is_ready()` will be immediate. Since everything else will also be so much faster than 1 second that it can be considered immediate - the entire thing will run in 1 second.

So, this is what async means - you start an IO and instead of waiting for it to finish you go to do other things(like sending more IOs).

### [Asynchronous I/O and vectored operations](https://lwn.net/Articles/170954/)

这篇文章主要是讨论怎样修改 read 系统调用的接口，使得 aio 能够批量的处理 I/O 操作。这个 [patch](API changes: interrupt handlers and vectored I/O) 最终确认了 aio 的 API。

```c
ssize_t (*aio_read) (struct kiocb *iocb, const struct iovec *iov,
             unsigned long niov, loff_t pos);
ssize_t (*aio_write) (struct kiocb *iocb, const struct iovec *iov,
             unsigned long niov, loff_t pos);
```

### [Asynchronous buffered file I/O](https://lwn.net/Articles/216200/)

> Asynchronous I/O (AIO) operations have the property of not blocking in the kernel. If an operation cannot be completed immediately, it is set in motion and control returns to the calling application while things are still in progress.

看了那么多关于 AIO 的文章，发现还是要看这些英文资料更加简明易懂，fxxk。

这篇文章主要介绍 [Buffered I/O](https://github.com/UtopianFuture/UtopianFuture.github.io/blob/master/linux-note/others.md#buffered-io) 是怎样处理 AIO 的。大致流程是这样的：

```pseudocode
for each file page to be read
	get the page into the page cache // 这里会被 block，需要进行 I/O 操作
	copy the contents to the user buffer
```

在 "get the page" 阶段，内核会创建一个 "wait entry" 加入到 `task_struct` 的 "wait queue" 中，然后返回到用户空间继续执行，等到 I/O 操作完成后，AIO queue 相关联的 "wakeup function" 会完成接下来的工作。

"wakeup function" 不会继续在原来发起 I/O 操作的进程上下文执行，而是创建一个 workqueue 线程去检查 I/O 操作的状态，如果需要的话，就返回到原来的进程继续执行。而在返回前，workqueue 线程还需要调整一个自己的 `mm_struct` 从而与发起 I/O 操作的进程共享进程地址空间，这样才能将 page 复制到 "user buffer"。当然，需要复制的可能不止一个 page，如果下一个 page 不在 cache  中，那么 workqueue 线程会确保新的获取 page 操作正常开始了，然后再 "go to sleep"，这样反复，直到整个 I/O 操作完成。

### [Fibrils and asynchronous system calls](https://lwn.net/Articles/219954/)

> The kernel's support for asynchronous I/O is incomplete, and it always has been. While certain types of operations (direct filesystem I/O, for example) work well in an asynchronous mode, many others do not. Often implementing asynchronous operation is hard, and nobody has ever gotten around to making it work. In other cases, patches have been around for some time, but they have not made it into the mainline; AIO patches can be fairly intrusive and hard to merge. Regardless of the reason, things tend to move very slowly in the AIO area.

总的来说就是 AIO 只能在部分 I/O 操作中使用，例如 direct/buffered I/O 等等，然后要增加新的 AIO 操作很困难（？）这就导致 AIO 发展很慢。这也是为什么说 io_uring 是革命性的技术，**将 linux-aio 的所有优良特性带给了普罗大众**（而非局限于数据库这样的细分领域）。

> Could it be that the way the kernel implements AIO is all wrong?  ——Zach Brown

我去，灵魂发问。

Zach Brown 提出使用一个新的名为 "fibril" 的轻量级内核线程来完成所有的 I/O 操作，但貌似这个做法没有被接受。

大致的思路是使用新的用户态 API：

```c
struct asys_input {
	int 		syscall_nr; // 系统调用号
	unsigned long	cookie;
	unsigned long	nr_args;
	unsigned long	*args; // 传入的参数
};
```

然后内核会为每个 "asys" 请求创建一个 fibril 完成所有的工作并返回用户态。但我感觉这种方法和上面的没有本质区别的。

### [Kernel fibrillation](https://lwn.net/Articles/220897/)

社区就 "fibril" 这一想法进行进一步的讨论。

LWN 关于 AIO 的讨论还有很多，深入了解的话太耗时间，目前就到此为止，要看的东西太多了。

### [A JIT for packet filters](https://lwn.net/Articles/437981/)

BPF 的设想是能够让应用程序指定一个 filtering function 去根据网络包的 IP 选择它想要的网络包。BPF 最初是工作在用户态的，但是网络流量非常高，如果将所有的网络包都从内核态 copy 到用户态，效率很低，所以直接将 BPF 运行在内核态，过滤不需要的包。

BPF 的组成大概是这样的（我也每搞懂），其拥一小块内存区域，一些算术、逻辑、跳转指令，一个 implicit array 包含对包的限制以及 2 个寄存器：

- 累加器：其用来完成算术操作；
- 索引寄存器：提供包或内存区域的偏移量（？）；

最简单的 BPF 程序大概长这样：

```pseudocode
	ldh	[12] // 在包的 [12] 偏移量处加载 16bit 标量（the Ethernet protocol type field）到累加器中
	jeq	#ETHERTYPE_IP, l1, l2 // 判断该包是不是想要的
l1:	ret	#TRUE
l2:	ret	#0
```

之后的 maintainer 又增加了一些改变：

- JIT compiler：翻译 BPF 代码到 host 汇编代码，每一条 BPF 指令都对应一系列的 x86（或其他架构）指令；
- 一些汇编语言的 helper 来实现虚拟机的 senmantics（？）；
- 累加器和索引寄存器直接存储在 CPU 寄存器中；
- 运行结果存在的 `vmalloc` 分配的一段空间；

### [BPF: the universal in-kernel virtual machine](https://lwn.net/Articles/599755/)

这篇文章有用的不多，主要是介绍 BPF 的发展史，有兴趣可以看看。

### [Extending extended BPF](https://lwn.net/Articles/603983/)
