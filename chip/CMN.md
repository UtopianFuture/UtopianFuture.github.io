## CMN

### 基础概述

CMN 是**Coherent Mesh Network**（一致性网格网络）的缩写，是 ARM 开发的高性能片上互连技术，用于连接 SoC 中的多个处理器核、缓存和外设，实现高效数据传输和缓存一致性管理。

**核心特点**：

- 采用**2D 网格拓扑结构**，由交叉点 (XP) 连接成网络，提供高带宽和低延迟；
- 支持**缓存一致性**，确保多个处理器看到的数据一致；
- 模块化设计，可通过增加网格行列扩展系统规模；

### 组件功能

![cmn](D:\gitlab\UtopianFuture.github.io\image\cmn.jpg)

#### 网络基础组件

| 术语     | 全称       | 含义                                                      |
| -------- | ---------- | --------------------------------------------------------- |
| **XP**   | CrossPoint | 网络核心组件，相当于交换机 / 路由器，负责数据包路由和转发 |
| **CML**  | CMN Link   | 连接相邻节点 (如两个 XP 或 XP 与 RN) 的点对点物理通道     |
| **Mesh** | 网格       | CMN 的物理拓扑结构，由 XP 按行和列排列形成的二维网络      |

##### CML-SMP

CML_SMP（Coherent Multichip Link - Symmetric Multiprocessing）是 ARM 专为**多芯片间构建统一对称多处理器（SMP）系统**设计的互连协议，基于**CML（一致性多芯片链路）** 硬件链路与**AMBA CHI（一致性集线器接口）** 协议扩展，核心目标是打破单芯片算力上限 —— 让多颗独立芯片的处理器核心形成 “逻辑统一、硬件分布” 的 SMP 集群，共享物理内存空间并由单一操作系统（如 Linux、QNX）动态调度，无需修改上层软件（仅需底层硬件 / 固件适配）。

CML_SMP 基于 CHI 协议扩展，通过 CMN（如 CMN S3AE、CMN-700）的全局一致性管理，解决多芯片核心的数据同步问题：

- **一致性域扩展**：将单芯片内的一致性域（由 HN-F/HN-S 节点管理）通过 CML 链路扩展到多芯片，例如 Chip A 的 Cortex-A78 核心写入的数据，Chip B 的同系列核心可直接读取，无需软件手动同步；
- **监听优化**：依赖 CMN 的 Snoop Filter（监听过滤器），避免跨芯片广播监听，仅向持有缓存副本的核心发送定向监听请求（如 SnpShared/SnpInvalid），据 ARM 数据可减少 60% 以上跨芯片监听流量；
- **共享缓存支持**：若多芯片连接共享 L3 Cache（如 CMN 的 SLC 系统级缓存），CML_SMP 可优化缓存命中路径，将跨芯片内存访问延迟从 100ns + 降至 30-50ns。

实际芯片设计中，local RN 可以通过 CCG（使用 CML-SMP 协议）连接到 CXS 总线，再通过 CXS 总线访问到 remote HN；local HN 通过 CCG 访问 remote RN。反过来也行。

#### 请求节点 (Request Node, RN)

**所有产生协议事务 (读 / 写请求) 的节点**，根据一致性级别分为三类：

| 术语     | 全称                        | 功能说明                                                     |
| :------- | :-------------------------- | ------------------------------------------------------------ |
| **RN-F** | Fully coherent Request Node | 完全一致性请求节点，通常是 CPU 核心、双核聚合层 (CAL) 或 DSU 集群，包含完整缓存一致性支持，可接收和响应监听请求 |
| **RN-I** | IO coherent Request Node    | IO 一致性请求节点，无硬件缓存，不支持 DVM 操作，主要用于 IO 设备，如 GPU、DMA 控制器 |
| **RN-D** | DVM Request Node            | 带 DVM 功能的请求节点，与 RN-I 类似，但可接收 DVM 消息，适用于带 TLB 但无缓存的 IO 设备 (如 SMMU) |

#### 主节点 (Home Node, HN)

**负责管理内存区域和缓存一致性的节点**：

| 术语     | 全称                     | 功能说明                                                     |
| -------- | ------------------------ | ------------------------------------------------------------ |
| **HN-F** | Fully coherent Home Node | 完全一致性主节点，通常是带监听过滤器 (Snoop Filter) 的 SLC 缓存块，负责管理特定内存区域的一致性 |
| **HN-I** | Home Node Index          | 主节点索引，用于系统地址映射 (SAM)，标识数据所属的主节点     |
| **HN-D** | DVM Home Node            | 集成了 DVM 等功能的 HN-I                                     |

HN-F 由以下部分组成：

- 系统级缓存（System Level Cache, SLC）：SLC 是末级缓存。数据行的 SLC 分配策略为独占式（检测到共享模式时除外），代码行的分配策略为伪包含式（由 RN-F 指示）。当启用 MTE（内存标记扩展）时，SLC 存储数据和标记后，所有代码行可在首次请求时分配到 SLC 中；
- 串行点 / 一致性点组合（Combined PoS/PoC）：负责对发送至 HN-F 的所有内存请求进行排序，包括对同一数据片段的多个未完成请求和操作进行串行化处理；
- 监听过滤器（Snoop Filter, SF）：跟踪 RN-F 中存在的缓存行。通过在可能的情况下优先使用定向监听而非广播监听，减少系统中的监听流量，大幅降低原本可能需要的监听响应流量。

HN-F 支持下面 4 种上电状态（Power States）：

- FAM: Full Associativity Mode (FAM), where the SF and the entire SLC are enabled.
- HAM: Half-Associativity Mode (HAM), where the SF is enabled but the upper half of the SLC ways are disabled and powered off.
- SFONLY: Snoop filter only mode (SFONLY), where the SF is enabled but the whole SLC is powered off.
- NOSFSLC: No-SLC Mode (NOSFSLC), where the SF and SLC are disabled and powered off.

#### 从节点 (Slave Node, SN)

**连接内存控制器或外设的节点**：

| 术语     | 全称                         | 功能说明                                                  |
| -------- | ---------------------------- | --------------------------------------------------------- |
| **SN-F** | Slave Node                   | 从节点，连接本地内存控制器 (如 DMC-620)，负责访问主存     |
| **SN-I** | Slave Node                   | **SN-I**连接具体外设（如 USB 控制器），执行操作后返回响应 |
| **SBSX** | Slave Bridge Snoop Extension | 从桥监听扩展，用于连接非一致性外设，提供有限的监听支持    |

#### 特殊功能组件

| 术语     | 全称                                               | 功能说明                                                     |
| -------- | -------------------------------------------------- | ------------------------------------------------------------ |
| **CAL**  | Component Aggregation Layer                        | 组件聚合层，封装两个 CPU 核心并连接到一个 RN-F 端口，提高效率 |
| **SAM**  | System Address Map                                 | 系统地址映射，将物理地址转换为目标节点 ID，用于路由决策      |
| **DVM**  | Data Virtualization Module                         | 数据虚拟化模块，提供内存虚拟化和地址转换功能，主要用于 RN-D 节点 |
| **MAPM** | Memory System Resource Partitioning and Monitoring | 内存系统资源分区与监控模块，解决**共享资源竞争导致的性能波动问题** |
| **CCG**  | CHI to CXS bridge                                  |                                                              |

##### SAM

SAM 负责将**物理地址转换为目标节点 ID**，指导数据包在网格中正确路由。

**核心功能：**

- **地址解码**：将 32/40/48 位物理地址映射到唯一的节点 ID；
- **路由决策**：为每个请求确定目标 HN（主节点）或 MN（混合节点）；
- **地址空间分区**：将内存空间划分为多个区域，分配给不同 HN 管理；
- **设备映射**：将外设地址映射到对应的 I/O 节点；

在手册上有两种 SAM：

RNSAM 职责（发起端）

1. 地址区间解码：判断地址是**本地 DIE / 跨 DIE / 内存 / SLC**；
2. 生成目标 HN‑F 节点 ID（发给 HNSAM）；
3. 本地 RN 窥探范围管理；
4. 减少无效跨 DIE 请求；

HNSAM 职责（主端 / SLC 端）

1. 管理 SLC / 内存的**全局地址归属**；
2. 把窥探（SNP）**精准发给目标 RN**，不广播；
3. 协调 SLC 缓存与内存的一致性；
4. 回传数据路径规划；

