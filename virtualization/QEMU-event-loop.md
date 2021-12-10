## QEMU Event Loop

### 问题

- 事件循环是啥？

  当程序需要访问设备的时候，比如写入、读取数据时，不是让 CPU 不断的轮询，而是当设备完成任务后通过这个机制通知挂起程序，使之重新加入到运行队列。这样能够让 CPU 保持 BUSY。

- 事件循环和时间有什么关系？

  在 QEMU 中，时钟也是用这种机制模拟的。

- 设备怎样使用事件通知机制？回调函数在那里注册？

  设备调用 `qemu_set_fd_handler` 将 fd 加入到 gsource ，一般的设备使用的都是 aiocontext 事件源。`qemu_set_fd_handler` 会创建 aiocontext，然后调用 `aio_set_fd_handler` 设置回调函数。

  ```c
  new_node->io_read = io_read;
  new_node->io_write = io_write;
  new_node->io_poll = io_poll;
  new_node->opaque = opaque;
  new_node->is_external = is_external;
  ```

  之后 aiocontext 的 dispatch 函数 `aio_ctx_finalize` 会检查所有的 handler 进行处理。

- 为什么要设置 timeout？

- Aiocontext 和 main_loop 有什么关系？

  main_loop 是一个线程，QEMU 使用的是 glib 的框架，这个框架可以使用不同的事件源。而 Aiocontext 是 QEMU 自定义的事件源。

  其有 3 个重要的函数：

  ```c
  static int os_host_main_loop_wait(int64_t timeout)
  {
      GMainContext *context = g_main_context_default();
      int ret;

      g_main_context_acquire(context);

      glib_pollfds_fill(&timeout); // 1. 获取所有需要监听的 fd

      qemu_mutex_unlock_iothread();
      replay_mutex_unlock();

      ret = qemu_poll_ns((GPollFD *)gpollfds->data, gpollfds->len, timeout); // 2. 用 ppoll 进行监听

      replay_mutex_lock();
      qemu_mutex_lock_iothread();

      glib_pollfds_poll(); // 3. 有事件发生，进行分发

      g_main_context_release(context);

      return ret;
  }
  ```

  而这 3 个函数对于 Aiocontext 事件源，会调用对应的处理函数。

  ```plain
  Thread 1 "qemu-system-x86" hit Breakpoint 4, aio_ctx_prepare (source=0x5555567972a0, timeout=0x7fffffffd924) at util/async.c:221
  221         AioContext *ctx = (AioContext *) source;
  (gdb) bt
  #0  aio_ctx_prepare (source=0x5555567972a0, timeout=0x7fffffffd924) at util/async.c:221
  #1  0x00007ffff71698ef in g_main_context_prepare () from /lib/x86_64-linux-gnu/libglib-2.0.so.0
  #2  0x0000555555e00498 in glib_pollfds_fill (cur_timeout=0x7fffffffd9f8) at util/main-loop.c:191
  #3  0x0000555555e00612 in os_host_main_loop_wait (timeout=29755634) at util/main-loop.c:232
  #4  0x0000555555e0076d in main_loop_wait (nonblocking=0) at util/main-loop.c:518
  #5  0x0000555555a1061f in main_loop () at vl.c:1812
  #6  0x0000555555a17b74 in main (argc=5, argv=0x7fffffffdd98, envp=0x7fffffffddc8) at vl.c:4473
  ```

  ```plain
  Thread 1 "qemu-system-x86" hit Breakpoint 5, aio_ctx_check (source=0x5555566ca290) at util/async.c:238
  238         AioContext *ctx = (AioContext *) source;
  (gdb) bt
  #0  aio_ctx_check (source=0x5555566ca290) at util/async.c:238
  #1  0x00007ffff7169da1 in g_main_context_check () from /lib/x86_64-linux-gnu/libglib-2.0.so.0
  #2  0x0000555555e005ce in glib_pollfds_poll () at util/main-loop.c:218
  #3  0x0000555555e0065c in os_host_main_loop_wait (timeout=0) at util/main-loop.c:242
  #4  0x0000555555e0076d in main_loop_wait (nonblocking=0) at util/main-loop.c:518
  #5  0x0000555555a1061f in main_loop () at vl.c:1812
  #6  0x0000555555a17b74 in main (argc=5, argv=0x7fffffffdd98, envp=0x7fffffffddc8) at vl.c:4473
  ```

  ```plain
  Thread 1 "qemu-system-x86" hit Breakpoint 6, aio_ctx_dispatch (source=0x5555567972a0, callback=0x0, user_data=0x0) at util/async.c:257
  257         AioContext *ctx = (AioContext *) source;
  (gdb) bt
  #0  aio_ctx_dispatch (source=0x5555567972a0, callback=0x0, user_data=0x0) at util/async.c:257
  #1  0x00007ffff716a17d in g_main_context_dispatch () from /lib/x86_64-linux-gnu/libglib-2.0.so.0
  #2  0x0000555555e005de in glib_pollfds_poll () at util/main-loop.c:219
  #3  0x0000555555e0065c in os_host_main_loop_wait (timeout=0) at util/main-loop.c:242
  #4  0x0000555555e0076d in main_loop_wait (nonblocking=0) at util/main-loop.c:518
  #5  0x0000555555a1061f in main_loop () at vl.c:1812
  #6  0x0000555555a17b74 in main (argc=5, argv=0x7fffffffdd98, envp=0x7fffffffddc8) at vl.c:4473
  ```

