## Signal in Kernel

信号（Signal）是一种比较原始的 IPC（Inter-Process Communication，进程间通信）机制。本文主要是进行源码的分析，阅读本文的前提是对 Linux 的信号机制有所了解。

### 术语概览

- 信号（Signal）
- 信号屏蔽/阻塞（Block）：一个进程可以选择阻塞/屏蔽一个信号。然后对于其他进程向自己发送的这个信号，就直接忽略。直到不再阻塞，才能接收到新的信号。
- 经典/不可靠（Regular）信号：编号范围在[1,31]的信号。
- 实时/可靠（Realtime）信号：编号在[SIGRTMIN, SIGRTMAX]之间的信号。其中，SIGRTMIN=34，SIGRTMAX=64。
- 挂起/未决（Pending）信号：当一个信号被发送到一个进程，而该进程尚未处理该信号时，则称该信号是 Pending 的。信号的处理是在某些时机下进行的，而不像硬件终端可以立即执行。所以一个信号刚刚发送到一个进程，该进程不会立即响应。
- 忽略（Ignore）信号：一个信号的处理函数可以被设置为 SIG_IGN。这相当于一个空函数，表示直接忽略该信号，不做任何处理。
- 默认信号处理函数：一个信号的处理函数可以被设置为 SIG_DFL。表示选用系统默认的信号处理函数。

### 数据结构

要深入分析 Signal 机制，需要先了解下面的数据结构。

#### Sigset

Sigset 是一个信号集合结构。其定义在 arch/x86/include/asm/signal.h 当中。

```C
#ifdef __i386__
# define _NSIG_BPW	32
#else
# define _NSIG_BPW	64
#endif

#define _NSIG_WORDS	(_NSIG / _NSIG_BPW)

typedef unsigned long old_sigset_t;		/* at least 32 bits */

typedef struct {
	unsigned long sig[_NSIG_WORDS];
} sigset_t;
```

可以看出，Linux 使用位掩码（Bit mask）来存储信号。这里 BPW 是 Bit Per Word 的意思，表示体系结构对应的字长是多少比特。NSIG 表示信号的总数目（为 64）。我们知道，unsigned long 数据类型是一字长（32 位下是 32bit，64 位下是 64bit），所以，通过 NSIG / _NSIG_BPW 个 unsigned long 类型的数字，即可表示集合。每一个信号对应了某一个数字中的某一位。

Linux 内核提供了很多操纵 sigset 的方式。在 include/linux/signal.h 当中有定义，可以直接去找。

#### 双向循环链表

信号处理机制中大量应用 Linux 中的链表数据结构。Linux 内核中官方提供的链表结构是双向循环链表。在进行内核开发的时候，建议使用 Linux 内部提供的链表。

Linux 中的链表类型在 include/linux/types.h 当中定义：

```C
struct list_head {
	struct list_head *next, *prev;
};
```

这种链表结构如何使用呢？其实我们只需要把它包含到我们自己的需要串成链表的类型中即可。这也是 Linux 内核链表的一个非常精妙的地方——是数据里面包含链表，而不是链表里面放数据！比如，我们需要一个 LinkedList<foo>这样的结构，只需要如下定义：

```c
struct foo_list {
	struct list_head list;
    foo val;
}
```

val 则是 foo_list 的实际的数据域。此外，我们称 foo_list 的每一个实例，为里面的 list 的 container（容器）。为了初始化链表，Linux 在 include/linux/list.h 当中定义了一些实用的宏和函数。

```C
#define LIST_HEAD_INIT(name) { &(name), &(name) }

#define LIST_HEAD(name) \
	struct list_head name = LIST_HEAD_INIT(name)

static inline void INIT_LIST_HEAD(struct list_head *list)
{
	list->next = list;
	list->prev = list;
}
```

下面的例程展示了如何动态创建一个链表：

```C
struct foo_list* foos;
struct foo_list* first_foo;

first_foo = kmalloc(sizeof(*node0), GFP_KERNEL);
first_foo->val = NULL; // 为val字段赋值
INIT_LIST_HEAD(&first_foo->list);

foos = first_foo;
```

通过 INIT_LIST_HEAD，我们将表头表尾都指向自身，构成了元素数量为 1 的循环链表。因为是环形链表，从任意一个节点开始都可以遍历到整个链表。当然这么做其实是不符合规范的。我们应该给环形链表一个索引节点。可以这样做:

```C
static LIST_HEAD(foos)
```

根据宏定义，其实等价于

```C
static struct list_head foos = {&foos, &foos};
```

这么做其实是创建了一个伪”头节点“。注意到 static，可以让在函数体内定义的这个变量被分配在静态存储区。防止退出函数体之后链表表头中的 next、prev 域指针失效。

##### 链表操纵函数

- 添加一个节点到 head 节点之后：list_add(struct list_head *new, struct list_head *head)
- 添加一个节点到 head 节点之前：list_add_tail(struct list_head* new, struct list_head* head)
- 从链表当中删除一个节点：list_del(struct list_head *entry)
- 从链表中删除一个节点并重新初始化它：list_del_init(struct list_head *entry)
- 判断链表是否为空：list_empty(struct list_head *head)
- 连接两个链表（把 list 插入到 head 之后）：list_splice(struct list_head *list, struct list_head *head)

##### 遍历链表

遍历链表可以使用 list_for_each 宏。其定义如下：

