## System Level Cache

首先考虑为什么需要 SLC。

将 CPU、GPU、加速器、内存等异构单元看作独立结点，通过总线协议进行拓扑连接，并引入 SLC 实现数据交互管理，维护 cache 和 IO 一致性，同时解决资源竞争，提升系统数据利用率并进行低功耗设计。

目前知道的有 ARM CI-700 和 Arteris NOC 两款 IP 支持 SLC，我们先看看 ARM CI-700。

### ARM CI-700

CI-700 是一种支持连接 1-8 个处理器的网格拓扑结构的一致性总线，其支持 SLC。它是遵从 AMBA5 CHI 总线协议的一种实现。

#### 结构

CI-700 的结构就是 Crosspoints(XPs) 和 CHI 兼容设备之间的排列。XPs 之间相互连接组成网格，CHI 兼容设备连接到 XPs 的设备端口上。CHI 兼容设备发送和接收网络请求，以及处理数据；XPs 就是网络路由器，负责在各个设备，XPs 之间转发请求、数据。

CI-700 能够配置成单 XPs 或多 XPs，它们的拓扑结构如下图，

![image-20230504084040783](D:\gitlab\UtopianFuture.github.io\image\ci-700-topoly.png)

![image-20230505090103935](D:\gitlab\UtopianFuture.github.io\image\ci-700-topoly-1.png)

#### 组件

CI-700 根据不同的功能须有能够挂载不同的设备，这里列举其支持的所有设备，

##### 外部设备

| Device               | Description                                                  |
| -------------------- | ------------------------------------------------------------ |
| RNF_CHIB_ESAM        | A CHI Issue B-compliant RN-F(*Fully coherent Requesting Nodes*) without a built-in SAM. **RN-Fs are master devices with hardware-coherent caches**. RN-Fs are processors, clusters, GPUs, or other RNs with a coherent cache. ESAM-type RN-Fs do not have a built-in SAM, and the SAM logic is contained within CI‑700. |
| RNF_CHIC_ESAM        | A CHI Issue C-compliant RN-F without a built-in SAM.         |
| RNF_CHID_ESAM        | A CHI Issue D-compliant RN-F without a built-in SAM.         |
| RNF_CHIE_ESAM        | A CHI Issue E-compliant RN-F without a built-in SAM.         |
| CHI Slave Node(SN-F) | **SN-Fs are CHI memory controllers**. SN-Fs are devices which solely receive CHI commands, limited to fulfilling simple read, write, and CMO requests targeting normal memory. |

##### 内部设备

