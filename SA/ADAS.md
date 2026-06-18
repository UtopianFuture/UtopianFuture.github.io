## ADAS

### 芯片分析

- 各厂商能做城市 NOA 芯片的对比分析（此处 XP6 为下一代 Turing 芯片标准）；

![对比](D:\gitlab\对比.png)

- 智驾芯片的门槛，以及对智驾算法的影响；

  我了解的，国内的智驾芯片（除了华为），只有算力单元，ISP 等少数 IP 是自研的，其他的如 CPU，GPU, DSP 等，都是购买 ARM,Synopsys, Cadence 等 IP 设计公司成熟的方案。所以这些大家都差不多，影响性能的大都在自研 IP 上。

  目前看到，算法的门槛在于算力单元，算力单元直接影响智驾算法的运行效率；以及系统能够达到的 DDR 带宽，PCIe/以太网的带宽，这决定了芯片内部，芯片间的数据交换能力，是系统的瓶颈。

  算力方面，小鹏/理想的算力单元都是用的自研的 IP，没有采用 NV 的 cuda 架构，这就导致上层的智驾模型需要重新部署。而自研 IP 的 INT8 算力堆的很高，达到了 710TOPS，但 FP64, FP32 等浮点算力不足，就需要用 INT8 去模拟 FP64, FP32，而这对算法的精度，性能会产生怎样的影响，有待观察。

  带宽方面，虽然大家都是采购别人家的 DDR，以及内部的总线，但是你的芯片能不能达到理论的带宽上限，是跟你的芯片设计能力有关的。比如说，由于经验问题，我们可能没有意识到 ACPU 会和 ISP 发生带宽抢占，导致 ISP 跑不到设计帧率，自然就影响智驾算法的运行。

  **图灵芯片的有效设计带宽为 123GB/s。**

  对智驾算法的影响，上面就是很典型的影响。总结一下，虽然设计算力有那么多，但是软件生态加上芯片设计问题，导致智驾算法没有跑在 NV 芯片上效率高。所以芯片团队需要回归到 GPGPU 的方向（初代由于没经验，采用稳妥方案，设计采用简单一点的 NPU 架构），大模型团队需要去优化算子，匹配底层的硬件。

- 图灵芯片的成本数据，对比使用 orinx, thor-u 能节省多少成本；

  待确认；


| spec                  | Thor-U                               | Turing           |
| --------------------- | ------------------------------------ | ---------------- |
| CPU ARCH              | 12 Poseidon-AE                       | 20 Cortex A78-AE |
| CPU Cluster Cache     | NA                                   | 8MB              |
| System Level Cache    | 16MB                                 | 8MB              |
| GPU ARCH              | Hopper-next                          | 12 Mali-G78AE    |
| DL F16/FP8/INT8(T/S)  | 350/700/700                          | 2.4/0/618        |
| GPU CUDA FP32         | 6.5                                  | 0.768            |
| ISP(Gpix/S)           | 3.5                                  | 2/1.6/2.4/9.6    |
| ASIL-D Safety Island  | 4x Lockstep R52 10K DIMPs            | 2x Lockstep R52  |
| DDR Memory BW(GB/S)   | 273                                  | 205              |
| Ethernet I/F          | 4x25GbE + 1x1GbE + 1x1GbE(FSI)       | 2x10GbE          |
| PCIe                  | Up to 14 Lanes Gen5 IDE              | 2x4 Lanes Gen4   |
| Coherent Chip to Chip | NVLINK-C2C                           | NA               |
| TDP                   | 700TOPS@90W（浮点算力远高于 Turing） | 618TOPS@95W(TBD) |

#### Turing 芯片分析

Turing 芯片使用的是 DLA 去运行所有的智驾算法，其结构如下：

算力的核心就是 NPU，其中有一个 RSU: cortex-R52 x1，负责推理网络任务的调度和执行，以及 NPC 下面各种计算资源，内存资源的实时管理；以及 4 个 NPU，负责跑 perception, chief 等算法、模型。DLA 的结构如下图所示：

![DLA-arch](D:\gitlab\ADAS\DLA-arch.svg)

各组件功能如下：

- DLA: Deep Learning Accelerator, 1 DLA contain 2 NPU cluster, which called NPC. Within a NPC, there are 4 NPU(neural processing unit) compute cores.
- NPU: 跑神经网络的；

- DSP: data signal processing for computing（SIMD 指令集），用于补充 NPU 支持不好的算子，主要是需要浮点运算的算子，承担额外算力支撑；

- RSU: 负责推理网络任务的调度和执行，以及 NPC 下面各种计算资源，内存资源的实时管理；

而 orinx, thor-u 都采用 GPGPU+DLA 的方案，我们来解释一下两种方案的区别（国内的智驾厂商基本都是 DLA 的玩法），以及 xp 未来的发展方向。

首先介绍一下 NPU 和 GPGPU 两种芯片的区别。

| **特性**       | **NPU**                     | GPGPU                               |
| :------------- | :-------------------------- | :---------------------------------- |
| **核心目标**   | 高能效AI推理/训练           | 通用并行计算（图形渲染、科学计算）  |
| **计算单元**   | 张量核心（固化矩阵乘加）    | SIMT 多线程核心（支持任意并行任务） |
| **精度支持**   | INT8 为主                   | FP32/FP64 为主                      |
| **内存架构**   | 近内存计算 + 大容量片上缓存 | 高带宽显存 + 分层内存管理           |
| **内存访问**   | 近内存计算，数据重用优化    | 依赖高带宽显存，数据搬运频繁        |
| **编程灵活性** | 依赖预定义算子，灵活性低    | 可自定义核函数，灵活性高            |
| **典型能效比** | 10-100 TOPS/W               | 1-5 TOPS/W                          |

第一代芯片处于保守的策略（要确保芯片能做出来）采用的较为简单的 NPU 路线，即针对当前智驾算法设计专用的硬件单元，达到最高的能效比。但这种方案相比于 GPGPU+DLA 不够通用。

当前 Turing 芯片相比 orinx 有如下优势：

- **算力大，专用网络效率高。**Turing 芯片设计时，主流的智驾算法用的是 CNN，针对小型卷积计算，利用特定算法可以做到多倍加速。而且 MAC 阵列设计的很大，INT8 算力堆的高。

有如下劣势：

- 智驾算法发展变化太快，**针对当前主流的 LLM，VLA 等大模型，Turing 芯片性能不佳**，也没法说下一代就针对 LLM 做优化，万一算法发展改变，关键算子也随之变化，那专用网络对于新的算子，效率就不高了。芯片设计的周期难以赶上算法迭代的周期，所以最佳的架构应该是 GPGPU+DLA，能够满足通用性和能效比的需求，但对芯片设计有更高的要求。**相比于理想现在 thor-u + MindVLA 的方案，小鹏的智驾方案要落后些。**但实际使用情况还有待观察。

- **只支持 int8 计算加速，可应用场景有限**。最新的 GPGPU 支持 FP64, FP32, FP16 等精度，可以在训练和推理时提高效率。而 NPU 处于专用的目的，主要就支持 INT8，对于有些对 F64, FP32 有需求的模型，Turing 使用一颗外购的 **Xtensa** DSP，浮点算力未知，但了解到对于那些需要大量浮点运算的模型性能没有那么好。

当前 Turing 芯片运行小鹏智驾算法，即 perception+map_manager+sensor_fusion+chief（规控算法）这一套，性能还是能超过 orinx 的，因为 NPU 的算力远远大于 orinx，但和 thor-u 相比，浮点差距很大。

小鹏的两段式大模型在 Turing 芯片上还在适配部署中，无法确定 7.31 前能否完成，后续可能通过 OTA 的方式升级。

下一代芯片可能会使用 GPGPU + DLA 方案，有可能兼容 cuda 生态，从而解决浮点算力的问题。

#### 征程-6 performance

##### 参数信息

| 型号                     | J6-P                                                         |
| ------------------------ | ------------------------------------------------------------ |
| 工艺                     | 未公布                                                       |
| 硅片面积                 | 未公布                                                       |
| 封装尺寸                 | 未公布                                                       |
| CPU(型号、数量、算力)    | 18x ARM Cortex-A78AE,410K DMIPS                              |
| DSP(型号、数量、算力)    | Multi Vision DSP                                             |
| GPU(型号、数量、算力)    | Arm G78-AE，从发布会来看，这个 GPU 只是为了将智能驾驶领域的图像输出到仪表或中控屏上。 |
| Vision/DNN Accelerators  | BPU,560TOPS，INT8 精度算力                                   |
| Functional Safety Island | 10x Arm Cortex-R52，可以配置成5个DCLS核，也可以按照3个DCLS + 4个Spilt模式核； |
| Memory System            | LPDDR5, 205GB/s，与英伟达Orinx相同                           |
| High-Speed               | PCIE4.0 ETH (10Gbps) UFS EMMC                                |
| C2C/D2D Interface        | 未公布                                                       |
| Imaging System           | Multi ISP. HDR MIPI CSI-2RX MIPI CSI-2TX 5.3Gpixel/s         |
| Video Decode             | 未公布                                                       |
| Video Encode             | 未公布                                                       |
| Display                  | 未公布                                                       |
| 功能安全等级             | AEC-Q100：Grade2                                             |

另外，发布会提到：为应对新一代大模型 Transformer 较多的矢量运算，特别添加了 VPU，即矢量浮点运算加速单元。这个 VPU 的作用有待确认，猜测是 J6 系列设计时，没有预料到 VLM, VLA 的大模型对智驾的影响这么大，为了增加买点，临时加的。

其他的公开参数信息和小鹏的图灵芯片类似，主要差异还是在 BPU 的设计与模型算法的部署上。

征程 6 有多款产品，每款的算力不同，猜测应该是 BSP 中放的 NPU 数量区别。官网中提到，J6-P 的 BPU Nash 有 4 核，那不同算力的芯片，就是对 NPU 核心数，以及 NPU 峰值频率的调整（算力计算方式：MAC 阵列数 n * NPU 峰值频率 * NPU 核心数）。

其他算法部署、优化方面工作，还需要找人请教。

#### 其他厂商

- 地平线，BPU 也采用的是以支持 INT8 类型的 NPU 为主要算力单元，和轻舟智航合作，对关键算子进行优化；
- 理想，采用数据流架构，和传统的冯 诺伊曼架构的由一条指令完成取值，运算，结果写回的流程不同，数据流架构是有数据来了就运算。一个是编译好的程序去运行，一个是有数据来才去运算。不过这条路没人走通过，理想也在编译器方面遇到了困难；
- 蔚来，名义上的 GPGPU 方案，其实内部是两个 NPU 集成，两者结构一样，但从功能上划分为通过 NPU 和专用 NPU，针对不同算子进行了优化；
- 黑芝麻，待了解；

### 算法分析

#### 发展历程

智驾算法的发展可以分为三个阶段：

- AV 1.0: Autonomous Driving by Design: 在这个早期阶段，主要目标是通过构建完整的、基于规则的 AV 流程来展示 AV 功能。这些系统主要是**手动设计的**，具有明确的、人工制定的感知、决策和控制逻辑。可扩展性有限，无法处理复现的现实路况；
- AV 2.0: Autonomous Driving through Discriminative Learning: 这一阶段标志着向数据驱动、基于学习方法的范式转变，特别是在判别式框架内利用深度学习。核心 AV 组件（如感知、预测和规划）越来越依赖于学习从观测输入到标记输出的映射——例如从传感器数据推断物体检测结果，或从交通场景生成控制动作。但在离散、有限的数据集上训练的模型**难以泛化到未见过的或罕见的情况**，而这些情况往往是最具安全关键性的。
- AV 3.0: Autonomous Driving through Generative Learning: 在 AV 3.0 中，智能体不再在孤立的实例上进行训练；相反，它旨在通过结构化和规范化的建模，在相互关联的场景之间进行泛化，使其能够从观察到的数据中学习，而不仅仅是模仿。

##### AV 1.0

2007 年的 DAPPA 城市挑战赛，各个参赛团队被要求设计能够在城市环境中完成完全自动驾驶旅程的自动驾驶汽车。在挑战赛中，车辆需要感知周围环境、避开动态障碍物、规划路径、控制车辆并到达目的地。挑战赛后，所有团队都发表了技术论文，详细阐述了他们实现自动驾驶的方法。一些团队随后开始了自动驾驶技术的商业开发，例如 Waymo（谷歌）。这标志着第一代自动驾驶系统架构（即 AV 1.0）共识的出现，并启动了对下图所示的各种系统模块的重点研究。

![image-20250605114323472](D:\gitlab\ADAS\AV_1.0_ARCH.png)

**架构**

广泛接受的自动驾驶系统结构主要包括感知（perception）、规划（planning）、控制（control）等模块。

**感知**

感知模块的目标是获取驾驶条件，包括静态和动态道路物体、道路车道、交通标志以及自车（ego vehicle）的运动学状态。常用的环境感知传感器包括激光雷达（LiDAR）、摄像头、雷达、GPS 和惯性测量单元（IMU）。