##### DVM

CMN S3 (AE) 中的 DVM 节点（DN）负责处理 DVM 消息，每颗芯片最多支持 4 个 DN，DN 存在于 HN-D、HN-T 和 HN-V 设备中。DVM 是 CMN 提供的**虚拟内存一致性维护机制**，确保多个处理器 / 设备的 MMU 和 TLB 保持同步。

**核心功能：**

- **TLB 维护**：在处理器间广播 TLB 失效指令，确保地址转换一致；
- **指令缓存一致性**：协调指令缓存失效，防止执行过时代码；
- **分支预测器同步**：确保预测状态一致，避免错误分支预测；
- **VMID 管理**：支持虚拟机 ID (VMID) 扩展，增强虚拟化支持；

**DVM 的工作机制**

DVM 采用**双阶段消息传递**机制处理各种内存一致性操作：

- 阶段一：请求发送

  - 请求节点 (RN-D 或 RN-F) 向 MN (混合节点) 发送 DVM 请求；

  - MN 回复 DBIDResp，表示准备接收第二部分数据；

- 阶段二：数据传输

  - 请求节点发送包含操作目标地址的第二部分数据；

  - MN 验证后向所有相关节点广播 DVM Snoop 信号；

- 响应处理

  - 目标节点执行相应操作 (TLB 失效等) 并返回响应；

  - MN 收集所有响应后向发起节点返回完成确认；

**DVM 操作类型**

DVM 支持两种主要操作类型，各有不同的同步要求：

- **非同步 DVM 操作 (DVM Non-Sync)：**

  - **TLB Invalidate**：使特定 VA 或 VA 范围的 TLB 条目失效；

  - **Instruction Cache Invalidate**：使指令缓存中特定区域失效；

  - **Branch Predictor Invalidate**：重置分支预测器状态；

  - **特点**：无需等待操作完成即可继续执行其他 DVM 操作；

- **同步 DVM 操作 (DVM Sync)：**

  - **DVM Synchronization**：确保所有之前的 DVM 操作已完成；

  - **特点**：必须等待所有未完成的 DVM 操作执行完毕，常用于内存屏障 (DSB) 指令后；

**DVM 的关键应用场景**

- **上下文切换优化：**

  - 进程 / 线程切换时，通过 DVM 广播 TLB 失效，确保新上下文地址转换正确；

  - 减少传统软件 TLB 维护开销，提高上下文切换效率；

- **虚拟化支持：**

  - 支持 SMMU (系统 MMU) 与 CPU MMU 的协同工作；

  - 在虚拟机迁移或创建时，确保所有处理器的 VMID 和页表状态一致；

  - 支持多达 16 位 VMID (ARMv8.1+)，显著增加可同时运行的虚拟机数量；

##### MPAM

MPAM 作为核心资源管理技术，旨在解决**共享资源竞争导致的性能波动问题**—— 通过硬件级资源分区实现关键任务的资源保障，同时通过实时监控优化资源利用率。

**核心目标**

- **资源隔离**：为不同任务 / 设备划分专属资源（如为 ADAS 任务预留 50% SLC 容量、2GB/s 内存带宽），避免非关键任务抢占关键资源；

- **资源监控**：实时统计各任务 / 设备的资源使用情况（如缓存命中率、带宽占用），为性能优化与故障排查提供数据支撑；

- **兼容性与灵活性**：适配 Arm 生态的软件栈（如 resctrl 文件系统），支持从 CPU 核到虚拟机粒度的精细化配置，兼容多芯片、CML_SMP 等复杂拓扑。

**功能实现**

CMN 中的 MPAM 特性围绕 “**分区控制**” 与 “**监控统计**” 两大核心展开，结合网格网络的分布式特性，实现全局资源的统一管理。

- 资源分区：硬件级隔离共享资源

  CMN 的 MPAM 支持对**缓存（SLC 为主）** 和**内存带宽**两类核心资源进行分区，隔离粒度与控制精度远超传统软件调度。

  - Cache 资源分区

    基于 CMN 的 SLC（系统级缓存，集成于 HN-F/HN-S）实现 way 级精细分区，核心特性：

    - **分区粒度**：支持 0.78%（1/2⁷）的精细化粒度（通过 7 位 CMAX 寄存器控制），例如 8MB SLC 可最小划分出 62.5KB 专属区域；

    - **非连续 way 分配**：采用 bitmap（位图）方式分配 Cache way，无需连续 way，灵活适配不同资源需求（如为低延迟任务分配分散的高优先级 way）；

    - **与 SLC 特性协同**：启用 MPAM 时，CMN 自动启用 eLRU（增强型 LRU）缓存替换策略，避免分区内资源被抢占；锁定的 Cache way（如 OCM 区域）不参与 MPAM 分区，确保专属资源不被干扰。

  - 内存带宽控制

    通过 CMN 的网格链路与内存控制器协同，实现内存带宽的精准限流：

    - **确定性流控**：区别于 x86 RDT 的 “尽力而为” 模式，CMN 的 MPAM 支持**百分比级精确控制**（如为虚拟机 A 分配 30% 内存带宽），保障关键任务的带宽稳定性；

    - **多维度限流**：可按请求类型（读 / 写）、QoS 等级、PARTID 区分限流策略，例如对写请求设置更严格的带宽上限，避免写风暴影响读性能。

  - 支持的资源隔离粒度

    CMN 的 MPAM 支持从 “全局” 到 “细粒度” 的多层级隔离，覆盖不同场景需求：

| 隔离粒度          | 描述                                        | 适用场景                        |
| ----------------- | ------------------------------------------- | ------------------------------- |
| 全局分区          | 划分整个 CMN 的共享资源（如 SLC 全局分区）  | 多芯片系统中芯片级资源隔离      |
| 虚拟机 / 容器分区 | 为每个虚拟机分配独立资源配额                | 云服务器混部                    |
| 进程 / 线程分区   | 为关键进程预留专属 Cache / 带宽             | 车载安全任务（如 ADAS）         |
| IO 设备分区       | 限制 SMMU/IO 设备的资源使用（如 PCIe 设备） | 避免 IO 密集型设备抢占 CPU 资源 |

- 资源监控：实时统计与故障告警

  CMN 的 MPAM 不仅能 “控资源”，还能 “看资源”，通过硬件计数器与中断机制实现全链路监控：

  - 资源使用统计

    - **核心监控指标**：Cache 占用率、缓存命中率、内存带宽使用量、资源配额利用率等；

    - **监控精度**：支持按 PARTID（分区 ID）、QoS 等级、请求源（如 RN-F/RN-I）分类统计，例如统计 “虚拟机 A 的读请求占用的 SLC 容量”；

    - **计数器支持**：每个分区可配置独立的监控计数器（CSUMON），支持 128/256 事务窗口的采样统计（如最近 128 个事务的 Cache 命中情况）。

  - 错误处理与告警

    - **错误日志**：通过 `cmn_hns_X_mpam_esr`（错误状态寄存器）记录 MPAM 相关错误（如 PARTID 越界、资源配额超量）；

    - **中断触发**：配置 `cmn_hns_X_mpam_ecr`（错误控制寄存器）后，可触发 INTREQMPAMERR 系列中断（如安全分区错误触发 INTREQMPAMERRS），及时通知软件处理；

    - **例外场景**：SLC 容量为 0KB 或 HN-F 工作在 SFONLY 模式时，MPAM 错误检测自动禁用，避免误报。

#### RN 系列节点对比

| 特性           | RN-F          | RN-I               | RN-D                        |
| -------------- | ------------- | ------------------ | --------------------------- |
| **缓存**       | 有完整缓存    | 无缓存             | 无缓存                      |
| **一致性支持** | 完全支持      | 部分支持 (无监听)  | 部分支持 (无监听)           |
| **DVM 支持**   | 支持          | 不支持             | 支持                        |
| **典型应用**   | CPU 核心、DSU | IO 控制器 (无 TLB) | IO 控制器 (带 TLB，如 SMMU) |

#### XP 与其他互连组件区别

**XP 是 CMN 的核心构建块**，与传统总线架构 (如 AXI) 的仲裁器不同：

- 支持**多通道并行传输**，同时处理请求、响应、数据和监听四种流量；
- 采用**分布式路由**，每个 XP 独立决策，减少中央瓶颈；
- 支持**自适应路由**，可根据网络负载动态调整路径；

