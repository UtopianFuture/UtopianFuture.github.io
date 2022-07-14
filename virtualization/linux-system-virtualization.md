## linux 系统虚拟化

### 零、背景

1. 设计目标：

   在一台 x86 的多核机器上运行多个虚拟机，这些虚拟机上能够执行非 x86 架构的系统。

2. 问题：

   （1）vm 到底是什么样的？

   其拥有的只是一系列的数据结构，而不拥有实际的资源。访问这些数据结构在 guest 看来就是在访问实际的硬件资源，但是这些访问要通过 kvm 的处理才能返回。

   （2）guest 和 host 的关系？

   guest 是 host 上运行的一个进程，用“进程 id” – VMCS 来记录实际状态。

   （3）kvm 其的作用是什么？

    首先，CPU 虚拟化就是需要用 kvm 来实现的，KVM 会创建 VMCS 数据结构，分配内存条等等。

   （4）虚拟中断的核心是什么？

   （5）硬件虚拟化的核心是什么？

### 一、CPU 虚拟化

​      物理 CPU 同时运行 host 和 guest，**在不同模式间按需切换**，每个模式需要保存上下文。VMX 设计了`VMCS`，每个 guest 都有自己的`VMCS`。通过 `VMLaunch `切换到 not root 模式，即进入 guest(vm entry)，当需要执行敏感指令时，退回到 root 模式，即退出 guest(vm exit)。

#### 陷入和模拟

1. 常用的外设访问方式：

   （1）MMIO

   I/O 设备被映射到内存地址空间而不是 I/O 空间。当程序需要访问 I/O 时，会导致 CPU 页面异常，从而退出 guest 进入 KVM 进行异常处理。这个异常处理的过程就是虚拟，即模拟指令的执行。KVM 根据指令目的操作数的地址找到 MMIO 设备，然后调用具体的 MMIO 设备处理函数，之后就是正常的 I/O 操作。

   （2）PIO

   使用专用的 I/O 指令 `(out, outs, in, ins) `访问外设。

2. 特殊指令

   （1）CPUID

   KVM 的用户空间通过 `cpuid` 指令获取 Host 的 CPU 特征，加上用户空间的配置，定义好 VCPU 支持的 CPU 特性，传递给 KVM 内核模块。

   KVM 用户空间按照如下结构体组织好 CPU 特性后传递给内核模块：

   ```c
   struct kvm_cpuid{
   	__u32 nent;
   	__u32 padding;
   	struct kvm_cpuid_entry entries[0];
   }
   ```

​	`kvm_cpuid_entry`就是 kvm 内核模块定义的 VCPU 特性结构体：

```c
struct kvm_cpuid_entry {
	__u32 function;
	__u32 eax;
	__u32 ebx;
	__u32 ecx;
	__u32 edx;
	__u32 padding;
};
```

​		guest 执行 cpuid 时发生 VM exit，KVM 会根据 `eax` 中的功能号以及`ecx`中的子功能号，从 `kvm_cpuid_entry` 实例中所以到相应的 entry，使用 entry 中的 `eax`, `ebx`, `ecx`, `edx` 覆盖结构体 vcpu 中的 regs 数组对应的字段，当再次切入 guest 时，KVM 会将他们加载到物理 CPU 的通用寄存器，guest 就可以正常的读取。

​	（2）hlt

​	guest 执行 hlt 只是暂停逻辑核。VCPU 调用内核的 `schedule()` 将自己挂起。

#### 对称多处理器虚拟化



#### KVM 用户空间实例

​      定义结构体 vm 表示一台虚拟机，每个虚拟机可能有多个处理器，每个处理器有自己的状态，定义结构体 vcpu 表示处理器。

1. 创建虚拟机实例

   每一台虚拟机都需要在 kvm 中有一个实例与其对应。KVM 中的 `KVM_CREATE_VM` 用来创建虚拟机，并返回指向这个虚拟机实例的一个文件描述符，之后的操作都需要通过文件描述符。

2. 创建内存

   物理机器通过插入的内存条来提供内存。KVM 通过定义数据结构来提供虚拟内存：

```c
struct kvm_userspace_memory_region {
	__u32 slot;
	__u32 flags;
	__u32 guest_phys_addr;
	__u32 memory_size;
	__u64 userspace_addr;
}
```

3. 创建处理器

4. 准备 guest，加载 guest 到内存

5. 运行 vm

### 二、内存虚拟化

