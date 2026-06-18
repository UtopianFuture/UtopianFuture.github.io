# SOC 性能验证

## Overview

SOC 在整个开发过程中会经历不同阶段，不同阶段都需要有对应的性能验证策略。本文用来指导SOC在整个芯片开发过程中如何有效的进行性能验证，最终做好性能signoff。本文的主要阅读对象是SE,DE,DV & SW。

## 性能验证Scope

SoC系统性能验证主要分成3个阶段（or 平台），分别是EDA, Palludim, 以及回片后。不同阶段所cover的重点也有所不同。下面这个思维导图分解了这3个阶段的验证计划：

![SOC-performance-verify](D:\gitlab\UtopianFuture.github.io\image\SOC-performance-verify.svg)

## EDA 平台验证阶段

 EDA验证阶段需要重点cover：

- subsys/ip的性能：包括理论算力，Throughput等；
- 各高速接口的理论BW：PCIe/UCIe/Eth/UFS/USB/MIPIRX/MIPITX/DP；
- subsys/ipP2P访问DDR的BW, Latency；

### SoC各IP/Subsys的验证

 这里的SoC各IP/Subsys是指对除NGC&APSS之外的其他SoC子系统的性能验证。其中包括：

- IP/Subsys的处理能力；
- IP/Subsys的P2P访存BW，Latency；
- IP有exclusive访问需求，P2P访问RAM的Latency；
- 各处理器通过MHU做核间通信的Latency；
- 各处理器、DMA访问片上RAM的BW，Latency；

#### Subsys/IP处理能力

  IP/Subsys本身的处理能力（比如ISP，编解码，GDC...等加速器本身的硬件处理性能），可以在UT环境下完成。

- **验证环境**

  这里不需要SOC环境，外部不需要集成总线，DDR用VIP实现，VIP内设置延时(该延时不易过大，按照P2P静态延时设置，该延时为NOC+DDR的总延时)，目的是为了给IP/Subsys提供足够的带宽，不让带宽成为性能Loss的因素。这里重点验证的是IP内本身的处理性能，看Pipeline上是否有bubble，是否有调度不合理的地方导致性能Loss等。

- **Sign off标准**
  - 子系统IP的处理能力是否能达到理论预期值
  - 如果没有达到预期，需要给出性能Loss的原因

| IP            | Throughput/算力需求                    | IP实际算力    | IP理论算力   | 算力利用率 | IP 主频 | Signoff标准          |
| ------------- | -------------------------------------- | ------------- | ------------ | ---------- | ------- | -------------------- |
| CVISP         | 2.36Gpixels/s                          | 2.38Gpixels/s | 2.7Gpixels/s | 88%        | 1.35GHz | > 2.7Gpixels/s*95%   |
| HVISP         | 2.47Gpixels/s                          | 2.55Gpixels/s | 3.0Gpixels/s | 85%        | 1.0GHz  | > 3.0Gpixels/s*95%   |
| Video-Encoder | 4K245                                  | 4K@324        | 4K@450       | 72%        | 1.0GHz  | > 4K@324 * 95%       |
| Video-Decoder | 4K180                                  | 4K@202        | 4K@220       | 92%        | 1.0GHz  | > 4K@202 * 95%       |
| RAW-Encoder   | 4K245                                  | 4K276         | 4K325        | 85%        | 1.35GHz | > 4K@276 * 95%       |
| RAW-Decoder   | 4K180                                  | 4K327         | 4K385        | 85%        | 0.8GHz  | > 4K327* 95%         |
| GDC           | 0.87Gpixels/s                          | 1.44Gpixels/s | 2.4G pixel/s | 60%        | 1.2GHz  | > 1.44Gpixels/s* 95% |
| cvDSP         | 所有任务1s内处理完所需要的时间是3955ms | 3955ms        | 4000ms       | 99%        | 1.2GHz  | > 4000ms*99%         |
| DPU           | 3*1080P@60                             | 4*4K@60       | 4*4K@60      | -          | 0.6GHz  | > 4*4K@60*95%        |
| GPU           | 0.6Gpixel/s                            | 1.2Gpixels/s  | 2Gpixels/s   | 0.6        | 1.0GHz  | > 1.2Gpixels*95%     |

#####   VI

- VI验证环境

  如下图所示，激励从16路IPI灌入，DDV(VIP) 设置Static Latency=200 cycles(Freq=1GHz)。

- case构造

  - ***Case1:*** *16路4K，RAW28 Camera+1前4角5个 4D毫米雷达的极限性能测试；*
  
  
    - ***Case2:*** *SOC的典型Sensor场景；*
  
      | Camera          | Width  | Height | FPS  | bpp  |
      | --------------- | ------ | ------ | ---- | ---- |
      | Fisheye         | 3840   | 2160   | 30   | 28   |
      | Narrow          | 3840   | 2160   | 30   | 28   |
      | Rear            | 3840   | 2160   | 30   | 28   |
      | Side-FrontLeft  | 2880   | 1860   | 30   | 28   |
      | Side-FrontRight | 2880   | 1860   | 30   | 28   |
      | Side-RearLeft   | 2880   | 1860   | 30   | 28   |
      | Side-RearRight  | 2880   | 1860   | 30   | 28   |
      | AVM-Front       | 2880   | 1860   | 30   | 28   |
      | AVM-Rear        | 2880   | 1860   | 30   | 28   |
      | AVM-Left        | 2880   | 1860   | 30   | 28   |
      | AVM-Right       | 2880   | 1860   | 30   | 28   |
      | DMS             | 1600   | 1300   | 60   | 10   |
      | OMS_Front       | 2560   | 1984   | 60   | 12   |
      | OMS_Middle      | 2560   | 1984   | 60   | 12   |
      | CMS-Right       | 2880   | 1860   | 60   | 16   |
      | CMS-Left        | 2880   | 1860   | 60   | 16   |
      | 4D毫米波雷达    | 1前4角 |        |      |      |


- 性能Signoff标准

  case1和case2均能满足性能要求；

##### CVISP

- CVISP验证环境

  如下图，原始图像数据可以从VI输入，也可以直接在DDR(VIP)的buf中直接写入数据。

- case构造
  
  - ***Case1:*** *12路4K Raw28@24fps，non-slice mode*
  
    | Camera  | Width | Height | FPS  | Line Penalty | bpp(Raw) | camera num | on/off | Throughput Req(Gpixel/s) |
    | ------- | ----- | ------ | ---- | ------------ | -------- | ---------- | ------ | ------------------------ |
    | Fisheye | 3840  | 2160   | 24   | 0            | RAW28    | 12         | 1      | 2.39                     |
    |         |       |        |      |              |          |            |        | 2.39                     |
    |         |       |        |      |              |          | efficiency | 89%    | 2.68                     |
  
  - ***Case2:*** *SOC典型应用场景下的Sensor规格*
  
    - ***Slice Mode(Slice =2)***
  
      | Camera         | Width | Height | FPS  | Line Penalty | bpp(Raw) | camera num | on/off | Throughput Req(Gpixel/s) |
      | -------------- | ----- | ------ | ---- | ------------ | -------- | ---------- | ------ | ------------------------ |
      | Fisheye        | 3840  | 2160   | 30   | 180          | RAW28    | 1          | 1      | 0.27                     |
      | Narrow         | 3840  | 2160   | 30   | 180          | RAW28    | 1          | 1      | 0.27                     |
      | Rear           | 3840  | 2160   | 30   | 180          | RAW28    | 1          | 1      | 0.27                     |
      | SideView*4     | 2880  | 1860   | 30   | 180          | RAW24    | 4          | 1      | 0.71                     |
      | SurroundView*4 | 2880  | 1860   | 30   | 180          | RAW24    | 4          | 1      | 0.71                     |
      | DMS            | 1600  | 1300   | 60   | 180          | RAW10    | 1          | 1      | 0.14                     |
      |                |       |        |      |              |          |            |        | 2.36                     |
      |                |       |        |      |              |          | efficiency | 88%    | 2.68                     |
  
    - ***non-Slice Mode***
    
      | Camera         | Width | Height | FPS  | Line Penalty | bpp(Raw) | camera num | on/off | Throughput Req(Gpixel/s) |
      | -------------- | ----- | ------ | ---- | ------------ | -------- | ---------- | ------ | ------------------------ |
      | Fisheye        | 3840  | 2160   | 30   | 0            | RAW28    | 1          | 1      | 0.25                     |
      | Narrow         | 3840  | 2160   | 30   | 0            | RAW28    | 1          | 1      | 0.25                     |
      | Rear           | 3840  | 2160   | 30   | 0            | RAW28    | 1          | 1      | 0.25                     |
      | SideView*4     | 2880  | 1860   | 30   | 0            | RAW24    | 4          | 1      | 0.64                     |
      | SurroundView*4 | 2880  | 1860   | 30   | 0            | RAW24    | 4          | 1      | 0.64                     |
      | DMS            | 1600  | 1300   | 60   | 0            | RAW10    | 1          | 1      | 0.12                     |
      |                |       |        |      |              |          |            |        | 2.16                     |
      |                |       |        |      |              |          | efficiency | 88%    | 2.45                     |

- 性能Signoff标准

  - *case1，12路4K@24fps满足性能要求*

  - *case2，SOC典型应用场景下，Slice mode/non-slice mode性能均满足要求*


#####   HVISP

- HVISP验证环境