### SLC 内存系统

#### Hardware-based cache flush engine

#### Software-configurable memory region locking

HN-F 支持通过路预留（way reservation）在 SLC 中锁定可变大小的内存区域，**锁定后的行不会从 SLC 中逐出**，且对这些行的任何访问均能命中 SLC（该保证适用于 CHI 规范中定义的普通内存可缓存且可分配请求，不可分配类型请求仍可能被推送至内存）。

锁定内存区域的大小由 SLC 总容量和锁定路数决定，例如：

- 若 SLC 为 16 路，则锁定 1、2、4、8 或 12 路时，锁定缓存容量分别为 SLC 总容量的 1/16、2/16、4/16、8/16 或 12/16。

#### Software-configurable On-Chip Memory

#### Source-based SLC cache partitioning

HN-F 支持为请求节点（RN-F、RN-I、RN-D）锁定 SLC 路的特性（HN-F 必须工作在 FAM 电源状态）。

该特性是基于地址的路锁定的扩展，但**锁定路基于请求者而非编程地址**，CMN S3 (AE) 支持在每个 HN-F 中编程：

- 可锁定的路数；
- 为其锁定路的 RN 设备；
- 分配策略。

启用该特性后，锁定的 SLC 路仅对编程的 RN 节点开放新缓存行分配。

#### Way-based SLC cache partitioning

可将每个 SLC 缓存实例划分为多个区域，每个区域由 4 个连续路组成，每个区域组的配置寄存器指示**哪些逻辑 RN-F、RN-I 或 RN-D 请求者可向对应的 SLC 路组分配数据**。

默认情况下，所有 RN 节点均可分配所有 16 路，具体如下表所示：

逻辑 RN-F ID 请求节点

| 寄存器                            | 地址偏移 | 预留路数 | 默认值        | 逻辑 RN-F ID 0-63 |
| --------------------------------- | -------- | -------- | ------------- | ----------------- |
| cmn_hns_slcway_partition0_rnf_vec | 0xC48    | [3:0]    | 64'b1（全 1） | 0/1               |
| cmn_hns_slcway_partition1_rnf_vec | 0xC50    | [7:4]    | 64'b1（全 1） | 0/1               |
| cmn_hns_slcway_partition2_rnf_vec | 0xC58    | [11:8]   | 64'b1（全 1） | 0/1               |
| cmn_hns_slcway_partition3_rnf_vec | 0xC60    | [15:12]  | 64'b1（全 1） | 0/1               |

逻辑 RN-I ID 请求节点

| 寄存器                            | 地址偏移 | 预留路数 | 默认值        | 逻辑 RN-I ID 0-63 |
| --------------------------------- | -------- | -------- | ------------- | ----------------- |
| cmn_hns_slcway_partition0_rni_vec | 0xC68    | [3:0]    | 32'b1（全 1） | 0/1               |
| cmn_hns_slcway_partition1_rni_vec | 0xC70    | [7:4]    | 32'b1（全 1） | 0/1               |
| cmn_hns_slcway_partition2_rni_vec | 0xC78    | [11:8]   | 32'b1（全 1） | 0/1               |
| cmn_hns_slcway_partition3_rni_vec | 0xC80    | [15:12]  | 32'b1（全 1） | 0/1               |

逻辑 RN-D ID 请求节点

| 寄存器                            | 地址偏移 | 预留路数 | 默认值        | 逻辑 RN-D ID 0-63 |
| --------------------------------- | -------- | -------- | ------------- | ----------------- |
| cmn_hns_slcway_partition0_rnd_vec | 0xC88    | [3:0]    | 32'b1（全 1） | 0/1               |
| cmn_hns_slcway_partition1_rnd_vec | 0xC90    | [7:4]    | 32'b1（全 1） | 0/1               |
| cmn_hns_slcway_partition2_rnd_vec | 0xC98    | [11:8]   | 32'b1（全 1） | 0/1               |
| cmn_hns_slcway_partition3_rnd_vec | 0xCA0    | [15:12]  | 32'b1（全 1） | 0/1               |

上述寄存器始终屏蔽 RN 节点可分配的路数：

- 比特值为 1：表示对应的逻辑 RN ID 或集群 ID 可在该区域分配（详见《配置 SF 跟踪的集群模式》）；
- 比特值为 0：表示对应的逻辑 RN ID 不可在该区域分配。

如需仅为部分 RN 节点预留路数，需按以下示例编程寄存器掩码：

示例：为 RN-F [0-3] 预留 0-3 路

- 向 cmn_hns_slcway_partition0_rnf_vec 寄存器写入 0x000000000000000F，允许逻辑 RN-F ID 0、1、2、3 分配到 0-3 路，其他 RN-F ID（4-63）不可分配；

- 向 cmn_hns_slcway_partition0_rni_vec 寄存器写入 0x0，禁止所有 RN-I 分配到 0-3 路；

- 向 cmn_hns_slcway_partition0_rnd_vec 寄存器写入 0x0，禁止所有 RN-D 分配到 0-3 路。

### 核心设计思想

#### **网格拓扑：突破传统互连瓶颈**

CMN 采用**2D 网格 (Mesh) 拓扑**而非传统的总线或交叉开关 (Crossbar)，这是其最根本的设计选择：

| 特性           | 传统总线 / Crossbar      | CMN 网格拓扑                    |
| -------------- | ------------------------ | ------------------------------- |
| **带宽扩展性** | 固定带宽，随节点增加下降 | 带宽随网格规模线性增长          |
| **延迟特性**   | 延迟与节点数相关         | 延迟仅与源 - 目的间 "跳数" 相关 |
| **可扩展性**   | 节点数受限 (通常 < 16)   | 支持大规模扩展 (>64 节点)       |
| **可靠性**     | 单点故障风险             | 多路径冗余，无单点故障          |
| **实现复杂度** | 随节点数指数增长         | 模块化，复杂度可控              |

**设计思想**：通过分布式路由和并行数据通路，将 "**消除系统中任何带宽瓶颈**" 作为核心原则。每个交叉点 (XP) 仅与相邻节点连接，使高频操作成为可能，同时降低布线复杂度。

#### **分层架构：功能解耦与性能优化**

CMN 采用清晰的分层设计理念：

- **物理层**：CML (CMN Mesh Link) 提供节点间的物理连接，支持高速数据传输
- **路由层**：XP (CrossPoint) 负责数据包转发，采用**分布式路由决策**，每个 XP 独立决定数据包走向
- **协议层**：基于 AMBA 5 CHI 协议，实现一致性操作和事务管理

**设计思想**：通过功能解耦，使各层专注于特定任务，提高整体效率。特别是，将**路由与一致性管理分离**，使网络拓扑与一致性协议相互独立，增强系统灵活性。

#### **一致性机制：硬件辅助简化软件**

CMN 设计的另一核心是**硬件实现的缓存一致性**，其设计思想基于以下原则：

- **硬件一致性的设计目的不是为了更快，而是为了软件更简单**；
- 采用**目录 (Directory) 机制**替代广播监听：每个主节点 (HN) 维护所负责内存区域的缓存状态，精准发送监听请求而非全局广播；
- 支持**多一致性域**：可配置不同节点组属于不同的监听域，减少跨域监听开销；

**设计思想**：通过硬件自动处理缓存一致性，将软件开发人员从复杂的缓存维护工作中解放出来，同时保证高性能。

#### **节点抽象：统一接口与灵活连接**

CMN 通过**标准化节点接口**实现异构组件的无缝集成：

- 定义三类节点：请求节点 (RN)、主节点 (HN) 和从节点 (SN)，每类节点有不同变种以适应不同场景；
- 所有节点通过标准 CHI 接口连接到网格，隐藏内部实现细节；
- 支持灵活的内存映射：**通过 SAM (System Address Map) 将物理地址空间映射到对应 HN**，实现数据本地化和 NUMA 优化；

**设计思想**："**以抽象接口隐藏复杂性**"，使不同 IP 核 (CPU、GPU、加速器、内存控制器) 能够以统一方式接入网络，降低系统集成难度。

#### **流量管理：精细化控制与服务质量**

CMN 设计了精细的流量管理机制，确保关键操作获得所需资源：

- 为不同类型的流量 (如取指、数据读写、中断、DMA) 分配独立的虚拟通道 (VC)；
- 每个 XP 内部为每个 VC 提供**独立的缓冲区队列和仲裁机制**，实现资源隔离和优先级保障；
- 支持**流量整形**：通过动态调整路由避开拥塞区域，平衡网络负载；

