## From keyboard to display

为什么要探究这个过程呢？在看 kernel 和 bmbt 代码的过程中，代码始终一块块孤零零的，脑子里没有镇个流程，不知道下一步该实现什么，总是跟着别人的脚步在走，感觉用 gdb 走一遍这个流程能对整个系统认识更加深刻。

### 执行流程

首先寻找到 kernel 中的 keyboard 是怎样处理字符的。

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

这个中断处理完就就继续执行 tty_buffer 的工作。

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
	WRITE_ONCE(*p, v);			\
} while (0)
```

```c
#define WRITE_ONCE(x, val)				\ // 这个代码看不懂
({							\
	union { typeof(x) __val; char __c[1]; } __u =	\
		{ .__val = (val) }; 			\
	__write_once_size(&(x), __u.__c, sizeof(x));	\
	__u.__val;					\
})
```

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
- kernel thread, workqueue 的设计思想是什么？
- tty_buffer 中的字符是怎样发送到 serial_port 中去的。
- `preempt_enable` 执行完后就会执行一次中断，这个中断是由哪个发出来的，怎样判断中断来源？
- 在调试的过程中，还执行过中断上下文切换的代码，汇编写的，搞清楚。
- `serial8250_tx_chars` 也是中断处理过程的一部分，serial 发的中断么？

### 用户态执行流程



### 用户态到内核态的上下文切换

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
	jmp	swapgs_restore_regs_and_return_to_usermode

1:
	/* kernel thread */
	UNWIND_HINT_EMPTY
	movq	%r12, %rdi
	CALL_NOSPEC rbx
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
	CALL_NOSPEC rbx
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

	calculate_sigpending(); // 3. 对比上面说的进程切换前需要处理的 3 件事，这个检查是否有待处理信号
}
```

这里涉及几个关键函数：

`finish_task_switch`：

`prepare_task_switch`：

第二个重要命令是 `CALL_NOSPEC rbx`，这条命令会跳转到 `kernel_init` 中（第一次执行 ret_from_fork 时才会跳转到这里，是初始化的过程，之后的 fork 会怎么执行再分析）。我在[这里](https://github.com/UtopianFuture/UtopianFuture.github.io/blob/master/virtualization/bmbt-virtualization.md)分析了内核的启动过程，那是启动的第一部分，初始化各种设备和内存，下面的内容涉及到内核初始化的第二阶段，来详细分析一下。

### kernel_init



### kernel thread

这条命令也会跳转到 kthreadd，即创建 kernel thread, 。这就是创建的过程：

```plain
#0  kernel_clone (args=args@entry=0xffffc9000001be40) at kernel/fork.c:2544
#1  0xffffffff810a2705 in kernel_thread (fn=fn@entry=0xffffffff810cc200 <kthread>, arg=arg@entry=0xffff8881001df900, flags=flags@entry=1553) at kernel/fork.c:2636
#2  0xffffffff810cc7cf in create_kthread (create=0xffff8881001df900) at kernel/kthread.c:342
#3  kthreadd (unused=<optimized out>) at kernel/kthread.c:685
#4  0xffffffff81004572 in ret_from_fork () at arch/x86/entry/entry_64.S:295
#5  0x0000000000000000 in ?? ()
```

改变想法，这个初始化的过程现在搞懂也是挺好的，正好有不错的文章可以看。

内核线程是在内核态执行周期性任务（如刷新磁盘高速缓存，交换出不用的页框，维护网络连接等等）的进程，其只运行在内核态，不需要进行上下文切换，但也只能使用大于 PAGE_OFFSET（传统的 x86_32 上是 3G）的地址空间。也可以称其为**内核守护进程**。

#### task_struct

```c
struct task_struct {
	...
}
```

#### kthread_create

```c
/**
 * kthread_create - create a kthread on the current node
 * @threadfn: the function to run in the thread
 * @data: data pointer for @threadfn()
 * @namefmt: printf-style format string for the thread name
 * @arg: arguments for @namefmt.
 *
 * This macro will create a kthread on the current node, leaving it in
 * the stopped state.  This is just a helper for kthread_create_on_node();
 * see the documentation there for more details.
 */
#define kthread_create(threadfn, data, namefmt, arg...) \
	kthread_create_on_node(threadfn, data, NUMA_NO_NODE, namefmt, ##arg)
```

#### kthread_run

```c
/**
 * kthread_run - create and wake a thread.
 * @threadfn: the function to run until signal_pending(current).
 * @data: data ptr for @threadfn.
 * @namefmt: printf-style name for the thread.
 *
 * Description: Convenient wrapper for kthread_create() followed by
 * wake_up_process().  Returns the kthread or ERR_PTR(-ENOMEM).
 */
#define kthread_run(threadfn, data, namefmt, ...)			   \
({									   \
	struct task_struct *__k						   \
		= kthread_create(threadfn, data, namefmt, ## __VA_ARGS__); \
	if (!IS_ERR(__k))						   \
		wake_up_process(__k);					   \
	__k;								   \
})
```

### workqueue



### Reference

[1] [kernel thread and workqueue](https://blog.csdn.net/gatieme/article/details/51589205)

[2] [ret_from_fork](https://www.oreilly.com/library/view/understanding-the-linux/0596002130/ch04s08.html)

[3] 基于龙芯的 Linux 内核探索解析

[4]

### 说明

这篇文章是探索内核的记录，不一定严谨，没有固定的流程（我尽量按照既定的问题走），可能调试的时候发现了不懂的问题就产生一个新的分支。