## ION 内存管理

这里先看 LWN 文章[^1] ，然后进一步分析。

ION 是 PMEM 的继承者，目的主要是通过**在硬件设备和用户空间之间分配和共享内存**，实现设备之间零拷贝[^4]共享内存（概念很简单，但是对这些功能缺乏实际的认识，所以难以理解。**需要搞懂如何做到共享内存**）。

ION 主要功能：

- 内存管理器：提供通用的内存管理接口，通过 heap 管理各种类型的内存；
- 共享内存：可提供驱动之间、用户进程之间、内核空间和用户空间之间的共享内存。

### 基本结构

下图展示了 ION 的基本框架。图中 PID1、PID2、PIDn 表示用户空间进程。ION core 表示 ION 核心层，它提供设备创建、注册等服务，同时提供统一的接口给用户使用。ION Driver 利用 ION core 对相应功能进行实现，可以说它是具体平台相关的，例如 SAMSUNG 平台、QUALCOMM 平台和 MTK 平台都会依据自己的特性开发相应的 ION Driver(soga)。

![ION](/home/guanshun/gitlab/UFuture.github.io/image/ION.png)



每个 heap 中可分配若干个 buffer，每个 client 通过 handle 管理对应的 buffer（也就是说，handle 是用户态的描述，而 buffer 是内核态的描述）。每个 buffer 只能有一个 handle 对应，每个用户进程只能有一个 client，每个 client 可能有多个 handle。两个 client 利用文件描述符 fd（和 handle 有所对应，通过 handle 获取），通过映射方式，将相应内存映射，实现共享内存[^5]。

![ion_heap](/home/guanshun/gitlab/UFuture.github.io/image/ion_heap.png)

### ION heaps

ION 管理一个或多个内存池，这些内存池部分是在 boot 阶段创建的，用来防止碎片化；部分是为了为特殊的硬件服务的，例如 GPU，显示控制器，相机等块设备。ION 将这些内存池以 ION heaps 的形式来管理。每个 Android 设备能够根据自身需求申请多个不同的 ION heaps。

#### ion_heap_type

ION 提供多种不同的 ION heaps：

```c
/**
 * enum ion_heap_types - list of all possible types of heaps
 * @ION_HEAP_TYPE_SYSTEM:	 memory allocated via vmalloc
 * @ION_HEAP_TYPE_SYSTEM_CONTIG: memory allocated via kmalloc
 * @ION_HEAP_TYPE_CARVEOUT:	 memory allocated from a prereserved
 *				 carveout heap, allocations are physically
 *				 contiguous
 * @ION_HEAP_TYPE_DMA:		 memory allocated via DMA API
 * @ION_NUM_HEAPS:		 helper for iterating over heaps, a bit mask
 *				 is used to identify the heaps, so only 32
 *				 total heap types are supported
 */
enum ion_heap_type {
	ION_HEAP_TYPE_SYSTEM,
	ION_HEAP_TYPE_SYSTEM_CONTIG,
	ION_HEAP_TYPE_CARVEOUT,
	ION_HEAP_TYPE_CHUNK,
	ION_HEAP_TYPE_DMA,
	ION_HEAP_TYPE_CUSTOM, /*
			       * must be last so device specific heaps always
			       * are at the end of this enum
			       */
	ION_NUM_HEAPS = 16,
};
```

开发者可以根据自身情况增加 type。

#### ion_heap_ops

ION 必须实现如下接口：