**设计思想**：将 "**减少事务排队延迟**"和"**确保关键流量优先**" 作为流量管理核心，特别是在处理实时和高带宽应用时。

### 访存流程

#### CPU 核心访存流程详解

```
CPU核(RN-F) → MMU → RN-F → XP → ... → HN-F → 
  ├── [缓存命中] → 目标RN-F → HN-F → XP → ... → RN-F → CPU核
  └── [缓存未命中] → SN-F → 内存控制器 → 内存 → 
        SN-F → HN-F → XP → ... → RN-F → CPU核
```

##### 访存前准备：地址转换

- CPU 核执行指令，译码后识别为访存操作，生成虚拟地址 (VA)；
- MMU将 VA 转换为物理地址 (PA)，添加访问权限和内存属性 (如缓存性、共享性)；
- **RN-F**(全一致性请求节点，通常为 CPU 核心) 将 PA 和事务类型 (读 / 写) 封装为 CHI 协议数据包，通过请求通道 (REQ) 发送至 CMN 网格；

##### 路由阶段：请求在 CMN 中传输

- **XP**(交叉点) 接收数据包，提取目标地址，查询**SAM**(系统地址映射) 确定负责的 HN-F；
- 使用**确定性路由算法**(通常是 XY 路由) 选择最佳路径，通过 mesh 端口转发至相邻 XP；
- 每个 XP 维护独立的**请求 / 响应 / 监听 / 数据**四个通道，支持并行传输，避免拥塞；

##### 主节点处理：一致性管理

- **HN-F**(全一致性主节点) 接收请求，通过**目录**或**监听过滤器**(SF) 检查地址状态；

  ```c
  // HN-F处理读请求伪代码
  if (地址在目录中且状态有效) {
    if (状态为"独占/脏") {
      向拥有者RN-F发送Snoop请求获取数据
    } else if (状态为"共享") {
      从缓存或内存获取数据
    }
  } else {
    向SN发送读请求获取数据 // SN: 从节点，连接本地内存控制器 (如 DMC-620)，负责访问主存
  }
  ```

- **PoS**(串行点) 保证同一地址的请求顺序执行，**PoC**(一致性点) 维护缓存一致性；

- **Snoop Filter**记录哪些 RN-F 缓存了该地址，避免广播监听，精准发送 Snoop 请求；

##### 数据获取：从内存或其他缓存读取

**情况 1：数据在其他缓存中**

- HN-F 向目标 **RN-F** 发送 **Snoop 请求**(如 SnpShared)；
- 目标 RN-F 检查缓存，若有脏数据，返回给 HN-F 并将状态改为**共享 / 无效**；
- HN-F 将数据返回给请求 RN-F，更新目录状态为**共享**；

**情况 2：数据在内存中**

- HN-F 向 **SN-F** (从节点，连接内存控制器) 发送 **ReadNoSnp** 请求；
- SN-F 将请求转发给**内存控制器**，读取数据后返回给 HN-F；
- HN-F 将数据返回给请求 RN-F，更新目录状态为**共享 / 独占**；

##### 数据回传与缓存更新

- 数据通过 **响应通道 (RSP)**和**数据通道 (DAT)** 原路返回至请求 RN-F；
- RN-F 将数据存入**L1/L2 缓存**，更新缓存状态 (根据协议为**MESI/MOESI**)；
- 若为**写操作**，HN-F 协调其他缓存使对应数据**失效**，确保一致性；

#### 外设访问流程详解

##### 请求发起与路由

- **外设**（如 GPU、DMA 控制器）通过 **RN-I/RN-D**（IO 一致性请求节点）发送请求；
- RN-I/RN-D 无缓存，不参与一致性，仅转换协议（ACE-Lite→CHI）；
- 流程与 CPU 访存类似，通过 XP 路由至负责该外设地址的 HN-I/HN-P；

##### 外设节点处理

- **HN-I**（IO 一致性主节点）不维护缓存，仅作为协议桥；
- 将 CHI 请求转换为 **ACE-Lite** 或特定协议（如 PCIe），转发至 **SN-I **（外设从节点）；

##### 外设响应

- **SN-I**连接具体外设（如 USB 控制器），执行操作后返回响应；
- 响应经 HN-I→CMN 网格→SN-I→外设，完成一次外设访问；

#### CMN 节点在访存流程中的核心作用对比

| 节点类型   | CPU 访存中的核心作用                                         | 外设访问中的核心作用                     |
| ---------- | ------------------------------------------------------------ | ---------------------------------------- |
| **RN-F**   | ・生成请求 (读 / 写)・维护私有缓存・响应 Snoop 请求・更新缓存状态 | 不参与 (外设使用 RN-I/D)                 |
| **RN-I/D** | 不参与 (CPU 使用 RN-F)                                       | ・外设接口・协议转换・发起非一致性请求   |
| **HN-F**   | ・一致性仲裁・目录管理・Snoop 决策・数据调度 (内存 / 缓存)   | 不参与 (外设使用 HN-I/P)                 |
| **HN-I/P** | 不参与 (CPU 使用 HN-F)                                       | ・外设地址映射・协议转换・顺序保证 (PoS) |
| **XP**     | ・分布式路由・流量管理・多通道并行・错误检测                 | 与 CPU 访存相同                          |
| **SN-F**   | ・内存控制器接口・物理内存访问・数据读写                     | 不参与 (外设使用 SN-I)                   |
| **SN-I**   | 不参与 (内存访问用 SN-F)                                     | ・外设接口・外设寄存器读写・设备控制     |

#### 关键技术点解析

##### **缓存一致性保证**

- **目录机制**：每个 HN-F 维护部分地址空间的缓存状态，记录哪些 RN-F 持有缓存副本；
- **Snoop Filter**：减少 90% 以上的监听流量，仅向可能持有数据的 RN-F 发送 Snoop；
- **写传播**：写操作时，HN-F 向所有持有该缓存行的 RN-F 发送**Invalidate**或**Update**信号；

##### **性能优化机制**

- **流量优先级**：CMN 为不同事务 (如取指、数据读写) 分配独立虚拟通道，确保关键操作优先；
- **自适应路由**：当某路径拥塞时，XP 自动选择替代路径，平衡网络负载；
- **预取**：HN-F 或 SN 可预测访问模式，提前加载数据至缓存，隐藏内存延迟；

### Performance events

#### HNS Performance events

