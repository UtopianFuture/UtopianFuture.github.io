## DMA heap

Android 多媒体访问的内存接口由内核的 heap 模块提供，heap 模块内部由多种类型的 heap 可以在不同情况下使用。通过 heap 拿到内存后可以通过 smmu 映射接口转成虚拟地址给多媒体使用。

### Reference

[^1]:[Destaging ION](https://lwn.net/Articles/792733/)
[^2]:[LWN：ION 变了个形，就要打入 Linux 内部了！](https://mp.weixin.qq.com/s/P6eWS_brN4pUO97B-fWTCA)
