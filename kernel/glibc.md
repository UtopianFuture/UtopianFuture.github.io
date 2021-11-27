## Glibc 与几种变体

### 1. Glibc

GNU 项(GNU Project)目，所实现的 C 语言标准库(C standard library)。GNU/Linux 类的系统中，都是用的这套 C 语言标准库。它实现了常见的 C 库的函数，支持很多种系统平台，功能很全，但是也相对比较臃肿和庞大。

### 2. uClibc

一个小型的 C 语言标准库，主要用于嵌入式。uClibc 的特点：

（1）uClibc 比 glibc 要小很多。

（2）uClibc 是独立的，为了应用于嵌入式系统中，完全重新实现出来的。和 glibc 源码兼容但二进制不兼容，也就是说从 glibc 换成 uClibc 程序要重新编译。

uClibc 现在还在支持，它和 eglibc 出发点类似。

### 3. EGLIBC(Embedded GLIBC)

glibc 的一种变体，目的在于将 glibc 用于嵌入式系统。EGLIBC 的目标是：

（1）保持源码和二进制级别的兼容于 Glibc 源代码架构和 ABI 层面兼容。如果真正实现了这个目标，那意味着你之前用 glibc 编译的程序，可以直接用 eglibc 替换，而不需要重新编译；

（2）降低(内存)资源占用/消耗；

（3）使更多的模块为可配置的(以实现按需裁剪不需要的模块)；

（4）提高对于交叉编译(cross-compilation)和交叉测试(cross-testing)的支持。

Eglibc 的最主要特点就是可配置，这样对于嵌入式系统中，你所不需要的模块，比如 NIS，locale 等，就可以裁剪掉，不把其编译到库中，使得降低生成的库的大小了。

但是 eglibc 现在不再维护了。

### 4. Musl-libc

### 5. dietlibc

标准 C 库，只支持最重要的和常用的函数。

### 6. klibc

标准 C 库的子集，只支持在系统启动阶段和早期用户空间使用。
