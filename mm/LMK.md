## Lowmemorykiller

LMK 由于功能和 OOM killer 重叠，4.2 版本之后已经被移出内核了，现在用的是 lmkd，即运行在用户态的守护进程。

Android 底层还是基于 Linux，在 Linux 中低内存是会有 oom killer 去杀掉一些进程去释放内存，而 Android 中的 lowmemorykiller 就是在此基础上做了一些调整来的。因为手机上的内存毕竟比较有限，而 Android 中 APP 在不使用之后并不是马上被杀掉，虽然上层 ActivityManagerService 中也有很多关于进程的调度以及杀进程的手段，但是毕竟还需要考虑手机剩余内存的实际情况，

lowmemorykiller 的作用就是当内存比较紧张的时候去及时杀掉一些 ActivityManagerService 还没来得及杀掉但是对用户来说不那么重要的进程，回收一些内存，保证手机的正常运行。 而 OOM 只能够保证系统的正常运行，但是有时候会很卡，这对于手机用户体验很不好。

lmkd 需要内核驱动 Lowmemorykiller 的支持才能运行。

### 整体结构

![lmk](/home/guanshun/gitlab/UFuture.github.io/image/lmk.png)

Framework 层(AMS)通过一定的规则调整进程的 adj 的值和内存空间阀值（动态的），而进程的优先级信息存储在 ProcessList 中，越重要的进程的优先级越低，前台 APP 的优先级为 0，系统 APP 的优先级一般都是负值，所以一般进程管理以及杀进程都是针对与上层的 APP 来说的。然后通过 socket 发送给 lmkd 进程，lmkd 两种处理方式， 一种将阀值写入文件节点发送给内核的 LowMemoryKiller，由内核进行杀进程处理，另一种是 lmkd 通过 cgroup 监控内存使用情况，自行计算杀掉进程。

### 背景知识

lowmemkiller 中会涉及到几个重要的概念：

`/sys/module/lowmemorykiller/parameters/minfree`：里面是以”,”分割的一组数，每个数字代表一个内存级别

`/sys/module/lowmemorykiller/parameters/adj`：对应上面的一组数，每个数组代表一个进程优先级级别

举个例子：

`/sys/module/lowmemorykiller/parameters/minfree`：18432,23040,27648,32256,55296,80640

`/sys/module/lowmemorykiller/parameters/adj`：0,100,200,300,900,906

代表的意思：两组数一一对应，当手机内存低于 80640 时，就去杀掉优先级 906 以及以上级别的进程，当内存低于 55296 时，就去杀掉优先级 900 以及以上的进程。

对每个进程来说：

/proc/pid/oom_adj：代表**当前进程的优先级**，这个优先级是 kernel 中的优先级，这个优先级与上层的优先级之间有一个换算。

/proc/pid/oom_score_adj：上层优先级，跟 ProcessList 中的优先级对应。

### 代码分析

以前看惯了内核代码，拿到这个代码，居然可以从 main 函数开始分析，心情大好:laughing:。

#### main

```cpp
int main(int argc, char **argv) {
    if ((argc > 1) && argv[1] && !strcmp(argv[1], "--reinit")) {
        if (property_set(LMKD_REINIT_PROP, "")) {
            ALOGE("Failed to reset " LMKD_REINIT_PROP " property");
        }
        return issue_reinit();
    }

    if (!update_props()) {
        ALOGE("Failed to initialize props, exiting.");
        return -1;
    }

    ctx = create_android_logger(KILLINFO_LOG_TAG);

    if (!init()) {
        if (!use_inkernel_interface) {
            /*
             * MCL_ONFAULT pins pages as they fault instead of loading
             * everything immediately all at once. (Which would be bad,
             * because as of this writing, we have a lot of mapped pages we
             * never use.) Old kernels will see MCL_ONFAULT and fail with
             * EINVAL; we ignore this failure.
             *
             * N.B. read the man page for mlockall. MCL_CURRENT | MCL_ONFAULT
             * pins ⊆ MCL_CURRENT, converging to just MCL_CURRENT as we fault
             * in pages.
             */
            /* CAP_IPC_LOCK required */
            // 这里为何要锁住内存？
            if (mlockall(MCL_CURRENT | MCL_FUTURE | MCL_ONFAULT) && (errno != EINVAL)) {
                ALOGW("mlockall failed %s", strerror(errno));
            }

            /* CAP_NICE required */
            struct sched_param param = {
                    .sched_priority = 1,
            };

            // pid 为 0，这应该是 parent pid，优先级为 1，实时进程，调度策略为 FIFO
            if (sched_setscheduler(0, SCHED_FIFO, &param)) {
                ALOGW("set SCHED_FIFO failed %s", strerror(errno));
            }
        }

        ...

        mainloop(); // 死循环，epoll_wait 等待 fd 事件
    }

    android_log_destroy(&ctx);
    close_handle_for_perf_iop();
    ALOGI("exiting");
    return 0;
}
```

