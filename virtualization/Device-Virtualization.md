## Device Virtualization

设备虚拟化可以分为完全虚拟化和半虚拟化。完全虚拟化就是类似 QEMU 的设备模拟，完全用软件来做，通过 Inter-Virtualization 大致就懂了，这里不再介绍。这篇文章来分析设备透传和 Virtio 虚拟化。

### 设备透传

SR-IOV(Single Root I/O Virtualization and Sharing)，**在硬件层面将一个物理设备虚拟出多个设备，每个设备可以透传给一台虚拟机**。 SR-IOV 引入了两个新的 function 类型，一个是 PF(Physical Function)，一个是 VF(Virtual Function)。一个 SR-IOV 可以支持多个 VF，每个 VF 可以分别透传给 guest，guest 就不用通过 VMM 的模拟设备访问物理设备。每个 VF 都有自己独立的用于数据传输的存储空间、队列、中断以及命令处理单元，即虚拟的物理设备，**VMM 通过 PF 管理这些 VF**。同时，host 上的软件仍然可以通过 PF 访问物理设备。

#### 虚拟配置空间

guest 访问 VF 的数据不需要经过 VMM，guest 的 I/O 性能提高了，但出于安全考虑，guest 访问 VF 的**配置空间**时会触发 VM exit 陷入 VMM，这一过程不会影响数据传输效率（但是上下文切换不也降低性能么？注意是配置空间，不是数据）。

在系统启动时，**host 的 bios 为 VF 划分了内存地址空间并存储在寄存器 BAR 中**，而且 guest 可以直接读取这个信息，但是因为 guest 不能直接访问 host 的物理地址，所以 kvmtool 要将 VF 的 BAR 寄存器中的 HPA 转换为 GPA（初始化的工作），这样 guest 才可以直接访问。之后当 guest 发出对 BAR 对应的内存地址空间的访问时，EPT 或影子页表会将 GPA 翻译为 HPA，PCI 或 Root Complex 将认领这个地址，完成外设读取工作。

#### DMA 重映射

由于虚拟机在指定设备 DMA 的时候能够随意指定地址，所以需要有一种机制来**对设备 DMA 地址访问进行隔离**。换句话说，采用设备透传时，如果设备的 DMA 访问没有隔离，该设备就能够访问物理机上所有的地址空间，即**guest 能够访问该设备下其他 guest 和 host 的内存**，导致安全隐患。为此，设计了 DMA 重映射机制。

当 VMM 处理透传给 guest 的外设时，VMM 将请求 kernel 为 guest 建立一个页表，这个页表的翻译由 DMA 重映射硬件负责，**它限制了外设只能访问这个页面覆盖的内存**。当外设访问内存时，内存地址首先到达 DMA 重映射硬件，DMA 重映射硬件根据这个外设的总线号、设备号和功能号确定其对应的页表，查表的出物理内存地址，然后将地址送上总线。