- case构*造*
  - ***Case1:*** *12路4K Raw28@30fps，non-slice mode*
  
    | Camera  | Width | Height | FPS  | Line Penalty | bpp(Raw) | camera num | on/off | Throughput Req(Gpixel/s) |
    | ------- | ----- | ------ | ---- | ------------ | -------- | ---------- | ------ | ------------------------ |
    | Fisheye | 3840  | 2160   | 24   | 0            | RAW28    | 12         | 1      | 2.39                     |
    |         |       |        |      |              |          |            |        | 2.39                     |
    |         |       |        |      |              |          | efficiency | 88%    | 2.71                     |
  
  - ***Case2:*** *SOC典型应用场景下的Sensor规格*
  
    - ***Slice Mode(Slice =2)***
  
      | Camera        | Width | Height | FPS  | Line Penalty | bpp(Raw) | camera num | on/off | Throughput Req(Gpixel/s) |
      | ------------- | ----- | ------ | ---- | ------------ | -------- | ---------- | ------ | ------------------------ |
      | Fisheye       | 3840  | 2160   | 30   | 180          | RAW28    | 1          | 1      | 0.27                     |
      | Narrow        | 3840  | 2160   | 30   | 180          | RAW28    | 1          | 0      | 0.00                     |
      | Rear          | 3840  | 2160   | 30   | 180          | RAW28    | 1          | 1      | 0.27                     |
      | SideView*4    | 2880  | 1860   | 30   | 180          | RAW24    | 2          | 1      | 0.35                     |
      | SuroundView*4 | 2880  | 1860   | 30   | 180          | RAW24    | 4          | 1      | 0.71                     |
      | DMS           | 1600  | 1300   | 60   | 180          | RAW10    | 1          | 0      | 0.00                     |
      | OMS           | 2592  | 1948   | 30   | 180          | RAW12    | 1          | 1      | 0.17                     |
      | CMS           | 2880  | 1860   | 60   | 180          | RAW16    | 2          | 1      | 0.71                     |
      |               |       |        |      |              |          |            |        | 2.47                     |
      |               |       |        |      |              |          | efficiency | 85%    | 2.90                     |
  
    - ***non-Slice Mode***
  
      | Camera        | Width | Height | FPS  | Line Penalty | bpp(Raw) | camera num | on/off | Throughput Req(Gpixel/s) |
      | ------------- | ----- | ------ | ---- | ------------ | -------- | ---------- | ------ | ------------------------ |
      | Fisheye       | 3840  | 2160   | 30   | 0            | RAW28    | 1          | 1      | 0.25                     |
      | Narrow        | 3840  | 2160   | 30   | 0            | RAW28    | 1          | 1      | 0.25                     |
      | Rear          | 3840  | 2160   | 30   | 0            | RAW28    | 1          | 1      | 0.25                     |
      | SideView*4    | 3840  | 2160   | 30   | 0            | RAW24    | 4          | 1      | 1.00                     |
      | SuroundView*4 | 1920  | 1536   | 30   | 0            | RAW24    | 4          | 1      | 0.35                     |
      | DMS           | 1600  | 1300   | 60   | 0            | RAW10    | 1          | 0      | 0.00                     |
      | OMS*2         | 2592  | 1948   | 30   | 0            | RAW12    | 1          | 0      | 0.00                     |
      | CMS-L/R       | 2880  | 1860   | 60   | 0            | RAW16    | 2          | 1      | 0.64                     |
      |               |       |        |      |              |          |            |        | 2.10                     |
      |               |       |        |      |              |          | efficiency | 85%    | 2.47                     |
  
- 性能Signoff标准

  - *case1，12路4K@24fps满足性能要求*

  - *case2，SOC典型应用场景下，Slice mode/non-slice mode性能均满足要求*


#####   Video-Codec

- Video-Codec 验证环境

- * *Encoder 理论性能：4K324(3core，4K@156\*72%)*
  * *Decoder 理论性能：4K200(1ore，4K@220\*92%)*

- case构造

  - **Encoder**

    - ***Case1:*** *需要构造在不同帧率、码率、GOP下的的编码性能case*

      | Chans | Width | Height | Format | 帧率 fps | 码率 Mbps | 压缩比      | GOP  | 编码性能 （4K@fps） | Avg RGB PSNR (dB) | Avg NV12 PSNR (dB) | HW利用率 （%） |
      | ----- | ----- | ------ | ------ | -------- | --------- | ----------- | ---- | ------------------- | ----------------- | ------------------ | -------------- |
      | 12    | 3840  | 2160   | RGB888 | 24       | 80        | 29.85984    | 12   | TBD                 | TBD               | TBD                | TBD            |
      | 12    | 3840  | 2160   | RGB888 | 24       | 60        | 39.81312    | 12   | TBD                 | TBD               | TBD                | TBD            |
      | 12    | 3840  | 2160   | RGB888 | 24       | 50        | 47.775744   | 12   | TBD                 | TBD               | TBD                | TBD            |
      | 12    | 3840  | 2160   | RGB888 | 24       | 40        | 59.71968    | 12   | TBD                 | TBD               | TBD                | TBD            |
      | 12    | 3840  | 2160   | RGB888 | 24       | 35        | 68.25106286 | 12   | TBD                 | TBD               | TBD                | TBD            |
      | 12    | 3840  | 2160   | RGB888 | 24       | 30        | 79.62624    | 12   | TBD                 | TBD               | TBD                | TBD            |
      | 12    | 3840  | 2160   | RGB888 | 12       | 60        | 19.90656    | 12   | TBD                 | TBD               | TBD                | TBD            |
      | 12    | 3840  | 2160   | RGB888 | 12       | 50        | 23.887872   | 12   | TBD                 | TBD               | TBD                | TBD            |
      | 12    | 3840  | 2160   | RGB888 | 12       | 40        | 29.85984    | 12   | TBD                 | TBD               | TBD                | TBD            |
      | 12    | 3840  | 2160   | RGB888 | 12       | 35        | 34.12553143 | 12   | TBD                 | TBD               | TBD                | TBD            |
      | 12    | 3840  | 2160   | RGB888 | 12       | 30        | 39.81312    | 12   | TBD                 | TBD               | TBD                | TBD            |

    - ***Case2:*** *SOC典型应用场景下，Encoder性能是否满足需求*


  - **Decoder**
    - ***Case1:*** *构造4K or 1080P分辨率下编码case，看起实际性能是否符合理论性能值；*
    - ***case2:*** *SOC典型应用场景下，Encoder性能是否满足需求；*

**性能signoff标准**

- V-Encoder符合理论性能预期4K324；
- V-Decoder符合理论性能预4K200；
- V-Encoder满足SOC典型应用场景下的编码性能需求；
- V-Decoder满足SOC典型应用场景下的编码性能需求；

#### 高速接口带宽

#####   PCIE

- 验证目标

  验证PCIE是否能达到理论预期性能

- 验证环境

- Signoff标准

- **PCIE IP GEN5 x4 性能Signoff 标准**
  - RD only : 16GB/s *75%（efficiency）
  - WR only : 16GB/s *75%（efficiency）
  - RD & WR in Parallel : 
    - RD:16GB/s *75%（efficiency）
    - WR: 16GB/s *75%（efficiency）
- **PCIE IP GEN5 x2 性能Signoff 标准**
  - RD only : 16GB/s *75%（efficiency）
  - WR only : 16GB/s *75%（efficiency）
  - RD & WR in Parallel : 
    - RD:16GB/s *75%（efficiency）
    - WR: 16GB/s *75%（efficiency）

- case建议
  - **参数设置：**
    - MRRS: 512byte, MPS: 512byte/256byte, transfer size: 256kB/32kB;
    - HDMA write channel: 16, read channel: 16
  
- **建议Testcase：**

  - *case1: 16channel， 每个channel transfer size:1M/4M/8M/16M,MPS=512Byte*
    - *在Noncoherent和Conherent情况下，PCIE-HDMA write/read均能达到性能要求*

  - *case2: 16channel， 每个channel transfer size:1M/4M/8M/16M,MPS=256Byte*

    - *在Noncoherent和Conherent情况下，PCIE-DMA write/read均能达到性能要求*

    - *相较于大包MPS为512Byte时， PCIE-HDMA write带宽有一定损失， 对read带宽影响较小*
  
  
    - *case3: 16channel， 每个channel transfer size:1M/4M/8M/16M,MPS=128Byte*
  - *在Noncoherent和Conherent情况下，PCIE-HDMA write/read均能达到性能要求*
    
  - *相较于大包MPS为512/256Byte时， PCIE-HDMA write带宽有一定损失，对read带宽影响较小
  
  
  - *case4: PCIE(RC/EP)+SMMU*
  
  
  - *case5: PCIE(RC/EP)+SMMU(Bypass)*
  


#####   USB

- 验证目标

  验证USB是否能达到理论预期性能

- 验证环境

- Signoff标准

| ID   | Case Name                     | Protocol Peak Rate | Throughput  [VIP700/1000] (USB DATA WL:600+ns, RL:800+ns) (USB Data OT:4) (USB Data BL:16) | Line Rate  [VIP700/1000] (USB DATA WL:600+ns, RL:800+ns) (USB Data OT:4) (USB Data BL:16) | Percent  [VIP700/1000] (USB DATA WL:600+ns, RL:800+ns) (USB Data OT:4) (USB Data BL:16) |
| ---- | ----------------------------- | ------------------ | ------------------------------------------------------------ | ------------------------------------------------------------ | ------------------------------------------------------------ |
| 1    | usb_facing_device_hs_bulkin   | 480Mbps            | 待测                                                         | 待测                                                         | >90%                                                         |
| 2    | usb_facing_device_hs_bulkout  | 480Mbps            | 待测                                                         | 待测                                                         | >90%                                                         |
| 3    | usb_facing_device_ss_bulkin   | 5Gbps              | 待测                                                         | 待测                                                         | >90%                                                         |
| 4    | usb_facing_device_ss_bulkout  | 5Gbps              | 待测                                                         | 待测                                                         | >90%                                                         |
| 5    | usb_facing_device_ssp_bulkin  | 10Gbps             | 待测                                                         | 待测                                                         | >80%                                                         |
| 6    | usb_facing_device_ssp_bulkout | 10Gbps             | 待测                                                         | 待测                                                         | >80%                                                         |
| 7    | usb_facing_host_hs_bulkin     | 480Mbps            | 待测                                                         | 待测                                                         | >85%                                                         |
| 8    | usb_facing_host_hs_bulkout    | 480Mbps            | 待测                                                         | 待测                                                         | >85%                                                         |
| 9    | usb_facing_host_ss_bulkin     | 5Gbps              | 待测                                                         | 待测                                                         | >80%                                                         |
| 10   | usb_facing_host_ss_bulkout    | 5Gbps              | 待测                                                         | 待测                                                         | >80%                                                         |
| 11   | usb_facing_host_ssp_bulkin    | 10Gbps             | 待测                                                         | 待测                                                         | >70%                                                         |
| 12   | usb_facing_host_ssp_bulkout   | 10Gbps             | 待测                                                         | 待测                                                         | >70%                                                         |

