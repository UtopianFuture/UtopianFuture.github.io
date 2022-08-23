## Interrupt Virtualization

### 目录

- [Background](#Background)
  - [虚拟中断](#虚拟中断)
  - [PIC虚拟化](#PIC虚拟化)
  - [APIC虚拟化](#APIC虚拟化)
  - [MSI(X)虚拟化](#MSI(X)虚拟化)
    - [MSI](#MSI)
    - [MSIX](#MSIX)
  - [硬件虚拟化支持](#硬件虚拟化支持)
  - [几种中断类型](#几种中断类型)
  - [IRQ号，中断向量和GSI](#IRQ号，中断向量和GSI)
- [中断模拟](#中断模拟)
  - [KVM中断注入](#KVM中断注入)
  - [PIC中断模拟](#PIC中断模拟)
    - [KVM中PIC的创建](#KVM中PIC的创建)
    - [QEMU中PIC的初始化](#QEMU中PIC的初始化)
    - [设备使用PIC中断](#设备使用PIC中断)
  - [I/O-APIC中断模拟](#I/O-APIC中断模拟)
  - [MSI中断模拟](#MSI中断模拟)
    - [关键函数msi_write_config](#关键函数msi_write_config)
    - [关键函数msi_notify](#关键函数msi_notify)
    - [关键函数msi_get_message](#关键函数msi_get_message)
    - [关键函数msi_send_message](#关键函数msi_send_message)
  - [KVM处理ioctl(KVM_SIGNAL_MSI)](#KVM处理ioctl(KVM_SIGNAL_MSI))
    - [关键函数kvm_send_userspace_msi](#关键函数kvm_send_userspace_msi)
    - [关键函数kvm_irq_delivery_to_apic](#关键函数kvm_irq_delivery_to_apic)
  - [MSIX中断模拟](#MSIX中断模拟)
- [APIC虚拟化](#APIC虚拟化)

- [Reference](#Reference)

### Background

#### 虚拟中断

物理 CPU 在执行完一条指令后，都会检查中断引脚是否有效，一旦有效，CPU 将处理中断，然后执行下一条指令。

对于软件虚拟的中断芯片而言，**“引脚”只是一个变量**。如果 KVM 发现虚拟中断芯片有中断请求，则向 `VMCS` 中的 `VM-entry control` 部分的 `VM-entry interruption-information field` 字段写入中断信息，在切入 non-root guest 时，**CPU** 将检查这个字段，如同检查 CPU 引脚，如果有中断，则进入中断执行过程。

**non-root 模式的 CPU 不能检测虚拟中断芯片的引脚**，只能在 VM entry 时由 KVM 模块代为检查，然后写入 `VMCS`，一旦有中断注入，那么处于 non-root 模式的 CPU 一定需要通过 VM exit 退出到 root 模式，这个上下文切换很麻烦。

在硬件层面增加对虚拟化的支持。在 non-root 模式下实现 `virtual-APIC page` 页面和虚拟中断逻辑。遇到中断时，将中断信息写入`posted-interrupt descriptor`，然后通过特殊的核间中断 `posted-interrupt notification` 通知 CPU，non-root 模式下的 CPU 就可以借助虚拟中断逻辑处理中断。

#### PIC虚拟化

PIC（可编程中断控制器，programmable interrupt controller），通过引脚向 CPU 发送中断信号。而虚拟设备请求中断是通过虚拟 8259A 芯片对外提供的一个 API。

（1）虚拟设备向 PIC 发送中断请求

guest 需要读取外设数据时，通过写 I/O 端口触发 CPU 从 non-root 到 root 态，KVM 中的块设备开始 I/O 操作。操作完后调用虚拟 8259A 提供的 API 发出中断请求。

（2）记录中断到 IRR(Interrupt Request Register)。

（3）设置待处理中断标识

虚拟 PIC 是被动中断，需要设置中断变量，等 VM entry 时 KVM 会检查是否有中断请求，如果有，则将需要处理的中断信息写入 VMCS 中。

（4）中断评估

即评估中断优先级。

（5）中断注入

VMCS 中有字段：`VM-entry interruption-information`，在 VM-entry 时 CPU 会检查这个字段。**如果 CPU 正处在 non-root 模式，则等待下一次 VM exit 和 VM entry；如果 VCPU 正在睡眠状态，则 kick**。

#### APIC虚拟化

APIC( Advanced Programmable Interrupt Controller)，其可以将接收到的中断按需分给不同的 processor 进行处理，而 PIC 只能应用于单核。

APIC 包含两个部分：`LAPIC `和 `I/O APIC`， LAPIC 位于处理器一端，接收来自 I/O APIC 的中断和核间中断 IPI(Inter Processor Interrupt)；I/O APIC 一般位于南桥芯片，响应来自外部设备的中断，并将中断发送给 LAPIC。其中断过程和 PIC 类似。

（1）核间中断过程

当 guest 发送 IPI 时，虚拟 LAPIC 确定目的 VCPU，向目的 VCPU 发送 IPI，实际上是向目的 VCPU 对应的虚拟 LAPIC 发送核间中断，再由目标虚拟 LAPIC 完成中断注入过程。

#### MSI(X)虚拟化

不基于管脚，而是基于消息。中断信息从设备直接发送到 LAPIC，不用通过 I/O LAPIC。之前当某个管脚有信号时，OS 需要逐个调用共享这个管脚的回调函数去试探是否可以处理这个中断，直到某个回调函数可以正确处理，而基于消息的 MSI 就没有共享引脚这个问题。同样因为不受引脚的约束，MSI 能够支持的中断数也大大增加。

##### MSI

MSI 是在 PCIe 的基础上设计的中断方式，关于 PCIe 的介绍可以看[这里](https://github.com/UtopianFuture/UtopianFuture.github.io/blob/master/virtualization/Device-Virtualization.md#pci%E8%AE%BE%E5%A4%87%E6%A8%A1%E6%8B%9F)。从 PCI 2.1 开始，如果设备需要扩展某种特性，可以向配置空间中的 Capabilities List 中增加一个 Capability，MSI 利用这个特性，将 I/O APIC 中的功能扩展到设备自身。我们来看看 MSI Capability 有哪些域。MSI Capability 的 ID 为 5， 共有四种组成方式，分别是 32 和 64 位的 Message 结构，32 位和 64 位带中断 Masking 的结构。

![MSI-capability.png](https://github.com/UtopianFuture/UtopianFuture.github.io/blob/master/image/MSI-capability.png?raw=true)

- `Next Pointer`、`Capability ID` 这两个 field 是 PCI 的任何 Capability 都具有的 field，分别表示下一个 Capability 在配置空间的位置、以及当前 Capability 的 ID；
- `Message Address` 和 `Message Data` 是 MSI 的关键，**只要将 Message Data 中的内容写入到 Message Address 指定的地址中，就会产生一个 MSI 中断，** ` Message Address` 中存放的其实就是对应 CPU 的 LAPIC 的地址；
- `Message Control` 用于系统软件对 MSI 的控制，如 enable MSI、使能 64bit 地址等；
- `Mask Bits` 可选，Mask Bits 字段由 32 位组成，其中每一位对应一种 MSI 中断请求。
- `Pending Bits` 可选，需要与 Mask bits 配合使用， 可以防止中断丢失。当 Mask bits 为 1 的时候，设备发送的MSI中断请求并不会发出，会将  pending bits 置为1，当 mask bits 变为 0 时，MSI 会成功发出，pending 位会被清除。

我们再深入了解一下这些寄存器每一位的功能，后面需要用到。

- MSI Message Control Register

| Bits    | Register Description                                         | Default Value            | Access |
| :------ | :----------------------------------------------------------- | :----------------------- | :----- |
| [31:25] | Not implemented                                              | 0                        | RO     |
| [24]    | Per-Vector Masking Capable. This bit is hardwired to 1. The design always supports per-vector masking of MSI interrupts. | 1                        | RO     |
| [23]    | 64-bit Addressing Capable. When set, the device is capable of using 64-bit addresses for MSI interrupts. | Set in Platform Designer | RO     |
| [22:20] | Multiple Message Enable. This field defines the number of interrupt vectors for this function. The following encodings are defined:                                                                   3'b000: 1 vector                                                                                                                        3'b001: 2 vectors                                                                                                                      3'b010: 4 vectors                                                                                                                      3'b011: 8 vectors                                                                                                                      3'b100: 16 vectors                                                                                                                    3'b101: 32 vectors                                                                                                                          The Multiple Message Capable field specifies the maximum value allowed. | 0                        | RW     |
| [19:17] | Multiple Message Capable. Defines the maximum number of interrupt vectors the function is capable of supporting. The following encodings are defined:                     3'b000: 1 vector                                                                                                                        3'b001: 2 vectors                                                                                                                      3'b010: 4 vectors                                                                                                                      3'b011: 8 vectors                                                                                                                      3'b100: 16 vectors                                                                                                                    3'b101: 32 vectors | Set inPlatform Designer  | RO     |
| [16]    | MSI Enable. This bit must be set to enable the MSI interrupt generation. | 0                        | RW     |
| [15:8]  | Next Capability Pointer. Points to either MSI-X or Power Management Capability. | 0x68 or 0x78             | RO     |
| [7:0]   | Capability ID. PCI-SIG assigns this value.                   | 0x05                     | RO     |

- MSI Message Address Registers

| Bits   | Register Description                                         | Default Value | Access |
| :----- | :----------------------------------------------------------- | :------------ | :----- |
| [1:0]  | The two least significant bits of the memory address. These are hardwired to 0 to align the memory address on a Dword boundary. | 0             | RO     |
| [31:2] | Lower address for the MSI interrupt.                         | 0             | RW     |
| [31:0] | Upper 32 bits of the 64-bit address to be used for the MSI interrupt. If the 64-bit Addressing Capable bit in the MSI Control register is set to 1, this value is concatenated with the lower 32-bits to form the memory address for the MSI interrupt. When the 64-bit Addressing Capable bit is 0, this register always reads as 0. | 0             | RW     |

- MSI Message Data Register

| Bits    | Register Description                                         | Default Value | Access |
| :------ | :----------------------------------------------------------- | :------------ | :----- |
| [15:0]  | Data for MSI Interrupts generated by this function. This base value is written to Root Port memory to signal an MSI interrupt. When one MSI vector is allowed, this value is used directly. When 2 MSI vectors are allowed, the upper 15 bits are used. And, the least significant bit indicates the interrupt number. When 4 MSI vectors are allowed, the lower 2 bits indicate the interrupt number, and so on. | 0             | RW     |
| [31:16] | Reserved                                                     | 0             | RO     |

- MSI Mask Register

| Bits | Register Description                                         | Default Value   | Access |
| :--- | :----------------------------------------------------------- | :-------------- | :----- |
| 31:0 | Mask bits for MSI interrupts. The number of implemented bits depends on the number of MSI vectors configured. When one MSI vectors is used , only bit 0 is RW. The other bits read as zeros. When two MSI vectors are used, bits [1:0] are RW, and so on. A one in a bit position masks the corresponding MSI interrupt. | See description | 0      |

- Pending Bits for MSI Interrupts Register

| Bits | Register Description                                         | Default Value | Access |
| :--- | :----------------------------------------------------------- | :------------ | :----- |
| 31:0 | Pending bits for MSI interrupts. A 1 in a bit position indicated the corresponding MSI interrupt is pending in the core. The number of implemented bits depends on the number of MSI vectors configured. When 1 MSI vectors is used, only bit 0 is RW. The other bits read as zeros. When 2 MSI vectors are used, bits [1:0] are RW, and so on. | RO            | 0      |

##### MSIX

为了支持多个中断，MSI-X 的 Capability Structure 在 MSI 的基础上增加了 table，其中 Table Offset 和 BIR(BAR Indicator Registor) 定义了 table 所在的位置，即指定使用哪个 BAR 寄存器（PCI 配置空间有 6 个 BAR 和 1 个 XROMBAR），然后从指定的这个 BAR 寄存器中取出 table 映射在 CPU 地址空间的基址，加上 Table Offset 就定位了 entry 的位置。类似的，`PBA BIR` 和 `PBA offset` 分别说明 MSIX- PBA 在哪个 BAR 中，在 BAR 中的什么位置。

![MSIX-capability.png](https://github.com/UtopianFuture/UtopianFuture.github.io/blob/master/image/MSIX-capability.png?raw=true)

MSI-X Table 中的 vector control 表示 PCIe 设备是否能够使用该 Entry 提交中断请求，类似 MSI 的 mask 位。

当外设准备发送中断信息时，其从 Capability Structure 中提取相关信息，信息地址取自 Message Address，其中 bits 20 - 31 是一个固定值 `0x0FEEH`。PCI 总线根据信息地址得知这是一个中断信息，会将其发送给 PCI-HOST 桥，**PCI-HOST 桥将其发送到目的 CPU（LAPIC）**，信息体取自 message data，主要部分是中断向量。

#### 硬件虚拟化支持

在基于软件虚拟中断芯片中，只能在 VM entry 时向 guest 注入中断，必须触发一次 VM exit，这是中断虚拟化的主要开销。

（1）`virtual-APIC page`（**解决读 LAPIC 时需要 VM exit 的问题**）

LAPIC 中有一个 4KB 大小的页面，intel 称之为 APIC page，LAPIC 的所有寄存器都存在这个页面上。当内核访问这些寄存器时，将触发 guest 退出到 host 的 KVM 模块中的虚拟 LAPIC。intel 在 guest 模式下实现了一个用于存储中断寄存器的 `virtual-APIC page`。配置之后的中断逻辑处理，很多中断就无需 vmm 介入。但发送 IPI 还是需要触发 VM exit。通过 `virtual-APIC page` 维护寄存器的状态，guest 读取这些寄存器时无需切换状态，而**写入时需要切换状态**。

（2）guest 模式下的中断评估逻辑（**解决 guest 无法及时响应中断和 guest 模式下中断评估的问题**）

在前文介绍的中断虚拟化过程中，guestos 只有在 VM entry 时才能进行中断注入，处理中断。但如果在 VM entry 时 guestos 是关中断的，或者正在执行不能被中断的指令，那么这时 guestos 无法处理中断。而又不能让中断处理延时过大，所以，一旦 guest 能够中断，CPU 应该马上从 guest 模式退出到 host 模式，这样就能在下一次 VM entry 时注入中断，为此， VMX 提供了一种特性: Interrupt-window exiting。

这个特性表示在任何指令执行前，如果 REFLAGS 寄存器的 IF 位置位，即 guestos 能够处理中断了，那么如果 Interrupt-window exiting 被设置为 1，则一旦有中断 pending，那么 guest 模式下的 CPU 要触发 VM exit，这个触发时机与物理 CPU 处理中断时类似的。如果 guestos 这时不能处理中断，那么需要设置 Interrupt-window exiting，告知 CPU 有中断在等待处理。

guest 模式下的 CPU 借助 VMCS 中的字段 `guest interrupt status` 评估中断。当 guest 开中断或者执行完不能中断的指令后，**CPU 会检查这个字段是否有中断需要处理**。（这个检查的过程是谁规定的？**guest 模式下的 CPU 自动检查 VMCS**）。当 guest 模式下支持中断评估后，guest 模式的 CPU 就不仅仅在 VM entry 时才能进行中断评估，其在 guest 模式下也能评估中断，一旦识别出中断，在 guest 模式即可自动完成中断注入，无须触发 VM exit。

（3）posted-interrupt processing（**解决无须 VM exit 进行中断注入的问题**）

当 CPU 支持在 guest 模式下的中断评估逻辑后，虚拟中断芯片可以在收到中断请求后，由 guest 模式下的中断评估逻辑评估后，将中断信息更新到 posted-interrupt descriptor 中，然后向处于 guest 模式下的 CPU 发送 posted-interrupt notification，向 guest 模式下的 CPU 直接递交中断。这个通知就是 IPI，目的 CPU 在接收到这个 IPI 后，将不再触发 VM exit，而是去处理被虚拟中断芯片写在 posted-interrupt notification 中的中断。

#### 几种中断类型

- SCI: System Control Interrupt, A system interrupt used by hardware to notify the OS of ACPI events. The SCI is an active, low, shareable, level interrupt.
- SMI: System Management Interrupt, An OS-transparent interrupt generated by interrupt events on legacy systems.
- NMI: Non-maskable Interrupt
- Normal Interrupt: handled through IDT with vector 0~255.

[详情](https://stackoverflow.com/questions/40583848/differences-among-various-interrupts-sci-smi-nmi-and-normal-interrupt)。

#### IRQ号，中断向量和GSI

- **IRQ 号是 PIC 时代引入的概念**，由于 ISA 设备通常是直接连接到到固定的引脚，所以对于 IRQ 号描述了设备连接到了 PIC 的哪个引脚上，同 IRQ 号直接和中断优先级相关，例如 IRQ0 比 IRQ3 的中断优先级更高。
- **GSI 号是 ACPI 引入的概念**，全称是 Global System Interrupt，用于为系统中每个中断源指定一个唯一的中断编号。注：ACPI Spec 规定 PIC 的 IRQ 号必须对应到 GSI0 - GSI15 上。kvm 默认支持最大 1024 个 GSI。
- 中断向量是针对逻辑 CPU 的概念，用来表示中断在 IDT 表的索引号，每个 IRQ（或者 GSI）最后都会被定向到某个 Vecotor 上。对于 PIC 上的中断，中断向量 = 32(start vector) + IRQ 号。在 IOAPIC 上的中断被分配的中断向量则是由操作系统分配。

### 中断模拟

注意，这里讨论的中断模拟是 QEMU + KVM 的方式，不是 tcg 的方式，关于所有 tcg 模拟之后有机会再分析。

中断模拟中设备创建、初始化到不是最难的，最复杂的应该是中断路由。

与中断有关的函数，包括创建中断设备，发起中断，中断注入等等。

```c
KVM 端：
| -- kvm_vm_compat_ioctl()
	| -- kvm_vm_ioctl()
		| -- kvm_arch_vm_ioctl() // PIC 创建
			| -- kvm_pic_init() // 创建 3 个 PIC 设备
			| -- kvm_ioapic_init() // 创建 ioapic 设备
			| -- kvm_setup_default_irq_routing() // 创建路由
 				| -- kvm_set_irq_routing() // 初始化 kvm_irq_routing_table -> guestos 用的路由表
    									   // kvm_kernel_irq_routing_entry -> hostos kernel 用的路由表项
    									   // kvm_irq_routing_entry -> 默认路由表项
    				| -- setup_routing_entry() // 将默认路由转换成内核中的路由信息，
    										   // 将 guestos 路由表项 mapping 到 hostos 表项中

QEMU 端：
| -- pc_init1()
    | -- pc_gsi_create() // PIC中断向量的初始化，初始化完后PIC设备就能分发中断 (1)
    	| -- qemu_allocate_irqs() // 24 个中断向量
    		| -- qemu_entend_irqs()
```

#### KVM中断注入

在 KVM 模拟虚拟 CPU 的数据结构中有字段 `VM-entry interruption-information field` 即用来设定虚拟机的中断信息。**物理机产生的中断要注入到这个字段中，虚拟机的虚拟中断才能处理**。

![VM-entry-inter info field.png](https://github.com/UtopianFuture/UtopianFuture.github.io/blob/master/image/VM-entry-inter%20info%20field.png?raw=true)

中断注入的大致流程如下：

```c
| -- vcpu_enter_guest
	| -- inject_pending_event
		| -- kvm_inject_exception
		| -- vmx_inject_irq
			| -- vmx_queue_exception
				| -- vmcs_write32
```

在进入 VMX non-root 之前，KVM 会调用 `vcpu_enter_guest` -> `inject_pending_event` 检查并处理其中的 pending request。这里不单单处理设备中断，还有 smi 和 nmi 中断的注入，在注入之前还要检查之前是否有异常需要注入。

这里有一个问题，pending 和 injected 分别代表什么？

```c
static int inject_pending_event(struct kvm_vcpu *vcpu, bool *req_immediate_exit)
{
	int r;
	bool can_inject = true;

	/* try to reinject previous events if any */
	// 从原本的注释中就知道，这里处理上一次 vcpu_enter_guest 没有处理的 exception
	if (vcpu->arch.exception.injected) { // 检查是否有异常需要注入
		kvm_inject_exception(vcpu);
		can_inject = false;
	}
	/*
	 * Do not inject an NMI or interrupt if there is a pending
	 * exception.  Exceptions and interrupts are recognized at
	 * instruction boundaries, i.e. the start of an instruction.
	 * Trap-like exceptions, e.g. #DB, have higher priority than
	 * NMIs and interrupts, i.e. traps are recognized before an
	 * NMI/interrupt that's pending on the same instruction.
	 * Fault-like exceptions, e.g. #GP and #PF, are the lowest
	 * priority, but are only generated (pended) during instruction
	 * execution, i.e. a pending fault-like exception means the
	 * fault occurred on the *previous* instruction and must be
	 * serviced prior to recognizing any new events in order to
	 * fully complete the previous instruction.
	 */
    // 翻译一下就是 exception 的优先级较高，要先注册，没有 exception 了才注册 interrupt
	else if (!vcpu->arch.exception.pending) { // 没有异常检查 nmi
		if (vcpu->arch.nmi_injected) {
			static_call(kvm_x86_set_nmi)(vcpu);
			can_inject = false;
		} else if (vcpu->arch.interrupt.injected) { // 为啥这里就 set_irq 了，
			static_call(kvm_x86_set_irq)(vcpu);		// 应该是 nmi 是另一种类型的中断，不用 set_irq
			can_inject = false;
		}
	}

    ...

	/* try to inject new event if pending */
	if (vcpu->arch.exception.pending) { // 注入异常事件
		trace_kvm_inj_exception(vcpu->arch.exception.nr,
					vcpu->arch.exception.has_error_code,
					vcpu->arch.exception.error_code);

		vcpu->arch.exception.pending = false; // 上面那个问题这里就有很好的解释。先有 pending 的 exception，
		vcpu->arch.exception.injected = true; // 然后 inject，这时的状态就变成 injected

		...

		kvm_inject_exception(vcpu); // 进行异常注入，之后再分析
		can_inject = false;
	}

    ...

	/*
	 * Finally, inject interrupt events.  If an event cannot be injected
	 * due to architectural conditions (e.g. IF=0) a window-open exit
	 * will re-request KVM_REQ_EVENT.  Sometimes however an event is pending
	 * and can architecturally be injected, but we cannot do it right now:
	 * an interrupt could have arrived just now and we have to inject it
	 * as a vmexit, or there could already an event in the queue, which is
	 * indicated by can_inject.  In that case we request an immediate exit
	 * in order to make progress and get back here for another iteration.
	 * The kvm_x86_ops hooks communicate this by returning -EBUSY.
	 */
    // 符合逻辑，exception 的优先级高于 interrupt
    // 一种种中断检查
	if (vcpu->arch.smi_pending) { // 为什么前面没有检查
		r = can_inject ? static_call(kvm_x86_smi_allowed)(vcpu, true) : -EBUSY; // 回调函数是 vmx_smi_allowed
		if (r < 0)
			goto out;
		if (r) {
			vcpu->arch.smi_pending = false;
			++vcpu->arch.smi_count;
			enter_smm(vcpu); // 进入 smm，它是 x86 cpu 的一种运行模式，用于处理 power，hardware 等问题，权限很高
			can_inject = false;
		} else
			static_call(kvm_x86_enable_smi_window)(vcpu); // 回调函数是 vmx_enable_smi_window
	}

	if (vcpu->arch.nmi_pending) {
		r = can_inject ? static_call(kvm_x86_nmi_allowed)(vcpu, true) : -EBUSY;
		if (r < 0)
			goto out;
		if (r) {
			--vcpu->arch.nmi_pending;
			vcpu->arch.nmi_injected = true;
			static_call(kvm_x86_set_nmi)(vcpu); // vmx_inject_nmi
			can_inject = false;
			WARN_ON(static_call(kvm_x86_nmi_allowed)(vcpu, true) < 0);
		}
		if (vcpu->arch.nmi_pending)
			static_call(kvm_x86_enable_nmi_window)(vcpu);
	}

	if (kvm_cpu_has_injectable_intr(vcpu)) { // 判断是否有中断需要注入
		r = can_inject ? static_call(kvm_x86_interrupt_allowed)(vcpu, true) : -EBUSY; // 是否允许注入
		if (r < 0)
			goto out;
		if (r) {
            // kvm_cpu_get_interrupt() 获取中断向量
            // 将中断向量写入 arch.interrupt.nr，见下面的 kvm_queue_interrupt() 函数
			kvm_queue_interrupt(vcpu, kvm_cpu_get_interrupt(vcpu), false);
			static_call(kvm_x86_set_irq)(vcpu); // vmx_inject_irq
			WARN_ON(static_call(kvm_x86_interrupt_allowed)(vcpu, true) < 0);
		}
		if (kvm_cpu_has_injectable_intr(vcpu))
			static_call(kvm_x86_enable_irq_window)(vcpu);
	}

    ...
}

```

问题：如果有多个异常或中断需要注入怎么办？

将中断向量写入 `arch.interrupt.nr`

```c
static inline void kvm_queue_interrupt(struct kvm_vcpu *vcpu, u8 vector,
	bool soft)
{
	vcpu->arch.interrupt.injected = true;
	vcpu->arch.interrupt.soft = soft;
	vcpu->arch.interrupt.nr = vector;
}
```

然后通过调用 `vmx_inject_irq` 进行写入

```c
static void vmx_inject_irq(struct kvm_vcpu *vcpu)
{
	struct vcpu_vmx *vmx = to_vmx(vcpu);
	uint32_t intr;
	int irq = vcpu->arch.interrupt.nr; // 上面写入的

	trace_kvm_inj_virq(irq);

	++vcpu->stat.irq_injections;
	if (vmx->rmode.vm86_active) {
		int inc_eip = 0;
		if (vcpu->arch.interrupt.soft)
			inc_eip = vcpu->arch.event_exit_inst_len;
		kvm_inject_realmode_interrupt(vcpu, irq, inc_eip); // 实模式下的写入，有趣
		return;
	}
	intr = irq | INTR_INFO_VALID_MASK;
	if (vcpu->arch.interrupt.soft) {
		intr |= INTR_TYPE_SOFT_INTR;
		vmcs_write32(VM_ENTRY_INSTRUCTION_LEN,
			     vmx->vcpu.arch.event_exit_inst_len);
	} else
		intr |= INTR_TYPE_EXT_INTR;
	vmcs_write32(VM_ENTRY_INTR_INFO_FIELD, intr); // 写入 vmcs

	vmx_clear_hlt(vcpu);
}
```

上面是 interrupt 的注入，exception 的注入也类似。

```c
static void vmx_queue_exception(struct kvm_vcpu *vcpu)
{
	...

	kvm_deliver_exception_payload(vcpu);

	if (has_error_code) {
		vmcs_write32(VM_ENTRY_EXCEPTION_ERROR_CODE, error_code);
		intr_info |= INTR_INFO_DELIVER_CODE_MASK;
	}

	if (vmx->rmode.vm86_active) {
		int inc_eip = 0;
		if (kvm_exception_is_soft(nr))
			inc_eip = vcpu->arch.event_exit_inst_len;
		kvm_inject_realmode_interrupt(vcpu, nr, inc_eip);
		return;
	}

	...

	vmcs_write32(VM_ENTRY_INTR_INFO_FIELD, intr_info); // 关键还是在这里写入

	vmx_clear_hlt(vcpu);
}
```

这部分还需要进一步分析。

#### PIC中断模拟

##### KVM中PIC的创建

和 CPU 一样，终端设备的模拟也分 KVM 端和 QEMU 端。QEMU 端在 `kvm_init` 中通过 ioctl 向 vmfd （这个 fd 在前面介绍过，KVM 中表示一个虚拟机）发起创建 irqchip 的请求，KVM 进行处理。

```plain
#0  kvm_init (ms=0x55555699a400) at ../accel/kvm/kvm-all.c:2307
#1  0x0000555555af387b in accel_init_machine (accel=0x55555681c140, ms=0x55555699a400) at ../accel/accel-softmmu.c:39
#2  0x0000555555c0752b in do_configure_accelerator (opaque=0x7fffffffd9dd, opts=0x555556a448e0, errp=0x55555677c100 <error_fatal>) at ../softmmu/vl.c:2348
#3  0x0000555555f01307 in qemu_opts_foreach (list=0x5555566a18c0 <qemu_accel_opts>, func=0x555555c07401 <do_configure_accelerator>, opaque=0x7fffffffd9dd, errp=0x55555677c100 <error_fatal>)
    at ../util/qemu-option.c:1135
#4  0x0000555555c07790 in configure_accelerators (progname=0x7fffffffe098 "/home/guanshun/gitlab/qemu-newest/build/x86_64-softmmu/qemu-system-x86_64") at ../softmmu/vl.c:2414
#5  0x0000555555c0a834 in qemu_init (argc=13, argv=0x7fffffffdc98, envp=0x7fffffffdd08) at ../softmmu/vl.c:3724
#6  0x000055555583b6f5 in main (argc=13, argv=0x7fffffffdc98, envp=0x7fffffffdd08) at ../softmmu/main.c:49
```

```c
static int kvm_init(MachineState *ms)
{
    MachineClass *mc = MACHINE_GET_CLASS(ms);

    ...

    do {
        ret = kvm_ioctl(s, KVM_CREATE_VM, type);
    } while (ret == -EINTR);

    if (s->kernel_irqchip_allowed) {
        kvm_irqchip_create(s); // 创建 irqchip
    }

	...

    return ret;
}
```

这里也有个问题，用 KVM 的话会在 KVM 中创建虚拟的 PIC 或 APIC 设备进行模拟，硬件中断将发送到这些虚拟的中断设备，然后再注入到虚拟机中，那么不用 KVM 用 tcg 的话是怎样的过程？

```c
static void kvm_irqchip_create(KVMState *s)
{
    int ret;

    ...

    /* First probe and see if there's a arch-specific hook to create the
     * in-kernel irqchip for us */
    ret = kvm_arch_irqchip_create(s);
    if (ret == 0) {
        if (s->kernel_irqchip_split == ON_OFF_AUTO_ON) {
            perror("Split IRQ chip mode not supported.");
            exit(1);
        } else {
            ret = kvm_vm_ioctl(s, KVM_CREATE_IRQCHIP);
        }
    }

    ...

    kvm_init_irq_routing(s);

    s->gsimap = g_hash_table_new(g_direct_hash, g_direct_equal);
}
```

然后 KVM 对 `ioctl` 请求进行处理。

```c
long kvm_arch_vm_ioctl(struct file *filp,
		       unsigned int ioctl, unsigned long arg)
{
	...

	switch (ioctl) {
	...
	case KVM_CREATE_IRQCHIP: {

		...

		r = kvm_pic_init(kvm);
		r = kvm_ioapic_init(kvm);
		r = kvm_setup_default_irq_routing(kvm);

		/* Write kvm->irq_routing before enabling irqchip_in_kernel. */
		smp_wmb();
		kvm->arch.irqchip_mode = KVM_IRQCHIP_KERNEL;
		break;
	}
	...
}
```

`KVM_CREATE_IRQCHIP` 主要调用 `kvm_pic_init` 创建 PIC 设备，`kvm_ioapic_init` 创建 IOAPIC 设备，它在下一节再分析，`kvm_setup_default_irq_routing` 初始化中断路由表。

```c
int kvm_pic_init(struct kvm *kvm)
{
	struct kvm_pic *s;
	int ret;

	s = kzalloc(sizeof(struct kvm_pic), GFP_KERNEL_ACCOUNT);
	if (!s)
		return -ENOMEM;
	spin_lock_init(&s->lock);
	s->kvm = kvm;
	s->pics[0].elcr_mask = 0xf8;
	s->pics[1].elcr_mask = 0xde;
	s->pics[0].pics_state = s;
	s->pics[1].pics_state = s;

	/*
	 * Initialize PIO device
	 */
	kvm_iodevice_init(&s->dev_master, &picdev_master_ops);
	kvm_iodevice_init(&s->dev_slave, &picdev_slave_ops);
	kvm_iodevice_init(&s->dev_elcr, &picdev_elcr_ops);
	mutex_lock(&kvm->slots_lock);
    // 在 kvm_pic 中创建 3 个设备，并注册端口，
    // 这个端口就是用来访问 pic 的么
	ret = kvm_io_bus_register_dev(kvm, KVM_PIO_BUS, 0x20, 2, &s->dev_master);
	ret = kvm_io_bus_register_dev(kvm, KVM_PIO_BUS, 0xa0, 2, &s->dev_slave);
	ret = kvm_io_bus_register_dev(kvm, KVM_PIO_BUS, 0x4d0, 2, &s->dev_elcr);

	mutex_unlock(&kvm->slots_lock);

	kvm->arch.vpic = s; // 创建好了

	...

	return ret;
}
```

这里比较复杂的是初始化中断路由表。`default_routing` 是默认路由信息。中断路由到底是个什么东西，为什么需要中断路由，没有完全搞懂，还需要进一步分析。

**中断路由其实就是通过中断号找到对应的处理函数**。

```c
// gsi 表示该中断在系统全局范围的中断号，type 用来决定中断的种类
// KVM_IRQ_ROUTING_IRQCHIP，表示 u.irqchip 有效
#define IOAPIC_ROUTING_ENTRY(irq) \
	{ .gsi = irq, .type = KVM_IRQ_ROUTING_IRQCHIP,	\
	  .u.irqchip = { .irqchip = KVM_IRQCHIP_IOAPIC, .pin = (irq) } }
#define ROUTING_ENTRY1(irq) IOAPIC_ROUTING_ENTRY(irq)

#define PIC_ROUTING_ENTRY(irq) \
	{ .gsi = irq, .type = KVM_IRQ_ROUTING_IRQCHIP,	\
	  .u.irqchip = { .irqchip = SELECT_PIC(irq), .pin = (irq) % 8 } }
#define ROUTING_ENTRY2(irq) \
	IOAPIC_ROUTING_ENTRY(irq), PIC_ROUTING_ENTRY(irq)

static const struct kvm_irq_routing_entry default_routing[] = {
	ROUTING_ENTRY2(0), ROUTING_ENTRY2(1), // apic 和 pic 前 16 个有重叠，
	ROUTING_ENTRY2(2), ROUTING_ENTRY2(3), // 所以初始化 2 个 entry
	ROUTING_ENTRY2(4), ROUTING_ENTRY2(5),
	ROUTING_ENTRY2(6), ROUTING_ENTRY2(7),
	ROUTING_ENTRY2(8), ROUTING_ENTRY2(9),
	ROUTING_ENTRY2(10), ROUTING_ENTRY2(11),
	ROUTING_ENTRY2(12), ROUTING_ENTRY2(13),
	ROUTING_ENTRY2(14), ROUTING_ENTRY2(15),
	ROUTING_ENTRY1(16), ROUTING_ENTRY1(17), // 剩下的 8 个 gsi 都是 apic 使用
	ROUTING_ENTRY1(18), ROUTING_ENTRY1(19),
	ROUTING_ENTRY1(20), ROUTING_ENTRY1(21),
	ROUTING_ENTRY1(22), ROUTING_ENTRY1(23),
};

int kvm_setup_default_irq_routing(struct kvm *kvm)
{
	return kvm_set_irq_routing(kvm, default_routing,
				   ARRAY_SIZE(default_routing), 0);
}
```

`kvm_set_irq_routing` 将 `kvm_irq_routing_entry` (`default_routing`) 转化成 `kvm_kernel_irq_routing_entry` ，后者用于在内核中记录中断信息，除了基本的中断号 `gsi` ，中断类型信息 `type` ，还有用于处理中断的回调函数 `set` 。

```c
struct kvm_kernel_irq_routing_entry {
	u32 gsi;
	u32 type;
	int (*set)(struct kvm_kernel_irq_routing_entry *e,
		   struct kvm *kvm, int irq_source_id, int level,
		   bool line_status);
	union {
		struct {
			unsigned irqchip;
			unsigned pin;
		} irqchip;
		...
	};
	struct hlist_node link;
};
```

```c
#ifdef CONFIG_HAVE_KVM_IRQ_ROUTING
struct kvm_irq_routing_table { // 和 default_routing 的类型不一样，kvm_irq_routing_entry
	int chip[KVM_NR_IRQCHIPS][KVM_IRQCHIP_NUM_PINS]; // 每一项代表芯片引脚对应的全局中断号 gsi
	u32 nr_rt_entries;
	/*
	 * Array indexed by gsi. Each entry contains list of irq chips
	 * the gsi is connected to.
	 */
	struct hlist_head map[]; // 链接 gsi 对应的所有的 kvm_kernel_irq_routing_entry
};
#end
```

```c
int kvm_set_irq_routing(struct kvm *kvm,
			const struct kvm_irq_routing_entry *ue,
			unsigned nr,
			unsigned flags)
{
	struct kvm_irq_routing_table *new, *old; // 这个是虚拟机的中断路由表
	struct kvm_kernel_irq_routing_entry *e; // 内核中断路由信息
	u32 i, j, nr_rt_entries = 0;
	int r;

	for (i = 0; i < nr; ++i) {
		if (ue[i].gsi >= KVM_MAX_IRQ_ROUTES)
			return -EINVAL;
		nr_rt_entries = max(nr_rt_entries, ue[i].gsi);
	}

	nr_rt_entries += 1;

	new = kzalloc(struct_size(new, map, nr_rt_entries), GFP_KERNEL_ACCOUNT);
	if (!new)
		return -ENOMEM;

	new->nr_rt_entries = nr_rt_entries;
	for (i = 0; i < KVM_NR_IRQCHIPS; i++)
		for (j = 0; j < KVM_IRQCHIP_NUM_PINS; j++)
			new->chip[i][j] = -1; // chip[][]，第一维表示 3 个 pic 设备，第二维表示引脚数

	for (i = 0; i < nr; ++i) {

        ...

		r = setup_routing_entry(kvm, new, e, ue); // 建立中断路由信息
		++ue;
	}

	...

	return r;
}
```

`setup_routing_entry` 将 `kvm_irq_routing_entry` 中的 gsi 和 type 复制到 `kvm_kernel_irq_routing_entry` 中，并根据 `kvm_irq_routing_entry` 中的中断类型**设置回调函数**。那虚拟机的中断处理流程是怎样的呢，还没有完全搞懂。

当 QEMU 中的虚拟设备向虚拟中断控制器发起中断时，进入到内核后，会根据设备注册的 gsi，首先会在 `kvm_irq_routing_table` 中寻找映射信息，然后根据 map 中找到对应的 `kvm_kernel_irq_routing_entry` ，而 `kvm_kernel_irq_routing_entry` 中根据 gsi 注册的回调函数 `kvm_set_pic_irq` 和 `kvm_set_ioapic_irq` 发起中断。这个流程在下面的章节会进一步分析。

```c
static int setup_routing_entry(struct kvm *kvm,
			       struct kvm_irq_routing_table *rt, // 虚拟机中断路由信息
			       struct kvm_kernel_irq_routing_entry *e, // 内核路由信息
			       const struct kvm_irq_routing_entry *ue) // 默认路由信息
{
	struct kvm_kernel_irq_routing_entry *ei;
	int r;
	u32 gsi = array_index_nospec(ue->gsi, KVM_MAX_IRQ_ROUTES); // 获取默认路由的 gsi

	/*
	 * Do not allow GSI to be mapped to the same irqchip more than once.
	 * Allow only one to one mapping between GSI and non-irqchip routing.
	 */
    // 翻译过来就是一个 gsi 只能映射到不同的芯片引脚上
	hlist_for_each_entry(ei, &rt->map[gsi], link)
		if (ei->type != KVM_IRQ_ROUTING_IRQCHIP ||
		    ue->type != KVM_IRQ_ROUTING_IRQCHIP ||
		    ue->u.irqchip.irqchip == ei->irqchip.irqchip)
			return -EINVAL;

    // 建立映射
	e->gsi = gsi;
	e->type = ue->type;
	r = kvm_set_routing_entry(kvm, e, ue); // 设置回调函数
	if (e->type == KVM_IRQ_ROUTING_IRQCHIP)
		rt->chip[e->irqchip.irqchip][e->irqchip.pin] = e->gsi; // 虚拟机中每个引脚的中断号为内核中的中断号

	hlist_add_head(&e->link, &rt->map[e->gsi]); // 为什么是 e 加到 rt 中？
	// 也就是说每个虚拟机中断(gsi)都对应到内核路由表项
	return 0;
}
```

##### QEMU中PIC的初始化

QEMU 虚拟机的中断状态由 `GSIState` 表示（所有的 PIC 和 APIC 中断状态）。其中 `qemu_irq` 表示一个中断引脚，这个 `qemu_irq` 其实就是通过 `typedef` 定义的 `IRQState *` 。

```c
typedef struct GSIState {
    qemu_irq i8259_irq[ISA_NUM_IRQS];
    qemu_irq ioapic_irq[IOAPIC_NUM_PINS];
    qemu_irq ioapic2_irq[IOAPIC_NUM_PINS];
} GSIState;
```

我被这个结构的注册函数搞糊涂了。不知道设备使用的是哪个函数，该函数又是在哪里注册的？

```c
typedef struct IRQState *qemu_irq;
struct IRQState { // 表示一个中断引脚
    Object parent_obj;
    qemu_irq_handler handler; // 该中断的处理函数
    void *opaque;
    int n; // 引脚号
};
```

中断初始化都是在 `pc_init1` 中进行的，

```c
/* PC hardware initialisation */
static void pc_init1(MachineState *machine,
                     const char *host_type, const char *pci_type)
{
    GSIState *gsi_state;

    ...

    // gsi_state 是中断路由的起点，构建 GSIState
    gsi_state = pc_gsi_create(&x86ms->gsi, pcmc->pci_enabled);

    if (pcmc->pci_enabled) {
    	PIIX3State *piix3;

        pci_bus = i440fx_init(host_type,
                              pci_type,
                              &i440fx_state,
                              system_memory, system_io, machine->ram_size,
                              x86ms->below_4g_mem_size,
                              x86ms->above_4g_mem_size,
                              pci_memory, ram_memory);
        pcms->bus = pci_bus;

        piix3 = piix3_create(pci_bus, &isa_bus);
        piix3->pic = x86ms->gsi;
        piix3_devfn = piix3->dev.devfn;
    }
    isa_bus_irqs(isa_bus, x86ms->gsi);

    pc_i8259_create(isa_bus, gsi_state->i8259_irq); // 创建具体的设备

    ...
}
```

首先调用 `pc_gsi_create` 创建虚拟机的 `GSIState` ，之后再创建具体的外设。这里 `x86ms->gsi`就是 guestos 中断路由的起点。但是在该函数中，只是分配一个指向该 `GSIState` 的指针，然后所有的设备有一个指向 `GSIState` 的指针，而 `GSIState` 本身还没有赋值。

```c
GSIState *pc_gsi_create(qemu_irq **irqs, bool pci_enabled)
{
    GSIState *s;

    s = g_new0(GSIState, 1);
    if (kvm_ioapic_in_kernel()) {
        kvm_pc_setup_irq_routing(pci_enabled);
    }
    *irqs = qemu_allocate_irqs(gsi_handler, s, GSI_NUM_PINS);

    return s;
}
```

这里 `gsi_handler` 就是中断处理函数，所有的中断引脚 `IRQState * irq -> handler` 都是指向这个处理函数，它会根据中断号请求对应的中断。0~15 号是 `i8259_irq` 中断，16~23 是 `ioapic_irq` 中断，24 是 `ioapic2_irq` 中断。这里有个问题，`s->i8259_irq[n]` 为什么就会指向 `kvm_pic_set_irq` ？这里只是将处理函数统一设置为 `gsi_handler`，而**每个 irq 对应的处理函数要等到对应的中断控制器初始化时才会设置**。在之后设备发起中断时，会先进入到 `gsi_handler` 中处理，然后由 `gsi_handler` 分发到不同的中断控制器处理函数进行处理。这个过程在后面能看到。

`opaque` 则是一个全局中断处理器 `GSIState` 的指针。最重要的 `x86ms->gsi` 已经准备好了。

```c
void gsi_handler(void *opaque, int n, int level)
{
    GSIState *s = opaque;

    trace_x86_gsi_interrupt(n, level);
    switch (n) {
    case 0 ... ISA_NUM_IRQS - 1:
        if (s->i8259_irq[n]) {
            /* Under KVM, Kernel will forward to both PIC and IOAPIC */
            qemu_set_irq(s->i8259_irq[n], level);
        }
        /* fall through */
    case ISA_NUM_IRQS ... IOAPIC_NUM_PINS - 1:
        qemu_set_irq(s->ioapic_irq[n], level);
        break;
    case IO_APIC_SECONDARY_IRQBASE
        ... IO_APIC_SECONDARY_IRQBASE + IOAPIC_NUM_PINS - 1:
        qemu_set_irq(s->ioapic2_irq[n - IO_APIC_SECONDARY_IRQBASE], level);
        break;
    }
}
```

而 `qemu_allocate_irqs` 则是分配一组 `qemu_irq` 。这就是初始化 `x86ms->gsi` ，为所有的中断号分配 `handler` ，**而 `opaque` 则是指向的 `GSIState` 的指针。**

```c
qemu_irq *qemu_extend_irqs(qemu_irq *old, int n_old, qemu_irq_handler handler,
                           void *opaque, int n)
{
    qemu_irq *s;
    int i;

    if (!old) {
        n_old = 0;
    }
    s = old ? g_renew(qemu_irq, old, n + n_old) : g_new(qemu_irq, n);
    for (i = n_old; i < n + n_old; i++) {
        s[i] = qemu_allocate_irq(handler, opaque, i);
    }
    return s;
}

qemu_irq *qemu_allocate_irqs(qemu_irq_handler handler, void *opaque, int n)
{
    return qemu_extend_irqs(NULL, 0, handler, opaque, n);
}

qemu_irq qemu_allocate_irq(qemu_irq_handler handler, void *opaque, int n)
{
    struct IRQState *irq;

    irq = IRQ(object_new(TYPE_IRQ));
    irq->handler = handler;
    irq->opaque = opaque;
    irq->n = n;

    return irq;
}
```

初始化之后的中断处理是这样的。之后创建了不同的设备之后，如后面的 `pc_i8259_create` 创建 pic 设备，中断就会由 pic 设备发起。

![GSIState.png](https://github.com/UtopianFuture/UtopianFuture.github.io/blob/master/image/GSIState.png?raw=true)

`x86ms->gsi` 就是 guestos 中断路由的起点（所谓起点没有特殊的含义，只是 `GSIState *` 是保存所有中断引脚的结构体，然后将所有的中断通过 `gsi_handler` 分发一次，便于理解和管理吧！），在调用 `pc_gsi_create` 初始化之后，`x86ms->gsi` 会被赋值给南桥 piix3 的 PIC 成员，PCI 设备的中断会从这里开始分发。

```c
gsi_state = pc_gsi_create(&x86ms->gsi, pcmc->pci_enabled);
```

南桥会创建一条 isa 总线 `isa_bus`，并调用 `isa_bus_irqs` 将 `x86ms->gsi` 赋值给 isabus 的 irq 成员。

```c
if (pcmc->pci_enabled) {
        PIIX3State *piix3;

        pci_bus = i440fx_init(host_type,
                              pci_type,
                              &i440fx_state,
                              system_memory, system_io, machine->ram_size,
                              x86ms->below_4g_mem_size,
                              x86ms->above_4g_mem_size,
                              pci_memory, ram_memory);
        pcms->bus = pci_bus;

        piix3 = piix3_create(pci_bus, &isa_bus);
        piix3->pic = x86ms->gsi;
        piix3_devfn = piix3->dev.devfn;
    } else {
        pci_bus = NULL;
        i440fx_state = NULL;
        isa_bus = isa_bus_new(NULL, get_system_memory(), system_io,
                              &error_abort);
        pcms->hpet_enabled = false;
    }
    isa_bus_irqs(isa_bus, x86ms->gsi);
	// 在这里将初始化的 pic 装入 gsi_state，所以中断是先发送到 gsi_state 的 gsi_handler
	// 然后再由它分发给注册到 gsi_state 中的不同的中断控制器中
	pc_i8259_create(isa_bus, gsi_state->i8259_irq);
```

`pc_i8259_create` 除了调用 `kvm_i8259_init` 创建 pic 设备，还会指定 pic 设备的中断回调函数 `kvm_pic_set_irq` 。之后发生 pic 设备中断就由 `kvm_pic_set_irq` 进行处理，这个之后会分析。

`pc_i8259_create` 在创建 pic 设备的时候会根据是否使用 kvm 模拟来选择不同的初始化函数。

```c
void pc_i8259_create(ISABus *isa_bus, qemu_irq *i8259_irqs)
{
    qemu_irq *i8259;

    if (kvm_pic_in_kernel()) {
        i8259 = kvm_i8259_init(isa_bus); // 使用 kvm 模拟
    } else if (xen_enabled()) {
        // xen 是我理解的 Xen 么，怎么哪都有它
        // 是的，因为 Xen 是半虚拟化，需要修改 guest 系统
        i8259 = xen_interrupt_controller_init();
    } else { // 使用 tcg 模拟，这个有机会还是要看看的
        i8259 = i8259_init(isa_bus, x86_allocate_cpu_irq());
    }

    for (size_t i = 0; i < ISA_NUM_IRQS; i++) {
        i8259_irqs[i] = i8259[i];
    }

    g_free(i8259);
}
```

对于 kvm 模拟，使用的是 `kvm_i8259_init` ，并指定控制器的回调函数为 `kvm_pic_set_irq` 。

```c
qemu_irq *kvm_i8259_init(ISABus *bus)
{
    // 挂载？
    i8259_init_chip(TYPE_KVM_I8259, bus, true);
    i8259_init_chip(TYPE_KVM_I8259, bus, false);

    // pic 的回调函数，gsi_handler 最后会调用到这里，圆满！
    return qemu_allocate_irqs(kvm_pic_set_irq, NULL, ISA_NUM_IRQS);
}
```

**它会通过 `kvm_pic_set_irq` -> `kvm_set_irq` -> `kvm_vm_ioctl(s, s->irq_set_ioctl, &event)` 向 kvm 发起系统调用**。

而对于用 tcg 模拟，使用的是 `i8259_init` 。

```c
qemu_irq *i8259_init(ISABus *bus, qemu_irq parent_irq)
{
    qemu_irq *irq_set;
    DeviceState *dev;
    ISADevice *isadev;
    int i;

    irq_set = g_new0(qemu_irq, ISA_NUM_IRQS);

    isadev = i8259_init_chip(TYPE_I8259, bus, true);
    dev = DEVICE(isadev);

    qdev_connect_gpio_out(dev, 0, parent_irq);
    for (i = 0 ; i < 8; i++) {
        irq_set[i] = qdev_get_gpio_in(dev, i);
    }

    isa_pic = dev;

    isadev = i8259_init_chip(TYPE_I8259, bus, false);
    dev = DEVICE(isadev);

    qdev_connect_gpio_out(dev, 0, irq_set[2]);
    for (i = 0 ; i < 8; i++) {
        irq_set[i + 8] = qdev_get_gpio_in(dev, i);
    }

    slave_pic = PIC_COMMON(dev);

    return irq_set;
}
```

初始化的逻辑是类似的，但是回调函数不同，tcg 的是 `pic_irq_request` ，KVM 是 `kvm_pic_set_irq`，我们先看看 tcg 设备发起中断的详细过程：

```
#0  pic_irq_request (opaque=0x0, irq=0, level=0) at ../hw/i386/x86.c:530
#1  0x0000555555ce3a77 in qemu_set_irq (irq=0x555556b045a0, level=0) at ../hw/core/irq.c:45
#2  0x00005555558bdc07 in qemu_irq_lower (irq=0x555556b045a0) at /home/guanshun/gitlab/qemu/include/hw/irq.h:17
#3  0x00005555558be508 in pic_update_irq (s=0x555556ae5800) at ../hw/intc/i8259.c:116
#4  0x00005555558be6ad in pic_set_irq (opaque=0x555556ae5800, irq=1, level=0) at ../hw/intc/i8259.c:156
#5  0x0000555555ce3a77 in qemu_set_irq (irq=0x555556b67680, level=0) at ../hw/core/irq.c:45
// 看，发起中断先进入 gsi_handler 处理，然后转发到对应的中断控制器注册的处理函数。
#6  0x0000555555b01fb5 in gsi_handler (opaque=0x555556b2d970, n=1, level=0) at ../hw/i386/x86.c:596
#7  0x0000555555ce3a77 in qemu_set_irq (irq=0x555556aec400, level=0) at ../hw/core/irq.c:45
#8  0x0000555555a8c544 in kbd_update_irq (s=0x5555576825d8) at ../hw/input/pckbd.c:177
#9  0x0000555555a8c5b3 in kbd_update_kbd_irq (opaque=0x5555576825d8, level=0) at ../hw/input/pckbd.c:189
#10 0x00005555558ebfcf in ps2_common_reset (s=0x55555774d200) at ../hw/input/ps2.c:916
#11 0x00005555558ec134 in ps2_kbd_reset (opaque=0x55555774d200) at ../hw/input/ps2.c:953
#12 0x0000555555ce1d42 in qemu_devices_reset () at ../hw/core/reset.c:69
#13 0x0000555555b07d9e in pc_machine_reset (machine=0x55555691a400) at ../hw/i386/pc.c:1643
#14 0x0000555555bee3d4 in qemu_system_reset (reason=SHUTDOWN_CAUSE_NONE) at ../softmmu/runstate.c:442
#15 0x000055555589f839 in qdev_machine_creation_done () at ../hw/core/machine.c:1292
#16 0x0000555555be81ed in qemu_machine_creation_done () at ../softmmu/vl.c:2613
#17 0x0000555555be82c0 in qmp_x_exit_preconfig (errp=0x5555566ce528 <error_fatal>) at ../softmmu/vl.c:2636
#18 0x0000555555bea989 in qemu_init (argc=5, argv=0x7fffffffe398, envp=0x7fffffffe3c8) at ../softmmu/vl.c:3669
#19 0x000055555581fe85 in main (argc=5, argv=0x7fffffffe398, envp=0x7fffffffe3c8) at ../softmmu/main.c:49
```

和开始给出的 kvm 调用过程类似，keyboard 设备还是通过 `kbd_update_kbd_irq` 发起中断。现在分析 `pic_irq_request` 函数是怎样向 guestos 注入中断的。

```c
/* IRQ handling */
static void pic_irq_request(void *opaque, int irq, int level)
{
    CPUState *cs = first_cpu;
    X86CPU *cpu = X86_CPU(cs);

    ...

    else {
        if (level) {
            cpu_interrupt(cs, CPU_INTERRUPT_HARD);
        } else {
            cpu_reset_interrupt(cs, CPU_INTERRUPT_HARD);
        }
    }
}
```

```c
void cpu_interrupt(CPUState *cpu, int mask)
{
    if (cpus_accel->handle_interrupt) {
        cpus_accel->handle_interrupt(cpu, mask);
    } else {
        generic_handle_interrupt(cpu, mask);
    }
}
```

这里 `handle_interrupt` 回调函数指的是 `tcg_handle_interrupt` ，也就是说，用 tcg 模拟时所有的中断都是由 `tcg_handle_interrupt` 最后处理。

```c
void tcg_handle_interrupt(CPUState *cpu, int mask)
{
    g_assert(qemu_mutex_iothread_locked());

    cpu->interrupt_request |= mask;

    /*
     * If called from iothread context, wake the target cpu in
     * case its halted.
     */
    if (!qemu_cpu_is_self(cpu)) {
        qemu_cpu_kick(cpu);
    } else {
        qatomic_set(&cpu_neg(cpu)->icount_decr.u16.high, -1);
    }
}
```

最后就是 `qemu_cpu_kick` 通知 CPU 进行处理。

再看看 KVM pic 设备发起中断的具体过程：

```
// 前面设置的处理函数
#0  kvm_pic_set_irq (opaque=0x0, irq=4, level=0) at ../hw/i386/kvm/i8259.c:118
#1  0x0000555555d4018c in qemu_set_irq (irq=0x555557005fb0, level=0) at ../hw/core/irq.c:45
// 看，发起中断先进入 gsi_handler 处理，然后转发到对应的中断控制器注册的处理函数。
#2  0x0000555555b39e8a in gsi_handler (opaque=0x555556bc52f0, n=4, level=0)
    at ../hw/i386/x86.c:599
#3  0x0000555555d4018c in qemu_set_irq (irq=0x555556bc5660, level=0) at ../hw/core/irq.c:45
#4  0x000055555593b0ed in qemu_irq_lower (irq=0x555556bc5660)
    at /home/guanshun/gitlab/qemu-newest/include/hw/irq.h:17
// serial 发起中断
#5  0x000055555593b65f in serial_update_irq (s=0x555556e804b0) at ../hw/char/serial.c:144
#6  0x000055555593cb3e in serial_receive1 (opaque=0x555556e804b0, buf=0x7fffffffcb80 "\r",
    size=1) at ../hw/char/serial.c:621
#7  0x0000555555e42c7a in mux_chr_read (opaque=0x5555569b8640, buf=0x7fffffffcb80 "\r",
    size=1) at ../chardev/char-mux.c:235
#8  0x0000555555e49fd6 in qemu_chr_be_write_impl (s=0x555556929f40, buf=0x7fffffffcb80 "\r",
    len=1) at ../chardev/char.c:201
#9  0x0000555555e4a03e in qemu_chr_be_write (s=0x555556929f40, buf=0x7fffffffcb80 "\r", len=1)
    at ../chardev/char.c:213
#10 0x0000555555e4cb34 in fd_chr_read (chan=0x555556a45b10, cond=G_IO_IN,
    opaque=0x555556929f40) at ../chardev/char-fd.c:73
#11 0x0000555555d4fb13 in qio_channel_fd_source_dispatch (source=0x555557533840,
    callback=0x555555e4c9fd <fd_chr_read>, user_data=0x555556929f40)
    at ../io/channel-watch.c:84
// 用 epoll 处理 host 发来的中断，即物理设备完成工作，发起中断，host 将其发送给 QEMU
// QEMU 再转发给对应的虚拟设备，虚拟设备再发起中断，由虚拟中断控制器再通过 ioctl 交由 KVM 处理
// KVM 进行中断注入
// 这样走一遍就知道设备直通能够大幅提升性能了吧
#12 0x00007ffff773a04e in g_main_context_dispatch ()
   from /lib/x86_64-linux-gnu/libglib-2.0.so.0
#13 0x0000555555f1dd0d in glib_pollfds_poll () at ../util/main-loop.c:232
#14 0x0000555555f1dd8b in os_host_main_loop_wait (timeout=1000000000)
    at ../util/main-loop.c:255
#15 0x0000555555f1de9c in main_loop_wait (nonblocking=0) at ../util/main-loop.c:531
#16 0x0000555555bf1c30 in qemu_main_loop () at ../softmmu/runstate.c:726
#17 0x000055555583b6fa in main (argc=15, argv=0x7fffffffde28, envp=0x7fffffffdea8)
    at ../softmmu/main.c:50
```

它会通过 `kvm_pic_set_irq` -> `kvm_set_irq` -> `kvm_vm_ioctl(s, s->irq_set_ioctl, &event)` 向 kvm 发起中断。而 KVM 注入中断的过程上面已经分析过了（完美闭环）。

##### 设备使用PIC中断

pic 设备使用 `isa_init_irq` 申请 irq 资源。每个设备都会传入一个 `isairq` 表示中断引脚号和自己的 `qemu_irq` ，根据 `isairq` 来获取 `isabus` 中对应的 `qemu_irq` ，共有 14 个设备使用 pic 中断。以键盘鼠标为例：

```c
static void i8042_realizefn(DeviceState *dev, Error **errp)
{
    ISADevice *isadev = ISA_DEVICE(dev);
    ISAKBDState *isa_s = I8042(dev);
    KBDState *s = &isa_s->kbd;

    isa_init_irq(isadev, &s->irq_kbd, 1);
    isa_init_irq(isadev, &s->irq_mouse, 12);

    isa_register_ioport(isadev, isa_s->io + 0, 0x60);
    isa_register_ioport(isadev, isa_s->io + 1, 0x64);

    s->kbd = ps2_kbd_init(kbd_update_kbd_irq, s); // 设置回调函数
    s->mouse = ps2_mouse_init(kbd_update_aux_irq, s);
    qemu_register_reset(kbd_reset, s);
}
```

每个设备都有指向中断引脚的指针，如  `&s->irq_kbd` 和 `&s->irq_mouse` 。

```c
/*
 * isa_get_irq() returns the corresponding qemu_irq entry for the i8259.
 *
 * This function is only for special cases such as the 'ferr', and
 * temporary use for normal devices until they are converted to qdev.
 */
qemu_irq isa_get_irq(ISADevice *dev, unsigned isairq)
{
    assert(!dev || ISA_BUS(qdev_get_parent_bus(DEVICE(dev))) == isabus);
    assert(isairq < ISA_NUM_IRQS);
    return isabus->irqs[isairq]; // 这里应该是在 pc_i8259_create 中初始化的
}

void isa_init_irq(ISADevice *dev, qemu_irq *p, unsigned isairq) // isairq 表示中断引脚号
{
    assert(dev->nirqs < ARRAY_SIZE(dev->isairq));
    assert(isairq < ISA_NUM_IRQS);
    dev->isairq[dev->nirqs] = isairq;
    *p = isa_get_irq(dev, isairq); // p 就是申请到的 qemu_irq
    dev->nirqs++;
}
```

这里 `isabus->irqs[isairq]` 就是 `x86ms->gsi`，也就是说申请的 `qemu_irq` 就是 `pc_gsi_create` 创建的。

```c
static void pc_init1(MachineState *machine,
                     const char *host_type, const char *pci_type)
{
	...

	isa_bus_irqs(isa_bus, x86ms->gsi);

	...
}
```

```c
void isa_bus_irqs(ISABus *bus, qemu_irq *irqs)
{
    bus->irqs = irqs;
}
```

对于 pic 设备，申请的 `qemu_irq` 对应的就是 `gsi_handler` 中的 `i8259_irq` 。

```c
	case 0 ... ISA_NUM_IRQS - 1:
        if (s->i8259_irq[n]) {
            /* Under KVM, Kernel will forward to both PIC and IOAPIC */
            qemu_set_irq(s->i8259_irq[n], level);
        }
```

申请了 `qemu_irq` 之后，设备会通过 `qemu_set_irq` 来发起中断。如 kbd 和 mouse 设备。

```c
/* update irq and KBD_STAT_[MOUSE_]OBF */
/* XXX: not generating the irqs if KBD_MODE_DISABLE_KBD is set may be
   incorrect, but it avoids having to simulate exact delays */
static void kbd_update_irq(KBDState *s)
{
    int irq_kbd_level, irq_mouse_level;

    irq_kbd_level = 0;
    irq_mouse_level = 0;
    s->status &= ~(KBD_STAT_OBF | KBD_STAT_MOUSE_OBF);
    s->outport &= ~(KBD_OUT_OBF | KBD_OUT_MOUSE_OBF);

    ...

    qemu_set_irq(s->irq_kbd, irq_kbd_level);
    qemu_set_irq(s->irq_mouse, irq_mouse_level);
}
```

`s->irq_kbd` 和 `s->irq_mouse` 上面已经说过了，是设备指向中断引脚的指针。

```c
void qemu_set_irq(qemu_irq irq, int level)
{
    if (!irq)
        return;

    irq->handler(irq->opaque, irq->n, level); // 由不同设备的中断引脚执行
}
```

这里 level 表示触发时的电平。根据申请时的 handler 进行中断处理。pic 设备的处理函数是 `kvm_pic_set_irq` ，前面有说明。

```c
static void kvm_pic_set_irq(void *opaque, int irq, int level)
{
    int delivered;

    pic_stat_update_irq(irq, level);
    delivered = kvm_set_irq(kvm_state, irq, level);
    apic_report_irq_delivered(delivered);
}
```

`kvm_set_irq` 通过 `kvm_vm_ioctl(s, s->irq_set_ioctl, &event)` 系统调用将中断传到 kvm 中，再由 kvm 根据前面分析的中断路由表注入到 guestos 中。`s->irq_set_ioctl` 会设置成 `KVM_IRQ_LINE_STATUS` 。

```c
int kvm_set_irq(KVMState *s, int irq, int level)
{
    struct kvm_irq_level event;
    int ret;

    assert(kvm_async_interrupts_enabled());

    event.level = level;
    event.irq = irq;
    ret = kvm_vm_ioctl(s, s->irq_set_ioctl, &event);
    if (ret < 0) {
        perror("kvm_set_irq");
        abort();
    }

    return (s->irq_set_ioctl == KVM_IRQ_LINE) ? 1 : event.status;
}
```

我们看看具体的执行流程（这个调用流程不是键鼠的，而是 piix3，不过意思都一样）：

```c
#0  kvm_pic_set_irq (opaque=0x0, irq=10, level=0) at ../hw/i386/kvm/i8259.c:118
#1  0x0000555555d4018c in qemu_set_irq (irq=0x555556fdb980, level=0) at ../hw/core/irq.c:45
#2  0x0000555555b39e8a in gsi_handler (opaque=0x555556b991a0, n=10, level=0) at ../hw/i386/x86.c:599
#3  0x0000555555d4018c in qemu_set_irq (irq=0x555556b98010, level=0) at ../hw/core/irq.c:45
#4  0x00005555559b708d in piix3_set_irq_pic (piix3=0x555556f3bd00, pic_irq=10) at ../hw/isa/piix3.c:43
#5  0x00005555559b7196 in piix3_set_irq_level (piix3=0x555556f3bd00, pirq=0, level=0) at ../hw/isa/piix3.c:75
#6  0x00005555559b728b in piix3_update_irq_levels (piix3=0x555556f3bd00) at ../hw/isa/piix3.c:108
// 南桥写完配置空间后发起中断给 guest 说明任务已经完成
#7  0x00005555559b7318 in piix3_write_config (dev=0x555556f3bd00, address=96, val=10, len=1) at ../hw/isa/piix3.c:121
#8  0x0000555555a3a99f in pci_host_config_write_common (pci_dev=0x555556f3bd00, addr=96, limit=256, val=10, len=1) at ../hw/pci/pci_host.c:84
#9  0x0000555555a3ab20 in pci_data_write (s=0x555556b9f1f0, addr=2147485792, val=10, len=1) at ../hw/pci/pci_host.c:122
// 访问 PCI 设备的配置空间都是通过 0xcf8 和 0xcfc，然后 PCI-HOST bridge 即北桥进行转发
#10 0x0000555555a3ac56 in pci_host_data_write (opaque=0x555556b9e100, addr=0, val=10, len=1) at ../hw/pci/pci_host.c:169
#11 0x0000555555bf49bb in memory_region_write_accessor (mr=0x555556b9e520, addr=0, value=0x7ffff228f3f8, size=1, shift=0, mask=255, attrs=...) at ../softmmu/memory.c:492
#12 0x0000555555bf4c09 in access_with_adjusted_size (addr=0, value=0x7ffff228f3f8, size=1, access_size_min=1, access_size_max=4, access_fn=0x555555bf48c1 <memory_region_write_accessor>,
    mr=0x555556b9e520, attrs=...) at ../softmmu/memory.c:554
#13 0x0000555555bf7d07 in memory_region_dispatch_write (mr=0x555556b9e520, addr=0, data=10, op=MO_8, attrs=...) at ../softmmu/memory.c:1504
// 先进行平坦化操作再 dispatch
#14 0x0000555555beaa5c in flatview_write_continue (fv=0x7ffde40417b0, addr=3324, attrs=..., ptr=0x7ffff7fc7000, len=1, addr1=0, l=1, mr=0x555556b9e520) at ../softmmu/physmem.c:2782
#15 0x0000555555beaba5 in flatview_write (fv=0x7ffde40417b0, addr=3324, attrs=..., buf=0x7ffff7fc7000, len=1) at ../softmmu/physmem.c:2822
#16 0x0000555555beaf1f in address_space_write (as=0x55555675cda0 <address_space_io>, addr=3324, attrs=..., buf=0x7ffff7fc7000, len=1) at ../softmmu/physmem.c:2914
// memory model 进程 dispatch
#17 0x0000555555beaf90 in address_space_rw (as=0x55555675cda0 <address_space_io>, addr=3324, attrs=..., buf=0x7ffff7fc7000, len=1, is_write=true) at ../softmmu/physmem.c:2924
// 遇到 KVM 不能处理的指令 —— guest 访问 i/o 地址空间，需要进行处理
#18 0x0000555555d0b58e in kvm_handle_io (port=3324, attrs=..., data=0x7ffff7fc7000, direction=1, size=1, count=1) at ../accel/kvm/kvm-all.c:2642
#19 0x0000555555d0bd3d in kvm_cpu_exec (cpu=0x555556a4bce0) at ../accel/kvm/kvm-all.c:2893
#20 0x0000555555d0dc13 in kvm_vcpu_thread_fn (arg=0x555556a4bce0) at ../accel/kvm/kvm-accel-ops.c:49
#21 0x0000555555ef8857 in qemu_thread_start (args=0x555556a59e80) at ../util/qemu-thread-posix.c:556
#22 0x00007ffff6753609 in start_thread () from /lib/x86_64-linux-gnu/libpthread.so.0
#23 0x00007ffff6678163 in clone () from /lib/x86_64-linux-gnu/libc.so.6
```

解释一下就是 `kvm_cpu_exec` 在调用 `ioctl(KVM_RUN)` 进入到 KVM 执行后，**KVM 遇到了不能处理的事情（在这里是 I/O 访问），返回到 QEMU 执行，QEMU 在执行完 I/O 操作后通过 pic 中断通知 KVM I/O 的活我这边处理完了，剩下了你自己搞定**。下面就是 KVM 怎样处理从 QEMU 传入的中断。

KVM 在 `kvm_vm_ioctl` 中处理所有的虚拟机有关的系统调用，在 `KVM_IRQ_LINE_STATUS` 中处理中断。

```c
#ifdef __KVM_HAVE_IRQ_LINE
	case KVM_IRQ_LINE_STATUS:
	case KVM_IRQ_LINE: {
		struct kvm_irq_level irq_event;

		r = -EFAULT;
        // destination, source, size
		if (copy_from_user(&irq_event, argp, sizeof(irq_event)))
			goto out;

		r = kvm_vm_ioctl_irq_line(kvm, &irq_event,
					ioctl == KVM_IRQ_LINE_STATUS);
		...

		break;
	}
#endif
```

这里值得一提的是 `copy_from_user` 和  `copy_to_user` 这两个函数在 xv6 中遇到过，是在用户态和系统态之间传递数据的（其实不止这两个函数，具体可以看[这篇](https://github.com/UtopianFuture/UtopianFuture.github.io/blob/master/kernel/Access-userspace-from-kernel.md)文章）。因为 QEMU 是用户态进程，而 KVM 是内核模块，所以需要将 QEMU 的参数拷贝到内核。

```c
int kvm_vm_ioctl_irq_line(struct kvm *kvm, struct kvm_irq_level *irq_event,
			bool line_status)
{
	if (!irqchip_in_kernel(kvm))
		return -ENXIO;

	irq_event->status = kvm_set_irq(kvm, KVM_USERSPACE_IRQ_SOURCE_ID,
					irq_event->irq, irq_event->level, // irq 和 level 就是 QEMU 中的 kvm_irq_level
					line_status);
	return 0;
}
```

```c
/*
 * Return value:
 *  < 0   Interrupt was ignored (masked or not delivered for other reasons)
 *  = 0   Interrupt was coalesced (previous irq is still pending)
 *  > 0   Number of CPUs interrupt was delivered to
 */
int kvm_set_irq(struct kvm *kvm, int irq_source_id, u32 irq, int level,
		bool line_status)
{
	struct kvm_kernel_irq_routing_entry irq_set[KVM_NR_IRQCHIPS]; // 内核路由信息
	int ret = -1, i, idx;

	trace_kvm_set_irq(irq, level, irq_source_id);

	/* Not possible to detect if the guest uses the PIC or the
	 * IOAPIC.  So set the bit in both. The guest will ignore
	 * writes to the unused one.
	 */
	idx = srcu_read_lock(&kvm->irq_srcu);
	i = kvm_irq_map_gsi(kvm, irq_set, irq); // 将 irq 转换成 gsi，这个 i 表示所有使用这个中断号的设备的数量
	srcu_read_unlock(&kvm->irq_srcu, idx);

	while (i--) {
		int r;
        // 依次调用该 irq 对应的回调函数，总有一个合适，因为中断线有限，所有很多设备要共用中断引脚
        // 这样无论是内核执行还是 QEMU 模拟都很慢，而且中断请求频率还不低，所以后来使用 MSIX
		r = irq_set[i].set(&irq_set[i], kvm, irq_source_id, level,
				   line_status);
		if (r < 0)
			continue;

		ret = r + ((ret < 0) ? 0 : ret);
	}

	return ret;
}
```

通过 `kvm->irq_routing` 获取虚拟机的中断路由表。entries 存储 map 中 gsi 对应的路由信息。

```c
int kvm_irq_map_gsi(struct kvm *kvm,
		    struct kvm_kernel_irq_routing_entry *entries, int gsi)
{
	struct kvm_irq_routing_table *irq_rt; // 虚拟机中断路由表
	struct kvm_kernel_irq_routing_entry *e; // 内核中断信息表
	int n = 0;

	irq_rt = srcu_dereference_check(kvm->irq_routing, &kvm->irq_srcu,
					lockdep_is_held(&kvm->irq_lock));
	if (irq_rt && gsi < irq_rt->nr_rt_entries) { // 这个很好理解
		hlist_for_each_entry(e, &irq_rt->map[gsi], link) { // map 中是 kernel 的中断路由信息
			entries[n] = *e; // 该设备使用该中断号
			++n;
		}
	}

	return n;
}
```

获取到路由信息后 while 循环调用每一个 set 回调函数，pic 对应的回调函数是 `kvm_pic_set_irq` 。

```c
int kvm_pic_set_irq(struct kvm_pic *s, int irq, int irq_source_id, int level)
{
	int ret, irq_level;

	BUG_ON(irq < 0 || irq >= PIC_NUM_PINS);

	pic_lock(s);
	irq_level = __kvm_irq_line_state(&s->irq_states[irq], // 计算中断信号的电平
					 irq_source_id, level);
	ret = pic_set_irq1(&s->pics[irq >> 3], irq & 7, irq_level);
	pic_update_irq(s);
	trace_kvm_pic_set_irq(irq >> 3, irq & 7, s->pics[irq >> 3].elcr,
			      s->pics[irq >> 3].imr, ret == 0);
	pic_unlock(s);

	return ret;
}
```

`pic_set_irq1` 根据电平设置中断控制器的状态，根据控制器的触发类型设置 `kvm_kpic_state->irr` 和 `kvm_kpic_state->last_irr` 。

```c
/*
 * set irq level. If an edge is detected, then the IRR is set to 1
 */
static inline int pic_set_irq1(struct kvm_kpic_state *s, int irq, int level)
{
	int mask, ret = 1;
	mask = 1 << irq;
	if (s->elcr & mask)	/* level triggered */
		if (level) {
			ret = !(s->irr & mask);
			s->irr |= mask;
			s->last_irr |= mask;
		} else {
			s->irr &= ~mask;
			s->last_irr &= ~mask;
		}
	else	/* edge triggered */
		if (level) {
			if ((s->last_irr & mask) == 0) {
				ret = !(s->irr & mask);
				s->irr |= mask;
			}
			s->last_irr |= mask;
		} else
			s->last_irr &= ~mask;

	return (s->imr & mask) ? -1 : ret;
}
```

`pic_update_irq` 继续根据中断请求设置 `kvm_pic` 对应的位，

```c
/*
 * raise irq to CPU if necessary. must be called every time the active
 * irq may change
 */
static void pic_update_irq(struct kvm_pic *s)
{
	int irq2, irq;

	irq2 = pic_get_irq(&s->pics[1]);
	if (irq2 >= 0) {
		/*
		 * if irq request by slave pic, signal master PIC
		 */
		pic_set_irq1(&s->pics[0], 2, 1);
		pic_set_irq1(&s->pics[0], 2, 0);
	}
	irq = pic_get_irq(&s->pics[0]);
	pic_irq_request(s->kvm, irq >= 0);
}
```

最后调用 `pic_unlock`  ，判断是否需要通过 `kvm_vcpu_kick` 唤醒 vcpu 。如果需要，即有中断需要注入，则通过 `kvm_make_request` 在对应的 vcpu 上挂载 `KVM_REQ_EVENT` 请求。

```c
static void pic_unlock(struct kvm_pic *s)
	__releases(&s->lock)
{
	bool wakeup = s->wakeup_needed;
	struct kvm_vcpu *vcpu;
	int i;

	s->wakeup_needed = false;

	spin_unlock(&s->lock);

	if (wakeup) {
		kvm_for_each_vcpu(i, vcpu, s->kvm) {
			if (kvm_apic_accept_pic_intr(vcpu)) {
				kvm_make_request(KVM_REQ_EVENT, vcpu);
				kvm_vcpu_kick(vcpu); // 唤醒 vcpu
				return;
			}
		}
	}
}
```

最后再经过上文介绍的中断注入过程向 guestos 注入中断。之后在进入 non-root 模式时会读取注入的中断进行处理。

这就是整个中断的执行流程。

#### I/O-APIC中断模拟

I/O apic 模拟的关键是这两个数据结构。`kvm_ioapic` 是 kvm 中用来表示 I/O apic 中断控制器，`kvm_ioapic_redirect_entry` 则表示重定位表项，I/O apic 有 24 个端口，且可以通过编程设置。

```c
union kvm_ioapic_redirect_entry {
	u64 bits;
	struct {
		u8 vector;
		u8 delivery_mode:3;
		u8 dest_mode:1;
		u8 delivery_status:1;
		u8 polarity:1;
		u8 remote_irr:1;
		u8 trig_mode:1;
		u8 mask:1;
		u8 reserve:7;
		u8 reserved[4];
		u8 dest_id;
	} fields;
};
```

```c
struct kvm_ioapic {
	u64 base_address; // I/O apic 的 MMIO 地址
	u32 ioregsel;
	u32 id;
	u32 irr;
	u32 pad;
	union kvm_ioapic_redirect_entry redirtbl[IOAPIC_NUM_PINS];
	unsigned long irq_states[IOAPIC_NUM_PINS];
	struct kvm_io_device dev;
	struct kvm *kvm; //
	void (*ack_notifier)(void *opaque, int irq);
	spinlock_t lock;
	struct rtc_status rtc_status;
	struct delayed_work eoi_inject;
	u32 irq_eoi[IOAPIC_NUM_PINS];
	u32 irr_delivered;
};
```

其中断请求过程和 pic 类似，之后有需要再进一步分析。

#### MSI中断模拟

上面介绍了 MSI(X) 的基本概念，这里介绍 QEMU 是怎样模拟 MSI(X) 中断的。先看看 PCI 设备是怎样通过 MSI 发起中断的。

只有下面 3 个函数会通过 MSI 发起中断，

```c
void pci_default_write_config(PCIDevice *d, uint32_t addr, uint32_t val_in, int l){};
static void pci_bridge_dev_write_config(PCIDevice *d, uint32_t address, uint32_t val, int len){};
static void pcie_pci_bridge_write_config(PCIDevice *d, uint32_t address, uint32_t val, int len){};
```

##### 关键函数msi_write_config

该函数主要检查 MSI Message Control Register，并调用 `msi_notify`。

```c
/* Normally called by pci_default_write_config(). */
void msi_write_config(PCIDevice *dev, uint32_t addr, uint32_t val, int len)
{
    // 根据 PCI capability 指针获取 MSI Message Control Register
    uint16_t flags = pci_get_word(dev->config + msi_flags_off(dev));
    bool msi64bit = flags & PCI_MSI_FLAGS_64BIT;
    bool msi_per_vector_mask = flags & PCI_MSI_FLAGS_MASKBIT;
    unsigned int nr_vectors;
    uint8_t log_num_vecs;
    uint8_t log_max_vecs;
    unsigned int vector;
    uint32_t pending;

    if (!msi_present(dev) ||
        !ranges_overlap(addr, len, dev->msi_cap, msi_cap_sizeof(flags))) {
        return;
    }

	...

    /*
     * nr_vectors might be set bigger than capable. So clamp it.
     * This is not legal by spec, so we can do anything we like,
     * just don't crash the host
     */
    // 根据 MSI Message Control Register 中的描述，可以知道这是检查 vector 是否超过 max
    // PCI_MSI_FLAGS_QSIZE 是现在有多少个 vector
    // PCI_MSI_FLAGS_QMASK 是最多能有多少个 vector
    log_num_vecs =
        (flags & PCI_MSI_FLAGS_QSIZE) >> ctz32(PCI_MSI_FLAGS_QSIZE);
    log_max_vecs =
        (flags & PCI_MSI_FLAGS_QMASK) >> ctz32(PCI_MSI_FLAGS_QMASK);
    if (log_num_vecs > log_max_vecs) {
        flags &= ~PCI_MSI_FLAGS_QSIZE;
        flags |= log_max_vecs << ctz32(PCI_MSI_FLAGS_QSIZE);
        pci_set_word(dev->config + msi_flags_off(dev), flags);
    }

    nr_vectors = msi_nr_vectors(flags);

    /* This will discard pending interrupts, if any. */
    // pending 和 mask 配合
    pending = pci_get_long(dev->config + msi_pending_off(dev, msi64bit));
    pending &= 0xffffffff >> (PCI_MSI_VECTORS_MAX - nr_vectors);
    pci_set_long(dev->config + msi_pending_off(dev, msi64bit), pending);

    /* deliver pending interrupts which are unmasked */
    for (vector = 0; vector < nr_vectors; ++vector) {
        if (msi_is_masked(dev, vector) || !(pending & (1U << vector))) {
            continue;
        }

        pci_long_test_and_clear_mask(
            dev->config + msi_pending_off(dev, msi64bit), 1U << vector);
        msi_notify(dev, vector);
    }
}
```

##### 关键函数msi_notify

```c
void msi_notify(PCIDevice *dev, unsigned int vector)
{
    uint16_t flags = pci_get_word(dev->config + msi_flags_off(dev));
    bool msi64bit = flags & PCI_MSI_FLAGS_64BIT;
    unsigned int nr_vectors = msi_nr_vectors(flags);
    MSIMessage msg;

    ...

    msg = msi_get_message(dev, vector);

    MSI_DEV_PRINTF(dev,
                   "notify vector 0x%x"
                   " address: 0x%"PRIx64" data: 0x%"PRIx32"\n",
                   vector, msg.address, msg.data);
    msi_send_message(dev, msg);
}
```

##### 关键函数msi_get_message

这个数据结构和理解的一样。

```c
struct MSIMessage {
    uint64_t address;
    uint32_t data;
};
```

构建一个 MSIMessage。

```c
MSIMessage msi_get_message(PCIDevice *dev, unsigned int vector)
{
    uint16_t flags = pci_get_word(dev->config + msi_flags_off(dev));
    bool msi64bit = flags & PCI_MSI_FLAGS_64BIT;
    unsigned int nr_vectors = msi_nr_vectors(flags);
    MSIMessage msg;

    assert(vector < nr_vectors);

    // 要注意 dev->config + msi_address_lo_off(dev) 只是获取到对应的寄存器
    // pci_get_quad 才是获取其中存储的地址
    if (msi64bit) { // PCI_MSI_ADDRESS_LO 和 PCI_MSI_ADDRESS_HI
        msg.address = pci_get_quad(dev->config + msi_address_lo_off(dev));
    } else { // PCI_MSI_ADDRESS_LO
        msg.address = pci_get_long(dev->config + msi_address_lo_off(dev));
    }

    /* upper bit 31:16 is zero */
    msg.data = pci_get_word(dev->config + msi_data_off(dev, msi64bit)); // 这个就很好理解了
    if (nr_vectors > 1) {
        msg.data &= ~(nr_vectors - 1);
        msg.data |= vector;
    }

    return msg;
}
```

##### 关键函数msi_send_message

```c
void msi_send_message(PCIDevice *dev, MSIMessage msg)
{
    MemTxAttrs attrs = {};

    // 这个是干嘛的？
    attrs.requester_id = pci_requester_id(dev);
    // 写入
    address_space_stl_le(&dev->bus_master_as, msg.address, msg.data,
                         attrs, NULL);
}
```

而 `address_space_stl_le` 会导致执行 QEMU 层 kvm-apic 设备 MMIO 写回调函数，即 `kvm_apic_mem_write`。其会封装一个 `MSIMessage` 然后调用 `kvm_send_msi`。

```c
static void kvm_send_msi(MSIMessage *msg)
{
    int ret;

    /*
     * The message has already passed through interrupt remapping if enabled,
     * but the legacy extended destination ID in low bits still needs to be
     * handled.
     */
    msg->address = kvm_swizzle_msi_ext_dest_id(msg->address);

    ret = kvm_irqchip_send_msi(kvm_state, *msg);
    if (ret < 0) {
        fprintf(stderr, "KVM: injection failed, MSI lost (%s)\n",
                strerror(-ret));
    }
}
```

`kvm_irqchip_send_msi` 使用 ioctl 向 host 发起 `KVM_SIGNAL_MSI` 中断，之后 host 再进行处理。

```c
int kvm_irqchip_send_msi(KVMState *s, MSIMessage msg)
{
    struct kvm_msi msi;
    KVMMSIRoute *route;

    if (kvm_direct_msi_allowed) {
        msi.address_lo = (uint32_t)msg.address;
        msi.address_hi = msg.address >> 32;
        msi.data = le32_to_cpu(msg.data);
        msi.flags = 0;
        memset(msi.pad, 0, sizeof(msi.pad));

        return kvm_vm_ioctl(s, KVM_SIGNAL_MSI, &msi);
    }

    ...
}
```

#### KVM处理ioctl(KVM_SIGNAL_MSI)

我们知道 KVM 会在 `kvm_vm_ioctl`, `kvm_device_ioctl`, `kvm_vcpu_ioctl` 中处理 ioctl，我们看看其是怎样处理 `KVM_SIGNAL_MSI` 的。

```c
static long kvm_vm_ioctl(struct file *filp,
			   unsigned int ioctl, unsigned long arg)
{
	struct kvm *kvm = filp->private_data;
	void __user *argp = (void __user *)arg;
	int r;

	if (kvm->mm != current->mm || kvm->vm_bugged)
		return -EIO;
	switch (ioctl) {

    ...

#ifdef CONFIG_HAVE_KVM_MSI
	case KVM_SIGNAL_MSI: {
		struct kvm_msi msi;

		r = -EFAULT;
		if (copy_from_user(&msi, argp, sizeof(msi))) // 将用户态参数复制到内核
			goto out;
		r = kvm_send_userspace_msi(kvm, &msi);
		break;
	}
#endif

    ...

out:
	return r;
}
```

##### 关键函数kvm_send_userspace_msi

```c
int kvm_send_userspace_msi(struct kvm *kvm, struct kvm_msi *msi)
{
	struct kvm_kernel_irq_routing_entry route;

	if (!irqchip_in_kernel(kvm) || (msi->flags & ~KVM_MSI_VALID_DEVID))
		return -EINVAL;

	route.msi.address_lo = msi->address_lo;
	route.msi.address_hi = msi->address_hi;
	route.msi.data = msi->data;
	route.msi.flags = msi->flags;
	route.msi.devid = msi->devid;

	return kvm_set_msi(&route, kvm, KVM_USERSPACE_IRQ_SOURCE_ID, 1, false);
}
```

`kvm_kernel_irq_routing_entry` 在前面介绍过，是 KVM 中创建的结构体，用于在内核中记录中断信息，除了基本的中断号 `gsi` ，中断类型信息 `type` ，还有用于处理中断的回调函数 `set` 。

##### 关键函数kvm_irq_delivery_to_apic

`kvm_set_msi` -> `kvm_irq_delivery_to_apic`

这个函数之后再分析，现在不要把战线拉的过长。

```c
int kvm_irq_delivery_to_apic(struct kvm *kvm, struct kvm_lapic *src,
		struct kvm_lapic_irq *irq, struct dest_map *dest_map)
{
	int i, r = -1;
	struct kvm_vcpu *vcpu, *lowest = NULL;
	unsigned long dest_vcpu_bitmap[BITS_TO_LONGS(KVM_MAX_VCPUS)];
	unsigned int dest_vcpus = 0;

	if (kvm_irq_delivery_to_apic_fast(kvm, src, irq, &r, dest_map))
		return r;

	if (irq->dest_mode == APIC_DEST_PHYSICAL &&
	    irq->dest_id == 0xff && kvm_lowest_prio_delivery(irq)) {
		printk(KERN_INFO "kvm: apic: phys broadcast and lowest prio\n");
		irq->delivery_mode = APIC_DM_FIXED;
	}

	memset(dest_vcpu_bitmap, 0, sizeof(dest_vcpu_bitmap));

	kvm_for_each_vcpu(i, vcpu, kvm) {
		if (!kvm_apic_present(vcpu))
			continue;

		if (!kvm_apic_match_dest(vcpu, src, irq->shorthand,
					irq->dest_id, irq->dest_mode))
			continue;

		if (!kvm_lowest_prio_delivery(irq)) {
			if (r < 0)
				r = 0;
			r += kvm_apic_set_irq(vcpu, irq, dest_map);
		} else if (kvm_apic_sw_enabled(vcpu->arch.apic)) {
			if (!kvm_vector_hashing_enabled()) {
				if (!lowest)
					lowest = vcpu;
				else if (kvm_apic_compare_prio(vcpu, lowest) < 0)
					lowest = vcpu;
			} else {
				__set_bit(i, dest_vcpu_bitmap);
				dest_vcpus++;
			}
		}
	}

	if (dest_vcpus != 0) {
		int idx = kvm_vector_to_index(irq->vector, dest_vcpus,
					dest_vcpu_bitmap, KVM_MAX_VCPUS);

		lowest = kvm_get_vcpu(kvm, idx);
	}

	if (lowest)
		r = kvm_apic_set_irq(lowest, irq, dest_map);

	return r;
}
```

#### MSIX中断模拟

MSIX 是在 MSI 的基础上为了支持多个中断增加了 MSI-X Table，具体结构看上面的图。它只在 `pci_default_write_config` 中被调用，而且和 MSI 一起被调用，这是为什么？

```c
void pci_default_write_config(PCIDevice *d, uint32_t addr, uint32_t val_in, int l)
{
    int i, was_irq_disabled = pci_irq_disabled(d);
    uint32_t val = val_in;

    assert(addr + l <= pci_config_size(d));

    ...

    msi_write_config(d, addr, val_in, l);
    msix_write_config(d, addr, val_in, l);
}
```

`msix_write_config` 还是根据控制位进行一些处理，然后调用 `msix_handle_mask_update` -> `msix_notify`，最后还是调用 `msi_send_message` 发送中断信息，看来 MSIX 和 MSI 在 QEMU 中的模拟类似啊，不过生成 message 的过程不同，

```c
MSIMessage msix_get_message(PCIDevice *dev, unsigned vector)
{
    uint8_t *table_entry = dev->msix_table + vector * PCI_MSIX_ENTRY_SIZE;
    MSIMessage msg;

    // 好吧，没有什么不一样，之后在 table 中获取 addr 和 data
    msg.address = pci_get_quad(table_entry + PCI_MSIX_ENTRY_LOWER_ADDR);
    msg.data = pci_get_long(table_entry + PCI_MSIX_ENTRY_DATA);
    return msg;
}
```

不过 MSIX 也有一些 MSI 没有的操作，

```c
static void msix_table_mmio_write(void *opaque, hwaddr addr,
                                  uint64_t val, unsigned size)
{
    PCIDevice *dev = opaque;
    int vector = addr / PCI_MSIX_ENTRY_SIZE;
    bool was_masked;

    assert(addr + size <= dev->msix_entries_nr * PCI_MSIX_ENTRY_SIZE);

    was_masked = msix_is_masked(dev, vector);
    pci_set_long(dev->msix_table + addr, val); // 通过 MMIO 的方式修改 MSIX Table
    // 很多地方都有这个 update mask 函数，不过现在没有深入分析 mask 对 MSI 和 MSIX 的影响
    msix_handle_mask_update(dev, vector, was_masked);
}
```

MSIX 远不止这些内容，不过目前了解这些已经够用了，之后有需要再进一步分析。

### APIC虚拟化

### 问题

1. 用 KVM 的话会在 KVM 中创建虚拟的 PIC 或 APIC 设备进行模拟，硬件中断将发送到这些虚拟的中断设备，然后在注入到虚拟机中，那么不用 KVM 用 tcg 的话是怎样的过程？

### Reference

[1] 深入探索 Linux 系统虚拟化 王柏生 谢广军 机械工业出版社

[2] https://www.cnblogs.com/haiyonghao/p/14440424.html

[3] https://www.intel.com/content/www/us/en/docs/programmable/683686/20-4/msi-registers.html

[4] https://blog.csdn.net/yhb1047818384/article/details/106676560
