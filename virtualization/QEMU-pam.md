## PAM in QEMU

PAM 对我来说是痛苦面具般的存在了，边写边理解吧。

### why PAM?

首先要明白为什么需要 PAM，下面是 QEMU 的官方文档给出的定义。

> The PAM registers make it possible to independently redirect reads and writes in the BIOS ROM area to main memory. The idea is to allow for RAM shadowing which allows read-access for ROMs to come from main memory whereas writes will continue to go to ROMs. This can dramatically improve BIOS execution speed on bare metal because ROM access is generally very slow.
>
> Each PAM register is split into two nibbles. The nibbles then have two reserved bits and then a read-enable and write-enabled bit. Each nibble corresponds to 16k of ROM space except for PAM[0] which refers to 32k.

也就是说，**PAM 寄存器能够将对 BIOS ROM 的读写操作重定向到内存地址中**。这样可以提高 BIOS 在裸机上的执行速度，因为直接在 ROM 上执行速度非常慢。这其中还涉及 shadow RAM 技术，需要解释一下。

#### Shadow RAM

在 32 根地址线的 PC 中（即 32 位系统），BIOS 固件是映射到内存的 `0x00000000fffc0000 - 0x00000000ffffffff (prio 0, rom): pc.bios` 地址中的，即 **4G 内存的最后 256KB**。但为了保持向前兼容性（16 位系统），也为了提高执行速度，机器启动的时候会自动**将 ROM 的 BIOS 复制到 RAM 的 BIOS ROM 区域当中**。所以，当开机后的第一条跳转指令 `ljmpw` 执行之后，因为系统已经将 BIOS 复制到 RAM 中了，**在 RAM 当中也有 BIOS 固件代码，这样 16 位系统也能在 32 bit 的机器上执行**。

**这个复制高地址处的 ROM 到低地址处的过程被称为 Shadow RAM 技术**。然而，在这个过程后，这段内存会被保护起来，无法进行写入（也就是说呈现给 16 位系统的是一个 ROM 空间，不过这个 ROM 空间是在 RAM 中的，所以我们要将其定义为只读）。但是 Seabios 在初始化的过程中需要让这段内存可写，从而便于更改一些静态分配的全局变量值，Seabios 中通过 `make_bios_writable` 来完成这一工作。

### QEMU 处理 PAM

然后每个 PAM 寄存器分为两个半字节，半字节有两个保留位，一个读使能和写使能位。每个半字节对应 16k 的 ROM 空间，但 PAM[0] 对应的是 32k。

在 `i440fx_init` 初始化的时候, 初始化所有 `PAMMemoryRegion`，一共 13 个

- 第一个映射: System BIOS Area Memory Segments, 也即映射 `0xf0000 ~ 0xfffff`（这不是 64K 么，为什么文档中说是 32K）
- 后面的 12 个映射 `0xc0000 ~ 0xeffff`, 每一个映射 `0x4000` 的地址空间

```c
/*
 * SMRAM memory area and PAM memory area in Legacy address range for PC.
 * PAM: Programmable Attribute Map registers
 *
 * 0xa0000 - 0xbffff compatible SMRAM
 *
 * 0xc0000 - 0xc3fff Expansion area memory segments
 * 0xc4000 - 0xc7fff
 * 0xc8000 - 0xcbfff
 * 0xcc000 - 0xcffff
 * 0xd0000 - 0xd3fff
 * 0xd4000 - 0xd7fff
 * 0xd8000 - 0xdbfff
 * 0xdc000 - 0xdffff
 * 0xe0000 - 0xe3fff Extended System BIOS Area Memory Segments
 * 0xe4000 - 0xe7fff
 * 0xe8000 - 0xebfff
 * 0xec000 - 0xeffff
 *
 * 0xf0000 - 0xfffff System BIOS Area Memory Segments
 */
typedef struct PAMMemoryRegion {
    MemoryRegion alias[4];  /* index = PAM value */
    unsigned current;
} PAMMemoryRegion;
```