### Event sources

An event loop monitors *event sources* for activity and invokes a callback function when an event occurs. This makes it possible to process multiple event sources within a single CPU thread. The application can appear to do multiple things at once without multithreading because it switches between handling different event sources.

The most important event sources in QEMU are:

- File descriptors such as sockets and character devices.
- Event notifiers (implemented as eventfds on Linux).
- Timers for delayed function execution.
- Bottom-halves (BHs) for invoking a function in another thread or deferring a function call to avoid reentrancy.

### Event loops and threads

QEMU has several different types of threads:

- **vCPU threads** that execute guest code and perform device emulation synchronously with respect to the vCPU.
- The **main loop** that runs the event loops (yes, there is more than one!) used by many QEMU components.
- **IOThreads** that run event loops for device emulation concurrently with vCPUs and "out-of-band" QMP monitor commands.

> It's worth explaining **how device emulation interacts with threads**. When guest code accesses a device register the vCPU thread traps the access and dispatches it to an emulated device. The device's read/write function runs in the vCPU thread. The vCPU thread cannot resume guest code execution until the device's read/write function returns. This means long-running operations like emulating a timer chip or disk I/O cannot be performed synchronously in the vCPU thread since they would block the vCPU. Most devices solve this problem using the main loop thread's event loops. They add timer or file descriptor monitoring callbacks to the main loop and return back to guest code execution. When the timer expires or the file descriptor becomes ready the callback function runs in the main loop thread. The final part of emulating a guest timer or disk access therefore runs in the main loop thread and not a vCPU thread.
>

The key point is that vCPU threads do not run an event loop. The main loop thread and IOThreads run event loops. vCPU threads can add event sources to the main loop or IOThread event loops. Callbacks run in the main loop thread or IOThreads.

![img](/home/guanshun/gitlab/UFuture.github.io/image/qemu-event-loop.1.svg)



### Main loop thread in QEMU

QEMU 的第一个 thread 启动了各个 vCPU 之后，然后就会调用 ppoll 来进行监听。

```plain
#0  qemu_poll_ns (fds=0x0, nfds=0, timeout=0) at util/qemu-timer.c:353
#1  0x0000555555e0063e in os_host_main_loop_wait (timeout=0) at util/main-loop.c:237
#2  0x0000555555e0076d in main_loop_wait (nonblocking=0) at util/main-loop.c:518
#3  0x0000555555a1061f in main_loop () at vl.c:1812
#4  0x0000555555a17b74 in main (argc=5, argv=0x7fffffffdd98, envp=0x7fffffffddc8) at vl.c:4473
```

```c
static int os_host_main_loop_wait(int64_t timeout)
{
    GMainContext *context = g_main_context_default();
    int ret;

    g_main_context_acquire(context);

    glib_pollfds_fill(&timeout); // 获取所要监听的 fd

    qemu_mutex_unlock_iothread();
    replay_mutex_unlock();

    ret = qemu_poll_ns((GPollFD *)gpollfds->data, gpollfds->len, timeout); // 调用 ppoll 监听 fd 上发生的事件

    replay_mutex_lock();
    qemu_mutex_lock_iothread();

    glib_pollfds_poll(); // 事件的分发处理

    g_main_context_release(context);

    return ret;
}
```

