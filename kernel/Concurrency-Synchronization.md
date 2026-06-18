## Concurrency and Synchronization

对于内核中复杂的同步机制，目前的要求是搞清楚不同的锁的原理和区别，在不同场景下应该使用什么锁，至于每种锁的具体实现，等之后需要用了再看。

### 目录

- [锁机制](#锁机制)
- [原子操作](#原子操作)
  - [原子交换函数](#原子交换函数)
  - [內联原子操作函数](#內联原子操作函数)
- [内存屏障](#内存屏障)
- [经典自旋锁](#经典自旋锁)
  - [spinlock](#spinlock)
  - [关键函数 spin_lock](#关键函数 spin_lock)
- [MCS 锁](#MCS 锁)
  - [optimistic_spin_node](#optimistic_spin_node)
  - [快速申请通道](#快速申请通道)
  - [中速申请通道](#中速申请通道)
  - [慢速申请通道](#慢速申请通道)
  - [释放锁](#释放锁)
- [排队自旋锁](#排队自旋锁)
  - [qspinlock](#qspinlock)
  - [快速申请通道](#快速申请通道)
  - [中速申请通道](#中速申请通道)
  - [慢速申请通道](#慢速申请通道)
  - [释放锁](#释放锁)
- [信号量](#信号量)
  - [semaphore](#semaphore)
  - [关键函数 down](#关键函数 down)
  - [关键函数 up](#关键函数 up)
- [互斥锁](#互斥锁)
  - [mutex](#mutex)
  - [快速申请通道](#快速申请通道)
  - [慢速申请通道](#慢速申请通道)
  - [释放锁](#释放锁)
- [读写锁](#读写锁)
  - [读写自旋锁](#读写自旋锁)
    - [rwlock_t](#rwlock_t)
  - [读写信号量](#读写信号量)
    - [rw_semaphore](#rw_semaphore)
    - [申请读者类型信号量](#申请读者类型信号量)
    - [释放读者类型信号量](#释放读者类型信号量)
    - [申请写者类型信号量](#申请写者类型信号量)
    - [释放写者类型信号量](#释放写者类型信号量)
- [RCU](#RCU)
- [问题](#问题)
- [Reference](#Reference)

### 锁机制

**临界区是指访问和操作共享数据的代码段**，其中的资源无法同时被多个执行线程访问，访问临界区的执行线程或代码路径称为并发源，在内核中产生并发访问的并发源主要有如下 4 种：

- 中断和异常：中断发生后，中断处理程序和被中断的进程之间可能产生并发访问；
- 软中断和 tasklet：因为它们的优先级比进程高，所以会打断当前正在执行的进程上下文；
- 内核抢占：多进程之间的并发访问；
- 多处理器并发执行：多个处理器同时执行多个进程；

在 SMP 系统中情况会复杂一些：

- **同一类型的中断处理程序不会并发执行**，但是不同类型的中断可能送达不同的 CPU，因此不同类型的中断处理程序可能会并发执行；
- 同一类型的软中断会在不同的 CPU 上并发执行；
- 同一类型的 tasklet 是串行执行的，不会在多个 CPU 上并发执行；
- 不同 CPU 上的进程上下文会并发执行；

真实的场景是这样的，如**在进程上下文中操作某个临界区的资源时发生了中断，而在中断处理程序中也访问了这个资源**，如果不使用内核同步机制，那么可能会触发 bug。

实际的项目中，真正的困难是**第三**。因此在编写代码时，需要考虑哪些资源位于临界区，应该采取哪些保护措施。任何可能从多个内核代码路径访问的数据都需要保护，保护对象包括静态全局变量、全局变量、共享的数据结构、缓存、链表、红黑树等各种形式锁隐含的数据。因此在编写内核和驱动代码时，需要对数据作如下考虑：

- 除了从当前代码路径外，是否还可能从其他内核代码路径访问这些数据？如中断处理程序、 worker、tasklet、软中断等等，也许写代码的时候在可能出问题的地方加上一段代码，检测所有的访问入口（BPF 在这种地方能派上用场）。
- 若当前内核代码访问数据时发生被抢占，被调度、执行的进程会不会访问这些数据？
- 进程会不会进入睡眠状态以等待该数据？

故内核提供了多种并发访问的保护机制，这些机制有各自的应用场景，

| 锁机制                    | 特点                                                         | 使用规则                                                     |
| ------------------------- | ------------------------------------------------------------ | ------------------------------------------------------------ |
| 原子操作 - atomic         | 使用处理器的原子指令，开销小                                 | 临界区中的数据是变量、位（这是什么？bit）等简单的数据结构    |
| 内存屏障 - memory         | 使用处理器内存屏障指令或 GCC 的屏障指令                      | 读写指令时序的调整                                           |
| 自旋锁- spinlock          | 自旋等待                                                     | 中断上下文，短期持有锁，不可递归，临界区不可睡眠，现在的实现是 qspinlock |
| 信号量 - semaphore        | 可睡眠的锁                                                   | 可长时间持有锁，适合内核和用户空间复杂的同步场景             |
| 读写信号量 - rw_semaphore | 可睡眠的锁，多个读者可以同时持有锁，同一时刻只能有一个写者，**读者和写者不能同时存在** | 程序员界定出临界区后读/写属性才有用                          |
| 互斥锁 - mutex            | 可睡眠的互斥锁，比信号量快速和简洁，实现自旋等待机制         | 同一时刻只有一个线程可以持有互斥锁，由锁持有者负责解锁，即在同一个上下文中解锁，不能递归持有锁，不适合内核和用户空间复杂的同步场景 |
| RCU - Read Copy Update    | 读者持有锁没有开销，多个读者和写者可以同时共存，写者必须等待所有读者离开临界区后才能销毁相关数据 | 受保护资源必须通过指针访问，如链表等                         |

### 原子操作

原子操作是指保证指令以原子的方式执行，执行过程不会被打断。例如我们假设 `i` 是一个全局变量，那么一条很简单的 `i++` 指令其实包括读取，修改，写回 3 个操作，如果不加以保护，在上面的几个并发源中这条简单的指令都可能出错。因此内核提供了 `atomic_t` 类型的原子变量。

```c
typedef struct {
	int counter;
} atomic_t;
```

内核中也有一系列的原子操作来操作这些原子变量，如 `atomic_inc(v)`，`atomic_dec(v)` 等，然后 CPU 必须提供原子操作的汇编指令来保证原子变量的完整性，如 `xadd`，`cmpxchg` 等。

这里记录一些之后常见的原子操作：

#### 原子交换函数

- `atomic_cmpxchg(ptr, old, new)`：原子地比较 ptr 的值是否与 old 的值相等，若相等，则将 new 的值设置到 ptr 地址中，返回 old 的值；
- `atomic_xchg(ptr, new)`：原子地把 new 的值设置到 ptr 地址中并返回 ptr 的原值；
- `atomic_try_cmpxchg(ptr, old, new)`：与 atomic_cmpxchg 类似，只是返回值发生变化，返回一个 bool 值，以判断 cmpxchg 返回值是否和 old 的值相等；

#### 內联原子操作函数

- {}_relaxed：不内联内存屏障原语；
- {}_acquire：内联加载-获取内存屏障原语，不允许它后面的操作重排到它前面；
- {}_release：内联存储-释放内存屏障原语，不允许它前面的操作重排到它后面；

### 内存屏障

在 ARM 架构中有 3 条内存屏障指令：

- 数据存储屏障（Data Memory Barrier, DMB）指令：确保在执行新的存储器访问前所有的存储器访问都已经完成；
- 数据同步屏障（Data Synchronization Barrier, DSB）指令：确保在下一个指令执行前所有存储器访问都已经完成；
- 指令同步屏障（Instruction Synchronization Barrier, ISB）指令：**清空流水线**，确保在执行新的指令前，之前所有的指令都已完成；

内核中有如下内存屏障接口，都是经常遇见的：

| 接口函数                                       | 描述                                                         |
| ---------------------------------------------- | ------------------------------------------------------------ |
| barrier()                                      | 编译优化屏障，阻止编译器为了性能优化而进行指令重排           |
| mb()                                           | 内存屏障（包括读和写），用于 SMP 和 UP                       |
| rmb()                                          | 读内存屏障                                                   |
| wmb()                                          | 写内存屏障                                                   |
| smp_mb()                                       | 顾名思义，smp 的内存屏障，从下面的实现可以看出，和 mb 有些不一样 |
| smp_rmb()                                      | smp 读内存屏障                                               |
| smp_wmb()                                      | smp 写内存屏障                                               |
| __smp_mb__before_atomic/__smp_mb__after_atomic | 在原子操作中插入一个通用内存屏障（还可以这样？）             |

```c
// X86
/* Optimization barrier */
/* The "volatile" is due to gcc bugs */
#define barrier() __asm__ __volatile__("": : :"memory")

#define mb() 	asm volatile("mfence":::"memory") // 这些都是 x86 指令
#define rmb()	asm volatile("lfence":::"memory")
#define wmb()	asm volatile("sfence" ::: "memory")

#define dma_rmb()	barrier()
#define dma_wmb()	barrier()

#define __smp_mb()	asm volatile("lock; addl $0,-4(%%" _ASM_SP ")" ::: "memory", "cc")

#define __smp_rmb()	dma_rmb()
#define __smp_wmb()	barrier()

#define __smp_mb__before_atomic()	do { } while (0) // 什么都不做？
#define __smp_mb__after_atomic()	do { } while (0)
```

我们来看看常见的例子。在进程睡眠前需要设置进程状态，这个过程就需要使用内存屏障，下面这段代码是读写信号量机制中 `down_read` 慢路经的代码，之后也会遇到，这里以它为例分析，

```c
	/* wait to be given the lock */
	for (;;) {
		set_current_state(state); // 在睡眠前需要设置进程状态为不可中断/可中断等等
		if (!smp_load_acquire(&waiter.task)) {
			/* Matches rwsem_mark_wake()'s smp_store_release(). */
			break;
		}

        ...

		schedule(); // 让出 CPU，进入睡眠
		lockevent_inc(rwsem_sleep_reader);
	}
```

而 `set_current_state` 宏在修改进程状态时隐含插入了内存屏障函数 `__smp_mb`，

```c
#define set_current_state(state_value)					\
	do {								\
		debug_normal_state_change((state_value));		\
		smp_store_mb(current->__state, (state_value));		\
	} while (0)
```

在更改 current->state 后需要插入一条内存屏障指令保证加载唤醒标志（可能是 I/O 操作、锁等等事件的发生）不会出现在修改 current->state 前，否则进程运行错误。

### 经典自旋锁

如果**临界区只有一个变量，那么适合用原子变量**，但是大多数情况临界区中是一个数据操作的集合，例如一个链表及相关操作。那么在整个执行过程中都需要保证原子性，即**在数据更新完毕前，不能从其他内核代码路径访问和更改这些数据**，这就需要用锁来完成。自旋锁是内核中最常见的一种锁机制。自旋锁的特性如下：

- 忙等。OS 中的锁可分为两类，一类是忙等，一类是睡眠等待。当内核路径无法获取自旋锁时会不断尝试，直到获取到锁为止；
- 同一时刻只能有一个内核代码路径可以获得锁；
- 持有者要尽快完成临界区的执行任务。自旋锁临界区不能睡眠；
- **可以在中断上下文使用**；

当然，现在 spinlock 都是使用 qspinlock 实现的，这一点通过代码就能看出来，后面也会介绍，所以这些锁很多都不是孤立的，它们的出现都是为了解决特定的问题一步步迭代出来的。

#### spinlock

老规矩，先看看相关的数据结构，

```c
typedef struct spinlock {
	union {
		struct raw_spinlock rlock;

		...
	};
} spinlock_t;
```

```c
typedef struct raw_spinlock {
	arch_spinlock_t raw_lock;

#ifdef CONFIG_DEBUG_SPINLOCK
	unsigned int magic, owner_cpu;
	void *owner;
#endif
#ifdef CONFIG_DEBUG_LOCK_ALLOC
	struct lockdep_map dep_map;
#endif
} raw_spinlock_t;

```

```c
typedef struct qspinlock {
	union {
		atomic_t val;

		/*
		 * By using the whole 2nd least significant byte for the
		 * pending bit, we can allow better optimization of the lock
		 * acquisition for the pending bit holder.
		 */
#ifdef __LITTLE_ENDIAN
		struct {
			u8	locked;
			u8	pending;
		};
		struct {
			u16	locked_pending;
			u16	tail;
		};
#else

        ...

#endif
	};
} arch_spinlock_t;
```

#### 关键函数 spin_lock

再看看其是怎么实现的，

```c
static __always_inline void spin_lock(spinlock_t *lock)
{
	raw_spin_lock(&lock->rlock);
}

#define raw_spin_lock(lock)	_raw_spin_lock(lock)

void __lockfunc _raw_spin_lock(raw_spinlock_t *lock)
{
	__raw_spin_lock(lock);
}

static inline void __raw_spin_lock(raw_spinlock_t *lock)
{
    // 关闭内核抢占，为什么是调用 barrier，而且什么时候开启呢？
    // 在 spin_unlock 的时候允许抢占
    // 内核抢占指用户程序在执行系统调用期间可以被抢占
    // 该进程暂时挂起，使新华性的高优先级进程能够运行
    // 如果不关闭内核抢占，会出现如下情况
    // 进程 a 持有 spinlock，进程 b 优先级更高
    // 抢占了 CPU，运行过程中也申请 spinlock，忙等
    // 但 spinlock 被 a 持有，没有释放，于是死锁
	preempt_disable();
	spin_acquire(&lock->dep_map, 0, 0, _RET_IP_);
	LOCK_CONTENDED(lock, do_raw_spin_trylock, do_raw_spin_lock);
}

#define LOCK_CONTENDED(_lock, try, lock) \
	lock(_lock) // 直接调用 do_raw_spin_lock

static inline void do_raw_spin_lock(raw_spinlock_t *lock) __acquires(lock)
{
	__acquire(lock);
	arch_spin_lock(&lock->raw_lock);
	mmiowb_spin_lock();
}

#define arch_spin_lock(l)		queued_spin_lock(l)

// 在原有基础上实现的排队自旋锁机制，后面分析
#ifndef queued_spin_lock
/**
 * queued_spin_lock - acquire a queued spinlock
 * @lock: Pointer to queued spinlock structure
 */
static __always_inline void queued_spin_lock(struct qspinlock *lock)
{
	int val = 0;

	if (likely(atomic_try_cmpxchg_acquire(&lock->val, &val, _Q_LOCKED_VAL)))
		return;

	queued_spin_lock_slowpath(lock, val);
}
#endif
```

前面讲到自旋锁可以在中断上下文使用，但是使用时却不能发生中断。

如果自旋锁临界区发生中断，中断返回时会检查抢占调度（调用 schedule()），那么这样会导致两个问题：

- 发生抢占，持有锁的进程睡眠了，那么其他申请锁的进程只能一直等待，浪费 CPU 时间；
- 抢占进程也可能申请该自旋锁，导致死锁；

同时，为了确保在持有自旋锁期间不会响应中断，内核还提供了其他的接口，

```c
static __always_inline void spin_lock_irq(spinlock_t *lock)
{
	raw_spin_lock_irq(&lock->rlock);
}

void __lockfunc _raw_spin_lock_irq(raw_spinlock_t *lock)
{
	__raw_spin_lock_irq(lock);
}

static inline void __raw_spin_lock_irq(raw_spinlock_t *lock)
{
	local_irq_disable(); // 关中断
	preempt_disable();
	spin_acquire(&lock->dep_map, 0, 0, _RET_IP_);
	LOCK_CONTENDED(lock, do_raw_spin_trylock, do_raw_spin_lock);
}
```

这里有一点需要注意，**`spin_lock_irq` 只会关闭本地中断，其他 CPU 还是可以响应中断**，如果 CPU0 持有该自旋锁，而 CPU1 响应中断且中断处理程序也申请该自旋锁，那么等 CPU0 释放后即可获取。

### MCS 锁

在一个锁争用激烈的系统中，**所有等待自旋锁的线程都在同一个共享变量上自旋**，申请和释放锁也都在一个变量上修改，cache 一致性原理导致参与自旋的 CPU 的 cache 行无效，也可能导致 cache 颠簸现象（这种现象是怎样发现的，又是如何确定其和自旋锁有关），导致系统性能降低。

MCS 算法可以解决 cache 颠簸问题，其关键思想是**每个锁的申请者只在本地 CPU 的变量上自旋，而不是全局变量上**（那全局变量怎么办？），各个 CPU 会等待其他 CPU 将锁传给自己，即每个持有锁的 CPU 在释放锁时会将 next->locked 设为 1。OSQ 锁是 MCS 锁机制的一种实现，而后来内核又引进了基于 MCS 机制的排队自旋锁，后面介绍。我开始以为实现了 qspinlock 后内核应该只会使用 qspinlock，其实不是， osq 也有使用，大部分是在互斥锁中使用。

```plain
#0  osq_lock (lock=lock@entry=0xffffffff83014a2c <kernfs_open_file_mutex+12>) at kernel/locking/osq_lock.c:91
#1  0xffffffff81c157fd in mutex_optimistic_spin (waiter=0x0 <fixed_percpu_data>, ww_ctx=0x0 <fixed_percpu_data>,
    lock=0xffffffff83014a20 <kernfs_open_file_mutex>) at kernel/locking/mutex.c:453
#2  __mutex_lock_common (use_ww_ctx=false, ww_ctx=0x0 <fixed_percpu_data>, ip=<optimized out>,
    nest_lock=0x0 <fixed_percpu_data>, subclass=0, state=2, lock=0xffffffff83014a20 <kernfs_open_file_mutex>)
    at kernel/locking/mutex.c:599
#3  __mutex_lock (lock=lock@entry=0xffffffff83014a20 <kernfs_open_file_mutex>, state=state@entry=2,
    ip=<optimized out>, nest_lock=0x0 <fixed_percpu_data>, subclass=0) at kernel/locking/mutex.c:729
#4  0xffffffff81c15933 in __mutex_lock_slowpath (lock=lock@entry=0xffffffff83014a20 <kernfs_open_file_mutex>)
    at kernel/locking/mutex.c:979
#5  0xffffffff81c15972 in mutex_lock (lock=lock@entry=0xffffffff83014a20 <kernfs_open_file_mutex>)
    at kernel/locking/mutex.c:280
#6  0xffffffff813e9fa3 in kernfs_put_open_node (of=of@entry=0xffff8881024fc780, kn=<optimized out>)
    at fs/kernfs/file.c:580
#7  0xffffffff813ea05a in kernfs_fop_release (inode=0xffff8881063dea80, filp=0xffff888102345a00)
    at fs/kernfs/file.c:760
#8  0xffffffff8132a19f in __fput (file=0xffff888102345a00) at fs/file_table.c:280
#9  0xffffffff8132a3ce in ____fput (work=<optimized out>) at fs/file_table.c:313
#10 0xffffffff810c94d0 in task_work_run () at kernel/task_work.c:164
#11 0xffffffff8113bce1 in tracehook_notify_resume (regs=0xffffc90000513f58) at ./include/linux/tracehook.h:189
#12 exit_to_user_mode_loop (ti_work=<optimized out>, regs=<optimized out>) at kernel/entry/common.c:175
#13 exit_to_user_mode_prepare (regs=0xffffc90000513f58) at kernel/entry/common.c:207
#14 0xffffffff81c0b017 in __syscall_exit_to_user_mode_work (regs=regs@entry=0xffffc90000513f58)
    at kernel/entry/common.c:289
#15 syscall_exit_to_user_mode (regs=regs@entry=0xffffc90000513f58) at kernel/entry/common.c:300
#16 0xffffffff81c07128 in do_syscall_64 (regs=0xffffc90000513f58, nr=<optimized out>) at arch/x86/entry/common.c:86
#17 0xffffffff81e0007c in entry_SYSCALL_64 () at arch/x86/entry/entry_64.S:113
#18 0x00007ffca4a40c08 in ?? ()
```

#### optimistic_spin_node

OSQ 主要涉及到两个数据结构，

```c
/*
 * An MCS like lock especially tailored for optimistic spinning for sleeping
 * lock implementations (mutex, rwsem, etc).
 */
struct optimistic_spin_node { // 本地 CPU 上的节点
	struct optimistic_spin_node *next, *prev;
	int locked; /* 1 if lock acquired */
	int cpu; /* encoded CPU # + 1 value */
};

// 每个 CPU 上都有一个 MCS 节点
static DEFINE_PER_CPU_SHARED_ALIGNED(struct optimistic_spin_node, osq_node);

struct optimistic_spin_queue {
	/*
	 * Stores an encoded value of the CPU # of the tail node in the queue.
	 * If the queue is empty, then it's set to OSQ_UNLOCKED_VAL.
	 */
	atomic_t tail;
};
```

其初始化也很简单，

```c
#define OSQ_UNLOCKED_VAL (0)

/* Init macro and function. */
#define OSQ_LOCK_UNLOCKED { ATOMIC_INIT(OSQ_UNLOCKED_VAL) }

static inline void osq_lock_init(struct optimistic_spin_queue *lock)
{
	atomic_set(&lock->tail, OSQ_UNLOCKED_VAL);
}
```

其实整个 OSQ 实现并不复杂，毕竟代码才 200 多行。 `osq_lock` 用来申请 MCS 锁，其分为 3 种情况，我们一个个来看，

#### 快速申请通道

```c
bool osq_lock(struct optimistic_spin_queue *lock)
{
	struct optimistic_spin_node *node = this_cpu_ptr(&osq_node); // 首先获取该 CPU 上的 node
	struct optimistic_spin_node *prev, *next;
	int curr = encode_cpu(smp_processor_id()); // cpu_nr + 1
	int old;

	node->locked = 0;
	node->next = NULL;
	node->cpu = curr;

    // 如果 lock->tail 旧值为 OSQ_UNLOCKED_VAL，说明没有 CPU 持有锁
    // 这就是快速申请通道，否则进入中速通道
	old = atomic_xchg(&lock->tail, curr);
	if (old == OSQ_UNLOCKED_VAL)
		return true;

    ...
}
```

#### 中速申请通道

```c
bool osq_lock(struct optimistic_spin_queue *lock)
{
    ...

	prev = decode_cpu(old); // 当前持有该锁的 CPU
	node->prev = prev;

	/*
	 * osq_lock()			unqueue
	 *
	 * node->prev = prev		osq_wait_next()
	 * WMB				MB
	 * prev->next = node		next->prev = prev // unqueue-C
	 *
	 * Here 'node->prev' and 'next->prev' are the same variable and we need
	 * to ensure these stores happen in-order to avoid corrupting the list.
	 */
	smp_wmb();

	WRITE_ONCE(prev->next, node); // 将当前 node 插入到 MCS 链表中

	/*
	 * Normally @prev is untouchable after the above store; because at that
	 * moment unlock can proceed and wipe the node element from stack.
	 *
	 * However, since our nodes are static per-cpu storage, we're
	 * guaranteed their existence -- this allows us to apply
	 * cmpxchg in an attempt to undo our queueing.
	 */

	/*
	 * Wait to acquire the lock or cancellation. Note that need_resched()
	 * will come with an IPI, which will wake smp_cond_load_relaxed() if it
	 * is implemented with a monitor-wait. vcpu_is_preempted() relies on
	 * polling, be careful.
	 */
    // 这里应该是自旋等待，检查 node->locked 是否为 1
    // 因为 prev_node 释放锁时会把它的下一个节点中的 locked 成员设置为 1
    // 然后才能成功释放锁，这个后面会介绍
    // 这样感觉就是一个排队等待的过程
    // 在自旋等待的过程中，如果有更高优先级的进程抢占或者被调度器调度出去
    // 那么应该放弃自旋等待，跳转到 unqueue 将 node 节点从 MCS 链表中删去
	if (smp_cond_load_relaxed(&node->locked, VAL || need_resched() ||
				  vcpu_is_preempted(node_cpu(node->prev))))
		return true;

	...
}
```

#### 慢速申请通道

慢速通道主要是实现删除链表等操作，其过程和一般的双向编表删除类似，不过要使用原子操作，所以比较复杂，这里对其具体操作就不进一步分析，之后有需要再看。

```c
bool osq_lock(struct optimistic_spin_queue *lock)
{
    ...

/* unqueue */
	/*
	 * Step - A  -- stabilize @prev
	 *
	 * Undo our @prev->next assignment; this will make @prev's
	 * unlock()/unqueue() wait for a next pointer since @lock points to us
	 * (or later).
	 */

	for (;;) {
		/*
		 * cpu_relax() below implies a compiler barrier which would
		 * prevent this comparison being optimized away.
		 */
		if (data_race(prev->next) == node &&
		    cmpxchg(&prev->next, node, NULL) == node)
			break;

		/*
		 * We can only fail the cmpxchg() racing against an unlock(),
		 * in which case we should observe @node->locked becoming
		 * true.
		 */
		if (smp_load_acquire(&node->locked))
			return true;

		cpu_relax();

		/*
		 * Or we race against a concurrent unqueue()'s step-B, in which
		 * case its step-C will write us a new @node->prev pointer.
		 */
		prev = READ_ONCE(node->prev);
	}

	/*
	 * Step - B -- stabilize @next
	 *
	 * Similar to unlock(), wait for @node->next or move @lock from @node
	 * back to @prev.
	 */

	next = osq_wait_next(lock, node, prev);
	if (!next)
		return false;

	/*
	 * Step - C -- unlink
	 *
	 * @prev is stable because its still waiting for a new @prev->next
	 * pointer, @next is stable because our @node->next pointer is NULL and
	 * it will wait in Step-A.
	 */

	WRITE_ONCE(next->prev, prev);
	WRITE_ONCE(prev->next, next);

	return false;
}
```

#### 释放锁

```c
void osq_unlock(struct optimistic_spin_queue *lock)
{
	struct optimistic_spin_node *node, *next;
	int curr = encode_cpu(smp_processor_id());

	/*
	 * Fast path for the uncontended case.
	 */
    // 如果 lock->tail 保存的 CPU 编号正好是该进程的 CPU 编号
    // 说明没有 CPU 来竞争，直接释放
	if (likely(atomic_cmpxchg_release(&lock->tail, curr,
					  OSQ_UNLOCKED_VAL) == curr))
		return;

	/*
	 * Second most likely case.
	 */
	node = this_cpu_ptr(&osq_node);
	next = xchg(&node->next, NULL); // 返回旧值，然后 node->next == NULL
	if (next) {
		WRITE_ONCE(next->locked, 1); // 前面说到过，将 next->locked 设为 1
		return;
	}

    // 如果后继节点为空，说明在执行 osq_unlock 时有成员擅自离队（？）
    // 调用 osq_wait_next 来确定或等待后继节点
	next = osq_wait_next(lock, node, NULL);
	if (next)
		WRITE_ONCE(next->locked, 1);
}
```

### 排队自旋锁

上面介绍的 MCS 锁能够解决 cache 颠簸问题（在每个 CPU 自己的变量上自旋），但是其会使 spinlock 数据结构变大，而 spinlock 在内核中的应用十分广泛，有些地方对数据结构的大小十分敏感，所以 MCS 机制一直没有应用到 spinlock 中，spinlock 也一直没有解决 cache 颠簸问题，直到排队自旋锁出现。

#### qspinlock

首先来看看数据结构（在经典自旋锁部分已经给出过），

```c
typedef struct qspinlock {
	union {
		atomic_t val; // 前面介绍的原子变量

		/*
		 * By using the whole 2nd least significant byte for the
		 * pending bit, we can allow better optimization of the lock
		 * acquisition for the pending bit holder.
		 */
#ifdef __LITTLE_ENDIAN
		struct {
			u8	locked;
			u8	pending;
		};
		struct {
			u16	locked_pending;
			u16	tail;
		};
#else
	... // 分为大小端
#endif
	};
} arch_spinlock_t;
```

`qspinlock` 将 val 字段分成多个域，

| 位      | 描述                                                         |
| ------- | ------------------------------------------------------------ |
| 0 - 7   | locked 域，表示持有锁                                        |
| 8       | pending 域，表示第一顺位继承者，自旋等待锁释放，pending 是 8 bit，但只有第一位使用了 |
| 9 - 15  | 保留位                                                       |
| 16 - 17 | tail_idx 域，用来获取 q_nodes，目前支持 4 种上下文的 msc_nodes —— 进程上下文、软中断上下文、硬中断上下文和不可屏蔽中断上下文（电源掉电、总线奇偶位出错，触发出错中断等） |
| 18 - 31 | tail_cpu 域，用来标识等待队列末尾的 CPU                      |

内核使用一个三元组 {x, y, z} 来表示锁的状态，`x` 表示 tail，`y` 表示 pending，`z` 表示 locked。

#### 快速申请通道

申请 `qspinlock` 的接口为 `queued_spin_lock`，

```c
static __always_inline void queued_spin_lock(struct qspinlock *lock)
{
	int val = 0;

    // 原子的比较和交换
    // 如果 lock->val = val = 0，两者相等，说明自旋锁没有被持有，可以赋值
    // 这时将 lock->val 置成 _Q_LOCKED_VAL
    // 如果 local->val != val，说明自旋锁被持有了，无法赋值
    // 这是 val 表示 lock->val 的值
    // 这就是快速路径，不然就是慢速路径
    // _Q_LOCKED_VAL = 1
	if (likely(atomic_try_cmpxchg_acquire(&lock->val, &val, _Q_LOCKED_VAL))) // _Q_LOCKED_VAL = 0
		return;

	queued_spin_lock_slowpath(lock, val);
}

static __always_inline bool
arch_atomic_try_cmpxchg_acquire(atomic_t *v, int *old, int new)
{
	int r, o = *old;
    // 如果比较后相等，r 就是 0
    // 如果比较后不相等，r 就是 lock->val 当前值
	r = arch_atomic_cmpxchg_acquire(v, o, new);
	if (unlikely(r != o))
		*old = r; // 不相等的话返回的 val 就是 lock->val 当前值，后面慢路径会使用
	return likely(r == o);
}
#define arch_atomic_try_cmpxchg_acquire arch_atomic_try_cmpxchg_acquire
```

我们以一个实际的场景来分析 `qspinlock` 的使用，假设 CPU0，CPU1，CPU2，CPU3 都在进程上下文中争用一个自旋锁。

在开始时，没有 CPU 持有锁，那么 CPU0 通过 `queued_spin_lock` 去申请该锁，这时在 `lock->val` 中，locked = 1，pending = 0，tail = 0，即**三元组为 {0, 0, 1}**。

![qspinlock.png](https://github.com/UtopianFuture/UtopianFuture.github.io/blob/master/image/qspinlock.png?raw=true)

#### 中速申请通道

当快速路径无法获取锁时，进入中速通道，`queued_spin_lock_slowpath` 很复杂，注释也很详细。

```c
/**
 * queued_spin_lock_slowpath - acquire the queued spinlock
 * @lock: Pointer to queued spinlock structure
 * @val: Current value of the queued spinlock 32-bit word
 *
 * (queue tail, pending bit, lock value)
 *
 *              fast     :    slow                                  :    unlock
 *                       :                                          :
 * uncontended  (0,0,0) -:--> (0,0,1) ------------------------------:--> (*,*,0)
 *                       :       | ^--------.------.             /  :
 *                       :       v           \      \            |  :
 * pending               :    (0,1,1) +--> (0,1,0)   \           |  :
 *                       :       | ^--'              |           |  :
 *                       :       v                   |           |  :
 * uncontended           :    (n,x,y) +--> (n,0,0) --'           |  :
 *   queue               :       | ^--'                          |  :
 *                       :       v                               |  :
 * contended             :    (*,x,y) +--> (*,0,0) ---> (*,0,1) -'  :
 *   queue               :         ^--'                             :
 */
void queued_spin_lock_slowpath(struct qspinlock *lock, u32 val)
{
	struct mcs_spinlock *prev, *next, *node;
	u32 old, tail;
	int idx;

	...

	/*
	 * Wait for in-progress pending->locked hand-overs with a bounded
	 * number of spins so that we guarantee forward progress.
	 *
	 * 0,1,0 -> 0,0,1
	 */
 	// 如果当前 spinlock 只有 pending 域被置位了，说明 spinlock 正处于一个临时状态
 	// 即锁持有者正在释放锁，但是 pending owner 还没有拾取锁
	// 存在这种情况，只一个竞争者在等待锁，三元组为 {0,1,1}
 	// 这时如果锁的持有者释放了锁，三元组变成了 {0,1,0}
 	// 这时 pending owner 还没有获取锁，又有其他的竞争者来申请锁
 	// 新来到的竞争者不能直接成为 pending owner，而是要等待上一位 pending owner
 	// 获取锁，它才能成为 pending owner
 	// 这种情况需要重读 spinlock，重读会进行 _Q_PENDING_LOOPS 次
 	// 如果还没有获取到，那么放弃读
 	// 大量并发的场景下存在这种情况
	if (val == _Q_PENDING_VAL) { // _Q_PENDING_VAL = 1 << 8
		int cnt = _Q_PENDING_LOOPS;
		val = atomic_cond_read_relaxed(&lock->val,
					 (VAL != _Q_PENDING_VAL) || !cnt--);
	}
 	// 为何这里我们要这么执着的等待 pending owner 线程将其状态从 pending 迁移到 locked 状态呢？
 	// 因为我们不想排队。如果带着 pending bit 往下走，在下面 if (val & ~_Q_LOCKED_MASK) 就会直接接入排队过程
 	// 一旦进入排队过程，我们就需要在 mcs lock 上
 	// 如果 pending owner 获取了锁，它会 clear pending 位
 	// 这样我们就成为第一个等锁线程，也就不需要排队，变成 pending owner 线程
 	// 理论上 owner 释放锁和 pending owner 获取锁应该很快
 	// 但是当竞争比较激烈的时候，难免会出现长时间的 pending bit 被设定的情况（这是什么场景？）
 	// 因此这里也不适合死等，我们设置 _Q_PENDING_LOOPS 等于 1，即重读一次就 OK 了

	/*
	 * If we observe any contention; queue.
	 */
 	// _Q_LOCKED_MASK = 0xff，判断 pending 和 tail 域是否为 1
 	// 如果 pending 为 1，说明已经有第一顺位继承者，只能排队等待
	if (val & ~_Q_LOCKED_MASK)
		goto queue; // 排队

	/*
	 * trylock || pending
	 *
	 * 0,0,* -> 0,1,* -> 0,0,1 pending, trylock
	 */
 	// 此时三元组从 {0,0,1} 变成 {0,1,1}
	val = queued_fetch_set_pending_acquire(lock); // 第一顺位继承者在这里设置 pending 域

	/*
	 * If we observe contention, there is a concurrent locker.
	 *
	 * Undo and queue; our setting of PENDING might have made the
	 * n,0,0 -> 0,0,0 transition fail and it will now be waiting
	 * on @next to become !NULL.
	 */
 	// 为什么这里还要判断一次？
 	// 上面返回的 val 是 lock 中的旧值，这里再判断一次是因为可能有多个申请者并发申请这个锁
 	// 其他的申请者已经设置了 pending 域
 	// 这种情况下，我们需要去排队或 undo pending
 	// 然后重新去排队
 	// 正常来说，set_pending 之前，三元组应该是 {0,0,1}
 	// 这里 tail 或 pending 位不为 0
 	// 说明已经被其他申请者修改了
 	// 要么 pending 位是 1，表示有 CPU 在自旋等待在锁上
 	// 要么 pending 位是 0，但是 tail 不是 0，表示还有 CPU 在自旋等待
 	// 在这之前肯定还有一个 CPU 抢到过 pending 位，但现在释放了
 	// 但是 lock 是 atomic 变量，按理来说不应该发生这种情况啊？
 	// 或者用一个 cmpxchg 命令来做，就不会出现这种情况了
 	// 那哪里会更改 pending 位呢？
	if (unlikely(val & ~_Q_LOCKED_MASK)) {

		/* Undo PENDING if we set it. */
 	// pending 位是 0 的话，说明这个 pending 位被当前 CPU 误“抢”了
 	// 原来的状态是(n, 0, *)，队列不为空，为了保证自旋锁的次序，本 CPU 不能“抢” pending 位
 	// 所以要将 pending 位重新置为 0
		if (!(val & _Q_PENDING_MASK)) // 这里判断锁是否已经被占用
			clear_pending(lock);

		goto queue;
	}

	/*
	 * We're pending, wait for the owner to go away.
	 *
	 * 0,1,1 -> 0,1,0
	 *
	 * this wait loop must be a load-acquire such that we match the
	 * store-release that clears the locked bit and create lock
	 * sequentiality; this is because not all
	 * clear_pending_set_locked() implementations imply full
	 * barriers.
	 */
 	// 自旋等待，一直原子的加载和判断条件是否成立，即 locked 域是否清 0
 	// 这里自旋并不是轮询，而是通过 WFE 指令让 CPU 停下来，降低功耗
 	// WFI(Wait for interrupt)和 WFE(Wait for event)是两个让 ARM 核进入 low-power standby 模式的指令
 	// 由 ARM architecture 定义，ARM core 实现
	if (val & _Q_LOCKED_MASK)
		atomic_cond_read_acquire(&lock->val, !(VAL & _Q_LOCKED_MASK)); // CPU1 自旋等待 CPU0 释放锁

	/*
	 * take ownership and clear the pending bit.
	 * 0,1,0 -> 0,0,1
	 */
	clear_pending_set_locked(lock); // CPU1 成功持有该锁
	lockevent_inc(lock_pending);
	return;

 ...
}
```

看图更加直观，

![qspinlock-2.png](https://github.com/UtopianFuture/UtopianFuture.github.io/blob/master/image/qspinlock-2.png?raw=true)

#### 慢速申请通道

慢速通道的场景是 **CPU0 持有锁，CPU1 设置 pending 域并自旋等待，这时 CPU2 也申请该自旋锁**，这时就是跳转到 queue 处，

```c
void queued_spin_lock_slowpath(struct qspinlock *lock, u32 val)
{
	struct mcs_spinlock *prev, *next, *node;
	u32 old, tail;
	int idx;

	...

	/*
	 * End of pending bit optimistic spinning and beginning of MCS
	 * queuing.
	 */
queue:
	lockevent_inc(lock_slowpath); // 记录慢路径次数
pv_queue:
    // 获取各个 CPU 的 mcs 队列
    // 这里 qnodes 是 Per-CPU 变量，也就是说，每个 CPU 自旋在自己的 mcs lock 上
    // 而不是公共的 spinlock 上
	node = this_cpu_ptr(&qnodes[0].mcs);
	idx = node->count++; // 该 mcs 锁的嵌套次数
	tail = encode_tail(smp_processor_id(), idx); // 根据 CPU id 和申请者个数构造 tail

	/*
	 * 4 nodes are allocated based on the assumption that there will
	 * not be nested NMIs taking spinlocks. That may not be true in
	 * some architectures even though the chance of needing more than
	 * 4  nodes will still be extremely unlikely. When that happens,
	 * we fall back to spinning on the lock directly without using
	 * any MCS node. This is not the most elegant solution, but is
	 * simple enough.
	 */
    // 如果获得的节点下标大于等于 4，表示当前 CPU 上自旋锁嵌套层数已经超过了 4
    // 这种情况极少出现，要么就是哪里用错了，要么就是当前平台上还支持不可屏蔽（NMI）中断的嵌套调用
    // 这个嵌套次数有点难以理解
    // 首先由于 spinlock 是忙等，所以在自选等待的时候该 CPU 不会有其他的进程来竞争该锁
    // 忙等，禁止睡眠，其他的进程不会得到调度
    // 但是中断的优先级比进程高，而 spinlock 可以在中断上下文使用
    // 所以该 CPU 上某个进程在 msc lock 上自旋等待的时候会被中断抢占
    // 中断有 3 种：软中断上下文、硬中断上下文和不可屏蔽中断上下文
    // 加上进程上下文，总共 4 种，所以这里 MAX_NODES = 4
	if (unlikely(idx >= MAX_NODES)) {
		lockevent_inc(lock_no_node);
        // 如果自旋锁嵌套层数操作 4 层，退化成普通自旋锁
        // 不断调用 queued_spin_trylock，直到获取该锁
        // 那为何嵌套 4 次就需要退化成普通自旋锁呢？
		while (!queued_spin_trylock(lock))
			cpu_relax();
		goto release;
	}

	node = grab_mcs_node(node, idx); // 根据 idx 获得对应的 msc 锁，qnodes 是指针数组，里面有 4 个元素
	// 重排到这后面累加，计数出错

	/*
	 * Keep counts of non-zero index values:
	 */
	lockevent_cond_inc(lock_use_node2 + idx - 1, idx);

	/*
	 * Ensure that we increment the head node->count before initialising
	 * the actual node. If the compiler is kind enough to reorder these
	 * stores, then an IRQ could overwrite our assignments.
	 */
    // 在同一个 CPU 上，最多可以有 4 个不同的自旋锁嵌套调用，嵌套的层数是由 CPU 上第一个节点的 count 域记录的
    // 这个域会在队列模式开头进行读取和累加操作，这个操作虽然写在一行代码里，但却不是一步就会完成了
    // 而是可以分解为读取和累加两个操作。读取操作会读出节点下标值，这个值后面会用来计算 tail 的值
    // 和后面的操作相关，因此编译器不会对其进行重排序。但是，累加操作就不一样了，它和后面的操作没有什么相关性
    // 很有可能被编译器优化到任何位置，因此如果没有编译屏障的话，代码可能被重排
    // 重排之后可能在获取新的 node 之后再累加，这样计数就出错了
	barrier();

    // 获得当前 CPU 上对应的节点后，要将其初始化，locked 域设置成 0（会自旋等待其变成 1）
    // 设置为 0 表示当前 CPU 的 mcs_lock 没有持有锁
    // next 域设置成 NULL（表示是最后一个节点）
	node->locked = 0;
	node->next = NULL;
	pv_init_node(node); // NOP

	/*
	 * We touched a (possibly) cold cacheline in the per-cpu queue node;
	 * attempt the trylock once more in the hope someone let go while we
	 * weren't watching.
	 */
    // 先再尝试一下获得自旋锁，因为当前经过上面这些操作后，有可能获得和等待该自旋锁的 CPU 都已经释放了
    // 当前的自旋锁是空闲的状态。这种情况下，当前 CPU 就可以轻易获得自旋锁，可以一定程度上提高效率
    // 这是一个优化，当然也可以坚持走队列模式，功能上说也是正常的
	if (queued_spin_trylock(lock))
		goto release;

	/*
	 * Ensure that the initialisation of @node is complete before we
	 * publish the updated tail via xchg_tail() and potentially link
	 * @node into the waitqueue via WRITE_ONCE(prev->next, node) below.
	 */
    // 保证将当前节点更新到自旋锁之前，一定要保证这个自旋锁节点已经完成了初始化
    // 也就是说，可以保证，当本 CPU 通过 xchg_tail 函数读到上一个节点的时候，它一定已经初始化好了
	smp_wmb();

	/*
	 * Publish the updated tail.
	 * We have already touched the queueing cacheline; don't bother with
	 * pending stuff.
	 *
	 * p,*,* -> n,*,*
	 */
    // 原子的读取自旋锁的 tail 域，并将自己节点更新上去
    // tail 表示最新添加得锁竞争者，可用于后续判断是否有新的竞争者
	old = xchg_tail(lock, tail);
	next = NULL;

	/*
	 * if there was a previous node; link it and wait until reaching the
	 * head of the waitqueue.
	 */
	if (old & _Q_TAIL_MASK) { // 如果之前队列不为空
        // 获取前一个节点
        // 如果当前 CPU 发现在将自己的节点更新到自选锁之前，自旋锁值的 tail 域不是 0 的话
        // 那么说明当前节点前面应该还有别的 CPU 上的节点也在等待自旋锁，这时候就需要更新前一个节点的 next 域
        // 让其指向本节点，从而将队列链接完整。要获得前一个节点的指针，需要对锁的 16 位 tail 域的值进行“解码”
		prev = decode_tail(old);

		/* Link @node into the waitqueue. */
		WRITE_ONCE(prev->next, node); // 将当前节点连接到等待队列中

		pv_wait_node(node, prev);
        // 自旋等待在自己的 mcs lock 上，等待本节点的 lock 域变为 1
		arch_mcs_spin_lock_contended(&node->locked); // CPU3 自旋等待 mcs lock 的 locked 域被置 1

		/*
		 * While waiting for the MCS lock, the next pointer may have
		 * been set by another lock waiter. We optimistically load
		 * the next pointer & prefetch the cacheline for writing
		 * to reduce latency in the upcoming MCS unlock operation.
		 */
        // 执行到这里，说明我们已经获得了 mcs lock，处于队首
        // 在等待的过程中，其他的申请者也加入到链表中
        // 更新 next，把 msc lock 让给下一个申请者
		next = READ_ONCE(node->next); // 尝试读取下一个节点
		if (next)
			prefetchw(next);
	}

	/*
	 * we're at the head of the waitqueue, wait for the owner & pending to
	 * go away.
	 *
	 * *,x,y -> *,0,0
	 *
	 * this wait loop must use a load-acquire such that we match the
	 * store-release that clears the locked bit and create lock
	 * sequentiality; this is because the set_locked() function below
	 * does not imply a full barrier.
	 *
	 * The PV pv_wait_head_or_lock function, if active, will acquire
	 * the lock and return a non-zero value. So we have to skip the
	 * atomic_cond_read_acquire() call. As the next PV queue head hasn't
	 * been designated yet, there is no way for the locked value to become
	 * _Q_SLOW_VAL. So both the set_locked() and the
	 * atomic_cmpxchg_relaxed() calls will be safe.
	 *
	 * If PV isn't active, 0 will be returned instead.
	 *
	 */
	if ((val = pv_wait_head_or_lock(lock, node)))
		goto locked;

    // 在 spinlock 上自旋等待
    // CPU2 自旋等待 locked 和 pending 域清零
    // 这里三元组为 {x,1,1}
	val = atomic_cond_read_acquire(&lock->val, !(VAL & _Q_LOCKED_PENDING_MASK));

// 当前 CPU 已经等到了前面所有 CPU 都释放了自旋锁，可以合法占有自旋锁
// 这里三元组为 {n,0,0}
locked:
	/*
	 * claim the lock:
	 *
	 * n,0,0 -> 0,0,1 : lock, uncontended
	 * *,*,0 -> *,*,1 : lock, contended
	 *
	 * If the queue head is the only one in the queue (lock value == tail)
	 * and nobody is pending, clear the tail code and grab the lock.
	 * Otherwise, we only need to grab the lock.
	 */

	/*
	 * In the PV case we might already have _Q_LOCKED_VAL set, because
	 * of lock stealing; therefore we must also allow:
	 *
	 * n,0,1 -> 0,0,1
	 *
	 * Note: at this point: (val & _Q_PENDING_MASK) == 0, because of the
	 *       above wait condition, therefore any concurrent setting of
	 *       PENDING will make the uncontended transition fail.
	 */
    // 如果当前节点已经是队列中的最后一个节点
	if ((val & _Q_TAIL_MASK) == tail) {
        // 尝试将自旋锁的值修改成(0, 0, 1)
    	// 返回 1 表示没有竞争发生，修改成功，直接撤销该节点就行了；
    	// 如果返回 0，表示有竞争的情况发生，也就是说从上面自旋等待锁(n, 0, 0)，
        // 到正式将其修改为(0, 0, 1)之间，有别的 CPU 已经修改了自旋锁的状态（哪怕只隔了一行代码）
        // 代码还要接着往下走
        // 正常来说，val 是上面 atomic_cond_read_acquire 命令读出来的，也就是 lock->val
        // 这里 cmpxchg 里面两者应该相等
        // 不相等说明有人更新了 tail，即有新的竞争者申请锁
        // 这样还不能立即持有锁
		if (atomic_try_cmpxchg_relaxed(&lock->val, &val, _Q_LOCKED_VAL))
			goto release; /* No contention */
	}

	/*
	 * Either somebody is queued behind us or _Q_PENDING_VAL got set
	 * which will then detect the remaining tail and queue behind us
	 * ensuring we'll see a @next.
	 */
    // 将自旋锁的 locked 域变成 1，表示当前 CPU 已经占有了这个自旋锁
	set_locked(lock);

	/*
	 * contended path; wait for next if not observed yet, release.
	 */
    // 自旋等待当前节点的 next 域变为非 NULL，等待下一个节点“出现”。因为如果发生了竞争
    // 就一定意味着有一个新节点会“出现”在后面
	if (!next)
		next = smp_cond_load_relaxed(&node->next, (VAL));

    // 将下一个节点的 locked 域设置成 1
    // 这样上面的 arch_mcs_spin_lock_contended(&node->locked); 在自己的 mcs lock 上自选等待的申请者
    // 就可以继续往下走了
	arch_mcs_spin_unlock_contended(&next->locked);
	pv_kick_node(lock, next);

release:
	/*
	 * release the node
	 */
	__this_cpu_dec(qnodes[0].mcs.count);
}
```

#### 释放锁

释放锁很简单，只要原子的将 locked 域清零。

```c
static __always_inline void queued_spin_unlock(struct qspinlock *lock)
{
	/*
	 * unlock() needs release semantics:
	 */
	smp_store_release(&lock->locked, 0);
}
```

我们来看看内核是怎样使用 spin_lock 的。

关于 qspinlock 貌似还有个[坑](https://zhuanlan.zhihu.com/p/3661545891)，可以深入研究一下。

```c
#define __CMPWAIT_CASE(w, sfx, sz)					\
static inline void __cmpwait_case_##sz(volatile void *ptr,		\
				       unsigned long val)		\
{									\
	unsigned long tmp;						\
									\
	asm volatile(							\
	"	sevl\n"							\
	"	wfe\n"							\
	"	ldxr" #sfx "\t%" #w "[tmp], %[v]\n"			\
	"	eor	%" #w "[tmp], %" #w "[tmp], %" #w "[val]\n"	\
	"	cbnz	%" #w "[tmp], 1f\n"				\
	"	wfe\n"							\
	"1:"								\
	: [tmp] "=&r" (tmp), [v] "+Q" (*(unsigned long *)ptr)		\
	: [val] "r" (val));						\
}

__CMPWAIT_CASE(w, b, 8);
__CMPWAIT_CASE(w, h, 16);
__CMPWAIT_CASE(w,  , 32);
__CMPWAIT_CASE( ,  , 64);
```

### 信号量

信号量最早接触是在本科的时候学操作系统的进程同步，PV 操作，它的经典使用场景是生产者和消费者问题。它和自旋锁的区别是自旋锁是忙等，能够在中断上下文中使用，而信号量允许进程睡眠，不能在中断上下文中使用。它的实现比自旋锁简单很多，

#### semaphore

```c
struct semaphore {
	raw_spinlock_t		lock; // 用自旋锁来保护 count 和 wait_list
	unsigned int		count; // 表示允许进入临界区的内核执行路径个数
	struct list_head	wait_list; // 在该信号量上睡眠等待的进程
};
```

PV 操作对应内核中的 `down()` 和 `up()` 函数。其中 `down()`  还有 `down_interruptible()` 变体，其在争用信号量失败时会让进程进入可中断睡眠状态，而 `down()` 则是进入不可中断睡眠状态。

#### 关键函数 down

```c
int down_interruptible(struct semaphore *sem)
{
	unsigned long flags;
	int result = 0;

	might_sleep();
	raw_spin_lock_irqsave(&sem->lock, flags); // 在自旋锁临界区操作
	if (likely(sem->count > 0)) // 很清晰，可以申请该信号量
		sem->count--;
	else // 不可申请，去睡眠
		result = __down_interruptible(sem);
	raw_spin_unlock_irqrestore(&sem->lock, flags);

	return result;
}
```

`__down_interruptible` 是 `__down_common` 的封装。

```c
static noinline void __sched __down(struct semaphore *sem) // 这两者设置的状态不同
{
	__down_common(sem, TASK_UNINTERRUPTIBLE, MAX_SCHEDULE_TIMEOUT);
}

static noinline int __sched __down_interruptible(struct semaphore *sem)
{
	return __down_common(sem, TASK_INTERRUPTIBLE, MAX_SCHEDULE_TIMEOUT);
}

static inline int __sched __down_common(struct semaphore *sem, long state,
								long timeout)
{
	struct semaphore_waiter waiter;

	list_add_tail(&waiter.list, &sem->wait_list);
	waiter.task = current; // 描述该进程
	waiter.up = false;

	for (;;) {
		if (signal_pending_state(state, current))
			goto interrupted;
		if (unlikely(timeout <= 0))
			goto timed_out;
		__set_current_state(state);
		raw_spin_unlock_irq(&sem->lock); // 因为自旋锁是不能睡眠的，所以要释放
		timeout = schedule_timeout(timeout); // 主动申请调度，让出 CPU
		raw_spin_lock_irq(&sem->lock); // 这里再申请
		if (waiter.up) // 被 UP 操作唤醒
			return 0;
	}

 timed_out:
	list_del(&waiter.list);
	return -ETIME;

 interrupted:
	list_del(&waiter.list);
	return -EINTR;
}
```

#### 关键函数 up

然后再看看 `up()`，

```c
void up(struct semaphore *sem)
{
	unsigned long flags;

	raw_spin_lock_irqsave(&sem->lock, flags);
	if (likely(list_empty(&sem->wait_list))) // 一样的操作，没有进程在等待的话直接加 1
		sem->count++;
	else
		__up(sem);
	raw_spin_unlock_irqrestore(&sem->lock, flags);
}

static noinline void __sched __up(struct semaphore *sem)
{
	struct semaphore_waiter *waiter = list_first_entry(&sem->wait_list,
						struct semaphore_waiter, list); // 同样是排队
	list_del(&waiter->list);
	waiter->up = true; // 看，在这里设置为 true
	wake_up_process(waiter->task); // 然后唤醒等待的进程，即将进程的状态设置为 TASK_NORMAL
}
```

信号量的一个特点是允许有任意数量的锁持有者，`sema_init` 就是信号量的初始化函数，

```c
#define __SEMAPHORE_INITIALIZER(name, n)				\
{									\
	.lock		= __RAW_SPIN_LOCK_UNLOCKED((name).lock),	\
	.count		= n,						\
	.wait_list	= LIST_HEAD_INIT((name).wait_list),		\
}

#define DEFINE_SEMAPHORE(name)	\
	struct semaphore name = __SEMAPHORE_INITIALIZER(name, 1)

static inline void sema_init(struct semaphore *sem, int val)
{
	static struct lock_class_key __key;
	*sem = (struct semaphore) __SEMAPHORE_INITIALIZER(*sem, val);
	lockdep_init_map(&sem->lock.dep_map, "semaphore->lock", &__key, 0);
}
```

当 count > 1 时，我们称之为计数信号量；当 count == 1 时，我们称之为互斥信号量，在内核中，大多使用 count == 1 的信号量（在内核初始化过程中没有用到 semaphore），其适用于情况复杂，加锁时间较长的应用场景。

### 互斥锁

互斥锁类似于 count == 1 的信号量，那为何还要重新开发 mutex？

互斥锁相比于信号量要简单轻便一些，同时在锁争用激烈的测试场景下，互斥锁比信号量执行速度更快，可扩展性更好。我们从代码上实际看看是怎么回事。

#### mutex

```c
struct mutex { // 和信号量类似
	atomic_long_t		owner; // 0 表示该锁没有被持有，非 0 表示持有者的 task_struct 指针
	raw_spinlock_t		wait_lock;
#ifdef CONFIG_MUTEX_SPIN_ON_OWNER
	struct optimistic_spin_queue osq; /* Spinner MCS lock */
#endif
	struct list_head	wait_list;

    ... // 从数据结构上来看并没有轻便啊

};
```

互斥锁实现了乐观自旋等待机制（其实就是自旋等待），其核心原理是**当发现锁持有者在临界区执行并且没有其他高优先级进程调度时，当前进程坚信锁持有者会很快离开临界区并释放锁，因此不睡眠等待，从而减少睡眠和唤醒的开销（这里应该就是更轻便的地方，semaphore 会调用 timeout_schedule 让出 CPU）**。其实这个自旋等待也就是 MCS 锁。

#### 快速申请通道

```c
void __sched mutex_lock(struct mutex *lock)
{
	might_sleep();

    // 快速通道
    // 这里其实很 hacking 的，暂时不分析
    // 和前面一样，检查锁是否被持有
    // 即 lock->owner 是否为 0，前面讲到 owner 指向持有进程的 task_struct 地址
    // unsigned long curr = (unsigned long)current;
	if (!__mutex_trylock_fast(lock))
		__mutex_lock_slowpath(lock);
}
```

#### 慢速申请通道

我们结合流程图看，加深印象。

![acquire_mutex_slow.png](https://github.com/UtopianFuture/UtopianFuture.github.io/blob/master/image/acquire_mutex_slow.png?raw=true)

这里还涉及到乐观自旋等待，下次再分析。

```c
__mutex_lock_slowpath -> __mutex_lock -> __mutex_lock_common

static __always_inline int __sched
__mutex_lock_common(struct mutex *lock, unsigned int state, unsigned int subclass,
		    struct lockdep_map *nest_lock, unsigned long ip,
		    struct ww_acquire_ctx *ww_ctx, const bool use_ww_ctx)
{
	struct mutex_waiter waiter;
	struct ww_mutex *ww; // 这个数据结构是用来干嘛的
	int ret;

	...

	preempt_disable();
	mutex_acquire_nest(&lock->dep_map, subclass, 0, nest_lock, ip);

	if (__mutex_trylock(lock) || // 这个函数就是不断的检查持有锁的进程是否就是当前进程
	    mutex_optimistic_spin(lock, ww_ctx, NULL)) {
		/* got the lock, yay! */
		lock_acquired(&lock->dep_map, ip);
		if (ww_ctx)
			ww_mutex_set_context_fastpath(ww, ww_ctx);
		preempt_enable();
		return 0;
	}

	raw_spin_lock(&lock->wait_lock);
	/*
	 * After waiting to acquire the wait_lock, try again.
	 */
	if (__mutex_trylock(lock)) { // 不理解
		if (ww_ctx)
			__ww_mutex_check_waiters(lock, ww_ctx);

		goto skip_wait;
	}

	debug_mutex_lock_common(lock, &waiter);
	waiter.task = current; // 构建一个 waiter 插入到等待链表中
	if (use_ww_ctx)
		waiter.ww_ctx = ww_ctx;

	lock_contended(&lock->dep_map, ip);

	if (!use_ww_ctx) {
		/* add waiting tasks to the end of the waitqueue (FIFO): */
		__mutex_add_waiter(lock, &waiter, &lock->wait_list);
	} else {
		/*
		 * Add in stamp order, waking up waiters that must kill
		 * themselves.
		 */
		ret = __ww_mutex_add_waiter(&waiter, lock, ww_ctx);
		if (ret)
			goto err_early_kill;
	}

    // 这里都设置进程状态为(不)可中断睡眠了，为什么后面还有自旋等待呢？
    // 应该是不断的睡眠，不断的被调度唤醒，直到能获取锁
    // 确定这样能更轻便么？反复睡眠唤醒
	set_current_state(state);
	for (;;) {
		bool first;

		/*
		 * Once we hold wait_lock, we're serialized against
		 * mutex_unlock() handing the lock off to us, do a trylock
		 * before testing the error conditions to make sure we pick up
		 * the handoff.
		 */
		if (__mutex_trylock(lock))
			goto acquired;

		/*
		 * Check for signals and kill conditions while holding
		 * wait_lock. This ensures the lock cancellation is ordered
		 * against mutex_unlock() and wake-ups do not go missing.
		 */
		if (signal_pending_state(state, current)) {
			ret = -EINTR;
			goto err;
		}

		if (ww_ctx) {
			ret = __ww_mutex_check_kill(lock, &waiter, ww_ctx);
			if (ret)
				goto err;
		}

		raw_spin_unlock(&lock->wait_lock);
		schedule_preempt_disabled();

		first = __mutex_waiter_is_first(lock, &waiter);

		set_current_state(state);
		/*
		 * Here we order against unlock; we must either see it change
		 * state back to RUNNING and fall through the next schedule(),
		 * or we must see its unlock and acquire.
		 */
		if (__mutex_trylock_or_handoff(lock, first) ||
		    (first && mutex_optimistic_spin(lock, ww_ctx, &waiter)))
			break;

		raw_spin_lock(&lock->wait_lock);
	}
	raw_spin_lock(&lock->wait_lock);
acquired:
	__set_current_state(TASK_RUNNING);

	if (ww_ctx) {
		/*
		 * Wound-Wait; we stole the lock (!first_waiter), check the
		 * waiters as anyone might want to wound us.
		 */
		if (!ww_ctx->is_wait_die &&
		    !__mutex_waiter_is_first(lock, &waiter))
			__ww_mutex_check_waiters(lock, ww_ctx);
	}

	__mutex_remove_waiter(lock, &waiter);

	debug_mutex_free_waiter(&waiter);

skip_wait:
	/* got the lock - cleanup and rejoice! */
	lock_acquired(&lock->dep_map, ip);

	if (ww_ctx)
		ww_mutex_lock_acquired(ww, ww_ctx);

	raw_spin_unlock(&lock->wait_lock);
	preempt_enable();
	return 0;

    ...
}
```

#### 释放锁

释放同样也分快路径和慢路径，快路径就是修改 `lock->owner` 的值，主要看看慢路径，

```c
static noinline void __sched __mutex_unlock_slowpath(struct mutex *lock, unsigned long ip)
{
	struct task_struct *next = NULL;
	DEFINE_WAKE_Q(wake_q);
	unsigned long owner;

	mutex_release(&lock->dep_map, ip);

	/*
	 * Release the lock before (potentially) taking the spinlock such that
	 * other contenders can get on with things ASAP.
	 *
	 * Except when HANDOFF, in that case we must not clear the owner field,
	 * but instead set it to the top waiter.
	 */
	owner = atomic_long_read(&lock->owner);
	for (;;) {
		MUTEX_WARN_ON(__owner_task(owner) != current);
		MUTEX_WARN_ON(owner & MUTEX_FLAG_PICKUP);

		if (owner & MUTEX_FLAG_HANDOFF)
			break;

		if (atomic_long_try_cmpxchg_release(&lock->owner, &owner, __owner_flags(owner))) {
			if (owner & MUTEX_FLAG_WAITERS)
				break;

			return; // 解锁成功，且没有等待者，直接返回
		}
	}

	raw_spin_lock(&lock->wait_lock);
	debug_mutex_unlock(lock);
	if (!list_empty(&lock->wait_list)) { // 有等待者
		/* get the first entry from the wait-list: */
		struct mutex_waiter *waiter =
			list_first_entry(&lock->wait_list,
					 struct mutex_waiter, list);

		next = waiter->task;

		debug_mutex_wake_waiter(lock, waiter);
		wake_q_add(&wake_q, next); // 将第一个等待者加入唤醒队列
	}

	if (owner & MUTEX_FLAG_HANDOFF)
		__mutex_handoff(lock, next);

	raw_spin_unlock(&lock->wait_lock);

	wake_up_q(&wake_q); // 唤醒
}
```

我们总结一下信号量和互斥锁：

比较信号量的 `down` 函数和互斥锁的 `__mutex_lock_slowpath`，我们发现**互斥锁在睡眠前会先尝试自旋等待获取锁，而信号量是直接去睡眠**，这就是为什么互斥锁要比信号量高效。

而互斥锁的轻便和高效使得其比信号的使用场景要求更加严格：

- 同一时刻时只有一个线程可以持有互斥锁，而信号量的 `count` 可以不为 1，即计数信号量；
- 只有锁持有者可以解锁，因此其不适用于内核和用户空间复杂的同步场景；
- 不允许递归的加锁解锁；
- 当进程持有互斥锁时，进程不能退出；
- 互斥锁必须使用官方接口来初始化；
- 互斥锁可以睡眠，所以不能在中断上下半部使用；

那么在写代码时该如何使用自旋锁、信号量和互斥锁呢？

在中断上下半部可以使用自旋锁；而如果临界区有睡眠、隐含睡眠的动作以内核接口函数（？），应避免使用自旋锁；而信号量和互斥锁的选择，除非使用环境不符合上述任何一条，否则优先使用互斥锁（怪不得互斥锁更为常见）。

在开发过程中遇到了 spinlock 和 mutex 使用的问题，这里进一步描述两者使用场景的区别。

- mutex 适合对锁操作非常频繁的场景，并且具有更好的适应性。尽管相比 spin lock 它会花费更多的开销（主要是上下文切换，因为它要去睡眠，虽然实现了乐观自旋，但是只有在没有更高优先级进程的调度时才会自旋等待，所以它还是有可能会睡眠的），但是它能适合实际开发中复杂的应用场景，在保证一定性能的前提下提供更大的灵活度；

- spin lock 的 lock/unlock 性能更好(花费更少的 cpu 指令)，但是它**只适应用于临界区运行时间很短的场景**。而在实际软件开发中，除非程序员对自己的程序的锁操作行为非常的了解，否则使用 spinlock 不是一个好主意(通常一个多线程程序中对锁的操作有数以万次，如果失败的锁操作(contended lock requests)过多的话就会浪费很多的时间进行空等待)；

- 更保险的方法或许是先（保守的）使用 mutex，然后如果对性能还有进一步的需求，可以尝试使用 spin lock 进行调优；

### 读写锁

信号量的 PV 操作很清晰明了，持有锁的进程能够睡眠（还没有进一步去看内核哪些地方会使用到 semaphore，这个是之后的工作）。但是其**没有区分临界区的读写属性**，为了进一步提高并发性能，社区又提出了读写锁，允许多个读者同时访问临界资源，但写者不允许，其具有如下特性：

- 允许多个读者同时进入临界区，但**同一时刻只能有一个写者进入**；
- 读者和写者不能同时进入临界区；

读写锁分为自旋类型和信号量类型，我们一个个来看。

#### 读写自旋锁

读写自旋锁在内核初始化的时候用的蛮多的，例如：

```c
#0  _raw_write_lock (lock=lock@entry=0xffffffff836adfe8 <proc_subdir_lock>) at kernel/locking/spinlock.c:299
#1  0xffffffff813db692 in proc_register (dir=0xffff888100222840, dp=0xffff888100222900) at fs/proc/generic.c:373
#2  0xffffffff813db8c7 in _proc_mkdir (name=name@entry=0xffffc90000013e0e "\377\377", mode=<optimized out>,
    mode@entry=0, parent=<optimized out>, data=data@entry=0x0 <fixed_percpu_data>,
    force_lookup=force_lookup@entry=false) at fs/proc/generic.c:495
#3  0xffffffff813db938 in proc_mkdir_data (data=0x0 <fixed_percpu_data>, parent=<optimized out>, mode=0,
    name=name@entry=0xffffc90000013e0e "\377\377") at fs/proc/generic.c:504
#4  proc_mkdir (name=name@entry=0xffffc90000013e46 "0", parent=<optimized out>) at fs/proc/generic.c:518
#5  0xffffffff81126c4a in register_irq_proc (irq=irq@entry=0, desc=0xffff888100180200) at kernel/irq/proc.c:360
#6  0xffffffff81126ee0 in init_irq_proc () at kernel/irq/proc.c:446
#7  0xffffffff831bab2a in do_basic_setup () at init/main.c:1409
#8  kernel_init_freeable () at init/main.c:1614
#9  0xffffffff81c0b31a in kernel_init (unused=<optimized out>) at init/main.c:1505
#10 0xffffffff81004572 in ret_from_fork () at arch/x86/entry/entry_64.S:295
#11 0x0000000000000000 in ?? ()
```

先看看数据结构，

##### rwlock_t

```c
typedef struct { // 和前面的自旋锁区别不大
	arch_rwlock_t raw_lock;
#ifdef CONFIG_DEBUG_SPINLOCK
	unsigned int magic, owner_cpu;
	void *owner;
#endif
#ifdef CONFIG_DEBUG_LOCK_ALLOC
	struct lockdep_map dep_map;
#endif
} rwlock_t;

typedef struct qrwlock {
	union {
		atomic_t cnts;
		struct {
#ifdef __LITTLE_ENDIAN
			u8 wlocked;	/* Locked for write? */
			u8 __lstate[3];
#else
			u8 __lstate[3];
			u8 wlocked;	/* Locked for write? */
#endif
		};
	};
	arch_spinlock_t		wait_lock;
} arch_rwlock_t;
```

其实现和前面的自旋锁类似，这里就不再详细介绍。

```c
static inline void queued_read_lock(struct qrwlock *lock)
{
	int cnts;

	cnts = atomic_add_return_acquire(_QR_BIAS, &lock->cnts);
	if (likely(!(cnts & _QW_WMASK)))
		return;

	/* The slowpath will decrement the reader count, if necessary. */
	queued_read_lock_slowpath(lock);
}
```

常用的函数如下：

- `write_lock`：申请写者锁；
- `read_lock`：申请读者锁；
- `read_lock_irq`：关闭中断并且申请读者锁；
- `write_lock_irq`：关闭中断并且申请写者锁；

#### 读写信号量

读写自旋锁和读写信号量有什么区别？

##### rw_semaphore

```c
struct rw_semaphore {
	atomic_long_t count; // 用于读写信号量的计数，具体每位的含义下面介绍
	/*
	 * Write owner or one of the read owners as well flags regarding
	 * the current state of the rwsem. Can be used as a speculative
	 * check to see if the write owner is running on the cpu.
	 */
	atomic_long_t owner; // 锁持有者的 task_struct 指针
#ifdef CONFIG_RWSEM_SPIN_ON_OWNER
	struct optimistic_spin_queue osq; /* spinner MCS lock */
#endif
	raw_spinlock_t wait_lock; // 原子的保护 count
	struct list_head wait_list; // 所有在该信号量上睡眠的进程

    ...
};
```

count 可以理解为一个二元数，其含义可以这样理解，

- `0x0000 0000`：初始值，表示没有读者和写者；
- `0x0000 000X`：表示有 x 个活跃的读者或者正在申请的读者，没有写者干扰；
- `0xFFFF 000X`：表示可能有 x 个活跃读者，还有写者正在等待，或者表示有一个写者持有锁，还有 x 个读者正在等待；
- `0xFFFF 0000`：表示 `WAITING_BIAS`，有读者或者写者正在等待，但是它们还没有成功获取锁；

不过我看 5.15 版的内核，其定义又改了，根据源码注释要这样理解：

```c
/*
 * On 64-bit architectures, the bit definitions of the count are:
 *
 * Bit  0    - writer locked bit
 * Bit  1    - waiters present bit
 * Bit  2    - lock handoff bit
 * Bits 3-7  - reserved
 * Bits 8-62 - 55-bit reader count
 * Bit  63   - read fail bit
 *
 * On 32-bit architectures, the bit definitions of the count are:
 *
 * Bit  0    - writer locked bit
 * Bit  1    - waiters present bit
 * Bit  2    - lock handoff bit
 * Bits 3-7  - reserved
 * Bits 8-30 - 23-bit reader count // 所以 rwsem_read_trylock 中要加 0x100
 * Bit  31   - read fail bit
 *
 * It is not likely that the most significant bit (read fail bit) will ever
 * be set. This guard bit is still checked anyway in the down_read() fastpath
 * just in case we need to use up more of the reader bits for other purpose
 * in the future.
 *
 * atomic_long_fetch_add() is used to obtain reader lock, whereas
 * atomic_long_cmpxchg() will be used to obtain writer lock.
 *
 * There are three places where the lock handoff bit may be set or cleared.
 * 1) rwsem_mark_wake() for readers.
 * 2) rwsem_try_write_lock() for writers.
 * 3) Error path of rwsem_down_write_slowpath().
 *
 * For all the above cases, wait_lock will be held. A writer must also
 * be the first one in the wait_list to be eligible for setting the handoff
 * bit. So concurrent setting/clearing of handoff bit is not possible.
 */
#define RWSEM_WRITER_LOCKED	(1UL << 0)
#define RWSEM_FLAG_WAITERS	(1UL << 1)
#define RWSEM_FLAG_HANDOFF	(1UL << 2)
#define RWSEM_FLAG_READFAIL	(1UL << (BITS_PER_LONG - 1))

#define RWSEM_READER_SHIFT	8
#define RWSEM_READER_BIAS	(1UL << RWSEM_READER_SHIFT)
#define RWSEM_READER_MASK	(~(RWSEM_READER_BIAS - 1))
#define RWSEM_WRITER_MASK	RWSEM_WRITER_LOCKED
#define RWSEM_LOCK_MASK		(RWSEM_WRITER_MASK|RWSEM_READER_MASK)
#define RWSEM_READ_FAILED_MASK	(RWSEM_WRITER_MASK|RWSEM_FLAG_WAITERS|\
				 RWSEM_FLAG_HANDOFF|RWSEM_FLAG_READFAIL)
```

主要有如下接口：

```c
/*
 * lock for reading
 */
// 如果一个进程持有读者锁，那么允许继续申请多个读者锁，如果持有写着锁，则要等待
extern void down_read(struct rw_semaphore *sem);
extern int __must_check down_read_interruptible(struct rw_semaphore *sem);
extern int __must_check down_read_killable(struct rw_semaphore *sem);

/*
 * trylock for reading -- returns 1 if successful, 0 if contention
 */
extern int down_read_trylock(struct rw_semaphore *sem);

/*
 * lock for writing
 */
// 如果一个进程持有写者锁，那么第二个进程申请写者锁就要自旋等待，申请读者锁要等待（忙等和睡眠等待么）
extern void down_write(struct rw_semaphore *sem);
extern int __must_check down_write_killable(struct rw_semaphore *sem);

/*
 * trylock for writing -- returns 1 if successful, 0 if contention
 */
extern int down_write_trylock(struct rw_semaphore *sem);

/*
 * release a read lock
 */
// 如果等待队列中第一个成员是写者，那么唤醒该写者，否则唤醒等待队列最前面连续的几个读者
extern void up_read(struct rw_semaphore *sem);

/*
 * release a write lock
 */
extern void up_write(struct rw_semaphore *sem);

/*
 * downgrade write lock to read lock
 */
extern void downgrade_write(struct rw_semaphore *sem);
```

##### 申请读者类型信号量

申请读者锁的过程比较复杂，我们先看看流程图，对整个过程有个了解。

![acquire_r_sem.png](https://github.com/UtopianFuture/UtopianFuture.github.io/blob/master/image/acquire_r_sem.png?raw=true)

由于内核版本问题，5.15 的内核实现和书中略有不同，不过大致的思路是一致的，这里结合源码来看。

```c
down_read -> __down_read -> __down_read_common

/*
 * lock for reading
 */
static inline int __down_read_common(struct rw_semaphore *sem, int state)
{
	long count;

	if (!rwsem_read_trylock(sem, &count)) { // 读者进程尝试快速路径获取锁
		if (IS_ERR(rwsem_down_read_slowpath(sem, count, state))) // 快速通道失败，走慢速通道
			return -EINTR;
		DEBUG_RWSEMS_WARN_ON(!is_rwsem_reader_owned(sem), sem);
	}
	return 0;
}
```

先看看快速通道，挺简单的，

```c
static inline bool rwsem_read_trylock(struct rw_semaphore *sem, long *cntp)
{
    // 前面我们知道从 bit 8 开始表示读者数量
	*cntp = atomic_long_add_return_acquire(RWSEM_READER_BIAS, &sem->count);

	if (WARN_ON_ONCE(*cntp < 0))
		rwsem_set_nonspinnable(sem);

	if (!(*cntp & RWSEM_READ_FAILED_MASK)) { // 检查是否有写者，等待线程等等，没有的话就设置 owner == current
		rwsem_set_reader_owned(sem); // 可是设置之后不就是独占么，怎样达到多个读者同时持有？
		return true;
	}

	return false;
}
```

主要是慢速通道，这意味着已经有一个写者持有该锁了，

```c
/*
 * Wait for the read lock to be granted
 */
static struct rw_semaphore __sched *
rwsem_down_read_slowpath(struct rw_semaphore *sem, long count, unsigned int state)
{
	long adjustment = -RWSEM_READER_BIAS;
	long rcnt = (count >> RWSEM_READER_SHIFT);
	struct rwsem_waiter waiter; // 获取锁失败即为等待者，会加入到 wait_list 中
	DEFINE_WAKE_Q(wake_q);
	bool wake = false;

	/*
	 * To prevent a constant stream of readers from starving a sleeping
	 * waiter, don't attempt optimistic lock stealing if the lock is
	 * currently owned by readers.
	 */
    // 为了防止不断到来的读者线程将等待线程饿死，如果当前锁被读者持有
    // 那么不要尝试使用乐观自旋等待去“偷锁”，而是将其设置为等待者
	if ((atomic_long_read(&sem->owner) & RWSEM_READER_OWNED) &&
	    (rcnt > 1) && !(count & RWSEM_WRITER_LOCKED))
		goto queue;

	/*
	 * Reader optimistic lock stealing.
	 */
    // “偷锁”是个什么机制？
    // 从代码上来看是没有写者持有锁
	if (!(count & (RWSEM_WRITER_LOCKED | RWSEM_FLAG_HANDOFF))) {
		rwsem_set_reader_owned(sem); // 读者持有锁
		lockevent_inc(rwsem_rlock_steal);

		/*
		 * Wake up other readers in the wait queue if it is
		 * the first reader.
		 */
		if ((rcnt == 1) && (count & RWSEM_FLAG_WAITERS)) {
			raw_spin_lock_irq(&sem->wait_lock);
			if (!list_empty(&sem->wait_list))
				rwsem_mark_wake(sem, RWSEM_WAKE_READ_OWNED, // 唤醒其他读者等待线程，还挺复杂
						&wake_q);
			raw_spin_unlock_irq(&sem->wait_lock);
			wake_up_q(&wake_q);
		}
		return sem;
	}

    // 这个其实很好理解，构建一个 waiter，将其加入到 wait_lock 中
queue:
	waiter.task = current;
	waiter.type = RWSEM_WAITING_FOR_READ; // 读和写的 type 是不一样的
	waiter.timeout = jiffies + RWSEM_WAIT_TIMEOUT;

	raw_spin_lock_irq(&sem->wait_lock);
	if (list_empty(&sem->wait_list)) { // 等待队列为空
		/*
		 * In case the wait queue is empty and the lock isn't owned
		 * by a writer or has the handoff bit set, this reader can
		 * exit the slowpath and return immediately as its
		 * RWSEM_READER_BIAS has already been set in the count.
		 */
		if (!(atomic_long_read(&sem->count) &
		     (RWSEM_WRITER_MASK | RWSEM_FLAG_HANDOFF))) {
			/* Provide lock ACQUIRE */
			smp_acquire__after_ctrl_dep();
			raw_spin_unlock_irq(&sem->wait_lock);
			rwsem_set_reader_owned(sem);
			lockevent_inc(rwsem_rlock_fast);
			return sem;
		}
		adjustment += RWSEM_FLAG_WAITERS; // 表示 wait_list 中有等待线程了
	}
	list_add_tail(&waiter.list, &sem->wait_list);

	/* we're now waiting on the lock, but no longer actively locking */
	count = atomic_long_add_return(adjustment, &sem->count);

	/*
	 * If there are no active locks, wake the front queued process(es).
	 *
	 * If there are no writers and we are first in the queue,
	 * wake our own waiter to join the existing active readers !
	 */
	if (!(count & RWSEM_LOCK_MASK)) { // 当前没有锁
		clear_nonspinnable(sem);
		wake = true;
	}
	if (wake || (!(count & RWSEM_WRITER_MASK) && // 当前没有锁或当前没有活跃的写者且有等待线程
		    (adjustment & RWSEM_FLAG_WAITERS)))
		rwsem_mark_wake(sem, RWSEM_WAKE_ANY, &wake_q); // 唤醒前几个读者等待线程

	raw_spin_unlock_irq(&sem->wait_lock);
	wake_up_q(&wake_q);

	/* wait to be given the lock */
	for (;;) {
		set_current_state(state);
		if (!smp_load_acquire(&waiter.task)) { // 在 rwsem_mark_wake 中被唤醒读者会设置 waiter.task 为 NULL
			/* Matches rwsem_mark_wake()'s smp_store_release(). */
			break;
		}
		if (signal_pending_state(state, current)) { // 没有获取锁，去睡眠等待
			raw_spin_lock_irq(&sem->wait_lock);
			if (waiter.task)
				goto out_nolock;
			raw_spin_unlock_irq(&sem->wait_lock);
			/* Ordered by sem->wait_lock against rwsem_mark_wake(). */
			break;
		}
		schedule(); // 让出 CPU
		lockevent_inc(rwsem_sleep_reader);
	}

	__set_current_state(TASK_RUNNING); // 成功获取锁
	lockevent_inc(rwsem_rlock);
	return sem;

    ...
}
```

##### 释放读者类型信号量

释放比较简单。

```c
/*
 * unlock after reading
 */
static inline void __up_read(struct rw_semaphore *sem)
{
	long tmp;

	DEBUG_RWSEMS_WARN_ON(sem->magic != sem, sem);
	DEBUG_RWSEMS_WARN_ON(!is_rwsem_reader_owned(sem), sem);

	rwsem_clear_reader_owned(sem);
	tmp = atomic_long_add_return_release(-RWSEM_READER_BIAS, &sem->count); // count - 0x100
	DEBUG_RWSEMS_WARN_ON(tmp < 0, sem);
    // 因为读者是可以同时存在的，那么所有读者释放后 count 应该中计数器应该为 0
    // 不为 0 说明者期间有写者请求锁
	if (unlikely((tmp & (RWSEM_LOCK_MASK|RWSEM_FLAG_WAITERS)) ==
		      RWSEM_FLAG_WAITERS)) { // 还有等待者，继续唤醒
		clear_nonspinnable(sem);
		rwsem_wake(sem);
	}
}
```

##### 申请写者类型信号量

再来看看申请写者锁的过程，

![acquire_w_sem.png](https://github.com/UtopianFuture/UtopianFuture.github.io/blob/master/image/acquire_w_sem.png?raw=true)

```c
/*
 * lock for writing
 */
static inline int __down_write_common(struct rw_semaphore *sem, int state)
{
	if (unlikely(!rwsem_write_trylock(sem))) {
		if (IS_ERR(rwsem_down_write_slowpath(sem, state)))
			return -EINTR;
	}

	return 0;
}
```

同样的，快速通道比较简单，

```c
static inline bool rwsem_write_trylock(struct rw_semaphore *sem)
{
	long tmp = RWSEM_UNLOCKED_VALUE;

    // 只是检查是否有写者持有锁，没有的话则 owner == current
	if (atomic_long_try_cmpxchg_acquire(&sem->count, &tmp, RWSEM_WRITER_LOCKED)) {
		rwsem_set_owner(sem);
		return true;
	}

	return false;
}
```

再来看看慢速通道，

```c
/*
 * Wait until we successfully acquire the write lock
 */
static struct rw_semaphore *
rwsem_down_write_slowpath(struct rw_semaphore *sem, int state)
{
	long count;
	enum writer_wait_state wstate;
	struct rwsem_waiter waiter;
	struct rw_semaphore *ret = sem;
	DEFINE_WAKE_Q(wake_q);

    // 不懂
    // 是这样的，自旋等待嘛，会不断的尝试获取锁，如果获取成功了，那么自然就返回该写者锁
	/* do optimistic spinning and steal lock if possible */
	if (rwsem_can_spin_on_owner(sem) && rwsem_optimistic_spin(sem)) {
		/* rwsem_optimistic_spin() implies ACQUIRE on success */
		return sem;
	}

	/*
	 * Optimistic spinning failed, proceed to the slowpath
	 * and block until we can acquire the sem.
	 */
	waiter.task = current;
	waiter.type = RWSEM_WAITING_FOR_WRITE; // 写者等待
	waiter.timeout = jiffies + RWSEM_WAIT_TIMEOUT;

	raw_spin_lock_irq(&sem->wait_lock);

    // 整个流程跟着注释看不难
	/* account for this before adding a new element to the list */
	wstate = list_empty(&sem->wait_list) ? WRITER_FIRST : WRITER_NOT_FIRST;

	list_add_tail(&waiter.list, &sem->wait_list);

	/* we're now waiting on the lock */
	if (wstate == WRITER_NOT_FIRST) {
		count = atomic_long_read(&sem->count);

		/*
		 * If there were already threads queued before us and:
		 *  1) there are no active locks, wake the front
		 *     queued process(es) as the handoff bit might be set.
		 *  2) there are no active writers and some readers, the lock
		 *     must be read owned; so we try to wake any read lock
		 *     waiters that were queued ahead of us.
		 */
		if (count & RWSEM_WRITER_MASK) // 有写者在等待了，只能加入到 wait_list
			goto wait;

		rwsem_mark_wake(sem, (count & RWSEM_READER_MASK)
					? RWSEM_WAKE_READERS
					: RWSEM_WAKE_ANY, &wake_q); // 唤醒写者前所有的读者

		if (!wake_q_empty(&wake_q)) {
			/*
			 * We want to minimize wait_lock hold time especially
			 * when a large number of readers are to be woken up.
			 */
			raw_spin_unlock_irq(&sem->wait_lock);
			wake_up_q(&wake_q);
			wake_q_init(&wake_q);	/* Used again, reinit */
			raw_spin_lock_irq(&sem->wait_lock);
		}
	} else { // wait_list 为空，需要设置 RWSEM_FLAG_WAITERS 表示有线程在等待了
		atomic_long_or(RWSEM_FLAG_WAITERS, &sem->count);
	}

wait:
	/* wait until we successfully acquire the lock */
	set_current_state(state); // 这个流程和申请读者锁类似
	for (;;) {
		if (rwsem_try_write_lock(sem, wstate)) {
			/* rwsem_try_write_lock() implies ACQUIRE on success */
			break;
		}

		raw_spin_unlock_irq(&sem->wait_lock);

		/*
		 * After setting the handoff bit and failing to acquire
		 * the lock, attempt to spin on owner to accelerate lock
		 * transfer. If the previous owner is a on-cpu writer and it
		 * has just released the lock, OWNER_NULL will be returned.
		 * In this case, we attempt to acquire the lock again
		 * without sleeping.
		 */
		if (wstate == WRITER_HANDOFF &&
		    rwsem_spin_on_owner(sem) == OWNER_NULL)
			goto trylock_again;

		/* Block until there are no active lockers. */
		for (;;) {
			if (signal_pending_state(state, current))
				goto out_nolock;

			schedule();
			lockevent_inc(rwsem_sleep_writer);
			set_current_state(state);
			/*
			 * If HANDOFF bit is set, unconditionally do
			 * a trylock.
			 */
			if (wstate == WRITER_HANDOFF)
				break;

			if ((wstate == WRITER_NOT_FIRST) &&
			    (rwsem_first_waiter(sem) == &waiter))
				wstate = WRITER_FIRST;

			count = atomic_long_read(&sem->count);
			if (!(count & RWSEM_LOCK_MASK))
				break;

			/*
			 * The setting of the handoff bit is deferred
			 * until rwsem_try_write_lock() is called.
			 */
			if ((wstate == WRITER_FIRST) && (rt_task(current) ||
			    time_after(jiffies, waiter.timeout))) {
				wstate = WRITER_HANDOFF;
				lockevent_inc(rwsem_wlock_handoff);
				break;
			}
		}
trylock_again:
		raw_spin_lock_irq(&sem->wait_lock);
	}
	__set_current_state(TASK_RUNNING);
	list_del(&waiter.list);
	raw_spin_unlock_irq(&sem->wait_lock);
	lockevent_inc(rwsem_wlock);

	return ret;

    ...
}
```

##### 释放写者类型信号量

```c
/*
 * unlock after writing
 */
static inline void __up_write(struct rw_semaphore *sem)
{
	long tmp;

	DEBUG_RWSEMS_WARN_ON(sem->magic != sem, sem);
	/*
	 * sem->owner may differ from current if the ownership is transferred
	 * to an anonymous writer by setting the RWSEM_NONSPINNABLE bits.
	 */
	DEBUG_RWSEMS_WARN_ON((rwsem_owner(sem) != current) &&
			    !rwsem_test_oflags(sem, RWSEM_NONSPINNABLE), sem);

	rwsem_clear_owner(sem);
	tmp = atomic_long_fetch_add_release(-RWSEM_WRITER_LOCKED, &sem->count); // 一样的， -RWSEM_WRITER_LOCKED
	if (unlikely(tmp & RWSEM_FLAG_WAITERS)) // 还有等待者
		rwsem_wake(sem);
}
```

我们来总结一下 down 和 up 操作，

- `down_read`：如果一个进程持有读者锁，那么允许继续申请多个读者锁，申请写者锁则要等待；
- `down_write`：如果一个进程持有写者所，那么第二个进程申请该写者锁要自旋等待，申请读者锁则要等待；
- `up_read / up_write`：如果等待队列中第一个成员是写者，那么唤醒该写者，否则唤醒排在等待队列中最前面几个连续的读者；

不过还有一个地方不清楚，`HANDOFF` 表示什么情况啊？

### RCU

RCU 全称 Read-Copy-Update，首先我们需要考虑一个问题，前面介绍到内核中已有很多锁机制，那为何还要设计复杂的 RCU 机制。

因为内存屏障、自旋锁、信号量、读写信号量等都需要使用原子操作，而多 CPU 争用共享变量时会让 **cache 一致性协议性能很低**。以读写信号量为例，除了上述问题，**其只允许多个读者存在，但是读者和写者不能同时存在**。

RCU 的目标是**使读者线程没有同步开销，或者同步开销很小，不需要额外的锁，也不需要原子操作和内存屏障即可快速的访问**；而将同步任务交给写者线程，写者线程会创建一个副本，在副本中进行修改，之后等待所有读者线程完成后才将旧数据销毁，使指针指向新数据。

RCU 提供的接口如下：

- `rcu_read_lock`/`rcu_read_unlock`：构成一个读者临界区；
- `rcu_dereference`：用于获取被 RCU 保护的指针，读者线程要访问 RCU 保护的共享数据，需要使用该函数创建一个新指针，指向被 RCU 保护的指针？
- `rcu_assign_pointer`：在写者线程完成数据更新后，调用该接口可以让被 RCU 保护的指针指向新建数据；
- `synchronize_rcu`：同步所有的读者进程；
- `call_rcu`：注册一个回调函数，当所有现存的读访问完成后，调用这个函数销毁旧数据。

RCU 中有两个重要概念：

- 宽限期（Grace Period, GP）。GP 有生命周期，有开始和结束之分，从 GP 开始算起，如果所有处于读者临界区的 CPU 都离开了临界区，也就是经历了一次 QS，那么认为一个 GP 结束了。GP 结束后，RCU 会调用注册的回调函数，如销毁旧数据等；
- 静止状态（Quiescent State）。如果一个 CPU 处于读者临界区，那么认为它是活跃的，如果时钟周期中检测到该 CPU 处于用户模式或空闲状态，说明该 CPU 已经离开了读者临界区，那么它是 QS 的。

在 linux 2.6.29 之前的 rcu 实现被称为经典 RCU，不过它在大型系统中遇到了问题，这里就不再分析。下面分析现在内核使用的实现，Tree RCU，也被称为 Hierarchical RCU。

#### 基本原理

rcu 的实现很复杂，但是底层逻辑却很清晰。算法有三个基本要素：

- Reader

  - 使用 `rcu_read_lock` 和 `rcu_read_unlock` 来界定读者的临界区，访问受 RCU 保护的数据时，需要始终在该临界区域内访问；

  - 在访问受保护的数据之前，需要使用 `rcu_dereference` 来获取 `RCU-protected` 指针；

  - 当使用不可抢占的 RCU 时，`rcu_read_lock/rcu_read_unlock` 之间不能使用可以睡眠的代码；

- Updater

  - 多个 Updater 更新数据时，需要使用互斥机制进行保护（怎样保护？）；

  - Updater 使用 `rcu_assign_pointer` 来移除旧的指针指向，指向更新后的临界资源；

  - Updater 使用 `synchronize_rcu`（同步）或 `call_rcu`（异步）来启动 Reclaimer，对旧的临界资源进行回收；

- Reclaimer

  - Reclaimer 回收的是旧的临界资源；

  - 为了确保没有读者正在访问要回收的临界资源，Reclaimer 需要等待所有的读者退出临界区，**这个等待的时间叫做宽限期（Grace Period）**；

![rcu_basic](D:\gitlab\UtopianFuture.github.io\image\rcu_basic.svg)

##### Grace Period

我们来进一步看看 Grace Period 是怎样划定的，

![grace_period](D:\gitlab\UtopianFuture.github.io\image\grace_period.svg)

- 图中 Readers 和 Updater 并发执行；
- 当 Updater 执行 Removal 操作后，调用 `synchronize_rcu`，标志着更新结束并开始进入回收阶段；
- 在 `synchronize_rcu` 调用后，此时可能还有新的读者来读取临界资源（更新后的内容），但是 `Grace Period` 只等待 `Pre-Existing` 的读者，也就是在图中的 Reader-3, Reader-4。只要这些之前就存在的 RCU 读者退出临界区后，意味着宽限期的结束，因此就进行回收处理工作了；
- `synchronize_rcu` 并不是在最后一个 `Pre-Existing` RCU 读者离开临界区后立马就返回，它可能存在一个调度延迟；

##### Quiescent State

- 普通的 QS：**CPU 发生进程切换，或是运行在用户态都标志着它进入了 QS**；

- Extended QS：CPU 执行 idle 进程、进入中断、运行在用户态的 tickless 模式下，都标志这它进入了 QS（暂不深入）；
- Deferred QS：调用 `rcu_read_unlock` 退出最外层 RCU 读临界区时若抢占是关的、或软中断是关的、或中断是关的三种情况之一都会导致读端临界区被延长，致使当前 CPU 的 QS 推迟上报。被推迟的 QS 会在读临界区以外的中断、软中断、进程切换中被上报（暂不深入）；

##### 时钟中断

在时钟中断的处理程序中，不仅包含进程调度，定时器等相关操作，也包含了 RCU 的部分任务。会在中断处理函数中检查是否有存在待处理的事情（**如需要上报的 QS，callback 过载等？**），有的话则唤醒 RCU 软中断/kthread 进行处理。

##### RCU 软中断/kthread

RT 内核中使用的是 rcu_cpu_kthread。其会执行如下几项操作，

- 处理本 CPU 上以已经进入 DONE 状态的 callback；
- 检测并更新本 CPU 的 gp 编号；
- 检测并上报本 CPU 的 QS；
- 有需要则唤醒 gp 线程；

##### GP 内核线程

rcu_gp_kthread，主要功能是处理宽限期相关的事务，包括开启新的宽限期、处理 forcing QS、清理已结束的宽限期。

#### 相关数据结构

tree RCU 通过 rcu_state, rcu_node, rcu_data 三种数据结构组成了整颗 RCU tree，它们分别代表树根、树干、树叶。

##### rcu_state

描述 rcu 全局状态，内核为每种 RCU 定义了一个 rcu_state 实例。

```c
struct rcu_state {
	struct rcu_node node[NUM_RCU_NODES]; // 存放了所有 rcu_node
	struct rcu_node *level[RCU_NUM_LVLS + 1];
						/* Hierarchy levels (+1 to */
						/*  shut bogus gcc warning) */
	int ncpus; // 系统中总的 cpu 数量

	/* The following fields are guarded by the root rcu_node's lock. */

	u8	boost ____cacheline_internodealigned_in_smp;
						/* Subject to priority boost. */
	unsigned long gp_seq; // 当前宽限期的编号，低两位表示状态
	unsigned long gp_max; // 当前系统上经历过最长的 gp 耗时
	struct task_struct *gp_kthread; // 指向 rcu_gp_kthread 内核线程
	struct swait_queue_head gp_wq;		/* Where GP task waits. */
	short gp_flags; // 用来控制 gp 线程行为的标志位，可以通知进行 FQS 或开启新的 GP
	short gp_state;	// rcu_gp_kthread 线程的状态
	unsigned long gp_wake_time;		/* Last GP kthread wake. */
	unsigned long gp_wake_seq;		/* ->gp_seq at ^^^. */

	/* End of fields guarded by root rcu_node's lock. */

	struct mutex barrier_mutex;		/* Guards barrier fields. */
	atomic_t barrier_cpu_count;		/* # CPUs waiting on. */
	struct completion barrier_completion;	/* Wake at barrier end. */
	unsigned long barrier_sequence;		/* ++ at start and end of */
	/* End of fields guarded by barrier_mutex. */

	struct mutex exp_mutex;			/* Serialize expedited GP. */
	struct mutex exp_wake_mutex;		/* Serialize wakeup. */
	unsigned long expedited_sequence;	/* Take a ticket. */
	atomic_t expedited_need_qs;		/* # CPUs left to check in. */
	struct swait_queue_head expedited_wq;	/* Wait for check-ins. */
	int ncpus_snap;				/* # CPUs seen last time. */
	u8 cbovld; // 当前系统 callback 是否堆积过多？
	u8 cbovldnext;

	...
};
```

##### rcu_node

描述一个处理器分组的 rcu 状态，是 Tree RCU 中的组织节点。

```c
struct rcu_node {
	raw_spinlock_t __private lock; // 保护此 node 的 spinlock
	unsigned long gp_seq; // 本节点当前宽限期的编号，低两位表示状态
	unsigned long gp_seq_needed; /* Track furthest future GP request. */
	unsigned long completedqs; // 当前 node 已经度过所有的 QS，且 gp_tasks 指针为 NULL
	unsigned long qsmask; // 记录当前 node 中的 cpu 或子 node 的 QS 情况，经历过为 0
	unsigned long rcu_gp_init_mask;	/* Mask of offline CPUs at GP init. */
	unsigned long qsmaskinit; // 每个 GP 开始时的初值，从 qsmaskinitnext 中获取，用来赋给 qsmask
	unsigned long qsmaskinitnext; // 下一个宽限期开始时的静止态位图，因为可能有些 cpu 下线了
	unsigned long expmask; // 类似于 qsmask，用来记录加速宽限期的位图
	unsigned long expmaskinit;
	unsigned long expmaskinitnext;
	unsigned long cbovldmask;
				/* CPUs experiencing callback overload. */
	unsigned long ffmask;	/* Fully functional CPUs. */
	unsigned long grpmask; // 此 node 对应父 node 的 qsmask 的哪个 bit
	int	grplo; // node 中最小的处理器编号
	int	grphi; // node 中最大的处理器编号
	u8	grpnum; // node 在上一层 node 中的编号
	u8	level; // node 在 tree 中的 level，root node 为 0
	bool	wait_blkd_tasks;
	struct rcu_node *parent; // 指向父节点
	struct list_head blkd_tasks; // 读临界区中被抢占的进程，都会被挂在此链表
    // 指向 blkd_tasks 中阻塞当前宽限期的第一个进程，当前 node 度过 qs 时，此成员必须为 NULL
	struct list_head *gp_tasks;
	
    ...
        
} ____cacheline_internodealigned_in_smp;
```

##### rcu_data

描述一个 CPU 的 rcu 状态，每个 CPU 对应一个 rcu_data 实例。每个 CPU 对于自己正在经历的宽限期会有自己的视角，不一定是全局统一的，这是因为有些 CPU可能经历了停止 TICK、长时间的 IDLE、甚至是 offline/online 等操作，这会带来与其他 CPU 之间的差异；此外，可能某些 CPU 的宽限期比较长，也会导致其宽限期落后于其他 CPU。

```c
struct rcu_data {
	unsigned long	gp_seq; // cpu 当前的 gp 编号，以及低两位表示 gp 的状态
	unsigned long	gp_seq_needed;	/* Track furthest future GP request. */
	union rcu_noqs	cpu_no_qs; // 表示 cpu 是否未度过 norm 或 exp 静止态
	bool		core_needs_qs; // 表示 RCU 是否需要此 cpu 上报 QS 状态
	bool		beenonline; // cpu 最近的一次状态是 online
	bool		gpwrap; // gp_seq 溢出后，此变量会置为 true
	bool		exp_deferred_qs; // 当前 cpu 是否在等待一个 deferred QS
	bool		cpu_started;	/* RCU watching this onlining CPU. */
	struct rcu_node *mynode; // 指向该 cpu 对应的 rcu_node
	unsigned long grpmask;		/*Mask to apply to leaf qsmask. */
    // 当前 gp 已经历过多少个 tick 中断，每个 tick 中断中都会对它加 1
	unsigned long	ticks_this_gp;
	struct irq_work defer_qs_iw;	/* Obtain later scheduler attention. */
	bool defer_qs_iw_pending;	/* Scheduler attention pending? */
	struct work_struct strict_work;	/* Schedule readers for strict GPs. */

	struct rcu_segcblist cblist; // 分段链表，存放当前cpu上的callback
	long		qlen_last_fqs_check;
					/* qlen at last check for QS forcing */
	unsigned long	n_cbs_invoked;	/* # callbacks invoked since boot. */
	unsigned long	n_force_qs_snap;
					/* did other CPU force QS recently? */
	long		blimit;		/* Upper limit on a processed batch */

	/* 3) dynticks interface. */
	int dynticks_snap;		/* Per-GP tracking for dynticks. */
	long dynticks_nesting;		/* Track process nesting level. */
	long dynticks_nmi_nesting;	/* Track irq/NMI nesting level. */
	atomic_t dynticks;		/* Even value for idle, else odd. */
	bool rcu_need_heavy_qs;		/* GP old, so heavy quiescent state! */
	bool rcu_urgent_qs;		/* GP old need light quiescent state. */
	bool rcu_forced_tick;		/* Forced tick to provide QS. */
	bool rcu_forced_tick_exp;	/*   ... provide QS to expedited GP. */

	...

	int cpu;
};
```

#### 如何度过静止态

##### 进程切换

```c
| schedule
|	-> __schedule
|		-> rcu_note_context_switch
```

**关键函数 rcu_note_context_switch**

```c
void rcu_note_context_switch(bool preempt)
{
	struct task_struct *t = current;
	struct rcu_data *rdp = this_cpu_ptr(&rcu_data);
	struct rcu_node *rnp;

    // rcu 只允许抢占
    // 如果是主动调度，且处于读临界区，那么打印 WARNNING
    // 判断是否处于读临界区很简单，current->rcu_read_lock_nesting 大于 0
    // 在 __rcu_read_lock 时会累加
	WARN_ON_ONCE(!preempt && rcu_preempt_depth() > 0);
    // 在读临界区，且是首次抢占
	if (rcu_preempt_depth() > 0 &&
	    !t->rcu_read_unlock_special.b.blocked) {

		/* Possibly blocking in an RCU read-side critical section. */
		rnp = rdp->mynode;
		raw_spin_lock_rcu_node(rnp);
        // 标记当前进程的 rcu 读临界区被抢占，并记录目前进程属于哪个 rcu node
		t->rcu_read_unlock_special.b.blocked = true;
		t->rcu_blocked_node = rnp;

		...
            
        // 将被抢占的进程加入 rcu node 中的被抢占进程链表
		rcu_preempt_ctxt_queue(rnp, rdp);
	} else { // 检测是否有适合且存在 pending 的 deferred QS 需要上报，有则上报
		rcu_preempt_deferred_qs(t);
	}

    // 记录本 CPU 已经度过 QS，但只是记录，上报会在后续的 RCU 软中断 kthread 中进行
    // 主要就是将 rcu_data.cpu_no_qs 写成 false
	rcu_qs();
	if (rdp->exp_deferred_qs)
		rcu_report_exp_rdp(rdp);
	rcu_tasks_qs(current, preempt);
}
```

##### CPU 进入 EQS

为什么要有 EQS 呢？

主要是为了动态时钟的场景，若系统开启了 NO_HZ_IDLE，在运行 idle 进程的情况下 CPU 是不响应 tick 中断的；若开启 NO_HZ_FULL，不仅 CPU Idle 时不响应 tick，在 CPU 只有一个 running 状态的进程时也不响应 tick 中断。

前面我们讲 tick 中断时已经说到它的功能包括检查 QS，以及唤醒 RCU 软中断。**那在没有的 tick 的情况下 CPU 如何上报 QS 呢？**

通过分析 `rcu_eqs_enter` 可知，进入 EQS 时会调用 `rcu_dynticks_eqs_enter` 将当前 CPU 的 `rdp->dynticks` 的 bit1 加 1 变为奇数，表示处于动态时钟模式，而 **gp 线程在处理强制静止态 force qs 的操作中会搜集所有的处于 EQS 的 CPU，并替它们上报 QS**。

既然有进入，那就会有退出，退出 EQS 模式时会将 rdp->dynticks 的 bit1 减 1，变为偶数。对应函数 `rcu_eqs_exit`，场景包括退出 idle 进程、进入中断。

- CPU 运行 Idle 进程

```c
| do_idle
|	-> cpu_idle_poll
|		-> rcu_idle_enter
|			->rcu_eqs_enter
|				->rcu_prepare_for_idle // 空函数
|				->rcu_preempt_deferred_qs // 判断是否有 deferred qs 需要上报，有则上报
|				// rdp->dynticks 加 0x10，gp 线程的 fqs 处理中通过判断 bit1 的奇偶
|				// 来判断 eqs，用 bit1 的原因是，bit0 被另做它用了
|				->rcu_dynticks_eqs_enter
|		-> cpu_relax
|		-> rcu_idle_exit
|			-> rcu_eqs_exit
```

**关键函数 rcu_eqs_enter**

```c
static noinstr void rcu_eqs_enter(bool user)
{
	struct rcu_data *rdp = this_cpu_ptr(&rcu_data);

	...

	rdp = this_cpu_ptr(&rcu_data);
	rcu_prepare_for_idle();
	rcu_preempt_deferred_qs(current);

	// instrumentation for the noinstr rcu_dynticks_eqs_enter()
	instrument_atomic_write(&rdp->dynticks, sizeof(rdp->dynticks));

	instrumentation_end();
	WRITE_ONCE(rdp->dynticks_nesting, 0); /* Avoid irq-access tearing. */
	// RCU is watching here ...
	rcu_dynticks_eqs_enter();
	// ... but is no longer watching here.
	rcu_dynticks_task_enter();
}
```

**关键函数 rcu_dynticks_eqs_enter**

```c
static noinstr void rcu_dynticks_eqs_enter(void)
{
	struct rcu_data *rdp = this_cpu_ptr(&rcu_data);
	int seq;

	...
       
    // RCU_DYNTICK_CTRL_CTR = 2，即 bit1 加 1
	seq = arch_atomic_add_return(RCU_DYNTICK_CTRL_CTR, &rdp->dynticks);
	
    ...
}
```

- CPU 返回用户空间

```c
| ret_to_user
|	-> user_enter_irqoff
|		-> exit_to_user_mode
|    		-> __context_tracking_enter
|	    		-> rcu_user_enter
|        			-> rcu_eqs_enter
```

#### 宽限期的创建、处理、结束

##### 关键函数 rcu_gp_kthread

宽限期的处理由内核线程 `rcu_gp_kthread` 全权负责，现在一起来分析下此线程的逻辑。

```
| rcu_gp_kthread
|	-> swait_event_idle_exclusive
|	-> rcu_gp_torture_wait
|	-> rcu_gp_init
|	-> rcu_gp_fqs_loop
|	-> rcu_gp_cleanup
```

```c
static int __noreturn rcu_gp_kthread(void *unused)
{
	rcu_bind_gp_kthread();
	for (;;) {

		/* Handle grace-period start. */
		for (;;) {
			rcu_state.gp_state = RCU_GP_WAIT_GPS; // 将 rcu 状态设为 RCU_GP_WAIT_GPS
			swait_event_idle_exclusive(rcu_state.gp_wq,
					 READ_ONCE(rcu_state.gp_flags) &
					 RCU_GP_FLAG_INIT); // 线程进入 idle 状态等待 RCU_GP_FLAG_INIT 被设置
			rcu_gp_torture_wait();
			rcu_state.gp_state = RCU_GP_DONE_GPS; // 此时线程已被唤醒，准备开启新的 gp
			/* Locking provides needed memory barrier. */
			if (rcu_gp_init()) // 初始化一个新的 gp
				break;
			cond_resched_tasks_rcu_qs();
			WRITE_ONCE(rcu_state.gp_activity, jiffies);
            
            ...
		}

        // 等待宽限期结束
		/* Handle quiescent-state forcing. */
		rcu_gp_fqs_loop();

        // 宽限期结束，做最后的清理
		/* Handle grace-period end. */
		rcu_state.gp_state = RCU_GP_CLEANUP;
		rcu_gp_cleanup();
		rcu_state.gp_state = RCU_GP_CLEANED; // GP 结束了
	}
}
```

此线程大部分时间是在睡眠，等待唤醒。那么谁会唤醒 gp 线程呢？地方挺多，例如 `call_rcu` 注册回调时，若发现 CPU 上的 callback 过载了，就唤醒 gp 线程进行一次 fqs；比较通用的唤醒位置则是在 RCU 软中断里，RCU 软中断又是被 tick 中断触发，下面会分析 tick 中断和 RCU 软中断。

##### 关键函数 rcu_gp_init

第一阶段，初始化一个新的 gp，成功则进入下一个阶段，处理强制静止状态。

```c
static bool rcu_gp_init(void)
{
	unsigned long flags;
	unsigned long oldmask;
	unsigned long mask;
	struct rcu_data *rdp;
	struct rcu_node *rnp = rcu_get_root();

    // 记录新的宽限期开始时间
	WRITE_ONCE(rcu_state.gp_activity, jiffies);
	raw_spin_lock_irq_rcu_node(rnp);
	if (!READ_ONCE(rcu_state.gp_flags)) {
		/* Spurious wakeup, tell caller to go back to sleep.  */
		raw_spin_unlock_irq_rcu_node(rnp);
		return false;
	}
	WRITE_ONCE(rcu_state.gp_flags, 0); /* Clear all flags: New GP. */

    // 如果当前 CPU 仍处于宽限期，那报 WARNNING，返回
	if (WARN_ON_ONCE(rcu_gp_in_progress())) {
		/*
		 * Grace period already in progress, don't start another.
		 * Not supposed to be able to happen.
		 */
		raw_spin_unlock_irq_rcu_node(rnp);
		return false;
	}

    // 这个就是更新 rcu_state.gp_start，跟 gp_acitvity 有什么区别？
	/* Advance to a new grace period and initialize state. */
	record_gp_stall_check_time();
	/* Record GP times before starting GP, hence rcu_seq_start(). */
	rcu_seq_start(&rcu_state.gp_seq); // gp_seq++
	ASSERT_EXCLUSIVE_WRITER(rcu_state.gp_seq);
	trace_rcu_grace_period(rcu_state.name, rcu_state.gp_seq, TPS("start"));
	raw_spin_unlock_irq_rcu_node(rnp);

	rcu_state.gp_state = RCU_GP_ONOFF;
	rcu_for_each_leaf_node(rnp) {
		raw_spin_lock(&rcu_state.ofl_lock);
		raw_spin_lock_irq_rcu_node(rnp);
		if (rnp->qsmaskinit == rnp->qsmaskinitnext &&
		    !rnp->wait_blkd_tasks) {
			/* Nothing to do on this leaf rcu_node structure. */
			raw_spin_unlock_irq_rcu_node(rnp);
			raw_spin_unlock(&rcu_state.ofl_lock);
			continue;
		}

		/* Record old state, apply changes to ->qsmaskinit field. */
		oldmask = rnp->qsmaskinit;
		rnp->qsmaskinit = rnp->qsmaskinitnext;

		/* If zero-ness of ->qsmaskinit changed, propagate up tree. */
		if (!oldmask != !rnp->qsmaskinit) {
			if (!oldmask) { /* First online CPU for rcu_node. */
				if (!rnp->wait_blkd_tasks) /* Ever offline? */
					rcu_init_new_rnp(rnp);
			} else if (rcu_preempt_has_tasks(rnp)) {
				rnp->wait_blkd_tasks = true; /* blocked tasks */
			} else { /* Last offline CPU and can propagate. */
				rcu_cleanup_dead_rnp(rnp);
			}
		}

		if (rnp->wait_blkd_tasks &&
		    (!rcu_preempt_has_tasks(rnp) || rnp->qsmaskinit)) {
			rnp->wait_blkd_tasks = false;
			if (!rnp->qsmaskinit)
				rcu_cleanup_dead_rnp(rnp);
		}

		raw_spin_unlock_irq_rcu_node(rnp);
		raw_spin_unlock(&rcu_state.ofl_lock);
	}
	rcu_gp_slow(gp_preinit_delay); /* Races with CPU hotplug. */

	rcu_state.gp_state = RCU_GP_INIT; // 初始化完成
	rcu_for_each_node_breadth_first(rnp) {
		rcu_gp_slow(gp_init_delay);
		raw_spin_lock_irqsave_rcu_node(rnp, flags);
		rdp = this_cpu_ptr(&rcu_data);
		rcu_preempt_check_blocked_tasks(rnp);
		rnp->qsmask = rnp->qsmaskinit;
		WRITE_ONCE(rnp->gp_seq, rcu_state.gp_seq);
		if (rnp == rdp->mynode)
			(void)__note_gp_changes(rnp, rdp);
		rcu_preempt_boost_start_gp(rnp);
		trace_rcu_grace_period_init(rcu_state.name, rnp->gp_seq,
					    rnp->level, rnp->grplo,
					    rnp->grphi, rnp->qsmask);
		/* Quiescent states for tasks on any now-offline CPUs. */
		mask = rnp->qsmask & ~rnp->qsmaskinitnext;
		rnp->rcu_gp_init_mask = mask;
		if ((mask || rnp->wait_blkd_tasks) && rcu_is_leaf_node(rnp))
			rcu_report_qs_rnp(mask, rnp, rnp->gp_seq, flags);
		else
			raw_spin_unlock_irq_rcu_node(rnp);
		cond_resched_tasks_rcu_qs();
		WRITE_ONCE(rcu_state.gp_activity, jiffies);
	}

	// If strict, make all CPUs aware of new grace period.
	if (IS_ENABLED(CONFIG_RCU_STRICT_GRACE_PERIOD))
		on_each_cpu(rcu_strict_gp_boundary, NULL, 0);

	return true;
}
```

##### 关键函数 rcu_gp_fqs_loop

上面初始化新的 gp 之后，就会 break 到这里， `rcu_state.gp_state = RCU_GP_WAIT_FQS;`，等待所有进程进入静止态，才好继续处理，结束本次宽限期。

```c
static void rcu_gp_fqs_loop(void)
{
	bool first_gp_fqs;
	int gf = 0;
	unsigned long j;
	int ret = 0;
	struct rcu_node *rnp = rcu_get_root();

	...

	for (;;) {

        ...

		rcu_state.gp_state = RCU_GP_WAIT_FQS;
        // RCU_GP_WAIT_FQS 状态，开始等待
        // 唤醒情况：
        // 1. callback 过载
        // 2. FQS flag 被设置
        // 3. root rcu node 的 qsmask 为 0 且无 block 进程
		ret = swait_event_idle_timeout_exclusive(
				rcu_state.gp_wq, rcu_gp_fqs_check_wake(&gf), j);
		rcu_gp_torture_wait();
		rcu_state.gp_state = RCU_GP_DOING_FQS;
		/* Locking provides needed memory barriers. */
		/* If grace period done, leave loop. */
        // 所有进程都进入静止状态了，结束等待，可以进入下一步了
        // 结束宽限期
		if (!READ_ONCE(rnp->qsmask) &&
		    !rcu_preempt_blocked_readers_cgp(rnp))
			break;
		/* If time for quiescent-state forcing, do it. */
        // 如果 rnp->qsmask 不为 0，或 gp_tasks 不为 0
        // 说明可能是 callback 过载设置了 FQS
        // 或是 gp 超时，gp 未结束，调用 rcu_gp_fqs 进行一次 fqs
		if (!time_after(rcu_state.jiffies_force_qs, jiffies) ||
		    (gf & (RCU_GP_FLAG_FQS | RCU_GP_FLAG_OVLD))) {
			rcu_gp_fqs(first_gp_fqs);
			gf = 0;
			if (first_gp_fqs) {
				first_gp_fqs = false;
				gf = rcu_state.cbovld ? RCU_GP_FLAG_OVLD : 0;
			}
			cond_resched_tasks_rcu_qs();
			WRITE_ONCE(rcu_state.gp_activity, jiffies);
			ret = 0; /* Force full wait till next FQS. */
			j = READ_ONCE(jiffies_till_next_fqs);
            // 若是等待事件超时，则调整睡眠时长重新进入 swait_event 等待
            // 若经过若干次的超时都未结束 gp，进行一次强制静止态 force_qs_rnp
		} else {
			/* Deal with stray signal. */
			cond_resched_tasks_rcu_qs();
			WRITE_ONCE(rcu_state.gp_activity, jiffies);
			WARN_ON(signal_pending(current));
			trace_rcu_grace_period(rcu_state.name, rcu_state.gp_seq,
					       TPS("fqswaitsig"));
			ret = 1; /* Keep old FQS timing. */
			j = jiffies;
			if (time_after(jiffies, rcu_state.jiffies_force_qs))
				j = 1;
			else
				j = rcu_state.jiffies_force_qs - j;
			gf = 0;
		}
	}
}
```

##### 关键函数 rcu_gp_cleanup

上面如果所有进程都进入了静止状态，那么结束宽限期。

```c
static void rcu_gp_cleanup(void)
{
	int cpu;
	bool needgp = false;
	unsigned long gp_duration;
	unsigned long new_gp_seq;
	bool offloaded;
	struct rcu_data *rdp;
	struct rcu_node *rnp = rcu_get_root();
	struct swait_queue_head *sq;

	WRITE_ONCE(rcu_state.gp_activity, jiffies);
	raw_spin_lock_irq_rcu_node(rnp);
	rcu_state.gp_end = jiffies;
	gp_duration = rcu_state.gp_end - rcu_state.gp_start;
	if (gp_duration > rcu_state.gp_max) // 更新时间最长的 gp 耗时
		rcu_state.gp_max = gp_duration;
	raw_spin_unlock_irq_rcu_node(rnp);

	new_gp_seq = rcu_state.gp_seq;
    // 当前宽限期的编号，低两位表示状态
    // 这里将最低位 +1，表示当前宽限期结束
	rcu_seq_end(&new_gp_seq);
    // 从 root node 向下逐个初始化 node
	rcu_for_each_node_breadth_first(rnp) {
		raw_spin_lock_irq_rcu_node(rnp);
		if (WARN_ON_ONCE(rcu_preempt_blocked_readers_cgp(rnp)))
			dump_blkd_tasks(rnp, 10);
		WARN_ON_ONCE(rnp->qsmask);
		WRITE_ONCE(rnp->gp_seq, new_gp_seq); // 更新 rcu_node 中的 gp_seq
		rdp = this_cpu_ptr(&rcu_data);
		if (rnp == rdp->mynode)
			needgp = __note_gp_changes(rnp, rdp) || needgp;
		/* smp_mb() provided by prior unlock-lock pair. */
		needgp = rcu_future_gp_cleanup(rnp) || needgp;
		// Reset overload indication for CPUs no longer overloaded
		if (rcu_is_leaf_node(rnp))
			for_each_leaf_node_cpu_mask(rnp, cpu, rnp->cbovldmask) {
				rdp = per_cpu_ptr(&rcu_data, cpu);
				check_cb_ovld_locked(rdp, rnp);
			}
		sq = rcu_nocb_gp_get(rnp);
		raw_spin_unlock_irq_rcu_node(rnp);
		rcu_nocb_gp_cleanup(sq);
		cond_resched_tasks_rcu_qs();
		WRITE_ONCE(rcu_state.gp_activity, jiffies);
		rcu_gp_slow(gp_cleanup_delay);
	}
	rnp = rcu_get_root();
	raw_spin_lock_irq_rcu_node(rnp); /* GP before ->gp_seq update. */

	/* Declare grace period done, trace first to use old GP number. */
	rcu_seq_end(&rcu_state.gp_seq); // 再更新 rcu_state 的 seq
	ASSERT_EXCLUSIVE_WRITER(rcu_state.gp_seq);
	rcu_state.gp_state = RCU_GP_IDLE;
	/* Check for GP requests since above loop. */
	rdp = this_cpu_ptr(&rcu_data);
	if (!needgp && ULONG_CMP_LT(rnp->gp_seq, rnp->gp_seq_needed)) {
		needgp = true;
	}
	/* Advance CBs to reduce false positives below. */
	offloaded = IS_ENABLED(CONFIG_RCU_NOCB_CPU) &&
		    rcu_segcblist_is_offloaded(&rdp->cblist);
	if ((offloaded || !rcu_accelerate_cbs(rnp, rdp)) && needgp) {
		WRITE_ONCE(rcu_state.gp_flags, RCU_GP_FLAG_INIT);
		WRITE_ONCE(rcu_state.gp_req_activity, jiffies);
	} else {
		WRITE_ONCE(rcu_state.gp_flags,
			   rcu_state.gp_flags & RCU_GP_FLAG_INIT);
	}
	raw_spin_unlock_irq_rcu_node(rnp);

	...
}
```

从下图中能直观看到宽限期的执行流程，

![rcu_flow](D:\gitlab\UtopianFuture.github.io\image\rcu_flow.svg)

#### 时钟中断 tick_handle_periodic

```c
| tick_handle_periodic
|	-> tick_periodic
|		-> update_process_times
|			-> rcu_sched_clock_irq
|				-> rcu_flavor_sched_clock_irq
|					-> rcu_qs // rcu_preempt_depth() == 0，标记本 CPU 已经经历了 QS
|				// 1. 检查是否发生了 cpu stall
|		        // 2. 检查当前 CPU 是否需要上报 QS
|				// 3. 检查是否有 done 状态的 callback 需要执行
|				-> rcu_pending
|				-> invoke_rcu_core
|					-> invoke_rcu_core_kthread
|						-> rcu_wake_cond // 唤醒 rcu_cpu_kthread
```

#### 关键函数 rcu_cpu_kthread

```c
| rcu_cpu_kthread
|	-> rcu_core
|		-> rcu_check_quiescent_state
|			-> note_gp_changes
|				-> rdp->gp_seq == rnp->gp_seq // 未产生新的 gp，无需同步，直接 return
|				-> __note_gp_changes
|			-> rcu_report_qs_rdp
|				// 说明已经当前 cpu 已经经历过 qs，直接返回
|				-> if(rdp->cpu_no_qs.b.norm==ture || rdp->gp_seq!=rnp->gp_seq)
|				-> if ((rnp->qsmask & mask) == 0) // 说明上报过 qs，无需再上报
|				// 如果是最后进入静止状态的 cpu，就会给 gp_flags 置上 RCU_GP_FLAG_FQS 位
|				// 然后唤醒 rcu_gp_kthread
|				-> rcu_report_qs_rsp
```

```c
static void rcu_cpu_kthread(unsigned int cpu)
{
	unsigned int *statusp = this_cpu_ptr(&rcu_data.rcu_cpu_kthread_status);
	char work, *workp = this_cpu_ptr(&rcu_data.rcu_cpu_has_work);
	int spincnt;

	for (spincnt = 0; spincnt < 10; spincnt++) {
		work = *workp;
		*workp = 0;
		if (work)
			rcu_core(); // 上报该 CPU 进入静止状态
		if (*workp == 0) {
			return;
		}
	}
	schedule_timeout_idle(2);
}
```

#### 关键函数 rcu_dereference

```c
#define rcu_dereference(p) rcu_dereference_check(p, 0)

#define rcu_dereference_check(p, c) \
	__rcu_dereference_check((p), (c) || rcu_read_lock_held(), __rcu)

#define __rcu_dereference_check(p, c, space) \
({ \
	/* Dependency order vs. p above. */ \
	typeof(*p) *________p1 = (typeof(*p) *__force)READ_ONCE(p); \
	RCU_LOCKDEP_WARN(!(c), "suspicious rcu_dereference_check() usage"); \
	rcu_check_sparse(p, space); \
	((typeof(*p) __force __kernel *)(________p1)); \
})
```

#### 关键函数 rcu_assign_pointer

```c
#define rcu_assign_pointer(p, v)					      \
do {									      \
	uintptr_t _r_a_p__v = (uintptr_t)(v);				      \
	rcu_check_sparse(p, __rcu);					      \
									      \
	if (__builtin_constant_p(v) && (_r_a_p__v) == (uintptr_t)NULL)	      \
		WRITE_ONCE((p), (typeof(p))(_r_a_p__v));		      \
	else								      \
		smp_store_release(&p, RCU_INITIALIZER((typeof(p))_r_a_p__v)); \
} while (0)
```

#### 关键函数 synchronize_rcu

```c
| synchronize_rcu
|	-> rcu_blocking_is_gp
|	-> rcu_gp_is_expedited
|	// 向其他 CPU 发送 IPI，强制宽限期快速结束
|	-> synchronize_rcu_expedited
|		-> wait_rcu_exp_gp // 放在 queue_work 中执行
|			-> rcu_exp_sel_wait_wake
|				-> sync_rcu_exp_select_cpus
|					-> sync_exp_reset_tree
|					-> sync_rcu_exp_select_node_cpus // queue_work_on
|						-> rcu_exp_handler
|				-> rcu_exp_wait_wake
|					-> synchronize_rcu_expedited_wait
|		// 等待 expedited gp 完成，当前进程才能继续执行
|		-> wait_event(rnp->exp_wq[rcu_seq_ctr(s) & 0x3], sync_exp_work_done(s));
|			-> rcu_exp_gp_seq_done // 检查 rcu_state.expedited_sequence，看是否都完成了
|	-> wait_rcu_gp
|		-> call_rcu
```

```c
void synchronize_rcu(void)
{
	...

	if (rcu_blocking_is_gp())
		return;
	if (rcu_gp_is_expedited())
        // 该函数会向其他处理器发送处理器间中断（Inter-Processor Interrupt，IPI）请求
        // 强制宽限期快速结束。我们把强制快速结束的宽限期称为加速宽限期（expedited grace period）
        // 把没有强制快速结束的宽限期称为正常宽限期（normal grace period）
		synchronize_rcu_expedited();
	else
        // 等待宽限期结束，即所有读者退出读端临界区，然后写者执行下一步操作
		wait_rcu_gp(call_rcu);
}
```

#### 关键函数 wait_rcu_exp_gp

```c
static void wait_rcu_exp_gp(struct work_struct *wp)
{
	struct rcu_exp_work *rewp;

	rewp = container_of(wp, struct rcu_exp_work, rew_work);
	rcu_exp_sel_wait_wake(rewp->rew_s);
}

static void rcu_exp_sel_wait_wake(unsigned long s)
{
	/* Initialize the rcu_node tree in preparation for the wait. */
	sync_rcu_exp_select_cpus();

	/* Wait and clean up, including waking everyone. */
	rcu_exp_wait_wake(s);
}
```

#### 关键函数 sync_rcu_exp_select_cpus

```c
static void sync_rcu_exp_select_cpus(void)
{
	int cpu;
	struct rcu_node *rnp;

	trace_rcu_exp_grace_period(rcu_state.name, rcu_exp_gp_seq_endval(), TPS("reset"));
	sync_exp_reset_tree();
	trace_rcu_exp_grace_period(rcu_state.name, rcu_exp_gp_seq_endval(), TPS("select"));

	/* Schedule work for each leaf rcu_node structure. */
	rcu_for_each_leaf_node(rnp) {
		rnp->exp_need_flush = false;
		if (!READ_ONCE(rnp->expmask))
			continue; /* Avoid early boot non-existent wq. */
		if (!READ_ONCE(rcu_par_gp_wq) ||
		    rcu_scheduler_active != RCU_SCHEDULER_RUNNING ||
		    rcu_is_last_leaf_node(rnp)) {
			/* No workqueues yet or last leaf, do direct call. */
			sync_rcu_exp_select_node_cpus(&rnp->rew.rew_work);
			continue;
		}
		INIT_WORK(&rnp->rew.rew_work, sync_rcu_exp_select_node_cpus);
		cpu = find_next_bit(&rnp->ffmask, BITS_PER_LONG, -1);
		/* If all offline, queue the work on an unbound CPU. */
		if (unlikely(cpu > rnp->grphi - rnp->grplo))
			cpu = WORK_CPU_UNBOUND;
		else
			cpu += rnp->grplo;
		queue_work_on(cpu, rcu_par_gp_wq, &rnp->rew.rew_work);
		rnp->exp_need_flush = true;
	}

	/* Wait for workqueue jobs (if any) to complete. */
	rcu_for_each_leaf_node(rnp)
		if (rnp->exp_need_flush)
			flush_work(&rnp->rew.rew_work);
}
```

#### 关键函数 call_rcu

```

```



```c
static void
__call_rcu(struct rcu_head *head, rcu_callback_t func)
{
	unsigned long flags;
	struct rcu_data *rdp;
	bool was_alldone;

	...

	head->func = func;
	head->next = NULL;
	local_irq_save(flags);
	kasan_record_aux_stack_noalloc(head);
	rdp = this_cpu_ptr(&rcu_data);

	/* Add the callback to our list. */
	if (unlikely(!rcu_segcblist_is_enabled(&rdp->cblist))) {
		// This can trigger due to call_rcu() from offline CPU:
		WARN_ON_ONCE(rcu_scheduler_active != RCU_SCHEDULER_INACTIVE);
		WARN_ON_ONCE(!rcu_is_watching());
		// Very early boot, before rcu_init().  Initialize if needed
		// and then drop through to queue the callback.
		if (rcu_segcblist_empty(&rdp->cblist))
			rcu_segcblist_init(&rdp->cblist);
	}

	check_cb_ovld(rdp);
	if (rcu_nocb_try_bypass(rdp, head, &was_alldone, flags))
		return; // Enqueued onto ->nocb_bypass, so just leave.
	// If no-CBs CPU gets here, rcu_nocb_try_bypass() acquired ->nocb_lock.
	rcu_segcblist_enqueue(&rdp->cblist, head);
	if (__is_kvfree_rcu_offset((unsigned long)func))
		trace_rcu_kvfree_callback(rcu_state.name, head,
					 (unsigned long)func,
					 rcu_segcblist_n_cbs(&rdp->cblist));
	else
		trace_rcu_callback(rcu_state.name, head,
				   rcu_segcblist_n_cbs(&rdp->cblist));

	/* Go handle any RCU core processing required. */
	if (IS_ENABLED(CONFIG_RCU_NOCB_CPU) &&
	    unlikely(rcu_segcblist_is_offloaded(&rdp->cblist))) {
		__call_rcu_nocb_wake(rdp, was_alldone, flags); /* unlocks */
	} else {
		__call_rcu_core(rdp, head, flags);
		local_irq_restore(flags);
	}
}
```

#### RCU STALL 分析

##### 检测原理

RCU stall检测机制的核⼼思想是监控宽限期的进展。当宽限期持续时间超过预定阈值时，内核会认为发生了 RCU stall，并输出警告信息。这个阈值由内核配置选项 CONFIG_RCU_CPU_STALL_TIMEOUT 定义，默认为 21s。

检测过程大致如下：

- 宽限期开始时，记录开始时间；

- 周期性检查是否所有 CPU 都报告了静止状态；

- 如果宽限期持续时间超过阈值，触发 stall 警告；

- 收集并打印相关调试信息，如堆栈跟踪；

##### 关键函数 check_cpu_stall

```c
static void check_cpu_stall(struct rcu_data *rdp)
{
	unsigned long gs1;
	unsigned long gs2;
	unsigned long gps;
	unsigned long j;
	unsigned long jn;
	unsigned long js;
	struct rcu_node *rnp;

	...
        
    // 获取当前时间和宽限期开始时间
	j = jiffies;
	gs1 = READ_ONCE(rcu_state.gp_seq);
	smp_rmb(); /* Pick up ->gp_seq first... */
	js = READ_ONCE(rcu_state.jiffies_stall);
	smp_rmb(); /* ...then ->jiffies_stall before the rest... */
	gps = READ_ONCE(rcu_state.gp_start);
	smp_rmb(); /* ...and finally ->gp_start before ->gp_seq again. */
	gs2 = READ_ONCE(rcu_state.gp_seq);
    /* No stall or GP completed since entering function. */
    // 宽限期已结束或还没到检查时间，直接返回
	if (gs1 != gs2 ||
	    ULONG_CMP_LT(j, js) ||
	    ULONG_CMP_GE(gps, js))
		return;

	rnp = rdp->mynode;
	jn = jiffies + 3 * rcu_jiffies_till_stall_check() + 3;
	if (rcu_gp_in_progress() &&
	    (READ_ONCE(rnp->qsmask) & rdp->grpmask) &&
	    cmpxchg(&rcu_state.jiffies_stall, js, jn) == js) {

		...

		/* We haven't checked in, so go dump stack. */
		print_cpu_stall(gps); // 打印堆栈信息
		if (READ_ONCE(rcu_cpu_stall_ftrace_dump))
			rcu_ftrace_dump(DUMP_ALL);

	} else if (rcu_gp_in_progress() &&
		   ULONG_CMP_GE(j, js + RCU_STALL_RAT_DELAY) &&
		   cmpxchg(&rcu_state.jiffies_stall, js, jn) == js) {

		...

		/* They had a few time units to dump stack, so complain. */
		print_other_cpu_stall(gs2, gps); // 打印堆栈信息
		if (READ_ONCE(rcu_cpu_stall_ftrace_dump))
			rcu_ftrace_dump(DUMP_ALL);
	}
}
```

```c
2025-04-16T13:46:17.886608+08:00 XP5 kernel: [  114.753469@16][       in:imklog][   C16] rcu: INFO: rcu_preempt detected stalls on CPUs/tasks:
2025-04-16T13:46:17.886610+08:00 XP5 kernel: [  114.753472@16][       in:imklog][   C16] rcu:   Tasks blocked on level-1 rcu_node (CPUs 10-19): P6132/1:b..l
2025-04-16T13:46:17.886611+08:00 XP5 kernel: [  114.753478@16][       in:imklog][   C16]    (detected by 16, t=7502 jiffies, g=18517, q=394831)
2025-04-16T13:46:17.886612+08:00 XP5 kernel: [  114.753482@16][       in:imklog][   C16] task:rtps-writer1    state:S stack:    0 pid: 6132 ppid:  2839 flags:0x00000204
2025-04-16T13:46:17.886622+08:00 XP5 kernel: [  114.753486@16][       in:imklog][   C16] Call trace:
2025-04-16T13:46:17.886623+08:00 XP5 kernel: [  114.753487@16][       in:imklog][   C16]  __switch_to+0xb4/0x10c
2025-04-16T13:46:17.886624+08:00 XP5 kernel: [  114.753498@16][       in:imklog][   C16]  __schedule+0x26c/0x670
2025-04-16T13:46:17.886625+08:00 XP5 kernel: [  114.753503@16][       in:imklog][   C16]  schedule+0x84/0x100
2025-04-16T13:46:17.886626+08:00 XP5 kernel: [  114.753504@16][       in:imklog][   C16]  futex_wait_queue_me+0xb4/0x114
2025-04-16T13:46:17.886627+08:00 XP5 kernel: [  114.753509@16][       in:imklog][   C16]  futex_wait+0xd0/0x1e0
2025-04-16T13:46:17.886628+08:00 XP5 kernel: [  114.753512@16][       in:imklog][   C16]  do_futex+0x14c/0xc54
2025-04-16T13:46:17.886629+08:00 XP5 kernel: [  114.753515@16][       in:imklog][   C16]  __arm64_sys_futex+0xf8/0x18c
2025-04-16T13:46:17.886630+08:00 XP5 kernel: [  114.753518@16][       in:imklog][   C16]  el0_svc_common.constprop.0+0x7c/0x1c0
2025-04-16T13:46:17.886631+08:00 XP5 kernel: [  114.753521@16][       in:imklog][   C16]  do_el0_svc+0x28/0x84
2025-04-16T13:46:17.886631+08:00 XP5 kernel: [  114.753524@16][       in:imklog][   C16]  el0_svc+0xc/0x14
2025-04-16T13:46:17.886632+08:00 XP5 kernel: [  114.753527@16][       in:imklog][   C16]  el0_sync_handler+0x100/0x10c
2025-04-16T13:46:17.886634+08:00 XP5 kernel: [  114.753530@16][       in:imklog][   C16]  el0_sync+0x168/0x180
```



#### 总结

多个 reader 可以无锁访问临界资源，updater 在 update 动作前，需要先 copy 一份数据块的副本，在副本中修改完成后，再将新的副本再重新 assign 到链表中替代旧的数据块。

那旧的数据块怎么办呢？当然是要回收了，那么什么时候回收呢？当然是要在没人访问它时再回收了，那怎么如何确定没人访问它了呢？当然是在宽限期（grace period）结束之后就可以确定了，那宽限期什么时候会结束呢？当然是在所有的 CPU 都经过一次静止态（Quiescent state）时就结束了，那 CPU 怎么才算度过一次静止态呢？

### 问题

- 在[关键函数 spin_lock](#关键函数spin_lock) 中涉及到关闭内核抢占，关于内核抢占的东西可以看看[这篇](https://zhuanlan.zhihu.com/p/405965784)文章，讲的很全面。
- rcu stall 原因：持锁后睡眠，cpu 卡住，looping；
- 使用 spinlock 需要注意的是，如果中断上下文会和进程上下文持有同一把锁，那么要使用 raw_spin_lock_irqsave，即关中断持有锁；
- 之后如果遇到在中断上下文等待锁，但是没有看到其他进程持有该锁，那么可以考虑是本 CPU 的进程持有了该锁；

### Reference

[^1]: 《奔跑吧 Linux 内核》卷 2 笨叔.
[^2]: [内核抢占](https://zhuanlan.zhihu.com/p/405965784)
[^3]: [spinlock 变体的区别](https://www.byteisland.com/%E8%87%AA%E6%97%8B%E9%94%81-spin_lock%E3%80%81-spin_lock_irq-%E4%BB%A5%E5%8F%8A-spin_lock_irqsave-%E7%9A%84%E5%8C%BA%E5%88%AB/)
[^4]: [rcu-LoyenWang YYDS:sunny:](https://www.cnblogs.com/LoyenWang/p/12681494.html)

[^5]: [rcu_flow](https://zhuanlan.zhihu.com/p/374902282)