- alias : 每一个 PAM 存在四种映射属性, 所以创建出来四个 alias（看后面的 32 位系统的地址空间布局可以知道每个空间对应 4 个 alias） 分别为:
  - 0 : read / write 到 PCI
  - 1 : read 转发到 PCI , write 到 ROM
  - 2 : 和 1 相反操作
  - 3 : read / write 到 RAM

当然，QEMU 目前的实现和这个存在一点点差异，参考 `init_pam`。

```c
void init_pam(DeviceState *dev, MemoryRegion *ram_memory,
              MemoryRegion *system_memory, MemoryRegion *pci_address_space,
              PAMMemoryRegion *mem, uint32_t start, uint32_t size)
{
    int i;

    /* RAM */
    memory_region_init_alias(&mem->alias[3], OBJECT(dev), "pam-ram", ram_memory,
                             start, size);
    /* ROM (XXX: not quite correct) */
    memory_region_init_alias(&mem->alias[1], OBJECT(dev), "pam-rom", ram_memory,
                             start, size);
    memory_region_set_readonly(&mem->alias[1], true);

    /* XXX: should distinguish read/write cases */
    memory_region_init_alias(&mem->alias[0], OBJECT(dev), "pam-pci", pci_address_space,
                             start, size);
    memory_region_init_alias(&mem->alias[2], OBJECT(dev), "pam-pci", ram_memory,
                             start, size);

    memory_region_transaction_begin();
    for (i = 0; i < 4; ++i) {
        memory_region_set_enabled(&mem->alias[i], false);
        memory_region_add_subregion_overlap(system_memory, start,
                                            &mem->alias[i], 1);
    }
    memory_region_transaction_commit();
    mem->current = 0;
}
```

当想要修改 PAM 寄存器的属性，最后会调用下面的函数，**将原来的映射 disable 掉**，启用新的映射，

```c
void pam_update(PAMMemoryRegion *pam, int idx, uint8_t val)
{
    assert(0 <= idx && idx < PAM_REGIONS_COUNT);

    memory_region_set_enabled(&pam->alias[pam->current], false);
    pam->current = (val >> ((!(idx & 1)) * 4)) & PAM_ATTR_MASK;
    memory_region_set_enabled(&pam->alias[pam->current], true);
}
```

那么总结起来是这样的，前 1M 的地址空间中，**在系统的不同阶段会映射为不同的内存类型**：

- 在 PAM 完全没有打开的时候，映射的 PCI
- 然后映射为 RAM
- 最后映射为 ROM

而 BMBT 的实现是初始化时就相当于 PAM 打开，映射 ROM。

### 32 位系统的地址空间布局

