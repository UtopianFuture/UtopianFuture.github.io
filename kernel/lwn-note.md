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

而非对齐访问会导致分 t 锁问题，因为一个原子操作可能会被分成访问多个 cacheline，然后根据 cache 一致性协议，需要在多个 CPU 的 cache 中保持数据的正确性，这会导致不可预料的性能和安全问题。因此嵌入式和高性能设备应该避免使用非对齐访问（平时开发也尽量避免吧，不然要是遇到 bug 都要怀疑人生）。

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

```plain
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

### [How likely should likely() be](https://lwn.net/Articles/70473/)

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
- SVE 更加复杂。它有多个 encryption keys 去保护所有的 guest 内存不受其他 guest 和 hypervisor 的攻击。在 boot 阶段，hypervisor 会在 guest 和 scure processor 之间创建一个 secure channel，然后为每个 guest 分配一个 ASID，根据 ASID 生成一个 ie key，这个 key 就是访问对应 guest 内存的关键。

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

BPF 的设想是能够让应用程序指定一个 filtering function 去根据网络包的 IP 选择它想要的网络包。BPF 最初是工作在用户态的，但是网络流量非常高，如果将所有的网络包都从内核态 copy 到用户态，效率很低，所以**直接将 BPF 运行在内核态，过滤不需要的包**。

BPF 的组成大概是这样的（没搞懂），其拥一小块内存区域，一些算术、逻辑、跳转指令，一个 implicit array 包含对包的限制以及 2 个寄存器：

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
- 运行结果存在的 `vmalloc` 分配的一段空间。

### [BPF: the universal in-kernel virtual machine](https://lwn.net/Articles/599755/)

这篇文章有用的不多，主要是介绍 BPF 的发展史，有兴趣可以看看。

### [Extending extended BPF](https://lwn.net/Articles/603983/)

很多地方都提到 BPF 是一种特殊的虚拟机，哪里体现出它虚拟机的特性？

BPF 从网络子系统中移出来，称为一个独立的子系统 `kernel/bpf`，然后 Alexei 又提交了一系列的 patch，使 BPF 能够在用户态发起系统调用，然后在内核态执行，

```c
static int __sys_bpf(int cmd, bpfptr_t uattr, unsigned int size)
{ // 使用 wrapper 函数，而不是将不同的功能函数实现为单独的 syscall
	union bpf_attr attr;
	int err;

	if (sysctl_unprivileged_bpf_disabled && !bpf_capable())
		return -EPERM;

	err = bpf_check_uarg_tail_zero(uattr, sizeof(attr), size);
	if (err)
		return err;
	size = min_t(u32, size, sizeof(attr));

	/* copy attributes from user space, may be less than sizeof(bpf_attr) */
	memset(&attr, 0, sizeof(attr));
	if (copy_from_bpfptr(&attr, uattr, size) != 0)
		return -EFAULT;

	err = security_bpf(cmd, &attr, size);
	if (err < 0)
		return err;

	switch (cmd) {
	case BPF_MAP_CREATE:
		err = map_create(&attr);
		break;
	case BPF_MAP_LOOKUP_ELEM:
		err = map_lookup_elem(&attr);
		break;

    ...

	case BPF_PROG_BIND_MAP:
		err = bpf_prog_bind_map(&attr);
		break;
	default:
		err = -EINVAL;
		break;
	}

	return err;
}

