## Transformer 原理

transformer 作为 LLM 发展的基石，对于理解 LLM 未来的发展方向，以及 AI 芯片的设计需求至关重要。

### 资源占用

#### 概述

对于标准 Transformer 模型，不管是 Encoder Only 的 Bert 系列模型，还是 Decoder Only 的 GPT 系列模型，同配置下参数量和计算量都是类似的。其中的一个关键点是：标准 Transformer block（层）输入、输出以及中间 Hidden Dim 保持不变，始终是 Token Embedding 的 Hidden Dim，所有的 Transformer Block 都非常规整。

如下图所示，Encoder主要参数都来自几个矩阵乘的 Weight 矩阵，其中 d 表示 Token Embedding 的 Hidden Dim，l 表示 Token 数，h 表示 MHA 中的 Head 个数，dFFN 表示 FFN 层中间升维后的 Dim。其主要几个模块的参数量如下。

- MHA：WQ，WK，WV 的大小都是 d x d。当然这里也可以从 h 个 Head 的角度去看，则每个 Head 的 WQ，WK，WV 为 d x d/h。在 MHA 的最后还有一个矩阵乘操作，对应的 Wout 维度依然为 d x d。所以MHA处权重矩阵的参数量是 3d×d+d×d。
- FFN：标准 Transformer 的 FFN 中有两个 Linear 层（先升维再降维），对应权重矩阵 W1 和W2 的大小都是 dFFN x d，并且标准的 dFFN 为 4d，也就是说 FFN 处两个权重矩阵的参数量为 8d x d。

![image-20250725100113027](C:\Users\xpeng\AppData\Roaming\Typora\typora-user-images\image-20250725100113027.png)

综上，在标准的 Transformer 模型或者 LLaMA 系列（MHA）中，如果忽略词表、Embedding、LayerNorm 等参数后，总参数量为（所有 Transformer Block）： N=nlayer×(nmha+nffn)=nlayer×(3d×d+d×d+8d×d)=12×nlayer×d×d

注意：本章参考了多篇论文，其中对术语的定义各不相同，因为模型结构也不同，所以计算结果与其它资料可能也有差异。

#### 参数显存占用

有参数的模块才会占用显存。这部份的显存占用和输入无关，模型加载完成之后就会占用。一般的卷积层都会占用显存，而我们经常使用的激活层Relu没有参数，所以不会占用缓存。

##### 有参数的层

常见的有参数的模块主要包括：

- 卷积层，通常的conv2d；
- 全连接层，也就是Linear层；
- BatchNorm层；
- Embedding层；

##### 无参数的层

常见的无参数的模块主要包括：

- 多数的激活层，比如Sigmoid/ReLU；
- 池化层；
- Dropout；

##### 所需资源

我们可以用如下公式来计算神经网络的显存占用：显存占用 = 模型显存占用 + 输入输出相关的显存。模型显存占用是模型中与输入无关的显存占用，主要包括：

- 模型权重参数；
- 梯度（一般是参数量的1倍）；
- 优化器的动量（和具体优化器密切相关，比如普通SGD没有动量，momentum-SGD动量与梯度一样，Adam优化器动量数量是梯度的两倍）；

输入输出相关的显存占用主要如下：

- batch_size × 每个样本的显存占用；
- 每一层的feature map，需要保存激活来进行反向传播；

因为 反向传播 / Adam-优化 / Transformer架构 等因素，一般来说，训练需要的显存，是 同样规模推理 的 3-4倍。

#### transformer 参数量

以Decoder only模型为例，其主要包括 3 个部分：embedding，decoder，head。最主要部分是decoder，其由若干个decoder-layer组成，每个decoder-layer又分为两部分：MHA和FFN。我们接下来逐一看看这些模块的参数量。

##### 术语

我们先给出本节使用的术语。

| Symbol | Meaning                                                      |
| ------ | ------------------------------------------------------------ |
| d      | 模型的词嵌入大小（The model size / hidden state dimension / positional encoding size） |
| h      | 注意力头个数                                                 |
| s      | 文本总长度（prompt+解码器输出）                              |
| b      | 数据batch size（批大小）                                     |
| l      | Transformer层数                                              |
| v      | 词表大小                                                     |

##### embedding层

