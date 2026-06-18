## NOC

### 引言：文档范围与技术定位

本文总结完全基于 Arteris FlexNoC 的技术文档，涵盖**时钟复位、架构概念、可观测性、物理实现、功耗管理、QoS、安全性、弹性、NIU 设计、客户自定义**等全技术栈。

### 核心概念与设计流程

#### 基础术语定义

该节明确 NoC 的核心术语，为后续技术模块提供统一语境：

- **事务（Transaction）**：Initiator（如 CPU、DMA）与 Target（如内存、外设）间的通信单元，含读 / 写请求、响应，支持 AMBA AXI/AHB、OCP 等标准协议，由 NIU 处理协议转换；
- **包（Packet）**：NoC 内部传输单元，由头部（Header）和可选载荷（Payload） 组成：
  - 头部：含 RouteId（路由标识，由 InitFlow/TargFlow/TargSubRange 组成）、Opcode（事务类型：RD/WR/RDX 等）、Addr（地址）、Len1（ payload 字节数 - 1）、Security（安全等级）、User（用户标志）；
  - 载荷：仅用于数据传输（写数据、读响应数据），无数据时可省略；
- **域（Domain）**：NoC 的逻辑 / 物理划分单元，分三类：
  - 时钟域（Clock Domain）：共享同一时钟信号的单元集合，由 “时钟 Regime→时钟管理器→时钟” 三级构成，同一 Regime 内时钟同步；
  - 电源域（Power Domain）：电源可独立开关的单元集合，支持 “电源断开协议”（如 DISC 错误码标识断开的 Target）；
  - 电压域（Voltage Domain）：共享同一供电电压的单元集合，与时钟 Regime 绑定，同一电压域可包含多个时钟域。
- **NTTP 协议**：FlexNoC 内部专有传输协议（NoC Transaction and Transport Protocol），实现事务与包的转换及统一传输，确保 NoC 核心的一致性。

#### 分层架构

NOC 定义**两层架构**，解耦事务处理与包传输，每个层级的功能与组件严格绑定：

| 层级     | 核心组件            | 功能描述                                                     |
| -------- | ------------------- | ------------------------------------------------------------ |
| 事务层   | 网络接口单元（NIU） | 1. 协议转换：外部协议（如 AXI4）→NTTP； <br />2. 事务适配：数据宽度不匹配、大 burst 拆分；<br />3. 安全检查：地址解码、安全等级校验； |
| 包传输层 | 包传输单元（PTU）   | 1. 包路由：基于 RouteId 通过交换机转发； <br />2. 仲裁：多包竞争链路时按优先级 / 公平性分配； <br />3. 跨域适配：时钟 / 电源 / 序列化适配；<br />4. 流控：基于 FIFO / 信用机制避免丢包； |

#### 设计流程

FlexNoC 通过**三阶段流程**实现从需求到硬件落地，由 FlexArtist 软件支撑，每个阶段的输入 / 输出和工具严格定义：

- 规格阶段（Specification）：

  - 核心任务：定义功能需求，包括接口协议（Socket 类型、数据宽度）、地址映射（Initiator→Target 的全局地址范围、访问权限）、域划分（时钟 Regime、电源域）、安全规则（安全等级、防火墙策略）；

  - 工具操作：FlexArtist Specification Editor（Interface: Ports/Sockets/Mappings 页面）；

  - 输出：规格文档（含 Socket 列表、地址映射表、域定义）。

- 架构阶段（Architecture）：

  - 核心任务：优化性能与面积 Trade-off，包括拓扑设计（交叉开关 / 总线 / 三阶段架构）、传输单元配置（交换机端口数、链路 FIFO 深度、适配器类型）、可观测性规划（探针点位置、观测器接口）；

  - 工具操作：FlexArtist Architecture Editor（Datapath Transport/Observability 页面）；

  - 输出：架构文档（拓扑图、传输单元参数表）。

- 结构阶段（Structure）：

  - 核心任务：生成硬件网表，包括自动合成 NIU/PTU/ 时钟管理器、寄存器配置（复位值、访问类型）、手动连接特殊端口（外部事件信号、调试接口）；

  - 工具操作：FlexArtist Structure Editor（Parameters/Registers 页面）；

  - 输出：RTL 网表、综合脚本、寄存器映射表。

#### 死锁预防

FlexNoC 在 “单 NoC 独立运行无死锁” 的基础上，针对多 NoC 互连或多端口 IP 场景，通过 “拓扑约束 + 依赖声明” 预防两类死锁：

##### 死锁类型与成因

- **环形死锁（Roundabout Deadlock）**：多分组在 MUX 仲裁器中形成循环阻塞，如两个总线拓扑 NoC 互连时，请求流控反向传播形成闭环。
- **拆分 / 重组死锁（Split/Reassembly Deadlock）**：事务拆分后，部分响应片段阻塞仲裁器，导致其他响应无法推进，最终阻塞原事务的请求片段（如多 NoC 互连时，响应在下游 NoC 中仲裁排队）。

##### 预防策略

- **拓扑约束**：架构编辑器禁止流控环路，如要求请求 / 响应路径单向（左到右），不允许反向链路；禁止 “发散 - 汇聚” 路径（如同一 initiator 的两个请求经不同路径汇聚到同一 target）。
- 依赖组（Dependency Group）声明：
  - 定义：将存在外部依赖的 socket 归为一组（如 NoC1 的 fromOtherNoC 与 toOtherNoC socket），FlexArtist 会检查拓扑，若存在通过依赖 socket 的流控环路则报错。
  - 规则：若两个 socket 的流控可能通过外部 IP（如其他 NoC、多端口内存控制器）相互影响，则必须归为同一依赖组。

##### 响应重组管理

通过 socket 的`responseForwarding`参数控制响应处理逻辑，确保非主 socket（Non-primary）的响应兼容性：

- **Whole**：要求响应无条件完成（如 AHB/APB），不支持响应交织，需全响应存储转发。
- **Partial**：支持响应部分完成（如 AXI/ACE），允许响应交织，超时未完成则中断分组。
- **Dont Know Yet**：保守策略，target 侧按 Partial 处理，initiator 侧按 Whole 处理。

### 硬件架构详解

为兼容传统总线、遗留 IP 及常见 SoC 架构，FlexNoC 采用事务驱动的通信架构，核心逻辑如下 ：

- **事务流转路径**：发起端 IP（initiator IP）发出的事务通过 “接口（socket）” 进入互连，再通过接口输出至目标端 IP（target IP） ；
- **接口协议特性**：接口的事务级协议支持各类标准（如 AMBA® AXI™、AHB™、APB™、OCP 等），由接口自身特性定义 ；
- **事务转换逻辑**：若发起端与目标端接口的协议、数据宽度、时钟等属性不同，进入互连的事务会转换为一个或多个等效事务，定向传输至目标端；无法转换的事务会被标记为错误并上报 。

**错误原因**：事务转换失败通常源于发起端的地址错误，极少数情况是目标端无法将事务转换为自身协议 。

![image-20250922195716194](C:\Users\xpeng\AppData\Roaming\Typora\typora-user-images\image-20250922195716194.png)

#### 网络接口单元（NIU）：事务层核心

##### NIU 分类与功能

NOC 将 NIU 分为两类，功能严格区分，避免跨职责处理：

