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

- 内核层级授权：可基于购买的ARM内核进行芯片开发，设计，有一定的自主开发权；

- 架构/指令集层级授权：可对ARM架构进行改造，甚至对ARM指令集进行扩展或缩减；

- ELP 的合作模式：Enhanced Lead Partner Program

### ARM 总线协议

ARM 中的总线协议多种多样，我看的头昏眼花，这里系统的总结一下，先看看整个发展：

![img](https://raw.githubusercontent.com/UtopianFuture/UtopianFuture.github.io/3663c45f89165d0460aa3a877d0b811b8024047a/image/ARM-interconnect.svg)

#### AMBA

**AMBA总线协议**是一套由ARM提供的互连规范，该规范标准化了各种IP之间的芯片通信机制。这些设计通常有一个或多个微处理器以及集成其他一些组件——内部存储器或外部存储器桥、DSP、DMA、加速器和各种其他外围设备，如USB、UART、PCIE、I2C等。AMBA协议的主要动机是用一种标准和高效的方法来重用这些跨多个设计的IP。

#### APB

**高级外围设备总线(APB)**用于连接低带宽的外围设备。它是一个简单的非流水线协议。读写操作共享同一组信号，不支持burst数据传输。

#### AHB

**高级高性能总线(AHB)**用于连接共享总线上需要更高带宽的组件。这些slave组件可以是内部内存或外部内存接口、DMA、DSP等。AHB可以通过burst数据传输来获得更高的带宽。

#### AHB-Lite

**AHB-lite**协议是AHB的一个简化版本。简化后**只支持一个主设计**，这消除了对任何仲裁、重试、分割事务等的需求。

#### AXI

随着越来越多的IP集成到SOC设计中，**读写共享**的AHB、APB总线已经无法满足互联需求了，故引入了点对点连接协议——AXI（高级可扩展接口）。

**高级可扩展接口(AXI)**适合于高带宽和低延迟互连。这是一个点对点的互连，并克服了AHB、APB等共享总线协议在可连接的代理数量方面的限制性。该协议**支持多个outstanding** 的数据传输、burst数据传输、单独的读写通道和支持不同的总线宽度。

#### AXI-Lite

**AXI-lite协议**是AXI的简化版本，简化后不支持突发数据传输。

#### AXI-Stream

**AXI-stream** 协议是AXI协议的另一种风格，它只支持数据流从master 流到slave。与完整的AXI或AXI-lite不同，AXI-stream 协议中没有单独的读/写通道，因为其目的是只在一个方向上流。

#### ACE

ACE协议是AXI4协议的扩展，将一致性监听snoop协议和AXI4总线相结合。ACE在AXI基础上增加了三个通道，snoop地址通道（snoop address channel，AC），snoop响应通道（snoop response channel，CR）和snoop数据通道（snoop data channel，CD）ACE通道仍是基于ready/valid握手的方式来完成。

#### CHI

ACE协议使用了master/slave之间的信号电平通信，因此互连需要大量的线和增加的通道来进行snoops 和响应。这对于具有2/4核移动SOC 的小一致性clusters非常有效。随着SOC上集成越来越多的一致性clusters ——AMBA5修订版引入了CHI协议。

CHI 是 AMBA第5代协议，ACE协议的进化版。其用于解决多个CPU（RN）之间的数据一致性问题。具有如下特点：

- 分层实现：包括协议层、网络层和链路层；
- 基于包传输：将所有的信息传输采用包packet的形式来完成，packet里分各个域段传递不同信息；

这里详细了解一下 CHI 协议。

##### CHI Multi-Copy(MCP)

单核系统上，用Single-copy atomicity这个术语来描述**内存访问操作是否是原子的**，是否是可以被打断的。多核系统上，用Multi-copy atomicity来描述多个CPU CORE发起对同一个地址进行写入的时候，这些内存访问表现出来的特性。
CHI协议要求系统架构为multi-copy atomic，所有相关组件必须确保所有的write-type必须是multi-copy atomic。一个写操作被定义为multi-copy atomic必须满足以下两个条件：

- 所有对同地址的写操作时串行的，即它们会被其他Requester以相同的顺序观察到，尽管有些Requester可能不会观察到所有写操作；
- 一笔写操作只有被所有Requester观察到后，才能被同地址的Read操作读出该值；

##### CHI 分层协议

- 一个Transaction就是一个单独的操作，比如读或写内存。每个节点都被分配一个ID；

- Message是协议层术语，用于定义两个组件之间交换信息的粒度，如Request/Data response/Snoop request，一个数据响应message可能由多个packets组成；

- Packet是互连端点间的传输粒度，一个message可能由一个或多个packets组成，每个packet包含有源和目的节点的ID来保证在interconnect上独立路由；

- Flit是最小流控单位，一个packet可以由一个或多个flits组成，对于同一个packet的所有flits在互连（interconnect，简称ICN）上传输必须遵循同样的路径。Phit是物理层传输单位，一个flit可以由一个或多个phits组成，phit定义为两相邻网络设备之间的一个传输。

  协议层通信基于transaction；在网络层基于packet；链路层基于flit。

##### CHI 一致性实现模型

- 需要存数据时，先把所有其它masters的数据备份失效掉；
- CHI协议允许（不强求）主存的数据不是实时更新；
- master可以确定一份cacheline是否是唯一的或者存在多份拷贝；
- 一致性是以cacheline粒度对齐，cacheline在64bytes对齐存储系统中大小为64bytes；
- 每个cacheline都维护状态位以决定节点访问cacheline后执行的动作；

##### CHI 组件分类

- HN（Home Node）协调所有的传输请求，包括snoop请求、访问cache和memory，位于ICN内，用于接收来自RN产生的协议transactions；

- RN（Request Node）：Request Node，产生协议transactions，包含读和写；

- SN（Slave Node）用于接收来自HN的请求，完成相应的操作并返回一个响应；

  总结：一个RN会产生transaction(read，write，maintenance)给HN；HN接收，并对RN发来的请求进行排序，产生
  transaction给SN；SN接收这些请求，返回数据或者响应。

### 问题

- 为什么使用同样的处理器，但是每家能够达到的频率不同？

  ![performance-compare](D:\gitlab\UtopianFuture.github.io\image\performance-compare.png)
  
  这个很正常，因为最高频率不仅和 A 核设计有关，还核整个系统的实现，如内存带宽、cache 系统设计等等有关。