## Memory Order in Kernel

这个主题的学习起源与这篇文章，[A formal kernel memory-ordering model (part 1)](https://lwn.net/Articles/718628/) 我开始以为其是介绍 linux 内存模型的，但并不是，文章的作者因为内存模型过于复杂，提出一个自动态工具 - [herd](https://github.com/herd/herdtools7/) 来分析代码是否符合特定架构的内存模型。进一步了解后，它们专门从事内存序（Memory Order）的研究。直觉告诉我这个方向很有意思，遂抽些时间学习。

暂时从这[几篇](https://mirrors.edge.kernel.org/pub/linux/kernel/people/paulmck/LWNLinuxMM/)文章入手。

### [A Strong Formal Model of Linux-Kernel Memory Ordering](https://mirrors.edge.kernel.org/pub/linux/kernel/people/paulmck/LWNLinuxMM/StrongModel.html)
