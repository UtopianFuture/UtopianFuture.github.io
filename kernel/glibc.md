## Glibc 与几种变体

### 1. Glibc

GNU项(GNU Project)目，所实现的C语言标准库(C standard library)。GNU/Linux类的系统中，都是用的这套C语言标准库。它实现了常见的C库的函数，支持很多种系统平台，功能很全，但是也相对比较臃肿和庞大。

### 2. uClibc

一个小型的C语言标准库，主要用于嵌入式。uClibc的特点：

（1）uClibc比glibc要小很多。

（2）uClibc是独立的，为了应用于嵌入式系统中，完全重新实现出来的。和 glibc 源码兼容但二进制不兼容，也就是说从 glibc 换成 uClibc 程序要重新编译。

uClibc 现在还在支持，它和 eglibc 出发点类似。

### 3. EGLIBC(Embedded GLIBC)

glibc的一种变体，目的在于将glibc用于嵌入式系统。EGLIBC的目标是：

（1）保持源码和二进制级别的兼容于Glibc源代码架构和ABI层面兼容。如果真正实现了这个目标，那意味着你之前用glibc编译的程序，可以直接用eglibc替换，而不需要重新编译；

（2）降低(内存)资源占用/消耗；

（3）使更多的模块为可配置的(以实现按需裁剪不需要的模块)；

（4）提高对于交叉编译(cross-compilation)和交叉测试(cross-testing)的支持。

Eglibc的最主要特点就是可配置，这样对于嵌入式系统中，你所不需要的模块，比如NIS，locale等，就可以裁剪掉，不把其编译到库中，使得降低生成的库的大小了。

但是 eglibc 现在不再维护了。

### 4. Musl-libc

### 5. dietlibc

标准 C 库，只支持最重要的和常用的函数。

### 6. klibc

标准 C 库的子集，只支持在系统启动阶段和早期用户空间使用。
