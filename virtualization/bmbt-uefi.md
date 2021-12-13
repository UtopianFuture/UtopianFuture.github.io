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