embedding层的输入形状是[b,s,v]，输出形状是[b,s,d]，参数量为v×d。如果采用可训练式的位置编码，会有一些可训练模型参数，但是其数量比较少。如果采用相对位置编码，例如RoPE和ALiBi，则不包含可训练的模型参数。因此我们忽略位置编码的参数。

##### Transformer层

Transformer模型由 l 个相同的层组成，每个层主要分为两部分：MHA和FFN。因为多头只是逻辑上切分，物理上没有增加模块，因此后续讨论中省略多头（某些论文中如果讨论多头相关，我们会以论文为准），而又因为Decoder only模型使用的是自注意力，因此接下来我们认为 Q、K、V、O的维度相等。

**MHA**

MHA中包含四个权重矩阵WQ,WK,WV,WO以及偏置（某些模型可能没有偏置）。4个权重矩阵的形状为 [d,d]，4个偏置的形状为 [d]，其中 d=h×dhead。因此，多头注意力层参数量为：4×(d×d+d)=4d2+4d。

**FFN**

FFN包括两个线性层。

- 第一层将原有的维度映射到4倍原维度大小，即从d映射到4d。权重矩阵形状是[d, 4d]，偏置形状是[4d]。参数量为：d×4d+4d
- 第二层从4倍维度降维回原始维度。即从4d映射到d。权重矩阵形状是[4d, d]，偏置形状是[d]。参数量为： 4d×d+d

最终FFN的参数是：8d2+5d。

**LayerNorm**

对于Layer Norm来说，其缩放参数 γ与平移参数 beta 维度都为 d，因此参数量是 2×d。因为MHA和FFN都有LayerNorm，因此总参数量是4×d。

综上，单个Transformer层的参数量是：12d^2+13d。

##### lm_head

lm_head是自然语言处理模型中的一个组件，主要作用是将模型的输出（通常是经过Transformer编码器处理后的隐藏状态）转换成预测下一个词的概率分布。

Head与embedding的参数量相同。如果是tied embedding（即，head权重矩阵与词嵌入矩阵是参数共享的），则两者公用一个参数。

##### 最终参数量

最终，l 层transformer模型的可训练模型参数量为l(12d^2+13d)+2vd 。当d较大时，可以忽略一次项，模型参数量近似为12ld^2 。

