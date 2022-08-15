## Timer Management

### 宏观概念

内核中很多函数是基于时间驱动的，其中有些函数需要周期或定期执行。比如有的每秒执行 100 次，有的在等待一个相对时间之后执行。除此之外，内核还必须管理系统运行的时间日期。

周期性产生的时间都是由系统定时器驱动的，**系统定时器是一种可编程硬件芯片**，它可以以固定频率产生中断，该中断就是所谓的定时器中断，**其所对应的中断处理程序负责更新系统时间，也负责执行需要周期性运行的任务**。

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

```plain
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

### x86 平台上的时钟

计算机里面一共有三类时钟硬件，分别是真时钟 RTC(Real Time Clock)、定时器 Timer、计时器 Counter。而每一类时钟可能有多个不同的硬件，某一个时钟硬件也可能实现多种不同的时钟类型。

真时钟 RTC，在 x86 上的硬件实现也叫做 RTC，和 CMOS(计算机中有很多叫做 CMOS 的东西，但是是不同的概念，此处的 CMOS 是指 BIOS 设置保存数据的地方)是放在一起的。由于在关机后都需要供电，所以两者放在了一起，由一个纽扣电池供电。所以有时候也会被人叫做 CMOS 时钟。

定时器 Timer，在 UP 时代是 PIT(Programmable Interval Timer)，它**以固定时间间隔向 CPU 发送中断信号**。PIT 可以在系统启动时设置每秒产生多少个定时器中断，一般设置是 100,250,300,1000，这个值叫做 HZ。到了 SMP 时代，PIT 就不适用了，此时有多种不同的定时器。有一个叫做 Local APIC Timer 的定时器，它是和中断系统相关的。中断系统有一个全局的 IO APIC，有 NR_CPU 个 Local APIC，一个 Local APIC 对应一个 CPU。所以在每个 Local APIC 都安装一个定时器，**专门给自己对应的 CPU 发送定时器中断**，就很方便。还有一个定时器叫做 HPET(High Precision Event Timer)，它是 Intel 和微软共同研发的。它**不仅是个定时器，而且还有计时器的功能**。HPET 不和特定的 CPU 绑定，所以它**可以给任意一个 CPU 发中断**，这点和 Local APIC Timer 不同。

计时器 Counter，RTC 或者定时器虽然也可以实现计时器的目的，但是由于精度太差，所以系统都有专门的计时器硬件。计时器一般都是一个整数寄存器，以特定的时间间隔增长，比如说 1 纳秒增加 1，这样两次读它的值就可以算出其中的时间差，而且精度很高。x86 上最常用的计时器叫做 TSC(Time Stamp Counter)，是个 64 位整数寄存器。还有一个计时器叫做 ACPI PMT(ACPI Power Management Timer)，但是它是一个设备寄存器，需要通过 IO 端口来读取。而 TSC 是 CPU 寄存器，可以直接读取，读取速度就非常快。

### 系统时钟的设计

在用户空间和内核空间都有知时的需求，而底层又有 RTC 硬件，这样看来知时的需求很好实现啊，直接访问 RTC 硬件就可以了。这么做行吗？我们来分析一下。首先 RTC 是个外设，访问 RTC 要走 IO 端口，而这相对来说是个很慢的操作。其次 RTC 的精度不够，有的 RTC 精度是秒，有的是毫秒，这显然是不够用的。最后系统要实现很多时间体系，直接访问 RTC 灵活性也不够。所以直接访问 RTC 是一个很差的设计，那么该怎么实现知时的需求呢?

Linux 提出了系统时钟的概念，它是一个软件时钟，相应的把 RTC 叫做硬件时钟。系统时钟是用一个变量 xtime(jiffies)记录现在的时间点，xtime 的初始值用 RTC 来初始化，这样就只用访问 RTC 一次就可以了，然后 xtime 的值随着计时器的增长而增长。xtime 的值的更新有两种情况，一种是调度器 tick 的时候从计时器更新一下，一种是读 xtime 的时候从计时器更新一下。

Linux 中用来实现系统时钟的软件体系叫做 The Linux Timekeeping Architecture。其有三个基本概念：

- 走时器(struct timekeeper)，用来记录一些基本数据，包括系统时钟的当前时间值和其它全局时间体系的一些数据；
- 时钟源(struct clocksouce)，是对计时器硬件的一种抽象；
- 时钟事件设备(struct clock_event_device)，是对定时器硬件的一种抽象。这三个对象相互配合共同构成了系统时钟。

```c
/**
 * struct timekeeper - Structure holding internal timekeeping values.
 * @tkr_mono:		The readout base structure for CLOCK_MONOTONIC
 * @tkr_raw:		The readout base structure for CLOCK_MONOTONIC_RAW
 * @xtime_sec:		Current CLOCK_REALTIME time in seconds
 * @ktime_sec:		Current CLOCK_MONOTONIC time in seconds
 * @wall_to_monotonic:	CLOCK_REALTIME to CLOCK_MONOTONIC offset
 * @offs_real:		Offset clock monotonic -> clock realtime
 * @offs_boot:		Offset clock monotonic -> clock boottime
 * @offs_tai:		Offset clock monotonic -> clock tai
 * @tai_offset:		The current UTC to TAI offset in seconds
 * @clock_was_set_seq:	The sequence number of clock was set events
 * @cs_was_changed_seq:	The sequence number of clocksource change events
 * @next_leap_ktime:	CLOCK_MONOTONIC time value of a pending leap-second
 * @raw_sec:		CLOCK_MONOTONIC_RAW  time in seconds
 * @monotonic_to_boot:	CLOCK_MONOTONIC to CLOCK_BOOTTIME offset
 * @cycle_interval:	Number of clock cycles in one NTP interval
 * @xtime_interval:	Number of clock shifted nano seconds in one NTP
 *			interval.
 * @xtime_remainder:	Shifted nano seconds left over when rounding
 *			@cycle_interval
 * @raw_interval:	Shifted raw nano seconds accumulated per NTP interval.
 * @ntp_error:		Difference between accumulated time and NTP time in ntp
 *			shifted nano seconds.
 * @ntp_error_shift:	Shift conversion between clock shifted nano seconds and
 *			ntp shifted nano seconds.
 * @last_warning:	Warning ratelimiter (DEBUG_TIMEKEEPING)
 * @underflow_seen:	Underflow warning flag (DEBUG_TIMEKEEPING)
 * @overflow_seen:	Overflow warning flag (DEBUG_TIMEKEEPING)
 *
 * Note: For timespec(64) based interfaces wall_to_monotonic is what
 * we need to add to xtime (or xtime corrected for sub jiffie times)
 * to get to monotonic time.  Monotonic is pegged at zero at system
 * boot time, so wall_to_monotonic will be negative, however, we will
 * ALWAYS keep the tv_nsec part positive so we can use the usual
 * normalization.
 *
 * wall_to_monotonic is moved after resume from suspend for the
 * monotonic time not to jump. We need to add total_sleep_time to
 * wall_to_monotonic to get the real boot based time offset.
 *
 * wall_to_monotonic is no longer the boot time, getboottime must be
 * used instead.
 *
 * @monotonic_to_boottime is a timespec64 representation of @offs_boot to
 * accelerate the VDSO update for CLOCK_BOOTTIME.
 */