| NIU 类型      | 对接对象                 | 核心功能                                                     |
| ------------- | ------------------------ | ------------------------------------------------------------ |
| Initiator NIU | Initiator IP（CPU/DMA）  | 1. 地址解码：匹配全局地址映射，生成 RouteId；<br />2. 协议转换：如 AXI4→NTTP，处理 burst 拆分；<br />3. 安全检查：验证事务安全等级，禁止非法访问；<br />4. 错误生成：地址越界（DEC）、安全违规（SEC）错误； |
| Target NIU    | Target IP（内存 / 外设） | 1. 包解析：提取 RouteId/Addr/ 数据，生成外部协议事务；<br />2. 响应重组：合并 NoC 的响应包，生成完整外部响应；<br />3. 超时处理：监测 Target 响应超时（TMO 错误）；<br />4. 错误转发：Target 返回的 SLV 错误包转发至观测器； |

##### NIU 内部结构（双端设计）

NIU 分为 “特定侧（Specific Side）” 与 “通用侧（Generic Side）”，解耦外部协议与内部传输，NOC 明确各侧的功能与可配置项：

| Side   | 对接对象        | 核心功能                                                     | 可配置参数                                                   |
| ------ | --------------- | ------------------------------------------------------------ | ------------------------------------------------------------ |
| 特定侧 | 外部 IP         | 1. 协议解析：如 AXI AWVALID/AWREADY 信号处理；<br />2. 事务适配：数据宽度转换（如 32 位→64 位）；<br />3. 信号同步：外部时钟与 NoC 时钟同步； | 协议类型（AXI/AHB/OCP）、<br />数据宽度（8~256 位）          |
| 通用侧 | NoC 核心（PTU） | 1. NTTP 包生成 / 解析：将事务转换为包（Initiator NIU）或反之（Target NIU）；<br />2. 事务拆分 / 重组：大 burst→NOC 支持的包大小；<br />3. 安全检查：验证事务安全等级与 Target 匹配； | 最大包长度（1~256 字节）、<br />安全等级（Secure/Non-Secure） |

##### NIU transaction 发起限制

存在多种限制 FlexNoC NIU 发起事务能力的场景。

例如，FlexNoC 通用发起端 NIU 可能因以下原因暂停请求流处理：

- 无可用的未完成事务上下文（pending transaction context）；

- 可用的 pending order ID 数量小于最大未完成事务数；

- 可用的重组缓冲（reassembly buffer）数量小于最大未完成事务数；

- 避免读响应交织：若发起端 NIU 配置为不支持读响应交织（即通用参数 `minInterleaveSize` 设为 0），则 NIU 可能暂停，以防止因发起端拆分或目标端交织导致的读响应交织；

- 排序约束：事务与之前发往其他目标的事务使用相同的序列 ID（sequence ID）；

- 启用流量整形（shaping）。

此外，FlexNoC NIU 的某些特殊特性也可能暂停请求流处理，例如：

- 运行时设置为 limiter mode 的 QoS box；
- 当 NIU 实现重排序缓冲时，无可用的重排序缓冲资源。

有时，限制也会通过为第三方协议配置 FlexNoC 专用 NIU 间接引入，典型示例包括：

- ARM AHB 协议的未完成事务处理；
- 无写响应的 OCP 套接字的 order ID 分配；
- ARM AXI 套接字的序列 ID 分配（AXI ID 需压缩为 FlexNoC 通用信号`SeqID`）。

这些限制可能与通用发起端 NIU 的排序相关问题叠加出现。

限制也可能由目标端施加，最典型的是 “未完成事务数量限制”；或由可分配的序列 ID 数量限制（当参数`seqIDAllocation`设为 “动态（DYNAMIC）” 模式（架构：性能：通用 NIU）时，可分配序列 ID 数量为 2）；亦或由第三方套接字协议（如 AHB）施加。

#### NIU transaction 处理操作

#### 处理步骤

- Narrow burst packing
- Splitting into precise bursts
- MRMD to SRMD conversion
- Address decoding and target-based splitting
- Wraping burst alignment
- Word alignment
- Burst splitting or SRMD->MRMD conversion

#### 包传输单元（PTU）：包传输层核心

PTU 由四类组件构成，NOC 详细定义各组件的功能、参数与适配逻辑：

- 交换机（Switch）：

  - 功能：n 输入 /m 输出节点，含解复用器（Demux）、复用器（Mux）、内部路由表，按 RouteId 转发包；

  - 仲裁策略：支持优先级仲裁（基于 Urgency 字段）、公平仲裁（Round-Robin）；

  - 参数：Architecture: Datapath Transport: Switch: nInput/nOutput（输入 / 输出端口数，1~32）。

- 链路（Link）：

  - 功能：连接交换机与 NIU，支持可选 FIFO（缓解带宽不连续）、流水线（优化时序）；

  - 参数：
    - Architecture: Datapath Transport: Link: fifoDepth（FIFO 深度，0~256 字）；
    - Architecture: Datapath Transport: Link: nPipe（流水线级数，0~8）。

- 时钟适配器（Clock Adapter）：

  - 功能：处理时钟域交叉，按域关系自动生成类型（异步 / 同步 / SubEdge）；

  - 适配逻辑：
    - 异步域：双同步 FIFO（6 字深确保全吞吐量）+ 格雷码计数器；
    - 同步域：基于 Clk_En 的流控，无 FIFO；

  - 参数：dmpFlexNoC_ClockingAndReset.pdf 第 3 章 “Clock Adapter Units”。

- 序列化适配器（Serialization Adapter）：

  - 功能：转换包的传输宽度（如 64 位→32 位），优化布线资源；

  - 参数：Architecture: Datapath Transport: Serialization: nBytePerWord（传输宽度，1~16 字节）、headerPenalty（头部传输周期，NONE/ONE/TWO/AUTO）。

### 时钟与复位设计

#### 时钟体系结构

##### 时钟束（Clock Bundle）

时钟域的信号集合，NOC 定义 7 类必需信号，确保时钟、复位、控制的协同：

| 信号名称              | 方向 | 功能描述                                              | 关联参数（Location）                                    |
| --------------------- | ---- | ----------------------------------------------------- | ------------------------------------------------------- |
| Clk                   | 输入 | 时钟信号（上升沿有效，经门控后输出）                  | Specification: Domain: Clock: rootClock                 |
| ResetN                | 输入 | 标准复位信号（低电平有效，复位所有非保留 DFF）        | Specification: Domain: Clock: resetN                    |
| RetentionResetN       | 输入 | 保留 DFF 复位信号（仅复位保留寄存器，暖复位时不生效） | Specification: Domain: Clock: retentionResetN           |
| Clk_En                | 输入 | 时钟使能信号（由根时钟同步，控制时钟 gated 状态）     | Architecture: Domain: Clock: type: enablePortSource     |
| Clock-Gating Activity | 输入 | 时钟门控活动信号（标识门控状态）                      | Structure: Global parameters: clockGatingAlgorithm      |
| TestMode              | 输入 | 测试模式信号（高电平禁用时钟门控，支持扫描测试）      | Specification: Domain: Clock: mainSignals: testMode     |
| Pwr_Idle              | 输出 | 空闲信号（标识单元无需时钟，触发时钟门控）            | Structure: Parameters: Nodes: interruptPropagationDelay |

##### 时钟管理器（Clock Manager）

生成并管理时钟束，是时钟域的核心控制单元，NOC 详细定义其参数与时钟类型：