```c
/**
 * struct ion_heap_ops - ops to operate on a given heap
 * @allocate:		allocate memory
 * @free:		free memory. Will be called with
 *			ION_PRIV_FLAG_SHRINKER_FREE set in buffer flags when
 *			called from a shrinker. In that case, the pages being
 *			free'd must be truly free'd back to the system, not put
 *			in a page pool or otherwise cached.
 * @phys		get physical address of a buffer (only define on
 *			physically contiguous heaps)
 * @map_dma		map the memory for dma to a scatterlist
 * @unmap_dma		unmap the memory for dma
 * @map_kernel		map memory to the kernel
 * @unmap_kernel	unmap memory to the kernel
 * @map_user		map memory to userspace
 * @unmap_user		unmap memory to userspace
 *
 * allocate, phys, and map_user return 0 on success, -errno on error.
 * map_dma and map_kernel return pointer on success, ERR_PTR on
 * error. @free will be called with ION_PRIV_FLAG_SHRINKER_FREE set in
 * the buffer's private_flags when called from a shrinker. In that
 * case, the pages being free'd must be truly free'd back to the
 * system, not put in a page pool or otherwise cached.
 */
struct ion_heap_ops {
	int (*allocate)(struct ion_heap *heap, // 分配 ion_buffer 的
			struct ion_buffer *buffer, unsigned long len,
			unsigned long align, unsigned long flags);
	void (*free)(struct ion_buffer *buffer);
	int (*phys)(struct ion_heap *heap, struct ion_buffer *buffer,
		    ion_phys_addr_t *addr, size_t *len);
	struct sg_table * (*map_dma)(struct ion_heap *heap,
				     struct ion_buffer *buffer);
	void (*unmap_dma)(struct ion_heap *heap, struct ion_buffer *buffer);
	void * (*map_kernel)(struct ion_heap *heap, struct ion_buffer *buffer);
	void (*unmap_kernel)(struct ion_heap *heap, struct ion_buffer *buffer);
	int (*map_user)(struct ion_heap *mapper, struct ion_buffer *buffer,
			struct vm_area_struct *vma);
	int (*shrink)(struct ion_heap *heap, gfp_t gfp_mask, int nr_to_scan);
	void (*unmap_user)(struct ion_heap *mapper, struct ion_buffer *buffer);
	int (*print_debug)(struct ion_heap *heap, struct seq_file *s,
			   const struct list_head *mem_map);
};
```

之后会分析 heap 的具体实现。

#### ion_heap

ION 是在各种 heaps 上分配内存，通过 ion_buffer 来描述所分配的内存。

```c
/**
 * struct ion_heap - represents a heap in the system
 * @node:		rb node to put the heap on the device's tree of heaps
 * @dev:		back pointer to the ion_device
 * @type:		type of heap
 * @ops:		ops struct as above
 * @flags:		flags
 * @id:			id of heap, also indicates priority of this heap when
 *			allocating.  These are specified by platform data and
 *			MUST be unique
 * @name:		used for debugging
 * @shrinker:		a shrinker for the heap
 * @priv:		private heap data
 * @free_list:		free list head if deferred free is used
 * @free_list_size	size of the deferred free list in bytes
 * @lock:		protects the free list
 * @waitqueue:		queue to wait on from deferred free thread
 * @task:		task struct of deferred free thread
 * @debug_show:		called when heap debug file is read to add any
 *			heap specific debug info to output
 *
 * Represents a pool of memory from which buffers can be made.  In some
 * systems the only heap is regular system memory allocated via vmalloc.
 * On others, some blocks might require large physically contiguous buffers
 * that are allocated from a specially reserved heap.
 */
struct ion_heap {
	struct plist_node node;
	struct ion_device *dev;
	enum ion_heap_type type;
	struct ion_heap_ops *ops; // 每一种 ion_heap 都要实现对应的 ops
	unsigned long flags;
	unsigned int id;
	const char *name;
	struct shrinker shrinker; // 又遇到了 shrinker
	void *priv;
	struct list_head free_list;
	size_t free_list_size;
	spinlock_t free_lock;
	wait_queue_head_t waitqueue;
	struct task_struct *task;

	int (*debug_show)(struct ion_heap *heap, struct seq_file *, void *);
	atomic_long_t total_allocated;
	atomic_long_t total_handles;
};
```

#### ion_buffer

`struct ion_buffer` 很重要，通过 ION 分配的内存就是通过它表示的。它和上面提到的 ion_handle 的区别主要在于一个是用户空间使用的，一个是内核空间使用的。即虽然常用的接口函数中使用的是 ion_handle，但实际上真正表示内存的其实是 ion_buffer。