- case建议
  - **SOC.USB as Device**
    - ioc path
    - non-ioc path

  - **SOC.USB as Host**
    - ioc path
    - non-ioc path

  - **Case list**
    - *usb_facing_device_hs_bulkout*
    - *usb_facing_device_hs_bulkin*
    - *usb_facing_device_ss_bulkout*
    - *usb_facing_device_ss_bulkin*
    - *usb_facing_device_ssp_bulkout*
    - *usb_facing_device_ssp_bulkin*
    - *usb_facing_host_hs_bulkout*
    - *usb_facing_host_hs_bulkin*
    - *usb_facing_host_ssp_bulkout*
    - *usb_facing_host_ssp_bulkin*
    - *usb_facing_host_ss_bulkout*
    - *usb_facing_host_ss_bulkin*


##### ETH

- 验证目标

  验证ETH是否能达到理论预期性能

- 验证环境

- **参数设置**

  GMII接口：312.5MHz * 32 bit/cycle = 10Gbps

  Payload length ：x byte

  Preamble: 8 byte

  帧间隙：12 byte

  如果ETH工作在最理想的情况下，数据速率与数据长度的关系：

   ***Bandwidth = x/(x+20) \* 10Gbps***

- Signoff标准：**Both coh & non-coh path**

  - RD only: 1.125GB/s(90% Line Rate utilization)
  - WR only: 1.125GB/s(90% Line Rate utilization)
  - RD & WR in parallel: RD:1.125GB/s, WR:1.125GB/s
  
- case建议

  - ETH+SMMU(bypass)+NoC+DDR(non coherent) ：512 Packet * 1024 Byte
  - ETH+SMMU(bypass)+NoC+DDR (non coherent) : 512 Packet * 32 Byte
  - ETH+SMMU+NoC+DDR (non coherent) : 512 Packet * 1024B
  - ETH+SMMU+NoC+DDR (non coherent) : 512 Packet * 32B
  - ETH+SMMU+NoC+DDR (coherent) : 128 Packet * 1024 Byte

#####  UFS

- 验证目标

 验证UFS IP本身的性能以及在具体业务场景下的性能

- 验证环境

- Signoff标准

  - Read: 1750MB/s * 90%（efficiency）

  - Write only: 800MB/s * 90%（efficiency）

  - RD & WR in Parallel :
    - RD: 1750MB/s * 90%（efficiency）
    - WR: 800MB/s * 90%（efficiency）

- case建议

  - xp5_ufs_3_1_read_write_test（include linkup, read&write, change low speed mode to high speed mode）

  - xp5_ufs_3_1_read_write_4kx32_random_ddr_test(32 queue 4k random read&write)

  - xp5_ufs_3_1_read_write_4kx32_random_coherent_test(32 queue 4k random read&write through cmn coherent)

  - xp5_ufs_3_1_read_write_512kx32_ddr_test(32 queue 512k random read&write for performance)

  - xp5_ufs_3_1_read_write_512kx32_coherent_test(32 queue 512k random read&write through cmn coherent for performance)

####  IP/Subsys P2P Access based on SoC NOC

#####  **验证目的**

P2P测试是check个Master/IP 点对点访问Sram/ocm等片上内存以及访问DDR的带宽和静态延时。它的主要目的是：

- 测试NOC本身有没有性能Loss。可以用VIP来代替Subsys/IP, 但是VIP发出的Trans需要Follow Subsys/IP本身的规格；

- 在NOC性能无损（或者几乎无损）的情况下，当前IP的trans能力能否匹配他的带宽需求；

- 测试各处理器通过访问片上内存进行核间通信的Latency；

#####  **验证环境**

如下图所示，P2P验证环境需要覆盖IOC Path & Non-IOC Path。 APU Core用CHI-VIP，其他Master用标准的AXI VIP。DDR model可以分2个阶段，第一阶段使用VIP替代（在VIP内设置合理的延时，该延时只包括DDR本身的延时）。第二阶段使用真实的DDR Model，片上内存采用真实的sram。

- case构造

  - 各master VIP按照设计提供的子系统/IP的典型transaction能力构造访存pattern；

  - 实际transaction能力涉及OT number/BurstLen/BurstSize/AXID/AXCACE/Atomic/....；
  - Transaction pattern: read only/write only/read & write in parallel；
  - Address pattern：no overlap、linear pattern；
  - Slave VIP默认不配置out of order，避免乱序因素干扰性能分析。使用真实的IP测试乱序下的延时和带宽；
  - 如果系统中一致性和非一致性path，则2种path都需要cover；
  - 对支持exclusive 访问的Master构造exclusive访问，测其访问片上内存的Latency；

#####  **Signoff标准**

- 输出验证数据统计。统计各master的总线带宽利用率，最大、最小&平均Latency，最大和平均OT number；
- 验证结果跟理论BW结果做对比，如果有Loss，分析Loss原因是否在预期内，是否合理,如未达到，需要给出合理的分析；

| Port Type    | Port op | Port name | handshake num | bandwidth% | ost_num | max_busrt_len | avg latency | Signoff标准      | Comment                              |
| ------------ | ------- | --------- | ------------- | ---------- | ------- | ------------- | ----------- | ---------------- | ------------------------------------ |
| Non-Coh Path |         |           |               |            |         |               |             |                  |                                      |
| mst/slv      | rd/wr   | 待测      | 待测          | 待测       | 待测    | 待测          | 待测        | bandwidth% > 95% | if not meet, Please find the reason  |
| mst/slv      | rd/wr   | 待测      | 待测          | 待测       | 待测    | 待测          | 待测        | bandwidth% > 95% | if not meet,Please find the reason   |
| ...          | ...     | ...       | ...           | ...        | ...     | ...           | ...         | ...              | ...                                  |
| ...          | ...     | ...       | ...           | ...        | ...     | ...           | ...         | ...              | ...                                  |
| ...          | ...     | ...       | ...           | ...        | ...     | ...           | ...         | ...              | ...                                  |
| ...          | ...     | ...       | ...           | ...        | ...     | ...           | ...         | ...              | ...                                  |
| Coh Path     |         |           |               |            |         |               |             |                  |                                      |
| mst/slv      | rd/wr   | 待测      | 待测          | 待测       | 待测    | 待测          | 待测        | bandwidth% > 95% | if not meet,, Please find the reason |
| mst/slv      | rd/wr   | 待测      | 待测          | 待测       | 待测    | 待测          | 待测        | bandwidth% > 95% | if not meet,, Please find the reason |
| ...          | ...     | ...       | ...           | ...        | ...     | ...           | ...         | ...              | ...                                  |
| ...          | ...     | ...       | ...           | ...        | ...     | ...           | ...         | ...              | ...                                  |
| ...          | ...     | ...       | ...           | ...        | ...     | ...           | ...         | ...              | ...                                  |
| ...          | ...     | ...       | ...           | ...        | ...     | ...           | ...         | ...              | ...                                  |

 *Note: 如果带宽利用率不满足signoff条件，请在上表中最右侧的comment栏里添加原因，比如是IP自身问题或者是NOC导致的性能Loss!*

#### IP/Subsys Access in Parallel based on SoC NOC

##### **验证目的**

验证一些子系统内多master并行访问片上RAM（SYS_RAM/OCM）以及DDR时是否有性能Loss，如果有性能Loss，是否可以接受(Loss后依然满足性能需求)，具体性能指标为：

- Single master slave test;

- Single master bw test;

- All master OT capability test;

- Bitwidth-convertion rate backpressure test（位宽转换引起的性能Loss）;

- Multi-master bw & latency test;

- High Priority QoS test;

##### 验证环境

在MSS子系统中有多个Master并行访问片内OCM的场景，并且对带宽需求相对较高，需要重点验证。

OCM在MSS中的位置以及OCM内部的Topology如下图所示：

v341，v4DR，RPU，xDMA，From_SoC & From VSS都用VIP替代;由VIP发激励并行访问OCM。

##### case构造

- 每个master（VIP）单独发起read only, write only, rd & wr， check其BW & Latency；

- 4个v341.idma+2个v4dr.idma并发， check 各master的BW & Latency；

- 4个v341.idma+4个v341.idma.comb+ 2个v4dr.idma并发， check 各master的BW & Latency；

- 4个v341.idma+4个v341.idma.comb+ 2个v4dr.idma并发+2个v4dr.comb并发， check 各master的BW & Latency；

- 4个v341.idma+4个v341.idma.comb+ 2个v4dr.idma并发+2个， check 各master的BW & Latencyv4dr.comb+RPU.mm+xDMA0/1/2+From_SoC+From_VSS并发 with the same QoS/not the Same QoS， check 各master的BW & Latency；