- 核心参数（Location：Specification: Domain: Clock）：

  - rootClock：指定输入根时钟端口（需在 Ports 页面预先定义输入时钟端口）；

  - resetN：分配复位端口（ResetN 类型，支持点击 “Create new” 新建端口）；

  - retentionResetN：保留复位端口（默认 None，此时复用 resetN 的信号）；

  - testMode：测试模式端口（默认 None，需在 Ports 页面定义 TestMode 类型端口）。

- **时钟类型配置（参数 type）**：

| 时钟类型 | 生成逻辑 | 关键参数（Location） | 适用场景 |
   |-------------------|--------------------------------------------------------------------------|-------------------------------------------|-----------------------------------|
   | Root | 直接复制根时钟，Clk_En/ClkEnS 硬连 1 | 无额外参数 | 主时钟域（如 CPU 核心域，400MHz） |
   | Static | 根时钟固定分频（参数 ratio：1~64），Clk_En 由内部计数器生成 | Architecture: Domain: Clock: type: ratio（分频比） | 固定频率域（如外设域，133MHz） |
   | Dynamic | 根时钟的同步子集（由 enablePortSource 指定外部使能端口），Clk_En 由外部信号驱动 | Architecture: Domain: Clock: type: enablePortSource（使能端口） | 动态频率调整域（如 GPU 变频域） |
   | Dynamic without enable | 无使能信号，时钟边沿与根时钟同步但可独立使能 | Architecture: Domain: Clock: type: minRatioToRoot（最小分频比） | 低功耗低频域（如传感器域，32MHz） |
   | Copy of | 复制同一 Regime 内的其他时钟（参数 clock 指定源时钟） | Architecture: Domain: Clock: type: clock（源时钟） | 跨电源域镜像（如安全域与非安全域） |

- 客户自定义单元（Customer Cell）：

     - 功能：需用户补全逻辑，包括复位同步（匹配系统复位方法论，如异步复位同步释放）、分频逻辑（按时钟策略生成 Clk_En）；

     - RTL 生成：FlexArtist 导出 rtl.ClockManagerCell.v 文件，可实例化为外部组件（ synthesis 用）或行为描述（仿真用）；

     - 输出：时钟束信号（Clk、ResetN、Clk_En 等），需确保与 NoC 核心的时序匹配。


##### 时钟门控（Clock Gating）

NOC 定义**两级门控**，平衡功耗与面积，参数可配置：

- 单元级时钟门控：

  - 触发逻辑：FlexArtist synthesis 引擎自动从链路、NIU、交换机中推断门控单元，避免冗余；

  - 配置参数：Structure: Global parameters: clockGatingAlgorithm：
    - OnePerSwitchOnePerNIU（默认）：每个交换机 / NIU 生成一个全局门控单元，优化时钟树；
    - NoClockGating：禁用单元级门控（如 FPGA 设计，避免时钟树逻辑冲突）。

- 寄存器级时钟门控：

  - 实现方式：RTL 中所有寄存器均连接使能信号（如 Clk_En），综合工具（如 Synopsys DC）利用该信号局部门控时钟；

  - 优势：最小化寄存器动态功耗（空闲时无时钟翻转），无需额外门控单元资源。

#### 时钟适配器（Clock Adapter）

处理时钟域交叉，NOC 按跨域场景自动生成适配类型，确保无亚稳态、无数据丢失：

| 跨域场景                        | 适配器类型                   | 核心逻辑                                                     | 面积开销                |
| ------------------------------- | ---------------------------- | ------------------------------------------------------------ | ----------------------- |
| 不同时钟 Regime（异步）         | 异步适配器                   | 双同步 FIFO（6 字深确保全吞吐量）+ 格雷码计数器（避免指针 skew 超过 1 周期） | 大（需 FIFO + 同步器）  |
| 同一 Regime，时钟边沿同步       | 同步适配器                   | 基于 Clk_En 的流控，无需 FIFO，仅用简单计数器（如 Clk_En 高电平采样数据） | 中（仅计数器）          |
| 同一 Regime，低频边沿与高频对齐 | SubEdge 适配器               | 无缓冲 / 亚稳态处理（低频边沿完全重合于高频边沿，如 133MHz 边沿对齐 400MHz） | 小（仅组合逻辑）        |
| 长距离跨域（如宏分区间）        | 准同步（Mesochronous）适配器 | 插入流水线寄存器，时钟反向传输以对齐数据采样边沿（避免时钟树平衡复杂） | 中（流水线 + 反向时钟） |

#### 复位策略

FlexNoC 复位信号均为**低电平有效（ResetN）**，NOC 定义复位配置、类型及仿真约束：

##### 核心配置参数

| 参数名称       | 选项                     | 功能描述                                                     | 适用场景                                                     |
| -------------- | ------------------------ | ------------------------------------------------------------ | ------------------------------------------------------------ |
| resetRegisters | Minimal/All              | Minimal：仅状态机 DFF 配复位引脚（减少面积，数据路径 DFF 未初始化）；<br />All：所有 DFF 配复位引脚（简化仿真，确保初始状态已知） | Minimal：面积优先设计（手机 SoC）；<br />All：仿真优先设计（早期验证） |
| resetDFFPin    | Synchronous/Asynchronous | 同步复位：复位断言 / 解除需与 Clk 上升沿同步（无亚稳态）；<br />异步复位：复位断言无时钟依赖，解除需与 Clk 同步（响应快） | 同步：时序敏感模块（交换机仲裁器）；<br />异步：紧急复位（安全违规） |

##### 复位类型细节

- 同步复位（resetDFFPin=Synchronous）：

  - 时序要求：复位断言（ResetN=0）需在 Clk 上升沿前稳定，解除（ResetN=1）需与 Clk 上升沿同步；

  - 优势：无亚稳态风险，适合对时序敏感的模块（如交换机的路由表寄存器）；

  - 劣势：复位响应慢（需等待时钟边沿，如 6 个边沿 for resetRegisters=Minimal）。

- 异步复位（resetDFFPin=Asynchronous）：

  - 时序要求：复位断言无时钟依赖（ResetN=0 立即生效），解除（ResetN=1）需与 Clk 上升沿同步；

  - 约束：ResetN 需无毛刺（即使存在串扰），时钟管理器复位输出需组合逻辑生成（避免复位延迟）；

  - 优势：复位响应快（紧急场景如安全违规），仿真初始状态稳定。

##### 部分复位与仿真延迟

- 部分复位风险：

  - 问题：时钟适配器的读 / 写格雷码计数器分属两个时钟域，单独复位一个域会导致计数器错位，生成错误包；

  - 解决方案：
    - 默认支持部分复位，需确保跨域复位重叠（如同步域复位先断言，异步域复位后断言，解除顺序相反）；
    - 禁用部分复位：在 Architecture 阶段设置 “partialResetSupport=False”，强制全局复位。

- 仿真复位延迟：

  - 定义：复位后需等待的最小时钟边沿数，确保所有存储单元进入已知状态；

  - 延迟表（基于参数组合）：

| resetRegisters | resetDFFPin  | 最小时钟边沿数 | 原因分析                                   |
| -------------- | ------------ | -------------- | ------------------------------------------ |
| Minimal        | Asynchronous | 5              | 仅状态机 DFF 复位，数据路径需 5 个边沿稳定 |
| Minimal        | Synchronous  | 6              | 同步复位响应慢，需多 1 个边沿确保同步      |
| All            | Asynchronous | 0              | 所有 DFF 复位，立即进入已知状态            |
| All            | Synchronous  | 1              | 同步解除需 1 个边沿                        |

- 扩展：若客户单元（如两级同步器）需额外延迟，需在复位序列中增加对应边沿（如同步器需加 2 个边沿）。

