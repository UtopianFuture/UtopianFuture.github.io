## LLM  accelerate

### llm特性总结

![llm_feature](D:\gitlab\ADAS\llm_feature.webp)

大模型推理可以分解为**处理prompt的阶段（prefill）和自回归生成（generate）输出标记解阶段（Decode）**。二者性质不同，**前者是计算密集型的（gemm运算），后者是访存密集型的（gemv）**。

内存开销巨大：

- 庞大的参数量。7B模型仅权重就需要14+G内存；
- 采用自回归生成token,需要缓存Attention的k, v, 且kv cache的size会随着序列长度线性增大，带来巨大的内存开销；

**动态shape：**

- 云端多batch时，batch size不固定；
- prompt请求序列长度不固定；
- Token逐个生成，且生成的token数量不定（llama 3.1上下文长度达到 128K）。

![token_size](D:\gitlab\ADAS\token_size.png)

#### 边缘端vs云端

|          | 边缘端                                          | 云端                                                         |
| :------- | :---------------------------------------------- | :----------------------------------------------------------- |
| 特点     | 一般是单batch推理                               | 一般是多batch推理模型规模大，序列长度大                      |
| 内存占用 | 单batch，权重占用的内存远大于kv cache           | 在多batch的情况下，kv cache对内存的占用远大于权重            |
| decode   | 单batch，更容易出现memory bound，压缩权重更迫切 | 多batch时，linear运算可以通过拼batch，来提升算力密度；但attention无法拼batch，因此kv cache会带来严重的memory bound，压缩kv cache更迫切 |

#### 推理特性

以下数据摘自论文splitewise（微软），数据图表来源于实际的线上任务。

- 不同的推理任务中，在Prefill和Decode的序列长度分布上非常的不同。下图中coding是指代码补全任务
  - 图（a）可以看出，Coding和Conversation输入Prompt长度的中位数分别为1500个Tokens和1020个Tokens；
  - 图（b）看出对于产生的token，Coding和Conversation分别有12个tokens和129个tokens；

