## A Survey of End to end driving

### Introduction

从广义上讲，自动驾驶的研究可以分为两种主要方法：

- 模块化方法，也称为中介感知方法；模块化方法是工业界广泛使用的方法，现在被认为是传统方法。模块化系统源自主要为**自主移动机器人**设计的架构，由自包含但相互连接的模块组成，如感知、定位、规划和控制。模块化方法的主要优势在于其可解释性——在系统出现故障或意外行为时，可以确定故障模块。然而，构建和维护这样的流程成本高昂，尽管经过多年的努力，这些方法仍然远未实现完全自主；
- 端到端方法。端到端驾驶，也称为行为反射，在文献中是模块化方法的替代方案，已成为自动驾驶研究的一个趋势。这种方法提出直接**将整个驾驶流程从处理传感器输入到生成转向和加速命令作为一个单一的机器学习任务进行优化**。驾驶模型可以通过模仿学习（IL）以监督学习的方式学习，以模仿人类驾驶员，或者通过从零开始探索和改进驾驶策略来学习，即通过强化学习（RL）。通常，架构比模块化堆叠更简单，组件更少。尽管概念上很有吸引力，但这种简单性**导致了可解释性的问题——由于缺乏中间输出，很难甚至无法确定模型为何会出错**。

### 模块化和端到端方案的对比

#### 模块化

模块化使工程团队能够专注于定义明确的子任务，并在整个堆栈中独立进行改进，只要中间输出保持功能正常，系统就能保持运行。然而，设计这些数十个模块的互连是一项复杂的任务。工程团队必须仔细选择每个模块的输入和输出，以适应最终的驾驶任务。同样，最终的驾驶决策是由一系列基于确定性规则处理不同过程的工程子系统共同得出的结果。明确定义的中间表示和确定性规则，使得基于模块化流程的自动驾驶系统在其既定能力范围内表现出可预测性，因为不同子系统之间存在严格且已知的相互依赖关系。此外，这种流程的一个主要优点是具有可解释性。在出现故障或系统意外行为的情况下，**可以追踪到最初的错误来源**，例如误检测。更一般地说，模块化允许可靠地推断系统是如何做出特定驾驶决策的。

然而，模块化系统也存在一些缺点。各个子系统预定义的输入和输出，在不同场景下可能并非最适合最终的驾驶任务。不同的道路和交通状况可能需要关注从环境中获取的不同信息。因此，**很难列出涵盖所有驾驶情况的详尽有用信息列表**。

例如，在感知方面，通常模块化堆栈会将动态对象压缩为三维边界框，后续模块无法获取此表示中未包含的信息。由于决策取决于道路和交通环境，驾驶场景和环境的多样性使得用适当的情境化解决方案覆盖所有情况变得极其困难。

例如，一个球突然滚到路上。在居民区，人们可能会预期有孩子来捡球，因此减速是合理的决定。然而，在高速公路上，突然刹车可能会导致追尾事故，撞上球型物体的风险可能比急刹车要小。为了处理这两种情况，首先，感知工程师必须决定在感知模块中，将 “球” 对象类型纳入可检测对象中。其次，负责控制的工程师必须根据环境为 “球” 标记不同的成本，从而产生不同的行为。还有许多类似的、罕见但相关的情况，如道路施工、前方事故、其他司机的失误等等。要为所有这些情况设计出适当的行为，所需的工程工作量是巨大的。此外，模块化流程中解决的一些子问题可能不必要地困难且浪费资源。例如，一个旨在检测场景中所有物体三维边界框的感知模块，会平等对待所有物体类型和位置，仅仅是为了最大化平均精度。然而，在自动驾驶场景中，显然优化检测附近的移动物体最为关键。试图检测所有物体不仅会导致计算时间延长，还会为了整体的良好精度而牺牲相关物体的精度。此外，在将检测到的物体框传递给后续模块时，检测的不确定性往往会丢失。

#### 端到端驾驶

在端到端驾驶中，将传感器输入转换为驾驶指令的整个流程被视为一个单一的学习任务。假设 IL 模型有足够的专家驾驶数据，或者 RL 有足够的探索和训练，模型应该能够为目标任务学习到最优的中间表示。该模型可以自由关注任何隐含的信息来源，因为不存在人为定义的信息瓶颈。例如，在黑暗中，可以通过其他物体反射的车头灯推断出有一辆被遮挡的汽车。如果驾驶场景被简化为物体位置（如模块化堆栈中那样），这种间接推理是不可能的，但如果模型能够自行学习关注哪些视觉模式，就可以实现。