### 可观测性设计

NOC 设计了 “探针 - 观测器 - 观测网络” 三级架构，实现全链路数据采集与分析，支持错误追踪、性能统计、事务调试。

#### 探针（Probe）：数据采集单元

探针部署于 NoC 关键节点，按功能分 4 类：

##### 错误探针（Error Probe）

- **触发条件**：自动生成于 “Errors” 类型探针点（Link/NIU 输出），检测 Status=ERR 的包；
- **错误码支持**：需设置全局参数 useErrorCodes=True（Specification: Global parameters），支持 8 类错误码：

| 错误码 | 来源                   | 类型描述                             | 处理逻辑                                      | 日志内容                         |
| ------ | ---------------------- | ------------------------------------ | --------------------------------------------- | -------------------------------- |
| SLV    | Target                 | Target 返回错误（如内存 ECC 错误）   | 记录 Addr、RouteId，触发观测器中断            | RouteId、Addr、错误码、User 标志 |
| DEC    | Initiator NIU          | 地址解码错误（非法地址）             | 需配置默认错误 Target，确保地址可重构         | RouteId、非法 Addr、Initiator ID |
| UNS    | Target NIU             | 不支持的请求（如 burst 长度超限）    | 拒绝事务，返回 ERR 响应                       | RouteId、请求类型、burst 长度    |
| DISC   | 电源断开单元           | Target / 域电源断开                  | 生成 DISC 错误包，不转发至 Target             | RouteId、电源域 ID               |
| SEC    | Initiator NIU / 防火墙 | 安全违规（非安全访问安全域）         | 记录安全等级，触发安全中断                    | RouteId、安全等级、访问地址      |
| HIDE   | 防火墙                 | 隐藏安全违规（对 Initiator 返回 OK） | 仅日志记录，不通知 Initiator                  | RouteId、安全等级、隐藏标志      |
| TMO    | Target NIU             | 事务超时（Target 无响应）            | 超时计数器（如 1000 周期）触发，返回 TMO 错误 | RouteId、超时周期、Target ID     |
| RSV    | 预留                   | 未定义错误                           | 按默认错误处理（转发至默认错误 Target）       | RouteId、错误码 = 7              |

- **输出**：错误包发送至观测器日志，支持中断触发（观测器断言 Fault 信号）。

##### 包探针（Packet Probe）

- **部署位置**：“Packets” 类型探针点（Link 输出），支持多探针点共享一个探针硬件（减少面积）；

- 核心功能与参数：

  - 包过滤

    - 配置参数：nFilter（0~4 个过滤器，Architecture: Observability: Probe）；

    - 过滤维度：
      - RouteId：通过 Filters_N_RouteIdBase/RoutIdMask 匹配 InitFlow/TargFlow/TargSubRange；
      - 地址：Filters_N_AddrBase_Low/High + WindowSize 匹配地址范围；
      - Opcode：Filters_N_Opcode 匹配 RD/WR/RDX 等事务类型；
      - User/Security 标志：Filters_N_UserBase/Mask、Filters_N_SecurityBase/Mask；
  
    - 逻辑组合：FilterLut 寄存器（2^n Filter 位）定义过滤器组合（如 F0&F1、F0|F2）。
  
  - 包追踪：

    - 头部追踪：默认使能，记录 RouteId、Addr、Opcode、Len1；

    - 载荷追踪：需设置 allowPayloadTracing=True（Architecture: Observability: Probe: tracing），运行时通过 MainCtl 寄存器 PayloadEn 使能；

    - 模式：
      - Spy（默认）：非侵入式，丢包不阻塞 datapath（适合正常运行）；
      - Intrusive：侵入式，阻塞流控确保不丢包（适合调试）。
  
  - 统计收集：

    - 配置参数：
      - statisticsCollection=True（使能统计）；
      - nStatisticsCounter（1~32 个计数器，Architecture: Observability: Probe: statisticsCollection）；
      - wStatisticsCounter（8~32 位计数器宽度）；
      - statisticsCounterAlarm=True（使能计数器超阈值告警，StatAlarm 信号）；
  
    - 统计项：
      - 包数（PKT）、字节数（BYTE）、带宽（字节数 / 时间）；
      - 压力水平（PRESS0/PRESS1/PRESS2，基于 Press 信号）；
      - 外部事件（EXT_EVENT，如 Target IP 的中断信号）。
  

##### 事务探针（Transaction Probe）

**部署位置**：Initiator NIU 的 “Transactions” 探针点，仅监测事务级信号（不影响包传输）；

核心功能与参数：

- 延迟测量：

  - 触发事件：
    - Start：请求开始（如 AXI AWVALID=1）；
    - Stop：响应开始（如 AXI BVALID=1）或请求接受（如 AXI AWREADY=1）；

  - 阈值配置：delayThresholds（分 bin 阈值，如 0~1、2~4、5~9、10~99、100 + 周期）；

  - 参数：
    - nObservable（1~4 个可观测 NIU，Architecture: Observability: Probe: transactionProfiling）；
    - nCounters（4~272 个计数器，需满足 NIU 的最大 Pending 事务数）；
    - wCounters（3~11 位，支持最大阈值 = 2^w Counters-1）。

- Pending 事务计数：

  - 测量：同时活跃的事务数（如 CPU 发起的未完成读事务）；

  - 阈值配置：pendingThresholds（如 0~2、3~5、6 + 个）；

  - 应用：评估 Initiator 的事务并发能力（如 DMA 的最大 Pending 数）。

##### 通用探针（Universal Probe）

**适用场景**：监测 NoC 外部的 IP 间事务（无 NoC 互连场景），如独立 CPU 与外设的通信；

核心组件与参数：

- 请求端口：

  - 配置：nRequestPorts（0~8 个，Specification: Interface: Observation）；

  - 信号：Id（0~16 位）、Address（8~48 位）、Length（2~12 位）、Prio（0~3 位）、User（0~256 位）、Security（0~8 位）；

  - 功能：监测接口状态（IDLE/BUSY/XFER），计数请求事件。

- 事务端口：

  - 配置：nTransactionPorts（0~16 个，Specification: Interface: Observation）；

  - 信号：Req_Valid/Req_Ready、Req_TId（1~7 位）、Rsp_Valid/Rsp_Ready、Rsp_TId（1~7 位）；

  - 功能：测量延迟 /pending 事务，需适配外部协议（如 AXI 的 AW/W、AR/R 通道）。

- 观测网络：

  - 独立 NoC 承载探针数据，仅含服务 Target（配置寄存器）；

  - 时钟：探针时钟可独立于事务时钟（需≥事务时钟频率）。

#### 观测器（Observer）：数据处理与输出单元

观测器接收探针数据，实现日志存储、调试输出与告警：

##### 错误日志管理

多日志链配置：

- 参数：errorLoggers（1~8 个日志，Specification: Interface: Observation: Observer）；
- 过滤：每个日志支持：
  - ignoredErrorCodes：忽略的错误码（如忽略 RSV 错误）；
  - flagFiltering：User 标志过滤（如仅记录 secure=1 的错误）；
  - forward：错误包是否转发至下一日志 / ATB 接口（True = 转发）。

- **日志内容**：错误包的 RouteId、Addr、错误码、时间戳（若使能），支持中断触发（日志非空时断言 Fault 信号）。

##### 调试输出接口

支持两类行业标准接口，适配 SoC 调试系统，参数配置明确：

