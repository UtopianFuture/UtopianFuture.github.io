## Lowmemorykiller

LMK 由于功能和 OOM killer 重叠，4.2 版本之后已经被移出内核了，现在用的是 lmkd，即运行在用户态的守护进程。

Android 底层还是基于 Linux，在 Linux 中低内存是会有 oom killer 去杀掉一些进程去释放内存，而 Android 中的 lowmemorykiller 就是在此基础上做了一些调整来的。因为手机上的内存毕竟比较有限，而 Android 中 APP 在不使用之后并不是马上被杀掉，虽然上层 ActivityManagerService 中也有很多关于进程的调度以及杀进程的手段，但是毕竟还需要考虑手机剩余内存的实际情况。

lmkd 的作用就是当内存比较紧张的时候去及时杀掉一些 ActivityManagerService 还没来得及杀掉但是对用户来说不那么重要的进程，回收一些内存，保证手机的正常运行。 而 OOM 只能够保证系统的正常运行，但是有时候会很卡，这对于手机用户体验很不好。

lmkd 需要内核驱动 Lowmemorykiller 的支持才能运行。这里先分析 lmkd，后面再对比分析 lowmemorykiller。

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

`/proc/pid/oom_adj`：代表**当前进程的优先级**，这个优先级是 kernel 中的优先级，这个优先级与上层的优先级之间有一个换算。

`/proc/pid/oom_score_adj`：上层优先级，跟 ProcessList 中的优先级对应。

### 数据结构

#### meminfo

lmkd 中的 `struct meminfo` 和内核 `/proc/meminfo` 并不是完全一样的，而是经过转换的。

```cpp
union meminfo {
  struct {
    int64_t nr_total_pages;
    int64_t nr_free_pages;
    int64_t cached;
    int64_t swap_cached;
    int64_t buffers;
    int64_t shmem;
    int64_t unevictable;
    int64_t mlocked;
    int64_t total_swap;
    int64_t free_swap;
    int64_t active_anon;
    int64_t inactive_anon;
    int64_t active_file;
    int64_t inactive_file;
    int64_t sreclaimable;
    int64_t sunreclaimable;
    int64_t kernel_stack;
    int64_t page_tables;
    int64_t ion_heap;
    int64_t ion_heap_pool;
    int64_t cma_free;
    /* fields below are calculated rather than read from the file */
    int64_t nr_file_pages;
    int64_t total_gpu_kb;
    int64_t easy_available;
  } field;
  int64_t arr[MI_FIELD_COUNT];
};
```

#### zoneinfo

```cpp
struct zoneinfo {
  int node_count;
  struct zoneinfo_node nodes[MAX_NR_NODES];
  int64_t totalreserve_pages;
  int64_t total_inactive_file;
  int64_t total_active_file;
};
```

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

