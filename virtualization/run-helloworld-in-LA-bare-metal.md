## Run "Hello World" in LA bare metal

由于 bmbt 是在裸机上直接运行的，那么必须搞清楚程序是怎样在裸机上运行的。按照惯例，从最简单的"Hello World"入手，即在 LA 裸机上运行“Hello World"。当然这里的裸机指的是支持 LA 的 qemu。

### 1. 显示原理

在没有 OS 支持下，程序必须自己操作硬件来完成输出。在现代计算机体系结构下，CPU 与硬件（尤其是块设备）的交互方式往往是内存地址映射，即程序通过对某个内存地址的读写来完成对硬件的读写。比如在 x86 下，CPU 与显示器的交互过程中，显卡被映射到了内存的`0xb8000`（注意是 20 位）处开始的 16KB。往`0xb8000`这段内存写入内容，就能立即显示在屏幕上。**那么在 LA 中显卡被映射到哪个内存地址？**

在显示器的文本模式下，每个字符都占用 2 个字节，第一个字节是 ASCII 码，第二个字节是字符的颜色，其中高 4 位表示背景颜色，低 4 位表示字体颜色。下图表示颜色值。

| 背景颜色 | 字体颜色   |
| :------- | :--------- |
| 0=黑色   | 8=灰色     |
| 1=蓝色   | 9=淡蓝色   |
| 2=绿色   | A=淡绿色   |
| 3=浅绿色 | B=淡浅绿色 |
| 4=红色   | C=淡红色   |
| 5=紫色   | D=淡紫色   |
| 6=黄色   | E=淡黄色   |
| 7=白色   | F=亮白色   |

### 2. 体系结构相关

搞清楚程序怎样才能在裸机上直接运行，我们先看看 bios 是怎样将内核拉起来的。

#### 2.1. BIOS

bios 完成的主要任务如下：

（1）上电自检（Power On Self Test, POST）指的是对硬件，如 CPU、主板、DRAM 等设备进行检测。POST 之后会有 Beep 声表示 POST 检查结果。一声简短的 beep 声意味着系统是 OK 的，两声 beep 则表示检查失败，错误码会显示在屏幕上；

（2）POST 之后初始化与启动相关的硬件，如磁盘、键盘控制器等；

（3）位 OS 创建一些参数，如 ACPI 表；

（4）选择引导设备，从设备中加载 bootloader。

bios 在完成对硬件进行检测，为 OS 准备相关参数之后，按照 BIOS 中设定的启动次序（Boot Sequence)逐一查找可启动设备，找到可启动的设备后，会把该设备第一个扇区（512 字节）—— MBR，的内容复制到内存的`0x7c00`处，然后检查内存的`0x7dfe`处（也就是从`0x7c00`开始的第 510 字节）开始的两个字节（510，511 字节，即 magic number）组成的数字是否是`0xaa55`。

```plain
                    512 bytes
     +-----------------------+---------+--+
     |                       | parti-  |  |
     |    boot loader        | tion    |  | --> magic number(2 bytes)
     |                       | table   |  |
     +-----------------------+---------+--+
            446 bytes       /           \
                           /  64 bytes   \
                          /               \
                          p1   p2   p3   p4(为啥有4个分区)
```

如果是，那么 bios 认为前 510 字节是一段可执行文件，于是 jump 到`0x7c00`处开始执行这段程序，即 MBR 开始执行；若不等于则转去尝试其他启动设备，如果没有启动设备满足要求则显示"NO ROM BASIC"然后死机。

```plain
qemu-img create -f qcow2 hello.img 10M
qemu-system-x86_64 hello.img
```