struct timekeeper {
	struct tk_read_base	tkr_mono;
	struct tk_read_base	tkr_raw;
	u64			xtime_sec;
	unsigned long		ktime_sec;
	struct timespec64	wall_to_monotonic;
	ktime_t			offs_real;
	ktime_t			offs_boot;
	ktime_t			offs_tai;
	s32			tai_offset;
	unsigned int		clock_was_set_seq;
	u8			cs_was_changed_seq;
	ktime_t			next_leap_ktime;
	u64			raw_sec;
	struct timespec64	monotonic_to_boot;

	/* The following members are for timekeeping internal use */
	u64			cycle_interval;
	u64			xtime_interval;
	s64			xtime_remainder;
	u64			raw_interval;
	/* The ntp_tick_length() value currently being used.
	 * This cached copy ensures we consistently apply the tick
	 * length for an entire tick, as ntp_tick_length may change
	 * mid-tick, and we don't want to apply that new value to
	 * the tick in progress.
	 */
	u64			ntp_tick;
	/* Difference between accumulated time and NTP time in ntp
	 * shifted nano seconds. */
	s64			ntp_error;
	u32			ntp_error_shift;
	u32			ntp_err_mult;
	/* Flag used to avoid updating NTP twice with same second */
	u32			skip_second_overflow;
#ifdef CONFIG_DEBUG_TIMEKEEPING
	long			last_warning;
	/*
	 * These simple flag variables are managed
	 * without locks, which is racy, but they are
	 * ok since we don't really care about being
	 * super precise about how many events were
	 * seen, just that a problem was observed.
	 */
	int			underflow_seen;
	int			overflow_seen;
#endif
};
```

```c
/**
 * struct clocksource - hardware abstraction for a free running counter
 *	Provides mostly state-free accessors to the underlying hardware.
 *	This is the structure used for system time.
 *
 * @read:		Returns a cycle value, passes clocksource as argument
 * @mask:		Bitmask for two's complement
 *			subtraction of non 64 bit counters
 * @mult:		Cycle to nanosecond multiplier
 * @shift:		Cycle to nanosecond divisor (power of two)
 * @max_idle_ns:	Maximum idle time permitted by the clocksource (nsecs)
 * @maxadj:		Maximum adjustment value to mult (~11%)
 * @uncertainty_margin:	Maximum uncertainty in nanoseconds per half second.
 *			Zero says to use default WATCHDOG_THRESHOLD.
 * @archdata:		Optional arch-specific data
 * @max_cycles:		Maximum safe cycle value which won't overflow on
 *			multiplication
 * @name:		Pointer to clocksource name
 * @list:		List head for registration (internal)
 * @rating:		Rating value for selection (higher is better)
 *			To avoid rating inflation the following
 *			list should give you a guide as to how
 *			to assign your clocksource a rating
 *			1-99: Unfit for real use
 *				Only available for bootup and testing purposes.
 *			100-199: Base level usability.
 *				Functional for real use, but not desired.
 *			200-299: Good.
 *				A correct and usable clocksource.
 *			300-399: Desired.
 *				A reasonably fast and accurate clocksource.
 *			400-499: Perfect
 *				The ideal clocksource. A must-use where
 *				available.
 * @id:			Defaults to CSID_GENERIC. The id value is captured
 *			in certain snapshot functions to allow callers to
 *			validate the clocksource from which the snapshot was
 *			taken.
 * @flags:		Flags describing special properties
 * @enable:		Optional function to enable the clocksource
 * @disable:		Optional function to disable the clocksource
 * @suspend:		Optional suspend function for the clocksource
 * @resume:		Optional resume function for the clocksource
 * @mark_unstable:	Optional function to inform the clocksource driver that
 *			the watchdog marked the clocksource unstable
 * @tick_stable:        Optional function called periodically from the watchdog
 *			code to provide stable synchronization points
 * @wd_list:		List head to enqueue into the watchdog list (internal)
 * @cs_last:		Last clocksource value for clocksource watchdog
 * @wd_last:		Last watchdog value corresponding to @cs_last
 * @owner:		Module reference, must be set by clocksource in modules
 *
 * Note: This struct is not used in hotpathes of the timekeeping code
 * because the timekeeper caches the hot path fields in its own data
 * structure, so no cache line alignment is required,
 *
 * The pointer to the clocksource itself is handed to the read
 * callback. If you need extra information there you can wrap struct
 * clocksource into your own struct. Depending on the amount of
 * information you need you should consider to cache line align that
 * structure.
 */