前面和 socket lmkd 相关的内容主要用于设置 lmk 参数和进程 oomadj。**当系统的物理内存不足时，将会触发 mp 事件(memory pressure events)**，这个时候 lmkd 就需要通过杀死一些进程来释放内存页了。主要的函数为 `init_monitors` -> `init_mp_common` -> `mp_event_common`。

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

  ...

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
        level = static_cast<vmpressure_level>(lvl); // 找到最紧急的事件
      }
    }
  }

  /* Start polling after initial PSI event */
  if (use_psi_monitors && events) {

      ... // 处理 psi 事件
  }

  ...

  // 这些参数解析都是读取内核参数的，
  // #define ZONEINFO_PATH "/proc/zoneinfo"
  // #define MEMINFO_PATH "/proc/meminfo"
  // #define VMSTAT_PATH "/proc/vmstat"
  if (meminfo_parse(&mi) < 0 || zoneinfo_parse(&zi) < 0) {
    ALOGE("Failed to get free memory!");
    return;
  }

  // 同样是从系统属性读取的配置，表示使用当我们准备杀死应用的时候，使用系统剩余的内存和文件缓存阈值作为判断依据
  if (use_minfree_levels) {
    int i;

    other_free = mi.field.nr_free_pages - zi.totalreserve_pages; // 系统可用的内存页数量
    if (mi.field.nr_file_pages >
        (mi.field.shmem + mi.field.unevictable + mi.field.swap_cached)) {
      other_file = (mi.field.nr_file_pages - mi.field.shmem - // shmem 表示 tmpfs 使用的内存大小
                    mi.field.unevictable - mi.field.swap_cached); // unevictable 表示不能 swap out 的页面
    } else {
      other_file = 0;
    }

    // 有了 other_free 和 other_file 后
    // 根据 lowmem_minfree 的值来确定 min_score_adj
    // min_score_adj 表示可以回收的最低的 oomadj 值（oomadj 越大，优先级越低，越容易被杀死）
    // oomadj 小于 min_score_adj 的进程在这次回收过程中不会被杀死
    min_score_adj = OOM_SCORE_ADJ_MAX + 1;
    for (i = 0; i < lowmem_targets_size; i++) {
      minfree = lowmem_minfree[i]; // 在 cmd_target 中初始化的
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

      ... // 不用杀进程
    }

    goto do_kill;
  }

  if (level == VMPRESS_LEVEL_LOW) {
    record_low_pressure_levels(&mi); // 记录目前遇到的最低可用内存页数和遇到的最大可用的内存页数

    ...
  }

  if (level_oomadj[level] > OOM_SCORE_ADJ_MAX) {
    /* Do not monitor this pressure level */
    return;
  }

  // mem_usage 是所用的内存数，memsw_usage 是内存数加上 swap out 的内存数
  // 接下来的代码根据这两个数据来计算内存压力
  if ((mem_usage = get_memory_usage(&mem_usage_file_data)) < 0) {
    goto do_kill;
  }
  if ((memsw_usage = get_memory_usage(&memsw_usage_file_data)) < 0) {
    goto do_kill;
  }

  // Calculate percent for swappinness.
  // swap 用的越多，值越小，压力越大
  mem_pressure = (mem_usage * 100) / memsw_usage;

  // lmkd 在给 mp level 升级的时候需要打开 enable_pressure_upgrade（默认关闭）
  // 而降级却总是可行的，说明 lmkd 尽力在不杀死应用的情况下满足系统的内存需求
  if (enable_pressure_upgrade && level != VMPRESS_LEVEL_CRITICAL) {
    // We are swapping too much.
    // swap 用的太多，内存压力大，提高 level，更加激进的杀进程
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
    if (mem_pressure > downgrade_pressure) { // 内存够用
      if (debug_process_killing) {
        ALOGI("Ignore %s memory pressure", level_name[level]);
      }
      return;
    } else if (level == VMPRESS_LEVEL_CRITICAL &&
               mem_pressure > upgrade_pressure) { // 为什么 downgrade 和 upgrade 都是 100
      if (debug_process_killing) {
        ALOGI("Downgrade critical memory pressure");
      }
      // Downgrade event, since enough memory available.
      // 内存足够，将事件降级
      level = downgrade_level(level);
    }
  }

do_kill:
  if (low_ram_device && per_app_memcg) { // 小内存设备，直接杀进程
    /* For Go devices kill only one task */
    // 回收内存使用最多的进程
    if (find_and_kill_process(use_minfree_levels ? min_score_adj // 前面计算好了
                                                 : level_oomadj[level], // 返回回收的内存大小
                              NULL, &mi, &wi, &curr_tm, NULL) == 0) {
      if (debug_process_killing) {
        ALOGI("Nothing to kill");
      }
    }
  } else { // 大内存设备
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

    pages_freed = // 流程上和小内存设备没有什么不同，还是计算 min_score_adj，杀进程
        find_and_kill_process(min_score_adj, NULL, &mi, &wi, &curr_tm, NULL);

    if (pages_freed == 0) {
      /* Rate limit kill reports when nothing was reclaimed */
      if (get_time_diff_ms(&last_report_tm, &curr_tm) < FAIL_REPORT_RLIMIT_MS) {
        report_skip_count++;
        return;
      }
    }

    ...
  }
  if (is_waiting_for_kill()) {
    /* pause polling if we are waiting for process death notification */
    poll_params->update = POLLING_PAUSE;
  }
}
```

`struct meminfo` 中的元素都不是 `/proc/meminfo` 中原有的，而是转换后的，例如，

```cpp
mi->field.nr_file_pages =
      mi->field.cached + mi->field.swap_cached + mi->field.buffers;
  mi->field.total_gpu_kb = read_gpu_total_kb();
  mi->field.easy_available = mi->field.nr_free_pages + mi->field.inactive_file;
