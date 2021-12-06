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

### Reference

[1] https://martins3.github.io/ 强烈建议关注这个家伙，简直是个宝库。

### 备注

“##” 为没有搞懂的部分，之后要慢慢吃下来。

"#ifdef BMBT" 是用来注释 QEMU 部分的代码。

### 一些教训

- 没有什么代码是看不懂的，GDB 多调调就会了。
- 对于某个问题要全面思考再作出提问。
