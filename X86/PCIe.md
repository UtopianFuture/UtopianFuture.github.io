## PCI设备

### 直观感受

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

都是熟悉的设备啊！

- 为什么有 5 个雷电 3 总线？

- `0x00` 号总线到底是低速总线还是高速总线啊？为什么即有 RAM memory 这个高速内存挂载，又有 PCH 这个低速设备集成芯片挂载，还有 serial。奇怪。

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

这里的位置信息就更清晰了。哦，原来不是所有的雷电 3 接口都是我能看到的。但是 U 盘是挂载在 `0x04` 总线上，不是 PCI 总线？`0x04` 总线是什么？

### 架构

![PCI-bus.png](https://github.com/UtopianFuture/UtopianFuture.github.io/blob/master/image/PCI-bus.png?raw=true)

### PCI device 配置空间

PCI 设备有自己的地址空间，叫做 PCI 地址空间，HOST-PCI 桥完成 CPU 访问的内存地址到 PCI 总线地址的转换。每个 PCI 设备都有一个配置空间，该空间至少有 256 字节，前 64 字节是标准化的，所有的设备都是这个格式，叫做 *PCI configuration register header*，后面的内容由设备自己决定，叫做 *device-specific PCI configuration register*。如上图所示，PCI 设备可以分成 PCI device 和 PCI bus，它们的配置空间不完全一样。下面是 PCI device 的配置空间的前 64 字节。

![PCI-DEVICE-config-space.png](https://github.com/UtopianFuture/UtopianFuture.github.io/blob/master/image/PCI-DEVICE-config-space.png?raw=true)

Vendor ID, Device ID, Class Code 用来表明设备的身份，有时还会配置 Subsystem Vendor ID 和 Subsystem Device ID。6 个 Base Address 表示 PCI 设备的 I/O 地址空间（这么大么），还可能有一个 ROM 的 BAR。两个与中断设置相关的域，IRQ Line 表示该设备使用哪个中断号（BIOS 中注册的 IVT），如传统的 8259 中断控制器，有 0 ~ 15 号 line，IRQ Line 表示的是用哪根线。而 IRQ Pin 表示使用哪个引脚连接中断控制器，PCI 总线上的设备可以通过 4 根中断引脚 INTA ~ D# 向中断控制器提交中断请求。

### BAR(base address register)

关于 BAR 只需要记住一句话：将 PCI 设备上的 RAM 存储设备映射到系统内存表中，XROMBAR 映射 PCI 设备上的扩展 ROM 存储设备到系统内存表中。

```
=== PCI bus & bridge init ===
PCI: pci_bios_init_bus_rec bus = 0x0
=== PCI device probing ===
Found 4 PCI devices (max PCI bus is 00)
=== PCI new allocation pass #1 ===
PCI: check devices
=== PCI new allocation pass #2 ===
// 下面这些由 BAR 实现的不同设备映射到系统内存表中的区间
PCI: IO: c000 - c01f
PCI: 32: 0000000080000000 - 00000000fec00000
PCI: map device bdf=00:01.0  bar 0, addr 0000c000, size 00000020 [io]
PCI: map device bdf=00:01.0  bar 1, addr febfe000, size 00001000 [mem]
PCI: map device bdf=00:02.0  bar 1, addr febff000, size 00001000 [mem]
PCI: map device bdf=00:02.0  bar 0, addr fc000000, size 02000000 [prefmem]
PCI: map device bdf=00:01.0  bar 4, addr fe000000, size 00004000 [prefmem]
PCI: init bdf=00:00.0 id=8086:1237
disable i440FX memory mappings
PCI: init bdf=00:01.0 id=1af4:1000
PCI: init bdf=00:02.0 id=1013:00b8
PCI: init bdf=00:1f.0 id=8086:7000
PIIX3/PIIX4 init: elcr=00 0c
disable PIIX3 memory mappings
PCI: Using 00:02.0 for primary VGA
```

系统固件负责初始化所有 BAR 的初始值，网上有详细的资料，这里就不花时间重复介绍，感兴趣的可以看这篇文章的[PCI bus base address registers initialization](https://resources.infosecinstitute.com/topic/system-address-map-initialization-in-x86x64-architecture-part-1-pci-based-systems/)部分。

### PCI bus 配置空间

下面是 PCI bus 的配置空间的前 64 字节。

![PCI-BUS-config-space.png](https://github.com/UtopianFuture/UtopianFuture.github.io/blob/master/image/PCI-BUS-config-space.png?raw=true)

> Registers marked with yellow determine **the memory and IO range** forwarded by the PCI-to-PCI bridge from its primary interface (the interface closer to the CPU) to its secondary interface (the interface farther away from the CPU). Registers marked with green determine the PCI bus number of the bus in the PCI-to-PCI bridge primary interface (Primary Bus Number), the PCI bus number of the PCI bus in its secondary interface (Secondary Bus Number) and the highest PCI bus number downstream of the PCI-to-PCI bridge (Subordinate Bus Number).

注意，PCI bus 不单能够从上向下转发，也能从下向上转发。DMA 就需要其从下向上转发。

#### 访问配置空间

This Configuration Address Space is accessible via:

- **Configuration Access Mechanism (CAM)** – PCI legacy method using I/O ports 0xCF8 (32-bit configuration address to access) and 0xCFC (data to and from the access). This method only supports 256 buses, each with up to 32 devices, each supporting 8 functions, and assumes use of PCI Segment 0. Either do it in x86 using the OUT instruction or use compiler intrinsic __outbyte(port, data).
- **Enhanced Configuration Access Mechanism (ECAM)** – PCIe supports access to device configuration spaces via a memory mapped address range, and ECAM support devices extends the config space from 256 bytes to 4096 bytes. ECAM also introduces PCI Segments, allowing more than 256 buses to be accessible by the CPU.

Using the legacy CAM method for example to access config space of an Nvidia video card device on BDF 03.00.0 and read the DeviceId at register 0x4, we could would construct the address like so `80000000h | 03h << 16 | 00h << 11 | 0h << 8 | 4 ` giving us `80030004h`.

Using the ECAM method, we could access config space proprietary register 0x500 of a Nvidia card at BDF 03.00.0 by (assuming no cached device tree) first checking the MCFG ACPI table, choosing the correct ECAM entry corresponding to PCI Segment the device lives in, in the ECAM struct **locating the system physical address for the ECAM base address** (e.g. F0000000h), then getting to the BDF offset via `F0000000h` `+ (03h << 20 | 00h << 15 | 0h << 12 | 500h) `giving us the system physical address of the 4K config space accessing offset 0x500 of the Nvidia card at `00000000_F0300500h`.

### MSI

MSI 是在 PCIe 的基础上设计的中断方式，关于 PCIe 的介绍可以看[这里](https://github.com/UtopianFuture/UtopianFuture.github.io/blob/master/virtualization/Device-Virtualization.md#pci%E8%AE%BE%E5%A4%87%E6%A8%A1%E6%8B%9F)。从 PCI 2.1 开始，如果设备需要扩展某种特性，可以向配置空间中的 Capabilities List 中增加一个 Capability，MSI 利用这个特性，将 I/O APIC 中的功能扩展到设备自身。我们来看看 MSI Capability 有哪些域。MSI Capability的ID为5， 共有四种组成方式，分别是 32 和 64 位的 Message 结构，32 位和 64 位带中断Masking 的结构。

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

### MSIX

为了支持多个中断，MSI-X 的 Capability Structure 在 MSI 的基础上增加了 table，其中 Table Offset 和 BIR(BAR Indicator Registor) 定义了 table 所在的位置，即指定使用哪个 BAR 寄存器（PCI 配置空间有 6 个 BAR 和 1 个 XROMBAR），然后从指定的这个 BAR 寄存器中取出 table 映射在 CPU 地址空间的基址，加上 Table Offset 就定位了 entry 的位置。类似的，`PBA BIR` 和 `PBA offset` 分别说明 MSIX- PBA 在哪个 BAR 中，在 BAR 中的什么位置。

![MSIX-capability.png](https://github.com/UtopianFuture/UtopianFuture.github.io/blob/master/image/MSIX-capability.png?raw=true)

MSI-X Table 中的 vector control 表示 PCIe 设备是否能够使用该 Entry 提交中断请求，类似 MSI 的 mask 位。

当外设准备发送中断信息时，其从 Capability Structure 中提取相关信息，信息地址取自 Message Address，其中 bits 20 - 31 是一个固定值 `0x0FEEH`。PCI 总线根据信息地址得知这是一个中断信息，会将其发送给 PCI-HOST 桥，PCI-HOST 桥将其发送到目的 CPU（LAPIC），信息体取自 message data，主要部分是中断向量。

### 问题

下面是一些我在学习 PCIe 过程中遇到的问题：

- 一次 PCI 访问的流程是什么？

  以 dfn 来举例，Seabios 或 OS 会将 PCI 设备对应的 dfn 写入 `0xcf8`，然后将要写入的值写入 `0xcfc`，PCI-HOST 桥就会将 `0xcfc` 中的内容写入到对应的 PCI 的配置空间。

- 可以用 dfn 来访问，也可以用 MMIO 来访问么？

  是的，可以使用 dfn 来访问，也可以用 MMIO 来访问 PCI 设备，但是**使用 MMIO 访问的是 PCI 的 ROM 空间，而 dfn 访问的是配置空间。**下面是英文参考资料：

  > In x86/x64 architecture, the PCI configuration register is accessible via two 32-bit I/O ports. I/O port CF8h-CFBh acts as address port, while I/O port CFCh-CFFh acts as the data port to read/write values into the PCI configuration register.

### Reference

[1] https://resources.infosecinstitute.com/topic/system-address-map-initialization-in-x86x64-architecture-part-1-pci-based-systems/

[2] https://resources.infosecinstitute.com/topic/system-address-map-initialization-x86x64-architecture-part-2-pci-express-based-systems/#gref

[3] https://astralvx.com/introduction-to-pcie/

[4] https://www.cnblogs.com/haiyonghao/p/14440424.html