1. ![img](https://xiaopeng.feishu.cn/space/api/box/stream/download/asynccode/?code=MGI1YTYxODlkMTE1YzJjOTZkNjdkNDAyNGRiZDk2MThfeGxIYm5wQUtEb0tZMXZhZk1WWDVTMXRvNGd3N1Z2WWtfVG9rZW46RHpVMmIyejl2bzhUNEN4SDVBd2NuQmI1bkpoXzE3NTE2MTQzODI6MTc1MTYxNzk4Ml9WNA)

- 对于大多数请求，E2E的时延大部分花在Decode阶段

![img](https://xiaopeng.feishu.cn/space/api/box/stream/download/asynccode/?code=ODhkYjJiMWFjMGNlMDFmNjI4YTgyZmJiZDNlMzY1YjBfU1FHT202blpTVXpKTjhwTWNoRk1FTm9ZZFNsV0dHV1pfVG9rZW46U0RsUmJXN3lEb2tQaUl4RHQ2V2NjREI5bjhjXzE3NTE2MTQzODI6MTc1MTYxNzk4Ml9WNA)

- Prefill阶段应该限制Batch size从而避免影响性能，相反，Decode阶段应该增大Batch size来获得更高的吞吐。

![img](https://xiaopeng.feishu.cn/space/api/box/stream/download/asynccode/?code=NjQ0OTA5YzEyMzUyZjU2NmZhYmMzYjc5MGVkNGVkZGFfZW5BMzRjOWRlbVgyNmVmZ1U1b3Y2bmRtZFMyelIyNmdfVG9rZW46RThwY2IzUkVqb1d6MkR4c2N5V2N6WWszbnlkXzE3NTE2MTQzODI6MTc1MTYxNzk4Ml9WNA)

- Prefill阶段算力是瓶颈，Decode阶段内存是瓶颈
  - 对Prefill阶段，内存需求不高，且相对稳定；
  - 对Decode阶段，内存需求会迅速增大。如果内存占用能够缩小，可以使用更大的batch推理。

![img](https://xiaopeng.feishu.cn/space/api/box/stream/download/asynccode/?code=OTYzY2I5ZWFmZDNkYWNmNzVjNjU2ZDdkNTFmZjJlM2FfR29MdUV2Mll6ekVRQ056ZmZxYUtLWWdjY2V3UHJBS2JfVG9rZW46TFN5dGI0VDlIb2l5T3B4OUNMamNCem9XbnhlXzE3NTE2MTQzODI6MTc1MTYxNzk4Ml9WNA)

- Decode阶段可以使用性能较弱、成本较低的设备

![img](https://xiaopeng.feishu.cn/space/api/box/stream/download/asynccode/?code=MzBiM2ZhNjlhNTBjNjI5ODIxM2VjM2VkNmZiOTE1OGJfc29vbUJIMUxFb1RFMktCZEVNNTRIb3JycXZDQktkSkJfVG9rZW46UDFxamJKMzdBb0d6b3p4YjhUamN2ZDRTblFoXzE3NTE2MTQzODI6MTc1MTYxNzk4Ml9WNA)

#### 基准评测

![img](https://xiaopeng.feishu.cn/space/api/box/stream/download/asynccode/?code=MzgxZmY4ZmZiNjllNzhmYTZkZjVkODNkYmI3NGRhMjNfMVptVUpzdENNQjY1MWZhUEdVQXNLenVkd0k1bXZOM2NfVG9rZW46QkNRQ2I3bkNGbzVEc1l4eGpaMWNIc3ZobjVyXzE3NTE2MTQzODI6MTc1MTYxNzk4Ml9WNA)

司南评测结果展示： https://rank.opencompass.org.cn/leaderboard-llm/?m=24-05

Chatbot Arena： https://huggingface.co/spaces/lmsys/chatbot-arena-leaderboard

#### 推理成本

![img](https://xiaopeng.feishu.cn/space/api/box/stream/download/asynccode/?code=ZDBhZmZjMzJiYzgxOTNjMGMwOTAxZTE1MGIzYjVlYTlfNHI2dzdZcjJNbG1GOG9zZlU4aWhjam1yTlM4aG9wZFlfVG9rZW46TXl6YWJFcXhMbzZDTHZ4Y0doOGMwVDFFblVoXzE3NTE2MTQzODI6MTc1MTYxNzk4Ml9WNA)

### 推理加速技术点

**关键问题：**

- 如何动态打Batching，在输出token长度不确定时减少无效的padding计算 -> batching调度（continuous batching）；

- 如何动态分配GPU显存给KVCache且没有内存碎片被浪费。 -> paged attention；

- 如何解决attention计算中间随序列增长，内存带宽需求呈现平方项增长 -> flash attention；

- 如何解决长序列，kv cache线性增长，计算量呈现平方项开销 -> mamba， streaming llm；

- 如何解决kv cache占用大量内存的问题  -> GQA, MLA，模型量化；

- 如果更好的利用两阶段推理计算密集和访存密集的特性 -> 分离式调度；

- 如果解决decode阶段访存密集的问题 -> MLA, mamba，量化，投机采样；

#### FlashAttention V3（2024.07.12）

##### 背景

2018.05   Nvidia提出[online softmax](https://link.zhihu.com/?target=https%3A//arxiv.org/abs/1805.02867)，奠定了flash attention的理论基础。

2021.12   Google Research 将[memory-efficient-attention](https://www.zhihu.com/search?q=memory-efficient-attention&search_source=Entity&hybrid_search_source=Entity&hybrid_search_extra={"sourceType"%3A"answer"%2C"sourceId"%3A"3270526584"})(flash attention)应用于TPU中。

2022.06   [FlashAttention](https://link.zhihu.com/?target=https%3A//arxiv.org/abs/2205.14135) 使用[online softmax](https://link.zhihu.com/?target=https%3A//arxiv.org/abs/1805.02867)技巧，将多头自注意力操作融合为单个算子，因此避免了从GPU全局内存中读区/存储注意力矩阵的开销。

2023.07   [FlashAttention2](https://link.zhihu.com/?target=https%3A//arxiv.org/abs/2307.08691) 通过更合理的tiling方案，以及减少非矩阵运算的数量（A100/H100的非矩阵运算(CUDA Cores)性能都只有矩阵运算(Tensor Cores)性能的1/16）进一步提升性能。

2024.07   [FlashAttention3](https://arxiv.org/pdf/2407.08608) 利用Hopper架构的硬件特性（TMA，fp8）进行加速。

##### Flashattention v3中的技术点

**New hardware features：WGMMA ，TMA**

TMA允许在全局内存和共享内存之间进行高效的异步数据传输，减少了对寄存器的依赖。用TMA类似于DMA方式在全局内存和共享内存之间异步拷贝，GPU可以把节省下来的指令cycle用来发射计算。

因此FA3将Q，K，V矩阵参数加载和WGMMA计算矩阵乘同步进行，重叠计算与加载数据的时间。

**Asynchrony: Overlapping GEMM and Softmax**

H100 GPU其FP16矩阵乘法性能为989 TFLOPS，但特殊函数性能仅为3.9 TFLOPS（吞吐量低256倍）。在attention中，矩阵乘法的计算量是softmax中指数运算量的512倍，即指数运算的时间是矩阵乘法的50%，这个latency不能轻易忽略掉。通过手动 schedule warp（ping-pong scheduling）做到将softmax的计算隐藏在GEMM中。

**fp8 quantization** 

LLM激活可能存在比其他特征大得多的异常值。这些异常值使得量化变得困难，产生了更大的量化误差。FA3使用了两种技术，使得linear与matmul计算都可以使用上fp8（之前的工作仅是linear用到了fp8）：

1. **block** **quatization**：将Q，K，V分block，每个block使用一个scaling factor。
2. **Incoherent Processing**：将Q，K在量化到fp8之前，分别乘一个随机正交的矩阵（等价关系$$(QM) (KM) ^T = QK^T，  MM^T = 𝐼$$）。这样每个Q，K 异常值都减少了，而且不影响最终结果。增加的计算时延融合到之前RoPE操作中。来自论文 [QuIP: 2-Bit Quantization of Large Language Models With Guarantees](https://link.zhihu.com/?target=https%3A//arxiv.org/abs/2307.13304)。

![img](https://xiaopeng.feishu.cn/space/api/box/stream/download/asynccode/?code=Y2RkOWQ5MWEwMzM3ZGYyY2Y1OTI3OGVhODc1ZjE0MGNfZHFkdmFvdzBvbFpSM2FRUXpOTlM1STB6YjdTS284NXFfVG9rZW46SkIwdmJNc3pvb2RWd0t4ZlVqQ2NuTm9KbndmXzE3NTE2MTQzODI6MTc1MTYxNzk4Ml9WNA)

![img](https://xiaopeng.feishu.cn/space/api/box/stream/download/asynccode/?code=ZDVkZDBjZjZmYTAyMTdmYTZiODQ2ZjkzMDUwOTgwM2FfYVBkaVhWQVF1VlVuNTFtQzJtdkxLQ0RYUTQxMEdWUmVfVG9rZW46U2x6QWJPQXJXb1hYVUN4cXdrcWN6SzVxbmNiXzE3NTE2MTQzODI6MTc1MTYxNzk4Ml9WNA)

**reference：**

Flash attention v1，v2：[Flash attention summary](https://xiaopeng.feishu.cn/docx/HvYldCTuBoDbzpxQ4L5cvizcnue)

Flash attention v3： https://tridao.me/blog/2024/flash3/

#### 网络结构改进

##### MHA->GQA->MLA（2017-2023.05-2024.05）

MLA 这个多头机制不仅是一篇单独的论文，也是随着深度求索 DeepSeek-V2 模型一起发布的，其发明的结构带来的效果，以及可以预见到的学术和工业影响力，都是不容小觑的贡献。特别是在国内搞大模型都是在follow国外开源模型的背景下，能从模型结构上进行优化改进，并实现非常亮眼的加速效果，实属不易。

**MHA & GQA & MLA简介**

MLA的核心是对keys和values进行低秩联合压缩来减少KV Cache的size。

MLA被视为GQA的一般化，它用投影矩阵的方式替代了GQA的分割、重复，并引入了一个恒等变换技巧来可以进一步**压缩KV** **Cache**，同时采用了一种混合方法来兼容RoPE。总的来说，MLA称得上是一种非常实用的attention变体。

原来的kv cache需要存下完整的kv向量，MLA只需要保存低维的c，在用到的时候再通过两个权重矩阵映射回k，v，大大减少了kv cache的空间占用。Prefill阶段，MLA虽然增大了计算量，但kv cache的减少也降低了显存和带宽的压力；decoding阶段，更多的是带宽瓶颈，因此减少kv cache可以明显提速。

![img](https://xiaopeng.feishu.cn/space/api/box/stream/download/asynccode/?code=MjAyNzI1NmI4NjZmNTM4OTIxNjIzMmE5YjNiZWYyY2ZfWnhVaE04VUhXcG0wZmRLSkJBeWxGWUFoR0RnUDJPaWlfVG9rZW46RlYzUGI4ZHk2bzVUd2J4R0xkNGNtcTQ0blZoXzE3NTE2MTQzODI6MTc1MTYxNzk4Ml9WNA)

**加速效果展示**

![img](https://xiaopeng.feishu.cn/space/api/box/stream/download/asynccode/?code=ZTNkZmNjM2IwMjRkYzY1MzljMmEwNjgzYzQxNzVjZjBfZ1lycVZlaHh2cGN1cE9SVGJTeDVNQ2NaUkptZnhzV1BfVG9rZW46RFdDbWJVRU1zb253a3d4dWVFZWN1MzZqblNkXzE3NTE2MTQzODI6MTc1MTYxNzk4Ml9WNA)

Reduce kv cache by 93.3%：单层kv cache=576/2048，层数=60/95，**6bit量化**=6/16。

**MLA原理简介**

![img](https://xiaopeng.feishu.cn/space/api/box/stream/download/asynccode/?code=ZWU3NDNhMDcyOGM4NzAzZGMzOTIxOTdhOWExNDY3MmNfYmRmeWpVWWJSbmFwd2xQTlkxMzhQRU5Pd3Y5c0xUNWxfVG9rZW46SWxIQWJLcDFUb2VpcVp4aEpsMmNQUUF6bmdnXzE3NTE2MTQzODI6MTc1MTYxNzk4Ml9WNA)

![img](https://xiaopeng.feishu.cn/space/api/box/stream/download/asynccode/?code=ZDY5N2FiNjNkNDg1YTJmOGE1MmE1MDlhOWI4NDMxZDZfQ2htM25pWHZrbm1NSG9JVG5nTnZJOFRqNmtoMWdXcUZfVG9rZW46QTdYeWJGTnRNbzBqbGx4c1B3VWNoTGFQbm5lXzE3NTE2MTQzODI6MTc1MTYxNzk4Ml9WNA)

1. k，v由原来的直接计算，变为间接计算（新增一个Wc矩阵和c矩阵）。因此MLA只需要保存低维的c（共享压缩变量），而不是像原来那样需要同时存储k，v，通过这个手段减少kv cache的占用。
2. 权重矩阵合并和吸收（实际推理中重计算效率更高，重计算避免了生成大矩阵）

![img](https://xiaopeng.feishu.cn/space/api/box/stream/download/asynccode/?code=ZWJjNWUyNTcxZmRlMWUyYzBlZDI3YTU3NTkxOTY1YjNfWnhsMExKVnpNT2diZDNZeUtWb0psdkh4WEdiN04xSVZfVG9rZW46WXBaVmJtaGFFb2Zpckd4b0NRS2NSRGREbktmXzE3NTE2MTQzODI6MTc1MTYxNzk4Ml9WNA)

![img](https://xiaopeng.feishu.cn/space/api/box/stream/download/asynccode/?code=YWZkMTNhYjhjMTllZjI4ZDg5MDVmNWNmOTQ4N2E2Y2ZfbDl4aGpqY2I1dzh1YmJQZ2ptZDJQS1JNbXNzZXF3NjRfVG9rZW46TXdKUWI0M0JKb211eFd4d0FGSGM3Zkxibmt0XzE3NTE2MTQzODI6MTc1MTYxNzk4Ml9WNA)

**其他细节阐述：**

- 为了配合这个低秩分解，其中涉及到 RoPE 的部分也做了特殊处理。

- $$𝑊_𝑞^{(𝑠)}𝑊_𝑘^{(𝑠)⊤}$$合并成一个矩阵的恒等变换，理论上只有在无限精度下才成立，实际上如果我们使用单精度尤其是BF16的话，经过变换后的精度损失往往还是挺明显的，经过多层累积后可能放大到比较可观的程度，这里可能要根据实际误差看要不要做一些后处理。

**总结**

- MLA 本质上就是一种折中，通过增加一定的计算来换取 memory-bound 硬件场景下中对 KVCache 传输的依赖。

- 既是 training efficient MQA，也是 inference efficient MHA，即训练和推理都会有加速。

**reference：**

https://zhuanlan.zhihu.com/p/696292840

https://zhuanlan.zhihu.com/p/700588653

https://arxiv.org/pdf/2405.04434

##### transformer结构的替换

当前llm推理的decode阶段，对算力会产生不少浪费。因此一些高效计算的transformer替代方案产生，比较著名的就是mamba，此外还有rwkv，ttt等其他工作。

- 2024.03 RWKV团队的EagleX-7B模型在仅使用1.7T tokens的情况下超越了LLaMA 2-7B模型
- 2024.07 Mistral AI推出一个基于数学的模型和一个面向程序员和开发人员的代码生成模型，该模型基于其他研究人员去年年底开发的新架构Mamba。该模型命名为**Codestral Mamba 7B**，即使在输入文本较长的情况下也能提供快速响应时间。Codestral Mamba适用于代码生产力用例，尤其是对于更本地的编码项目。

**Mamba（2024.07.17）**

[Mamba网络分析](https://xiaopeng.feishu.cn/docx/ZERLdGMsVoAQbaxuclUc7Xp9ndG)

**对mamba评价：**

- 在序列长度小于4k(预估)的时候，mamba可以和Transformer架构的精度效果匹配，但在速度并不能有显著优势(此时的Self Attention还没有成为瓶颈)
- 在序列长度很长的时候（例如上百k），mamba会有推理速度优势，但会有显著的效果下降，而transformer的缺点只是慢。

**RWKV （2023.05）**

https://arxiv.org/pdf/2305.13048

[RWKV：Reinventing RNNs for the Transformer Era](https://xiaopeng.feishu.cn/docx/R6tsdC3xJoe1XQx8Wy8cebTnnRh)

##### **总结**

- Transformer是被很多公司、组织证明过的非常有用的东西。其它别的都没有得到过大规模的工业级的验证，于是就算有了新的模型大家的第一决策是在自己的工业应用与创业项目上肯定是减少风险，Transformer 是第一选择，用它至少下限是能保证的。
- 当前新的网络架构在效果上还没能达到transformer的效果，但未来的改进版效果如何，不好判断。

**reference：**

https://www.zhihu.com/question/661867405?utm_psn=1797287983575097344

https://mp.weixin.qq.com/s/fFB0A0vv_2Deb0rWd4tagw

#### 模型量化（2022.10-至今）

##### hansong team在llm量化方向的工作

2022.11  Smoothquant

2023.06  Llm-awq（MLSys 2024 Best Paper）

2024.05  Qserve：W4A8 KV4：https://arxiv.org/pdf/2405.04532

[LLm optimization（quantization&speed up）](https://xiaopeng.feishu.cn/docx/QkxvdvKu4oOxwBxyqlfc0BAGnZY)

LLM Decoding阶段attention的计算密度很低(每个token逐步回归迭代)，这个阶段还是memory-bound，所以KV Cache的load越快计算吞吐就越高，采用KV4可以得到KV8的两倍峰值性能。

![img](https://xiaopeng.feishu.cn/space/api/box/stream/download/asynccode/?code=YWNiNjYzNTExYWRiYTY1MGQzYmMzYTkwNTQ4YTllZmZfR2tPQWZkN1NGYU1lY0JFRWdGTktYNkF4RlRBcVZObnlfVG9rZW46S2RzNWJQTXVQb1BZcjR4ZVlBd2NKY0habkRmXzE3NTE2MTQzODI6MTc1MTYxNzk4Ml9WNA)

![img](https://xiaopeng.feishu.cn/space/api/box/stream/download/asynccode/?code=MWFkYjJhYWVkN2Y3MjU4ZTAyNzc0M2Q4N2MzZDM2YzNfZ3MwNzV6Mk0wV2FhVWgwenhodkMyWEJHZXdKaGdubGZfVG9rZW46SGl3UGJtcXhHb0FBUVh4a05yUWNNRFVybnJnXzE3NTE2MTQzODI6MTc1MTYxNzk4Ml9WNA)

![img](https://xiaopeng.feishu.cn/space/api/box/stream/download/asynccode/?code=ZGIxZWZiN2ZlNjYyZjc2ODc3NzJjM2ZjMzZmZWI0NmVfYWZKbHFUYng1ek10bGE3QWJJVmtQUlNMV2ZsSlVjM3ZfVG9rZW46UjE0T2JjRWltb0JCbE94cTl1ZGN2OGJMbnliXzE3NTE2MTQzODI6MTc1MTYxNzk4Ml9WNA)

![img](https://xiaopeng.feishu.cn/space/api/box/stream/download/asynccode/?code=MWRjNDMyNTM1NTM0MzdkNDFhMWQ5N2JjMTk4ZjdlZGZfMUVxa01URWNjSHV5b3ZrZk10WDIyc2M4R3ZKZHJmdmFfVG9rZW46S2tBMGJSclBxb3c3Vnl4S2N5QWNJTldqbjZiXzE3NTE2MTQzODI6MTc1MTYxNzk4Ml9WNA)

**Ampere and Hopper架构，为什么量化不选择W4A4？**

1. 精度比W4A8会有下降
2. FP32 cuda core的性能是INT4 tensor core的2%，因此patial sum与per block的反量化会成为性能瓶颈
3. 需要同时存储FP32和INT32 的patial sum，需要更多的register file，这导致了SM 中active warp 的数目减少，SM的occupancy降低，active warp减少导致更难隐藏延迟，加剧了计算的开销。

![img](https://xiaopeng.feishu.cn/space/api/box/stream/download/asynccode/?code=NjFjMDNlYTU1ZTkzMDMwZTM4NWNiYzZhMzA0OTFiZWVfWER5VGxPRFRBeHk2UG5lRVpZeVBxN2RTaE52V2xnc3lfVG9rZW46SDE4RWJxR2ZYb3Z5dUF4RUZ6OGNkRHFMbmxiXzE3NTE2MTQzODI6MTc1MTYxNzk4Ml9WNA)

transformer中各个部分的量化精度：

![img](https://xiaopeng.feishu.cn/space/api/box/stream/download/asynccode/?code=ZGJiNTE5ODBhNjc1Y2NhMzY5ZTBhZTYxMWM0MTEyNTFfRmVWQUdzemZqNk14Vk9Mck5rTVhJN1FoZFZ1aVJGbUNfVG9rZW46RWNFeWJTeE1Qb3dDQ0x4TEM5QWNkSlREbmxjXzE3NTE2MTQzODI6MTc1MTYxNzk4Ml9WNA)

结果展示：

定点的W4A8KV4，在llama-2可以做到0.5%以内的掉点。

![img](https://xiaopeng.feishu.cn/space/api/box/stream/download/asynccode/?code=OTc5NjFhZmY1ZTAwYTg0ODdkZjhhNTE2YTQ0MmM3ZDdfWmlWOFhqbmQ5eGRNRFBIS3pHbnEyRFdGdzJGeFNOWjVfVG9rZW46Rk5sdWJtZ29Cb25pSnZ4Nnk3TmNKdEtnbm1lXzE3NTE2MTQzODI6MTc1MTYxNzk4Ml9WNA)

##### tensorrt-llm在量化方向的工作

https://github.com/NVIDIA/TensorRT-LLM/blob/main/docs/source/blogs/quantization-in-TRT-LLM.md

![img](https://xiaopeng.feishu.cn/space/api/box/stream/download/asynccode/?code=ZmFhZjE2Y2VjNGE3NjcxMjQyMTI3ZjZhM2MzYzQ4YjRfTzR1dDVwSFpHMFkxVFd2c05UQTZ0aTBZcUdWTFNVWmpfVG9rZW46T0EyVWJNalA0b1hZR2h4VlB4UGNqTEZpblRoXzE3NTE2MTQzODI6MTc1MTYxNzk4Ml9WNA)

#### 投机采样（2022.11）

投机采样（Speculative Decoding）是Google在2022年提出的大模型推理加速方法。它可以在不损失生成效果前提下，获得3x以上的加速比。GPT-4泄密报告也提到OpenAI线上模型推理使用了该技术。

**问题**：

decoder过程中，某些token的解码相对容易，某些token的解码则很困难。

**解决方案**：

简单的token生成可以交给小型模型处理，而困难的token则交给大型模型处理。这里的小型模型可以采用与原始模型相同的结构，但参数更少。

因此投机采样使用两个模型：一个是原始目标模型，另一个是比原始模型小得多的近似模型。近似模型用于进行自回归串行采样，而大型模型则用于评估采样结果。

**投机采样过程：**

1. 用小模型（Mq）做自回归采样连续生成 𝛾 个tokens。
2. 把生成的𝛾个tokens和前缀拼接一起送进大模（Mp）执行一次forwards。
3. 使用大、小模型logits结果做比对，如果发现某个token小模型生成的不好，重新采样这个token。重复步骤1。
4. 如果小模型生成结果都满意，则用大模型采样下一个token。重复步骤1。

**示例：**

在第一行中，近似小模型生成了5个token，目标大模型使用这4个token和前缀拼接后的句子”[START] japan’s bechmark bond”作为输入，通过一次推理执行来验证小模型的生成效果。这里，最后一个token ”bond“被目标模型拒绝，重新采样生成”n“。这样中间的4个tokens，”japan” ，“’”，“s”，“benchmark”都是小模型生成的。

![img](https://xiaopeng.feishu.cn/space/api/box/stream/download/asynccode/?code=ZWMxM2QzYjQ5OTdmZTcwYmQ3NDVlOWQ1MDQ1NWFjNzhfVDBkdHlrcDZmN1h6OWtPTkZMcEtvTXBRdkpSTTF3dXVfVG9rZW46QXdZeWJ4aTM5b2xiUVp4WDM3cmNNamNGbk5nXzE3NTE2MTQzODI6MTc1MTYxNzk4Ml9WNA)

使用投机采样，解码时间大幅缩减：

![img](https://xiaopeng.feishu.cn/space/api/box/stream/download/asynccode/?code=MWM4YjAwNDUyM2VjMDdhZWE3MzVmZTY1NzYwZWQ1ZWJfRTZDMFYwaHhxcXZsYnNuWFg2Sml0QWZWc1VCZFMxdzdfVG9rZW46Sk5uUmJmOU9ub2VrSmJ4dnVkemMycXoxblBmXzE3NTE2MTQzODI6MTc1MTYxNzk4Ml9WNA)

**效果展示**：

T5模型在不同配置下有2-3x加速效果

![img](https://xiaopeng.feishu.cn/space/api/box/stream/download/asynccode/?code=MjJkN2I4MDQyMmFiMGI2ZmZkNzI3Y2I0Mzc5ZDg1ZmVfeGt3M2s2bWN4c1dtS2xOUDdRV2ZmSko3UmxpOFNLYm9fVG9rZW46SXN4RWJHb1lSb1UxejJ4Wko3M2M4WXE5bmRoXzE3NTE2MTQzODI6MTc1MTYxNzk4Ml9WNA)

**总结分析：**

理论上靠投机采样技术可以完全解决memory bound的问题，但实际上当前最好的方法加速比也只有2~5倍，原因必然是小模型能力不够带来的，该方向还需要更好的范式来解决这个问题。

**reference**：https://zhuanlan.zhihu.com/p/651359908

#### Moe (2020.06)

**update：DeepSeekMoE**

DeepSeek-V2 采用了自研的稀疏MoE架构DeepSeekMoE，它主要包括细粒度专家（Finegrained experts）和共享专家（shared experts）这两个核心组件。在激活的专家参数量和总专家参数量相同的情况下，DeepSeekMoE的性能可以远远超越传统的MoE架构。

虽然细粒度的专家提高了模型表达/处理任务的能力，但是不同的专家之间还是存在公共知识，如果交叉的公共知识较多，实际上会带来参数冗余，所以deepseek 同时提出了shared expert，将公共知识收敛在shared 专家上，避免不必要的参数冗余。

![img](https://xiaopeng.feishu.cn/space/api/box/stream/download/asynccode/?code=YzZmYzRmNGI4NTU2NGFhNmFlNDk1OWYyZWVlZmI5NmNfU0x4aWY5TExHZ2NsZm9POFlHSzZJYnMzTmtGNVlQSllfVG9rZW46UnBYaGIyY2RVb1RhdEx4SWN1NWMyTzFnbjVkXzE3NTE2MTQzODI6MTc1MTYxNzk4Ml9WNA)

moe技术分析：[MoE（Mixture-of-Experts）分析](https://xiaopeng.feishu.cn/docx/TGDgdCFlcohHlcxIr8hcWIVFnIh)

#### Streaming LLM（2023.09）

https://arxiv.org/pdf/2309.17453.pdf

如何解决大模型应用在多轮对话等流式应用上遇到的两个问题：

1. 一个就是随着对话的不断进行KV Cache不断增长。
2. 生成的长度超过了训练时候的长度。

![img](https://xiaopeng.feishu.cn/space/api/box/stream/download/asynccode/?code=YzkzZmY4ZTkyYTI4NDUxZTNkYmNlOTQyMmMxNzlmNWNfaFJzUHhuS1ZNYUVmTlFWVGo5cVhnZXNBQ3hVOVdnaE9fVG9rZW46WkVZdGJ4dGhvb3dUMkR4T0NaOGNHM0Y1bllnXzE3NTE2MTQzODI6MTc1MTYxNzk4Ml9WNA)

当推理时的文本长度 T 大于训练时的最大长度 L时，一个自然的想法就是滑动窗口。主要有两种方式：

1. 固定窗口长图（图b），问题是精度下降较大
2. kv cache重计算（图c），精度能够保证，问题是性能损失较大

**方案：**

应用Attention Sink优化改进了Window Attention，使其能在不牺牲效果的情况下不限长度的持续输出。具体操作是保留前几个token，这样Softmax的输出分布就不会改变了

![img](https://xiaopeng.feishu.cn/space/api/box/stream/download/asynccode/?code=MjAzNWI2ODNjZTllMGIxNzcxOTMwM2I5MTUwMDVlNzNfUzhWRG5HcXIzRXZNUEw3V29JaEZxa3hqNmdXNHpHVDVfVG9rZW46UjZWdGJWMTlGb3NXMTN4MjA4bWNSczJ2bkJnXzE3NTE2MTQzODI6MTc1MTYxNzk4Ml9WNA)

虽然StreamingLLM可以几乎“无限”进行输出，但是并没有无限记忆，根据实现方法也可以看到，其运用的还是KV Cache里面有限的空间，只不过加上了最开始的几个token。

**滑窗方案的示例：**

[MoE（Mixture-of-Experts）分析](https://xiaopeng.feishu.cn/docx/TGDgdCFlcohHlcxIr8hcWIVFnIh)

#### batching调度策略（2022.09）

如何动态打Batching，在输出token长度不确定时减少无效的padding计算

##### continuous batching（vllm）

![img](https://xiaopeng.feishu.cn/space/api/box/stream/download/asynccode/?code=Nzk2YTRiYzNlOTUyMDgxMzBmMGY3Yzg1MjMxZmNkYWZfV01sWmpURFZ2ckVwVldzS1kzMkRIUGx2THNDZU00VFhfVG9rZW46RW5YR2JmckxPb1ZTSFR4WUVqcGNJOFpVbnFiXzE3NTE2MTQzODI6MTc1MTYxNzk4Ml9WNA)

continuous batching只是具有batched manner，非显式batching，并不发生真正的将多个request的input data堆叠到一起的操作。continuous batching以request为调度粒度，一个request服务完成，另一个紧跟上。

效果：该方法相比于静态 Batch 与其他动态 Batch 的方法吞吐性能提升明显。

![img](https://xiaopeng.feishu.cn/space/api/box/stream/download/asynccode/?code=NzU0NTM1ZTcyZTYwNmQ2OGZkMGJkYzdhNjUxYWE5NWVfbXNubnpzc0VRSlM3TnlETXFnR3gwZ1hXZkdPTG1xWlRfVG9rZW46WVU3NGJNZXBSb1VYNWd4TDFEWmNWWnhXbmZvXzE3NTE2MTQzODI6MTc1MTYxNzk4Ml9WNA)

##### selective batching （OCRA）

selective batching主要目的是解决continuous batching device利用率低下的问题，可以认为是显式batching，不同的request，只有MHA attention可以像continuous一样执行，其他operator以攒成batch的形式执行，如下图所示：

![img](https://xiaopeng.feishu.cn/space/api/box/stream/download/asynccode/?code=YTM0Y2MxNTIyNzhmZDEwZWQ5OWU0N2IxMDA1MTY0NmZfUUZTRG9sTk9NaGgySEc2RUhjbkJKR0FrcTNCc3psdjJfVG9rZW46U2lUeWJkQ1dCb01rUzh4aHB4MWNuZkFJbmdnXzE3NTE2MTQzODI6MTc1MTYxNzk4Ml9WNA)

##### chunk prefill batching（微软 deepspeed）

这种策略只适用输入超长输出超短的任务。

引入continuous batching后，我们会面临一个问题，跟某个请求Prefill阶段batching到一起计算的Generate阶段请求，latency会显著提高，在streaming decode的对话输出场景中，用户体验应该是，输出答案时，某个字符的输出会突然卡住，等待一段时间后才能继续输出。

如图1所示，G0_0，G0_1, G1_0的lantency，由于分别跟P1/P2 batching到一起计算，会导致latency增加。

![img](https://xiaopeng.feishu.cn/space/api/box/stream/download/asynccode/?code=MzhiNzgzN2MwNmZjNjllNjRjNzZiZWEyZjFkZmU4ZDdfVzlWN1p1R0FEZjZTd0VRN2JIQ3pucHE3WnNSelYyS2RfVG9rZW46SUVqMmJpYWxlbzl4VmN4Y25PcmNHWFRZbmhmXzE3NTE2MTQzODI6MTc1MTYxNzk4Ml9WNA)

可以将Prefill阶段在seq_q维度拆成多个小份，每个小份跟其他Generate阶段请求组batch参与计算，减少其他请求Generate的Latency。

![img](https://xiaopeng.feishu.cn/space/api/box/stream/download/asynccode/?code=N2MyNGNiMjkxZTkxYjVhMzcxZjQwNDQxNTI3ZDIxYzVfdTROUXZGUkJmcThaZkVlU0R1UDk4WHJPRFRuTFNkMTBfVG9rZW46T2VzSGJjbG14b1FTMTJ4NmM0UmNDTFZabnV2XzE3NTE2MTQzODI6MTc1MTYxNzk4Ml9WNA)

prefill prompt中的sequence length太长也不好，只有某个合适的长度才能达到device utility的最优。因此chunk prefill将prefill中的比较长的sequence拆成短的trunk（拆之前寻找一个最优的trunk中的sequence长度）。

![img](https://xiaopeng.feishu.cn/space/api/box/stream/download/asynccode/?code=MWU2NTc3ZDU2Njg1YjFhMjNhZGFlMjI4NmMzODk2MDZfaXhJZXJVYUJrSmI3c1QxTWxmSmljSGNZWmo1b3NtWHNfVG9rZW46VDdwZmJiYnhBb2Q1a3h4dnBxcGM3NFVPbk1iXzE3NTE2MTQzODI6MTc1MTYxNzk4Ml9WNA)

如果采用chunk prefill batching策略，那么，需要改变kv cache中缓存的block size的大小，并对attention mask做特别处理，如下图所示：

![img](https://xiaopeng.feishu.cn/space/api/box/stream/download/asynccode/?code=OGY2MWE4MmU4ODliMDUzYmZlZDUxZjg0YzUzNDViOTFfYTF6M3dXdzNSMlNuN0JPenFWZVJ6TGRvbnM3dzlNY0xfVG9rZW46VWxnVmJqNHR3bzBWb3h4MEs4YmNGcnNZbkVnXzE3NTE2MTQzODI6MTc1MTYxNzk4Ml9WNA)

**reference**：https://www.semanticscholar.org/reader/a9caf21a845cb0b1b1d453c052188de118006093

#### Paged Attention（2023.06）

vLLM提出了 [PageAttention](https://link.zhihu.com/?target=https%3A//arxiv.org/abs/2309.06180) ，其中KV-Cache被储存在了页表结构中，从而减轻LLM服务中的内存碎片等问题。

##### 背景

- 显存占用大：在 LLaMA-13B 中，缓存单个序列最多需要 1.7GB 显存；
- 动态变化：KV 缓存的大小取决于序列长度，这是高度可变和不可预测的。因此，这对有效管理 KV cache 挑战较大。该研究发现，由于碎片化和过度保留，现有系统浪费了 60% - 80% 的显存。

![img](https://xiaopeng.feishu.cn/space/api/box/stream/download/asynccode/?code=Yjk3YzBlZDRmNmNiN2RkMzIwNzFmMGE4MTI3ODY5YjNfdW54enVXUnY5SDd4cDlJSkpXZFJ0Rnk0VHc0bG5NdFZfVG9rZW46RzhKU2JvaUtVb0VkMGx4UzRMMmNic3dkbmViXzE3NTE2MTQzODI6MTc1MTYxNzk4Ml9WNA)

##### 原理

Page Attention的主要思想是建立一个全局K/V Cache Tensor，模仿OS中的内存页管理，将每个request 的KV Cache 通过Page Table建立映射关系，避免了按Max Seqlen 创建KV Cache 造成的内存浪费，增加了潜在的序列并发量，从而增加了系统吞吐量。

这意味着kv cache 分配可以 just-in-time，当启动一个新的生成任务时，框架不需要分配大小为 Maxnum context length 的连续 cache。 每次迭代，调度程序都可以决定特定生成任务是否需要更多空间，并动态分配，而不会降低 PagedAttention 的性能。

##### 步骤

对 KV cache 的管理是通过提前将一大块空间（显存或内存）划分成多个块（block），每块有 block_size 个 slot，每个 slot 存放一个 token 对应的 K V 值。

![img](https://xiaopeng.feishu.cn/space/api/box/stream/download/asynccode/?code=MmJiZDFhNmJiODE5MzBmYzM4NDFiNWNkZDNmMTgwZTRfdWY5VFIwYXlObTZiN3lDN05MbGdmWHNJbG5YazBMSmVfVG9rZW46RTlVRmJ3d1h2b3c4Z0d4enFudmNpekc3bmdoXzE3NTE2MTQzODI6MTc1MTYxNzk4Ml9WNA)

假设 vLLM 在这一轮只能调度两个请求进行处理，那么根据先到先处理的原则，会从 waiting 队列中选择 s0 ("Hello, my name is") 和 s1 ("The future of AI is") 放入到 running 队列。对于 s0，Worker 生成的 token 为 Dustin，对于 s1，Worker 生成的 token 为 bright。同时，Worker 会将计算过程产生的 KV 值存储在 KV cache 中

![img](https://xiaopeng.feishu.cn/space/api/box/stream/download/asynccode/?code=MDE4MDFhZjgyNmEwNWI3NTcxMTIxMGIwYzI4OWEwMmNfakZDbnpNMWxFcHNxOHBRemZkTkljREpKYnJmNmRJWVBfVG9rZW46TVByVmJwbW9Ob0x1MmJ4ZGhDUGNsUFBDblZlXzE3NTE2MTQzODI6MTc1MTYxNzk4Ml9WNA)

由于 waiting 队列中还有一个请求 s2（The life is)，因此，vLLM 在第二轮只会处理这一个请求，因为前面提到，vLLM 只会处理要么都是填充阶段的请求，要么都是生成阶段的请求。

此时，由于kv cache的总空间已满，无法支持更多的请求处理，只能等待request推理结束，释放了kv cache，才能支持新的推理request。

![img](https://xiaopeng.feishu.cn/space/api/box/stream/download/asynccode/?code=ZDQxNTQ4ODk5ODA3Y2EzMWE2MmYxNmQ1OGM5MzEyYmNfeVRqMmY0U1NLNGxzVTFCSG1YWlY4TFVtbjlMNDZHbTBfVG9rZW46QUNhQWJOWVRKb2NpQ1l4ejRkeGNXSUxubmhKXzE3NTE2MTQzODI6MTc1MTYxNzk4Ml9WNA)

#### 分离式调度策略（2024.06）

##### 推理指标要求

**Service level objective（SLO）**：评价用户体验的服务指标，服务该有的功能得达到什么样子，像每分钟平均 qps > 100k/s；99%访问延迟< 500ms；99%每分钟带宽> 200MB/s

**Time to first token（TTFT）**：首个token的延时，对应prefill阶段的延时

**Time per output token（TPOT）/ Time Between Tokens (TBT)**：每个token的生成延时，对应decoding阶段的生成一个token的延时

**不同的应用对SLO的要求也不一致：**

聊天机器人（Chatbot），用户输入对话后渴望能马上得到第一个字的输出响应，但后续生成的速度只要满足人类阅读速度即可，所以聊天机器人应用非常对 TTFT 指标很严格，但是对 TPOT 指标则较为宽松。

而用户对文本摘要应用对第一个字的响应速度的容忍程度就高一点，同时后续生成速度满足人类阅读速度即可，所以对 TTFT 指标以及 TPOT 指标都较为宽松。

![img](https://xiaopeng.feishu.cn/space/api/box/stream/download/asynccode/?code=YjhlN2U2ZjFiZjk3Mzg4Zjg4ODRmZjIwOGRiYTQ2YTBfdU9BenpMZGFjVkNwcUs5am1yUVJmVE9NaUJOUmVYdU9fVG9rZW46RGpSYmI5cmRZbzZWQld4Mm9hcWM4RDN3bmNmXzE3NTE2MTQzODI6MTc1MTYxNzk4Ml9WNA)

##### DistServe（北大）

**背景：**

现有的 LLM 服务系统大部份都将 Prefill 阶段和 Decode 阶段合并，放在相同的 GPU 上处理，这样会导致 Prefill 阶段和 Decode 阶段会共享 GPU 分配策略和模型并行策略，**无法针对不同阶段采用同时兼顾吞吐与****SLOs****更优的配置。**

- Prefill 和 Decode 放在相同的 GPUs 上运行会互相影响；
- Prefill 和 Decode 放在相同的 GPUs 上运行会强制使用相同的GPU分配策略以及并行策略。

![img](https://xiaopeng.feishu.cn/space/api/box/stream/download/asynccode/?code=MWI1ZjdkNjhjYWRmYTZhMTU4YTM4MWMzZjVjZTJmZmRfTVZTSjdGTXFqVkwxWXZONVBObVV4c1IzWjBqczc0R2RfVG9rZW46S01LTGJtZ0p3b2M0S3F4R1MxNGNpaXlUbkNlXzE3NTE2MTQzODI6MTc1MTYxNzk4Ml9WNA)

对于 Prefill，增加 Prefill 请求的 TTF；对于 Decode，会造成 Decode 请求被 Prefill 请求拖慢。

**解决方案：**

将 Prefill 和 Decode 分离，并且提出一个最优配置算法：

- Prefill 和 Decode 不会互相影响 TTFT 和 TPOT。
- GPU分配与模型并行策略解耦合。可以分别对 Prefill 和 Decode 划分不同的 **GPUs 数量**以及采取不同的**模型并行策略**。

![img](https://xiaopeng.feishu.cn/space/api/box/stream/download/asynccode/?code=MjliYWM3YzVmNzYxOTcyMDI2MDJkNTBmZDcxYzkwZDdfbDNySGFPakJubFN5Yk00TXFqZWZ3QktQSXRGSkFxR3pfVG9rZW46UFROV2JQYkd4bzd3Mlh4b3llbmN6WngzbnRnXzE3NTE2MTQzODI6MTc1MTYxNzk4Ml9WNA)

Prefill：Prefill Worker 对新到达的请求执行 Prefill 运算，得到第一个 tokens 以及 prompt 对应的 KVCache；

Migrate：将 Prefill 后的请求、对应的 KVCache 迁移至 Decode Worker；

Decode： Decode Worker 对迁移的请求循环执行 Decode 运算，直至完成生成。

**KVCache 迁移开销讨论**

![img](https://xiaopeng.feishu.cn/space/api/box/stream/download/asynccode/?code=ZGU3ZDc1YzFkM2QzOTg0OWJmMWVmMmRiZjBjMGJiYjFfa2tpQlFEWm1xZ1NUaFNRelFONmlGc2Y1NXVaZmtVYVBfVG9rZW46TVZWQ2JqUUVRb2VreHZ4Qmc3VWNwRzBnbnJnXzE3NTE2MTQzODI6MTc1MTYxNzk4Ml9WNA)

迁移开销低于 Decode 一步的开销。KVCache 迁移只会发生一次，并且当模型越大、序列越长、多卡通信设备带宽越高，KVCache 迁移开销占比越低。

**效果展示：**

Prefill 和 Decode 分离模型服务单卡的吞吐大约为传统 LLM 模型服务的 2 倍。

![img](https://xiaopeng.feishu.cn/space/api/box/stream/download/asynccode/?code=ODVjMjliMDk5OTE4ZjAyOGRmNTRhM2E1MmM4NGJhN2RfeWE4bXBrUnZkWHlKcjBMZXJEVktqS1VKUnNONEEzY2tfVG9rZW46UEUyTWJobnIwb1V0RnZ4OE9CRWNVd1kxbkRmXzE3NTE2MTQzODI6MTc1MTYxNzk4Ml9WNA)

https://arxiv.org/pdf/2401.09670

https://zhuanlan.zhihu.com/p/698341542

##### Splitewise（华盛顿大学&微软）

https://arxiv.org/pdf/2311.18677

##### Mooncake（kimi&清华）：

**简介：**

Mooncake 是由月之暗面创建的超人气智能助手[Kimi](https://link.zhihu.com/?target=https%3A//kimi.ai/)的底层推理平台。

它采用分离式设计，将Prefill和Decode两阶段解耦，构建了一个全局KVCache Pool，实现以Cache为中心的调度。

Mooncake分离式架构动机是Prefill和Decode阶段性质不同，Prefill是计算密集，受限算力带宽用不满，Decode是访存密集性，受限带宽算力用不满，所以**用同一种硬件部署两阶段往往顾此失彼，不是最有性价比。**

Mooncake 的架构是非常典型的分离式架构，将单个同构 GPU 集群的资源打散并重新组织成三个可以独立弹性伸缩的资源池。其中 Prefill Pool 处理用户输入，主要对 Time To First Token (TTFT) 负责。同时因为 Prefill 相对计算密集，这一部分也承担着抬高整体资源利用率的任务。Prefill 处理完之后对应的 KVCache 会被送到 Decode Pool 进行 autoregression 式的流式输出。虽然我们希望尽可能攒大的 batch 以提升Model FLOPs Utilization（MFU），但这一部分主要需要对 Time Between Tokens (TBT) 负责。

![img](https://xiaopeng.feishu.cn/space/api/box/stream/download/asynccode/?code=NjY4YzA3ODAwYWNiZjBlMTY1MmQ0NmU0NWI2NDgyODRfYlN1WW4za2cxUVBIQ3o0Y1lWaTBwdGJRa1d6amNsY01fVG9rZW46WW5BMWIwQnd4b1NOUlp4ZzV2YWNUQlFpbkFoXzE3NTE2MTQzODI6MTc1MTYxNzk4Ml9WNA)

**分离式架构：**

1. 将Prefill与Decode计算资源分开，这与前人工作无异，如splitwise和distserve等。
2. 将KVCache和计算分离开，它将GPU集群的CPU、DRAM、SSD和RDMA资源分组组成Distributed KVCache Pool，KVCache也是分块以Paged方式管理，KVCache Blocks如何在Pool中**调度请求和复用KVCache**是mooncake精髓。

**模型推理步骤：**

s1. **KVCache Reuse**：MoonShot用户很多，request之间有重叠部分，可以reuse共同前缀，核心是需要尽可能将token block调度到Prefix KVCache最长的节点处理。

s2. **Incremental Prefill**：使用Prefix Cache，Prefill Instance只需要计算Prefix Cache没覆盖的部分。

s3. **KVCache Transfer**: Mooncake通过异步传输，与上述Incremental Prefill步骤重叠，将每个模型层生成的KVCache流式传输到目标Decode Instance的CPU内存，以减少等待时间。

s4. **Decoding**: 接收到所有KVCache后,开始token by token的进行decoding。

![img](https://xiaopeng.feishu.cn/space/api/box/stream/download/asynccode/?code=MjE5MzQ5M2JiZDEzYzlhZWMyYjk1YjhlYTE2MWQ0ODBfUjgyNU45Y2lIQktSdEMzbHU3M1VxUElZdXp1SGxBd2NfVG9rZW46U0VFVWJnOFZlb0dnRkd4cnNVaWM4bFo0bkRlXzE3NTE2MTQzODI6MTc1MTYxNzk4Ml9WNA)

**Prefill阶段使用了Chunked Pipeline Parallelism (CPP)**

TeraPipe将模型沿着Layer切分成N个Stage（N是GPU数），将一个长序列输入分割成多个子序列，并在这些子序列上并行处理不同Stage的计算，形成流水线并行。如下图所示，切分方式看左图，流水方式看右图一个Forward过程即可。

![img](https://xiaopeng.feishu.cn/space/api/box/stream/download/asynccode/?code=NjMzYzlmOWIyZTljN2UxZWRhMGJkZTExYTEyNzA2M2NfdUlFZFFCSnpvWDhTS2FlTVVnU1VjRkhWeG14QTFHTUxfVG9rZW46Um9BUWJCWDVub0R1TTJ4Z3puUWNXb2xrbmNnXzE3NTE2MTQzODI6MTc1MTYxNzk4Ml9WNA)

该切分策略的优势：

1）它仅在每个stage的边界处需要跨节点通信，这可以很容易地与计算重叠。这导致更好的MFU并且KVCache传输的网络资源争用更少。

2）layer分布在不同设备不需要改变，长短上下文都可以用一套调度策略。

**Prefill时负载均衡**

Causal Mask来只让Attention QK矩阵下三角有效，上三角不参与计算。均分chunk则会导致负载是不均衡的。比如，如下图Attention按照序列均分到4个GPU，GPU 3的计算量量接近GPU 0的七倍。

![img](https://xiaopeng.feishu.cn/space/api/box/stream/download/asynccode/?code=OTZmYzJlNjcyMjc0ZDFkNTZmNDBkZGYyOTA5YTIyNzdfY2dHakdXSXN3VFQ2aEZJeWJoY2ZnNTVYZmVCaU9NbUxfVG9rZW46TzVmNGIwdkZBb3pXb094SHBMRGN1d3owbmtoXzE3NTE2MTQzODI6MTc1MTYxNzk4Ml9WNA)

在序列开始时使用较长的切片，在结束时使用较短的切片，以优化计算负载分布，减少流水线中的瓶颈。用动态规划来找最佳划分（切分的长度）策略。

**Layer-wise Prefill**

在Prefill阶段对KVCache做CPU Offloading（或者传输到Decode Instance）。这些操作都是异步的，而且是Layer-wise的，也就是一层Prefill一个Layer的KVCache Block算出来，就立刻transfer（发给decode Instance）。

![img](https://xiaopeng.feishu.cn/space/api/box/stream/download/asynccode/?code=YmJlMWJkN2RmZGJlZDBjODUzMGZkZjhmNTJiMzBkNTZfUjREdmJqQUlQOUVhekJqRkc1SzZpRWhtNW5MeGppT3hfVG9rZW46WVo5aWI3OTBrb3V3Qkl4VU1UQmNOVjBDbjBjXzE3NTE2MTQzODI6MTc1MTYxNzk4Ml9WNA)

**优势：**

1. 因为Prefill负载占LLM推理的比例低，导致Prefill机器数量比Decode机器少，而Prefill产生的KVCache和Decode消耗KVCache一样多，因此Prefill机器显存不够，需要一个layer产生了kv cache，就立刻transfer给Decode机器，来节省Prefill时的显存。
2. transfer操作可以和Prefill计算重叠起来，甚至和Decode第一个step部分计算重叠起来。

**Prefix Cache**

Prefix Cache的主要思想是充分复用请求间的共享prompt前缀从而减少计算开销。

MaaS厂商每天需要处理海量的LLM并发请求，海量的请求势必会有大量重复的prefix，而重复的内容意味着重复的计算，可以将重复的内容计算的中间结果(KVCache)缓存下来反复使用，如此便可省去这部分计算时间，从而缩短响应时间提高吞吐量。

需要kv cache resue的场景：

1. 问答类场景，每次请求模型时会拼接之前的问答，随着对话轮数的增加，请求长度变长，导致模型的TTFT逐渐变长。
2. 不同用户对同一篇论文进行总结摘要。当论文很长，又是热点论文时，发生重复计算的概率很高。

**KVCache-centric Scheduling**

KVCache Pool用Paged方式管理KVCache（本章简称Cache)。Prefill节点接收到一个request，它根据prefix cache ID将前prefix cache从远程CPU内存加载到本地GPU内存，以启动request的prefill计算。如果不存在prefix cache，则需要自己Prefill计算了。这种选择平衡了三个目标：尽可能多地重用KVCache、平衡不同预填充节点的工作负载，并保证TTFT SLO。

![img](https://xiaopeng.feishu.cn/space/api/box/stream/download/asynccode/?code=NTMyZTI1Mjk2MGJiY2U4MGE0MDlkZWJlZDZiNWNmZjZfMGpIVFQzVHJ3SXpaaDJXZkZQRWtsS014OU9VMGFTaVpfVG9rZW46WGJ4RWIzM1ZUb2RYRGJ4WjVPTWMwRXNwblVmXzE3NTE2MTQzODI6MTc1MTYxNzk4Ml9WNA)

**问题1： kv cache resue是必要的么？直接重新计算kv cache还是从cpu中load已有的cache？**

seq_len较小的情况下，attention的占比不高。这时候由于GEMM和KVCache传输都是的O(seq_len)复杂度，直接比较per token下，GEMM的计算耗时和KVCache传输耗时就能确定哪个有优势。

seq_len逐渐增大后，由于Attention的计算是O(seq_len^2)的复杂度，所以一定会在一个点上使得KVCache传输的开销小于重计算的开销。

而kimi的一特色就是处理长文本，因此长序列的需求多，非常有比较进行 kv cache resue。

**问题2：KVCache blocks都是在Distributed KVCache Pool中，不一定在本地内存，怎么快速检索到众多前缀KVCache blocks呢？**

建立一个token block -> KVCache block的映射关系，根据映射关系去检索prefix token blocks的KVCache blocks。这个映射关系是一个Hash Table。

**问题3：如何找到max prefix cache来尽可能reuse kvcache blocks？**

每个Block的Hash Key是基于前一个Block的Hash Key计算得到，图中B=Hash(A+b)。如果两个Block Hash Key相同，不仅该token block的KVCache block，那么之前所有prefix KVCache也都可以复用。如果不这样设计，你可能要反复遍历所有KVCache Hash，而用这种方式只需要遍历一次，检索代价从多项式降低到线性。

**问题4：如何做负载均衡？**

每个Prefill机器都会维护一个缓存队列的时间表。当一个新的request到来时，先预估每台机器TTFT的时间，将request分配给估计最短TTFT的机器，并更新该机器的缓存队列时间。预估时间如果无法实现SLO，直接返回HTTP 429（Too Many Requests）。

**总结：**

1. KVCache 的容量会长期保持高位，因此围绕着 KVCache 来优化是非常有必要的。
2. 尽可能多地重用 KVCache 以减少所需的计算资源，以及在分布式环节中转移到目标节点的耗时。
3. 分离之后，允许整个系统往 “算力/$”（prefill） 和 “带宽/$”（decoding） 的两个方向独立发展，可以选择不同的硬件来做到更好性价比，Prefill阶段用高算力芯片，decode阶段用高带宽芯片。
4. 如何在不同设备之间传输KVCache，集群需要高速网络来互联，而网络成本不容小觑。
5. 芯片层面值得做为一个重要的设计考量，在芯片的 IO 能力上要多预留一些能力。

ref：https://arxiv.org/pdf/2407.00079

https://zhuanlan.zhihu.com/p/706097807

##### Mélange（UC Berkeley vllm团队）

https://arxiv.org/pdf/2404.14527

如何基于负载差异性，利用GPU异构性以降低部署成本的问题

**LLM服务特性决定GPU的推理成本：**

1. 请求大小（Request Size）：LLM请求的大小由其输入和输出token长度组成。直观理解，对于较小的请求大小，低端GPU通常比高端GPU产生更高的T/$。
2. 请求速率（Request Rate）：响应服务的GPU容量应与请求速率相匹配。直观理解，在低请求速率下，服务可以通过从昂贵的高端GPU调整为便宜的低端GPU来降低成本。
3. 服务级别目标（SLO）：服务通常会建立延迟SLO以确保服务质量。直观理解，由于低端GPU通常比高端GPU产生更高的延迟，因此对于严格的SLO，需要高端GPU，而在宽松SLO设置下，低端GPU可以降低成本。

利用Request之间的输入、输出和Request ratio等因素的变化，将不同类型请求用不同GPU服务

![img](https://xiaopeng.feishu.cn/space/api/box/stream/download/asynccode/?code=ZDlkZjBmYWU1NjkxYjZmODdlNDY5NmY1M2E5ZTk4ZWFfckFwODhqbG5URllBc1RTMURrcmQ5RGFHSWxObUhhMjVfVG9rZW46TnZJcmJmYk9Vb1k2VTR4c3BlMGNGVGhEbm5iXzE3NTE2MTQzODI6MTc1MTYxNzk4Ml9WNA)

Prefill子集群，用A800/H800高算力卡计算，节点用PCIe互联，Decode节点间可以用以太网交换机来交换KVCache。

Decode子集群，用H20高内存高带宽卡计算，节点内可能需要NVLink做SP/TP方式解码计算，Decode节点间不需要配网络，因为没有Decode Instance间的KVCache搬运需求。

Prefill-Decode Instance之间，用RDMA网络来传输Prefill和Decode之间的KVCache，这个需要高带宽低延迟。

#### Dynamic shape探讨

Dynamic shape是我们成功运行大语言模型的关键，因为sequence length(seq_len)和kv cache的大小都是动态的。seq_len根据模型输入决定 ，而kv cache的大小随每次模型forward而增长。之前的大部分基于编译的深度学习框架采用static shape，采用那些框架必须使用padding技术和bucketing技术，会造成重复编译，极大增加编译时间。

##### vllm

Continuous batch + paged attention

不走编译路线，直接编写cuda，进行动态内存管理，以及动态kernel计算。

##### MLC-LLM（TVM， 2023.05）

https://zhuanlan.zhihu.com/p/627449108

https://zhuanlan.zhihu.com/p/625959003

MLC-LLM是基于TVM relax搭建的，**在介绍其优化手段时，第一条就提到了dynamic shape**。不难理解，对于语言模型，由于其输入长度不固定的缘故，如果每次inference都按照固定的最大长度进行计算，会导致极大的浪费。

TVM Unity 原生支持dynamic shape，所以我们可以将seq_len和kv cache 大小分别表示成抽象变量，避免重复编译。TVM的symbolic shape的支持可以使得我们针对动态的计算也可以进行如算子融合，TensorIR融合和内存分配等优化，并且灵活地和其他优化结合起来。

TVM采用了专家知识和自动调优结合的方法。首先，我们得知大部分库采用高度优化的固定shape的micro-kernel，循环调用它，来实现dynamic shape的优化。我们采用相似的方法，首先使用auto tuning优化一个micro-kernel，如32*32*32的矩阵乘法（下文以该micro-kernel举例），然后使用专家知识，手工pad算子的形状为32的倍数，然后在tiling之后，使用micro-kernel替换内部循环。在推动模型的支持的过程中我们也基本确认TVM Unity可以有效针对性地处理动态shape的编译迭代。

##### DISC：A Dynamic Shape Compiler for Machine Learning Workloads（MLIR，2021）

https://arxiv.org/pdf/2103.05288

https://github.com/alibaba/BladeDISC

BladeDISC是阿里基于MLIR开发的dynamic shape编译器

- 目前的中间表示 (Intermediate Representation, IR) 缺少对动态形状特性的支持，所以编译器在HLO Dialect基础上构建DHLO (Dynamic HLO) 。
- 编译器建立了运行时(runtime)来支持动态形状逻辑，用了即时编译 (just in time compile) 的方法，可以进行主设备（一般是CPU）端设备（一般是GPU，NPU）联合编译优化。
- 可以在缺少完整形状信息的条件下应用算子融合 (kernel fusion) ，基于算子间的shape传播特性和编译时捕获的shape约束信息，进行内核融合优化。

**Adaptive Shape Inference**

Shape constraints：编译阶段，用于代码生成优化

Shape calculation：运行时阶段。编译生成shape inference代码置于主机端，内核计算代码置于设备端

**Dynamic Buffer Management**

由于动态shape模型无法像静态shape模型一样去获知缓冲区的生命周期，DISC利用Shape constraints，对缓冲区进行管理和优化，如对于shape相同的两个张量，其缓冲区空间大小相等，因而可以考虑进行复用。

### 具身智能

[Robotics Transformer](https://xiaopeng.feishu.cn/docx/TodLdQkhKoks9VxTaxYclEGlnDd)

### 个人思考

- 优化算法与硬件深度绑定（fp8，nvlink），blackwell硬件大规模使用后，后续会出现更多的玩法，例如fp4

- 优化与调度越来越复杂，需要依赖社区，集思广益，靠1-2家公司很难做好优化。

- 在memory bound未彻底解决之前，开源社区一直有动力做优化，尽管优化会带来工程上的复杂度。

- 以上列出的技术点几乎不存在冲突，可以叠加使用，但叠加到一起是一个巨大的工程挑战。

- scale law的存在，模型依旧在变大，序列长度继续增加，请求的规模也在增加。

- 目前llm为各个领域赋能，正处于加速落地阶段，在应用领域开始百花齐放。

- 调度层面（服务系统）实现复杂度在变高，对工程力要求也迅速变高，最终演变出新的推理框架（如tensorrt-llm，vllm，mooncake等），而一个良好的推理框架是需要投入极大的研发成本的。

- 大模型由黑铁时代->黄金时代 - >白银时代。低垂的果实已经摘的差不多了，但依旧有众多方向未收敛，后续还会有不少创新（包括 algo->拼idea、infra->拼工程实力 ），需要继续跟踪（有些技术是几天前的工作），不然几个月的时间内就会掉队。

- algo、infra迭代迅速，但是硬件迭代并不快速。因此很多优化越来越与硬件绑定，需要充分利用硬件性能，构成了algo，infra围绕nvidia产品进行迭代，而nv构建的生态（cuda等）也为很多优秀的idea（vllm，mooncake，llm量化）带来了快速落地的实际收益，实现了双赢（很多专用芯片因为使用难度高不利于新技术的传播）。

- 成本意识，如何做到低成本推理。不仅仅要考虑硬件成本的降低，好的软件+算法优化手段可以带来1-2个数量级的降本。