- 设置RPU的QOS高于其他master，在并发场景下check RPU访问OCM的Latency，并在波形中确认仲裁情况；

**以上4个case中的各vip需按照IP规格拉满OT,cover访问OCM的round-trip Latency。**

##### **signoff标准**

| Port Name                                                 | Port Op | handshake num | bandwidth% | ost_num | max_busrt_len | avg latency | Signoff标准（BW） | Signoff标准（Latency）         |
| --------------------------------------------------------- | ------- | ------------- | ---------- | ------- | ------------- | ----------- | ----------------- | ------------------------------ |
| 各master P2P Access                                       |         |               |            |         |               |             |                   |                                |
| v341_0.idma                                               | rd/wr   | 待测          | 待测       | 待测    | 待测          | 待测        | bandwidth% > 95%  | <60 cycles                     |
| v341_1.idma                                               | rd/wr   | 待测          | 待测       | 待测    | 待测          | 待测        | bandwidth% > 95%  | <60 cycles                     |
| v341_2.idma                                               | rd/wr   | 待测          | 待测       | 待测    | 待测          | 待测        | bandwidth% > 95%  | <60 cycles                     |
| v341_3.idma                                               | rd/wr   | 待测          | 待测       | 待测    | 待测          | 待测        | bandwidth% > 95%  | <60 cycles                     |
| v4dr_0.idma                                               | rd/wr   | 待测          | 待测       | 待测    | 待测          | 待测        | bandwidth% > 95%  | <60 cycles                     |
| v4dr_1.idma                                               | rd/wr   | 待测          | 待测       | 待测    | 待测          | 待测        | bandwidth% > 95%  | <60 cycles                     |
| RPU.mm                                                    | rd/wr   | 待测          | 待测       | 待测    | 待测          | 待测        | bandwidth% > 50%  | <60 cycles                     |
| xDMA0/1/2                                                 | rd/wr   | 待测          | 待测       | 待测    | 待测          | 待测        | bandwidth% > 95%  | <60 cycles                     |
| From SoC                                                  | rd/wr   | 待测          | 待测       | 待测    | 待测          | 待测        | bandwidth% > 95%  | <60 cycles                     |
| From VSS(VI&RPU)                                          | rd/wr   | 待测          | 待测       | 待测    | 待测          | 待测        | bandwidth% > 95%  | <60 cycles                     |
| all master Access in Paralell based on each paralell case |         |               |            |         |               |             |                   |                                |
| v341_0.idma                                               | rd/wr   | 待测          | 待测       | 待测    | 待测          | 待测        | bandwidth% > 85%  | <80 cycles(可适当调整,+-10%)   |
| v341_1.idma                                               | rd/wr   | 待测          | 待测       | 待测    | 待测          | 待测        | bandwidth% > 85%  | <80 cycles(可适当调整,+-10%)   |
| v341_2.idma                                               | rd/wr   | 待测          | 待测       | 待测    | 待测          | 待测        | bandwidth% > 85%  | <80 cycles(可适当调整,+-10%)   |
| v341_3.idma                                               | rd/wr   | 待测          | 待测       | 待测    | 待测          | 待测        | bandwidth% > 85%  | <80 cycles(可适当调整,+-10%)   |
| v4dr_0.idma                                               | rd/wr   | 待测          | 待测       | 待测    | 待测          | 待测        | bandwidth% > 85%  | <80 cycles(可适当调整,+-10%)   |
| v4dr_1.idma                                               | rd/wr   | 待测          | 待测       | 待测    | 待测          | 待测        | bandwidth% > 85%  | <80 cycles(可适当调整,+-10%)   |
| RPU.mm                                                    | rd/wr   | 待测          | 待测       | 待测    | 待测          | 待测        | bandwidth% > 50%  | <60 cycles                     |
| xDMA0/1/2                                                 | rd/wr   | 待测          | 待测       | 待测    | 待测          | 待测        | bandwidth% > 85%  | <100 cycles(可适当调整，+-10%) |
| From SoC                                                  | rd/wr   | 待测          | 待测       | 待测    | 待测          | 待测        | bandwidth% > 85%  | <100 cycles(可适当调整，+-10%) |
| From VSS(VI&RPU)                                          | rd/wr   | 待测          | 待测       | 待测    | 待测          | 待测        | bandwidth% > 85%  | <100 cycles(可适当调整，+-10%) |

### Real Time Path 通路验证

#### 验证环境

如下图所示，NOC上游的master都可以由VIP来提供，DDR也可以用VIP替代。

#### **验证目的**

*为了能够快速验证Realtime Path在**并行**场景下争抢带宽的能力，‘SOC采取的策略是给VI提供了一条访问**DDR**的专线，直接routing到mnoc的Target NIU出口的arbiter上，即只在最后以及arbiter参与跟其他**master**的仲裁。*

*快速验证VI与其他**master**在总线上的竞争情况。*

#### case构造

为了快速验证realtime的通路，case构造可以分2个阶段；

- **阶段1：**

  VI的VIP和NGC的24个VIP同时发traffic, 使其在MNOC的Target NIU侧造车充分的竞争。

- **pattern 构造:**

  |           | ID     | Data Wdith | Max OT | BLEN | Addr Type | Peak BW Req(GB/s) | Comment |
  | --------- | ------ | ---------- | ------ | ---- | --------- | ----------------- | ------- |
  |           | Write  |            |        |      |           |                   |         |
  | VI        | 相同ID | 32B        | 30     | 4    | Linear    | 21.5              |         |
  | NGC.VIP0  | 不同ID | 32B        | 20     | 4    | Linear    | 12                |         |
  | NGC.VIP1  | 不同ID | 32B        |        |      |           | 12                |         |
  | ...       | ...    | ...        | ...    | ...  | ...       | ...               |         |
  | ...       | ...    | ...        | ...    | ...  | ...       | ...               |         |
  | NGC.VIP23 | 不同ID | 32B        | 20     | 4    | Linear    | 12                |         |
  |           | Read   |            |        |      |           |                   |         |
  | VI        | NA     | NA         | NA     | NA   | NA        | NA                |         |
  | NGC.VIP0  | 不同ID | 32B        | 20     | 4    | Linear    | 12                |         |
  | NGC.VIP1  | 不同ID | 32B        |        |      |           | 12                |         |
  | ...       | ...    | ...        | ...    | ...  | ...       | ...               |         |
  | ...       | ...    | ...        | ...    | ...  | ...       | ...               |         |
  | NGC.VIP23 | 不同ID | 32B        | 20     | 4    | Linear    | 12                |         |

- **阶段2：**

  让更多的master（比如媒体子系统中的其他IP，HSP子系统中的其他IP以及APSS等，也可以用VIP替代）参与到竞争。

- **pattern 构造:**

  |                    | ID     | Data Wdith | Max OT | BLEN | Addr Type | Peak BW Req(GB/s) | QoS  |
  | ------------------ | ------ | ---------- | ------ | ---- | --------- | ----------------- | ---- |
  |                    | Write  |            |        |      |           |                   |      |
  | VI                 | 相同ID | 32B        | 30     | 4    | Linear    | 21.5              | 6    |
  | NGC.VIP0           | 不同ID | 32B        | 20     | 4    | Linear    | 12                | 0    |
  | NGC.VIP1           | 不同ID | 32B        |        |      |           | 12                | 0    |
  | ...                | ...    | ...        | ...    | ...  | ...       | ...               |      |
  | ...                | ...    | ...        | ...    | ...  | ...       | ...               |      |
  | NGC.VIP23          | 不同ID | 32B        | 20     | 4    | Linear    | 12                | 0    |
  | HSP.PCIEx4(VIP0~1) | 相同ID | 16B        | 28     | 4    | Linear    | 6.14              | 0    |
  | HSP.ETH(VIP0~3)    | 相同ID | 16B        | 10     | 4    | Linear    | 2.2               | 0    |
  | HSP.USB(VIP)       | 相同ID | 16B        | 4      | 4    | Linear    | 0.87              | 0    |
  | HSP.UFS(VIP)       | 相同ID | 16B        | 4      | 4    | Linear    | 0.87              | 0    |
  | CVISP(VIP)         | 相同ID | 64B        | 16     | 4    | Linear    | 22.1              | 0    |
  | HVISP.wr0(VIP)     | 相同ID | 16B        | 16     | 4    | Linear    | 4.9               | 0    |
  | GDC                | 相同ID |            |        |      |           |                   | 0    |
  | SBSX0              | 不同ID | 32B        | 4      | 4    | Linear    | 4.6               | 1    |
  | SBSX1              | 不同ID | 32B        | 4      | 4    | Linear    | 4.6               | 1    |
  | SBSX2              | 不同ID | 32B        | 4      | 4    | Linear    | 4.6               | 1    |
  | SBSX3              | 不同ID | 32B        | 4      | 4    | Linear    | 4.6               | 1    |
  | SBSX4              | 不同ID | 32B        | 4      | 4    | Linear    | 4.6               | 1    |
  | SBSX5              | 不同ID | 32B        | 4      | 4    | Linear    | 4.6               | 1    |
  | SBSX6              | 不同ID | 32B        | 4      | 4    | Linear    | 4.6               | 1    |
  | SBSX7              | 不同ID | 32B        | 4      | 4    | Linear    | 4.6               | 1    |
  |                    | Read   |            |        |      |           |                   |      |
  | VI                 | NA     | NA         | NA     | NA   | NA        | NA                | NA   |
  | NGC.VIP0           | 不同ID | 32B        | 20     | 4    | Linear    | 12                | 0    |
  | NGC.VIP1           | 不同ID | 32B        |        |      |           | 12                | 0    |
  | ...                | ...    | ...        | ...    | ...  | ...       | ...               |      |
  | ...                | ...    | ...        | ...    | ...  | ...       | ...               |      |
  | NGC.VIP23          | 不同ID | 32B        | 20     | 4    | Linear    | 12                | 0    |
  | HSP.PCIEx4(VIP0~1) | 相同ID | 16B        | 28     | 8    | Linear    | 12.3              | 0    |
  | HSP.ETH(VIP0~3)    | 相同ID | 16B        | 10     | 4    | Linear    | 2.2               | 0    |
  | HSP.USB(VIP)       | 相同ID | 16B        | 4      | 4    | Linear    | 0.87              | 0    |
  | HSP.UFS(VIP)       | 相同ID | 16B        | 4      | 4    | Linear    | 0.87              | 0    |
  | CVISP(VIP)         | 相同ID | 16B        | 32     | 4    | Linear    | 11                | 0    |
  | HVISP.rd0(VIP)     | 相同ID | 16B        | 24     | 4    | Linear    | 7.3               | 0    |
  | GDC                | 相同ID |            |        |      |           |                   | 0    |
  | SBSX0              | 不同ID | 32B        | 4      | 4    | Linear    | 4.6               | 1    |
  | SBSX1              | 不同ID | 32B        | 4      | 4    | Linear    | 4.6               | 1    |
  | SBSX2              | 不同ID | 32B        | 4      | 4    | Linear    | 4.6               | 1    |
  | SBSX3              | 不同ID | 32B        | 4      | 4    | Linear    | 4.6               | 1    |
  | SBSX4              | 不同ID | 32B        | 4      | 4    | Linear    | 4.6               | 1    |
  | SBSX5              | 不同ID | 32B        | 4      | 4    | Linear    | 4.6               | 1    |
  | SBSX6              | 不同ID | 32B        | 4      | 4    | Linear    | 4.6               | 1    |
  | SBSX7              | 不同ID | 32B        | 4      | 4    | Linear    | 4.6               | 1    |

