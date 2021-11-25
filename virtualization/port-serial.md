## QEMU Serial

### Serial in QEMU

串口通信是通过总线或信道向设备每次发送一比特的数据。QEMU 模拟的是 16550A UART 芯片，具体实现在 serial.c 文件中。现在分析 serial 是怎么将数据转发到不同的设备中。

QEMU 中 `SerialState` 用来模拟串口设备。

```c
struct SerialState {
    DeviceState parent;

    uint16_t divider;
    uint8_t rbr; /* 0x00 receive buffer register */
    uint8_t thr; /* 0x00 transmit holding register */
    uint8_t tsr; /* transmit shift register */
    uint8_t ier; /* 0x01 interrupt enable register */
    uint8_t iir; /* 0x02 interrupt identification register */
    uint8_t lcr; /* 0x03 line control register */
    uint8_t mcr; /* 0x04 modem control register */
    uint8_t lsr; /* 0x05 line status register */
    uint8_t msr; /* 0x06 modem status register */
    uint8_t scr;
    uint8_t fcr; /* 0x02 fifo control register */
    uint8_t fcr_vmstate; /* we can't write directly this value
                            it has side effects */
    /* NOTE: this hidden state is necessary for tx irq generation as
       it can be reset while reading iir */
    int thr_ipending;
    qemu_irq irq;
    CharBackend chr;
    int last_break_enable;
    uint32_t baudbase;
    uint32_t tsr_retry;
    guint watch_tag;
    bool wakeup;

    /* Time when the last byte was successfully sent out of the tsr */
    uint64_t last_xmit_ts;
    Fifo8 recv_fifo;
    Fifo8 xmit_fifo;
    /* Interrupt trigger level for recv_fifo */
    uint8_t recv_fifo_itl;

    QEMUTimer *fifo_timeout_timer;
    int timeout_ipending;           /* timeout interrupt pending state */

    uint64_t char_transmit_time;    /* time to transmit a char in ticks */
    int poll_msl;

    QEMUTimer *modem_status_poll;
    MemoryRegion io;
};
typedef struct SerialState SerialState;
```

serial 过程如下：

```plain
#0  serial_ioport_write (opaque=0x555556a20aa0, addr=1, val=2, size=1) at ../hw/char/serial.c:334
#1  0x0000555555c3c200 in memory_region_write_accessor (mr=0x555556a20c00, addr=1, value=0x7ffff135af58, size=1, shift=0, mask=255, attrs=...) at ../softmmu/memory.c:491
#2  0x0000555555c3c437 in access_with_adjusted_size (addr=1, value=0x7ffff135af58, size=1, access_size_min=1, access_size_max=1, access_fn=0x555555c3c113 <memory_region_write_accessor>,
    mr=0x555556a20c00, attrs=...) at ../softmmu/memory.c:552
#3  0x0000555555c3f4cf in memory_region_dispatch_write (mr=0x555556a20c00, addr=1, data=770, op=MO_8, attrs=...) at ../softmmu/memory.c:1502
#4  0x0000555555c933b5 in address_space_stb (as=0x5555566ca6c0 <address_space_io>, addr=1017, val=770, attrs=..., result=0x0) at /home/guanshun/gitlab/qemu/memory_ldst.c.inc:382
#5  0x0000555555b72146 in helper_outb (env=0x555556c45280, port=1017, data=770) at ../target/i386/tcg/misc_helper.c:47
#6  0x00007fff6412f3c5 in code_gen_buffer ()
#7  0x0000555555befd74 in cpu_tb_exec (cpu=0x555556c3ca10, itb=0x7fffa412f200, tb_exit=0x7ffff135b5b0) at ../accel/tcg/cpu-exec.c:190
#8  0x0000555555bf0cf8 in cpu_loop_exec_tb (cpu=0x555556c3ca10, tb=0x7fffa412f200, last_tb=0x7ffff135b5b8, tb_exit=0x7ffff135b5b0) at ../accel/tcg/cpu-exec.c:673
#9  0x0000555555bf0fe9 in cpu_exec (cpu=0x555556c3ca10) at ../accel/tcg/cpu-exec.c:798
#10 0x0000555555bffd71 in tcg_cpus_exec (cpu=0x555556c3ca10) at ../accel/tcg/tcg-accel-ops.c:68
#11 0x0000555555bb43d4 in mttcg_cpu_thread_fn (arg=0x555556c3ca10) at ../accel/tcg/tcg-accel-ops-mttcg.c:70
#12 0x0000555555eca1dd in qemu_thread_start (args=0x555556a70bd0) at ../util/qemu-thread-posix.c:521
#13 0x00007ffff6857609 in start_thread () from /lib/x86_64-linux-gnu/libpthread.so.0
#14 0x00007ffff677e293 in clone () from /lib/x86_64-linux-gnu/libc.so.6
```

也就是说当 load, store 指令需要访存是会使用调 `helper_outb`，qemu 根据访存的地址用 `memory_region_dispatch_write` 将数据 dispatch 到不同的设备。

对于 qemu 怎样模拟 serial 的硬件操作我们不需要关系，只需要知道 serial 也是通过 `qemu_set_irq` 进行操作即可。下面 `serial_ioport_write` 为通过 serial 进行写入。