```C
#define list_for_each(pos, head) \
	for (pos = (head)->next; pos != (head); pos = pos->next)
```

有索引节点的 list 都可以用 list_for_each 来遍历。将索引节点传给 list_for_each 的 head 参数即可。pos 是循环变量，表示链表中的每一个元素。可以看下面：

```c
struct list_head* p;
struct foo_list* node;

list_for_each(p, &foos) {
    node = list_entry(p, struct foo_list, list);
}
```

等等，这个 list_entry 是什么？这是 Linux 内核最精彩的几个地方之一。我们知道，我们现在在遍历的其实是 list_head 结构，而不是 foo_list。然而 list_head 是包含在 foo_list 当中的。那我们如果遍历到某一个 list_head，我们怎么知道它对应的 foo_list 是谁呢？这看似是无法做到的。

仔细分析以下。我们知道，某一个 list_head 变量相对于包含它的 foo_list 的偏移，是在编译时期就确定的！我们只需要将当前 list_head 的地址加上这个偏移，不就可以实现了吗？没错，Linux kernel 里面有一个 container_of 宏（在 include/linux/kernel.h），就是这么干的：

```C
#define container_of(ptr, type, member) ({			\
	const typeof( ((type *)0)->member ) *__mptr = (ptr);	\
	(type *)( (char *)__mptr - offsetof(type,member) );})
```

代码还是非常精妙的。仔细看一下，typeof( ((type *)0)->member )表示 member 成员的类型。_mptr 是一个这个类型的指针，然后将 ptr 复制给__mptr。那可能要问，为什么还要用 mptr 这玩意呢？直接用 ptr 不好吗？这里是为了当 ptr 的类型和 member 的类型不匹配的时候，编译器产生警告的 Warning，以提示程序员。

第二行，就是将 ptr 的地址减去在结构体内的偏移——offsetof(type, member)。所以它可以实现 container_of 的目的。

而 list_entry 就是一个 container_of 的 wrapper：

```c
#define list_entry(ptr, type, member) \
	container_of(ptr, type, member)
```

每次把链表遍历写成上面那样不够方便。所以，还提供了——list_for_each_entry(pos, head, member) 宏。上面的代码可以改写成

```C
struct foo_list* node;

list_for_each_entry(node, &foos, list) {
    ...
}
```

### 进程和信号的绑定数据结构

每一个进程都对应了一个 task_struct。对一个进程发送信号，以及安装信号，本质上都是操纵这个进程 task_struct 当中的项。在 task_struct 当中，有下列字段是与信号相关的：

（include/linux/sched.h）

```c
/* signal handlers */
struct signal_struct *signal;
struct sighand_struct *sighand;

sigset_t blocked, real_blocked;
sigset_t saved_sigmask;	/* restored if set_restore_sigmask() was used */
struct sigpending pending;
```

我们重点关注 sighand、pending、blocked。

#### sighand_struct

每一个进程对应一个 sighand_struct。主要是存储信号与处理函数之间的映射。定义如下，

```c
struct sighand_struct {
	atomic_t		count;
	struct k_sigaction	action[_NSIG];
	spinlock_t		siglock;
	wait_queue_head_t	signalfd_wqh;
};
```

其中的 action 是我们最需要关注的。它是一个长度为_NSIG 的数组。下标为 k 的元素，就代表编号为 k 的信号的处理函数。k_sigaction 实际上就是在内核态中对于 sigaction 的一个包装。

```c
struct k_sigaction {
	struct sigaction sa;
};
```

### pending

每一个进程都有一个挂起信号等待队列。我们需要注意，信号和硬中断不一样。信号处理程序只会在一些特定的时机（从内核态回到用户态的时候）被调用。所以，如果发送信号的频率比较高，对于一个进程来说，可能有一个信号还没处理，又接受到了另一个相同种类的信号。在旧的 UNIX 操作系统中，这种情况新的信号就会被丢弃。在 Linux 内核中，增加了可靠信号的支持。其实现方案很简单，就是为每个进程使用一个队列，接收到信号，则先将信号放入队列中。然而 Linux 内核有保证了对于 1-31 号信号的前向兼容性。具体来说，Linux 内核对于可靠信号的处理如下：

- 当一个进程接收到 regular signal（[1,31]），首先判断该进程当前是否已经接收过但未处理这种 signal。如果是的话，则什么也不干。否则，将该信号加入到未处理信号队列中。
- 当一个进程接收到 realtime signal 的时候，无论如何都会把该信号加入未处理信号队列中。

从这个行为来看，为了快速判断/设定某个信号是否在未处理信号队列中，可以使用上面的 sigset 数据结构来实现。然后，队列则使用链表来实现。事实上，内核就是这么做的。

```c
struct sigpending {
	struct list_head list;
	sigset_t signal;
};
```

这里的 signal，就是用来记录当前在未处理信号队列中的信号集合。而 list，则表示信号队列链表的表头。这个 list 的 container 是 sigqueue。

```c
struct sigqueue {
	struct list_head list;
	int flags;
	siginfo_t info;
	struct user_struct *user;
};
```

我们重点关注里面的 info 域。

