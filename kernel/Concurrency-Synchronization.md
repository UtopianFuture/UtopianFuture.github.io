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

### 排队自旋锁

### 信号量

### 互斥锁

### 读写锁

### 读写信号量

### RCU
