## Network on Chip

### 相关背景

NoC(Network on Chip)是 SOC 发展的产物，顾名思义，它是 SOC 上各个 IP 相互通讯的互联网络。

在 SOC 的应用需求越来越丰富，集成的 IP 越来越多，传统的基于总线的集中式互连架构已经难以满足现今片上多核间通讯的性能需求。NoC 总线是基于报文交换的片上网络，能够解决这一问题。

在 NoC 中，路由节点之间通过局部互连线相连接，每一个路由节点通过网络接口 NI 与一个本地 IP 核相连接，源路由节点和目的路由节点之间的数据通讯需要经过多个跳步来实现。因此，NoC 技术的出现使得片上系统 SoC 的设计也将从以计算为中心逐渐过渡到以通讯为中心。

与传统的总线互连技术相比，NoC 具有如下优点：

- 网络带宽。总线结构互连多个 IP 核，共享一条数据总线，其缺点是同一时间只能有一对 IP 进行通信。片上网络具有非常丰富的信道资源，为系统提供了一个网络化的通信平台。网络中的多个节点可以同时利用网络中的不同物理链路进行信息交换，支持多个 IP 核并发地进行数据通信。随着网络规模的增大，网络上的信道资源也相应增多。因此，NoC 技术相对于 Bus 互连技术具有较高的带宽，以及更高的通信效率；
- 可扩展性和设计成本。总线结构需要针对不同的系统需求单独进行设计，当系统功能扩展时，需要对现有的设计方案重新设计。NoC 中每个路由节点和本地 IP 核通过网络接口（Network Interface, NI）相连，当系统需要升级扩展新功能时，只需要将新增加的处理器核通过网络接口 NI 接入到网络中的路由节点即可，无需重新设计网络；
- 功耗。随着 SoC 规模的不断增大，总线上每次信息交互都需要驱动全局互连线，所消耗的功耗将显著增加，并且随着集成电路工艺的不断发展，想要保证全局时钟同步也将变得难以实现。而在 NoC 中，信息交互消耗的功耗与进行通讯的路由节点之间的距离密切相关，距离较近的两个节点进行通讯时消耗的功耗就比较低；
- 信号完整性和信号延迟。随着集成电路特征尺寸的不断减小，电路规模的不断增大，互连线的宽度和间距也在不断地减小，线间耦合电容相应增大，长的全局并行总线会引起较大的串扰噪声，从而影响信号的完整性以及信号传输的正确性。同时，互连线上的延迟将成为影响信号延迟的主要因素，总线结构全局互连线上的延迟将大于一个时钟周期，从而使得时钟的偏移很难管理；
- 全局同步。总线结构采用全局同步时钟，随着芯片集成度的提高，芯片的工作频率也在不断提高，在芯片内会形成很庞大的时钟树，因此很难实现片上各个模块的全局同步时钟（那 NoC 是怎么做的？）；
 NoC 的基本组成为：IP 核、路由器、网络适配器以及网络链路，其中 IP 核和路由器位于系统层，网络适配器位于网络适配层。
- IP核：是 SoC 中的基本处理单元，负责执行特定的计算或控制任务。
- 路由器：实现通信协议，负责数据包的路由和转发。路由器的作用是负责接收来自不同节点的数据，然后根据预先定义的路由算法，将数据转发到目标节点。路由器还包含一个逻辑块，实现流控制策略(路由、仲裁器等)，并定义通过 NoC 移动数据的总体策略。
- 网络适配器：在网络接口（NI）和 IP 核之间建立逻辑连接，实现数据包的封装和解析。IP 的接口多样，这里进行数据统一。
- 网络链路：连接各个路由器和 IP 核的物理通道，负责数据的实际传输。链路是路由器之间传输数据的物理通道，可以是电气信号线、光纤、无线信号等。链路的带宽和延迟是影响 NoC 性能的重要因素，因此链路的设计需要充分考虑数据传输的速度和可靠性。
 NoC 根据拓扑结构的不同，也分为星形(Star)连接、环形(Ring)连接和网格(Mesh)连接。具体采用哪种总线结构，需根据设计目标进行复杂的仿真分析。