```c
typedef struct siginfo {
	int si_signo;
	int si_errno;
	int si_code;

	union {
		int _pad[SI_PAD_SIZE];

		/* kill() */
		struct {
			__kernel_pid_t _pid;	/* sender's pid */
			__ARCH_SI_UID_T _uid;	/* sender's uid */
		} _kill;

		/* POSIX.1b timers */
		struct {
			__kernel_timer_t _tid;	/* timer id */
			int _overrun;		/* overrun count */
			char _pad[sizeof( __ARCH_SI_UID_T) - sizeof(int)];
			sigval_t _sigval;	/* same as be信号的信息low */
			int _sys_private;       /* not to be passed to user */
		} _timer;

		/* POSIX.1b signals */
		struct {
			__kernel_pid_t _pid;	/* sender's pid */
			__ARCH_SI_UID_T _uid;	/* sender's uid */
			sigval_t _sigval;
		} _rt;

		/* SIGCHLD */
		struct {
			__kernel_pid_t _pid;	/* which child */
			__ARCH_SI_UID_T _uid;	/* sender's uid */
			int _status;		/* exit code */
			__ARCH_SI_CLOCK_T _utime;
			__ARCH_SI_CLOCK_T _stime;
		} _sigchld;

		/* SIGILL, SIGFPE, SIGSEGV, SIGBUS */
		struct {
			void __user *_addr; /* faulting insn/memory ref. */
#ifdef __ARCH_SI_TRAPNO
			int _trapno;	/* TRAP # which caused the signal */
#endif
			short _addr_lsb; /* LSB of the reported address */
			struct {
				void __user *_lower;
				void __user *_upper;
			} _addr_bnd;
		} _sigfault;

		/* SIGPOLL */
		struct {
			__ARCH_SI_BAND_T _band;	/* POLL_IN, POLL_OUT, POLL_MSG */
			int _fd;
		} _sigpoll;

		/* SIGSYS */
		struct {
			void __user *_call_addr; /* calling user insn */
			int _syscall;	/* triggering system call number */
			unsigned int _arch;	/* AUDIT_ARCH_* of syscall */
		} _sigsys;
	} _sifields;
} __ARCH_SI_ATTRIBUTES siginfo_t;
```

可以看到，这里记录了接收到信号的信息。

### blocked

一个进程可以选择阻塞某些信号。对于这些被阻塞的信号，其他进程仍可以发送阻塞信号给这个进程，但是这个进程将不会接收到这个信号。blocked 也是一个 sigset，表示被阻塞的信号集合。需要注意的是，SIGKILL 信号无法被阻塞。

### pt_regs

pt_regs 是一个结构体。定义如下：

```C
struct pt_regs {
/*
 * C ABI says these regs are callee-preserved. They aren't saved on kernel entry
 * unless syscall needs a complete, fully filled "struct pt_regs".
 */
	unsigned long r15;
	unsigned long r14;
	unsigned long r13;
	unsigned long r12;
	unsigned long bp;
	unsigned long bx;
/* These regs are callee-clobbered. Always saved on kernel entry. */
	unsigned long r11;
	unsigned long r10;
	unsigned long r9;
	unsigned long r8;
	unsigned long ax;
	unsigned long cx;
	unsigned long dx;
	unsigned long si;
	unsigned long di;
/*
 * On syscall entry, this is syscall#. On CPU exception, this is error code.
 * On hw interrupt, it's IRQ number:
 */
	unsigned long orig_ax;
/* Return frame for iretq */
	unsigned long ip;
	unsigned long cs;
	unsigned long flags;
	unsigned long sp;
	unsigned long ss;
/* top of stack page */
};
```

这个结构体实际上起到这样的作用。在执行系统调用的时候，需要陷入到内核态。在进入内核态之前，内核会先将用户态下各个寄存器的情况保存下来。在准备回到用户态的时候，会将这些寄存器的值恢复。pt_regs 往往是存储在内核栈当中的。

首先我们需要明确这些寄存器在内核栈中的内存分布。

```x86asm
高地址

ss
sp
flags
cs
ip
orig_ax
di
si
...
r13
r14
r15

低地址
```

在恢复这些寄存器的时候，首先通过 mov 指令将前面的（ip 寄存器以下）的部分全部恢复。这时，栈中只剩下（从高到低）ss、sp、flags、cs、ip。这些剩余寄存器是不能通过 mov 恢复的。而是直接通过一条 iret 指令。

iret 指令将会按顺序执行下面操作：

- 从栈顶弹出并恢复 RIP
- 从栈顶弹出并恢复 CS
- 从栈顶弹出并恢复标志寄存器
- 从栈顶弹出并恢复 RSP
- 从栈顶弹出并恢复 SS

刚好符合 pt_regs 的内存分布。

### 信号处理过程

一次完整的信号处理过程，由下面的几个部分组成。

#### 安装信号处理函数

首先，用户可以自定义信号的处理函数。看下面的例程：

```c
#include <stdio.h>
#include <signal.h>

void handler(int sig) {
    printf("Received signal: %u\n", sig);
}

int main() {
    struct sigaction sa;

    sa.sa_handler = handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;

    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGKILL, &sa, NULL);

    while (1) {
        sigsuspend(&sa.sa_mask);
        printf("Haha\n");
    }

    return 0;
}
```

glibc 库将 Linux 提供的和信号有关的系统调用进行了包装（Wrap）。我们可以便捷地调用 sigaction 函数来安装信号处理函数。首先，使用 sigaction 结构体设置信号处理函数的信息。

sigaction 结构体定义于内核源码的 include/linux/signal.h。