1. VMM 为 Guest 准备物理内存

   系统启动后，先运行 bios，bios 会检查内存信息，记录下来，并对外提供内存查询功能。bios 将中断向量表 `(IVT)` 的第 0x15 个表项中的地址设置为查询内存信息函数的地址，后续的 os 就可以通过发起 0x15 中断来获取系统内存信息。VMM 需要模拟这个功能，在 bios 数据区中构造内存信息表。

   在这个数据结构中：

   ```c
   struct kvm_userspace_memory_region {
   	__u32 slot;
   	__u32 flags;
   	__u32 guest_phys_addr;
   	__u32 memory_size;
   	__u64 userspace_addr;
   };
   ```

   `	slot` 表示这个结构体 `kvm_userspace_memory_region` 实例描述的是第几个内存条， `guest_phys_addr` 表示这块内存条在 guest 物理地址空间中的起始地址，`memory_size` 表示内存条大小， `userspace_addr` 表示 host 用户空间的起始地址，这个是虚拟地址，guest 的内存条就不用直接在物理内存上分配，能提高内存的利用率。

2. 保护模式的 guest 寻址

   在没有虚拟化的情况下，cr3 寄存器指向的是 GPA 到 HPA 转换的映射表。但在虚拟化需要在 guest 中将 GVA 转换成 GPA，然后 MMU 将 GPA 转换成 HPA。

   影子页表：KVM 建立的一张将 GVA 转换成 HPA 的表，这张表需要根据 guest 内部页表的信息更新。guest 中每个任务都有对应的也表，所有当 guest 任务切换时，影子页表也需要跟着切换。

3. EPT

   EPT(extented page table pointer) 是硬件机制，完成从 GPA 到 HPA 的映射。EPT 的缺页处理的原理与 MMU 基本相同。**MMU 完成 GVA  到 GPA 的映射，EPT 完成 GPA  到 HPA  的映射，MMU 和  EPT 在硬件层面直接配合，不需要软件干涉，经过 MMU 翻译的 GPA 将在硬件层面给到 EPT** 。增加 EPT 后不需要频繁的 VM exit，同时，对于 host 而言，一个虚拟机就是一个进程，因此一个虚拟机只需要维护一个 EPT 表即可。

   VMX 在 `VMCS` 中定义了一个字段 `extended-page-table-pointer`，KVM 将 EPT 页表的位置写入这个字段，这样当 CPU 进入 guest 模式时，就可以从这个字段读出 EPT 页表的位置。而 guest 模式下的 `cr3` 寄存器指向 guest 内部的页表。

   当 guest 发生缺页异常时，CPU 不再切换到 host 模式，而是由 guest 自身的缺页异常处理函数处理。当地址从 GVA 翻译到 GPA 后，GPA 在硬件内部从 MMU 流转到 EPT。如果 EPT 页表中存在 GPA 到 HPA 的映射，则 EPT 最终获取了对应的 HPA，将 HPA 送上地址总线。如果不存在映射，那**么抛出 EPT 异常， CPU 将从 guest 模式切换到 host 模式，进行正常的异常处理**。建立好映射之后返回 guest 模式。

   需要退出 guest 时，会将引发异常的 GPA 保存到 `VMCS` 的 `guest physical address` 字段，然后 KVM 就可以根据这个 GPA 调用异常处理函数，处理 EPT 缺页异常。

   简单举例：如果 guest 采用 2 级页表，那么在通过一级页表目录读取二级页表地址时，需要通过 EPT，然后通过二级页表读取页帧地址时，需要通过 EPT，最后通过 offset 在页帧中读取字节需要通过 EPT。

### 三、中断虚拟化

1. 虚拟中断

   物理 CPU 在执行完一条指令后，都会检查中断引脚是否有效，一旦有效，CPU 将处理中断，然后执行下一条指令。

   对于软件虚拟的中断芯片而言，**“引脚”只是一个变量**。如果 KVM 发现虚拟中断芯片有中断请求，则向 `VMCS` 中的 `VM-entry control` 部分的 `VM-entry interruption-information field` 字段写入中断信息，在切入 guest 模式的一刻，**CPU** 将检查这个字段，如同检查 CPU 引脚，如果有中断，则进入中断执行过程。

   guest 模式的 CPU 不能检测虚拟中断芯片的引脚，只能在 VM entry 时由 KVM 模块代为检查，然后写入 `VMCS`，一旦有中断注入，那么处于 guest 模式的 CPU 一定需要通过 VM exit 退出到 host 模式，这个上下文切换很麻烦。

   在硬件层面增加对虚拟化的支持。在 guest 模式下实现 `virtual-APIC page` 页面和虚拟中断逻辑。**遇到中断时，将中断信息写入 `posted-interrupt descriptor`，然后通过特殊的核间中断 `posted-interrupt notification` 通知 CPU，guest 模式下的 CPU 就可以借助虚拟中断逻辑处理中断**。

