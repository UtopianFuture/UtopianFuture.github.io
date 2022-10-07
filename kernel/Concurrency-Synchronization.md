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
  - [关键函数spin_lock](#关键函数spin_lock)
- [MCS锁](#MCS锁)
  - [optimistic_spin_node](#optimistic_spin_node)
- [排队自旋锁](#排队自旋锁)
  - [qspinlock](#qspinlock)
- [信号量](#信号量)
  - [semaphore](#semaphore)
  - [关键函数down](#关键函数down)
  - [关键函数up](#关键函数up)

- [互斥锁](#互斥锁)
  - [mutex](#mutex)
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

### 锁机制

临界区是指访问和操作共享数据的代码段，其中的资源无法同时被多个执行线程访问，访问临界区的执行线程或代码路径称为并发源，在内核中产生并发访问的并发源主要有如下 4 种：

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

实际的项目中，真正的困难是如何发现内核代码存在并发访问的可能并采取保护措施。因此在编写代码时，需要考虑哪些资源位于临界区，应该采取哪些保护措施。任何可能从多个内核代码路径访问的数据都需要保护，保护对象包括静态全局变量、全局变量、共享的数据结构、缓存、链表、红黑树等各种形式锁隐含的数据。因此在编写内核和驱动代码时，需要对数据作如下考虑：

- 除了从当前代码路径外，是否还可能从其他内核代码路径访问这些数据？如中断处理程序、 worker、tasklet、软中断等等，也许写代码的时候在可能出问题的地方加上一段代码，检测所有的访问入口。
- 若当前内核代码访问数据时发生被抢占，被调度、执行的进程会不会访问这些数据？
- 进程会不会进入睡眠状态以等待该数据？

故内核提供了多种并发访问的保护机制，这些机制有各自的应用场景，

| 锁机制                    | 特点                                                         | 使用规则                                                     |
| ------------------------- | ------------------------------------------------------------ | ------------------------------------------------------------ |
| 原子操作 - atomic         | 使用处理器的原子指令，开销小                                 | 临界区中的数据是变量、位（这是什么？）等简单的数据结构       |
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
- {}_acquire：内联加载-获取内存屏障原语；
- {}_release：内联存储-释放内存屏障原语；

### 内存屏障

在 ARM 架构中有 3 条内存屏障指令：

- 数据存储屏障（Data Memory Barrier, DMB）指令：确保在执行新的存储器访问前所有的存储器访问都已经完成；
- 数据同步屏障（Data Synchronization Barrier, DSB）指令：确保在下一个指令执行前所有存储器访问都已经完成；
- 指令同步屏障（Instruction Synchronization Barrier, ISB）指令：清空流水线，确保在执行新的指令前，之前所有的指令都已完成；

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
/* Optimization barrier */
/* The "volatile" is due to gcc bugs */
#define barrier() __asm__ __volatile__("": : :"memory")

#define mb() 	asm volatile("mfence":::"memory")
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

#### 关键函数spin_lock

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
	preempt_disable(); // 关闭内核抢占，为什么是调用 barrier，而且什么时候开启呢？
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

### MCS锁

在一个锁争用激烈的系统中，**所有等待自旋锁的线程都在同一个共享变量上自旋**，申请和释放锁也都在一个变量上修改，cache 一致性原理导致参与自旋的 CPU 的 cache 行无效，也可能导致 cache 颠簸现象（这种现象是怎样发现的，又是如何确定其和自旋锁有关），导致系统性能降低。

MCS 算法可以解决 cache 颠簸问题，其关键思想是**每个锁的申请者只在本地 CPU 的变量上自旋，而不是全局变量上**，各个 CPU 会等待其他 CPU 将锁传给自己，即每个持有锁的 CPU 在释放锁时会将 next->locked 设为 1。OSQ 锁是 MCS 锁机制的一种实现，而后来内核又引进了基于 MCS 机制的排队自旋锁，后面介绍。我开始以为实现了 qspinlock 后内核应该只会使用 qspinlock，其实不是， osq 也有使用，大部分是在互斥锁中使用。

```
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

上面介绍的 MCS 锁能够解决 cache 颠簸问题，但是其会使 spinlock 数据结构变大，而 spinlock 在内核中的应用十分广泛，有些地方对数据结构的大小十分敏感，所以 MCS 机制一直没有应用到 spinlock 中，spinlock 也一直没有解决 cache 颠簸问题，直到排队自旋锁出现。

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
| 16 - 17 | tail_idx 域，用来获取 q_nodes，目前支持 4 种上下文的 msc_nodes —— 进程上下文、软中断上下文、硬中断上下文和不可屏蔽中断上下文（不懂） |
| 18 - 31 | tail_cpu 域，用来标识等待队列末尾的 CPU                      |

内核使用一个三元组 {x, y, z} 来表示锁的状态，`x` 表示 tail，`y` 表示 pending，`z` 表示 locked。

#### 快速申请通道

申请 `qspinlock` 的接口为 `queued_spin_lock`，

```c
static __always_inline void queued_spin_lock(struct qspinlock *lock)
{
	int val = 0;

    // 自旋锁没有被持有，返回 true
    // 并将 lock->val 的 _Q_LOCKED_VAL 位置为 1
    // 这就是快速路径，不然就是慢速路径
	if (likely(atomic_try_cmpxchg_acquire(&lock->val, &val, _Q_LOCKED_VAL))) // _Q_LOCKED_VAL = 0
		return;

	queued_spin_lock_slowpath(lock, val);
}
```

我们以一个实际的场景来分析 `qspinlock` 的使用，假设 CPU0，CPU1，CPU2，CPU3 都在进程上下文中争用一个自旋锁。

在开始时，没有 CPU 持有锁，那么 CPU0 通过 `queued_spin_lock` 去申请该锁，这时在 `lock->val` 中，locked = 1，pending = 0，tail = 0，即三元组为 {0, 0, 1}。

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
    // 上面介绍过，pending 域表示第一顺位继承者，自旋等待锁释放
	if (val == _Q_PENDING_VAL) { // _Q_PENDING_VAL = 8
		int cnt = _Q_PENDING_LOOPS;
		val = atomic_cond_read_relaxed(&lock->val,
					       (VAL != _Q_PENDING_VAL) || !cnt--);
	}

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
	val = queued_fetch_set_pending_acquire(lock); // 第一顺位继承者在这里设置 pending 域

	/*
	 * If we observe contention, there is a concurrent locker.
	 *
	 * Undo and queue; our setting of PENDING might have made the
	 * n,0,0 -> 0,0,0 transition fail and it will now be waiting
	 * on @next to become !NULL.
	 */
	if (unlikely(val & ~_Q_LOCKED_MASK)) { // 为什么这里还要判断一次？

		/* Undo PENDING if we set it. */
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
	if (val & _Q_LOCKED_MASK)
		atomic_cond_read_acquire(&lock->val, !(VAL & _Q_LOCKED_MASK));

	/*
	 * take ownership and clear the pending bit.
	 *
	 * 0,1,0 -> 0,0,1
	 */
	clear_pending_set_locked(lock);
	lockevent_inc(lock_pending); // CPU1 成功持有该锁
	return;

    ...
}
```

看图更加直观，

![qspinlock-2.png](https://github.com/UtopianFuture/UtopianFuture.github.io/blob/master/image/qspinlock-2.png?raw=true)

#### 慢速申请通道

慢速通道的场景是 CPU0 持有锁，CPU1 设置 pending 域并自旋等待，这时 CPU2 也申请该自旋锁，这时就是跳转到 queue 处，

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
	lockevent_inc(lock_slowpath);
pv_queue:
	node = this_cpu_ptr(&qnodes[0].mcs);
	idx = node->count++;
	tail = encode_tail(smp_processor_id(), idx);

	/*
	 * 4 nodes are allocated based on the assumption that there will
	 * not be nested NMIs taking spinlocks. That may not be true in
	 * some architectures even though the chance of needing more than
	 * 4 nodes will still be extremely unlikely. When that happens,
	 * we fall back to spinning on the lock directly without using
	 * any MCS node. This is not the most elegant solution, but is
	 * simple enough.
	 */
	if (unlikely(idx >= MAX_NODES)) {
		lockevent_inc(lock_no_node);
		while (!queued_spin_trylock(lock))
			cpu_relax();
		goto release;
	}

	node = grab_mcs_node(node, idx);

	/*
	 * Keep counts of non-zero index values:
	 */
	lockevent_cond_inc(lock_use_node2 + idx - 1, idx);

	/*
	 * Ensure that we increment the head node->count before initialising
	 * the actual node. If the compiler is kind enough to reorder these
	 * stores, then an IRQ could overwrite our assignments.
	 */
	barrier();

	node->locked = 0;
	node->next = NULL;
	pv_init_node(node);

	/*
	 * We touched a (possibly) cold cacheline in the per-cpu queue node;
	 * attempt the trylock once more in the hope someone let go while we
	 * weren't watching.
	 */
	if (queued_spin_trylock(lock))
		goto release;

	/*
	 * Ensure that the initialisation of @node is complete before we
	 * publish the updated tail via xchg_tail() and potentially link
	 * @node into the waitqueue via WRITE_ONCE(prev->next, node) below.
	 */
	smp_wmb();

	/*
	 * Publish the updated tail.
	 * We have already touched the queueing cacheline; don't bother with
	 * pending stuff.
	 *
	 * p,*,* -> n,*,*
	 */
	old = xchg_tail(lock, tail);
	next = NULL;

	/*
	 * if there was a previous node; link it and wait until reaching the
	 * head of the waitqueue.
	 */
	if (old & _Q_TAIL_MASK) {
		prev = decode_tail(old);

		/* Link @node into the waitqueue. */
		WRITE_ONCE(prev->next, node);

		pv_wait_node(node, prev);
		arch_mcs_spin_lock_contended(&node->locked);

		/*
		 * While waiting for the MCS lock, the next pointer may have
		 * been set by another lock waiter. We optimistically load
		 * the next pointer & prefetch the cacheline for writing
		 * to reduce latency in the upcoming MCS unlock operation.
		 */
		next = READ_ONCE(node->next);
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

	val = atomic_cond_read_acquire(&lock->val, !(VAL & _Q_LOCKED_PENDING_MASK));

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
	if ((val & _Q_TAIL_MASK) == tail) {
		if (atomic_try_cmpxchg_relaxed(&lock->val, &val, _Q_LOCKED_VAL))
			goto release; /* No contention */
	}

	/*
	 * Either somebody is queued behind us or _Q_PENDING_VAL got set
	 * which will then detect the remaining tail and queue behind us
	 * ensuring we'll see a @next.
	 */
	set_locked(lock);

	/*
	 * contended path; wait for next if not observed yet, release.
	 */
	if (!next)
		next = smp_cond_load_relaxed(&node->next, (VAL));

	arch_mcs_spin_unlock_contended(&next->locked);
	pv_kick_node(lock, next);

release:
	/*
	 * release the node
	 */
	__this_cpu_dec(qnodes[0].mcs.count);
}
```

好吧，这个对我而言有点复杂，之后再分析。

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

#### 关键函数down

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

#### 关键函数up

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

这部分不是很懂！我们结合流程图看，加深印象。

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

比较信号量的 `down` 函数和互斥锁的 `__mutex_lock_slowpath`，我们发现互斥锁在睡眠前会先尝试自旋等待获取锁，而信号量是直接去睡眠，这就是为什么互斥锁要比信号量高效。

而互斥锁的轻便和高效使得其比信号的使用场景要求更加严格：

- 同一时刻时只有一个线程可以持有互斥锁，而信号量的 `count` 可以不为 1，即计数信号量；
- 只有锁持有者可以解锁，因此其不适用于内核和用户空间复杂的同步场景；
- 不允许递归的加锁解锁；
- 当进程持有互斥锁时，进程不能退出（？）；
- 互斥锁必须使用官方接口来初始化；
- 互斥锁可以睡眠，所以不能在中断上下半部使用；

那么在写代码时该如何使用自旋锁、信号量和互斥锁呢？

在中断上下半部可以使用自旋锁；而如果临界区有睡眠、隐含睡眠的动作以内核接口函数（？），应避免使用自旋锁；而信号量和互斥锁的选择，除非使用环境不符合上述任何一条，否则优先使用互斥锁（怪不得互斥锁更为常见）。

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
extern void down_write(struct rw_semaphore *sem);
extern int __must_check down_write_killable(struct rw_semaphore *sem);

/*
 * trylock for writing -- returns 1 if successful, 0 if contention
 */
extern int down_write_trylock(struct rw_semaphore *sem);

/*
 * release a read lock
 */
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

之前总听到师兄说 RCU 很复杂，很难，这里开始啃吧！

RCU 全称 Read-Copy-Update，首先我们需要考虑一个问题，前面介绍到内核中已有很多锁机制，那为何还要设计复杂的 RCU 机制。

因为内存屏障、自旋锁、信号量、读写信号量等都需要使用原子操作，而多 CPU 争用共享变量时会让 **cache 一致性协议性能很低**（确实，因为需要不断更新不同 CPU 中 cache 的内容）。以读写信号量为例，除了上述问题，**其只允许多个读者存在，但是读者和写者不能同时存在**。

RCU 的目标是使读者线程没有同步开销，或者同步开销很小，不需要额外的锁，也不需要原子操作和内存屏障即可快速的访问；而将同步任务交给写者线程，写者线程会创建一个副本，在副本中进行修改，之后等待所有读者线程完成后才将旧数据销毁，使指针指向新数据。

RCU 提供的接口如下：

- `rcu_read_lock`/`rcu_read_unlock`：构成一个读者临界区；
- `rcu_dereference`：用于获取被 RCU 保护的指针，读者线程要访问 RCU 保护的贡献数据，需要使用该函数创建一个新指针，指向被 RCU 保护的指针？
- `rcu_assign_pointer`：在写者线程完成数据更新后，调用该接口可以让被 RCU 保护的指针指向新建数据；
- `synchronize_rcu`：同步所有的读者进程；
- `call_rcu`：注册一个回调函数，当所有现存的读访问完成后，调用这个函数销毁旧数据。

关于这些接口的使用以及更详细的说明，可以看 Documents/RCU/whatisRCU.rst。书中该章节有一个简单的例子，看看他可以加深对这些接口的理解。

RCU 中有两个重要概念：

- 宽限期（Grace Period, GP）。GP 有生命周期，有开始和结束之分，从 GP 开始算起，如果所有处于读者临界区的 CPU 都离开了临界区，也就是经历了一次 QS，那么认为一个 GP 结束了。GP 结束后，RCU 会调用注册的回调函数，如销毁旧数据等。
- 静止状态（Quiescent State）。如果一个 CPU 处于读者临界区，那么认为它是活跃的，如果时钟周期中检测到该 CPU 处于用户模式或空闲状态，说明该 CPU 已经离开了读者临界区，那么它是 QS 的。

### 问题

- 在[关键函数spin_lock](#关键函数spin_lock) 中涉及到关闭内核抢占，关于内核抢占的东西可以看看[这篇](https://zhuanlan.zhihu.com/p/405965784)文章，讲的很全面。

### Reference

[1] 《奔跑吧 Linux 内核》卷 2 苯叔.

[2] https://zhuanlan.zhihu.com/p/405965784