```
memory-region: memory
  0000000000000000-ffffffffffffffff (prio 0, i/o): system
    0000000000000000-00000000bfffffff (prio 0, i/o): alias ram-below-4g @pc.ram 0000000000000000-00000000bfffffff
    0000000000000000-ffffffffffffffff (prio -1, i/o): pci
      00000000000a0000-00000000000bffff (prio 1, i/o): vga-lowmem
      00000000000c0000-00000000000dffff (prio 1, rom): pc.rom
      00000000000e0000-00000000000fffff (prio 1, i/o): alias isa-bios @pc.bios 0000000000020000-000000000003ffff
      00000000fd000000-00000000fdffffff (prio 1, ram): vga.vram
      00000000febc0000-00000000febdffff (prio 1, i/o): e1000-mmio
      00000000febf0000-00000000febf0fff (prio 1, i/o): vga.mmio
        00000000febf0000-00000000febf00ff (prio 0, i/o): edid
        00000000febf0400-00000000febf041f (prio 0, i/o): vga ioports remapped
        00000000febf0500-00000000febf0515 (prio 0, i/o): bochs dispi interface
        00000000febf0600-00000000febf0607 (prio 0, i/o): qemu extended regs
      00000000fffc0000-00000000ffffffff (prio 0, rom): pc.bios
    00000000000a0000-00000000000bffff (prio 1, i/o): alias smram-region @pci 00000000000a0000-00000000000bffff
    00000000000c0000-00000000000c3fff (prio 1, i/o): alias pam-ram @pc.ram 00000000000c0000-00000000000c3fff [disabled]
    00000000000c0000-00000000000c3fff (prio 1, i/o): alias pam-pci @pc.ram 00000000000c0000-00000000000c3fff [disabled]
    00000000000c0000-00000000000c3fff (prio 1, i/o): alias pam-rom @pc.ram 00000000000c0000-00000000000c3fff
    00000000000c0000-00000000000c3fff (prio 1, i/o): alias pam-pci @pci 00000000000c0000-00000000000c3fff [disabled]
    00000000000c4000-00000000000c7fff (prio 1, i/o): alias pam-ram @pc.ram 00000000000c4000-00000000000c7fff [disabled]
    00000000000c4000-00000000000c7fff (prio 1, i/o): alias pam-pci @pc.ram 00000000000c4000-00000000000c7fff [disabled]
    00000000000c4000-00000000000c7fff (prio 1, i/o): alias pam-rom @pc.ram 00000000000c4000-00000000000c7fff
    00000000000c4000-00000000000c7fff (prio 1, i/o): alias pam-pci @pci 00000000000c4000-00000000000c7fff [disabled]
    00000000000c8000-00000000000cbfff (prio 1, i/o): alias pam-ram @pc.ram 00000000000c8000-00000000000cbfff [disabled]
    00000000000c8000-00000000000cbfff (prio 1, i/o): alias pam-pci @pc.ram 00000000000c8000-00000000000cbfff [disabled]
    00000000000c8000-00000000000cbfff (prio 1, i/o): alias pam-rom @pc.ram 00000000000c8000-00000000000cbfff
    00000000000c8000-00000000000cbfff (prio 1, i/o): alias pam-pci @pci 00000000000c8000-00000000000cbfff [disabled]
    00000000000cb000-00000000000cdfff (prio 1000, i/o): alias kvmvapic-rom @pc.ram 00000000000cb000-00000000000cdfff
    00000000000cc000-00000000000cffff (prio 1, i/o): alias pam-ram @pc.ram 00000000000cc000-00000000000cffff [disabled]
    00000000000cc000-00000000000cffff (prio 1, i/o): alias pam-pci @pc.ram 00000000000cc000-00000000000cffff [disabled]
    00000000000cc000-00000000000cffff (prio 1, i/o): alias pam-rom @pc.ram 00000000000cc000-00000000000cffff
    00000000000cc000-00000000000cffff (prio 1, i/o): alias pam-pci @pci 00000000000cc000-00000000000cffff [disabled]
    00000000000d0000-00000000000d3fff (prio 1, i/o): alias pam-ram @pc.ram 00000000000d0000-00000000000d3fff [disabled]
    00000000000d0000-00000000000d3fff (prio 1, i/o): alias pam-pci @pc.ram 00000000000d0000-00000000000d3fff [disabled]
    00000000000d0000-00000000000d3fff (prio 1, i/o): alias pam-rom @pc.ram 00000000000d0000-00000000000d3fff
    00000000000d0000-00000000000d3fff (prio 1, i/o): alias pam-pci @pci 00000000000d0000-00000000000d3fff [disabled]
    00000000000d4000-00000000000d7fff (prio 1, i/o): alias pam-ram @pc.ram 00000000000d4000-00000000000d7fff [disabled]
    00000000000d4000-00000000000d7fff (prio 1, i/o): alias pam-pci @pc.ram 00000000000d4000-00000000000d7fff [disabled]
    00000000000d4000-00000000000d7fff (prio 1, i/o): alias pam-rom @pc.ram 00000000000d4000-00000000000d7fff
    00000000000d4000-00000000000d7fff (prio 1, i/o): alias pam-pci @pci 00000000000d4000-00000000000d7fff [disabled]
    00000000000d8000-00000000000dbfff (prio 1, i/o): alias pam-ram @pc.ram 00000000000d8000-00000000000dbfff [disabled]
    00000000000d8000-00000000000dbfff (prio 1, i/o): alias pam-pci @pc.ram 00000000000d8000-00000000000dbfff [disabled]
    00000000000d8000-00000000000dbfff (prio 1, i/o): alias pam-rom @pc.ram 00000000000d8000-00000000000dbfff
    00000000000d8000-00000000000dbfff (prio 1, i/o): alias pam-pci @pci 00000000000d8000-00000000000dbfff [disabled]
    00000000000dc000-00000000000dffff (prio 1, i/o): alias pam-ram @pc.ram 00000000000dc000-00000000000dffff [disabled]
    00000000000dc000-00000000000dffff (prio 1, i/o): alias pam-pci @pc.ram 00000000000dc000-00000000000dffff [disabled]
    00000000000dc000-00000000000dffff (prio 1, i/o): alias pam-rom @pc.ram 00000000000dc000-00000000000dffff
    00000000000dc000-00000000000dffff (prio 1, i/o): alias pam-pci @pci 00000000000dc000-00000000000dffff [disabled]
    00000000000e0000-00000000000e3fff (prio 1, i/o): alias pam-ram @pc.ram 00000000000e0000-00000000000e3fff [disabled]
    00000000000e0000-00000000000e3fff (prio 1, i/o): alias pam-pci @pc.ram 00000000000e0000-00000000000e3fff [disabled]
00000000000e0000-00000000000e3fff (prio 1, i/o): alias pam-rom @pc.ram 00000000000e0000-00000000000e3fff
00000000000e0000-00000000000e3fff (prio 1, i/o): alias pam-pci @pci 00000000000e0000-00000000000e3fff [disabled]
00000000000e4000-00000000000e7fff (prio 1, i/o): alias pam-ram @pc.ram 00000000000e4000-00000000000e7fff [disabled]
00000000000e4000-00000000000e7fff (prio 1, i/o): alias pam-pci @pc.ram 00000000000e4000-00000000000e7fff [disabled]
00000000000e4000-00000000000e7fff (prio 1, i/o): alias pam-rom @pc.ram 00000000000e4000-00000000000e7fff
00000000000e4000-00000000000e7fff (prio 1, i/o): alias pam-pci @pci 00000000000e4000-00000000000e7fff [disabled]
00000000000e8000-00000000000ebfff (prio 1, i/o): alias pam-ram @pc.ram 00000000000e8000-00000000000ebfff
00000000000e8000-00000000000ebfff (prio 1, i/o): alias pam-pci @pc.ram 00000000000e8000-00000000000ebfff [disabled]
00000000000e8000-00000000000ebfff (prio 1, i/o): alias pam-rom @pc.ram 00000000000e8000-00000000000ebfff [disabled]
00000000000e8000-00000000000ebfff (prio 1, i/o): alias pam-pci @pci 00000000000e8000-00000000000ebfff [disabled]
00000000000ec000-00000000000effff (prio 1, i/o): alias pam-ram @pc.ram 00000000000ec000-00000000000effff
00000000000ec000-00000000000effff (prio 1, i/o): alias pam-pci @pc.ram 00000000000ec000-00000000000effff [disabled]
00000000000ec000-00000000000effff (prio 1, i/o): alias pam-rom @pc.ram 00000000000ec000-00000000000effff [disabled]
00000000000ec000-00000000000effff (prio 1, i/o): alias pam-pci @pci 00000000000ec000-00000000000effff [disabled]
00000000000f0000-00000000000fffff (prio 1, i/o): alias pam-ram @pc.ram 00000000000f0000-00000000000fffff [disabled]
00000000000f0000-00000000000fffff (prio 1, i/o): alias pam-pci @pc.ram 00000000000f0000-00000000000fffff [disabled]
00000000000f0000-00000000000fffff (prio 1, i/o): alias pam-rom @pc.ram 00000000000f0000-00000000000fffff
00000000000f0000-00000000000fffff (prio 1, i/o): alias pam-pci @pci 00000000000f0000-00000000000fffff [disabled]
00000000fec00000-00000000fec00fff (prio 0, i/o): kvm-ioapic
00000000fed00000-00000000fed003ff (prio 0, i/o): hpet
00000000fee00000-00000000feefffff (prio 4096, i/o): kvm-apic-msi
0000000100000000-000000023fffffff (prio 0, i/o): alias ram-above-4g @pc.ram 00000000c0000000-00000001ffffffff
```