struct clocksource {
	u64			(*read)(struct clocksource *cs);
	u64			mask;
	u32			mult;
	u32			shift;
	u64			max_idle_ns;
	u32			maxadj;
	u32			uncertainty_margin;
#ifdef CONFIG_ARCH_CLOCKSOURCE_DATA
	struct arch_clocksource_data archdata;
#endif
	u64			max_cycles;
	const char		*name;
	struct list_head	list;
	int			rating;
	enum clocksource_ids	id;
	enum vdso_clock_mode	vdso_clock_mode;
	unsigned long		flags;

	int			(*enable)(struct clocksource *cs);
	void			(*disable)(struct clocksource *cs);
	void			(*suspend)(struct clocksource *cs);
	void			(*resume)(struct clocksource *cs);
	void			(*mark_unstable)(struct clocksource *cs);
	void			(*tick_stable)(struct clocksource *cs);

	/* private: */
#ifdef CONFIG_CLOCKSOURCE_WATCHDOG
	/* Watchdog related data, used by the framework */
	struct list_head	wd_list;
	u64			cs_last;
	u64			wd_last;
#endif
	struct module		*owner;
};
```

```c
/**
 * struct clock_event_device - clock event device descriptor
 * @event_handler:	Assigned by the framework to be called by the low
 *			level handler of the event source
 * @set_next_event:	set next event function using a clocksource delta
 * @set_next_ktime:	set next event function using a direct ktime value
 * @next_event:		local storage for the next event in oneshot mode
 * @max_delta_ns:	maximum delta value in ns
 * @min_delta_ns:	minimum delta value in ns
 * @mult:		nanosecond to cycles multiplier
 * @shift:		nanoseconds to cycles divisor (power of two)
 * @state_use_accessors:current state of the device, assigned by the core code
 * @features:		features
 * @retries:		number of forced programming retries
 * @set_state_periodic:	switch state to periodic
 * @set_state_oneshot:	switch state to oneshot
 * @set_state_oneshot_stopped: switch state to oneshot_stopped
 * @set_state_shutdown:	switch state to shutdown
 * @tick_resume:	resume clkevt device
 * @broadcast:		function to broadcast events
 * @min_delta_ticks:	minimum delta value in ticks stored for reconfiguration
 * @max_delta_ticks:	maximum delta value in ticks stored for reconfiguration
 * @name:		ptr to clock event name
 * @rating:		variable to rate clock event devices
 * @irq:		IRQ number (only for non CPU local devices)
 * @bound_on:		Bound on CPU
 * @cpumask:		cpumask to indicate for which CPUs this device works
 * @list:		list head for the management code
 * @owner:		module reference
 */
