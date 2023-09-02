## Security Memory Management

什么是 security memory？为什么需要 security memory？

首先需要理解 ARM 架构的 SOC 系统安全架构。为什么需要安全架构？

### 可信执行环境

为避免用户敏感信息被获取，需要在系统中提供一个相对可信赖的运行环境，使用户的关键数据或应用在这个相对可信赖的环境中使用和运行。这样一来，即便系统被攻破，入侵者也无法直接获取用户的重要信息，用户的信息安全也就实现了，这就是**可信执行环境（Trusted Execution Environment，TEE）**的主要作用和理念。

**Trustzone 技术就是 ARM 用来提供可信运行环境的技术；X86 的是 SGX(Software Guard Extensions)**。

首先其将 CPU 的工作状态分为了正常世界状态（Normal World Status，NWS）和安全世界状态（Secure World Status，SWS）。支持 TrustZone 技术的芯片提供了对外围硬件资源的硬件级别的保护和安全隔离。**当 CPU 处于正常世界状态时，任何应用都无法访问安全硬件设备，也无法访问属于安全世界状态下的内存、缓存（Cache）以及其他外围安全硬件设备**。系统通过调用安全监控模式调用（secure
monitor call，smc）指令实现 CPU 的安全状态与非安全状态之间的切换。

其次，在整个系统的软件层面，一般的操作系统以及应用运行在 NWS 中，TEE 运行在 SWS 中， NWS 内的开发资源相对于 SWS 较为丰富，因此通常称运行在 NWS 中的环境为丰富执行环境（Rich Execution Environment，REE），而可信任的操作系统以及上层的可信应用（Trusted Application，TA）运行于 SWS，运行在 SWS 中的系统就是 TEE。

对 CPU 的工作状态区分之后，处于 NWS 中的 Linux 即使被 root 也无法访问安全世界状态中的任何资源，包括操作安全设备、访问安全内存数据、获取缓存数据等。这是因为 CPU 在访问安全设备或者安全内存地址空间时，**芯片级别的安全扩展组件（各种各样的安全芯片）**会去校验 CPU 发送的访问请求的**安全状态读写信号位（Non-secure bit，NS bit）**是 0 还是 1，以此来判定当前 CPU 发送的资源访问请求是安全请求还是非安全请求。而处于非安全状态的 CPU 将访问指令发送到系统总线上时，其访问请求的 NS bit 都会被强制设置成 1，表示当前 CPU 的访问请求为非安全请求。而非安全请求试图去访问安全资源时会被安全扩展组件认为是非法访问的，于是就禁止其访问安全资源，因此该 CPU 访问请求的返回结果要么是访问失败，要么就是返回无效结果，这也就实现了对系统资源硬件级别的安全隔离和保护（有可能被攻破么）。

### TrustZone

SOC 由 ARM 核、bus、onchipram、onchiprom、外设等组成，只有支持 TrustZone 技术的 ARM 核配合安全扩展组件，才能为整个系统提供芯片硬件级别的保护和隔离。下图是一个典型的支持 TrustZone 的 SoC 的硬件框图。

