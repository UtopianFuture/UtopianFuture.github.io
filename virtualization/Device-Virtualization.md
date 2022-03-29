## Device Virtualization

设备虚拟化可以分为完全虚拟化和半虚拟化。完全虚拟化就是类似与 QEMU 的设备模拟，完全用软件来做，通过 Inter-Virtualization 大致就懂了，这里不再介绍。这篇文章来分析设备透传和 Virtio 虚拟化。

### 1. 设备透传

SR-IOV(Single Root I/O Virtualization and Sharing)，**在硬件层面将一个物理设备虚拟出多个设备，每个设备可以透传给一台虚拟机**。 SR-IOV 引入了两个新的 function 类型，一个是 PF(Physical Function)，一个是 VF(Virtual Function)。一个 SR-IOV 可以支持多个 VF，每个 VF 可以分别透传给 guest，guest 就不用通过 VMM 的模拟设备访问物理设备。每个 VF 都有自己独立的用于数据传输的存储空间、队列、中断以及命令处理单元，即虚拟的物理设备，VMM 通过 PF 管理这些 VF。同时，host 上的软件仍然可以通过 PF 访问物理设备。

#### 1.1. 虚拟配置空间

guest 访问 VF 的数据不需要经过 VMM，guest 的 I/O 性能提高了，但出于安全考虑，guest 访问 VF 的**配置空间**时会触发 VM exit 陷入 VMM，这一过程不会影响数据传输效率（但是上下文切换不也降低性能么？注意是配置空间，不是数据）。

在系统启动时，host 的 bios 为 VF 划分了内存地址空间并存储在寄存器 BAR 中，而且 guest 可以直接读取这个信息，但是因为 guest 不能直接访问 host 的物理地址，所有 kvmtool 要将 VF 的 BAR 寄存器中的`HPA`转换为`GPA`，这样 guest 才可以直接访问。之后当 guest 发出对 BAR 对应的内存地址空间的访问时，EPT 或影子页表会将`GPA`翻译为`HPA`，PCI 或 Root Complex 将认领这个地址，完成外设读取工作。

#### 1.2. DMA 重映射

采用设备透传时，guest 能够访问该设备下其他 guest 和 host 的内存，导致安全隐患。为此，设计了 DMA 重映射机制。

当 VMM 处理透传给 guest 的外设时，VMM 将请求 kernel 为 guest 建立一个页表，这个页表的翻译由 DMA 重映射硬件负责，它限制了外设只能访问这个页面覆盖的内存。当外设访问内存时，内存地址首先到达 DMA 重映射硬件，DMA 重映射硬件根据这个外设的总线号、设备号和功能号确定其对应的页表，查表的出物理内存地址，然后将地址送上总线。

#### 1.3. 中断重映射

为避免外设编程发送一些恶意的中断引入了中断虚拟化机制，即在外设和 CPU 之间加了一个硬件中断重映射单元(IOMMU)。当接受到中断时，该单元会对中断请求的来源进行有效性验证，然后以中断号为索引查询中断重映射表，之后代发中断。中断映射表由 VMM 进行设置。

### 2. Virtio 虚拟化

与完全虚拟化相比，使用 Virtio 协议的驱动和设备模拟的交互不再使用寄存器等传统的 I/O 方式，而是采用了 Virtqueue 的方式来传输数据。这种方式减少了 vm exit 和 vm entry 的次数，提高了设备访问性能。

#### 2.1. 执行流程

下面以 virtio-blk 为例，简单描述一下 read request 从发出到读到数据的过程。

- VM 中，guest OS 的用户空间进程通过系统调用发出 read request，进程被挂起，等待数据（同步 read）。此时进入到内核空间，request 依次经过 vfs、page cache、具体 fs（通常是 ext4 或者 xfs）、buffer cache、generic block layer 后，来到了 virtio-blk 的驱动中。

- virtio-blk 驱动将 request 放入 virtio 的 vring 中的 avail ring 中，vring 是一个包含 2 个 ring buffer 的结构，一个 ring buffer 用于发送 request，称为 avail ring，另一个用于接收数据和资源回收，称为 used ring。在适当的时机，guest 会通知 host 有 request 需要处理，此动作称为 kick，在此之前会将 avail ring 中管理的 desc 链挂入 used ring 中，后续在 host 中可以通过 used ring 找到所有的 desc，每个 desc 用于管理一段 buffer。

- host 收到 kick 信号，就开始处理 avail ring 中的 request。主要是通过 avail ring 中的 index 去 desc 数组中找到真正的 request，建立起 desc 对应的 buffer 的内存映射，即建立 guest kernel 地址 GPA 与 qemu 进程空间地址 HVA 的映射，然后将 request 转发到 qemu 的设备模拟层中（此时没有内存的 copy），qemu 设备模拟层再把 request 经过合并排序形成新的 request 后，将新 request 转发给 host 的 linux IO 栈，也就是通过系统调用，进入 host 内核的 IO 栈，由内核把新 request 发给真实的外设（磁盘）。真实的外设收到新 request 后，将数据通过 DMA 的方式传输到 DDR 中 host 的内核空间，然后通过中断通知 host 内核 request 已经处理完成。host 的中断处理程序会将读到的数据 copy 到 qemu 的地址空间，然后通知 qemu 数据已经准备好了。qemu 再将数据放入 vring 的 used ring 管理的 desc 中（也即 avail ring 中的 desc，由 avail ring 挂入到了 used ring 中的），这里也没有数据的 copy，通过使用之前建立的 GPA 和 HVA 的映射完成。host 再通过虚拟中断通知 guest 数据已经就绪。4）guest 收到中断，中断处理程序依次处理如下：a）回收 desc 资源。存放读到的数据的 buffer 在不同的层有不同的描述方式，在 virtio 层中是通过 desc 来描述的，在 block 层中是通过 sglist 描述的。在 guest kernel 发出 read request 的时候，已经分别在 block 层和 virtio 层建立了 buffer 的描述方式，即在任何层可以都找到 buffer。具体请参考附件 pdf。所以此时回收 desc 不会导致 block 层找不到读到的数据。b）将数据 copy 到等待数据的用户进程（即发出 read request 的用户进程)。

至此，read request 流程结束。write request 与 read request 的处理流程相同。

![virtio](https://github.com/UtopianFuture/UtopianFuture.github.io/blob/master/image/virtio.png)

#### 2.2. Virtio 协议

Virtio 的核心数据结构是 Virtqueue，其是 guestOS 驱动和 VMM 中模拟设备之间传输数据的载体。一个设备可以有一个 Virtqueue，也可以有多个 Virtqueue。Virtqueue 主要包含 3 个部分：描述符表（vring_desc）、可用描述符区域（vring_avail）和已用描述符表（vring_used）。

```c
/* Virtio ring descriptors: 16 bytes.  These can chain together via "next". */
struct vring_desc {
	/* Address (guest-physical). */
	__virtio64 addr;
	/* Length. */
	__virtio32 len;
	/* The flags as indicated above. */
	__virtio16 flags;
	/* We chain unused descriptors via this, too */
	__virtio16 next;
};

struct vring_avail {
	__virtio16 flags;
	__virtio16 idx;
	__virtio16 ring[];
};

struct vring_used {
	__virtio16 flags;
	__virtio16 idx;
	vring_used_elem_t ring[];
};
```

每个描述符指向一块内存，该内存保存 guest 写入虚拟设备或虚拟设备写入 guest 的数据。Virtqueue 由 guest 中的驱动负责。

### reference

[1] https://www.cxybb.com/article/gong0791/79578316