```c
/**
 * struct ion_buffer - metadata for a particular buffer
 * @ref:		reference count
 * @node:		node in the ion_device buffers tree
 * @dev:		back pointer to the ion_device
 * @heap:		back pointer to the heap the buffer came from
 * @flags:		buffer specific flags
 * @private_flags:	internal buffer specific flags
 * @size:		size of the buffer
 * @priv_virt:		private data to the buffer representable as
 *			a void *
 * @priv_phys:		private data to the buffer representable as
 *			an ion_phys_addr_t (and someday a phys_addr_t)
 * @lock:		protects the buffers cnt fields
 * @kmap_cnt:		number of times the buffer is mapped to the kernel
 * @vaddr:		the kernel mapping if kmap_cnt is not zero
 * @dmap_cnt:		number of times the buffer is mapped for dma
 * @sg_table:		the sg table for the buffer if dmap_cnt is not zero
 * @pages:		flat array of pages in the buffer -- used by fault
 *			handler and only valid for buffers that are faulted in
 * @vmas:		list of vma's mapping this buffer
 * @handle_count:	count of handles referencing this buffer
 * @task_comm:		taskcomm of last client to reference this buffer in a
 *			handle, used for debugging
 * @pid:		pid of last client to reference this buffer in a
 *			handle, used for debugging
*/
struct ion_buffer {
	struct kref ref;
	union {
		struct rb_node node;
		struct list_head list;
	};
	struct ion_device *dev;
	struct ion_heap *heap;
	unsigned long flags;
	unsigned long private_flags;
	size_t size;
	union {
		void *priv_virt;
		ion_phys_addr_t priv_phys;
	};
	struct mutex lock;
	int kmap_cnt;
	void *vaddr;
	int dmap_cnt;
	struct sg_table *sg_table;
	struct page **pages;
	struct list_head vmas;
	/* used to track orphaned buffers */
	int handle_count;
	char task_comm[TASK_COMM_LEN];
	pid_t pid;
};
```

### Using ION from user space

用户态进程会通过 C/C++ 库使用 ION 分配大的连续的地址空间。例如，camera library 能够为相机设备分配一块 capture buffer，当 buffer 满了时，camera library 能够将 buffer 中的数据传输给内核，然后让 jpeg 硬件加密块设备进行处理（意思是这个过程不需要将数据传输到用户态，直接在内核态传到另一个硬件设备中？）。

它们首先需要保证能够访问 `/dev/ion`（对应上面的 `struct ion_device`），可以通过 `open("dev/ion", O_RDONLY)` 来获取一个文件描述符作为 ION client 的句柄，每个进程只能申请一个 client。在申请空间前，需要填充 `struct ion_allocation_data` 结构，

```c
struct ion_allocation_data {
	size_t len;
	size_t align;
	unsigned int heap_id_mask;
	unsigned int flags;
	ion_user_handle_t handle; // 返回参数，表示申请到的 buffer。这个 buffer CPU 不能访问（？）
};

struct ion_fd_data {
    struct ion_handle *handle;
    int fd;
}

struct ion_handle_data {
    struct ion_handle *handle;
}
```

然后就可以通过 ioctl 分配 buffer。

```c
int ioctl(int client_fd, ION_IOC_ALLOC, struct ion_allocation_data *allocation_data)
```

这个 handle 只能被用来获取一个 buffer sharing 的文件描述符（ION_IOC_ALLOC 不是已经分配好了么，为什么还要再使用一次 ioctl？从后面的描述来看，这个 fd 可以用来在各个 client 之间 share），

```c
int ioctl(int client_fd, ION_IOC_SHARE, struct ion_fd_data *fd_data);
```

在 android 系统中，可能会通过 Binder 机制将共享的文件描述符 fd 发送给另外一个进程。

为了获得被共享的 buffer，第二个用户进程必须通过首先调用 `open("/dev/ion", O_RDONLY)` 获取一个 client handle，ION 通过进程 ID 跟踪它的用户空间 clients。 **在同一个进程中重复调用 `open("/dev/ion", O_RDONLY)` 将会返回另外一个文件描述符号，这个文件描述符号会引用内核同样的 client 结构** 。

获取到共享文件描述符 fd 后，共享进程可以通过 mmap 来操作共享内存。