SYSCALL_DEFINE3(bpf, int, cmd, union bpf_attr __user *, uattr, unsigned int, size)
{
	return __sys_bpf(cmd, USER_BPFPTR(uattr), size);
}
```

自然，在内核中运行代码会带来一系列的安全问题，所以 patch 中很大一部分是 'verifier' 的实现，'verifier' 的作用是模拟（？） BPF 代码的执行，如果有问题，那么这个程序就不会被加载到内核执行。其工作大致包含如下几类：

- 跟踪每个 BPF 寄存器，保证它们不会发生“写前读”；
- 跟踪每个寄存器的数据类型（？）；
- ld/st 指令只能操作包含正确数据类型的寄存器；
- 对所有指令进行边界检查；
- 保证 BPF 程序没有循环，能正常退出；

`bpf_prog_load` 中还定义了一个 `license` 变量，用来检查 BPF 程序是否使用 GPL 协议。

在这一系列的 patch 中，还增加了一个 feature——'map'，

> A map is a simple key/value data store that can be shared between user space and eBPF scripts and is persistent within the kernel.

这个 feature 没有搞懂有什么用。具体的使用可以看[这里](https://lwn.net/Articles/603984/)，

```c
static struct sock_filter_int prog[] = { // 这就是一个 BPF 程序么
	BPF_ALU64_REG(BPF_MOV, BPF_REG_6, BPF_REG_1),
	BPF_LD_ABS(BPF_B, 14 + 9 /* R0 = ip->proto */),
	BPF_STX_MEM(BPF_W, BPF_REG_10, BPF_REG_0, -4), /* *(u32 *)(fp - 4) = r0 */
	BPF_ALU64_REG(BPF_MOV, BPF_REG_2, BPF_REG_10),
	BPF_ALU64_IMM(BPF_ADD, BPF_REG_2, -4), /* r2 = fp - 4 */
	BPF_ALU64_IMM(BPF_MOV, BPF_REG_1, MAP_ID), /* r1 = MAP_ID */
	BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 0, 0, BPF_FUNC_map_lookup_elem),
	BPF_JMP_IMM(BPF_JEQ, BPF_REG_0, 0, 2),
	BPF_ALU64_IMM(BPF_MOV, BPF_REG_1, 1), /* r1 = 1 */
	BPF_RAW_INSN(BPF_STX | BPF_XADD | BPF_DW, BPF_REG_0, BPF_REG_1, 0, 0), /* xadd r0 += r1 */
	BPF_ALU64_IMM(BPF_MOV, BPF_REG_0, 0), /* r0 = 0 */
	BPF_EXIT_INSN(),
};
```

### [Persistent BPF objects](https://lwn.net/Articles/664688/)

背景知识了解的差不多了，接下来应该具体学学 BPF 是怎么用的。

### [A thorough introduction to eBPF](https://lwn.net/Articles/740157/)

这篇文章算是对前面几篇文章的总结，以及介绍如何在内核中使用它。

- What can you do with eBPF?

  eBPF 程序是 "attached" 到内核代码路径上的，所以除了最开始的“过滤”的功能，还能用来调试和分析内核。它相比其他分析方法，最大的优点就是“快速”和“安全”；

- The eBPF in-kernel verifier

  前面有介绍；

- The bpf() system call

  前面有介绍；

- eBPF program types

  目前 eBPF 支持的程序类型（这个看 syscall_bpf 很清楚）：

  - `BPF_PROG_TYPE_SOCKET_FILTER`: a network packet filter
  - `BPF_PROG_TYPE_KPROBE`: determine whether a kprobe should fire or not
  - `BPF_PROG_TYPE_SCHED_CLS`: a network traffic-control classifier
  - `BPF_PROG_TYPE_SCHED_ACT`: a network traffic-control action
  - `BPF_PROG_TYPE_TRACEPOINT`: determine whether a tracepoint should fire or not
  - `BPF_PROG_TYPE_XDP`: a network packet filter run from the device-driver receive path
  - `BPF_PROG_TYPE_PERF_EVENT`: determine whether a perf event handler should fire or not
  - `BPF_PROG_TYPE_CGROUP_SKB`: a network packet filter for control groups
  - `BPF_PROG_TYPE_CGROUP_SOCK`: a network packet filter for control groups that is allowed to modify socket options
  - `BPF_PROG_TYPE_LWT_*`: a network packet filter for lightweight tunnels
  - `BPF_PROG_TYPE_SOCK_OPS`: a program for setting socket parameters
  - `BPF_PROG_TYPE_SK_SKB`: a network packet filter for forwarding packets between sockets
  - `BPF_PROG_CGROUP_DEVICE`: determine if a device operation should be permitted or not

  还可以继续增加新的类型。

- eBPF data structures

  eBPF 程序使用的主要的数据结构就是 eBPF map。map 能够让数据在用户态和内核态之间来回传送。而数据是通过 key 去索引的，所以将其称之为 map。

  map 通过 `bpf` 系统调用创建，创建成功后会返回一个 `fd`，`fd` 销毁后 map 也就销毁了。内核中支持如下 map 类型：

  - `BPF_MAP_TYPE_HASH`: a hash table
  - `BPF_MAP_TYPE_ARRAY`: an array map, optimized for fast lookup speeds, often used for counters
  - `BPF_MAP_TYPE_PROG_ARRAY`: an array of file descriptors corresponding to eBPF programs; used to implement jump tables and sub-programs to handle specific packet protocols
  - `BPF_MAP_TYPE_PERCPU_ARRAY`: a per-CPU array, used to implement histograms of latency
  - `BPF_MAP_TYPE_PERF_EVENT_ARRAY`: stores pointers to `struct perf_event`, used to read and store perf event counters
  - `BPF_MAP_TYPE_CGROUP_ARRAY`: stores pointers to control groups
  - `BPF_MAP_TYPE_PERCPU_HASH`: a per-CPU hash table
  - `BPF_MAP_TYPE_LRU_HASH`: a hash table that only retains the most recently used items
  - `BPF_MAP_TYPE_LRU_PERCPU_HASH`: a per-CPU hash table that only retains the most recently used items
  - `BPF_MAP_TYPE_LPM_TRIE`: a longest-prefix match trie, good for matching IP addresses to a range
  - `BPF_MAP_TYPE_STACK_TRACE`: stores stack traces
  - `BPF_MAP_TYPE_ARRAY_OF_MAPS`: a map-in-map data structure
  - `BPF_MAP_TYPE_HASH_OF_MAPS`: a map-in-map data structure
  - `BPF_MAP_TYPE_DEVICE_MAP`: for storing and looking up network device references
  - `BPF_MAP_TYPE_SOCKET_MAP`: stores and looks up sockets and allows socket redirection with BPF helper functions

  所有的 map 在 eBPF 程序和用户程序中都可以通过 `bpf_map_lookup_elem` 和 `bpf_map_update_elem` 函数访问。

- How to write an eBPF program

  > Historically, it was necessary to write eBPF assembly by hand and use the kernel's `bpf_asm` assembler to generate BPF bytecode.

  好家伙，如果只能写汇编，那发展困难啊！

  现在可以直接用 C 写 eBPF 程序，然后用 LLVM Clang 使用 `-march=bpf` 参数编译成可执行文件，然后再用 `bpf` 系统调用加 `BPF_PROG_LOAD` 命令加载到内核中执行。

  在内核 `samples/bpf` 路径中有 bpf 程序的简单例子。带有 `_kern.c` 后缀的就是 bpf 程序，带有 `_user.c` 后缀的是使用这个 bpf 程序的用户程序。但是这种用法有个问题，需要将 bpf 程序编译到内核源码中才能使用，比较麻烦，所以又有了下面介绍的 BCC。

### [An introduction to the BPF Compiler Collection](https://lwn.net/Kernel/Index/)

介绍了如何写 eBPF 程序，有几个非常有用的网站：

- [BPF Compiler Collection](https://github.com/iovisor/bcc)

- [the BCC tools](https://www.brendangregg.com/ebpf.html)

#### Example - Hello World

在 `bcc/examples` 中有一个简单的例子 `hello_world.py`（python 的学习也要提上日程啊），

```c
#!/usr/bin/python
# Copyright (c) PLUMgrid, Inc.
# Licensed under the Apache License, Version 2.0 (the "License")