#### init

创建 epoll 连接，初始化 socket 并连接 AMS。

```cpp
static int init(void) {
    static struct event_handler_info kernel_poll_hinfo = { 0, kernel_event_handler };

    ...

    // 创建 socket 连接，lmkd 监听这个 socket 并等待客户的连接
    ctrl_sock.sock = android_get_control_socket("lmkd");
    if (ctrl_sock.sock < 0) {
        ALOGE("get lmkd control socket failed");
        return -1;
    }

    ret = listen(ctrl_sock.sock, MAX_DATA_CONN); // 监听，最多监听 3 个
    if (ret < 0) {
        ALOGE("lmkd control socket listen failed (errno=%d)", errno);
        return -1;
    }

    epev.events = EPOLLIN;
    ctrl_sock.handler_info.handler = ctrl_connect_handler;
    epev.data.ptr = (void *)&(ctrl_sock.handler_info);
    if (epoll_ctl(epollfd, EPOLL_CTL_ADD, ctrl_sock.sock, &epev) == -1) {
        ALOGE("epoll_ctl for lmkd control socket failed (errno=%d)", errno);
        return -1;
    }
    maxevents++;

    // "/sys/module/lowmemorykiller/parameters/minfree"
    has_inkernel_module = !access(INKERNEL_MINFREE_PATH, W_OK);
    use_inkernel_interface = has_inkernel_module &&
        (property_get_bool("ro.vendor.lmk.force_inkernel_lmk", false) || !enable_userspace_lmk);

    // 使用内核中的 lmk 驱动
    if (use_inkernel_interface) {
        ALOGI("Using in-kernel low memory killer interface");
        if (init_poll_kernel()) { // 主要是创建一个 fd，打开 "/proc/lowmemorykiller"
            epev.events = EPOLLIN;
            epev.data.ptr = (void*)&kernel_poll_hinfo; // 上面创建的 kernel_event_handler，也是不断的 pread
            if (epoll_ctl(epollfd, EPOLL_CTL_ADD, kpoll_fd, &epev) != 0) {
                ALOGE("epoll_ctl for lmk events failed (errno=%d)", errno);
                close(kpoll_fd);
                kpoll_fd = -1;
            } else {
                maxevents++;
                /* let the others know it does support reporting kills */
                property_set("sys.lmk.reportkills", "1");
            }
        }
    } else { // 如果不支持内核不支持 lmk，则调用 init_mp_common 初始化，在 lmkd 中杀进程，之后再分析
        if (!init_monitors()) {
            return -1;
        }
        /* let the others know it does support reporting kills */
        property_set("sys.lmk.reportkills", "1");
    }

    for (i = 0; i <= ADJTOSLOT(OOM_SCORE_ADJ_MAX); i++) {
        procadjslot_list[i].next = &procadjslot_list[i];
        procadjslot_list[i].prev = &procadjslot_list[i];
    }

    memset(killcnt_idx, KILLCNT_INVALID_IDX, sizeof(killcnt_idx));

    /*
     * Read zoneinfo as the biggest file we read to create and size the initial
     * read buffer and avoid memory re-allocations during memory pressure
     */
    if (reread_file(&file_data) == NULL) {
        ALOGE("Failed to read %s: %s", file_data.filename, strerror(errno));
    }

    /* check if kernel supports pidfd_open syscall */
    pidfd = TEMP_FAILURE_RETRY(pidfd_open(getpid(), 0));
    if (pidfd < 0) {
        pidfd_supported = (errno != ENOSYS);
    } else {
        pidfd_supported = true;
        close(pidfd);
    }
    ALOGI("Process polling is %s", pidfd_supported ? "supported" : "not supported" );

    if (!lmkd_init_hook()) {
        ALOGE("Failed to initialize LMKD hooks.");
        return -1;
    }

    return 0;
}
```

