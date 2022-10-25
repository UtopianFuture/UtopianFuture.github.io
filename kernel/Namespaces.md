## Namespaces

namespace 的学习从 lwn 的[这个](https://lwn.net/Articles/531114/#series_index)系列文章开始。

### [Namespaces in operation, part 1: namespaces overview](https://lwn.net/Articles/531114/)

首先 namaspace 的提出是为了支持 containers——一种轻量化虚拟机的实现。

按照在内核中的实现的顺序，文章介绍了几种 namespace：

- mount namespaces：其隔离了文件系统的挂载点，使得每个 namespace 的进程看到的文件系统结构是不一样的。然后 mount 和 umount 系统调用操作的也不再是全局变量，而是仅能影响到每个 namespace。同时该 namespace 能够构成一个主从树状结构；
- UTS namespaces: 该 namespace 影响的主要是 uname 系统调用（不知道这个 uname 系统调用和 uname 命令是不是同一个东西，如果是同一个东西，那么就很好理解），其隔离两个系统标识符 `nodename` 和 `domainname`，使得每个 container 能够拥有自己的 hostname 和 [NIS](https://en.wikipedia.org/wiki/Network_Information_Service) domain name；
- IPC namespaces: 隔离 interprocess communication 资源，包括 System V IPC 和 POSIX message queues；
- PID namespaces: 隔离 PID，不同的 namespace 中进程可以有相同的 PID，这样做的好处是能够在 containers 热迁移时继续相同的 PID。同时相同的进程在不同层次的 namespace 是有不同的 PID；
- network namespaces: 每个 network namespace 有它们自己的网络设备（虚拟的）、IP 地址、IP 路由表、端口号等等；
- user namespaces: 没搞懂这个和 PID 有什么区别，user and group ID 是啥？内核中有这个结构么。但是它能实现的功能很有意思，一个在 namespace 外普通特权级（这里说的不准确，那有什么普通特权级，应该是特权级为 3）的进程在 namespace 内能够有成为特权级 0 的进程；

应该还有其他的 namespace 会慢慢开发出来。

### [Namespaces in operation, part 2: the namespaces API](https://lwn.net/Articles/531381/)

主要介绍了基本的 namesapces API：

- clone：创造一个新的 namespace

  ```c
  child_pid =
        clone(childFunc, child_stack + STACK_SIZE, /* Points to start of
                                                      downwardly growing stack */
              CLONE_NEWUTS | SIGCHLD, argv[1]);
  ```

- setns：能够使进程加入一个新的 namespace；

- unshare：使进程离开某个 namespace；

### [Namespaces in operation, part 3: PID namespaces](https://lwn.net/Articles/531419/)

PID namespace 能够创建不同的 PID，那么会产生一个问题，新的 namespace 中子进程的父进程是什么？因为在新的 namespace 中不能再看到原先 namespace 的进程，这里 clone 将新进程的 ppid 统一设置为 0。

同时 pid namespaces 也能够嵌套使用，子 namespace 能够看到（看到意味着能够发送信号给这些进程）所有在该 namespace 中的进程，包括嵌套在其中的进程，但不能看见父 namespace 的进程。

### [Namespaces in operation, part 4: more on PID namespaces](https://lwn.net/Articles/532748/)
