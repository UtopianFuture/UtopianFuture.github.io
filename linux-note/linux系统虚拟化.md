## linux系统虚拟化

### 零、背景

1. 设计目标：

   在一台x86的多核机器上运行多个虚拟机，这些虚拟机上能够执行非x86架构的系统。

2. 问题：

   （1）vm到底是什么样的？

   其拥有的只是一系列的数据结构，而不拥有实际的资源。访问这些数据结构在guest看来就是在访问实际的硬件资源，但是这些访问要通过kvm的处理才能返回。

   （2）guest和host的关系？

   guest是host上运行的一个进程，用“进程id” – VMCS来记录实际状态。

   （3）kvm其的作用是什么？

   首先，CPU虚拟化就是需要用kvm来实现的，KVM会创建VMCS数据结构，分配内存条等等。

   （4）虚拟中断的核心是什么？

   （5）硬件虚拟化的核心是什么？

### 一、CPU虚拟化

​      物理CPU同时运行host和guest，**在不同模式间按需切换**，每个模式需要保存上下文。VMX设计了`VMCS`，每个guest都有自己的`VMCS`。通过`VMLaunch`切换到not root模式，即进入guest(vm entry)，当需要执行敏感指令时，退回到root模式，即退出guest(vm exit)。

#### 陷入和模拟

1. 常用的外设访问方式：

   （1）MMIO

   I/O设备被映射到内存地址空间而不是I/O空间。当程序需要访问I/O时，会导致CPU页面异常，从而退出guest进入KVM进行异常处理。这个异常处理的过程就是虚拟，即模拟指令的执行。KVM根据指令目的操作数的地址找到MMIO设备，然后调用具体的MMIO设备处理函数，之后就是正常的I/O操作。

   （2）PIO

   使用专用的I/O指令`(out, outs, in, ins)`访问外设。

2. 特殊指令

   （1）CPUID

   KVM的用户空间通过`cpuid`指令获取Host的CPU特征，加上用户空间的配置，定义好VCPU支持的CPU特性，传递给KVM内核模块。

   KVM用户空间按照如下结构体组织好CPU特性后传递给内核模块：

```
struct kvm_cpuid{
	__u32 nent;
	__u32 padding;
	struct kvm_cpuid_entry entries[0];
}
```

​	`kvm_cpuid_entry`就是kvm内核模块定义的VCPU特性结构体：

```
struct kvm_cpuid_entry {
	__u32 function;
	__u32 eax;
	__u32 ebx;
	__u32 ecx;
	__u32 edx;
	__u32 padding;
};
```

​		guest执行cpuid时发生VM exit，KVM会根据`eax`中的功能号以及`ecx`中的子功能号，从`kvm_cpuid_entry`实例中所以到相应的entry，使用entry中的`eax`, `ebx`, `ecx`, `edx`覆盖结构体vcpu中的regs数组对应的字段，当再次切入guest时，KVM会将他们加载到物理CPU的通用寄存器，guest就可以正常的读取。

​	（2）hlt

​	guest执行hlt只是暂停逻辑核。VCPU调用内核的schedule()将自己挂起。

#### 对称多处理器虚拟化



#### KVM用户空间实例

​      定义结构体vm表示一台虚拟机，每个虚拟机可能有多个处理器，每个处理器有自己的状态，定义结构体vcpu表示处理器。

1. 创建虚拟机实例

   每一台虚拟机都需要在kvm中有一个实例与其对应。KVM中的`KVM_CREATE_VM`用来创建虚拟机，并返回指向这个虚拟机实例的一个文件描述符，之后的操作都需要通过文件描述符。

2. 创建内存

   物理机器通过插入的内存条来提供内存。KVM通过定义数据结构来提供虚拟内存：

```
struct kvm_userspace_memory_region {
	__u32 slot;
	__u32 flags;
	__u32 guest_phys_addr;
	__u32 memory_size;
	__u64 userspace_addr;
}
```

3. 创建处理器

4. 准备guest，加载guest到内存

5. 运行vm

### 二、内存虚拟化

1. VMM为Guest准备物理内存

   系统启动后，先运行bios，bios会检查内存信息，记录下来，并对外提供内存查询功能。bios将中断向量表`(IVT)`的第0x15个表项中的地址设置为查询内存信息函数的地址，后续的os就可以通过发起0x15中断来获取系统内存信息。VMM需要模拟这个功能，在bios数据区中构造内存信息表。

   在这个数据结构中：

```
struct kvm_userspace_memory_region {
	__u32 slot;
	__u32 flags;
	__u32 guest_phys_addr;
	__u32 memory_size;
	__u64 userspace_addr;
};
```

​	`	slot`表示这个结构体 `kvm_userspace_memory_region`实例描述的是第几个内存条， `guest_phys_addr`表示这块内存条在guest物理地址空间中的起始地址，`memory_size`表示内存条大小， `userspace_addr`表示host用户空间的起始地址，这个是虚拟地址，guest的内存条就不用直接在物理内存上分配，能提高内存的利用率。