```

总结起来就是根据内存压力计算 `min_score_adj`，这个参数就是选择进程的标准。

### lowmemorykiller

前面分析的时候提到过，如果开启了 `use_inkernel_interface`，那么就是内核的 lowmemorykiller 来杀死进程。接下来我们分析 lowmemorykiller。

lowmemorykiller 中是**通过 linux 的 shrinker 实现的**，这个是 linux 的内存回收机制的一种，由内核线程 kswapd 负责监控，在 lowmemorykiller 初始化的时候注册 register_shrinker。

```c
static int __init lowmem_init(void)
{
	register_shrinker(&lowmem_shrinker);
	lmk_event_init();
	return 0;
}

static struct shrinker lowmem_shrinker = {
	.scan_objects = lowmem_scan,
	.count_objects = lowmem_count,
	.seeks = DEFAULT_SEEKS * 16
};
```

这里有两个重要的数组，

```c
static u32 lowmem_debug_level = 1;
static short lowmem_adj[6] = {
	0,
	1,
	6,
	12,
};

static int lowmem_adj_size = 4;
static int lowmem_minfree[6] = {
	3 * 512,	/* 6MB */
	2 * 1024,	/* 8MB */
	4 * 1024,	/* 16MB */
	16 * 1024,	/* 64MB */
};

