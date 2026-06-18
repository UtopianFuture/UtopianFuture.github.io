## Neural Process Unit

NPU 在芯片架构以及指令集上要比 CPU 简单很多，

### 指令集

NPU是具有乱序内存子系统的有序计算机。总体设计有点像是一种状态机。指令集比较简单，只有8条指令：DMA Read，DMA Write，Convolution，Deconvolution，Inner-product，Scale，Eltwidth，Stop。NPU只是运行这些命令，直到碰到停止命令为止。还有一个额外的参数slots ，可以更改指令的属性（例如，卷积运算的不同变体）。有一个标志slots ，用于数据依赖性处理。还有另一个扩展slots 。该slots 存储了整个微程序命令序列，每当有一些复杂的后处理时，这些序列就会发送到SIMD单元。因为这，指令从32字节一直到非常长的256字节不等。

### 数据流转

![img](https://img2023.cnblogs.com/blog/1251718/202305/1251718-20230508033830727-113789893.png)

### MAC 阵列

NPU 的工作就是做卷积运算，MAC 阵列的大小，决定 NPU 的算力。

![img](https://img2023.cnblogs.com/blog/1251718/202305/1251718-20230508033947256-395563458.png)

NPU Tops 计算方式：MAC 阵列数 n * NPU 峰值频率 * m（系统中 NPU 总数）

### GPU 和 NPU 的区别

GPU 是一种多功能的并行计算设备，既可用于图形处理也可用于通用计算；GPGPU 是 GPU 用于非图形计算的实践；而 NPU 则是专门针对机器学习任务优化的计算单元。

### Reference

[^1]: https://www.cnblogs.com/wujianming-110117/p/17380574.html