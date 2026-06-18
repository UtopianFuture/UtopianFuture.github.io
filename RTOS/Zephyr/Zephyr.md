## Zephyr

### 目录

- [生态系统](#生态系统)
- [系统架构](#系统架构)
- [编译框架](#编译框架)
  - [编译方法](#编译方法)
  - [编译脚本](#编译脚本)
- [设备树](#设备树)
- [内核初始化](#内核初始化)
  - [关键函数 z_arm_prep_c](#关键函数 z_arm_prep_c)
  - [关键函数 z_cstart](#关键函数 z_cstart)
  - [关键函数 prepare_multithreading](#关键函数 prepare_multithreading)
  - [关键函数 switch_to_main_thread](#关键函数 switch_to_main_thread)
  - [关键函数 bg_thread_main](#关键函数 bg_thread_main)
- [内存管理](#内存管理)
  - [sys_heap](#sys_heap)
    - [chunk](#chunk)
    - [chunk 的操作](#chunk 的操作)
    - [sys_heap 初始化](#sys_heap 初始化)
    - [内存分配](#内存分配)
    - [内存释放](#内存释放)
  - [kheap](#kheap)
    - [kheap 初始化](#kheap 初始化)
    - [kheap 分配内存](#kheap 分配内存)
    - [kheap 释放内存](#kheap 释放内存)
  - [问题](#问题)
  - [slab](#slab)
- [进程调度](#进程调度)
  - [协作线程](#协作线程)
  - [抢占线程](#抢占线程)
  - [z_kernel](#z_kernel)
  - [k_thread](#k_thread)
  - [_thread_base](#_thread_base)
  - [就绪队列](#就绪队列)
  - [等待队列](#等待队列)
  - [关键函数 z_setup_new_thread](#关键函数 z_setup_new_thread)
  - [关键函数 update_cache](#关键函数 update_cache)
  - [关键函数 z_reschedule](#关键函数 z_reschedule)
  - [关键函数 z_arm_pendsv](#关键函数 z_arm_pendsv)
- [中断管理](#中断管理)
- [功耗管理](#功耗管理)
- [同步与互斥](#同步与互斥)
- [驱动模型](#驱动模型)
- [文件系统](#文件系统)
- [问题](#问题)
- [Reference](#Reference)

### 生态系统

目前所知有 4 个子系统会运行 zephyr，

- SPU: cortex-R52 x2 (DLCS)。System Power Unit 是一种用于处理处理器的电源管理的子系统，具体而言，**SPU** **主要负责处理器的电源管理和功耗控制**。它监测处理器的电源状态，根据需要调整电源供应，以实现功耗优化和节能。SPU 还可以监测处理器的温度和电压等参数，以确保处理器的安全运行；

- RPU: cortex-R52 x4 (AMP)。在 xVSS 子系统中，以处理中断为主，并不涉及复杂的计算任务。xVSS 子系统对 MIPIRXSS 传入的 sensor （camera, lidar 等）的数据进行处理，sensor 获取到的数据算法不能直接处理，需要经过 xVSS 的 RSP 流程处理，放到 DDR 中，给后续模块使用；

- SCP: cortex-R52 x1。system Control Subsystem（系统控制子系统）主要负责处理器的系统级控制和管理功能。它包含了多个模块和功能，**用于管理处理器的电源、时钟、重置、中断控制、安全性等方面**。 

  - 电源管理：负责处理处理器的电源控制和管理，包括电源状态的转换、电源域的划分、电源模式的管理等；

  - 时钟管理：控制和管理处理器的时钟频率、时钟源的选择、时钟分频等，以确保处理器的正常运行和节能优化；

  - 重置控制：管理处理器的重置信号，包括处理器的复位、外设的重置等，以确保处理器和系统在需要时能够正确地进行重启和初始化；

  - 中断控制：处理和管理处理器的中断信号，包括中断的触发、中断优先级的管理、中断处理程序的调度等；

  - 安全性管理：提供安全性相关的功能和机制，如安全状态的切换、安全监视器的管理、安全性扩展等，以保护处理器和系统的安全性；

- RSU: cortex-R52 x1。在 DLA 子系统中，负责推理网络任务的调度和执行，以及 NPC 下面各种计算资源，内存资源的实时管理；

这里先从 HPC，也就是负责运行 preception 等神经网络算法的组件入手。DLA 的结构如下图所示：

![image-20250219143703028](C:\Users\xpeng\AppData\Roaming\Typora\typora-user-images\image-20250219143703028.png)

各组件功能如下：

- DLA: Deep Learning Accelerator, 1 DLA contain 2 NPU cluster, which called NPC. Within a NPC, there are 4 NPU(neural processing unit) compute cores.
- NPU: 跑神经网络的；

- DSP: data signal processing for computing（SIMD 指令集），用于补充 NPU 支持不好的算子，承担额外算力支撑；**哪些业务 NPU 不支持，需要运行在 DSP 上？**

- RSU: 负责推理网络任务的调度和执行，以及 NPC 下面各种计算资源，内存资源的实时管理；

整个 DLA 的工作流程如下：

![image-20250219104049413](C:\Users\xpeng\AppData\Roaming\Typora\typora-user-images\image-20250219104049413.png)

①Task Request ：APU向RSU发起推理任务Request，APU事先建立的Task Req在L3.DDR的Task Ring中

②Fetch task packet ：RSU从L3.DDR的Task Ring中取Task packet； 

③Fetch fetch CMD packet ：RSU从L3.DDR中取CMD packet； 

④Task distribution ： RSU 下发指令指针并Launch NPU/CDTU/DSP，RSU解析Task和Model CMD packet， 根据Model CMD中的信息下发各engine待运行指令指针（DDR 地址和长度）； 

⑤Instruction Fetch：各Engine取指令； 

⑥Load Data to L2 ：CDTU 将L3.DDR Input数据load到L2，数据类型包括Input； 

⑦Load Data to L1 ：NPU/DSP中的DTU/DMA将L2 Input数据load到NPU/DSP中的L1； 

⑧Load Parameter to L2 ：CDTU 将L3.DDR中的Parameter load到L2； 

⑨Load Parameter to L1 ：NPU/DSP中的DTU/DMA将L2 Parameter load到NPU/DSP中的L1； 

⑩Compute ：NPU/DSP 算子计算； 

⑪Store Data to L2 ：NPU/DSP中的DTU/DMA将L1数据store到L2，数据类型包括中间缓存的激活数据（Activation）； 

⑫Store Data to L3 ：CDTU将L2数据store到L3，数据类型包括中间缓存的激活数据（Activation）； 

⑬Load Data to L2 ：CDTU 将L3.DDR中计算的中间数据load到L2； 

⑭Load Data to L1 ：NPU/DSP中的DTU/DMA将L2 中计算的中间数据 load到NPU/DSP中的L1； 

（⑤ ~ ⑭ 分部循环执行） 

⑮Store Inference Result to L2 ：NPU/DSP中的DTU/DMA将L1计算的结果store到L2; 

⑯Store Inference Result to L2 ：NDTU将L1中的结果store到L2; 

⑰Task finish response to RSU ：NPU/DSP/CDTU通过Mail或中断方式向RSU返回Task结束response 

⑱Task finish response to APU ：RSU通过Mail或中断方式向APU返回Task结束response

zephyr 在 XP5 中的作用，一个就是运行在 NPC 的 RSU（一个 cortex R52 core）上，**负责推理网络任务的调度和执行，以及 NPC 下面各种计算资源，内存资源的实时管理**；

下面从代码角度分析其怎样进行资源管理，后续再深入分析每一步操作。

### 系统架构

Zephyr OS 的系统架构图如下，

![zephyr-arch](D:\gitlab\UtopianFuture.github.io\image\zephyr-arch.png)

Kernel 涵盖以下内容：

- 提供 Kernel service，例如内存管理， 进程调度，中断管理，电源管理，同步，数据传递，时间管理等；
- 平台相关的特殊驱动，例如 Radios，传感器，加密硬件，Flash 等；

### 编译框架

#### 编译方法

```sh
build rtos
```

#### 编译脚本

```c
| build/scope/rtos/build_rtos.sh: build_rtos
|    -> tools/ztools/zbuild/zbuild.py: custom_process
|        -> tools/ztools/zbuild/python/zcompile.py: FirmwareCompile: process
|            -> tools/ztools/zbuild/sh/zbuild.sh: main
|                -> tools/ztools/zbuild/sh/func_zcompile.sh: build_compile
|                    -> tools/ztools/zbuild/sh/func_zcompile.sh: build_zephyr
```

rtos 采用的是 cmake 编译框架，CMakeLists.txt 就是编译命令，会自动生成 makefile 文件。

### 设备树

zephyr 设备树主要面向资源受限的嵌入式系统，**Zephyr 并不直接使用设备树编译后的二进制文件（DTB）**。相反，它**在构建编译阶段通过脚本将设备树信息转换为 C 语言头文件中的宏定义**。这样，内核和应用程序可以通过调用这些宏定义来访问设备树信息，减少了运行时的资源消耗。

**Linux 设备树则直接使用设备树编译后的 DTB 文件**。在系统启动时，Linux 内核会解析 DTB 文件，获取硬件信息并进行板级初始化。这种方式提供了更高的灵活性和实时性。

### 内核初始化

```c
| arch/aarch32/reset.S: __start
|	-> arch/arm/core/aarch32/prep_c.c: z_arm_prep_c
|		-> kernel/init.c: z_cstart
|			-> arch_kernel_init
|			-> z_device_state_init
|			-> switch_to_main_thread
|			-> arch_switch_to_main_thread
|				-> z_arm_prepare_switch_to_main
|				
|			-> prepare_multithreading
|				-> z_sched_init
|				-> z_setup_new_thread // 初始化 z_main_thread，这个进程会做哪些工作呢？
|				// 就是将 k_thread->base 中表示进程状态的 thread_state 的 _THREAD_PRESTART 位清 0
|				-> z_mark_thread_as_started
|			-> bg_thread_main
```

#### 关键函数 z_arm_prep_c

```c
void z_arm_prep_c(void)
{
	relocate_vector_table(); // 重新分配向量表
#if defined(CONFIG_CPU_HAS_FPU)
	z_arm_floating_point_init();
#endif
	z_bss_zero(); // bss 段清 0，其实就是 memset，和 kernel 有区别么？
	z_data_copy(); // 复制 data 段，memcpy
#if (defined(CONFIG_ARMV7_R) || defined(CONFIG_CPU_AARCH32_CORTEX_R)) && defined(CONFIG_INIT_STACKS)
	z_arm_init_stacks();
#endif
	z_arm_interrupt_init(); // 回调到 __NVIC_SetPriority，这种写法没咋见过
	z_cstart(); // 相当于 start_kernel
	CODE_UNREACHABLE;
}
```

#### 关键函数 z_cstart

相比于 `start_kernel`，这个太简单了。

```c
__boot_func FUNC_NORETURN void z_cstart(void)
{
	/* gcov hook needed to get the coverage report.*/
	gcov_static_init(); // 厉害啊，连 gcov 都实现了

	LOG_CORE_INIT();

	/* perform any architecture-specific initialization */
	arch_kernel_init();

#if defined(CONFIG_MULTITHREADING)
	/* Note: The z_ready_thread() call in prepare_multithreading() requires
	 * a dummy thread even if CONFIG_ARCH_HAS_CUSTOM_SWAP_TO_MAIN=y
	 */
	struct k_thread dummy_thread;

	z_dummy_thread_init(&dummy_thread);
#endif
	/* do any necessary initialization of static devices */
	z_device_state_init(); // I/O 框架后续再分析

	/* perform basic hardware initialization */
	z_sys_init_run_level(_SYS_INIT_LEVEL_PRE_KERNEL_1);
	z_sys_init_run_level(_SYS_INIT_LEVEL_PRE_KERNEL_2);

	...

#ifdef CONFIG_MULTITHREADING
	switch_to_main_thread(prepare_multithreading()); // 切换到 main thread 执行
#else
    ...
#endif /* CONFIG_MULTITHREADING */

	/*
	 * Compiler can't tell that the above routines won't return and issues
	 * a warning unless we explicitly tell it that control never gets this
	 * far.
	 */

    // kernel 中到没见过这种写法
    // 而是进入到 rest_init，切换到用户态开始执行了
	CODE_UNREACHABLE; /* LCOV_EXCL_LINE */
}
```

#### 关键函数 prepare_multithreading

```c
__boot_func static char *prepare_multithreading(void)
{
	char *stack_ptr;

	/* _kernel.ready_q is all zeroes */
    // 初始化就绪队列，_kernel.ready_q.runq
    // 系统中就绪队列只有一个，等待队列有多个
	z_sched_init();

#ifndef CONFIG_SMP
	_kernel.ready_q.cache = &z_main_thread;
#endif
    // z_main_thread 是主进程，bg_thread_main 是该进程执行的操作
    // 一个表示资源，一个表示任务
	stack_ptr = z_setup_new_thread(&z_main_thread, z_main_stack,
				       CONFIG_MAIN_STACK_SIZE, bg_thread_main,
				       NULL, NULL, NULL,
				       CONFIG_MAIN_THREAD_PRIORITY,
				       K_ESSENTIAL, "main");
	z_mark_thread_as_started(&z_main_thread);
	z_ready_thread(&z_main_thread);

    // 看起来是每个 CPU 都有一个 idle_thread，和 kernel 有区别
    // kernel 是 0 号进程完成初始化操作之后转为 idle 进程
    // 后续如果没有进程在跑，就会转到 idle 进程
    // 但进一步想想，每个 CPU 的任务是不一样的，如果多个 CPU 都没有 ready 状态的进程，
    // 那这个 idle 进程是怎样执行的呢？
	for (int i = 0; i < CONFIG_MP_NUM_CPUS; i++) {
		init_idle_thread(i);
		_kernel.cpus[i].idle_thread = &z_idle_threads[i];
		_kernel.cpus[i].id = i;
		_kernel.cpus[i].irq_stack =
			(Z_KERNEL_STACK_BUFFER(z_interrupt_stacks[i]) +
			 K_KERNEL_STACK_SIZEOF(z_interrupt_stacks[i]));

	...

	}

	return stack_ptr; // 不是有 z_main_thread 么，为什么还要这个指针
}

```

#### 关键函数 switch_to_main_thread

```c
| switch_to_main_thread
|	-> arch_switch_to_main_thread
|		-> z_thread_entry
```

```c
FUNC_NORETURN void z_thread_entry(k_thread_entry_t entry,
				 void *p1, void *p2, void *p3)
{
	entry(p1, p2, p3); // 通过这种方式进程开始工作，entry 在这里是 bg_thread_main

	k_thread_abort(k_current_get()); // 这个是干嘛的
}
```

#### 关键函数 bg_thread_main

```c
__boot_func
static void bg_thread_main(void *unused1, void *unused2, void *unused3)
{
	...
    
	z_sys_post_kernel = true;

	z_sys_init_run_level(_SYS_INIT_LEVEL_POST_KERNEL);

	boot_banner(); // 打印一些 OS 信息

    ...

	/* Final init level before app starts */
	z_sys_init_run_level(_SYS_INIT_LEVEL_APPLICATION);

    // 不知道这些 static threads 在哪里定义的
    // _static_thread_data_area 应该是个关键
    // 对于 __static_thread_data_area_list_start 
    // - __static_thread_data_area_list_end 数组内的 _static_thread_data
    // 都会调用 z_setup_new_thread 初始化
    // 然后调用 schedule_new_thread 标记上 _THREAD_PRESTART
    // 最后加入到 _kernel.ready_q.runq 中？
	z_init_static_threads();

	...

	extern void main(void);

	main(); // 为啥是 nop 函数

	/* Mark nonessenrial since main() has no more work to do */
	z_main_thread.base.user_options &= ~K_ESSENTIAL;

	...
}
```

具体进程怎样加入到调度队列，请看后面的[进程调度](#进程调度)。

### 内存管理

Heap 分配器是一个 Zephyr 内核对象，Heap 管理的是一片固定大小的连续内存区域，用户可以在 Heap 管理的内存区域中动态分配任意长度的内存。在 Zephyr 中 lib/os/heap.c 提供了底层的 Heap 内存管理算法，称作为`sys_heap`，该层级的 API 无法支持多线程。kernel/kheap.c 建立在 `sys_heap` 之上实现了 `k_heap`，`k_heap ` 提供多线程和同步等待内存管理功能，当线程在 heap 内存不足时申请 heap 内存，可以等待其它线程释放内存。

#### sys_heap

##### chunk

##### chunk 的操作

##### sys_heap 初始化

##### 内存分配

##### 内存释放

#### kheap

`sys_heap` 提供的是基本的内存分配算法，无多线程安全。kheap 对 `sys_heap` 进行包装提供多线程安全的 API，以及同步分配等待的特性。从 `struct k_heap` 的结构就可以看出来：

```c
struct k_heap {
	struct sys_heap heap; // 使用的 sys_heap
	_wait_q_t wait_q; // waitq, 用于在分配时无内存进行等待
	struct k_spinlock lock; // 分配/释放内存时上锁，保证多线程访问安全
};
```

##### kheap 初始化

kheap 初始化分为静态和动态两种，静态初始化使用下面宏定义 K_HEAP，在 k_heap section 中放置 bytes 大小的全局变量，

```c
#define Z_HEAP_DEFINE_IN_SECT(name, bytes, in_section)		\
	char in_section						\
	     __aligned(8) /* CHUNK_UNIT */			\
	     kheap_##name[MAX(bytes, Z_HEAP_MIN_SIZE)];		\ // 定义一个 bytes 大小的变量表示该 heap
	STRUCT_SECTION_ITERABLE(k_heap, name) = {		\
		.heap = {					\
			.init_mem = kheap_##name,		\
			.init_bytes = MAX(bytes, Z_HEAP_MIN_SIZE), \
		 },						\
	}

#define K_HEAP_DEFINE(name, bytes)				\
	Z_HEAP_DEFINE_IN_SECT(name, bytes,			\
			      __noinit_named(kheap_buf_##name))
```

这种管理方式叫做 heap，不过在 linux kernel 中，定义局部变量（mmap 也是走栈地址空间）是从进程地址空间的栈（stack）空间申请内存的，一般是从高地址向低地址扩展。而堆（heap）虚拟地址空间是 malloc 分配内存时使用的。

##### kheap 分配内存

##### kheap 释放内存

#### 问题

- linux kernel 中的 buddy 系统使用的内存是从 dts 中解析，然后在 kernel 初始化经过 memblock 释放给 buddy 的，那 kheap 的物理内存从哪里来？
  - 想通过正面分析，看有没有类似于 linux kernel 的 memblock 或者 of 类接口解析 dts，但都没有；
  - 尝试将所有的分配方式都分析一下，看来源在哪里；
  - 结案了，zephyr 没有 mmu，没有虚实地址转换，dts 也不是通过 of 类接口解析，而是在预处理的时候转换成头文件，devicetree.h。而内核运行的物理地址是在 linker.ld 链接脚本中指定的。zephyr 内核可以理解为一个对象，每个子系统都有自己的实例，通过不同的 linker.ld 声明。

    ```c
    MEMORY
    {
        ATCM    (wx) : ORIGIN = CONFIG_A_TCM_BASE_ADDR, LENGTH = (CONFIG_A_TCM_SIZE * 1K)
        BTCM    (wx) : ORIGIN = CONFIG_B_TCM_BASE_ADDR, LENGTH = (CONFIG_B_TCM_SIZE * 1K)
        CTCM    (wx) : ORIGIN = CONFIG_C_TCM_BASE_ADDR, LENGTH = (CONFIG_C_TCM_SIZE * 1K)
        OCM     (wx) : ORIGIN = CONFIG_OCM_BASE_ADDR,   LENGTH = (CONFIG_OCM_SIZE * 1K)
    
        /* Used by and documented in include/linker/intlist.ld */
        IDT_LIST (wx) : ORIGIN = 0xFFFFF000, LENGTH = 4K
    }
    ```
  
- 每个 heap 的大小都是固定的，如果内存不够怎么办？
- 用户态申请内存的流程是怎样的？

#### slab

Slab 分配器是一个 Zephyr 内核对象，通过 Slab 分配器可以从指定的内存区域动态分配内存块，Slab 管理的内存块大小相同且固定，可以避免产生内存碎片，并能高效快速的分配和释放内存。Slab 作为 Zephyr 内核对象，允许在分配不到内存块时进行等待，等到超时退出或者其它 Slab 用户释放内存为止。Slab 常用于对内存需求是固定大小的情况。在一个系统中可以定义多个 Slab，每个 Slab 能分配出来的内存块大小不一样，以满足不同功能模块的需求。

```c
#define K_MEM_SLAB_DEFINE(name, slab_block_size, slab_num_blocks, slab_align) \
	char __noinit_named(k_mem_slab_buf_##name) \
	   __aligned(WB_UP(slab_align)) \
	   _k_mem_slab_buf_##name[(slab_num_blocks) * WB_UP(slab_block_size)]; \
	STRUCT_SECTION_ITERABLE(k_mem_slab, name) = \
		Z_MEM_SLAB_INITIALIZER(name, _k_mem_slab_buf_##name, \
					WB_UP(slab_block_size), slab_num_blocks)
```

slab 的初始化和使用流程和 kheap 是类似的。

### 进程调度

Zephyr 将线程分为协作线程和抢占线程两类。协作线程的优先级大于抢占线程的优先级，官方给出了优先级的示意图：

![zephyr_process_priority](D:\gitlab\UtopianFuture.github.io\image\zephyr_process_priority.svg)

#### 协作线程

**优先级为负**的是协作线程，协作线程的优先级从 -1 开始，最大为 -CONFIG_NUM_COOP_PRIORITIES 配置项决定。**协作线程一旦拿到 CPU 后会一直占用，其他线程都无法抢占 CPU**，只有协作线程等待资源时才会让出 CPU 给其它线程，为了避免协作线程长时间占用 CPU 导致更高或相同优先级的线程长时间无法执行，在编写协作线程代码时应**使用 k_sleep 或 k_yield 主动让出 CPU**。

因此在设计协作线程时，需要关注该线程是否 CPU 密集型的任务长时间占用 CPU，如果存在这种情况，应该根据整体的系统情况适时的使用 k_sleep 放出 CPU，让同等或更高优先级的协作线程能够工作。

#### 抢占线程

**优先级非负**的是可抢占式线程，抢占式线程的优先级从 0 开始，最大为 CONFIG_NUM_PREEMPT_PRIORITIES。当高优先级任务（包括协作线程和更高优先级的抢占线程）就绪时，**会直接抢夺低优先级任务的 CPU**，为了避免抢占式线程长时间占用 CPU 导致低优先级的线程无法执行，内核为抢占式线程配置了时间片，让低优先级线程有机会执行。在编写抢占式线程代码时可使用 k_sleep 或 k_yield 主动让出 CPU 让低优先级任务有机会执行。

- k_yield 只是引发一次调度，让其它同优先级和高优先级的协作线程有机会被调度，而**不会调度低优先级的线程**；
- k_sleep 是让线程进入睡眠，让出指定时间的 CPU，这样低优先级的协作线程和抢夺式线程就都机会被调度。

不只是协作线程有让出 CPU 的问题，在抢占式线程中，如果抢占式线程一直处于就绪态，那么低优先级的抢占式线程将无法被调度，还是需要高优先级线程通过 k_sleep 让出 CPU，让低优先级线程有机会执行；

#### z_kernel

zephyr 还有一个表示所有 cpu 和 thread 的结构？

```c
struct z_kernel {
	struct _cpu cpus[CONFIG_MP_NUM_CPUS]; // 对应 cpu 的 current thread, idle thread 等信息

#ifdef CONFIG_PM
	int32_t idle; /* Number of ticks for kernel idling */
#endif

	/*
	 * ready queue: can be big, keep after small fields, since some
	 * assembly (e.g. ARC) are limited in the encoding of the offset
	 */
	struct _ready_q ready_q;

	...

#if defined(CONFIG_THREAD_MONITOR)
	struct k_thread *threads; /* singly linked list of ALL threads */
#endif

	...

};
```

#### k_thread

zephyr 中使用 `struct k_thread` 表示资源的最小使用单元，

```c
struct k_thread {

	struct _thread_base base; // 包含进程状态

	/** defined by the architecture, but all archs need these */
	struct _callee_saved callee_saved;

	/** static thread init data */
	void *init_data;

	/** threads waiting in k_thread_join() */
	_wait_q_t join_queue;

#if defined(CONFIG_POLL)
	struct z_poller poller;
#endif

#if defined(CONFIG_THREAD_MONITOR)
	/** thread entry and parameters description */
	struct __thread_entry entry;

	/** next item in list of all threads */
	struct k_thread *next_thread;
#endif

#if defined(CONFIG_THREAD_NAME)
	/** Thread name */
	char name[CONFIG_THREAD_MAX_NAME_LEN];
#endif

	...

#if defined(CONFIG_THREAD_STACK_INFO)
	/** Stack Info */
	struct _thread_stack_info stack_info;
#endif /* CONFIG_THREAD_STACK_INFO */

	...
        
	/** resource pool */
	struct k_heap *resource_pool;

#ifdef CONFIG_DEMAND_PAGING_THREAD_STATS
	/** Paging statistics */
	struct k_mem_paging_stats_t paging_stats;
#endif

	...

	/** arch-specifics: must always be at the end */
	struct _thread_arch arch;
};
```

#### _thread_base

```c
struct _thread_base {

	/* this thread's entry in a ready/wait queue */
	union {
		sys_dnode_t qnode_dlist;
		struct rbnode qnode_rb;
	};

	/* wait queue on which the thread is pended (needed only for
	 * trees, not dumb lists)
	 */
    // 记录该进程在哪个队列等待，后续方便移除
    // linux 中在 sched_entity 中有个 my_q 变量，同样的功能
	_wait_q_t *pended_on;

	/* user facing 'thread options'; values defined in include/kernel.h */
	uint8_t user_options;

	/* thread state */
	uint8_t thread_state;

	/*
	 * scheduler lock count and thread priority
	 *
	 * These two fields control the preemptibility of a thread.
	 *
	 * When the scheduler is locked, sched_locked is decremented, which
	 * means that the scheduler is locked for values from 0xff to 0x01. A
	 * thread is coop if its prio is negative, thus 0x80 to 0xff when
	 * looked at the value as unsigned.
	 *
	 * By putting them end-to-end, this means that a thread is
	 * non-preemptible if the bundled value is greater than or equal to
	 * 0x0080.
	 */
	union {
		struct {
			int8_t prio;
			uint8_t sched_locked; // 为什么会有调度锁的存在？
		};
		uint16_t preempt;
	};

    // 看来还支持多种调度类
#ifdef CONFIG_SCHED_DEADLINE
	int prio_deadline;
#endif

	uint32_t order_key;

#ifdef CONFIG_SCHED_CPU_MASK
	/* "May run on" bits for each CPU */
	uint8_t cpu_mask;
#endif

	/* data returned by APIs */
	void *swap_data;

#ifdef CONFIG_SYS_CLOCK_EXISTS
	/* this thread's entry in a timeout queue */
	struct _timeout timeout;
#endif

	...
};
```

zephyr 设计了如下进程状态，

```c
/* Not a real thread */
#define _THREAD_DUMMY (BIT(0))

/* Thread is waiting on an object */
#define _THREAD_PENDING (BIT(1))

/* Thread has not yet started */
// bg_main_thread 创建时就是这个状态
#define _THREAD_PRESTART (BIT(2))

/* Thread has terminated */
#define _THREAD_DEAD (BIT(3))

/* Thread is suspended */
#define _THREAD_SUSPENDED (BIT(4))

/* Thread is being aborted */
#define _THREAD_ABORTING (BIT(5))

/* Thread is present in the ready queue */
#define _THREAD_QUEUED (BIT(7))
```

发生调度的时间点有两个：

- 进程主动让出 CPU，PendSV；
  - 所有内核组件启动，等待，释放时会发生上下文切换，z_reschedule 函数完成切换；
  - 进程 sleep, yield, suspend, abort 时会发生上下文切换，z_swap 函数完成切换；
- IRQ 退出时，发起调度；
  - 中断回调函数返回时调用 _IntExit，会检查是否需要调度；

#### 就绪队列

zephyr 有就绪队列和等待队列，每种队列都有对应的进/出队列函数。

```c
| queue_thread
|	-> _priq_run_add
|		-> z_priq_dumb_add
|			-> z_sched_prio_cmp
|			-> sys_dlist_insert // 插入双链表的操作

| dequeue_thread
|   -> _priq_run_remove
|		-> z_priq_dumb_remove
|			-> sys_dlist_remove // 从双链表删除节点的操作
```

```c
static ALWAYS_INLINE void queue_thread(void *pq,
				       struct k_thread *thread)
{
	thread->base.thread_state |= _THREAD_QUEUED; // 上面有介绍，表示加入到就绪队列
	if (should_queue_thread(thread)) {
		_priq_run_add(pq, thread);
	}
}

ALWAYS_INLINE void z_priq_dumb_add(sys_dlist_t *pq, struct k_thread *thread)
{
	struct k_thread *t;

    // 每个 CPU 在初始化的时候都会创建一个 idle 进程
    // 它不能加入到就绪队列
	__ASSERT_NO_MSG(!z_is_idle_thread_object(thread));

	SYS_DLIST_FOR_EACH_CONTAINER(pq, t, base.qnode_dlist) {
		if (z_sched_prio_cmp(thread, t) > 0) { // 很简单的比较 k_thread->base.prio 操作
			sys_dlist_insert(&t->base.qnode_dlist,
					 &thread->base.qnode_dlist);
			return;
		}
	}

	sys_dlist_append(pq, &thread->base.qnode_dlist);
}
```

#### 等待队列

```c
| pend // 将进程加入到等待队列
|	-> add_to_waitq_locked
|		-> unready_thread
|			-> dequeue_thread // 如果在就绪队列中，需要删除
|			-> update_cache // 然后选择新的进程运行
|		-> z_mark_thread_as_pending // 标记为 pending 进程
|		-> z_priq_wait_add
|			-> z_priq_dumb_add 
|				-> z_sched_prio_cmp
|	-> z_add_thread_timeout


| unpend_thread_no_timeout // 从等待队列删除后，还需要把 _THREAD_PENDING 位取消
|	-> _priq_wait_remove
```

```c
// 前面说过，系统中有多个等待队列
static void pend(struct k_thread *thread, _wait_q_t *wait_q,
		 k_timeout_t timeout)
{
	LOCKED(&sched_spinlock) { // sched_spinlock 有什么用
		add_to_waitq_locked(thread, wait_q);
	}

	add_thread_timeout(thread, timeout);
}

static void add_to_waitq_locked(struct k_thread *thread, _wait_q_t *wait_q)
{
	unready_thread(thread);
	z_mark_thread_as_pending(thread);

	SYS_PORT_TRACING_FUNC(k_thread, sched_pend, thread);
	SYS_PROT_OBJ_EVENT_RECORD(EVENT_INSTANT, _TRACE_THREAD_SCHED_PEND, thread, 0, 0, 0)

	if (wait_q != NULL) {
		thread->base.pended_on = wait_q; // 后续可以从 pended_on 队列中删除
		z_priq_wait_add(&wait_q->waitq, thread); // 和加入就绪队列同一个函数
	}
}
```

扩展一下， 这里的调度原理和 linux 中类似。linux 是采用红黑树来管理所有的**调度实体 `sched_entity`**。调度实体通过 `enqueue_entity` 和 `dequeue_entity` 来进行红黑树的出队入队。这里没有调度实体的概念，就用 `k_thread`。

#### 关键函数 z_setup_new_thread

该函数是用来创建进程的。

#### 关键函数 update_cache

很多实现都是新的进程加入/移除就绪队列，然后调用 `update_cache` 选择新的最合适的进程保存在 `_kernel.ready_q.cache` 占用 CPU。

```c
| update_cache
|	-> next_up // 如果没有找到最合适的，就返回 idle_thread
|		-> _priq_run_best // 在 ready_q.runq 中寻找
|			-> _priq_dumb_mask_best
```

```c
static void update_cache(int preempt_ok) // 是否允许抢占
{
	struct k_thread *thread = next_up();

	if (should_preempt(thread, preempt_ok)) {
#ifdef CONFIG_TIMESLICING
		if (thread != _current) {
			z_reset_time_slice();
		}
#endif
		update_metairq_preempt(thread);
		_kernel.ready_q.cache = thread; // 这样配置上了，那什么时候开始执行呢
	} else {
		_kernel.ready_q.cache = _current; // 不允许抢占的话，当前进程继续执行
	}
}
```

#### 关键函数 z_reschedule

从代码上来看，很多 `update_cache` 之后就会调用 `z_reschedule` 进程调度。

```c
| z_reschedule
|	-> resched
|	-> need_swap
|	-> z_swap
|		-> z_swap_irqlock
|			-> arch_swap // 出发 PendSV 中断，在中断回调函数中完成进程切换
```

```c
// 为什么入参是 spinlock 和 spinlock_key 呢
void z_reschedule(struct k_spinlock *lock, k_spinlock_key_t key)
{
	if (resched(key.key) && need_swap()) {
		z_swap(lock, key);
	} else {
		k_spin_unlock(lock, key); // 为啥进程切换会跟锁扯上关系？
	}
}

static inline bool resched(uint32_t key)
{
    // 没有处于中断上下文才能 schedule
    // 因为 rtos 要保证中断的及时响应
	return arch_irq_unlocked(key) && !arch_is_in_isr();
}

int arch_swap(unsigned int key)
{
	/* store off key and return value */
	_current->arch.basepri = key;
	_current->arch.swap_return_value = _k_neg_eagain;

#if defined(CONFIG_CPU_CORTEX_M)
	/* set pending bit to make sure we will take a PendSV exception */
	SCB->ICSR |= SCB_ICSR_PENDSVSET_Msk; // 回调函数为 swap_helper.S: arch_swap

	/* clear mask or enable all irqs to take a pendsv */
	irq_unlock(0);
#elif defined(CONFIG_CPU_AARCH32_CORTEX_R)
	z_arm_cortex_r_svc();
	irq_unlock(key);
#endif

	/* Context switch is performed here. Returning implies the
	 * thread has been context-switched-in again.
	 */
	return _current->arch.swap_return_value;
}
```

执行上文切换，就是让 `update_cache` 选出保存在 `_kernel.ready_q.cache` 占用 CPU。上下文切换和体系架构相关，在 coretex-m 系列可以由 PendSV 主动触发，也可以在其它中断服务返回的时候进行。

#### 关键函数 z_arm_pendsv

汇编，比较复杂，暂不深入分析。

### 中断管理



### 功耗管理

### 同步与互斥

### 驱动模型

### 文件系统

### 问题

- Zephyr 是啥？用在什么地方；
- 怎样编译的；
- 架构是怎样的；
- 其和 linux kernel 有何不同；
- 我能学到什么；

### Reference
