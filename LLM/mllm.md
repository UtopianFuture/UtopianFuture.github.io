## MLLM 介绍

AI多模态大模型发展至今，每年都有非常优秀的工作产出，按照当前模型设计思路，多模态大模型的架构主要包括以下几个部分：

- 模态编码器(Modality Encoder, ME)：负责将不同模态的输入编码成特征。常见的编码器包括图像的NFNet-F6、ViT、CLIP ViT等，音频的Whisper、CLAP等，视频编码器等。
- 输入投影器(Input Projector)：负责将其他模态的特征投影到文本特征空间，并与文本特征一起输入给语言模型。常用的投影器包括线性投影器、MLP、交叉注意力等。
- 语言模型骨架(LLM Backbone)：利用预训练的语言模型，负责处理各种模态的特征，进行语义理解、推理和决策。常用的语言模型包括Flan-T5、ChatGLM、UL2等。
- 输出投影器(Output Projector)：负责将语言模型输出的信号转换成其他模态的特征，以供后续模态生成器使用。常用的投影器包括Tiny Transformer、MLP等。
- 模态生成器(Modality Generator, MG)：负责生成其他模态的输出。常用的生成器包括图像的Stable Diffusion、视频的Zeroscope、音频的AudioLDM等。

### Referenct

[^1]: https://l12k1v7ghxm.feishu.cn/wiki/KeVmwU8W2iyWfZkb6iBcbcr6nEg