| 接口类型 | 配置参数（debugOutput，Location）                            | 核心功能                                                     | 关键子参数                                                   |
| -------- | ------------------------------------------------------------ | ------------------------------------------------------------ | ------------------------------------------------------------ |
| ATB      | Specification: Interface: Observation: Observer: debugOutput=ATB | 输出 Trace / 统计数据，支持多观测器无冲突；<br />ATB ID 唯一标识观测器； | AtbId（0~15，唯一）；<br />SyncPeriod（同步周期，1~256 周期） |
| STPv2    | Specification: Interface: Observation: Observer: debugOutput=STPv2 | 封装 ATB 数据，含时间戳同步； <br />支持 MIPI 调试系统；     | wTimeStamp（0~48 位，时间戳宽度）； <br />AsyncPeriod（同步序列周期，1~1024 周期） |

##### 时间戳同步

**探针端**：TimeStamp 信号（输入，宽度 = wTimeStamp）为事件添加绝对时间戳，精度为探针时钟周期；

**观测器端：**

- 计算时间差（当前时间戳 - 上一时间戳），附加到 STPv2 包头部；

- 同步序列：观测器定期发送同步序列（含当前时间戳），确保调试系统时间同步；

应用：定位事件时序（如错误发生顺序、事务延迟分布）。

#### 观测网络调优

观测网络为探针→观测器的专用传输网络，NOC 提供调优策略：

- 带宽适配：

  - 低带宽场景（仅错误日志 / 统计）：nBytePerWord=1、headerPenalty=AUTO（8 位序列化，节省布线）；

  - 高带宽场景（包追踪）：nBytePerWord=4（32 位）、headerPenalty=NONE（头部独立传输，减少延迟），匹配 ATB 输出速率。

- 跨域处理：

  - 异步域：插入 6 字深 FIFO（确保全吞吐量，dmpFlexNoC_ClockingAndReset.pdf 第 3 章）；

  - 同步域：利用时钟适配器使能信号，避免 FIFO 资源。

- 电源域管理：

  - 观测器电源域：建议设为 SUPPLY 类型（正常工作时断电，调试时上电，dmpFlexNoC_Power_Management.pdf 第 2 章）；

  - 跨电源域：设置 powerStall=CONST_ERR（目标域断电时丢包，无响应路径）。

### NIU 事务处理详解

该节定义 NIU 的协议转换、事务拆分重组、安全检查逻辑，是事务层实现的核心模块。

#### 协议转换：外部协议→NTTP

以 **AXI4→NTTP** 为例，NOC 拆解转换步骤，确保协议语义无丢失：

##### Initiator NIU（AXI4→NTTP）

- AW 通道处理：

  - 解析 AXI 信号：
    - AWADDR→NTTP Addr（需匹配全局地址映射，生成 RouteId）；
    - AWLEN→NTTP Len1（AXI burst 长度 - 1，如 AWLEN=7→Len1=7，8 字节）；
    - AWBURST→NTTP Opcode（FIXED→RD/WR，INCR→RDW/WRW，WRAP→RDW/WRW）；
    - AWSIZE→NTTP Be（字节使能，如 AWSIZE=2→Be=1111，32 位数据）；

  - 生成 NTTP 请求包头部：填充 RouteId、Opcode、Addr、Len1、Be、Security、User 标志。

- W 通道处理：

  - 数据写入：AXI WDATA→NTTP 包 Payload，WSTRB→NTTP Be（补充字节使能）；

  - 包生成：若 burst 长度超过 NOC 最大包长度（如 256 字节），拆分多个 NTTP 包，共享同一 RouteId。

- B 通道处理：

  - 接收 NTTP 响应包（Status=RSP/ERR）；

  - 生成 AXI B 信号：BRESP（OKAY/ERROR，对应 NTTP Status=RSP/ERR）、BVALID（响应有效）。

##### Target NIU（NTTP→AXI4）

- 请求包解析：

  - 提取 NTTP 头部：RouteId→Target ID，Opcode→AXI 事务类型，Addr→AXI AWADDR/ARADDR，Len1→AXI AWLEN/ARLEN；

  - 生成 AXI 请求：AWVALID/ARVALID=1，传递 Addr、Len、Burst 类型。

- 响应生成：

  - 接收 AXI 响应：BRESP/RRESP→NTTP Status（OKAY→RSP，ERROR→ERR），RDATA→NTTP Payload；

  - 生成 NTTP 响应包：填充 RouteId、Status、Payload（读数据），发送至 Initiator NIU。

#### 事务拆分与重组

NOC 定义拆分 / 重组的触发条件与逻辑，确保大事务适配 NoC 传输：

##### 事务拆分（Initiator NIU）

- **触发条件**：外部事务 burst 长度超过 NOC 最大包长度（由 PTU 参数 maxPacketLength=256 字节限制）；

- 拆分逻辑：

  - 按 maxPacketLength 拆分 burst 为 N 个片段（如 512 字节→2 个 256 字节片段）；

  - 每个片段生成独立 NTTP 包，共享同一 RouteId，按顺序发送；
  
  - 片段标识：在 NTTP 包头部添加 “片段序号”（0~N-1），确保重组顺序。

##### 事务重组（Target NIU）

- **触发条件**：接收多个同一事务的 NTTP 包（RouteId 相同，片段序号连续）；

- 重组逻辑：

  - 按片段序号排序（忽略乱序包，等待重传）；

  - 合并 Payload 数据（如 2 个 256 字节→512 字节）；
  
  - 生成完整 AXI 事务：AWLEN = 原 burst 长度 - 1，发送至 Target IP。

#### 安全检查与错误处理

##### 安全检查

- 配置：在 Specification 阶段定义：

  - Initiator 安全等级（Secure/Non-Secure，Specification: Interface: NIU: security）；
  - Target 安全域（Secure/Non-Secure，Specification: Interface: Socket: securityDomain）；
  
- 检查逻辑：

  - Initiator NIU 验证事务安全等级与 Target 安全域匹配；

  - 不匹配时生成 SEC 错误包（dmpFlexNoC_AddressingAndSecurity.pdf 第 3 章），发送至默认错误 Target。

##### 错误处理

- 地址错误（DEC）：

  - 触发：Initiator NIU 解析 AWADDR/ARADDR，无匹配的 Target 映射；
  - 处理：生成 DEC 错误包，发送至默认错误 Target（需在 Specification 阶段配置 defaultErrorTarget）。
  
- Target 错误（SLV）：

  - 触发：Target IP 返回 AXI BRESP/RRESP=ERROR；
  - 处理：Target NIU 生成 SLV 错误包，转发至观测器日志。
  
- 超时错误（TMO）：

  - 配置：Target NIU 参数 timeout=1000 周期（Specification: Interface: NIU: timeout）；
  - 触发：发送 AXI 请求后，1000 周期内无 B/R 响应；
  - 处理：生成 TMO 错误包，中断 Target 事务。

### 物理实现

聚焦 NOC 的物理设计落地，涵盖布局、布线、时序、CTS，确保满足频率与面积目标。

#### 布局规划（Floorplanning）

自动布局（FlexNoC FloorPlanner）

- 核心策略：

  - 拓扑匹配：按 NoC 拓扑划分物理区域（如交叉开关→中心区域，NIU→靠近对接 IP）；

  - 域隔离：时钟 / 电源域边界对齐物理分区（如 Secure 域→左侧，Non-Secure 域→右侧），减少跨域布线；
  
  - IP 对齐：Initiator NIU 布局靠近 CPU/DMA，Target NIU 布局靠近内存 / 外设，缩短接口布线。

