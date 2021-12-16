## From keyboard to display

为什么要探究这个过程呢？在看 kernel 和 bmbt 代码的过程中，代码始终一块块孤零零的，脑子里没有镇个流程，不知道下一步该实现什么，总是跟着别人的脚步在走，感觉用 gdb 走一边这个流程能对整个系统认识更加深刻。

首先寻找到 kernel 中的 keyboard 是怎样处理字符的。

asm_common_interrupt

common_interrupt

__common_interrupt

```plain
i8042_kbd_write (port=<optimized out>, c=<optimized out>) at drivers/input/serio/i8042.c:376
#1  0xffffffff818ef11a in serio_write (data=244 '\364', serio=<optimized out>) at ./include/linux/serio.h:125
#2  ps2_do_sendbyte (ps2dev=ps2dev@entry=0xffff888106426800, byte=byte@entry=244 '\364', timeout=200, max_attempts=max_attempts@entry=2) at drivers/input/serio/libps2.c:40
#3  0xffffffff818ef591 in __ps2_command (ps2dev=ps2dev@entry=0xffff888106426800, param=param@entry=0x0 <fixed_percpu_data>, command=command@entry=244) at drivers/input/serio/libps2.c:265
#4  0xffffffff818efab1 in ps2_command (ps2dev=ps2dev@entry=0xffff888106426800, param=param@entry=0x0 <fixed_percpu_data>, command=command@entry=244) at drivers/input/serio/libps2.c:332
#5  0xffffffff818fa2c9 in atkbd_activate (atkbd=0xffff888106426800) at drivers/input/keyboard/atkbd.c:734
#6  0xffffffff818fadda in atkbd_connect (serio=0xffff888106424800, drv=<optimized out>) at drivers/input/keyboard/atkbd.c:1291
#7  0xffffffff818ed606 in serio_connect_driver (drv=0xffffffff830c36e0 <atkbd_drv>, serio=0xffff888106424800) at drivers/input/serio/serio.c:47
#8  serio_driver_probe (dev=0xffff888106424958) at drivers/input/serio/serio.c:778
#9  0xffffffff8179e692 in call_driver_probe (drv=0xffffffff830c3730 <atkbd_drv+80>, drv=0xffffffff830c3730 <atkbd_drv+80>, dev=0xffff888106424958) at drivers/base/dd.c:517
#10 really_probe (drv=0xffffffff830c3730 <atkbd_drv+80>, dev=0xffff888106424958) at drivers/base/dd.c:596
#11 really_probe (dev=0xffff888106424958, drv=0xffffffff830c3730 <atkbd_drv+80>) at drivers/base/dd.c:541
#12 0xffffffff8179e999 in __driver_probe_device (drv=drv@entry=0xffffffff830c3730 <atkbd_drv+80>, dev=dev@entry=0xffff888106424958) at drivers/base/dd.c:751
#13 0xffffffff8179ea33 in driver_probe_device (drv=drv@entry=0xffffffff830c3730 <atkbd_drv+80>, dev=dev@entry=0xffff888106424958) at drivers/base/dd.c:781
#14 0xffffffff8179eeed in __driver_attach (dev=0xffff888106424958, data=0xffffffff830c3730 <atkbd_drv+80>) at drivers/base/dd.c:1140
#15 0xffffffff8179c38e in bus_for_each_dev (bus=<optimized out>, start=start@entry=0x0 <fixed_percpu_data>, data=data@entry=0xffffffff830c3730 <atkbd_drv+80>,
    fn=fn@entry=0xffffffff8179ee30 <__driver_attach>) at drivers/base/bus.c:301
#16 0xffffffff8179debe in driver_attach (drv=drv@entry=0xffffffff830c3730 <atkbd_drv+80>) at drivers/base/dd.c:1157
#17 0xffffffff818ed7c1 in serio_attach_driver (drv=0xffffffff830c36e0 <atkbd_drv>) at drivers/input/serio/serio.c:807
#18 serio_handle_event (work=<optimized out>) at drivers/input/serio/serio.c:227
#19 0xffffffff810c4a49 in process_one_work (worker=worker@entry=0xffff8881001d8600, work=0xffffffff830c2cc0 <serio_event_work>) at kernel/workqueue.c:2297
#20 0xffffffff810c4c3d in worker_thread (__worker=0xffff8881001d8600) at kernel/workqueue.c:2444
#21 0xffffffff810cc32a in kthread (_create=0xffff888100317500) at kernel/kthread.c:319
#22 0xffffffff81004572 in ret_from_fork () at arch/x86/entry/entry_64.S:295
```

暂时没有跟踪到是哪个函数处理 keyboard 的输入，但是跟踪到了怎样输出。使用 serial 输出。具体是 `serial8250_tx_chars` 中的 `serial_out` 函数。

```plain
serial8250_tx_chars (up=up@entry=0xffffffff836d1c60 <serial8250_ports>) at drivers/tty/serial/8250/8250_port.c:1819
#1  0xffffffff8174ccf4 in serial8250_handle_irq (port=port@entry=0xffffffff836d1c60 <serial8250_ports>, iir=<optimized out>) at drivers/tty/serial/8250/8250_port.c:1932
#2  0xffffffff8174ce11 in serial8250_handle_irq (iir=<optimized out>, port=0xffffffff836d1c60 <serial8250_ports>) at drivers/tty/serial/8250/8250_port.c:1905
#3  serial8250_default_handle_irq (port=0xffffffff836d1c60 <serial8250_ports>) at drivers/tty/serial/8250/8250_port.c:1949
#4  0xffffffff817490e8 in serial8250_interrupt (irq=4, dev_id=0xffff8881058fd1a0) at drivers/tty/serial/8250/8250_core.c:126
#5  0xffffffff8111d892 in __handle_irq_event_percpu (desc=desc@entry=0xffff8881001cda00, flags=flags@entry=0xffffc90000003f54) at kernel/irq/handle.c:156
#6  0xffffffff8111d9e3 in handle_irq_event_percpu (desc=desc@entry=0xffff8881001cda00) at kernel/irq/handle.c:196
#7  0xffffffff8111da6b in handle_irq_event (desc=desc@entry=0xffff8881001cda00) at kernel/irq/handle.c:213
#8  0xffffffff81121ef3 in handle_edge_irq (desc=0xffff8881001cda00) at kernel/irq/chip.c:822
#9  0xffffffff810395a3 in generic_handle_irq_desc (desc=0xffff8881001cda00) at ./include/linux/irqdesc.h:158
#10 handle_irq (regs=<optimized out>, desc=0xffff8881001cda00) at arch/x86/kernel/irq.c:231
#11 __common_interrupt (regs=<optimized out>, vector=39) at arch/x86/kernel/irq.c:250
#12 0xffffffff81c085d8 in common_interrupt (regs=0xffffc9000064fc08, error_code=<optimized out>) at arch/x86/kernel/irq.c:240
#13 0xffffffff81e00cde in asm_common_interrupt () at ./arch/x86/include/asm/idtentry.h:629
```
