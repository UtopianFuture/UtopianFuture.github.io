```
address-space: memory
  0000000000000000-ffffffffffffffff (prio 0, i/o): system
    0000000000000000-00000000bfffffff (prio 0, ram): alias ram-below-4g @pc.ram 0000000000000000-00000000bfffffff
    0000000000000000-ffffffffffffffff (prio -1, i/o): pci
      00000000000a0000-00000000000bffff (prio 1, i/o): vga-lowmem
      00000000000c0000-00000000000dffff (prio 1, rom): pc.rom
      00000000000e0000-00000000000fffff (prio 1, rom): alias isa-bios @pc.bios 0000000000020000-000000000003ffff
      00000000fd000000-00000000fdffffff (prio 1, ram): vga.vram
      00000000fe000000-00000000fe003fff (prio 1, i/o): virtio-pci
        00000000fe000000-00000000fe000fff (prio 0, i/o): virtio-pci-common-virtio-9p
        00000000fe001000-00000000fe001fff (prio 0, i/o): virtio-pci-isr-virtio-9p
        00000000fe002000-00000000fe002fff (prio 0, i/o): virtio-pci-device-virtio-9p
        00000000fe003000-00000000fe003fff (prio 0, i/o): virtio-pci-notify-virtio-9p
      00000000febc0000-00000000febdffff (prio 1, i/o): e1000-mmio
      00000000febf0000-00000000febf3fff (prio 1, i/o): nvme-bar0
        00000000febf0000-00000000febf1fff (prio 0, i/o): nvme
        00000000febf2000-00000000febf240f (prio 0, i/o): msix-table
        00000000febf3000-00000000febf300f (prio 0, i/o): msix-pba
      00000000febf4000-00000000febf4fff (prio 1, i/o): vga.mmio
        00000000febf4000-00000000febf417f (prio 0, i/o): edid
        00000000febf4400-00000000febf441f (prio 0, i/o): vga ioports remapped
        00000000febf4500-00000000febf4515 (prio 0, i/o): bochs dispi interface
        00000000febf4600-00000000febf4607 (prio 0, i/o): qemu extended regs
      00000000febf5000-00000000febf5fff (prio 1, i/o): virtio-9p-pci-msix
        00000000febf5000-00000000febf501f (prio 0, i/o): msix-table
        00000000febf5800-00000000febf5807 (prio 0, i/o): msix-pba
      00000000fffc0000-00000000ffffffff (prio 0, rom): pc.bios
    00000000000a0000-00000000000bffff (prio 1, i/o): alias smram-region @pci 00000000000a0000-00000000000bffff
    00000000000c0000-00000000000c3fff (prio 1, ram): alias pam-rom @pc.ram 00000000000c0000-00000000000c3fff
    00000000000c4000-00000000000c7fff (prio 1, ram): alias pam-rom @pc.ram 00000000000c4000-00000000000c7fff
    00000000000c8000-00000000000cbfff (prio 1, ram): alias pam-rom @pc.ram 00000000000c8000-00000000000cbfff
    00000000000cb000-00000000000cdfff (prio 1000, ram): alias kvmvapic-rom @pc.ram 00000000000cb000-00000000000cdfff
    00000000000cc000-00000000000cffff (prio 1, ram): alias pam-rom @pc.ram 00000000000cc000-00000000000cffff
    00000000000d0000-00000000000d3fff (prio 1, ram): alias pam-rom @pc.ram 00000000000d0000-00000000000d3fff
    00000000000d4000-00000000000d7fff (prio 1, ram): alias pam-rom @pc.ram 00000000000d4000-00000000000d7fff
    00000000000d8000-00000000000dbfff (prio 1, ram): alias pam-rom @pc.ram 00000000000d8000-00000000000dbfff
    00000000000dc000-00000000000dffff (prio 1, ram): alias pam-rom @pc.ram 00000000000dc000-00000000000dffff
    00000000000e0000-00000000000e3fff (prio 1, ram): alias pam-rom @pc.ram 00000000000e0000-00000000000e3fff
    00000000000e4000-00000000000e7fff (prio 1, ram): alias pam-ram @pc.ram 00000000000e4000-00000000000e7fff
    00000000000e8000-00000000000ebfff (prio 1, ram): alias pam-ram @pc.ram 00000000000e8000-00000000000ebfff
    00000000000ec000-00000000000effff (prio 1, ram): alias pam-ram @pc.ram 00000000000ec000-00000000000effff
    00000000000f0000-00000000000fffff (prio 1, ram): alias pam-rom @pc.ram 00000000000f0000-00000000000fffff
    00000000fec00000-00000000fec00fff (prio 0, i/o): ioapic
    00000000fed00000-00000000fed003ff (prio 0, i/o): hpet
    00000000fee00000-00000000feefffff (prio 4096, i/o): apic-msi
    0000000100000000-00000001bfffffff (prio 0, ram): alias ram-above-4g @pc.ram 00000000c0000000-000000017fffffff

address-space: I/O
  0000000000000000-000000000000ffff (prio 0, i/o): io
    0000000000000000-0000000000000007 (prio 0, i/o): dma-chan
    0000000000000008-000000000000000f (prio 0, i/o): dma-cont
    0000000000000020-0000000000000021 (prio 0, i/o): pic
    0000000000000040-0000000000000043 (prio 0, i/o): pit
    0000000000000060-0000000000000060 (prio 0, i/o): i8042-data
    0000000000000061-0000000000000061 (prio 0, i/o): pcspk
    0000000000000064-0000000000000064 (prio 0, i/o): i8042-cmd
    0000000000000070-0000000000000071 (prio 0, i/o): rtc
      0000000000000070-0000000000000070 (prio 0, i/o): rtc-index
    000000000000007e-000000000000007f (prio 0, i/o): kvmvapic
    0000000000000080-0000000000000080 (prio 0, i/o): ioport80
    0000000000000081-0000000000000083 (prio 0, i/o): dma-page
    0000000000000087-0000000000000087 (prio 0, i/o): dma-page
    0000000000000089-000000000000008b (prio 0, i/o): dma-page
    000000000000008f-000000000000008f (prio 0, i/o): dma-page
    0000000000000092-0000000000000092 (prio 0, i/o): port92
    00000000000000a0-00000000000000a1 (prio 0, i/o): pic
    00000000000000b2-00000000000000b3 (prio 0, i/o): apm-io
    00000000000000c0-00000000000000cf (prio 0, i/o): dma-chan
    00000000000000d0-00000000000000df (prio 0, i/o): dma-cont
    00000000000000f0-00000000000000f0 (prio 0, i/o): ioportF0
    0000000000000170-0000000000000177 (prio 0, i/o): ide
    00000000000001ce-00000000000001d1 (prio 0, i/o): vbe
    00000000000001f0-00000000000001f7 (prio 0, i/o): ide
    0000000000000376-0000000000000376 (prio 0, i/o): ide
    0000000000000378-000000000000037f (prio 0, i/o): parallel
    00000000000003b4-00000000000003b5 (prio 0, i/o): vga
    00000000000003ba-00000000000003ba (prio 0, i/o): vga
    00000000000003c0-00000000000003cf (prio 0, i/o): vga
    00000000000003d4-00000000000003d5 (prio 0, i/o): vga
    00000000000003da-00000000000003da (prio 0, i/o): vga
    00000000000003f1-00000000000003f5 (prio 0, i/o): fdc
    00000000000003f6-00000000000003f6 (prio 0, i/o): ide
    00000000000003f7-00000000000003f7 (prio 0, i/o): fdc
    00000000000003f8-00000000000003ff (prio 0, i/o): serial
    0000000000000402-0000000000000402 (prio 0, i/o): isa-debugcon
    00000000000004d0-00000000000004d0 (prio 0, i/o): elcr
    00000000000004d1-00000000000004d1 (prio 0, i/o): elcr
    0000000000000510-0000000000000511 (prio 0, i/o): fwcfg
    0000000000000514-000000000000051b (prio 0, i/o): fwcfg.dma
    0000000000000600-000000000000063f (prio 0, i/o): piix4-pm
      0000000000000600-0000000000000603 (prio 0, i/o): acpi-evt
      0000000000000604-0000000000000605 (prio 0, i/o): acpi-cnt
      0000000000000608-000000000000060b (prio 0, i/o): acpi-tmr
    0000000000000700-000000000000073f (prio 0, i/o): pm-smbus
    0000000000000cf8-0000000000000cfb (prio 0, i/o): pci-conf-idx
    0000000000000cf9-0000000000000cf9 (prio 1, i/o): piix3-reset-control
    0000000000000cfc-0000000000000cff (prio 0, i/o): pci-conf-data
    0000000000005658-0000000000005658 (prio 0, i/o): vmport
    000000000000ae00-000000000000ae17 (prio 0, i/o): acpi-pci-hotplug
    000000000000af00-000000000000af1f (prio 0, i/o): acpi-cpu-hotplug
    000000000000afe0-000000000000afe3 (prio 0, i/o): acpi-gpe0
    000000000000c000-000000000000c03f (prio 1, i/o): e1000-io
    000000000000c040-000000000000c05f (prio 1, i/o): virtio-pci
    000000000000c060-000000000000c06f (prio 1, i/o): piix-bmdma-container
      000000000000c060-000000000000c063 (prio 0, i/o): piix-bmdma
      000000000000c064-000000000000c067 (prio 0, i/o): bmdma
      000000000000c068-000000000000c06b (prio 0, i/o): piix-bmdma
      000000000000c06c-000000000000c06f (prio 0, i/o): bmdma

address-space: cpu-memory-0
  0000000000000000-ffffffffffffffff (prio 0, i/o): system
    0000000000000000-00000000bfffffff (prio 0, ram): alias ram-below-4g @pc.ram 0000000000000000-00000000bfffffff
    0000000000000000-ffffffffffffffff (prio -1, i/o): pci
      00000000000a0000-00000000000bffff (prio 1, i/o): vga-lowmem
      00000000000c0000-00000000000dffff (prio 1, rom): pc.rom
      00000000000e0000-00000000000fffff (prio 1, rom): alias isa-bios @pc.bios 0000000000020000-000000000003ffff
      00000000fd000000-00000000fdffffff (prio 1, ram): vga.vram
      00000000fe000000-00000000fe003fff (prio 1, i/o): virtio-pci
        00000000fe000000-00000000fe000fff (prio 0, i/o): virtio-pci-common-virtio-9p
        00000000fe001000-00000000fe001fff (prio 0, i/o): virtio-pci-isr-virtio-9p
        00000000fe002000-00000000fe002fff (prio 0, i/o): virtio-pci-device-virtio-9p
        00000000fe003000-00000000fe003fff (prio 0, i/o): virtio-pci-notify-virtio-9p
      00000000febc0000-00000000febdffff (prio 1, i/o): e1000-mmio
      00000000febf0000-00000000febf3fff (prio 1, i/o): nvme-bar0
        00000000febf0000-00000000febf1fff (prio 0, i/o): nvme
        00000000febf2000-00000000febf240f (prio 0, i/o): msix-table
        00000000febf3000-00000000febf300f (prio 0, i/o): msix-pba
      00000000febf4000-00000000febf4fff (prio 1, i/o): vga.mmio
        00000000febf4000-00000000febf417f (prio 0, i/o): edid
        00000000febf4400-00000000febf441f (prio 0, i/o): vga ioports remapped
        00000000febf4500-00000000febf4515 (prio 0, i/o): bochs dispi interface
        00000000febf4600-00000000febf4607 (prio 0, i/o): qemu extended regs
      00000000febf5000-00000000febf5fff (prio 1, i/o): virtio-9p-pci-msix
        00000000febf5000-00000000febf501f (prio 0, i/o): msix-table
        00000000febf5800-00000000febf5807 (prio 0, i/o): msix-pba
      00000000fffc0000-00000000ffffffff (prio 0, rom): pc.bios
    00000000000a0000-00000000000bffff (prio 1, i/o): alias smram-region @pci 00000000000a0000-00000000000bffff
    00000000000c0000-00000000000c3fff (prio 1, ram): alias pam-rom @pc.ram 00000000000c0000-00000000000c3fff
    00000000000c4000-00000000000c7fff (prio 1, ram): alias pam-rom @pc.ram 00000000000c4000-00000000000c7fff
    00000000000c8000-00000000000cbfff (prio 1, ram): alias pam-rom @pc.ram 00000000000c8000-00000000000cbfff
    00000000000cb000-00000000000cdfff (prio 1000, ram): alias kvmvapic-rom @pc.ram 00000000000cb000-00000000000cdfff
    00000000000cc000-00000000000cffff (prio 1, ram): alias pam-rom @pc.ram 00000000000cc000-00000000000cffff
    00000000000d0000-00000000000d3fff (prio 1, ram): alias pam-rom @pc.ram 00000000000d0000-00000000000d3fff
    00000000000d4000-00000000000d7fff (prio 1, ram): alias pam-rom @pc.ram 00000000000d4000-00000000000d7fff
    00000000000d8000-00000000000dbfff (prio 1, ram): alias pam-rom @pc.ram 00000000000d8000-00000000000dbfff
    00000000000dc000-00000000000dffff (prio 1, ram): alias pam-rom @pc.ram 00000000000dc000-00000000000dffff
    00000000000e0000-00000000000e3fff (prio 1, ram): alias pam-rom @pc.ram 00000000000e0000-00000000000e3fff
    00000000000e4000-00000000000e7fff (prio 1, ram): alias pam-ram @pc.ram 00000000000e4000-00000000000e7fff
    00000000000e8000-00000000000ebfff (prio 1, ram): alias pam-ram @pc.ram 00000000000e8000-00000000000ebfff
    00000000000ec000-00000000000effff (prio 1, ram): alias pam-ram @pc.ram 00000000000ec000-00000000000effff
    00000000000f0000-00000000000fffff (prio 1, ram): alias pam-rom @pc.ram 00000000000f0000-00000000000fffff
    00000000fec00000-00000000fec00fff (prio 0, i/o): ioapic
    00000000fed00000-00000000fed003ff (prio 0, i/o): hpet
    00000000fee00000-00000000feefffff (prio 4096, i/o): apic-msi
    0000000100000000-00000001bfffffff (prio 0, ram): alias ram-above-4g @pc.ram 00000000c0000000-000000017fffffff

address-space: cpu-smm-0
  0000000000000000-ffffffffffffffff (prio 0, i/o): memory
    0000000000000000-00000000ffffffff (prio 1, i/o): alias smram @smram 0000000000000000-00000000ffffffff
    0000000000000000-ffffffffffffffff (prio 0, i/o): alias memory @system 0000000000000000-ffffffffffffffff

address-space: cpu-memory-1
  0000000000000000-ffffffffffffffff (prio 0, i/o): system
    0000000000000000-00000000bfffffff (prio 0, ram): alias ram-below-4g @pc.ram 0000000000000000-00000000bfffffff
    0000000000000000-ffffffffffffffff (prio -1, i/o): pci
      00000000000a0000-00000000000bffff (prio 1, i/o): vga-lowmem
      00000000000c0000-00000000000dffff (prio 1, rom): pc.rom
      00000000000e0000-00000000000fffff (prio 1, rom): alias isa-bios @pc.bios 0000000000020000-000000000003ffff
      00000000fd000000-00000000fdffffff (prio 1, ram): vga.vram
      00000000fe000000-00000000fe003fff (prio 1, i/o): virtio-pci
        00000000fe000000-00000000fe000fff (prio 0, i/o): virtio-pci-common-virtio-9p
        00000000fe001000-00000000fe001fff (prio 0, i/o): virtio-pci-isr-virtio-9p
        00000000fe002000-00000000fe002fff (prio 0, i/o): virtio-pci-device-virtio-9p
        00000000fe003000-00000000fe003fff (prio 0, i/o): virtio-pci-notify-virtio-9p
      00000000febc0000-00000000febdffff (prio 1, i/o): e1000-mmio
      00000000febf0000-00000000febf3fff (prio 1, i/o): nvme-bar0
        00000000febf0000-00000000febf1fff (prio 0, i/o): nvme
        00000000febf2000-00000000febf240f (prio 0, i/o): msix-table
        00000000febf3000-00000000febf300f (prio 0, i/o): msix-pba
      00000000febf4000-00000000febf4fff (prio 1, i/o): vga.mmio
        00000000febf4000-00000000febf417f (prio 0, i/o): edid
        00000000febf4400-00000000febf441f (prio 0, i/o): vga ioports remapped
        00000000febf4500-00000000febf4515 (prio 0, i/o): bochs dispi interface
        00000000febf4600-00000000febf4607 (prio 0, i/o): qemu extended regs
      00000000febf5000-00000000febf5fff (prio 1, i/o): virtio-9p-pci-msix
        00000000febf5000-00000000febf501f (prio 0, i/o): msix-table
        00000000febf5800-00000000febf5807 (prio 0, i/o): msix-pba
      00000000fffc0000-00000000ffffffff (prio 0, rom): pc.bios
    00000000000a0000-00000000000bffff (prio 1, i/o): alias smram-region @pci 00000000000a0000-00000000000bffff
    00000000000c0000-00000000000c3fff (prio 1, ram): alias pam-rom @pc.ram 00000000000c0000-00000000000c3fff
    00000000000c4000-00000000000c7fff (prio 1, ram): alias pam-rom @pc.ram 00000000000c4000-00000000000c7fff
    00000000000c8000-00000000000cbfff (prio 1, ram): alias pam-rom @pc.ram 00000000000c8000-00000000000cbfff
    00000000000cb000-00000000000cdfff (prio 1000, ram): alias kvmvapic-rom @pc.ram 00000000000cb000-00000000000cdfff
    00000000000cc000-00000000000cffff (prio 1, ram): alias pam-rom @pc.ram 00000000000cc000-00000000000cffff
    00000000000d0000-00000000000d3fff (prio 1, ram): alias pam-rom @pc.ram 00000000000d0000-00000000000d3fff
    00000000000d4000-00000000000d7fff (prio 1, ram): alias pam-rom @pc.ram 00000000000d4000-00000000000d7fff
    00000000000d8000-00000000000dbfff (prio 1, ram): alias pam-rom @pc.ram 00000000000d8000-00000000000dbfff
    00000000000dc000-00000000000dffff (prio 1, ram): alias pam-rom @pc.ram 00000000000dc000-00000000000dffff
    00000000000e0000-00000000000e3fff (prio 1, ram): alias pam-rom @pc.ram 00000000000e0000-00000000000e3fff
    00000000000e4000-00000000000e7fff (prio 1, ram): alias pam-ram @pc.ram 00000000000e4000-00000000000e7fff
    00000000000e8000-00000000000ebfff (prio 1, ram): alias pam-ram @pc.ram 00000000000e8000-00000000000ebfff
    00000000000ec000-00000000000effff (prio 1, ram): alias pam-ram @pc.ram 00000000000ec000-00000000000effff
    00000000000f0000-00000000000fffff (prio 1, ram): alias pam-rom @pc.ram 00000000000f0000-00000000000fffff
    00000000fec00000-00000000fec00fff (prio 0, i/o): ioapic
    00000000fed00000-00000000fed003ff (prio 0, i/o): hpet
    00000000fee00000-00000000feefffff (prio 4096, i/o): apic-msi
    0000000100000000-00000001bfffffff (prio 0, ram): alias ram-above-4g @pc.ram 00000000c0000000-000000017fffffff

address-space: cpu-smm-1
  0000000000000000-ffffffffffffffff (prio 0, i/o): memory
    0000000000000000-00000000ffffffff (prio 1, i/o): alias smram @smram 0000000000000000-00000000ffffffff
    0000000000000000-ffffffffffffffff (prio 0, i/o): alias memory @system 0000000000000000-ffffffffffffffff

address-space: i440FX
  0000000000000000-ffffffffffffffff (prio 0, i/o): bus master container

address-space: PIIX3
  0000000000000000-ffffffffffffffff (prio 0, i/o): bus master container

address-space: VGA
  0000000000000000-ffffffffffffffff (prio 0, i/o): bus master container

address-space: e1000
  0000000000000000-ffffffffffffffff (prio 0, i/o): bus master container

address-space: piix3-ide
  0000000000000000-ffffffffffffffff (prio 0, i/o): bus master container

address-space: PIIX4_PM
  0000000000000000-ffffffffffffffff (prio 0, i/o): bus master container

address-space: nvme
  0000000000000000-ffffffffffffffff (prio 0, i/o): bus master container
    0000000000000000-ffffffffffffffff (prio 0, i/o): alias bus master @system 0000000000000000-ffffffffffffffff

address-space: virtio-9p-pci
  0000000000000000-ffffffffffffffff (prio 0, i/o): bus master container

memory-region: pc.ram
  0000000000000000-000000017fffffff (prio 0, ram): pc.ram

memory-region: pc.bios
  00000000fffc0000-00000000ffffffff (prio 0, rom): pc.bios

memory-region: pci
  0000000000000000-ffffffffffffffff (prio -1, i/o): pci
    00000000000a0000-00000000000bffff (prio 1, i/o): vga-lowmem
    00000000000c0000-00000000000dffff (prio 1, rom): pc.rom
    00000000000e0000-00000000000fffff (prio 1, rom): alias isa-bios @pc.bios 0000000000020000-000000000003ffff
    00000000fd000000-00000000fdffffff (prio 1, ram): vga.vram
    00000000fe000000-00000000fe003fff (prio 1, i/o): virtio-pci
      00000000fe000000-00000000fe000fff (prio 0, i/o): virtio-pci-common-virtio-9p
      00000000fe001000-00000000fe001fff (prio 0, i/o): virtio-pci-isr-virtio-9p
      00000000fe002000-00000000fe002fff (prio 0, i/o): virtio-pci-device-virtio-9p
      00000000fe003000-00000000fe003fff (prio 0, i/o): virtio-pci-notify-virtio-9p
    00000000febc0000-00000000febdffff (prio 1, i/o): e1000-mmio
    00000000febf0000-00000000febf3fff (prio 1, i/o): nvme-bar0
      00000000febf0000-00000000febf1fff (prio 0, i/o): nvme
      00000000febf2000-00000000febf240f (prio 0, i/o): msix-table
      00000000febf3000-00000000febf300f (prio 0, i/o): msix-pba
    00000000febf4000-00000000febf4fff (prio 1, i/o): vga.mmio
      00000000febf4000-00000000febf417f (prio 0, i/o): edid
      00000000febf4400-00000000febf441f (prio 0, i/o): vga ioports remapped
      00000000febf4500-00000000febf4515 (prio 0, i/o): bochs dispi interface
      00000000febf4600-00000000febf4607 (prio 0, i/o): qemu extended regs
    00000000febf5000-00000000febf5fff (prio 1, i/o): virtio-9p-pci-msix
      00000000febf5000-00000000febf501f (prio 0, i/o): msix-table
      00000000febf5800-00000000febf5807 (prio 0, i/o): msix-pba
    00000000fffc0000-00000000ffffffff (prio 0, rom): pc.bios

memory-region: smram
  0000000000000000-00000000ffffffff (prio 0, i/o): smram
    00000000000a0000-00000000000bffff (prio 0, ram): alias smram-low @pc.ram 00000000000a0000-00000000000bffff

memory-region: system
  0000000000000000-ffffffffffffffff (prio 0, i/o): system
    0000000000000000-00000000bfffffff (prio 0, ram): alias ram-below-4g @pc.ram 0000000000000000-00000000bfffffff
    0000000000000000-ffffffffffffffff (prio -1, i/o): pci
      00000000000a0000-00000000000bffff (prio 1, i/o): vga-lowmem
      00000000000c0000-00000000000dffff (prio 1, rom): pc.rom
      00000000000e0000-00000000000fffff (prio 1, rom): alias isa-bios @pc.bios 0000000000020000-000000000003ffff
      00000000fd000000-00000000fdffffff (prio 1, ram): vga.vram
      00000000fe000000-00000000fe003fff (prio 1, i/o): virtio-pci
        00000000fe000000-00000000fe000fff (prio 0, i/o): virtio-pci-common-virtio-9p
        00000000fe001000-00000000fe001fff (prio 0, i/o): virtio-pci-isr-virtio-9p
        00000000fe002000-00000000fe002fff (prio 0, i/o): virtio-pci-device-virtio-9p
        00000000fe003000-00000000fe003fff (prio 0, i/o): virtio-pci-notify-virtio-9p
      00000000febc0000-00000000febdffff (prio 1, i/o): e1000-mmio
      00000000febf0000-00000000febf3fff (prio 1, i/o): nvme-bar0
        00000000febf0000-00000000febf1fff (prio 0, i/o): nvme
        00000000febf2000-00000000febf240f (prio 0, i/o): msix-table
        00000000febf3000-00000000febf300f (prio 0, i/o): msix-pba
      00000000febf4000-00000000febf4fff (prio 1, i/o): vga.mmio
        00000000febf4000-00000000febf417f (prio 0, i/o): edid
        00000000febf4400-00000000febf441f (prio 0, i/o): vga ioports remapped
        00000000febf4500-00000000febf4515 (prio 0, i/o): bochs dispi interface
        00000000febf4600-00000000febf4607 (prio 0, i/o): qemu extended regs
      00000000febf5000-00000000febf5fff (prio 1, i/o): virtio-9p-pci-msix
        00000000febf5000-00000000febf501f (prio 0, i/o): msix-table
        00000000febf5800-00000000febf5807 (prio 0, i/o): msix-pba
      00000000fffc0000-00000000ffffffff (prio 0, rom): pc.bios
    00000000000a0000-00000000000bffff (prio 1, i/o): alias smram-region @pci 00000000000a0000-00000000000bffff
    00000000000c0000-00000000000c3fff (prio 1, ram): alias pam-rom @pc.ram 00000000000c0000-00000000000c3fff
    00000000000c4000-00000000000c7fff (prio 1, ram): alias pam-rom @pc.ram 00000000000c4000-00000000000c7fff
    00000000000c8000-00000000000cbfff (prio 1, ram): alias pam-rom @pc.ram 00000000000c8000-00000000000cbfff
    00000000000cb000-00000000000cdfff (prio 1000, ram): alias kvmvapic-rom @pc.ram 00000000000cb000-00000000000cdfff
    00000000000cc000-00000000000cffff (prio 1, ram): alias pam-rom @pc.ram 00000000000cc000-00000000000cffff
    00000000000d0000-00000000000d3fff (prio 1, ram): alias pam-rom @pc.ram 00000000000d0000-00000000000d3fff
    00000000000d4000-00000000000d7fff (prio 1, ram): alias pam-rom @pc.ram 00000000000d4000-00000000000d7fff
    00000000000d8000-00000000000dbfff (prio 1, ram): alias pam-rom @pc.ram 00000000000d8000-00000000000dbfff
    00000000000dc000-00000000000dffff (prio 1, ram): alias pam-rom @pc.ram 00000000000dc000-00000000000dffff
    00000000000e0000-00000000000e3fff (prio 1, ram): alias pam-rom @pc.ram 00000000000e0000-00000000000e3fff
    00000000000e4000-00000000000e7fff (prio 1, ram): alias pam-ram @pc.ram 00000000000e4000-00000000000e7fff
    00000000000e8000-00000000000ebfff (prio 1, ram): alias pam-ram @pc.ram 00000000000e8000-00000000000ebfff
    00000000000ec000-00000000000effff (prio 1, ram): alias pam-ram @pc.ram 00000000000ec000-00000000000effff
    00000000000f0000-00000000000fffff (prio 1, ram): alias pam-rom @pc.ram 00000000000f0000-00000000000fffff
    00000000fec00000-00000000fec00fff (prio 0, i/o): ioapic
    00000000fed00000-00000000fed003ff (prio 0, i/o): hpet
    00000000fee00000-00000000feefffff (prio 4096, i/o): apic-msi
    0000000100000000-00000001bfffffff (prio 0, ram): alias ram-above-4g @pc.ram 00000000c0000000-000000017fffffff
```