#### mainloop

这部分代码结构很清晰，调用 `epoll_wait` 一直监听事件发生，然后调用回调函数。

```c
static void mainloop(void) {
  struct event_handler_info *handler_info;
  struct polling_params poll_params;
  struct timespec curr_tm;
  struct epoll_event *evt;
  long delay = -1;

  poll_params.poll_handler = NULL;
  poll_params.paused_handler = NULL;
  union vmstat poll1, poll2;

  memset(&poll1, 0, sizeof(union vmstat));
  memset(&poll2, 0, sizeof(union vmstat));
  while (1) {
    struct epoll_event events[MAX_EPOLL_EVENTS];
    int nevents;
    int i;
    bool skip_call_handler = false;

    if (poll_params.poll_handler) { // poll_params.poll_handler = NULL 赋值后貌似就没有改变过

        ...

    } else {
      if (kill_timeout_ms && is_waiting_for_kill()) {
        clock_gettime(CLOCK_MONOTONIC_COARSE, &curr_tm);
        delay = kill_timeout_ms - get_time_diff_ms(&last_kill_tm, &curr_tm);
        /* Wait for pidfds notification or kill timeout to expire */
        nevents =
            (delay > 0) ? epoll_wait(epollfd, events, maxevents, delay) : 0;
        if (nevents == 0) {
          /* Kill notification timed out */
          stop_wait_for_proc_kill(false);
          if (polling_paused(&poll_params)) {
            clock_gettime(CLOCK_MONOTONIC_COARSE, &curr_tm);
            poll_params.update = POLLING_RESUME;
            resume_polling(&poll_params, curr_tm);
          }
        }
      } else {
        /* Wait for events with no timeout */
        nevents = epoll_wait(epollfd, events, maxevents, -1);
      }
    }

    ...

    /*
     * First pass to see if any data socket connections were dropped.
     * Dropped connection should be handled before any other events
     * to deallocate data connection and correctly handle cases when
     * connection gets dropped and reestablished in the same epoll cycle.
     * In such cases it's essential to handle connection closures first.
     */
    for (i = 0, evt = &events[0]; i < nevents; ++i, evt++) {
      if ((evt->events & EPOLLHUP) && evt->data.ptr) {
        ALOGI("lmkd data connection dropped");
        handler_info = (struct event_handler_info *)evt->data.ptr;
        watchdog.start();
        ctrl_data_close(handler_info->data);
        watchdog.stop();
      }
    }

    /* Second pass to handle all other events */
    for (i = 0, evt = &events[0]; i < nevents; ++i, evt++) {

      ...

      if (evt->data.ptr) {
        handler_info = (struct event_handler_info *)evt->data.ptr;
        if ((handler_info->handler == mp_event_common ||
             handler_info->handler == mp_event_psi) &&
            (handler_info->data == VMPRESS_LEVEL_MEDIUM ||
             handler_info->data == VMPRESS_LEVEL_CRITICAL)) {
          check_cont_lmkd_events(handler_info->data);
        }
        call_handler(handler_info, &poll_params, evt->events); // 在这里调用 ctrl_connect_handler
      }
    }
  }
}
```

#### lmkd 客户端接口

进程 lmkd 的客户主要是 activity manager，它通过 socket `/dev/socket/lmkd` 跟 lmkd 进行通信。通过前面的代码我们已经知道，有客户连接时，调用的是 `ctrl_connect_handler`。

```cpp
static void ctrl_connect_handler(int data __unused, uint32_t events __unused,
                                 struct polling_params *poll_params __unused) {
  struct epoll_event epev;
  int free_dscock_idx = get_free_dsock();

  if (free_dscock_idx < 0) {
    /*
     * Number of data connections exceeded max supported. This should not
     * happen but if it does we drop all existing connections and accept
     * the new one. This prevents inactive connections from monopolizing
     * data socket and if we drop ActivityManager connection it will
     * immediately reconnect.
     */
    for (int i = 0; i < MAX_DATA_CONN; i++) {
      ctrl_data_close(i);
    }
    free_dscock_idx = 0;
  }

  data_sock[free_dscock_idx].sock = accept(ctrl_sock.sock, NULL, NULL);
  if (data_sock[free_dscock_idx].sock < 0) {
    ALOGE("lmkd control socket accept failed; errno=%d", errno);
    return;
  }

  ALOGI("lmkd data connection established");
  /* use data to store data connection idx */
  data_sock[free_dscock_idx].handler_info.data = free_dscock_idx;
  data_sock[free_dscock_idx].handler_info.handler = ctrl_data_handler;
  data_sock[free_dscock_idx].async_event_mask = 0;
  epev.events = EPOLLIN;
  epev.data.ptr = (void *)&(data_sock[free_dscock_idx].handler_info);
  if (epoll_ctl(epollfd, EPOLL_CTL_ADD, data_sock[free_dscock_idx].sock,
                &epev) == -1) {
    ALOGE("epoll_ctl for data connection socket failed; errno=%d", errno);
    ctrl_data_close(free_dscock_idx);
    return;
  }
  maxevents++;
}
```