```c
static void serial_ioport_write(void *opaque, hwaddr addr, uint64_t val,
                                unsigned size)
{
    SerialState *s = opaque;

    assert(size == 1 && addr < 8);
    trace_serial_write(addr, val);
    switch(addr) {
    default:
    case 0:
        if (s->lcr & UART_LCR_DLAB) {
            s->divider = deposit32(s->divider, 8 * addr, 8, val);
            serial_update_parameters(s);
        } else {
            s->thr = (uint8_t) val;
            if(s->fcr & UART_FCR_FE) {
                /* xmit overruns overwrite data, so make space if needed */
                if (fifo8_is_full(&s->xmit_fifo)) {
                    fifo8_pop(&s->xmit_fifo);
                }
                fifo8_push(&s->xmit_fifo, s->thr);
            }
            s->thr_ipending = 0;
            s->lsr &= ~UART_LSR_THRE;
            s->lsr &= ~UART_LSR_TEMT;
            serial_update_irq(s);
            if (s->tsr_retry == 0) {
                serial_xmit(s);
            }
        }
        break;
    ...
    }
}
```

关键在于怎样 serial 的注册过程，因为 bmbt 简化了 QOM 的 realize ，下面为正常 qemu 的注册流程。

```plain
#0  serial_realize (dev=0x555556a20a00, errp=0x7fffffffd300) at ../hw/char/serial.c:924
#1  0x0000555555ce0f03 in device_set_realized (obj=0x555556a20a00, value=true, errp=0x7fffffffd408) at ../hw/core/qdev.c:761
#2  0x0000555555cff560 in property_set_bool (obj=0x555556a20a00, v=0x55555766fbb0, name=0x555556041bd9 "realized", opaque=0x555556768b70, errp=0x7fffffffd408) at ../qom/object.c:2257
#3  0x0000555555cfd581 in object_property_set (obj=0x555556a20a00, name=0x555556041bd9 "realized", v=0x55555766fbb0, errp=0x7fffffffd570) at ../qom/object.c:1402
#4  0x0000555555cf8f1e in object_property_set_qobject (obj=0x555556a20a00, name=0x555556041bd9 "realized", value=0x55555766fa00, errp=0x7fffffffd570) at ../qom/qom-qobject.c:28
#5  0x0000555555cfd8f9 in object_property_set_bool (obj=0x555556a20a00, name=0x555556041bd9 "realized", value=true, errp=0x7fffffffd570) at ../qom/object.c:1472
#6  0x0000555555cdff23 in qdev_realize (dev=0x555556a20a00, bus=0x0, errp=0x7fffffffd570) at ../hw/core/qdev.c:389
#7  0x0000555555aa8053 in serial_isa_realizefn (dev=0x555556a20950, errp=0x7fffffffd570) at ../hw/char/serial-isa.c:79
#8  0x0000555555ce0f03 in device_set_realized (obj=0x555556a20950, value=true, errp=0x7fffffffd678) at ../hw/core/qdev.c:761
#9  0x0000555555cff560 in property_set_bool (obj=0x555556a20950, v=0x55555766fa40, name=0x555556041bd9 "realized", opaque=0x555556768b70, errp=0x7fffffffd678) at ../qom/object.c:2257
#10 0x0000555555cfd581 in object_property_set (obj=0x555556a20950, name=0x555556041bd9 "realized", v=0x55555766fa40, errp=0x5555566ce528 <error_fatal>) at ../qom/object.c:1402
#11 0x0000555555cf8f1e in object_property_set_qobject (obj=0x555556a20950, name=0x555556041bd9 "realized", value=0x55555766f8b0, errp=0x5555566ce528 <error_fatal>)
    at ../qom/qom-qobject.c:28
#12 0x0000555555cfd8f9 in object_property_set_bool (obj=0x555556a20950, name=0x555556041bd9 "realized", value=true, errp=0x5555566ce528 <error_fatal>) at ../qom/object.c:1472
#13 0x0000555555cdff23 in qdev_realize (dev=0x555556a20950, bus=0x555556a21e90, errp=0x5555566ce528 <error_fatal>) at ../hw/core/qdev.c:389
#14 0x0000555555cdff54 in qdev_realize_and_unref (dev=0x555556a20950, bus=0x555556a21e90, errp=0x5555566ce528 <error_fatal>) at ../hw/core/qdev.c:396
#15 0x0000555555ad8efd in isa_realize_and_unref (dev=0x555556a20950, bus=0x555556a21e90, errp=0x5555566ce528 <error_fatal>) at ../hw/isa/isa-bus.c:179
#16 0x0000555555aa8409 in serial_isa_init (bus=0x555556a21e90, index=0, chr=0x555556a5a660) at ../hw/char/serial-isa.c:167
#17 0x0000555555aa84a1 in serial_hds_isa_init (bus=0x555556a21e90, from=0, to=4) at ../hw/char/serial-isa.c:179
#18 0x0000555555b0651a in pc_superio_init (isa_bus=0x555556a21e90, create_fdctrl=true, no_vmport=false) at ../hw/i386/pc.c:1064
#19 0x0000555555b06a1d in pc_basic_device_init (pcms=0x55555691a400, isa_bus=0x555556a21e90, gsi=0x5555569c0000, rtc_state=0x7fffffffd9b0, create_fdctrl=true, hpet_irqs=4)
    at ../hw/i386/pc.c:1174
#20 0x0000555555b24ff3 in pc_init1 (machine=0x55555691a400, host_type=0x555555ff7bf4 "i440FX-pcihost", pci_type=0x555555ff7bed "i440FX") at ../hw/i386/pc_piix.c:241
#21 0x0000555555b2567b in pc_init_v6_0 (machine=0x55555691a400) at ../hw/i386/pc_piix.c:427
#22 0x000055555589f6f0 in machine_run_board_init (machine=0x55555691a400) at ../hw/core/machine.c:1232
#23 0x0000555555be80d7 in qemu_init_board () at ../softmmu/vl.c:2560
#24 0x0000555555be82b6 in qmp_x_exit_preconfig (errp=0x5555566ce528 <error_fatal>) at ../softmmu/vl.c:2634
#25 0x0000555555bea989 in qemu_init (argc=5, argv=0x7fffffffdda8, envp=0x7fffffffddd8) at ../softmmu/vl.c:3669
#26 0x000055555581fe85 in main (argc=5, argv=0x7fffffffdda8, envp=0x7fffffffddd8) at ../softmmu/main.c:49
```

