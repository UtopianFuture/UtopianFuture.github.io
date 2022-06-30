## Timer in QEMU

因为 guest 需要周期性的注入时钟中断(apic / ioapic)，各种时钟设备(pit / hpet)设备的模拟，以及一些模拟设备的需求。 QEMU 需要实现定时器的功能。

### DataStruct

首先要清楚几个和 timer 相关的数据结构：

```c
typedef struct QEMUClock {
    /* We rely on BQL to protect the timerlists */
    QLIST_HEAD(, QEMUTimerList) timerlists;

    QEMUClockType type;
    bool enabled;
} QEMUClock;
```

```c
/* A QEMUTimerList is a list of timers attached to a clock. More
 * than one QEMUTimerList can be attached to each clock, for instance
 * used by different AioContexts / threads. Each clock also has
 * a list of the QEMUTimerLists associated with it, in order that
 * reenabling the clock can call all the notifiers.
 */

struct QEMUTimerList {
    QEMUClock *clock;
    QemuMutex active_timers_lock;
    QEMUTimer *active_timers;
    QLIST_ENTRY(QEMUTimerList) list;
    QEMUTimerListNotifyCB *notify_cb;
    void *notify_opaque;

    /* lightweight method to mark the end of timerlist's running */
    QemuEvent timers_done_ev; // 为了防止一个 thread 正在执行 timerlist 的 timer 的 cb 的时候，
    						  // 另一个 thread 却在 enable 或者 disable。
};
```

```c
struct QEMUTimer {
    int64_t expire_time;        /* in nanoseconds */
    QEMUTimerList *timer_list;
    QEMUTimerCB *cb;
    void *opaque;
    QEMUTimer *next;
    int attributes;
    int scale;
};
```

```c
struct QEMUTimerListGroup {
    QEMUTimerList *tl[QEMU_CLOCK_MAX];
};
```

因为 QEMU 有四种不同的 CLOCK，不同种类的 CLOCK 上的时间的进度不同，为了方便管理，一个类型的 CLOCK 都会插入到相同的 `QEMUTimerList` 上。一个事件监听 thread 需要持有一个 `QEMUTimerListGroup`，在设置 timeout 的时候会遍历 `QEMUTimerListGroup` 中所有 `QEMUClockType` 对应的 `QEMUTimerList` 上的 `QEMUTimer`，这点在 main_loop 的执行中可以清楚的看出。