- 物体：
  - 激光雷达：激光雷达发射车辆周围的密集点云并测量每个点与车辆的距离，通过应用 [K-最近邻](https://pmc.ncbi.nlm.nih.gov/articles/PMC4916348/pdf/atm-04-11-218.pdf)（K-Nearest Neighbors, KNN）聚类等方法，可以识别出高于地面的物体。需要了解更多激光雷达算法，以及激光雷达在整个 AV1.0, AV2.0, AV3.0 的位置；
  - 相机：通过从图像中提取特征点，并利用相机的物理特性估计它们相对于车辆的位置，从而识别高于地面的物体。
    - 立体相机使用两个相机之间的相对距离，结合 [PnP（Perspective-n-Point）算法](https://pismin.com/10.1016/j.trc.2019.03.009)（xpeng 当前的静态感知算法）；
    - 单目相机依赖相机距地面的高度及其内参，使用[逆透视映射](https://pismin.com/10.1109/tencon.2004.1414393)（Inverse Perspective Mapping, IPM）。立体相机的测量精度通常取决于两幅图像之间特征点匹配的精度，而单目相机则更依赖于校准精度；
  - 动态物体：
    - **光流法（Optical Flow Method）**：该技术分析连续两帧之间的差异以生成光流，用于估计动态障碍物的运动；
    - **特征点匹配法**：诸如尺度不变特征变换（SIFT）、加速鲁棒特征（SURF）和定向 FAST 及[鲁棒 BRIEF（ORB）](https://d1wqtxts1xzle7.cloudfront.net/90592905/145_s14_01-libre.pdf?1662172284=&response-content-disposition=inline%3B+filename%3DORB_An_efficient_alternative_to_SIFT_or.pdf&Expires=1749171559&Signature=XqcYynLr4iPB3pgm5qh~fHofyKPD8cqjgyuK7OgIcIonRFYPadZdUSRhzESBAjMulEiEM2G7N0zHWs1nHXYIn-dceQQkdrL1ogph8rCfdXTmQ5v8gDTk9jAyOAbSzBPi2Tuld-iLXOXQh5lG7I~fyWQSd1wO5SdcVWYvuOs-CHD5rgUxlRHs-qtkNj0OEU0lVYkA9JYrgr-KX~FhKOH7FRcPEEk-iLLaTA4NMOweO8ojeBma2E2zhw~BSAj5Ceaxl1uN8ZP2sipYBPmSAhmdQ8z4KwccxOyddoe-DJ~voefns-LlWlDiHNLfSbrkDNuQAnOzB-wfelWa031HJlx5Gw__&Key-Pair-Id=APKAJLOHF5GGSLRBV4ZA)等算法在连续帧之间匹配特征点，这些点的位移用于估计其速度，进而估计物体的运动。这种方法精度更高，使用更广；
- 车道：[相关论文](https://romisatriawahono.net/lecture/rm/survey/computer%20vision/Yenikaya%20-%20Keeping%20the%20Vehicle%20on%20the%20Road%20-%202013.pdf)
- 交通标志：[相关论文](https://www.cv-foundation.org/openaccess/content_cvpr_2016/papers/Zhu_Traffic-Sign_Detection_and_CVPR_2016_paper.pdf)
- 自车状态：自车状态通常包括其位置、方向、速度和加速度，这些信息通过车载传感器测量。位置估计通常采用精度更高的差分实时动态定位 RTK-GPS，精度可达 10 厘米以内。车辆的航向使用放置在车辆前后两个位置的 GPS 设备计算得出。速度和加速度可以从位置差推导或使用轮速传感器和[惯性测量单元（IMU）](https://www.ijsps.com/uploadfile/2013/1128/20131128022014877.pdf)测量；

**路径规划与控制**

并非所有智驾系统都包含下面四个组件。

**导航**

传统的导航系统通常输出一系列路段和交叉口，然后，理想中的自动驾驶汽车应根据是是环境规划路径。

**行为规划**

行为规划经历了三个关键阶段：

- [有限状态机（Finite State Machines）](https://robots.stanford.edu/papers/junior08.pdf)：对所有环境状态进行分类，并为每个类别分配特定行为以实现决策；这种方法后来扩展到[基于场景的自动驾驶（scenario-based autonomous driving）](https://www.fshuo.tech/publication/ieee-t-its-part-i/IEEE-T-ITS-Part%20I.pdf)，通过构建广泛的场景库进一步细化车辆行为。这种方法在很长一段时间内显著影响了自动驾驶系统的开发。然而，随着场景库的扩大，研究人员开始遇到诸如场景描述过于复杂、场景边界不清晰以及**难以定义某些场景**等挑战。现在行车、泊车分开，是不是也是基于这个思想的。
- 行为建模（Behavior Modeling）：引入了驾驶员行为模型来整合多个场景。然而，由于场景分割的复杂性，尚未提出被广泛接受的特定场景模型，限制了这种方法的进一步发展。
- 通用行为规划（General Behavior Planning）：该方法将行为生成视为一个优化问题：在车辆可达状态空间内找到一条可行且成本效益高的轨迹。[代表方法](https://www.ri.cmu.edu/pub_files/2011/5/20100914_icra2011-mcnaughton.pdf)，[基于网格的规划（lattice-based planning）](https://rpal.cs.cornell.edu/docs/PivEtal_JFR_2009.pdf)。

**轨迹规划**

轨迹规划的目标是生成从当前点或特定起点到目标点的路径或轨迹。轨迹规划算法后来演变成并收敛于 [Lattice 算法](https://www.researchgate.net/profile/Moritz-Werling/publication/224156269_Optimal_Trajectory_Generation_for_Dynamic_Street_Scenarios_in_a_Frenet_Frame/links/54f749df0cf210398e9277af/Optimal-Trajectory-Generation-for-Dynamic-Street-Scenarios-in-a-Frenet-Frame.pdf)，该算法基于空间采样并使用五次多项式进行平滑。这种方法为各种行为提供了显著的灵活性，同时保持高效率，使其在自动驾驶系统中被广泛采用。

**控制**

控制模块旨在使车辆能够精确跟踪规划的轨迹。PID 控制器和最优控制技术（如 [MPC](https://shbing.github.io/papers/first-author/J7_Design-Analysis-and-Experiments-of-Preview-Path-Tracking-Control-for-Autonomous-Vehicles.pdf)）被广泛采用。

**讨论**

AV 1.0 阶段的著名系统有 Autoware，百度 Apollo 等，它们提供了模块化、全栈的流程，涵盖核心 AV 功能，包括感知、规划和控制。这些平台允许研究人员和开发人员试验、定制和扩展 AV 功能，加速了创新并促进了学术界和工业界的合作。

当前 xpeng 的智驾系统还有 Apollo 的影子。

##### AV 2.0

两个主要因素催化了 AV 2.0 的出现。首先，深度学习（Deep Learning, DL）的兴起彻底改变了处理高维任务的方法。[AlexNet](https://proceedings.neurips.cc/paper_files/paper/2012/file/c399862d3b9d6b76c8436e924a68c45b-Paper.pdf) 等里程碑式的成就展示了 DL 在计算机视觉方面的潜力，而 [AlphaGo](Mastering the game of go with deep neural networks and tree search) 则凸显了强化学习（Reinforcement Learning, RL）在复杂领域决策中的前景。其次，大规模、高质量数据集（如 KITTI和 Waymo 开放数据集）的可用性使得数据驱动方法得以快速发展。

![image-20250606090201080](D:\gitlab\ADAS\AV_2.0_ARCH.png)

**架构**

- **感知**：感知模块完全转向基于学习的方法。深度学习在计算机视觉方面的突破推动了物体检测、语义分割和传感器融合（利用来自摄像头、激光雷达和雷达的数据）的进步。
- **规划**：虽然基于模型的方法因其鲁棒性和可解释性而仍然有价值，但基于学习的方法，特别是模仿学习（Imitation Learning）和强化学习（Reinforcement Learning），在规划任务中获得了关注，在复杂环境中提供了更强的适应性。
- **端到端（E2E）流程**：虽然模块化流程在可解释性以及开发和调试的便利性方面具有优势，但 E2E 架构通过联合优化整个 AV 堆栈，具有实现更高上限性能的潜力。
- **测试与评估**：随着 AV 技术的进步，安全测试和评估对于部署准备变得至关重要。AV 2.0 引入了功能安全检查和基于场景的评估，以评估 AV 硬件和软件组件的性能和可靠性。
- **支持工具**：公开可用的传感和轨迹数据集对感知和规划模块的基准测试和推进起到了重要作用。高保真模拟器的出现为训练和评估 AV 系统提供了虚拟环境。

**感知**

- 早期里程碑：基于图像与 CNN 的感知：基于深度学习的感知系统浪潮始于 2012 年 AlexNet 的引入，该网络利用卷积神经网络（CNNs）进行图像分类。这一突破催生了众多有影响力的工作，将 CNNs 扩展到物体检测、语义分割和跟踪任务。著名的里程碑包括 OverFeat [63]、R-CNN 系列 [64, 65, 66, 67]、YOLO 系列 [68, 69, 70]、SPP [71]、FCN [72]、SSD [73]、CenterNet [74] 和 Deep SORT [75]。这些方法通常建立在基础架构之上，如 AlexNet [57]、VGG-16 [76]、Inception [77] 和 ResNet [78]。基于图像的感知系统显著提高了 AV 使用摄像头输入感知和解释周围环境的能力，与传统的基于手工特征的方法相比，实现了前所未有的可扩展性和准确性。

- 迈向 3D：基于激光雷达的感知：激光雷达对于 AV 感知至关重要，它在不同的光照和天气条件下提供精确的深度测量和可靠的性能，是对基于摄像头系统的补充。虽然摄像头捕获丰富的语义细节，但它们在低光或恶劣环境中表现不佳，而激光雷达在几何精度和鲁棒性方面表现出色。这种协同作用使得能够采用融合两种传感器优势的多模态方法。激光雷达开启了 3D 感知的时代，PointNet [81]、PointNet++ [82]、VoxelNet [83]、PIXOR [84] 和 PointPillars [85] 等方法利用激光雷达生成的 3D 点云。这些方法将 2D CNN 概念应用于 3D CNN 架构，以从空间数据中提取有意义的特征。2D 图像（富含语义）和 3D 点云（几何和距离精确）的互补性激发了多模态感知方法。开创性的工作如 MV3D [86]、Frustum PointNet [87]、Cont-Fuse [88] 和 AVOD [89] 结合了摄像头和激光雷达数据，实现了更全面的感知能力。

  Transformer 架构 [92] 的引入显著改变了感知格局。最初在自然语言处理（NLP）领域取得成功，Transformer 在计算机视觉任务中也显示出巨大潜力，这得益于其强大的序列建模能力和处理长距离依赖关系的能力。像 DETR [93]、Deformable DETR [94] 和 Conditional DETR [95] 这样的开创性工作展示了 Transformer 在物体检测中的有效性。ViT [97] 及其变体（如 Swin Transformer [98]、Twins [99]、Transformer-in-Transformer [100]）在图像分类和分割方面取得了最先进的性能。SegFormer [101] 等模型进一步证明了 Transformer 在检测、分割和跟踪方面超越传统基于 CNN 方法的能力。

- 鸟瞰图（BEV）表示的出现：鸟瞰图具有无缝传感器融合、减少遮挡和尺度问题以及与规划和控制自然对齐等优势。早期工作如 PIXOR [84] 和 HDNET [103] 奠定了基础，但在特斯拉采用基于 BEV 的向量空间构建 [104] 后，BEV 获得了广泛关注。基于 Transformer 的模型如 BEVFormer [105] 和 BEVFusion [106] 通过利用多视角摄像头和激光雷达数据进一步推动了前沿发展。

  随着 BEV 成为主流感知方法，其发展分成了两个主要方向：2D 到 3D 提升（lifting）和直接 3D 投影。前者估计深度并将特征反投影（unproject）到 3D 空间，如 BEVFormerV2 [107] 引入了一个带有透视图监督（perspective view supervision）的两阶段 BEV 检测框架。后者将预定义的 3D 坐标体投影到 2D 视图上并聚合图像特征，以 SparseBEV [108] 为例，它引入了尺度自适应注意力机制和时空采样策略，显著增强了检测器在 BEV 和图像空间的适应性。

  最近的进展还将语言作为补充模态纳入，以增强语义理解。BEV-TSR [109] 通过跨模态 BEV-语言嵌入实现场景检索，而 CLIP-BEVFormer [110] 应用对比学习将语言监督以最少真值（ground truth）的方式整合到 BEV 特征中。

- 从 2D 到 3D 整体表示：基于占据（Occupancy-based）的感知植根于经典的占据栅格地图（Occupancy Grid Mapping, OGM）[112]，已演变为 3D 占据预测（3D occupancy prediction）——一种更丰富的场景表示，将体素（voxels）分类为空闲、占据或未观察，通常带有语义标签。与 BEV 或 OGM 不同，现代 3D 占据方法消除了对测距传感器和静态场景假设的依赖，提供了更完整的空间理解。

  单目和以视觉为中心的方法最近取得了进展，包括 MonoScene [113]、特斯拉的占据网络 [114] 和 TPVFormer [115]，仅使用摄像头就达到了激光雷达级别的性能。像 SurroundOcc [116]、OccFormer [117] 和 FB-OCC [118] 这样的模型增强了多视图融合和空间推理能力。Cam4DOcc [119] 进一步将占据扩展到时空域，用于动态场景预测。

**规划**

整体流程与 AV 1.0 结构保持一致，包括导航、行为规划、轨迹规划和控制。其中，导航和控制变化最小，而行为和轨迹规划方面则取得了显著进步。事实上，行为规划和轨迹规划之间的界限变得越来越模糊，并且在许多 AV 2.0 系统中，两者被当作一个整体处理。

- 模仿学习：模块化规划流程通常涉及两个关键模块：轨迹预测（trajectory prediction）和规划（planning）。在轨迹预测中，基于 IL 的方法学习预测周围智能体（如车辆、行人和骑行者）的行为，以实现安全有效的规划。通过利用大规模驾驶数据集，这些方法使用监督学习将观察到的状态映射到未来轨迹。显著的进展包括 Social LSTM [127]、MultiPath [128]、Multipath++ [129]、VectorNet [130]、TNT [131]、DenseTNT [132]、MotionLM [133]、MTR [134]、MTR++ [135] 和 QCNet [136]。其中，VectorNet [130] 特别引入了路网的向量化表示，后来被其他方法广泛采用，提高了模型效率和可扩展性。

  一旦轨迹预测完成，规划模块利用预测的智能体行为，通过模仿人类驾驶行为来规划自车的轨迹。在训练期间，除了标准的 IL 模仿损失外，系统还纳入了与防撞、偏离道路和地图遵守相关的额外损失项，以优化未来轨迹。然而，轨迹预测和规划之间的区别并不总是明确的。许多方法，如 ChauffeurNet [137]、LookOut [138]、PiP [139]、MP3 [140] 和 DIPP [141]，联合预测周围智能体的行为和自车未来的轨迹。虽然 IL 实现了显著进步，但它仍受到诸如累积误差（compounding errors）[142] 等挑战的困扰，这可能导致 AV 序列决策过程中的性能下降。

- 强化学习：强化学习（RL）在 AV 2.0 阶段的兴起为 AV 规划带来了一个有前景的新方向。与依赖专家演示的 IL 不同，RL 使智能体能够自主探索环境并通过试错学习，使 AV 有潜力在复杂和动态的驾驶任务中实现超人类表现。受 RL 在 Atari 游戏 [143]、AlphaGo [58] 和 AlphaGo Zero [144] 中成功的启发，广泛的研究 [145, 146, 147, 148, 149, 150, 151, 152] 探索了 RL 在 AV 规划中的应用，在模拟和真实世界环境中使用了基于值（value-based）、基于策略（policy-based）和演员-评论员（actor-critic）方法。著名的研究，如 [146]，开发了用于在模拟器中控制车辆的 RL 框架，而 [147] 应用深度确定性策略梯度（Deep Deterministic Policy Gradient, DDPG）在现实世界环境中控制全尺寸机器人车辆。

  虽然 RL 已展现出有希望的结果，但其在现实世界的部署面临诸如数据效率低下、模拟到现实的差距（sim-to-real gap）以及缺乏理论安全保证等挑战。为了解决这些问题，安全 RL（safe RL）[153] 和置信感知 RL（confidence-aware RL）[154] 的最新发展专注于确保在动态环境中的持续性能改进和安全性。关于 RL 在 AV 规划中应用的全面综述可在 [155] 中找到。

- 其他：

**端到端（E2E）流程**

端到端（End-to-End, E2E）流程从根本上不同于传统的模块化架构，后者独立设计感知、规划和控制组件。E2E 系统的核心思想是联合优化整个自动驾驶堆栈，要么通过完全可微分的模块化系统，要么作为一个完整的黑盒模型，使用数据驱动方法（如 IL）进行整体训练。这种统一的训练框架实现了集成的任务优化，并允许跨组件的无缝信息流。

早期的 E2E 方法证明了使用深度神经网络将原始传感器输入直接映射到控制命令的可行性。值得注意的是，英伟达的 DAVE-2 [169] 和其他同时代的系统 [170, 147, 171] 使用 CNN 从前向摄像头图像预测转向命令，在现实世界设置中实现了短距离自动驾驶。这些开创性的努力通过消除对人工设计的中间表示和独立模块的需求，展示了 E2E 学习的直观吸引力。

在此基础上，最近的系统通过架构创新进一步提升了性能和泛化能力。VAD [173] 引入了完全向量化的场景表示，避免了栅格化输入和后处理，从而提高了可解释性和准确性。OccNet [174] 采用了多视图、以视觉为中心的架构，利用占据图和物体检测输出来提高规划精度。VADv2 [175] 将规划策略建模为环境条件化的非平稳随机过程，使用概率场将动作空间映射到概率分布，显著提高了闭环性能。SparseDrive [176] 通过引入稀疏感知模块和具有分层决策层的并行规划器，解决了密集 BEV 表示的低效问题，在安全性和计算效率方面取得了显著提升。

尽管取得了这些进展，挑战仍然存在。当前大多数 E2E 系统缺乏安全保证，并且在复杂驾驶场景中难以可靠地泛化。特别是在安全约束下进行不确定性下的概率规划，在端到端自动驾驶中仍然是一个开放性问题。

**测试与验证**

**支持工具**

##### AV 3.0

AV 3.0 目标的最显著转变是它不再在孤立案例上进行训练，而是学习驾驶场景之间的内在关系，从而实现可扩展且可信赖的自动驾驶。

向 AV 3.0 范式的转变带来了显著的好处。与严重依赖广泛数据收集的传统方法不同，AV 3.0 利用学习到的内部关系，能够从有限的演示示例中进行外推（extrapolation），使系统能够在广泛的类似场景中扩展。此外，这些结构化的内部关系通过保留先前获得的驾驶知识来支持持续学习（continual learning）。因此，系统可以整合新经验并持续适应新场景，而不会降低先前的性能。至关重要的是，AV 3.0 赋予智能体一个关键能力——预测和导航先前未见过的场景，同时始终维持可靠的防撞能力，尤其是在高风险和时间紧迫的条件下。这些特性使 AV 3.0 成为一个有前景的范式，能够实现本质上可扩展且安全的自动驾驶。

![image-20250606113233443](D:\gitlab\ADAS\AV_3.0_ARCH.png)

**利用 LLM/VLM/MLLM 增强 AV 系统**

大语言模型（LLMs）、视觉语言模型（VLMs）以及更广泛的多模态大语言模型（Multimodal Large Language Models, MLLMs）已展现出在自然语言内部以及跨模态（特别是语言和视觉之间）捕获稳定内在关系的能力——通常被称为常识知识（commonsense knowledge）。

首先，这些预训练模型可用于描述[多样化](https://arxiv.org/pdf/2403.09630)的[驾驶条件](https://arxiv.org/pdf/2408.09251) 、[解释交通规则](https://arxiv.org/pdf/2402.05932)和[表征正在进行的上下文事件](https://arxiv.org/pdf/2310.02251)。它们作为中介，将高级语义信息转化为自动驾驶系统可以解释的可操作命令，从而使 AV 能够处理系统设计时未明确考虑的场景。

这些模型及其相关的建模技术也可以集成到自动驾驶系统的[感知](https://arxiv.org/pdf/2312.14074)、[运动预测](https://arxiv.org/pdf/2309.16534)、[场景构建](https://arxiv.org/pdf/2310.17642)、[行为规划模块](https://arxiv.org/pdf/2410.22313)和[驾驶员交互](https://arxiv.org/pdf/2309.10228)中。在大多数这些方法中，模块特定的输入首先被转换为类似语言的表示，允许 LLMs 在语言空间中进行推理或生成。然后输出被转换回原始模态或控制格式，实现与现有 AV 系统组件的兼容集成。

在我们自己的[研究](https://arxiv.org/pdf/2505.00284)中，我们评估了 12 种不同 VLM 的驾驶相关能力，发现它们当前的性能仍然达不到实际部署要求。

下面简要概括上述论文：

- Generalized predictive model for autonomous driving

  GenAD 能够以零样本方式泛化到各种未见过的驾驶数据集，性能超越通用或专用的视频预测模型。此外，GenAD 可适配为动作条件预测模型或运动规划器，在实际驾驶应用中具有巨大潜力。

  智驾算法存在的问题：结构化自动驾驶系统中学习模型的泛化能力有限。具体而言，感知模型难以在地理位置、传感器配置、天气条件、开放集物体等变化的多样化环境中泛化；预测和规划模型无法应对罕见场景和不同驾驶意图的非确定性未来。

  该模型首先用 SDXL 进行初始化，这是一个用于文本到图像生成的大规模潜在扩散模型（LDM），以利用其合成具有丰富视觉细节的高质量图像的能力。

  然后 GenAD 被训练通过在过去帧和文本条件的指导下联合从噪声潜在变量去噪来预测未来。

  依靠在驾驶场景中训练良好的视频预测能力，进一步探索预训练模型在动作控制预测和规划中的潜力，通过冻结的 GenAD 的 UNet 编码器提取两个历史帧的时空特征，该编码器几乎是整个模型大小的一半，并将它们输入多层感知机（MLP）以预测未来路点。

  **只是给出预测，但不做最后的规控。**

  ![image-20250616162834761](C:\Users\xpeng\AppData\Roaming\Typora\typora-user-images\image-20250616162834761.png)

- V2x-vlm: End-to-end v2x cooperative autonomous driving through large vision-language models: 

  V2X-VLM 是另一种利用 VLM 增强轨迹规划水平的方案。

  该模型整合三种来源的数据：1）车载摄像头图像 Lv，这提供了来自车辆的直接视觉输入，捕获了车辆周围的关键实时信息，包括其他车辆、道路标记和障碍物；2）基础设施摄像头图像 Li，该图像从放置在交叉路口等战略基础设施点的摄像头收集，提供了更广泛的交通模式和行人活动的视图，这些可能从车辆的视角不可见；3）文本提示 E，其中包括与驾驶上下文相关的描述性文本信息。如下图左侧所示，来自两个视角的图像对([Lv,Li])与提示E一起通过嵌入在 VLM 中的图像编码器和文本编码器进行处理，这种方法通过***\*确保图像与其对应的描述性提示正确对齐\****，增强了 V2X-VLM 框架的场景理解能力，从而输出准确的轨迹路线。此外，该框架包含一个突发特征检查与校正模块，以解决大型 VLM 输出轨迹中出现的突发特征，如下图右侧所示。

  但是该模型需要高效的网络条件获取基础设施摄像头图像，经计算，如果传输频率为 2Hz，需要 12GB/s 的网络带宽。而 2Hz 轨迹输出频率也无法满足自动驾驶的要求，该轨迹只能用于优化传统规控算法的轨迹路线。另一方面，如何获取基础设施摄像头图像也是一个问题。

  ![image-20250616163626102](C:\Users\xpeng\AppData\Roaming\Typora\typora-user-images\image-20250616163626102.png)

- Driving everywhere with large language model policy adaptation:

  本文提出 LLaDA（大语言驾驶助手），这是一种简单而强大的工具，通过将任务和运动规划适配到新地区的交通规则，使人类驾驶员和自动驾驶汽车都能在任何地方驾驶。

  我们的目标是构建大语言驾驶助手（LLaDA），以快速适应本地交通规则和习俗（图 1）。我们的方法包括三个步骤：首先，利用现有方法生成可执行策略；其次，当遇到自然语言描述的意外情况时（由人类提示或 GPT-4V [24]、LINGO-1 [32] 等视觉语言模型提供），利用交通规则提取器（TRE）从本地交通法规中提取与当前场景相关的信息性交通规则；最后，将 TRE 的输出与原始规划一起输入预训练 LLM（本文使用 GPT-4V [24]），相应地调整规划。

  归根结底，只是辅助的定位。

- Talk2bev: Language-enhanced bird’s-eye view maps for autonomous driving:

- Lidar-llm: Exploring the potential of large language models for 3d lidar understanding

  LiDAR-LLM，核心思想是将高度稀疏且几何复杂的激光雷达数据转换为大型语言模型（LLMs）可理解的表示空间，从而增强 LLM 对视觉特征的空间方位理解。

  其提出的View-Aware Transformer（VAT）***\*将激光雷达 BEV 特征投影到预训练 LLaMA 的词嵌入空间\****。VAT 的输入包括激光雷达的 BEV 特征投影与一组 K 个可学习查询嵌入。这些查询通过交叉注意力机制与 BEV 特征交互。VAT 输出 K 个编码视觉向量（每个查询嵌入对应一个），经多层感知机（MLP）处理后输入冻结的 LLM。

  预训练的LLaMA能够输出对激光雷达BEV数据进行解析，输出对当前场景的描述：1）当前场景或交通状况的一般描述；2）物体及其关系的详细描述；3）道路潜在风险的识别。

  ![image-20250616165124227](C:\Users\xpeng\AppData\Roaming\Typora\typora-user-images\image-20250616165124227.png)

- Drive anywhere: Generalizable end-to-end autonomous driving with multi-modal foundation models:

- Senna: Bridging large vision-language models and end-to-end autonomous driving

  本文提出 Senna，一种将 LVLM（Senna-VLM）与端到端模型（Senna-E2E）相结合的自动驾驶系统。Senna 将高级规划与低级轨迹预测解耦。Senna-VLM 以自然语言生成规划决策，Senna-E2E 预测精确轨迹。

  Senna-VLM 是专为驾驶任务设计的。它支持多图像输入以编码环视数据，这对于理解驾驶场景和确保安全至关重要。

  Senna-VLM 以自然语言预测高级决策信息，避免了 LVLMs 预测精确数值的需要。这种方法与涉及自然语言预测的 LVLM 预训练任务一致，有效利用预训练知识做出准确的规划决策。基于 Senna-VLM 预测的决策，Senna-E2E 将生成最终的规划轨迹。

  ![image-20250616170934428](C:\Users\xpeng\AppData\Roaming\Typora\typora-user-images\image-20250616170934428.png)

- Drive as you speak: Enabling human-like interaction with large language models in autonomous vehicles:

  通过整合 LLM 的自然语言处理能力和上下文理解能力、专用工具的使用能力，以及与自动驾驶汽车上各种模块的协同推理和行动能力，该框架旨在将 LLM 先进的语言和推理能力无缝集成到自动驾驶汽车中。所提出的框架有望彻底改变自动驾驶汽车的运行方式，提供个性化协助、持续学习和透明的决策过程，最终为更安全、更高效的自动驾驶技术做出贡献。

  这里只是提高自动驾驶的交互性与可解释性，不做规控方面的工作。

- Hydra-mdp: End-to-end multimodal planning with multi-target hydra-distillation:

- Activead: Planning-oriented active learning for end-to-end autonomous driving:

- End-to-end autonomous driving without costly modularization and 3d manual annotation:

**基于 MLLM 构建的 AV 系统**

一些方法不是简单地将 MLLMs 集成到现有的 AV 架构中，而是旨在重新训练或调整 MLLMs 作为构建自动驾驶智能体的基础。与上一节的关键区别在于，MLLM 现在被赋予了做出最终控制决策的角色，超越了其早期作为感知或语义推理辅助模块的用途。

例如，[DriveGPT4](https://arxiv.org/pdf/2310.01412)、[DriveMLM](https://arxiv.org/pdf/2312.09245) 和 [LanguageMPC](https://arxiv.org/pdf/2310.03026) 使 MLLMs 能够分析场景并从预定义的动作集中预测规划动作。[MTD-GPT](https://arxiv.org/pdf/2307.16118) 将决策过程制定为序列建模任务，也可以[利用驾驶数据](https://arxiv.org/pdf/2312.14115)以及[先进的 MLLMs](https://arxiv.org/pdf/2312.14150)，通过问答（QA）任务进行场景理解和评估。[GPT-Driver](https://arxiv.org/pdf/2310.01415) 采取了不同的方法，将运动规划构建为语言建模问题，从 GPT 风格模型的角度提供基于令牌（token）的规划过程解释。

Wayve 提出的 [Driving with LLMs](https://arxiv.org/pdf/2310.01957) 引入了一种架构，将向量化驾驶输入嵌入到 LLM 中，结合两阶段的预训练和微调策略，以增强驾驶特定的推理和控制。此外，像 [DriveVLM](https://arxiv.org/pdf/2402.12289)（微调 QWen-VL）和 [EMMA](https://arxiv.org/pdf/2410.23262)（微调 Gemini）这样的模型，以及后续工作 OpenEMMA 和 LightEMMA，展示了将通用 MLLMs 适配用于驾驶任务的可行性。[DriveMLM](https://arxiv.org/pdf/2312.09245) 和 [LMDrive](https://arxiv.org/pdf/2312.07488) 也以 LLaMA为骨干进行微调，结合额外的视觉编码器以提高驾驶性能。

此外，一些工作针对基于 MLLM 的 AV 智能体进行了优化，以提高计算效率，例如 DriveVLM 和 [Senna](https://arxiv.org/pdf/2410.22313)。一些新颖的数据集和基准测试也被提出，以进一步增强 VLM 在驾驶领域的推理能力。LANGPROP 应用 LLMs 来优化驾驶应用中的代码以提高自动驾驶算法的计算效率和性能。

然而，应该承认的是，虽然这些工作探索了将 MLLMs 应用于自动驾驶任务的各种方式，但它们未能从根本上解决自动驾驶的核心挑战。此外，对于 MLLMs 在自动驾驶框架中应扮演的确切角色，仍然缺乏共识。

下面简要概括上述论文：

- Drivegpt4: Interpretable end-to-end autonomous driving via large language model: 
- Drivemlm: Aligning multi-modal large language models with behavioral planning states for autonomous driving
- Languagempc: Large language models as decision makers for autonomous driving
- Mtd-gpt: A multi-task decision-making gpt model for autonomous driving at unsignalized intersections
- Lingoqa: Video question answering for autonomous driving
- Drivelm: Driving with graph visual question answering
- Gpt-driver: Learning to drive with gpt
- Driving with llms: Fusing object-level vector modality for explainable autonomous driving
- Drivevlm: The convergence of autonomous driving and large vision-language models
- Qwen-vl: A frontier large vision-language model with versatile abilities
- Emma: End-to-end multimodalmodel for autonomous driving
- Gemini: a family of highly capable multimodal models
- Open-emma: Open-source multimodal model for end-to-end autonomousdriving
- Lmdrive: Closed-loop end-to-end driving with large language models
- Llama:Open and efficient foundation language models
- Reason2drive: Towards interpretable and chain-based reasoning for autonomous driving
- Surrealdriver: Designing generative driver agent simulation framework in urban contexts based on large language model
- Langprop: A code optimization frame-work using large language models applied to driving

**基于世界模型的 AV**

另一条探索路线涉及训练模型以捕获环境动态的紧凑、结构化表示，以支持 AV 系统中的规划和控制，称为[世界模型（World Models）](https://openreview.net/pdf?id=BZ5a1r-kVsf)。这些世界模型通常与[强化学习（RL）](https://www.nature.com/articles/s41586-025-08744-2) 或[模型预测控制（MPC）](https://arxiv.org/pdf/2310.16828) 结合，以在陌生场景中增强可扩展性和泛化能力。

**世界模型指的是一个AI系统内部构建的、用来理解、预测和推理其所处外部环境（或“世界”）如何运作的表示或模拟。**

你可以把它想象成AI大脑里的一个“内部模拟器”或“心智地图”。这个模型不是对世界的完美复制，而是AI通过学习从环境中感知到的数据（图像、文本、声音、动作反馈等）而提炼出的关于**世界关键要素、它们之间的动态关系以及行动如何影响世界状态变化**的压缩表示。

没有强大的世界模型，自动驾驶系统就很难真正理解复杂、动态、充满不确定性的真实交通环境，其行为会显得僵硬、短视，难以安全高效地处理交互性强的场景和未知挑战。

世界模型可以通过各种模态构建，包括[视频预测](https://arxiv.org/pdf/2311.17918)、[点云预测](https://openreview.net/pdf?id=Psl75UCoZM)、[占据预测](https://arxiv.org/pdf/2311.16038)和[抽象状态预测](https://arxiv.org/pdf/2409.16663)。[OccLLaMA](https://arxiv.org/pdf/2409.03272) 通过结合 4D 场景理解、时间推理和运动规划，在大规模真实世界数据集上训练，扩展了传统的占据预测。通过利用世界模型的生成能力，AV 系统在处理先前未见场景方面的泛化能力得到了显著增强。例如，[DriveDreamer2](https://arxiv.org/pdf/2403.06845) 将 LLMs 与扩散模型（diffusion models）集成，从自然语言输入生成可控的驾驶场景视频——包括突然超车等罕见事件——并在感知性能上实现了超过 4% 的提升。此外，一些研究探索了将世界模型与 [RL](https://arxiv.org/pdf/2412.03572)、[MPC](https://arxiv.org/pdf/2408.14197) 和持续学习（continual learning）[154] 集成，以增强 AV 智能体处理极端案例（corner cases）的能力。这些方法利用来自世界模型的基于想象（imagination-based）的推演（rollouts）来提高复杂或罕见驾驶场景中的样本效率、长时程推理（long-horizon reasoning）和适应性。

与通用世界模型相比，一部分研究专注于自动驾驶特定的世界模型，主要侧重于捕获和预测周围智能体的行为。显著的例子包括 NeuralNDE [164]、MTR/++ [134, 135]、SMART [288]、MotionDiffuser [289] 和 CTG++ [290]。这些工作直接建模对自动驾驶影响最大的智能体行为，同时有意忽略不相关的场景细节，如路边的树木或光照变化。因此，它们提供了一个更直接、更高效的世界模型来提高自动驾驶性能。

另一部分研究侧重于构建高度精确的环境 3D 表示，旨在捕获尽可能多的空间细节以重建场景中的每个元素。代表性的工作是神经辐射场（Neural Radiance Fields, NeRF）[291] 及其衍生模型 [292, 293, 294, 295, 296]，旨在重建照片般逼真的 3D 环境。另一类是基于 3D 高斯泼溅（3D Gaussian Splatting, 3DGS）[297]，后续工作 [298, 299, 300] 改进了动态场景的实时渲染和几何保真度。然而，NeRF 和 3DGS 方法都倾向于提供有限的多样性，并且需要相当大的努力来生成与真实世界光照、天气条件和其他因素一致的多摄像头视频。泛化到全新的视角仍然具有挑战性。

尽管最近取得了进展，但世界模型在自动驾驶中的应用仍然面临一个显著的差距：许多现有工作仍然与传统的感知和运动预测范式紧密相连。这种方法通常继承了模块化流程的设计思维，或者采用了将感知、预测和控制集成到单一整体结构中的端到端公式。然而，按照世界模型的概念，对于两个关键挑战的研究仍然有限：(1) 如何将多模态感官输入转换为统一且兼容的潜在表示（latent representation），以及 (2) 如何有效利用这种潜在表示来指导决策和动作生成。正如前面所强调的，世界模型不仅仅是预测工具——它们是更广泛决策框架的组成部分。从这个角度来看，用于自动驾驶的世界模型的开发仍处于起步阶段。一个能够可靠处理自动驾驶复杂性、不确定性和实时需求的世界模型尚未实现。

下面简要概括上述论文：

- Navigation World Model
- A Path Towards Autonomous Machine Intelligence
- Mastering diverse control tasks through world models
- TD-MPC2: Scalable, Robust World Models for Continuous Control
- OccLLaMA: An Occupancy-Language-Action Generative World Model for Autonomous Driving
- DriveDreamer-2: LLM-Enhanced World Models for Diverse Driving Video Generation
- OccWorld: Learning a 3D Occupancy World Model for Autonomous Driving
- WorldDreamer: Towards General World Models for Video Generation via Predicting Masked Tokens

##### 未来方向

尽管这些新技术潜力巨大，但尚未建立起从根本上解决自动驾驶汽车安全性和可扩展性的明确技术路线图。在下文中，我们在新的生成式学习范式下概述了一些想法和建议。

**基于 LLM/VLM 构建的自动驾驶基础模型**：LLMs 和 VLMs 作为自然语言和视觉语言处理领域的生成模型已取得巨大成功，其基本原理与 AV 3.0 的目标高度契合。然而，自动驾驶超越了语言任务；它不仅需要上下文推理，还需要空间推理、动态感知等。因此，用于自动驾驶汽车的生成式基础模型不仅应利用 LLMs 或 VLMs，还应引入专门为驾驶任务设计的新模态。研究什么结构最适合建模驾驶行为，以及如何有效训练统一的视觉-语言-驾驶（Vision-Language-Driving）基础模型，仍然是未来探索的重要方向。

**物理编码的世界模型**：如果世界模型能够捕获跨驾驶场景的稳定关系，它们自然适合 AV 3.0 范式。然而，现有的世界模型对于安全关键应用仍然不足。为了有意义地增强 AV 系统的安全性和可扩展性，世界模型必须显式编码物理约束、动力学和环境交互。因此，研究如何将物理定律融入世界模型，或如何构建能够准确反映现实世界复杂性的高保真世界模型，是实现 AV 3.0 愿景的关键方向。

**面向驾驶任务的新型生成模型**：LLMs 和世界模型最初并非为驾驶任务而开发。因此，它们自然需要进一步适配，其在自动驾驶中的最终性能仍不确定。然而，驾驶任务本身具有许多独特特征——例如，需要理解空间-行为关系，以及交通规则和信号对行为的固有影响。使用通用的学习架构难以有效捕获这些关系。因此，AV 3.0 中一个有前景的方向是开发专门与驾驶任务性质对齐的新生成模型结构，使自动驾驶汽车能够进行更有针对性和高效的训练。

##### 对芯片需求的讨论

LLM 在智驾领域的应用，提高了智驾算法的能力，也对智驾芯片提出新的需求。

- **模型规模（参数量）：** 这是最核心的因素之一。更大的模型（如 GPT-4, Claude Opus）拥有数十亿甚至万亿参数，推理时需要从 UFS 中加载更多的权重数据到 DDR 中、进行更多的计算（尤其是矩阵乘法），显著增加内存访问和运算需求，导致推理速度变慢。

例如一个 70B 参数的 FP16 精度的模型，权重体积达 **140GB，生成每个 token** 需将 140GB 权重从显存搬运至计算单元。

**计算/访存比（O/I）失衡**

| 计算类型          | 计算量 (FLOPs/token) | 访存量 (Bytes) | 计算/访存比   |
| ----------------- | -------------------- | -------------- | ------------- |
| 矩阵乘法 (MatMul) | ~2 × 参数规模        | ~权重体积      | < 1 FLOP/Byte |
| LLM典型值         | 140 GFLOPs (70B模型) | 140GB          | 1:1           |
| GPU能力 (H100)    | 2000 TFLOPS          | 3 TB/s         | 666:1         |

- 模型架构
  - **注意力机制：** Transformer 中的自注意力计算复杂度通常是序列长度的平方（O(n²)）。处理长文本（长上下文）时，注意力计算成为主要瓶颈。
  - **层数/深度：** 模型层数越多，前向传播需要经过的计算步骤就越多，耗时越长。
    - **激活函数：** 某些复杂的激活函数计算成本更高。
    - **稀疏性/MoE：** 像 Mixture of Experts 这样的架构，理论上可以更快（只激活部分参数），但路由机制本身也有开销，并且对硬件和软件优化要求高。

- **序列长度（Context Length / Token Count）：**
  - **输入长度：** 需要处理的提示词越长，初始计算（尤其是注意力）开销越大。
  - **输出长度：** 生成式推理是**自回归**的，即逐个生成 token。每个新 token 的生成都需要基于之前所有已生成的token重新进行完整或部分的前向计算（取决于优化技术）。因此，**需要生成的响应长度（输出token数）是影响总推理时间的最直接因素之一**。

- **精度（Precision）：**
  - 模型权重和激活值通常以 FP32（单精度）、FP16（半精度）、BF16（脑浮点16）、INT8（8位整数）、INT4（4位整数）等格式存储和计算。
  - 使用更低的精度（如 INT8/INT4 量化）可以**显著减少内存占用和带宽需求，加速计算**（尤其是在支持低精度计算的硬件上），但可能会带来轻微的质量损失。FP16/BF16 通常是速度和精度的较好折衷。


#### 算法部署

深度神经网络（DNN）的计算特征主要包括以下 5 个方面：

- 计算量大且集中在卷积操作；
- 卷积参数量大，**带宽需求高**；
- 卷积参数存在高度复用的可能；
- 网络算法、结构不断变化，**可编程性要求高**；
- 网络本身具有精度容忍性，推理需要多种可变换精度的支持。

在 DNN 加速器设计中，除了算力，精度也是一个重要的考虑因素。加速器区分了训练用途加速器和推理用途加速器。训练用途加速器多采用单精度浮点数（FP32）格式存储特征图和权重；推理用途加速器则采用半精度浮点数（FP16）或更低精度的整型数（如 INT8, INT4 等）的数据格式存储权重和特征图。使用低精度进行推理的原因在于，神经网络本地能够容忍精度上的些许误差，通过基于预定义模型的重训练等操作，可以很大程度上恢复计算精度上的损失，同时获取硬件性能的大幅提升。

GPGPU 本就有大量高吞吐高带宽的存储设计，加上专为 DNN 设计的，能够大幅提升矩阵运算能力的张量核心（tensor core），而且还支持多种精度，能使 GPGPU 能够使用 DNN 不同场景、不同应用的精度需求。更为重要的是，GPGPU 本身的可编程性能够使用多种操作，为各种新增的卷积操作提供了支持。如 im2col 算法，将卷积操作转换为 GEMM，利用 tensor core 提高卷积操作的效率。

与之相比，目前各个厂商自研的 NPU **能够提供足够的算力**，满足 DNN 的“计算量大且集中在卷积操作”这一需求，同时由于网络本身具有精度容忍性，**推理用途加速器对浮点的要求没有那么高**，但不表示没有要求，下一小节可以看到浮点算力不足对智驾算法的影响。

但是带宽要求和可编程性限制了 NPU 的发展，使之面对新的算子时无法提供高能效的运算（不是跑不了，是速度、能耗不行）。orinx 使用的是 [Ampere 架构](https://images.nvidia.com/aem-dam/en-zz/Solutions/data-center/nvidia-ampere-architecture-whitepaper.pdf) GPGPU，设计为 80GB 显存，2039GB/s 带宽；thor-u 使用的 [Blackwell 架构](https://link.zhihu.com/?target=https%3A//resources.nvidia.com/en-us-blackwell-architecture)，设计为 192GB 显存，8TB/s 带宽（这些数据是 A100, B200 的数据，实际 orinx, thor-u 上的没有这个高，对应的带宽只有 204GB/s，273GB/s）。这是 NPU 很难达到的（xpeng 的 NPU 使用 32MB L2 缓存，24MB L1 缓存，设计总线带宽为 123GB/s）；在可编程性方面，NV 的 cuda 架构提供了便捷的 API 能够完成各种计算，而定制的 NPU 需要完成算子开发，编译器适配等各项工作，比在 cuda 上开发麻烦很多，可编程性不高。

传统大模型即LLM的参数一般在70亿至2000亿之间，而早期的CNN模型参数通常不到1000万，CNN模型目前大多做骨干网使用，参数飞速增加。特斯拉使用META的RegNet，参数为8400万，消耗运算资源很少，得分82.9也算不低；小米UniOcc使用META的ConvNeXt-B，参数8900万，消耗运算资源最少，得分83.8；华为RadOcc使用微软的Swin-B，参数8800万。

智驾领域的算法从过去基于CNN算法的多个不同任务的感知网络，向以Transformer为基础框架的BEV大模型演进。BEV作为新一代自动驾驶感知算法，在传统CNN加速芯片上部署难度极大甚至完全无法部署。Transformer模型对访存要求相对传统CNN算法会高出很多，**需要较高的存储带宽**，同时Transformer内的非线性层（这是什么）有非常高的精度要求，相应需要更多的浮点计算资源，而**绝大多数AI加速器都是定点整数计算资源**。其次Attention模块是一个matmul-softmax-matmul的结构，在序列长度比较大时，Reduce维度的计算对Vector（向量）计算资源要求非常多。此外BEV模型里Grid Sample算法里还有一些类似聚合、分散的特殊算子。所有这些计算需求在传统AI芯片的硬件上难以满足，需要近似CPU的运算资源。

transformer 结构如下，其详细运算过程可以看[这篇文章](https://juejin.cn/post/7152002993204756487#heading-9)。

![transformer-第 2 页.drawio](https://p3-juejin.byteimg.com/tos-cn-i-k3u1fbpfcp/79ffaec1d4144ff095dfc9d3e29aa838~tplv-k3u1fbpfcp-zoom-in-crop-mark:1512:0:0:0.awebp)

DSA非常简单，典型代表就是谷歌的TPU，近来针对AI运算的DSA高度雷同，也没什么技术门槛。简单地说就是在NPU的MAC阵列上加入了标量运算和向量运算单元。

- 标量算力，主要用于逻辑控制，任务调度；
- 向量算力，主要用于激活、池化、排序等计算；
- 矩阵算力，主要是矩阵乘法，用于卷积，全连接等计算；

随着算力专有程度的提高，其算力通用性也会降低，算力的可编程性变差。打个比方，我们分别把矩阵算力、向量算力和标量算力，类比作飞机，高铁和汽车三种交通工具。飞机，速度最快，但乘坐成本最高，且只能往来于特定的几个机场站点；高铁，速度相对较快，准备工作相对较少，高铁站的数量也相对较多；汽车，速度相较最慢，但便捷性最高，也无需额外的准备工作。标量算力是最通用的算力，可以从功能上覆盖向量计算和矩阵计算，且理论上可以覆盖几乎所有的计算需求；同理，向量计算也可以覆盖矩阵计算的功能；最后，矩阵计算，其算力专有程度最高，只能用于矩阵计算。但随着算力专有程度变高，越容易堆叠算力，算力的能效比也越高。由于矩阵算力的可编程性较差，需要借助算子库或者DSL（Domain Specific Language）才能把矩阵算力很好的利用起来。**向量算力用于保证DSA的可编程性和兼容性，如TPU、NPU中的Vector Unit，GPGPU中的CUDA core；标量算力则主要用于逻辑控制。**GPGPU 中的 Tensor core 用来做矩阵运算。

![图片7.png](https://db.shujubang.com/uploads/ueditor/image/20240911/1726045989199605.png)

Roofline Model可以看出平均带宽需求和峰值计算能力如天花板一样是整个系统计算的能力上限，以计算强度上限Imax为界，划分出AI芯片的两个瓶颈区域，即图中橘色的内存受限区（Memory Bound）和图中蓝色的计算受限区（Compute Bound）。存储决定了下限，计算决定了上限。因为 Decoding 阶段 Token 逐个处理，使用 KV Cache 之后， **Multi-Head Attention 里的矩阵乘矩阵操作全部降级为矩阵乘向量即GEMV**。除此之外，Transformer 模型中的另一个关键组件 FFN 中主要也**包含两个矩阵乘法操作**，但Token之间不会交叉融合，也就是任何一个Token都可以独立计算，因此在Decoding阶段不用Cache之前的结果，但同样会出现矩阵乘矩阵操作降级为矩阵乘向量。Prefill阶段则是GEMM，矩阵与矩阵的乘法。**GEMV是访存密集型操作，性能完全取决于存储带宽**。

当前的主流 LLM 基本都是Decoder Only的Transformer模型，其推理过程可分为两个阶段：

![1.png](https://db.shujubang.com/uploads/ueditor/image/20240617/1718608069735070.png)

- Prefill：根据输入Tokens(i, like, natural, language) 生成第一个输出 Token(A)，通过一次Forward就可以完成，在Forward中，输入Tokens间可以并行执行(类似 Bert这些Encoder模型)，因此执行效率很高。

- Decoding：从生成第一个Token(A)之后开始，采用自回归方式一次生成一个Token，直到生成一个特殊的Stop Token(或者满足用户的某个条件，比如超过特定长度)才会结束。假设输出总共有N个Token，则Decoding阶段需要执行N-1次Forward，这N-1次Forward 只能串行执行，效率很低。另外，在生成过程中，需要关注的Token越来越多(每个Token 的生成都需要Attention之前的Token)，计算量也会适当增大。

![2.png](https://db.shujubang.com/uploads/ueditor/image/20240617/1718608095116597.png)

在车载自动驾驶应用场合，序列长度基本可等同于摄像头的像素数量和激光雷达的点云密度。

![3.png](https://db.shujubang.com/uploads/ueditor/image/20240617/1718608129686194.png)

在 LLM 推理中最关键的就是上图中的Multi-Head Attention(MHA)，**其主要的计算集中在左图中灰色的 Linear(矩阵乘)和Scaled Dot-Product Attention中的MatMul 矩阵乘法**。

图中的Mask是一个下三角矩阵，也是因为这个下三角矩阵实现了LLM Decoder的主要特性，每个Token都只能看到当前位置及之前的Token。其中的QKV可以理解为一个相关性矩阵，4个Token对应4 个Step，其中：

- Step 2依赖Step 1的结果，相关性矩阵的第1行不用重复计算。
- Step 3依赖Step 1和Step 2的结果，相关性矩阵的第1行和第2行不用重复计算。
- Step 4依赖Step 1、Step 2和Step 3的结果，相关性矩阵的第1行、第2行和第3行不用重复计算。

在Decoding阶段Token是逐个生成的，上述的计算过程中每次都会依赖之前的结果，换句话说这是串行计算，而非GPU擅长的并行计算，GPU大部分时候都在等待数据搬运。**加速的办法是计算当前Token时直接从KV Cache中读取而不是重新计算**。对于通用LLM，应用场景是要考虑多个并发客户使用，即Batch Size远大于1，KV Cache的缓存量会随着Batch Size暴增，但在车里用户只有一个，就是自动驾驶端到端大模型，即Batch Size为1。

因为Decoding阶段Token逐个处理，使用KV Cache之后，上面介绍的Multi-Head Attention 里的矩阵乘矩阵操作全部降级为矩阵乘向量即GEMV。此外，Transformer模型中的另一个关键组件FFN 中主要也包含两个矩阵乘法操作，但 Token之间不会交叉融合，也就是任何一个Token都可以独立计算，因此在Decoding阶段不用Cache之前的结果，但同样会出现矩阵乘矩阵操作降级为矩阵乘向量。Prefill阶段则是GEMM，矩阵与矩阵的乘法。 前面介绍的Multi-Head Attention 里的矩阵乘矩阵操作全部降级为矩阵乘向量即GEMV。

**矩阵乘向量操作是明显的访存bound**，而以上操作是LLM推理中最主要的部分，这也就导致LLM推理是访存bound类型。

![4.png](https://db.shujubang.com/uploads/ueditor/image/20240617/1718608233137156.png)

上图是三星对GPT大模型workload分析。在运算操作数量上，GEMV所占的比例高达86.53%；在大模型运算延迟分析上，82.27%的延迟都来自GEMV，GEMM所占只有2.12%，非线性运算也就是神经元激活部分占的比例也远高于GEMM。

![5.png](https://db.shujubang.com/uploads/ueditor/image/20240617/1718608285250682.png)

上图是三星对GPU利用率的分析，可以看出在GEMV算子时，GPU的利用率很低，一般不超过20%，换句话说80%的时间GPU都是在等待存储数据的搬运。还有如矩阵反转，严格地说没有任何运算，只是存储行列对调，完全是存储器和CPU在忙活。解决办法很简单且只有一个，就是用HBM高宽带内存。

与传统LLM最大不同就是车载的Batch Size是1，导致GPU运算效率暴跌，传统LLM的Batch Size通常远大于1，这让GPU效率增加。

![6.png](https://db.shujubang.com/uploads/ueditor/image/20240617/1718608330511867.png)

图上不难看出，Batch Size越大，推理速度反而越快，但KV Cache容量会暴增;车载的Batch Size是1，推理速度反而很慢，好处是根本不用考虑KV Cache的容量。

最终我们可以得出结论，存储带宽决定了推理计算速度的上限。假设一个大模型参数为70亿，按照车载的INT8精度，它所占的存储是7GB，如果是英伟达的RTX4090，它的显存带宽是1008GB/s，也就是每7毫秒生成一个token，这个就是RTX4090的理论速度上限。特斯拉第一代FSD芯片的存储带宽是63.5GB/s，即每110毫秒生成一个token，帧率不到10Hz，自动驾驶领域一般图像帧率是30Hz；英伟达的Orin存储带宽是204.5GB/s，即每34毫秒生成一个token，勉强可以达到30Hz，注意这只是计算的数据搬运所需要的时间，数据计算的时间都完全忽略了，实际速度要远低于这个数据。并且一个token也不够用，至少需要两个token，端到端的最终输出结果用语言描述就是一段轨迹，比如直行，直行需要有个限制条件，至少有个速度的限制条件，多的可能需要5个以上token，简单计算即可得出存储带宽需要1TB/s以上。

实际情况远比这个复杂的多。车载领域不是传统LLM使用CPU和GPU分离形式，车载领域的计算SoC都是将CPU和AI运算部分合二为一，AI运算部分通常是GPU或加速器是和CPU共享内存的。而在非车载领域，GPU或AI运算部分有独立的存储，即显存。车载领域共享内存一般是LPDDR，它主要是为CPU设计的，注重速度即频率而非带宽。不像显存，一般是GDDR或HBM，注重带宽，不看重频率高低。上述所有理论都是基于显存的，在车载领域共享LPDDR，其性能远远低于单独配置的显存，无论是速度还是容量，共享存储都必须远比单独的显存要高才能做到大模型推理计算。

理想用英伟达Orin做了测试，纯端到端模式延迟高达1.5秒。

![7.png](https://db.shujubang.com/uploads/ueditor/image/20240617/1718608376596051.png)

所以车载领域存储比算力重要很多，最好的解决办法是HBM，但太贵了，32GB HBM2最低成本也得2000美元，汽车领域对价格还是比较敏感的，退而求其次，就是GDDR了。GDDR6的成本远低于HBM，32GB GDDR6大概只要180美元或更低。

![8.png](https://db.shujubang.com/uploads/ueditor/image/20240617/1718608468133790.png)

基本上GDDR6的理论上限就是672GB/s，特斯拉第二代FSD芯片就支持第一代GDDR6，HW4.0上的GDDR6容量为32GB，型号为MT61M512M32KPA-14，频率1750MHz(LPDDR5最低也是3200MHz之上)，是第一代GDDR6，速度较低。即使用了GDDR6，要流畅运行百亿级别的大模型，还是无法实现，不过已经是目前最好的了。

大模型时代，Attention Is All You Need，同样大模型时代 Bandwidth Is All You Need。

#### 各厂商发展现状

##### xpeng

小鹏是国内智驾的代表企业之一，在自研芯片高算力的加持下，进入到 AV3.0 阶段，即能够**利用 LLM/VLM/MLLM 增强 AV 系统。**

![adas-580](D:\gitlab\ADAS\adas-580.png)

- Map Manager 模块负责构建实时地图的几何信息。其输入源有两个：Perception 和 HD Lite。

  - 在大部分区域基于感知的 olm-perception 即可满足下游的需求，但是对于一些复杂场景（大路口），需要离线制作好的轻量级地图辅助，以拓宽 Perception 的边界，保证 XNGP 体验的延续性。

  - OLM-HD Lite Fusion 模块主要负责：
    - 快速匹配对齐 olm-perception 和 HD Lite；
    - 将 HD Lite 的 elements 融合到 olm-perception，构造增强版的 olm-hd lite 并通过统一的接口（OLM Topic）发送给下游，达到 HD Lite 对下游完全透明、无感的效果。

- Sensor Fusion：功能是把相机识别到的障碍物的信息和特征，就是环境的信息和同毫米波雷达，结合起来，提供给下游的算法去做进一步分析；

- OCR(Optical Character Recognition)：路面复杂指示牌信息理解，提前并识别路面指示牌上面的文字信息。
- AEB（）：主动安全（rule-based）；
- Parking：泊车算法；
- Xplanner：planning与prediction网络的合并，基于神经网络的规划大模型。XPlanner 就像人类的小脑，通过海量数据时刻训练，使得驾驶策略不断向拟人进化，拥有“老司机般的脚法”，使得前后顿挫减少50%、违停卡死减少40%、安全接管减少60%，让用户舒适性、安全性体验都再上新台阶。
- Dynamic Xnet + Static Xnet + Occupancy：三网合一的感知大模型，通过聚合动态XNet、静态XNet和行业首个量产纯视觉2K占用网络，能够让自动驾驶系统如同裸眼3D。行业首个量产2K纯视觉占用网络，用超过200万个网格重构世界，对现实世界中的可通行空间进行3D高真实度还原，清晰识别静态障碍物的每一个细节，使得感知范围提升2倍，面积可达1.8个足球场大小，能精准识别50+个目标物，让用户如同拥有鹰眼视觉，驾驶时看得更清、更远。
  - Dynamic xnet，occupancy完成petr的架构升级；
  - Static xnet完成maptr架构升级，网络输出变为Vectorized Map改造，并打通下游；
  - 完成三网合一；
- XBrain：语言模型，在XBrain的加持下，ADAS 能够认识待转区、潮汐车道、特殊车道、路牌文字，秒懂各种令行禁止、快慢缓急的行为指令，进而做出兼顾安全、性能的拟人驾驶决策；
- ViT Encoder+LLM/Decoder：**首次引入 MLLM 方案**，使用的是量化后的开源 [InternVL2.5 MLLM 模型](https://huggingface.co/OpenGVLab/InternVL2_5-1B)；
- 2D Models：暂不知用途。

从架构图可知，小鹏最新的智驾方案位于 AV3.0 阶段的，即**利用 LLM/VLM/MLLM 增强 AV 系统**，MLLM 受限于芯片算力和带宽，只能以 6Hz 的频率输出轨迹信息，。

该方案和学界的最新研究成果，如 V2X-VLM 等类似，旨在通过整合车载摄像头、路侧传感器的多模态数据及文本信息，提升态势感知、决策能力和最终的轨迹规划水平，**但不做最后的规控**。

该方案预计 7 月低发布公测，8 月底全量发布。

小鹏世界基座模型负责人刘先明博士在 CVPR2025 会议上发表了题为《通过大规模基础模型实现自动驾驶的规模化》（Scaling up Autonomous Driving via Large Foudation Models）的演讲，系统地介绍了小鹏汽车自研的业界首个超大规模自动驾驶基座模型的历程和方法。本次分享的核心就是：**扩展法则（Scaling Law）在自动驾驶领域是有效的**。

小鹏的 LLM 上车分为三步：

**利用 Scaling Law 提升模型能力**

小鹏汽车在云上训练了10亿、30亿、70亿、720亿等多个参数的模型，并且持续向模型“投喂”更大规模的训练数据。目前，小鹏世界基座模型累计“吃下”2000多万条视频片段（每条时长30秒）。在这一过程中，研发团队清晰地看到了规模法则（Scaling Law）的显现。也就是说，模型的参数量越大、模型学习的数据越多，模型的性能越强。这是AI大模型浪潮以来，行业内首次明确验证规模法则在自动驾驶VLA模型上持续生效。

![img](https://xps01.xiaopeng.com/cms/content/pic/2025/06-20/16-29-5504921538604819.png)

实验数据显示，当模型参数量从10亿增加到720亿，以及训练视频片段数量从100万增加到3000万时，衡量轨迹预测精准度的关键指标——纵向平均位移误差(Long ADE)——呈现出一条近乎完美的、持续下降的曲线。这意味着模型预测未来轨迹的精准度，确实随着其规模增加而系统性提升。

**利用 CoT 和 RL 增加模型的驾驶能力**

- **用 CoT，让模型学会分步骤思考**

  在模型训练中，先通过大规模图文数据建立基础语义理解；接着在监督微调（SFT）阶段引入CoT机制，让模型在做出关键决策前生成一段推理链，包括环境描述、逻辑分析和动作规划，提升可解释性和鲁棒性。

- **用 RL，让模型在试错中进化**

  为了处理自动驾驶中的长尾问题，小鹏采用 RL 在虚拟环境（应该是世界模型）中反复训练。

**模型上车**

由于车端算力限制，最终能够部署上车的模型，参数规模通常都比较小。小鹏的解决方案是在云端“不计成本”地训练超大规模世界基座模型，再通过蒸馏的方式生产出适配车端算力的小模型。

总结：蒸馏出的模型效果未知，当前智驾方案使用的是量化后的开源 [InternVL2.5 MLLM 模型](https://huggingface.co/OpenGVLab/InternVL2_5-1B)。

最新进展，小鹏在使用 MLLM 做轨迹优化，需要关注 MLLM 的结构，怎样使用这个 MLLM 去优化轨迹的。以及模型部署进度，模型效率，和上一代模型端到端方案的差异。

小鹏的历史发展（早期算法是怎样的，对激光雷达的看法）

算法现状，static-xnet + dynamic - xnet + pnp。

目前的XBrain 由XNet2.0 和XPlanner 等组成。XNet2.0 采用三网合一的创新架构，包括动静态BEV 网络和最占据网络，使XNGP 感知系统在处理遮挡、光照不清和复杂路口等场景时展现出强大的能力。XPlanner 采用长时序规划、多对象决策和强推理，提高了规划和决策的综合性。

具体分析，xpeng 的智驾算法，芯片现状如下：

- xpeng 现在部署的智驾模型不是 LLM。

  依旧是在传统 perception+sensor fusion+prediction+planning 的基础上的迭代优化。如采用更好的预测算法，QCnet，更好的感知算法 stream petr 等。当然这些算法都使用 transformer，但和 LLM, VLA 等大模型是两个东西。

  目前小鹏内部有 3 个以上的团队在跟进 VLA 方案，但从目前的实验来看，**效果不如传统的这套方案**。

  另外有一点，小鹏计划在 4 块图灵芯片级联的车上，使用 1 块图灵芯片部署 LLM，用于座舱，而不是智驾。

- npu 对哪些模型支持的不好，为什么对这些模型支持的不好；

  - NPU 不支持 FP32 精度，FP32->FP16 低精度量化后的 Xplanner & Prednet 模型（xplanner 为规划模型，prednet 为预测模型）跑在 NPU 上的效果十分不好（精度丢失导致轨迹蛇形化）；

  - cudaGetValidVoxel，模型需要使用的一个算子，计算逻辑和输入输出取数都能够在 DSP（外购一个支持浮点运算的 IP，和 NPU 集成在一起，用来支持 NPU 无法完成的浮点运算），性能也会比较 OK。但是**只能用浮点指令完成计算，能否使用 16 位定点化计算优化需要进一步分析输入输出的数据特征**。

  - Occlusion 算子主要用来检测 camera 是否有遮挡，避免因图像丢失有效内容影响自动驾驶控制，在各个 IP 上运算性能如下，

    | operator                             | NV MX450 GPU | XP5 GPU (mali 12core) | XP5 Dsp 单核 |
    | :----------------------------------- | :----------- | :-------------------- | :----------- |
    | **ProcParkingFrame()** (整个pipline) | 4.844 ms     | 10.684 ms             | 13.004 ms    |

  - 还有 resize 算法，softmax 算子等，都存在精度丢失问题。

##### 地平线

##### 理想

理想使用 Thor-U 部署 VLA，公开的模型是 MindVLA。下面分析 MindVLA 的技术要点。

2025年3月18日，理想汽车自动驾驶技术研发负责人贾鹏在NVIDIA GTC 2025发表主题演讲《VLA：迈向自动驾驶物理智能体的关键一步》，分享了理想汽车对于下一代自动驾驶技术MindVLA的最新思考和进展。

![文章内容](https://media.licdn.com/dms/image/v2/D5612AQFpqvOOvNzlIg/article-inline_image-shrink_1000_1488/B56ZWtI5lqHQAQ-/0/1742366547219?e=2147483647&v=beta&t=AVxgOEYy5Wa3t68Aqqksm70PxVKXfVGZMIjM9vSGbdg)

对 MindVLA 方案做一个总结就是：3D空间编码器通过语言模型，和逻辑推理结合在一起后，给出合理的驾驶决策，并输出一组Action Token（动作词元），Action Token指的是对周围环境和自车驾驶行为的编码，并通过Diffusion（扩散模型）进一步优化出最佳的驾驶轨迹，整个推理过程都要发生在车端，并且要做到实时运行。

具体来看，**利用Diffusion将Action Token解码成优化的轨迹**，并通过自车行为生成和他车轨迹预测的联合建模，提升在复杂交通环境中的博弈能力。同时Diffusion可以根据外部条件，例如风格指令，动态调整生成结果。为了解决Diffusion模型效率低的问题，MindVLA采用Ordinary Differential Equation（常微分方程）采样器，实现了2-3步就能完成高质量轨迹的生成。面对部分长尾场景，理想建立起人类偏好数据集，并且**创新性地应用RLHF（基于人类反馈的强化学习）微调模型的采样过程**，最终使MindVLA能够学习和对齐人类驾驶行为，显著提升安全下限。

![文章内容](https://media.licdn.com/dms/image/v2/D5612AQHqSqNev-3--Q/article-inline_image-shrink_1000_1488/B56ZWtI.eeHQAQ-/0/1742366567062?e=2147483647&v=beta&t=MhqEPRSyr6wJ3fqex_oq9YvtI88uCb52qA1kq82_U0c)

从0开始设计和训练了适合MindVLA的LLM基座模型，采用**MoE混合专家架构**，引入Sparse Attention（稀疏注意力），实现模型稀疏化，保证模型规模增长的同时，不降低端侧的推理效率。从展示的架构图中可以看出，MindGPT 中存在多个 Expert，由 Router 决定哪些 token 被发送到哪些 Expert 进行处理。这些 token 包括用户语音信息、交通规则信息、camera, Lidar 传感器信息等。

![文章内容](https://media.licdn.com/dms/image/v2/D5612AQGBB9hQc4pP9A/article-inline_image-shrink_1500_2232/B56ZWtJDT7GUAY-/0/1742366586898?e=2147483647&v=beta&t=8WhyzGiJbRIkZYDm8oeGuSqH_NAFFTxw9CIsiEYmCSg)

MindVLA基于自研的重建+生成云端统一世界模型，深度融合重建模型的三维场景还原能力与生成模型的新视角补全，以及未见视角预测能力，构建接近真实世界的仿真环境。源于世界模型的技术积累与充足计算资源的支撑，MindVLA实现了基于仿真环境的大规模闭环强化学习，即真正意义上的从“错误中学习”。过去一年，理想自动驾驶团队完成了世界模型大量的工程优化，显著提升了场景重建与生成的质量和效率，其中一项工作是将3D GS的训练速度提升至7倍以上。

前面介绍过，LLM 上车最大的问题是自驾芯片的算力和带宽，Thor-U 的带宽相比于 Orin-X 提升不是那么明显，为 273GB/s，如果采用的 MindGPT 是一个 7B 的精度为 FP16 模型，那么每次将将 7B 参数从 UFS 搬运到 DDR，需要大约 47ms，帧率能够达到 21Hz，勉强达到智驾的标准。但是 Thor-U 中 GPGPU 是和其他组件共用总线的，所以带宽无法全部给到 GPGPU 的，帧率可能得打个 6 折，那就达不到智驾得标准。那么如何保证车端推理速度？

- 系统自主切换快慢思考
  - 学习人类思考过程，**慢思考时，使用一个简短的CoT模板；快思考时，直接输出action token。这一点和理想上一代 DriveVLM 的快慢双系统如出一辙。**


- 推理速度优化
  - 针对 COT，采用小词表和投机推理，大幅提升 COT 效率；
  - 针对 action token 推理，采用了创新性的并行解码方法，语言逻辑的推理通过因果注意力逐字输出，而 action token 采用双向注意力机制，一次性全部输出。

MindVLA 的介绍中没有提及怎样使用 LiDAR 的数据，我们从理想汽车过往发表的论文来研究其怎样进行多传感器的感知融合。

首先，现有感知融合策略可分为三级：**数据级、特征级和 BEV 级融合**。

- 数据级融合方法**通过相机投影矩阵将激光雷达点云投影到图像**，投影的稀疏深度图可与图像数据共同输入网络，或与图像语义特征结合以增强输入；

- 特征级融合方法**在特征空间通过 Transformer 融合不同模态**：先生成激光雷达特征图，再通过交叉注意力在激光雷达特征上查询图像特征，最后拼接用于下游任务；

- BEV 级融合方法分别提取激光雷达和图像的 BEV 特征，再通过拼接或融合模块进行融合。例如，HDMapNet使用 MLP 将相机分支的 PV 特征映射到 BEV 特征，激光雷达分支使用 PointPillars 编码 BEV 特征；近期 BEVFusion 工作在相机分支使用 LSS 进行视图变换，激光雷达分支使用 VoxelNet，最终通过 BEV 对齐模块融合。

FUTR3D 是2023 年理想汽车参与复旦大学、卡内基梅隆大学、清华大学、麻省理工学院合作提出的一个端到端 3D 检测框架（3D 检测融合 Transformer），可适配任何传感器组合与配置，如相机 - 激光雷达融合、相机 - 雷达融合、相机 - 激光雷达 - 雷达融合等。FUTR3D 首先对每个模态独立编码特征，然后使用基于查询的模态无关特征采样器（MAFS）在统一空间中从不同模态提取特征，最后通过 Transformer 解码器对一组 3D 查询进行操作，实现对象的集合预测。

![image-20250701211832471](C:\Users\xpeng\AppData\Roaming\Typora\typora-user-images\image-20250701211832471.png)

FUTR3D 在概念上可分为四部分：首先，**不同传感器模态的数据由各自的模态特定特征编码器编码**；然后，基于查询的模态无关特征采样器（MAFS）根据查询的初始位置从所有模态采样并聚合特征；接着，共享的 Transformer 解码器头使用迭代优化模块基于融合特征优化边界框预测；最后，损失函数基于预测与真实值之间的集合到集合匹配。

FUTR3D 属于特征级融合的技术。

另一方面，HDMapNet 是 2022 年和清华大学合作提出的一种语义地图学习方法。该方法从周围相机的图像和 / 或激光雷达等点云中生成矢量化的地图元素。

![image-20250701212745198](C:\Users\xpeng\AppData\Roaming\Typora\typora-user-images\image-20250701212745198.png)

该方案中分为三个核心模块：

- Image encoder

  - Perspective view image encoder：将输入的每个图像通过共享神经网络得到透视视图特征图。
  - Neural view transformer：首先将图像特征从透视视图转换到相机坐标系，再转换到 BEV 特征图。

- Point Cloud encoder

  Point Cloud encoder 是 PointPillar 的变体，采用动态体素化(dynamic voxelization)，将 3D 空间划分为多个柱，并从柱的点云特征中学习特征图。

- BEV decoder

  - BEV decoder 是一个全卷积网络（FCN），包含三个分支：语义分割分支、实例嵌入分支和方向预测分支。输入为图像特征图 BEV_I 或点云特征图 BEV_F，若两者都存在则进行拼接。

HDMapNet 采用了明显的 BEV 级的图像和激光雷达数据融合方式。

##### 蔚来

智驾算法

蔚来的自动驾驶系统核心在于其全栈神经网络模型，它整合了感知与规控两大环节。）感知模块，运用了统一的融合感知架构（占用网络2.0）。**该架构能同时处理动态和静态目标，并利用可变分辨率机制有效应对不同距离目标的识别挑战。云端部署的大型无监督模型为车载感知提供辅助训练支持。2）规控模块采用分层数据驱动策略。**首先，一个多模态注意力网络分析感知数据，筛选出数十至上百种潜在未来场景。接着，系统对这些场景进行深度推演，通过交互搜索树模拟未来7秒的发展，并借助基于人类偏好数据训练的博弈价值网络评估最优路径。最终的行为规划与控制由奖励函数网络执行，旨在生成最拟人化、舒适的操控指令。为确保绝对安全，所有输出结果都会经过凸优化空间的最终验证。这种深度融合的感知-决策模型是蔚来实现通用自动驾驶能力的基础。

 

| 图表1：**蔚来智驾算法架构**                                  |
| ------------------------------------------------------------ |
| ![img](file:///C:\Users\xpeng\AppData\Local\Temp\ksohtml18272\wps1.jpg) |
| 资料来源：蔚来汽车官网，华源证券研究所                       |

 

蔚来构建的NWM(NIO World Model)具备对信息的全景理解力，在想象的维度理解物理规律，并重建世界。通过构建人类大脑的复杂认知过程，模拟人类感知和决策过程，为自动驾驶系统提供预测和适应动态环境的能力。

该模型可以在100毫秒内，推演216种可能发生的轨迹、寻找最优路径。底层应该有规则代码，用来判断这些路径中哪些概率最高，和规控算法计算出来的轨迹最相似，优化最终的轨迹路线。

 

| 图表2：**蔚来NWM架构**                                       |
| ------------------------------------------------------------ |
| ![img](file:///C:\Users\xpeng\AppData\Local\Temp\ksohtml18272\wps2.jpg) |
| 资料来源：蔚来汽车官网，华源证券研究所                       |

##### 华为

**智驾算法**

GOD 3.0网络（General Obstacle Detection）：融合激光雷达点云与视觉语义信息，可识别140+类异形障碍物（如倾倒的快递车、动物尸体）；

BEV+Occupancy联合建模：鸟瞰视角与体素占用网络结合，解决传统BEV的垂直空间盲区。

决策规划层：DriveMind大模型，该模型工作流程如下：

![image-20250626112550889](C:\Users\xpeng\AppData\Roaming\Typora\typora-user-images\image-20250626112550889.png)

DriveMind 将两个 VLM(Static VLM, Dynamic VLM)、车辆状态反馈和预测世界模型集成到端到端自动驾驶的自调整奖励框架中。Static VLM 将每个 BEV 连续编码为稳定的 semantic embedding，同时 Novelty Detection 通过滑动窗口监控偏差，然后 Novelty Detection 必要时会触发 Dynamic VLM，生成上下文特定的 “present” 和 “ideal”  prompt。这个“必要时”是指当前场景明显偏离了过去的经验。之后“present” 和 “ideal”  prompt 会输入给 Reward Combiner。

Reward Combiner 框架有三个子模块：（1）Adaptive Contrastive Semantic Reward（自适应对比语义奖励），该模块利用输入的 “present” 和 “ideal”  prompt 在每个时间间隔都生成对应的语言描述。这些语言描述又被 embedding 到一个静态对比空间，这使得 DriveMind 能够测量当前 visual embedding 与每个上下文特定概念的对齐程度。该模块能够使得 Static VLM 输出的轨迹向 "ideal" prompt 靠近；（2）Hierarchical Vehicle-State Fusion Reward（分层车辆状态融合奖励），该模块通过将速度调节、车道居中、航向对齐和横向稳定性四个归一化指标融合为单个乘法项，来让实际驾驶符合运动学安全；（3）Predictive Contrastive Foresight Module（预测对比前瞻模块），该模块是一个小的 WM，用来从当前 embedding 和轨迹预测下一步 visual embedding，优化当前生成的轨迹；

**[智驾芯片](https://www.nextplatform.com/2024/08/13/huaweis-hisilicon-can-compete-with-nvidia-gpus-in-china/)**

MDC 1000：基于昇腾 910B 芯片，算力达 1000TOPS，支持16路摄像头 + 3 颗激光雷达实时处理，同时采用双 MDC 1000 互为热备的冗余设计。

猜测 MDC 1000 是基于  Ascend 910B 同样的架构设计的，其中的关系可以这样类比：NV 提出了 Ampere 架构，在此基础上设计了训练用的 A100 芯片，以及智驾芯片 Orin-X。

目前无从知晓 MDC 1000 的芯片架构以及关键的带宽信息，不过我们可以从 Ascend 910 系列和 NV A100 等系列芯片的对比，来对华为当前智驾芯片处于哪个层次有个初略的了解。

![img](https://www.nextplatform.com/wp-content/uploads/2024/08/nvidia-vs-huawei-gpu-salients.jpg)

总结：如果 MDC 1000 和 Ascend910B 同样采用 HBM 的内存，理论带宽为 1600GB/s，那么带宽将远超 Thor-U 的 273GB/s，可运行的 LLM 参数理论最大值为 1600GB/s / 30Hz = 53GB/s，也就是 57B INT8 规模的 LLM。但这样会导致芯片成本升高。

**传感器配置**

激光雷达：3颗华为自研192线激光雷达（前向+双侧），探测距离提升至300m（@10%反射率），垂直视场角扩展至28°（应对高架桥、限高杆）；

摄像头：11颗800万像素高清摄像头（含2颗双目广角），支持120°超广角覆盖；

4D毫米波雷达：6颗华为毫米波雷达Pro，新增垂直分辨率（32通道），可识别低矮障碍物（如倒地锥桶）；

超声波雷达：12颗，支持全向厘米级避障。

### Robotaxi

#### Waymo

#### Ponyai

#### 文远知行

### 总结

国内智驾芯片厂商基本采用 NPU 架构，**定点算力很高，但对浮点的支持不足**，所以对于当前智驾算法发展火热的 LLM, VLM, VLA 等大模型，支持的不如 NV thor-u 好。

#### CNN 与 transform

CNN时代AI加速器一般叫NPU，大模型transformer时代则多叫DSA。智驾领域的算法从过去基于CNN算法的多个不同任务的感知网络向以Transformer为基础框架的BEV大模型演进。BEV作为新一代自动驾驶感知算法，在传统CNN加速芯片上部署难度极大甚至完全无法部署。**Transformer模型对访存要求相对传统CNN算法会高出很多，需要较高的存储带宽，同时Transformer内的非线性层有非常高的精度要求，相应需要更多的浮点计算资源，而绝大多数AI加速器都是定点整数计算资源。**其次Attention模块是一个matmul-softmax-matmul的结构，在序列长度比较大时，Reduce维度的计算对Vector（向量）计算资源要求非常多。此外BEV模型里Grid Sample算法里还有一些类似聚合、分散的特殊算子。所有这些计算需求在传统AI芯片的硬件上难以满足，需要近似CPU的运算资源。

DSA非常简单，典型代表就是谷歌的TPU，近来针对AI运算的DSA高度雷同，也没什么技术门槛。简单地说就是在NPU的MAC阵列上加入了标量运算和向量运算单元。

- 标量算力，主要用于逻辑控制，任务调度。

- 向量算力，主要用于激活、池化、排序等计算。

- 矩阵算力，主要是矩阵乘法，用于卷积，全连接等计算。

随着算力专有程度的提高，其算力通用性也会降低，算力的可编程性变差。打个比方，我们分别把矩阵算力、向量算力和标量算力，类比作飞机，高铁和汽车三种交通工具。飞机，速度最快，但乘坐成本最高，且只能往来于特定的几个机场站点；高铁，速度相对较快，准备工作相对较少，高铁站的数量也相对较多；汽车，速度相较最慢，但便捷性最高，也无需额外的准备工作。标量算力是最通用的算力，可以从功能上覆盖向量计算和矩阵计算，且理论上可以覆盖几乎所有的计算需求；同理，向量计算也可以覆盖矩阵计算的功能；最后，矩阵计算，其算力专有程度最高，只能用于矩阵计算。但随着算力专有程度变高，越容易堆叠算力，算力的能效比也越高。由于矩阵算力的可编程性较差，需要借助算子库或者DSL（Domain Specific Language）才能把矩阵算力很好的利用起来。向量算力用于保证DSA的可编程性和兼容性，如TPU、NPU中的Vector Unit，GPGPU中的CUDA core；标量算力则主要用于逻辑控制。

存储决定了下限，计算决定了上限。因为 Decoding 阶段 Token 逐个处理，使用 KV Cache 之后， Multi-Head Attention 里的矩阵乘矩阵操作全部降级为矩阵乘向量即GEMV。除此之外，Transformer 模型中的另一个关键组件 FFN 中主要也包含两个矩阵乘法操作，但Token之间不会交叉融合，也就是任何一个Token都可以独立计算，因此在Decoding阶段不用Cache之前的结果，但同样会出现矩阵乘矩阵操作降级为矩阵乘向量。Prefill阶段则是GEMM，矩阵与矩阵的乘法。GEMV是访存密集型操作，性能完全取决于存储带宽。

支持大模型参数数量由存储带宽和外在的存储容量决定，假设一个大模型参数为300亿，按照车载的INT8精度，它所占的存储是30GB，如果是英伟达的RTX4090，它的显存带宽是1008GB/s，也就是每30毫秒生成一个token，这个就是RTX4090的理论速度上限。特斯拉第一代FSD芯片的存储带宽是63.5GB/s，也就是每471毫秒生成一个token，帧率不到3Hz，自动驾驶领域一般图像帧率是30Hz，而英伟达的Orin存储带宽是204.5GB/s，即每146毫秒生成一个token，勉强可以达到7Hz，注意这只是计算的数据搬运所需要的时间，数据计算的时间都完全忽略了，要做到30Hz，带宽至少要提高5倍，也就是1TB/s。实际情况远比这个复杂的多，车载领域不是传统LLM使用CPU和GPU分离形式，车载领域的计算SoC都是将CPU和AI运算部分合二为一，AI运算部分通常是GPU或加速器，是和CPU共享内存的，而在非车载领域，GPU或AI运算部分有独立的存储，即显存。车载领域共享内存一般是LPDDR，它主要是为CPU设计的，注重速度即频率而非带宽，不像显存，一般是GDDR或HBM，注重带宽，不看重频率高低。上述所有理论都是基于显存的，在车载领域共享LPDDR，其性能远远低于单独配置的显存，无论是速度还是容量，共享存储都必须远比单独的显存要高才能做到大模型推理计算。

车载领域存储比算力重要很多，最好的解决办法是HBM，但太贵了，32GB HBM2最低成本也得2000美元，汽车领域对价格还是比较敏感的，退而求其次，就是GDDR了。GDDR6的成本远低于HBM，32GB GDDR6大概只要180美元或更低。

![GDDR-HBM](D:\gitlab\ADAS\GDDR-HBM.png)

上表是几代GDDR的性能对比，基本上GDDR6的理论上限就是672GB/s，特斯拉第二代FSD芯片就支持第一代GDDR6，HW4.0上的GDDR6容量为32GB，型号为MT61M512M32KPA-14，频率1750MHz（LPDDR5最低也是3200MHz之上），是第一代GDDR6，速度较低。即使用了GDDR6，要流畅运行百亿级别的大模型，还是无法实现，不过已经是目前最好的了。

### 问题

- 小鹏现在部署的模型是不是 llm；

  不是 LLM，依旧是在传统 perception+sensor fusion+prediction+planning 的基础上的迭代优化。如采用更好的预测算法，QCnet，更好的感知算法 stream petr 等。当然这些算法都使用 transformer，但和 LLM, VLA 等大模型是两个东西。

  目前小鹏内部有 3 个以上的团队在跟进 VLA 方案，但从目前的实验来看，效果不如传统的这套方案。

  另外有一点，小鹏计划在 4 块图灵芯片级联的车上，使用 1 块图灵芯片部署 VLM，用于座舱，而不是智驾。
  
- npu 对哪些模型支持的不好，为什么对这些模型支持的不好；

  - 举个例子，NPU 不支持 FP32 精度，FP32->FP16 低精度量化后的 Xplanner & Prednet 模型跑在 NPU 上的效果十分不好（精度丢失导致轨迹蛇形化），https://xiaopeng.feishu.cn/wiki/RcFUwkAksijNtpkcuSwcSMyOnte?sheet=2izrYy。
  - cudaGetValidVoxel：计算逻辑和输入输出取数都比较适合DSP，性能也会比较 OK。但是**只能用浮点指令完成计算，能否使用 16 位定点化计算优化需要进一步分析输入输出的数据特征**。

  - Occlusion 算子主要用来检测 camera 是否有遮挡，避免因图像丢失有效内容影响自动驾驶控制

    | operator                             |                                                              | NV MX450 GPU                                           | XP5 GPU (mali 12core) | XP5 Dsp 单核            |
    | :----------------------------------- | :----------------------------------------------------------- | :----------------------------------------------------- | :-------------------- | :---------------------- |
    | **BilateralFilterTests()**           | ApplyBilateralFilter()                                       | 1.021 ms(0.509 cpu->gpu + 0.393 calc + 0.119 gpu->cpu) | 2.102 ms （calc）     | 3.391 ms (3.353 calc)   |
    | **EntropyTests()**                   | ApplyBilateralFilter()                                       | 同上                                                   | 同上                  | 同上                    |
    | CalculateEntropy()                   | 0.260 ms(0.125 cpu->gpu + 0.111 calc + 0.024 gpu->cpu)       | 0.152 ms (calc)                                        | 0.351 ms (0.340 calc) |                         |
    | **SobelTests()**                     | ApplyBilateralFilter()                                       | 同上                                                   | 同上                  | 同上                    |
    | CalculateMaxPooledGradients()        | 0.977 ms(0.398 cpu->gpu + 0.506 calc + 0.073 gpu->cpu)       | 0.258 ms (calc)                                        | 0.228 ms (0.218 calc) |                         |
    | **CannyTests()**                     | CalculateCannyEdges()                                        | 0.272 ms(0.117 cpu->gpu + 0.126 calc + 0.029 gpu->cpu) | 1.984 ms (calc)       | 1.126 ms (1.097 calc)   |
    | **ProcParkingFrame()** (整个pipline) | ApplyBilateralFilter() CalculateEntropy() CalculateMaxPooledGradients() CalculateCannyEdges(near) CalculateCannyEdges(far) | 4.844 ms(2.284 cpu->gpu + 2.048 calc + 0.512 gpu->cpu) | 10.684 ms (calc)      | 13.004 ms (12.879 calc) |

  - resize 算法，https://xiaopeng.feishu.cn/wiki/WNg8w6oCtiq3J5k4bHtc5m6Cnfe；

  - softmax 算子 https://xiaopeng.feishu.cn/docx/FKeCdL735oW2P9xQYqccY9AtnVd；

- llm 部署到车端，需要怎样的算力；

  - 带宽支持；
  - 上述算子，算法在浮点情况下精度更高，性能更好，开发起来更方便。

- 基于 transformer 网络架构的模型和 LLM 有什么区别；

### 下一步

- 进一步理解 LLM 对芯片的需求，
  - https://db.shujubang.com/home/login/index/gid/20870；
  - https://db.shujubang.com/home/login/index/gid/20803；
  - https://xiaopeng.feishu.cn/docx/AwzUd6lXqoIOjIxRBZycJMOHnHc；
  - https://mp.weixin.qq.com/s/vZa1VswGg1-5JVRSio6bOA；
  - https://aicarrier.feishu.cn/wiki/VFYcwkO4RitoCHkd2ZiciCgNnze；

- 将 xhpc 的框架图画的深入一点，；
  - https://xiaopeng.feishu.cn/wiki/wikcnIwd0h4iLUClpWThjPS30ih；
- 将所有的模型信息整理好，
  - https://xiaopeng.feishu.cn/wiki/VW9LwMYzHibFsdkYm1hczd1YnSd;
  - https://xiaopeng.feishu.cn/wiki/VFEkwwj34iC85Bk8xSOcFln0nsH；
  - 预测模块从消费prednet切换为消费PNP;
- xp5 典型场景总结，https://xiaopeng.feishu.cn/wiki/wikcn7H7s8OtyynHLmVim1Uq2Pe；
- 各模型部署情况介绍，
  - dynamic xnet: https://xiaopeng.feishu.cn/wiki/ZwnKwiQOZi3McJkREe9cjLdynpc；
  - xplanner: https://xiaopeng.feishu.cn/wiki/MYP9wNWcpiU3whkucoLceLY7npc;
  - xnet: https://xiaopeng.feishu.cn/wiki/wikcngpvmy1ISRwYjq5wZzaYBFe;
  - static xnet: https://xiaopeng.feishu.cn/docx/DmuZd6PZ3oyUnLxmawScxNKBnyd;
- 算子信息分析：
  - https://xiaopeng.feishu.cn/wiki/MwCpwrWn2isadVk8u3IcKctnnwc，以及该目录的其他文件；
  - https://xiaopeng.feishu.cn/docx/RKXLdkwYOorOCJxUFM1cWUQSnof;
  - https://xiaopeng.feishu.cn/wiki/PmLpwVF1wiXWpskXtavcqOrLnYf;
  - https://xiaopeng.feishu.cn/wiki/WNg8w6oCtiq3J5k4bHtc5m6Cnfe；
  - https://xiaopeng.feishu.cn/wiki/W3Piwi4vNiIYYSkVVqZcD51Kn0c；
  - https://xiaopeng.feishu.cn/docx/FKeCdL735oW2P9xQYqccY9AtnVd；
  - https://xiaopeng.feishu.cn/wiki/wikcnwFPWakB3cV4jqvmvj8fC1c；
  - https://xiaopeng.feishu.cn/wiki/WNg8w6oCtiq3J5k4bHtc5m6Cnfe；
  - https://xiaopeng.feishu.cn/wiki/wikcnpF3RXIeSApVfTNucDnFC7e，算子基础知识；
- 浮点问题：
  - 当从32位FP数据转换为8位整数格式时，开发人员会失去神经网络的准确性。DRIVE Thor具有2000 teraflops 的FP8精度，允许在不牺牲精度的情况下过渡到8位，FP8参考论文：https://arxiv.org/pdf/2209.05433.pdf；
- 科普性质：
  - https://xiaopeng.feishu.cn/wiki/K2oAwOl5Iie44nkBItWcFVbOnde；
  - https://xiaopeng.feishu.cn/docx/P76MdOUsuoDH2axyRrKc7Ijcn5b；
- 了解关键算法的实现详情，以及对芯片算力的需求；
- 梳理 GPGPU 和 NPU 的芯片架构区别，以及它们适合运行哪些算法；
- MX450 是啥？
- deepseek 已经展示了fp8的威力，不支持fp8的推理芯片性价比极低。fp8权重，混合精度推理，将是下一代智驾模型的基础。

### 报告注意事项

- 描述要具体，不能抽象。大概要深入到算法级别，要将读者当作这方面的专家；
- 数据描述要具体；

### Reference

[1] https://zhuanlan.zhihu.com/p/696284671

[2] Autonomous Vehicles: A Critical Review (2004-2024) and a Vision for the Future