可以看出来非常复杂。

### PCI in QEMU

也对比一下 qemu 和 bmbt 中 pci 的注册流程。

qemu:

```plain
#0  pci_qdev_realize (qdev=0x555556db7b10, errp=0x7fffffffd5e0) at ../hw/pci/pci.c:2089
#1  0x0000555555ce0f03 in device_set_realized (obj=0x555556db7b10, value=true, errp=0x7fffffffd6e8) at ../hw/core/qdev.c:761
#2  0x0000555555cff560 in property_set_bool (obj=0x555556db7b10, v=0x555556a47fd0, name=0x555556041bd9 "realized", opaque=0x555556768b70, errp=0x7fffffffd6e8) at ../qom/object.c:2257
#3  0x0000555555cfd581 in object_property_set (obj=0x555556db7b10, name=0x555556041bd9 "realized", v=0x555556a47fd0, errp=0x5555566ce528 <error_fatal>) at ../qom/object.c:1402
#4  0x0000555555cf8f1e in object_property_set_qobject (obj=0x555556db7b10, name=0x555556041bd9 "realized", value=0x555556a25af0, errp=0x5555566ce528 <error_fatal>)
    at ../qom/qom-qobject.c:28
#5  0x0000555555cfd8f9 in object_property_set_bool (obj=0x555556db7b10, name=0x555556041bd9 "realized", value=true, errp=0x5555566ce528 <error_fatal>) at ../qom/object.c:1472
#6  0x0000555555cdff23 in qdev_realize (dev=0x555556db7b10, bus=0x555556ae3e90, errp=0x5555566ce528 <error_fatal>) at ../hw/core/qdev.c:389
#7  0x0000555555cdff54 in qdev_realize_and_unref (dev=0x555556db7b10, bus=0x555556ae3e90, errp=0x5555566ce528 <error_fatal>) at ../hw/core/qdev.c:396
#8  0x00005555558c8a17 in pci_realize_and_unref (dev=0x555556db7b10, bus=0x555556ae3e90, errp=0x5555566ce528 <error_fatal>) at ../hw/pci/pci.c:2182
#9  0x00005555558c8a67 in pci_create_simple_multifunction (bus=0x555556ae3e90, devfn=0, multifunction=false, name=0x555555ff7bed "i440FX") at ../hw/pci/pci.c:2190
#10 0x00005555558c8a9f in pci_create_simple (bus=0x555556ae3e90, devfn=0, name=0x555555ff7bed "i440FX") at ../hw/pci/pci.c:2196
#11 0x00005555558345f1 in i440fx_init (host_type=0x555555ff7bf4 "i440FX-pcihost", pci_type=0x555555ff7bed "i440FX", pi440fx_state=0x7fffffffd9a8, address_space_mem=0x555556829700,
    address_space_io=0x555556808400, ram_size=8589934592, below_4g_mem_size=3221225472, above_4g_mem_size=5368709120, pci_address_space=0x5555567a0b00, ram_memory=0x5555568e3460)
    at ../hw/pci-host/i440fx.c:266
#12 0x0000555555b24dfb in pc_init1 (machine=0x55555691a400, host_type=0x555555ff7bf4 "i440FX-pcihost", pci_type=0x555555ff7bed "i440FX") at ../hw/i386/pc_piix.c:202
#13 0x0000555555b2567b in pc_init_v6_0 (machine=0x55555691a400) at ../hw/i386/pc_piix.c:427
#14 0x000055555589f6f0 in machine_run_board_init (machine=0x55555691a400) at ../hw/core/machine.c:1232
#15 0x0000555555be80d7 in qemu_init_board () at ../softmmu/vl.c:2560
#16 0x0000555555be82b6 in qmp_x_exit_preconfig (errp=0x5555566ce528 <error_fatal>) at ../softmmu/vl.c:2634
#17 0x0000555555bea989 in qemu_init (argc=5, argv=0x7fffffffdda8, envp=0x7fffffffddd8) at ../softmmu/vl.c:3669
#18 0x000055555581fe85 in main (argc=5, argv=0x7fffffffdda8, envp=0x7fffffffddd8) at ../softmmu/main.c:49
```

bmbt:

```plain
#0  pci_qdev_realize (pci_dev=0x5555559360a0 <__pci_i440fx>, name=0x5555556f00d2 "i440FX") at src/hw/pci/pci.c:1930
#1  0x00005555555f8dda in pci_create_simple_multifunction (bus=0x5555559304a0 <__pci_bus>, devfn=0, multifunction=false, name=0x5555556f00d2 "i440FX") at src/hw/pci/pci.c:2030
#2  0x00005555555f8e6a in pci_create_simple (bus=0x5555559304a0 <__pci_bus>, devfn=0, name=0x5555556f00d2 "i440FX") at src/hw/pci/pci.c:2039
#3  0x0000555555607b44 in i440fx_init (host_type=0x5555556f00d9 "i440FX-pcihost", pci_type=0x5555556f00d2 "i440FX", pi440fx_state=0x7fffffffdb90, address_space_mem=0x0,
    address_space_io=0x0, ram_size=134217728, below_4g_mem_size=134217728, above_4g_mem_size=0, pci_address_space=0x0, ram_memory=0x0) at src/hw/pci-host/i440fx.c:311
#4  0x00005555555f49f1 in pc_init1 (machine=0x555555940240 <__pcms>, host_type=0x5555556f00d9 "i440FX-pcihost", pci_type=0x5555556f00d2 "i440FX") at src/hw/i386/pc_piix.c:137
#5  0x00005555555f4d73 in pc_init_v4_2 (machine=0x555555940240 <__pcms>) at src/hw/i386/pc_piix.c:273
#6  0x00005555555ff3bd in machine_run_board_init (machine=0x555555940240 <__pcms>) at src/hw/core/machine.c:214
#7  0x000055555562bc38 in qemu_init () at src/qemu/vl.c:291
#8  0x00005555555cf121 in test_qemu_init () at src/main.c:158
#9  0x00005555555cf3ed in wip () at src/main.c:173
#10 0x00005555555d02a4 in greatest_run_suite (suite_cb=0x5555555cf395 <wip>, suite_name=0x5555556ed6d7 "wip") at src/main.c:176
#11 0x00005555555d136e in main (argc=1, argv=0x7fffffffde08) at src/main.c:185
```

可以看出来大大简化了，去除了 qdev 那一套。

### Port92 in QEMU

pci 设备比较复杂，我们分析一个简单的，和 serial 类似的设备  port92 这是一个 tcp/udp 用来通讯的端口。先看看 qemu 中的注册流程：

```plain
#0  isa_init_ioport (dev=0x5555568df650, ioport=146) at ../hw/isa/isa-bus.c:123
#1  0x0000555555ad8d54 in isa_register_ioport (dev=0x5555568df650, io=0x5555568df6f0, start=146) at ../hw/isa/isa-bus.c:131
#2  0x0000555555b0423a in port92_realizefn (dev=0x5555568df650, errp=0x7fffffffd5b0) at ../hw/i386/port92.c:96
#3  0x0000555555ce0f03 in device_set_realized (obj=0x5555568df650, value=true, errp=0x7fffffffd6b8) at ../hw/core/qdev.c:761
#4  0x0000555555cff560 in property_set_bool (obj=0x5555568df650, v=0x55555754f840, name=0x555556041bd9 "realized", opaque=0x555556768b70, errp=0x7fffffffd6b8) at ../qom/object.c:2257
#5  0x0000555555cfd581 in object_property_set (obj=0x5555568df650, name=0x555556041bd9 "realized", v=0x55555754f840, errp=0x5555566ce528 <error_fatal>) at ../qom/object.c:1402
#6  0x0000555555cf8f1e in object_property_set_qobject (obj=0x5555568df650, name=0x555556041bd9 "realized", value=0x555557777a60, errp=0x5555566ce528 <error_fatal>)
    at ../qom/qom-qobject.c:28
#7  0x0000555555cfd8f9 in object_property_set_bool (obj=0x5555568df650, name=0x555556041bd9 "realized", value=true, errp=0x5555566ce528 <error_fatal>) at ../qom/object.c:1472
#8  0x0000555555cdff23 in qdev_realize (dev=0x5555568df650, bus=0x555556ae5350, errp=0x5555566ce528 <error_fatal>) at ../hw/core/qdev.c:389
#9  0x0000555555cdff54 in qdev_realize_and_unref (dev=0x5555568df650, bus=0x555556ae5350, errp=0x5555566ce528 <error_fatal>) at ../hw/core/qdev.c:396
#10 0x0000555555ad8efd in isa_realize_and_unref (dev=0x5555568df650, bus=0x555556ae5350, errp=0x5555566ce528 <error_fatal>) at ../hw/isa/isa-bus.c:179
#11 0x0000555555ad8ec8 in isa_create_simple (bus=0x555556ae5350, name=0x555555ff143c "port92") at ../hw/isa/isa-bus.c:173
#12 0x0000555555b06669 in pc_superio_init (isa_bus=0x555556ae5350, create_fdctrl=true, no_vmport=false) at ../hw/i386/pc.c:1091
#13 0x0000555555b06a1d in pc_basic_device_init (pcms=0x55555691a400, isa_bus=0x555556ae5350, gsi=0x5555569b5210, rtc_state=0x7fffffffd9b0, create_fdctrl=true, hpet_irqs=4)
    at ../hw/i386/pc.c:1174
#14 0x0000555555b24ff3 in pc_init1 (machine=0x55555691a400, host_type=0x555555ff7bf4 "i440FX-pcihost", pci_type=0x555555ff7bed "i440FX") at ../hw/i386/pc_piix.c:241
#15 0x0000555555b2567b in pc_init_v6_0 (machine=0x55555691a400) at ../hw/i386/pc_piix.c:427
#16 0x000055555589f6f0 in machine_run_board_init (machine=0x55555691a400) at ../hw/core/machine.c:1232
#17 0x0000555555be80d7 in qemu_init_board () at ../softmmu/vl.c:2560
#18 0x0000555555be82b6 in qmp_x_exit_preconfig (errp=0x5555566ce528 <error_fatal>) at ../softmmu/vl.c:2634
#19 0x0000555555bea989 in qemu_init (argc=5, argv=0x7fffffffdda8, envp=0x7fffffffddd8) at ../softmmu/vl.c:3669
#20 0x000055555581fe85 in main (argc=5, argv=0x7fffffffdda8, envp=0x7fffffffddd8) at ../softmmu/main.c:49
```

