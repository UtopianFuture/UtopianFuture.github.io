## QEMU Serial

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

```
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

```
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

可以看出来非常复杂。让我们对比一下 qemu 和 bmbt 中 pci 的注册流程。

qemu:

```
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

```
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

pci 设备比较复杂，我们分析一个简单的，和 serial 类似的设备  port92 这是一个 tcp/udp 用来通讯的端口。先看看 qemu 中的注册流程：

```
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

```
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

background

One drawback of the earlier 8250 UARTs and 16450 UARTs was that interrupts were generated for each byte received. This generated high rates of interrupts as transfer speeds increased. More critically, with only a 1-byte buffer there is a genuine risk that a received byte will be overwritten if interrupt service delays occur. To overcome these shortcomings, the 16550 series UARTs incorporated a 16-byte FIFO buffer with a programmable interrupt trigger of 1, 4, 8, or 14 bytes.