#### Signoff标准

- **VI BW check**
  - *check VI是否能获得持续的获得预期的21.5GB/s带宽;*
- **VI Latency check**
  - 确保单笔VI Trans的Latency < 250 cycles;
  - 确保VI Trans的AVG Latency < 200 cycles;
- **Arbiter check**
  - **波形上Check VI跟NGC.VIP的在NOC内部的仲裁情况，需要同时满足以下3个条件**：
    - *如果当VI的traffic到达TNIU时，有其他trans已经获得仲裁正常传输了，那么确保该trans结束后，下一个仲裁权一定能被VI获得*
    - *如果当前VI的traffic到达TNIU时没有其他已经在传输的trans，但是刚好碰到同时到达的来自另一个**master**的traffic，则确保此次仲裁一定能被VI获得*
    - *如果当前正常仲裁的是VI，并且有下一个Pending的来自VI的trans，则确保当前trans结束后，下一个获得仲裁的依然是来自VI的trans*

## 全系统典型场景下的性能验证

### 典型场景描述

SOC典型场景下，Vision，DSP, NGC, APU以及HSP都会并行协同工作。这里以最大workload的场景下分析Throughput, bandwidth & Latency等相关性能指标。下图表示在行车过程中个IP并行工作时的workload叠加在一起，各IP在实际工作中可能并非如图中所示那样完全叠加在一起，但是完全叠加是最worst case。

### **典型场景下的workload分配**

### **典型场景下的整体带宽需求**

| Subsys        | IP             | Peak Rd  BW Req(GB/s) | Peak Wr  BW Req(GB/s) | AVG Rd  BW Req(GB/s) | AVG Wr  BW Req(GB/s) | AVG Wr+Rd(GB/s) | Clock Freq(GHz) |
| ------------- | -------------- | --------------------- | --------------------- | -------------------- | -------------------- | --------------- | --------------- |
| VSS           | VI             | 0.00                  | 18.13                 | 0.00                 | 12.91                |                 | 1.20            |
| CV-ISP        | 10.40          | 21.06                 | 9.01                  | 20.70                |                      | 1.35            |                 |
| Raw Encoder   | 0.00           | 3.78                  | 0.00                  | 2.65                 |                      | 1.35            |                 |
| GPU           | 2.20           | 2.20                  | 2.20                  | 2.20                 |                      | 1.00            |                 |
| Human-ISP     | 7.30           | 4.41                  | 6.76                  | 4.24                 |                      | 1.00            |                 |
| GDC           | 8.31           | 5.15                  | 3.41                  | 2.22                 |                      | 1.20            |                 |
| DPU           | 8.16           | 0.00                  | 4.06                  | 0.00                 |                      | 0.60            |                 |
| VDMA          | 1.00           | 1.00                  | 0.50                  | 0.50                 |                      | 0.60            |                 |
| Total         | 37.37          | 55.73                 | 25.94                 | 45.42                | 71.36                |                 |                 |
| MSS           | Video-Encoder  | 11.69                 | 2.20                  | 9.12                 | 1.84                 |                 | 1.00            |
| Video-Decoder | 2.20           | 11.70                 | 1.89                  | 4.43                 |                      | 1.00            |                 |
| Raw-Decoder   | 4.76           | 6.80                  | 2.92                  | 4.17                 |                      | 0.80            |                 |
| cvDSP         | 24.00          | 16.00                 | 12.00                 | 8.00                 |                      | 1.20            |                 |
| XDMA          | 3              | 3                     | 2                     | 2                    |                      | 0.60            |                 |
| RPU(4 cores)  | 1              | 1                     | 1                     | 1                    |                      | 1.20            |                 |
| Total         | 46.65          | 40.70                 | 28.93                 | 21.44                | 50.37                |                 |                 |
| HSP           | PCIE(2*PCIEx4) | 32                    | 32                    | 9.33                 | 5                    |                 | 1               |
| ETH*4         | 5              | 5                     | 1.5                   | 1.5                  |                      | 0.4             |                 |
| UFS           | 2.32           | 2.32                  | 0.5                   | 0.5                  |                      | 0.3             |                 |
| USB           | 1.25           | 1.25                  | 0.5                   | 0.5                  |                      | 0.3             |                 |
| Total         | 40.57          | 40.57                 | 11.83                 | 7.5                  | 19.33                |                 |                 |
| NGC           | L2C_0          | 76.8                  | 76.8                  | 21                   | 12                   |                 | 1.3             |
| L2C_1         | 76.8           | 76.8                  | 21                    | 12                   |                      | 1.3             |                 |
| L2C_2         | 76.8           | 76.8                  | 21                    | 12                   |                      | 1.3             |                 |
| L2C_3         | 76.8           | 76.8                  | 21                    | 12                   |                      | 1.3             |                 |
| L2C_4         | 76.8           | 76.8                  | 21                    | 12                   |                      | 1.3             |                 |
| L2C_5         | 76.8           | 76.8                  | 21                    | 12                   |                      | 1.3             |                 |
| L2C_6         | 76.8           | 76.8                  | 21                    | 12                   |                      | 1.3             |                 |
| L2C_7         | 76.8           | 76.8                  | 21                    | 12                   |                      | 1.3             |                 |
| L2C_8         | 76.8           | 76.8                  | 21                    | 12                   |                      | 1.3             |                 |
| L2C_9         | 76.8           | 76.8                  | 21                    | 12                   |                      | 1.3             |                 |
| L2C_10        | 76.8           | 76.8                  | 21                    | 12                   |                      | 1.3             |                 |
| L2C_11        | 76.8           | 76.8                  | 21                    | 12                   |                      | 1.3             |                 |
| L2C_12        | 76.8           | 76.8                  | 21                    | 12                   |                      | 1.3             |                 |
| L2C_13        | 76.8           | 76.8                  | 21                    | 12                   |                      | 1.3             |                 |
| L2C_14        | 76.8           | 76.8                  | 21                    | 12                   |                      | 1.3             |                 |
| L2C_15        | 76.8           | 76.8                  | 21                    | 12                   |                      | 1.3             |                 |
| Total         | 614.4          | 614.4                 | 336                   | 192                  | 528.00               |                 |                 |
| APSS          | SBSX_0         | 64                    | 64                    | 1.5                  | 1.5                  |                 | 2               |
| SBSX_1        | 64             | 64                    | 1.5                   | 1.5                  |                      | 2               |                 |
| SBSX_2        | 64             | 64                    | 1.5                   | 1.5                  |                      | 2               |                 |
| SBSX_3        | 64             | 64                    | 1.5                   | 1.5                  |                      | 2               |                 |
| SBSX_4        | 64             | 64                    | 1.5                   | 1.5                  |                      | 2               |                 |
| SBSX_5        | 64             | 64                    | 1.5                   | 1.5                  |                      | 2               |                 |
| SBSX_6        | 64             | 64                    | 1.5                   | 1.5                  |                      | 2               |                 |
| SBSX_7        | 64             | 64                    | 1.5                   | 1.5                  |                      | 2               |                 |
| Total         | NA             | NA                    | 12                    | 12                   | 24.00                |                 |                 |
| SoC           | Total          |                       |                       | 414.7                | 278.36               | 693.06          |                 |

*这里NGC的带宽**528GB/s**并非正向计算分析得出，而是根据**65%**的DDR带宽利用率，扣除其他IP/Subsystem的带宽需求后，剩余的带宽认为可以分给NGC。实际上在LLM的decoder阶段，永远都是带宽Bond，也就是说这个阶段有多少带宽，NGC就能吃掉多少带宽。*

**典型场景下SoC整体带宽分布百分比如下图所示：**

#### 典型场景Case构造

##### EDA平台

EDA平台下，目的是为了测试全系统的访存能力，而并非IP本身的性能（IP本身的性能在子系统/IP的性能验证中已经Cover），所以分几个阶段验证：

