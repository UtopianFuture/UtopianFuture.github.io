## Interrupt and exception

这是测试 LA 内核的中断的小程序。

运行环境：qemu_la（在 la 机器上直接 sudo apt install qemu） + 3a5000
运行命令：

```plain
qemu-system-loongarch64 -nographic -m 2G -cpu Loongson-3A5000 -serial mon:stdio -bios ~/research/bmbt/qemu-la/pc-bios/loongarch_bios.bin -M loongson7a,kernel_irqchip=off -kernel ~/gitlab/timer-interrupt/hello_period.elf
```

调试命令：

```plain
qemu-system-loongarch64 -nographic -m 2G -cpu Loongson-3A5000 -serial mon:stdio -bios ~/research/bmbt/qemu-la/pc-bios/loongarch_bios.bin -M loongson7a,kernel_irqchip=off -kernel ~/gitlab/timer-interrupt/hello_period.elf -gdb tcp::5678 -S
```

正常的话会周期性的打印 "say_hi!!" 和 "timer interrupt coming!!"

目前遇到的问题是无法在 LA 的机器上编译。
会出现 'fatal error: no match insn: sym_func_start(__cpu_wait)' 的报错，
`SYM_FUNC_START` 是内核实现的 macro，其能够将汇编指令转化成类似于 C 函数。具体解释在[这里](https://www.kernel.org/doc/html/latest/asm-annotations.html#assembler-annotations)

loongarch 的中断处理过程如下。

```plain
#0  serial8250_tx_chars (up=0x9000000001696850 <serial8250_ports>) at drivers/tty/serial/8250/8250_port.c:1805
#1  0x90000000009f1198 in serial8250_handle_irq (port=0x9000000001696850 <serial8250_ports>, iir=194) at drivers/tty/serial/8250/8250_port.c:1924
#2  0x90000000009f1320 in serial8250_handle_irq (iir=<optimized out>, port=<optimized out>) at drivers/tty/serial/8250/8250_port.c:1897
#3  serial8250_default_handle_irq (port=0x9000000001696850 <serial8250_ports>) at drivers/tty/serial/8250/8250_port.c:1940
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

`except_vec_vi_handler` 执行完后会跳转到 `do_vi` ，根据 irq 对应的 action 执行相应的回调函数。对于时间中断来说是 `plat_irq_dispatch`。

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

`struct irq_domain`与中断控制器对应，完成的工作是硬件中断号到 `Linux irq` 的映射，时钟中断的硬件中断号为 11，`linux irq` 为 61。

```c
asmlinkage void __weak plat_irq_dispatch(int irq)
{
	unsigned int virq;

	virq = irq_linear_revmap(irq_domain, irq);
	do_IRQ(virq);
}
```

中断号 irq 是用 a0 寄存器传递的，发现在进入 `except_vec_vi_handler` 时中断号就已经是 11 了，所以 `except_vec_vi_handler` 还不是最开始处理中断的地方。发现在 `except_vec_vi_handler` 可以使用 backtrace 。

```plain
#0  0x90000000002035c8 in except_vec_vi_handler () at arch/loongarch/kernel/genex.S:45
#1  0x90000000010154a4 in csr_xchgl (reg=<optimized out>, mask=<optimized out>, val=<optimized out>) at arch/loongarch/include/asm/loongarchregs.h:341
#2  arch_local_irq_enable () at arch/loongarch/include/asm/irqflags.h:22
#3  __do_softirq () at kernel/softirq.c:276
```

![image-20211228192240982](https://github.com/UtopianFuture/UtopianFuture.github.io/blob/master/image/bmbt-virtualization.1.png?raw=true)

手册说的很清楚，中断信号存储在 CSR.ESTA.IS 域，但不清楚是不是 cpu 会自动将 IS 域与 CSR.ECFG.LIT 域相与，得到 int_vec，需要测试。同时经过调试发现只要将 CSR.CRMD.IE 置为 1，就会执行中断，没有发现有指令来进行按位相于的操作。还有一个问题，为什么将 CSR.CRMD.IE 置为 1 后会立刻跳转到 `except_vec_vi_handler`，从哪里跳转过去的。

其实还是在 `trap_init` 中初始化的，

```c
void __init trap_init(void)
{
	...

	/* set interrupt vector handler */
	for (i = EXCCODE_INT_START; i < EXCCODE_INT_END; i++) {
		vec_start = vector_table[i - EXCCODE_INT_START];
		set_handler(i * VECSIZE, vec_start, VECSIZE);
	}

    ...

	cache_error_setup();
}
```

这里 `vector_talbe` 就是中断向量表。

```c
        .globl  vector_table
vector_table:
	PTR	handle_vi_0
	PTR	handle_vi_1
	PTR	handle_vi_2
	PTR	handle_vi_3
	PTR	handle_vi_4
	PTR	handle_vi_5
	PTR	handle_vi_6
	PTR	handle_vi_7
	PTR	handle_vi_8
	PTR	handle_vi_9
	PTR	handle_vi_10
	PTR	handle_vi_11
	PTR	handle_vi_12
	PTR	handle_vi_13
```

当然 `handle_vi_` 只是一个跳转地址，指向真正的处理函数，

```c
/*
 * Macro helper for vectored interrupt handler.
 */
	.macro	BUILD_VI_HANDLER num
	.align	5
SYM_FUNC_START(handle_vi_\num)
	csrwr	t0, LOONGARCH_CSR_KS0
	csrwr	t1, LOONGARCH_CSR_KS1
	SAVE_SOME #docfi=1
	addi.d	v0, zero, \num
	la.abs	v1, except_vec_vi_handler
	jirl	zero, v1, 0
SYM_FUNC_END(handle_vi_\num)
```

然后就能跳转到 `except_vec_vi_handler` 执行。

ok，再看看 `set_handler` 是怎样注册的。

```c
/* Install CPU exception handler */
void set_handler(unsigned long offset, void *addr, unsigned long size)
{
	memcpy((void *)(ebase + offset), addr, size);
	local_flush_icache_range(ebase + offset, ebase + offset + size);
}
```

`ebase` 是一个全局变量，其在 `start_kernel` -> `setup_arch` -> `cpu_probe` -> `per_cpu_trap_init` -> `configure_exception_vector` 就已经初始化好了。

```c
static void configure_exception_vector(void)
{
	ebase		= (unsigned long)exception_handlers;
	refill_ebase	= (unsigned long)exception_handlers + 80*VECSIZE;

	csr_writeq(ebase, LOONGARCH_CSR_EBASE);
	csr_writeq(refill_ebase, LOONGARCH_CSR_TLBREBASE);
	csr_writeq(ebase, LOONGARCH_CSR_ERREBASE);
}
```

而 `LOONGARCH_CSR_EBASE` 就是 loongson cpu 的中断入口寄存器。

发现在设置 `LOONGARCH_CSR_EBASE` 时 ebase 的值明明是 20070 ，但写入到 `LOONGARCH_CSR_EBASE` 的值却是 30070，用于写入的寄存器不一样。
下面是内核的设置过程，是正常的，记录下来做个对比。
```plain
0x900000000020a438 <per_cpu_trap_init+16>       pcaddu12i $r14,5110(0x13f6)
0x900000000020a43c <per_cpu_trap_init+20>       addi.d $r14,$r14,-1072(0xbd0)
0x900000000020a440 <per_cpu_trap_init+24>       lu12i.w $r13,10(0xa)
0x900000000020a444 <per_cpu_trap_init+28>       stptr.d $r12,$r14,0
0x900000000020a448 <per_cpu_trap_init+32>       add.d  $r13,$r12,$r13
0x900000000020a44c <per_cpu_trap_init+36>       pcaddu12i $r14,5110(0x13f6)
0x900000000020a450 <per_cpu_trap_init+40>       addi.d $r14,$r14,-1100(0xbb4)
0x900000000020a454 <per_cpu_trap_init+44>       stptr.d $r13,$r14,0
0x900000000020a45c <per_cpu_trap_init+52>       csrwr  $r14,0xc
0x900000000020a460 <per_cpu_trap_init+56>       csrwr  $r13,0x88
0x900000000020a464 <per_cpu_trap_init+60>       csrwr  $r12,0x93
```

### 注意
1. 在时间中断的处理函数中需要将 `LOONGARCH_CSR_TINTCLR` 的 `CSR_TINTCLR_TI` 位置为 0，不然会一直处理时间中断。

```c
void do_vi(int irq) {
  if (irq == 0) { // time interrupt
    clear_time_intr();
    timer_interrupt();
  }
  // irq_exit();
  return;
}
```

```c
void clear_time_intr() {
  unsigned long val = CSR_TINTCLR_TI;
  asm volatile("csrwr %0, %1\n\t" : "=r"(val) : "i"(LOONGARCH_CSR_TINTCLR) :);
}
```

2. 内核注册中断入口是这样的：

```c
/* set interrupt vector handler */
for (i = EXCCODE_INT_START; i < EXCCODE_INT_END; i++) {
  vec_start = vector_table[i - EXCCODE_INT_START];
  set_handler(i * VECSIZE, vec_start, VECSIZE);
}
```
但是自己按照这种方式设置会有问题，即中断来了不能正常跳转到注册的位置：`ebase + offset` 只能跳转到 `ebase` ，为了暂时跳过这个问题，按照如下方式设置中断入口：

```c
for (i = 0; i < EXCCODE_INT_END - EXCCODE_INT_START; i++) {
    vec_start = vector_table[i];
    set_handler(i * VECSIZE, vec_start, VECSIZE);
}
```
3. 系统态开发的正确方法应该是多看手册和内核。在遇到无法解决的问题时根据手册内容多尝试。
4. 在检查值是否写入了特定的寄存器时，一定要先写，再读取，看读取的值是否正确。

```c
  asm volatile("csrxchg %0, %1, %2\n\t"
               : "=r"(val)
               : "r"(CSR_ECFG_VS), "i"(LOONGARCH_CSR_ECFG)
               :);

  asm volatile("csrrd %0, %1\n\t" : "=r"(ecfg_val) : "i"(LOONGARCH_CSR_ECFG));
```
现在遇到的问题是无法接收到 serial 中断和 keyboard 中断。
在看书的过程中发现 la 有 8 个硬中断，这些硬中断的中断源来自处理器核外部，其直接来源是核外的中断控制器。也就是说 serial 发起的中断并不是直接将 cpu 的硬中断之一拉高，而是发送到中断控制器，如 8259 就是 pin1 作为 keyboard 中断，pin3, pin4 都是 serial 中断。那么是不是我没有设置中断控制器的映射从而导致无法接收到 serial 中断。
定时器中断能够响应是因为 cpu 中有一个线中断： TI 作为定时器中断。