> 因为 iothread 的引入，QEMU 不仅仅在 main loop 使用 ppoll 等待，还有可能在 iothread 中来等待时钟。 在 [aio / timers: Split QEMUClock into QEMUClock and QEMUTimerList](https://github.com/qemu/qemu/commit/ff83c66eccf5b5f6b6530d504e3be41559250dcb) 创建出来了 QEMUTimerListGroup，一个事件监听 thread 持有一个 group。

QEMU 中模拟了 4 种 CLOCK，

```c
/**
 * QEMUClockType:
 *
 * The following clock types are available:
 *
 * @QEMU_CLOCK_REALTIME: Real time clock
 *
 * The real time clock should be used only for stuff which does not
 * change the virtual machine state, as it runs even if the virtual
 * machine is stopped.
 *
 * @QEMU_CLOCK_VIRTUAL: virtual clock
 *
 * The virtual clock only runs during the emulation. It stops
 * when the virtual machine is stopped.
 *
 * @QEMU_CLOCK_HOST: host clock
 *
 * The host clock should be used for device models that emulate accurate
 * real time sources. It will continue to run when the virtual machine
 * is suspended, and it will reflect system time changes the host may
 * undergo (e.g. due to NTP).
 *
 * @QEMU_CLOCK_VIRTUAL_RT: realtime clock used for icount warp
 *
 * Outside icount mode, this clock is the same as @QEMU_CLOCK_VIRTUAL.
 * In icount mode, this clock counts nanoseconds while the virtual
 * machine is running.  It is used to increase @QEMU_CLOCK_VIRTUAL
 * while the CPUs are sleeping and thus not executing instructions.
 */

typedef enum {
    QEMU_CLOCK_REALTIME = 0,
    QEMU_CLOCK_VIRTUAL = 1,
    QEMU_CLOCK_HOST = 2,
    QEMU_CLOCK_VIRTUAL_RT = 3,
    QEMU_CLOCK_MAX
} QEMUClockType;
```

对不同类型的 CLOCK 采用不同的响应方式，

```c
int64_t qemu_clock_get_ns(QEMUClockType type)
{
    switch (type) {
    case QEMU_CLOCK_REALTIME:
        return get_clock();
    default:
    case QEMU_CLOCK_VIRTUAL:
        if (use_icount) {
            return cpu_get_icount();
        } else {
            return cpu_get_clock();
        }
    case QEMU_CLOCK_HOST:
        return REPLAY_CLOCK(REPLAY_CLOCK_HOST, get_clock_realtime());
    case QEMU_CLOCK_VIRTUAL_RT:
        return REPLAY_CLOCK(REPLAY_CLOCK_VIRTUAL_RT, cpu_get_clock());
    }
}
```

其中 `QEMU_CLOCK_HOST`，`QEMU_CLOCK_VIRTUAL_RT` 由于要考虑 Replay mode ，实现的比较复杂，如果不考虑 Replay mode ，其实现也是 `get_clock_realtime`，`cpu_get_clock` 。让我们看看具体是怎么响应的，

```c
static inline int64_t get_clock(void)
{
	/* XXX: using gettimeofday leads to problems if the date
           changes, so it should be avoided. */
    return get_clock_realtime();
}
```

```c
/* get host real time in nanosecond */
static inline int64_t get_clock_realtime(void)
{
    struct timeval tv;

    gettimeofday(&tv, NULL);
    return tv.tv_sec * 1000000000LL + (tv.tv_usec * 1000);
}
```

`gettimeofday` 是 C 库函数。

```c
/* Return the virtual CPU time, based on the instruction counter.  */
int64_t cpu_get_icount(void)
{
    int64_t icount;
    unsigned start;

    do {
        start = seqlock_read_begin(&timers_state.vm_clock_seqlock);
        icount = cpu_get_icount_locked();
    } while (seqlock_read_retry(&timers_state.vm_clock_seqlock, start));

    return icount;
}
```

```c
/* Return the monotonic time elapsed in VM, i.e.,
 * the time between vm_start and vm_stop
 */
int64_t cpu_get_clock(void)
{
    int64_t ti;
    unsigned start;

    do {
        start = seqlock_read_begin(&timers_state.vm_clock_seqlock);
        ti = cpu_get_clock_locked();
    } while (seqlock_read_retry(&timers_state.vm_clock_seqlock, start));

    return ti;
}
```

```c
static int64_t cpu_get_clock_locked(void)
{
    int64_t time;

    time = timers_state.cpu_clock_offset;
    if (timers_state.cpu_ticks_enabled) {
        time += get_clock();
    }

    return time;
}
```

### 设备注册 Timer

设备注册的函数调用如下：

```c
/**
 * timer_new_ns:
 * @type: the clock type to associate with the timer
 * @cb: the callback to call when the timer expires
 * @opaque: the opaque pointer to pass to the callback
 *
 * Create a new timer with nanosecond scale on the default timer list
 * associated with the clock.
 * See timer_new_full for details.
 *
 * Returns: a pointer to the newly created timer
 */
static inline QEMUTimer *timer_new_ns(QEMUClockType type, QEMUTimerCB *cb,
                                      void *opaque)
{
    return timer_new(type, SCALE_NS, cb, opaque);
}
```

```c
/**
 * timer_new:
 * @type: the clock type to use
 * @scale: the scale value for the timer
 * @cb: the callback to be called when the timer expires
 * @opaque: the opaque pointer to be passed to the callback
 *
 * Create a new timer with the given scale,
 * and associate it with the default timer list for the clock type @type.
 * See timer_new_full for details.
 *
 * Returns: a pointer to the timer
 */
static inline QEMUTimer *timer_new(QEMUClockType type, int scale,
                                   QEMUTimerCB *cb, void *opaque)
{
    return timer_new_full(NULL, type, scale, 0, cb, opaque);
}
```

```c
/**
 * timer_new_full:
 * @timer_list_group: (optional) the timer list group to attach the timer to
 * @type: the clock type to use
 * @scale: the scale value for the timer
 * @attributes: 0, or one or more OR'ed QEMU_TIMER_ATTR_<id> values
 * @cb: the callback to be called when the timer expires
 * @opaque: the opaque pointer to be passed to the callback
 *
 * Create a new timer with the given scale and attributes,
 * and associate it with timer list for given clock @type in @timer_list_group
 * (or default timer list group, if NULL).
 * The memory is allocated by the function.
 *
 * This is not the preferred interface unless you know you
 * are going to call timer_free. Use timer_init or timer_init_full instead.
 *
 * The default timer list has one special feature: in icount mode,
 * %QEMU_CLOCK_VIRTUAL timers are run in the vCPU thread.  This is
 * not true of other timer lists, which are typically associated
 * with an AioContext---each of them runs its timer callbacks in its own
 * AioContext thread.
 *
 * Returns: a pointer to the timer
 */
static inline QEMUTimer *timer_new_full(QEMUTimerListGroup *timer_list_group,
                                        QEMUClockType type,
                                        int scale, int attributes,
                                        QEMUTimerCB *cb, void *opaque)
{
    QEMUTimer *ts = g_malloc0(sizeof(QEMUTimer));
    timer_init_full(ts, timer_list_group, type, scale, attributes, cb, opaque);
    return ts;
}
```

```c
void timer_init_full(QEMUTimer *ts,
                     QEMUTimerListGroup *timer_list_group, QEMUClockType type,
                     int scale, int attributes,
                     QEMUTimerCB *cb, void *opaque)
{
    if (!timer_list_group) {
        timer_list_group = &main_loop_tlg;
    }
    ts->timer_list = timer_list_group->tl[type];
    ts->cb = cb;
    ts->opaque = opaque;
    ts->scale = scale;
    ts->attributes = attributes;
    ts->expire_time = -1;
}
```

和 `timer_new_ns`  同类型的函数还有 `timer_new_us`，`timer_new_ms`。我们看看哪些设备会使用 `timer_new_ns` 注册 Timer。

![image-20211211115729371](/home/guanshun/.config/Typora/typora-user-images/image-20211211115729371.png)

其中 rtc 的 timer 回调函数为 `rtc_update_timer`，我们会在下面看到调用这个回调函数。

```c
	s->periodic_timer = timer_new_ns(rtc_clock, rtc_periodic_timer, s);
    s->update_timer = timer_new_ns(rtc_clock, rtc_update_timer, s);
```

### Timer 的使用

这里 timer 的使用结合 main_loop 来分析。

在看代码的时候 `main_loop_wait` 中会设置 `timeout_ns`，一直搞不懂这个 `timeout_ns` 是干什么的。

> 在 main loop 中使用 ppoll 来监听 fd，一旦 fd 事件到达（比如 socket 上有数据发送），那么 ppoll 就可以返回了。 实际上，QEMU 监听 timer timeout 的机制和监听 fd 的机制合并在一起的。 对于 timer，让 main loop 从 ppoll 上返回的方法是设置其参数 timeout，这样，只要无论是 fd ready 还是 timer timeout 都会导致 ppoll 返回。
>
> 这点分析的很清楚，加深理解 timer 与 main loop 的关系。

```c
void main_loop_wait(int nonblocking)
{
    MainLoopPoll mlpoll = {
        .state = MAIN_LOOP_POLL_FILL,
        .timeout = UINT32_MAX,
        .pollfds = gpollfds,
    };
    int ret;
    int64_t timeout_ns;

    if (nonblocking) {
        mlpoll.timeout = 0;
    }

    /* poll any events */
    g_array_set_size(gpollfds, 0); /* reset for new iteration */
    /* XXX: separate device handlers from system ones */
    notifier_list_notify(&main_loop_poll_notifiers, &mlpoll);

    if (mlpoll.timeout == UINT32_MAX) {
        timeout_ns = -1;
    } else {
        timeout_ns = (uint64_t)mlpoll.timeout * (int64_t)(SCALE_MS);
    }

    timeout_ns = qemu_soonest_timeout(timeout_ns,
                                      timerlistgroup_deadline_ns(
                                          &main_loop_tlg));

    ret = os_host_main_loop_wait(timeout_ns);
    mlpoll.state = ret < 0 ? MAIN_LOOP_POLL_ERR : MAIN_LOOP_POLL_OK;
    notifier_list_notify(&main_loop_poll_notifiers, &mlpoll);

    /* CPU thread can infinitely wait for event after
       missing the warp */
    qemu_start_warp_timer();
    qemu_clock_run_all_timers();
}
```

遍历所有 CLOCK，

```c
int64_t timerlistgroup_deadline_ns(QEMUTimerListGroup *tlg)
{
    int64_t deadline = -1;
    QEMUClockType type;
    for (type = 0; type < QEMU_CLOCK_MAX; type++) {
        if (qemu_clock_use_for_deadline(type)) {
            deadline = qemu_soonest_timeout(deadline,
                                            timerlist_deadline_ns(tlg->tl[type]));
        }
    }
    return deadline;
}
```

扫描一个 QEMUTimerList 上的所有 timerout 的时间，从而计算出最近的 timeout 时间。

```c
int64_t timerlist_deadline_ns(QEMUTimerList *timer_list)
{
    int64_t delta;
    int64_t expire_time;

    if (!atomic_read(&timer_list->active_timers)) {
        return -1;
    }

    if (!timer_list->clock->enabled) {
        return -1;
    }

    /* The active timers list may be modified before the caller uses our return
     * value but ->notify_cb() is called when the deadline changes.  Therefore
     * the caller should notice the change and there is no race condition.
     */
    qemu_mutex_lock(&timer_list->active_timers_lock);
    if (!timer_list->active_timers) {
        qemu_mutex_unlock(&timer_list->active_timers_lock);
        return -1;
    }
    expire_time = timer_list->active_timers->expire_time;
    qemu_mutex_unlock(&timer_list->active_timers_lock);

    delta = expire_time - qemu_clock_get_ns(timer_list->clock->type);

    if (delta <= 0) {
        return 0;
    }

    return delta;
}
```

而每次返回都会执行 `qemu_clock_run_all_timers`，

```c
bool qemu_clock_run_all_timers(void)
{
    bool progress = false;
    QEMUClockType type;

    for (type = 0; type < QEMU_CLOCK_MAX; type++) {
        if (qemu_clock_use_for_deadline(type)) {
            progress |= qemu_clock_run_timers(type);
        }
    }

    return progress;
}
```

`qemu_clock_run_timers` 会调用 `timerlist_run_timers`，

```c
bool timerlist_run_timers(QEMUTimerList *timer_list)
{
    QEMUTimer *ts;
    int64_t current_time;
    bool progress = false;
    QEMUTimerCB *cb;
    void *opaque;
    bool need_replay_checkpoint = false;

    if (!atomic_read(&timer_list->active_timers)) {
        return false;
    }

    qemu_event_reset(&timer_list->timers_done_ev);

    ...

    /*
     * Extract expired timers from active timers list and and process them.
     *
     * In rr mode we need "filtered" checkpointing for virtual clock.  The
     * checkpoint must be recorded/replayed before processing any non-EXTERNAL timer,
     * and that must only be done once since the clock value stays the same. Because
     * non-EXTERNAL timers may appear in the timers list while it being processed,
     * the checkpoint can be issued at a time until no timers are left and we are
     * done".
     */
    current_time = qemu_clock_get_ns(timer_list->clock->type);
    qemu_mutex_lock(&timer_list->active_timers_lock);
    while ((ts = timer_list->active_timers)) {

        ...

        /* remove timer from the list before calling the callback */
        timer_list->active_timers = ts->next;
        ts->next = NULL;
        ts->expire_time = -1;
        cb = ts->cb;
        opaque = ts->opaque;

        /* run the callback (the timer list can be modified) */
        qemu_mutex_unlock(&timer_list->active_timers_lock);
        cb(opaque); // 调用不同设备注册的回调函数，然后由设备将时间中断注入到 guest 中，这种设计，太强了
        qemu_mutex_lock(&timer_list->active_timers_lock);

        progress = true;
    }
    qemu_mutex_unlock(&timer_list->active_timers_lock);

out:
    qemu_event_set(&timer_list->timers_done_ev);
    return progress;
}
```

下面就是处理 timeout 的情况：

```plain
#0  gui_update (opaque=0x555557111650) at ui/console.c:199
#1  0x0000555555dffc2f in timerlist_run_timers (timer_list=0x555556797040) at util/qemu-timer.c:588
#2  0x0000555555dffcdd in qemu_clock_run_timers (type=QEMU_CLOCK_REALTIME) at util/qemu-timer.c:602
#3  0x0000555555dfffc8 in qemu_clock_run_all_timers () at util/qemu-timer.c:688
#4  0x0000555555e007a2 in main_loop_wait (nonblocking=0) at util/main-loop.c:525
#5  0x0000555555a1061f in main_loop () at vl.c:1812
#6  0x0000555555a17b74 in main (argc=5, argv=0x7fffffffdd98, envp=0x7fffffffddc8) at vl.c:4473
```

```plain
#0  rtc_update_timer (opaque=0x555556927c50) at /home/guanshun/gitlab/qemu/hw/rtc/mc146818rtc.c:423
#1  0x0000555555dffc2f in timerlist_run_timers (timer_list=0x555556797140) at util/qemu-timer.c:588
#2  0x0000555555dffcdd in qemu_clock_run_timers (type=QEMU_CLOCK_HOST) at util/qemu-timer.c:602
#3  0x0000555555dfffc8 in qemu_clock_run_all_timers () at util/qemu-timer.c:688
#4  0x0000555555e007a2 in main_loop_wait (nonblocking=0) at util/main-loop.c:525
#5  0x0000555555a1061f in main_loop () at vl.c:1812
#6  0x0000555555a17b74 in main (argc=5, argv=0x7fffffffdd98, envp=0x7fffffffddc8) at vl.c:4473
```

### 其他

#### Replay mode

Record/replay feature is implementation of deterministic replay for system-level simulation (softmmu mode).

Record/replay functions **are used for the reverse execution and deterministic replay of qemu execution**. Determinitsic replay is used to record volatile system execution once and replay it for multiple times for the sake of analysis, debugging, logging, etc. This implementation of deterministic replay can be used for deterministic and reverse debugging of guest code through a gdb remote interface.

### Reference

[1] https://martins3.github.io/qemu/timer.html