| 编号 | 事件名称                               | 所属模块  | 事件含义                            | 触发原因（双 DIE 场景）                             | 瓶颈判断                                         | 解决方案                                               |
| ---- | -------------------------------------- | --------- | ----------------------------------- | --------------------------------------------------- | ------------------------------------------------ | ------------------------------------------------------ |
| 1    | PMU_HN_CACHE_MISS_EVENT                | HN‑F(HNS) | SLC 首次查找缓存未命中总次数        | mcf 无局部性、跨 DIE 访存远端内存、SLC 无法缓存     | 计数≈总请求 = mcf 正常；<br />远小于请求数为异常 | 无需优化（mcf 固有特性）；<br />地址锁定效果极差       |
| 2    | PMU_HNSLC_SF_CACHE_ACCESS_EVENT        | HN‑F(HNS) | SLC 首次缓存访问总次数              | 所有跨 DIE / 本地请求访问 SLC 的总流量              | 数值稳定 = 正常；<br />突增 = 流量暴涨           | 作为基准指标，无需优化                                 |
| 3    | PMU_HN_CACHE_FILL_EVENT                | HN‑F(HNS) | SLC 缓存行分配填充总次数            | SLC 未命中后从远端 DIE 内存加载、mcf 全 miss 必填充 | 计数≈miss 数 = 正常；远大于 miss 数为异常        | 关闭非热点地址 SLC 填充，降低带宽浪费                  |
| 4    | PMU_HN_POCQ_RETRY_EVENT                | HN‑F(HNS) | POCQ 满 / 序类满返回 RetryAck 次数  | 跨 DIE 延迟高、请求并发过载、POCQ 资源耗尽          | **计数持续↑= 严重队列瓶颈**                      | 提升跨 DIE 请求优先级；<br />增大 POCQ 深度；降低并发  |
| 5    | PMU_HN_POCQ_REQS_RECVD_EVENT           | HN‑F(HNS) | HN‑F 接收的总请求次数               | DIE1→DIE0 跨 DIE 访存、mcf 随机读持续流入           | 数值稳定 = 正常；<br />突增 = 流量异常           | 作为流量基准，无需优化                                 |
| 6    | PMU_HN_SF_HIT_EVENT                    | HN‑F(HNS) | SF 窥探过滤器命中次数               | 跨 DIE 共享数据、SF 精准跟踪缓存副本                | 计数≈0=mcf 正常；<br />偏低 = SF 失效            | 增大 SF 容量；<br />优化 HNSAM 路由配置                |
| 7    | PMU_HN_SF_EVICTIONS_EVENT              | HN‑F(HNS) | SF 缓存失效驱逐次数                 | SF 表项满、跨 DIE 共享数据频繁变更                  | 计数↑=SF 容量不足                                | 增加 SF 条目数；<br />减少跨 DIE 数据共享              |
| 8    | PMU_HN_DIR_SNOOPS_SENT_EVENT           | HN‑F(HNS) | 发送定向窥探总次数                  | 跨 DIE 缓存一致性、单节点数据副本查询               | 计数偏高 = 正常一致性流量；<br />过高 = 冗余窥探 | 优化 RNSAM 路由；<br />提升 SF 过滤效率                |
| 9    | PMU_HN_BRD_SNOOPS_SENTEVENT            | HN‑F(HNS) | 发送广播窥探总次数                  | SF 失效、共享者超上限、跨 DIE 窥探风暴              | **计数↑= 致命性能瓶颈**                          | 增大 SF 精准跟踪能力；<br />关闭不必要共享             |
| 10   | PMU_HN_SLC_EVICTION_EVENT              | HN‑F(HNS) | SLC 脏数据驱逐写回次数              | 跨 DIE 写事务、脏缓存行替换                         | 计数≈0=mcf 只读正常；偏高 = 写带宽压力           | 优化 SLC 替换策略；<br />调整脏数据回写阈值            |
| 11   | PMU_HN_SLC_FILL_INVALID_WAY_EVENT      | HN‑F(HNS) | SLC 向无效路填充次数                | SLC 无空闲路、跨 DIE 填充过于频繁                   | 计数↑=SLC 容量不足                               | 增大 SLC 路数；<br />调整 SLC 分区配额                 |
| 12   | PMU_HN_MC_RETRIES_LOCAL_EVENT          | HN‑F(HNS) | 本地内存控制器重试次数              | 本地内存拥堵、跨 DIE 回写抢占带宽                   | 计数↑= 本地内存瓶颈                              | 优化内存 QoS；<br />降低跨 DIE 突发流量                |
| 13   | PMU_HN_MC_REQS_LOCAL_EVENT             | HN‑F(HNS) | 发往本地内存的请求次数              | DIE0 本地访存、SLC 未命中访问本地 MC                | 数值稳定 = 正常；突增 = 本地流量暴涨             | 作为本地流量基准，无需优化                             |
| 14   | PMU_HN_QOS_HH_RETRY_EVENT              | HN‑F(HNS) | 最高优请求被重试次数                | 跨 DIE 低优流量抢占、QoS 配置错误                   | 计数↑=QoS 调度失效                               | 调整 POCQ 序类优先级；<br />提升高优流量权重           |
| 15   | PMU_HNF_POCQ_OCCUPANCY_EVENT           | HN‑F(HNS) | POCQ 活跃表项周期累加和（驻留时长） | 跨 DIE 往返延迟高、请求等待响应时间变长             | 为本地 5~6 倍 = 正常；远超 6 倍 = 异常           | 优化 CXS 链路延迟；<br />缩短跨 DIE 路由；增大链路信用 |
| 16   | PMU_HN_POCQ_ADDRHAZ_EVENT              | HN‑F(HNS) | POCQ 分配地址冲突次数               | 跨 DIE 同地址并发访问、请求串行化阻塞               | 计数↑= 地址冲突瓶颈                              | 优化内存访问分布；<br />减少同地址跨 DIE 并发          |
| 17   | PMU_HN_POCQ_ATOMICS_ADDRHAZ_EVENT      | HN‑F(HNS) | 原子操作 POCQ 地址冲突次数          | 跨 DIE 原子操作、同地址原子访存阻塞                 | 计数↑= 原子访存瓶颈                              | 减少跨 DIE 原子操作；<br />提升原子序类优先级          |
| 20   | PMU_HN_TXDAT_STALL_EVENT               | HN‑F(HNS) | 数据 flit 无信用发送停顿            | 片间 CXS 反压、接收端信用返还慢                     | 计数↑= 跨 DIE 数据发送瓶颈                       | 增大 CXS 数据信用；<br />优化链路流控                  |
| 21   | PMU_HN_TXRSP_STALL_EVENT               | HN‑F(HNS) | 响应 flit 无信用发送停顿            | 跨 DIE 读响应拥堵、链路信用不足                     | 计数↑= 跨 DIE 响应瓶颈                           | 增大 CXS 响应信用；<br />优化接收端缓冲区              |
| 23   | PMU_HN_SEQ_HIT_EVENT                   | HN‑F(HNS) | 请求命中 SF 待驱逐条目              | 跨 DIE 访问即将失效的缓存行                         | 计数≈0 = 正常；偏高 = SF 替换异常                | 调整 SF 替换策略；延长有效缓存时间                     |
| 24   | PMU_HN_SNP_SENT_EVENT                  | HN‑F(HNS) | 全部类型窥探发送总次数              | 跨 DIE 一致性流量、SFBI + 定向 + 广播总和           | 计数过高 = 一致性开销过大                        | 优化 SF 过滤；<br />减少跨 DIE 数据共享                |
| 25   | PMU_HN_SFBI_DIR_SNP_SENT_EVENT         | HN‑F(HNS) | SF 回写无效引发定向窥探             | SF 失效、跨 DIE 缓存强制无效                        | 计数↑=SF 频繁失效                                | 增大 SF 容量；<br />优化 SLC/SF 联动策略               |
| 26   | PMU_HN_SFBI_BRD_SNP_SENT_EVENT         | HN‑F(HNS) | SF 回写无效引发广播窥探             | SF 精准跟踪失效、跨 DIE 窥探风暴                    | **计数↑= 系统级瓶颈**                            | 提升 SF 跟踪上限；关闭跨 DIE 不必要共享                |
| 27   | PMU_HN_SNP_SENT_UNTRK_EVENT            | HN‑F(HNS) | 向未跟踪 RN 发送窥探次数            | RNSAM/HNSAM 路由错误、节点配置异常                  | 计数↑= 路由配置错误                              | 检查地址路由表；重新配置 RNSAM/HNSAM                   |
| 28   | PMU_HN_INTV_DIRTY_EVENT                | HN‑F(HNS) | SF 回写无效触发脏数据干预           | 跨 DIE 脏行回写、一致性强制同步                     | 计数≈0=mcf 只读正常；偏高 = 写拥堵               | 优化脏数据回写策略；<br />降低跨 DIE 写并发            |
| 31   | PMU_HN_SNP_FWDED_EVENT                 | HN‑F(HNS) | 数据转发窥探发送次数                | 跨 DIE 数据从远端 RN 转发、非直访内存               | 计数↑= 转发路径过长                              | 优化跨 DIE 路由；缩短数据转发路径                      |
| 33   | PMU_HN_MPAM_REQ_OVER_HARDLIM_EVENT     | HN‑F(HNS) | 写请求超 SLC 硬限无法分配           | SLC 分区配额满、跨 DIE 写流量超限                   | 计数↑=SLC 配额不足                               | 调整 MPAM 硬限；增大跨 DIE 写 SLC 配额                 |
| 34   | PMU_HN_MPAM_REQ_OVER_SOFTLIM_EVENT     | HN‑F(HNS) | 写请求超 SLC 软限告警               | SLC 配额接近上限、跨 DIE 写流量偏高                 | 计数↑= 配额即将耗尽                              | 提升软限阈值；<br />优化 SLC 分区分配                  |
| 35   | PMU_HN_SNP_SENT_CLUSTER_EVENT          | HN‑F(HNS) | 发往簇的批量窥探次数                | 跨簇跨 DIE 共享数据、批量一致性查询                 | 计数偏高 = 正常；过高 = 簇间窥探冗余             | 优化簇间路由；<br />减少簇间数据共享                   |
| 36   | PMU_HN_SF_IMPRECISE_EVICT_EVENT        | HN‑F(HNS) | SF 聚类导致驱逐丢弃                 | SF 容量不足、跨 DIE 共享者过多                      | 计数↑=SF 精准能力不足                            | 增大 SF 表项；<br />提升精准跟踪上限                   |
| 37   | PMU_HN_SF_EVICT_SHARED_LINE_EVENT      | HN‑F(HNS) | SF 驱逐共享行次数                   | 跨 DIE 共享数据频繁失效、SF 替换频繁                | 计数↑= 共享数据过多                              | 减少跨 DIE 数据共享；<br />调整 SF 替换策略            |
| 38   | PMU_HN_POCQ_CLASS_OCCUPANCY_EVENT      | HN‑F(HNS) | 指定序类 POCQ 占用累加              | 跨 DIE 单类流量（读 / 写 / QoS）驻留过长            | 单类计数↑= 该类型流量瓶颈                        | 提升该序类优先级；<br />增大序类 POCQ 配额             |
| 39   | PMU_HN_POCQ_CLASS_RETRY_EVENT          | HN‑F(HNS) | 指定序类 POCQ 重试次数              | 单类跨 DIE 流量过载、序类队列满                     | **计数↑= 序类队列瓶颈**                          | 调整序类 QoS；<br />增大序类 POCQ 深度                 |
| 40   | PMU_HN_CLASS_MC_REQS_EVENT             | HN‑F(HNS) | 指定序类发往内存请求次数            | 跨 DIE 单类流量访问内存、QoS 流量统计               | 数值稳定 = 正常；突增 = 流量异常                 | 作为分类流量基准，无需优化                             |
| 42   | PMU_HN_NUM_SN_CBUSY_THROTTLE_EVENT     | HN‑F(HNS) | CBusy 导致 SN 请求限流              | 从节点拥堵、跨 DIE 访问 SN 过载                     | 计数↑= 从节点瓶颈                                | 优化 SN 节点负载；降低跨 DIE 访问速率                  |
| 43   | PMU_HN_NUM_SN_CBUSY_THROTTLE_MIN_EVENT | HN‑F(HNS) | CBusy 强制最小限流次数              | SN 节点严重拥堵、跨 DIE 流量被强制限制              | **计数↑=SN 节点瘫痪**                            | 扩容 SN 节点；<br />分流跨 DIE 访问流量                |
| 44   | PMU_HN_SF_PRECISE_TO_IMPRECISE_EVENT   | HN‑F(HNS) | SF 精准转模糊模式次数               | 跨 DIE 共享者超上限、触发广播窥探                   | **计数↑= 窥探风暴前兆**                          | 提升 SF 精准跟踪数；<br />减少跨 DIE 数据共享          |
| 45   | PMU_HN_SNP_INTV_CLN_EVENT              | HN‑F(HNS) | 干净数据窥探干预次数                | 跨 DIE 干净行转发、无脏数据回写                     | 计数偏低 = 正常；过高 = 转发冗余                 | 优化数据转发路径；<br />减少跨 DIE 干净行访问          |
| 46   | PMU_HN_NC_EXCL_EVENT                   | HN‑F(HNS) | 非缓存独占请求到达次数              | 跨 DIE 不可缓存访存、配置标记错误                   | 计数↑= 内存属性配置错误                          | 检查内存类型配置；<br />修正可缓存属性                 |
| 48   | PMU_HN_SNP_REQ_RECVD_EVENT             | HN‑F(HNS) | 接收外部窥探请求次数                | 跨 DIE 反向一致性查询、DIE0→DIE1 窥探               | 计数过高 = 双向窥探开销大                        | 优化双向路由；<br />降低反向窥探频率                   |
| 49   | PMU_HN_SNP_REQ_BYP_POCQ_EVENT          | HN‑F(HNS) | 窥探绕过队列直接入 POCQ             | 高优窥探、跨 DIE 紧急一致性请求                     | 计数偏高 = 正常；过高 = 请求阻塞                 | 调整窥探优先级；优化 POCQ 调度                         |
| 50   | PMU_HN_DIR_CCGHA_SNP_SENT_EVENT        | HN‑F(HNS) | 发往 CCG‑HA 定向窥探次数            | 跨 DIE 片间一致性、精准查询 CCG 节点                | 计数偏高 = 正常；过高 = 跨 DIE 窥探冗余          | 优化 CCG 路由；<br />提升 SF 跨 DIE 过滤               |
| 51   | PMU_HN_BRD_CCGHA_SNP_SENT_EVENT        | HN‑F(HNS) | 发往 CCG‑HA 广播窥探次数            | 跨 DIE 片间窥探风暴、SF 失效                        | **计数↑= 跨 DIE 致命瓶颈**                       | 修复 SF 配置；<br />禁止跨 DIE 广播窥探                |
| 53   | PMU_HN_LBT_REQ_OVER_HARDLIM_EVENT      | HN‑F(HNS) | LBT 写请求超 SLC 硬限               | 低带宽跨 DIE 写流量超限、配额不足                   | 计数↑=LBT 配额不足                               | 增大 LBT SLC 硬限；<br />优化低带宽流量 QoS            |
| 54   | PMU_HN_HBT_REQ_OVER_HARDLIM_EVENT      | HN‑F(HNS) | HBT 写请求超 SLC 硬限               | 高带宽跨 DIE 写流量超限、配额不足                   | 计数↑=HBT 配额不足                               | 增大 HBT SLC 硬限；<br />分流高带宽流量                |
| 55   | PMU_HN_SF_REUPDATE_EVENT               | HN‑F(HNS) | SF 强制重更新次数                   | 跨 DIE 缓存状态同步失败、SF 表项错误                | 计数↑=SF 同步异常                                | 重新配置 SF；<br />检查节点间同步信号                  |
| 56   | PMU_HN_EXCL_SF_IMPRECISE_EVENT         | HN‑F(HNS) | 独占请求命中模糊 SF                 | 跨 DIE 独占访问、SF 状态异常                        | 计数↑=SF 跟踪错误                                | 优化 SF 状态机；<br />减少跨 DIE 独占访问              |
| 57   | PMU_HN_SNP_POCQ_ADDRHAZ_EVENT          | HN‑F(HNS) | 窥探引发 POCQ 地址冲突              | 跨 DIE 窥探与请求访存冲突、串行阻塞                 | 计数↑= 窥探与请求冲突瓶颈                        | 分离窥探 / 请求序类；<br />优化 POCQ 调度              |
| 58   | PMU_HN_MC_RETIRES_REMOTE_EVENT         | HN‑F(HNS) | 远端内存控制器重试次数              | 跨 DIE 内存拥堵、DIE1→DIE0 访存过载                 | 计数↑= 远端内存瓶颈                              | 优化跨 DIE 内存 QoS；<br />降低访问并发                |
| 59   | PMU_HN_MC_REQS_REMOTE_EVENT            | HN‑F(HNS) | 发往远端内存请求次数                | mcf 全 miss、DIE1→DIE0 跨 DIE 访存                  | 数值高 = mcf 正常；突增 = 流量异常               | 无需优化                                               |
| 60   | PMU_HN_CLASS_MC_REQS_REMOTE_EVENT      | HN‑F(HNS) | 指定序类远端内存请求次数            | 跨 DIE 分类流量访存、QoS 流量统计                   | 数值稳定 = 正常；突增 = 流量异常                 | 作为分类跨 DIE 流量基准，无需优化                      |

