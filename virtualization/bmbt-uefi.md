## Port bmbt to UEFI

移植主要就是参照 Lua 和 StdLib 的 .dsc, .dec, .inf 文件。

### 问题与方法

#### 1. 无法引用 include 子目录中的头文件

```c
#include <exec/memory.h>
```

参照 StdLib 的代码，是没有在 .inf 文件中加入对自身的引用。

因此要在 .inf 中添加

```plain
[Packages]
  BmbtPkg/BmbtPkg.dec
```

在 .dec 中添加

```plain
[Includes]
  include
```

#### 2. 无法引用 <sys/mman.h> 头文件

```plain
fatal error: sys/mman.h: No such file or directory
   14 | #include <sys/mman.h> // for MAP_FAILED
```

但是 StdLib 中的文件能够引用，所以应该是哪个库没有正确的包含。

#### 3. error: ‘load32’ defined but not used [-Werror=unused-function]

找到编译命令，将 -werro 注释掉即可，所有的编译命令都在 `BaseTools/Conf/tools_def.template` 中。

#### 4. warning: xxx defined but not used [-Wunused-function]

在编译 BMBT 时，会遇到这个问题，从而导致不能通过编译，

```plain
/home/guanshun/gitlab/edk2/BmbtPkg/src/hw/core/cpu.c:41:13: warning: ‘cpu_common_get_paging_enabled’ defined but not used [-Wunused-function]
```

所以我将 -Werror 关掉了。

#### 5. error: too few arguments to function ‘open’

```
/home/guanshun/gitlab/edk2/BmbtPkg/src/hw/core/loader.c: In function ‘rom_add_file’:
/home/guanshun/gitlab/edk2/BmbtPkg/src/hw/core/loader.c:206:8: error: too few arguments to function ‘open’
  206 |   fd = open(rom->path, O_RDONLY);
```

添加了一个形参

```c
#ifdef UEFI
  fd = open(rom->path, O_RDONLY);
#else
  fd = open(rom->path, O_RDONLY, S_IRUSR); // S_IRUSR,read for owner
#endif
```

#### 6. error: missing binary operator before token "("

```
/home/guanshun/gitlab/edk2/BmbtPkg/src/../include/qemu/compiler.h:54:20: error: missing binary operator before token "("
   54 | #if __has_attribute(flatten) || !defined(__clang__)
```

应该是编译器版本的问题，老版本的 gcc 没有 `__has_attribute` ，按照 QEMU 的源码添加一个兼容函数即可。

```c
#ifndef __has_attribute
#define __has_attribute(x) 0 /* compatibility with older GCC */
#endif
```

#### 7. Error: no such instruction: `la $r12,__bss_start'

src/kernel 目录下的文件没用，开始不知道，将 head.S 加入到 bmbt.inf 中，而 head.S 中是 la 的汇编，所以会出现这个问题，将 src/kernel 所有的文件从 bmbt.inf 中删除即可。
