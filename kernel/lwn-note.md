## LWN Note

长期以来我都沉浸在自己的世界，一个人学内核，主要是考虑到基础很薄弱，与其花很多时间去看内核方面的新闻、文章，不如先把内核基础的内存管理、进程调度、中断控制、并发同步和文件系统这些子模块搞清楚，以对整个内核有一定的了解，这样去看别人讨论或和别人讨论才能有底气，现在基础知识掌握的还行，该看看 LWN 和别人的博客，学些更有意思的东西。另一方面是最近和朋友聊天，我想到我们做技术的，不能总在国内卷，要出去和外国人竞争，所以也需要加强这方面的能力。

### [Why printk() is so complicated (and how to fix it)](https://lwn.net/Articles/800946/)

首先 printk 不像我们我们想象的那么简单，它有很长的发展历史，同时因为它在内核中无处不在，它需要考虑不同的场景，例如它会工作在 atomic context、NMIs 中等等，这就是它为什么这么复杂。可以将矛盾点分为两个：

- 非交互性：对于不重要的信息，我们可以将 printk 设计成完成可抢占的，这样对系统性能影响最小；
- 可靠性：对于重要的信息，例如 kernel panic，developer 需要快速，全面的了解原因，这类就涉及成 "atomic consoles"；

然后 printk 的实现似乎是涉及到 ring buffer，我以为就是简单从 serial 输出。其实不对，messages 都从 console 输出，但是其可能加载到 serial, graphic adapters or nexwork connections。

或许应该去看看这个 ring buffer 是什么东西。

### [Why RISC-V doesn't (yet) support KVM](https://lwn.net/Articles/856685/)

总的来说，RISC-V 的 KVM 支持其实已经完成了，并且在 hardware 上工作良好，但是 RISC-V 分支的代码合并策略（patch acceptance policy）有些问题，总在变化，导致 RISC-V KVM 一直处于 staging 状态，而没有 frozen，不能合并和 mainline 中。

### [VMX virtualization runs afoul of split-lock detection](https://lwn.net/Articles/816918/)

这篇文章提到 split-lock（分体锁）的使用会影响到 VMX 的正确性。首先分体锁的使用是有一定意义的，它能够避开潜在的 denial-of-service attack vector（不清楚是啥），同时也能摆脱延迟密集型负载的性能问题。但是使用分体锁会导致 guest 需要处理对其检查异常。

这里涉及到到的分体锁之后去了解，同时对 x86 的非对其访问也需要看看。

### [Detecting and handling split locks](https://lwn.net/Articles/790464/)

### MISC

- Spectre: it is a subset of security [vulnerabilities](https://en.wikipedia.org/wiki/Vulnerability_(computing)) within the class of vulnerabilities known as microarchitectural timing [side-channel attacks](https://en.wikipedia.org/wiki/Side-channel_attacks). 一个安全漏洞的 patch
- Zen 4: is the [codename](https://en.wikipedia.org/wiki/Codename) for a [CPU](https://en.wikipedia.org/wiki/CPU) [microarchitecture](https://en.wikipedia.org/wiki/Microarchitecture) by [AMD](https://en.wikipedia.org/wiki/AMD), released on September 27, 2022. It is the successor to [Zen 3](https://en.wikipedia.org/wiki/Zen_3) and uses [TSMC](https://en.wikipedia.org/wiki/TSMC)'s [5 nm](https://en.wikipedia.org/wiki/5_nm_process) process. Zen 4 powers [Ryzen 7000](https://en.wikipedia.org/wiki/Ryzen_7000) mainstream desktop processors (codenamed "Raphael") and will be used in high-end mobile processors (codenamed "Dragon Range"), thin & light mobile processors (codenamed "Phoenix"), as well as [Epyc](https://en.wikipedia.org/wiki/Epyc) 7004 server processors (codenamed "Genoa" and "Bergamo"). CPU 微架构