# run in project examples directory with:
# sudo ./hello_world.py"
# see trace_fields.py for a longer example

from bcc import BPF

# This may not work for 4.17 on x64, you need replace kprobe__sys_clone with kprobe____x64_sys_clone
BPF(text='int kprobe__sys_clone(void *ctx) { bpf_trace_printk("Hello, Clone!\\n"); return 0; }').trace_print()
```

这个 eBPF 程序会在每次执行 `clone` 系统调用时打印 `Hello, Clone!`。`kprobe__` 前缀就是 BCC 工具链将一个 kprobe attach 到内核 symbol 的标识。

text="..." 中的就是要执行的 eBPF 程序，BPF() 和 `trace_print()` 就是将 eBPF 代码加载到内核中运行的函数。

正确执行会产生如下输出：

```plain
ThreadPoolForeg-1323464 [003] d...1 343831.036264: bpf_trace_printk: Hello, Clone!'
ThreadPoolSingl-913145  [006] d...1 343835.207102: bpf_trace_printk: Hello, Clone!'
ThreadPoolForeg-1302374 [004] d...1 343842.572502: bpf_trace_printk: Hello, Clone!'
ThreadPoolForeg-1318650 [003] d...1 343842.576755: bpf_trace_printk: Hello, Clone!'
ThreadPoolForeg-1325165 [005] d...1 343842.580014: bpf_trace_printk: Hello, Clone!'
ThreadPoolForeg-1325165 [005] d...1 343842.581470: bpf_trace_printk: Hello, Clone!'
```

输出的格式为：

- 触发 kprobe 的进程；
- 进程 ID；
- 该进程运行的 CPU；
- 时间戳；

其实这个简单的 eBPF 程序其运行的流程就是上面介绍的，只不过所有复杂的工作都由 bcc 完成了，比如使用 LLVM 编译成可执行文件，调用 `bpf` 系统调用，使用 `BPF_PROG_LOAD` 命令将其加载到内核中。

#### More Exaples

更多 eBPF 的用法，先要懂一些 python 语法。

### [Some advanced BCC topics](https://lwn.net/Articles/747640/)

这篇文章介绍了 BPF 其他的用法。

BCC 提供了 `TRACEPOINT_PROBE` 宏，它声明了一个被挂载到 tracepoint 的函数，

```python
#!/usr/bin/env python

    from bcc import BPF
    from time import sleep

    program = """
    	# PF_HASH 固定用法，callers 是创建的 map，u64 是默认的 value，
    	# unsigned long 是 key
        BPF_HASH(callers, u64, unsigned long);

		# 参数为 tracepoint 的类型和要挂载的 tracepoint
        TRACEPOINT_PROBE(kmem, kmalloc) {
        	# kmalloc 传递的参数能够通过 args 访问
        	# args->call_site 就是 kmalloc 函数调用者的地址
            u64 ip = args->call_site;
            unsigned long *count;
            unsigned long c = 1;

			# 根据地址来判断不同的调用者，并计算不同的调用者执行 kmalloc 的次数
            count = callers.lookup((u64 *)&ip);
            if (count != 0)
                c += *count;

			# 将次数存储在创建的 hash table 中
			# 之后也可以通过 lookup 访问 hash table
            callers.update(&ip, &c);

            return 0;
        }
    """
    b = BPF(text=program)

    while True:
        try:
            sleep(1)
            for k,v in sorted(b["callers"].items()): # callers 就是 hash table
                # b.ksym() 函数能够将调用函数地址转换成符号
                print ("%s %u" % (b.ksym(k.value), v.value))
            print
        except KeyboardInterrupt: # python 直接定义了一个键盘终端啊
            exit()