2. PIC 虚拟化

   PIC（可编程中断控制器，programmable interrupt controller），通过引脚向 CPU 发送中断信号。而虚拟设备请求中断是通过虚拟 8259A 芯片对外提供的一个 API。

   （1）虚拟设备向 PIC 发送中断请求

   guest 需要读取外设数据时，通过写 I/O 端口触发 CPU 从 guest 到 host，KVM 中的块设备开始 I/O 操作。操作完后调用虚拟 8259A 提供的 API 发出中断请求。

   （2）记录中断到 IRR (Interrupt Request Register)。

   （3）设置待处理中断标识

   虚拟 PIC 是被动中断，需要设置中断变量，**等 VM entry 时 KVM 会检查是否有中断请求，如果有，则将需要处理的中断信息写入 VMCS 中**。

   （4）中断评估

   即评估中断优先级。

   （5）中断注入

   VMCS 中有字段：`VM-entry interruption-information`，在 VM-entry 时 CPU 会检查这个字段。如果 CPU 正处在 guest 模式，则等待下一次 VM exit 和 VM entry；如果 VCPU 正在睡眠状态，则 kick。

3. APIC 虚拟化

   APIC( Advanced Programmable Interrupt Controller)，其可以将接收到的中断按需分给不同的 processor 进行处理，而 PIC 只能应用于单核。

   APIC 包含两个部分：`LAPIC` 和 `I/O APIC`， LAPIC 位于处理器一端，接收来自 I/O APIC 的中断和核间中断 IPI(Inter Processor Interrupt)；I/O APIC 一般位于南桥芯片，相应来自外部设备的中断，并将中断发送给 LAPIC。其中断过程和 PIC 类似。

   （1）核间中断过程

   当 guest 发送 IPI 时，虚拟 LAPIC 确定目的 VCPU，向目的 VCPU 发送 IPI，实际上是向目的 VCPU 对应的虚拟 LAPIC 发送核间中断，再由目标虚拟 LAPIC 完成中断注入过程。

4. MSI(X)虚拟化（没怎么懂）

   不基于管脚，而是基于消息。中断信息从设备直接发送到 LAPIC，不用通过 I/O LAPIC。

5. 硬件虚拟化支持

   在基于软件虚拟中断芯片中，只能在 VM entry 时向 guest 注入中断，必须触发一次 VM exit，这是中断虚拟化的主要开销。

   （1）`virtual-APIC page`。LAPIC 中有一个 4KB 大小的页面，intel 称之为 APIC page，LAPIC 的所有寄存器都存在这个页面上。当内核访问这些寄存器时，将触发 guest 退出到 host 的 KVM 模块中的虚拟 LAPIC。intel 在 guest 模式下实现了一个用于存储中断寄存器的 `virtual-APIC page`。配置之后的中断逻辑处理，很多中断就无需 vmm 介入。但发送 IPI 还是需要触发 VM exit。通过 `virtual-APIC page` 维护寄存器的状态，guest 读取这些寄存器时无需切换状态，而写入时需要切换状态。

   （2）guest 模式下的中断评估逻辑。guest 模式下的 CPU 借助 VMCS 中的字段 `guest interrupt status` 评估中断。当 guest 开中断或者执行完不能中断的指令后，CPU 会检查这个字段是否有中断需要处理。（这个检查的过程是谁规定的？guest 模式下的 CPU 自动检查 VMCS）。

   （3）`posted-interrupt processing`。当 CPU 支持在 guest 模式下的中断评估逻辑后，虚拟中断芯片可以在收到中断请求后，由 guest 模式下的中断评估逻辑评估后，将中断信息更新到 `posted-interrupt descriptor` 中，然后向处于 guest 模式下的 CPU 发送 `posted-interrupt notification`，向 guest 模式下的 CPU 直接递交中断。

### 四、设备虚拟化

设备虚拟化就是系统虚拟化软件使用软件的方式呈现给 guest 硬件设备的逻辑。