struct clock_event_device {
	void			(*event_handler)(struct clock_event_device *);
	int			(*set_next_event)(unsigned long evt, struct clock_event_device *);
	int			(*set_next_ktime)(ktime_t expires, struct clock_event_device *);
	ktime_t			next_event;
	u64			max_delta_ns;
	u64			min_delta_ns;
	u32			mult;
	u32			shift;
	enum clock_event_state	state_use_accessors;
	unsigned int		features;
	unsigned long		retries;

	int			(*set_state_periodic)(struct clock_event_device *);
	int			(*set_state_oneshot)(struct clock_event_device *);
	int			(*set_state_oneshot_stopped)(struct clock_event_device *);
	int			(*set_state_shutdown)(struct clock_event_device *);
	int			(*tick_resume)(struct clock_event_device *);

	void			(*broadcast)(const struct cpumask *mask);
	void			(*suspend)(struct clock_event_device *);
	void			(*resume)(struct clock_event_device *);
	unsigned long		min_delta_ticks;
	unsigned long		max_delta_ticks;

	const char		*name;
	int			rating;
	int			irq;
	int			bound_on;
	const struct cpumask	*cpumask;
	struct list_head	list;
	struct module		*owner;
};
```

系统可能会有很多计时器硬件和定时器硬件。在系统启动时每个硬件都会初始化并注册自己。注册完之后系统会**选择一个最佳的时钟源作为走时器的时钟源，选择一个最佳的时钟事件设备作为更新系统时钟的设备**。**系统启动时会去读取 RTC 的值来初始化系统时钟的值**，然后**时钟事件设备不断产生周期性的定时器事件**，在定时器事件处理函数中会读取时钟源的值，再减去上一次读到的值，得到时间差，这个时间差就是系统时钟应该前进的时间值，把这个值更新到走时器中，并相应更新其它时间体系的值。系统时钟就是按照这种方式不断地在走时。系统时钟除了在启动时和休眠唤醒时会去读取 RTC 的值，其它时间都不会和 RTC 交换，两者各自独立地走时，互不影响。

同时我们也可以用系统命令 hwclock，修改 RTC，以及同步系统时间和硬件时间。hwclock --hctosys 把硬件时钟同步到系统时钟，hwclock --systohc 把系统时钟同步到硬件时钟。事实上我们发现用 settimeofday 修改的系统时钟在系统重启后生效了，并没有丢失，这是为什么呢？是因为系统默认的关机脚本里面会执行 hwclock --systohc，把系统时钟同步到硬件时钟，所以我们修改的系统时钟才不会丢失。所以如果计算机不联网，不能更新系统时间的话，一段时间后会发现时间不准确了。

### 动态 tick 与定时器

低精度定时器是内核在早期就有的定时器接口，它的实现是靠调度器 tick 来驱动的。高精度定时器是随着硬件和软件的发展而产生的。**调度器 tick 的 HZ(每秒 tick 多少次)是可以配置**，它的配置选项有 4 个，100,、250、300、1000，也即是说每次 tick 的间隔是 10ms、4ms、3.3ms、1ms。所以用调度器 tick 来驱动低精度定时器是很合适的，tick 的精度能满足低精度定时器的精度。但是用调度器 tick 来驱动高精度定时器就不合适了，因为这样高精度定时器的精度最多是 1ms，达不到纳秒的级别，这样就算不上是高精度定时器了。所以对于高精度定时器来说，情况就正好反了过来，高精度定时器直接用硬件实现，然后创建一个软件高精度定时器来模拟调度器 tick。也就是说，对于只有低精度定时器的系统来说，是调度器 tick 驱动低精度定时器；**对于有高精度定时器的系统来说，是高精度定时器驱动调度器 tick，这个调度器 tick 再去驱动低精度定时器**。

内核的低精度定时器接口和高精度定时器接口都是一次性的，不是周期性的。通过一次性的定时器可以实现周期性的定时器，方法是**在每次定时器到期时再设置下一次的定时器，一直这样就形成了周期性的**（QEMU 也是这样实现的）。这里说的是定时器接口的一次性和周期性，而不是定时器硬件。下面我们再来看看定时器硬件是一次性的还是周期性的。定时器硬件本身可以是一次性的也可以是周期性的，也可以两种模式都存在，由内核选择使用哪一种。对于低精度定时器来说，它的定时器硬件可以是一次性的也可以是周期性的，由于调度器 tick 是周期性的，所以它的底层硬件就是周期性的。低精度定时器的精度最多是 1ms，也就是定时器中断做多一秒有 1000 次，这对于系统来说是可以承受的。但是对于高精度定时器来说，理论上它的定时器硬件也可以是周期性的。但是如果它的定时器硬件是周期性的，由于它的精度最多可以达到 1 纳秒，也就是说 1 纳秒要发生一次定时器中断，每秒发生 10 亿次。这对于系统来说是不可承受的，而且并不是每纳秒都有定时器事件要处理，所以大部分定时器中断是没有用的。如果我们把 1ns 1 次中断改为 1ms，1ms 1 次中断不就可以大大减少中断的数量，但是这样定时器的精度就是 1ms，达不到 1ns 的要求了。所以对于高精度定时器，底层的定时器硬件就只能是一次性的了。每次定时器事件到来的时候再去查看一下下一个最近的定时器事件什么时候到期，然后再去设置一下定时器硬件。这样高精度定时器就可以一直运行下去了。但是我们的调度器 tick 也需要定时器中断，而且是周期性的，怎么办？好办，创建一个到期时间为 1ms 的高精度定时器，每次到期的时候再设置一下继续触发，这样就形成了一个 1000HZ 周期性的定时器事件，就可以驱动调度器 tick。

下面我们讲一下定时器和调度器 tick 的初始化过程，以 x86 为例。系统启动时会先初始化 timekeeping。然后 hpet 注册自己，hpet 既有定时器也有计时器，hpet 定时器会成为系统定时器，hpet 计时器会成为 timekeeper 的时钟源。后面 tsc 计时器也会注册自己，并成为最终的时钟源。Local APIC Timer 定时器也会注册自己，并成为最终的 per CPU tick device。hpet 最终只能做 broadcast 定时器了。系统在每次 run local timer 的时候都会检测一下，**如果不支持高精度定时器，就尝试切换到动态 tick 模式**，如果支持高精度定时器就切换到高精度定时器模式，此模式下会尝试切换到动态 tick 模式。当高精度定时器和动态 tick 设置成功之后，Local APIC Timer 会运行在一次性模式，调度器 tick 是由一个叫做 sched_timer 的高精度定时器驱动的。每次定时器到期时都会 reprogram next event。

### 注意

这篇文章大部分内容来自于[这里](https://mp.weixin.qq.com/s/RaWdSnpgWr0-G6fqXP36Fg)，这里做个记录，加强自己对时钟子系统概念的理解，之后有需要再结合源码进行分析。

### Reference

[1] https://zhuanlan.zhihu.com/p/355852183

[2] https://mp.weixin.qq.com/s/RaWdSnpgWr0-G6fqXP36Fg
