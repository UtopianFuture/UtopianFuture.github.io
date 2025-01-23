## A Survey of Autonomous Driving

### 前景与挑战

我们对自动驾驶的期望不是说达到人的操作水平，而是要操作人的操作水平，将人从驾驶任务中解放出来，然后将车祸事故降低到 0。

#### 前景

#### 挑战

汽车工程师协会（SAE）在定义了五个驾驶自动化级别。

- L0：完全没有自动化。
- L1：基本的驾驶辅助系统，如自适应巡航控制、防抱死制动系统和稳定性控制；
- L2：部分自动化，将高级辅助系统如紧急制动或碰撞避免集成其中，L2 自动化是可行的技术；
- L3：有条件的自动化；在正常运行期间，驾驶员可以专注于除驾驶之外的任务，然而，驾驶员必须能够快速响应车辆发出的紧急警报并准备好接管。此外，L3 ADS 仅在有限的操作设计域（ODDs）中运行，例如高速公路；
- L4：在任何程度上都不需要人类注意力的是 L4 和 L5。然而，L4 只能在存在特殊基础设施或详细地图的有限 ODDs 中运行。在离开这些区域的情况下，车辆必须通过自动停车来停止行程；
- L5：完全自动化的系统，可以在任何道路网络和任何天气条件下运行。

### 系统组件和架构

#### 系统架构

##### Ego-only systems

在任何时候，一台车上能够完成所有必要的自动驾驶操作，不依赖于其他车辆和基础设施；

##### Modular systems

模块化 ADS 的核心功能可以总结为：localization and mapping, preception, assessment, planning and decision making, vehicle control, and human-machine.

Typical **pipelines** start with feeding raw sensor inputs to localization and object detection modules, followed by scene prediction and decision making. Finally, motor commands are generated at the end of
the stream by the control module.

modular system 的优势是将复杂的自动驾驶任务分成不同的子任务，这些子任务，如 computer vision, robotics 等都发展很长时间了。另外一个优势就是可以在复杂的自动驾驶算法之上添加一些安全约束，以在不修改算法的情况下强制执行一些 hard code 的紧急规则。模块化方法的主要优势在于其可解释性——在系统出现故障或意外行为时，可以确定故障模块。这是目前业界广泛使用的方案，也是传统方案，与之对应的端到端方案。

主要的缺点是会出现错误传播以及算法过于复杂。

##### end-to-end driving

端到端自动驾驶，即直接从传感器输入生成自我运动。这种方法提出直接将整个驾驶流程从处理传感器输入到生成转向和加速命令作为一个单一的机器学习任务进行优化。驾驶模型可以通过模仿学习（IL）以监督学习的方式学习，以模仿人类驾驶员，或者通过从零开始探索和改进驾驶策略来学习，即通过强化学习（RL）。通常，架构比模块化堆叠更简单，组件更少。尽管概念上很有吸引力，但这种简单性**导致了可解释性的问题**——由于缺乏中间输出，很难甚至无法确定模型为何会出错。

端到端自动驾驶主要有三种方法：

- direct supervised deep learning（直接监督深度学习）：[1]~[5]；
- neuroevolution（神经进化？）：[8], [9]；
- deep reinforcement learning（深度强化学习）：[6], [7]；

端到端自动驾驶应该模仿人类的驾驶动作/习惯么？

##### Connected systems

还没有使用这种技术的 ADS，但某些研究人员认为这是自动驾驶的未来。所谓 connected systems 就是通过使用车辆自组织网络（VANETs），自动驾驶的基本操作可以分布在各个车辆之间。V2X 是一个术语，代表“车辆对一切”，从行人的移动设备到交通灯上的固定传感器，车辆可以通过 V2X 访问大量数据。通过在不同车辆之间共享交通网络的详细信息，可以消除仅限自身的平台的缺点，如感知范围、盲点和计算限制。

有道理，这样就能很大程度上缓解城市堵车。

#### 传感器和硬件

现在的 ADS 有大量的传感器，用于感知环境，包括动态和静态对象，例如可行驶区域、建筑物、人行横道。下面列举一些现在常用的传感器：

- Monocular Cameras：单目相机；

- Omnidirectional Camera：全向相机；

- Event Cameras：事件相机；

- Radar：毫米波雷达；

- Lidar：激光雷达；

- Proprioceptive sensors：感知车辆运动状态的传感器，这些传感器通过 CAN 协议链接起来。
  - wheel encoders，车轮编码器主要用于里程测量；
  - IMU(Inertial Measurement Units)，惯性测量单元用于监测速度和位置变化；
  - tachometers，转速表用于测量速度；
  - altimeters，气压计用于测量高度。

