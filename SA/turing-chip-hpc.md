## 图灵芯片之算力篇

第一代图灵芯片的精华都在算力单元，也是就 DLA(Deep Learning Accelerator)，其他 IP 基本都是外购的。这里我们分析一下 DLA 的芯片架构，以及智驾算法在 DLA 上是怎样运行的，并和 GPGPU 芯片架构做比对，以此窥探智驾芯片未来的发展方向。

### Overview

#### introduce

![image-20250608153956053](D:\gitlab\ADAS\dla-arch-logic.png)

![image-20250608154005745](D:\gitlab\ADAS\dla-arch-phy.png)

各组件功能如下：

- DLA: Deep Learning Accelerator, **1 DLA contain 2 NPU cluste**r, which called NPC. Within a NPC, there are 4 NPU(neural processing unit) compute cores.
- NPU: 跑神经网络的；

- DSP: data signal processing for computing（SIMD 指令集），用于补充 NPU 支持不好的算子，主要是需要浮点运算的算子，承担额外算力支撑；

- RSU: 负责推理网络任务的调度和执行，以及 NPC 下面各种计算资源，内存资源的实时管理；

- CDTU : Cluster Data Transfer Unit is responsible for **data move and transfer** between L2 and L3 which is DDR memory；

#### capacity

| NN Inference               | NPU/DSP | NPC    | DLA    |
| -------------------------- | ------- | ------ | ------ |
| Process                    | TSMC7N  | TSMC7N | TSMC7N |
| Area(mm^2)(TBC)            | ~11     | ~96    | ~192   |
| Peak Freq(MHz)             | 1200    | 1200   | 1200   |
| NPU Number/NPC             | /       | 4      | /      |
| NPC Number/Chip            | /       | /      | 2      |
| INT8 MACs                  | 16384   | 65536  | 131072 |
| Peak INT8 MAC TOPS         | 39      | 157    | 315    |
| Peak INT8 TOPS w/ Winograd | 88      | 354    | 708    |
| Element-wise TOPS          | 0.3     | 1.2    | 2.4    |
| General TOPS(DSP.INT8)     | 2.4     | 14.5   | 29     |
| General TOPS(DSP.INT16)    | 0.3     | 3.65   | 7.3    |
| L1 Size(MB)                | 6       | 24     | 48     |
| L2 Size(MB)                | /       | 32     | 64     |
| L2->L1 BW(Bytes/Cycle)     | 128     | 512    | 1024   |
| L1->L2 BW(Bytes/Cycle)     | 128     | 1024   | 1024   |
| L3<->L2 Peak BW(GB/S)      | 24      | 96     | 96     |
| Data Compression           | /       | /      | /      |
| 4D DMA                     | 1 NDTU  | 1 CDTU | /      |
| RSU                        | /       | 1      | 2      |

#### memory resource

![image-20250608154951465](D:\gitlab\ADAS\dla-mem-res.png)

#### Compute Resource

8*NPU:

- Tensor operation

- Matrix operation

- Vector operation

- Activation operation

- Scalar operation

- Mix operation such as Softmax

6*DSP

- Vector operation

- Scalar operation

- Scatter&Gather

### NPU

![img](D:\gitlab\ADAS\npu-arch.png)

As above diagram, NPU v2.0 contain the blow modules

- Tensor Matrix Unit (TMU)
  - Convolution/GEMM

- VEctor Unit (VEU)
  - Activation/element-wise/Pooling/Quantization

- NPU Data Transform Unit (NDTU)
  - L2/DRAM <-> L1, L1 <-> L1, permute/reshape

- L1 RAM
  - unified buffer

- command DMA (cDMA)
  - command fetch/decode/dispatch

- Control Status Register (CSR)

### Scheduler Architecture

#### RSU

![image-20250608162014678](D:\gitlab\ADAS\rsu-arch.png)

RSU contains a uCPU. Firmware running in the uCPU triggers DMA to prefetch task streams from DDR into local memory, then firmware reads task streams from local memory, parse the command packets, schedules hardware resource, synchronizes execution flow, collects and reports completion result. 