![IOMMU](https://github.com/UtopianFuture/UtopianFuture.github.io/blob/master/image/IOMMU.png?raw=true)

从这幅图看一看出，DMA Remapping 也需要建立类似于 MMU 的页表来完成 DMA 的地址转换。

![DMA-Remapping](https://github.com/UtopianFuture/UtopianFuture.github.io/blob/master/image/DMA-Remapping.png?raw=true)

#### 中断重映射

为避免外设编程发送一些恶意的中断引入了中断虚拟化机制，即在外设和 CPU 之间加了一个硬件中断重映射单元(IOMMU)。当接收到中断时，该单元会对中断请求的来源进行有效性验证，然后以中断号为索引查询中断重映射表，之后代发中断。中断映射表由 VMM 进行设置。

### Virtio虚拟化

与完全虚拟化相比，使用 Virtio 协议的驱动和设备模拟的交互**不再使用寄存器等传统的 I/O 方式**，而是采用了 Virtqueue 的方式来传输数据。这种方式减少了 vm exit 和 vm entry 的次数，提高了设备访问性能。

#### 执行流程

下面以 virtio-blk 为例，简单描述一下 read request 从发出到读到数据的过程。

- VM 中，guest OS 的用户空间进程通过系统调用发出 read request，进程被挂起，等待数据（同步 read）。此时进入到内核空间，request 依次经过 vfs、page cache、具体 fs（通常是 ext4 或者 xfs）、buffer cache、generic block layer 后，来到了 virtio-blk 的驱动中。

- virtio-blk 驱动将 request 放入 virtio 的 vring 中的 avail ring 中，vring 是一个包含 2 个 ring buffer 的结构，一个 ring buffer 用于发送 request，称为 avail ring，另一个用于接收数据和资源回收，称为 used ring。在适当的时机，guest 会通知 host 有 request 需要处理，此动作称为 kick，在此之前会将 avail ring 中管理的 desc 链挂入 used ring 中，后续在 host 中可以通过 used ring 找到所有的 desc，每个 desc 用于管理一段 buffer。

- host 收到 kick 信号，就开始处理 avail ring 中的 request。主要是通过 avail ring 中的 index 去 desc 数组中找到真正的 request，建立起 desc 对应的 buffer 的内存映射，即建立 guest kernel 地址 GPA 与 qemu 进程空间地址 HVA 的映射，然后将 request 转发到 qemu 的设备模拟层中（此时没有内存的 copy），qemu 设备模拟层再把 request 经过合并排序形成新的 request 后，将新 request 转发给 host 的 linux IO 栈，也就是通过系统调用，进入 host 内核的 IO 栈，由内核把新 request 发给真实的外设（磁盘）。真实的外设收到新 request 后，将数据通过 DMA 的方式传输到 DDR 中 host 的内核空间，然后通过中断通知 host 内核 request 已经处理完成。host 的中断处理程序会将读到的数据 copy 到 qemu 的地址空间，然后通知 qemu 数据已经准备好了。qemu 再将数据放入 vring 的 used ring 管理的 desc 中（也即 avail ring 中的 desc，由 avail ring 挂入到了 used ring 中的），这里也没有数据的 copy，通过使用之前建立的 GPA 和 HVA 的映射完成。host 再通过虚拟中断通知 guest 数据已经就绪。4）guest 收到中断，中断处理程序依次处理如下：a）回收 desc 资源。存放读到的数据的 buffer 在不同的层有不同的描述方式，在 virtio 层中是通过 desc 来描述的，在 block 层中是通过 sglist 描述的。在 guest kernel 发出 read request 的时候，已经分别在 block 层和 virtio 层建立了 buffer 的描述方式，即在任何层可以都找到 buffer。具体请参考附件 pdf。所以此时回收 desc 不会导致 block 层找不到读到的数据。b）将数据 copy 到等待数据的用户进程（即发出 read request 的用户进程)。

至此，read request 流程结束。write request 与 read request 的处理流程相同。

![virtio](https://github.com/UtopianFuture/UtopianFuture.github.io/blob/master/image/virtio.png?raw=true)

#### Virtio协议

Virtio 的核心数据结构是 Virtqueue，其是 **guest 驱动和 VMM 中模拟设备之间传输数据的载体**。一个设备可以有一个 Virtqueue，也可以有多个 Virtqueue。Virtqueue 主要包含 3 个部分：描述符表（vring_desc）、可用描述符区域（vring_avail）和已用描述符表（vring_used）。

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

### 设备直通与 VFIO

### PCI设备模拟

#### PCI设备

使用 lspci 命令可以查看当前系统挂载在 PCI 总线上的设备：

```
00:00.0 Host bridge: Intel Corporation 8th Gen Core Processor Host Bridge/DRAM Registers (rev 07)
00:02.0 VGA compatible controller: Intel Corporation UHD Graphics 630 (Mobile)
00:04.0 Signal processing controller: Intel Corporation Xeon E3-1200 v5/E3-1500 v5/6th Gen Core Processor Thermal Subsystem (rev 07)
00:08.0 System peripheral: Intel Corporation Xeon E3-1200 v5/v6 / E3-1500 v5 / 6th/7th/8th Gen Core Processor Gaussian Mixture Model
00:12.0 Signal processing controller: Intel Corporation Cannon Lake PCH Thermal Controller (rev 10)
00:14.0 USB controller: Intel Corporation Cannon Lake PCH USB 3.1 xHCI Host Controller (rev 10)
00:14.2 RAM memory: Intel Corporation Cannon Lake PCH Shared SRAM (rev 10)
00:15.0 Serial bus controller [0c80]: Intel Corporation Cannon Lake PCH Serial IO I2C Controller #0 (rev 10)
00:15.1 Serial bus controller [0c80]: Intel Corporation Cannon Lake PCH Serial IO I2C Controller #1 (rev 10)
00:16.0 Communication controller: Intel Corporation Cannon Lake PCH HECI Controller (rev 10)
00:1b.0 PCI bridge: Intel Corporation Cannon Lake PCH PCI Express Root Port #17 (rev f0)
00:1d.0 PCI bridge: Intel Corporation Cannon Lake PCH PCI Express Root Port #9 (rev f0)
00:1d.4 PCI bridge: Intel Corporation Cannon Lake PCH PCI Express Root Port #13 (rev f0)
00:1d.5 PCI bridge: Intel Corporation Cannon Lake PCH PCI Express Root Port #14 (rev f0)
00:1f.0 ISA bridge: Intel Corporation HM470 Chipset LPC/eSPI Controller (rev 10)
00:1f.3 Audio device: Intel Corporation Cannon Lake PCH cAVS (rev 10)
00:1f.4 SMBus: Intel Corporation Cannon Lake PCH SMBus Controller (rev 10)
00:1f.5 Serial bus controller [0c80]: Intel Corporation Cannon Lake PCH SPI Controller (rev 10)
01:00.0 PCI bridge: Intel Corporation JHL7540 Thunderbolt 3 Bridge [Titan Ridge 4C 2018] (rev 06)
02:00.0 PCI bridge: Intel Corporation JHL7540 Thunderbolt 3 Bridge [Titan Ridge 4C 2018] (rev 06)
02:01.0 PCI bridge: Intel Corporation JHL7540 Thunderbolt 3 Bridge [Titan Ridge 4C 2018] (rev 06)
02:02.0 PCI bridge: Intel Corporation JHL7540 Thunderbolt 3 Bridge [Titan Ridge 4C 2018] (rev 06)
02:04.0 PCI bridge: Intel Corporation JHL7540 Thunderbolt 3 Bridge [Titan Ridge 4C 2018] (rev 06)
03:00.0 System peripheral: Intel Corporation JHL7540 Thunderbolt 3 NHI [Titan Ridge 4C 2018] (rev 06)
37:00.0 USB controller: Intel Corporation JHL7540 Thunderbolt 3 USB Controller [Titan Ridge 4C 2018] (rev 06)
6b:00.0 Non-Volatile memory controller: Samsung Electronics Co Ltd NVMe SSD Controller SM981/PM981/PM983
6c:00.0 Unassigned class [ff00]: Realtek Semiconductor Co., Ltd. RTS522A PCI Express Card Reader (rev 01)
6d:00.0 Network controller: Intel Corporation Wi-Fi 6 AX200 (rev 1a)
```

都是熟悉的设备啊！为什么有 5 个雷电 3 总线？

每个 PCI 设备在系统中的位置由总线号（Bus Number）、设备号（Device Number）和功能号（Function Number）唯一确定，上面的设备信息中前 3 组数字应该分别对应总线号、设备号、功能号。我们可以看到有不同的总线号，按理来说不都是挂载在 HOST-PCI 桥上，总线号应该是一样的。是这样的，可以在 PCI 总线上挂一个桥设备，之后在该桥上再挂载一个 PCI 总线或其他总线。比如我们看看 usb 设备的挂载情况：

```
Bus 004 Device 003: ID 0bda:8153 Realtek Semiconductor Corp. RTL8153 Gigabit Ethernet Adapter
Bus 004 Device 004: ID 0dd8:3d01 Netac Technology Co., Ltd USB3.0 Hub // 挂载一个 U 盘
Bus 004 Device 002: ID 2109:0817 VIA Labs, Inc. USB3.0 Hub
Bus 004 Device 001: ID 1d6b:0003 Linux Foundation 3.0 root hub
Bus 003 Device 001: ID 1d6b:0002 Linux Foundation 2.0 root hub
Bus 002 Device 001: ID 1d6b:0003 Linux Foundation 3.0 root hub
Bus 001 Device 007: ID 27c6:55a4 Shenzhen Goodix Technology Co.,Ltd. Goodix FingerPrint Device
Bus 001 Device 006: ID 13d3:56bb IMC Networks Integrated Camera
Bus 001 Device 005: ID 048d:c937 Integrated Technology Express, Inc. ITE Device(8910)
Bus 001 Device 004: ID 2109:2817 VIA Labs, Inc. USB2.0 Hub
Bus 001 Device 003: ID 1a81:1202 Holtek Semiconductor, Inc. wireless dongle
Bus 001 Device 008: ID 8087:0029 Intel Corp.
Bus 001 Device 002: ID 062a:5918 MosArt Semiconductor Corp. 2.4G Keyboard Mouse
Bus 001 Device 001: ID 1d6b:0002 Linux Foundation 2.0 root hub
```

这里的位置信息就更清晰了。哦，原来不是所有的雷电 3 接口都是我能看到的。但是 U 盘是挂载在 0x04 总线上，不是 PCI 总线？

![PCI-bus.png](https://github.com/UtopianFuture/UtopianFuture.github.io/blob/master/image/PCI-bus.png?raw=true)

PCI 设备有自己的地址空间，叫做 PCI 地址空间，HOST-PCI 桥完成 CPU 访问的内存地址到 PCI 总线地址的转换。每个 PCI 设备都有一个配置空间，该空间至少有 256 字节，前 64 字节是标准化的，所有的设备都是这个格式，后面的内容由设备自己决定。

![PCI-config-space](/home/guanshun/gitlab/UFuture.github.io/image/PCI-config-space.png)

Vendor ID, Device ID, Class Code 用来表明设备的身份，有时还会配置 Subsystem Vendor ID 和 Subsystem Device ID。6 个 Base Address 表示 PCI 设备的 I/O 地址空间（这么大么），还可能有一个 ROM 的 BAR。两个与中断设置相关的域，IRQ Line 表示该设备使用哪个中断号（BIOS 中注册的 IVT），IRQ Line 表示使用哪条引脚连接中断控制器，PCI 总线上可以通过 4 根中断引脚 INTA ~ D# 向中断控制器提交中断请求（不懂）。

#### PCI设备的模拟

老规矩，先看看数据结构。在我看来，虚拟化无非就是定义设备对应的数据结构，初始化它，然后完成对应的操作函数。

QEMU 用 `PCIDevice` 来完成 PCI 设备的虚拟化。在分析的时候如果觉得 QOM 过于复杂可以忽视它，也不影响对某个模块的模拟，当然最好的方式是[理解它](https://github.com/UtopianFuture/UtopianFuture.github.io/blob/master/virtualization/QEMU-qom.md)，战胜它。

##### PCIDevice

```c
struct PCIDevice {
    DeviceState qdev;
    bool partially_hotplugged;
    bool has_power;

    /* PCI config space */
    uint8_t *config;

    /* Used to enable config checks on load. Note that writable bits are
     * never checked even if set in cmask. */
    uint8_t *cmask; // 用来检测相关功能

    /* Used to implement R/W bytes */
    uint8_t *wmask;

    /* Used to implement RW1C(Write 1 to Clear) bytes */
    uint8_t *w1cmask;

    /* Used to allocate config space for capabilities. */
    uint8_t *used;

    /* the following fields are read only */
    int32_t devfn; // 这个是干嘛的，功能号么
    /* Cached device to fetch requester ID from, to avoid the PCI
     * tree walking every time we invoke PCI request (e.g.,
     * MSI). For conventional PCI root complex, this field is
     * meaningless. */
    PCIReqIDCache requester_id_cache;
    char name[64];
    PCIIORegion io_regions[PCI_NUM_REGIONS];
    AddressSpace bus_master_as;
    MemoryRegion bus_master_container_region;
    MemoryRegion bus_master_enable_region;

    /* do not access the following fields */
    PCIConfigReadFunc *config_read;
    PCIConfigWriteFunc *config_write;

    /* Legacy PCI VGA regions */
    MemoryRegion *vga_regions[QEMU_PCI_VGA_NUM_REGIONS];
    bool has_vga;

    /* Current IRQ levels.  Used internally by the generic PCI code.  */
    uint8_t irq_state;

    /* Capability bits */
    uint32_t cap_present;

    /* Offset of MSI-X capability in config space */
    uint8_t msix_cap;

    /* MSI-X entries */
    int msix_entries_nr;

    /* Space to store MSIX table & pending bit array */
    uint8_t *msix_table;
    uint8_t *msix_pba;
    /* MemoryRegion container for msix exclusive BAR setup */
    MemoryRegion msix_exclusive_bar;
    /* Memory Regions for MSIX table and pending bit entries. */
    MemoryRegion msix_table_mmio;
    MemoryRegion msix_pba_mmio;
    /* Reference-count for entries actually in use by driver. */
    unsigned *msix_entry_used;
    /* MSIX function mask set or MSIX disabled */
    bool msix_function_masked;
    /* Version id needed for VMState */
    int32_t version_id; // 不是 PCI 配置空间中的域，应该是为了支持虚拟化加的

    /* Offset of MSI capability in config space */
    uint8_t msi_cap;

    /* PCI Express */
    PCIExpressDevice exp;

    /* SHPC */
    SHPCDevice *shpc;

    /* Location of option rom */
    char *romfile;
    uint32_t romsize;
    bool has_rom;
    MemoryRegion rom;
    uint32_t rom_bar;

    /* INTx routing notifier */
    PCIINTxRoutingNotifier intx_routing_notifier;

    /* MSI-X notifiers */
    MSIVectorUseNotifier msix_vector_use_notifier;
    MSIVectorReleaseNotifier msix_vector_release_notifier;
    MSIVectorPollNotifier msix_vector_poll_notifier;

    /* ID of standby device in net_failover pair */
    char *failover_pair_id;
    uint32_t acpi_index;
};
```

##### 关键函数pci_qdev_realize

这个函数完成 PCI 设备的初始化，可以在这个函数中设置断点，就会发现上文中出现的设备会一一调用它进行初始化。下面给出南桥芯片的初始化，从上文的图中可以直到南桥芯片也是挂载在 PCI 根总线上的一个 PCI 设备。

```c
static void pci_qdev_realize(DeviceState *qdev, Error **errp)
{
    PCIDevice *pci_dev = (PCIDevice *)qdev;
    PCIDeviceClass *pc = PCI_DEVICE_GET_CLASS(pci_dev);
    ObjectClass *klass = OBJECT_CLASS(pc);
    Error *local_err = NULL;
    bool is_default_rom;
    uint16_t class_id;

    ...

    // 这个应该是初始化前 64 字节的，在后面分析
    pci_dev = do_pci_register_device(pci_dev,
                                     object_get_typename(OBJECT(qdev)),
                                     pci_dev->devfn, errp);

    // 而这具体的设备的回调函数，应该是用于初始化 64 字节后的数据
    if (pc->realize) {
        pc->realize(pci_dev, &local_err);
        if (local_err) {
            error_propagate(errp, local_err);
            do_pci_unregister_device(pci_dev);
            return;
        }
    }

	...

    /* rom loading */
    // 有些 PCI 设备有 ROM 存储设备（？）
    is_default_rom = false;
    // QEMU 命令行没有指定 ROM，使用 PCI 设备默认的
    if (pci_dev->romfile == NULL && pc->romfile != NULL) {
        pci_dev->romfile = g_strdup(pc->romfile);
        is_default_rom = true;
    }

    pci_add_option_rom(pci_dev, is_default_rom, &local_err); // 这里会调用 pci_register_bar，继续初始化

    ...

    pci_set_power(pci_dev, true);
}
```

```plain
#0  pci_qdev_realize (qdev=0x555556f67c60, errp=0x7fffffffd550) at ../hw/pci/pci.c:2115
// 这一系列的调用都是 QOM 的实现，可以跳过它
#1  0x0000555555d3d678 in device_set_realized (obj=0x555556f67c60, value=true,
    errp=0x7fffffffd660) at ../hw/core/qdev.c:531
#2  0x0000555555d47703 in property_set_bool (obj=0x555556f67c60, v=0x555556f68870,
    name=0x5555560bc819 "realized", opaque=0x55555681ef50, errp=0x7fffffffd660)
    at ../qom/object.c:2268
#3  0x0000555555d4566e in object_property_set (obj=0x555556f67c60,
    name=0x5555560bc819 "realized", v=0x555556f68870, errp=0x7fffffffd660)
    at ../qom/object.c:1403
#4  0x0000555555d49bd4 in object_property_set_qobject (obj=0x555556f67c60,
    name=0x5555560bc819 "realized", value=0x555556f68780, errp=0x55555677c100 <error_fatal>)
    at ../qom/qom-qobject.c:28
#5  0x0000555555d459e9 in object_property_set_bool (obj=0x555556f67c60,
    name=0x5555560bc819 "realized", value=true, errp=0x55555677c100 <error_fatal>)
    at ../qom/object.c:1472
#6  0x0000555555d3cf0c in qdev_realize (dev=0x555556f67c60, bus=0x555556bc9df0,
    errp=0x55555677c100 <error_fatal>) at ../hw/core/qdev.c:333
#7  0x0000555555d3cf3d in qdev_realize_and_unref (dev=0x555556f67c60, bus=0x555556bc9df0,
    errp=0x55555677c100 <error_fatal>) at ../hw/core/qdev.c:340
#8  0x0000555555a37189 in pci_realize_and_unref (dev=0x555556f67c60, bus=0x555556bc9df0,
    errp=0x55555677c100 <error_fatal>) at ../hw/pci/pci.c:2210
#9  0x0000555555a371d9 in pci_create_simple_multifunction (bus=0x555556bc9df0, devfn=-1,
    multifunction=true, name=0x555555fdf04b "PIIX3") at ../hw/pci/pci.c:2218
// piix3 就是南桥芯片了，负责连接 ISA 等低速设备
#10 0x00005555559b79b8 in piix3_create (pci_bus=0x555556bc9df0, isa_bus=0x7fffffffd880)
    at ../hw/isa/piix3.c:385
#11 0x0000555555b442d1 in pc_init1 (machine=0x555556a2f000,
    host_type=0x555556061704 "i440FX-pcihost", pci_type=0x5555560616fd "i440FX")
    at ../hw/i386/pc_piix.c:209
#12 0x0000555555b44b1d in pc_init_v6_2 (machine=0x555556a2f000) at ../hw/i386/pc_piix.c:425
#13 0x000055555594b889 in machine_run_board_init (machine=0x555556a2f000)
    at ../hw/core/machine.c:1181
#14 0x0000555555c08082 in qemu_init_board () at ../softmmu/vl.c:2652
#15 0x0000555555c082ad in qmp_x_exit_preconfig (errp=0x55555677c100 <error_fatal>)
    at ../softmmu/vl.c:2740
#16 0x0000555555c0a936 in qemu_init (argc=15, argv=0x7fffffffdcb8, envp=0x7fffffffdd38)
    at ../softmmu/vl.c:3775
#17 0x000055555583b6f5 in main (argc=15, argv=0x7fffffffdcb8, envp=0x7fffffffdd38)
    at ../softmmu/main.c:49
```

##### 关键函数do_pci_register_device

该函数完成设备及其对应 PCI 总线上的一些初始化工作。

```c
/* -1 for devfn means auto assign */
static PCIDevice *do_pci_register_device(PCIDevice *pci_dev,
                                         const char *name, int devfn,
                                         Error **errp)
{
    PCIDeviceClass *pc = PCI_DEVICE_GET_CLASS(pci_dev);
    PCIConfigReadFunc *config_read = pc->config_read; // 不同设备对应的回调函数
    PCIConfigWriteFunc *config_write = pc->config_write;
    Error *local_err = NULL;
    DeviceState *dev = DEVICE(pci_dev);
    PCIBus *bus = pci_get_bus(pci_dev);

    ...

    // 如果指定的 devfn 为 -1，表示由总线自己选择插槽（？）
    // 得到插槽号后保存在 PCIDevice->devfn 中，如果在设备命令行中指定了 addr
    // 则 addr 会作为设备的 devfn。
    if (devfn < 0) {
        for(devfn = bus->devfn_min ; devfn < ARRAY_SIZE(bus->devices);
            devfn += PCI_FUNC_MAX) {
            if (pci_bus_devfn_available(bus, devfn) &&
                   !pci_bus_devfn_reserved(bus, devfn)) {
                goto found;
            }
        }
        error_setg(errp, "PCI: no slot/function available for %s, all in use "
                   "or reserved", name);
        return NULL;
    found: ;
    }

    ...

    pci_dev->devfn = devfn;
    pci_dev->requester_id_cache = pci_req_id_cache_get(pci_dev);
    pstrcpy(pci_dev->name, sizeof(pci_dev->name), name);

    memory_region_init(&pci_dev->bus_master_container_region, OBJECT(pci_dev),
                       "bus master container", UINT64_MAX);
    address_space_init(&pci_dev->bus_master_as,
                       &pci_dev->bus_master_container_region, pci_dev->name);

    if (phase_check(PHASE_MACHINE_READY)) {
        pci_init_bus_master(pci_dev);
    }
    pci_dev->irq_state = 0;
    // 使用 g_malloc0 为 config, cmask, wmask, w1cmask, used 分配空间
    pci_config_alloc(pci_dev);

    // 这部分就很好理解了
    pci_config_set_vendor_id(pci_dev->config, pc->vendor_id);
    pci_config_set_device_id(pci_dev->config, pc->device_id);
    pci_config_set_revision(pci_dev->config, pc->revision);
    pci_config_set_class(pci_dev->config, pc->class_id);

    if (!pc->is_bridge) {
        if (pc->subsystem_vendor_id || pc->subsystem_id) { // 需要设置这两个域
            pci_set_word(pci_dev->config + PCI_SUBSYSTEM_VENDOR_ID,
                         pc->subsystem_vendor_id); // 通过写对应的位来配置
            pci_set_word(pci_dev->config + PCI_SUBSYSTEM_ID,
                         pc->subsystem_id);
        } else {
            pci_set_default_subsystem_id(pci_dev); // 默认配置
        }
    } else {
        /* subsystem_vendor_id/subsystem_id are only for header type 0 */
        assert(!pc->subsystem_vendor_id);
        assert(!pc->subsystem_id);
    }
    // 具体的配置，应该跟架构相关，这里先不关注
    pci_init_cmask(pci_dev);
    pci_init_wmask(pci_dev);
    pci_init_w1cmask(pci_dev);
    if (pc->is_bridge) {
        pci_init_mask_bridge(pci_dev);
    }
    // 这个是干嘛的
    pci_init_multifunction(bus, pci_dev, &local_err);

    ...

    // 这些都很好理解
    if (!config_read)
        config_read = pci_default_read_config;
    if (!config_write)
        config_write = pci_default_write_config;
    pci_dev->config_read = config_read;
    pci_dev->config_write = config_write;
    bus->devices[devfn] = pci_dev; // 这样 bus 就可以找到对应的设备
    pci_dev->version_id = 2; /* Current pci device vmstate version */
    return pci_dev;
}
```

##### 关键函数piix3_realize

这里还是拿南桥芯片举例，其对应的回调函数是 `piix3_realize`。

```c
static void piix3_realize(PCIDevice *dev, Error **errp)
{
    PIIX3State *d = PIIX3_PCI_DEVICE(dev);

    if (!isa_bus_new(DEVICE(d), get_system_memory(),
                     pci_address_space_io(dev), errp)) {
        return;
    }

    // 初始化该设备的 MMIO 空间，1 为 1M 空间
    memory_region_init_io(&d->rcr_mem, OBJECT(dev), &rcr_ops, d,
                          "piix3-reset-control", 1);
    memory_region_add_subregion_overlap(pci_address_space_io(dev),
                                        PIIX_RCR_IOPORT, &d->rcr_mem, 1);

    qemu_register_reset(piix3_reset, d);
}
```

`piix3_reset` 用硬编码的方式设置 64 字节后的数据。

##### 关键函数pci_register_bar

`pci_qdev_realize` -> `pci_add_option_rom` -> `pci_register_bar`

继续配置 PCI 设备空间。

```c
void pci_register_bar(PCIDevice *pci_dev, int region_num,
                      uint8_t type, MemoryRegion *memory)
{
    PCIIORegion *r;
    uint32_t addr; /* offset in pci config space */
    uint64_t wmask;
    pcibus_t size = memory_region_size(memory);
    uint8_t hdr_type;

    assert(region_num >= 0);
    assert(region_num < PCI_NUM_REGIONS);
    assert(is_power_of_2(size));

    /* A PCI bridge device (with Type 1 header) may only have at most 2 BARs */
    hdr_type =
        pci_dev->config[PCI_HEADER_TYPE] & ~PCI_HEADER_TYPE_MULTI_FUNCTION;
    assert(hdr_type != PCI_HEADER_TYPE_BRIDGE || region_num < 2);

    r = &pci_dev->io_regions[region_num];
    r->addr = PCI_BAR_UNMAPPED;
    r->size = size;
    r->type = type;
    r->memory = memory;
    r->address_space = type & PCI_BASE_ADDRESS_SPACE_IO
                        ? pci_get_bus(pci_dev)->address_space_io
                        : pci_get_bus(pci_dev)->address_space_mem;

    wmask = ~(size - 1);
    if (region_num == PCI_ROM_SLOT) {
        /* ROM enable bit is writable */
        wmask |= PCI_ROM_ADDRESS_ENABLE;
    }

    addr = pci_bar(pci_dev, region_num);
    pci_set_long(pci_dev->config + addr, type);

    if (!(r->type & PCI_BASE_ADDRESS_SPACE_IO) &&
        r->type & PCI_BASE_ADDRESS_MEM_TYPE_64) {
        pci_set_quad(pci_dev->wmask + addr, wmask);
        pci_set_quad(pci_dev->cmask + addr, ~0ULL);
    } else {
        pci_set_long(pci_dev->wmask + addr, wmask & 0xffffffff);
        pci_set_long(pci_dev->cmask + addr, 0xffffffff);
    }
}
```

##### 关键函数piix3_write_config

接下来我们看看南桥的读写回调函数。

好吧，南桥芯片的读写和我想的不一样，它只有一个写函数，

```c
static void piix3_write_config(PCIDevice *dev,
                               uint32_t address, uint32_t val, int len)
{
    pci_default_write_config(dev, address, val, len);
    if (ranges_overlap(address, len, PIIX_PIRQCA, 4)) {
        PIIX3State *piix3 = PIIX3_PCI_DEVICE(dev);
        int pic_irq;

        pci_bus_fire_intx_routing_notifier(pci_get_bus(&piix3->dev));
        piix3_update_irq_levels(piix3);
        for (pic_irq = 0; pic_irq < PIIX_NUM_PIC_IRQS; pic_irq++) {
            piix3_set_irq_pic(piix3, pic_irq);
        }
    }
}
```

并且使用了 PCI 设备默认的读写函数，并且使用 msi 机制进行读写，这个之后再分析。

```c
void pci_default_write_config(PCIDevice *d, uint32_t addr, uint32_t val_in, int l)
{
    int i, was_irq_disabled = pci_irq_disabled(d);
    uint32_t val = val_in;

    assert(addr + l <= pci_config_size(d));

    for (i = 0; i < l; val >>= 8, ++i) {
        uint8_t wmask = d->wmask[addr + i];
        uint8_t w1cmask = d->w1cmask[addr + i];
        assert(!(wmask & w1cmask));
        d->config[addr + i] = (d->config[addr + i] & ~wmask) | (val & wmask);
        d->config[addr + i] &= ~(val & w1cmask); /* W1C: Write 1 to Clear */
    }

    ...

    msi_write_config(d, addr, val_in, l);
    msix_write_config(d, addr, val_in, l);
}
```

这就是一个普通的 PCI 设备的初始化和访问过程。

在[PCI设备](#PCI设备)我们讲到 HOST-PCI 桥完成 CPU 访问的内存地址到 PCI 总线地址的转换，接下来我们分析一下 HOST-PCI 是怎样初始化和访问的。

##### PCIHostState

```c
struct PCIHostState {
    SysBusDevice busdev;

    MemoryRegion conf_mem; // 配置地址寄存器
    MemoryRegion data_mem; // 配置数据寄存器
    MemoryRegion mmcfg;
    uint32_t config_reg;
    bool mig_enabled;
    PCIBus *bus;
    bool bypass_iommu;

    QLIST_ENTRY(PCIHostState) next;
};
```

我们知道 CPU 是通过读写地址寄存器和数据寄存器来访问 PCI 设备的，而地址寄存器对应的物理地址就是 `0xcf8`，数据寄存器是 `0xcfc`，这两个寄存器分别是 `PCIHostState -> conf_mem` 和 `PCIHostState -> data_mem`，它们在 `i440fx_pcihost_initfn` 中初始化。

```c
static void i440fx_pcihost_initfn(Object *obj)
{
    PCIHostState *s = PCI_HOST_BRIDGE(obj);

    memory_region_init_io(&s->conf_mem, obj, &pci_host_conf_le_ops, s,
                          "pci-conf-idx", 4); // 这里只是创建一个 memory region 并指定回调函数
    memory_region_init_io(&s->data_mem, obj, &pci_host_data_le_ops, s,
                          "pci-conf-data", 4);
}
```

```c
static void i440fx_pcihost_realize(DeviceState *dev, Error **errp)
{
    PCIHostState *s = PCI_HOST_BRIDGE(dev);
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);

    sysbus_add_io(sbd, 0xcf8, &s->conf_mem); // 将 conf_mem 和 0xcf8 关联起来。这种设计！！！需要学习
    sysbus_init_ioports(sbd, 0xcf8, 4);

    // 当然，这里整个实现和 QEMU 的 memory region 强相关，BMBT 可以不这样实现
    sysbus_add_io(sbd, 0xcfc, &s->data_mem);
    sysbus_init_ioports(sbd, 0xcfc, 4);

    /* register i440fx 0xcf8 port as coalesced pio */
    memory_region_set_flush_coalesced(&s->data_mem); // 每次更改内存结构都需要 commit
    memory_region_add_coalescing(&s->conf_mem, 0, 4);
}
```

我们看一下调用栈，在 `i440fx` 初始化的时候就需要完成 `PCI-HOST` 初始化，这个应该和 `i440fx` 芯片组的结构有关。

```
#0  i440fx_pcihost_initfn (obj=0x555556bc8d00) at ../hw/pci-host/i440fx.c:207
#1  0x0000555555d431fe in object_init_with_type (obj=0x555556bc8d00, ti=0x5555567d9880)
    at ../qom/object.c:376
#2  0x0000555555d43751 in object_initialize_with_type (obj=0x555556bc8d00, size=1648,
    type=0x5555567d9880) at ../qom/object.c:518
#3  0x0000555555d43e83 in object_new_with_type (type=0x5555567d9880) at ../qom/object.c:733
#4  0x0000555555d43ee2 in object_new (typename=0x555556061704 "i440FX-pcihost")
    at ../qom/object.c:748
#5  0x0000555555d3c86f in qdev_new (name=0x555556061704 "i440FX-pcihost")
    at ../hw/core/qdev.c:153
#6  0x0000555555a4a5f4 in i440fx_init (host_type=0x555556061704 "i440FX-pcihost",
    pci_type=0x5555560616fd "i440FX", pi440fx_state=0x7fffffffd9f8,
    address_space_mem=0x5555568220c0, address_space_io=0x555556821fb0, ram_size=8589934592,
    below_4g_mem_size=3221225472, above_4g_mem_size=5368709120,
    pci_address_space=0x555556a5a030, ram_memory=0x5555569a2af0)
    at ../hw/pci-host/i440fx.c:258
#7  0x0000555555b44298 in pc_init1 (machine=0x555556a2f000,
    host_type=0x555556061704 "i440FX-pcihost", pci_type=0x5555560616fd "i440FX")
    at ../hw/i386/pc_piix.c:200
#8  0x0000555555b44b1d in pc_init_v6_2 (machine=0x555556a2f000) at ../hw/i386/pc_piix.c:425
#9  0x000055555594b889 in machine_run_board_init (machine=0x555556a2f000)
    at ../hw/core/machine.c:1181
#10 0x0000555555c08082 in qemu_init_board () at ../softmmu/vl.c:2652
#11 0x0000555555c082ad in qmp_x_exit_preconfig (errp=0x55555677c100 <error_fatal>)
    at ../softmmu/vl.c:2740
#12 0x0000555555c0a936 in qemu_init (argc=15, argv=0x7fffffffde28, envp=0x7fffffffdea8)
    at ../softmmu/vl.c:3775
#13 0x000055555583b6f5 in main (argc=15, argv=0x7fffffffde28, envp=0x7fffffffdea8)
    at ../softmmu/main.c:49
```

也就是说这两段地址空间其实就是北桥芯片的一部分，就像 PAM 技术一样，将前 1M 地址空间声明为 rom 空间，这里将这两段地址空间声明为 PCI 寄存器。同时指定了回调函数，

```c
const MemoryRegionOps pci_host_conf_le_ops = {
    .read = pci_host_config_read,
    .write = pci_host_config_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

const MemoryRegionOps pci_host_data_le_ops = {
    .read = pci_host_data_read,
    .write = pci_host_data_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
};
```

##### PCI设备读写

读写的原理相同，这里只看看写过程。`pci_host_config_write` 将 guest 需要访问的 PCI 设备地址保存在 `PCIHostState -> config_reg` 中。

```c
static void pci_host_config_write(void *opaque, hwaddr addr,
                                  uint64_t val, unsigned len)
{
    PCIHostState *s = opaque;

    PCI_DPRINTF("%s addr " TARGET_FMT_plx " len %d val %"PRIx64"\n",
                __func__, addr, len, val);
    if (addr != 0 || len != 4) {
        return;
    }
    s->config_reg = val;
}
```

配置地址寄存器的访问必须是 4 字节的。通过 [intel 手册](https://wiki.qemu.org/images/b/bb/29054901.pdf)知道其第 31 位表示是否使能 PCI 设备的配置功能，如果想要读写 PCI 设备的配置空间，需要将该位置为 1，24 ~30 位为保留位，16 ~ 23 位表示 PCI 总线号，11 ~ 15 位表示该总线上的设备号，8 ~ 10 表示对应设备的功能号，2 ~ 7 位表示需要写入对应设备配置空间寄存器的值，0 ~ 1 位为保留位。

```c
static void pci_host_data_write(void *opaque, hwaddr addr,
                                uint64_t val, unsigned len)
{
    PCIHostState *s = opaque;

    if (s->config_reg & (1u << 31)) // 首先判断是否可以读写配置空间
        pci_data_write(s->bus, s->config_reg | (addr & 3), val, len);
}
```

```c
void pci_data_write(PCIBus *s, uint32_t addr, uint32_t val, unsigned len)
{
    PCIDevice *pci_dev = pci_dev_find_by_addr(s, addr); // 找到需要访问的 PCI 设备
    uint32_t config_addr = addr & (PCI_CONFIG_SPACE_SIZE - 1);

    pci_host_config_write_common(pci_dev, config_addr, PCI_CONFIG_SPACE_SIZE,
                                 val, len); // 读写该设备的配置空间
}
```

```c
void pci_host_config_write_common(PCIDevice *pci_dev, uint32_t addr,
                                  uint32_t limit, uint32_t val, uint32_t len)
{
    pci_adjust_config_limit(pci_get_bus(pci_dev), &limit);

    ...

    // 不同设备自己的读写回调函数，如 i440fx 就是 i440fx_write_config
    // i440fx 和 piix3 一样，都是使用 pci_default_write_config
    pci_dev->config_write(pci_dev, addr, val, MIN(len, limit - addr));
}
```

我们看看整个写的过程，

```
#0  pci_host_config_write (opaque=0x555556bc8d00, addr=0, val=2147483648, len=4)
    at ../hw/pci/pci_host.c:142
#1  0x0000555555bf49bb in memory_region_write_accessor (mr=0x555556bc9020, addr=0,
    value=0x7ffff228f3f8, size=4, shift=0, mask=4294967295, attrs=...)
    at ../softmmu/memory.c:492
#2  0x0000555555bf4c09 in access_with_adjusted_size (addr=0, value=0x7ffff228f3f8, size=4,
    access_size_min=1, access_size_max=4,
    access_fn=0x555555bf48c1 <memory_region_write_accessor>, mr=0x555556bc9020, attrs=...)
    at ../softmmu/memory.c:554
#3  0x0000555555bf7d07 in memory_region_dispatch_write (mr=0x555556bc9020, addr=0,
    data=2147483648, op=MO_32, attrs=...) at ../softmmu/memory.c:1504
#4  0x0000555555beaa5c in flatview_write_continue (fv=0x5555578e0580, addr=3320, attrs=...,
    ptr=0x7ffff7fc7000, len=4, addr1=0, l=4, mr=0x555556bc9020) at ../softmmu/physmem.c:2782
#5  0x0000555555beaba5 in flatview_write (fv=0x5555578e0580, addr=3320, attrs=...,
    buf=0x7ffff7fc7000, len=4) at ../softmmu/physmem.c:2822
#6  0x0000555555beaf1f in address_space_write (as=0x55555675cda0 <address_space_io>,
    addr=3320, attrs=..., buf=0x7ffff7fc7000, len=4) at ../softmmu/physmem.c:2914
#7  0x0000555555beaf90 in address_space_rw (as=0x55555675cda0 <address_space_io>, addr=3320,
    attrs=..., buf=0x7ffff7fc7000, len=4, is_write=true) at ../softmmu/physmem.c:2924
#8  0x0000555555d0b58e in kvm_handle_io (port=3320, attrs=..., data=0x7ffff7fc7000,
    direction=1, size=4, count=1) at ../accel/kvm/kvm-all.c:2642
#9  0x0000555555d0bd3d in kvm_cpu_exec (cpu=0x555556a4e730) at ../accel/kvm/kvm-all.c:2893
#10 0x0000555555d0dc13 in kvm_vcpu_thread_fn (arg=0x555556a4e730)
    at ../accel/kvm/kvm-accel-ops.c:49
#11 0x0000555555ef8857 in qemu_thread_start (args=0x555556a5c920)
    at ../util/qemu-thread-posix.c:556
#12 0x00007ffff6753609 in start_thread () from /lib/x86_64-linux-gnu/libpthread.so.0
#13 0x00007ffff6678163 in clone () from /lib/x86_64-linux-gnu/libc.so.6
```



#### PCI设备中断模拟

### 网卡模拟

### reference

[1] https://www.cxybb.com/article/gong0791/79578316

[2] QEMU/KVM 源码解析与应用 李强 机械工业出版社
