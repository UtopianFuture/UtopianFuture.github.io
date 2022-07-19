## CPU timer

初步思路：LA 中有指令 `rdtime.d rd, rj` 可以将恒定频率计时器 Stable Counter 的信息写入通用寄存器 rd 中，将每个计时器的软件可配置的全局唯一编号 Count ID 信息写入 rj 中。那么我只需要用 `rdtime.d` 指令获取时间，然后保存在一个 64 位变量中就可以实现定时的功能。

### 1. 硬定时器

#### 1.1. 实时时钟（RTC）

所有的 PC 都包含一个实时时钟（Renl Time Clock, RTC），它是**独立于 CPU 和所有其他芯片的，即是当 PC 被切断电源，RTC 还继续工作，因为它靠一个小电池供电**。Linux 只用 RTC 来获取时间和日期。内核通过 `0x70`, `0x71` I/O 端口访问 RTC，LA 应该类似。

#### 1.2. 时间戳计数器（TSC）

**所有的 x86 处理器都包含一条 CLK 输入引线**，它接受外部振荡器的时钟信号，同时实现一个 64 位的时间戳计数器（Time Stamp Counter, TSC）寄存器，它在每个时钟信号到达时加 1，可以用汇编指令 `rdtsc` 读这个寄存器。在 LA 中这个寄存器是 SC（Stable Counter），用指令 `rdtime.d` 可以读取。

#### 1.3. 可编程间隔定时器（PIT）

定时器和前两个时钟不同，前两个是用来计数的，它是在经过一个时间间隔后通过时钟中断（timer interrupt）来通知内核。

#### 1.4. CPU 本地定时器

和 PIT 类似，能够产生单步中断或周期性中断的设备。不同的是本地 APIC 定时器把中断只发送给自己的处理器，而 PIT 产生的是全局性中断。

#### 1.5. 高精度时间定时器（HPET）

相比于 PIT 功能更全面，更精确。可以通过映射到内存空间的寄存器来对 HPET 芯片编程。

#### 1.6. ACPI 电源管理定时器

和 TSC 类似，在每个时钟节拍到来时加 1，也是通过寄存器访问。

### 2. 周期性输出 Hello World

这个比较简单，只要用 `rdtime.d rd, rj` 指令获取到 Stable Counter，然后再设置多少个时钟周期输出一次即可。

```c
asm(".long 0x1badb002, 0, (-(0x1badb002 + 0))");

#define NSEC_PER_SEC	1000000000L
unsigned char *videobuf = (unsigned char*)0x800000001fe001e0;
const char *str = "Hello World!!\n";

int start_entry(void)
{
	unsigned long long stable_count = 1;
	unsigned long long cound_id = 0;
	while (1) {
		if ((stable_count % 10000000) == 0) {
    		for (int i = 0; str[i]; i++) {
    		    videobuf[0] = str[i];
	    		videobuf[1] = 0x0f;
			}
		}
		stable_count = 1; //initialize count every time
		asm volatile("rdtime.d %0, %1\n\t"
					: "=r"(stable_count), "=r"(cound_id)
					: :);
	}
	while (1) { }
    // asm("break");
	return 0;
}
```

但这种方法有个问题，即只能设置间隔多少个时钟周期输出一次，而不能指定多少秒输出一次，因为不知道 CPU 频率，这点还需要看看内核怎么计算的。

### 问题

（1）能够周期性产生时钟信号的硬件有哪些？

（2）哪些地方需要用到时钟？

《深入理解 LINUX 内核》第六章-LINUX 计时体系结构

（3）jiffies 和 TSC 有什么区别？

jiffies 也是计时器，用来记录自系统启动后产生的节拍总数；xtime 变量存放当前时间和日期。这两个变量相当于 TSC 和 RTC 的高级语言定义。