```C
struct sigaction {
	__sighandler_t	sa_handler;
	unsigned long	sa_flags;
	__sigrestore_t sa_restorer;
	sigset_t	sa_mask;
};
```

sa_handler 是处理函数的函数指针。其类型__sighandler_t 如下

```c
typedef void __signalfn_t(int);
typedef __signalfn_t __user *__sighandler_t;
```

可知 sa_handler 是用户态下的函数，接收一个 int 变量，表示信号的编号。sa_flags 这里不谈。sa_restorer 表示信号处理函数执行结束之后，去往哪一个地址当中。GLIBC 库会自动设置这一项为 rs_sigreturn 系统调用。后面会作具体介绍。sa_mask 可以指定阻塞信号集合。当该 sa_handler 在执行中，进程会暂时屏蔽（阻塞）这个集合内的信号。（注意：当 sa_handler 不在执行的时候，仍然可以接收到这些信号）。

在设置好 sigaction 的参数之后，通过 sigaction 来安装设定。sigaction 是 GLIBC 库中的一个 Wrapper 函数，其本质是 rt_sigaction 系统调用。

rt_sigaction 的定义位于 kernel/signal.c 当中。

```C
SYSCALL_DEFINE4(rt_sigaction, int, sig,
		const struct sigaction __user *, act,
		struct sigaction __user *, oact,
		size_t, sigsetsize)
{
	struct k_sigaction new_sa, old_sa;
	int ret = -EINVAL;

	/* XXX: Don't preclude handling different sized sigset_t's.  */
	if (sigsetsize != sizeof(sigset_t))
		goto out;

	if (act) {
		if (copy_from_user(&new_sa.sa, act, sizeof(new_sa.sa)))
			return -EFAULT;
	}

	ret = do_sigaction(sig, act ? &new_sa : NULL, oact ? &old_sa : NULL);

	if (!ret && oact) {
		if (copy_to_user(oact, &old_sa.sa, sizeof(old_sa.sa)))
			return -EFAULT;
	}
out:
	return ret;
}
```

sig 参数表示对应的信号。act 参数表示新安装的 sigaction 结构体，是一个指向用户空间 sigaction 的指针。稍后该系统调用会将这个 sigaction 结构体拷贝到内核空间。oact 可以用来查询之前在该信号上安装的 sigaction 结构体。sigsetsize 需要被设置为 sizeof(sigset_t)（目的不清楚，sigset_t 在前面有详细介绍）。

首先，通过 copy_from_user 函数，将用户空间内的 act 拷贝到局部变量 new_sa 当中。

然后调用 do_sigaction。该函数定义如下

```c
int do_sigaction(int sig, struct k_sigaction *act, struct k_sigaction *oact)
{
	struct task_struct *p = current, *t;
	struct k_sigaction *k;
	sigset_t mask;

	if (!valid_signal(sig) || sig < 1 || (act && sig_kernel_only(sig)))
		return -EINVAL;

	k = &p->sighand->action[sig-1];

	spin_lock_irq(&p->sighand->siglock);
	if (oact)
		*oact = *k;

	if (act) {
		sigdelsetmask(&act->sa.sa_mask,
			      sigmask(SIGKILL) | sigmask(SIGSTOP));
		*k = *act;

		/*
		 * POSIX 3.3.1.3:
		 *  "Setting a signal action to SIG_IGN for a signal that is
		 *   pending shall cause the pending signal to be discarded,
		 *   whether or not it is blocked."
		 *
		 *  "Setting a signal action to SIG_DFL for a signal that is
		 *   pending and whose default action is to ignore the signal
		 *   (for example, SIGCHLD), shall cause the pending signal to
		 *   be discarded, whether or not it is blocked"
		 */
		if (sig_handler_ignored(sig_handler(p, sig), sig)) {
			sigemptyset(&mask);
			sigaddset(&mask, sig);
			flush_sigqueue_mask(&mask, &p->signal->shared_pending);
			for_each_thread(p, t)
				flush_sigqueue_mask(&mask, &t->pending);
		}
	}

	spin_unlock_irq(&p->sighand->siglock);
	return 0;
}
```

首先通过 if 语句检查信号是否合法。如果不合法，返回 EINVAL 错误号码。具体检查项有：

- 信号数值是否在[1,64]之间。
- sig_kernel_only 检查信号是否为 SIGKILL 或 SIGSTOP。这意味着你不能为 SIGKILL 或者 SIGSTOP 绑定处理函数。

然后通过 sigdelsetmask 强制删除 sa_mask 当中的 SIGKILL 和 SIGSTOP。这意味着你无法在一个信号处理函数执行的时候屏蔽调 SIGKILL 和 SIGSTOP 信号。

然后是最重要的一句话：*k = *act。这样，就把处理的 sigaction 结构体直接绑定到当前进程 task_struct 当中的 action 数组中（前面数据结构部分有详细介绍）。

sig_handler_ignored 可以判断一个信号是否是被忽略的信号。其定义如下：

```c
static int sig_handler_ignored(void __user *handler, int sig)
{
	/* Is it explicitly or implicitly ignored? */
	return handler == SIG_IGN ||
		(handler == SIG_DFL && sig_kernel_ignore(sig));
}
```

有两种情况，一个信号是被忽略的：

- 显式忽略：信号处理函数是 SIG_IGN。
- 隐式忽略：信号处理函数是 SIG_DFL，但是默认操作就是忽略。

