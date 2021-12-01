## UEFI

The Unified EFI (UEFI) Specification (previously known as the EFI Specification) defines an interface between an operating system and platform firmware. 所以 [UEFI 和 EFI](https://www.quora.com/unanswered/Whats-the-difference-between-UEFI-and-EFI) 其实是一个东西，都是定义固件和操作系统之间的接口。UEFI 只是一个协议，没有提供实现，具体的实现有 INTEL 开源的 EDK2 等。

### 编译运行 EFI 程序

首先是 hello.c 源码

```c
#include <efi.h>
#include <efilib.h>

efi_status
efiapi
efi_main(efi_handle imagehandle, efi_system_table *systemtable) {
  initializelib(imagehandle, systemtable);
  print(l"hello, world!\n");

  return efi_success;
}
```

注意这里使用了  `#include <efi.h>` ，需要 `sudo apt install gnu-efi` ，然后用下面的 Makefile 脚本进行编译，没有问题的话会生成 hello.efi 文件。

```makefile
ARCH            = $(shell uname -m | sed s,i[3456789]86,ia32,)

$(info $(ARCH))
OBJS            = hello.o
TARGET          = hello.efi

EFIINC          = /usr/include/efi
EFIINCS         = -I$(EFIINC) -I$(EFIINC)/$(ARCH) -I$(EFIINC)/protocol
LIB             = /usr/lib
EFILIB          = /usr/lib
EFI_CRT_OBJS    = $(EFILIB)/crt0-efi-$(ARCH).o
EFI_LDS         = $(EFILIB)/elf_$(ARCH)_efi.lds

CFLAGS          = $(EFIINCS) -fno-stack-protector -fpic \
		  -fshort-wchar -mno-red-zone -Wall
ifeq ($(ARCH),x86_64)
  CFLAGS += -DEFI_FUNCTION_WRAPPER
endif

LDFLAGS         = -nostdlib -znocombreloc -T $(EFI_LDS) -shared \
		  -Bsymbolic -L $(EFILIB) -L $(LIB) $(EFI_CRT_OBJS)

all: $(TARGET)

hello.so: $(OBJS)
	ld $(LDFLAGS) $(OBJS) -o $@ -lefi -lgnuefi

%.efi: %.so
	objcopy -j .text -j .sdata -j .data -j .dynamic \
		-j .dynsym  -j .rel -j .rela -j .reloc \
		--target=efi-app-$(ARCH) $^ $@
```

最后用 `uefi.sh` 脚本运行 `./uefi.sh hello.efi`。这里 OVMF 是 ubuntu 下的 UEFI shell ，可以直接下载。

```sh
#!/bin/bash

WORK_DIR=/home/guanshun/research/bmbt/uefi
DISK_IMG=${WORK_DIR}/uefi.img
PART_IMG=${WORK_DIR}/part.img
if [[ ! -f /usr/share/OVMF/OVMF_CODE.fd ]]; then
	echo "ovmf not found"
	echo "sudo apt install ovmf"
fi

if [[ $# -ne 1 ]]; then
	echo "need extractly one "
	exit 1
fi
EFI="$1"

if [[ ! -f ${EFI} ]]; then
	echo "${EFI} not found"
	exit 1
fi

dd if=/dev/zero of=${DISK_IMG} bs=512 count=93750
parted ${DISK_IMG} -s -a minimal mklabel gpt
parted ${DISK_IMG} -s -a minimal mkpart EFI FAT16 2048s 93716s
parted ${DISK_IMG} -s -a minimal toggle 1 boot

dd if=/dev/zero of=${PART_IMG} bs=512 count=91669
mformat -i ${PART_IMG} -h 32 -t 32 -n 64 -c 1

mcopy -i ${PART_IMG} "${EFI}" ::

dd if=${PART_IMG} of=${DISK_IMG} bs=512 count=91669 seek=2048 conv=notrunc

# ref: https://blog.hartwork.org/posts/get-qemu-to-boot-efi/
qemu-system-x86_64 \
	-enable-kvm -cpu host -m 2G \
	-bios /usr/share/OVMF/OVMF_CODE.fd \
	-drive file=${DISK_IMG},format=raw -net none
```

没有问题的话会在 QEMU 中运行 UEFI shell ，输入 `FS0:` （后来发现不输入也行）

![image](https://user-images.githubusercontent.com/66514719/144043552-1a64bfe3-c348-4dba-a340-e21ecb4e953e.png)

UEFI sh 不区分大小写。

### 编译 EDK2

```plain
git clone https://github.com/tianocore/edk2.git
cd edk2
git submodule update --init // 下载子模块，不然会出现找不到头文件的问题
make -C BaseTools
. edksetup.sh
```

没有问题的话 Conf 子目录中会多出几个文件，修改 target.txt 文件

```plain
ACTIVE_PLATFORM       = MdeModulePkg/MdeModulePkg.dsc
TARGET_ARCH           = X64 # 注意是 TARGET_ARCH 不是 TARGET
TOOL_CHAIN_TAG        = GCC5 # 默认的应该是 VS
MAX_CONCURRENT_THREAD_NUMBER = 6 #CPU 核心数量
```

然后 `build` ，没有问题的话 `X64`目录下会编译出很多 efi 文件，这是 EDK2 自带的 DEMO

```plain
/home/guanshun/gitlab/edk2/Build/MdeModule/DEBUG_GCC5/X64/HelloWorld.efi
```

这些 efi 文件都可以用上面的 `uefi.sh` 运行。

### SimpelPkg

现在对 UEFI 的理解是每个程序都以 Pkg 的方式存，其中有几个重要的文件，.dsc, .dec, .inf，这些文件的作用可以看[这里](https://damn99.com/2020-05-18-edk2-first-app/)，现在我们以 Pkg 的方式来构建一个 helloworld ，之后的 bmbt 也是以这种方式来构建。

具体步骤看[这里](https://damn99.com/2020-05-18-edk2-first-app/) ，写好之后编译可能会出现 Pkg 不存在，这个简单，哪个不存在手动安装然后重新编译就好。

我的 demo ，也可以直接下下来放在 edk2 ，用 `build -p SimplePkg/SimplePkg.dsc` 便于然后运行，这里 -p 参数指定要运行的包。

也可以设置 target.txt

```plain
ACTIVE_PLATFORM       = SimplePkg/SimplePkg.dsc
```

这样直接使用就可以了

```plain
build
```

### 编译 main 入口的 helloworld

- 源码：cmain.c

```c
#include <stdio.h>

int main(int argc, char ** argv)
{
  printf("HelloWorld\n");
  return 0;
}
```

- 项目文件：cmain.inf

```plain
[Defines]
  INF_VERSION                    = 0x00010006
  BASE_NAME                      = Cmain // 程序名
  FILE_GUID                      = a912f198-7f0e-4803-b908-b757b806ec93 // 每个程序都不一样
  MODULE_TYPE                    = UEFI_APPLICATION // 程序类型，main 入口程序就用这
  VERSION_STRING                 = 0.1
  ENTRY_POINT                    = ShellCEntryLib // 程序入口

#
#  VALID_ARCHITECTURES           = IA32 X64
#

[Sources]
  cmain.c // 包含的源文件

[Packages]
  MdePkg/MdePkg.dec
  ShellPkg/ShellPkg.dec
  StdLib/StdLib.dec

[LibraryClasses]
  UefiLib
  ShellCEntryLib // 提供 ShellCEntryLib 函数
  LibC // 提供 ShellAppMain 函数
  LibStdio // 提供 printf 函数
```

- 编译：

将 `CPkg/cmain.inf` 加到 `AppPkg/AppPkg.dsc` 的 `[Components]` 中

```shell
build -p AppPkg/AppPkg.dsc -m CPkg/cmain.inf
```

- 运行：

```shell
./uefi.sh /home/guanshun/gitlab/edk2/Build/AppPkg/DEBUG_GCC5/X64/Cmain.efi
```

这里的路径可以用编译的信息中得到。

在使用 main 函数的应用程序工程模块（.inf）中使用了 StdLib ，而 StdLib 提供了 ShellAppMain 函数。这里 main 其实不是程序最开始执行的入口，真正的入口函数是 ShellCEntryLib ，调用过程为 ShellCEntryLib -> ShellAppMain -> main 。

如果程序使用了 printf 等标准 C 库函数，那么一定要使用此种类型的应用程序工程模块，即 .inf 中的 MODULE_TYPE 和 ENTRY_POINT。ShellCEntryLib 会调用 ShellAppMain ，ShellAppMain 函数会对 StdLib 进行初始化。StdLib 初始化完成后才可以调用 StdLib 中的函数。

### 生成 compile_commands.json

具体的步骤可以根据[这个](https://github.com/makaleks/edk2-tools/tree/master/compilation_database_patch)走。

但是有些地方需要修改：

1. 在 `Import Modules` 下添加 `from edk2_compile_commands import update_compile_commands_file`
2. 在 1057 和 1067 行下添加 `update_compile_commands_file(TargetDict, self._AutoGenObject, self.Macros)`

重新 build ，即可生成 compile_commands.json 。

### Reference

[1] 戴正华 UEFI 原理与编程
