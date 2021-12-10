## Memory Virtualization

### Guest RAM configuration

The QEMU command-line option `-m [size=]megs[,slots=n,maxmem=size]` specifies the initial guest RAM size as well as the maximum guest RAM size and number of slots for memory chips (DIMMs).

The reason for the maximum size and slots is that QEMU emulates DIMM hotplug so the guest operating system can detect when new memory is added and removed using the same mechanism as on real hardware. This involves plugging or unplugging a DIMM into a slot, just like on a physical machine. In other words, changing the amount of memory available isn't done in byte units, it's done by changing the set of DIMMs plugged into the emulated machine.

The MemoryRegion is the link between guest physical address space and the RAMBlocks containing the memory. Each MemoryRegion has the ram_addr_t offset of the RAMBlock and each RAMBlock has a MemoryRegion pointer.

### Reference

[1] http://blog.vmsplice.net/2016/01/qemu-internals-how-guest-physical-ram.html