![image-20211018162600419](https://github.com/UtopianFuture/UtopianFuture.github.io/blob/master/image/run.1.png?raw=true)

但 MBR 容量太小，放不下完整的 boot loader 代码，所以现在的 MBR 中的功能也从直接启动操作系统变为了启动 boot loader，一般是 grub。

那么问题来了：

（1）LA 中这个过程是怎么样的？即 bios 是否也是把磁盘的第一个扇区的内容复制到内存的`0x7c00`处，然后检查 510，511 字节。手册中没有说明。

（2）LA 中有实模式的概念么？有段寄存器么？

### 3. Hello World for x86

#### 3.1 源码及命令

```c
asm(".long 0x1badb002, 0, (-(0x1badb002 + 0))");

unsigned char *videobuf = (unsigned char*)0xb8000;
const char *str = "Hello World!!";

int start_entry(void)
{
	int i;
	for (i = 0; str[i]; i++) {
		videobuf[i * 2 + 0] = str[i];
		videobuf[i * 2 + 1] = 0x0F;
	}
	for (; i < 80; i++) {
		videobuf[i * 2 + 0] = ' ';
		videobuf[i * 2 + 1] = 0x0f;
	}
	while (1) { }
	return 0;
}
```

```plain
gcc -c -ffreestanding -nostdlib -m32 hello.c -o hello.o
ld -e start_entry -m elf_i386 -Ttext-seg=0x100000 hello.o -o hello.elf
qemu-system-x86_64 -kernel hello.elf
```

命令解析：

（1）`-ffreestanding`

```plain
-ffreestanding
	Assert that compilation targets a freestanding environment.  This implies -fno-builtin.  A freestanding environment is one in which the standard library may not exist, and program startup may not necessarily be at "main".  The most obvious example is an OS kernel.  This is equivalent to -fno-hosted.
```

（2）`-nostdlib`

```plain
-nostdlib
	Do not use the standard system startup files or libraries when linking.  No startup files and only the libraries you specify are passed to the linker, and options specifying linkage of the system libraries, such as -static-libgcc or -shared-libgcc, are ignored.
	The compiler may generate calls to "memcmp", "memset", "memcpy" and "memmove".  These entries are usually resolved by entries in libc.  These entry points should be supplied through some other mechanism when this option is specified.
	One of the standard libraries bypassed by -nostdlib and -nodefaultlibs is libgcc.a, a library of internal subroutines which GCC uses to overcome shortcomings of particular machines, or special needs for some languages.
	In most cases, you need libgcc.a even when you want to avoid other standard libraries.  In other words, when you specify -nostdlib or -nodefaultlibs you should usually specify -lgcc as well.
       This ensures that you have no unresolved references to internal GCC library subroutines.  (An example of such an internal subroutine is "__main", used to ensure C++ constructors are called.)
```

（3）`-e start_entry`

指定程序的入口地址。

（4）`-m elf_i386`

指定链接后的输出格式为 32 位的 elf。还可以输出成 binary 格式，就是没有任何额外信息，你代码里面写了什么就是什么。一般 MBR 就是用的 binary 格式。

但是这里我们需要 elf 格式，因为 elf 格式除了代码和数据外还有很多有用的信息，**可以被标准 boot loader 识别**， grub 可以直接加载，qemu 也可以直接运行。所以之后的 bmbt 也时要编译成 elf 格式，直接用现成 boot loader 识别，不用重写 boot loader。关于 elf 文件更多的信息在这里。

（5）是不是所有 elf 文件都可以被 boot loader 加载？

不是，需要第一个段中（没有 ld 文件的话，第一个段默认就是 text）头部包含 `multiboot header`，即：

```text
asm(".long 0x1badb002, 0, (-(0x1badb002 + 0))");
```

一共定义了十二个字节的 multiboot header，第一个 long 是 magic code, grub/qemu 等需要检查，第二个 long 代表你需要 grub 提供哪些信息（比如内存布局，elf 结构），这里填写 0，不需要它提供任何信息。第三个 long 是代表前两个运算以后的一个 checksum，grub/qemu 会检查这个值确认你真的是一个可以引导的 kernel。

#### 3.2. 结果

![image-20211018194315703](https://github.com/UtopianFuture/UtopianFuture.github.io/blob/master/image/run.2.png?raw=true)

### 4. Hello World for LA

#### 4.1. 源码及调试

```c
asm(".long 0x1badb002, 0, (-(0x1badb002 + 0))");

unsigned char *videobuf = (unsigned char*)0x800000001fe001e0;
const char *str = "Hello World!!";

int start_entry(void)
{
	int i;
    for (int i = 0; str[i]; i++) {
        videobuf[0] = str[i];
        videobuf[1] = 0x0f;
	}
	while (1) { }
    // asm("break");
	return 0;
}
```

```plain
qemu-system-loongarch64 -nographic -m 2G -cpu Loongson-3A5000 -serial mon:stdio -bios ~/research/bmbt/qemu-la/pc-bios/loongarch_bios.bin -M loongson7a,kernel_irqchip=off -kernel la_hello.elf // 该命令用于直接运行
```

```plain
qemu-system-loongarch64 -nographic -m 2G -cpu Loongson-3A5000 -serial mon:stdio -bios ~/research/bmbt/qemu-la/pc-bios/loongarch_bios.bin -M loongson7a,kernel_irqchip=off -kernel la_hello.elf -s -S // 该命令用于调试，配合LA的gdb
```

```plain
guanshun@Jack-ubuntu ~/r/b/test> loongarch64-linux-gnu-gdb la_hello.elf
GNU gdb (GDB) 8.1.50.20190122-git
Copyright (C) 2018 Free Software Foundation, Inc.
License GPLv3+: GNU GPL version 3 or later <http://gnu.org/licenses/gpl.html>
This is free software: you are free to change and redistribute it.
There is NO WARRANTY, to the extent permitted by law.
Type "show copying" and "show warranty" for details.
This GDB was configured as "--host=x86_64-pc-linux-gnu --target=loongarch64-linux-gnu".
Type "show configuration" for configuration details.
For bug reporting instructions, please see:
<http://www.gnu.org/software/gdb/bugs/>.
Find the GDB manual and other documentation resources online at:
    <http://www.gnu.org/software/gdb/documentation/>.

For help, type "help".
Type "apropos word" to search for commands related to "word"...
Reading symbols from la_hello.elf...(no debugging symbols found)...done.
(gdb) b start_entry
Breakpoint 1 at 0x80000000000000fc
(gdb) target remote localhost:1234
Remote debugging using localhost:1234
0x000000001c000000 in ?? ()
(gdb) c
Continuing.

Breakpoint 1, 0x80000000000000fc in start_entry ()
(gdb) layout asm

```

这里有几点需要注意：

（1）LA 中只有两个窗口地址在裸机上能够使用

```plain
cache地址：9000000000000000
I/O地址：8000000000000000
```

（2）LA 的串口地址是`0x1fe001e0`，因此输出字符的地址为`0x800000001fe001e0`。

（3）LA 不需要像 x86 一样，一个地址对应一个字符

```c
for (i = 0; str[i]; i++) {
	videobuf[i * 2 + 0] = str[i];
	videobuf[i * 2 + 1] = 0x0F;
}
```

而是直接对串口进行输出

```c
for (int i = 0; str[i]; i++) {
    videobuf[0] = str[i];
    videobuf[1] = 0x0f;
}
```

#### 4.2. 结果

![image-20211020151308828](https://github.com/UtopianFuture/UtopianFuture.github.io/blob/master/image/run.3.png?raw=true)

### 5. 总结

总的来说，在裸机上运行程序就是要编译一个不依赖任何标准库的程序，而且这个程序还要是 boot 或 boot loader 可以识别的格式（如 elf），然后在编译的时候手动指定程序的入口地址，如内核在编译时就指定了入口地址是`kernel_entry`，和程序加载地址，即程序放在内存哪个地方执行。这样 boot 在硬件自检完后就会跳转到程序入口执行。

### 6. 问题

1. 在 LA 中显卡被映射到哪个内存地址？
2. LA 中 bios 是否也是把磁盘的第一个扇区的内容复制到内存的`0x7c00`处，然后检查 510，511字节。

reference

[1] https://www.zhihu.com/question/49580321 这个问题下面的回答都很有帮助。

[2] https://zhou-yuxin.github.io/articles/2015/x86%E8%A3%B8%E6%9C%BAHelloWorld/index.html 裸机 helloworld 的另一个写法。

[3] https://nancyyihao.github.io/2020/04/10/Linux-Boot-Process/ bios 启动写的很详细。
