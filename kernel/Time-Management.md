## Timer Management

内核中很多函数是基于时间驱动的，其中有些函数需要周期或定期执行。比如有的每秒执行 100 次，有的在等待一个相对时间之后执行。除此之外，内核还必须管理系统运行的时间日期。

周期性产生的时间都是有系统定时器驱动的，**系统定时器是一种可编程硬件芯片**，它可以以固定频率产生中断，该中断就是所谓的定时器中断，**其所对应的中断处理程序负责更新系统时间，也负责执行需要周期性运行的任务**。

系统定时器以某种频率自行触发时钟中断，该频率可以通过编程预定，称作节拍率。当时钟中断发生时，内核就通过一种特殊的中断处理器对其进行处理。**内核知道连续两次时钟中断的间隔时间，该间隔时间就称为节拍**。内核就是靠这种已知的时钟中断间隔来计算实际时间和系统运行时间的。内核通过控制时钟中断维护实际时间，另外内核也为用户提供一组系统调用获取实际日期和实际时间。时钟中断对才操作系统的管理来说十分重要，**系统更新运行时间、更新实际时间、均衡调度程序中个处理器上运行队列、检查进程是否用尽时间片等工作都利用时钟中断来周期执行**。

系统定时器频率是通过静态预定义的，也就是 HZ，体系结构不同，HZ 的值也不同。内核在 `<asm/param.h>` 文件中定义，在 x86 上时钟中断频率为 100HZ，也就是说在 i386 处理上每秒时钟中断 100 次。

linux 内核众多子系统都依赖时钟中断工作，所以是时钟中断频率的选择必须考虑频率所有子系统的影响。提高节拍就使得时钟中断产生的更频繁，中断处理程序就会更加频繁的执行，这样就提高了时间驱动时间的准确度，误差更小。如 HZ=100，那么时钟每 10ms 中断一次，周期事件每 10ms 运行一次，如果 HZ=1000，那么周期事件每 1ms 就会运行一次，这样依赖定时器的系统调用能够以更高的精度运行。既然提高时钟中断频率这么好，那为何要将 HZ 设置为 100 呢？因为提高时钟中断频率也会产生副作用，中断频率越高，系统的负担就增加了，**处理器需要花时间来执行中断处理程序**，中断处理器占用 cpu 时间越多。这样处理器执行其他工作的时间及越少，并且还会打乱处理器高速缓存。所以选择时钟中断频率时要考虑多方面，要取得各方面的折中的一个合适频率。

内核有一个全局变量 jiffies，该变量用来记录系统起来以后产生的节拍总数。系统启动是，该变量被设置为 0，此后**每产生一次时钟中断就增加该变量的值**。jiffies 每一秒增加的值就是 HZ。jiffies 定义于头文件 `<include/linux/jiffies.h>` 中：

```c
extern unsigned long volatile __jiffy_data jiffies;
```

对于 32 位 unsigned long，可以存放最大值为 4294967295，所以当节拍数达到最大值后还要继续增加的话，它的值就会回到 0 值。内核提供了四个宏（位于文件<include/linux/jiffies.h>中）来比较节拍数，这些宏可以正确处理节拍计数回绕情况。

```c
#define time_after(a,b)         \
        (typecheck(unsigned long, a) && \
         typecheck(unsigned long, b)	 && \
         ((long)(b) - (long)(a) < 0))
#define time_before(a,b)        time_after(b,a)
#define time_after_eq(a,b)      \
        (typecheck(unsigned long, a) && \
         typecheck(unsigned long, b) && \
         ((long)(a) - (long)(b) >= 0))
#define time_before_eq(a,b)     time_after_eq(b,a)
```

在内核的启动过程中是这样打印时间戳 log 的，

```
#0  print_time (buf=<optimized out>, ts=<optimized out>) at kernel/printk/printk.c:1044
#1  print_prefix (msg=0xc1c57730 <__log_buf+8336>, syslog=<optimized out>, buf=0x0)
    at kernel/printk/printk.c:1075
#2  0xc10914dd in msg_print_text (msg=<optimized out>, prev=<optimized out>,
    syslog=<optimized out>,
    buf=0xc1c54a00 <text> "\n ACPI AML tables successfully acquired and loaded1\nue calculated
 using timer frequency.. 112604467 ns\n32 kB)\n[    0.000000]     pkmap   : 0xff800000 - 0xffc
00000   (4096 kB)\n[    0.000000]     vmallo"..., size=1024) at kernel/printk/printk.c:1112
#3  0xc1092ed6 in console_unlock () at kernel/printk/printk.c:2305
#4  console_unlock () at kernel/printk/printk.c:2229
#5  0xc1093336 in vprintk_emit (facility=0, level=<optimized out>, dict=0x0, dictlen=0,
    fmt=0xc1a1c8e4 "\001\066Security Framework initialized\n", args=<optimized out>)
    at kernel/printk/printk.c:1824
#6  0xc1093656 in vprintk_default (fmt=<optimized out>, args=<optimized out>)
    at kernel/printk/printk.c:1864
#7  0xc110df47 in printk (fmt=0xc1a1c8e4 "\001\066Security Framework initialized\n")
    at kernel/printk/printk.c:1914
```

即调用 `print_time`，

```c
static size_t print_time(u64 ts, char *buf)
{
	unsigned long rem_nsec;

	if (!printk_time) // 这个变量是通过 CONFIG_PRINTK_TIME 配置的，所以可以在 .config 中设置是否打印时间戳
		return 0;

	rem_nsec = do_div(ts, 1000000000);

	if (!buf)
		return snprintf(NULL, 0, "[%5lu.000000] ", (unsigned long)ts);

	return sprintf(buf, "[%5lu.%06lu] ",
		       (unsigned long)ts, rem_nsec / 1000);
}
```



### Reference

[1] https://zhuanlan.zhihu.com/p/355852183