使用端到端优化引发了可解释性的问题。由于没有中间输出，追踪错误的最初原因以及解释模型为何做出特定驾驶决策变得更加困难。

虽然神经网络功能强大且成功，但众所周知，它容易受到对抗攻击。通过对输入进行微小但精心选择的改变，模型可能会被愚弄并犯错。这在将神经网络应用于自动驾驶等高风险领域时，引发了严重的安全问题。

### 学习方法

#### 模仿学习（Imitation Learning）

IL 或行为克隆（behavior cloning）是端到端驾驶中使用的主要范式。IL 是一种监督学习方法，其中模型被训练来模仿专家行为。在自动驾驶的情况下，专家是人类驾驶员，被模仿的行为是驾驶指令，如转向、加速和刹车。模型经过优化，**根据记录的人类驾驶时的传感器输入，产生与人类相同的驾驶动作**。收集大量人类驾驶数据的简单性，使得 IL 方法在诸如车道跟随等简单任务中表现良好。然而，对于更复杂和罕见出现的交通场景，这种方法仍然具有挑战性。

这个很容易理解。但它有如下问题：

##### 分布转移问题（Distribution Shift Problem）

自动驾驶使汽车进入训练期间未见过的状态，这被称为分布转移问题，即驾驶时的实际观测与训练期间展示的专家驾驶情况不同。例如，如果专家总是在道路中心附近驾驶，模型就从未见过在偏离道路一侧时如何恢复。

分布转移问题的潜在解决方案包括数据增强、数据多样化和在线策略学习。所有这些方法都以某种方式使训练数据多样化，即通过收集或生成额外的数据。训练数据的多样性对于模型的泛化能力至关重要。

- 数据增强（Data Augmentation）：收集足够大且多样化的数据集可能具有挑战性。相反，可以**通过数据增强生成额外的人工数据**。模糊、裁剪、改变图像亮度和向图像添加噪声是标准方法，也已应用于自动驾驶领域。此外，原始摄像头图像可以进行平移和旋转，就好像汽车偏离了车道中心一样。人工图像需要与从这种偏差中恢复的目标驾驶指令相关联。这种人工偏差足以避免在车道保持中出现误差累积。
- 数据多样化（Data Diversification）：除了数据增强之外，还可以在数据收集期间使训练数据多样化。在记录时，可以向专家的驾驶指令中添加噪声。噪声会使汽车偏离轨迹，迫使专家对干扰做出反应，从而提供如何处理偏差的示例。
- 在线策略学习（On-Policy Learning）：

#### 强化学习（Reinforcement Learning）

强化学习（RL）是一种机器学习范式，系统通过在环境中采取行动来学习最大化所获得的奖励。IL 在训练期间常常难以接触到多样的驾驶场景，RL 对这个问题的抵抗力更强。学习是在线进行的，因此在训练阶段进行充分的探索，能让模型遇到并学习处理相关情况。然而，强化学习的数据效率比模仿学习低，并且在现实世界中应用也颇具挑战。

所以常用的策略是先通过 IL 进行训练，然后通过 RL 进行微调。这种方式减少了 RL 的长时间训练问题，并且能够帮助克服 IL 离线学习的问题。

#### 从模拟到现实世界的迁移（Transfer From Simulation to Real World）

为了避免使用真实汽车时发生代价高昂的碰撞并危及人类安全，可以在模拟环境中训练模型，然后在现实世界中应用。然而，由于模拟和现实世界的输入存在一定差异，如果不采取措施最小化输入分布的差异或调整模型，模型的泛化能力可能会受到影响。使模型适应新的、不同数据的问题，可以通过监督学习或无监督学习来解决。

### 输入模式

驾驶时，ADS 不像人类只有视觉作为输入，基本的摄像头输入、车辆的行驶状态、导航输入、激光雷达等，可以相互补充，可以有效提高 ADS 的准确性。

#### 摄像头视觉（Camera Vision）



#### 语意表示（Semantic Representations）

所谓语意表示，就是 RGB 图像经过算法进行处理后，得到的语义分割、深度图、表面法线、光流和反照率等。虽然原始 RGB 图像包含所有这些信息，但明确提取这些预定义的表示并将其作为（额外）输入，已被证明可以提高模型的鲁棒性。

#### 车辆状态（Vehicle State）

当前速度、加速等等信息。

#### 导航输入（Navigational Inputs）

#### 雷达（LiDAR）

#### 高清地图（HD Maps）

#### 多模态融合（Multimodal Fusion）

在多模态方法中，需要结合来自多个输入源的信息。多种模态可能在计算的不同阶段进行融合。

