## BMBT

### Background

跟着 Martins3 做 BMBT 已经 2 个月了，但总感觉对 BMBT 的整体把控不是很好，所以写一个关于 BMBT 的详细文档，一方面加强自己的理解，一方面便于后续维护者快速入手。BMBT 是将 QEMU 的复杂实现简化了，如 QOM，memory virtualization 等，所以要理解 BMBT ，对 QEMU 的理解也是少不了的，李强的书将 QEMU 分成 5 部分：QOM, CPU virtualization, memory virtualization, interrupt virtualization, device virtualization，我只看了 QOM, CPU virtualization, interrupt virtualization 部分，其余两部分太复杂，之后有需要再看。

虚拟化也就是这几部分，在 BMBT 中，

QOM 被简化了。

CPU virtualization 使用的是 LATX，即 32 位系统态二进制翻译器，是我们实验室的[牛博士](https://github.com/NiuGenen)维护的。

memory virtualization 的实现主要是 `memory.c` 文件，其中的关键就是 `memory_map_init` 中的 3 个回调函数，负责将 guestOS 会访问到的地址直接映射到物理地址。

interrupt virtualization 几乎和 QEMU 一样。

device virtualization 只实现了几个 guestOS 必须的几个设备，之后会以 serial 为例进行分析。

### 流程分析

#### 初始化

先来看看 BMBT 的初始化过程，和 QEMU 相比简单很多。

```plain
qemu_init
| -- MachineState *current_machine;
| -- MachineClass *machine_class;
| -- call_constructor(); // LATX 初始化相关，不用看
| -- init_xtm_options(); // 解析传入 LATX 参数
| -- qemu_init_cpu_loop(); // 初始化 qemu_global_mutex 互斥锁
| -- setup_timer_interrupt(); // 初始化时钟 ##
| -- qemu_mutex_lock_iothread(); // 关中断
| -- qemu_init_cpu_list(); // 初始化 qemu_cpu_list_lock 互斥锁
| -- memory_map_init(ram_size); // 设置 address space 的回调函数，内存管理主要就是这个，
								// 这里需要把 pc.ram low, smram, pam, system bios,
								// pc.ram, pc.bios 搞懂
								// 这部分还是很有趣的，对理解系统启动过程帮助很大
								// 内存模块进行分析
| -- init_real_host_page_size(); // 通过 c 库函数 getpagesize 得到 host 的 pagesize
| -- init_cache_info(); // 设置 icache, dcache 大小，看看 QEMU 怎么实现的
| -- qemu_set_log(0); // 写入 log
| -- PCMachineState *pcms = machine_init();
|	-- QOM_machine_init(); // 初始化各种和 CPU 有关的参数
| -- tcg_init(); // tcg 初始化，过于复杂，暂时不分析
| -- current_machine = MACHINE(&__pcms);
| -- machine_run_board_init(current_machine);
| -- QOM_init_debugcon(); // debugcon 用于调试
| -- qemu_register_reset(qbus_reset_all_fn, NULL);
| -- qemu_run_machine_init_done_notifiers();
| -- machine_class = MACHINE_GET_CLASS(current_machine);
| -- qemu_system_reset(machine_class, current_machine);
| -- vm_start(); // 暂时不清楚
| -- qemu_mutex_unlock_iothread();
\
```

#### 执行流程

```plain
#0  qemu_tcg_rr_cpu_thread_fn (arg=0x120425530 <__x86_cpu>) at src/qemu/cpus.c:234
#1  0x0000000120009580 in qemu_boot () at /home/niugen/lgs/bmbt/include/sysemu/sysemu.h:45
#2  0x000000012000afa0 in test_qemu_init () at src/main.c:159
#3  0x000000012000b470 in wip () at src/main.c:173
#4  0x000000012000ca68 in greatest_run_suite (suite_cb=0x12000b3e8 <wip>, suite_name=0x1201abea0 "wip") at src/main.c:176
#5  0x000000012000e3d4 in main (argc=1, argv=0xffffff5f48) at src/main.c:185
```

`greatest_run_suite`, `wip`, `test_qemu_init` 3 个函数是单元测试的，可以跳过。下面详细分析 `qemu_tcg_rr_cpu_thread_fn`，去掉冗余代码，便于阅读。

```c
void *qemu_tcg_rr_cpu_thread_fn(void *arg) {
  CPUState *cpu = arg;

  ...

  /* wait for initial kick-off after machine start */
  while (first_cpu->stopped) {
    /* process any pending work */
    CPU_FOREACH(cpu) {
      current_cpu = cpu;
      qemu_wait_io_event_common(cpu);
    }
  }

  start_tcg_kick_timer();

  cpu = first_cpu;

  /* process any pending work */
  cpu->exit_request = 1;

  while (1) {
    if (!cpu) {
      cpu = first_cpu;
    }

    while (cpu && !cpu->queued_work_first && !cpu->exit_request) {
      if (cpu_can_run(cpu)) {
        int r;

        qemu_mutex_unlock_iothread();
        prepare_icount_for_run(cpu);

        r = tcg_cpu_exec(cpu);

        process_icount_data(cpu);
        qemu_mutex_lock_iothread();

        if (r == EXCP_DEBUG) {
          cpu_handle_guest_debug(cpu);
          break;
        } else if (r == EXCP_ATOMIC) {
          qemu_mutex_unlock_iothread();
          cpu_exec_step_atomic(cpu);
          qemu_mutex_lock_iothread();
          break;
        }
      }

      cpu = CPU_NEXT(cpu);
    } /* while (cpu && !cpu->exit_request).. */

    if (cpu && cpu->exit_request) {
      atomic_mb_set(&cpu->exit_request, 0);
    }

    qemu_tcg_rr_wait_io_event();
    deal_with_unplugged_cpus();
  }

  rcu_unregister_thread();
  return NULL;
}
```

这里主要就是 while(1) 循环。`tcg_cpu_exec` 进入 LATX 执行。目前只有一个 CPU 能够执行。

```c
static int tcg_cpu_exec(CPUState *cpu) {
  int ret;
  assert(tcg_enabled());
  cpu_exec_start(cpu);
  ret = cpu_exec(cpu);
  cpu_exec_end(cpu);
  return ret;
}
```

```c
int cpu_exec(CPUState *cpu) {
  CPUClass *cc = CPU_GET_CLASS(cpu);

 ...

  /* if an exception is pending, we execute it here */
  while (!cpu_handle_exception(cpu, &ret)) {
    TranslationBlock *last_tb = NULL;
    int tb_exit = 0;

    while (!cpu_handle_interrupt(cpu, &last_tb)) {
      u32 cflags = cpu->cflags_next_tb;
      TranslationBlock *tb;

      /* When requested, use an exact setting for cflags for the next
         execution.  This is used for icount, precise smc, and stop-
         after-access watchpoints.  Since this request should never
         have CF_INVALID set, -1 is a convenient invalid value that
         does not require tcg headers for cpu_common_reset.  */
      if (cflags == -1) {
        cflags = curr_cflags();
      } else {
        cpu->cflags_next_tb = -1;
      }

      tb = tb_find(cpu, last_tb, tb_exit, cflags);

      trace_tb_execution(cpu, tb);

      cpu_loop_exec_tb(cpu, tb, &last_tb, &tb_exit);

      /* Try to align the host and virtual clocks
         if the guest is in advance */
      align_clocks(&sc, cpu);
    }
  }

  cc->cpu_exec_exit(cpu);
  rcu_read_unlock();

  return ret;
}
```

```c
void x86_cpu_exec_exit(CPUState *cs) {
  X86CPU *cpu = X86_CPU(cs);
  CPUX86State *env = &cpu->env;

  env->eflags = cpu_compute_eflags(env);
}
```

如果  LATX 需要访存，会调用 `misc_helper.c:helper_outb`  ，这就到了 memory virtualization 的地方。

```plain
#0  debugcon_ioport_write (opaque=0x1203cafb8 <__isa+8>, addr=0, val=83, width=1) at src/hw/char/debugcon.c:34
#1  0x000000012009c948 in memory_region_write_accessor (mr=0x1203cafb8 <__isa+8>, addr=0, value=0xffffff5a38, size=1, shift=0, mask=255, attrs=...) at src/qemu/memory.c:353
#2  0x000000012009cc18 in access_with_adjusted_size (addr=0, value=0xffffff5a38, size=1, access_size_min=1, access_size_max=4, access_fn=0x12009c8a4 <memory_region_write_accessor>,
    mr=0x1203cafb8 <__isa+8>, attrs=...) at src/qemu/memory.c:394
#3  0x000000012009d490 in memory_region_dispatch_write (mr=0x1203cafb8 <__isa+8>, addr=0, data=83, op=MO_8, attrs=...) at src/qemu/memory.c:507
#4  0x0000000120016c44 in address_space_stb (as=0x120452348 <address_space_io>, addr=1026, val=83, attrs=..., result=0x0) at src/tcg/memory_ldst.c:304
#5  0x00000001200b07fc in helper_outb (env=0x12042dcf0 <__x86_cpu+34752>, port=1026, data=83) at src/i386/misc_helper.c:21
#6  0x000000007000596c in ?? ()
```

debugcon 是用来调试的，通过下面的指令将输出重定向到 seabios.log 中

```c
static void debugcon_isa_realizefn(ISADebugconState *isa) {
  isa->state.log = get_logfile("seabios.log");

  DebugconState *s = &isa->state;
  memory_region_init_io(&s->io, &debugcon_ops, s, TYPE_ISA_DEBUGCON_DEVICE, 1);
  io_add_memory_region(isa->iobase, &s->io);
}
```

执行到 `debugcon_ioport_write` 说明已经能在 LA 上执行 X86 指令。执行完这个函数后，查看 seabios.log ，会发现 ‘S’ 已经写入了。 写入之后进行中断处理，至于为什么需要中断处理还没有搞清楚。下面是中断处理的流程：

```plain
#0  gsi_handler (opaque=0x120481570, n=0, level=1) at src/hw/i386/pc.c:428
#1  0x0000000120050f2c in qemu_set_irq (irq=0x120481790, level=1) at src/hw/core/irq.c:16
#2  0x000000012005ba60 in pit_irq_timer_update (s=0x1203cc148 <__pit+88>, current_time=17878218075) at src/hw/timer/i8254.c:253
#3  0x000000012005bb48 in pit_irq_timer (opaque=0x1203cc148 <__pit+88>) at src/hw/timer/i8254.c:268
#4  0x00000001200a7e4c in timerlist_run_timers (timer_list=0x120464720) at src/util/qemu-timer.c:322
#5  0x00000001200a81b8 in qemu_clock_run_timers (type=QEMU_CLOCK_VIRTUAL) at src/util/qemu-timer.c:369
#6  0x00000001200a8264 in qemu_clock_run_all_timers () at src/util/qemu-timer.c:378
#7  0x00000001200a8660 in timer_interrupt_handler (sig=127, si=0xffffff42e8, uc=0xffffff4370) at src/util/qemu-timer.c:441
#8  <signal handler called>
#9  0x000000fff7c94d8c in sigprocmask () from /lib/loongarch64-linux-gnu/libc.so.6
#10 0x00000001200a28c4 in unblock_interrupt () at src/util/signal-timer.c:91
#11 0x000000012009677c in qemu_mutex_unlock_iothread () at src/qemu/cpus.c:63
#12 0x0000000120016d48 in address_space_stb (as=0x120452348 <address_space_io>, addr=1026, val=83, attrs=..., result=0x0) at src/tcg/memory_ldst.c:316
#13 0x00000001200b07fc in helper_outb (env=0x12042dcf0 <__x86_cpu+34752>, port=1026, data=83) at src/i386/misc_helper.c:21
#14 0x000000007000596c in ?? ()
```

通过 `address_space_stb` 的实参可以确定就是在写入 ‘S’ 之后进行的。但是根据这个执行流程来看，`sigprocmask` 是 C 库函数，而之后处理的是中间中断，因此产生一个问题，BMBT 包括 QEMU 是如何处理信号的。

```c
void address_space_stb(AddressSpace *as, hwaddr addr, uint32_t val,
                       MemTxAttrs attrs, MemTxResult *result) {
  uint8_t *ptr;
  MemoryRegion *mr;
  hwaddr l = 1;
  hwaddr addr1;
  MemTxResult r;
  bool release_lock = false;

  rcu_read_lock();
  mr = address_space_translate(as, addr, &addr1, &l, true, attrs);
  if (!memory_access_is_direct(mr, true)) {
    release_lock |= prepare_mmio_access(mr);
    r = memory_region_dispatch_write(mr, addr1, val, MO_8, attrs);
  } else {

    ptr = qemu_map_ram_ptr(mr->ram_block, addr1);
    stb_p(ptr, val);
    invalidate_and_set_dirty(mr, addr1, 1);
    r = MEMTX_OK;
  }
  if (result) {
    *result = r;
  }
  if (release_lock) {
    qemu_mutex_unlock_iothread();
  }
  rcu_read_unlock();
}
```

这就是整个 BMBT 的执行流程，这样看比较简单清晰。这只是 debugcon 一种设备，而实际上 guestOS 需要多种设备，我们通过 Seabios 的初始化来逐步实现每一种必须的设备。关于 Seabios 的分析可以看我这篇文章，里面分析了 Seabios 的 boot 流程，而关于所有的设备 probe ，是在 `maininit` 这个函数中实现的。

这就是 BMBT 的大致开发流程。

### 子模块分析

#### 时钟中断

QEMU 的整个执行流程是 vl.c:main 函数，大致可以分为各种设备的初始化和初始化之后的执行，而执行分为几个线程： vcpu 线程，main loop 线程，I/Othread 线程。vcpu 线程就是执行 guestos 代码的，而 main loop 线程则是基于 glib 的事件监听机制，能够监听各种设备的事件，包括时间中断。这个在我的[这篇文章](https://github.com/UtopianFuture/UtopianFuture.github.io/blob/master/virtualization/QEMU-event-loop.md)中详细介绍了。但是 BMBT 没有用多线程，所以不能用 main loop 的方式实现时钟中断，BMBT 采用信号的方式来处理。也就是说包括创建 timer ，设置 timeout 在内的函数都和 QEMU 一样，但是响应时间中断的方式不一样。下面看看 BMBT 是怎样响应的。

```c
// signal-timer.c
typedef void(TimerHandler)(int sig, siginfo_t *si, void *uc);
timer_t setup_timer(TimerHandler handler);
```

在 `main` 中就通过 `setup_timer` 设置时间中断的回调函数 `timer_interrupt_handler`，

```c
void setup_timer_interrupt() {
  init_clocks();
  setup_timer(timer_interrupt_handler);
}
```

```c
static timer_t interrpt_tid;
timer_t setup_timer(TimerHandler handler) {
  struct sigaction sa;
  struct sigevent sev;

  sa.sa_flags = SA_SIGINFO;
  sa.sa_sigaction = handler;
  sigemptyset(&sa.sa_mask);
  if (sigaction(TIMER_SIG, &sa, NULL) == -1) {
    error_report("set sigaction failed\n");
  }
  sev.sigev_value.sival_ptr = &interrpt_tid;
  sev.sigev_notify = SIGEV_SIGNAL; /* Notify via signal */
  sev.sigev_signo = TIMER_SIG;     /* Notify using this signal */

  if (timer_create(CLOCK_REALTIME, &sev, &interrpt_tid) == -1) {
    error_report("timer_create failed\n");
  }
  return interrpt_tid;
}
```

和 `main_loop_wait` 一样，调用 `qemu_clock_run_all_timers` ，这个函数会调用对应设备的时间中断的回调函数。

```c
static void timer_interrupt_handler(int sig, siginfo_t *si, void *uc) {
  duck_check(!is_interrupt_blocked());
  qemu_log("timer interrupt comming");
  enter_interrpt_context();
  int64_t timeout_ns = -1;

  qemu_clock_run_all_timers();

  timeout_ns = qemu_soonest_timeout(timeout_ns,
                                    timerlistgroup_deadline_ns(&main_loop_tlg));
  if (timeout_ns == -1) {
    warn_report("no timer to fire");
  }
  soonest_interrupt_ns(timeout_ns);
  leave_interrpt_context();
}
```

下面看看调用过程。

```plain
#0  rtc_update_timer (opaque=0x12040c730 <__mc146818_rtc>) at src/hw/rtc/mc146818rtc.c:262
#1  0x00000001200c0568 in timerlist_run_timers (timer_list=0x1204a46e0) at src/util/qemu-timer.c:322
#2  0x00000001200c097c in qemu_clock_run_timers (type=QEMU_CLOCK_REALTIME) at src/util/qemu-timer.c:369
#3  0x00000001200c0a4c in qemu_clock_run_all_timers () at src/util/qemu-timer.c:378
#4  0x00000001200c0f04 in timer_interrupt_handler (sig=127, si=0xffffff45e8, uc=0xffffff4670) at src/util/qemu-timer.c:441
#5  <signal handler called>
#6  0x000000fff7ca0d8c in sigprocmask () from /lib/loongarch64-linux-gnu/libc.so.6
#7  0x00000001200ba1d0 in unblock_interrupt () at src/util/signal-timer.c:91
#8  0x00000001200ac504 in qemu_mutex_unlock_iothread () at src/qemu/cpus.c:63
#9  0x00000001200af64c in qemu_init () at src/qemu/vl.c:331
#10 0x000000012000b104 in test_qemu_init () at src/main.c:158
#11 0x000000012000b71c in wip () at src/main.c:173
#12 0x000000012000d06c in greatest_run_suite (suite_cb=0x12000b670 <wip>, suite_name=0x1201ea640 "wip") at src/main.c:176
#13 0x000000012000edc8 in main (argc=1, argv=0xffffff5f48) at src/main.c:185
```

至于 signal_timer.c 文件中的函数都是为了使用 signal 机制写的，这里暂时不分析，因为 edk2 的信号实现和 glibc 实现有些差异，下一步我们需要搞懂 edk2 中的 StdLib 是怎样实现的，然后修改对 API 的调用。

#### 内存模拟

内存初始化看代码不难，但我似乎没有仔细想过为什么要这样初始化，以及每块内存区域的含义，正好在将 BMBT 移植到 edk2 的过程中遇到了 sys/mmap.h 头没有在 edk2 中没有实现的问题，晚上花了很久在向怎样解决这个问题，主要的思路是看看 edk2 中已经实现的代码中是怎样使用 mmap 的，但这条路似乎不太行，所以换个思路，理解为什么要这样实现，有没有替换的方法。下面分析一下 BMBT 的内存虚拟化。代码主要在 memory.c 中。

先看看几个重要的数据结构，之后会理清这些数据结构之间的关系：

```c
typedef struct RAMList {
  DirtyMemoryBlocks *dirty_memory[DIRTY_MEMORY_NUM];

  /* RCU-enabled, writes protected by the ramlist lock. */
  struct {
    RAMBlock block;
    MemoryRegion mr;
  } blocks[RAM_BLOCK_NUM];
  RAMBlock *mru_block;
} RAMList;
```

```C
typedef struct RAMBlock {
  uint8_t *host;
  ram_addr_t offset;     // offset in ram list
  ram_addr_t max_length; // same with MemoryRegion::size
  MemoryRegion *mr;
} RAMBlock;
```

```c
typedef struct MemoryRegion {
  // BMBT: In QEMU ram_block != NULL doesn't mean ram == true
  // see memory_region_init_rom_device_nomigrate, but BMBT is simplified
  bool ram;
  bool readonly;
  struct RAMBlock *ram_block;

  const struct MemoryRegionOps *ops;
  // FIXME add more checks with opage ?
  void *opaque;

  hwaddr offset;
  uint64_t size;
  const char *name;
} MemoryRegion;
```

```C
typedef struct {
  MemoryRegion *segments[MAX_SEGMENTS_IN_AS];
  int segment_num;
  char name[100];
} AddressSpaceDispatch;
```

```C
typedef struct AddressSpace {
  AddressSpaceDispatch *dispatch;
  bool smm;
  MemoryRegion *(*mr_look_up)(struct AddressSpace *as, hwaddr offset,
                              hwaddr *xlat, hwaddr *plen);
} AddressSpace;
```

在 `qemu_init` 中会调用 `memory_map_init`

```c
void qemu_init() {
  ...

  ram_size = 128 * MiB;
  ...

  memory_map_init(ram_size);

  init_real_host_page_size();
  init_cache_info();

  ...
}
```

先设置 memory_dispatch, look_up 的回调函数，然后调用 `ram_init`

```C
void memory_map_init(ram_addr_t size) {
  address_space_io.mr_look_up = io_mr_look_up;
  address_space_io.dispatch = &__io_dispatch;

  address_space_memory.mr_look_up = mem_mr_look_up;
  address_space_memory.dispatch = &__memory_dispatch;

  address_space_smm_memory.mr_look_up = mem_mr_look_up;
  address_space_smm_memory.dispatch = &__memory_dispatch;
  address_space_smm_memory.smm = true;

  ram_init(size);
  unassigned_io_setup();
}
```

这个函数是关键。

```C
static void ram_init(ram_addr_t total_ram_size) {
  ram_addr_t rom_size;
  void *host = alloc_ram(total_ram_size);
  ...

  init_ram_block("pc.ram low", PC_LOW_RAM_INDEX, false, 0, SMRAM_C_BASE);
  init_ram_block("smram", SMRAM_INDEX, false, SMRAM_C_BASE, SMRAM_C_SIZE);

  // pam expan and pam exbios
  duck_check(PAM_EXPAN_SIZE == PAM_EXBIOS_SIZE);
  for (int i = 0; i < PAM_EXPAN_NUM + PAM_EXBIOS_NUM; ++i) {
    const char *name = i < PAM_EXPAN_NUM ? "pam expan" : "pam exbios";
    hwaddr offset = SMRAM_C_END + i * PAM_EXPAN_SIZE;
    init_ram_block(name, PAM_INDEX + i, true, offset, PAM_EXPAN_SIZE);
  }

  init_ram_block("system bios", PAM_BIOS_INDEX, true, PAM_BIOS_BASE,
                 PAM_BIOS_SIZE);

  duck_check(SMRAM_C_BASE + SMRAM_C_SIZE +
                 PAM_EXPAN_SIZE * (PAM_EXPAN_NUM + PAM_EXBIOS_NUM) ==
             PAM_BIOS_BASE);
  duck_check(ram_list.blocks[PAM_BIOS_INDEX].mr.offset +
                 ram_list.blocks[PAM_BIOS_INDEX].mr.size ==
             X86_BIOS_MEM_SIZE);

  init_ram_block("pc.ram", PC_RAM_INDEX, false, X86_BIOS_MEM_SIZE,
                 total_ram_size - X86_BIOS_MEM_SIZE);
  init_ram_block("pc.bios", PC_BIOS_INDEX, true, 4 * GiB - PC_BIOS_IMG_SIZE,
                 PC_BIOS_IMG_SIZE);

  ...

  // pc.bios's block::offset is not same with it's mr.offset
  RAMBlock *block = &ram_list.blocks[PC_BIOS_INDEX].block;
  block->offset = total_ram_size;

  for (int i = 0; i < RAM_BLOCK_NUM; ++i) {
    MemoryRegion *mr = &ram_list.blocks[i].mr;
    mem_add_memory_region(mr);
  }

  // isa-bios / pc.bios's host point to file
  rom_size = x86_bios_rom_init();
  setup_dirty_memory(total_ram_size + rom_size);
}
```

这是所有初始化的 block 分配情况：

```plain
name: pc.ram low, index: 0, offset: 0, size: 640
name: smram, index: 1, offset: 640, size: 128
name: pam expan, index: 2, offset: 768, size: 16
name: pam expan, index: 3, offset: 784, size: 16
name: pam expan, index: 4, offset: 800, size: 16
name: pam expan, index: 5, offset: 816, size: 16
name: pam expan, index: 6, offset: 832, size: 16
name: pam expan, index: 7, offset: 848, size: 16
name: pam expan, index: 8, offset: 864, size: 16
name: pam expan, index: 9, offset: 880, size: 16
name: pam exbios, index: 10, offset: 896, size: 16
name: pam exbios, index: 11, offset: 912, size: 16
name: pam exbios, index: 12, offset: 928, size: 16
name: pam exbios, index: 13, offset: 944, size: 16
name: system bios, index: 14, offset: 960, size: 64
name: pc.ram, index: 15, offset: 1024, size: 130048
name: pc.bios, index: 16, offset: 4194048, size: 256
```

需要搞清楚每个 block 的意义。

前面的都很好理解，为什么到 pc.bios 的 offset 就对不上了呢？

在所有的 block 都初始化了之后，会调用 `as_add_memory_regoin` ，将 memory_region 加到 `dispatch->segments` 中，但为什么这样加还没有搞清楚。这样添加之后因为 `AddressSpace` 的回调函数已经初始化为  `mem_mr_look_up` ，所以之后可以直接根据 `offset` 找到该 block。

```c
static void as_add_memory_regoin(AddressSpaceDispatch *dispatch,
                                 MemoryRegion *mr) {
#ifndef RELEASE_VERSION
  duck_check(mr_initialized(mr));
#endif
  tcg_commit();

  ...

  for (int k = dispatch->segment_num; k > i; --k) {
    dispatch->segments[k] = dispatch->segments[k - 1];
  }
  dispatch->segments[i] = mr;

  dispatch->segment_num++;
}
```

这部分初始化之后就是 `x86_bios_rom_init`。没懂，将 bios.bin 读出来放到 `ram_list.blocks[PC_BIOS_INDEX].block` 里？

```c
static ram_addr_t x86_bios_rom_init() {
  int fd = open("./seabios/out/bios.bin", O_RDONLY);
  duck_check(fd != -1);

  lseek(fd, 0, SEEK_SET);
  int rc = read(fd, __pc_bios, PC_BIOS_IMG_SIZE);
  duck_check(rc == PC_BIOS_IMG_SIZE);
  close(fd);

  RAMBlock *block = &ram_list.blocks[PC_BIOS_INDEX].block;
  block->host = (void *)(&__pc_bios[0]);

  // isa-bios is handled in function isa_bios_access
  return PC_BIOS_IMG_SIZE;
}
```

然后是 `setup_dirty_memory` ，为什么要初始化 dirty memory，BMBT 需要这个干啥？

所有的初始化内容就是这些，接下来看看还有哪些地方会用到 memory region。

还是不懂，看看 QEMU 中的 memory virtualization 吧！之后进一步看 kernel 的内存管理。

### 问题

按理说设置一个回调函数不就可以了，为什么还要 `timer_create` ，`interrpt_tid` 有什么用？

同样是在 `timer_interrupt_handler` 中，会调用 `soonest_interrupt_ns` 设置 `timeout_ns` ，但为什么这样设置还需要看看。edk2 中没有 `timer_create` 等函数，要怎么办？

发现一个问题，在 edk2 中编译无法找到的 `siginfo_t` 似乎根本不需要，我看了 `timer_interrupt_handler` ，`siginfo_t *si` 没有用到，所以我直接将它注释掉。

### Reference

[1] https://martins3.github.io/ 强烈建议关注这个家伙，简直是个宝库。

### 备注

“##” 为没有搞懂的部分，之后要慢慢吃下来。

"#ifdef BMBT" 是用来注释 QEMU 部分的代码。

### 一些教训

- 没有什么代码是看不懂的，GDB 多调调就会了。
- 对于某个问题要全面思考再作出提问。