- **工具操作**：FlexArtist Floorplan Viewer（Structure: Floorplan 页面），导入 SoC floorplan 后自动生成 NoC 布局。

##### 手动布局约束

- **模块位置**：通过 FlexArtist Structure Editor 指定模块（交换机、时钟管理器）的物理区域（如 X: 100~200μm，Y: 300~400μm）；
- **禁止区域**：标记布线阻塞区域（如其他 IP 核、电源条），避免 NoC 布线绕远（增加延迟）。

#### 布线优化

NOC 提供布线优化策略，平衡延迟与面积：

- 序列化适配：

  - 长链路（如跨芯片半高，>500μm）：采用低序列化（nBytePerWord=1），减少布线数量（如 64 位→8 位，布线减少 87.5%）；

  - 短链路（<200μm）：采用高序列化（nBytePerWord=8），减少延迟（如 8 字节 / 周期→64 字节 / 周期）。

- 流水线插入：

  - 触发：长链路时序不满足（如 400MHz 时钟，链路延迟 > 2ns）；

  - 操作：在链路中插入流水线寄存器（Architecture: Datapath Transport: Link: nPipe=1~8），打破时序路径。

- 时钟域布线：

  - 异步域：布线避免与时钟线平行，减少串扰（串扰会导致复位毛刺，dmpFlexNoC_ClockingAndReset.pdf 第 4 章）；

  - 同步域：布线共享时钟树，优化时延（如同一 Regime 的链路布线靠近时钟树分支）。

#### 时序约束

##### 时钟约束

- 时钟定义：为每个时钟域定义：

  - 周期：如根时钟 400MHz→周期 2.5ns，Static 时钟 133MHz→周期 7.5ns；
  - 占空比：默认 50%（可配置为 40%/60%，适应不同 IP 需求）；
  - 不确定性：添加时钟抖动（Jitter=±100ps）与偏斜（Skew=±200ps）。
  
- 工具命令：在综合脚本中添加：

  ```tcl
  create_clock -name clk_400mhz -period 2.5 [get_ports clk_400mhz]
  set_clock_uncertainty 0.1 [get_clocks clk_400mhz]
```

##### 跨域路径约束

- **异步域**：设置 set_max_delay=80% 的发送时钟周期（如 400MHz→2.0ns），避免指针 skew 超过 1 周期（dmpFlexNoC_ClockingAndReset.pdf 第 3 章）；

- 同步域：设置 set_setup/hold 约束，基于时钟使能信号优化：

  ```tcl
  set_setup_multicycle_path 2 -from [get_clocks clk_400mhz] -to [get_clocks clk_133mhz]
```

#### 时钟树综合（CTS）

- **时钟树结构**：按时钟域生成独立时钟树，时钟管理器为根节点，门控单元为分支节点；

- 优化策略：

  - 负载平衡：确保同一时钟域内各模块的时钟负载差异 < 20%（避免偏斜过大）；

  - 门控整合：将单元级时钟门控单元作为时钟树分支节点，减少时钟树缓冲器数量；
  
  - 时序优化：平衡时钟树时延，确保同一时钟域内时钟偏斜 < 200ps。

- **工具支持**：Synopsys ICC2，导入 FlexArtist 生成的时钟约束文件（clock_constraints.tcl）。

### 功耗管理

定义 NoC 的多维度功耗优化策略，覆盖时钟、电源、寄存器三级，满足低功耗需求。

#### 电源域划分

- **划分原则**：按功能模块（主 NoC、外设 NoC、观测网络）划分电源域，支持独立开关；

- 参数配置：

  - IPpowerDomain（Specification: Interface: Observation/Socket）：指定模块的电源域（如 “Main_PD”“Periph_PD”）；
  - disconnect 协议（Specification: Interface: Power: disconnect）：电源断开时的流控策略：
    - CONST_ERR：生成 DISC 错误包，不转发事务；
    - STALL：阻塞事务，电源恢复后继续。
  
- **示例**：将观测网络电源域设为 “Debug_PD”，正常工作时断电，调试时上电，节省 30% 空闲功耗。

#### 功耗优化机制

- 时钟门控：

  - 单元级：OnePerSwitchOnePerNIU（默认），空闲时关闭交换机 / NIU 的时钟；

  - 寄存器级：RTL 寄存器连接 Clk_En，综合工具局部门控，减少动态功耗（文档第 3 章）。

- 电源开关：

  - 低功耗模式：关闭非关键电源域（如外设 NoC），仅保留主 NoC；

  - 唤醒机制：通过服务网络发送唤醒信号（如写电源控制寄存器），开启电源域，唤醒时间 < 100μs。

- 保留寄存器：

  - 配置：retentionResetN 参数指定保留复位端口（Specification: Domain: Clock: retentionResetN）；

  - 优势：电源域唤醒后无需重新初始化数据（如路由表、安全配置），减少唤醒时间与功耗。

#### 功耗分析与优化

- 工具支持：FlexExplorer（dmpFlexNoC_Concepts.pdf 第 2 章），仿真各模块的动态 / 静态功耗：

  - 动态功耗：交换机（仲裁器、路由表）、NIU（协议转换）、链路（流水线寄存器）；
  - 静态功耗：时钟树缓冲器、FIFO（异步适配器）。
  
- 优化方向：

  - 高功耗模块：异步适配器（FIFO）→减少跨异步域数量；
  - 空闲模块：观测网络→电源域断电；
  - 时钟频率：Dynamic 时钟→根据负载调整频率（如 GPU 空闲时从 400MHz→200MHz）。

### 服务质量（QoS）

定义 NoC 的 QoS 机制，确保关键流量的带宽与延迟需求，同时优化普通流量的资源利用。

- Fix mode 
  - 功能： 将该I-NIU发出的transaction的Qos 值fix成某个特定值，rd & wr 单独可配
  - 优缺点：对性能影响最显著，但是很难实现动态配置，如果将某个大带宽的IP fix值配置很高，会带来问题◦
  - 配置流程：配置成Qos Generator Mode，mode配置成Fixed 模式：SocketQosEn = 0 ； Mode = 0；
    配置fix value： P0 =Wr ； P1 =Rd。
- Limiter mode
  - 功能：实时检测Initiator NIU 侧request带宽，如果比预设带宽值高，在将带宽控制在预设值以下；如果比带宽预设值低，就将带宽控制在预设值以上；
    在这种模式下，Qos值可以来源于Generator，也可以来源于外部接口信号，配置方式参照2.1；如果来源于Generator，属于fix mode。
  - 优缺点：通过限制带宽的方式对某个IP 进行实时监控，如果不调整Qos value的情况下，主要用于限制带宽，不会让带宽有更大的提升；
  - 配置流程：配置为limiter mode：Mode=2’b01；配置Saturation：定义的是payload counter的容限；设置的是当前配置带宽下的burst 容限（计算方式：burst-trans（bytes）/16）。需要使用时，使能ExtLimiten。
- Regulator mode
  - 功能：实时检测Initiator NIU response的带宽，当前低于配置带宽的时候，使用P1的Qos值，当前带宽高于配置带宽的时候，使用P0的带宽值
  - 优缺点：是一种更加柔和得限制带宽的方式；
  - 配置流程：SocketQosEn = 0 ； Mode = 3；此时Qos值来源于qos-box，regulator mode；配置priority p0 &p1的值 ； p0配置为带宽超出预期的值；p1配为带宽低于预期的值；配置Saturation：定义的是payload counter的容限；设置的是当前配置带宽下的burst 容限（计算方式：burst-trans（bytes）/16）