不懂 socket 机制，先记录下来。就是在 `ctrl_sock` 上调用 `accept` 接受一个客户的连接，然后放到 `data_sock`（回想一下，`data_sock` 存放的是 lmkd 的客户，最多可以有 3 个）。

对客户连接来说，它的处理函数是 `ctrl_data_handler`，`handler_info.data` 对应 `data_sock` 数组的下标，这样在 `ctrl_data_handler` 执行时才知道是哪个客户端可读了。lmkd 接受如下命令：

```cpp
/*
 * Supported LMKD commands
 */
enum lmk_cmd {
    LMK_TARGET = 0,         /* Associate minfree with oom_adj_score */
    LMK_PROCPRIO,           /* Register a process and set its oom_adj_score */
    LMK_PROCREMOVE,         /* Unregister a process */
    LMK_PROCPURGE,          /* Purge all registered processes */
    LMK_GETKILLCNT,         /* Get number of kills */
    LMK_SUBSCRIBE,          /* Subscribe for asynchronous events */
    LMK_PROCKILL,           /* Unsolicited msg to subscribed clients on proc kills */
    LMK_UPDATE_PROPS,       /* Reinit properties */
    LMK_STAT_KILL_OCCURRED, /* Unsolicited msg to subscribed clients on proc kills for statsd log */
    LMK_STAT_STATE_CHANGED, /* Unsolicited msg to subscribed clients on state changed */
};
```

这里不再关注 lmkd 如何接收、解析命令，先看看是怎样处理不同命令的。

##### LMK_TARGET

> Associate minfree with oom_adj_score

`cmd_target` 设置 `lowmem_minfree`， `lowmem_adj`，这两组参数用于控制 low memory 时候的行为。如果使用的是驱动 lmk，那就把参数写到 `INKERNEL_MINFREE_PATH` 和 `INKERNEL_ADJ_PATH`。

```cpp
static void cmd_target(int ntargets, LMKD_CTRL_PACKET packet) {
  int i;
  struct lmk_target target;
  char minfree_str[PROPERTY_VALUE_MAX];
  char *pstr = minfree_str;
  char *pend = minfree_str + sizeof(minfree_str);
  static struct timespec last_req_tm;
  struct timespec curr_tm;

  ...

  last_req_tm = curr_tm;

  for (i = 0; i < ntargets; i++) {
    lmkd_pack_get_target(packet, i, &target); // 从 socket 传递的参数中解析出 lmk_target
    lowmem_minfree[i] = target.minfree; // 这两个数组有什么用呢？
    lowmem_adj[i] = target.oom_adj_score;

    ...
  }

  lowmem_targets_size = ntargets;

  /* Override the last extra comma */
  pstr[-1] = '\0';
  property_set("sys.lmk.minfree_levels", minfree_str);

  if (has_inkernel_module) {

    ...

    writefilestring(INKERNEL_MINFREE_PATH, minfreestr, true);
    writefilestring(INKERNEL_ADJ_PATH, killpriostr, true);
  }
}
```

##### LMK_PROCPRIO

> Register a process and set its oom_adj_score

`cmd_procprio` 用于设置进程的 oomadj，在把 oomadj 写入 `"/proc/#pid/oom_score_adj"` 后，如果使用的是驱动 lmk，就可以直接返回了（驱动 lmk 会处理剩下的工作）。如果不是，接着往下执行。

如果机子是 `low_ram_device`（小内存机器），那么 lmkd 会根据应用的 oomadj 调整应用可用的内存大小。（震惊，原来还有这种骚操作。那在小内存机器里，应用处于后台时就更容易 OOM 了）。

