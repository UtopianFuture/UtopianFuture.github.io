## ARM 体系结构

目前的工作是基于 ARM 架构设计手机 SOC，在和文档阅读、和同时交流过程中，发现对于各种 ARM 架构的组件，如总线，CPU 等都不熟悉，所以在这里系统性的总结一下 ARM 架构。当然，不单单要了解其功能特性，还需要横向对比其不足，因为在请教同事的过程中，发现其对 ARM，X86，RISC-V 架构的弊端，以及它们的生态处境如数家珍，我觉得这是一个很了不起的能力。这里算是一个雏形，之后不断完善。

### ARMv8-A 架构

ARMv8-A 是 ARM 公司发布的第一代支持 64 位处理器的指令集和架构。ARMv8 处理器支持两种执行执行状态——AArch64 和 AArch32 状态。

AArch64 状态的异常等级确定了处理器当前运行的特权级别，包括如下级别：

- EL0：用户特权，用于运行普通用户程序；
- EL1：系统特权，通常用于运行操作系统；
- EL2：运行虚拟化扩展的虚拟监控程序（hypervisor）；
- EL3：运行安全世界中的安全监控器（secure monitor）；

![img](https://raw.githubusercontent.com/UtopianFuture/UtopianFuture.github.io/3663c45f89165d0460aa3a877d0b811b8024047a/image/ARM-state-level.svg)

不同的异常等级由系统寄存器表示。

### ARM 授权模式

ARM 的 IP 授权模式主要分为三种：

- 使用层级授权：可使用封装好的 ARM 芯片，而不能进行任何修改；

- 内核层级授权：可基于购买的 ARM 内核进行芯片开发，设计，有一定的自主开发权；

- 架构/指令集层级授权：可对 ARM 架构进行改造，甚至对 ARM 指令集进行扩展或缩减；

- ELP 的合作模式：Enhanced Lead Partner Program

### ARM 总线协议

ARM 中的总线协议多种多样，我看的头昏眼花，这里系统的总结一下，先看看整个发展：

![img](https://raw.githubusercontent.com/UtopianFuture/UtopianFuture.github.io/3663c45f89165d0460aa3a877d0b811b8024047a/image/ARM-interconnect.svg)

#### AMBA

**AMBA 总线协议**是一套由 ARM 提供的互连规范，该规范标准化了各种 IP 之间的芯片通信机制。这些设计通常有一个或多个微处理器以及集成其他一些组件——内部存储器或外部存储器桥、DSP、DMA、加速器和各种其他外围设备，如 USB、UART、PCIE、I2C 等。AMBA 协议的主要动机是用一种标准和高效的方法来重用这些跨多个设计的 IP。

#### APB

**高级外围设备总线(APB)**用于连接低带宽的外围设备。它是一个简单的非流水线协议。读写操作共享同一组信号，不支持 burst 数据传输。

#### AHB

**高级高性能总线(AHB)**用于连接共享总线上需要更高带宽的组件。这些 slave 组件可以是内部内存或外部内存接口、DMA、DSP 等。AHB 可以通过 burst 数据传输来获得更高的带宽。

#### AHB-Lite

**AHB-lite**协议是 AHB 的一个简化版本。简化后**只支持一个主设计**，这消除了对任何仲裁、重试、分割事务等的需求。

#### AXI

随着越来越多的 IP 集成到 SOC 设计中，**读写共享**的 AHB、APB 总线已经无法满足互联需求了，故引入了点对点连接协议——AXI（高级可扩展接口）。

**高级可扩展接口(AXI)**适合于高带宽和低延迟互连。这是一个点对点的互连，并克服了 AHB、APB 等共享总线协议在可连接的代理数量方面的限制性。该协议**支持多个 outstanding** 的数据传输、burst 数据传输、单独的读写通道和支持不同的总线宽度。

#### AXI-Lite

**AXI-lite 协议**是 AXI 的简化版本，简化后不支持突发数据传输。

#### AXI-Stream

**AXI-stream** 协议是 AXI 协议的另一种风格，它只支持数据流从 master 流到 slave。与完整的 AXI 或 AXI-lite 不同，AXI-stream 协议中没有单独的读/写通道，因为其目的是只在一个方向上流。

#### ACE

ACE 协议是 AXI4 协议的扩展，将一致性监听 snoop 协议和 AXI4 总线相结合。ACE 在 AXI 基础上增加了三个通道，snoop 地址通道（snoop address channel，AC），snoop 响应通道（snoop response channel，CR）和 snoop 数据通道（snoop data channel，CD）ACE 通道仍是基于 ready/valid 握手的方式来完成。

#### CHI

ACE 协议使用了 master/slave 之间的信号电平通信，因此互连需要大量的线和增加的通道来进行 snoops 和响应。这对于具有 2/4 核移动 SOC 的小一致性 clusters 非常有效。随着 SOC 上集成越来越多的一致性 clusters ——AMBA5 修订版引入了 CHI 协议。

CHI 是 AMBA 第 5 代协议，ACE 协议的进化版。其用于解决多个 CPU（RN）之间的数据一致性问题。具有如下特点：

- 分层实现：包括协议层、网络层和链路层；
- 基于包传输：将所有的信息传输采用包 packet 的形式来完成，packet 里分各个域段传递不同信息；

这里详细了解一下 CHI 协议。

##### CHI Multi-Copy(MCP)

单核系统上，用 Single-copy atomicity 这个术语来描述**内存访问操作是否是原子的**，是否是可以被打断的。多核系统上，用 Multi-copy atomicity 来描述多个 CPU CORE 发起对同一个地址进行写入的时候，这些内存访问表现出来的特性。
CHI 协议要求系统架构为 multi-copy atomic，所有相关组件必须确保所有的 write-type 必须是 multi-copy atomic。一个写操作被定义为 multi-copy atomic 必须满足以下两个条件：

- 所有对同地址的写操作时串行的，即它们会被其他 Requester 以相同的顺序观察到，尽管有些 Requester 可能不会观察到所有写操作；
- 一笔写操作只有被所有 Requester 观察到后，才能被同地址的 Read 操作读出该值；

##### CHI 分层协议

- 一个 Transaction 就是一个单独的操作，比如读或写内存。每个节点都被分配一个 ID；

- Message 是协议层术语，用于定义两个组件之间交换信息的粒度，如 Request/Data response/Snoop request，一个数据响应 message 可能由多个 packets 组成；

- Packet 是互连端点间的传输粒度，一个 message 可能由一个或多个 packets 组成，每个 packet 包含有源和目的节点的 ID 来保证在 interconnect 上独立路由；

- Flit 是最小流控单位，一个 packet 可以由一个或多个 flits 组成，对于同一个 packet 的所有 flits 在互连（interconnect，简称 ICN）上传输必须遵循同样的路径。Phit 是物理层传输单位，一个 flit 可以由一个或多个 phits 组成，phit 定义为两相邻网络设备之间的一个传输。

  协议层通信基于 transaction；在网络层基于 packet；链路层基于 flit。

##### CHI 一致性实现模型

- 需要存数据时，先把所有其它 masters 的数据备份失效掉；
- CHI 协议允许（不强求）主存的数据不是实时更新；
- master 可以确定一份 cacheline 是否是唯一的或者存在多份拷贝；
- 一致性是以 cacheline 粒度对齐，cacheline 在 64bytes 对齐存储系统中大小为 64bytes；
- 每个 cacheline 都维护状态位以决定节点访问 cacheline 后执行的动作；

##### CHI 组件分类

- HN（Home Node）协调所有的传输请求，包括 snoop 请求、访问 cache 和 memory，位于 ICN 内，用于接收来自 RN 产生的协议 transactions；

- RN（Request Node）：Request Node，产生协议 transactions，包含读和写；

- SN（Slave Node）用于接收来自 HN 的请求，完成相应的操作并返回一个响应；

  总结：一个 RN 会产生 transaction(read，write，maintenance)给 HN；HN 接收，并对 RN 发来的请求进行排序，产生
transaction 给 SN；SN 接收这些请求，返回数据或者响应。

### ARM 可信固件

ARM 可信任固件（ARM Trusted Firmware，ATF）是由 ARM 官方提供的底层固件，该固件统一了 ARM 底层接口标准，如电源状态控制接口（Power Status Control Interface，PSCI）、安全启动需求（Trusted Board Boot Requirements，TBBR）、安全世界状态（SWS）与正常世界状态（NWS）切换的安全监控模式调用（secure monitor call，smc）操作等。ATF 旨在将 ARM 底层的操作统一使代码能够重用和便于移植。

#### ATF

ATF 的源代码共分为 bl1、bl2、bl31、bl32、bl33 部分，其中 bl1、bl2、bl31 部分属于固定的固件，bl32 和 bl33 分别用于加载 TEE OS 和 REE 侧的镜像。整个加载过程可配置成安全启动的方式，每一个镜像文件在被加载之前都会验证镜像文件的电子签名是否合法。
ATF 主要完成的功能如下：

- 初始化安全世界状态运行环境、异常向量、控制寄存器、中断控制器、配置平台的中断；
- 初始化 ARM 通用中断控制器（General Interrupt Controller，GIC）2.0 版本和 3.0 版本的驱动初始化；
- 执行 ARM 系统 IP 的标准初始化操作以及安全扩展组件的基本配置；
- 安全监控模式调用（Secure Monitor Call，smc）请求的逻辑处理代码（Monitor 模式/EL3）。实现可信板级引导功能，对引导过程中加载的镜像文件进行电子签名检查；
- 支持自有固件的引导，开发者可根据具体需求将自有固件添加到 ATF 的引导流程中。

#### ATF 与 TEE 的关系

为规范和简化 TrustZone OS 的集成，在 ARMv8 架构中，ARM 引入 ATF 作为底层固件并开放了源码，用于完成系统中 BootLoader、Linux 内核、TEE OS 的加载和启动以及正常世界状态和安全世界状态的切换。ATF 将整个启动过程划分成不同的启动阶段，由 BLx 来表示。例如，TEE OS 的加载是由 ATF 中的 bl32 来完成的，安全世界状态和正常世界状态之间的切换是由 bl31 来完成的。在加载完 TEE OS 之后，TEE OS 需要返回一个处理函数的接口结构体变量给 bl31。当在 REE 侧触发安全监控模式调用指令时，bl31 通过查询该结构体变量就可知需要将安全监控模式调用指令请求发送给 TEE 中的那个接口并完成正常世界状态到安全世界状态的切换。

### 问题

- 为什么使用同样的处理器，但是每家能够达到的频率不同？

  ![performance-compare](D:\gitlab\UtopianFuture.github.io\image\performance-compare.png)

  这个很正常，因为最高频率不仅和 A 核设计有关，还核整个系统的实现，如内存带宽、cache 系统设计等等有关。