#### 优先级机制

- 优先级编码：

  - 包头部 Urgency 字段采用 bar-graph 编码（0~nUrgencyLevel-1 级），高优先级（如 Urgency=3）优先仲裁；
  - 配置参数：nUrgencyLevel（Specification: Global parameters: nUrgencyLevel=1~8 级）。
  
- 仲裁策略：

  - 交换机采用 “优先级 + 公平” 仲裁：高优先级包优先，同优先级按 Round-Robin 公平分配；
  - 示例：CPU→内存的事务（Urgency=3）优先于外设→CPU 的事务（Urgency=1），确保 CPU 访存延迟。
  
- 优先级映射：

  - Initiator NIU：将外部协议优先级（如 AXI AWPRIORITY）映射为 NTTP Urgency 字段；
- 配置：Architecture: Datapath NIU: priorityMapping（AWPRIORITY [3:0]→Urgency [2:0]）。

#### 带宽控制

##### FIFO 缓冲

- **触发条件**：链路两端带宽不连续（如 400MHz 64 位→133MHz 32 位，带宽比 = 6:1）；
- **配置参数**：Architecture: Datapath Transport: Link: fifoDepth=8~256 字；
- **功能**：缓存高带宽端的包，按低带宽端速率发送，避免包丢失与流控扩散（如阻塞 CPU 事务）。

##### 速率适配器（Rate Adapter）

- **触发条件**：低带宽域接收高带宽域数据，等待周期过多（如 133MHz 接收 400MHz 数据，每 3 周期接收 1 次）；

- 功能：

  - 缓存足够数据（如 2 个包）后批量发送；

  - 减少等待周期（如 3 周期→1 周期），提升链路效率；
  
- **配置**：Architecture: Datapath Transport: Link: rateAdapter=True。

#### 压力管理（Pressure Management）

- 压力信号（Press）：

  - 定义：侧带信号（0~3 级），标识链路流量压力（0 = 无压力，3 = 满负载）；
  - 生成：由链路 FIFO 的占用率生成（如 FIFO 占用 > 75%→Press=3）；
  - 配置：usePress=True（Specification: Global parameters: usePress）。
  
- 压力反馈：

  - Initiator NIU 接收 Press 信号，高压力时减少事务发送速率（如降低 burst 长度）；
- 优势：避免流控扩散（如外设压力不阻塞 CPU）。

#### VC-Link 隧道

- **功能**：多独立流量共享同一链路，通过 FIFO 隔离避免相互阻塞；

- 约束：

  - 最大共享链路数：8 条（Architecture: Datapath Transport: VC-Link: nVC=1~8）；
  - 时钟域：输入侧需同一时钟域，输出侧可不同（需时钟适配器）；
  - 流控：基于信用机制（Credit），确保 FIFO 不溢出。
  
- **适用场景**：环形 /torus 拓扑（如 AI 芯片的多核互连），减少布线资源（如 8 条流量→1 条链路 + 8 个 FIFO）。

### 安全性与功能安全

#### 安全性机制

##### 地址映射与安全隔离

- 全局地址空间：

  - Initiator/Target 的局部地址映射到全局地址，避免冲突（Specification: Mappings: Memory）；
  - 示例：CPU 局部地址 0x0000_0000→全局地址 0x8000_0000（内存），0x1000_0000→全局地址 0x9000_0000（外设）。
  
- 安全域划分：

  - 配置：
    - Target 安全域（Secure/Non-Secure，Specification: Interface: Socket: securityDomain）；
    - Initiator 安全等级（Secure/Non-Secure，Specification: Interface: NIU: securityLevel）；
  - 隔离：Initiator NIU 验证事务安全等级与 Target 安全域匹配，不匹配则生成 SEC 错误包。

##### 防火墙（Firewall）

- **部署**：在链路中插入防火墙（Architecture: Datapath Transport: Firewall）；

- 规则配置：

  - 基于 RouteId：禁止特定 Initiator→Target 的通信（如禁止外设 Initiator 访问安全内存）；
  - 基于 Addr：禁止访问特定地址范围（如 0x8000_0000~0x8FFF_FFFF 为安全内存，仅 Secure 事务允许）；
  - 基于 User 标志：禁止 User [0]=1 的事务（如禁止调试事务）；
  
- **错误处理**：非法包生成 SEC 错误（通知 Initiator）或 HIDE 错误（隐藏错误，不通知 Initiator）。

#### 功能安全

##### 安全目标与合规性

- **合规标准**：支持 ISO 26262（汽车功能安全），最高支持 ASIL D 等级；

- 安全目标：

  - 避免 NoC 错误导致的汽车功能失效（如制动系统通信中断）；
- 检测并恢复 NoC 错误（如包传输错误、时钟故障）。

#####  安全机制

- **错误检测**：

  - 包错误：头部 ECC/parity（dmpFlexNoC_ResilienceFeatures.pdf 第 3 章），检测传输错误；

  - 时钟故障：时钟监视器（检测时钟停振），触发全局复位；

  - 超时：Target NIU 超时检测（TMO 错误），避免事务永久阻塞。


- **错误恢复**：

  - 包重传：支持重传机制（需外部协议配合，如 AXI 重传请求）；

  - 域隔离：错误域断开（生成 DISC 错误），避免错误扩散；

  - 冗余：关键单元（时钟管理器、交换机）支持冗余部署，故障时切换备份。


##### 安全验证

- 测试策略：

  - 安全机制测试：注入 ECC 错误、时钟故障，验证检测 / 恢复逻辑；
  - 时序安全：确保安全相关路径（如复位、错误中断）满足时序约束；
  - 覆盖率：安全相关功能的代码覆盖率≥95%，故障注入覆盖率≥90%。
  
- **文档交付**：提供安全手册、FMEA（故障模式与影响分析）报告，支持客户 ASIL 认证。

### 弹性与错误恢复

定义 NoC 的错误检测与恢复机制，提升系统可靠性。

#### 错误检测

##### 包错误检测

- ECC/ parity：

  - 包头部 Resilience 字段含 ECC（64 位数据→8 位 ECC）或 parity 位（1 位 / 8 字节）；
  - 配置：Architecture: Datapath Transport: Resilience: useECC=True/useParity=True；
  - 检测：接收端验证 ECC/parity，不匹配则标记包错误（Status=ERR）。
  
- 包丢失检测：

  - 基于序列号（SeqId，包头部字段）：Initiator NIU 发送包时递增 SeqId，Target NIU 检测 SeqId 不连续→包丢失；
- 处理：触发重传请求（需外部协议支持）。

##### 链路错误检测

- 串扰检测：

  - 跨时钟域链路添加串扰监测逻辑（如双采样寄存器），检测信号毛刺；
  - 处理：毛刺导致的错误包标记为 ERR，转发至观测器。
  
- 断连检测：

  - 电源域断开时，电源管理单元生成 DISC 错误包（dmpFlexNoC_Power_Management.pdf 第 2 章）；
- 处理：禁止事务进入断开域，避免无效传输。

#### 错误恢复

- **包重传**：

  - 触发：包错误（ECC/parity 不匹配）、包丢失（SeqId 不连续）；

  - 逻辑：Initiator NIU 缓存已发送包，接收重传请求后重新发送；

  - 配置：Architecture: Datapath NIU: retransmission=True，缓存深度 = 8~256 包。


