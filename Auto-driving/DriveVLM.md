## DriveVLM

### 一

将学习框架整合到运动规划一直是研究的活跃方向，这包括使用机器学习方法来改进自动驾驶车辆和机器人的运动规划能力。一条有前景的研究方法是结合强化学习和模仿学习，这些方法可以学习一个端到端的规划策略，**直接将原始的传感器输入映射为控制动作，无需复杂的中间表示**。有一些工作通过构建密集的代价图（cost maps）来提高可解释性，这些代价图是从学习模块派生出来的**。代价图是一种表示不同路径或行为的地图，用于指导运动规划决策**。最近的一个趋势是以端到端的方式训练多个模块，这些方法能增强整体表现，但是依赖于未来轨迹预测损失的反向传播，决策过程缺乏可解释性。

DriveVLM 包含了一个由三个核心模块组成的思维链（CoT）：场景描述、场景分析和层级规划。场景描述模块用自然语言描述驾驶环境，识别出场景内关键的目标（车辆、行人、交通标志、信号灯等）；场景分析模块专注于关键目标的特性（速度、大小、方向和可能的行为）以及它们对自车的影响；分层规划模块负责制定计划，从元动作（meta-actions）和决策描述到具体的路径点，规划过程从高层次的抽象动作（如“变道”、“超车”）开始，逐步细化到具体的操作（如通过特定的路径点）。这些模块分别对应了传统智驾方案的感知、预测、规划流程，区别是这些模块处理的是**目标感知、意图级预测和任务级规划**。

- 意图级预测，指的是系统不仅要预测其他道路使用者可能的行动轨迹，还要预测他们的意图和目的，这需要对人类行为有更深入的理解。
- 任务级规划，指的是系统在规划时不仅考虑简单的路径规划，还要考虑更复杂的任务，如在特定情况下需要执行的特定动作或决策。

尽管 VLM 在视觉理解方面很强，但在空间定位和推理方面存在局限性，并且它们**需要消耗大量的计算资源**，作者提出了 DriveVLM-Dual，该混合系统结合了 DriveVLM 和传统自动驾驶系统的优势。DriveVLM-Dual 整合了 3D 感知和规划模块，如 3D 目标检测器、占用网络和运动规划器，能更好地理解目标在三维空间的位置，并且通过高频的路径规划，适应快速变化的驾驶环境。双系统设计类似于人脑的快速和慢速思维过程，可以高效地适应不同复杂性的驾驶场景，灵活地处理简单和复杂的驾驶情况，提高自动驾驶的适应性和安全性。[^1]

### 二

比如，ChatGPT 被用作教师来生成更多关于自车的对话，提示通常遵循 LLaVA 中使用的提示设计

- 为了使 ChatGPT 能够“看到”视频，让YOLOv8 (Reis et al., 2023) 来检测视频每一帧中常见的对象(例如，车辆、行人)，获得的边界框坐标按照LLaVA 进行归一化，并作为特权信息发送给 ChatGPT
  除了物体检测结果外，视频片段的真实控制信号序列和字幕也可以被ChatGPT访问

  比如下图展示了输入ChatGPT的信息，包括视频字幕、控制信号和通过YOLOv8获得的物体检测结果

![img](https://i-blog.csdnimg.cn/direct/052ae332e4134af09aaf8f7e83dad5c8.png)

- 基于这些权信息，ChatGPT被提示生成关于自车、交通信号灯、转向方向、车道变更、周围物体、物体之间的空间关系等多个回合和类型的对话
  最终，他们收集了56K视频-文本指令跟随样本，包括16K BDD-X问答和40K由ChatGPT生成的问答

  ![img](https://i-blog.csdnimg.cn/direct/facdc426ace249ac91b97fbe5dd51ca8.png)

### 三

DriveMLM框架将大型语言模型（LLMs）的世界知识和推理能力整合到自动驾驶（AD）系统中，**实现了在真实模拟器中的闭环驾驶**，如下图图3所示，该框架有三个关键设计：

![img](https://i-blog.csdnimg.cn/direct/3da9d0c5aba5462880f95d66709d6d9c.png)

- 行为规划状态对齐(Behavioral Planning States Alignment)
  这部分将LLM的语言决策输出与像Apollo这样的成熟模块化AD系统的行为规划模块对齐。通过这种方式，LLM的输出可以轻松转换为车辆控制信号
  MLLM规划器(MLLM Planner)
  它是多模态分词器和多模态LLM（MLLM）解码器的结合
  多模态分词器将多视角图像、LiDAR、交通规则和用户需求等多样化输入转化为统一的token，MLLM解码器基于统一token做出决策
  The multi-modal tokenizer transforms diverse inputs like multi-view images, LiDAR, traffic rules,and user requirements into unified tokens, and the MLLM decoder makes decisions based on the unified tokens

- 在推理过程中，DriveMLM框架利用多模态数据进行驾驶决策，这些数据包括：
    多视图图像，其中表示时间长度，表示视图数量，和表示图像的高度和宽度
    来自LiDAR点云的点云，其中代表点的数量
    系统消息，其中表示系统消息标记的数量。系统消息是任务定义、交通规则和决策状态定义的集合
    用户指令，其中代表用户指令token的数量

  这些输入通过多模态tokenizer进行tokenization，结果为：，分别代表多视图图像、LiDAR点云、交通规则和用户指令的token嵌入。这里，表示由QFormer [32]的查询数量决定的输出token数量，每个token嵌入具有维度

- 接下来，这些token被输入到MLLM解码器中，生成决策状态token以及相应的解释
  最后，决策状态S被输入到运动规划和控制模块中。该模块计算用于车辆控制的最终轨迹
  高效数据收集策略
  它为基于LLM的自动驾驶引入了一种量身定制的数据收集方法，确保了涵盖决策状态、决策解释和用户命令的全面数据集

### Reference

[1^]: https://blog.csdn.net/calvinpaean/article/details/142616173

[2^]: 自动驾驶(多模态)大模型综述(https://blog.csdn.net/v_july_v/article/details/141113628?utm_medium=distribute.pc_relevant.none-task-blog-2~default~baidujs_baidulandingword~default-0-141113628-blog-142616173.235^v43^pc_blog_bottom_relevance_base9&spm=1001.2101.3001.4242.1&utm_relevant_index=2)