### 定位和制图(Localization and mapping)

定位（Localization）是确定自我位置相对于环境中参考框架的任务，是任何 ADS 的基础。车辆必须使用正确的车道并准确地定位自己。定位也是全球导航的基本要求。

#### GPS-IMU Fusion

GPS-IMU 融合的主要原理是在绝对位置读数的间隔内纠正里程测量的累积误差。在 GPS-IMU 系统中，**位置和方向的变化由 IMU（陀螺仪） 测量**，并且这些信息被处理以使用里程测量定位车辆。但 IMU 有一个显著缺点：误差随时间累积，这会导致定位信息不准确。通过整合 GPS 读数，可以间隔性地纠正 IMU 的累积误差。

**城市自动驾驶所需的精度对于当前在量产汽车中使用的 GPS-IMU 系统来说太高**。此外，在密集的城市环境中，精度进一步降低，并且由于隧道和高楼大厦，GPS 有时会停止工作。尽管 GPS-IMU 系统本身不符合性能要求，并且只能用于高级路线规划，但它们与激光雷达和其他传感器一起用于最先进的定位系统中的初始姿态估计。

那现在 ADS 是怎样做 Localization 的？

[RTK+激光雷达配准+IMU+轮式里程计](自动驾驶系统中怎么实现车辆定位？ - 布莱克的回答 - 知乎
https://www.zhihu.com/question/265760094/answer/299205095)？

#### Simultaneous localization and mapping (SLAM)

SLAM 能同时完成制图和车辆定位的操作，它不需要关于环境的先验信息。SLAM 在机器人领域应用广泛，主要应用在室内。它有个好处就是在任何环境下都能工作，不过在 ADS 领域性能不好。

#### A priori map-based localization

基于先验地图（priori map-based）的定位技术的核心思想是匹配：定位是通过将在线读数与详细先验地图上的信息进行比较并找到最佳可能匹配的位置来实现的。通常在匹配开始时使用 GPS 等定位手段来估计初始姿态。

不过环境的变化会影响基于地图的方法的性能。这种影响在农村地区尤为普遍，因为地图上的过去信息可能由于路边植被和建筑的变化而与实际环境不符。此外，这种方法需要额外的制图步骤。

有两种不同的基于地图的方法：地标搜索和匹配。

##### Landmark search

相比于点云匹配（Point cloud matching），地标搜索在计算上更加便宜。**只要存在足够数量的地标**，它就是一种稳健的定位技术。在城市环境中，电线杆、路缘、标志和道路标记可以作为地标。在 [10] 中使用了激光雷达和蒙特卡洛定位（MCL）的道路标记检测方法。在此方法中，道路标记和路缘与 3D 地图匹配以找到车辆的位置。

这种方法的一个主要缺点是，对地标数量的依赖使得系统在地标不足的地方容易出问题。

##### Point cloud matching

点云是在同一空间参考系下表达目标空间分布和目标表面特性的海量点集合，在获取物体表面每个采样点的空间坐标后，得到的是点的集合，称之为“点云”（Point Cloud）。

三维图像是一种特殊的图像信息表达形式。相比较于常见的二维图像，其最大的特征是表达了空间中三个维度（长度宽度和深度）的数据。

点云配准分为[粗配准](https://zhida.zhihu.com/search?content_id=108412837&content_type=Article&match_order=1&q=粗配准&zhida_source=entity)（Coarse Registration）和精配准（Fine Registration）两个阶段。

- 粗配准是指在点云[相对位姿](https://zhida.zhihu.com/search?content_id=108412837&content_type=Article&match_order=1&q=相对位姿&zhida_source=entity)完全未知的情况下对点云进行配准，可以为精配准提供良好的初始值。当前较为普遍的点云自动粗配准算法包括基于穷举搜索的[配准算法](https://zhida.zhihu.com/search?content_id=108412837&content_type=Article&match_order=3&q=配准算法&zhida_source=entity)和基于特征匹配的配准算法；

  - 基于穷举搜索的配准算法：

    遍历整个变换空间以选取使[误差函数](https://zhida.zhihu.com/search?content_id=108412837&content_type=Article&match_order=1&q=误差函数&zhida_source=entity)最小化的变换关系或者列举出使最多点对满足的变换关系。如RANSAC 配准算法、四点一致集配准算法（4-Point Congruent Set, 4PCS）、Super4PCS 算法等；

  - 基于特征匹配的配准算法：

    通过被测物体本身所具备的形态特性构建点云间的匹配对应，然后采用相关算法对变换关系进行估计。如基于点 FPFH 特征的 SAC-IA、FGR 等算法、基于点 SHOT 特征的 [AO 算法](https://zhida.zhihu.com/search?content_id=108412837&content_type=Article&match_order=1&q=AO算法&zhida_source=entity)以及基于线特征的 ICL 等…

- 精配准的目的是在粗配准的基础上让点云之间的空间位置差别最小化。应用最为广泛的精配准算法应该是 ICP以及 ICP 的各种变种（稳健 ICP、point to plane ICP、Point to line ICP、MBICP、GICP、NICP）。

一些研究人员认为鉴于道路网络的规模和快速变化，先验地图方法是不可行的，制图和维护既耗时又耗费资源。

##### 2D to 3D matching

将在线 2D 读数匹配到 3D 先验地图是一种新兴技术。这种方法只需要在配备 ADS 的车辆上使用相机，而不是激光雷达（贵）。**先验地图仍然需要使用激光雷达创建**。在 [11] 中，使用单目相机将车辆定位在点云地图中。通过初始姿态估计，从离线 3D 点云地图创建 2D 合成图像，并与从相机接收的在线图像通过归一化互信息进行比较。这种方法增加了定位任务的计算负担。在 [12] 中介绍了一种使用立体相机设置的视觉匹配算法，将在线读数与从 3D 先验生成的合成深度图像进行比较。基于相机的定位方法可能会在未来变得流行，因为其硬件要求比基于激光雷达的系统更便宜。

Localization 目前分为有图和无图两种技术路线。

- 有图方案，顾名思义，是自动驾驶系统**依赖高精度地图来实现车辆定位、路径规划和驾驶决策**的一种方案。高精度地图包含了详细的道路信息，如车道线、交通标志、信号灯等，这些信息为自动驾驶车辆提供了精确的导航和决策依据。在高速公路、快速路等结构化道路上，有图方案能够表现出色，实现较高级别的自动驾驶功能，如自动驾驶车辆在高速公路上的自主行驶，也就是现在一直宣传的 “高速NOA”功能。
- 无图方案则主要依赖车载传感器（如激光雷达、摄像头、[毫米波雷达](https://zhida.zhihu.com/search?content_id=695443463&content_type=Answer&match_order=1&q=毫米波雷达&zhida_source=entity)等）实时收集的环境数据来进行障碍物检测、[道路识别](https://zhida.zhihu.com/search?content_id=695443463&content_type=Answer&match_order=1&q=道路识别&zhida_source=entity)和驾驶决策。这种方案不需要预先绘制的高精度地图，只需要普通的地图提供传统道路网数据，通过车辆自身的[感知系统](https://zhida.zhihu.com/search?content_id=695443463&content_type=Answer&match_order=1&q=感知系统&zhida_source=entity)实现自主构造地图。无图方案显著降低了自动驾驶系统对高精度地图的依赖，使得自动驾驶车辆能够在未被地图覆盖或地图信息不精确的地区正常运行。这特别适合于城市道路等非结构化的复杂环境，无图方案能够实时应对道路的突发变化。

### 感知(Perception)

感知周围环境并提取对导航至关重要的信息。

#### 检测(Detection)

##### Image-based Object Detection

对象检测就是识别 ADS 感兴趣的物体的位置和大小。包括静态对象，如交通信号灯，路标等，以及动态对象，如其他的车辆，行人，非机动车辆等。对象识别发展很多年了，当前最新的对象识别技术都是基于 Deep convolutional neural network(DCNN)，这些技术可以分为两类：

- 单阶段检测框架（single stage detection frameworks）使用单个网络同时产生对象检测位置和类别预测；
- 区域提议检测框架（region proposal detection frameworks）使用两个不同的阶段，首先提出一般感兴趣区域，然后由单独的分类器网络进行分类；

区域提议方法目前在基准测试中处于领先地位，但代价是需要高计算能力，并且通常难以实现、训练和微调。与此同时，单阶段检测算法倾向于具有快速推理时间和低内存成本，这非常适合实时驾驶自动化。

##### Semantic Segmentation（没懂）

除了图像分类和对象检测之外，计算机视觉研究还解决了图像分割任务。这包括将图像的每个像素分类为类别标签。此任务对于驾驶自动化特别重要，因为一些感兴趣对象通过边界框定义不佳，特别是道路、交通线、人行道和建筑物。

##### 3D Object Detection

相机在 ADS 中的应用很广泛，但是其有个关键的问题，不知道距离/尺寸。如果 ADS 只使用相机作为传感器，那么为了在自动驾驶过程中使用这些信息，例如障碍物避免，就需要从 2D 基于图像的检测桥接到 3D 度量空间。因此，需要深度估计，这些匹配算法的成本是很高的，这为已经复杂的 perception pipeline 增加了大量的处理成本。一种相对较新的 perception 模式：3D 激光雷达，为 3D 感知提供了替代方案。收集的 3D 数据本身解决了尺度问题，并且由于它们有自己的发射源，它们对照明条件的依赖性要小得多，并且对恶劣天气的敏感性要小得多。

雷达已经用于各种感知应用，各种类型的车辆，不同型号的雷达在互补运行。它的精度不足，对于行人等物理无法分辨，另一个问题就是大多数雷达的视野非常有限，这迫使车上部署多个雷达来覆盖整个视野。**虽然不如激光雷达准确，但它可以在高范围内检测到物体并估计它们的速度**。现在是毫米波雷达和激光雷达搭配使用，雷达具有长范围、低成本和对恶劣天气的鲁棒性，而激光雷达提供精确的对象定位能力。

#### Object Tracking

对象跟踪也通常被称为多对象跟踪（Multiple object tracking, MOT）和检测和跟踪多个对象（DATMO, Detection and tracking of multiple objects）。对于在复杂和高速场景中的完全自动驾驶，仅估计位置是不够的，需要估计动态对象的航向和速度，以便应用运动模型跟踪对象随时间的变化并预测未来的轨迹以避免碰撞。这些轨迹必须在车辆坐标系中估计，以便用于规划，因此必须通过多个相机系统、激光雷达或雷达传感器获得范围信息。3D 激光雷达通常用于其精确的范围信息和大视野，允许更长时间的跟踪。为了更好地应对不同感测模式的限制和不确定性，通常使用传感器融合策略进行跟踪。常用的对象跟踪器依赖于简单的数据关联技术，然后是传统的滤波方法。当在 3D 空间中以高帧率跟踪对象时，最近邻方法通常足以建立对象之间的关联。

#### Road and lane detection

道路和车道检测，之前覆盖的边界框估计方法对于感知某些某兴趣的对象很有用，但对于像道路这样的连续表面是不足够的。确定可行驶表面对于 ADS 至关重要，并且已经作为检测问题的一个子集进行了专门研究。虽然可以通过语义(Semantic Segmentation)分割来确定可行驶表面，但自动驾驶车辆需要理解道路语义，以正确协商道路。理解车道及其通过合并和交叉口的连接从感知角度来看的仍然是一个挑战。

最简单的是从自我车辆的角度确定可行驶区域，然后可以将道路划分为车道，并确定车辆的主机车道。ADAS 技术允许在合理距离内确定主机车道，如车道偏离警告、车道保持和自适应巡航控制。更具挑战性的是确定其他车道及其方向，最后理解复杂语义，如它们的当前和未来方向，或合并和转弯车道。这些 ADAS 或 ADS 技术在任务、检测距离和可靠性率方面有不同的标准，但完全自动驾驶需要对道路结构的完整、语义理解，并能够在长距离内检测多个车道。

当前用于道路理解的方法通常首先依赖于外感受器数据预处理。当使用相机时，这通常意味着执行图像颜色校正以标准化照明条件。对于激光雷达，可以使用几种过滤方法来减少数据中的杂波，例如地面提取或基于地图的过滤。无论任何感测模式，识别与静态道路场景冲突的动态对象是一个重要的预处理步骤，然后在纠正后的数据上执行道路和车道特征提取。颜色统计和强度信息，梯度信息和各种其他过滤器已被用于检测车道标记。类似的方法已被用于道路估计，其中道路的通常均匀性和边缘的高程间隙允许应用区域生长方法（？）。立体相机系统以及 3D 激光雷达已被用于直接确定道路的 3D 结构。最近，基于机器学习的方法，要么将地图与视觉融合，要么使用完全基于外观的分割，已经被使用。

一旦估计了表面，模型拟合就被用来建立道路和车道的连续性。通过参数模型如线和样条进行几何拟合已经被使用，以及非参数连续模型。假设平行车道的模型已经被使用，并且最近提出了整合拓扑元素如车道分裂和合并的模型。

时间积分完成了道路和车道分割管道。在这里，车辆动力学与道路跟踪系统结合使用，以实现平滑的结果。动态信息也可以与卡尔曼滤波或粒子滤波一起使用，以实现更平滑的结果。道路和车道估计是一个研究良好的领域，并且许多方法已经成功地集成用于车道保持辅助系统。然而，大多数方法仍然充满了假设和限制，真正通用的系统，能够处理复杂的道路拓扑结构尚未开发。通过标准化道路地图，编码拓扑结构和新兴的基于机器学习的道路和车道分类方法，用于驾驶自动化的稳健系统正在慢慢形成。

### Assessment

一个稳健的 ADS 应该不断评估(Assessment)整体风险水平并预测周围人类驾驶员和行人的意图，缺乏敏锐的评估机制可能导致事故。

#### Risk and uncertainty assessment

整体评估可以总结为量化和衡量驾驶场景的不确定性和风险水平。

#### Surrounding driving behavior assessment

理解周围人类驾驶员的意图与中长期预测和决策制定最相关。为了增加对周围对象行为的预测范围，应该考虑人类特征并将其纳入预测和评估步骤。

#### Driving style recognition

驾驶风格是一个广泛的概念，没有建立共同的定义。此外，识别周围人类驾驶风格是一个严重未被研究的主题。

### Planning and decision making

#### Global planning

全局规划器负责在道路网络上找到从起点到最终目的地的最合理的路线。

#### Local planning

局部规划器的目标是在不失败的情况下执行全局计划。换句话说，为了完成行程，ADS 必须找到轨迹以避免障碍物并满足配置空间（Cspace）中的优化标准，给定起点和目的地。

### Human machine interaction

### Reference

[1] C. Chen, A. Seff, A. Kornhauser, and J. Xiao, “Deepdriving: Learning affordance for direct perception in autonomous driving,” in Proceedings of the IEEE International Conference on Computer Vision, 2015, pp.
2722–2730.

[2] D. A. Pomerleau, “Alvinn: An autonomous land vehicle in a neural network,” in Advances in neural information processing systems, 1989, pp. 305–313.

[3] U. Muller, J. Ben, E. Cosatto, B. Flepp, and Y. L. Cun, “Off-road obstacle avoidance through end-to-end learning,” in Advances in neural information processing systems, 2006, pp. 739–746.

[4] M. Bojarski, D. Del Testa, D. Dworakowski, B. Firner, B. Flepp, P. Goyal, L. D. Jackel, M. Monfort, U. Muller, J. Zhang, et al., “End to end learning for self-driving cars,” arXiv preprint arXiv:1604.07316, 2016.

[5] H. Xu, Y. Gao, F. Yu, and T. Darrell, “End-to-end learning of driving models from large-scale video datasets,” arXiv preprint, 2017.

[6] A. E. Sallab, M. Abdou, E. Perot, and S. Yogamani, “Deep reinforcement learning framework for autonomous driving,” Electronic Imaging, vol. 2017, no. 19, pp. 70–76, 2017.

[7] A. Kendall, J. Hawke, D. Janz, P. Mazur, D. Reda, J.-M. Allen, V.-D. Lam, A. Bewley, and A. Shah, “Learning to drive in a day,” arXiv preprint arXiv:1807.00412, 2018.

[8] S. Baluja, “Evolution of an artificial neural network based autonomous land vehicle controller,” IEEE Transactions on Systems, Man, and Cybernetics-Part B: Cybernetics, vol. 26, no. 3, pp. 450–463, 1996.

[9] J. Koutník, G. Cuccu, J. Schmidhuber, and F. Gomez, “Evolving large- scale neural networks for vision-based reinforcement learning,” in Proceedings of the 15th annual conference on Genetic and evolutionary
computation. ACM, 2013, pp. 1061–1068.

[10] A. Hata and D. Wolf, “Road marking detection using lidar reflective intensity data and its application to vehicle localization,” in 17th International Conference on Intelligent Transportation Systems (ITSC). IEEE,
2014, pp. 584–589.

[11] R. W. Wolcott and R. M. Eustice, “Visual localization within lidar maps for automated urban driving,” in IEEE/RSJ International Conference on Intelligent Robots and Systems (IROS). IEEE, 2014, pp. 176–183.

[12] C. McManus, W. Churchill, A. Napier, B. Davis, and P. Newman, “Distraction suppression for vision-based pose estimation at city scales,” in IEEE International Conference on Robotics and Automation (ICRA). IEEE, 2013, pp. 3762–3769.