1. **早期融合**：在将多个输入源输入到可学习的端到端系统之前进行融合。例如，可以按通道连接 RGB 图像和深度图像。在融合之前，可能需要对输入进行预处理（例如转换到相同的参考框架并调整尺寸以匹配）。
2. **中间融合**：在对部分或所有模态进行一些特征提取之后进行融合。特征提取本身通常是端到端模型的一部分，并且是可学习的。对融合后的（例如连接的）特征进行进一步计算，以获得最终输出。
3. **晚期融合**：分别计算每个输入模态的输出。然后以某种方式组合这些单独的输出。一种著名的晚期融合方法是集成，例如使用卡尔曼滤波器或专家混合。

在本综述涵盖的研究中，我们主要遇到早期融合和中间融合方法。早期融合通常在计算上效率最高。最常见的融合技术是连接，即简单地堆叠输入。事实证明，连接 RGB 颜色通道和深度通道是融合 RGB 和深度输入的最佳性能解决方案。Zhou 等人将各种语义地图（分割、深度等）与 RGB 图像进行早期融合。在连接激光雷达和视觉输入的情况下，早期融合和中间融合都已成功应用。车辆状态测量值，如速度和加速度，通常与视觉输入进行中间融合（两种情况均为连接）。Hecker 等人通过将从摄像头馈送中使用长短期记忆（LSTM）模块获得的视觉时间特征，与从地图或 GPS 坐标中提取的特征连接起来，进行中间融合。作为另一种融合方法，Kim 等人使用元素乘法将文本命令与视觉信息进行中间融合。

晚期融合在输入期望的导航命令（左转、右转、直走和继续行驶）方面，已被证明比早期融合更有效。具体来说，导航命令用于在输出分支之间进行切换。或者，Chowdhuri 等人通过中间融合解决了类似的问题（切换行为模式）。

#### 多时间戳（Multiple Timesteps）

驾驶场景的某些物理特征，如自身和其他物体的速度和加速度，**无法从单个摄像头图像中直接观察到**。因此，通过考虑多个过去的输入可能会有所帮助。

- **CNN + RNN**：最常见的是，基于卷积神经网络的图像处理层后面接一个循环神经网络（RNN；最常用的是 LSTM）。RNN 接收提取的图像特征序列并产生最终输出。可以并行使用多个 RNN 来执行不同的子任务。此外，多个信息源（例如激光雷达和摄像头）可以由卷积神经网络处理，连接后一起输入到 RNN 中。或者，对每个信息源分别进行时空特征提取，最终输出基于连接的 RNN 输出进行计算。循环模块也可以堆叠在一起，Kim 等人使用卷积神经网络图像编码器和基于 LSTM 的文本编码器，后面再接一个 LSTM 控制模块。

- **固定窗口 CNN**：或者，可以将固定数量的先前输入输入到空间特征提取卷积神经网络模块中。得到的特征图可以作为后续特定任务层的输入。对于激光雷达，Zeng 等人沿着高度轴堆叠最近的十个激光雷达三维占用网格，并使用二维卷积。

驾驶是一项动态任务，时间上下文很重要。利用过去的信息可以帮助优秀的驾驶模型记住暂时被遮挡的物体的存在，或者考虑其他驾驶员的行为和驾驶风格。

### 输出模式

#### 转向和速度（Steering and Speed）

大多数端到端模型的输出是下一时间步的转向角度和速度（或加速度与刹车指令）。

#### 路标点（Waypoints）

一种更高级的输出模态是**预测未来的路标点或期望轨迹**。已有研究对这种方法进行了探讨。网络输出的路标点可以**通过另一个可训练的网络模型转换为低级的转向和加速度指令**，也可以通过控制器模块实现，例如在一些研究中使用的 PID 控制器。有多种控制算法可用于此类控制器模块，PID 是一种常用的算法。这些控制器模块可以可靠地生成低级的转向和加速度 / 刹车指令，以到达期望的点。为了实现更平稳的驾驶，可以对有噪声的预测路标点拟合一条曲线，并将该曲线作为期望轨迹。

与预测瞬时的转向和加速度指令不同，通过输出一系列路标点或轨迹，模型被迫进行前瞻性规划。使用路标点作为输出的另一个优点是，它们与汽车几何结构无关。此外，基于路标点的轨迹比低级网络输出（如瞬时转向指令）更易于解释和分析。

#### 代价地图（Cost Maps）



### 评估

针对端到端方法开发了一套专用的指标。

开环评估：

闭环评估：

### 可解释性

在发生故障的情况下，理解模型为何如此驾驶至关重要，这样才能避免未来出现类似的故障。

### 可能的最佳实现