```c
static void glib_pollfds_fill(int64_t *cur_timeout)
{
    GMainContext *context = g_main_context_default();
    int timeout = 0;
    int64_t timeout_ns;
    int n;

    g_main_context_prepare(context, &max_priority);

    glib_pollfds_idx = gpollfds->len;
    n = glib_n_poll_fds;
    do {
        GPollFD *pfds;
        glib_n_poll_fds = n; // 需要监听的 fd 数量
        g_array_set_size(gpollfds, glib_pollfds_idx + glib_n_poll_fds);
        pfds = &g_array_index(gpollfds, GPollFD, glib_pollfds_idx);
        n = g_main_context_query(context, max_priority, &timeout, pfds,
                                 glib_n_poll_fds); // 获取需要监听的 fd，保存在 gpollfds.data 中
    } while (n != glib_n_poll_fds);

    if (timeout < 0) {
        timeout_ns = -1;
    } else {
        timeout_ns = (int64_t)timeout * (int64_t)SCALE_MS;
    }

    *cur_timeout = qemu_soonest_timeout(timeout_ns, *cur_timeout);
}
```

```c
int qemu_poll_ns(GPollFD *fds, guint nfds, int64_t timeout)
{
#ifdef CONFIG_PPOLL
    if (timeout < 0) {
        return ppoll((struct pollfd *)fds, nfds, NULL, NULL);
    } else {
        struct timespec ts;
        int64_t tvsec = timeout / 1000000000LL;
        /* Avoid possibly overflowing and specifying a negative number of
         * seconds, which would turn a very long timeout into a busy-wait.
         */
        if (tvsec > (int64_t)INT32_MAX) {
            tvsec = INT32_MAX;
        }
        ts.tv_sec = tvsec;
        ts.tv_nsec = timeout % 1000000000LL;
        return ppoll((struct pollfd *)fds, nfds, &ts, NULL); // 监听 fd 上的事件
    }
#else
    return g_poll(fds, nfds, qemu_timeout_ns_to_ms(timeout));
#endif
}
```

```c
static void glib_pollfds_poll(void)
{
    GMainContext *context = g_main_context_default();
    GPollFD *pfds = &g_array_index(gpollfds, GPollFD, glib_pollfds_idx);

    if (g_main_context_check(context, max_priority, pfds, glib_n_poll_fds)) {
        g_main_context_dispatch(context); // 事件分发
    }
}
```

问题来了，`g_main_context_dispatch` 事件分发，分发给哪个回调函数呢？

### Aiocontext

要回答上面这个问题，我们首先来看看什么是 Aiocontext。

```c
struct AioContext {
    GSource source;

    /* Used by AioContext users to protect from multi-threaded access.  */
    QemuRecMutex lock;

    /* The list of registered AIO handlers.  Protected by ctx->list_lock. */
    QLIST_HEAD(, AioHandler) aio_handlers;
    uint32_t notify_me;
    QemuLockCnt list_lock;

    /* Anchor of the list of Bottom Halves belonging to the context */
    struct QEMUBH *first_bh;
    bool notified;
    EventNotifier notifier;

    QSLIST_HEAD(, Coroutine) scheduled_coroutines;
    QEMUBH *co_schedule_bh;

    /* Thread pool for performing work and receiving completion callbacks.
     * Has its own locking.
     */
    struct ThreadPool *thread_pool;

#ifdef CONFIG_LINUX_AIO
    /* State for native Linux AIO.  Uses aio_context_acquire/release for
     * locking.
     */
    struct LinuxAioState *linux_aio;
#endif

    /* TimerLists for calling timers - one per clock type.  Has its own
     * locking.
     */
    QEMUTimerListGroup tlg;

    int external_disable_cnt;

    /* Number of AioHandlers without .io_poll() */
    int poll_disable_cnt;

    /* Polling mode parameters */
    int64_t poll_ns;        /* current polling time in nanoseconds */
    int64_t poll_max_ns;    /* maximum polling time in nanoseconds */
    int64_t poll_grow;      /* polling time growth factor */
    int64_t poll_shrink;    /* polling time shrink factor */

    /* Are we in polling mode or monitoring file descriptors? */
    bool poll_started;

    /* epoll(7) state used when built with CONFIG_EPOLL */
    int epollfd;
    bool epoll_enabled;
    bool epoll_available;
};
```

这个结构体比较复杂，我也没有完全搞懂，但从 QEMU 怎样使用 Aiocontext 上我们可以大致了解 Aiocontext 是干什么的。