![img](https://img2024.cnblogs.com/blog/1850883/202503/1850883-20250321181134848-1724419527.jpg)

#### transformer 计算量

广义上，当处理一个 token 时，模型执行两种类型的操作：注意力计算和矩阵-向量乘法。

- MHA（红框）：WQ，WK，WV 对应的计算量都为 2 x (d x d x l)，**其中 2 表示一个乘法和一个加法**；
- MHA（蓝框）：Wout 对应的计算量为 2 x (d x d x l)；
- MHA Attention（绿色圆角方块）：计算量是2 x (l x d/h x l + l x d/h x l) x h = 4 x d x l x l。如果是 Decoder（LLM），由于 Causal Mask 的存在，此处的计算量应该减半，也就是 2 x d x l x l；
- FFN（绿框）：W1 和 W2 对应的计算量为 2×(dFFN×d×l)和 2×(d×FFN×l)。LLaMA 的 SwiGLU 类似；

![img](https://img2024.cnblogs.com/blog/1850883/202503/1850883-20250321181336825-1748092788.jpg)

我们后续也按照megatron论文的术语进行分析，忽略多头，即头数为1（考虑多头的话，应该再乘以 h）。

##### 矩阵乘法

在decode阶段，则主要是矩阵-向量乘法。一个大矩阵乘以一个向量，得到另一个向量。

因此我们首先看看矩阵乘法的计算特点。人们定义算术强度（Arithmetic Intensity）为FLOP : I/O。当将一个N×M矩阵与一个M×P矩阵相乘以产生一个N×P矩阵时，矩阵-向量乘法对每个矩阵元素执行一次乘加运算。FLOP（浮点操作，即计算量）为2M×P×N，I/O（从GPU内存传输到GPU寄存器的数据传输）计数为M×N+M×P+N×P 。

##### 前向传播计算量

**Embedding**

Embedding操作的输入是[b,s]。在实际计算的矩阵-向量乘法中，embedding操作并不会使用这整个embedding大矩阵，每个 token 只读取这个矩阵中的一行，就是查表操作。最终输出张量变成[b,s,h]。因此计算量相对很小，后面我们将忽略这部分。

**MHA**

在标准的Transformer计算中，假设Q,K,V∈$R^{s×h}$，则计算如下（省略了$\sqrt h$）。N是序列长度，h是维度。

- 获取注意力分数 ：S=Q$K^T$∈$R^{s*s}$。对每个 query 向量，都计算它与所有位置的 key 向量之间的点积；
-  获取注意力权重：P=softmax(S)∈$R^{s×s}$。即归一化得到的一组标量；
- 计算最终输出：O=PV∈$R^{s×h}$。使用注意力权重，对所有之前的 value 向量进行加权求和来计算一个向量o；

因此我们可以知道，计算S和O是主要的部分。

- **计算Q、K、V**：单个矩阵乘法是：[b, s, h] x [h, h] 得到 [b, s, h]，因此其计算量是2bs$h^2$。三个矩阵的计算量是 3×2bs$h^2$=6bs$h^2$；

- **QK^T**：在这个阶段，针对每个query元素，注意力计算会对每个键元素执行一次乘加操作以计算点积。总体操作为：[b, s, h] x [b, h, s] = [b, s, s] ，其计算量是：2b$s^2$h。softmax 函数不会改变输入矩阵的维度，即 [𝑠,𝑠]→[*s*,*s*]，native softmax 的 `FLOPs` 为 (4/5)*sh*。因为比较小，所以可以忽略。缩放 $\sqrt h$ 是逐元素操作，也可以忽略；

- **乘以V**：乘以V（attention over values）阶段会对每个值元素执行一次乘加操作以计算加权和。总体操作为： [b, s, s] x [b, s, h] = [b, s, h]，计算量 2b$s^2$h；

- **线性映射**：线性映射(post-attention linear projection)这一步是与WO的多头融合，矩阵乘法的输入和输出形状为 [b,s,ℎ]×[ℎ,ℎ]→[b,s,ℎ] 。计算量 2bs$h^2$ ；

- **MLP**：这一步涉及两个操作。

  - 第一个线性层，矩阵乘法的输入和输出形状为 [b,s,ℎ]×[ℎ,4ℎ]→[b,s,4ℎ] 。计算量为 8bs$h^2$ 。

  - 第二个线性层，矩阵乘法的输入和输出形状为 [b,s,4ℎ]×[4ℎ,ℎ]→[b,s,ℎ] 。计算量为 8bs$h^2$ 。


**LayerNorm**

`LayerNorm` 操作是逐元素进行的，因此不存在通用的计算公式。**`LayerNorm` 层的两个权重都是一个长度为 ℎ 的向量，`FLOPs` 可以预估为: 2ℎ，但通常忽略不计。**

**单层layer**

将上述计算量相加，得到前向传播阶段中每个transformer层的计算量大约为 $24bsℎ^2+4bs^2ℎ $，可以发现：

- 参数量和计算量跟head数量无关（？公式中不是有 h 么，怎么无关了），head划分更多是通过特征子空间划分提高精度，而不是为了节省参数量或者计算量；
- 回忆参数量是$12lh^2$，所以在给定固定序列长度的情况下，计算量也随着参数的数量增加而线性增加。
- 计算复杂度随着序列长度的增加呈二次方增加的趋势。

| Attention   | 计算量 | FFN                   | 计算量            |
| ----------- | ------ | --------------------- | ----------------- |
| 计算Q、K、V | 6bsh2  | 第一个线性层          | 8bsh2             |
| QK^T        | 2bs2h  | 第二个线性层          | 8bsh2             |
| 乘以V       | 2bs2h  | 每个transformer层总计 | $24bsℎ^2+4bs^2ℎ $ |
| 线性映射    | 2bsh2  |                       |                   |

##### 综合思考

模型的训练包含前向传播和反向传播过程。上述只是主要考虑到前向传播阶段中，Transformer的计算量。我们接下来结合反向传播来综合考虑。反向传播过程实际上包含两部分，一部分是对输入的梯度的计算，一部分是对权重的梯度。其实这两部分主要的计算量都是矩阵乘法，并且大小与 前向传播中的计算量大小一致，因此往往会直接把反向传播的计算量近似为前向传播的 2 倍。

**反向传播**

我们把反向传播加进来继续分析。

**单层**

单个Transformer层的计算量现在如下：

- 前向传播所需要的浮点数运算：$24bsℎ^2+4bs^2ℎ $；

- 对于backward，对于神经网络中的权重和输入需要计算梯度，因此反向传播需要2倍FLOPs；
- 如果使用activation checkpointing：在backward的时候，每一层需要额外的计算forward；

所以每层需要的总浮点数计算为$4×(24bsh^2+4bs^2h)=96bsh^2(1+s/6h)$。

**logits**

另一个耗费计算量的部分是 logits 的计算：**将隐藏向量映射为词表大小，得到每个 token 对应的 logits 向量。**矩阵乘法的输入和输出形状为 [b,s,ℎ]×[ℎ,V]→[b,s,V] 。矩阵乘法的输入和输出形状为: [𝑠,ℎ]×[ℎ,𝑉]−>[*s*,*V*]。

因此前向传播需要 2bsℎV ，反向传播需要 4bsℎV ，总体需要 6bsℎV 的计算量。

**总体计算量**

Megatron-LM的经典论文 "Efficient Large-Scale Language Model Training on GPU Clusters Using Megatron-LM"给出了一个计算标准Transformer-decoder结构浮点数运算的公式。对于一个 l 层的transformer模型，输入形状为 [b,s] 时，其计算量如下。

- 单次推理，前向传播所需要的浮点数运算：$l×(24bsh^2+4bs^2h)+2bshV$

- 单次训练，前向后向传播需要浮点运算为：

  $$96blsh^2(1+\frac s{6h}+\frac V{16lh})+2bshV$$

如果没有如果使用activation checkpointing，则是 $$72blsh^2(1+\frac s{6h}+\frac V{16lh})+2bshV$$

在Megatron-Deepspeed的代码里，我们也能看到用这个公式来计算TFLOPS（每秒所执行的浮点运算次数，floating-point operations per second）：

```python
# General TFLOPs formula (borrowed from Equation 3 in Section 5.1 of
# https://arxiv.org/pdf/2104.04473.pdf).
# The factor of 4 is when used with activation check-pointing,
# otherwise it will be 3, but for 200B model, activation check-pointing will always be on.
checkpoint_activations_factor = 4 if args.checkpoint_activations else 3
# GLU activations double the hidden states in the upscaling feed-forward in each transformer layer
# This leads to 16bsh^2 instead of 8bsh^2 per first feed-forward layer in MLP, thus we increase the coefficient by 8.
# Refer to https://github.com/bigscience-workshop/Megatron-DeepSpeed/pull/283#issue-1260805063 for more details.
coefficient = 32 if args.glu_activation else 24
flops_per_iteration = (coefficient * checkpoint_activations_factor * batch_size * seq_len * num_layers * (hidden_size**2)) * (1. + (seq_len / (6. * hidden_size)) + (vocab_size / (16. * num_layers * hidden_size)))
tflops = flops_per_iteration / (elapsed_time_per_iteration * args.world_size * (10**12))
```

##### 计算特点

**与参数量的关系**

我们先给出结论：计算量主要和模型参数和token数相关。假设数据集中总共包含 D 个 Token，模型参数量为N，则对于序列不是特别长的场景，所有 Token Forward的计算量可以近似为2ND。

**单次推理**

单次推理时候，计算量和参数量的关系如下：

$$\frac {计算量}{参数量}=\frac {l∗(24bsh^2+4bs^2h)+2bshV}{l(12h^2+13h)+2vh}≈\frac {24lbsh^2}{12lh^2}=2bs$$

因为单次推理时输入的token数为bs，因此可以近似认为，在一次前向传播中，对于每个token，每个模型参数需要进行2次浮点运算（一次乘法，一次加法）。即从单个 Token 单个矩阵乘的视角，可以近似认为，单次推理时（只包含正向传播）的计算量就是参数量的 2 倍，就是每个 token 过一遍所有参数的计算量。

一次迭代训练包含了前向传播和后向传播，后向传播的计算量是前向传播的 `2` 倍。因此，即一次迭代训练中，对于每个 token 和 每个模型参数，需要进行 6 次浮点数运算。

在论文"Scaling Laws for Neural Language Model"中也有类似的计算公式，具体如下图所示。

![img](https://img2024.cnblogs.com/blog/1850883/202503/1850883-20250321181401046-827998950.jpg)

**单次训练**

一次训练迭代包含了前向传播和后向传播，因为反向传播计算量是前向传播的2倍，所以单次训练，对于每个token，每个模型参数需要进行6次浮点运算。训练总算力（Flops）= 6 * 模型的参数量 * 训练数据的 token 数。这就是所有训练数据过一遍训练所需的算力。如果需要训练需要多少时长，则可以近似使用下面公式：

$$\frac {6×模型参数量×数据量}{(GPU数量×GPU FLOPS×GPU利用率)}$$

**带宽受限**

算力并不能说明一切，模型还需要访问 GPU 内存，内存带宽也可能成为瓶颈。因为需要把参数从内存里面读出来吧？内存访问量 = 参数数量 * 2 bytes。针对内存带宽部分，大语言模型中的计算具备一些鲜明的特点。我们一一进行分析。

**注意力计算**

在大语言模型推理中，注意力计算是访存密集型的，其耗时受限于硬件的访存带宽，而非运算速度。

对于矩阵乘法算子，其特点如下。

- 参数量太大会导致矩阵乘法算子成为访存密集型。当输入的数据不够多，运算量不够大的时候，这些算子会因为参数访存过多而受限于访存带宽。
- 计算量将随着Batchsize增长而快速增加。当Batchsize小于16时，我们可以认为矩阵乘法算子为访存密集型的。只有当Batchsize充分大时，矩阵乘法算子才会变成计算密集型的，它们的性质会随着Batchsize变化而变化。

**FFN计算**

对于FFN算子，在大多数端侧应用中，我们都是以Batchsize=1的方式去调用大语言模型，此时网络中大部分的计算量和访存量都集中在FFN中。大语言模型整体的运算-访存比极低，整个网络都将是访存密集的，其运行耗时完全受限于访存带宽而非硬件算力。

**KV Cache的影响**

KV Cache是对注意力优化的重要途经。它本质上是文本中每个之前位置的 key 向量和 value 向量的集合。这项技术的出现大大缩减了Self Attention的计算量。这使得在KV Cache技术出现后，可以把推理流程分为prefill和decode阶段（我们会在后文详细分析）。下图就是decode阶段对应的图例。概括地说，其中包含了两种算子：

- 自注意力（*Self-Attention*，黄色标出）涉及矩阵-矩阵乘法。
- 密集投影（*Dense Projection*，绿色标出）涉及向量-矩阵乘法。

Self Attention算子的计算特点非常显著：这是一个运算访存比接近1:1的访存密集型算子。对其访存量和计算量进行理论估计，可得发现，其内存访问量和计算量的复杂度都是O(batch size×sequence length×hidden dimension)。作为对比，对MatMul和FeedForward（都是矩阵乘法算子）做类似的估计，可得结论：其内存访问量和计算量的复杂度都是O(batch size×hidden dimension)。

![img](https://img2024.cnblogs.com/blog/1850883/202503/1850883-20250321181427443-822489092.jpg)

**prefill**：MHA 块的 `FLOPs`: $$8sh^2+4s^2h$$。FFN的是$$16sh^2$$；

**decode**：`MHA` 层每一轮解码的 `FLOPs`: $8h^2+4(s+1)h$。FFN的是$16h^2$。

**总体**

而在输入数据形状为 [𝑏,𝑠]的情况下，一次训练/推理：

`prefill` 阶段每轮总计算量：$b×(24lh^2s+4lhs^2)+2bshV)=24lh^2bs+4lhbs^2+2bshV$，可以粗略的以 $24bslh^2$ 计算；

`decode` 阶段每轮总计算量：$b×(8lh^2+4lh(s+1)+16lh^2)+2bhV=24lh^2b+4lhb(s+1)+2bshV$，可以粗略的以 $24blh^2$ 计算；

而模型的参数量计算公式为：$12lh^2$，所以对于 prefill 阶段，每次推理的计算/访存比为：$2bs$；对于 decode 阶段，每次推理的计算/访存比为：$2b$。我们以 Thor-U 为例，其 INT8 算力约为 1000TOPS，带宽为 273GT/s，其计算/访存比为：1000000GOPS/273=3663。如果每次推理的输入矩阵为 448*448，那么 prefill 阶段，推理的计算/访存比为 2\*448\*448=401408；decode 阶段，推理的计算/访存比为 2\*448=896。所以在 prefill 阶段，瓶颈在算力，而在 decode 阶段，瓶颈在于带宽。

**kv cache 节省了多少计算量**

对于上下文长度 *s*，不使用 kv cache d的 self-attention 的总计算量复杂度为：总计算量：O(s3h)，使用后的总计算量近似为 O(s2h)。计算量节省比率：

$$节省比率=O(s^3h)−O(s^2h)=1−\frac 1s$$ 

计算复杂度从 $O(s^3h) $降低到 $O(s^2h)$，即使用 kv cache 可节省约 𝑠 倍的计算量。当 𝑠较大时，1/s接近于 0。输出 tokens 数越多，计算量节省越可观。

### 计算流程分析

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

### 加速方案

#### 理想汽车

##### DriveVLM

这个是最先提出来的全局端到端方案，对应的论文是《DRIVEVLM: The Convergence of Autonomous Driving and Large Vision-Language Models》，论文发布时间是2024.06.25。方案架构如下图所示。

![image-20250803111401000](C:\Users\xpeng\AppData\Roaming\Typora\typora-user-images\image-20250803111401000.png)

受限于智驾芯片的算力和带宽，车端部署使用的 Qwen1.5 1.8B 的 LLM，加上 visual encoder (1.9 billion), vision-language adapter (0.08 billion)，共计 2B 左右的参数。输入的图像被调整为 448\*448（入参大小会影响计算量，图像质量越高，计算量越大，每次推理的计算量可以使用  24bs*lh^2 计算，其中 b,s 表示输入的矩阵大小，l 表示 transformer 的层数，h 表示隐藏维度。）。从测试效果来看，模型参数越大，各种测试的效果越好，4B 的模型效果要好于 1.8B 的模型。

![image-20250803144254491](C:\Users\xpeng\AppData\Roaming\Typora\typora-user-images\image-20250803144254491.png)

部署情况：在配备两个 OrinX 处理器的自动驾驶车辆上部署 DriveVLM-Dual，OrinX-1 上部署高频端到端驾驶系统，OrinX-2 上部署 DriveVLM。首先看一下 orin-x 的性能参数，Ampere 架构的 GPGPU，Tensor Core 提供两种精度的算力：INT8 精度，稀疏算力为 170 TOPS 稠密算力为 85TOPS；或 FP16 精度，稀疏算力为 85TFLOPs，稠密算力为 43TOPS。CUDA Core 也提供两种精度的算力：FP32 精度，算力为 5.3TFLOPs；或 FP16 精度，算力为 10.6TFLOPs。第二代 DLA，用于支持卷积运算，**提供 INT8 稀疏算力为 105 TOPS**，INT8 稠密算力为 11.4 TOPS，可以在 DLA 上以 INT8 或 FP16 精度运行各种神经网络。LLM 的计算主要使用 GPGPU，DLA 在 GEMM 方面的运算能效不高。

从目前公布的情况来看，有如下几个关键时间点：

- 原始 VLM，**推理耗时 4.1s**。应该指未量化的模型，使用 FP16 精度；

- LLM GPTQ 量化，**耗时 1.9s**。Qwen1.5 1.8B 本身提供一个 INT8 类型的 GTPQ 模型，假设 Qwen1.5 1.8B 的模型层数为 24，隐藏维度为 2048，输入图像为 448*448，**那么每次推理的计算量为 24bs\*lh^2 = 24x448x448x24x2048x2048 = 484,884,627,849,216OPS，即 485TOPS，而 Orin-X 的 INT8 稀疏算力为 275 TOPS，大致是符合的，由于使用的模型参数较少，204GB/s 的带宽能够满足要求，所以该阶段，瓶颈在于算力；**

- ViT 算子融合，**耗时 1.4s**。推测该操作为将 ViT 中多个操作化简合并，减少计算量；

- LLM 投机采样，**耗时 0.7s**。所谓“投机采样”，指的是用小模型的输出去投机。在特定任务场景中，存在很多样板话，小模型也能生成的很好。比如我希望写一个 SQL 来获取所有成绩大于 30 分的学生，

  ```
  SELECT * FROM student WHERE score > 30
  ```

  这个前面的 SELECT * FROM 就是样板话，小模型也能正确生成。让小模型先生成一段简单的，再让大模型去生成后面一段复杂的，这样就能减轻大模型的运算量。

- 流式视频解码器，**耗时 0.3s**。暂无资料介绍该优化方案；

##### MindVLA

这个是迭代方案，和 DriveVLM 是两套完全不同的方案。LLM 不再使用开源的 Qwen，而是使用从零开始训练的 MindGPT，**其使用 8x0.4B 的 MoE 架构，同时Thor-U 也支持 MoE 架构，这是其能够以 10Hz 部署在车端的核心原因。**目前的信息来源是：理想汽车自动驾驶技术研发负责人贾鹏在NVIDIA GTC 2025发表主题演讲[《VLA：迈向自动驾驶物理智能体的关键一步》](https://mp.weixin.qq.com/s/Q0XBU4fOFHNlAxRqTf48AA)。

该图解释了 MindVLA 方案和 DriveVLM 方案的本质区别，LLM 直接输出轨迹。

![image-20250803115833893](C:\Users\xpeng\AppData\Roaming\Typora\typora-user-images\image-20250803115833893.png)

该图介绍了 MindVLA 各个阶段使用的优化方案，

![image-20250803142405942](C:\Users\xpeng\AppData\Roaming\Typora\typora-user-images\image-20250803142405942.png)

该图概括了 MindGPT 模型的框架，MoE + Spare Attention，同时减小了词表，使用投机采用，提高模型推理速度。

![image-20250803142540770](C:\Users\xpeng\AppData\Roaming\Typora\typora-user-images\image-20250803142540770.png)

该图显示了 MindGPT 的推理流程，其是一个 decoder only 的模型，对于输出的结果使用 RLHF 强化训练，使之更符合人类的驾驶习惯。

![image-20250803142753204](C:\Users\xpeng\AppData\Roaming\Typora\typora-user-images\image-20250803142753204.png)

从目前公布的情况来看，有如下几个关键时间点：

- 原始 VLA，**推理耗时 450ms**。我们依旧假设 MindGPT 的模型层数为 24，隐藏维度为 2048，输入图像为 448*448，那么每次推理的计算量为 24bs\*lh^2 = 24x448x448x24x2048x2048 = 484,884,627,849,216OPS，即 485TOPS，而 Thor-U 的 INT8 算力约为 1000TOPS，推理耗时符合预期，**由于采用了 MoE 架构，每次需要加载的参数量保持在 0.5B 左右，该方案的模型推理速度瓶颈依旧在算力**。

  **排除 Orin-X 到 Thor-U 算力提升的因素，使用 MoE 架构，使得模型的整体参数倍增，但推理速度不变。**

  Orin-X 不支持 MoE 架构，暂不知道在 Orin-X 上怎样部署 VLA。

  - MindGPT 是 8*0.4B 的 MoE 架构，共有 8 个专家（E1~E8），每个专家的参数量为 0.4B，加上 3D projector, Diffusion Decoder 等模块，参数量达到 4B。MoE 模型是传统的 transformer 模型执行流程有所区别，其将**传统 Transformer 模型中的每个前馈网络 (FFN) 层替换为 MoE 层**，然后由门控函数(Router)负责协调专家计算，即判定哪个输入样本应该由哪些专家处理，哪些专家将被激活并参与到当前的计算中。这样每次推理，就只有少数专家参与计算，计算量极大的减少了，所以原始 VLA 的推理速度也远大于原始 VLM 的 4.1s，因为参与计算的参数量不是一个量级；
  - Sparse Attention。attention 是 transformer 的核心机制，标准的Attention机制在计算时需要考虑到每一对输入元素之间的关系，这会导致计算量和内存消耗急剧增加，尤其是在处理长序列时。**Sparse Attention（稀疏注意力）通过在计算时仅关注部分重要的输入元素**，从而大大减少了计算和存储的开销。其能够将计算量从  O(n^2) 降到  O(n) 或 O(n \log n) ，由于不需要存储整个注意力矩阵，Sparse Attention 能够降低对内存容量及带宽的需求。但是，选择怎样的稀疏模式，才既保证计算效率，又不损失重要的信息，是工程化面临的主要挑战；同时稀疏化需要特殊的数据结构和算法来处理稀疏矩阵的乘法等操作，会增加模型部署复杂度。

- 自研跨平台推理引擎，**耗时 200ms**。暂无详细信息，所谓跨平台，应该是指跨 Thor 和 Orin 平台；

- 图像、点云零拷贝传输，**耗时 154ms**。从系统角度来看，camera, lidar 工作时需要申请一块内存 A，用于存储图像、点云信息，然后在后续特征化过程中，3D feature 模块还需要重新申请内存 B，然后将图像、点云数据从 A 拷贝到 B 中，考虑到 camera, lidar 的帧率在 30Hz 左右，这些耗时很客观，系统级的优化能降低这些耗时；

- 自研高性能算子库，**耗时 128ms**。调整算子，让算子和芯片目前的能力更匹配，魔改 CUDA 底层，重写 PTX 底层指令以提高算子执行性能；

- 投机采样&词表精简，**耗时 109ms**。

  - 投机采样上面介绍过；
  - 词表精简。在 LLM 中，词表(vocabulary)是模型能够识别和处理的所有 token(标记)的集合。标准 LLM 词表大小通常在 **30,000-50,000个token** 之间，某些大型模型(如PaLM)可能达到 **256,000** 甚至更大。词表大小直接影响模型输出层的参数量：`hidden_size × vocab_size`，例如：7B模型中，若hidden_size=4096，vocab_size=50,000，则输出层参数约为205M(占总参数3%)，精简词表可显著减少这部分参数，从而降低计算量以及内存使用量；

- FP8 量化，**耗时 96ms**。FP8 量化这一优化方案对性能提升并不明显，推测原模型应该是 FP16/FP32 的，**在 Thor 量产之前量化到 INT4 和 FP16 混合精度，Thor 新增了 FP8 精度，所以在 MindGPT 上进一步量化到 INT4, FP8 和 INT8 混合精度。**这是芯片支持混合精度对算法性能提升的直观体现。

除此之外，郎博士还提到一个关键点：自研芯片的核心原因是作为一个专用芯片能够针对自己的算法进行特定的优化处理，性价比和效率都会很高。当前使用 Thor 芯片是因为其对新的算子支持较好，算力充足，同时目前整体 VLA 方案依然存在变化的可能性。如果未来算法锁定，未来更好的效率和成本，大家都会考虑自研芯片。实际情况却是如此，以图灵芯片为例，由于没有兼容 CUDA 生态，算子的支持并不全面，模型在部署过程中，经常会遇到某个算子不支持，需要重新开发算子，这会拖慢模型的部署进度，从而无法像理想这样跳过生态兼容这一步，直接研究如何优化模型运行性能。

对应的优化方案：

- 自研的基座架构：MindGPT，MoE 架构，0.4x8B，效率高于开源模型；
- 从 2024 年开始用 Orin 芯片做大模型部署，魔改 CUDA 底层，重写 PTX 底层指令；
- 调整算子，让算子和芯片目前的能力更匹配；
- 自研芯片的核心原因是作为一个专用芯片能够针对自己的算法进行特定的优化处理，性价比和效率都会很高。当前使用 Thor 芯片是因为其对新的算子支持较好，算力充足，同时目前整体 VLA 方案依然存在变化的可能性。如果未来算法锁定，未来更好的效率和成本，大家都会考虑自研芯片；
- 在部署时的效率和蒸馏上做了很多平衡。最开始训练了 32B 的模型，然后蒸馏到 0.4x8B MoE 模型上。更进一步的细节上，针对 Diffusion 做了很多工程优化，并不是直接使用标准的 Diffusion，而是进行了推理的压缩，即进一步的蒸馏。以前的 Diffusion 可能需要推理 10 个步骤，在使用 flow matching 流匹配之后只需要 2 步就行。这方面的压缩时能够部署 VLA 的本质；
- 使用 QAT 量化，将 FP32 量化到 INT8/FP8。但是这个量化过程由于精度下降，模型会变得不稳定；
- 做了大量的数据清理，将不好的数据去掉，让模型训练变得稳定，变得收敛。将模型量化到低精度，需要在训练上花更多的精力，做更多的数据迭代、数据清理；

