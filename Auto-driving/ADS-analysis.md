## ADS Analysis

### 总体架构

![auto_driver](D:\gitlab\UtopianFuture.github.io\image\auto_driver.svg)

![auto_driver_flow](D:\gitlab\UtopianFuture.github.io\image\auto_driver_flow.svg)

![ngp](D:\gitlab\UtopianFuture.github.io\image\ngp.svg)

- AI ISP: XCamera 通过ISP将 Raw 数据转换成RGB, Raw的图像格式

- DLA: 

  - perception, 读取图像后，基于DLA做推理计算，输出CameraPerceptionTopic数据

  - perception, 读取Lidar Voxel数据后，基于DLA做推理计算，输出LidarPerceptionTopic数据

  - Perception, 基于图像数据，通过DLA做推理计算，输出DynamicXNetTopic

- CVC(DSP): run_perception 中基于 RGB 图像数据，进行特征点提取(SIFT)，进行特征点匹配后输出 VIOTopic 信息给到 Localization；

- APSS: ACPU，5 个 cluster，每个 cluster 里面有 4 个 ARM A78ae。每个 cluster 里面都有 DSU-FMU 和 DSU-AE：
  - DSU-FMU（Debug and Trace Subsystem - Functional Monitoring Unit）：DSU-FMU是一种调试和跟踪子系统，用于监控处理器的功能和性能。它可以提供详细的运行时信息，包括指令执行、数据访问、缓存命中率等，以帮助开发人员进行性能分析、调试和优化；
  - DSU-AE（Debug and Trace Subsystem - Asynchronous Event）：DSU-AE是一种调试和跟踪子系统，用于处理处理器的异步事件。它可以监控和响应处理器的异步事件，如中断、异常、错误等。DSU-AE可以帮助开发人员进行故障排查和系统调试，以确保处理器的稳定运行和可靠性；
  - 运行 Localization, sensor_fusion, chief, umf, control, sr 等业务；
- SPU: System Power Unit 是一种用于处理处理器的电源管理的子系统，具体而言，**SPU** **主要负责处理器的电源管理和功耗控制**。它监测处理器的电源状态，根据需要调整电源供应，以实现功耗优化和节能。SPU 还可以监测处理器的温度和电压等参数，以确保处理器的安全运行；
- SCP: system Control Subsystem（系统控制子系统）主要负责处理器的系统级控制和管理功能。它包含了多个模块和功能，用于管理处理器的电源、时钟、重置、中断控制、安全性等方面。 
  - 电源管理：负责处理处理器的电源控制和管理，包括电源状态的转换、电源域的划分、电源模式的管理等；
  - 时钟管理：控制和管理处理器的时钟频率、时钟源的选择、时钟分频等，以确保处理器的正常运行和节能优化；
  - 重置控制：管理处理器的重置信号，包括处理器的复位、外设的重置等，以确保处理器和系统在需要时能够正确地进行重启和初始化；
  - 中断控制：处理和管理处理器的中断信号，包括中断的触发、中断优先级的管理、中断处理程序的调度等；
  - 安全性管理：提供安全性相关的功能和机制，如安全状态的切换、安全监视器的管理、安全性扩展等，以保护处理器和系统的安全性；