- **域隔离**：

  - 触发：错误域（如时钟故障、电源断开）；

  - 处理：断开错误域与其他域的链路，生成 DISC 错误包，避免错误扩散；

  - 恢复：错误修复后（时钟恢复、电源上电），重新连接链路，恢复通信。


- **冗余设计**：

  - 关键单元：时钟管理器、交换机支持冗余部署（Architecture: Datapath Transport: Redundancy: nRedundant=2）；

  - 切换：故障检测后（如时钟监视器检测停振），切换至备份单元，切换时间 < 100ns。


### 运行时配置

定义 NoC 的动态配置能力，支持运行时调整参数，无需重新合成。

#### 服务网络（Service Network）

- **功能**：低带宽管理网络，用于访问 NoC 寄存器（探针过滤器、QoS 优先级、安全规则）；

- **拓扑**：独立于 datapath，采用低序列化（nBytePerWord=1），支持跨域；

- 访问方式：

  - 服务 Initiator（如 CPU）通过服务 Socket 发送配置事务；
  - 目标寄存器：探针 Filter 寄存器、QoS 优先级寄存器、安全规则寄存器；
  - 协议：NTTP 协议，支持读 / 写寄存器（8/16/32 位）。

#### 寄存器映射

- 导出：FlexArtist 支持导出寄存器映射表（.txt/.xml 格式），含：

  - 寄存器地址：偏移量（如 0x0008=MainCtl）、基地址（如探针基地址 = 0x1000_0000）；
  - 复位值：默认值（如 MainCtl=0x00）；
  - 访问类型：读 / 写（R/W）、只读（R）；
  - 字段描述：如 MainCtl [0]=ErrEn（错误使能）。
  
- **工具操作**：Creation→Export→Register Map（FlexArtist 菜单）。

#### 动态配置场景

- **时钟调整**：

  - Dynamic 时钟：修改时钟管理器的 enablePortSource 寄存器（如使能信号从高→低，时钟频率从 400MHz→200MHz）；

  - 应用：GPU 空闲时降频，节省功耗。


- **QoS 调整**：

  - 修改探针 FilterLut 寄存器（如 F0|F2→F0&F2），调整包过滤逻辑；

  - 修改 QoS 优先级寄存器（如 CPU 事务 Urgency 从 3→2），适配系统负载。


- **可观测性调整**：

  - 运行时启用 / 禁用探针（MainCtl 寄存器 TraceEn=1/0），避免调试逻辑影响正常运行；

  - 修改观测器 ATB ID（AtbId 寄存器），避免多观测器冲突。


### 客户自定义单元

提供客户自定义单元的设计指南，支持用户根据系统需求补全 NoC 逻辑。

#### 时钟管理器客户单元

- 功能需求：

  - 复位同步：匹配系统复位方法论（如异步复位同步释放，需 2 级同步寄存器）；
  - 分频逻辑：按时钟类型生成 Clk_En（如 Static 时钟 ratio=2→Clk_En=1010...）；
  
- RTL 模板：FlexArtist 生成 rtl.ClockManagerCell.v，用户需补全：

  ```verilog
  module ClockManager_Core (
    input In_Clk,
    input In_RstN,
    output reg Out_Clk_En
  );
    // 异步复位同步释放
    reg rstn_sync1, rstn_sync2;
    always @(posedge In_Clk or negedge In_RstN) begin
      if (!In_RstN) begin
        rstn_sync1 <= 0;
        rstn_sync2 <= 0;
      end else begin
        rstn_sync1 <= 1;
        rstn_sync2 <= rstn_sync1;
      end
    end
    // 分频逻辑（ratio=2）
    reg [0:0] cnt;
    always @(posedge In_Clk or negedge rstn_sync2) begin
      if (!rstn_sync2) begin
        cnt <= 0;
        Out_Clk_En <= 0;
      end else begin
        cnt <= cnt + 1;
        Out_Clk_En <= (cnt == 0);
      end
    end
  endmodule
  ```
  
- **验证**：需与 NoC 仿真环境集成，验证 Clk_En 与时钟的同步性。

#### 探针客户过滤单元

- **适用场景**：标准过滤不满足需求（如复杂 User 标志组合，Fwd=Flag1&(Flag2|Flag3)）；

- 设计要求：

  - 接口：输入 User/Security 标志、Vld（数据有效），输出 Fwd（1 = 允许包通过）；
  - 逻辑：用户自定义组合方程，需无毛刺（同步于探针时钟）；
  
- RTL 示例

  ```verilog
  module Probe_Core (
    input [1:0] Flag, // Flag1=Flag[0], Flag2=Flag[1]
    input Vld,
    output reg Fwd
  );
    always @(*) begin
      if (Vld) begin
        Fwd = Flag[0] & (Flag[1] | 1'b0); // Fwd=Flag1&Flag2
      end else begin
        Fwd = 0;
      end
    end
  endmodule
  ```
  
- **约束**：时序需满足探针时钟周期（如 400MHz→2.5ns），面积需 < 100 门。

#### 自定义约束

- **时序**：自定义单元需满足 NoC 时钟周期，关键路径延迟 < 80% 时钟周期；
- **功耗**：避免冗余逻辑（如不必要的寄存器），动态功耗 < 10μW/MHz；
- **安全**：安全相关自定义单元（如安全过滤）需通过功能安全验证（AIP-FlexNoc4.7_Safety_Manual-v1.2.pdf 第 5 章）。

### 总结

NoC 技术可概括为 “**分层架构为核、多域隔离为基、全栈功能为翼**”：

- **分层架构**：事务层（NIU）与包传输层（PTU）解耦，支持多协议与灵活拓扑，适配异构 SoC；
- **多域隔离**：时钟 / 电源 / 电压域独立配置，平衡性能（时钟域）、功耗（电源域）、安全（电压域）；
- **全栈功能**：时钟复位（稳定运行）、可观测性（调试监控）、QoS（带宽保障）、安全性（防非法访问）、弹性（错误恢复）、客户自定义（差异化需求）覆盖从设计到落地的全需求。

### 技术点

- PMU（Power Management Unit in FlexNoc）是FlexNoc内部提供的一种可以保证IP独立复位和上下电模块，通过将Noc内部划分成不同的独立的power domain/reset domain，通过一套握手流程，保证各个domain可以单独的复位以及上下电：
  - Power-supply ： 
    - 单独上下电需求 （需要插入isolation-cell，如果电压域也不同还需要插入level-shifter）
    - 电压域不同，需要插level-shifter； 
    - 同一个power domain可以包含多个voltage domain；同一个voltage domain，也可以被拆分成多个power domain；
  - Clock-only：
    - 不需要单独上下电（isolation 和level-shifter均不用插入）
- I-Timeout机制，该机制会检查当前节点是否有超过设定阈值的未完成的总线传输，如果有，则会上报timeout中断以及missionFault中断。该机制除了用于FUSA外，还可以用于回片过程中的hang-up查询。
- 当MissionInt置位时，意味着发生了fusa-error，主要有以下几列：
  - Initiator Timeout ： 当initiator超过一定时间没有收到response，可能原因是： 元器件故障/target 没有返回resp；这种情况下，可能会出现多个missionInt，某个hang Path让多个路径fail；
  - Interface protection ：总线的接口ECC/Parity策略与IP不match；
  - Duplicate check error/ Noc interconnect error 

### 问题

- MRMD 和 SRMD 是什么？
- wrapping burst 和 narrow burst 是什么？
- group 到底是个什么概念，怎样实现的，有什么影响；
- NV share memory 是什么；
- 