首先是创建一个 Aiocontext 的关键函数，

```c
AioContext *aio_context_new(Error **errp)
{
    int ret;
    AioContext *ctx;

    ctx = (AioContext *) g_source_new(&aio_source_funcs, sizeof(AioContext)); // 设置 4 个回调函数，下面有介绍
    aio_context_setup(ctx);

    ret = event_notifier_init(&ctx->notifier, false); // 初始化代表该事件源的事件通知函数对象 ctx->notifier
    if (ret < 0) {
        error_setg_errno(errp, -ret, "Failed to initialize event notifier");
        goto fail;
    }
    g_source_set_can_recurse(&ctx->source, true);
    qemu_lockcnt_init(&ctx->list_lock);

    ctx->co_schedule_bh = aio_bh_new(ctx, co_schedule_bh_cb, ctx);
    QSLIST_INIT(&ctx->scheduled_coroutines);

    aio_set_event_notifier(ctx, &ctx->notifier, // 设置 ctx->notifier 对应的事件通知函数
                           false,
                           event_notifier_dummy_cb,
                           event_notifier_poll);
#ifdef CONFIG_LINUX_AIO
    ctx->linux_aio = NULL;
#endif
    ctx->thread_pool = NULL;
    qemu_rec_mutex_init(&ctx->lock);
    timerlistgroup_init(&ctx->tlg, aio_timerlist_notify, ctx);

    ctx->poll_ns = 0;
    ctx->poll_max_ns = 0;
    ctx->poll_grow = 0;
    ctx->poll_shrink = 0;

    return ctx;
fail:
    g_source_destroy(&ctx->source);
    return NULL;
}
```

它的调用情况如下，

```plain
(gdb) bt
#0  aio_context_new (errp=0x5555566393a8 <error_abort>) at util/async.c:419
#1  0x0000555555e00809 in iohandler_init () at util/main-loop.c:545
#2  0x0000555555e00820 in iohandler_get_aio_context () at util/main-loop.c:551
#3  0x0000555555c31046 in monitor_init_globals_core () at monitor/monitor.c:607
#4  0x0000555555976039 in monitor_init_globals () at /home/guanshun/gitlab/qemu/monitor/misc.c:2308
#5  0x0000555555a142a3 in main (argc=5, argv=0x7fffffffdd98, envp=0x7fffffffddc8) at vl.c:2919
```

进一步分析 `aio_set_event_notifier` 是怎样设置事件的通知函数的，它会调用 `aio_set_fd_handler` 函数。

```c
void aio_set_fd_handler(AioContext *ctx, // ctx 表示事件源
                        int fd,
                        bool is_external,
                        IOHandler *io_read,
                        IOHandler *io_write,
                        AioPollFn *io_poll,
                        void *opaque)
{
    AioHandler *node;
    AioHandler *new_node = NULL;
    bool is_new = false;
    bool deleted = false;
    int poll_disable_change;

    qemu_lockcnt_lock(&ctx->list_lock);

    node = find_aio_handler(ctx, fd); // 寻找对应该 fd 的 handler，如果没有，则是一个新的 fd，创建 handler

    /* Are we deleting the fd handler? */
    if (!io_read && !io_write && !io_poll) {
        if (node == NULL) {
            qemu_lockcnt_unlock(&ctx->list_lock);
            return;
        }
        /* Clean events in order to unregister fd from the ctx epoll. */
        node->pfd.events = 0;

        poll_disable_change = -!node->io_poll;
    } else {
        poll_disable_change = !io_poll - (node && !node->io_poll);
        if (node == NULL) {
            is_new = true;
        }
        /* Alloc and insert if it's not already there */
        new_node = g_new0(AioHandler, 1);

        /* Update handler with latest information */
        new_node->io_read = io_read; // 该 fd 对应的 handler
        new_node->io_write = io_write;
        new_node->io_poll = io_poll;
        new_node->opaque = opaque; // ctx->notifier
        new_node->is_external = is_external; // 用于块设备，对于事件监听的 fd 都为 false

        if (is_new) {
            new_node->pfd.fd = fd;
        } else {
            new_node->pfd = node->pfd;
        }
        g_source_add_poll(&ctx->source, &new_node->pfd); // 加入到事件源的监听列表

        new_node->pfd.events = (io_read ? G_IO_IN | G_IO_HUP | G_IO_ERR : 0); // 将发生的事件保存到 pfd.events
        new_node->pfd.events |= (io_write ? G_IO_OUT | G_IO_ERR : 0);

        QLIST_INSERT_HEAD_RCU(&ctx->aio_handlers, new_node, node); // 插入到 ctx->aio_handlers 链表中
    }
    if (node) {
        deleted = aio_remove_fd_handler(ctx, node);
    }

    /* No need to order poll_disable_cnt writes against other updates;
     * the counter is only used to avoid wasting time and latency on
     * iterated polling when the system call will be ultimately necessary.
     * Changing handlers is a rare event, and a little wasted polling until
     * the aio_notify below is not an issue.
     */
    atomic_set(&ctx->poll_disable_cnt,
               atomic_read(&ctx->poll_disable_cnt) + poll_disable_change);

    if (new_node) {
        aio_epoll_update(ctx, new_node, is_new);
    } else if (node) {
        /* Unregister deleted fd_handler */
        aio_epoll_update(ctx, node, false);
    }
    qemu_lockcnt_unlock(&ctx->list_lock);
    aio_notify(ctx);

    if (deleted) {
        g_free(node);
    }
}
```