- 验证环境

  - **阶段1**
    - 所有的Master都用VIP，设定VIP的各种访存能力参数，如OT，BLEN等；
    - 初始打平所有Master的QoS，后续根据实测带宽分配获取情况全局调整各Master的QoS值；
    - 采用DDR VIP，设定合理Latency（70cycles@1.2GHz）；
  - **阶段2**
    - 加速器可以用replay的方式灌入激励，其余可编程器件继续采用VIP；
    - 初始打平所有Master的Qos，后续根据实测带宽分配获取情况全局调整各Master的QoS值；
    - 采用DDR VIP，设定合理Latency（70cycles@1.2GHz）；
  
  - **阶段3**
  
    - 加速器可以用replay的方式灌入激励，其余可编程器件继续采用VIP。
  
    - 初始打平所有Master的Qos，后续根据实测带宽分配获取情况全局调整各Master的QoS值
  
    - 采用真实DDR Model
  
- **case构造**
  DV case需要跑并发case来check各master获得的带宽情况以及平均动态Latency。具体case中各VIP发出的Transaction行为约束见下表：

  |                  | ID     | Data Wdith | Max OT | BLEN | Addr Type | QoS  | AVG BW(GB/s) | 理论预期带宽(GB/s)                                           | Signoff Criteria                                     |
  | ---------------- | ------ | ---------- | ------ | ---- | --------- | ---- | ------------ | ------------------------------------------------------------ | ---------------------------------------------------- |
  | Write            |        |            |        |      |           |      |              |                                                              |                                                      |
  | VI(VIP)          | 相同ID | 32B        | 17     | 4    | Linear    | 6    | 13.06        | 12.91                                                        | >= 理论预期带宽*100%                                 |
  | NGC.VIP0         | 不同ID | 32B        | 20     | 4    | Linear    | 0    | 12.29        | 12                                                           | >= 理论预期带宽*95%                                  |
  | NGC.VIP1         | 不同ID | 32B        | 20     | 4    | Linear    | 0    | 12.29        | 12                                                           | >= 理论预期带宽*95%                                  |
  | ...              | ...    | ...        | ...    | ...  | ...       |      | ...          | ...                                                          | ...                                                  |
  | ...              | ...    | ...        | ...    | ...  | ...       |      | ...          | ...                                                          | ...                                                  |
  | NGC.VIP23        | 不同ID | 32B        | 20     | 4    | Linear    | 0    | 12.29        | 12                                                           | >= 理论预期带宽*95%                                  |
  | HSP.PCIE0x4(VIP) | 相同ID | 16B        | 12     | 4    | Linear    | 0    | 2.63         | 2.5                                                          | >= 理论预期带宽*95%                                  |
  | HSP.PCIE1x4(VIP) | 相同ID | 16B        | 12     | 4    | Linear    | 0    | 2.63         | 2.5                                                          | >= 理论预期带宽*95%                                  |
  | HSP.ETH0(VIP)    | 相同ID | 16B        | 3      | 4    | Linear    | 0    | 0.58         | 0.4                                                          | >= 理论预期带宽*95%                                  |
  | HSP.ETH1(VIP)    | 相同ID | 16B        | 3      | 4    | Linear    | 0    | 0.58         | 0.4                                                          | >= 理论预期带宽*95%                                  |
  | HSP.ETH2(VIP)    | 相同ID | 16B        | 3      | 4    | Linear    | 0    | 0.58         | 0.4                                                          | >= 理论预期带宽*95%                                  |
  | HSP.ETH3(VIP)    | 相同ID | 16B        | 3      | 4    | Linear    | 0    | 0.58         | 0.4                                                          | >= 理论预期带宽*95%                                  |
  | HSP.USB(VIP)     | 相同ID | 16B        | 3      | 4    | Linear    | 0    | 0.66         | 0.5                                                          | >= 理论预期带宽*95%                                  |
  | HSP.UFS(VIP)     | 相同ID | 16B        | 3      | 4    | Linear    | 0    | 0.66         | 0.5                                                          | >= 理论预期带宽*95%                                  |
  | CVISP(VIP)       | 相同ID | 64B        | 18     | 4    | Linear    | 0    | 24.88        | 23.35                                                        | >= 理论预期带宽*95%                                  |
  | HVISP.core0.wo0  | 相同ID | 16B        | 16     | 4    | Linear    | 0    | 4.92         | 4.41                                                         | >= 理论预期带宽*95%                                  |
  | HVISP.core0.wo1  | -      | -          | -      | -    | -         | -    | -            | HVISP的带宽获取能力统一由一个口子来模拟                      | -                                                    |
  | HVISP.core0.rw0  | -      | -          | -      | -    | -         | -    | -            | -                                                            |                                                      |
  | HVISP.core0.rw1  | -      | -          | -      | -    | -         | -    | -            | -                                                            |                                                      |
  | HVISP.core1.wo0  | -      | -          | -      | -    | -         | -    | -            | -                                                            |                                                      |
  | HVISP.core1.wo1  | -      | -          | -      | -    | -         | -    | -            | -                                                            |                                                      |
  | HVISP.core1.rw0  | -      | -          | -      | -    | -         | -    | -            | -                                                            |                                                      |
  | HVISP.core1.rw1  | -      | -          | -      | -    | -         | -    | -            | -                                                            |                                                      |
  | HVISP.core2.wo0  | -      | -          | -      | -    | -         | -    | -            | -                                                            |                                                      |
  | HVISP.core2.wo1  | -      | -          | -      | -    | -         | -    | -            | -                                                            |                                                      |
  | HVISP.core2.rw0  | -      | -          | -      | -    | -         | -    | -            | -                                                            |                                                      |
  | HVISP.core2.rw1  | -      | -          | -      | -    | -         | -    | -            | -                                                            |                                                      |
  | GDC              | 相同ID | 64B        | 2      | 4    | Linear    | 0    | 2.46         | 2.22                                                         | >= 理论预期带宽*95%                                  |
  | RAW_Encoder      | -      | -          | -      | -    | -         | -    | -            | 写带宽合入到CVISP中模拟体现                                  | -                                                    |
  | DPU.rw0          | -      | -          | -      | -    | -         | -    | -            | DPU No Need to Write                                         | -                                                    |
  | DPU.rw1          | -      | -          | -      | -    | -         | -    | -            | -                                                            |                                                      |
  | DPU.rw2          | -      | -          | -      | -    | -         | -    | -            | -                                                            |                                                      |
  | DPU.rw3          | -      | -          | -      | -    | -         | -    | -            | -                                                            |                                                      |
  | V-Encoder        | 相同ID | 16B        | 16     | 4    | Linear    | 0    | 2.34         | 1.84                                                         | >= 理论预期带宽*95%                                  |
  | V-Decoder        | 相同ID | 16B        | 28     | 4    | Linear    | 0    | 5.12         | 4.43                                                         | >= 理论预期带宽*95%                                  |
  | R-Decoder        | 相同ID | 64B        | 8      | 4    | Linear    | 0    | 4.61         | 4.17                                                         | >= 理论预期带宽*95%                                  |
  | v341DSP0.dma     | 相同ID | 32B        | 6      | 4    | Linear    | 0    | 2.63         | 2                                                            | >= 理论预期带宽*95%                                  |
  | v341DSP1.dma     | 相同ID | 32B        | 6      | 4    | Linear    | 0    | 2.63         | 2                                                            | >= 理论预期带宽*95%                                  |
  | v341DSP2.dma     | 相同ID | 32B        | 6      | 4    | Linear    | 0    | 2.63         | 2                                                            | >= 理论预期带宽*95%                                  |
  | v341DSP3.dma     | 相同ID | 32B        | 6      | 4    | Linear    | 0    | 2.63         | 2                                                            | >= 理论预期带宽*95%                                  |
  | v341DSP0.comb    | 相同ID | 16B        | 2      | 4    | Linear    | 5    | 0.7          | 无明确带宽需求，可定位0.5 Per port, 重点check 单笔访问的Latency; | BW>0.5GB/s, Dynamic Latency <230 cycles              |
  | v341DSP1.comb    | 相同ID | 16B        | 2      | 4    | Linear    | 5    | 0.7          | BW>0.5GB/s, Dynamic Latency <230 cycles                      |                                                      |
  | v341DSP2.comb    | 相同ID | 16B        | 2      | 4    | Linear    | 5    | 0.7          | BW>0.5GB/s, Dynamic Latency <230 cycles                      |                                                      |
  | v341DSP3.comb    | 相同ID | 16B        | 2      | 4    | Linear    | 5    | 0.7          | BW>0.5GB/s, Dynamic Latency <230 cycles                      |                                                      |
  | v4dr0.dma        | 相同ID | 32B        | 2      | 4    | Linear    | 0    | 0.88         | 0.8                                                          | >= 理论预期带宽*95%                                  |
  | v4dr1.dma        | 相同ID | 32B        | 2      | 4    | Linear    | 0    | 0.88         | 0.8                                                          | >= 理论预期带宽*95%                                  |
  | v4dr0.comb       | 相同ID | 16B        | 2      | 4    | Linear    | 5    | 0.88         | 无明确带宽需求，可定位0.5 Per port, 重点check 单笔访问的Latency; | BW>0.5GB/s, Dynamic Latency <230 cycles              |
  | v4dr1.comb       | 相同ID | 16B        | 2      | 4    | Linear    | 5    | 0.88         | BW>0.5GB/s, Dynamic Latency <230 cycles                      |                                                      |
  | MSS.xDMA0        | 相同ID | 16B        | 22     | 4    | Linear    | 0    | 2.19         | 2                                                            | >= 理论预期带宽*95%                                  |
  | MSS.xDMA1        | -      | -          | -      | -    | -         | -    | -            | xDMA带宽获取能力统一由xDMA0来承担模拟                        | -                                                    |
  | MSS.xDMA2        | -      | -          | -      | -    | -         | -    | -            | -                                                            |                                                      |
  | RPU              | 相同ID | 32B        | 4      | 4    | Linear    | 5    | 1.76         | 1                                                            | >= 理论预期带宽*95%                                  |
  | SBSX0            | 不同ID | 32B        | 3      | 4    | Linear    | 1    | 2.3          | 1.5                                                          | >= 理论预期带宽*95%                                  |
  | SBSX1            | 不同ID | 32B        | 3      | 4    | Linear    | 1    | 2.3          | 1.5                                                          | >= 理论预期带宽*95%                                  |
  | SBSX2            | 不同ID | 32B        | 3      | 4    | Linear    | 1    | 2.3          | 1.5                                                          | >= 理论预期带宽*95%                                  |
  | SBSX3            | 不同ID | 32B        | 3      | 4    | Linear    | 1    | 2.3          | 1.5                                                          | >= 理论预期带宽*95%                                  |
  | SBSX4            | 不同ID | 32B        | 3      | 4    | Linear    | 1    | 2.3          | 1.5                                                          | >= 理论预期带宽*95%                                  |
  | SBSX5            | 不同ID | 32B        | 3      | 4    | Linear    | 1    | 2.3          | 1.5                                                          | >= 理论预期带宽*95%                                  |
  | SBSX6            | 不同ID | 32B        | 3      | 4    | Linear    | 1    | 2.3          | 1.5                                                          | >= 理论预期带宽*95%                                  |
  | SBSX7            | 不同ID | 32B        | 3      | 4    | Linear    | 1    | 2.3          | 1.5                                                          | >= 理论预期带宽*95%                                  |
  | Read             |        |            |        |      |           |      |              |                                                              |                                                      |
  | VI               | NA     | NA         | NA     | NA   | NA        | NA   | NA           | NA                                                           | -                                                    |
  | NGC.VIP0         | 不同ID | 32B        | 20     | 4    | Linear    | 0    | 12.29        | 12                                                           | >= 理论预期带宽*95%                                  |
  | NGC.VIP1         | 不同ID | 32B        |        |      |           | 0    | 12.29        | 12                                                           | >= 理论预期带宽*95%                                  |
  | ...              | ...    | ...        | ...    | ...  | ...       |      | ...          | ...                                                          | ...                                                  |
  | ...              | ...    | ...        | ...    | ...  | ...       |      | ...          | ...                                                          | ...                                                  |
  | NGC.VIP23        | 不同ID | 32B        | 20     | 4    | Linear    | 0    | 12.29        | 12                                                           | >= 理论预期带宽*95%                                  |
  | HSP.PCIE0x4(VIP) | 相同ID | 16B        | 24     | 4    | Linear    | 0    | 5.27         | 4.67                                                         | >= 理论预期带宽*95%                                  |
  | HSP.PCIE1x4(VIP) | 相同ID | 16B        | 24     | 4    | Linear    | 0    | 5.27         | 4.67                                                         | >= 理论预期带宽*95%                                  |
  | HSP.ETH0(VIP)    | 相同ID | 16B        | 3      | 4    | Linear    | 0    | 0.58         | 0.4                                                          | >= 理论预期带宽*95%                                  |
  | HSP.ETH1(VIP)    | 相同ID | 16B        | 3      | 4    | Linear    | 0    | 0.58         | 0.4                                                          | >= 理论预期带宽*95%                                  |
  | HSP.ETH2(VIP)    | 相同ID | 16B        | 3      | 4    | Linear    | 0    | 0.58         | 0.4                                                          | >= 理论预期带宽*95%                                  |
  | HSP.ETH3(VIP)    | 相同ID | 16B        | 3      | 4    | Linear    | 0    | 0.58         | 0.4                                                          | >= 理论预期带宽*95%                                  |
  | HSP.USB(VIP)     | 相同ID | 16B        | 3      | 4    | Linear    | 0    | 0.66         | 0.5                                                          | >= 理论预期带宽*95%                                  |
  | HSP.UFS(VIP)     | 相同ID | 16B        | 3      | 4    | Linear    | 0    | 0.66         | 0.5                                                          | >= 理论预期带宽*95%                                  |
  | CVISP(VIP)       | 相同ID | 16B        | 32     | 4    | Linear    | 0    | 11.06        | 10.4                                                         | >= 理论预期带宽*95%                                  |
  | HVISP.core0.wo0  | -      | -          | -      | -    | -         | -    | -            | -                                                            | -                                                    |
  | HVISP.core0.wo1  | -      | -          | -      | -    | -         | -    | -            | -                                                            | -                                                    |
  | HVISP.core0.rw0  | 相同ID | 16B        | 24     | 4    | Linear    | 0    | 7.37         | 7.3                                                          | >= 理论预期带宽*95%                                  |
  | HVISP.core0.rw1  | -      | -          | -      | -    | -         | -    | -            | -                                                            | -                                                    |
  | HVISP.core1.wo0  | -      | -          | -      | -    | -         | -    | -            | -                                                            | -                                                    |
  | HVISP.core1.wo1  | -      | -          | -      | -    | -         | -    | -            | -                                                            | -                                                    |
  | HVISP.core1.rw0  | -      | -          | -      | -    | -         | -    | -            | -                                                            | -                                                    |
  | HVISP.core1.rw1  | -      | -          | -      | -    | -         | -    | -            | -                                                            | -                                                    |
  | HVISP.core2.wo0  | -      | -          | -      | -    | -         | -    | -            | -                                                            | -                                                    |
  | HVISP.core2.wo1  | -      | -          | -      | -    | -         | -    | -            | -                                                            | -                                                    |
  | HVISP.core2.rw0  | -      | -          | -      | -    | -         | -    | -            | -                                                            | -                                                    |
  | HVISP.core2.rw1  | -      | -          | -      | -    | -         | -    | -            | -                                                            | -                                                    |
  | GDC              | 相同ID | 64B        | 8      | 4    | Linear    | 6    | 9.83         | 8.31                                                         | >= 理论预期带宽*95%                                  |
  | RAW_Encoder      | -      | -          | -      | -    | -         | -    | -            | online 对接CVISP                                             | >= 理论预期带宽*95%                                  |
  | DPU.rw0          | 相同ID | 16B        | 24     | 4    | Linear    | 6    | 4.61         | 4.06                                                         | >= 理论预期带宽*100%                                 |
  | DPU.rw1          | -      | -          | -      | -    | -         | -    | -            | DPU的4个读口BW统一由0口来模拟                                | -                                                    |
  | DPU.rw2          | -      | -          | -      | -    | -         | -    | -            | -                                                            |                                                      |
  | DPU.rw3          | -      | -          | -      | -    | -         | -    | -            | -                                                            |                                                      |
  | V-Encoder        | 相同ID | 16B        | 64     | 4    | Linear    | 0    | 9.36         | 9.12                                                         | >= 理论预期带宽*95%                                  |
  | V-Decoder        | 相同ID | 16B        | 12     | 4    | Linear    | 0    | 2.19         | 1.89                                                         | >= 理论预期带宽*95%                                  |
  | R-Decoder        | 相同ID | 64B        | 6      | 4    | Linear    | 0    | 3.51         | 2.92                                                         | >= 理论预期带宽*95%                                  |
  | v341DSP0.dma     | 相同ID | 32B        | 7      | 4    | Linear    | 0    | 3.07         | 2.6                                                          | >= 理论预期带宽*95%                                  |
  | v341DSP1.dma     | 相同ID | 32B        | 7      | 4    | Linear    | 0    | 3.07         | 2.6                                                          | >= 理论预期带宽*95%                                  |
  | v341DSP2.dma     | 相同ID | 32B        | 7      | 4    | Linear    | 0    | 3.07         | 2.6                                                          | >= 理论预期带宽*95%                                  |
  | v341DSP3.dma     | 相同ID | 32B        | 7      | 4    | Linear    | 0    | 3.07         | 2.6                                                          | >= 理论预期带宽*95%                                  |
  | v341DSP0.comb    | 相同ID | 16B        | 2      | 4    | Linear    | 5    | 0.7          | 无明确带宽需求，可定位0.5 Per port, 重点check 单笔访问的Latency; | BW>0.5GB/s, Dynamic Latency <230 cycles              |
  | v341DSP1.comb    | 相同ID | 16B        | 2      | 4    | Linear    | 5    | 0.7          | BW>0.5GB/s, Dynamic Latency <230 cycles                      |                                                      |
  | v341DSP2.comb    | 相同ID | 16B        | 2      | 4    | Linear    | 5    | 0.7          | BW>0.5GB/s, Dynamic Latency <230 cycles                      |                                                      |
  | v341DSP3.comb    | 相同ID | 16B        | 2      | 4    | Linear    | 5    | 0.7          | BW>0.5GB/s, Dynamic Latency <230 cycles                      |                                                      |
  | v4dr0.dma        | 相同ID | 32B        | 2      | 4    | Linear    | 0    | 0.88         | 0.8                                                          | >= 理论预期带宽*95%                                  |
  | v4dr1.dma        | 相同ID | 32B        | 2      | 4    | Linear    | 0    | 0.88         | 0.8                                                          | >= 理论预期带宽*95%                                  |
  | v4dr0.comb       | 相同ID | 16B        | 2      | 4    | Linear    | 0    | 0.88         | 无明确带宽需求，可定位0.5 Per port, 重点check 单笔访问的Latency; | BW>0.5GB/s, Dynamic Latency <230 cycles              |
  | v4dr1.comb       | 相同ID | 16B        | 2      | 4    | Linear    | 0    | 0.88         | BW>0.5GB/s, Dynamic Latency <230 cycles                      |                                                      |
  | MSS.xDMA0        | 相同ID | 16B        | 22     | 4    | Linear    | 0    | 2.19         | 2                                                            | >= 理论预期带宽*95%                                  |
  | MSS.xDMA1        | -      | -          | -      | -    | -         | -    | -            | xDMA带宽获取能力统一由xDMA0来承担模拟                        | -                                                    |
  | MSS.xDMA2        | -      | -          | -      | -    | -         | -    | -            | -                                                            |                                                      |
  | RPU              | 相同ID | 32B        | 4      | 4    | Linear    | 5    | 1.76         | 1                                                            | 1.BW>理论预期带宽*95%  2.Dynamic Latency <220 cycles |
  | SBSX0            | 不同ID | 32B        | 3      | 4    | Linear    | 1    | 2.3          | 1.5                                                          | >= 理论预期带宽*95%                                  |
  | SBSX1            | 不同ID | 32B        | 3      | 4    | Linear    | 1    | 2.3          | 1.5                                                          | >= 理论预期带宽*95%                                  |
  | SBSX2            | 不同ID | 32B        | 3      | 4    | Linear    | 1    | 2.3          | 1.5                                                          | >= 理论预期带宽*95%                                  |
  | SBSX3            | 不同ID | 32B        | 3      | 4    | Linear    | 1    | 2.3          | 1.5                                                          | >= 理论预期带宽*95%                                  |
  | SBSX4            | 不同ID | 32B        | 3      | 4    | Linear    | 1    | 2.3          | 1.5                                                          | >= 理论预期带宽*95%                                  |
  | SBSX5            | 不同ID | 32B        | 3      | 4    | Linear    | 1    | 2.3          | 1.5                                                          | >= 理论预期带宽*95%                                  |
  | SBSX6            | 不同ID | 32B        | 3      | 4    | Linear    | 1    | 2.3          | 1.5                                                          | >= 理论预期带宽*95%                                  |
  | SBSX7            | 不同ID | 32B        | 3      | 4    | Linear    | 1    | 2.3          | 1.5                                                          | >= 理论预期带宽*95%                                  |

