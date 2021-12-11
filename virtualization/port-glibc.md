## Port glibc

### 想法

先把 bmbt 中所有需要用到的 glibc 头文件打印出来，这个可以用 `make pthead=1` 指令，makefile 文件中有定义，可以看看。然后一个个文件移植。

移植不完全按照 glibc 的实现，而是参考 uclibc, musl 等嵌入式 glibc 库，会简单一点。

编译 uclibc



### 遇到的问题

（1）glibc 中的依赖错综复杂，很难把需要的头文件完全移出来。例如在移植 byteswap.h 时，该头文件定义了 8, 16 ,32 64 bits 交换的函数，函数比较简单，可以直接移出来，但问题是 glibc 中其他的头文件也依赖 byteswap.h ，如 stdlib.h ，如果没有移植 stdlib.h ，在编译时就会产生变量重定义的问题，因为 stdlib.h 中依赖的 glibc 中的 byteswap.h ，而不是移植出来的，当然你可能会说把 stdlib.h 也移出来就好了，但问题是非常多的 #include <stdlib.h> ，一下子完全移出来又会导致其他的依赖问题。

（2）开始走了弯路，只是移植了 .h 文件，但是具体的实现没有移植，所以看起来很简单的移植其实很复杂，如 fctnl.h 文件，就用到了其中的 open 函数，用来打开文件的，但是其实现很复杂，移植比较麻烦。

### 进展

UEFI 有实现 StdLib ，现在将 bmbt 移植到 edk2 中，确保能正常的编译，然后在移植到 LA 的 UEFI 实现中。