关键的就是 `port92_realizefn` 函数。而注册这种简单的设备主要有两个工作：分配 memory region 和 qemu_irq 。

```c
static void port92_realizefn(DeviceState *dev, Error **errp)
{
    ISADevice *isadev = ISA_DEVICE(dev);
    Port92State *s = PORT92(dev);

    isa_register_ioport(isadev, &s->io, 0x92);
}
```

让我们看看是怎么做的。

```c
static inline void isa_init_ioport(ISADevice *dev, uint16_t ioport)
{
    if (dev && (dev->ioport_id == 0 || ioport < dev->ioport_id)) {
        dev->ioport_id = ioport;
    }
}

void isa_register_ioport(ISADevice *dev, MemoryRegion *io, uint16_t start)
{
    memory_region_add_subregion(isabus->address_space_io, start, io);
    isa_init_ioport(dev, start);
}
```

这里是分配 memory region 和 I/O 端口号。分配 qemu_irq 的暂时没有跟踪到。

而 bmbt 则大大简化了。

```plain
#0  QOM_port92_init () at src/hw/i386/pc.c:905
#1  0x00005555555f235e in pc_superio_init (isa_bus=0x5555559369e8 <__isabus>, create_fdctrl=true, no_vmport=false) at src/hw/i386/pc.c:1379
#2  0x00005555555f2477 in pc_basic_device_init (isa_bus=0x5555559369e8 <__isabus>, gsi=0x5555559de4d0, rtc_state=0x7fffffffdb98, create_fdctrl=true, no_vmport=false, has_pit=true,
    hpet_irqs=4) at src/hw/i386/pc.c:1467
#3  0x00005555555f4c37 in pc_init1 (machine=0x555555940240 <__pcms>, host_type=0x5555556f00d9 "i440FX-pcihost", pci_type=0x5555556f00d2 "i440FX") at src/hw/i386/pc_piix.c:179
#4  0x00005555555f4d73 in pc_init_v4_2 (machine=0x555555940240 <__pcms>) at src/hw/i386/pc_piix.c:273
#5  0x00005555555ff3bd in machine_run_board_init (machine=0x555555940240 <__pcms>) at src/hw/core/machine.c:214
#6  0x000055555562bc38 in qemu_init () at src/qemu/vl.c:291
#7  0x00005555555cf121 in test_qemu_init () at src/main.c:158
#8  0x00005555555cf3ed in wip () at src/main.c:173
#9  0x00005555555d02a4 in greatest_run_suite (suite_cb=0x5555555cf395 <wip>, suite_name=0x5555556ed6d7 "wip") at src/main.c:176
#10 0x00005555555d136e in main (argc=1, argv=0x7fffffffde08) at src/main.c:185
```

先是用 `QOM_port92_init` 分配 memory region 和指定中断引脚为 `a20_out`。然后使用 `qemu_allocate_irqs` 申请中断引脚，在 `port92_init` 中将分配好的 qemu_irq 赋值到 `&s->gpio`。

```c
  qemu_irq *a20_line;
  Port92State *port92;
  // port92 = isa_create_simple(isa_bus, "port92");
  port92 = QOM_port92_init();

  a20_line = qemu_allocate_irqs(handle_a20_line_change, first_cpu, 2);
#ifdef NEED_LATER
  i8042_setup_a20_line(i8042, a20_line[0]);
#endif
  port92_init(port92, a20_line[1]);
```

```c
static void port92_initfn(Port92State *s) {
  // Port92State *s = PORT92(obj);

  memory_region_init_io(&s->io, &port92_ops, s, "port92", 1);

  s->outport = 0;

  // qdev_init_gpio_out_named(DEVICE(obj), &s->a20_out, PORT92_A20_LINE, 1);
  qdev_init_gpio_out(&s->gpio, &s->a20_out, 1);
}

static void port92_realizefn(Port92State *s) {
#ifdef BMBT
  ISADevice *isadev = ISA_DEVICE(dev);
  Port92State *s = PORT92(dev);

  isa_register_ioport(isadev, &s->io, 0x92);
#endif
  io_add_memory_region(0x92, &s->io);
}

static Port92State *QOM_port92_init() {
  Port92State *port92 = &__port92;
  port92_initfn(port92);
  port92_realizefn(port92);

  return port92;
}
```

```c
static void port92_init(Port92State *s, qemu_irq a20_out) {
  // qdev_connect_gpio_out_named(DEVICE(dev), PORT92_A20_LINE, 0, a20_out);
  qdev_connect_gpio_out(&s->gpio, 0, a20_out);
}
```

```c
static inline void qdev_connect_gpio_out(GPIOList *dev_gpio, int n,
                                         qemu_irq pin) {
  assert(n == 0);
  *(dev_gpio->out[n]) = pin;
}
```

