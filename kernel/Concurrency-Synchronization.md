## Concurrency and Synchronization

临界区是指访问和操作共享数据的代码段，其中的资源无法同时被多个执行线程访问，访问临界区的执行线程或代码路径称为并发源，在内核中产生并发访问的并发源主要有如下 4 中：

- 中断和异常：中断发生后，中断处理程序和被中断的进程之间可能产生并发访问；
- 软中断和 tasklet：因为它们的优先级比进程高，所以会打断当前正在执行的进程上下文；
- 内核抢占：多进程之间的并发访问；
- 多处理器并发执行：多个处理器同时执行多个进程。

在 SMP 系统中情况会复杂一些：

- 同一类型的中断处理程序不会并发执行，但是不同类型的中断可能送达不同的 CPU，因此不同类型的中断处理程序可能会并发执行；
- 同一类型的软中断会在不同的 CPU 上并发执行；
- 同一类型的 tasklet 是串行执行的，不会在多个 CPU 上并发执行；
- 不同 CPU 上的进程上下文会并发执行。

真实的场景是这样的，如在进程上下文中操作某个临界区的资源时发生了中断，而在中断处理程序中也访问了这个资源，如果不使用内核同步机制，那么可能会触发 bug。

实际的项目中，真正的困难是如何发现内核代码存在并发访问的可能并采取保护措施。因此在编写代码时，需要考虑哪些资源位于临界区，应该采取哪些保护措施。任何可能从多个内核代码路径访问的数据都需要保护，保护对象包括静态全局变量、全局变量、共享的数据结构、缓存、链表、红黑树等各种形式锁隐含的数据。因此在编写内核和驱动代码时，需要对数据作如下考虑：

- 除了从当前代码路径外，是否还可能从其他内核代码路径访问这些数据？如中断处理程序、 worker、tasklet、软中断等等，也许写代码的时候在可能出问题的地方加上一段代码，检测所有的访问入口。
- 若当前内核代码访问数据时发生被抢占，被调度、执行的进程会不会访问这些数据？
- 进程会不会进入睡眠状态以等待该数据？

故内核提供了多种并发访问的保护机制，这些机制有各自的应用场景，

| 锁机制     | 特点                                                         | 使用规则                                                     |
| ---------- | ------------------------------------------------------------ | ------------------------------------------------------------ |
| 原子操作   | 使用处理器的原子指令，开销小                                 | 临界区中的数据是变量、位（这是什么？）等简单的数据结构       |
| 内存屏障   | 使用处理器内存屏障指令或 GCC 的屏障指令                      | 读写指令时序的调整                                           |
| 自旋锁     | 自旋等待                                                     | 中断上下文，短期持有锁，不可递归，临界区不可睡眠             |
| 信号量     | 可睡眠的锁                                                   | 可长时间持有锁                                               |
| 读写信号量 | 可睡眠的锁，多个读者可以同时持有锁，同一时刻只能有一个写者，读者和写者不能同时存在 | 程序员界定出临界区后读/写属性才有用                          |
| 互斥锁     | 可睡眠的互斥锁，比信号量快速和简洁，实现自旋等待机制         | 同一时刻只有一个线程可以持有互斥锁，由锁持有者负责解锁，即在同一个上下文中解锁，不能递归持有锁，不适合内核和用户空间复杂的同步场景 |
| RCU        | 读者持有锁没有开销，多个读者和写者可以同时共存，写者必须等待所有读者离开临界区后才能销毁相关数据 | 受保护资源必须通过指针访问，如链表等                         |

### 原子操作

原子操作是指保证指令以原子的方式执行，执行过程不会被打断。例如我们假设 `i` 是一个全局变量，那么一条很简单的 `i++` 指令其实包括读取，修改，写回 3 个操作，如果不加以保护，在上面的几个并发源中这条简单的指令都可能出错。因此内核提供了 `atomic_t` 类型的原子变量。

```c
typedef struct {
	int counter;
} atomic_t;
```

内核中也有一系列的原子操作来操作这些原子变量，如 `atomic_inc(v)`，`atomic_dec(v)` 等，然后 CPU 必须提供原子操作的汇编指令来保证原子变量的完整性，如 `xadd`，`cmpxchg` 等。

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
| smp_rmb()                                      |                                                              |
| smp_wmb()                                      |                                                              |
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

### 经典自旋锁

如果临界区只有一个变量，那么适合用原子变量，但是大多数情况临界区中是一个数据操作的集合，例如一个链表及相关操作。那么在整个执行过程中都需要保证原子性，即在数据更新完毕前，不能从其他内核代码路径访问和更改这些数据，这就需要用锁来完成。自旋锁是内核中最常见的一种锁机制。自旋锁的特性如下：

- 忙等。OS 中的锁可分为两类，一类是忙等，一类是睡眠等待。当内核路径无法获取自旋锁时会不断尝试，直到获取到锁为止；
- 同一时刻只能有一个内核代码路径可以获得锁；
- 持有者要尽快完成临界区的执行任务。自旋锁临界区不能睡眠；
- 可以在中断上下文使用；

#### 自旋锁的实现

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
	preempt_disable(); // 关闭内核抢占，为什么是调用 barrier
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

这里有一点需要注意，`spin_lock_irq` 只会关闭本地中断，其他 CPU 还是可以响应中断，如果 CPU0 持有该自旋锁，而 CPU1 响应中断且中断处理程序也申请该自旋锁，那么等 CPU0 释放后即可获取。

### MCS 锁

在一个锁争用激烈的系统中，所有等待自旋锁的线程都在同一个共享变量上自旋，申请和释放锁也都在一个变量上修改，cache 一致性原理导致参与自旋的 CPU 的 cache 行无效，也可能导致 cache 颠簸现象（这种现象是怎样发现的，又是如何确定其和自旋锁有关），导致系统性能降低。

MCS 算法可以解决 cache 颠簸问题，其关键思想是**每个锁的申请者只在本地 CPU 的变量上自旋，而不是全局变量上**。OSQ 锁是 MCS 锁机制的一种实现，而后来内核又引进了基于 MCS 机制的排队自旋锁，后面介绍。我开始以为实现了 qspinlock 后内核应该只会使用 qspinlock，其实不是 osq 也有使用。不大部分是在互斥锁中使用。

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

其实整个 OSQ 实现并不复杂，毕竟代码才 200 多行。 `osq_lock` 用来申请 MCS 锁，其分为 3 中情况，我们一个个来看，

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

排队自旋锁能够解决在争用激烈场景下导致的性能下降问题。

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
		struct {
			u16	tail;
			u16	locked_pending;
		};
		struct {
			u8	reserved[2];
			u8	pending;
			u8	locked;
		};
#endif
	};
} arch_spinlock_t;
```

`qspinlock` 将 val 字段分成多个域，

| 位      | 描述                                                         |
| ------- | ------------------------------------------------------------ |
| 0 - 7   | locked 域，表示持有锁                                        |
| 8       | pending 域，表示第一顺位继承者，自旋等待锁释放               |
| 9 - 15  | 保留位                                                       |
| 16 - 17 | tail_idx 域，用来获取 q_nodes，目前支持 4 种上下文的 msc_nodes —— 进程上下文、软中断上下文、硬中断上下文和不可屏蔽中断上下文 |
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

![qspinlock](/home/guanshun/gitlab/UFuture.github.io/image/qspinlock.png)

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
		if (!(val & _Q_PENDING_MASK))
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

![qspinlock-2](/home/guanshun/gitlab/UFuture.github.io/image/qspinlock-2.png)

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



#### 释放锁

### 信号量

### 互斥锁

### 读写锁

### 读写信号量

### RCU