如果一个信号是被忽略的，那么就将通过 flush_sigqueue_mask 函数，将该信号从当前进程的等待队列中移除。这个实现较为简单，不多介绍。

#### 其他进程向当前进程发送信号

在信号安装完毕之后，程序就正常执行。现在，有某一个进程向自己发送了某一种信号。内核会怎么处理呢？系统调用 kill 就可以让我们实现这一目的。

```c
SYSCALL_DEFINE2(kill, pid_t, pid, int, sig)
{
	struct siginfo info;

	info.si_signo = sig;
	info.si_errno = 0;
	info.si_code = SI_USER;
	info.si_pid = task_tgid_vnr(current);
	info.si_uid = from_kuid_munged(current_user_ns(), current_uid());

	return kill_something_info(sig, &info, pid);
}
```

我们知道 siginfo 就是用来存储接收到的信号的。我们可以大概猜想发送信号的核心，就是先构建一个 siginfo，然后把这个 siginfo 结构体给放入目标进程 task_struct 当中的 pending 队列当中。

在这里，我们看到了 siginfo 的构建。然后，调用了 kill_something_info 函数。

（kernel/signal.c）

```c
static int kill_something_info(int sig, struct siginfo *info, pid_t pid)
{
	int ret;

	if (pid > 0) {
		rcu_read_lock();
		ret = kill_pid_info(sig, info, find_vpid(pid));
		rcu_read_unlock();
		return ret;
	}

    ......
}
```

关注最关键的部分。就是 pid>0 的 if 语句块当中。我们发现，又调用了 kill_pid_info。

（kernel/signal.c）

```c
int kill_pid_info(int sig, struct siginfo *info, struct pid *pid)
{
	int error = -ESRCH;
	struct task_struct *p;

	for (;;) {
		rcu_read_lock();
		p = pid_task(pid, PIDTYPE_PID);
		if (p)
			error = group_send_sig_info(sig, info, p);
		rcu_read_unlock();
		if (likely(!p || error != -ESRCH))
			return error;

		/*
		 * The task was unhashed in between, try again.  If it
		 * is dead, pid_task() will return NULL, if we race with
		 * de_thread() it will find the new leader.
		 */
	}
}
```

pid_task 函数的目的是根据 pid 获取到对应的 task_struct。然后，调用 group_send_sig_info 函数。

（kernel/signal.c）

```c
int group_send_sig_info(int sig, struct siginfo *info, struct task_struct *p)
{
	int ret;

	rcu_read_lock();
	ret = check_kill_permission(sig, info, p);
	rcu_read_unlock();

	if (!ret && sig)
		ret = do_send_sig_info(sig, info, p, true);

	return ret;
}
```

然后进一步调用 do_send_sig_info 函数

（kernel/signal.c）

```c
int do_send_sig_info(int sig, struct siginfo *info, struct task_struct *p,
			bool group)
{
	unsigned long flags;
	int ret = -ESRCH;

	if (lock_task_sighand(p, &flags)) {
		ret = send_signal(sig, info, p, group);
		unlock_task_sighand(p, &flags);
	}

	return ret;
}
```

这一层一层的太复杂了。然后调用的是 send_signal 函数

（kernal/signal.c）

```c
static int send_signal(int sig, struct siginfo *info, struct task_struct *t,
			int group)
{
	int from_ancestor_ns = 0;

#ifdef CONFIG_PID_NS
	from_ancestor_ns = si_fromuser(info) &&
			   !task_pid_nr_ns(current, task_active_pid_ns(t));
#endif

	return __send_signal(sig, info, t, group, from_ancestor_ns);
}
```

最终调用的是__send_signal：

（kernel/signal.c）

```c
static int __send_signal(int sig, struct siginfo *info, struct task_struct *t,
			int group, int from_ancestor_ns)
{
	struct sigpending *pending;
	struct sigqueue *q;
	int override_rlimit;
	int ret = 0, result;

	assert_spin_locked(&t->sighand->siglock);

	result = TRACE_SIGNAL_IGNORED;
	if (!prepare_signal(sig, t,
			from_ancestor_ns || (info == SEND_SIG_FORCED)))
		goto ret;

	pending = group ? &t->signal->shared_pending : &t->pending;
	/*
	 * Short-circuit ignored signals and support queuing
	 * exactly one non-rt signal, so that we can get more
	 * detailed information about the cause of the signal.
	 */
	result = TRACE_SIGNAL_ALREADY_PENDING;
	if (legacy_queue(pending, sig))
		goto ret;

	result = TRACE_SIGNAL_DELIVERED;
	/*
	 * fast-pathed signals for kernel-internal things like SIGSTOP
	 * or SIGKILL.
	 */
	if (info == SEND_SIG_FORCED)
		goto out_set;

	/*
	 * Real-time signals must be queued if sent by sigqueue, or
	 * some other real-time mechanism.  It is implementation
	 * defined whether kill() does so.  We attempt to do so, on
	 * the principle of least surprise, but since kill is not
	 * allowed to fail with EAGAIN when low on memory we just
	 * make sure at least one signal gets delivered and don't
	 * pass on the info struct.
	 */
	if (sig < SIGRTMIN)
		override_rlimit = (is_si_special(info) || info->si_code >= 0);
	else
		override_rlimit = 0;

	q = __sigqueue_alloc(sig, t, GFP_ATOMIC | __GFP_NOTRACK_FALSE_POSITIVE,
		override_rlimit);
	if (q) {
		list_add_tail(&q->list, &pending->list);
		switch ((unsigned long) info) {
		case (unsigned long) SEND_SIG_NOINFO:
			q->info.si_signo = sig;
			q->info.si_errno = 0;
			q->info.si_code = SI_USER;
			q->info.si_pid = task_tgid_nr_ns(current,
							task_active_pid_ns(t));
			q->info.si_uid = from_kuid_munged(current_user_ns(), current_uid());
			break;
		case (unsigned long) SEND_SIG_PRIV:
			q->info.si_signo = sig;
			q->info.si_errno = 0;
			q->info.si_code = SI_KERNEL;
			q->info.si_pid = 0;
			q->info.si_uid = 0;
			break;
		default:
			copy_siginfo(&q->info, info);
			if (from_ancestor_ns)
				q->info.si_pid = 0;
			break;
		}

		userns_fixup_signal_uid(&q->info, t);

	} else if (!is_si_special(info)) {
		if (sig >= SIGRTMIN && info->si_code != SI_USER) {
			/*
			 * Queue overflow, abort.  We may abort if the
			 * signal was rt and sent by user using something
			 * other than kill().
			 */
			result = TRACE_SIGNAL_OVERFLOW_FAIL;
			ret = -EAGAIN;
			goto ret;
		} else {
			/*
			 * This is a silent loss of information.  We still
			 * send the signal, but the *info bits are lost.
			 */
			result = TRACE_SIGNAL_LOSE_INFO;
		}
	}

out_set:
	signalfd_notify(t, sig);
	sigaddset(&pending->signal, sig);
	complete_signal(sig, t, group);
ret:
	trace_signal_generate(sig, info, t, group, result);
	return ret;
}
```