```cpp
static void cmd_procprio(LMKD_CTRL_PACKET packet, int field_count,
                         struct ucred *cred) {
  struct proc *procp;
  char path[LINE_MAX];
  char val[20];
  int soft_limit_mult;
  struct lmk_procprio params;
  bool is_system_server;
  struct passwd *pwdrec;
  int64_t tgid;
  static char buf[PAGE_SIZE];

  lmkd_pack_get_procprio(packet, field_count, &params); // 解析参数

  ...

  // oom_score_adj 表示上层优先级，跟 ProcessList 中的优先级对应
  // 跟内核优先级不同，oomadj 是数值范围是 [-1000, 1000]
  snprintf(path, sizeof(path), "/proc/%d/oom_score_adj", params.pid);
  snprintf(val, sizeof(val), "%d", params.oomadj);
  if (!writefilestring(path, val, false)) {
    ALOGW("Failed to open %s; errno=%d: process %d might have been killed",
          path, errno, params.pid);
    /* If this file does not exist the process is dead. */
    return;
  }

  if (use_inkernel_interface) { // 内核中的 lmk 驱动处理
    stats_store_taskname(params.pid,
                         proc_get_name(params.pid, path, sizeof(path)));
    return;
  }

  /* lmkd should not change soft limits for services */
  if (params.ptype == PROC_TYPE_APP && per_app_memcg) {
    if (params.oomadj >= 900) { // 注意 oomadj 不是内核中的优先级，而是 AMS 中的优先级
      soft_limit_mult = 0;
    } else if (params.oomadj >= 800) {
      soft_limit_mult = 0;
    } else if (params.oomadj >= 700) {
      soft_limit_mult = 0;
    } else if (params.oomadj >= 600) {
      // Launcher should be perceptible, don't kill it.
      params.oomadj = 200; // 什么意思
      soft_limit_mult = 1;
    } else if (params.oomadj >= 500) {
      soft_limit_mult = 0;
    } else if (params.oomadj >= 400) {
      soft_limit_mult = 0;
    } else if (params.oomadj >= 300) {
      soft_limit_mult = 1;
    } else if (params.oomadj >= 200) {
      soft_limit_mult = 8;
    } else if (params.oomadj >= 100) {
      soft_limit_mult = 10;
    } else if (params.oomadj >= 0) {
      soft_limit_mult = 20;
    } else {
      // Persistent processes will have a large
      // soft limit 512MB.
      soft_limit_mult = 64;
    }

    std::string path;
    if (!CgroupGetAttributePathForTask("MemSoftLimit", params.pid, &path)) {
      ALOGE("Querying MemSoftLimit path failed");
      return;
    }

    snprintf(val, sizeof(val), "%d", soft_limit_mult * EIGHT_MEGA); // 为什么这样计算

    /*
     * system_server process has no memcg under /dev/memcg/apps but should be
     * registered with lmkd. This is the best way so far to identify it.
     */
    is_system_server =
        (params.oomadj == SYSTEM_ADJ && (pwdrec = getpwnam("system")) != NULL &&
         params.uid == pwdrec->pw_uid);
    writefilestring(path.c_str(), val, !is_system_server);
  }

  // 将这个应用的 oomadj 保存在一个哈希表里
  procp = pid_lookup(params.pid);
  if (!procp) {
    int pidfd = -1;

    ...

    procp->pid = params.pid;
    procp->pidfd = pidfd;
    procp->uid = params.uid;
    procp->reg_pid = cred->pid;
    procp->oomadj = params.oomadj;
    procp->valid = true;
    proc_insert(procp);
  } else {
    if (!claim_record(procp, cred->pid)) {
      char buf[LINE_MAX];
      char *taskname = proc_get_name(cred->pid, buf, sizeof(buf));
      /* Only registrant of the record can remove it */
      ALOGE("%s (%d, %d) attempts to modify a process registered by another "
            "client",
            taskname ? taskname : "A process ", cred->uid, cred->pid);
      return;
    }
    proc_unslot(procp);
    procp->oomadj = params.oomadj;
    proc_slot(procp);
  }
}
```

##### LMK_PROCREMOVE

> Unregister a process

最后的 `pid_remove` 把这个 pid 对应的 `struct proc` 从 `pidhash` 和 `procadjslot_list` 里移除。