| Device                                                   | Description                                                  |
| -------------------------------------------------------- | ------------------------------------------------------------ |
| IO coherent Request Node (RN-I)                          | Use an RN-I to connect one or more AXI or ACE-Lite master devices to CI‑700. RN-Is have 3 external AXI or ACE-Lite slave interfaces for connecting AXI or ACE-Lite master devices to, and bridges between AXI/ACE-Lite and CHI protocols. Within the interconnect, the RN-I is a non-caching I/O-coherent master device. Therefore, it acts as a CHI RN-I proxy for each upstream AXI or ACE-Lite master device. There is no capability to issue snoop transactions to RN-Is. |
| IO coherent Request Node with DVM support (RN-D)         | A subtype of RN-I with an ACE-Lite-with-DVM slave interface, allowing the RN-D to accept Distributed Virtual Memory (DVM) messages on the snoop channel |
| Fully coherent Home Node (HN-F)                          | **The HN-F acts as a Home Node (HN) for a coherent region of memory**. HN-Fs accept coherent requests from RN-Fs and RN-Is, and generate snoops to all applicable RN-Fs in the system as required to support the coherency protocol. HN-Fs are typically configured with one or both of the internal SLC and SF components. **The SLC acts as a last-level cache and the SF tracks cachelines that RN-Fs in the system have cached**. HN-Fs also contain the combined Point-of-Serialization and Point-of-Coherency (PoS and PoC), which is responsible for ordering of all memory requests sent to the HN-F. |
| IO coherent Home Node (HN-I)                             | Use HN-Is to connect non-coherent I/O slaves or subsystems to CI‑700. HN-Is have an external AXI or ACE-Lite master interface and act as an HN for a downstream I/O slave or subsystem. They are responsible for ensuring proper ordering of requests targeting the slave. HN-Is do not support caching of any data read from or written to the downstream I/O slave or slave subsystem. |
| IO coherent Home Node with Debug Trace Controller (HN-T) | A subtype of HN-I with a built-in Debug Trace Controller (DTC) and ATB. |
| IO coherent Home Node with DVM node (HND)                | A subtype of HN-I with the following built-in components:<br/>• DTC<br/>• DVM Node (DN)<br/>• Configuration Node (CFG)<br/>• Global Configuration Slave<br/>• Power/Clock Control Block (PCCB)<br/>Note:<br/>Exactly one HN‑D is required per CI-700 instance. The HN-D has an AXI or ACE-Lite external master interface, an APB interface, and supports ATB. |
| CHI to AXI or ACE-Lite bridge (SBSX)                     | Use an SBSX to connect an AXI or ACE-Lite slave memory device to CI-700. For example, you can use an SBSX to connect the CoreLink™ DMC-400 Dynamic Memory Controller to a CI-700 system. SBSXs have an external AXI or ACE-Lite master interface for connecting an AXI or ACE-Lite slave to, and bridges between CHI and AXI/ACE-Lite protocols. They convert and forward simple CHI read, write, and CMO commands to the slave memory device. If connecting a device that supports AXI-G or earlier through the SBSX, then you must tie some of the SBSX pins to certain values |
| Memory Tag Slave Interface (MTSX)                        | Use an MTSX to connect AXI slave devices that do not support memory tagging to CI‑700. MTSXs have an external AXI or ACE-Lite master interface for connecting an AXI or ACE-Lite slave to, and bridges between CHI and AXI/ACE-Lite protocols. They convert and forward simple CHI read, write, and CMO commands to the slave memory device. |

- SF

  **The SF tracks cache lines that are present in the RN-Fs**. Using the SF **reduces snoop traffic in the system** by favoring（偏向） directed snoops over snoop broadcasts when possible（不懂）. This approach substantially reduces the snoop response traffic that might otherwise be required.
  
  这种方式应该是目录协议，将所有的 cache line status 保存在 directory 中，SF 就起目录的作用，全局统一管理 cache 状态；

总结一下，上面的这些节点可以分为 3 类：

- HN(Home Node)：协调所有的传输请求，包括snoop请求、访问cache和memory，位于ICN内，用于接收来自RN产生的协议transactions；

- RN（Request Node）：Request Node，产生协议transactions，包含读和写。它主要是 CPU，GPU，或者其他具有缓存的设备；

- SN（Slave Node）用于接收来自HN的请求，完成相应的操作并返回一个响应。它主要是能够响应 CHI 请求的各种设备；

  总结：一个RN会产生transaction(read, write, maintenance)给HN，HN接收后对RN发来的请求进行排序，产生transaction给SN，SN接收这些请求，返回数据或者响应。

##### mesh 组件

| Component                         | Description                                                  |
| --------------------------------- | ------------------------------------------------------------ |
| Crosspoint (XP)                   | A switch or router logic module. XPs are the fundamental building block of the CI-700 transport mechanism. XPs connect together through mesh ports. Devices connect to the mesh through device ports on XPs. |
| Component Aggregation Layer (CAL) | Allows multiple devices to connect to a single device port on an XP. Only certain devices can connect to CALs. All devices that connect to a single CAL must be of the same type and you must configure them identically. |
| Credited Slices (CSs)             | Credited register slices that incur latency in communication but help with timing closure. There are various types of CSs, which are used for different parts of the interconnect. |
| CHI Domain Bridge (CDB)           | Bridges two CHI interfaces that operate in two different clock domains, power/voltage domains, or both. |
| AMBA Domain Bridge (ADB)          | Bridges two AXI, ACE5-Lite, or ACE5-Lite-with-DVM interfaces that operate in two different clock domains, power/voltage domains, or both. |