```

这就是使用 BCC 调试内核的过程。

因为 BCC 中集成了 LLVM 的编译器，所以还可以使用 `cflags` 来控制编译过程，

```python
program = """
	...

""", cflags=["-w", "-DRETURNCODE=%s" % ret, "-DCTXTYPE=%s" % ctxtype,
			 "-DMAPTYPE=\"%s\"" % maptype],
     device=offload_device)
```

当然还可以使用 debugging flags 来调试。BCC 项目中有很多相关的例子，需要多看看源码，这比什么网上的教程都管用。

### [Using user-space tracepoints with BPF](https://lwn.net/Articles/753601/)

User Statically-Defined Tracing(USDT) ，BPF 可以用这种 probe 来分析、调试用户态程序（以后调试可以试试用这个）。

这里分析了一些 BCC 中的例子，之后有需要再详细看。

### [Binary portability for BPF programs](https://lwn.net/Articles/773198/)

使用 BPF 分析内核有个问题，某个结构体中的变量的偏移是随着架构、内核配置的改变而改变的，如果 BPF 没有正确获取到变量的偏移量，那么程序就无法获取正确的结果。

对于问题的场景和解决方法还无法理解。

### [Concurrency management in BPF](https://lwn.net/Articles/779120/)

eBPF 的 map 能够在内核态和用户态之间传递信息，也能在多个 eBPF 程序之间共享信息，这就会导致并发问题。所以 developers 讨论需要增加原子操作和自旋锁机制，然后也讨论了 BPF 的内存模型（BPF 有内存模型么？）。

#### BPF spinlock

`bpf_spin_lock` 的用法和一般的 spinlock 一样，

```c
struct hash_elem {
	int cnt;
   	struct bpf_spin_lock lock;
};

