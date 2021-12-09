## QEMU Event Loop

### 问题

- 事件循环是啥？

  当程序需要访问设备的时候，比如写入、读取数据时，不是让 CPU 不断的轮询，而是当设备完成任务后通过这个机制通知挂起程序，使之重新加入到运行队列。这样能够让 CPU 保持 BUSY。

- 事件循环和时间有什么关系？

  在 QEMU 中，时钟也是用这种机制模拟的。

An event loop monitors *event sources* for activity and invokes a callback function when an event occurs. This makes it possible to process multiple event sources within a single CPU thread. The application can appear to do multiple things at once without multithreading because it switches between handling different event sources.

那么问题来了，设备怎样使用事件通知机制。

### Event sources

The most important event sources in QEMU are:

- File descriptors such as sockets and character devices.
- Event notifiers (implemented as eventfds on Linux).
- Timers for delayed function execution.
- Bottom-halves (BHs) for invoking a function in another thread or deferring a function call to avoid reentrancy.

### Event loops and threads

QEMU has several different types of threads:

- vCPU threads that execute guest code and perform device emulation synchronously with respect to the vCPU.
- The main loop that runs the event loops (yes, there is more than one!) used by many QEMU components.
- IOThreads that run event loops for device emulation concurrently with vCPUs and "out-of-band" QMP monitor commands.

It's worth explaining how device emulation interacts with threads. When guest code accesses a device register the vCPU thread traps the access and dispatches it to an emulated device. The device's read/write function runs in the vCPU thread. The vCPU thread cannot resume guest code execution until the device's read/write function returns. This means long-running operations like emulating a timer chip or disk I/O cannot be performed synchronously in the vCPU thread since they would block the vCPU. Most devices solve this problem using the main loop thread's event loops. They add timer or file descriptor monitoring callbacks to the main loop and return back to guest code execution. When the timer expires or the file descriptor becomes ready the callback function runs in the main loop thread. The final part of emulating a guest timer or disk access therefore runs in the main loop thread and not a vCPU thread.

The key point is that vCPU threads do not run an event loop. The main loop thread and IOThreads run event loops. vCPU threads can add event sources to the main loop or IOThread event loops. Callbacks run in the main loop thread or IOThreads.

### Reference

[1] https://zhuanlan.zhihu.com/p/63179839

[2] http://blog.vmsplice.net/2020/08/qemu-internals-event-loops.html

[3] QEMU/KVM 源码解析与应用

[4] http://blog.vmsplice.net/2020/08/qemu-internals-event-loops.html 他是 QEMU Event 的主要维护者，文章很值得一看。