- signoff标准

    Signoff标准详见上面case pattern构造表格的最右边一列。

##### PLD 平台验证环境

考虑到全系统版本占用PLD资源过大，因此需要对IP的规格进行裁剪，创建合理的PLD image。需要部分占用资源比较大的子系统，采用AVIP的形式来替换提供背景带宽:

- VI(12路camera+Radar) //following PR/AR中的Camera + Radar中的规格
- CVISP(12路camera)  //following PR/AR中的Camera + Radar中的规格
- HVISP(12路camera)  //following PR/AR中的Camera + Radar中的规格
- V-Encode: 4k@324 in Parallel
- V-Decode:4k@200 in Parallel
- display:4*1080p@60fps, rgb888
- APSS: 如果做适当的裁剪（比如只放一个cluster），就引入APSS，如果都放不下，用AVIP提供背景BW
- NGC: 用AVIP提供背景BW
- HSP: 用AVIP提供背景BW

**子系统的bringup**

- 背景带宽

  NGC/APSS/HSP等背景带宽构造

- APSS bring up

  - **APU** **带宽测试pattern设计**

  - **问题调试**

- CVISP子系统的bringup

  12路 Camera典型业务能跑通 //follow PR/AR规格；

- HVISP子系统的bringup

  12路 Camera典型业务能跑通 //follow PR/AR规格