2. 保护模式的guest寻址

   在没有虚拟化的情况下，cr3寄存器指向的是GPA到HPA转换的映射表。但在虚拟化需要在guest中将GVA转换成GPA，然后MMU将GPA转换成HPA。

   影子页表：KVM建立的一张将GVA转换成HPA的表，这张表需要根据guest内部页表的信息更新。guest中每个任务都有对应的也表，所有当guest任务切换时，影子页表也需要跟着切换。

3. EPT

   EPT(extented page table pointer)是硬件机制，完成从GPA到HPA的映射。EPT的缺页处理的原理与MMU基本相同。增加EPT后不需要频繁的VM exit，同时，对于host而言，一个虚拟机就是一个进程，因此一个虚拟机只需要维护一个EPT表即可。

   VMX在`VMCS`中定义了一个字段`extended-page-table-pointer`，KVM将EPT页表的位置写入这个字段，这样当CPU进入guest模式时，就可以从这个字段读出EPT页表的位置。而guest模式下的`cr3`寄存器指向guest内部的页表。当guest发生缺页异常，需要退出guest时，会将引发异常的GPA保存到`VMCS`的`guest physical address`字段，然后KVM就可以根据这个GPA调用异常处理函数，处理EPT缺页异常。

   简单举例：如果guest采用2级页表，那么在通过一级页表目录读取二级页表地址时，需要通过EPT，然后通过二级页表读取页帧地址时，需要通过EPT，最后通过offset在页帧中读取字节需要通过EPT。

### 三、中断虚拟化

1. 虚拟中断

   物理CPU在执行完一条指令后，都会检查中断引脚是否有效，一旦有效，CPU将处理中断，然后执行下一条指令。

   对于软件虚拟的中断芯片而言，**“引脚”只是一个变量**。如果KVM发现虚拟中断芯片有中断请求，则向`VMCS`中的`VM-entry control`部分的`VM-entry interruption-information field`字段写入中断信息，在切入guest模式的一刻，**CPU**将检查这个字段，如同检查CPU引脚，如果有中断，则进入中断执行过程。

   guest模式的CPU不能检测虚拟中断芯片的引脚，只能在VM entry时由KVM模块代为检查，然后写入`VMCS`，一旦有中断注入，那么处于guest模式的CPU一定需要通过VM exit退出到host模式，这个上下文切换很麻烦。

   在硬件层面增加对虚拟化的支持。在guest模式下实现`virtual-APIC page`页面和虚拟中断逻辑。遇到中断时，将中断信息写入`posted-interrupt descriptor`，然后通过特殊的核间中断`posted-interrupt notification`通知CPU，guest模式下的CPU就可以借助虚拟中断逻辑处理中断。

2. PIC虚拟化

   PIC（可编程中断控制器，programmable interrupt controller），通过引脚向CPU发送中断信号。而虚拟设备请求中断是通过虚拟8259A芯片对外提供的一个API。

   （1）虚拟设备向PIC发送中断请求

   guest需要读取外设数据时，通过写I/O端口触发CPU从guest到host，KVM中的块设备开始I/O操作。操作完后调用虚拟8259A提供的API发出中断请求。（2）记录中断到IRR

   （2）IRR(Interrupt Request Register)。

   （3）设置待处理中断标识

   虚拟PIC是被动中断，需要设置中断变量，等VM entry时KVM会检查是否有中断请求，如果有，则将需要处理的中断信息写入VMCS中。

   （4）中断评估

   即评估中断优先级。

   （5）中断注入

   VMCS中有字段：`VM-entry interruption-information`，在VM-entry时CPU会检查这个字段。如果CPU正处在guest模式，则等待下一次VM exit和VM 	entry；如果VCPU正在睡眠状态，则kick。

3. APIC虚拟化

   APIC( Advanced Programmable Interrupt Controller)，其可以将接收到的中断按需分给不同的processor进行处理，而PIC只能应用于单核。

   APIC包含两个部分：`LAPIC`和`I/O APIC`， LAPIC位于处理器一端，接收来自I/O APIC的中断和核间中断IPI(Inter Processor Interrupt)；I/O APIC一般位于南桥芯片，相应来自外部设备的中断，并将中断发送给LAPIC。其中断过程和PIC类似。

   （1）核间中断过程

   当guest发送IPI时，虚拟LAPIC确定目的VCPU，向目的VCPU发送IPI，实际上是向目的VCPU对应的虚拟LAPIC发送核间中断，再由目标虚拟LAPIC完成中断注入过程。

4. MSI(X)虚拟化（没怎么懂）

   不基于管脚，而是基于消息。中断信息从设备直接发送到LAPIC，不用通过I/O LAPIC。