- MIPIRXSS: [XP5 MIPIRXSS LLD](https://xiaopeng.feishu.cn/wiki/IJyCw51HRiwjcik8dYWcnh0nnJc)，接收 sensor 数据并输出的子系统，接收从 sensor 送入 SER/DES 的数据后通过 IPI 接口送入 xVSS 的 CFE 模块和 MEDIASS 的 ISP_TOP 模块进行处理。和之前接触的手机 SOC 不同，汽车上的 sensor 和 SOC 之间的距离很远，MIPI 协议能够完成这种传输。
- xVSS: 对 MIPIRXSS 传入的 sensor 的数据进行处理，sensor 获取到的数据算法不能直接处理，需要经过 xVSS 的 RSP 流程处理，放到 DDR 中，给后续模块使用。其有如下组件：
  - Camera Front End(CFE)，对图像进行裁剪，解压缩等操作，操作完后，通过 CFE DMA 写入到 DDR 中；
  - CSE：从 DDR 取数据，进行数据统计；
  - 从 DDR 取数据，做裁剪，空间转换，色彩转换等；
  - Vision Subsystem Processor，负责配置前面几个组件的参数等；
  - RPU：以处理中断为主，并不涉及复杂的计算任务；
- MEDIASS：没找到更多介绍，不知道干嘛的；
- HSPSS：ETHER, PCIE, USB 三个子系统，都是陌生的领域；
- DDRSS：两个 DDR 子系统；
- SSPSS：低速子系统；
- NPC：整个 NPC 模块包括 NPU, DSP, RSU, DLA，这些组件分别负责什么业务？硬件架构是怎样的，运行什么系统呢？
  - NPU: 跑神经网络的；
  - DSP: data signal processing for computing（SIMD 指令集），用于补充 NPU 支持不好的算子，承担额外算力支撑；哪些业务 NPU 不支持，需要运行在 DSP 上
  - RSU: 负责推理网络任务的调度和执行，以及NPC下面各种计算资源，内存资源的实时管理；
  - DLA: Deep Learning Accelerator, 1 DLA contain 2 NPU cluster, which called NPC. Within a NPC, there are 4 NPU(neural processing unit) compute cores.
- GPU：Xlidar 读取 Lidar 数据，做前处理生成 Voxel 数据
- SI/AI: Security Island, Application Island 分别指什么；
- RPU(ISP?):

#### HPC

算力的核心就是 NPU，其中有一个 RSU: cortex-R52 x1，负责推理网络任务的调度和执行，以及 NPC 下面各种计算资源，内存资源的实时管理；以及 4 个 NPU，负责跑 perception, chief 等算法、模型。DLA 的结构如下图所示：

![image-20250219143703028](C:\Users\xpeng\AppData\Roaming\Typora\typora-user-images\image-20250219143703028.png)

各组件功能如下：

- DLA: Deep Learning Accelerator, 1 DLA contain 2 NPU cluster, which called NPC. Within a NPC, there are 4 NPU(neural processing unit) compute cores.
- NPU: 跑神经网络的；

- DSP: data signal processing for computing（SIMD 指令集），用于补充 NPU 支持不好的算子，承担额外算力支撑；**哪些业务 NPU 不支持，需要运行在 DSP 上？**

- RSU: 负责推理网络任务的调度和执行，以及 NPC 下面各种计算资源，内存资源的实时管理；

整个 DLA 的工作流程如下：

![image-20250219104049413](C:\Users\xpeng\AppData\Roaming\Typora\typora-user-images\image-20250219104049413.png)

①Task Request ：APU 向 RSU 发起推理任务 Request，APU 事先建立的 Task Req 在 L3.DDR 的 Task Ring 中；

②Fetch task packet ：RSU 从 L3.DDR 的 Task Ring 中取 Task packet； 

③Fetch fetch CMD packet ：RSU 从 L3.DDR 中取 CMD packet； 

④Task distribution ： RSU 下发指令指针并 Launch NPU/CDTU/DSP，RSU 解析 Task 和 Model CMD packet， 根据 Model CMD 中的信息下发各 engine 待运行指令指针（DDR 地址和长度）； 

⑤Instruction Fetch：各 Engine 取指令； 

⑥Load Data to L2 ：CDTU 将 L3.DDR Input 数据 load到 L2，数据类型包括 Input； 

⑦Load Data to L1 ：NPU/DSP 中 的 DTU/DMA 将 L2 Input 数据 load 到 NPU/DSP 中的 L1； 

⑧Load Parameter to L2 ：CDTU 将 L3.DDR 中的 Parameter load 到 L2； 

⑨Load Parameter to L1 ：NPU/DSP 中的 DTU/DMA 将 L2 Parameter load 到NPU/DSP中的L1； 

⑩Compute ：NPU/DSP 算子计算； 

⑪Store Data to L2 ：NPU/DSP 中的 DTU/DMA 将 L1 数据 store到 L2，数据类型包括中间缓存的激活数据（Activation）； 

⑫Store Data to L3 ：CDTU 将 L2 数据 store 到 L3，数据类型包括中间缓存的激活数据（Activation）； 

⑬Load Data to L2 ：CDTU 将 L3.DDR 中计算的中间数据 load 到 L2； 

⑭Load Data to L1 ：NPU/DSP中的DTU/DMA将L2 中计算的中间数据 load到NPU/DSP中的L1； 

（⑤ ~ ⑭ 分部循环执行） 

⑮Store Inference Result to L2 ：NPU/DSP中的DTU/DMA将L1计算的结果store到L2; 

⑯Store Inference Result to L2 ：NDTU将L1中的结果store到L2; 

⑰Task finish response to RSU ：NPU/DSP/CDTU通过Mail或中断方式向RSU返回Task结束response 

⑱Task finish response to APU ：RSU通过Mail或中断方式向APU返回Task结束response

zephyr 在 XP5 中的作用，一个就是运行在 NPC 的 RSU（一个 cortex R52 core）上，**负责推理网络任务的调度和执行，以及 NPC 下面各种计算资源，内存资源的实时管理**；

### perception

perception 模块负责通过传感器来去获取车辆在整个环境中的状态。如障碍物的信息，以及除了障碍物以外的其他的一些环境上的更多可能需要用到的一些信息，如车道线，红绿灯等，这就是感知。这些信息再传给下游的 Localization, sensor fusion, chief 模块，做进一步的处理。

在 driving 和 parking 算法中（一般是不同业务组负责的），感知关注的对象是不同的。driving 关注的是红绿灯，车道线，人行道，行人，非机动车，路标等。而 parking 关注的是墙体，车位线，雪糕筒等。

就是拿到多个 camera 的图像、IMU、轮速计等的参数，然后通过模型获取对应的信息。其中 ISP 中有诸多算法能够对这些图像进行预处理，

算法的切换通过一个全局的行车状态来控制，可能跟车辆挡位有关。

智驾算法变化非常快。

### Localization

定位主要解决的问题是车辆在当前环境中的位置是怎样的。地图显示的是车辆所处在真实世界是怎样的，它包括去往目的地的路径规划。

定位跟地图可以举个例子来说明。比如说大家走到公园里面，在公园里面的一个十字路口发现自己迷路了，那迷路之后我们会去看公园的地图，在这个地图上能够得到一些信息。比如这个公园大概分哪些模块？处于哪个位置？同样它上面会有个小红点标识当前你现在所处的位置。

那么这就是很典型的定位，因为没有这个位置信息，这个地图对你而言是无效的，你必须要知道你自己在地图上的定位，才继续去往下走。从自动驾驶业务上去看，也是一样的，模型通过地图得到车辆当前的位置，以及周边的请看，才能去规划行车路线。

具体来说，localization 就是拿着 GPS 和 IMU、车辆轮速记等等信息，得出车辆当前的位置信息，然后地图拿着这个位置信息，加上感知识别到的一些位置信息，以及高精地图去做定位操作。

Sensor Fusion 的功能是把相机识别到的障碍物的信息和特征，就是环境的信息和同毫米波雷达，结合起来，提供给下游的 chief 去做规划和控制。

### Map Manager

map manager 负责跟构建跟本地的一些轻量化的地图和高精地图做一些融合，来去把感知识别到的信息和地图上面去做一些处理。

### Chief

Localication 往后就是根据目的地和意图去做控制和规划，即从当前这个位置规划一条到目的地的可行途径；控制就是把这条可行途径以某种方式去把它给落实下来，去控制车辆。其有几个核心模块：

- Sensor fusion 的核心是汇总所有数据，建立不同传感器之间观测结果的联系；

- Behavior prediction 的核心是根据 fusion 的结果，预测运动物体的行为和轨迹；prediction 就是去预测周边的一些物体的运动轨迹或其他信息，比如像一个行人，他的意图会是什么样？
- routing，就是去找一些道路信息，traffic lines，加上导航信息，跟实时地图组合在一起，做 planning；

- planning，其实分四种模式。

  - DSP，也应该是可行驶空间的 planning；

  - HNGP，就是高速 NGP；

  - XNGP，现在主推的一种，无高精地图场景下的 NGP；

  - CNGP，就是 city NGP；

​	planning 出来的最终结果给到 control，去控制车辆状态。 

现在各个模块都引入了大模型。

### 功能区分

#### 主动安全

- 前向碰撞预警FCW
- 自动紧急制动AEB
- 车道偏离预警LDW
- 交通标志识别TSR
- 后方横向来车预警RCTA
- 盲区监测预警BSD
- 车门开启预警DOW
- 后向碰撞预警RCW
- 车辆偏离辅助LKA
- 智能远光灯控制IHB
- 儿童车内防遗忘监测

#### 全场景泊车

- 全场景泊车
- 倒车雷达预警
- 倒车影像
- 360°全景环视
- 超级自动泊车辅助
- 自动泊出辅助AEP
- 钥匙泊车辅助
- 手机泊车辅助
- 停车场记忆泊车VPA
- 停车场记忆泊车 - 多楼层VPA-L
- 自动代客泊车AVP

#### 辅助/自动驾驶

- 自适应巡航ACC
- 自适应弯道巡航ATC
- 自动限速调节ASL
- 车道居中辅助LCC
- 自动变道辅助ALC
- NGP自动导航辅助驾驶-高速NGP
- NGP自动导航辅助驾驶-城市NGP
- 城市中低速自动驾驶

#### 其他功能

- 哨兵模式：Surround Camera * 4

- 云座舱：DSM Camera

- 行车记录仪： Fisheye

- 增强版交通信息显示：Front Camera*2， Side Camera * 4， Rear Camera， MRR， SRR*4， GNSS HDMAP

- 辅助驾驶模拟显示SD:  XP-Narrow,  XP-Wide,  MRR

- 自动驾驶模拟显示SR：Front Camera *2， Side Camera *4， MRR， SRR * 4

- 底盘透视影像系统：Surround Camera * 4