代码太长了。我们分成几个部分来看。

```c
result = TRACE_SIGNAL_ALREADY_PENDING;
if (legacy_queue(pending, sig))
	goto ret;
```

这里 legacy_queue 适用于检测当前的信号是否满足下面两个条件：

- 是 regular signal（不可靠信号）
- 已经在挂起信号队列中了

```c
static inline int legacy_queue(struct sigpending *signals, int sig)
{
	return (sig < SIGRTMIN) && sigismember(&signals->signal, sig);
}
```

也就是说，在发送信号的时候，对于已经存在于队列的不可靠信号，直接忽略。

然后看

```C
q = __sigqueue_alloc(sig, t, GFP_ATOMIC | __GFP_NOTRACK_FALSE_POSITIVE,
                     override_rlimit);
if (q) {
    list_add_tail(&q->list, &pending->list);
    switch ((unsigned long) info) {
        case (unsigned long) SEND_SIG_NOINFO:
            q->info.si_signo = sig;
            q->info.si_errno = 0;
            q->info.si_code = SI_USER;
            q->info.si_pid = task_tgid_nr_ns(current,
                                             task_active_pid_ns(t));
            q->info.si_uid = from_kuid_munged(current_user_ns(), current_uid());
            break;
        case (unsigned long) SEND_SIG_PRIV:
            q->info.si_signo = sig;
            q->info.si_errno = 0;
            q->info.si_code = SI_KERNEL;
            q->info.si_pid = 0;
            q->info.si_uid = 0;
            break;
        default:
            copy_siginfo(&q->info, info);
            if (from_ancestor_ns)
                q->info.si_pid = 0;
            break;
    }

    userns_fixup_signal_uid(&q->info, t);
```

首先创建一个 sigqueue 节点。然后，一般情况下，将调用 copy_siginfo 函数，将信息复制到当前节点的 info 域内。实际上这是完全符合刚才的猜想的。

还需要提一下，接下来调用的 complete_signal 函数会调用 signal_wake_up。这个函数会将线程的 TIF_SIGPENDING 标志设为 1。这样后面就可以快速检测是否有未处理的信号了。

#### 当前进程陷入内核态，并准备返回用户态时处理信号

现在，当前进程正在正常执行。刚才已经有进程发送信号，通过 send_signal 将信号存储在了当前进程的 Pending queue 当中。当前进程显然不会立刻处理这个信号。处理信号的时机，实际上是当前进程因为一些原因陷入内核态，然后返回用户态的时候。

现在，假设当前进程因为下面的原因进入内核态：

- 中断
- 系统调用
- 异常

执行完内核态的操作之后，返回用户态。返回用户态内核内部将会使用这个函数：exit_to_usermode_loop。该函数的代码就不放了，但是该函数中有一个重要操作：

```C
if (cached_flags & _TIF_SIGPENDING)
	do_signal(regs);
```

返回用户态的时候，将会判断当前线程是否被设置了 TIF_SIGPENDING 标志。如果设定，证明存在未处理的信号。此时，就需要调用 do_signal 函数，来处理未处理的信号。

下面是 do_signal 函数，

```C
void do_signal(struct pt_regs *regs)
{
	struct ksignal ksig;

	if (get_signal(&ksig)) {
		/* Whee! Actually deliver the signal.  */
		handle_signal(&ksig, regs);
		return;
	}

    ...
}
```

可以看出，do_signal 的核心是 handle_signal。如下，