### Seabios 处理 PAM

Seabios 在 `__make_bios_writable_intel` 中处理 PAM，我们直接看代码：

```c
// Enable shadowing and copy bios.
static void
__make_bios_writable_intel(u16 bdf, u32 pam0)
{
    // Read in current PAM settings from pci config space
    union pamdata_u pamdata;
    pamdata.data32[0] = pci_config_readl(bdf, ALIGN_DOWN(pam0, 4));
    pamdata.data32[1] = pci_config_readl(bdf, ALIGN_DOWN(pam0, 4) + 4);
    u8 *pam = &pamdata.data8[pam0 & 0x03];

    // Make ram from 0xc0000-0xf0000 writable
    int i;
    for (i=0; i<6; i++)
        pam[i + 1] = 0x33;

    // Make ram from 0xf0000-0x100000 writable
    int ram_present = pam[0] & 0x10;
    pam[0] = 0x30;

    // Write PAM settings back to pci config space
    pci_config_writel(bdf, ALIGN_DOWN(pam0, 4), pamdata.data32[0]);
    pci_config_writel(bdf, ALIGN_DOWN(pam0, 4) + 4, pamdata.data32[1]);

    if (!ram_present)
        // Copy bios.
        memcpy(VSYMBOL(code32flat_start)
               , VSYMBOL(code32flat_start) + BIOS_SRC_OFFSET
               , SYMBOL(code32flat_end) - SYMBOL(code32flat_start));
}
```