1. PCI 配置空间及其模拟

   每个 PCI 设备都需要实现一个配置空间(configuration space)的结构，用来存储设备的基本信息和中断信息。系统初始化时 BIOS（或者 UEFI）将把 PCI 设备的配置空间映射到处理器的 I/O 地址空间，之后 OS 通过 I/O 端口访问配置空间中的寄存器。

   新的 PCI 设备会**自动**向 OS 申请需要的地址空间大小等，然后将这些需求即如在配置空间的寄存器 BAR 中。在系统初始化时 BIOS 会查询所有设备的 BAR 寄存器，统一为 PCI 设备分配地址空间。

   PCI 总线通过 PCI host bridge 和 CPU 总线相连，PCI host bridge 和 PCI 设备之间通过 PCI 总线通信。PCI host bridge 内部有两个寄存器用于系统软件访问 PCI 设备的配置空间，一个是 `CONFIG_ADDRESS`，另一个是 `CONFIG_DATA`。当系统软件访问 PCI 设备配置空间中的寄存器时，首先将目标地址写入寄存器 `CONFIG_ADDRESS` 中，然后向寄存器 `CONFIG_DATA` 发起访问操作。当 PCI host bridge 接收到访问 `CONFIG_DATA` 的信号，其将 `CONFIG_ADDRESS` 中的地址转换成 PCI 总线地址格式，根据地址信息片选 PCI 设备，然后根据其中的功能号和寄存器号发送到 PCI 总线上。目标 PCI 设备在接收到信息后，发送数据。

   当 guest 访问 `CONFIG_ADDRESS` 时会触发 VM exit 陷入 VMM，VMM 进行模拟处理。而之后 guest 将通过访问 `CONFIG_DATA` 读写 PCI 配置空间头的信息，这个操作也会触发 VM exit，进入 KVM（一次访问设备需要两次 exit，加上设备准备好之后的请求中断，也需要 exit，效率很低）。

2. 设备透传

   SR-IOV(Single Root I/O Virtualization and Sharing)，**在硬件层面将一个物理设备虚拟出多个设备，每个设备可以透传给一台虚拟机**。 SR-IOV 引入了两个新的 function 类型，一个是 PF(Physical Function)，一个是 VF(Virtual Function)。一个 SR-IOV 可以支持多个 VF，每个 VF 可以分别透传给 guest，guest 就不用通过 VMM 的模拟设备访问物理设备。每个 VF 都有自己独立的用于数据传输的存储空间、队列、中断以及命令处理单元，即虚拟的物理设备，VMM 通过 PF 管理这些 VF。同时，host 上的软件仍然可以通过 PF 访问物理设备。

   （1）虚拟配置空间

   guest 访问 VF 的数据不需要经过 VMM，guest 的 I/O 性能提高了，但出于安全考虑，guest 访问 VF 的**配置空间**时会触发 VM exit 陷入 VMM，这一过程不会影响数据传输效率（但是上下文切换不也降低性能么？注意是配置空间，不是数据）。

   在系统启动时，host 的 bios 为 VF 划分了内存地址空间并存储在寄存器 BAR 中，而且 guest 可以直接读取这个信息，但是因为 guest 不能直接访问 host 的物理地址，所有 kvmtool 要将 VF 的 BAR 寄存器中的`HPA`转换为`GPA`，这样 guest 才可以直接访问。之后当 guest 发出对 BAR 对应的内存地址空间的访问时，EPT 或影子页表会将`GPA`翻译为`HPA`，PCI 或 Root Complex 将认领这个地址，完成外设读取工作。

   （2）DMA 重映射

   采用设备透传时，guest 能够访问该设备下其他 guest 和 host 的内存，导致安全隐患。为此，设计了 DMA 重映射机制。

   当 VMM 处理透传给 guest 的外设时，VMM 将请求 kernel 为 guest 建立一个页表，这个页表的翻译由 DMA 重映射硬件负责，它限制了外设只能访问这个页面覆盖的内存。当外设访问内存时，内存地址首先到达 DMA 重映射硬件，DMA 重映射硬件根据这个外设的总线号、设备号和功能号确定其对应的页表，查表的出物理内存地址，然后将地址送上总线。

   （3）中断重映射

   为避免外设编程发送一些恶意的中断引入了中断虚拟化机制，即在外设和 CPU 之间加了一个硬件中断重映射单元(IOMMU)。当接受到中断时，该单元会对中断请求的来源进行有效性验证，然后以中断号为索引查询中断重映射表，之后代发中断。中断映射表由 VMM 进行设置。