那么我们移植 serial 的思路就很清晰了，去掉 qdev 一系列复杂的调用，直接分配 memory region 和 qemu_irq 。当然 serial 会有和 port92 不一样的细节，搞清楚这些细节，哪些需要移植，哪些干掉。

### 问题

- 在 4.2.1 版本的 QEMU 中，superio 是挂载在 ISA 总线上的，但是 bmbt 中 ISA 形同虚设，那么 superio 设备要怎样和 CPU 进行数据交互呢？

- qdev 中的结构是设备挂载在总线上，那么设备怎样通过总线和 CPU 或 memory 交互呢？这个是物理设备的实现，而软件上的表现就是 PCIBus 结构体中的 PCIDevice 数组。

  ```c
  struct PCIBus {
    // BusState qbus;
    enum PCIBusFlags flags;
    PCIIOMMUFunc iommu_fn;
    void *iommu_opaque;
    uint8_t devfn_min;
    uint32_t slot_reserved_mask;
    pci_set_irq_fn set_irq;
    pci_map_irq_fn map_irq;
    pci_route_irq_fn route_intx_to_irq;
    void *irq_opaque;
    PCIDevice *devices[PCI_SLOT_MAX * PCI_FUNC_MAX];
    PCIDevice *parent_dev;
    MemoryRegion *address_space_mem;
    MemoryRegion *address_space_io;

    QLIST_HEAD(, PCIBus) child;  /* this will be replaced by qdev later */
    QLIST_ENTRY(PCIBus) sibling; /* this will be replaced by qdev later */

    /* The bus IRQ state is the logical OR of the connected devices.
       Keep a count of the number of devices with raised IRQs.  */
    int nirq;
    int *irq_count;

    Notifier machine_done;
  };
  ```

  如下就是在 ISAbus 上挂载设备的过程。

  ```c
  ISADevice *isa_create(ISABus *bus, const char *name)
  {
      DeviceState *dev;

      dev = qdev_create(BUS(bus), name);
      return ISA_DEVICE(dev);
  }

  ISADevice *isa_try_create(ISABus *bus, const char *name)
  {
      DeviceState *dev;

      dev = qdev_try_create(BUS(bus), name);
      return ISA_DEVICE(dev);
  }

  ISADevice *isa_create_simple(ISABus *bus, const char *name)
  {
      ISADevice *dev;

      dev = isa_create(bus, name);
      qdev_init_nofail(DEVICE(dev));
      return dev;
  }
  ```

  这样设计是为了便于管理，比如在虚拟机迁移的时候，我只需要知道一根总线就可以加载出下面挂载的所有设备。

- bmbt  应该是所有的设备都放在 PCIDevice 数组中，通过这个去访问。这种说法不全对，访问的设备的方法是给设备分配了 memory region 之后就就可以根据分配到 I/O 端口号去操作设备，而不是说有一个指向设备结构体的指针就行。

- serial 怎样在 bmbt 中申请 qemu_irq ，这是在 qemu 中申请的过程。

  ```c
  /*
   * isa_get_irq() returns the corresponding qemu_irq entry for the i8259.
   *
   * This function is only for special cases such as the 'ferr', and
   * temporary use for normal devices until they are converted to qdev.
   */
  qemu_irq isa_get_irq(ISADevice *dev, int isairq)
  {
      assert(!dev || ISA_BUS(qdev_get_parent_bus(DEVICE(dev))) == isabus);
      if (isairq < 0 || isairq > 15) {
          hw_error("isa irq %d invalid", isairq);
      }
      return isabus->irqs[isairq];
  }

  void isa_init_irq(ISADevice *dev, qemu_irq *p, int isairq)
  {
      assert(dev->nirqs < ARRAY_SIZE(dev->isairq));
      dev->isairq[dev->nirqs] = isairq;
      *p = isa_get_irq(dev, isairq);
      dev->nirqs++;
  }
  ```

  很简单，就是用 `isa_bus` 的 `irqs`

  ```c
  // serial_hds_isa_init(isa_bus, 0, MAX_ISA_SERIAL_PORTS);
    SerialState * serial;
    qemu_irq serial_irq;
    serial_irq = isa_bus->irqs[4];
    serial = QOM_serial_init(serial_irq);
  ```

### 移植过程

移植 serial 前：

![image-20211123085757901](/home/guanshun/.config/Typora/typora-user-images/image-20211123085757901.png)

移植 serial 后：

![image-20211123085709528](/home/guanshun/.config/Typora/typora-user-images/image-20211123085709528.png)

可以看出来，在同一个地方 ERROR。首先分析为什么这个地方会 ERROR 掉。

问题出在有些设备没有实现，如 ps2, serial, dma controller 等，而 seabios 中会用到这些设备，所以需要在 seabios 中将这些设备注释掉。

