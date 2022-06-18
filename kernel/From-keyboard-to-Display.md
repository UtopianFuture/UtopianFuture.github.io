## From keyboard to display

为什么要探究这个过程呢？在看 kernel 和 bmbt 代码的过程中，代码始终一块块孤零零的，脑子里没有整个流程，不知道下一步该实现什么，总是跟着别人的脚步在走，感觉用 gdb 走一遍这个流程能对整个系统认识更加深刻。

### 目录

- [执行流程](#执行流程)
  - [keyboard中断](#keyboard中断)
  - [tty处理字符](#tty处理字符)
  - [serial读取](#serial读取)
  - [QEMU模拟](#QEMU模拟)
- [用户态执行流程](#用户态执行流程)
- [用户态到内核态的上下文切换](#用户态到内核态的上下文切换)
- [fork新的kernel_thread](#fork新的kernel_thread)
- [kernel_thread处理字符输入](#kernel_thread处理字符输入)
- [serial中断](#serial中断)
- [Reference](#Reference)
- [说明](#说明)
- [经验](#经验)

### 执行流程

#### keyboard中断

首先我们要确定 keyboard 中断是怎样处理的。现在的猜想是 keyboard 按下一个键就会触发一次中断，这个中断貌似和 serial 中断处理流程类似。先看看相关文档。

基本流程应该是这样的：

> 1. The keyboard sends a scan code of the key to the keyboard controller (Scan code for key pressed and key released is different)
>
> 2) The keyboard controller interprets the scan code and stores it in a buffer
>
> 3) The keyboard controller sends a hardware interrupt to the processor. This is done by putting signal on “interrupt request line”: IRQ 1
>
> 4) The interrupt controller maps IRQ 1 into INT 9
>
> 5) An interrupt is a signal which tells the processor to stop what it was doing currently and do some special task
>
> 6) The processor invokes the “Interrupt handler” CPU fetches the address of “Interrupt Service Routine” (ISR) from “Interrupt Vector Table” maintained by the OS (Processor use the IRQ number for this)
>
> 7) The ISR reads the scan code from port 60h and decides whether to process it or pass the control to program for taking action.

> Basically, when a key is pressed, the keyboard controller tells a device called the PIC, to cause an interrupt. Because of the wiring of keyboard and PIC, IRQ #1 is the keyboard interrupt, so when a key is pressed, IRQ 1 is sent to the PIC. The role of the PIC will be to decide whether the CPU should be immediately notified of that IRQ or not and to translate the IRQ number into an *interrupt vector* (i.e. a number between 0 and 255) for the CPU's table.

但具体的处理还是需要看代码。

需要明确以下几件事情：

1. keyboard controller 在 la 中有么，是哪一个？
2. 中断号是哪个。

ok，键鼠是这样的，输入的信息会写入到串口的寄存器中（忘了是哪个寄存器），而串口的寄存器一旦写入了值，就是向中断控制器发起中断，在龙芯机器上是 extioi 中断控制器，在 x86 机器上是 ioapic。中断控制器介绍到中断后就会向 CPU 或更高一层的控制器发起中断，CPU 根据硬中断号得到对应的软中断号，然后进行层层转发，最后转发到串口注册的中断处理函数，进行处理。

#### tty处理字符

再寻找到 kernel 中的 keyboard 是怎样处理字符的。

设了很多断点，发现 keyboard 输入的字符是存储在 `tty_buffer.c` 中的 `receive_buf` 中。接下来看看为什么会存入到 tty_buffer 中，怎么存的，以及存到 tty_buffer 的字符用什么方式传输到 serial_port。

```plain
(gdb) p p
$22 = (unsigned char *) 0xffff88810431f429 "a"
(gdb) bt
#0  receive_buf (count=<optimized out>, head=0xffff88810431f400, port=0xffff888100a80000) at drivers/tty/tty_buffer.c:493
#1  flush_to_ldisc (work=0xffff888100a80008) at drivers/tty/tty_buffer.c:543
#2  0xffffffff810c4a49 in process_one_work (worker=worker@entry=0xffff88810401ea80, work=0xffff888100a80008) at kernel/workqueue.c:2297
#3  0xffffffff810c4c3d in worker_thread (__worker=0xffff88810401ea80) at kernel/workqueue.c:2444
#4  0xffffffff810cc32a in kthread (_create=0xffff88810400aec0) at kernel/kthread.c:319
#5  0xffffffff81004572 in ret_from_fork () at arch/x86/entry/entry_64.S:295
#6  0x0000000000000000 in ?? ()
```

我们从 `receive_buf` 开始看 char 是怎样传输到 serial_port 中去的。

```c
static int
receive_buf(struct tty_port *port, struct tty_buffer *head, int count)
{
	unsigned char *p = char_buf_ptr(head, head->read);
	const char *f = NULL;
	int n;

	if (~head->flags & TTYB_NORMAL)
		f = flag_buf_ptr(head, head->read);

	n = port->client_ops->receive_buf(port, p, f, count);
	if (n > 0)
		memset(p, 0, n);
	return n;
}
```

已经知道 `serial8250_tx_chars` 是打印字符到显示器上的，我在这里设置了一个断点，发现

```c
n = port->client_ops->receive_buf(port, p, f, count);
```

这条语句居然能直接跑到 `serial8250_tx_chars` ，有古怪，进一步跟踪。`tty_port_default_receive_buf` 是 `receive_buf` 的回调函数。之后是一系列的调用，这里不了解 tty 的工作原理，所以暂时不分析细节，我的关注点是**整个执行流程，然后是涉及到的 kernel 框架，再是设备的处理细节**。

```c
static int tty_port_default_receive_buf(struct tty_port *port,
					const unsigned char *p,
					const unsigned char *f, size_t count)
{
	int ret;
	struct tty_struct *tty;
	struct tty_ldisc *disc;

	tty = READ_ONCE(port->itty);
	if (!tty)
		return 0;

	disc = tty_ldisc_ref(tty);
	if (!disc)
		return 0;

	ret = tty_ldisc_receive_buf(disc, p, (char *)f, count);

	tty_ldisc_deref(disc);

	return ret;
}
```

最终是在这里将字符添加到 ldata 中，也就是说，`n_tty_data` 是缓存 tty 输入的数据。

```c
/**
 *	put_tty_queue		-	add character to tty
 *	@c: character
 *	@ldata: n_tty data
 *
 *	Add a character to the tty read_buf queue.
 *
 *	n_tty_receive_buf()/producer path:
 *		caller holds non-exclusive termios_rwsem
 */

static inline void put_tty_queue(unsigned char c, struct n_tty_data *ldata)
{
	*read_buf_addr(ldata, ldata->read_head) = c;
	ldata->read_head++;
}
```

从这里就可以清晰的看到 tty 的数据是怎样传输到 serial 中的（后来发现这里并不是传输到 serial_port 的过程，比这还要复杂，需要对 kernel 的执行框架有一定了解才行）。

```plain
#0  serial8250_start_tx (port=0xffffffff836d1c60 <serial8250_ports>) at drivers/tty/serial/8250/8250_port.c:1654
#1  0xffffffff817440ab in __uart_start (tty=<optimized out>) at drivers/tty/serial/serial_core.c:127
#2  0xffffffff817453d2 in uart_start (tty=0xffff88810666e800) at drivers/tty/serial/serial_core.c:137
#3  0xffffffff8174543e in uart_flush_chars (tty=<optimized out>) at drivers/tty/serial/serial_core.c:549
#4  0xffffffff81728ec9 in __receive_buf (count=<optimized out>, fp=<optimized out>, cp=<optimized out>, tty=0xffff88810666e800) at drivers/tty/n_tty.c:1581
#5  n_tty_receive_buf_common (tty=<optimized out>, cp=<optimized out>, fp=<optimized out>, count=<optimized out>, flow=flow@entry=1) at drivers/tty/n_tty.c:1674
#6  0xffffffff81729d54 in n_tty_receive_buf2 (tty=<optimized out>, cp=<optimized out>, fp=<optimized out>, count=<optimized out>) at drivers/tty/n_tty.c:1709
#7  0xffffffff8172bc22 in tty_ldisc_receive_buf (ld=ld@entry=0xffff8881027f49c0, p=p@entry=0xffff88810671e428 "d", f=f@entry=0x0 <fixed_percpu_data>, count=count@entry=1)
    at drivers/tty/tty_buffer.c:471
#8  0xffffffff8172c592 in tty_port_default_receive_buf (port=<optimized out>, p=0xffff88810671e428 "d", f=0x0 <fixed_percpu_data>, count=1) at drivers/tty/tty_port.c:39
#9  0xffffffff8172bfd1 in receive_buf (count=<optimized out>, head=0xffff88810671e400, port=0xffff888100a80000) at drivers/tty/tty_buffer.c:491
#10 flush_to_ldisc (work=0xffff888100a80008) at drivers/tty/tty_buffer.c:543
#11 0xffffffff810c4a49 in process_one_work (worker=worker@entry=0xffff888100a2b0c0, work=0xffff888100a80008) at kernel/workqueue.c:2297
#12 0xffffffff810c4c3d in worker_thread (__worker=0xffff888100a2b0c0) at kernel/workqueue.c:2444
#13 0xffffffff810cc32a in kthread (_create=0xffff888100a5a280) at kernel/kthread.c:319
#14 0xffffffff81004572 in ret_from_fork () at arch/x86/entry/entry_64.S:295
#15 0x0000000000000000 in ?? ()
```

`tty->ops->flush_chars(tty);` 这里的回调函数就是 `uart_flush_chars`，

```c
static void __receive_buf(struct tty_struct *tty, const unsigned char *cp,
			  const char *fp, int count)
{
	struct n_tty_data *ldata = tty->disc_data;
	bool preops = I_ISTRIP(tty) || (I_IUCLC(tty) && L_IEXTEN(tty));

	if (ldata->real_raw)
		n_tty_receive_buf_real_raw(tty, cp, fp, count);
	else if (ldata->raw || (L_EXTPROC(tty) && !preops))
		n_tty_receive_buf_raw(tty, cp, fp, count);
	else if (tty->closing && !L_EXTPROC(tty))
		n_tty_receive_buf_closing(tty, cp, fp, count);
	else {
		n_tty_receive_buf_standard(tty, cp, fp, count);

		flush_echoes(tty);
		if (tty->ops->flush_chars)
			tty->ops->flush_chars(tty); // 这里会跑到 serial 中去
	}

	if (ldata->icanon && !L_EXTPROC(tty))
		return;

	/* publish read_head to consumer */
	smp_store_release(&ldata->commit_head, ldata->read_head); // 这里应该就是将字符发送给 serial 的地方
															  // 但为什么用这种方式呢？
	if (read_cnt(ldata)) {
		kill_fasync(&tty->fasync, SIGIO, POLL_IN);
		wake_up_interruptible_poll(&tty->read_wait, EPOLLIN);
	}
}
```

而这里 `start_tx` 的回调函数就是 `serial8250_start_tx`（这里只列出关键点），

```c
static void __uart_start(struct tty_struct *tty)
{
	struct uart_state *state = tty->driver_data;
	struct uart_port *port = state->uart_port;

	if (port && !uart_tx_stopped(port))
		port->ops->start_tx(port);
}
```

```plain
#0  __start_tx (port=0xffffffff836d1c60 <serial8250_ports>) at drivers/tty/serial/8250/8250_port.c:1565
#1  serial8250_start_tx (port=0xffffffff836d1c60 <serial8250_ports>) at drivers/tty/serial/8250/8250_port.c:1667
#2  0xffffffff817440ab in __uart_start (tty=<optimized out>) at drivers/tty/serial/serial_core.c:127
```

发现 `__start_tx` 执行完后就直接处理中断，然后执行到 `serial8250_tx_chars` 了，为什么会这样呢？

```c
static inline void __start_tx(struct uart_port *port)
{
	struct uart_8250_port *up = up_to_u8250p(port);

	if (up->dma && !up->dma->tx_dma(up))
		return;

	if (serial8250_set_THRI(up)) {
		if (up->bugs & UART_BUG_TXEN) {
			unsigned char lsr;

			lsr = serial_in(up, UART_LSR);
			up->lsr_saved_flags |= lsr & LSR_SAVE_FLAGS;
			if (lsr & UART_LSR_THRE)
				serial8250_tx_chars(up);
		}
	}

	/*
	 * Re-enable the transmitter if we disabled it.
	 */
	if (port->type == PORT_16C950 && up->acr & UART_ACR_TXDIS) {
		up->acr &= ~UART_ACR_TXDIS;
		serial_icr_write(up, UART_ACR, up->acr);
	}
}
```

知道了，我们看看 `uart_start`，

```c
static void uart_start(struct tty_struct *tty)
{
	struct uart_state *state = tty->driver_data;
	struct uart_port *port;
	unsigned long flags;

	port = uart_port_lock(state, flags);
	__uart_start(tty);
	uart_port_unlock(port, flags);
}
```

`uart_port_unlock` 是 unlock 的过程，至于为什么需要 lock 和 unlock 我暂时还不清楚，但是 unlock 之后就会处理中断，而上面说到 `serial8250_tx_chars` 就是中断的处理过程。但是 unlock 之后执行的中断不是由于需要处理输入的字符，因为这个时候 tty_buffer 中的字符还没有传输给 serial_port，这个传输的过程我们之后会看到，我们来看看执行完 unlock 之后为什么需要执行中断处理函数，而这个中断和 serial 有什么关系，这个时候还没有获取到输入的字符（这个没有搞清楚，需要对 kernel 更深入的理解才能懂吧）。

这个 unlock 的过程：

```plain
#0  _raw_spin_unlock_irqrestore (lock=lock@entry=0xffffffff836d1c60 <serial8250_ports>, flags=flags@entry=646) at ./include/linux/spinlock_api_smp.h:161
#1  0xffffffff817453dd in spin_unlock_irqrestore (flags=646, lock=0xffffffff836d1c60 <serial8250_ports>) at ./include/linux/spinlock.h:418
#2  uart_start (tty=0xffff888100b17000) at drivers/tty/serial/serial_core.c:138
#3  0xffffffff8174543e in uart_flush_chars (tty=<optimized out>) at drivers/tty/serial/serial_core.c:549
#4  0xffffffff81728ec9 in __receive_buf (count=<optimized out>, fp=<optimized out>, cp=<optimized out>, tty=0xffff888100b17000) at drivers/tty/n_tty.c:1581
```

在 `preempt_enable` 执行之后就会处理中断。

```c
static inline void __raw_spin_unlock_irqrestore(raw_spinlock_t *lock,
					    unsigned long flags)
{
	spin_release(&lock->dep_map, _RET_IP_);
	do_raw_spin_unlock(lock);
	local_irq_restore(flags);
	preempt_enable();
}
```

```c
#define preempt_enable()			barrier()
#define barrier() __asm__ __volatile__("": : :"memory") // 这个 barrier 是干什么的，memory barrier？
```

这个中断处理完就继续执行 tty_buffer 的工作。

上面我们知道 `__receive_buf` 函数是处理输入的字符的，从注释中我们猜测应该是在这里将 buf 中的字符发送给 serial，因为 `ldata` 就是存储字符的数据结构，我们来看看是不是。

```c
/* publish read_head to consumer */
	smp_store_release(&ldata->commit_head, ldata->read_head); // 这里应该就是将字符发送给 serial 的地方
															  // 但为什么用这种方式呢？
```

```c
#define smp_store_release(p, v)			\
do {						\
	barrier();				\ // 这个地方涉及到 memory barrier，需要搞懂
	WRITE_ONCE(*p, v);			\ // 内存屏障，保证这条指令前的所有访存指令都执行完了
} while (0)
```

```c
#define WRITE_ONCE(x, val)				\ // 这个代码看不懂，应该就是向指定的地址写值
({							\
	union { typeof(x) __val; char __c[1]; } __u =	\
		{ .__val = (val) }; 			\
	__write_once_size(&(x), __u.__c, sizeof(x));	\
	__u.__val;					\
})
```

#### serial读取

那么键盘输入的字符怎样读取到 tty_buffer 中呢？

```plain
#0  serial8250_read_char (up=up@entry=0xffffffff836d1c60 <serial8250_ports>, lsr=lsr@entry=97 'a')
    at drivers/tty/serial/8250/8250_port.c:1729
#1  0xffffffff8174a460 in serial8250_rx_chars (up=up@entry=0xffffffff836d1c60 <serial8250_ports>,
    lsr=lsr@entry=97 'a') at drivers/tty/serial/8250/8250_port.c:1784
#2  0xffffffff8174cd6f in serial8250_handle_irq (
    port=port@entry=0xffffffff836d1c60 <serial8250_ports>, iir=<optimized out>)
    at drivers/tty/serial/8250/8250_port.c:1927
#3  0xffffffff8174ce11 in serial8250_handle_irq (iir=<optimized out>,
    port=0xffffffff836d1c60 <serial8250_ports>) at drivers/tty/serial/8250/8250_port.c:1905
#4  serial8250_default_handle_irq (port=0xffffffff836d1c60 <serial8250_ports>)
    at drivers/tty/serial/8250/8250_port.c:1949
#5  0xffffffff817490e8 in serial8250_interrupt (irq=4, dev_id=0xffff8881045601c0)
    at drivers/tty/serial/8250/8250_core.c:126
#6  0xffffffff8111d892 in __handle_irq_event_percpu (desc=desc@entry=0xffff8881001cda00,
    flags=flags@entry=0xffffc90000003f54) at kernel/irq/handle.c:156
#7  0xffffffff8111d9e3 in handle_irq_event_percpu (desc=desc@entry=0xffff8881001cda00)
    at kernel/irq/handle.c:196
    at drivers/tty/serial/8250/8250_core.c:126
#6  0xffffffff8111d892 in __handle_irq_event_percpu (desc=desc@entry=0xffff8881001cda00,
    flags=flags@entry=0xffffc90000003f54) at kernel/irq/handle.c:156
#7  0xffffffff8111d9e3 in handle_irq_event_percpu (desc=desc@entry=0xffff8881001cda00)
    at kernel/irq/handle.c:196
#8  0xffffffff8111da6b in handle_irq_event (desc=desc@entry=0xffff8881001cda00)
    at kernel/irq/handle.c:213
#9  0xffffffff81121ef3 in handle_edge_irq (desc=0xffff8881001cda00) at kernel/irq/chip.c:822
#10 0xffffffff810395a3 in generic_handle_irq_desc (desc=0xffff8881001cda00)
    at ./include/linux/irqdesc.h:158
#11 handle_irq (regs=<optimized out>, desc=0xffff8881001cda00) at arch/x86/kernel/irq.c:231
#12 __common_interrupt (regs=<optimized out>, vector=39) at arch/x86/kernel/irq.c:250
#13 0xffffffff81c085d8 in common_interrupt (regs=0xffffffff82e03d68, error_code=<optimized out>)
    at arch/x86/kernel/irq.c:240
#14 0xffffffff81e00cde in asm_common_interrupt () at ./arch/x86/include/asm/idtentry.h:629
```

也是处理 serial_interrupt 来获取输入，为什么会这样？键盘和 serial 到底有什么关系。

我们来看看 `serial8250_read_char` 或许会有收获。

```c
void serial8250_read_char(struct uart_8250_port *up, unsigned char lsr)
{
	struct uart_port *port = &up->port;
	unsigned char ch;
	char flag = TTY_NORMAL;

	if (likely(lsr & UART_LSR_DR))
		ch = serial_in(up, UART_RX); // ch 就是输入的字符
	...

	port->icount.rx++;

	lsr |= up->lsr_saved_flags;
	up->lsr_saved_flags = 0;

	...

		/*
		 * Mask off conditions which should be ignored.
		 */
		lsr &= port->read_status_mask;

		if (lsr & UART_LSR_BI) {
			dev_dbg(port->dev, "handling break\n");
			flag = TTY_BREAK;
		} else if (lsr & UART_LSR_PE)
			flag = TTY_PARITY;
		else if (lsr & UART_LSR_FE)
			flag = TTY_FRAME;
	}
	if (uart_prepare_sysrq_char(port, ch))
		return;

	uart_insert_char(port, lsr, UART_LSR_OE, ch, flag);
}
EXPORT_SYMBOL_GPL(serial8250_read_char);
```

现在可以确定，**键盘输入后字符直接放到 serial 的 rx 寄存器，然后触发中断**，通过上面的执行流程读取，再调用 `uart_insert_char` 函数写入到 tty_buffer 中。

ok，现在整个流程闭环了，唯一的问题是 serial 中断怎样触发，其实应该开中断就可以了，需要测试。

或许 tty_buffer 将字符写入到 serial 这个过程能有帮助。

尝试了很多方法，还是不知道 serial 是怎样触发中断的，现在看看 qemu 是怎么模拟中断的，希望会有帮助。详细分析见下面的 [qemu 模拟](# QEMU 模拟)子部分。

#### serial 输出

再看看是怎样输出到屏幕中的。使用 serial 输出，具体是 `serial8250_tx_chars` 中的 `serial_out` 函数。

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

```c
static inline void serial_out(struct uart_8250_port *up, int offset, int value)
{
	up->port.serial_out(&up->port, offset, value);
}
```

而 `serial_out` 的回调函数是 `io_serial_out`，

```c
static void io_serial_out(struct uart_port *p, int offset, int value)
{
	offset = offset << p->regshift;
	outb(value, p->iobase + offset);
}
```

outb 是封装 outb 指令的宏，其实最后还是通过 outb 指令输出。

```c
#define outb outb
BUILDIO(b, b, char)
#define BUILDIO(bwl, bw, type)						\
static inline void out##bwl(unsigned type value, int port)		\
{									\
	asm volatile("out" #bwl " %" #bw "0, %w1"			\
		     : : "a"(value), "Nd"(port));			\
}
```

这就是整个后端的输出过程。

分析一下 `serial8250_tx_chars` 的执行过程。

```c
void serial8250_tx_chars(struct uart_8250_port *up)
{
	struct uart_port *port = &up->port;
	struct circ_buf *xmit = &port->state->xmit;
	int count;

	...

	count = up->tx_loadsz; // 整个 port 缓冲的字符数
	do {
		serial_out(up, UART_TX, xmit->buf[xmit->tail]); // 每次输出一个字符
		if (up->bugs & UART_BUG_TXRACE) {
			serial_in(up, UART_SCR);
		}
		xmit->tail = (xmit->tail + 1) & (UART_XMIT_SIZE - 1); // 指向下一个字符
		port->icount.tx++;
		if (uart_circ_empty(xmit))
			break;
		if ((up->capabilities & UART_CAP_HFIFO) &&
		    (serial_in(up, UART_LSR) & BOTH_EMPTY) != BOTH_EMPTY)
			break;
		/* The BCM2835 MINI UART THRE bit is really a not-full bit. */
		if ((up->capabilities & UART_CAP_MINI) &&
		    !(serial_in(up, UART_LSR) & UART_LSR_THRE))
			break;
	} while (--count > 0);

	if (uart_circ_chars_pending(xmit) < WAKEUP_CHARS)
		uart_write_wakeup(port);

	/*
	 * With RPM enabled, we have to wait until the FIFO is empty before the
	 * HW can go idle. So we get here once again with empty FIFO and disable
	 * the interrupt and RPM in __stop_tx()
	 */
	if (uart_circ_empty(xmit) && !(up->capabilities & UART_CAP_RPM))
		__stop_tx(up);
}
EXPORT_SYMBOL_GPL(serial8250_tx_chars);
```

这是整个 serial port 的字符

```plain
(gdb) p up->port->state->xmit
$11 = {
  buf = 0xffff88810260c000 "Loading, please wait...\r\nStarting version 245.4-4ubuntu3.13\r\nBegin: Loading essential drivers ... done.\r\nBegin: Running /scripts/init-premount ... done.\r\n
Begin: Mounting root file system ... Begin: Running /scripts/local-top ... done.\r\nBegin: Running /scripts/local-premount ... Begin: Waiting for suspend/resume device ... Begin: Running /sc
ripts/local-block ... done.\r\ndone.\r\nGave up waiting for suspend/resume device\r\ndone.\r\nNo root device specified. Boot arguments must include a root= parameter.\r\n\r\n\r\nBusyBox v1.3
0.1 (Ubuntu 1:1.30.1-4ubuntu6.4) built-in shell (ash)\r\nEnter 'help' for a list of built-in commands.\r\n\r\n(initramfs) \033[6ne", head = 639, tail = 638}
```

但是这不是我想要了解的，还需要分析整个中断的过程等等。

貌似分析到这里就分析不下去了，因为很多东西不懂。那么就先把涉及到的东西搞懂，再进一步。需要理解的地方：

- 用户态的执行流程是怎样的？（这个暂时不好分析，因为不知道用户态进程是哪个）
- 用户态到内核态的上下文切换在哪里，怎么做的？（这个是切入点）
  - 用户态到内核态的上下文切换

- kernel thread, workqueue 的设计思想是什么？ （[搞定](https://github.com/UtopianFuture/UtopianFuture.github.io/blob/master/kernel/workqueue.md)）
  - kernel thread, workqueu 的设计及实现
  - `schedule` 进程调度（[搞定](https://github.com/UtopianFuture/UtopianFuture.github.io/blob/master/kernel/Process-Schedule.md)）

- tty_buffer 中的字符是怎样发送到 serial_port 中去的。
- `preempt_enable` 执行完后就会执行一次中断，这个中断是由哪个发出来的，怎样判断中断来源？
  - `preempt_enable` 之后就会执行中断，这些中断是怎样发起的。中断的发起机制。

- 在调试的过程中，还执行过中断上下文切换的代码，汇编写的，搞清楚。
  - 中断的上下文切换

- `serial8250_tx_chars` 也是中断处理过程的一部分，serial 发的中断么？
  - 中断的处理过程。

#### QEMU模拟

在 qemu 中是这样模拟字符输入的，

```plain
#0  kvm_vm_ioctl (s=0x55555681c140, type=-1073172889) at ../accel/kvm/kvm-all.c:3008
#1  0x0000555555d090af in kvm_set_irq (s=0x55555681c140, irq=4, level=0) at ../accel/kvm/kvm-all.c:1691
#2  0x0000555555b6fcbd in kvm_pic_set_irq (opaque=0x0, irq=4, level=0) at ../hw/i386/kvm/i8259.c:119
#3  0x0000555555d4018c in qemu_set_irq (irq=0x555556fe6690, level=0) at ../hw/core/irq.c:45
#4  0x0000555555b39e8a in gsi_handler (opaque=0x555556ba45d0, n=4, level=0) at ../hw/i386/x86.c:599
#5  0x0000555555d4018c in qemu_set_irq (irq=0x555556ba7800, level=0) at ../hw/core/irq.c:45
#6  0x000055555593b0ed in qemu_irq_lower (irq=0x555556ba7800)
    at /home/guanshun/gitlab/qemu-newest/include/hw/irq.h:17
#7  0x000055555593b65f in serial_update_irq (s=0x555556f69180) at ../hw/char/serial.c:144
#8  0x000055555593cb3e in serial_receive1 (opaque=0x555556f69180,
    buf=0x7fffffffca00 "amffffc90000003e80,40#51c3Z1,ffffffff831b9faa,1#a3$Z1,ffffffff831b9faa,1#a3-event
s+;exec-events+;vContSupported+;QThreadEvents+;no-resumed+;xmlRegisters=i386#6a", size=1)
    at ../hw/char/serial.c:621
#9  0x0000555555e42c7a in mux_chr_read (opaque=0x5555569b8640,
    buf=0x7fffffffca00 "amffffc90000003e80,40#51c3Z1,ffffffff831b9faa,1#a3$Z1,ffffffff831b9faa,1#a3-event
s+;exec-events+;vContSupported+;QThreadEvents+;no-resumed+;xmlRegisters=i386#6a", size=1)
    at ../chardev/char-mux.c:235
#10 0x0000555555e49fd6 in qemu_chr_be_write_impl (s=0x555556929f40,
    buf=0x7fffffffca00 "amffffc90000003e80,40#51c3Z1,ffffffff831b9faa,1#a3$Z1,ffffffff831b9faa,1#a3-event
s+;exec-events+;vContSupported+;QThreadEvents+;no-resumed+;xmlRegisters=i386#6a", len=1)
    at ../chardev/char.c:201
#11 0x0000555555e4a03e in qemu_chr_be_write (s=0x555556929f40,
    buf=0x7fffffffca00 "amffffc90000003e80,40#51c3Z1,ffffffff831b9faa,1#a3$Z1,ffffffff831b9faa,1#a3-event
s+;exec-events+;vContSupported+;QThreadEvents+;no-resumed+;xmlRegisters=i386#6a", len=1)
    at ../chardev/char.c:213
#12 0x0000555555e4cb34 in fd_chr_read (chan=0x555556a45b10, cond=G_IO_IN, opaque=0x555556929f40)
    at ../chardev/char-fd.c:73
#13 0x0000555555d4fb13 in qio_channel_fd_source_dispatch (source=0x555556a39760,
    callback=0x555555e4c9fd <fd_chr_read>, user_data=0x555556929f40) at ../io/channel-watch.c:84
#14 0x00007ffff773a04e in g_main_context_dispatch () from /lib/x86_64-linux-gnu/libglib-2.0.so.0
#15 0x0000555555f1dd0d in glib_pollfds_poll () at ../util/main-loop.c:232
#16 0x0000555555f1dd8b in os_host_main_loop_wait (timeout=1000000000) at ../util/main-loop.c:255
#17 0x0000555555f1de9c in main_loop_wait (nonblocking=0) at ../util/main-loop.c:531
#18 0x0000555555bf1c30 in qemu_main_loop () at ../softmmu/runstate.c:726
#19 0x000055555583b6fa in main (argc=15, argv=0x7fffffffdca8, envp=0x7fffffffdd28)
    at ../softmmu/main.c:50
```

QEMU 用线程——`qemu_main_loop` 来模拟 VCPU 的运行，线程——`main_loop_wait` 来响应 I/O 请求。`main_loop_wait` 使用了 epoll 的方式，接收到 host 发来的中断后进行转发，这里转发到 `serial_receive1`，然后 serial 通过 `serial_update_irq` 发起中断，进入到 KVM，在 `kvm_vm_ioctl` 中使用 `ioctl` 系统调用，这里就跟不下去了，再想办法。

```c
int kvm_vm_ioctl(KVMState *s, int type, ...)
{
    int ret;
    void *arg;
    va_list ap;

    va_start(ap, type);
    arg = va_arg(ap, void *);
    va_end(ap);

    trace_kvm_vm_ioctl(type, arg);
    ret = ioctl(s->vmfd, type, arg);
    if (ret == -1) {
        ret = -errno;
    }
    return ret;
}
```

KVM 会修改 VMCS，然后又会执行 vm entry 进入到 guest，这样完成一个完整的 I/O 请求。

### 用户态执行流程

🚧

### 用户态到内核态的上下文切换

🚧

### fork新的kernel_thread

`ret_from_fork` 是上下文切换的函数，这应该是内核态，用户态怎么处理的？

```c
/*
 * A newly forked process directly context switches into this address.
 *
 * rax: prev task we switched from
 * rbx: kernel thread func (NULL for user thread)
 * r12: kernel thread arg
 */
.pushsection .text, "ax"
SYM_CODE_START(ret_from_fork)
	UNWIND_HINT_EMPTY
	movq	%rax, %rdi
	call	schedule_tail			/* rdi: 'prev' task parameter */

	testq	%rbx, %rbx			/* from kernel_thread? */
	jnz	1f				/* kernel threads are uncommon */

2:
	UNWIND_HINT_REGS
	movq	%rsp, %rdi
	call	syscall_exit_to_user_mode	/* returns with IRQs disabled */
	jmp	swapgs_restore_regs_and_return_to_usermode // 2. 返回 user mode

1:
	/* kernel thread */
	UNWIND_HINT_EMPTY
	movq	%r12, %rdi
	CALL_NOSPEC rbx // 返回 kernel thread
	/*
	 * A kernel thread is allowed to return here after successfully
	 * calling kernel_execve().  Exit to userspace to complete the execve()
	 * syscall.
	 */
	movq	$0, RAX(%rsp)
	jmp	2b
SYM_CODE_END(ret_from_fork)
.popsection
```

这里涉及到 kernel thread —— 即运行在内核态，执行周期性任务的线程，没有普通进程的上下文切换。

`ret_from_fork` 的作用是终止 `fork()`, `vfork()` 和 `clone()` 系统调用。从[这里](https://github.com/UtopianFuture/UtopianFuture.github.io/blob/master/linux-note/others.md#ret_from_fork)我们可以知道 `ret_from_fork` 会完成进程切换的剩余工作，也就是切换到 fork 出来的新进程，我们来分析一下它是怎么做的。

当执行中断或异常的处理程序后需要返回到原来的进程，在返回前还需要处理以下几件事：

1. 并发执行的内核控制路径数（kernel control path）

   如果只有一个，CPU 必须切换回用户模式。

2. 待处理进程切换请求

   如果有任何请求，内核必须进行进程调度；否则，控制权返回到当前进程。

3. 待处理信号

   如果一个信号被发送到当前进程，它必须被处理。

第一个重要的函数是 `schedule_tail`，

```c
/**
 * schedule_tail - first thing a freshly forked thread must call.
 * @prev: the thread we just switched away from.
 */
asmlinkage __visible void schedule_tail(struct task_struct *prev)
	__releases(rq->lock)
{
	/*
	 * New tasks start with FORK_PREEMPT_COUNT, see there and
	 * finish_task_switch() for details.
	 *
	 * finish_task_switch() will drop rq->lock() and lower preempt_count
	 * and the preempt_enable() will end up enabling preemption (on
	 * PREEMPT_COUNT kernels).
	 */

	finish_task_switch(prev);
	preempt_enable();

	if (current->set_child_tid)
		put_user(task_pid_vnr(current), current->set_child_tid);

	calculate_sigpending(); // 3. 对应上面说的进程切换前需要处理的 3 件事，这个检查是否有待处理信号
}
```

这里涉及几个关键函数：

`finish_task_switch`：

`prepare_task_switch`：

第二个重要命令是 `CALL_NOSPEC rbx`，这条命令会跳转到 `kernel_init` 中（第一次执行 ret_from_fork 时才会跳转到这里，是初始化的过程，之后的 fork 会怎么执行再分析。我在[这里](https://github.com/UtopianFuture/UtopianFuture.github.io/blob/master/virtualization/bmbt-virtualization.md)分析了内核的启动过程，那是启动的第一部分，初始化各种设备和内存，下面的内容涉及到内核初始化的第二阶段，我在[这里](https://github.com/UtopianFuture/UtopianFuture.github.io/blob/master/kernel/kernel_init.md)进行分析。

总结起来就是内核用 fork 创建了一个新的 kernel thread 用来执行 tty 的相关工作，`ret_from_fork` 就是 fork 的最后部分，然后通过 `CALL_NOSPEC rbx` 开始新的 kernel thread 的执行。这就可以解释为什么在 `ret_from_fork` 设置断点会运行到不同的处理函数。

[这篇文章](https://github.com/UtopianFuture/UtopianFuture.github.io/blob/master/kernel/kernel_init.md)中我分析了 workqueue 的原理，现在我们结合执行流程来看看 kernel thread 是怎样处理 tty_buffer 字符这一任务。

### kernel_thread处理字符输入

我在 `ret_from_fork`， `kthread`，`worker_thread` 都设置了断点，但是内核还是会运行 `receive_buf` 才停下来。那这是属于创建了一个 kernel thread 来处理字符输入还是将内核函数加入到 workqueue，用 worker 来处理呢？

知道了，是做为一个 work 来处理的，每次输入一个字符都会跳转到 `process_one_work`。

注意 `kthreadd` 才是 2 号内核线程，用来创建其他内核线程的，这里的 `kthread` 是内核线程，不要搞混了。我在[这篇文章](https://github.com/UtopianFuture/UtopianFuture.github.io/blob/master/kernel/kthreadd.md)分析了整个创建 kernel thread 的过程，看完应该能很明白为什么 `ret_from_fork` 是跳转到 `kthread` 执行，而 `kthread` 为什么又会调用 `worker_threakd` 执行接下来的任务。

```plain
#0  receive_buf (count=1, head=0xffff888106aa7400, port=0xffff888100a50000) at drivers/tty/tty_buffer.c:484
#1  flush_to_ldisc (work=0xffff888100a50008) at drivers/tty/tty_buffer.c:543
#2  0xffffffff810c4a49 in process_one_work (worker=worker@entry=0xffff888102745d80, work=0xffff888100a50008) at kernel/workqueue.c:2297
#3  0xffffffff810c4c3d in worker_thread (__worker=0xffff888102745d80) at kernel/workqueue.c:2444
#4  0xffffffff810cc32a in kthread (_create=0xffff888102735540) at kernel/kthread.c:319
#5  0xffffffff81004572 in ret_from_fork () at arch/x86/entry/entry_64.S:295
#6  0x0000000000000000 in ?? ()
```

貌似涉及到进程和进程调度的内容，没办法继续分析下去。那就快速浏览一遍书吧。

现在我们从 kthread 开始分析，看 kernel thread 是怎样处理字符输入的。这个 `kthread` 到底什么作用没懂。从调用关系来看，它是 `kthreadd` 线程用 `create_kthread` 创建的线程。我是不是对线程的理解有问题，一个内核线程对应一个 `task_struct`，但是我没有找到 `kernel_init`，`kthreadd`，`kthread` 对应的 `task_struct` 啊！

```c
static int kthread(void *_create)
{
	/* Copy data: it's on kthread's stack */
	struct kthread_create_info *create = _create;
	int (*threadfn)(void *data) = create->threadfn;
	void *data = create->data;
	struct completion *done;
	struct kthread *self;
	int ret;

	set_kthread_struct(current); // 当前 task 没有子进程，kzalloc 分配一个 kthread，
    							 // 并将 current->set_child_tid 设置为 kthread
	self = to_kthread(current); // 设置 current->set_child_tid 为 kthread

	...

	self->threadfn = threadfn;
	self->data = data;
	init_completion(&self->exited);
	init_completion(&self->parked);
	current->vfork_done = &self->exited;

	/* OK, tell user we're spawned, wait for stop or wakeup */
	__set_current_state(TASK_UNINTERRUPTIBLE); // current 被挂起，等待资源释放
	create->result = current;
	/*
	 * Thread is going to call schedule(), do not preempt it,
	 * or the creator may spend more time in wait_task_inactive().
	 */
	preempt_disable();
	complete(done);
	schedule_preempt_disabled(); // 创建了新的线程之后 schedule
	preempt_enable();

	ret = -EINTR;
	if (!test_bit(KTHREAD_SHOULD_STOP, &self->flags)) {
		cgroup_kthread_ready();
		__kthread_parkme(self);
		ret = threadfn(data); // 为什么这里会跑到 worker_thread 中去
	}
	do_exit(ret); // do_exit 是撤销线程
}
```

应该找到一个真实创建的线程，看看这个线程是干什么的。

之后是 `worker_thread`，

```c
/**
 * worker_thread - the worker thread function
 * @__worker: self
 *
 * The worker thread function.  All workers belong to a worker_pool -
 * either a per-cpu one or dynamic unbound one.  These workers process all
 * work items regardless of their specific target workqueue.  The only
 * exception is work items which belong to workqueues with a rescuer which
 * will be explained in rescuer_thread().
 *
 * Return: 0
 */
static int worker_thread(void *__worker)
{
	struct worker *worker = __worker;
	struct worker_pool *pool = worker->pool;

	/* tell the scheduler that this is a workqueue worker */
	set_pf_worker(true);
woke_up:
	raw_spin_lock_irq(&pool->lock);

	/* am I supposed to die? */
	if (unlikely(worker->flags & WORKER_DIE)) {
		raw_spin_unlock_irq(&pool->lock);
		WARN_ON_ONCE(!list_empty(&worker->entry));
		set_pf_worker(false);

		set_task_comm(worker->task, "kworker/dying");
		ida_free(&pool->worker_ida, worker->id);
		worker_detach_from_pool(worker);
		kfree(worker);
		return 0;
	}

	worker_leave_idle(worker);
recheck:
	/* no more worker necessary? */
	if (!need_more_worker(pool))
		goto sleep;

	/* do we need to manage? */
	if (unlikely(!may_start_working(pool)) && manage_workers(worker))
		goto recheck;

	/*
	 * ->scheduled list can only be filled while a worker is
	 * preparing to process a work or actually processing it.
	 * Make sure nobody diddled with it while I was sleeping.
	 */
	WARN_ON_ONCE(!list_empty(&worker->scheduled));

	/*
	 * Finish PREP stage.  We're guaranteed to have at least one idle
	 * worker or that someone else has already assumed the manager
	 * role.  This is where @worker starts participating in concurrency
	 * management if applicable and concurrency management is restored
	 * after being rebound.  See rebind_workers() for details.
	 */
	worker_clr_flags(worker, WORKER_PREP | WORKER_REBOUND);

	do {
		struct work_struct *work =
			list_first_entry(&pool->worklist, // 在 worklist 中选择一个 work 执行
					 struct work_struct, entry);

		pool->watchdog_ts = jiffies;

		if (likely(!(*work_data_bits(work) & WORK_STRUCT_LINKED))) {
			/* optimization path, not strictly necessary */
			process_one_work(worker, work); // 这里让 woker 处理 work
			if (unlikely(!list_empty(&worker->scheduled)))
				process_scheduled_works(worker);
		} else {
			move_linked_works(work, &worker->scheduled, NULL);
			process_scheduled_works(worker);
		}
	} while (keep_working(pool));

	worker_set_flags(worker, WORKER_PREP);
sleep:
	/*
	 * pool->lock is held and there's no work to process and no need to
	 * manage, sleep.  Workers are woken up only while holding
	 * pool->lock or from local cpu, so setting the current state
	 * before releasing pool->lock is enough to prevent losing any
	 * event.
	 */
	worker_enter_idle(worker);
	__set_current_state(TASK_IDLE);
	raw_spin_unlock_irq(&pool->lock);
	schedule();
	goto woke_up;
}
```

发现这一系列的操作根源都在这个结构体，

```c
struct kthread_create_info
{
	/* Information passed to kthread() from kthreadd. */
	int (*threadfn)(void *data);
	void *data;
	int node;

	/* Result passed back to kthread_create() from kthreadd. */
	struct task_struct *result;
	struct completion *done;

	struct list_head list;
};
```

`kthread` 的实参 `_create` 就是 `kthread_create_info`，`ret = threadfn(data);` 中的 data 就是结构体中的。如果跳转到 `worker_thread` 中，data 就被转换成 `__worker`，之后 `process_one_work` 也是根据这个 `worker` 执行。

这个结构体是在这里被定义的。

```c
int kthreadd(void *unused)
{
	...

	for (;;) {

        ...

		while (!list_empty(&kthread_create_list)) {
			struct kthread_create_info *create;

			create = list_entry(kthread_create_list.next,
					    struct kthread_create_info, list);
			list_del_init(&create->list);
			spin_unlock(&kthread_create_lock);

			create_kthread(create);

			spin_lock(&kthread_create_lock);
		}
		spin_unlock(&kthread_create_lock);
	}

	return 0;
}
```

那么 `kthread_create_list` 又是由谁维护呢？ `__kthread_create_on_node` 在创建线程的时候将 `__create` 一个个添加到 `kthread_create_list` 中。

### serial中断

在阅读源码的过程中，发现最重要的数据结构是 `irq_desc`，每个中断号对应一个，之后所有的处理都跟这个 `irq_desc` 有关。然后在中断分发的过程中，任何一个中断控制器的 dispatch 都会通过 `irq_linear_revmap` 函数来将 hwirq 转换成 linux irq，再通过 `irq_to_desc` 找到对应的 `irq_desc`。因此最重要的就是 hwirq 到 linux irq 之间的映射是怎样做的。

```c
static inline unsigned int irq_linear_revmap(struct irq_domain *domain,
					     irq_hw_number_t hwirq)
{
	return hwirq < domain->revmap_size ? domain->linear_revmap[hwirq] : 0;
}
```

下面来看看是怎样映射的。

先尝试找到 serial interrupt 在哪里注册的。通过这个 backtrace 知道 serial 的 irq 是 19，

```plain
#4  0x90000000009ece10 in serial8250_interrupt (irq=19, dev_id=0x900000027da36300) at drivers/tty/serial/8250/8250_core.c:125
#5  0x9000000000283c70 in __handle_irq_event_percpu (desc=0x9000000001696850 <serial8250_ports>, flags=0x6) at kernel/irq/handle.c:149
#6  0x9000000000283ed0 in handle_irq_event_percpu (desc=0x900000027d14da00) at kernel/irq/handle.c:189
#7  0x9000000000283f7c in handle_irq_event (desc=0x900000027d14da00) at kernel/irq/handle.c:206
#8  0x9000000000288348 in handle_level_irq (desc=0x900000027d14da00) at kernel/irq/chip.c:650
#9  0x9000000000282aac in generic_handle_irq_desc (desc=<optimized out>) at include/linux/irqdesc.h:155
#10 generic_handle_irq (irq=<optimized out>) at kernel/irq/irqdesc.c:639
#11 0x90000000008f97ac in extioi_irq_dispatch (desc=<optimized out>) at drivers/irqchip/irq-loongson-extioi.c:305
#12 0x9000000000282aac in generic_handle_irq_desc (desc=<optimized out>) at include/linux/irqdesc.h:155
#13 generic_handle_irq (irq=<optimized out>) at kernel/irq/irqdesc.c:639
#14 0x90000000010153f8 in do_IRQ (irq=<optimized out>) at arch/loongarch/kernel/irq.c:103
#15 0x9000000000203674 in except_vec_vi_handler () at arch/loongarch/kernel/genex.S:92
```

那么就看 irq = 19 的注册过程。

通过分析内核知道了中断注册的大致流程：`init_IRQ` -> `arch_init_irq` -> `setup_IRQ` -> `irqchip_init_default` -> `loongarch_cpu_irq_init` -> `__loongarch_cpu_irq_init` -> `irq_domain_associate_many` -> `irq_domain_associate`

```c
int irq_domain_associate(struct irq_domain *domain, unsigned int virq,
			 irq_hw_number_t hwirq)
{
	struct irq_data *irq_data = irq_get_irq_data(virq);

    ...

	mutex_lock(&irq_domain_mutex);
	irq_data->hwirq = hwirq;
	irq_data->domain = domain;
	if (domain->ops->map) {
		ret = domain->ops->map(domain, virq, hwirq); // 跳转到回调函数 set_vi_handler

        ...

	}

	domain->mapcount++;
	irq_domain_set_mapping(domain, hwirq, irq_data); // 设置 irq_domain::linear_revmap 第 hwirq 项为 irq
	mutex_unlock(&irq_domain_mutex);

	irq_clear_status_flags(virq, IRQ_NOREQUEST);

	return 0;
}
EXPORT_SYMBOL_GPL(irq_domain_associate);
```

```c
static void irq_domain_set_mapping(struct irq_domain *domain,
				   irq_hw_number_t hwirq,
				   struct irq_data *irq_data)
{
	if (hwirq < domain->revmap_size) {
		domain->linear_revmap[hwirq] = irq_data->irq;
	}

    ...

}
```

在中断分发的时候会首先执行 `do_vi` 函数，这个 irq 是硬中断号，之后需要转换成 linux irq ，上面说的 `irq_domain` 就是负责将硬中断号转换成 linux irq 的。

```c
void do_vi(int irq)
{
	vi_handler_t	action;

	action = ip_handlers[irq];
	if (action)
		action(irq);
	else
		pr_err("vi handler[%d] is not installed\n", irq);
}
```

这里的 `ip_handlers` 也需要注意一下。它是在 `set_vi_handler` 中初始化的，而 `set_vi_handler` 是在 `domain->ops->map` 的回调函数 `loongarch_cpu_intc_map` 中调用的。`loongarch_cpu_intc_map` 同时也会设置这个 irq 对应的回调函数。irq 的回调函数设置分为几次，开始是全部设置为 `handle_percpu_irq` ，之后不同的中断控制器再设置相应的回调函数会将 `handle_percpu_irq` 覆盖掉。

同时，这个执行下来设置的映射只有如下中断：

```c
#define EXCCODE_INT_START   64 // 这里的中断号和 virq 不一样，
#define EXCCODE_SIP0        64 // 是因为这个中断号是用来设置vector_handler 的
#define EXCCODE_SIP1        65 // 在 la 手册中有说明，
#define EXCCODE_IP0         66 // 在计算中断入口地址时，中断对应的例外号是其自身的中断号加上 64
#define EXCCODE_IP1         67
#define EXCCODE_IP2         68
#define EXCCODE_IP3         69
#define EXCCODE_IP4         70
#define EXCCODE_IP5         71
#define EXCCODE_IP6         72
#define EXCCODE_IP7         73
#define EXCCODE_PC          74 /* Performance Counter */
#define EXCCODE_TIMER       75
#define EXCCODE_IPI         76
#define EXCCODE_NMI         77
#define EXCCODE_INT_END     78
```

还有其他类型的中断，

```plain
#0  alloc_desc (irq=16, node=-1, flags=0, affinity=0x0, owner=0x0) at include/linux/slab.h:720
#1  0x9000000001008f04 in alloc_descs (owner=<optimized out>, affinity=<optimized out>, node=-1,
    cnt=<optimized out>, start=<optimized out>) at kernel/irq/irqdesc.c:490
#2  __irq_alloc_descs (irq=<optimized out>, from=<optimized out>, cnt=1, node=-1, owner=0x0, affinity=0x0)
    at kernel/irq/irqdesc.c:756
#3  0x900000000028deb4 in __irq_domain_alloc_irqs (domain=0x90000000fa0bfa00, irq_base=-1, nr_irqs=1,
    node=<optimized out>, arg=<optimized out>, realloc=<optimized out>, affinity=<optimized out>)
    at kernel/irq/irqdomain.c:1310
#4  0x900000000028e4f8 in irq_domain_alloc_irqs (arg=<optimized out>, node=<optimized out>,
    nr_irqs=<optimized out>, domain=<optimized out>) at include/linux/irqdomain.h:466
#5  irq_create_fwspec_mapping (fwspec=0x90000000013dbd50) at kernel/irq/irqdomain.c:810
#6  0x9000000000ffdbe4 in pch_lpc_domain_init () at arch/loongarch/la64/irq.c:203
#7  irqchip_init_default () at arch/loongarch/la64/irq.c:287
#8  0x9000000001539008 in setup_IRQ () at arch/loongarch/la64/irq.c:311
#9  0x9000000001539034 in arch_init_irq () at arch/loongarch/la64/irq.c:360
#10 0x900000000153ab90 in init_IRQ () at arch/loongarch/kernel/irq.c:59
```

来看看这些中断是哪些设备需要。

> The **Low Pin Count** (**LPC**) bus is a computer bus used on IBM-compatible personal computers to connect low-bandwidth devices to the CPU, such as the BIOS ROM, "legacy" I/O devices, and Trusted Platform Module (TPM). "Legacy" I/O devices usually include serial and parallel ports, PS/2 keyboard, PS/2 mouse, and floppy disk controller.

从这段解释中可以知道是一些低速设备会用到 lpc bus，这就能对上了，irq = 19 对应 serial，所以是在这里初始化的。

ok，上面就是映射过程，但中断似乎没有这么简单。在 la 的机器上 hwirq 有 14 个，但是 linux irq 不止 14 个，这样情况要怎么处理？同时虽然知道 serial irq 是 19，设置断点 `b do_vi if irq=19` 能够停下来，但是 `action` 却不能执行到对应的回调函数，这是为什么？

在分析过程中发现时间中断和串口中断的处理流程是不一样的，下面是时间中断的处理流程，

```plain
#0  constant_timer_interrupt (irq=61, data=0x0) at arch/loongarch/kernel/time.c:42
#1  0x9000000000283c70 in __handle_irq_event_percpu (desc=0x3d, flags=0x0) at kernel/irq/handle.c:149
#2  0x9000000000283ed0 in handle_irq_event_percpu (desc=0x90000000fa0bf000) at kernel/irq/handle.c:189
#3  0x900000000028966c in handle_percpu_irq (desc=0x90000000fa0bf000) at kernel/irq/chip.c:873
#4  0x9000000000282aac in generic_handle_irq_desc (desc=<optimized out>) at include/linux/irqdesc.h:155
#5  generic_handle_irq (irq=<optimized out>) at kernel/irq/irqdesc.c:639
#6  0x90000000010153f8 in do_IRQ (irq=<optimized out>) at arch/loongarch/kernel/irq.c:103
```

也就是在 `generic_handle_irq_desc` 执行的回调函数不同。

```c
static inline void generic_handle_irq_desc(struct irq_desc *desc)
{
	desc->handle_irq(desc);
}
```

前面讲到不同的中断控制器会设置不同的回调函数会覆盖 `handle_percpu_irq` ，对于  extioi 中断控制器来说，它的回调函数是 `extioi_irq_dispatch`，设置过程如下，

```plain
#0  __irq_do_set_handler (desc=0x90000000fa0be000, handle=0x90000000008f96f0 <extioi_irq_dispatch>,
    is_chained=1, name=0x0) at kernel/irq/chip.c:929
#1  0x9000000000289410 in irq_set_chained_handler_and_data (irq=<optimized out>,
    handle=0x90000000008f96f0 <extioi_irq_dispatch>, data=0x90000000fa011340) at kernel/irq/chip.c:1021
#2  0x90000000008fa04c in extioi_vec_init (fwnode=0x90000000fa0112c0, cascade=53, vec_count=256, misc_func=0,
    eio_en_off=0, node_map=1, node=0) at drivers/irqchip/irq-loongson-extioi.c:416
#3  0x9000000000ffdaa0 in eiointc_domain_init () at arch/loongarch/la64/irq.c:251
#4  irqchip_init_default () at arch/loongarch/la64/irq.c:283
#5  0x9000000001539008 in setup_IRQ () at arch/loongarch/la64/irq.c:311
#6  0x9000000001539034 in arch_init_irq () at arch/loongarch/la64/irq.c:360
#7  0x900000000153ab90 in init_IRQ () at arch/loongarch/kernel/irq.c:59
```

通过下面这些宏定义就能明白为什么时钟中断和串口中断的执行过程不同，

```c
#define LOONGARCH_CPU_IRQ_TOTAL 	14
#define LOONGARCH_CPU_IRQ_BASE 		50
#define LOONGSON_LINTC_IRQ   		(LOONGARCH_CPU_IRQ_BASE + 2) /* IP2 for CPU legacy I/O interrupt controller */
#define LOONGSON_BRIDGE_IRQ 		(LOONGARCH_CPU_IRQ_BASE + 3) /* IP3 for bridge */
#define LOONGSON_TIMER_IRQ  		(LOONGARCH_CPU_IRQ_BASE + 11) /* IP11 CPU Timer */
#define LOONGARCH_CPU_LAST_IRQ 		(LOONGARCH_CPU_IRQ_BASE + LOONGARCH_CPU_IRQ_TOTAL)
```

总共 14 个 hwirq，irq_base 为 50（这就能解释为什么时钟中断的 hwirq = 11, softirq = 61），`LOONGSON_BRIDGE_IRQ` 对应 extioi 控制器。

发现一个问题，找不到 extioi 中断控制器在哪里初始化 `linear_revmap` 的，如果这个没有初始化，那  `irq_linear_revmap` 怎样找到对应的 softirq 呢？

终于找到了，原来不在 `extioi_vec_init` 中设置，而是通过如下方式设置，但这里似乎不是最开始注册的地方。

```plain
#0  irq_domain_set_info (domain=0x90000000fa0bfa00, virq=19, hwirq=2,
    chip=0x9000000001468850 <pch_pic_irq_chip>, chip_data=0x90000000fa0115c0,
    handler=0x9000000000285a18 <handle_level_irq>, handler_data=0x0, handler_name=0x0)
    at kernel/irq/irqdomain.c:1189
#1  0x90000000008d97b8 in pch_pic_alloc (domain=0x90000000fa0bfa00, virq=19, nr_irqs=<optimized out>,
    arg=<optimized out>) at drivers/irqchip/irq-loongson-pch-pic.c:290
#2  0x900000000028b48c in irq_domain_alloc_irqs_hierarchy (arg=<optimized out>, nr_irqs=<optimized out>
    irq_base=<optimized out>, domain=<optimized out>) at kernel/irq/irqdomain.c:1270
#3  __irq_domain_alloc_irqs (domain=0x90000000fa0bfa00, irq_base=19, nr_irqs=1, node=<optimized out>,
    arg=<optimized out>, realloc=<optimized out>, affinity=<optimized out>)
    at kernel/irq/irqdomain.c:1326
#4  0x900000000028ba30 in irq_domain_alloc_irqs (arg=<optimized out>, node=<optimized out>,
    nr_irqs=<optimized out>, domain=<optimized out>) at ./include/linux/irqdomain.h:466
#5  irq_create_fwspec_mapping (fwspec=0x90000000fa403a20) at kernel/irq/irqdomain.c:810
#6  0x900000000020c440 in acpi_register_gsi (dev=<optimized out>, gsi=19, trigger=<optimized out>,
    polarity=<optimized out>) at arch/loongarch/kernel/acpi.c:89
#7  0x90000000009546ac in acpi_dev_get_irqresource (res=0x90000000fa0bfa00, gsi=19,
    triggering=<optimized out>, polarity=<optimized out>, shareable=<optimized out>,
    legacy=<optimized out>) at drivers/acpi/resource.c:432
#8  0x90000000009547e4 in acpi_dev_resource_interrupt (ares=<optimized out>, index=<optimized out>,
    res=<optimized out>) at drivers/acpi/resource.c:488
#9  0x90000000009974b8 in pnpacpi_allocated_resource (res=0x90000000fa7f5148, data=0x90000000fa7e5400)
    at drivers/pnp/pnpacpi/rsparser.c:191
#10 0x900000000097ef00 in acpi_walk_resource_buffer (buffer=<optimized out>, user_function=0x13,
    context=0x2) at drivers/acpi/acpica/rsxface.c:547
#11 0x900000000097f744 in acpi_walk_resources (context=<optimized out>, user_function=<optimized out>,
    name=<optimized out>, device_handle=<optimized out>) at drivers/acpi/acpica/rsxface.c:623
#12 acpi_walk_resources (device_handle=<optimized out>, name=<optimized out>,
    user_function=0x9000000000997418 <pnpacpi_allocated_resource>, context=0x90000000fa7e5400)
    at drivers/acpi/acpica/rsxface.c:594
#13 0x90000000009977e0 in pnpacpi_parse_allocated_resource (dev=0x90000000fa0bfa00)
    at drivers/pnp/pnpacpi/rsparser.c:289
#14 0x90000000015366ac in pnpacpi_add_device (device=<optimized out>) at drivers/pnp/pnpacpi/core.c:271
#15 pnpacpi_add_device_handler (handle=<optimized out>, lvl=<optimized out>, context=<optimized out>,
    rv=<optimized out>) at drivers/pnp/pnpacpi/core.c:308
#16 0x9000000000979598 in acpi_ns_get_device_callback (return_value=<optimized out>,
    context=<optimized out>, nesting_level=<optimized out>, obj_handle=<optimized out>)
    at drivers/acpi/acpica/nsxfeval.c:740
#17 acpi_ns_get_device_callback (obj_handle=0x90000000fa0c8398, nesting_level=2,
    context=0x90000000fa403d58, return_value=0x0) at drivers/acpi/acpica/nsxfeval.c:635
#18 0x9000000000978de4 in acpi_ns_walk_namespace (type=<optimized out>, start_node=0x90000000fa0c8050,
    max_depth=<optimized out>, flags=<optimized out>, descending_callback=<optimized out>,
    ascending_callback=0x0, context=0x90000000fa403d58, return_value=0x0)
    at drivers/acpi/acpica/nswalk.c:229
#19 0x9000000000978ef8 in acpi_get_devices (HID=<optimized out>, user_function=<optimized out>,
    context=<optimized out>, return_value=0x0) at drivers/acpi/acpica/nsxfeval.c:805
#20 0x90000000015364d0 in pnpacpi_init () at drivers/pnp/pnpacpi/core.c:321
#21 0x9000000000200b8c in do_one_initcall (fn=0x9000000001536468 <pnpacpi_init>) at init/main.c:884
#22 0x90000000014f0e8c in do_initcall_level (level=<optimized out>) at ./include/linux/init.h:131
#23 do_initcalls () at init/main.c:960
#24 do_basic_setup () at init/main.c:978
#25 kernel_init_freeable () at init/main.c:1145
#26 0x9000000000fc4fa0 in kernel_init (unused=<optimized out>) at init/main.c:1062
#27 0x900000000020330c in ret_from_kernel_thread () at arch/loongarch/kernel/entry.S:85
```

这两个函数比较重要，`extioi_domain_translate`, `extioi_domain_alloc`。

这个是串口中断对应的 `irq_domain`，

```plain
$2 = {irq_common_data = {state_use_accessors = 37749248, node = 4294967295, handler_data = 0x0, msi_desc = 0x0,affinity = {{bits = {1}}}, effective_affinity = {{bits = {1}}}}, irq_data = {mask = 0, irq = 19, hwirq = 2,common = 0x90000000fa7fdc00, chip = 0x9000000001468850 <pch_pic_irq_chip>, domain = 0x90000000fa0bfa00, parent_data = 0x90000000fa83e1c0, chip_data = 0x90000000fa0115c0}, kstat_irqs = 0x90000000015906a0 <swapper_pg_dir+1696>, handle_irq = 0x9000000000285a18 <handle_level_irq>, action = 0x90000000faa83180, ...
```

注册和使用的 `irq_domain` 能够对上。

再从 backtrace 继续跟踪发现 `domain->linear_revmap` 是在 `__irq_domain_alloc_irqs` 初始化的，我们来看看是怎样初始化的。

```c
/**
 * __irq_domain_alloc_irqs - Allocate IRQs from domain
 * @domain:	domain to allocate from
 * @irq_base:	allocate specified IRQ nubmer if irq_base >= 0
 * @nr_irqs:	number of IRQs to allocate
 * @node:	NUMA node id for memory allocation
 * @arg:	domain specific argument
 * @realloc:	IRQ descriptors have already been allocated if true
 * @affinity:	Optional irq affinity mask for multiqueue devices
 *
 * The whole process to setup an IRQ has been split into two steps.
 * The first step, __irq_domain_alloc_irqs(), is to allocate IRQ
 * descriptor and required hardware resources. The second step,
 * irq_domain_activate_irq(), is to program hardwares with preallocated
 * resources. In this way, it's easier to rollback when failing to
 * allocate resources.
 */
int __irq_domain_alloc_irqs(struct irq_domain *domain, int irq_base,
			    unsigned int nr_irqs, int node, void *arg,
			    bool realloc, const struct cpumask *affinity)
{
	int i, ret, virq;

	...

	mutex_lock(&irq_domain_mutex);
	ret = irq_domain_alloc_irqs_hierarchy(domain, virq, nr_irqs, arg); // 从这里继续执行
    																   // 设置其他的 irq_domain 信息
	if (ret < 0) {
		mutex_unlock(&irq_domain_mutex);
		goto out_free_irq_data;
	}
	for (i = 0; i < nr_irqs; i++)
		irq_domain_insert_irq(virq + i); // 调用 irq_domain_set_mapping 设置映射信息，
    									 // 对于 serial，hwirq = 2, softirq = 19
	mutex_unlock(&irq_domain_mutex);

	return virq;

	...
}
```

也就是说 extioi 中断控制器所有的映射都是在这里完成的，之后需要实现其他的设备来这里找即可。

softirq = 53 是 extioi 的中断号，而 extioi 到子一级的中断映射是这样做的，

```c
static void extioi_irq_dispatch(struct irq_desc *desc)
{
	int i;
	u64 pending;
	bool handled = false;
	struct extioi *priv = irq_desc_get_handler_data(desc);
	int reg_count;
	chained_irq_enter(chip, desc);

	reg_count = priv->vec_count >> 6;

	for (i = 0; i < reg_count; i++) { // 处理所有的 irq
#ifdef CONFIG_LOONGARCH   // #define LOONGARCH_IOCSR_EXTIOI_ISR_BASE 0x1800
		pending = iocsr_readq(LOONGARCH_IOCSR_EXTIOI_ISR_BASE + (i << 3)); // extioi 对应的位是否置为 1
#endif

	...

#ifdef CONFIG_LOONGARCH
		iocsr_writeq(pending, LOONGARCH_IOCSR_EXTIOI_ISR_BASE + (i << 3));
#endif
		while (pending) {
			int bit = __ffs(pending);
			int virq = irq_linear_revmap(priv->extioi_domain, // 找到对应的 irq
					bit + VEC_COUNT_PER_REG * i);
			if (virq > 0) generic_handle_irq(virq);
			pending &= ~BIT(bit);
			handled = true;
		}
	}

	...
}
```

好，那么下一步是找到串口如何发起中断。

这个比较容易找到，

```c
	/*
	 * Must disable interrupts or else we risk racing with the interrupt
	 * based handler.
	 */
	if (up->port.irq) {
		ier = serial_in(up, UART_IER);
		serial_out(up, UART_IER, 0);
	}
```

就是用 `serial_out` 写 `UART_IER` 寄存器。估计 serial 发起中断也是这样的方式。所以现在的关键是实现 `serial_out`。

现在唯一的问题就是 serial 怎样发起中断。这一步解决这篇文章探究的问题就闭环了。

现在遇到的问题是无法接收到 serial 中断和 keyboard 中断。  在看书的过程中发现 la 有 8 个硬中断，这些硬中断的中断源来自处理器核外部，其直接来源是核外的中断控制器。也就是说 serial 发起的中断并不是直接将 cpu 的硬中断之一拉高，而是发送到中断控制器，如 8259 就是 pin1 作为 keyboard 中断，pin3, pin4 都是 serial 中断。那么是不是我没有设置中断控制器的映射从而导致无法接收到 serial 中断。定时器中断能够响应是因为 cpu 中有一个线中断： TI 作为定时器中断。

![img](https://github.com/UtopianFuture/UtopianFuture.github.io/blob/master/image/from-keyboard-to-display.1.png?raw=true)

### Reference

[1] [kernel thread and workqueue](https://blog.csdn.net/gatieme/article/details/51589205)

[2] [ret_from_fork](https://www.oreilly.com/library/view/understanding-the-linux/0596002130/ch04s08.html)

[3] 基于龙芯的 Linux 内核探索解析

[4] [irq_domain](https://www.cnblogs.com/LoyenWang/p/13052677.html) 图画的清晰易懂

### 说明

这篇文章是探索内核的记录，不一定严谨，没有固定的流程（我尽量按照既定的问题走），可能调试的时候发现了不懂的问题就产生一个新的分支。

### 经验

向 《深入理解 LINUX 内核》靠拢，而不是模仿《QEMU/KVM 源码分析》。