下面几个 events 是调试核心参数，

| 事件名称                            | 所属模块  | 事件含义                            | 触发原因（双 DIE 场景）                                | 瓶颈判断                                        | 解决方案                                                     |
| ----------------------------------- | --------- | ----------------------------------- | ------------------------------------------------------ | ----------------------------------------------- | ------------------------------------------------------------ |
| **PMU_HN_CACHE_MISS_EVENT**         | HN-F(HNS) | SLC 首次查找未命中总次数            | mcf 无局部性、跨 DIE 访问远端内存、SLC 完全无法缓存    | 计数≈总请求 =**mcf 正常**；远小于请求数才是异常 | 无需优化（mcf 固有特性）；<br />可锁热点地址但效果极差       |
| **PMU_HN_CACHE_FILL_EVENT**         | HN-F(HNS) | SLC 缓存行分配填充总次数            | SLC 未命中后强制从远端内存加载数据、mcf 每次访问都填充 | 计数≈未命中数 =**正常**；远大于未命中数为异常   | 无需优化；<br />关闭非热点地址的 SLC 填充可降低带宽浪费      |
| **PMU_HN_POCQ_RETRY_EVENT**         | HN-F(HNS) | POCQ 满 / 序类满返回 RetryAck 次数  | 跨 DIE 延迟过高、请求并发过载、POCQ 队列资源耗尽       | **计数持续↑= 严重瓶颈**                         | 1. 提升跨 DIE 请求优先级；<br />2. 增大 POCQ 深度；<br />3. 降低请求并发 |
| **PMU_HN_POCQ_REQS_RECVD_EVENT**    | HN-F(HNS) | HN-F 接收的跨 DIE 请求总数          | mcf 持续发起随机读、DIE1→DIE0 访存流量基准             | 数值稳定 =**正常**                              | 作为流量基准，无需优化                                       |
| **PMU_HNF_POCQ_OCCUPANCY_EVENT**    | HN-F(HNS) | POCQ 活跃表项周期累加和（驻留时长） | 跨 DIE 往返延迟高、请求在队列等待响应时间变长          | 为本地 5~6 倍 =**正常**；远超 6 倍为异常        | 1. 优化片间 CXS 链路延迟；<br />2. 缩短跨 DIE 路由路径；<br />3. 增大链路信用 |
| **PMU_HN_TXRSP_STALL_EVENT**        | HN-F(HNS) | 读响应 flit 因无信用无法发送        | 片间链路反压、接收端信用返还慢、跨 DIE 响应拥塞        | 计数↑=**跨 DIE 响应瓶颈**                       | 1. 增大 CXS 响应信用；<br />2. 优化链路流控；<br />3. 降低响应突发 |
| **PMU_HN_BRD_CCGHA_SNP_SENT_EVENT** | HN-F(HNS) | 发往 CCG-HA 的跨 DIE 广播窥探次数   | SF 过滤失效、共享者超上限、缓存一致性异常              | **计数↑= 窥探风暴瓶颈**                         | 1. 增大 SF 表项容量；<br />2. 优化 RNSAM/HNSAM 路由；<br />3. 减少数据共享 |
| **PMU_HN_MC_REQS_REMOTE_EVENT**     | HN-F(HNS) | 发往远端 DIE 内存控制器的请求数     | mcf 全未命中、所有请求穿透到 DIE0 远端内存             | 数值高 =**mcf 正常**                            | 无需优化；<br />属于 mcf 固有访存行为                        |