static int lowmem_minfree_size = 4;
```

`lowmem_adj` 和 `lowmem_minfree` 也就是 lmkd 中用到的 `INKERNEL_ADJ_PATH` 和 `INKERNEL_MINFREE_PATH`。

从上面的初始化函数知道，最重要的就是 `lowmem_scan` 和 `lowmem_count`。

#### lowmem_scan

```c
static unsigned long lowmem_scan(struct shrinker *s, struct shrink_control *sc)
{
	struct task_struct *tsk;
	struct task_struct *selected = NULL;
	unsigned long rem = 0;
	int tasksize;
	int i;
	short min_score_adj = OOM_SCORE_ADJ_MAX + 1;
	int minfree = 0;
	int selected_tasksize = 0;
	short selected_oom_score_adj;
	int array_size = ARRAY_SIZE(lowmem_adj);
	int other_free = global_page_state(NR_FREE_PAGES) - totalreserve_pages;
	int other_file = global_node_page_state(NR_FILE_PAGES) - // 同样是读取系统内存信息，和 lmkd 一样
				global_node_page_state(NR_SHMEM) -
				global_node_page_state(NR_UNEVICTABLE) -
				total_swapcache_pages();

	if (lowmem_adj_size < array_size)
		array_size = lowmem_adj_size;
	if (lowmem_minfree_size < array_size)
		array_size = lowmem_minfree_size;
	for (i = 0; i < array_size; i++) {
		minfree = lowmem_minfree[i];
		if (other_free < minfree && other_file < minfree) { // 根据内存情况，确定 min_score_adj
			min_score_adj = lowmem_adj[i];
			break;
		}
	}

	lowmem_print(3, "lowmem_scan %lu, %x, ofree %d %d, ma %hd\n",
		     sc->nr_to_scan, sc->gfp_mask, other_free,
		     other_file, min_score_adj);

	if (min_score_adj == OOM_SCORE_ADJ_MAX + 1) { // 内存充裕，可以不用杀
		lowmem_print(5, "lowmem_scan %lu, %x, return 0\n",
			     sc->nr_to_scan, sc->gfp_mask);
		return 0;
	}

	selected_oom_score_adj = min_score_adj;

	rcu_read_lock();
	for_each_process(tsk) { // 遍历所有进程
		struct task_struct *p;
		short oom_score_adj;

		if (tsk->flags & PF_KTHREAD)
			continue;

		p = find_lock_task_mm(tsk);
		if (!p)
			continue;

		if (task_lmk_waiting(p) &&
		    time_before_eq(jiffies, lowmem_deathpending_timeout)) {
			task_unlock(p);
			rcu_read_unlock();
			return 0;
		}
        // 按照解释，oom_score_adj 是用户态对应的优先级，[-1000, 1000]
        // 但是 min_score_adj 是根据 lowmem_adj 来的啊，其值为[0,1,6,12]
        // 有转换函数 lowmem_oom_adj_to_oom_score_adj
		oom_score_adj = p->signal->oom_score_adj;
		if (oom_score_adj < min_score_adj) {
			task_unlock(p);
			continue;
		}
		tasksize = get_mm_rss(p->mm);
		task_unlock(p);
		if (tasksize <= 0)
			continue;
		if (selected) {
			if (oom_score_adj < selected_oom_score_adj)
				continue;
			if (oom_score_adj == selected_oom_score_adj &&
			    tasksize <= selected_tasksize)
				continue;
		}
		selected = p;
		selected_tasksize = tasksize;
		selected_oom_score_adj = oom_score_adj;
		lowmem_print(2, "select '%s' (%d), adj %hd, size %d, to kill\n",
			     p->comm, p->pid, oom_score_adj, tasksize);
	}
	if (selected) {
		long cache_size = other_file * (long)(PAGE_SIZE / 1024);
		long cache_limit = minfree * (long)(PAGE_SIZE / 1024);
		long free = other_free * (long)(PAGE_SIZE / 1024);

		task_lock(selected);
		send_sig(SIGKILL, selected, 0); // 这里杀死进程，发送 SIGKILL 信号
		if (selected->mm)
			task_set_lmk_waiting(selected);
		task_unlock(selected);
		trace_lowmemory_kill(selected, cache_size, cache_limit, free);
		lowmem_print(1, "Killing '%s' (%d) (tgid %d), adj %hd,\n"
				 "   to free %ldkB on behalf of '%s' (%d) because\n"
				 "   cache %ldkB is below limit %ldkB for oom_score_adj %hd\n"
				 "   Free memory is %ldkB above reserved\n",
			     selected->comm, selected->pid, selected->tgid,
			     selected_oom_score_adj,
			     selected_tasksize * (long)(PAGE_SIZE / 1024),
			     current->comm, current->pid,
			     cache_size, cache_limit,
			     min_score_adj,
			     free);
		lowmem_deathpending_timeout = jiffies + HZ;
		rem += selected_tasksize;
		get_task_struct(selected);
	}

	lowmem_print(4, "lowmem_scan %lu, %x, return %lu\n",
		     sc->nr_to_scan, sc->gfp_mask, rem);
	rcu_read_unlock();

	if (selected) {
		handle_lmk_event(selected, selected_tasksize, min_score_adj);
		put_task_struct(selected);
	}
	return rem;
}
```

#### lowmem_count

这个很简单，就是读取系统内存信息。

```c
static unsigned long lowmem_count(struct shrinker *s,
				  struct shrink_control *sc)
{
	return global_node_page_state(NR_ACTIVE_ANON) +
		global_node_page_state(NR_ACTIVE_FILE) +
		global_node_page_state(NR_INACTIVE_ANON) +
		global_node_page_state(NR_INACTIVE_FILE);
}
```

由于 Android 中的进程启动的很频繁，四大组件（？）都会涉及到进程启动，进程启动之后做完要做的事情之后就会很快被 AMS 把优先级降低。面对低内存的情况以及用户开启太多优先级很高的 APP，AMS 这边就有一些无力了。为了保证手机正常运行必须进行进程清理，内存回收。根据当前手机剩余内存的状态，在 minfree 中找到当前等级，再根据这个等级去 adj 中找到这个等级应该杀掉的进程的优先级，然后去杀进程，直到释放足够的内存。目前大多都使用 kernel 中的 lowmemorykiller，但是上层用户的 APP 的优先级的调整还是 AMS 来完成的，lmkd 在中间充当了一个桥梁的角色，通过把上层的更新之后的 adj 写入到文件节点，提供 lowmemorykiller 杀进程的依据。

### 一些想法

- epoll 是很常用的一个机制，有简单了解过，但还不够。
- 搞懂 lmkd 的关键在于明白 `minfree`, `oom_adj_score`, `oomadj`, `min_oomadj`, `max_oomadj` 这些变量分别代表什么意思。
- 例外还可以借此机会搞明白 `struct zoneinfo`, `struct meminfo_field`, `struct meminfo`, `struct vmstat`, `struct watermark_info` 这些内存管理中关键的结构体是怎样使用的。
- slab shrinker 在分析[内存管理](../kernel/Memory-Management.md)的文章中提到过，但是不清楚其实现。这个 shrinker 和 slab shrinker 有什么关系？

### 注意

自己目前对这些机制仅限于知道原理，但是对于它们的实现不懂，包括很多关键的数据结构，所以这篇文章基本上都是参考以下几篇文章。之后再不断补充。

### Reference

[1] https://jekton.github.io/2019/03/21/android9-lmk-lmkd/

[2] https://www.cnblogs.com/linhaostudy/p/12593441.html

[3] https://github.com/reklaw-tech/reklaw_blog/blob/master/Android/ULMK/Android%20Q%20LMKD%E5%8E%9F%E7%90%86%E7%AE%80%E4%BB%8B.md