Meanwhile, all interrupts from local block and other IPs can be sent to the uCPU. In firmware, scheduling tasks (prefetch, parsing, scheduling, execution of each streams) and IP interrupt service tasks are managed by RTOS (Real-Time Operate System) to achieve better performance. If multiple tasks are managed well, from task point of view, they can be executed parallelly.

![image-20250608162336720](D:\gitlab\ADAS\rsu-stream-arch.png)

RSU is designed as a scheduler, which is an interface between DLA hardware engines (NPU, DTU, CSYNC etc) and driver for task、resource、sync scheduling. Driver puts command streams in DDR, RSU prefetches and parses the command streams, then schedules hardware engines to complete task streams. During scheduling, RSU firmware should arrange hardware resource、dispatch command packet  and synchronizes them.

RSU Scheduling flow

- Step1: APU(or host driver) put command streams in DDR 
- Step2: APU(or host driver) sent task request to RSU,RSU checks task priority and selects command stream pointer.
- Step3: RSU read command stream and parses command packet. 
- Step4: **RSU handle these packets in stream one by one in each stream slot**.
- Step5: **Build deployment pipeline base on packets in stream**.
- Step6: Setup Sync (Mailbox/Csync) to connect all pipeline engine.
- Step7: Launch all pipeline engine(NPU/DTU) to run network task.
- Step8: Response finish of network task.

![image-20250608162600224](D:\gitlab\ADAS\rsu-schedule-flaw.png)

#### DSP

![image-20250608163144629](D:\gitlab\ADAS\dsp-arch.png)

The DSP system has the follow components:

- Four DSP(Data Signals Process),The DSP is VQ8 

- 2 XDMA: Each XDMA was shared by 2 DSPs，is responsible for data transfer between L1/L2 and L2/L3 and push instruction into VQ8 DSP ~8MB Level 2 shared memory
- IPCM : Mailbox support sync between DSPs or between DSP and APU

- Data/Control Fabric:  Support HASH access to L2B

- SPM : High speed shared buffer for sync data interaction between DSPs or DSP and RSU to support atomic accessed by all DSPs and RSU

##### Operation

- NN operator benchmark

  - GRU
  - Layernormal
  - Top.K
  - Gather
  - Scatter
  - 3D Affine
  - Sparse 3D Convolution 
  - Sparse Submanifold 3DConvolution

  - Confidential filter
  - Convolution with int16
  - Custom transcendental functions (6 operators，Refer to below)

![image-20250608163632082](D:\gitlab\ADAS\Custom-transcendental-functions.png)

- Supported operator（P2）
  - NN operators ： 200+kernels

### Power&Area

The below table is power rough target when compute engine running on 60% workload.

#### Power estimation

| NPU Power(W) Rough Target Break down @60%workload@1.2GHz@Sparsity 25% |      |      |
| ------------------------------------------------------------ | ---- | ---- |
| MACs array power                                             | 3.43 | 0.7  |
| Wino transform                                               | 0.05 | 0.01 |
| VEU                                                          | 0.01 | 0    |
| L1                                                           | 1.09 | 0.22 |
| NDTU                                                         | 0.2  | 0.04 |
| Total                                                        | 4.87 | 1    |

| NPC Power(W) Rough Target Break down @60%workload@1.2GHz@Sparsity 25% |       |      |
| ------------------------------------------------------------ | ----- | ---- |
| NPU                                                          | 19.49 | 0.85 |
| CDTU                                                         | 0.2   | 0.01 |
| L2                                                           | 2.8   | 0.12 |
| RSU                                                          | 0.15  | 0.01 |
| TOP                                                          | 0.22  | 0.01 |
| Total                                                        | 22.86 | 1    |

| DLA Power(W) Rough Target Break down @60%workload@1.2GHz@Sparsity 25% |       |      |
| ------------------------------------------------------------ | ----- | ---- |
| NPU                                                          | 38.98 | 0.75 |
| CDTU                                                         | 0.4   | 0.01 |
| L2                                                           | 5.6   | 0.11 |
| RSU                                                          | 0.3   | 0.01 |
| DSP                                                          | 6.01  | 0.12 |
| TOP                                                          | 0.44  | 0.01 |
| Total                                                        | 51.72 | 1    |

