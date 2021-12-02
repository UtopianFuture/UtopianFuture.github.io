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

### 执行流程

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

这里主要就是 while(1) 循环。`tcg_cpu_exec` 进入 LATX 执行。

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



### Reference

[1] https://martins3.github.io/ 强烈建议关注这个家伙，简直是个宝库。

### 备注

“##” 为没有搞懂的部分，之后要慢慢吃下来。

"#ifdef BMBT" 是用来注释 QEMU 部分的代码。

### 一些教训

- 没有什么代码是看不懂的，GDB 多调调就会了。
- 对于某个问题要全面思考再作出提问。