从代码中清晰的看到，Seabios 通过 `memcpy`  将 `VSYMBOL(code32flat_start) + BIOS_SRC_OFFSET` 的 bios 复制到 `VSYMBOL(code32flat_start)`。而前面的 pam 操作则是对 `0xc0000 ~ 0xfffff` 内存空间的读写操作的重定向定义。

当 bios 结束之后，即 `mainint` 中主要的操作都执行完毕，这些 PAM 的位置会再次设置上 `make_bios_readonly_intel`，但是 `0xe4000 ~ 0xeffff` 部分会被豁免（为什么，这段地址空间是用来干嘛的）。

```c
static void
make_bios_readonly_intel(u16 bdf, u32 pam0)
{
    // Flush any pending writes before locking memory.
    wbinvd();

    // Read in current PAM settings from pci config space
    union pamdata_u pamdata;
    pamdata.data32[0] = pci_config_readl(bdf, ALIGN_DOWN(pam0, 4));
    pamdata.data32[1] = pci_config_readl(bdf, ALIGN_DOWN(pam0, 4) + 4);
    u8 *pam = &pamdata.data8[pam0 & 0x03];

    // Write protect roms from 0xc0000-0xf0000
    u32 romlast = BUILD_BIOS_ADDR, rommax = BUILD_BIOS_ADDR;
    if (CONFIG_WRITABLE_UPPERMEMORY)
        romlast = rom_get_last();
    if (CONFIG_MALLOC_UPPERMEMORY)
        rommax = rom_get_max();
    int i;
    for (i=0; i<6; i++) {
        u32 mem = BUILD_ROM_START + i * 32*1024;
        if (romlast < mem + 16*1024 || rommax < mem + 32*1024) {
            if (romlast >= mem && rommax >= mem + 16*1024)
                pam[i + 1] = 0x31;
            break;
        }
        pam[i + 1] = 0x11;
    }

    // Write protect 0xf0000-0x100000
    pam[0] = 0x10;

    // Write PAM settings back to pci config space
    pci_config_writel(bdf, ALIGN_DOWN(pam0, 4), pamdata.data32[0]);
    pci_config_writel(bdf, ALIGN_DOWN(pam0, 4) + 4, pamdata.data32[1]);
}
```

### References

[1] https://wiki.qemu.org/Documentation/Platforms/PC

[2] https://martins3.github.io/qemu/bios-memory.html