```tex
diff --git a/src/hw/dma.c b/src/hw/dma.c
index 20c9fbb..f048560 100644
--- a/src/hw/dma.c
+++ b/src/hw/dma.c
@@ -57,6 +57,7 @@ dma_floppy(u32 addr, int count, int isWrite)
 void
 dma_setup(void)
 {
+  return;
     // first reset the DMA controllers
     outb(0, PORT_DMA1_MASTER_CLEAR);
     outb(0, PORT_DMA2_MASTER_CLEAR);
diff --git a/src/hw/ps2port.c b/src/hw/ps2port.c
index 9b099e8..117bea8 100644
--- a/src/hw/ps2port.c
+++ b/src/hw/ps2port.c
@@ -59,7 +59,7 @@ i8042_flush(void)
 {
     dprintf(7, "i8042_flush\n");
     int i;
-    for (i=0; i<I8042_BUFFER_SIZE; i++) {
+    for (i=0; i<0; i++) {
         u8 status = inb(PORT_PS2_STATUS);
         if (! (status & I8042_STR_OBF))
             return 0;
diff --git a/src/hw/timer.c b/src/hw/timer.c
index b6f102e..67dd95c 100644
--- a/src/hw/timer.c
+++ b/src/hw/timer.c
@@ -111,7 +111,7 @@ timer_setup(void)
     cpuid(0, &eax, &ebx, &ecx, &edx);
     if (eax > 0)
         cpuid(1, &eax, &ebx, &ecx, &cpuid_features);
-    if (cpuid_features & CPUID_TSC)
+    if (cpuid_features & CPUID_TSC & 0)
         tsctimer_setup();
 }

diff --git a/src/serial.c b/src/serial.c
index 88349c8..84a7646 100644
--- a/src/serial.c
+++ b/src/serial.c
@@ -206,7 +206,8 @@ lpt_setup(void)
 {
     if (! CONFIG_LPT)
         return;
-    dprintf(3, "init lpt\n");
+    dprintf(1, "init lpt\n");
+    return;

     u16 count = 0;
     count += detect_parport(PORT_LPT1, 0x14, count);
```

打上 patch 后，ERROR 还是一样的，出在这：

![](/home/guanshun/.config/Typora/typora-user-images/image-20211123105822406.png)

注意 `REGS=0x3f8` ，应该就是没有找到 serial 设备。通过 gdb 看一下 serial 注册之后是什么样的，

![image-20211123143912166](/home/guanshun/.config/Typora/typora-user-images/image-20211123143912166.png)

再对比一下 port92 ，看看哪里出问题了。

![image-20211123144559549](/home/guanshun/.config/Typora/typora-user-images/image-20211123144559549.png)

这样看没有问题阿，为什么 bios 会挂呢？

其实没有挂，因为只实现了 serial 的一个 port ，而 serial 有 4 个 port ，所以剩下的报错，在每个 `detect_serial` 后都加入 `dprintf` 就发现是正常的。

```c
static const int isa_serial_io[MAX_ISA_SERIAL_PORTS] = {
    0x3f8, 0x2f8, 0x3e8, 0x2e8
};
static const int isa_serial_irq[MAX_ISA_SERIAL_PORTS] = {
    4, 3, 4, 3
};
```

```c
void
serial_setup(void)
{
    if (! CONFIG_SERIAL)
        return;
    dprintf(1, "init serial\n");

    u16 count = 0;
    count += detect_serial(PORT_SERIAL1, 0x0a, count);
    dprintf(1, "Found %d serial ports\n", count);
    count += detect_serial(PORT_SERIAL2, 0x0a, count);
    dprintf(1, "Found %d serial ports\n", count);
    count += detect_serial(PORT_SERIAL3, 0x0a, count);
    dprintf(1, "Found %d serial ports\n", count);
    count += detect_serial(PORT_SERIAL4, 0x0a, count);
    dprintf(1, "Found %d serial ports\n", count);

    // Equipment word bits 9..11 determing # serial ports
    set_equipment_flags(0xe00, count << 9);
}
```

![image-20211123153823962](/home/guanshun/.config/Typora/typora-user-images/image-20211123153823962.png)

接下来就把 4 个 port 都实现。这个很简单，加个 for 循环就行。

```c
static void serial_isa_realizefn(SerialState *s, qemu_irq irq, const hwaddr offset)
{
  serial_realize_core(s);
  memory_region_init_io(&s->io, &serial_io_ops, s, "serial", 8);
  qdev_init_gpio_out(&s->gpio, &s->irq, 1);
  io_add_memory_region(offset, &s->io);
  qdev_connect_gpio_out(&s->gpio, 0, irq);
}

void QOM_serial_init(ISABus *isa_bus) {
   for (int i = 0; i < MAX_ISA_SERIAL_PORTS; i++) {
      SerialState *serial = &__serial[i];
      serial_isa_realizefn(serial, isa_bus->irqs[isa_serial_irq[i]], isa_serial_io[i]);
   }
}
```

最后 bios 跑完就是这样的，会出现 "Found 4 serial ports" 。

![image-20211123161403005](/home/guanshun/.config/Typora/typora-user-images/image-20211123161403005.png)

这个地方实现有问题，其实内核只用了 1 个 serial ，QEMU 也只注册了一个。下面的是 QEMU 源码，serial_hd 只有在 i= 0 是 return true 。

```c
void serial_hds_isa_init(ISABus *isa_bus) {
  // int i;

  // assert(from >= 0);
  // assert(to <= MAX_ISA_SERIAL_PORTS);

  // for (i = from; i < to; ++i) {
  //   if (serial_hd(i)) {
  //     serial_isa_init(bus, i, serial_hd(i));
  //   }
  // }

  SerialState *serial = &__serial[0];
  serial_isa_init(serial, isa_bus->irqs[isa_serial_irq[0]], isa_serial_io[0], 0);
}
```

但只注册一个 serial 的话 seabios 过不了，即当 seabios 访问到 `0x2f9` 时会 `go_assert_not_reach` 。