在释放 buffer 时，第二个 client 需要通过 munmap 来取消 mmap 的效果，第一个 client 需要关闭通过 ION_IOC_SHARE 命令获得的文件描述符号，并且使用 ION_IOC_FREE 如下：

```csharp
int ioctl(int client_fd, ION_IOC_FREE, struct ion_handle_data *handle_data);
```

命令会导致 handle 的引用计数减少 1。当这个引用计数达到 0 的时候，ion_handle 对象会被析构，同时 ION 的索引数据结构被更新。

这里有一个简单的 demo[^2]，

```c
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>

#include ".../goldfish/include/linux/ion.h"

void main()
{
	int *p;
	struct ion_fd_data fd_data;
	struct ion_allocation_data ionAllocData;
	ionAllocData.len=0x1000;
	ionAllocData.align = 0;
	ionAllocData.flags = ION_HEAP_TYPE_SYSTEM;

	int fd=open("/dev/ion",O_RDWR);
	ioctl(fd,ION_IOC_ALLOC, &ionAllocData); // 使用 ION_IOC_ALLOC 分配 buffer
	fd_data.handle = ionAllocData.handle; // handle 表示分配的 buffer
	ioctl(fd,ION_IOC_SHARE,&fd_data);

    p=mmap(0,0x1000,PROT_READ|PROT_WRITE,MAP_SHARED,fd_data.fd,0);

	p[0]=99;
	perror("test");
	printf("hello all %d\n",p[0]);
}
```

### Sharing ION buffers in the kernel

用户进程也可以和内核驱动共享 ION buffer。

内核中可以有多个 ION clients，每个使用 ION 的 driver 拥有一个 client。

```objectivec
struct ion_client *ion_client_create(struct ion_device *dev,unsigned int heap_mask, const char *debug_name)
```

用户传递 **ion 共享文件描述符**给内核驱动，驱动**转成 ion_handle** ：

```rust
struct ion_handle *ion_import_fd(struct ion_client *client, int fd_from_user);
```

在许多包含多媒体中间件的智能手机中，用户进程经常从 ion 中分配 buffer，然后使用 ION_IOC_SHARE 命令获取文件描述符号，然后**将文件描述符号传递给内核驱动**。内核驱动调用 `ion_import_fd` 将文件描述符转换成 ion_handle 对象。内核驱动使用 ion_handle 对象做为对共享 buffer 的 client 本地引用。该函数查找 buffer 的物理地址确认是否这个 client 是否之前分配了同样的 buffer，如果是，则仅增加相应 handle 的引用计数。

有些硬件块只能操作物理地址连续的 buffer，所以相应的驱动应**对 ion_handle 转换** ：

```go
int ion_phys(struct ion_client *client, struct ion_handle *handle, ion_phys_addr_t *addr, size_t *len)
```

若 buffer 的物理地址不连续，这个调用会失败。

在处理 client 的调用之时，ion 始终会对 input file descriptor, client 和 handle arguments 进行确认。例如：当 import 一个 fd 时，ion 会保证这个文件描述符确实是通过 ION_IOC_SHARE 命令创建的。当 `ion_phys` 被调用之时，ION 会验证 buffer handle 是否在 client 允许访问的 handles 列表中，若不是，则返回错误。这些验证机制减少了期望之外的访问与资源泄露。

### Comparing ION and DMABUF

### 一些思考

- DMA 机制我是清楚的，但是内核中使用、实现似乎比我以为的复杂，需要花时间整理一下；
- 在 lowmemorykiller 和 ion 中都遇到了 shrinker，不太理解这个是做什么的；
- android 中有一个名为 binder 的核间通讯机制，有时间需要看看。

### Reference

[^1]: [The Android ION memory allocator](https://lwn.net/Articles/480055/)
[^2]:[demo](https://devarea.com/android-ion/#.Y_bC8dJBy0p)
[^3]:[Integrating the ION memory allocator](https://lwn.net/Articles/565469/)
[^4]:[zero copy](https://www.cnblogs.com/xiaolincoding/p/13719610.html)

[^5]:https://www.jianshu.com/p/4f681f6ddc3b