struct hash_elem *val = bpf_map_lookup_elem(&hash_map, &key);
if (val) {
   	bpf_spin_lock(&val->lock);
   	val->cnt++;
  	bpf_spin_unlock(&val->lock);
}
```

由于 eBPF 程序运行在要求严格的内核态，所以其使用 spinlock 要十分小心，[这里](https://lwn.net/ml/netdev/20190131234012.3712779-2-ast@kernel.org/)介绍了具体的要求，

- bpf_spin_lock is only allowed inside HASH and ARRAY maps.
- BTF description of the map is mandatory for safety analysis.
- bpf program can take one bpf_spin_lock at a time, since two or more can cause dead locks.
- only one 'struct bpf_spin_lock' is allowed per map element. It drastically simplifies implementation yet allows bpf program to use any number of bpf_spin_locks.
- when bpf_spin_lock is taken the calls (either bpf2bpf or helpers) are not allowed.
- bpf program must bpf_spin_unlock() before return.
- bpf program can access 'struct bpf_spin_lock' only via bpf_spin_lock()/bpf_spin_unlock() helpers.
- load/store into 'struct bpf_spin_lock lock;' field is not allowed.
- to use bpf_spin_lock() helper the BTF description of map value must be a struct and have 'struct bpf_spin_lock anyname;' field at the top level. Nested lock inside another struct is not allowed.
- syscall map_lookup doesn't copy bpf_spin_lock field to user space.
- syscall map_update and program map_update do not update bpf_spin_lock field.
- bpf_spin_lock cannot be on the stack or inside networking packet. bpf_spin_lock can only be inside HASH or ARRAY map value.
- bpf_spin_lock is available to root only and to all program types.
- bpf_spin_lock is not allowed in inner maps of map-in-map.
- ld_abs is not allowed inside spin_lock-ed region.
- tracing progs and socket filter progs cannot use bpf_spin_lock due to insufficient preemption checks

#### The BPF memory model

目前社区对于 BPF 内存模型的做法就是和底层架构保持一致，没有更好的做法。

### [Managing sysctl knobs with BPF](https://lwn.net/Articles/785263/)

首先 sysctl 是配置内核运行时参数的一种机制。sysctl knobs 是什么东西，从文章中的描述，其就是 `/proc/sys` 文件下的各个子文件。那么结合起来看 sysctl knob 其是就是不同方面的可配置参数。

```plain
guanshun@guanshun-ubuntu /p/sys> ls
abi/  debug/  dev/  fs/  kernel/  net/  user/  vm/
```

这个功能平时没有用到过。

OK，但是如果想控制 sysctl knob 访问权限，目前没有相应的解决方案，要不就是限制对整个 /proc 的访问，无法做到细粒度的控制。使用 BPF 可以做到这点。BPF 增加了一个新的程序类型 `BPF_PROG_TYPE_CGROUP_SYSCTL` 和一个新的操作新的 bpf 系统调用操作 `BPF_CGROUP_SYSCTL`。也就是说 BPF 可以像 cgroup 管理硬件资源那样控制用户对 sysctl knob 的访问。关于 cgroup 可以看 [Namespaces](https://github.com/UtopianFuture/UtopianFuture.github.io/blob/master/kernel/Namespaces.md) 和 [Control group](https://github.com/UtopianFuture/UtopianFuture.github.io/blob/master/kernel/Control-groups.md) 两篇文章。

我的理解是，将 BPF 程序 attach 到对应的 sysctl knob 上后，当用户要访问该 sysctl knob 就是触发该 BPF 程序，根据系统调用传来的数据，“过滤”掉没有权限的访问。

### [BPF: what's good, what's coming, and what's needed](https://lwn.net/Articles/787856/)

### [A formal kernel memory-ordering model (part 1)](https://lwn.net/Articles/718628/)

这个系列的文章我开始以为是介绍 linux 内存模型的，但其是因为内存模型过于复杂，提出一个自动态工具 - [herd](https://github.com/herd/herdtools7/) 来分析代码是否符合特定架构的内存模型。这篇文章是介绍工具开发的原则，在没有了解工具的使用前这篇文章看不懂。当然，如果想了解 linux [强](https://mirrors.edge.kernel.org/pub/linux/kernel/people/paulmck/LWNLinuxMM/StrongModel.html)/[弱](https://mirrors.edge.kernel.org/pub/linux/kernel/people/paulmck/LWNLinuxMM/WeakModel.html)内存模型，文章也给出了传送门。之后可以分析。

这个系列的文章不适合 lwn_note 的主题，太复杂了，遂另起[一篇](./Memory-Order.md)分析。

### [ARM's multiply-mapped memory mess](https://lwn.net/Articles/409689/)

内存可以分为 "normal memory" 和 "device memory"，两者具有不同的性质。MMU 也会根据不同的内存采用不同的缓存机制，如 ARM 将 ARM 映射为 "normal memory"，并采用 "writeback caching" 机制，而 "device memory" 是 "uncached" 的。

`ioremap` 系统调用是用来映射 I/O 内存给 CPU 使用的，这些内存映射为 "device memory"，但是当使用 `ioremap` 创建一个 RAM 的映射时，事情变得棘手起来。问题在于 RAM 的多重映射导致其具有多重内存属性（？）。在 ARM 架构第 6 个版本中这种行为被定义成 "unpredictable"，可能会导致数据泄露。但是它又允许这样的映射（？），这让很多驱动开发人员通过 `ioremap` 重映射 RAM。

**这里有个不理解的点，为什么 RAM 不能多重映射？然后其他架构是什么情况？**

之后就是内核维护者和驱动开发人员的扯皮，内核开发者为了安全直接让 `ioremap` 无法映射 RAM，很多驱动没法跑了。

最后的解决方案是在内核启动时保留一部分 RAM 内存留作它用，驱动就可以从这块内存池中申请内存。

内核开发的通常原则是任何人修改了 API 也要负责搞定该行为带来的混乱。

### [ARM wrestling](https://lwn.net/Articles/437162/)

ARM 架构的内核代码存在一个问题，每一代芯片和上一代相差太大，每个 SOC 开发商提供和其他开发者不兼容的代码，这导致 linus 在 merge 的时候发现很多重复的、互相冲突的、架构相关的（应该是 ARM 目录下，这些代码应该放在 ARM/boot 目录下）代码。

问题的根本在于 ARM 架构没有一个“预包装”窗口，它支持嵌入式设备，很多开发者能够直接修改源码并上传到社区（这也是 ARM 如此受欢迎的原因，开发者自由度很高），因此 ARM 架构下有大量的子平台，每个子平台是相互独立的，开发者也没有兴趣将每个子平台架构无关的部分独立出来，单独维护。然后很多子平台并不是长期维护的（项目终止、公司倒闭），所有产生了很多垃圾代码。

毫无疑问，想解决这个问题，ARM 需要更多的 maintainer，将架构无关部分分离出来，维护整个 ARM tree 的整洁。

> As [Thomas Gleixner said](https://lwn.net/Articles/437183/):
>
> The only problem is to find a person, who is willing to do that, has enough experience, broad shoulders and a strong accepted voice. Not to talk about finding someone who is willing to pay a large enough compensation for pain and suffering.

看下面的评论，大家相互扯皮也很有意思。

### [Rationalizing the ARM tree](https://lwn.net/Articles/439314/)

好家伙，说干就干。问题不仅仅是在 ARM 社区内部讨论，linus 提出了意见，很多 maintainer 在将架构无关的代码分离出来。大家的执行力真的高。

### [ARM kernel consolidation](https://lwn.net/Articles/443510/)

这篇文章介绍了 ARM 内核代码的改进，主要分为如下几个方面：

- Code organization

  这部分由于缺乏对 arm 内核的了解，无法理解；

- Git tree

看了 2.6.0 版本的内核，mach 目录下的代码很简单啊，和最新的内核代码结构上没有太大差别，那么文章中说的代码冗余体现在哪里？

存在两个问题：

- 对 arch/arm 内核代码结构不熟悉；
- 对 arm 架构没有整体的认识。这个方面可以先从看看 v8 的指令集手册。

要学的太多了。

### [Linux support for ARM big.LITTLE](https://lwn.net/Articles/481055/)

这篇文章介绍内核如何支持 ARM 的大小核。所谓大小核是指设计两组核，小核每赫兹性能低，面积小，跑在低频；大核每赫兹性能高，面积大，跑在高频。运行简单任务，大核关闭，小核在低频，动态和静态功耗都低，而大核用高频运行复杂任务。小核在低功耗场景下，通常只需要大核一半的面积和五分之一的功耗。这和不区分大小核，单纯调节电压频率比，有显著优势。

我们以一个簇 A 中有 4 个 Cortex-A15（大核），一个簇 B 中有 4 个 Cortex-A7（小核）为例，Linux 原生的是适用于 SMP 系统的调度算法，但是大小核不是 SMP 系统。设计新的调度算法需要解决如下问题：

- 让 A15 处理交互任务，和 A7 处理后台任务是否合理？
- 如果交互任务足够轻量，能够让小核一直执行呢？
- 如果用户在等待后台任务执行呢？
- 当小核在以 100% 的性能运行任务时，如何决定该任务是否要迁移到大核中执行呢？
- 调度器是自适应的还是用户空间的原则也能影响它（这里应该可以理解为用户使用手机的状态）？
- 如果是后者，怎样设计用户接口才能足够高效和面向未来？

考虑到修改 scheduler 的复杂性，在新的 scheduler 完成前，需要一个临时解决方案。ARM 提供的原型软件解决方案是用一个小的 hypervisor 使用 A15 和 A7 的硬件虚拟化扩展让底层 OS 认为只有一个 A15 簇。这样 OS 就不需要改。hypervisor 能够自动暂行整个系统的运行，将一个簇的 CPU 状态迁移到另一个簇的 CPU 中。

还是以上面的例子为说明。当切换开始时，簇 A 中的 4 个 CPU 状态会迁移到对应的簇 B 中的 CPU 中，中断也会重新路由到簇 B 中的 CPU。当簇 A 暂停执行时，簇 B 开始执行，然后簇 A 断电。上层 OS 看到的只有性能和能耗区别。

但是这个方法毕竟是临时性的，有其局限性。

- hypervisor 带来了软件上的复杂性；
- 簇 A 与簇 B 间的 cache 一致性问题（所有的 cache 内容也要迁移么）；
- CPU 间切换的负载；

貌似新的解决方案还没有完成。但是完成之后会具有如下优势;

- 能够以单个 CPU 为单位进行调度，而不是整个簇；
- 甚至只迁移不同的寄存器；
- 根据 CPU 运行频率决定是否迁移；

### [Supporting multi-platform ARM kernels](https://lwn.net/Articles/496400/)

ARM 架构最大的优势就是多样化，制造商能够围绕 ARM 内核创造各种各样的 SOC。但是这种多样性加上普遍缺乏硬件可发现性（？），使得内核很难支持 ARM 架构。就目前的情况而言，必须为特定的 ARM 系统构建特定的内核。而其他大多数架构只需要编译一个可执行文件就能够在大部分系统上运行。

目前在 arch/arm 目录下，有上百个板级文件（2012 年），还有很多没有贡献到上游社区的。这些代码充斥在/arch/arm/plat-xxx 和/arch/arm/mach-xxx 目录，对内核而言这些 platform 设备、resource、i2c_board_info、spi_board_info 以及各种硬件的 platform_data 绝大多数属于冗余代码。为此，开发者使用了其他体系架构已经使用的 Flattened Device Tree。

“A data structure by which bootloaders pass hardware layout to Linux in a device-independent manner, simplifying hardware probing.”开源文档中对设备树的描述是，一种描述硬件资源的数据结构，它通过 bootloader 将硬件资源传给内核，使得内核和硬件资源描述相对独立(也就是说*.dtb 文件由 Bootloader 读入内存，之后由内核来解析)。

Device Tree 可以描述的信息包括 CPU 的数量和类别、内存基地址和大小、总线和桥、外设连接、中断控制器和中断使用情况、GPIO 控制器和 GPIO 使用情况、Clock 控制器和 Clock 使用情况。

设备树的主要优势：对于同一 SOC 的不同主板，只需更换设备树文件.dtb 即可实现不同主板的无差异支持，而无需更换内核文件。

### [A big.LITTLE scheduler update](https://lwn.net/Articles/501501/)

好家伙，动作真快。这篇文章除了给出两种可行的支持大小核调度的解决方案，还介绍了开发者在支持 big.LITTLE 做的工作。

第一种方案是将每个大小核配对，用户程序在任意时刻只能看到一个 CPU（大核或小核），由调度器在大小核之间透明的迁移。这种方法称为"big.LITTLE switcher"。

第二种方法是将所有的大小核放在一起调度（我觉得这种方法好一点，简单直观，便于理解）。

### [ELC: In-kernel switcher for big.LITTLE](https://lwn.net/Articles/539840/)

这是上述第一种方法的详细说明。

#### 实验设计

开发者使用 3 个 Cortex-A7 作为一个簇，2 个 Cortex-A15 作为一个簇。这是为了模拟最坏情况故意设计的，这样可以发现很多潜在的问题，一般而言，都会使用相同数量的大小核。然后将一个 A7 和一个 A15 配对成一个虚拟的 CPU。

两个簇通过支持一致性协议的总线连接，使用能够将中断路由到任意 CPU 的中断控制器。其他 SOC 配置就不再说明了。

#### 实验过程

虚拟 CPU 有一个 CPU 运行频率表，包含 A7 和 A15 能够运行的频率范围。决定是否进行切换的逻辑很简单，如果当前运行的 CPU 无法提供需要的合适的运行频率，就会切换到另一个 CPU。

比如，虚拟 CPU0 运行以 200MHZ 的频率运行在 A7 上，但是来了一个任务需要提高 1.2GHZ，driver（这应该是算法主体）时别到 A7 无法提供如此高的频率，那么它就会决定关掉 A7(outbound processor)，打开 A15(inbound processor)。打开 A15 后 A7 还是会继续执行，直到受到 A15 的 "inbound alive" 信号，A15 发送信号后，需要执行初始化工作并且完成 cache 一致性。然后 A15 等待 A7 的信号。

A7 收到 "inbound alive" 信号后，进入断电阶段，包括关中断，迁移中断信号到 A15，保存上下文。完成后发信号给 A15，A15 恢复上下文，开启中断，从 A7 中断的地方开始执行。这就完成了整个迁移任务（原理很简单，但调试的时候应该会很麻烦。就我的技术水平，很容易忘了关中断，迁移中断等任务）。

#### 实验结果

性能不错。以两个 A15 60% 的性能达到其 90% 的性能。

#### 总结

优势：

- 性能达标；
- 代码简单，能够产品化；

我之前说过，直观而言我觉得 HMP 更有希望，但这个人一番讲解下来，我觉得这种方法挺好啊。其是否有不足？

文章中提到一点很有意思，两种解决方案，IKS 和 HMP 在相同的内核树下同步进行。不仅如此，还可以在内核命令行上启用或者再运行时通过 sysfs 切换两种方式。

### [LC-Asia: A big LITTLE MP update](https://lwn.net/Articles/541005/)

OK，LKS 的缺点来了，同一时间只能有一半的 CPU 在工作。

文章重点在与介绍这项工作的难度，该工作目前（2013）还没有完成，所以不能像上文那样分析。不过有[代码](https://git.linaro.org/arm/big.LITTLE/mp.git/log/)可以分析，需要花时间看看。

最后 HMP 调度器被用于Android 5.x 和 Android6.x 中，但并没有被合入内核 mainline 中。详细的介绍可以看[这里](https://android.googlesource.com/kernel/msm/+/android-msm-bullhead-3.10-marshmallow-dr/Documentation/scheduler/sched-hmp.txt)。
