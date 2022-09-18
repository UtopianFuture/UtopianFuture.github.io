## 系统虚拟化

在[蔚来](https://www.nio.cn/)面试的时候，前两面和面试官聊的都很好，因为做的项目确实有点东西，对内核也有一定了解，所以这两面都没有写题。但是在三面的时候应该是部门的大 leader，之前做了 20 多年的虚拟化，而且他做过和我类似的工作，所以我这点微末道行在他面前根本不够看，种种不足表露无疑。最重要的是对虚拟化没有一个全面、准确的认识，比如他问我什么是虚拟化，我的回答是“虚拟化其实就是为你需要虚拟的设备创建一个数据结构，然后构造一系列的函数来维护这个数据结构”。其实从 QEMU 来看这种理解是没有问题的，但是 QEMU 只是系统虚拟化的一种实现，而虚拟化是一个非常大的、很早就提出来的概念，所以他说我对虚拟化的理解还是片面的没有问题。他给我的建议是加强对虚拟化基础知识的准确把握，我认为他的建议非常有道理，所以再看一看《系统虚拟化——原理与实现》从而纠正之前以项目为驱动导致的对基础概念理解不到位的问题，同时也算是为之后写论文打个基础吧。

当然，这本书其实年头有些久，其中的实现我结合 《Linux 系统虚拟化》加以补充，之后还会看看《Hardware and Software Support for Virtualization》进一步增强对虚拟化技术的理解。

### 背景

在学习过程中带着如下问题学习：

- 设计目标：

  在一台 x86 的多核机器上运行多个虚拟机，这些虚拟机上能够执行非 x86 架构的系统。

- 问题：

  - vm 到底是什么样的？

    其拥有的只是一系列的数据结构，而不拥有实际的资源。访问这些数据结构在 guest 看来就是在访问实际的硬件资源，但是这些访问要通过 kvm 的处理才能返回。

  - guest 和 host 的关系？

    guest 是 host 上运行的一个进程，用“进程 id” – VMCS 来记录实际状态。

  - kvm 其的作用是什么？

    首先，CPU 虚拟化就是需要用 kvm 来实现的，KVM 会创建 VMCS 数据结构，分配内存条等等。

  - 虚拟中断的核心是什么？

  - 硬件虚拟化的核心是什么？

### 虚拟化概述

虚拟化技术根本的思想就是“中间层”和“模块化”。

> Virtualization is the application of the layering principle through enforced modularity, whereby the exposed virtual resource is identical to the underlying physical resource being virtualized.

#### 可虚拟化架构与不可虚拟化架构

在没有虚拟化的环境中，是由操作系统管理物理硬件的，但是在虚拟环境中，VMM 抢占了操作系统的位置，变成真实物理硬件的管理者，同时向上层的软件呈现虚拟的硬件平台。

虚拟机可以看作是物理机资源的一种高效隔离的复制，其中需要完成三个任务：**同质、高效和资源受控**。如果一个 VMM 没有实现这个三个任务，那么可以说这个虚拟机是失败的。

首先介绍两个重要的概念：特权指令和敏感指令。

特权指令：系统中操作和管理关键系统资源的指令，这些指令**只有在最高特权级上能够正确运行**。如果在非特权级上运行，特权指令会引发一个异常，处理器会陷入到最高特权级，交由系统软件处理。

敏感指令：操作特权资源的指令，包括修改虚拟机的运行模式或者下面物理机的状态；读写敏感的寄存器或内存，如时钟或中断寄存器；访问存储保护系统、内存系统或者地址重定位系统以及所有的 I/O 指令。

显然，所有的特权指令都是敏感指令，但不是所有的敏感指令都是特权指令。VMM 为了控制所有的系统资源，不允许 guestos 直接执行敏感指令。如果一个架构所有的敏感指令都是特权指令，那么很好办：将 VMM 运行在系统的最高特权级上，而将 guestos 运行在非最高特权级上，当 guestos 因执行敏感指令而陷入到 VMM 时，VMM 模拟执行引起异常的敏感指令，这种方法称为“陷入再模拟”。

故判断一个架构是不是可虚拟化架构，就在于其对敏感指令的支持。如果该架构无法支持在所有的敏感指令上触发异常，那么我们称该架构有“虚拟化漏洞”。当然有一些方法能够填补“虚拟化漏洞”，最简单粗暴的就是对所有指令都模拟，“二进制翻译”就是这样一种技术，所以说在没有硬件辅助的虚拟化出现之前，“二进制翻译”就是虚拟化技术的一种实现，直到现在，“二进制翻译”也是异构虚拟化的解决方案。

#### VMM相关

根据 VMM 提供的虚拟平台类型可以将 VMM 分成两类：

- 完全虚拟化，无需对操作系统进行任何修改，guestos 觉察不到是运行在一个虚拟平台上（但是可以通过一些技术手段来检查是否运行在虚拟机上）。其经历了软件辅助的完全虚拟化和硬件辅助的完全虚拟化两个阶段。
- 类虚拟化，需要对 guestos 进行一些修改以适应虚拟环境。

### CPU 虚拟化

物理 CPU 同时运行 host 和 guest，**在不同模式间按需切换**，每个模式需要保存上下文。VMX 设计了 `VMCS`，每个 guest 的每个 VCPU 都有自己的 `VMCS`，在进行 guest 和 host 切换时就是将 `VMCS` 中的内容加载到 CPU 中和将 CPU 中的内容保存到 `VMCS` 对应的域中。通过 `VMLaunch ` 切换到 not root 模式，即进入 guest(vm entry)，当需要执行敏感指令时，退回到 root 模式，即退出 guest(vm exit)。

#### 陷入和模拟

1. 常用的外设访问方式：

   （1）MMIO

   I/O 设备被映射到内存地址空间而不是 I/O 空间。当程序需要访问 I/O 时，会导致 CPU 页面异常，从而退出 guest 进入 KVM 进行异常处理。这个异常处理的过程就是虚拟，即模拟指令的执行。KVM 根据指令目的操作数的地址找到 MMIO 设备，然后调用具体的 MMIO 设备处理函数，之后就是正常的 I/O 操作。

   （2）PIO

   使用专用的 I/O 指令 `(out, outs, in, ins) `访问外设。

2. 特殊指令

   （1）CPUID

   QEMU 在 KVM 的用户空间通过 `cpuid` 指令获取 Host 的 CPU 特征，然后加上用户空间的配置，定义好 VCPU 支持的 CPU 特性，传递给 KVM 内核模块。

   KVM 用户空间按照如下结构体组织好 CPU 特性后传递给内核模块：

   ```c
   struct kvm_cpuid{
   	__u32 nent;
   	__u32 padding;
   	struct kvm_cpuid_entry entries[0];
   }
   ```

   `kvm_cpuid_entry` 就是 kvm 内核模块定义的 VCPU 特性结构体：

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

   guest 执行 cpuid 时发生 VM exit，KVM 会根据 `eax` 中的功能号以及 `ecx` 中的子功能号，从 `kvm_cpuid_entry` 实例中所以到相应的 entry，使用 entry 中的 `eax`, `ebx`, `ecx`, `edx` 覆盖结构体 vcpu 中的 regs 数组对应的字段，当再次切入 guest 时，KVM 会将他们加载到物理 CPU 的通用寄存器，guest 就可以正常的读取。

​		（2）hlt

​		guest 执行 hlt 只是暂停逻辑核。VCPU 调用内核的 `schedule()` 将自己挂起。

#### 对称多处理器虚拟化

#### KVM 用户空间实例

定义结构体 vm 表示一台虚拟机，每个虚拟机可能有多个处理器，每个处理器有自己的状态，定义结构体 vcpu 表示处理器。

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

#### VMCS

VT-d 所有的特性都可以在这里找到。

```C
/* VMCS Encodings */
enum vmcs_field {
	VIRTUAL_PROCESSOR_ID            = 0x00000000,
	POSTED_INTR_NV                  = 0x00000002,
	GUEST_ES_SELECTOR               = 0x00000800,
	GUEST_CS_SELECTOR               = 0x00000802,
	GUEST_SS_SELECTOR               = 0x00000804,
	GUEST_DS_SELECTOR               = 0x00000806,
	GUEST_FS_SELECTOR               = 0x00000808,
	GUEST_GS_SELECTOR               = 0x0000080a,
	GUEST_LDTR_SELECTOR             = 0x0000080c,
	GUEST_TR_SELECTOR               = 0x0000080e,
	GUEST_INTR_STATUS               = 0x00000810,
	GUEST_PML_INDEX					= 0x00000812,
	HOST_ES_SELECTOR                = 0x00000c00,
	HOST_CS_SELECTOR                = 0x00000c02,
	HOST_SS_SELECTOR                = 0x00000c04,
	HOST_DS_SELECTOR                = 0x00000c06,
	HOST_FS_SELECTOR                = 0x00000c08,
	HOST_GS_SELECTOR                = 0x00000c0a,
	HOST_TR_SELECTOR                = 0x00000c0c,
	IO_BITMAP_A                     = 0x00002000,
	IO_BITMAP_A_HIGH                = 0x00002001,
	IO_BITMAP_B                     = 0x00002002,
	IO_BITMAP_B_HIGH                = 0x00002003,
	MSR_BITMAP                      = 0x00002004,
	MSR_BITMAP_HIGH                 = 0x00002005,
	VM_EXIT_MSR_STORE_ADDR          = 0x00002006,
	VM_EXIT_MSR_STORE_ADDR_HIGH     = 0x00002007,
	VM_EXIT_MSR_LOAD_ADDR           = 0x00002008,
	VM_EXIT_MSR_LOAD_ADDR_HIGH      = 0x00002009,
	VM_ENTRY_MSR_LOAD_ADDR          = 0x0000200a,
	VM_ENTRY_MSR_LOAD_ADDR_HIGH     = 0x0000200b,
	PML_ADDRESS						= 0x0000200e,
	PML_ADDRESS_HIGH				= 0x0000200f,
	TSC_OFFSET                      = 0x00002010,
	TSC_OFFSET_HIGH                 = 0x00002011,
	VIRTUAL_APIC_PAGE_ADDR          = 0x00002012,
	VIRTUAL_APIC_PAGE_ADDR_HIGH     = 0x00002013,
	APIC_ACCESS_ADDR				= 0x00002014,
	APIC_ACCESS_ADDR_HIGH			= 0x00002015,
	POSTED_INTR_DESC_ADDR           = 0x00002016,
	POSTED_INTR_DESC_ADDR_HIGH      = 0x00002017,
	VM_FUNCTION_CONTROL             = 0x00002018,
	VM_FUNCTION_CONTROL_HIGH        = 0x00002019,
	EPT_POINTER                     = 0x0000201a,
	EPT_POINTER_HIGH                = 0x0000201b,
	EOI_EXIT_BITMAP0                = 0x0000201c,
	EOI_EXIT_BITMAP0_HIGH           = 0x0000201d,
	EOI_EXIT_BITMAP1                = 0x0000201e,
	EOI_EXIT_BITMAP1_HIGH           = 0x0000201f,
	EOI_EXIT_BITMAP2                = 0x00002020,
	EOI_EXIT_BITMAP2_HIGH           = 0x00002021,
	EOI_EXIT_BITMAP3                = 0x00002022,
	EOI_EXIT_BITMAP3_HIGH           = 0x00002023,
	EPTP_LIST_ADDRESS               = 0x00002024,
	EPTP_LIST_ADDRESS_HIGH          = 0x00002025,
	VMREAD_BITMAP                   = 0x00002026,
	VMREAD_BITMAP_HIGH              = 0x00002027,
	VMWRITE_BITMAP                  = 0x00002028,
	VMWRITE_BITMAP_HIGH             = 0x00002029,
	XSS_EXIT_BITMAP                 = 0x0000202C,
	XSS_EXIT_BITMAP_HIGH            = 0x0000202D,
	ENCLS_EXITING_BITMAP			= 0x0000202E,
	ENCLS_EXITING_BITMAP_HIGH		= 0x0000202F,
	TSC_MULTIPLIER                  = 0x00002032,
	TSC_MULTIPLIER_HIGH             = 0x00002033,
	GUEST_PHYSICAL_ADDRESS          = 0x00002400,
	GUEST_PHYSICAL_ADDRESS_HIGH     = 0x00002401,
	VMCS_LINK_POINTER               = 0x00002800,
	VMCS_LINK_POINTER_HIGH          = 0x00002801,
	GUEST_IA32_DEBUGCTL             = 0x00002802,
	GUEST_IA32_DEBUGCTL_HIGH        = 0x00002803,
	GUEST_IA32_PAT					= 0x00002804,
	GUEST_IA32_PAT_HIGH				= 0x00002805,
	GUEST_IA32_EFER					= 0x00002806,
	GUEST_IA32_EFER_HIGH			= 0x00002807,
	GUEST_IA32_PERF_GLOBAL_CTRL		= 0x00002808,
	GUEST_IA32_PERF_GLOBAL_CTRL_HIGH= 0x00002809,
	GUEST_PDPTR0                    = 0x0000280a,
	GUEST_PDPTR0_HIGH               = 0x0000280b,
	GUEST_PDPTR1                    = 0x0000280c,
	GUEST_PDPTR1_HIGH               = 0x0000280d,
	GUEST_PDPTR2                    = 0x0000280e,
	GUEST_PDPTR2_HIGH               = 0x0000280f,
	GUEST_PDPTR3                    = 0x00002810,
	GUEST_PDPTR3_HIGH               = 0x00002811,
	GUEST_BNDCFGS                   = 0x00002812,
	GUEST_BNDCFGS_HIGH              = 0x00002813,
	GUEST_IA32_RTIT_CTL				= 0x00002814,
	GUEST_IA32_RTIT_CTL_HIGH		= 0x00002815,
	HOST_IA32_PAT					= 0x00002c00,
	HOST_IA32_PAT_HIGH				= 0x00002c01,
	HOST_IA32_EFER					= 0x00002c02,
	HOST_IA32_EFER_HIGH				= 0x00002c03,
	HOST_IA32_PERF_GLOBAL_CTRL		= 0x00002c04,
	HOST_IA32_PERF_GLOBAL_CTRL_HIGH	= 0x00002c05,
	PIN_BASED_VM_EXEC_CONTROL       = 0x00004000,
	CPU_BASED_VM_EXEC_CONTROL       = 0x00004002,
	EXCEPTION_BITMAP                = 0x00004004,
	PAGE_FAULT_ERROR_CODE_MASK      = 0x00004006,
	PAGE_FAULT_ERROR_CODE_MATCH     = 0x00004008,
	CR3_TARGET_COUNT                = 0x0000400a,
	VM_EXIT_CONTROLS                = 0x0000400c,
	VM_EXIT_MSR_STORE_COUNT         = 0x0000400e,
	VM_EXIT_MSR_LOAD_COUNT          = 0x00004010,
	VM_ENTRY_CONTROLS               = 0x00004012,
	VM_ENTRY_MSR_LOAD_COUNT         = 0x00004014,
	VM_ENTRY_INTR_INFO_FIELD        = 0x00004016,
	VM_ENTRY_EXCEPTION_ERROR_CODE   = 0x00004018,
	VM_ENTRY_INSTRUCTION_LEN        = 0x0000401a,
	TPR_THRESHOLD                   = 0x0000401c,
	SECONDARY_VM_EXEC_CONTROL       = 0x0000401e,
	PLE_GAP                         = 0x00004020,
	PLE_WINDOW                      = 0x00004022,
	VM_INSTRUCTION_ERROR            = 0x00004400,
	VM_EXIT_REASON                  = 0x00004402,
	VM_EXIT_INTR_INFO               = 0x00004404,
	VM_EXIT_INTR_ERROR_CODE         = 0x00004406,
	IDT_VECTORING_INFO_FIELD        = 0x00004408,
	IDT_VECTORING_ERROR_CODE        = 0x0000440a,
	VM_EXIT_INSTRUCTION_LEN         = 0x0000440c,
	VMX_INSTRUCTION_INFO            = 0x0000440e,
	GUEST_ES_LIMIT                  = 0x00004800,
	GUEST_CS_LIMIT                  = 0x00004802,
	GUEST_SS_LIMIT                  = 0x00004804,
	GUEST_DS_LIMIT                  = 0x00004806,
	GUEST_FS_LIMIT                  = 0x00004808,
	GUEST_GS_LIMIT                  = 0x0000480a,
	GUEST_LDTR_LIMIT                = 0x0000480c,
	GUEST_TR_LIMIT                  = 0x0000480e,
	GUEST_GDTR_LIMIT                = 0x00004810,
	GUEST_IDTR_LIMIT                = 0x00004812,
	GUEST_ES_AR_BYTES               = 0x00004814,
	GUEST_CS_AR_BYTES               = 0x00004816,
	GUEST_SS_AR_BYTES               = 0x00004818,
	GUEST_DS_AR_BYTES               = 0x0000481a,
	GUEST_FS_AR_BYTES               = 0x0000481c,
	GUEST_GS_AR_BYTES               = 0x0000481e,
	GUEST_LDTR_AR_BYTES             = 0x00004820,
	GUEST_TR_AR_BYTES               = 0x00004822,
	GUEST_INTERRUPTIBILITY_INFO     = 0x00004824,
	GUEST_ACTIVITY_STATE            = 0X00004826,
	GUEST_SYSENTER_CS               = 0x0000482A,
	VMX_PREEMPTION_TIMER_VALUE      = 0x0000482E,
	HOST_IA32_SYSENTER_CS           = 0x00004c00,
	CR0_GUEST_HOST_MASK             = 0x00006000,
	CR4_GUEST_HOST_MASK             = 0x00006002,
	CR0_READ_SHADOW                 = 0x00006004,
	CR4_READ_SHADOW                 = 0x00006006,
	CR3_TARGET_VALUE0               = 0x00006008,
	CR3_TARGET_VALUE1               = 0x0000600a,
	CR3_TARGET_VALUE2               = 0x0000600c,
	CR3_TARGET_VALUE3               = 0x0000600e,
	EXIT_QUALIFICATION              = 0x00006400,
	GUEST_LINEAR_ADDRESS            = 0x0000640a,
	GUEST_CR0                       = 0x00006800,
	GUEST_CR3                       = 0x00006802,
	GUEST_CR4                       = 0x00006804,
	GUEST_ES_BASE                   = 0x00006806,
	GUEST_CS_BASE                   = 0x00006808,
	GUEST_SS_BASE                   = 0x0000680a,
	GUEST_DS_BASE                   = 0x0000680c,
	GUEST_FS_BASE                   = 0x0000680e,
	GUEST_GS_BASE                   = 0x00006810,
	GUEST_LDTR_BASE                 = 0x00006812,
	GUEST_TR_BASE                   = 0x00006814,
	GUEST_GDTR_BASE                 = 0x00006816,
	GUEST_IDTR_BASE                 = 0x00006818,
	GUEST_DR7                       = 0x0000681a,
	GUEST_RSP                       = 0x0000681c,
	GUEST_RIP                       = 0x0000681e,
	GUEST_RFLAGS                    = 0x00006820,
	GUEST_PENDING_DBG_EXCEPTIONS    = 0x00006822,
	GUEST_SYSENTER_ESP              = 0x00006824,
	GUEST_SYSENTER_EIP              = 0x00006826,
	HOST_CR0                        = 0x00006c00,
	HOST_CR3                        = 0x00006c02,
	HOST_CR4                        = 0x00006c04,
	HOST_FS_BASE                    = 0x00006c06,
	HOST_GS_BASE                    = 0x00006c08,
	HOST_TR_BASE                    = 0x00006c0a,
	HOST_GDTR_BASE                  = 0x00006c0c,
	HOST_IDTR_BASE                  = 0x00006c0e,
	HOST_IA32_SYSENTER_ESP          = 0x00006c10,
	HOST_IA32_SYSENTER_EIP          = 0x00006c12,
	HOST_RSP                        = 0x00006c14,
	HOST_RIP                        = 0x00006c16,
};
```

### 内存虚拟化

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

   在没有虚拟化的情况下，cr3 寄存器指向的是 HVA 到 HPA 转换的映射表。但在虚拟化环境中需要在 guest 中将 GVA 转换成 GPA，然后 MMU 将 HVA 转换成 HPA。

   影子页表：**KVM 建立的一张将 GVA 转换成 HPA 的表，这张表需要根据 guest 内部页表的信息更新**。guest 中每个进程都有对应的页表，所有当 guest 任务切换时，影子页表也需要跟着切换，所以需要维护的页表是非常多的。

3. EPT

   EPT(extented page table pointer) 是硬件机制，完成从 GPA 到 HPA 的映射。EPT 的缺页处理的原理与 MMU 基本相同。**MMU 完成 GVA  到 GPA 的映射，EPT 完成 GPA  到 HPA  的映射，MMU 和  EPT 在硬件层面直接配合，不需要软件干涉，经过 MMU 翻译的 GPA 将在硬件层面给到 EPT** 。增加 EPT 后不需要频繁的 VM exit，同时，对于 host 而言，一个虚拟机就是一个进程，因此一个虚拟机只需要维护一个 EPT 表即可。

   VMX 在 `VMCS` 中定义了一个字段 `extended-page-table-pointer`，KVM 将 EPT 页表的位置写入这个字段，这样当 CPU 进入 guest 模式时，就可以从这个字段读出 EPT 页表的位置。而 **guest 模式下的 `cr3` 寄存器指向 guest 内部的页表**。

   当 guest 发生缺页异常时，CPU 不再切换到 host 模式，而是由 guest 自身的缺页异常处理函数处理。当地址从 GVA 翻译到 GPA 后，GPA 在硬件内部从 MMU 流转到 EPT。如果 EPT 页表中存在 GPA 到 HPA 的映射，则 EPT 最终获取了对应的 HPA，将 HPA 送上地址总线。如果不存在映射，那**么抛出 EPT 异常， CPU 将从 guest 模式切换到 host 模式，进行正常的异常处理**。建立好映射之后返回 guest 模式。

   需要退出 guest 时，会将引发异常的 GPA 保存到 `VMCS` 的 `guest physical address` 字段，然后 KVM 就可以根据这个 GPA 调用异常处理函数，处理 EPT 缺页异常。

   简单举例：如果 guest 采用 2 级页表，那么在通过一级页表目录读取二级页表地址时，需要通过 EPT，然后通过二级页表读取页帧地址时，需要通过 EPT，最后通过 offset 在页帧中读取字节需要通过 EPT。

### 中断虚拟化

#### 虚拟中断

物理 CPU 在执行完一条指令后，都会检查中断引脚是否有效，一旦有效，CPU 将处理中断，然后执行下一条指令。

对于软件虚拟的中断芯片而言，**“引脚”只是一个变量**。如果 KVM 发现虚拟中断芯片有中断请求，则向 `VMCS` 中的 `VM-entry control` 部分的 `VM-entry interruption-information field` 字段写入中断信息，**在切入 guest 模式的一刻，CPU 将检查这个字段，如同检查 CPU 引脚，如果有中断，则进入中断执行过程**。

guest 模式的 CPU 不能检测虚拟中断芯片的引脚，只能在 VM entry 时由 KVM 模块代为检查，然后写入 `VMCS`，一旦有中断注入，那么处于 guest 模式的 CPU 一定需要通过 VM exit 退出到 host 模式，这个上下文切换很麻烦。

在硬件层面增加对虚拟化的支持。在 guest 模式下实现 `virtual-APIC page` 页面和虚拟中断逻辑。**遇到中断时，将中断信息写入 `posted-interrupt descriptor`，然后通过特殊的核间中断 `posted-interrupt notification` 通知 CPU，guest 模式下的 CPU 就可以借助虚拟中断逻辑处理中断**。

#### PIC 虚拟化

PIC（可编程中断控制器，programmable interrupt controller），通过引脚向 CPU 发送中断信号。而虚拟设备请求中断是通过虚拟 8259A 芯片对外提供的一个 API。

- 虚拟设备向 PIC 发送中断请求

  guest 需要读取外设数据时，通过写 I/O 端口触发 CPU 从 guest 到 host，KVM 中的块设备开始 I/O 操作。操作完后调用虚拟 8259A 提供的 API 发出中断请求。

- 记录中断到 IRR (Interrupt Request Register)。

- 设置待处理中断标识

  虚拟 PIC 是被动中断，需要设置中断变量，**等 VM entry 时 KVM 会检查是否有中断请求，如果有，则将需要处理的中断信息写入 VMCS 中**。

- 中断评估

  即评估中断优先级。

- 中断注入

  VMCS 中有字段：`VM-entry interruption-information`，在 VM-entry 时 CPU 会检查这个字段。如果 CPU 正处在 guest 模式，则等待下一次 VM exit 和 VM entry；如果 VCPU 正在睡眠状态，则 kick。

#### APIC 虚拟化

APIC( Advanced Programmable Interrupt Controller)，其可以将接收到的中断按需分给不同的 processor 进行处理，而 PIC 只能应用于单核。

APIC 包含两个部分：`LAPIC` 和 `I/O APIC`， LAPIC 位于处理器一端，接收来自 I/O APIC 的中断和核间中断 IPI(Inter Processor Interrupt)；I/O APIC 一般位于南桥芯片，相应来自外部设备的中断，并将中断发送给 LAPIC。其中断过程和 PIC 类似。

- 核间中断过程

  当 guest 发送 IPI 时，虚拟 LAPIC 确定目的 VCPU，向目的 VCPU 发送 IPI，实际上是**向目的 VCPU 对应的虚拟 LAPIC 发送核间中断，再由目标虚拟 LAPIC 完成中断注入过程**。

#### MSI(X)虚拟化

不基于管脚，而是基于消息。中断信息从设备直接发送到 LAPIC，不用通过 I/O LAPIC。支持 MSI(X) 的设备绕过 I/O APIC，直接与 LAPIC 通过系统总线相连。

从 PCI 2.1 开始，如果设备需要扩展某种特性，可以向配置空间中的 capability list 中增加一个 capability，MSI(X) 利用这个特性，将 I/O APIC 中的功能扩展到设备自身。

**为了支持多个中断**，MSI-X 的 Capability Structure 在 MSI 的基础上增加了 table，其中 Table Offset 和 BIR(BAR Indicator Registor) 定义了 table 所在的位置，即指定使用哪个 BAR 寄存器（PCI 配置空间有 6 个 BAR 和 1 个 XROMBAR），然后从指定的这个 BAR 寄存器中取出 table 映射在 CPU 地址空间的基址，加上 Table Offset 就定位了 entry 的位置。类似的，`PBA BIR` 和 `PBA offset` 分别说明 MSIX- PBA 在哪个 BAR 中，在 BAR 中的什么位置。

![MSIX-capability.png](https://github.com/UtopianFuture/UtopianFuture.github.io/blob/master/image/MSIX-capability.png?raw=true)

##### MSI 设备中断过程

对于虚拟设备而言，如果启用了 MSI，中断过程就不需要虚拟 I/O APIC 参与了。虚拟设备直接从自身的 MSI(X) Capability Structure 中提取目的 CPU 等信息，向目的 CPU 关联的虚拟 LAPIC 发送中断请求，后续 LAPIC 的操作就和前面的虚拟 APIC 一样。

#### 硬件虚拟化支持

最初虚拟中断芯片是在用户空间实现的，后来为了减少内和空间和用户空间之间的上下文切换带来的开销，虚拟中断芯片实现在内核空间（哪个是这样实现的？），之后为了进一步提升效率，intel 提出了硬件辅助的虚拟化 VT-x，这里介绍一下硬件层面对虚拟化的支持。

在基于软件虚拟中断芯片中，只能在 VM entry 时向 guest 注入中断，必须触发一次 VM exit，这是中断虚拟化的主要开销。其实性能提升很多都是**想尽办法减少上下文切换的开销**。

- `virtual-APIC page`。

  LAPIC 中有一个 4KB 大小的页面，intel 称之为 APIC page，LAPIC 的所有寄存器都存在这个页面上。当内核访问这些寄存器时，将触发 guest 退出到 host 的 KVM 模块中的虚拟 LAPIC。intel 在 guest 模式下实现了一个用于存储中断寄存器的 `virtual-APIC page`。配置之后的中断逻辑处理，很多中断就无需 vmm 介入。但发送 IPI 还是需要触发 VM exit。通过 `virtual-APIC page` 维护寄存器的状态，guest 读取这些寄存器时无需切换状态，而写入时需要切换状态。其实这个很好理解，APIC page 是物理资源，non-root 态下的 guestos 想访问的话必须切换到 root 态，这个代价很大，所以设计一个 virtual APIC page，这个在 non-root 态下就能访问，其内容和 APIC page 是一样的，这样的话就不需要进行状态切换，性能提升。但是这个仅限于读操作，写操作的话因为需要改变寄存器的状态，而 virtual APIC page 仅仅是 APIC page 的一个拷贝，所以光写 virtual APIC page 是不够的，还是需要切换到 root 态访问 APIC page。

- guest 模式下的中断评估逻辑。

  在没有硬件层面的 guest 模式中的中断评估等逻辑支持时，每次中断注入必须发生在 vm entry。而如果当 vm entry 时 guestos 处于关中断状态下，那么 guestos 无法处理中断，中断无法注入，可能会导致 bug。

  所以为了让 guestos 能够快速处理中断，一旦 guestos 打开中断，并且没有执行不能中断的指令，CPU 应该马上从 non-root 态切换到 root 态，这样就能在下一次 vm entry 时处理中断。为此 intel 提供了一种特性：Interrupt-window exiting，即如果该位设置为 1，那么当 RFLAGS 中的 IF 位设置为 1，且没有执行不能中断的指令，那么 CPU 执行 vm exit。触发 vm exit 的时机与指令之间检查中断类似。

  这个思想也很简单，多加一位表示是否有中断，有中断来了就将这位置 1，等能够执行中断了就立刻区执行。

  但是到目前为止，处理中断还是需要切换到 root 态，那有没有办法不切换呢？接着往下看。

  如果 non-root 模式下的 CPU 能够进行中断评估（中断评估包括检查中断是否被屏蔽，多个中断请求的优先级等等），那么 non-root 模式下的 CPU 就能自动进行中断注入，无需触发 vm exit。

  guest 模式下的 CPU 借助 VMCS 中的字段 `guest interrupt status`（上面的 VMCS 中可以找到）评估中断。当 guest 开中断或者执行完不能中断的指令后，CPU 会检查这个字段是否有中断需要处理。（这个检查的过程是谁规定的？guest 模式下的 CPU 自动检查 VMCS）。

  所以当启用了 non-root 态下的 CPU 的中断评估支持后，KVM 在注入中断时，也需要适当修改，需要将注入的中断信息更新到字段 `guest interrupt status`。这样即使 vm entry 时不能处理中断，等到 non-root 可以处理中断时，也可直接处理记录在该字段的中断了，当然具体的中断信息还是存储在 virtual APIC page 中。

  还是同样的思想，不能访问关键资源我就构造一个 non-root 态可以访问的结构，当中断经过路由后发现需要注入虚拟机，那么就写入这个构造出来的字段。这即不会导致安全问题，也能提高效率。

- `posted-interrupt processing`。

  当 CPU 支持在 non-root 模式下的中断评估逻辑后，虚拟中断芯片可以在收到中断请求后，由 guest 模式下的中断评估逻辑评估后，将中断信息更新到 `posted-interrupt descriptor` 中，然后向处于 non-root 模式下的 CPU 发送 `posted-interrupt notification`（一个 IPI），向 guest 模式下的 CPU 直接递交中断。目的 CPU 收到该 IPI 后，将不再触发 vm exit，而是去处理虚拟中断芯片写在 `posted-interrupt descriptor` 的中断。

  ![post-interrupt.png](https://github.com/UtopianFuture/UtopianFuture.github.io/blob/master/image/post-interrupt.png?raw=true)

上图展示的外部中断在处于 non-root 态的 CPU0 上发起，但是目标 guestos 确运行于另一个 CPU 上的情况。外设的中断落在 CPU0 上，将导致 CPU0 切换到 root 态的 KVM，KVM 中的虚拟 LAPIC 将更新目标 CPU 的 `posted-interrupt descriptor`，然后通过 IPI 通知 CPU1 进行中断评估并处理中断，而 CPU1 无需进行 vm entry 和 vm exit。

上述技术大致流程是这样的，但是 KVM 中的具体实现还没有看，之后补上。

### 设备虚拟化

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

### 类虚拟化技术

#### 概述

#### 类虚拟化体系结构

### 前沿虚拟化技术

#### 基于容器的虚拟化技术

#### 系统安全

#### 系统标准化

#### 智能设备

##### 多队列网卡

##### SR-IOV