不过 `aio_set_fd_handler` 一般被块设备相关的操作直接调用，如果仅仅添加一个普通的事件相关的 fd 到事件源，通常会调用其封装函数 `qemu_set_fd_handler`，该函数将 fd 添加到 `iohandler_ctx` 中。

```c
void qemu_set_fd_handler(int fd,
                         IOHandler *fd_read,
                         IOHandler *fd_write,
                         void *opaque)
{
    iohandler_init();
    aio_set_fd_handler(iohandler_ctx, fd, false,
                       fd_read, fd_write, NULL, opaque);
}
```

可以看看这个函数的调用。

![qemu-event-loop.2](/home/guanshun/gitlab/UFuture.github.io/image/qemu-event-loop.2.png)

这个是 aio 的 4 个回调函数（用 gdb 设置断点看看这 4 个回调函数的被调用情况），

```c
static GSourceFuncs aio_source_funcs = {
    aio_ctx_prepare,
    aio_ctx_check,
    aio_ctx_dispatch,
    aio_ctx_finalize
};
```

先来看看 `aio_ctx_prepare` 的调用情况。根据 backtrace 和 p *source 可以清楚的看到在 `glib_pollfds_fill` 中调用了，即在通过 `g_main_context_query` 获取到 fd 信息前。而且 prepare 的是 aio-context 。

```plain
Thread 1 "qemu-system-x86" hit Breakpoint 1, aio_ctx_prepare (source=0x5555567972a0, timeout=0x7fffffffd924) at util/async.c:221
221         AioContext *ctx = (AioContext *) source;
(gdb) p *source
$1 = {callback_data = 0x0, callback_funcs = 0x0, source_funcs = 0x5555565dbc60 <aio_source_funcs>, ref_count = 3, context = 0x555556797690, priority = 0, flags = 33, source_id = 1,
  poll_fds = 0x555556793230, prev = 0x0, next = 0x5555566ca290, name = 0x555556797670 "aio-context", priv = 0x5555566ca430}
(gdb) bt
#0  aio_ctx_prepare (source=0x5555567972a0, timeout=0x7fffffffd924) at util/async.c:221
#1  0x00007ffff71698ef in g_main_context_prepare () from /lib/x86_64-linux-gnu/libglib-2.0.so.0
#2  0x0000555555e00498 in glib_pollfds_fill (cur_timeout=0x7fffffffd9f8) at util/main-loop.c:191
#3  0x0000555555e00612 in os_host_main_loop_wait (timeout=29942493) at util/main-loop.c:232
#4  0x0000555555e0076d in main_loop_wait (nonblocking=0) at util/main-loop.c:518
#5  0x0000555555a1061f in main_loop () at vl.c:1812
#6  0x0000555555a17b74 in main (argc=5, argv=0x7fffffffdd98, envp=0x7fffffffddc8) at vl.c:4473
```

继续执行，io-handler 也 prepare 了。

```plain
(gdb) p *source
$2 = {callback_data = 0x0, callback_funcs = 0x0, source_funcs = 0x5555565dbc60 <aio_source_funcs>, ref_count = 3, context = 0x555556797690, priority = 0, flags = 33, source_id = 2,
  poll_fds = 0x555556793220, prev = 0x5555567972a0, next = 0x5555567ef460, name = 0x5555567977e0 "io-handler", priv = 0x5555566ca400}
```

