# 杂项

## page coloring

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

If addr is **NULL**, then the kernel chooses **the (page-aligned) address** at which to create the mapping.  If addr is not NULL, then the  kernel takes  it  as  a hint about where to place the mapping;

The  contents  of a file mapping are initialized using **length** bytes starting at offset **offset** in the file (or other object) referred to by the file descriptor **fd**.

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

Seabios 中有实现两者是怎样切换的，而 Seabios.md 中详细分析了其实现。

### 守护进程

Linux Daemon（守护进程）是运行在后台的一种特殊进程。它独立于控制终端并且周期性地执行某种任务或等待处理某些发生的事件。它不需要用户输入就能运行而且提供某种服务，不是对整个系统就是对某个用户程序提供服务。Linux 系统的大多数服务器就是通过守护进程实现的。常见的守护进程包括系统日志进程 syslogd、 web 服务器 httpd、邮件服务器 sendmail 和数据库服务器 mysqld 等。

守护进程一般在系统启动时开始运行，除非强行终止，否则直到系统关机都保持运行。守护进程经常以超级用户（root）权限运行，因为它们要使用特殊的端口（1-1024）或访问某些特殊的资源。

一个守护进程的父进程是 init 进程，因为它真正的父进程在 fork 出子进程后就先于子进程 exit 退出了，所以它是一个**由 init 继承的孤儿进程**。守护进程是非交互式程序，没有控制终端，所以任何输出，无论是向标准输出设备 stdout 还是标准出错设备 stderr 的输出都需要特殊处理。

守护进程的名称通常以 d 结尾，比如 sshd、xinetd、crond 等

### sockets

A **network socket** is a software structure within a [network node](https://en.wikipedia.org/wiki/Node_(networking)) of a [computer network](https://en.wikipedia.org/wiki/Computer_network) that serves as an endpoint for sending and receiving data across the network. The structure and properties of a socket are defined by an [application programming interface](https://en.wikipedia.org/wiki/Application_programming_interface) (API) for the networking architecture. Sockets are created only during the lifetime of a [process](https://en.wikipedia.org/wiki/Process_(computing)) of an application running in the node.

### level-triggered vs edge-triggered

- level-triggered: get a list of every file descriptor you’re interested in that is readable.
- edge-triggered: get notifications every time a file descriptor becomes readable.

### 字符设备和块设备

系统中能够随机（不需要按顺序）访问固定大小数据片（chunks）的设备被称作块设备，这些数据片就称作块。最常见的块设备是硬盘，除此以外，还有软盘驱动器、CD-ROM 驱动器和闪存等等许多其他块设备。注意，它们都是以安装文件系统的方式使用的——这也是块设备的一般访问方式。

另一种基本的设备类型是字符设备。字符设备按照字符流的方式被有序访问，像串口和键盘就都属于字符设备。如果一个硬件设备是以字符流的方式被访问的话，那就应该将它归于字符设备；反过来，如果一个设备是随机（无序的）访问的，那么它就属于块设备。