#### Memory target

| Memory         | Option-1 | Total |
| -------------- | -------- | ----- |
| Level 2 memory | 64MB     | 112MB |
| Level 1 memory | 8*6MB    |       |

#### Bandwidth target

| Memory   | Bandwidth |
| -------- | --------- |
| L2<->DDR | 64GB      |
| L2<->L1  | 2.4T      |

#### Area estimation

| NPU      | NPC      | Total(2*NPC) |
| -------- | -------- | ------------ |
| 10.5mm^2 | 91.5mm^2 | 183mm^2      |

| TSMC7nm : NPC PD Area(mm^2) Rough Target Break down |      |      |
| --------------------------------------------------- | ---- | ---- |
| NPU0 Area                                           | 10.5 | 0.11 |
| NPU1 Area                                           | 10.5 | 0.11 |
| NPU2 Area                                           | 10.5 | 0.11 |
| NPU3 Area                                           | 10.5 | 0.11 |
| 32MB L2 Area                                        | 24   | 0.25 |
| CDTU Area                                           | 0.5  | 0.01 |
| NOC Area                                            | 6    | 0.11 |
| RSU Area                                            | 0.8  | 0.01 |
| 6DSP Area                                           | 14.2 | 0.11 |
| Top Reserve                                         | 4    | 0.05 |
| Total Area                                          | 91.5 | 1    |

#### FloorPlan guide

![image-20250608164908115](D:\gitlab\ADAS\floorplan-guild.png)

### System&Data flow

![image-20250608165404562](D:\gitlab\ADAS\interface-data-flaw.png)

①Task Request ：APU向RSU发起推理任务Request，APU事先建立的Task Req在L3.DDR的Task Ring中； 

②Fetch task packet ：RSU从L3.DDR的Task Ring中取Task packet； 

③Fetch fetch CMD packet ：RSU从L3.DDR中取CMD packet； 

④Task distribution ： RSU 下发指令指针并Launch NPU/CDTU/DSP，RSU解析Task和Model CMD packet， 根据Model CMD中的信息下发各engine待运行指令指针（DDR 地址和长度）； 

⑤Instruction Fetch：各Engine取指令； 

⑥Load Data to L2 ：CDTU 将L3.DDR Input数据load到L2，数据类型包括Input； 

⑦Load Data to L1 ：NPU/DSP中的DTU/DMA将L2 Input数据load到NPU/DSP中的L1； 

⑧Load Parameter to L2 ：CDTU 将L3.DDR中的Parameter load到L2； 

⑨Load Parameter to L1 ：NPU/DSP中的DTU/DMA将L2 Parameter load到NPU/DSP中的L1； 

⑩Compute ：NPU/DSP 算子计算； 

⑪Store Data to L2 ：NPU/DSP中的DTU/DMA将L1数据store到L2，数据类型包括中间缓存的激活数据（Activation）； 

⑫Store Data to L3 ：CDTU将L2数据store到L3，数据类型包括中间缓存的激活数据（Activation）； 

⑬Load Data to L2 ：CDTU 将L3.DDR中计算的中间数据load到L2； 

⑭Load Data to L1 ：NPU/DSP中的DTU/DMA将L2 中计算的中间数据 load到NPU/DSP中的L1； 

（⑤ ~ ⑭ 分部循环执行） 

⑮Store Inference Result to L2 ：NPU/DSP中的DTU/DMA将L1计算的结果store到L2; 

⑯Store Inference Result to L2 ：NDTU将L1中的结果store到L2; 

⑰Task finish response to RSU ：NPU/DSP/CDTU通过Mail或中断方式向RSU返回Task结束response 

⑱Task finish response to APU ：RSU通过Mail或中断方式向APU返回Task结束response

![image-20250608165502400](D:\gitlab\ADAS\dla-interface-data-flaw.png)