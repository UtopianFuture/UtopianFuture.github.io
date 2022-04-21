## Seabios in QEMU

### 执行过程

![seabios.png](https://github.com/UtopianFuture/UtopianFuture.github.io/blob/master/image/seabios.png?raw=true)

SeaBIOS 是一个 16bit 的 x86 BIOS 的开源实现，常用于 QEMU 等仿真器中使用。本文将结合[SeaBIOS Execution and code flow](https://www.seabios.org/Execution_and_code_flow)和[SeaBIOS 的源码](https://github.com/coreboot/seabios/)对 SeaBIOS 的全过程进行简单分析。需要注意，本文不是深入的分析，对于一些比较复杂和繁琐的部分直接跳过了。

从整体角度出发，SeaBIOS 包含四个阶段。

- 加电自检（Power On Self Test, POST)
- 引导（Boot）
- 运行时（Main runtime phase）
- 继续运行和重启（Resume and reboot）

### 加电自检阶段

QEMU 仿真器会将 SeaBIOS 加电自检阶段的第一条指令放置在`F000:FFF0`的位置。当 QEMU 仿真器启动之后，将会执行这一条指令。为什么放置在 F000:FFF0 位置呢？

```plain
+------------------+  <- 0xFFFFFFFF (4GB)
|      32-bit      |
|  memory mapped   |
|     devices      |
|                  |
/\/\/\/\/\/\/\/\/\/\

/\/\/\/\/\/\/\/\/\/\
|                  |
|      Unused      |
|                  |
+------------------+  <- depends on amount of RAM
|                  |
|                  |
| Extended Memory  |
|                  |
|                  |
+------------------+  <- 0x00100000 (1MB)
|     BIOS ROM     |
+------------------+  <- 0x000F0000 (960KB)
|  16-bit devices, |
|  expansion ROMs  |
+------------------+  <- 0x000C0000 (768KB)
|   VGA Display    |
+------------------+  <- 0x000A0000 (640KB)
|                  |
|    Low Memory    |
|                  |
+------------------+  <- 0x00000000
```

这是从 MIT 6.828 Lab1 当中截取下来的一个图。PC 的物理地址空间根据人们长期实践下来的约定，往往被按照上面的方式来划分。而 BIOS 固件，将会被放置在 BIOS ROM 的区域当中。这一块区域的范围是`F000:0000～F000:FFFF`。刚好是 64KB。而为什么放在这 64KB 区域的顶部？因为英特尔设计 8088 处理器的时候，就设计成了上电启动时，**将指令指针寄存器 IP 设置为 0xFFF0**，将代码段寄存器 CS 设置为 0xF000（指向 BIOS ROM 这一段）。所以，将第一条指令放在 F000:FFF0 位置，启动后它将立刻被执行。（实际上，这么说不是很严谨。其实 CPU 是从 0xFFFFFFF0，也就是 32bit 地址线可寻址空间的最后 16 字节位置开始执行代码的。在刚开机的时候虽然 CS 为 0xF000，但是它的段基址实际上是 0xFFFF0000，而不是按照*16 方法计算出来的 0xF0000。这一点的原因在后面的“关于 `make_bios_writable`”部分介绍）。也就是说，处理器初始化后 CS 段的 base 值为 `FFFF0000H`, IP 为 `0000FFF0H`，第一条指令将会在物理地址 `FFFFFFF0H` 上，ROM 设备会映射到物理地址空间中的这个位置上。然而在 bus 的解码上 `FFFFFFF0H` 这个地址还是会被转发到 `F000:FFF0H` 上。当跳转到 `F000:E05B` 后，CS.base 会接着被刷新为 `F0000H`(F000H << 4)，这是真正的是模式基地址。

这个 SeaBIOS 里的“第一条指令”就在 romlayout.S 的 `reset_vector` 中。

```assembly
reset_vector:
        ljmpw $SEG_BIOS, $entry_post
```

通过这条 `jmp` 指令，程序跳转到 CS:IP 为 `SEGBIOS:SEGBIOS:entry_post` 的位置。这两个是两个常量，分别为 `0xF000` 和 `0xE05B`。而 `entry_post` 是如下定义的：

```assembly
        ORG 0xe05b
entry_post:
        cmpl $0, %cs:HaveRunPost                // Check for resume/reboot
        jnz entry_resume
        ENTRY_INTO32 _cfunc32flat_handle_post   // Normal entry point
```

`entry_post` 中，首先通过一条 `cmpl` 指令，**判断是否已经经历过 POST 阶段**。如果已经经历过该阶段，意味着当前不应该重新进行，而应该进入继续运行(Resume)。所以，如果 `%cs:HaveRunPost` 不为 0，意味着已经经历过 POST 阶段，则进入继续运行 `entry_resume`，具体的过程在第四个阶段会介绍。而对于其他情况，就会进入 `handle_post` 函数。

`handle_post` 是一个 32bit 的 C 函数，在 post.c 文件中。需要注意，**此时机器是在 16bit 实模式下的**。为了调用 32bit 的 C 函数，通过 `ENTRY_INTO32`，**先将机器切换到保护模式**，然后才能调用。

我们进一步分析 `ENTRY_INTO32` 是如何实现的。`ENTRY_INTO32` 是一个宏，**用于将机器切换到保护模式**，然后调用一个 C 函数。

```assembly
        .macro ENTRY_INTO32 cfunc
        xorw %dx, %dx
        movw %dx, %ss
        movl $ BUILD_STACK_ADDR , %esp
        movl $ \cfunc , %edx
        jmp transition32
        .endm
```

可以看到，这里的 cfunc 是一个指向 C 编译器生成的函数的 Label，被传递到 edx 寄存器中。此外，`ENTRY_INTO32` 还会设置好堆栈段寄存器 SS 为 0，也就是将 BIOS ROM 程序函数调用中的堆栈保存在 Low Memory 区域。

`transition32` 将使用到 edx 寄存器里面的值。下面是 `transition32` 的实现：

```assembly
transition32:
        // Disable irqs (and clear direction flag)
        cli
        cld

        // Disable nmi
        movl %eax, %ecx
        movl $CMOS_RESET_CODE|NMI_DISABLE_BIT, %eax
        outb %al, $PORT_CMOS_INDEX
        inb $PORT_CMOS_DATA, %al

        // enable a20
        inb $PORT_A20, %al
        orb $A20_ENABLE_BIT, %al
        outb %al, $PORT_A20
        movl %ecx, %eax

transition32_nmi_off:
        // Set segment descriptors
        lidtw %cs:pmode_IDT_info
        lgdtw %cs:rombios32_gdt_48

        // Enable protected mode
        movl %cr0, %ecx
        andl $~(CR0_PG|CR0_CD|CR0_NW), %ecx
        orl $CR0_PE, %ecx
        movl %ecx, %cr0

        // start 32bit protected mode code
        ljmpl $SEG32_MODE32_CS, $(BUILD_BIOS_ADDR + 1f)

        .code32
        // init data segments
1:      movl $SEG32_MODE32_DS, %ecx
        movw %cx, %ds
        movw %cx, %es
        movw %cx, %ss
        movw %cx, %fs
        movw %cx, %gs

        jmpl *%edx
```

首先，先屏蔽中断，并清空方向标志位。然后通过向一个端口写入 `NMI_DISABLE_BIT` 的方式屏蔽 NMI（这里具体的不探究）。然后这一点是非常重要的——启动 A20 Gate。

### 启动 A20 总线

首先将介绍 A20 总线。我们知道，8086/8088 系列的 CPU，在实模式下，按照**段地址:偏移地址**的方式来寻址。这种方式可以访问的最大内存地址为 `0xFFFF:0xFFFF`，转换为物理地址 `0x10FFEF`。而这个物理地址是 21bit 的，所以为了表示出这个最大的物理地址，至少需要 21 根地址线才能表示。

然而，8086/8088 地址总线只有 20 根。所以在 8086/8088 系列的 CPU 上，比如如果需要寻址 0x10FFEF，则会因为地址线数目不够，被截断成 0x0FFEF。再举个例子，如果要访问物理地址 0x100000，则会被截断成 0x00000。第 21 位会被省略。也就是说地址不断增长，直到 0x100000 的时候，会回到“0x00000”的实际物理地址。这个现象被称为“回环”现象。这种地址越界而产生回环的行为被认为是合法的，以至于当时很多程序利用到了这个特性（比如假定访问 0x100000 就是访问 0x00000）。

然而，80286 到来了。80286 具有 24 根地址总线。对于为 8086 处理器设计的程序，设计者可能假定第 21 位会被省略。然而，在具有 24 根地址总线的 80286 机器上，则没有这个特性了。于是，如果不做出一些调整。地址总线数目的增加，可能导致向下兼容性被破坏。于是，当时的工程师们想了一个办法，设计了 A20 总线，用**来控制第 21 位（如果最低位编号为 0，那第 21 位的编号就是 20）及更高位是否有效**。实际上可以想象成，第 21 位（及更高位）都接入了一个和 A20 总线的与门。当 A20 总线为 1，则高位保持原来的。当 A20 总线为 0，则高位就始终为 0。这样，当 A20 总线为 0 的时候，8086/8088 的回环现象将会保持。这么一来旧程序就可以兼容了。

控制 A20 总线的端口被称为 A20-Gate。使用 in/out 指令控制，即可控制 A20 总线是否打开。A20 Gate 是 0x92 端口的第二个 bit。先获得 0x92 端口的值并存放在 al 寄存器中，然后通过 or 将该寄存器的第二个 bit 设置为 1。然后再将 al 的值写入 0x92 端口即可。这就是上面的 enable a20 部分的原理。

### 从实模式进入 32 位保护模式

在 16bit 实模式下，最多访问 20 根地址线。且段内偏移不能超过 64KB（16 位）。而 32 位保护模式下，则没有了最多访问 20 根地址线的限制，且段内偏移可以达到 4GB（32 位）。

此外，保护模式最大的特点是：原先的段基地址:段偏移的寻址方式，变为段选择符:段偏移的寻址方式。这里不再继续介绍保护模式，因为篇幅有限。有需要者可以自己查阅资料。

首先，前两条指令将设定中断描述符表和全局描述符表。我们重点关注全局描述符表。

```C
// GDT
u64 rombios32_gdt[] VARFSEG __aligned(8) = {
    // First entry can't be used.
    0x0000000000000000LL,
    // 32 bit flat code segment (SEG32_MODE32_CS)
    GDT_GRANLIMIT(0xffffffff) | GDT_CODE | GDT_B,
    // 32 bit flat data segment (SEG32_MODE32_DS)
    GDT_GRANLIMIT(0xffffffff) | GDT_DATA | GDT_B,
    // 16 bit code segment base=0xf0000 limit=0xffff (SEG32_MODE16_CS)
    GDT_LIMIT(BUILD_BIOS_SIZE-1) | GDT_CODE | GDT_BASE(BUILD_BIOS_ADDR),
    // 16 bit data segment base=0x0 limit=0xffff (SEG32_MODE16_DS)
    GDT_LIMIT(0x0ffff) | GDT_DATA,
    // 16 bit code segment base=0xf0000 limit=0xffffffff (SEG32_MODE16BIG_CS)
    GDT_GRANLIMIT(0xffffffff) | GDT_CODE | GDT_BASE(BUILD_BIOS_ADDR),
    // 16 bit data segment base=0 limit=0xffffffff (SEG32_MODE16BIG_DS)
    GDT_GRANLIMIT(0xffffffff) | GDT_DATA,
};

// GDT descriptor
struct descloc_s rombios32_gdt_48 VARFSEG = {
    .length = sizeof(rombios32_gdt) - 1,
    .addr = (u32)rombios32_gdt,
};
```

先看（从第 0 项开始的）第 1 项

```C
GDT_GRANLIMIT(0xffffffff) | GDT_CODE | GDT_B
```

这个 GDT 项对应的 32 位段基地址是 `0x00000000`。而长度限制 limit 为 `0xFFFFFFFF`。并且在 32bit 保护模式下偏移量也是 32bit 的。这意味着这个 GDT 项可以映射到整个物理地址空间（所以叫“Flat” code segment）。

然后 Enter protected mode 那里则是进入保护模式的经典方法。控制寄存器 CR0 的最低位（PE 位）如果为 1，则表示处理器处于保护模式，否则则处于实模式。我们重点关注

```assembly
orl $CR0_PE, %ecx
```

这一个指令将 PE 位置为 1。然后再次写入 cr0 寄存器。处理器即进入保护模式。下一条指令非常奇怪：

```assembly
ljmpl $SEG32_MODE32_CS, $(BUILD_BIOS_ADDR + 1f)
```

这里的 1f 要区分清楚。指的是前方第一个标签为“1”的位置，而不是代表十六进制数 0x1F。下一个标签“1”就是这个指令的下一条。所以，看起来这个跳转是没有价值的。实际上，在 cr0 寄存器被设定好之前，下一条指令已经被放入流水线。而再放入的时候这条指令还是在实模式下的。所以这个 `ljmp` 指令是**为了清空流水线，确保下一条指令在保护模式下执行**。

现在，我们已经在保护模式了！在这里，程序进行了这些操作：

```Assembly
        .code32
        // init data segments
1:      movl $SEG32_MODE32_DS, %ecx
        movw %cx, %ds
        movw %cx, %es
        movw %cx, %ss
        movw %cx, %fs
        movw %cx, %gs
```

这里实际上含义很明确。就是初始化 ds、es、ss、fs、gs 寄存器，将数据段的段选择器传递给它们即可。

然后，就交给 C 语言编译器编译产生的代码啦！通过一个跳转指令

```Assembly
        jmpl *%edx
```

就完成了跳转。

### 从 32 位保护模式回到实模式

虽然没有用到这个，但是这里顺便分析一下从 32 位保护模式回到实模式的方法。SeaBIOS 是这样实现的：

```assembly
transition16:
        // Reset data segment limits
        movl $SEG32_MODE16_DS, %ecx
        movw %cx, %ds
        movw %cx, %es
        movw %cx, %ss
        movw %cx, %fs
        movw %cx, %gs

        // Jump to 16bit mode
        ljmpw $SEG32_MODE16_CS, $1f

        .code16
        // Disable protected mode
1:      movl %cr0, %ecx
        andl $~CR0_PE, %ecx
        movl %ecx, %cr0

        // far jump to flush CPU queue after transition to real mode
        ljmpw $SEG_BIOS, $2f

        // restore IDT to normal real-mode defaults
2:      lidtw %cs:rmode_IDT_info

        // Clear segment registers
        xorw %cx, %cx
        movw %cx, %fs
        movw %cx, %gs
        movw %cx, %es
        movw %cx, %ds
        movw %cx, %ss  // Assume stack is in segment 0

        jmpl *%edx
```

这里需要注意一些地方。

#### 恢复段描述符高速缓冲寄存器

首先要了解段描述符高速缓冲寄存器（Descriptor cache register）。我们知道，GDT 是存在存储器当中的。每一次在存储器中存取数据的时候，CPU 需要先寻址，而寻址需要先根据段选择子计算段基址。这个计算又需要对存储器中的 GDT 做一次读取。于是就多了一次存储器访问，大大影响程序执行性能。所以，Intel 提供的解决方案是为每一个段寄存器配备一个段描述符高速缓冲寄存器。当段寄存器被重新赋值的时候，就根据段选择子，从存储器中读取 GDT 中的项，然后将段基址以及其他的段描述符信息存储在这个段寄存器对应的段描述符高速缓冲寄存器中。下一次寻址的时候，就可以直接查询这个段描述符高速缓冲寄存器。性能好了很多。

然而这个寄存器，在实模式下仍然是有效的。也就是实模式下仍然会查询该寄存器以获取段基址（其实值就是当前段寄存器的值*16）。具体表格如下（[出处](https://zhuanlan.zhihu.com/p/31972482)）

![img](https://pic2.zhimg.com/80/v2-2a42a2716b6eb3d200b0dcc27887353d_720w.jpg)

这就给我们一个需要注意的地方。我们在从 32 位保护模式切换到实模式的时候，要先把段描述符高速缓冲寄存器的内容恢复成实模式下的状态，然后再切换回去。因为在实模式下，我们无法再像保护模式中那样设定寄存器中的值了。

如何恢复呢？对于非代码段，其实很简单。因为我们段基地址全部初始化成 0，其他段界限等也都一样，所以只需要在 GDT 中新建一个表项目，也就是 SeaBIOS 源码中打*的这一项，然后将各个数据段寄存器设置成它即可。

```C
// GDT
u64 rombios32_gdt[] VARFSEG __aligned(8) = {
    // First entry can't be used.
    0x0000000000000000LL,
    // 32 bit flat code segment (SEG32_MODE32_CS)
    GDT_GRANLIMIT(0xffffffff) | GDT_CODE | GDT_B,
    // 32 bit flat data segment (SEG32_MODE32_DS)
    GDT_GRANLIMIT(0xffffffff) | GDT_DATA | GDT_B,
    // 16 bit code segment base=0xf0000 limit=0xffff (SEG32_MODE16_CS)
    GDT_LIMIT(BUILD_BIOS_SIZE-1) | GDT_CODE | GDT_BASE(BUILD_BIOS_ADDR),
    // 16 bit data segment base=0x0 limit=0xffff (SEG32_MODE16_DS)
  	// ************************************************
    GDT_LIMIT(0x0ffff) | GDT_DATA,
    // 16 bit code segment base=0xf0000 limit=0xffffffff (SEG32_MODE16BIG_CS)
    GDT_GRANLIMIT(0xffffffff) | GDT_CODE | GDT_BASE(BUILD_BIOS_ADDR),
    // 16 bit data segment base=0 limit=0xffffffff (SEG32_MODE16BIG_DS)
    GDT_GRANLIMIT(0xffffffff) | GDT_DATA,
};
```

具体实现就对应了上面的“Reset data segment limits”部分的代码。

#### 代码段寄存器的恢复

我们知道 CS 寄存器不能通过 mov 指令修改，于是就不能通过 mov 指令来恢复 CS 寄存器对应的段描述符高速缓冲寄存器了。修改 CS 的唯一方法是通过 JMP 指令。

SeaBIOS 的实现是，创建了一个 `SEG32_MODE16_CS` 表项。然后通过一个 `ljmp` 指令跳转，来恢复 CS 寄存器。

```assembly
ljmpw $SEG32_MODE16_CS, $1f
```

#### 关闭 PE

在上面代码的 “Disable protected mode” 部分，将 CR0 寄存器的 PE 位置 0 即刻关闭保护模式。然后，和前面一样，通过一个 ljmp 刷新流水线。确保后面的指令都是在实模式中运行。

### 进入 handle_post 函数

通过 `ENTRY_INTO32 _cfunc32flat_handle_post` 语句，即先进入保护模式，然后完成对 C 函数 `handle_post` 的调用。

`handle_post` 函数的定义如下：

```C
// Entry point for Power On Self Test (POST) - the BIOS initilization
// phase.  This function makes the memory at 0xc0000-0xfffff
// read/writable and then calls dopost().
void VISIBLE32FLAT
handle_post(void)
{
    if (!CONFIG_QEMU && !CONFIG_COREBOOT)
        return;

    serial_debug_preinit();
    debug_banner();

    // Check if we are running under Xen.
    xen_preinit();

    // Allow writes to modify bios area (0xf0000)
    make_bios_writable();

    // Now that memory is read/writable - start post process.
    dopost();
}
```

首先是一些基本的准备工作，比如启动串口调试等等，这些细节我们就忽略了。从 `make_bios_writable` 开始。

#### 关于 make_bios_writable

该函数的作用是允许更改 RAM 中的 BIOS ROM 区域。在介绍 `make_bios_writable` 之前，首先对 Shadow RAM 做一些介绍。实际上，尽管在启动的时候，是从 `F000:FFF0` 加载第一条指令的，你可能会觉得在启动的时候代码段段基址是 `0xF0000`。其实，并不是这样的。在计算机启动的时候，代码段段基地址实际上是是 `0xFFFF0000`（这里就不符合那个乘 16 的计算方式了）。笔者猜测这一一点的实现方式是通过段描述符高速缓冲寄存器实现的（实模式下也是通过查询这个寄存器来获得段基址的），开机的时候代码段的对应基址项被设置成 `0xFFFF0000`。

为什么从这里开始呢？我们知道 BIOS 是存储在 ROM 当中的。而 Intel 有一个习惯，**将 BIOS 固件代码从 ROM 中映射到可寻址地址的末端（最后 64K 内）**。这里的“映射”，并不是复制，而是当读取这个地址的时候，就直接读取 ROM 存储器当中的值。在 8086 时期，可寻址的地址为 0x00000-0xFFFFF，所以说它的“末端”确实是从我们理解的 0xF0000 开始的。所以在 8086 时期，硬件设备会将原本存储于 ROM 的 BIOS 映射到 F000:0000-F000:FFFF。然而，到了后面有 32 根地址线，实际上末端应该是 `0xFFFF0000-0xFFFFFFFF` 这一部分。此时的计算机，实际上是将 BIOS 固件代码映射到 `0xFFFF0000-0xFFFFFFFF` 中。

所以，实际上 SeaBIOS 的这一行指令：

```assembly
reset_vector:
        ljmpw $SEG_BIOS, $entry_post
```

是位于 `0xFFFFFFF0` 的物理地址位置的。但是我们注意到这是一个 Long jump 指令，这个指令会使 CPU 重新计算代码段寄存器，原本的 `0xFFFF0000` 基地址，在这一个指令执行之后，就会变成符合乘 16 计算方式的 0xF0000！

读者可能会想，这不就出问题了吗？32 根地址线的 PC，BIOS 固件明明在最后呀！实际上，为了保持向前兼容性，机器启动的时候会自动**将 ROM 的 BIOS 复制到 RAM 的 BIOS ROM 区域当中**。所以，通过 `ljmpw` 指令跳转之后，因为已经复制了，在 RAM 当中也有 BIOS 固件代码。所以是不会有问题的。

**这个复制高地址处的 ROM 到低地址处的过程被称为 Shadow RAM 技术**。然而，在这个过程后，这段内存会被保护起来，无法进行写入。`make_bios_writable` 函数就用于让这段内存可写，从而便于更改一些静态分配的全局变量值。

这里的分析应该是有问题的，并不是自动将 ROM 的 BIOS 复制到 RAM 的 BIOS ROM 区域当中，而就是通过 `make_bios_writable` 来复制的，我们看看代码：

```c
// Enable shadowing and copy bios.
static void
__make_bios_writable_intel(u16 bdf, u32 pam0)
{
    // Read in current PAM settings from pci config space
    union pamdata_u pamdata;
    pamdata.data32[0] = pci_config_readl(bdf, ALIGN_DOWN(pam0, 4));
    pamdata.data32[1] = pci_config_readl(bdf, ALIGN_DOWN(pam0, 4) + 4);
    u8 *pam = &pamdata.data8[pam0 & 0x03];

    // Make ram from 0xc0000-0xf0000 writable
    int i;
    for (i=0; i<6; i++)
        pam[i + 1] = 0x33;

    // Make ram from 0xf0000-0x100000 writable
    int ram_present = pam[0] & 0x10;
    pam[0] = 0x30;

    // Write PAM settings back to pci config space
    pci_config_writel(bdf, ALIGN_DOWN(pam0, 4), pamdata.data32[0]);
    pci_config_writel(bdf, ALIGN_DOWN(pam0, 4) + 4, pamdata.data32[1]);

    if (!ram_present)
        // Copy bios.
        memcpy(VSYMBOL(code32flat_start)
               , VSYMBOL(code32flat_start) + BIOS_SRC_OFFSET
               , SYMBOL(code32flat_end) - SYMBOL(code32flat_start));
}
```

从代码中清晰的看到，Seabios 通过 `memcpy`  将 `VSYMBOL(code32flat_start) + BIOS_SRC_OFFSET` 的 bios 复制到 `VSYMBOL(code32flat_start)`。而前面的 pam 操作则是对 `0xc0000 ~ 0xfffff` 内存空间的读写操作的重定向定义。

#### 进入 dopost

刚才已经做好了准备。然后，就可以进入 `dopost` 函数了。这个函数是 POST 过程的主体。`depost` 会调用 `maininit` 函数。

### maininit 过程

`dopost` 函数定义如下

```c
// Setup for code relocation and then relocate.
void VISIBLE32INIT
dopost(void)
{
    code_mutable_preinit();

    // Detect ram and setup internal malloc.
    qemu_preinit();
    coreboot_preinit();
    malloc_preinit();

    // Relocate initialization code and call maininit().
    reloc_preinit(maininit, NULL);
}
```

首先，看 `code_mutable_preinit`。

```C
void
code_mutable_preinit(void)
{
    if (HaveRunPost)
        // Already run
        return;
    // Setup reset-vector entry point (controls legacy reboots).
    rtc_write(CMOS_RESET_CODE, 0);
    barrier();
    HaveRunPost = 1;
    barrier();
}
```

这一段的核心是将 `HaveRunPost` 设置为 1。可以看出，`HaveRunPost` 实际上相当于一个全局变量，在 BIOS ROM 中实际上是被初始化为 0 的。然后将 ROM 映射到 RAM 中的 BIOS ROM 区域之后，通过 `make_bios_writable`，**使得这一段 RAM 区域可写**，然后才能更改 `HaveRunPost` 的值。

为了初始化内存，SeaBIOS 实现了自己的 `malloc` 函数。通过 `malloc_preinit` 进行初始化，然后通过 `reloc_preinit` 函数将自身代码进行重定位。这些步骤有非常多的工程细节，就忽略不看了。接下来从重要的函数：`maininit` 开始分析。

```C
// Main setup code.
static void
maininit(void)
{
    // Initialize internal interfaces.
    interface_init();

    // Setup platform devices.
    platform_hardware_setup();

    // Start hardware initialization (if threads allowed during optionroms)
    if (threads_during_optionroms())
        device_hardware_setup();

    // Run vga option rom
    vgarom_setup();
    sercon_setup();
    enable_vga_console();

    // Do hardware initialization (if running synchronously)
    if (!threads_during_optionroms()) {
        device_hardware_setup();
        wait_threads();
    }

    // Run option roms
    optionrom_setup();

    // Allow user to modify overall boot order.
    interactive_bootmenu();
    wait_threads();

    // Prepare for boot.
    prepareboot();

    // Write protect bios memory.
    make_bios_readonly();

    // Invoke int 19 to start boot process.
    startBoot();
}
```

`maininit` 函数中，有很多重要的部分。我们来看一看。

首先是 `interface_init` 函数。

```C
void
interface_init(void)
{
    // Running at new code address - do code relocation fixups
    malloc_init();

    // Setup romfile items.
    qemu_cfg_init();
    coreboot_cbfs_init();
    multiboot_init();

    // Setup ivt/bda/ebda
    ivt_init();
    bda_init();

    // Other interfaces
    boot_init();
    bios32_init();
    pmm_init();
    pnp_init();
    kbd_init();
    mouse_init();
}
```

这个函数用于加载内部的一些接口。下面是其步骤。

#### 初始化中断向量表 IVT

中断向量表（Interrupt Vector Table）是一张在实模式下使用的表。顾名思义，这个表将中断号映射到中断过程的一个列表。中断向量表必须存储在低地址区域（也就是从 0x00000000）开始，大小一般是 0x400 字节，是一块由很多个项组成的连续的内存空间。每一项，就对应了一个中断，如下所示（[出处](https://wiki.osdev.org/Interrupt_Vector_Table)）：

```plain
 +-----------+-----------+
 |  Segment  |  Offset   |
 +-----------+-----------+
 4           2           0
```

每一项被称为中断向量（Interrupt Vector）。可以看出每一项占据 4 个字节，前两个字节是段，后两个字节是偏移。而这一项实际上就对应了一个中断服务处理程序的入口地址（段：偏移），只需要修改这个表里的地址，即可更换中断处理程序。

而每一个中断都有一个中断号码，号码就是中断向量的索引。比如这个表里前四个字节对应的项，中断号就是 0，然后 4-8 字节对应的项中断号就是 1。可以很容易地看出，**中断号*4 为首地址的 4 字节内存区域就对应了该中断号对应的中断处理程序位置**。

SeaBIOS 中，`ivt_init` 就是初始化一些中断。我们看实现。

```C
static void
ivt_init(void)
{
    dprintf(3, "init ivt\n");

    // Initialize all vectors to the default handler.
    int i;
    for (i=0; i<256; i++)
        SET_IVT(i, FUNC16(entry_iret_official));

    // Initialize all hw vectors to a default hw handler.
    for (i=BIOS_HWIRQ0_VECTOR; i<BIOS_HWIRQ0_VECTOR+8; i++)
        SET_IVT(i, FUNC16(entry_hwpic1));
    for (i=BIOS_HWIRQ8_VECTOR; i<BIOS_HWIRQ8_VECTOR+8; i++)
        SET_IVT(i, FUNC16(entry_hwpic2));

    // Initialize software handlers.
    SET_IVT(0x02, FUNC16(entry_02));
    SET_IVT(0x05, FUNC16(entry_05));
    SET_IVT(0x10, FUNC16(entry_10));
    SET_IVT(0x11, FUNC16(entry_11));
    SET_IVT(0x12, FUNC16(entry_12));
    SET_IVT(0x13, FUNC16(entry_13_official));
    SET_IVT(0x14, FUNC16(entry_14));
    SET_IVT(0x15, FUNC16(entry_15_official));
    SET_IVT(0x16, FUNC16(entry_16));
    SET_IVT(0x17, FUNC16(entry_17));
    SET_IVT(0x18, FUNC16(entry_18));
    SET_IVT(0x19, FUNC16(entry_19_official));
    SET_IVT(0x1a, FUNC16(entry_1a_official));
    SET_IVT(0x40, FUNC16(entry_40));

    // INT 60h-66h reserved for user interrupt
    for (i=0x60; i<=0x66; i++)
        SET_IVT(i, SEGOFF(0, 0));

    // set vector 0x79 to zero
    // this is used by 'gardian angel' protection system
    SET_IVT(0x79, SEGOFF(0, 0));
}
```

首先，`ivt_init` 将所有中断初始化到一个空处理函数（相当于只有一条 return 语句）。将所有硬中断也都初始化到一个默认处理函数。然后是一些默认的中断处理程序。比如经典的 VGA 服务 `INT 10H`，经典的磁盘服务 `INT 13H` 等等。对于每一项的实现，这里不多介绍。

#### 实模式下发起中断

因为历史原因，操作系统需要兼容之前的处理器，所以当系统刚刚启动的时候，CPU 是处于实模式的，这个时候和原来的模式是兼容的，只不过这个过程很短暂，很快切入到了保护模式而已。

之前分析内核时一直认为内核的入口是 `start_kernel`，其实这只是保护模式的入口，内核刚刚启动时也是处于实模式，`arch/x86/boot` 中的代码就是实模式的代码，我们可以看看主要执行流程：

```c
void main(void)
{
	/* First, copy the boot header into the "zeropage" */
	copy_boot_params();

	/* Initialize the early-boot console */
	console_init();
	if (cmdline_find_option_bool("debug"))
		puts("early console in setup code\n");

	/* End of heap check */
	init_heap();

	/* Make sure we have all the proper CPU support */
	if (validate_cpu()) {
		puts("Unable to boot - please use a kernel appropriate "
		     "for your CPU.\n");
		die();
	}

	/* Tell the BIOS what CPU mode we intend to run in. */
	set_bios_mode();

	/* Detect memory layout */
	detect_memory();

	/* Set keyboard repeat rate (why?) and query the lock flags */
	keyboard_init();

	/* Query Intel SpeedStep (IST) information */
	query_ist();

	/* Query APM information */
#if defined(CONFIG_APM) || defined(CONFIG_APM_MODULE)
	query_apm_bios();
#endif

	/* Query EDD information */
#if defined(CONFIG_EDD) || defined(CONFIG_EDD_MODULE)
	query_edd();
#endif

	/* Set the video mode */
	// set_videio();

	/* Do the last things and invoke protected mode */
	go_to_protected_mode();
}
```

这里做一些初始化的工作， 如 `detect_memory` 就是通过 `int 0x15` 号中断获取 e820 的内存信息，然后 `go_to_protected_mode` 再进入保护模式。

之前我一直认为 IVT 是定义在内核中的，但其实不是，就是上文分析的定义在 Seabios 中，IVT 和 IDT 的中断向量是不一样的，即每个中断号对应的处理函数不同，这点不要搞混了。

在实模式下同样通过 `intcall` 来发起中断，这里我们分析一下 `int 0x15` 号中断。在此之前我只知道内核通过 e820 来获取 bios 探测到的内存信息，但具体过程不理解。这里详细分析一下。

物理内存在硬件上可能连续也可能不连续，连续的物理内存就是一整块的区域，而不连续的两块内存中间存在的空间称为内存空洞，或者称为 Hole。 这些 Hole 要么是系统预留给某些特定的硬件设备使用，要么没有真实的物理内存。 由于物理内存的布局关系，物理内存会被分作很多区块，BIOS 在启动过程中探测到这些 物理内存区域之后，使用 Entry 为单位维护内存区块， Entry 的结构布局如下:

```
Offset  Size    Description
00h     QWORD   base address
08h     QWORD   length in bytes
10h     DWORD   type of address range
```

Entry 结构中，”Base address” 字段表示一个物理内存区域的起始物理地址，其长度 是一个 64 bit 的数据； “length” 字段表示一个物理内存区域的长度，其长度是一个 64 bit 的数据；“type” 字段表示物理内存区域的类型，其长度是一个 32 bit 的数据。BIOS 支持识别的物理内存类型如下表：

```
Values for System Memory Map address type:
01h    memory, available to OS
02h    reserved, not available (e.g. system ROM, memory-mapped device)
03h    ACPI Reclaim Memory (usable by OS after reading ACPI tables)
04h    ACPI NVS Memory (OS is required to save this memory between NVS sessions)
other  not defined yet -- treat as Reserved
```

在 BIOS 识别物理内存类型中，01 代表可用的物理内存；02 代表预留空间，这些空间可能为系统的 ROM/IOMEM 预留；03 表示 ACPI 可回收内存。下面就是内核请求内存布局的实现，`boot_params` 是一个全局变量，表示所有从 bios 中获取的信息，`e820entry` 就是上文的 Entry 结构。

```c
static int detect_memory_e820(void)
{
	int count = 0;
	struct biosregs ireg, oreg;
	struct e820entry *desc = boot_params.e820_map;
	static struct e820entry buf; /* static so it is zeroed */

	initregs(&ireg);
	ireg.ax  = 0xe820;
	ireg.cx  = sizeof buf;
	ireg.edx = SMAP;
	ireg.di  = (size_t)&buf;

	/*
	 * Note: at least one BIOS is known which assumes that the
	 * buffer pointed to by one e820 call is the same one as
	 * the previous call, and only changes modified fields.  Therefore,
	 * we use a temporary buffer and copy the results entry by entry.
	 *
	 * This routine deliberately does not try to account for
	 * ACPI 3+ extended attributes.  This is because there are
	 * BIOSes in the field which report zero for the valid bit for
	 * all ranges, and we don't currently make any use of the
	 * other attribute bits.  Revisit this if we see the extended
	 * attribute bits deployed in a meaningful way in the future.
	 */

	do {
		intcall(0x15, &ireg, &oreg);
		ireg.ebx = oreg.ebx; /* for next iteration... */

		/* BIOSes which terminate the chain with CF = 1 as opposed
		   to %ebx = 0 don't always report the SMAP signature on
		   the final, failing, probe. */
		if (oreg.eflags & X86_EFLAGS_CF)
			break;

		/* Some BIOSes stop returning SMAP in the middle of
		   the search loop.  We don't know exactly how the BIOS
		   screwed up the map at that point, we might have a
		   partial map, the full map, or complete garbage, so
		   just return failure. */
		if (oreg.eax != SMAP) {
			count = 0;
			break;
		}

		*desc++ = buf;
		count++;
	} while (ireg.ebx && count < ARRAY_SIZE(boot_params.e820_map));

	return boot_params.e820_entries = count;
}
```

 e820 系统调用(`int 0x15`)如下:

```
AX  = E820h
INT 0x15
EAX = 0000E820h
EDX = 534D4150h ('SMAP')
EBX = continuation value or 00000000h to start at beginning of map
ECX = size of buffer for result, in bytes (should be >= 20 bytes)
ES:DI -> buffer for result
```

调用 e820 之前，**需要将 0xe820 储存到 AX 寄存器**（中断号结合 EAX 寄存器确定是哪个处理函数，`int 0x80` 号系统调用页是这样的），并将标识码 “SMAP” 存储到 EDX 寄存器里面，接着将存储内存区域信息的地址存储到 DI 寄存器，并且将存储内存区域 的长度存储到 CX 寄存器内。由于 BIOS 能够探测到多个内存区域，因此 EBX 用于指定读取第几条内存区域信息。

准备好上面的寄存器之后，执行 BIOS 调用。调用完毕之后 EFLAGS 寄存器的 **CF 标志位用于指示本次调用的成功状态**，如果 CF 标志位置位，那么此次调用失败; 反之如果 CF 标志位清零，那么此次调用成功。接着再检查 EAX 的值是否为 “SMAP”, 如果不是也代表 此次调用失败。以上两个检测都通过的话，那么 BIOS 会将一条内存信息存储在 ES:DI 指向的地址上，即之前设置缓存的位置。由于 BIOS 中存在多条内存区域的信息，因此 BIOS 会将下一条内存区域的信息存储在 EBX 寄存器里，因此可以使用循环将所有的内存 区域信息都读出来。

```c
static void
handle_15e8(struct bregs *regs)
{
    switch (regs->al) {
    case 0x01: handle_15e801(regs); break;
    case 0x20: handle_15e820(regs); break;
    default:   handle_15e8XX(regs); break;
    }
}
```

Seabios 支持多种内存格式。

#### 初始化 BIOS 数据区域 BDA

BDA（BIOS Data Area），是存放计算机当前一些状态的位置。在 SeaBIOS 中，其定义如下：

```c
struct bios_data_area_s {
    // 40:00
    u16 port_com[4];
    u16 port_lpt[3];
    u16 ebda_seg;
    // 40:10
    u16 equipment_list_flags;
    u8 pad1;
    u16 mem_size_kb;
    u8 pad2;
    u8 ps2_ctrl_flag;
    u16 kbd_flag0;
    u8 alt_keypad;
    u16 kbd_buf_head;
    u16 kbd_buf_tail;
    // 40:1e
    u8 kbd_buf[32];
    u8 floppy_recalibration_status;
    u8 floppy_motor_status;
    // 40:40
    u8 floppy_motor_counter;
    u8 floppy_last_status;
    u8 floppy_return_status[7];
    u8 video_mode;
    u16 video_cols;
    u16 video_pagesize;
    u16 video_pagestart;
    // 40:50
    u16 cursor_pos[8];
    // 40:60
    u16 cursor_type;
    u8 video_page;
    u16 crtc_address;
    u8 video_msr;
    u8 video_pal;
    struct segoff_s jump;
    u8 other_6b;
    u32 timer_counter;
    // 40:70
    u8 timer_rollover;
    u8 break_flag;
    u16 soft_reset_flag;
    u8 disk_last_status;
    u8 hdcount;
    u8 disk_control_byte;
    u8 port_disk;
    u8 lpt_timeout[4];
    u8 com_timeout[4];
    // 40:80
    u16 kbd_buf_start_offset;
    u16 kbd_buf_end_offset;
    u8 video_rows;
    u16 char_height;
    u8 video_ctl;
    u8 video_switches;
    u8 modeset_ctl;
    u8 dcc_index;
    u8 floppy_last_data_rate;
    u8 disk_status_controller;
    u8 disk_error_controller;
    u8 disk_interrupt_flag;
    u8 floppy_harddisk_info;
    // 40:90
    u8 floppy_media_state[4];
    u8 floppy_track[2];
    u8 kbd_flag1;
    u8 kbd_led;
    struct segoff_s user_wait_complete_flag;
    u32 user_wait_timeout;
    // 40:A0
    u8 rtc_wait_flag;
    u8 other_a1[7];
    struct segoff_s video_savetable;
    u8 other_ac[4];
    // 40:B0
    u8 other_b0[5*16];
} PACKED;
```

`bda_init` 就是初始化该区域的函数。这里不多介绍。

##### BOOT 阶段前最后的准备

然后是一些其他的接口，比如键盘、鼠标接口的加载。在 `interface_init` 函数的最后部分定义。这里不再介绍。在 `maininit` 函数中，剩余的部分

```C
// Run vga option rom
vgarom_setup();
sercon_setup();
enable_vga_console();

// Do hardware initialization (if running synchronously)
if (!threads_during_optionroms()) {
  device_hardware_setup();
  wait_threads();
}

// Run option roms
optionrom_setup();

// Allow user to modify overall boot order.
interactive_bootmenu();
wait_threads();

// Prepare for boot.
prepareboot();

// Write protect bios memory.
make_bios_readonly();

// Invoke int 19 to start boot process.
startBoot();
```

用于加载 VGA 设备、初始化硬件、为用户提供更改启动顺序的界面。然后，将刚才被设置为可写的 RAM 中的 BIOS ROM 部分，重新保护起来。然后，通过一个 `startBoot` 函数，调用 `INT19` 中断，进入 Boot 状态。

#### startBoot

```C
void VISIBLE32FLAT
startBoot(void)
{
    // Clear low-memory allocations (required by PMM spec).
    memset((void*)BUILD_STACK_ADDR, 0, BUILD_EBDA_MINIMUM - BUILD_STACK_ADDR);

    dprintf(3, "Jump to int19\n");
    struct bregs br;
    memset(&br, 0, sizeof(br));
    br.flags = F_IF;
    call16_int(0x19, &br);
}
```

当前，CPU 处于保护模式。但是**在引导至 OS Bootloader 之前，需要告别保护模式，回到实模式**。`call16_int` 使用之前提到的 `trainsition16` 回到实模式之后，调用 INT 19H 中断，进入 BOOT 状态。

## 引导阶段

boot 阶段的代码，是从 `handle_19`，也就是的 `0x19` 中断处理程序开始。

```C
void VISIBLE32FLAT
handle_19(void)
{
    debug_enter(NULL, DEBUG_HDL_19);
    BootSequence = 0;
    do_boot(0);
}
```

`do_boot` 函数：

```c
static void
do_boot(int seq_nr)
{
    if (! CONFIG_BOOT)
        panic("Boot support not compiled in.\n");

    if (seq_nr >= BEVCount)
        boot_fail();

    // Boot the given BEV type.
    struct bev_s *ie = &BEV[seq_nr];
    switch (ie->type) {
    case IPL_TYPE_FLOPPY:
        printf("Booting from Floppy...\n");
        boot_disk(0x00, CheckFloppySig);
        break;
    case IPL_TYPE_HARDDISK:
        printf("Booting from Hard Disk...\n");
        boot_disk(0x80, 1);
        break;
    case IPL_TYPE_CDROM:
        boot_cdrom((void*)ie->vector);
        break;
    case IPL_TYPE_CBFS:
        boot_cbfs((void*)ie->vector);
        break;
    case IPL_TYPE_BEV:
        boot_rom(ie->vector);
        break;
    case IPL_TYPE_HALT:
        boot_fail();
        break;
    }

    // Boot failed: invoke the boot recovery function
    struct bregs br;
    memset(&br, 0, sizeof(br));
    br.flags = F_IF;
    call16_int(0x18, &br);
}
```

可以发现这里为从软盘、硬盘、CDROM 等一系列设备中引导提供了支持。我们重点关注从硬盘引导。

```C
static void
boot_disk(u8 bootdrv, int checksig)
{
    u16 bootseg = 0x07c0;

    // Read sector
    struct bregs br;
    memset(&br, 0, sizeof(br));
    br.flags = F_IF;
    br.dl = bootdrv;
    br.es = bootseg;
    br.ah = 2;
    br.al = 1;
    br.cl = 1;
    call16_int(0x13, &br);

    if (br.flags & F_CF) {
        printf("Boot failed: could not read the boot disk\n\n");
        return;
    }

    if (checksig) {
        struct mbr_s *mbr = (void*)0;
        if (GET_FARVAR(bootseg, mbr->signature) != MBR_SIGNATURE) {
            printf("Boot failed: not a bootable disk\n\n");
            return;
        }
    }

    tpm_add_bcv(bootdrv, MAKE_FLATPTR(bootseg, 0), 512);

    /* Canonicalize bootseg:bootip */
    u16 bootip = (bootseg & 0x0fff) << 4;
    bootseg &= 0xf000;

    call_boot_entry(SEGOFF(bootseg, bootip), bootdrv);
}
```

### 读取主引导扇区

首先，通过调用 0x13 中断读取主引导扇区。读扇区的参数定义如下：

```plain
功能描述：读扇区
入口参数：AH＝02H
AL＝扇区数
CH＝柱面
CL＝扇区
DH＝磁头
DL＝驱动器，00H~7FH：软盘；80H~0FFH：硬盘
ES:BX＝缓冲区的地址
出口参数：CF＝0——操作成功，AH＝00H，AL＝传输的扇区数，否则，AH＝状态代码
```

首先，需要了解（复习）磁盘的 CHS 模式。Cylinder-head-sector（柱面-磁头-扇区， CHS）是一个定位磁盘数据位置的方法。因为向磁盘读取数据，要先告诉磁盘读取哪里的数据。

下面的两张图片选自[Wikipedia](https://en.wikipedia.org/wiki/Cylinder-head-sector)。

![Cylinder, head, and sector of a hard drive.](https://img2020.cnblogs.com/blog/2101132/202101/2101132-20210116200132694-1537545889.png)

首先介绍磁头。一个磁盘可以看成多个重叠的盘片组成。磁头可以选择读取哪一个盘片上的数据。

其次是柱面。在某一个盘片上，找一个同心圆，这个圆对应的圈叫做磁道。而把所有盘片叠在一起，所有磁道构成的面叫做柱面。所以实际上柱面可以指定在当前磁头所指向的盘片上磁道的“半径”。

然后是扇区。扇区的概念非常重要。一个磁道上连续的一段称为扇区。每个磁道被等分为若干个扇区。一般来说，一个扇区包含 512 字节的数据。

磁头、柱面都是从 0 开始编号的。扇区是从 1 开始编号的。

从代码的 Read sector 部分可以看出，boot_disk 将读取使用 bootdrv 指定的磁盘驱动器上，0 磁头，0 柱面，1 扇区为起始位置，扇区数为 1（512 字节）的一段数据。然后将这段部分复制到 0x7c00 的内存地址当中。这个内存地址可谓是非常经典。

在 checksig 的部分，GET_FARVAR 那一句的含义就是以 bootseg 为段来读取 mbr 中的 signature。而 mbr 此时指向的地址偏移量为 0。我们看 mbr_s 数据结构的定义：

```C
struct mbr_s {
    u8 code[440];
    // 0x01b8
    u32 diskseg;
    // 0x01bc
    u16 null;
    // 0x01be
    struct partition_s partitions[4];
    // 0x01fe
    u16 signature;
} PACKED;
```

可以看出 signature 是 MBR 主引导扇区代码的最后两个字节。这里又是一大经典。可以看出，checksig 的部分是用于校验主引导扇区代码的最后两个字节是否为 MBR_SIGNATURE。而这个值恰恰就是那个经典的数字：0xAA55。

### 接近尾声

引导过程快要接近尾声了。我们注意到一个“规范化”（Canonicalize）操作：

```C
    /* Canonicalize bootseg:bootip */
    u16 bootip = (bootseg & 0x0fff) << 4;
    bootseg &= 0xf000;
```

这个操作实际上是为了调用 OS Loader 的时候，段为 0x0000，而偏移为 0x7C00。而不是段为 0x07C0，而偏移为 0x0000。

最后，通过一个 call_boot_entry 函数，转移到 OS Loader。此时，**系统处于实模式下**。

```C
static void
call_boot_entry(struct segoff_s bootsegip, u8 bootdrv)
{
    dprintf(1, "Booting from %04x:%04x\n", bootsegip.seg, bootsegip.offset);
    struct bregs br;
    memset(&br, 0, sizeof(br));
    br.flags = F_IF;
    br.code = bootsegip;
    // Set the magic number in ax and the boot drive in dl.
    br.dl = bootdrv;
    br.ax = 0xaa55;
    farcall16(&br);
}
```

这就是引导部分的主要内容。

### 问题

文章关于 Seabios 是如何 boot 的分析的很好，但看完后还有有一些问题没有搞懂。

- BMBT 中初始化的一系列 block 是什么意思？`pc.ram low`，`smram`，`system bios` 等等。

  ```c
  init_ram_block("pc.ram low", PC_LOW_RAM_INDEX, false, 0, SMRAM_C_BASE);
    init_ram_block("smram", SMRAM_INDEX, false, SMRAM_C_BASE, SMRAM_C_SIZE);

    // pam expan and pam exbios
    duck_check(PAM_EXPAN_SIZE == PAM_EXBIOS_SIZE);
    for (int i = 0; i < PAM_EXPAN_NUM + PAM_EXBIOS_NUM; ++i) {
      const char *name = i < PAM_EXPAN_NUM ? "pam expan" : "pam exbios";
      hwaddr offset = SMRAM_C_END + i * PAM_EXPAN_SIZE;
      init_ram_block(name, PAM_INDEX + i, true, offset, PAM_EXPAN_SIZE);
    }

    init_ram_block("system bios", PAM_BIOS_INDEX, true, PAM_BIOS_BASE,
                   PAM_BIOS_SIZE);

    duck_check(SMRAM_C_BASE + SMRAM_C_SIZE +
                   PAM_EXPAN_SIZE * (PAM_EXPAN_NUM + PAM_EXBIOS_NUM) ==
               PAM_BIOS_BASE);
    duck_check(ram_list.blocks[PAM_BIOS_INDEX].mr.offset +
                   ram_list.blocks[PAM_BIOS_INDEX].mr.size ==
               X86_BIOS_MEM_SIZE);

    init_ram_block("pc.ram", PC_RAM_INDEX, false, X86_BIOS_MEM_SIZE,
                   total_ram_size - X86_BIOS_MEM_SIZE);
    init_ram_block("pc.bios", PC_BIOS_INDEX, true, 4 * GiB - PC_BIOS_IMG_SIZE,
                   PC_BIOS_IMG_SIZE);
  ```

### 注意

本文并非原创，原文在[这里](https://www.cnblogs.com/gnuemacs/p/14287120.html)，直接复制过来是为了方便自己阅读同时根据自己的需求做些补充。