```C
static void
handle_signal(struct ksignal *ksig, struct pt_regs *regs)
{
    ...

	/*
	 * If TF is set due to a debugger (TIF_FORCED_TF), clear TF now
	 * so that register information in the sigcontext is correct and
	 * then notify the tracer before entering the signal handler.
	 */
	stepping = test_thread_flag(TIF_SINGLESTEP);
	if (stepping)
		user_disable_single_step(current);

	failed = (setup_rt_frame(ksig, regs) < 0);
	if (!failed) {
		/*
		 * Clear the direction flag as per the ABI for function entry.
		 *
		 * Clear RF when entering the signal handler, because
		 * it might disable possible debug exception from the
		 * signal handler.
		 *
		 * Clear TF for the case when it wasn't set by debugger to
		 * avoid the recursive send_sigtrap() in SIGTRAP handler.
		 */
		regs->flags &= ~(X86_EFLAGS_DF|X86_EFLAGS_RF|X86_EFLAGS_TF);
		/*
		 * Ensure the signal handler starts with the new fpu state.
		 */
		if (fpu->fpstate_active)
			fpu__clear(fpu);
	}
	signal_setup_done(failed, ksig, stepping);
}
```

主要的 handle_signal 操作，是通过 setup_rt_frame 来设定的。而 setup_rt_frame 的核心是__setup_rt_frame。

```c
static int __setup_rt_frame(int sig, struct ksignal *ksig,
			    sigset_t *set, struct pt_regs *regs)
{
	struct rt_sigframe __user *frame;
	void __user *fp = NULL;
	int err = 0;

	frame = get_sigframe(&ksig->ka, regs, sizeof(struct rt_sigframe), &fp);

	if (!access_ok(VERIFY_WRITE, frame, sizeof(*frame)))
		return -EFAULT;

	if (ksig->ka.sa.sa_flags & SA_SIGINFO) {
		if (copy_siginfo_to_user(&frame->info, &ksig->info))
			return -EFAULT;
	}

	put_user_try {
		/* Create the ucontext.  */
		if (cpu_has_xsave)
			put_user_ex(UC_FP_XSTATE, &frame->uc.uc_flags);
		else
			put_user_ex(0, &frame->uc.uc_flags);
		put_user_ex(0, &frame->uc.uc_link);
		save_altstack_ex(&frame->uc.uc_stack, regs->sp);

		/* Set up to return from userspace.  If provided, use a stub
		   already in userspace.  */
		/* x86-64 should always use SA_RESTORER. */
		if (ksig->ka.sa.sa_flags & SA_RESTORER) {
			put_user_ex(ksig->ka.sa.sa_restorer, &frame->pretcode);
		} else {
			/* could use a vstub here */
			err |= -EFAULT;
		}
	} put_user_catch(err);

	err |= setup_sigcontext(&frame->uc.uc_mcontext, fp, regs, set->sig[0]);

	err |= __copy_to_user(&frame->uc.uc_sigmask, set, sizeof(*set));

	if (err)
		return -EFAULT;

	/* Set up registers for signal handler */
	regs->di = sig;
	/* In case the signal handler was declared without prototypes */
	regs->ax = 0;

	/* This also works for non SA_SIGINFO handlers because they expect the
	   next argument after the signal number on the stack. */
	regs->si = (unsigned long)&frame->info;
	regs->dx = (unsigned long)&frame->uc;

	regs->ip = (unsigned long) ksig->ka.sa.sa_handler;

	regs->sp = (unsigned long)frame;

	/* Set up the CS register to run signal handlers in 64-bit mode,
	   even if the handler happens to be interrupting 32-bit code. */
	regs->cs = __USER_CS;

	return 0;
}
```

先看 get_sigframe 函数。

```c
static void __user *
get_sigframe(struct k_sigaction *ka, struct pt_regs *regs, size_t frame_size,
	     void __user **fpstate)
{
	/* Default to using normal stack */
	unsigned long math_size = 0;
	unsigned long sp = regs->sp;
	unsigned long buf_fx = 0;
	int onsigstack = on_sig_stack(sp);
	struct fpu *fpu = &current->thread.fpu;

	/* redzone */
	if (config_enabled(CONFIG_X86_64))
		sp -= 128;

    ...

	sp = align_sigframe(sp - frame_size);

    ...

	return (void __user *)sp;
}
```

我们只需要关注两个部分就可以了。

首先是 redzone 部分。

```c
/* redzone */
if (config_enabled(CONFIG_X86_64))
	sp -= 128;
```

红色区域（Red zone）是 X86-64 调用规范中的一个重要标准。它指出，在%rsp 指向的栈顶之后的 128 字节被保留，不能被信号和中断处理程序使用（注意：只是不被信号和中断处理程序使用，而不表示不被其他函数调用使用）。

所以，如果体系结构是 x86_64，那么就把 sp，也就是用户态的栈顶指针减去 128，保留那 128 字节的空间。

然后，我们注意到 get_sigframe 将会将栈顶指针 sp 减掉 frame_size。这其实是相当于在栈中保留 frame_size 字节的空间。（然后通过 align_sigframe 确保栈顶指针的 16 字节对齐）。然后，返回新的栈顶指针。

这个空间用来放什么呢？我们看 get_sigframe 的调用者，

```c
struct rt_sigframe __user *frame;
frame = get_sigframe(&ksig->ka, regs, sizeof(struct rt_sigframe), &fp);
```

可以看到，新的栈顶指针指向的 sizeof(struct rt_sigframe)字节的内存空间，被复制给了 frame。也就是说，刚才的 get_sigframe 实际上就是为了保留好 rt_sigframe 的空间。