多次执行之后发现只 prepare 了 io-handler 和 aio-handler ，但是 gpollgds.data 中却有 6 个 fd，这是怎么回事？不是一回事，io-handler 和 aio-handler 是 GSource，不是 fd。

```plain
(gdb) x /56xb gpollfds.data
0x55555716c600: 0x04    0x00    0x00    0x00    0x01    0x00    0x00    0x00
0x55555716c608: 0x05    0x00    0x00    0x00    0x01    0x00    0x00    0x00
0x55555716c610: 0x07    0x00    0x00    0x00    0x01    0x00    0x00    0x00
0x55555716c618: 0x08    0x00    0x00    0x00    0x01    0x00    0x00    0x00
0x55555716c620: 0x09    0x00    0x00    0x00    0x01    0x00    0x00    0x00
0x55555716c628: 0x0a    0x00    0x00    0x00    0x01    0x00    0x00    0x00
0x55555716c630: 0x00    0x00    0x00    0x00    0x00    0x00    0x00    0x00
```

继续执行，发现 io-handler 会调用 `aio_ctx_check` ，这里不懂，为啥 aio-context 不会调用？

```plain
Thread 1 "qemu-system-x86" hit Breakpoint 2, aio_ctx_check (source=0x5555566ca290) at util/async.c:238
238         AioContext *ctx = (AioContext *) source;
(gdb) bt
#0  aio_ctx_check (source=0x5555566ca290) at util/async.c:238
#1  0x00007ffff7169da1 in g_main_context_check () from /lib/x86_64-linux-gnu/libglib-2.0.so.0
#2  0x0000555555e005ce in glib_pollfds_poll () at util/main-loop.c:218
#3  0x0000555555e0065c in os_host_main_loop_wait (timeout=0) at util/main-loop.c:242
#4  0x0000555555e0076d in main_loop_wait (nonblocking=0) at util/main-loop.c:518
#5  0x0000555555a1061f in main_loop () at vl.c:1812
#6  0x0000555555a17b74 in main (argc=5, argv=0x7fffffffdd98, envp=0x7fffffffddc8) at vl.c:4473
(gdb) p *source
$3 = {callback_data = 0x0, callback_funcs = 0x0, source_funcs = 0x5555565dbc60 <aio_source_funcs>, ref_count = 3, context = 0x555556797690, priority = 0, flags = 33, source_id = 2,
  poll_fds = 0x555556793220, prev = 0x5555567972a0, next = 0x5555567ef460, name = 0x5555567977e0 "io-handler", priv = 0x5555566ca400}
```

这里在仔细分析一下 `aio_ctx_dispatch` 分发函数，其最终会调用 `aio_dispatch_handlers`。

```c
static bool aio_dispatch_handlers(AioContext *ctx)
{
    AioHandler *node, *tmp;
    bool progress = false;

    QLIST_FOREACH_SAFE_RCU(node, &ctx->aio_handlers, node, tmp) { // 遍历 ctx->aio_handlers
        int revents;

        revents = node->pfd.revents & node->pfd.events; // fd 发生的事件
        node->pfd.revents = 0;

        if (!node->deleted && // 调用对应的 handler
            (revents & (G_IO_IN | G_IO_HUP | G_IO_ERR)) &&
            aio_node_check(ctx, node->is_external) &&
            node->io_read) {
            node->io_read(node->opaque);

            /* aio_notify() does not count as progress */
            if (node->opaque != &ctx->notifier) {
                progress = true;
            }
        }
        if (!node->deleted &&
            (revents & (G_IO_OUT | G_IO_ERR)) &&
            aio_node_check(ctx, node->is_external) &&
            node->io_write) {
            node->io_write(node->opaque);
            progress = true;
        }

        if (node->deleted) {
            if (qemu_lockcnt_dec_if_lock(&ctx->list_lock)) {
                QLIST_REMOVE(node, node);
                g_free(node);
                qemu_lockcnt_inc_and_unlock(&ctx->list_lock);
            }
        }
    }

    return progress;
}
```

### Reference

[1] https://zhuanlan.zhihu.com/p/63179839 介绍 epoll 原理的，将的很清楚

[2] http://blog.vmsplice.net/2020/08/qemu-internals-event-loops.html 他是 QEMU Event 的主要维护者，文章很值得一看。

[3] QEMU/KVM 源码解析与应用

[4] https://martins3.github.io/qemu/threads.html
