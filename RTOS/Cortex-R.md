## What is Cortex R?

A系列功能强大，对标通用处理器，可运行大型OS。有MMU，TCM，cache，debug，中断，FPU，DSP等各类功能单元。总线接口也以高性能，支持多核cache一致性为主。

R系列针对实时性场景，相比A系列，少了对页表的支持，也就是说软件看到的地址都是物理地址，相对来说软件运行时间和中断响应速度都更加快速稳定，容易预测。

M系列针对MCU场景，相比R就更加精简了，更短的流水线，更简单的指令集，更少的运算单元，调试单元，总线性能要求不高，以低功耗为主。

### features

- Armv8-R implementations support EL0, EL1, and EL2;
- Armv8-A, Supports a Virtual Memory System Architecture (VMSA) based on a Memory Management Unit (MMU).

- Armv8-R, Supports a Protected Memory System Architecture (PMSAv8-32) based on a Memory Protection Unit (MPU).
- The Protected Memory System Architecture (PMSA) that defines memory ordering and memory management with a single 32-bit physical address (PA) space.

- Each PMSAv8-32 MPU has an associated default memory map which is used when the MPU is not enabled. 这个 memory map 有什么用？