#### RNI Performance events

RNI 的 events 很多，这里不一一列举，只根据手册提供的 bottleneck analysis 列举对应的 events。
CMN S3(AE) provides events that observe **the locations where the nodes or bridges are full**, which can cause delays in the rest of the system.
This feature enables you to monitor the current bottlenecks in the system, and checks multiple events in the RN-Is, HN-Fs, and memory controllers. In the RN-I bridges, the events monitor the following:

- The number of times the bridge **is forced to retry** because of the lack of dynamic credits;

- The number of times **the read and write tracker is full** and therefore cannot accept new requests in the system. This condition can cause delays in the AXI requesters;

- The number of **read request replays**, because of decoupling of the read request buffers and read data buffers in the RN-I system;

| 编号 | 事件名称                     | 对应手册监控点    | 事件含义                           | 瓶颈判断                                | 解决方案                                          |
| ---- | ---------------------------- | ----------------- | ---------------------------------- | --------------------------------------- | ------------------------------------------------- |
| 7    | PMU_RNI_TXREQFLITV_RETRIED   | 1. 缺信用强制重试 | RN-I 因信用不足，发送重试 TXREQ    | **计数↑ = RN-I 入口反压、信用不够**     | 增大 CHI 链路信用；<br />调优信用返还速率         |
| 10   | PMU_RNI_TXREQFLITV_REPLAYED  | 3. 读请求重放     | 读请求缓冲与读数据缓冲解耦引发重放 | **计数↑ = 读通路缓冲不匹配、重放风暴**  | 增大 RDB/PADB 深度；<br />匹配请求 / 数据缓冲比例 |
| 8    | PMU_RNI_RRT_OCCUPANCY_SLICE0 | 2. 读 Tracker 满  | Slice0 读请求跟踪器全满            | 任一↑ = **读 Tracker 满，新读请求被堵** | 增大 RRT 容量；<br />均衡读流量到多 Slice         |
| 19   | PMU_RNI_RRT_OCCUPANCY_SLICE1 | 2. 读 Tracker 满  | Slice1 读 Tracker 满               | 同上                                    | 同上                                              |
| 20   | PMU_RNI_RRT_OCCUPANCY_SLICE2 | 2. 读 Tracker 满  | Slice2 读 Tracker 满               | 同上                                    | 同上                                              |
| 21   | PMU_RNI_RRT_OCCUPANCY_SLICE3 | 2. 读 Tracker 满  | Slice3 读 Tracker 满               | 同上                                    | 同上                                              |
| 36   | PMU_RNI_RRT_OCCUPANCY_SLICE4 | 2. 读 Tracker 满  | Slice4 读 Tracker 满               | 同上                                    | 同上                                              |
| 37   | PMU_RNI_RRT_OCCUPANCY_SLICE5 | 2. 读 Tracker 满  | Slice5 读 Tracker 满               | 同上                                    | 同上                                              |
| 38   | PMU_RNI_RRT_OCCUPANCY_SLICE6 | 2. 读 Tracker 满  | Slice6 读 Tracker 满               | 同上                                    | 同上                                              |
| 39   | PMU_RNI_RRT_OCCUPANCY_SLICE7 | 2. 读 Tracker 满  | Slice7 读 Tracker 满               | 同上                                    | 同上                                              |
| 9    | PMU_RNI_WRT_OCCUPANCY        | 2. 写 Tracker 满  | 写请求跟踪器全满                   | **计数↑ = 写 Tracker 满，新写请求被堵** | 增大 WRT 深度；<br />降低写并发                   |

对应上面的 bottleneck analysis：

- be forced to retry：看 **7 号**：有没有重试 → 信用够不够；

- read request replays：看 **10 号**：有没有重放 → 缓冲匹不匹配；

- the read and write tracker is full：看 **8/19-21/36-39 + 9 号**：Tracker 满没满 → 请求收不收得进；

#### SBSX Performance events

This section contains information about bottleneck analysis at SBSX bridges. CMN S3(AE) provides events that observe the locations where the nodes or bridges are full, which can cause delays in the rest of the system. This feature enables you to monitor the current bottlenecks in the system, and checks multiple events in all CMN S3(AE) components. The events monitor the following:

- The number of times the bridge **is forced to retry** because of the lack of dynamic credits;

- The number of cycles the bridge **is forced to stall** because of backpressures on AXI or CHI interface;