#### SLC

SLC 是由 Fully coherent Home Node (HN-F) 节点实现的。在一个系统中，HN-F 可以配置 1~8 个，每个 HN-F 节点具有如下属性：

- 0KB, 128KB, 256KB, 512KB, 1MB, 2MB, 3MB, or 4MB of SLC data RAM and tag RAM（会不会太小了，L3 cache 都有 8MB）;
- Combined Point-of-Coherency (PoC) and Point-of-Serialization (PoS);
- SF size of 512KB, 1MB, 2MB, 4MB, or 8MB（不理解啊，为什么 SF 要比 SLB 要大，这代价也太大了）;

每个 HN-F 节点可以配置成管理特定部分的地址空间，对于这个部分的地址空间，HN-F 能够：

- 将数据缓存在 SLC 中；
- 管理 PoC 和 PoS 特性（不懂）；
- 使用 SF 跟踪 cache line；

整个 SLC 系统具备如下信息：

- PIPT；
- cacheline 大小为 64B；
- SLC 和 SF 都是 16 路组相联；
- SLC 和 SF 默认都是采用伪随机算法选择 victim；
- CI-700 也支持 enhanced LRU cache 替换算法；
- 支持 MPAM；

还有更多特性，之后有需要再看。

##### 组件和配置

每个 HN-F 能够发送三种类型的 snoop 信息：

- Directed：发送给 RN-F；
- Multicast：发送给多个但不是所有的 RN-F；
- Brodcast：发送给所有的 RN-F。

HN-F 支持一个基于硬件的 cache 刷新机制来刷新 SF 和 SLC。能够通过特性的寄存器来配置刷新所有的 cacheline。HN-F 刷新 cache 分为以下几个步骤：

- 刷新 CI-700 SF。这个操作刷新所有在 low-level（这个 low-level 不是很理解，难道 HN-F 里面还分 low-level cache 和 high-level cache？应该是指高地址和低地址空间） 的 cacheline；
- 刷新 SLC；

HN-F 的默认配置是在将 cacheline 置为 invalid 前需要将 modified data 写回到对应的内存。另外其还支持两种配置：

- ClenaInvalid: Write back and invalidate (default);
- MakeInvalid: modified data is not written back to memory;
- CleanShare: 
- modified data is written back to memory but clean data remain in internal caches;

HN-F 还支持通过 way reservation 的方式将部分 memory region 锁在内存中，memory region 的大小能够通过软件配置。这部分 memory region 永远不会从 SLC 中被 evicted，任何访问这些 cacheline 的请求都是 hit。HN-F 是通过特定的寄存器来配置哪些 way 需要 lock。

上面描述的是根据地址空间的范围（memory region）来 lock way，更进一步，HN-F 还支持为某个特定的 RN 节点 lock way。这一特性使得 locked way 中新的 cacheline 只能够被特性的 RN 节点使用。

每个 SLC 实体能够被分成不同的 region，这种分区允许每个 RN 节点被分配到到一个或多个 region，每个 region 包含 4 个连续的 way。

##### Transaction handling

CI-700 能够处理不同类型的 CHI 操作和传输类型。CI-700 通过几种 CHI Cache Maintenance Operations(CMOs) 来维护 cache 一致性。

- CleanInvalid.
- CleanShared.
- MakeInvalid.
- CleanSharedPersist.
- CleanSharedPersistSep.

这些操作会对 SLC 和 SF 完成如下动作：

- Clean and invalidate the line if present in the SLC.
- If the CMO is snoopable, the HN-F sends a snoop to the RN-F post SF lookup if necessary.
- If the cache line is modified in the SLC or in the cache of the RN-Fs, the HN-F initiates a memory controller WriteBack if necessary

### 场景分析

上面介绍的是 CI-700 的基本结构，但是实际开发中需要结合使用场景分析。



### 问题

- CI-700 怎样完成 cache 一致性？
- 不同 HN-F 之间如何交互？
- 

### Reference

[1] Arm® CoreLink™ CI-700 Coherent Interconnect Technical Reference Manual