- Video codec子系统的bringup

  Follow典型场景下Video Codec编解码规格

- DSP子系统的bringup

  DSP的各种典型业务bringup

  - **VIO：**SIFT/KNN
  - **Lidar****：**Position_trans/Voxel_gen**
  - Perception:** IHB/OC/Freestitch/AVMstitch/Yolo_blend/OCR_blend/哨兵模式(周边MOD检测)
  - **Resize**
  - 4D **Radar**

- 全系统的集成bringup

|                               | Boot 流程                       | 操作内容    |
| ----------------------------- | ------------------------------- | ----------- |
| 启动PLD                       | 启动PLD                         | 执行命令... |
| Backdoor镜像                  | Backdoor kernel启动所有依赖镜像 | 执行命令... |
| Backdoor 带宽测试依赖镜像     | 执行命令...                     |             |
| 初始带宽测试配置, boot kernel | spu introm_pld                  | 执行命令... |
| apu_secloader                 | 执行命令...                     |             |
| ...                           | 执行命令...                     |             |
| ...                           | 执行命令...                     |             |
| 启动测试业务                  | 开启avip monitor                | 执行命令... |
| 开启codec业务case             | 执行命令...                     |             |
| 开启cvisp/hvisp业务case       | 执行命令...                     |             |
| 开启display业务case           | 执行命令...                     |             |
| ...                           | 执行命令...                     |             |
| ...                           | 执行命令...                     |             |

##### 测试数据获取

开启monitor后，会自动记录所有监控的master的带宽，每个master的监控数据都单独保存为一个文件

##### 波形数据分析 

- **第一帧数据(xx(ms)~yy(ms))**

  - 第一帧数据各个port的带宽分布图
    - 待测；
  - 类似于下图：
  - 第一帧数据读写总带宽的带宽分布图
    - 待测；
  - 类似于下图：
  - 第一帧数据原始的excel统计结果
    -   ......
- **第二帧数据(xx(ms)~yy(ms))**

  - 第**二**帧数据各个port的带宽分布图

    - 待测；
  - 类似于下图：
  - 第**二**帧数据读写总带宽的带宽分布图

    - 待测；
  - 类似于下图：
  - 第**二**帧数据原始的excel统计结果

    -   ......

##### PLD 平台下的signoff标准

PLD平台下的性能signoff 标准与EDA验证环境下signoff标准一致。

## 回片后的性能验证

### 子系统/IP性能

####  APSS

- 回归所有的benchmark；
- 回归在EDA/PLD平台上构造的带宽/Latency测试用例；

####  NGC

TBD

####  Vision Subsys

####  P99

 P99(99 th Percentile)性能统计方法，是一个在系统性能监控领域至关重要的概念。

 P99，即第99百分位数，是一种衡量响应时间分布的方法。它表示在所有请求中，99%的请求响应时间都低于这个值，换句话说，只有1%的请求的响应时间大于或者等于这个值；

#####  **为什么要引入P99？**

跟P99相关的概念有平均值和最大值(Max)

- 平均值:  容易误导人，平均响应时间很好，但是可能因为少数极慢的请求，导致大量用户感到卡顿；

- 最大值(Max): 最慢请求的响应时间，容易受极端异常值影响，参考价值不如P99；另外，如果出现偶尔1~2次的Max，是处于系统的tolerance之内的（即合理接接受）。

**P99重要性**

- 识别长尾问题
     - *就像自动驾驶汽车在99%的常见、标准路况下都能表现得很好，但要让系统能够处理剩下1%的罕见、极端、怪异和不可预测的“边缘情况” 类似。P99能够发现在性能验证过程中是否有类似的1%(or >1%)经常出现，如果是，就需要check是不是可以接受；*
     - 设定有意义的目标，比如99%的请求响应时间低于200ms；
     - 更真实反映用户体验；

#####   P99验证数据分析

- 数据收集，形成数据集

- 排序

- 设置告警，比如PM，P99超过500就需要告警

- 关联分析,P99涉及访存带宽，访存Latency，NOC的NIU口是否busy等，需要综合考虑分析。

#####  P99实战应用

对单个模型的监控，用PM监控模型跑的过程中Latency是否符合P99的限制

| 模型名 | PM&Noc_busy情况 | 是否PASS     | Comment |
| ------ | --------------- | ------------ | ------- |
| 模型1  | 待测图形化数据  | Pass or Fail |         |
| 模型2  | 待测图形化数据  | Pass or Fail |         |
| 模型3  | 待测图形化数据  | Pass or Fail |         |
| ...    | ...             | ...          |         |
| ...    | ...             | ...          |         |

跑模型过程中监控NOC TNIU处的idle/busy/xfer实例:

- 单个模型发布前P99的验证
- DSP算子编程需多考虑访存pattern（地址按照64B对齐，前后两笔cmd不能有地址overlap）
- NGC的Narrow Transfer 百分比控制（例如<5%）

上图是一个比较正常的模型实测数据，整个过程中memory noc TNIU几乎没有busy，偶尔有一丁点几十cycle级别的busy，都属于正常现象。

XP6可以借鉴XP5中P99对单网模型甚至是单个算子进行性能验证，确保发布后模型/算子本身没有问题（比如模型中的LD/ST指令经常有overlap，有大量的Partial Write等等）。

#### **NOC_Probe**

FlexNoC 提供了packet probe 和transaction probe 功能，分别为：

- **Packet Probe**
  - 可实时通过编程选择probe点；
  - 可实时通过编程按某种规则过滤某些包（比如长短包）；
  - 可产生trace包或者统计包；
  - 可打上时间戳；
- **Transaction Probe**
  - 统计Transacton的Latency；
  - 统计Pending Trans；
  - 产生统计信息Packet；
  - 可以打上时间戳；

在芯片回片测试中可以用NOC Probe功能统计各Master访存的性能：

**#Latency统计#**

![img](https://xiaopeng.feishu.cn/space/api/box/stream/download/asynccode/?code=MGJlNzBlNzFhMzg1YzQ1ZDM2YTcxYzhkNTlhMWZlMzlfMlJWQVVUQUtRWk1hbEtrUHZoUWswNFVvZmFTVmx4MGNfVG9rZW46VDIyVGJsWmtCb0FZcXd4dTUxemNSZ1libmdiXzE3NjAzNjAwNDA6MTc2MDM2MzY0MF9WNA)

**#Bandwidth统计#**