| 编号 | 事件名称                      | 所属模块 | 事件含义                                           | 触发原因（双 DIE 场景）                     | 瓶颈判断                                         | 解决方案                                          |
| ---- | ----------------------------- | -------- | -------------------------------------------------- | ------------------------------------------- | ------------------------------------------------ | ------------------------------------------------- |
| 1    | PMU_RD_REQ                    | SBSX     | 接收的读请求总数（读事务计数）                     | DIE1→DIE0 跨 DIE 读请求流入、mcf 随机读流量 | 数值稳定 = 正常；突增 = 读流量暴涨               | 作为跨 DIE 读流量基准，无需优化                   |
| 2    | PMU_WR_REQ                    | SBSX     | 接收的写请求总数（写事务计数）                     | 跨 DIE 写事务、DIE1 向 DIE0 写数据          | 数值稳定 = 正常；突增 = 写流量暴涨               | 作为跨 DIE 写流量基准，无需优化                   |
| 3    | PMU_CMO_REQ                   | SBSX     | 接收的缓存维护操作（CMO）请求总数                  | 跨 DIE 缓存同步、清理、无效化操作           | 计数偏高 = 正常一致性操作；过高 = 冗余 CMO       | 减少不必要跨 DIE CMO 操作；<br />优化缓存策略     |
| 4    | PMU_TXRSP_RETRYACK            | SBSX     | 发送的 RetryAck 类型 TXRSP flit 数（CHI 重试计数） | SBSX 资源不足、跨 DIE 请求过载、队列满      | **计数持续↑=SBSX 请求拥堵瓶颈**                  | 增大 SBSX 请求队列深度；<br />降低跨 DIE 请求并发 |
| 5    | PMU_TXDAT_FLITV               | SBSX     | 发送的 TXDAT flit 数（CHI 读数据带宽）             | 跨 DIE 读响应数据返回、DIE0→DIE1 数据传输   | 数值与读请求匹配 = 正常；异常偏低 = 数据传输阻塞 | 检查 CXS 链路信用；<br />优化数据传输路径         |
| 6    | PMU_TXRSP_FLITV               | SBSX     | 发送的 TXRSP flit 数（CHI 响应带宽）               | 跨 DIE 请求响应返回、状态 / 握手信号传输    | 数值与请求匹配 = 正常；偏低 = 响应阻塞           | 增大 TXRSP 链路信用；<br />优化响应流控           |
| 17   | PMU_RD_REQ_TRKR_OCC_CNT_OVFL  | SBSX     | 读请求 Tracker 表项全满溢出                        | 跨 DIE 读请求并发过高、Tracker 资源耗尽     | **计数↑= 读请求 Tracker 满，跨 DIE 读阻塞**      | 增大读 Tracker 容量；<br />降低跨 DIE 读并发      |
| 18   | PMU_WR_REQ_TRKR_OCC_CNT_OVFL  | SBSX     | 写请求 Tracker 表项全满溢出                        | 跨 DIE 写请求并发过高、Tracker 资源耗尽     | **计数↑= 写请求 Tracker 满，跨 DIE 写阻塞**      | 增大写 Tracker 容量；<br />限流跨 DIE 写流量      |
| 19   | PMU_CMO_REQ_TRKR_OCC_CNT_OVFL | SBSX     | CMO 请求 Tracker 表项全满溢出                      | 跨 DIE CMO 操作过多、一致性请求过载         | **计数↑=CMO Tracker 满，缓存维护阻塞**           | 减少跨 DIE CMO 频率；<br />增大 CMO Tracker 容量  |
| 20   | PMU_WDB_OCC_CNT_OVFL          | SBSX     | 写数据缓冲区（WDB）全满溢出                        | 跨 DIE 写数据涌入过快、接收端处理缓慢       | **计数↑= 写数据缓冲满，写传输阻塞**              | 增大 WDB 深度；优化写数据流控                     |
| 21   | PMU_RD_AXI_TRKR_OCC_CNT_OVFL  | SBSX     | AXI 待处理读 DART Tracker 全满溢出                 | 跨 DIE 读请求转 AXI 后阻塞、内存响应慢      | **计数↑=AXI 读 Tracker 满，读转换阻塞**          | 优化 AXI 接口时序；<br />增大 DART Tracker 容量   |
| 22   | PMU_CMO_AXI_TRKR_OCC_CNT_OVFL | SBSX     | AXI 待处理 CMO DART Tracker 全满溢出               | 跨 DIE CMO 转 AXI 阻塞、一致性操作延迟高    | **计数↑=AXI CMO Tracker 满，维护操作阻塞**       | 降低跨 DIE CMO 量；<br />增大 DART Tracker 容量   |
| 23   | PMU_RDB_OCC_CNT_OVFL          | SBSX     | 读数据缓冲区（RDB）全满溢出（仅 MTU 使能）         | 跨 DIE 读数据返回过快、RDB 排空不及时       | **计数↑= 读数据缓冲满，读响应阻塞**              | 增大 RDB 深度；开启 MTU 优化；加快数据排空        |
| 33   | PMU_ARVALID_NO_ARREADY        | SBSX     | AXI 读地址通道有请求无握手（AR 通道停顿）          | 跨 DIE 读转 AXI 后、内存控制器无响应        | 计数↑=**AXI 读地址通道瓶颈**                     | 优化 AXI 从端响应；<br />增大 AR 通道信用         |
| 34   | PMU_AWVALID_NO_AWREADY        | SBSX     | AXI 写地址通道有请求无握手（AW 通道停顿）          | 跨 DIE 写转 AXI 后、内存控制器无响应        | 计数↑=**AXI 写地址通道瓶颈**                     | 优化 AXI 从端响应；<br />增大 AW 通道信用         |
| 35   | PMU_WVALID_NO_WREADY          | SBSX     | AXI 写数据通道有数据无握手（W 通道停顿）           | 跨 DIE 写数据传输过快、AXI 从端无法接收     | 计数↑=**AXI 写数据通道瓶颈**                     | 增大 W 通道缓冲；限流写数据突发                   |
| 36   | PMU_TXDAT_STALL               | SBSX     | CHI TXDAT 有效但无链路信用（数据发送停顿）         | 片间 CXS 链路反压、跨 DIE 数据信用不足      | 计数↑=**跨 DIE 数据发送瓶颈**                    | 增大 CXS TXDAT 信用；<br />优化链路流控           |
| 37   | PMU_TXRSP_STALL               | SBSX     | CHI TXRSP 有效但无链路信用（响应发送停顿）         | 片间 CXS 链路反压、跨 DIE 响应信用不足      | 计数↑=**跨 DIE 响应发送瓶颈**                    | 增大 CXS TXRSP 信用；<br />优化响应传输路径       |

对应上面的 bottleneck analysis：

- **4 号（RetryAck）**：有没有重试？→ 信用 / 资源够不够

- **33 号（AXI 读停顿）**：AXI 读地址堵不堵？

- **36 号（TXDAT_STALL）**：跨 DIE 读数据回传堵不堵？

- **37 号（TXRSP_STALL）**：跨 DIE 读响应堵不堵？

- **17 号（读 Tracker 满）**：内部读跟踪器满没满？

#### CCG Performace events

| 事件名称                        | 所属模块 | 事件含义                            | 触发原因（双 DIE 场景）                  | 瓶颈判断        | 解决方案                                                     |
| ------------------------------- | -------- | ----------------------------------- | ---------------------------------------- | --------------- | ------------------------------------------------------------ |
| **LA_TX_CXS_LCRD_BACKPRESSURE** | LA       | 片间 CXS 链路因**信用不足**产生反压 | 接收端缓冲区满、信用返还慢、链路带宽打满 | 该事件持续计数↑ | 1. 增大片间链路信用；<br />2. 加大接收端缓冲区3. 减少跨 DIE 突发流量； |
| **RA_REQ_PCRD_STALLS_LNKx**     | RA       | 跨 DIE 请求因链路无信用**发不出去** | 链路反压传导到请求层、请求并发过高       | 该事件计数↑     | 1. 增加请求信用；<br />2. 降低跨 DIE 请求并发；<br />3. 合并小请求； |
| **HA_SNP_PCRD_STALLS_LNKx**     | HA       | 跨 DIE 窥探请求阻塞、发不出去       | 跨 DIE 可缓存访问→**snoop 窥探风暴**     | 该事件暴涨↑     | 1. 跨 DIE 地址设为**非缓存**；<br />2. 关闭不必要 snoop；<br />3. 数据放同 DIE； |
| **HA_CHI_RSP_UPLOAD_STALLS**    | HA       | 一致性响应上传冲突、回传阻塞        | 响应与请求抢通道、HA/RA 内部竞争         | 该事件计数↑     | 1. 增加响应通道带宽；<br />2. 调整内部仲裁优先级；           |
| **RA_REQ_TRK_OCC**              | RA       | 请求跟踪器占用溢出、队列满          | 跨 DIE 请求堆积、处理不过来              | 该事件计数↑     | 1. 加大 RA 请求队列深度；<br />2. 限流跨 DIE 请求；          |
| **HA_RD_DAT_BUF_OCC**           | HA       | 读数据缓冲区占用溢出                | 跨 DIE 读返回数据堆积、处理慢            | 该事件计数↑     | 1. 加大 HA 读缓冲区；<br />2. 加快读响应处理；               |
| **PMU_XP_TXFLIT_STALL**         | XP       | CCG-CMN 网格端口发送停顿            | 内部网格拥塞、XP 端口反压                | 该事件计数↑     | 1. 优化 CMN 路由配置；<br />2. 增加网格端口带宽；            |
| **LA_TX_CXS**                   | LA       | 片间链路发送 CXS flit 总数          | 统计跨 DIE 真实流量                      | 流量≠带宽利用率 | 用于核对是否超链路设计上限                                   |