```cpp
static void cmd_procremove(LMKD_CTRL_PACKET packet, struct ucred *cred) {
  struct lmk_procremove params;
  struct proc *procp;

  lmkd_pack_get_procremove(packet, &params);

  if (use_inkernel_interface) {
    /*
     * Perform an extra check before the pid is removed, after which it
     * will be impossible for poll_kernel to get the taskname. poll_kernel()
     * is potentially a long-running blocking function; however this method
     * handles AMS requests but does not block AMS.
     */
    poll_kernel(kpoll_fd);

    stats_remove_taskname(params.pid);
    return;
  }

  procp = pid_lookup(params.pid);
  if (!procp) {
    return;
  }

  ...

  pid_remove(params.pid);
}
```

剩余的命令之后有需要再分析。

#### 杀死进程

前面和 socket lmkd 相关的内容主要用于设置 lmk 参数和进程 oomadj。当系统的物理内存不足时，将会触发 mp 事件，这个时候 lmkd 就需要通过杀死一些进程来释放内存页了。主要的函数为 `init_monitors` -> `init_mp_common` -> `mp_event_common`。

```cpp
// The implementation of this function relies on memcg statistics that are only
// available in the v1 cgroup hierarchy.
static void mp_event_common(int data, uint32_t events,
                            struct polling_params *poll_params) {
  unsigned long long evcount;
  int64_t mem_usage, memsw_usage;
  int64_t mem_pressure;
  union meminfo mi;
  struct zoneinfo zi;
  union vmstat s_crit_current;
  struct timespec curr_tm;
  static struct timespec last_pa_update_tm;
  static unsigned long kill_skip_count = 0;
  enum vmpressure_level level = (enum vmpressure_level)data;
  long other_free = 0, other_file = 0;
  int min_score_adj;
  int minfree = 0;
  static const std::string mem_usage_path = GetCgroupAttributePath("MemUsage");
  static struct reread_data mem_usage_file_data = {
      .filename = mem_usage_path.c_str(),
      .fd = -1,
  };
  static const std::string memsw_usage_path =
      GetCgroupAttributePath("MemAndSwapUsage");
  static struct reread_data memsw_usage_file_data = {
      .filename = memsw_usage_path.c_str(),
      .fd = -1,
  };
  static struct wakeup_info wi;

  if (!s_crit_event) {
    level = upgrade_vmpressure_event(level);
  }

  if (debug_process_killing) {
    ALOGI("%s memory pressure event is triggered", level_name[level]);
  }

  if (!use_psi_monitors) {
    /*
     * Check all event counters from low to critical
     * and upgrade to the highest priority one. By reading
     * eventfd we also reset the event counters.
     */
    for (int lvl = VMPRESS_LEVEL_LOW; lvl < VMPRESS_LEVEL_COUNT; lvl++) {
      if (mpevfd[lvl] != -1 &&
          TEMP_FAILURE_RETRY(read(mpevfd[lvl], &evcount, sizeof(evcount))) >
              0 &&
          evcount > 0 && lvl > level) {
        level = static_cast<vmpressure_level>(lvl);
      }
    }
  }

  /* Start polling after initial PSI event */
  if (use_psi_monitors && events) {
    /* Override polling params only if current event is more critical */
    if (!poll_params->poll_handler || data > poll_params->poll_handler->data) {
      poll_params->polling_interval_ms = PSI_POLL_PERIOD_SHORT_MS;
      poll_params->update = POLLING_START;
    }
    /*
     * Nonzero events indicates handler call due to recieved epoll_event,
     * rather than due to epoll_event timeout.
     */
    if (events) {
      if (data == VMPRESS_LEVEL_SUPER_CRITICAL) {
        s_crit_event = true;
        poll_params->polling_interval_ms = psi_poll_period_scrit_ms;
        vmstat_parse(&s_crit_base);
      } else if (s_crit_event) {
        /* Override the supercritical event only if the system
         * is not in direct reclaim.
         */
        int64_t throttle, sync;

        vmstat_parse(&s_crit_current);
        throttle = s_crit_current.field.pgscan_direct_throttle -
                   s_crit_base.field.pgscan_direct_throttle;
        sync = s_crit_current.field.pgscan_direct -
               s_crit_base.field.pgscan_direct;
        if (!throttle && !sync) {
          s_crit_event = false;
        }
        s_crit_base = s_crit_current;
      }
    }
  }

  if (clock_gettime(CLOCK_MONOTONIC_COARSE, &curr_tm) != 0) {
    ALOGE("Failed to get current time");
    return;
  }

  record_wakeup_time(&curr_tm, events ? Event : Polling, &wi);

  if (kill_timeout_ms && get_time_diff_ms(&last_kill_tm, &curr_tm) <
                             static_cast<long>(kill_timeout_ms)) {
    /*
     * If we're within the no-kill timeout, see if there's pending reclaim work
     * from the last killed process. If so, skip killing for now.
     */
    if (is_kill_pending()) {
      kill_skip_count++;
      wi.skipped_wakeups++;
      return;
    }
    /*
     * Process is dead, stop waiting. This has no effect if pidfds are supported
     * and death notification already caused waiting to stop.
     */
    stop_wait_for_proc_kill(true);
  } else {
    /*
     * Killing took longer than no-kill timeout. Stop waiting for the last
     * process to die because we are ready to kill again.
     */
    stop_wait_for_proc_kill(false);
  }

  if (kill_skip_count > 0) {
    ALOGI("%lu memory pressure events were skipped after a kill!",
          kill_skip_count);
    kill_skip_count = 0;
  }

  if (meminfo_parse(&mi) < 0 || zoneinfo_parse(&zi) < 0) {
    ALOGE("Failed to get free memory!");
    return;
  }

  if (use_minfree_levels) {
    int i;

    other_free = mi.field.nr_free_pages - zi.totalreserve_pages;
    if (mi.field.nr_file_pages >
        (mi.field.shmem + mi.field.unevictable + mi.field.swap_cached)) {
      other_file = (mi.field.nr_file_pages - mi.field.shmem -
                    mi.field.unevictable - mi.field.swap_cached);
    } else {
      other_file = 0;
    }

    min_score_adj = OOM_SCORE_ADJ_MAX + 1;
    for (i = 0; i < lowmem_targets_size; i++) {
      minfree = lowmem_minfree[i];
      if (other_free < minfree && other_file < minfree) {
        min_score_adj = lowmem_adj[i];
        // Adaptive LMK
        if (enable_adaptive_lmk && level == VMPRESS_LEVEL_CRITICAL &&
            i > lowmem_targets_size - 4) {
          min_score_adj = lowmem_adj[i - 1];
        }
        break;
      }
    }

    if (min_score_adj == OOM_SCORE_ADJ_MAX + 1) {
      if (debug_process_killing && lowmem_targets_size) {
        ALOGI("Ignore %s memory pressure event "
              "(free memory=%ldkB, cache=%ldkB, limit=%ldkB)",
              level_name[level], other_free * page_k, other_file * page_k,
              (long)lowmem_minfree[lowmem_targets_size - 1] * page_k);
      }
      return;
    }

    goto do_kill;
  }

  if (level == VMPRESS_LEVEL_LOW) {
    record_low_pressure_levels(&mi);
    if (enable_preferred_apps) {
      if (get_time_diff_ms(&last_pa_update_tm, &curr_tm) >=
          pa_update_timeout_ms) {
        if (!use_perf_api_for_pref_apps) {
          if (perf_ux_engine_trigger) {
            perf_ux_engine_trigger(PAPP_OPCODE, preferred_apps);
          }
        } else {
          if (perf_sync_request) {
            const char *tmp = perf_sync_request(PAPP_PERF_TRIGGER);
            if (tmp != NULL) {
              strlcpy(preferred_apps, tmp, strlen(tmp));
              free((void *)tmp);
            }
          }
        }
        last_pa_update_tm = curr_tm;
      }
    }
  }

  if (level_oomadj[level] > OOM_SCORE_ADJ_MAX) {
    /* Do not monitor this pressure level */
    return;
  }

  if ((mem_usage = get_memory_usage(&mem_usage_file_data)) < 0) {
    goto do_kill;
  }
  if ((memsw_usage = get_memory_usage(&memsw_usage_file_data)) < 0) {
    goto do_kill;
  }

  // Calculate percent for swappinness.
  mem_pressure = (mem_usage * 100) / memsw_usage;

  if (enable_pressure_upgrade && level != VMPRESS_LEVEL_CRITICAL) {
    // We are swapping too much.
    if (mem_pressure < upgrade_pressure) {
      level = upgrade_level(level);
      if (debug_process_killing) {
        ALOGI("Event upgraded to %s", level_name[level]);
      }
    }
  }

  // If we still have enough swap space available, check if we want to
  // ignore/downgrade pressure events.
  if (mi.field.total_swap &&
      (get_free_swap(&mi) >=
       mi.field.total_swap * swap_free_low_percentage / 100)) {
    // If the pressure is larger than downgrade_pressure lmk will not
    // kill any process, since enough memory is available.
    if (mem_pressure > downgrade_pressure) {
      if (debug_process_killing) {
        ALOGI("Ignore %s memory pressure", level_name[level]);
      }
      return;
    } else if (level == VMPRESS_LEVEL_CRITICAL &&
               mem_pressure > upgrade_pressure) {
      if (debug_process_killing) {
        ALOGI("Downgrade critical memory pressure");
      }
      // Downgrade event, since enough memory available.
      level = downgrade_level(level);
    }
  }

do_kill:
  if (low_ram_device && per_app_memcg) {
    /* For Go devices kill only one task */
    if (find_and_kill_process(use_minfree_levels ? min_score_adj
                                                 : level_oomadj[level],
                              NULL, &mi, &wi, &curr_tm, NULL) == 0) {
      if (debug_process_killing) {
        ALOGI("Nothing to kill");
      }
    }
  } else {
    int pages_freed;
    static struct timespec last_report_tm;
    static unsigned long report_skip_count = 0;

    if (!use_minfree_levels) {
      if (!enable_watermark_check) {
        /* Free up enough memory to downgrate the memory pressure to low level
         */
        if (mi.field.nr_free_pages >= low_pressure_mem.max_nr_free_pages) {
          if (debug_process_killing) {
            ULMK_LOG(I,
                     "Ignoring pressure since more memory is "
                     "available (%" PRId64 ") than watermark (%" PRId64 ")",
                     mi.field.nr_free_pages,
                     low_pressure_mem.max_nr_free_pages);
          }
          return;
        }
        min_score_adj = level_oomadj[level];
      } else {
        min_score_adj = zone_watermarks_ok(level);
        if (min_score_adj == OOM_SCORE_ADJ_MAX + 1) {
          ULMK_LOG(I, "Ignoring pressure since per-zone watermarks ok");
          return;
        }
      }
    }

    pages_freed =
        find_and_kill_process(min_score_adj, NULL, &mi, &wi, &curr_tm, NULL);

    if (pages_freed == 0) {
      /* Rate limit kill reports when nothing was reclaimed */
      if (get_time_diff_ms(&last_report_tm, &curr_tm) < FAIL_REPORT_RLIMIT_MS) {
        report_skip_count++;
        return;
      }
    }

    /* Log whenever we kill or when report rate limit allows */
    if (use_minfree_levels) {
      ALOGI("Reclaimed %ldkB, cache(%ldkB) and free(%" PRId64
            "kB)-reserved(%" PRId64 "kB) "
            "below min(%ldkB) for oom_score_adj %d",
            pages_freed * page_k, other_file * page_k,
            mi.field.nr_free_pages * page_k, zi.totalreserve_pages * page_k,
            minfree * page_k, min_score_adj);
    } else {
      ALOGI("Reclaimed %ldkB at oom_score_adj %d", pages_freed * page_k,
            min_score_adj);
    }

    if (report_skip_count > 0) {
      ALOGI("Suppressed %lu failed kill reports", report_skip_count);
      report_skip_count = 0;
    }

    last_report_tm = curr_tm;
  }
  if (is_waiting_for_kill()) {
    /* pause polling if we are waiting for process death notification */
    poll_params->update = POLLING_PAUSE;
  }
}
```



### 一些想法

- epoll 是很常用的一个机制，有简单了解过，但还不够。
- 搞懂 lmkd 的关键在于明白 `minfree`, `oom_adj_score`, `oomadj`, `min_oomadj`, `max_oomadj` 这些变量分别代表什么意思。
- 例外还可以借此机会搞明白 `struct zoneinfo`, `struct meminfo_field`, `struct meminfo`, `struct vmstat`, `struct watermark_info` 这些内存管理中关键的结构体是怎样使用的。

### Reference

[1] https://jekton.github.io/2019/03/21/android9-lmk-lmkd/

[2] https://www.cnblogs.com/linhaostudy/p/12593441.html

[3] https://github.com/reklaw-tech/reklaw_blog/blob/master/Android/ULMK/Android%20Q%20LMKD%E5%8E%9F%E7%90%86%E7%AE%80%E4%BB%8B.md