rt_sigframe 可谓非常重要。其定义如下：

（x86/include/asm/sigframe.h）

```c
struct rt_sigframe {
	char __user *pretcode;
	struct ucontext uc;
	struct siginfo info;
};
```

用户栈上的内存分布：

```perl
高地址

info
uc
pretcode

低地址
```

pretcode 域记录了原本用户态的执行位置。你可能会问，现在陷入在内核态中，那么这个位置不是被保存在内核栈的 pt_regs 里面了马？因为这个 pt_regs 我们会修改（后面会看到），所以用户态原本的执行位置将被保存在用户栈上的 pretcode 域内！

然后就是 uc。ucontext 是用户上下文的意思。ucontext 结构体定义如下：

```c
struct ucontext {
	unsigned long	  uc_flags;
	struct ucontext  *uc_link;
	stack_t		  uc_stack;
	struct sigcontext uc_mcontext;
	sigset_t	  uc_sigmask;	/* mask last for extensibility */
};
```

这里 uc_mcontext 是一个 sigcontext 结构体。

```C
struct sigcontext_64 {
	__u64				r8;
	__u64				r9;
	__u64				r10;
	__u64				r11;
	__u64				r12;
	__u64				r13;
	__u64				r14;
	__u64				r15;
	__u64				di;
	__u64				si;
	__u64				bp;
	__u64				bx;
	__u64				dx;
	__u64				ax;
	__u64				cx;
	__u64				sp;
	__u64				ip;
	__u64				flags;
	__u16				cs;
	__u16				gs;
	__u16				fs;
	__u16				__pad0;
	__u64				err;
	__u64				trapno;
	__u64				oldmask;
	__u64				cr2;

	/*
	 * fpstate is really (struct _fpstate *) or (struct _xstate *)
	 * depending on the FP_XSTATE_MAGIC1 encoded in the SW reserved
	 * bytes of (struct _fpstate) and FP_XSTATE_MAGIC2 present at the end
	 * of extended memory layout. See comments at the definition of
	 * (struct _fpx_sw_bytes)
	 */
	__u64				fpstate; /* Zero when no FPU/extended context */
	__u64				reserved1[8];
};
```

很明显这个结构体是用来保存用户态程序寄存器状态的。

然后，__setup_rt_frame 函数会做的工作包括但不限于（可以对照着代码看）：

- 检查为 rt_sigframe 保存的栈区域是否可写
- 填入 ucontext 中的内容
- 填入 rt_sigframe 中的 pretcode（put_user_ex(ksig->ka.sa.sa_restorer, &frame->pretcode);）
- 保存原用户态的寄存器情况到 uc_mcontext 当中

最后，可以看到最关键的一部分：

```C
/* Set up registers for signal handler */
regs->di = sig;
/* In case the signal handler was declared without prototypes */
regs->ax = 0;

/* This also works for non SA_SIGINFO handlers because they expect the
	   next argument after the signal number on the stack. */
regs->si = (unsigned long)&frame->info;
regs->dx = (unsigned long)&frame->uc;

regs->ip = (unsigned long) ksig->ka.sa.sa_handler;

regs->sp = (unsigned long)frame;

/* Set up the CS register to run signal handlers in 64-bit mode,
	   even if the handler happens to be interrupting 32-bit code. */
regs->cs = __USER_CS;
```

注意，这里将会修改 pt_regs。首先将 sig 传递给 di，这样一来，rdi 寄存器中保存着 sig 的值。就可以实现传入参数给信号处理函数。

IP 寄存器也会被修改成 sa_handler。sa_handler 是一个指向用户空间下信号处理函数的指针。代码段寄存器 CS 也会被修改成用户空间下的代码段寄存器。这样一来，在回到用户态时，iret 将会设置 IP 和 CS 到用户空间下信号处理函数对应的位置。

然后就到了用户空间函数了！有一个细节，用户空间函数返回的时候，根据栈分布，是会返回到 pretcode 位置的。在 Glibc 中，sa_restorer 会自动设定为一个调用 rt_sigreturn 的函数。于是，最终，rt_sigreturn 系统调用被调用。

```c
asmlinkage long sys_rt_sigreturn(void)
{
	struct pt_regs *regs = current_pt_regs();
	struct rt_sigframe __user *frame;
	sigset_t set;

	frame = (struct rt_sigframe __user *)(regs->sp - sizeof(long));
	if (!access_ok(VERIFY_READ, frame, sizeof(*frame)))
		goto badframe;
	if (__copy_from_user(&set, &frame->uc.uc_sigmask, sizeof(set)))
		goto badframe;

	set_current_blocked(&set);

	if (restore_sigcontext(regs, &frame->uc.uc_mcontext))
		goto badframe;

	if (restore_altstack(&frame->uc.uc_stack))
		goto badframe;

	return regs->ax;

badframe:
	signal_fault(regs, frame, "rt_sigreturn");
	return 0;
}
```

这里代码含义非常清晰。进行一系列地恢复操作即可。首先恢复寄存器到陷入内核态之前的状态，然后恢复栈。这就是完整的信号生命周期。

### 注意

本文并非原创，原文在[这里](https://www.cnblogs.com/gnuemacs/p/14311120.html)。信号是一个困扰我很久的机制，看到这篇文章觉得很好，先拿过来放着，之后有机会再学习。直接复制过来是为了方便自己阅读同时根据自己的需求做些补充。