![image-20211125215906463](/home/guanshun/.config/Typora/typora-user-images/image-20211125215906463.png)

那为什么 QEMU 不会报错呢？



接下来将 serial 的输出重定向到特定的文件中。

这个比较简单，只要在 serial 输出的地方重定向即可。

```c
static void serial_xmit(SerialState *s)
{
    do {
        assert(!(s->lsr & UART_LSR_TEMT));

        ...

        if (s->mcr & UART_MCR_LOOP) {
            /* in loopback mode, say that we just received a char */
            serial_receive1(s, &s->tsr, 1);
        } else {
          assert(fprintf(s->log, "%s", &s->tsr) == 1);
          fflush(s->log);
          // we just use serial output to a specify file
#ifdef BMBT
          int rc = qemu_chr_fe_write(&s->chr, &s->tsr, 1);

          if ((rc == 0 || (rc == -1 && errno == EAGAIN)) &&
              s->tsr_retry < MAX_XMIT_RETRY) {
            assert(s->watch_tag == 0);
            s->watch_tag = qemu_chr_fe_add_watch(&s->chr, G_IO_OUT | G_IO_HUP,
                                                 serial_watch_cb, s);
            if (s->watch_tag > 0) {
              s->tsr_retry++;
              return;
            }
          }
#endif
        }

        ...
}
```

### 进一步分析

这个地方实现有问题：

```c
static void serial_isa_init(ISABus *bus, int index, Chardev *chr)
{
    DeviceState *dev;
    ISADevice *isadev;

    isadev = isa_create(bus, TYPE_ISA_SERIAL);
    dev = DEVICE(isadev);
    qdev_prop_set_uint32(dev, "index", index);
    qdev_prop_set_chr(dev, "chardev", chr);
    qdev_init_nofail(dev);
}

static void serial_isa_realizefn(SerialState *s, qemu_irq irq,
                                 const hwaddr offset) {
  serial_realize_core(s);
  memory_region_init_io(&s->io, &serial_io_ops, s, "serial", 8);
  qdev_init_gpio_out(&s->gpio, &s->irq, 1);
  io_add_memory_region(offset, &s->io);
  qdev_connect_gpio_out(&s->gpio, 0, irq);
}
```

`qdev_prop_set_uint32` 和 `qdev_prop_set_chr` 不能简单的注释掉，这两个函数是注册设备属性的，而设备属性不是很懂，下面进一步分析。

首先看看 qemu 是怎样注册设备并设置属性的。

```c
ISADevice *isa_create(ISABus *bus, const char *name)
{
    DeviceState *dev;

    dev = qdev_create(BUS(bus), name);
    return ISA_DEVICE(dev);
}
```

这里 BUS 宏最终会调用到这里，

```c
Object *object_dynamic_cast_assert(Object *obj, const char *typename,
                                   const char *file, int line, const char *func)
{
    trace_object_dynamic_cast_assert(obj ? obj->class->type->name : "(null)",
                                     typename, file, line, func);

#ifdef CONFIG_QOM_CAST_DEBUG
    int i;
    Object *inst;

    for (i = 0; obj && i < OBJECT_CLASS_CAST_CACHE; i++) {
        if (atomic_read(&obj->class->object_cast_cache[i]) == typename) {
            goto out;
        }
    }

    inst = object_dynamic_cast(obj, typename);

    if (!inst && obj) {
        fprintf(stderr, "%s:%d:%s: Object %p is not an instance of type %s\n",
                file, line, func, obj, typename);
        abort();
    }

    assert(obj == inst);

    if (obj && obj == inst) {
        for (i = 1; i < OBJECT_CLASS_CAST_CACHE; i++) {
            atomic_set(&obj->class->object_cast_cache[i - 1],
                       atomic_read(&obj->class->object_cast_cache[i]));
        }
        atomic_set(&obj->class->object_cast_cache[i - 1], typename);
    }

out:
#endif
    return obj;
}
```

我们来看看为什么需要这要注册。QEMU 中设备是通过 qdev 管理的，然后又用 QOM 实现，所以追踪一下 ISABus 所有的父类。

```c
struct ISABus {
    /*< private >*/
    BusState parent_obj;
    /*< public >*/

    MemoryRegion *address_space;
    MemoryRegion *address_space_io;
    qemu_irq *irqs;
    IsaDma *dma[2];
}
```

```c
struct BusState {
    Object obj;
    DeviceState *parent;
    char *name;
    HotplugHandler *hotplug_handler;
    int max_index;
    bool realized;
    int num_children;
    QTAILQ_HEAD(, BusChild) children;
    QLIST_ENTRY(BusState) sibling;
};
```

```c
struct Object
{
    /*< private >*/
    ObjectClass *class;
    ObjectFree *free;
    GHashTable *properties;
    uint32_t ref;
    Object *parent;
};
```

```c
struct DeviceState {
    /*< private >*/
    Object parent_obj;
    /*< public >*/

    const char *id;
    char *canonical_path;
    bool realized;
    bool pending_deleted_event;
    QemuOpts *opts;
    int hotplugged;
    bool allow_unplug_during_migration;
    BusState *parent_bus;
    QLIST_HEAD(, NamedGPIOList) gpios;
    QLIST_HEAD(, BusState) child_bus;
    int num_child_bus;
    int instance_id_alias;
    int alias_required_for_version;
};
```