![ARM-SOC.png](https://github.com/UtopianFuture/UtopianFuture.github.io/blob/master/image/ARM-SOC.png?raw=true)

#### ARM 安全扩展组件

TrustZone 技术之所以能提高系统的安全性，是因为对外部资源和内存资源的**硬件隔离**。这些硬件隔离包括中断隔离、onchipram 和 onchiprom 的隔离、片外 RAM 和 ROM 的隔离、外设的硬件隔离、外部 RAM 和 ROM 的隔离等。实现硬件层面的各种隔离，需要对整个系统的硬件和处理器核做出相应的扩展。这些扩展包括：

- 对处理器核的虚拟化，也就是将 AMR 处理器的运行状态分为安全态和非安全态；
- 对总线的扩展，增加安全位读写信号线；
- 对内存管理单元（Memory ManagementUnit，MMU）的扩展，增加页表的安全位；
- 对缓存（Cache）的扩展，增加安全位；
- 对其他外围组件进行了相应的扩展，提供安全操作权限控制和安全操作信号。

##### AXI 总线上安全状态位的扩展

为了支持 TrustZone 技术，控制处理器在不同状态下对硬件资源访问的权限，ARM 对 AXI 系统总线进行了扩展。在原有 AXI 总线基础上对每一个读写信道增加了一个额外的控制信号位（Non-Secure bit），用来表示当前的读写操作是安全操作还是非安全操作。

##### AXI-to-APB 桥

外设总线（Advanced PeripheralBus，APB）不具有 NS bit，为实现 APB 外设与 TrustZone 技术相兼容，保护外设安全，增加一个 APB-to-AXI 桥。其拒绝不匹配的安全事务设置，并且不会将该事务请求发送给外设。

##### TZASC 地址空间控制组件

TrustZone 地址空间控制组件（TrustZone Address Space Controller，TZASC）是 AXI 总线上的一个主设备，**能够将从设备全部的地址空间分割成一系列的不同地址范围**。在安全状态下，通过编程 TZASC 能够将这一系列分割后的地址区域设定成安全空间或者是非安全空间。

需要注意的是，TZASC 组件只支持存储映射设备（如 off-Soc、DRAM 等）对安全和非安全区域的划分与扩展，但不支持对块设备（如 EMMC、NAND flash 等）的安全和非安全区域的划分与扩展。下图为使用 TZASC 组件使用的例子，

![TZSAC.png](https://github.com/UtopianFuture/UtopianFuture.github.io/blob/master/image/TZSAC.png?raw=true)

##### TZMA 内存适配器组件

TrustZone 内存适配器组件（TrustZone Memory Adapter，TZMA）允许对 SRAM 或者 onchiprom 进行安全区域和非安全区域的划分。TZMA 支持最大 2MB 空间的 SRAM 的划分，可以将 2MB 空间划分成两个部分，高地址部分为非安全区域，低地址部分为安全区域，两个区域必须按照 4KB 进行对齐。

![TZMA.png](https://github.com/UtopianFuture/UtopianFuture.github.io/blob/master/image/TZMA.png?raw=true)

##### TZPC 保护控制器组件

TrustZone 保护控制器组件（TrustZone Protection Controller，TZPC）是用来设定 TZPCDECPORT 信号和 TZPCR0SIZE 等相关控制信
号的。这些信号用来告知 APB-to-AXI 对应的外设是安全设备还是非安全设备，而 TZPCR0SIZE 信号用来控制 TZMA 对 SRAM 或 onchiprom 安全区域大小的划分。TZPC 包含三组通用寄存器 TZPCDECPROT[2:0]，每组通用寄存器可以产生 8 种 TZPCDECPROT 信号，也就是 TZPC 最多可以将 24 个外设设定成安全外设。TZPC 组件还包含一个 TZPCROSIZE 寄存器，该寄存器用来为 TZMA 提供分区大小信息。下图为 TZPC 的使用例子。

![TZPC.png](https://github.com/UtopianFuture/UtopianFuture.github.io/blob/master/image/TZPC.png?raw=true)

##### TZIC 中断控制器组件

在支持 TrustZone 的 SoC 上，ARM 添加了 TrustZone 中断控制器（TrustZone Interrupt Controller，TZIC）。TZIC 的作用是**让处理器处**
**于非安全态时无法捕获到安全中断**。TZIC 是第一级中断控制器，**所有的中断源都需要接到 TZIC 上**。TZIC 根据配置来判定产生的中断类型，然后决定是将该中断信号先发送到非安全的向量中断控制器（Vector Interrupt Controller，VIC）后以 nIRQ 信号发送到处理器，还是以 nTZICFIQ 信号直接发送到处理器。下图为 TZIC 在 SoC 中的使用示意，

![TZIC.png](https://github.com/UtopianFuture/UtopianFuture.github.io/blob/master/image/TZIC.png?raw=true)

##### Cache 和 MMU 的扩展

在支持 TrustZone 的 SoC 上，会对 MMU 进行虚拟化，使得寄存器 TTBR0、TTBR1、TTBCR 在安全状态和非安全状态下是相互隔离的，因此两种状态下的虚拟地址转换表是独立的。**存放在 MMU 中的每一条页表描述符都会包含一个安全状态位，用以表示被映射的内存是属于安全内存还是非安全内存**。虚拟化的 MMU 共享 TLB，同样 TLB 中的每一项也会打上安全状态位标记，只不过该标记是用来表示该条转换是正常世界状态转化的还是安全世界状态转化的。Cache 也同样进行了扩展，Cache 中的每一项都会按照安全状态和非安全状态打上对应的标签，在不同的状态下，处理器只能使用对应状态下的 Cache。