5. 硬件虚拟化支持

   在基于软件虚拟中断芯片中，只能在VM entry时向guest注入中断，必须触发一次VM exit，这是中断虚拟化的主要开销。

   （1）`virtual-APIC page`。LAPIC中有一个 4KB大小的页面，intel称之为APIC page，LAPIC的所有寄存器都存在这个页面上。当内核访问这些寄存器时，将触发guest退出到host的KVM模块中的虚拟LAPIC。intel在guest模式下实现了一个用于存储中断寄存器的 `virtual-APIC page`。配置之后的中断逻辑处理，很多中断就无需vmm介入。但发送IPI还是需要触发VM exit。通过 `virtual-APIC page`维护寄存器的状态，guest读取这些寄存器时无需切换状态，而写入时需要切换状态。

   （2）guest模式下的中断评估逻辑。guest模式下的CPU借助VMCS中的字段`guest interrupt status`评估中断。当guest开中断或者执行完不能中断的指令后，CPU会检查这个字段是否有中断需要处理。（这个检查的过程是谁规定的？guest模式下的CPU自动检查VMCS）。

   （3）`posted-interrupt processing`。当CPU支持在guest模式下的中断评估逻辑后，虚拟中断芯片可以在收到中断请求后，由 guest模式下的中断评估逻辑评估后，将中断信息更新到`posted-interrupt descriptor`中，然后向处于guest模式下的CPU发送`posted-interrupt notification`，向guest模式下的CPU直接递交中断。

### 四、设备虚拟化

设备虚拟化就是系统虚拟化软件使用软件的方式呈现给guest硬件设备的逻辑。

1. PCI配置空间及其模拟

   每个PCI设备都需要实现一个配置空间(configuration space)的结构，用来存储设备的基本信息和中断信息。系统初始化时BIOS（或者UEFI）将把PCI设备的配置空间映射到处理器的I/O地址空间，之后OS通过I/O端口访问配置空间中的寄存器。

   新的PCI设备会自动向OS申请需要的地址空间大小等，然后将这些需求即如在配置空间的寄存器BAR中。在系统初始化时BIOS会查询所有设备的BAR寄存器，统一为PCI设备分配地址空间。

   PCI总线通过PCI host bridge和CPU总线相连，PCI host bridge和PCI设备之间通过PCI总线通信。PCI host bridge内部有两个寄存器用于系统软件访问PCI设备的配置空间，一个是`CONFIG_ADDRESS`，另一个是`CONFIG_DATA`。当系统软件访问PCI设备配置空间中的寄存器时，首先将目标地址写入寄存器`CONFIG_ADDRESS`中，然后向寄存器`CONFIG_DATA`发起访问操作。当PCI host bridge接收到访问`CONFIG_DATA`的信号，其将`CONFIG_ADDRESS`中的地址转换成PCI总线地址格式，根据地址信息片选PCI设备，然后根据其中的功能号和寄存器号发送到PCI总线上。目标PCI设备在接收到信息后，发送数据。

   当guest访问`CONFIG_ADDRESS`时会触发VM exit陷入VMM，VMM进行模拟处理。而之后guest将通过访问`CONFIG_DATA`读写PCI配置空间头的信息，这个操作也会触发VM exit，进入KVM（一次访问设备需要两次exit，加上设备准备好之后的请求中断，也需要exit，效率很低）。

2. 设备透传

   SR-IOV(Single Root I/O Virtualization and Sharing)，**在硬件层面将一个物理设备虚拟出多个设备，每个设备可以透传给一台虚拟机**。 SR-IOV引入了两个新的function类型，一个是(Physical Function)，一个是VF(Virtual Function)。一个 SR-IOV可以支持多个VF，每个VF可以分别透传给guest，guest就不用通过VMM的模拟设备访问物理设备。每个VF都有自己独立的用于数据传输的存储空间、队列、中断以及命令处理单元，即虚拟的物理设备，VMM通过PF管理这些VF。同时，host上的软件仍然可以通过PF访问物理设备。

   （1）虚拟配置空间

   guest访问VF的数据不需要经过VMM，guest的I/O性能提高了，但出于安全考虑，guest访问VF的**配置空间**时会触发VM exit陷入VMM，这一过程不会影响数据传输效率（但是上下文切换不也降低性能么？注意是配置空间，不是数据）。

   在系统启动时，host的bios为VF划分了内存地址空间并存储在寄存器BAR中，而且guest可以直接读取这个信息，但是因为guest不能直接访问host的物理地址，所有kvmtool要将VF的BAR寄存器中的`HPA`转换为`GPA`，这样guest才可以直接访问。之后当guest发出对BAR对应的内存地址空间的访问时，EPT或影子页表会将`GPA`翻译为`HPA`，PCI或Root Complex将认领这个地址，完成外设读取工作。

   （2）DMA重映射

   采用设备透传时，guest能够访问该设备下其他guest和host的内存，导致安全隐患。为此，设计了DMA重映射机制。

   当VMM处理透传给guest的外设时，VMM将请求kernel为guest建立一个页表，这个页表的翻译由DMA重映射硬件负责，它限制了外设只能访问这个页面覆盖的内存。当外设访问内存时，内存地址首先到达DMA重映射硬件，DMA重映射硬件根据这个外设的总线号、设备号和功能号确定其对应的页表，查表的出物理内存地址，然后将地址送上总线。

   （3）中断重映射

   为避免外设编程发送一些恶意的中断引入了中断虚拟化机制，即在外设和CPU之间加了一个硬件中断重映射单元。当接受到中断时，该单元会对中断请求的来源进行有效性验证，然后以中断号为索引查询中断重映射表，之后代发中断。中断映射表由VMM进行设置。                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                          
