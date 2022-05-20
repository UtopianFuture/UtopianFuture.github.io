# 杂项

### page coloring

系统在为进程分配内存空间时，是按照 virtual memory 分配的，然后 virtual memory 会映射到不同的 physical memory，这样会导致一个问题，相邻的 virtual memory 会映射到不相邻的 physical memory 中，然后在缓存到 cache 中时，相邻的 virtual memory 会映射到同一个 cache line，从而导致局部性降低，性能损失。

Page coloring 则是将 physical memory 分为不同的 color，不同 color 的 physical memory 缓存到不同的 cache line，然后映射到 virtual memory 时，virtual memory 也有不同的 color。这样就不存在相邻的 virtual memory 缓存到 cache 时在同一个 cache line。（很有意思，现在没能完全理解。和组相联映射有什么关系？4k 页和 16k 页和这个又有什么关系？）

![](https://github.com/UtopianFuture/UtopianFuture.github.io/blob/master/image/others.1.png?raw=true)

### 程序优化技术

优化程序性能的基本策略：

（1）高级设计。为遇到的问题选择合适的算法和数据结构。

（2）基本编码原则。避免限制优化的因素，这样就能产生高效的代码。

消除连续的函数调用。在可能时，将计算移到循环外。考虑有选择地妥协程序的模块性以获得更大的效率。

消除不必要的内存引用。引入临时变量来保存中间结果。只有在最后的值计算出来时，才将结果存放到数组或全局变量中。

（3）低级优化。结构化代码以利用硬件功能。

展开循环，降低开销，并且使得进一步的优化成为可能。

通过使用多个累积变量和重新结合等技术，找到方法提高指令级并行。

用功能性的风格重新些条件操作，使得编译采用条件数据传送而不是条件跳转。

最后，由于优化之后的代码结构发生变化，每次优化都需要对代码进行测试，以此保证这个过程没有引入新的错误。

（4）程序剖析工具：

gcc -Og -pg xxx.c -o xxx

./xxx file.txt

​	gprof xxx

### 代理

代理（Proxy）是一种特殊的网络服务，允许一个网络终端（一般为客户端）通过这个服务与另一个网络终端（一般为服务器）进行非直接的连接。一般认为代理服务有利于保障网络终端的隐私或安全，防止攻击。

代理服务器提供代理服务。一个完整的代理请求过程为：客户端首先与代理服务器创建连接，接着根据代理服务器所使用的代理协议，请求对目标服务器创建连接、或者获得目标服务器的指定资源。在后一种情况中，代理服务器可能对目标服务器的资源下载至本地缓存，如果客户端所要获取的资源在代理服务器的缓存之中，则代理服务器并不会向目标服务器发送请求，而是直接传回已缓存的资源。一些代理协议允许代理服务器改变客户端的原始请求、目标服务器的原始响应，以满足代理协议的需要。代理服务器的选项和设置在计算机程序中，通常包括一个“防火墙”，允许用户输入代理地址，它会遮盖他们的网络活动，可以允许绕过互联网过滤实现网络访问。

代理服务器的基本行为就是接收客户端发送的请求后转发给其他服务器。代理不改变请求 URI，并不会直接发送给前方持有资源的目标服务器。

HTTP/HTTPS/SOCKS 代理指的是客户端连接代理服务器的协议，指客户端和代理服务器之间交互的协议。

### Socks

SOCKS 是一种网络传输协议，主要用于客户端与外网服务器之间通讯的中间传递。SOCKS 工作在比 HTTP 代理更低的层次：SOCKS 使用握手协议来通知代理软件其客户端试图进行的 SOCKS 连接，然后尽可能透明地进行操作，而常规代理可能会解释和重写报头（例如，使用另一种底层协议，例如 FTP；然而，HTTP 代理只是将 HTTP 请求转发到所需的 HTTP 服务器）。虽然 HTTP 代理有不同的使用模式，HTTP CONNECT 方法允许转发 TCP 连接；然而，SOCKS 代理还可以转发 UDP 流量（仅 SOCKS5），而 HTTP 代理不能。

### fpu, xmm, mmx 之间的关系和区别

​		(1) x87 fpu 浮点部分，有 8 个浮点寄存器，st(i)，top 指针指向当前使用的寄存器。

​       			        ![](https://github.com/UtopianFuture/UtopianFuture.github.io/blob/master/image/others.2.png?raw=true)

![](https://github.com/UtopianFuture/UtopianFuture.github.io/blob/master/image/others.3.png?raw=true)

(2) xmm 支持 simd 指令的技术。8 个 xmm 寄存器，和 fpu 一样的组织形式。

The MMX state is aliased to the x87 FPU state. No new states or modes have been added to IA-32 architecture to support the MMX technology. The same floating-point instructions that save and restore the x87 FPU state also handle the MMX state.

![](https://github.com/UtopianFuture/UtopianFuture.github.io/blob/master/image/others.6.png?raw=true)

![](https://github.com/UtopianFuture/UtopianFuture.github.io/blob/master/image/others.5.png?raw=true)

(3) sse extensions(The streaming SIMD extensions)

和 mmx 一样，也是 simd 扩展，但是 sse 是 128-bit 扩展。在 32-bit 模式下有 8 个 128-bit 寄存器，也称作 xmm 寄存器，在 64-bit 模式下有 16 个 128-bit 寄存器。

32-bit 的 mxcsr 寄存器为 xmm 寄存器提供控制和状态位。

![](https://github.com/UtopianFuture/UtopianFuture.github.io/blob/master/image/others.6.png?raw=true)

![](https://github.com/UtopianFuture/UtopianFuture.github.io/blob/master/image/others.7.png?raw=true)

### mmap

`mmap`, `munmap` - map or unmap files or devices into memory

```plain
#include <sys/mman.h>

void *mmap(void *addr, size_t length, int prot, int flags,
           int fd, off_t offset);
int munmap(void *addr, size_t length);
```

`mmap()`  creates  a  new  mapping in the virtual address space of the calling process for a device, file and etc, so process can operate the device use va, rather than use read and write.  The starting address for the new mapping is specified in **addr**.  The **length** argument specifies the length of the mapping (which must be greater than 0).

If addr is **NULL**, then the kernel chooses **the (page-aligned) address** at which to create the mapping.  If addr is not NULL, then the kernel takes it as a hint about where to place the mapping;

The  contents of a file mapping are initialized using **length** bytes starting at offset **offset** in the file (or other object) referred to by the file descriptor **fd**.

The **prot** argument describes the desired memory protection of the mapping.

The **flags** argument determines whether updates to the mapping are visible to other processes mapping the same region, and whether updates are carried through to the underlying file.

The `munmap()` system call deletes the mappings for the specified address range, and causes further references to addresses within the range to generate invalid memory references. If the process has modified the memory and has it mapped `MAP_SHARED`, the modifications should first be written to the file.

### `#include <>` and `#include "bar"`

Usually, the `#include <xxx>` makes it look into system folders first, like `/usr/local/include`, `/usr/include`, the `#include "xxx"` makes it look into the current or custom folders first.

在文件首尾的地方添加

```c
#ifndef _XXXXXX_H_
#define _XXXXXX_H_

....

#endif
```

用以确保声明只被 `#include` 一次。可以定义一个头文件，引用项目所有的头文件，然后其他的 .c 文件只需要引用这个头文件就可以。

对于自定义的头文件，如果需要引用，需要给 gcc 加上 `-I` 参数，设置头文件所在的目录。

用 `#include` 引用头文件时，编译器在预处理时会将头文件在引用的地方展开。

### 交叉编译

在 x86 机器上编译 x86 的可执行文件叫本地编译， 即在当前编译平台下，编译出来的程序只能放到当前平台下运行。

交叉编译可以理解为，在当前编译平台下，编译出来的程序能运行在不同体系结构的机器上，但是编译平台本身却不能运行该程序。比如，我们在 x86 平台上，编译能运行在 loongarch 机器的程序，编译得到的可执行文件在 x86 平台上是不能运行的，必须放到 loongarch 机器上才能运行。

### ioctl

```plain
NAME
       ioctl - control device
SYNOPSIS
       #include <sys/ioctl.h>
       int ioctl(int fd, unsigned long request, ...);
DESCRIPTION
       The ioctl() system call manipulates the underlying device parameters of special files.  In particular, many operating characteristics of character special files (e.g., terminals) may be controlled with ioctl() requests.  The argument fd must be an open file descriptor.
       The second argument is a device-dependent request code.  The third argument is an untyped pointer to memory.  It's traditionally char *argp (from the days before void * was valid C), and will be so named for this discussion.
		An  ioctl() request has encoded in it whether the argument is an in parameter or out parameter, and the size of the argument argp in bytes.  Macros and defines used in specifying an ioctl() request are located in the file <sys/ioctl.h>.
```

### C 语言中结构体成员变量前的点的作用

结构体中成员变量前的点： 结构体成员指定初始化

（1）该结构体要先定义；

（2）一个成员变量赋值完后用逗号而不是分号；

（3）初始化语句的元素以固定的顺序出现，和被初始化的数组或结构体中的元素顺序一样，这样可以不用按顺序初始化；

（4）C99 才开始支持的。

### 二进制兼容和源码兼容

二进制兼容：在升级库文件的时候，不必**重新编译**使用此库的可执行文件或其他库文件，并且程序的功能不被破坏。

源码兼容：在升级库文件的时候，不必**修改**使用此库的可执行文件或其他库文件的**源代码**，只需重新编译应用程序，即可使程序的功能不被破坏。

### SMBIOS

SMBIOS is an industry-standard mechanism for low-level system software to export hardware configuration information to higher-level system management software.

### Midware

Middleware is software that provides common services and capabilities to applications outside of what’s offered by the operating system. Data management, application services, messaging, authentication, and API management are all commonly handled by middleware.

Middleware helps developers build applications more efficiently. It acts like the connective tissue between applications, data, and users.

Middleware can support application environments that work smoothly and consistently across a highly distributed platform.

### What Are the Components of a Linux Distribution

- [Linux Kernel](https://fosspost.org/what-are-the-components-of-a-linux-distribution/#Linux_Kernel)

- [GNU Tools](https://fosspost.org/what-are-the-components-of-a-linux-distribution/#GNU_Tools)

- [Display Server or Windows System](https://fosspost.org/what-are-the-components-of-a-linux-distribution/#Display_Server_or_Windows_System)

  A display server is the responsible software about drawing the graphical user interface on the screen. From icons to windows and menus, every graphical thing you see on the screen is done by a display server (known also as windows system).

- [Display Manager](https://fosspost.org/what-are-the-components-of-a-linux-distribution/#Display_Manager)

  DMs are used to show the welcome screen after the boot loader and start desktop sessions as a connection with the X display server.

- [Daemons](https://fosspost.org/what-are-the-components-of-a-linux-distribution/#Daemons)

  Daemons are programs that run in the background of the operating system instead of being normal applications with windows on the user interface. They run some specific jobs and processes that are needed by the operating system, like the network-manager daemon which allows you to connect to the network automatically when you login to your system.

  The most famous daemon is called “[systemd](https://en.wikipedia.org/wiki/Systemd)” which is the main daemon controlling the whole operating system jobs. It is the first process that is executed after loading the Linux kernel. Its job is simply to control other daemons and run them when needed at boot time or at any time you want, it controls all the services available on the operating system and it can turn it on or off or modify it when needed.

  ![Daemons and other software running under the OS, By Shmuel Csaba Otto Traian, CC BY-SA 3.0, https://commons.wikimedia.org/w/index.php?curid=27799196](https://fosspost.org/wp-content/uploads/2016/07/1200px-Free_and_open-source-software_display_servers_and_UI_toolkits.svg_.png)

- [Package Manager](https://fosspost.org/what-are-the-components-of-a-linux-distribution/#Package_Manager)

- [Desktop Environment](https://fosspost.org/what-are-the-components-of-a-linux-distribution/#Desktop_Environment)

- [User Applications](https://fosspost.org/what-are-the-components-of-a-linux-distribution/#User_Applications)

### HT 总线

和 PCI 总线的区别在于 pci 总线是系统总线，用于连接外设，而 HT 总线速度更快，带宽更高，可以用来多核芯片的核间互联。

### Super I/O

Super I/O 是主板上用来连接像 floppy-disk controller, parallel port, serial port, RTC 之类的低速设备的芯片。

### extioi

extioi 是 LA 架构上的一个中断控制器。

### 系统启动过程

cpu 上电之后先从 rom 中加载 bios ，bios 完成的主要任务如下：

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

kernel 要加载文件系统，也就是 / ，但是 / 是存储在磁盘中的，而读取磁盘需要驱动，而这时文件系统都没起来，驱动也没有，这就陷入一个死循环，所以 linux 做了一个初级的文件系统 initramfs ，initramfs 足够小，可以通过 bios 从磁盘加载到内存中，grub 会指定 initramfs 存放在内存的位置信息，这些信息放在一个单独的空页，并把这个页的地址提供给内核，内核根据地址找到 initramfs ，再通过 initramfs 启动 / ，启动 / 之后再 chroot ，切换到 / 。

开机时会进入 bios ，bios 初始化硬件信息，并跳转到 bootloader ，也就是 grub ，在 grub 中可以加入 break=mount ，使得在之后的启动中进入 initramfs 的 shell ，而不是直接执行 initramfs 。

细节还需要补充。

### 16550A UART

One drawback of the earlier 8250 UARTs and 16450 UARTs was that interrupts were generated for each byte received. This generated high rates of interrupts as transfer speeds increased. More critically, with only a 1-byte buffer there is a genuine risk that a received byte will be overwritten if interrupt service delays occur. To overcome these shortcomings, the 16550 series UARTs incorporated a 16-byte FIFO buffer with a programmable interrupt trigger of 1, 4, 8, or 14 bytes.

### QEMU 虚拟机热迁移

虚拟机在运行过程中透明的从源宿主机迁移到目的宿主机。QEMU 中的 migration 子目录负责这部分工作，通过每个设备中有一个 `VMStateDesrciption` 的结构记录了热迁移过程中需要的迁移数据及相关回调函数。

### 同步与异步

所谓同步，就是在发出一个功能调用时，在没有得到结果之前，该调用就不返回。按照这个定义，其实绝大多数函数都是同步调用（例如 sin, isdigit 等）。但是一般而言，我们在说同步、异步的时候，特指那些需要其他部件协作或者需要一定时间完成的任务。

同步，可以理解为在执行完一个函数或方法之后，一直等待系统返回值或消息，这时进程是出于阻塞的，只有接收到返回的值或消息后才往下执行其他的命令。

异步的概念和同步相对。当一个异步过程调用发出后，调用者不能立刻得到结果。由另外的并行进程执行这段代码，处理完这个调用的部件在完成后，通过**状态**、**通知**和**回调**来通知调用者。

异步，执行完函数或方法后，不必阻塞性地等待返回值或消息，只需要向系统委托一个异步过程，那么当系统接收到返回值或消息时，系统会自动触发委托的异步过程，从而完成一个完整的流程。

### 虚拟文件系统

在 Linux 中，普通文件、目录、字符设备、块设备、套接字等都以文件被对待；他们具体的类型及其操作不同，但需要向上层提供统一的操作接口。

虚拟文件系统 (VFS) 就是 Linux 内核中文件系统的抽象层，**向上**给用户空间程序提供文件系统操作接口；**向下**允许不同类型的文件系统共存，这些文件系统通过 VFS 结构封装向上提供统一操作接口。

通过虚拟文件系统，程序可以利用标准 Linux 文件系统调用在不同的文件系统中进行交互和操作。

### _init and _initdata in kernle

These macros are used to mark some functions or initialized data (doesn't apply to uninitialized data) as `initialization' functions. The kernel can take this as hint that the function is used only during the initialization phase and free up used memory resources after.

### exception_table_entry

```c
/*
 * The exception table consists of pairs of addresses: the first is the
 * address of an instruction that is allowed to fault, and the second is
 * the address at which the program should continue.  No registers are
 * modified, so it is entirely up to the continuation code to figure out
 * what to do.
 *
 * All the routines below use bits of fixup code that are out of line
 * with the main instruction path.  This means when everything is well,
 * we don't even have to jump over them.  Further, they do not intrude
 * on our cache or tlb entries.
 */

struct exception_table_entry
{
	unsigned long insn, fixup;
};

```

```plain
start_kernel
| -- sort_main_extable
|	-- sort_extable
```

```c
void __init sort_main_extable(void)
{
	if (main_extable_sort_needed && __stop___ex_table > __start___ex_table) {
		pr_notice("Sorting __ex_table...\n");
		sort_extable(__start___ex_table, __stop___ex_table);
	}
}
```

```c
void sort_extable(struct exception_table_entry *start,
		  struct exception_table_entry *finish)
{
	sort(start, finish - start, sizeof(struct exception_table_entry),
	     cmp_ex_sort, swap_ex);
}
```

### 实模式与保护模式之间的切换

Seabios 中有实现两者是怎样切换的，而 [Seabios.md](https://github.com/UtopianFuture/UtopianFuture.github.io/blob/master/virtualization/Analysis-Seabios.md) 中详细分析了其实现。

### 守护进程

Linux Daemon（守护进程）是运行在后台的一种特殊进程。它独立于控制终端并且周期性地执行某种任务或等待处理某些发生的事件。它不需要用户输入就能运行而且提供某种服务，不是对整个系统就是对某个用户程序提供服务。Linux 系统的大多数服务器就是通过守护进程实现的。常见的守护进程包括系统日志进程 syslogd、 web 服务器 httpd、邮件服务器 sendmail 和数据库服务器 mysqld 等。

守护进程一般在系统启动时开始运行，除非强行终止，否则直到系统关机都保持运行。守护进程经常以超级用户（root）权限运行，因为它们要使用特殊的端口（1-1024）或访问某些特殊的资源。

一个守护进程的父进程是 init 进程，因为它真正的父进程在 fork 出子进程后就先于子进程 exit 退出了，所以它是一个**由 init 继承的孤儿进程**。守护进程是非交互式程序，没有控制终端，所以任何输出，无论是向标准输出设备 stdout 还是标准出错设备 stderr 的输出都需要特殊处理。

守护进程的名称通常以 d 结尾，比如 sshd、xinetd、crond 等

### sockets

A **network socket** is a software structure within a [network node](https://en.wikipedia.org/wiki/Node_(networking)) of a [computer network](https://en.wikipedia.org/wiki/Computer_network) that serves as an endpoint for sending and receiving data across the network. The structure and properties of a socket are defined by an [application programming interface](https://en.wikipedia.org/wiki/Application_programming_interface) (API) for the networking architecture. Sockets are created only during the lifetime of a [process](https://en.wikipedia.org/wiki/Process_(computing)) of an application running in the node.

### 边沿触发和电平触发

边沿触发分上升沿触发和下降沿触发，简单说就是电平变化那一瞬间进行触发。
电平触发也有高电平触发、低电平触发，一般都是低电平触发。如果是低电平触发，那么在低电平时间内中断一直有效。

### 字符设备和块设备

系统中能够随机（不需要按顺序）访问固定大小数据片（chunks）的设备被称作块设备，这些数据片就称作块。最常见的块设备是硬盘，除此以外，还有软盘驱动器、CD-ROM 驱动器和闪存等等许多其他块设备。注意，它们都是以安装文件系统的方式使用的——这也是块设备的一般访问方式。

另一种基本的设备类型是字符设备。字符设备按照字符流的方式被有序访问，像串口和键盘就都属于字符设备。如果一个硬件设备是以字符流的方式被访问的话，那就应该将它归于字符设备；反过来，如果一个设备是随机（无序的）访问的，那么它就属于块设备。

### SMM

SMM is **a special-purpose operating mode** provided for handling system-wide functions like **power management, system hardware control, or proprietary OEM designed code**. It is intended for use only by system firmware ([BIOS](https://en.wikipedia.org/wiki/BIOS) or [UEFI](https://en.wikipedia.org/wiki/UEFI)), not by applications software or general-purpose systems software. The main benefit of SMM is that it **offers a distinct and easily isolated processor environment** that operates transparently to the operating system or executive and software applications.

翻译过来就是 SMM 是一个特别的处理器工作模式，它运行在独立的空间里，具有自己独立的运行环境，与 real mode/protected mode/long mode 隔离，但是具有极高的权限。**SMM 用来处理 power 管理、hardware 控制这些比较底层，比较紧急的事件**。这些事件使用 **SMI(System Management Interrupt)** 来处理，因此进入了 SMI 处理程序也就是进入了 SMM ，SMI 是不可屏蔽的外部中断，并且不可重入（在 SMI 处理程序中不能响应另一个 SMI 请求）。

### [微内核和宏内核](https://zhuanlan.zhihu.com/p/53612117)

微内核是将各种服务功能放到内核之外，自身仅仅是一个消息中转站，用于各种功能间的通讯，如 WindowNT。
宏内核是将所有服务功能集成于一身，使用时直接调用，如 Linux。
从理论上来看，微内核的思想更好些，微内核把系统分为各个小的功能块，降低了设计难度，系统的维护与修改也容易，但通信带来的效率损失是个问题。宏内核的功能块之间的耦合度太高造成修改与维护的代价太高，但其是直接调用，所以效率是比较高的。

### What is a Unikernel

Normally, a hypervisor loads a Virtual Machine with a fully functional operating system, like some flavor of Linux, Windows, or one of the BSDs. These operating systems were designed to be run on hardware, so they have all the complexity needed for a variety of hardware drivers from an assortment of vendors with different design concepts. These operating systems are also intended to be multi-user, multi-process, and multi-purpose. They are designed to be everything for everyone, so they are necessarily complex and large.

A Unikernel, on the other hand, is (generally) single-purpose. It is not designed to run on hardware, and so lacks the bloat and complexity of drivers. It is not meant to be multi-user or multi-process, so it can **focus on creating a single thread of code which runs one application, and one application only**. Most are not multi-purpose, as the target is to create a single payload that a particular instance will execute (OSv is an exception). Thanks to this single-minded design, the Unikernel is small, lightweight, and quick.

### kernel control path

A *kernel control path* is the sequence of instructions executed by a kernel to handle a *system call*, an *interrupt* or an *exception*.

The sequence of instructions executed in Kernel Mode to handle a kernel request is denoted as kernel control path : when a User Mode process issues a system call request, for instance, the first instructions of the corresponding kernel control path are those included in the initial part of the `system_call(` function, while the last instructions are those included in the `ret_from_sys_call()` function.

总结来说，就是 kernel 中处理 system call， interrupt 和 exception 的程序，一般以 `system_call()` 开始，以 `ret_from_sys_call()`结束。

### ASCII

![ascii.gif](https://github.com/UtopianFuture/UtopianFuture.github.io/blob/master/image/ascii.gif?raw=true)

### [ret_from_fork](https://www.oreilly.com/library/view/understanding-the-linux/0596002130/ch04s08.html)

当执行中断或异常的处理程序后需要返回到原来的进程，在返回前还需要处理以下几件事：

- 并发执行的内核控制路径数（kernel control path）

  如果只有一个，CPU 必须切换回用户模式。

- 待处理进程切换请求

  如果有任何请求，内核必须进行进程调度；否则，控制权返回到当前进程。

- 待处理信号

  如果一个信号被发送到当前进程，它必须被处理。

`ret_from_fork` 就是终止 `fork()`, `vfork()` 和 `clone()` 系统调用的。类似的函数还有

`ret_from_exception`：终止除了 0x80 异常外的所有异常处理程序。

`ret_from_intr`：终止中断处理程序

`ret_from_sys_call`：终止所有的系统调用处理程序。

下面是处理流程。

![ret_from_fork.png](https://github.com/UtopianFuture/UtopianFuture.github.io/blob/master/image/ret_from_fork.png?raw=true)

Initially, the `ebx` register stores **the address of the descriptor of the process being replaced by the child** (usually the parent’s process descriptor); this value is passed to the `schedule_tail()` function as a parameter. When that function returns, `ebx` is reloaded with the `current`’s process descriptor address. Then the `ret_from_fork()` function checks the value of the `ptrace` field of the `current` (at offset 24 of the process descriptor). If the field is not null, the `fork( )`, `vfork( )`, or `clone( )` system call is traced, so the `syscall_trace( )` function is invoked to notify the debugging process.

### CFS

The **Completely Fair Scheduler** (**CFS**) is a default process scheduler of the tasks of the `SCHED_NORMAL` class (i.e., tasks that have no real-time execution constraints). It handles CPU resource allocation for executing processes, and aims to maximize overall CPU utilization while also maximizing interactive performance.

### runqueue

The run queue may contain priority values for each process, which will be used by the scheduler to determine **which process to run next**. To ensure each program has a fair share of resources, each one is run for some time period (quantum) before it is paused and placed back into the run queue. When a program is stopped to let another run, the program with the highest priority in the run queue is then allowed to execute.

Processes are also removed from the run queue when they ask to *sleep*, are waiting on a resource to become available, or have been terminated.

Each CPU in the system is given a run queue, which maintains both an active and expired array of processes. Each array contains 140 (one for each priority level) pointers to doubly linked lists, which in turn reference all processes with the given priority. The scheduler selects the next process from the active array with highest priority.

### watchdog

The Linux kernel can reset the system if serious problems are detected. This can be implemented via special watchdog hardware, or via a slightly less reliable software-only watchdog inside the kernel. Either way, there **needs to be a daemon that tells the kernel the system is working fine**. If the daemon stops doing that, the system is **reset**.

**watchdog** is such a daemon. It opens */dev/watchdog*, and keeps writing to it often enough to keep the kernel from resetting, at least once per minute. **Each write delays the reboot time another minute**. After a minute of inactivity the watchdog hardware will cause the reset. In the case of the software watchdog the ability to reboot will depend on the state of the machines and interrupts.

### [LWP](https://cloud.tencent.com/developer/article/1339562)

轻量级进程(LWP)是**建立在内核之上并由内核支持的用户线程**，它是内核线程的高度抽象，每一个轻量级进程都与一个特定的内核线程关联。内核线程只能由内核管理并像普通进程一样被调度。

轻量级进程**由 clone()系统调用创建，参数是 CLONE_VM**，即与父进程是共享进程地址空间和系统资源。

与普通进程区别：**LWP 只有一个最小的执行上下文和调度程序所需的统计信息**。

- 处理器竞争：因与特定内核线程关联，因此可以在全系统范围内竞争处理器资源
- 使用资源：与父进程共享进程地址空间
- 调度：像普通进程一样调度

### 用户线程

用户线程是完全建立在用户空间的线程库，用户线程的创建、调度、同步和销毁全又库函数在用户空间完成，不需要内核的帮助。因此这种线程是极其低消耗和高效的。

LWP 虽然本质上属于用户线程，但 LWP 线程库是建立在内核之上的，LWP 的许多操作都要进行系统调用，因此效率不高。而这里的用户线程指的是完全建立在用户空间的线程库，用户线程的建立，同步，销毁，调度完全在用户空间完成，不需要内核的帮助。因此这种线程的操作是极其快速的且低消耗的。

### GFP

Most of the memory allocation APIs use GFP flags to express how that memory should be allocated. The GFP acronym stands for **“get free pages”**, the underlying memory allocation function.

### .macro

The commands `.macro` and `.endm` allow you to define macros that generate assembly output. For example, this definition specifies a macro `sum` that puts a sequence of numbers into memory:

```plain
             .macro  sum from=0, to=5
             .long   \from
             .if     \to-\from
             sum     "(\from+1)",\to
             .endif
             .endm
```

With that definition, `SUM 0,5' is equivalent to this assembly input:

```plain
             .long   0
             .long   1
             .long   2
             .long   3
             .long   4
             .long   5
```

### declaration and definition

**Declaration of a variable or function simply declares that the variable or function exists somewhere in the program, but the memory is not allocated for them**. The declaration of a variable or function serves an important role–it tells the program what its type is going to be. In case of *function* declarations, it also tells the program the arguments, their data types, the order of those arguments, and the return type of the function. So that’s all about the declaration.

Coming to the **definition**, when we *define* a variable or function, in addition to everything that a declaration does, **it also allocates memory for that variable or function**. Therefore, we can think of definition as a superset of the declaration (or declaration as a subset of definition).

A variable or function can be *declared* any number of times, but it can be *defined* only once.

### extern

the extern keyword extends the function’s visibility to the whole program, the function can be used (called) anywhere in any of the files of the whole program, provided those files contain a declaration of the function.

### CFI

On some architectures, exception handling must be managed with **Call Frame Information** directives. These directives are used in the assembly to **direct exception handling**. These directives are available on Linux on POWER, if, for any reason (portability of the code base, for example), the GCC generated exception handling information is not sufficient.

### LPC

The **Low Pin Count** (**LPC**) bus is a computer bus used on IBM-compatible personal computers to connect low-bandwidth devices to the CPU, such as the BIOS ROM, "legacy" I/O devices, and Trusted Platform Module (TPM). "Legacy" I/O devices usually include serial and parallel ports, PS/2 keyboard, PS/2 mouse, and floppy disk controller.

### PCH

The **Platform Controller Hub** (**PCH**) is a family of Intel's single-chip chipsets. The PCH controls certain data paths and support functions used in conjunction with Intel CPUs.

### GPIO

A **general-purpose input/output** (**GPIO**) is an uncommitted digital signal pin on an integrated circuit or electronic circuit board which may be used as an input or output, or both, and is controllable by the user at runtime.

### [IOMMU](https://en.wikipedia.org/wiki/Input%E2%80%93output_memory_management_unit)

IOMMU(**input–output memory management unit**) 有两大功能：控制设备 dma 地址映射到机器物理地址（DMA Remmaping），中断重映射(intremap)（可选）

第一个功能进一步解释是这样的，在没有 Iommu 的时候，设备通过 dma 可以访问到机器的全部的地址空间，这样会导致系统被攻击。当出现了 iommu 以后，iommu 通过控制每个设备 dma 地址到实际物理地址的映射转换，使得在一定的内核驱动框架下，用户态驱动能够完全操作某个设备 dma 和中断成为可能，同时保证安全性。

而 mmio 是设备 i/o 地址映射到内存，使得不用专门的 i/o 访存指令。

[iommu 技术综述](https://www.jianshu.com/p/dd8ab6b68c6a)

[amd iommu](https://www.jianshu.com/p/dd8ab6b68c6a)

### System.map

system.map 是保存内核的符号和该符号在内存中的地址映射关系的表。

### control registor in X86

Control registers (CR0, CR1, CR2, CR3, and CR4; CR8 is available in 64-bit mode only.) determine **operating mode of the processor** and **the characteristics of the currently executing task**. These registers are 32 bits in all 32-bit modes and compatibility mode. In 64-bit mode, control registers are expanded to 64 bits.

- **CR0** — Contains system control flags that control operating mode and states of the processor.
- CR1 — Reserved.
- CR2 — Contains **the page-fault linear address** (the linear address that caused a page fault).
- CR3 — Contains **the physical address of the base of the paging-structure hierarchy** and two flags (PCD and PWT). Only the most-significant bits (less the lower 12 bits) of the base address are specified; **the lower 12 bits of the address are assumed to be 0**. The first paging structure must thus be aligned to a page (4-KByte) boundary. The PCD and PWT flags control caching of that paging structure in the processor’s internal data caches (they do not control TLB caching of page-directory information).
- CR4 — Contains a group of flags that enable several architectural extensions, and indicate operating system or executive support for specific processor capabilities.
- CR8 — Provides read and write access to the Task Priority Register (TPR). It specifies the priority threshold value that operating systems use to control the priority class of **external interrupts allowed to interrupt the processor**.

this content can be find in Inter software developer's manual: Volume 3 2.5.

### [TLB exceptions in mips](https://techpubs.jurassic.nl/manuals/hdwr/developer/R10K_UM/sgi_html/t5.Ver.2.0.book_353.html)

Three types of TLB exceptions can occur:

- **TLB Refill** occurs when there is **no TLB entry that matches** an attempted reference to a mapped address space.
- **TLB Invalid** occurs when a virtual address reference **matches** a TLB entry that is marked **invalid**.
- **TLB Modified** occurs when a store operation virtual address reference to memory matches a TLB entry which is marked **valid** but is not dirty (the entry is not writable).

### Linux address space layout

![layout](https://github.com/UtopianFuture/UtopianFuture.github.io/blob/master/image/linux-address-space-layout.png?raw=true)

### Implicit Rules of Make

The important variables used by implicit rules are:

- `CC`: Program for compiling C programs; default `cc`
- `CXX`: Program for compiling C++ programs; default `g++`
- `CFLAGS`: Extra flags to give to the C compiler
- `CXXFLAGS`: Extra flags to give to the C++ compiler
- `CPPFLAGS`: Extra flags to give to the C preprocessor
- `LDFLAGS`: Extra flags to give to compilers when they are supposed to invoke the linker

### 用户栈

用户栈是属于用户进程空间的一块区域，**用于保存用户进程子程序间的相互调用的参数、返回值**等。

用户栈的栈底靠近进程空间的上边缘，但一般不会刚好对齐到边缘，出于安全考虑，会在栈底与进程上边缘之间插入一段随机大小的隔离区。这样，程序在每次运行时，栈的位置都不同，这样黑客就不大容易利用基于栈的安全漏洞来实施攻击。

当用户程序逐级调用函数时，用户栈从高地址向低地址方向扩展，**每次增加一个栈帧**，一个栈帧中存放的是函数的参数、返回地址和局部变量等，所以栈帧的长度是不定的。

### 内核栈

内核栈是属于操作系统空间的一块固定区域，可以**用于保存中断现场、保存操作系统子程序间相互调用的参数、返回值**等。

### 用户栈和内核栈的区别

内核在创建进程（进程这个概念是经典的操作系统参考书中用到的，其实内核源码中没有 thread 这个数据结构，都是 task 数据结构）的时候，需要创建 `task_struct` ，同时也会创建相应的堆栈。**每个进程会有两个栈**，一个用户栈，存在于用户空间，一个内核栈，存在于内核空间。当进程在用户空间运行时，cpu 堆栈指针寄存器（sp）里面的内容是用户堆栈地址，使用用户栈；**当进程因为中断或者系统调用而陷入内核态执行时**，sp 里面的内容是内核栈空间地址，使用内核栈。

进程陷入内核态后，先把用户态堆栈的地址保存在内核栈之中，然后设置 sp 的内容为内核栈的地址，这样就完成了用户栈向内核栈的转换；当进程从内核态恢复到用户态执行时，在内核态执行的最后将保存在内核栈里面的用户栈的地址恢复到堆栈指针寄存器即可。这样就实现了内核栈和用户栈的互转。

那么，我们知道从内核转到用户态时用户栈的地址是在陷入内核的时候保存在内核栈里面的，但是在陷入内核的时候，我**们是如何知道内核栈的地址的呢？**

关键在进程从用户态转到内核态的时候，**进程的内核栈总是空的**。这是因为当进程在用户态运行时，使用的是用户栈，当进程陷入到内核态时，**内核栈保存进程在内核态运行的相关信息**，但是一旦进程返回到用户态后，内核栈中保存的信息无效，会全部恢复，因此**每次进程从用户态陷入内核的时候得到的内核栈都是空的**。所以在进程陷入内核的时候，直接把内核栈的栈顶地址给堆栈指针寄存器就可以了。

### x86 五级页表

内核为了支持不同的 CPU 体系架构，设计了五级分页模型。五级分页模型是为了兼容X86-64体系架构中的5-Level Paging分页模式。

![five-level-paging.png](https://github.com/UtopianFuture/UtopianFuture.github.io/blob/master/image/five-level-paging.png?raw=true)

五级分页每级命名分别为页全局目录(PGD)、页4级目录(P4D)、页上级目录(PUD)、页中间目录(PMD)、页表(PTE)。对应的相关宏定义命名如下：

```c
#define PGDIR_SHIFT
#define P4D_SHIFT
#define PUD_SHIFT
#define PMD_SHIFT
#define PAGE_SHIFT
```

### 主缺页和次缺页

缺页中断可分为主缺页中断（Major Page Fault）和次缺页中断（Minor Page Fault），要从磁盘读取数据而产生的中断是主缺页中断；从内存中而不是直接从硬盘中读取数据而产生的中断是次缺页中断。

需要访存时内核先在 cache 中寻找数据，如果 cache miss，产生次缺页中断从内存中找，如果还没有发现的话就产生主缺页中断从硬盘读取。

### 系统调用

在内核中 `int 0x80` 表示系统调用，这是上层应用程序与内核进行交互通信的唯一接口。通过检查寄存器 `%eax` 中的值，内核会收到用户程序想要进行哪个系统调用的通知。

下面是部分系统调用（arch/x86/include/generated/asm/syscalls_64.h）：

```c
__SYSCALL(0, sys_read)
__SYSCALL(1, sys_write)
__SYSCALL(2, sys_open)
__SYSCALL(3, sys_close)
__SYSCALL(4, sys_newstat)
__SYSCALL(5, sys_newfstat)
__SYSCALL(6, sys_newlstat)
__SYSCALL(7, sys_poll)
__SYSCALL(8, sys_lseek)
__SYSCALL(9, sys_mmap)
__SYSCALL(10, sys_mprotect)
__SYSCALL(11, sys_munmap)
__SYSCALL(12, sys_brk)
```

### Kernel Panic

起内核时经常会遇到这个问题：**Kernel Panic (not syncing): attempted to kill init!**

这是什么意思？

这个问题分为 3 个部分：

- **Kernel panic**:
  A "kernel panic" is an unrecoverable error condition in, or detected by, the Linux kernel. It represents a situation where Linux literally finds it *impossible* to continue running, and stops dead in its tracks. *(There is also a so-called "Oops" facility for recoverable errors.)* Linux prints messages on the console and halts. Very often, the root cause of the problem will be indicated by some of these immediately-preceding messages.

- **not syncing**:
  This part of the message indicates that the kernel was not in the middle of writing data to disk at the time the failure occurred. This is a supplemental piece of information that is not directly related to the panic message itself.

- **attempted to kill init**:
  This is the situation that Linux actually detected, and this message is somewhat misleading. It actually means that "Process #1" either terminated or, just as likely, failed to start.


解释起来就是尝试杀死 `init` 进程。关于 `init` 进程的介绍可以看[这里](https://github.com/UtopianFuture/UtopianFuture.github.io/blob/master/kernel/kernel_init.md)。

**So, what can I do?**
In spite of the message referring to *"kill* **init**," the most likely reason is that **the process failed to start**. Process #1 is an ordinary user process, running as root, and so it has the same basic requirements as any other process. Messages will usually be found in the log, immediately preceding the panic, which tell you more about exactly what went wrong.

For instance, a disk-driver that is needed to access the hard drive might have failed to load, because of a recent update to Linux that somehow went wrong. A menu of installed kernel versions usually appears briefly when you start the machine: try booting from the previous version of the Linux kernel.

Another distinct possibility is that **the file system has become corrupt**. In this case, you may need to try to boot the machine into a "recovery mode" *(depending on your distro),* or boot from a DVD or memory-stick. For instance, the startup menu on the installation disk for most Linux distros contains a "recovery" option which will attempt to check for and fix errors on the boot drive. Boot the system from that DVD and select this option. Any unexplained mis-behavior of a disk drive is also a strong indication that the drive may very soon need to be replaced.

### 系统态和用户态

You can tell if you're in user-mode or kernel-mode from the privilege level set **in the code-segment register (CS)**. Every instruction loaded into the CPU from the memory pointed to by the RIP(EIP) will read from the segment described in the global descriptor table (GDT) by the **current code-segment descriptor**. **The lower two-bits of the code segment descriptor will determine the current privilege level that the code is executing at**. When a syscall is made, which is typically done through a software interrupt, the CPU will check the current privilege-level, and if it's in user-mode, will exchange the current code-segment descriptor for a kernel-level one as determined by the syscall's software interrupt gate descriptor, as well as **make a stack-switch and save the current flags, the user-level CS value and RIP value on this new kernel-level stack**. When the syscall is complete, the user-mode CS value, flags, and instruction pointer (EIP or RIP) value are restored from the kernel-stack, and a stack-switch is made back to the current executing processes' stack.

### MMU

MMU是 CPU 中的一个硬件单元，通常每个核有一个 MMU。MMU由两部分组成：TLB(Translation Lookaside Buffer)和 table walk unit。

TLB 很熟悉了，就不再分析。主要介绍一下 table walk unit。

首先对于硬件 page table walk 的理解是有些问题的，即内核中有维护一套进程页表，为什么还要硬件来做呢？两者有何区别？第一个问题很好理解，效率嘛，第二个问题就是我们要分析的。

如果发生 TLB miss，就需要查找当前进程的 page table，接下来就是 table walk unit 的工作。而使用 table walk unit硬件单元来查找page table的方式被称为hardware TLB miss handling，通常被CISC架构的处理器（比如IA-32）所采用。如果在page table中查找不到，出现page fault，那么就会交由软件（操作系统）处理，之后就是我们熟悉的 PF。

好吧，从找到的资料看 page table walk 和我理解的内核的 4/5 级页表转换没有什么不同。但是依旧有一个问题，即 mmu 访问的这些 page table 是不是就是内核访问的 page table，都是存放在内存中的。

### .bss 段清 0

根据C语法的规定，**局部变量**不设置初始值的时候，其初始值是不确定的，局部变量（不含静态局部变量）的存储位置位于栈上，具体位置不固定。**全局变量（和静态局部变量）**存储在 .bss 段，初始值是0，所以所有的进程在加载到内核执行时都要将 .bss 段清 0。

早期的计算机存储设备是很贵的，而很多时候，数据段里的全局变量都是0（或者没有初始值），那么存储这么多的 0 到目标文件里其实是没有必要的。所以为了节约空间，在生成目标文件的时候，就**把没有初始值（实际就是0）的数据段里的变量都放到 BSS 段里**，这样目标文件就不需要那么大的体积里（节约磁盘空间）。只有当目标文件被载入的时候，加载器负责把BSS段清零（一个循环就可以搞定）。

### RCU

RCU（Read-Copy Update）是数据同步的一种方式，在当前的Linux内核中发挥着重要的作用。RCU 主要针对的数据对象是链表，目的是提高遍历读取数据的效率，使用 RCU 机制读取数据的时候**不对链表进行耗时的加锁操作**。这样在同一时间可以有多个线程同时读取该链表，并且允许一个线程对链表进行修改（修改的时候，需要加锁）。RCU 适用于需要频繁的读取数据，而相应修改数据并不多的情景，例如在文件系统中，经常需要查找定位目录，而对目录的修改相对来说并不多，这就是 RCU 发挥作用的最佳场景。

### register in X86

在 X86 架构中，对于不同寄存器的使用一定的约定：

- %rax 作为函数返回值使用；
- %rsp 栈指针寄存器，指向栈顶；
- %rdi，%rsi，%rdx，%rcx，%r8，%r9 用作函数参数，依次对应第1参数，第2参数等等；
- %rbx，%rbp，%r12，%r13，%14，%15 用作数据存储，遵循被调用者使用规则，简单说就是随便用，调用子函数之前要备份它，以防被修改；

- %r10，%r11 用作数据存储，遵循调用者使用规则，简单说就是使用之前要先保存原值；

### X86/64 的权限

- CPL(Current Privilege Level)：当前的权限级别，CPL 的值放在 CS 寄存器的 Selector 域的 RPL，CS.Selector.RPL 与 SS 寄存器的 Selector.RPL 总是相等的，因此 SS.Selector.RPL 也是 CPL。
- DPL(Descriptor Privilege Level)：DPL 存放在 Descriptor（包括 Segment Descriptor 和 Gate Descriptor）里的 DPL 域，它指示访问这些 segment 所需要的权限。
- RPL(Requested Privilege Level)：和 CPL 一样。

当 CPL > DPL 时，表示当前运行的程序权限不足，无法访问 segment 或 gate。

### x86 模式切换

x86 架构中 CPU 的模式众多，幸好手册给出了它们之间的关系。不过还有一个 Long Mode 是啥，好吧 intel 中 IA-32e 就是 Long Mode。

![x86-modes.png](https://github.com/UtopianFuture/UtopianFuture.github.io/blob/master/image/x86-modes.png?raw=true)

### __randomize_layout

结构体随机化。不管是地址随机化，还是结构体随机化，其目的都是增强内核安全性。结构体随机化也是GCC编译器的一个重要特性，使能后将在编译时随机排布结构体中元素的顺序，从而使攻击者无法通过地址偏移进行攻击。

在内核中，结构体中存在函数指针的部分是攻击者重点关注的对象。因此，只存储函数指针的结构体，是默认开启结构体随机化的，如果不需要，需要添加`__no_randomize_layout`进行排除。另一方面，如果特定结构体希望主动开启保护，需要添加`__randomize_layout`标识。

结构体随机化后的差异

- 既然已经开启了结构体随机化，在进行赋值或初始化时，就需要按照元素名称进行赋值，否则会出现非预期结果。

- 不要对开启了随机化的结构体指针或对象进行强制数据转换，因为内存排布是不可预测的。

- 涉及到远程调用的结构体，如果需要保证结构体内容的一致性，需要添加例外。

- 调试时，根据dump推算结构体内容将极为麻烦，因为每个版本、每个平台的布局都将不同。

- 部分以模块形式添加到内核中的驱动，为保持结构体一致性，在编译时需要采用与内核相同的随机数种子，这带来了极大的安全风险。

### efi

EFI 系统分区（EFI system partition，ESP），是一个 [FAT](https://zh.wikipedia.org/wiki/FAT) 或 [FAT32](https://zh.wikipedia.org/wiki/FAT32) 格式的磁盘分区。UEFI 固件可从 ESP 加载 EFI 启动程式或者 EFI 应用程序。

### [cpio](https://unix.stackexchange.com/questions/7276/why-use-cpio-for-initramfs)

cpio 是 UNIX 操作系统的一个文件备份程序及文件格式。

The initial ramdisk needs to be unpacked by the kernel during boot, cpio is used because it is already implemented in kernel code.

All 2.6 Linux kernels **contain a gzipped "cpio" format archive,** which is extracted into rootfs when the kernel boots up.  After extracting, the kernel
checks to see if rootfs contains a file "init", and if so it executes it as PID. If found, this init process is responsible for bringing the system the rest of the way up, including locating and mounting the real root device (if any).  If rootfs does not contain an init program after the embedded cpio archive is extracted into it, the kernel will fall through to the older code to locate and mount a root partition, then exec some variant of /sbin/init
out of that.

### ACPI（建议浏览一下 ACPI[手册](https://uefi.org/sites/default/files/resources/ACPI_6_3_final_Jan30.pdf)）

Advanced Configuration and Power Interface (ACPI). Before the development of ACPI, operating systems (OS) primarily used BIOS (Basic Input/
Output System) interfaces for **power management and device discovery and configuration**.

ACPI can first be understood as an architecture-independent power management and configuration framework that forms a subsystem within the host OS. This framework **establishes a hardware register set to define power states** (sleep, hibernate, wake, etc). The hardware register set can accommodate operations on dedicated hardware and general purpose hardware.

The primary intention of the standard ACPI framework and the hardware register set is to enable power management and system configuration without directly calling firmware natively from the OS. **ACPI serves as an interface layer between the system firmware (BIOS) and the OS**.

There are 2 main parts to ACPI. **The first part** is the tables used by the OS for configuration during boot (these include things like how many CPUs, APIC details, NUMA memory ranges, etc). The second part is the run time ACPI environment, which consists of AML code (a platform independent OOP language that comes from the BIOS and devices) and the ACPI SMM (System Management Mode) code.

关键数据结构：

**RSDT** (Root System Description Table) is a data structure used in the [ACPI](https://wiki.osdev.org/ACPI) programming interface. This table contains pointers to all the other System Description Tables. However there are many kinds of SDT. All the SDT may be split into two parts. One (**the header**) which is common to all the SDT and another (data) which is different for each table. RSDT contains 32-bit physical addresses, XSDT contains 64-bit physical addresses.

### [NUMA](https://zhuanlan.zhihu.com/p/62795773)

NUMA 指的是针对某个 CPU，内存访问的距离和时间是不一样的。其解决了多 CPU 系统下共享 BUS 带来的性能问题（链接中的图很直观）。

NUMA 的特点是：被共享的内存物理上是分布式的，所有这些内存的集合就是全局地址空间。所以处理器访问这些内存的时间是不一样的，显然访问本地内存的速度要比访问全局共享内存或远程访问外地内存要快些。

### initrd

Initrd ramdisk 或者 initrd 是指一个临时文件系统，它在启动阶段被 Linux 内核调用。initrd 主要用于当“根”文件系统被[挂载](https://zh.wikipedia.org/wiki/Mount_(Unix))之前，进行准备工作。

### [设备树（dt）](https://e-mailky.github.io/2019-01-14-dts-1)

Device Tree 由一系列被命名的结点（node）和属性（property）组成，而结点本身可包含子结点。所谓属性， 其实就是成对出现的 name 和 value。在 Device Tree 中，可描述的信息包括（原先这些信息大多被 hard code 到 kernel 中）：

- CPU 的数量和类别
- 内存基地址和大小
- 总线和桥
- 外设连接
- 中断控制器和中断使用情况
- GPIO 控制器和 GPIO 使用情况
- Clock 控制器和 Clock 使用情况

它基本上就是画一棵电路板上 CPU、总线、设备组成的树，**Bootloader 会将这棵树传递给内核**，然后内核可以识别这棵树， 并根据它**展开出 Linux 内核中的** platform_device、i2c_client、spi_device 等**设备**，而这些设备用到的内存、IRQ 等资源， 也被传递给了内核，内核会将这些资源绑定给展开的相应的设备。

是否 Device Tree 要描述系统中的所有硬件信息？答案是否定的。基本上，那些可以动态探测到的设备是不需要描述的， 例如 USB device。不过对于 SOC 上的 usb hostcontroller，它是无法动态识别的，需要在 device tree 中描述。同样的道理， 在 computersystem 中，PCI device 可以被动态探测到，不需要在 device tree 中描述，但是 PCI bridge 如果不能被探测，那么就需要描述之。

设备树和 ACPI 有什么关系？

### [BootMem 内存分配器](https://cloud.tencent.com/developer/article/1376122)

**[Bootmem](https://www.kernel.org/doc/html/v4.19/core-api/boot-time-mm.html#bootmem) is a boot-time physical memory allocator and configurator**.

It is used early in the boot process before the page allocator is set up.

Bootmem is based on the most basic of allocators, a First Fit allocator which uses a bitmap to represent memory. If a bit is 1, the page is allocated and 0 if unallocated. To satisfy allocations of sizes smaller than a page, the allocator records the **Page Frame Number (PFN)** of the last allocation and the offset the allocation ended at. Subsequent small allocations are merged together and stored on the same page.

The information used by the bootmem allocator is represented by `struct bootmem_data`. An array to hold up to `MAX_NUMNODES` such structures is statically allocated and then it is discarded when the system initialization completes. **Each entry in this array corresponds to a node with memory**. For UMA systems only entry 0 is used.

The bootmem allocator is initialized during early architecture specific setup. Each architecture is required to supply a `setup_arch` function which, among other tasks, is responsible for acquiring the necessary parameters to initialise the boot memory allocator. These parameters define limits of usable physical memory:

- **min_low_pfn** - the lowest PFN that is available in the system
- **max_low_pfn** - the highest PFN that may be addressed by low memory (`ZONE_NORMAL`)
- **max_pfn** - the last PFN available to the system.

After those limits are determined, the `init_bootmem` or `init_bootmem_node` function should be called to initialize the bootmem allocator. The UMA case should use the init_bootmem function. It will initialize `contig_page_data` structure that represents the only memory node in the system. In the NUMA case the `init_bootmem_node` function should be called to initialize the bootmem allocator for each node.

Once the allocator is set up, it is possible to use either single node or NUMA variant of the allocation APIs.

现在的 bootmem 初始化是用的 memblock，详细看这个。

LoongArch 的 bootmem 似乎和 x86 的不一样，以下为 x86 的 bootmem 初始化过程。

bootmem_data 结构：

```c
/**
 * struct bootmem_data - per-node information used by the bootmem allocator
 * @node_min_pfn: the starting physical address of the node's memory
 * @node_low_pfn: the end physical address of the directly addressable memory
 * @node_bootmem_map: is a bitmap pointer - the bits represent all physical
 *		      memory pages (including holes) on the node.
 * @last_end_off: the offset within the page of the end of the last allocation;
 *                if 0, the page used is full
 * @hint_idx: the PFN of the page used with the last allocation;
 *            together with using this with the @last_end_offset field,
 *            a test can be made to see if allocations can be merged
 *            with the page used for the last allocation rather than
 *            using up a full new page.
 * @list: list entry in the linked list ordered by the memory addresses
 */
typedef struct bootmem_data {
	unsigned long node_min_pfn;
	unsigned long node_low_pfn;
	void *node_bootmem_map;
	unsigned long last_end_off;
	unsigned long hint_idx;
	struct list_head list;
} bootmem_data_t;
```

bootmem 的需求是简单，因此使用 first fit 的方式。该分配器使用一个位图来管理页，位图中的 bit 数等于物理页数，bit 为 1，表示该页使用；bit 为 0，表示该页未用。在需要分配内存时，bootmem 逐位扫描位图，知道找到一个空间足够大的连续页的位置。这种每次分配都需要从头扫描的方式效率不高，因此内核初始化结束后就转用伙伴系统（连同 slab、slub 或 slob 分配器）。

NUMA 内存体系中，每个节点都要初始化一个 bootmem 分配器。

开始时位图中的 bit 都是 1，根据 BIOS 提供的可用内存区的列表，释放所有可用的内存页。由于 bootmem 需要一些内存页保存位图，必须先调用 reserve_bootmem 分配这些内存页（ACPI 数据和 SMP 启动时的配置也是通过 reserve_bootmem 保存的）。

在停用 bootmem 时，需要扫描位图释放每个未使用的页，释放完后，位图所在的页也要释放。

### [SWIOTLB](https://blog.csdn.net/liuhangtiant/article/details/87825466)

龙芯 3 号的访存能力是 48 位，而龙芯的顶级 IO 总线是 40 位的，部分 PCI 设备的总线只有 32 位，如果系统为其分配了超过 40 位或 32 位总线寻址能力的地址，那么这些设备就不能访问对应的 DMA 数据，为了**让访存能力有限的 IO 设备能够访问任意的 DMA 空间**，就必须**在硬件上设置一个 DMA 地址 - 物理地址的映射表**，或者由内核在设备可访问的地址范围预先准备一款内存做中转站——SWIOTLB。
