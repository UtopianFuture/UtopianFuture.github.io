## EDK2 build Specification

因为在 edk2 中编译 BMBT 遇到了很多问题，所以想把 edk2 的 build 过程搞懂。

build 主要是 Conf 目录下的 3 个文件，build_rule.txt, target.txt, tools_def.txt。其中 tools_def.txt 是指定 gcc 编译指令的。

```plain
DEFINE GCC_ALL_CC_FLAGS            = -g -Os -fshort-wchar -fno-builtin -fno-strict-aliasing -Wall -Werror -Wno-array-bounds -include AutoGen.h -fno-common
```

在编译 BMBT 时，会遇到这个问题，从而导致不能通过编译，

```plain
/home/guanshun/gitlab/edk2/BmbtPkg/src/hw/core/cpu.c:41:13: warning: ‘cpu_common_get_paging_enabled’ defined but not used [-Wunused-function]
```

所以我将 -Werror 关掉了。

### Reference

[1] [edk2 build specification](https://edk2-docs.gitbook.io/edk-ii-build-specification/5_meta-data_file_specifications/53_target_txt_file) 官方文档，很全，但是找到需要的信息需要耐心。
