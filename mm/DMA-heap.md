## DMA heap

Android 多媒体访问的内存接口由内核的 heap 模块提供，heap 模块内部由多种类型的 heap 可以在不同情况下使用。通过 heap 拿到内存后可以通过 smmu 映射接口转成虚拟地址给多媒体使用。

[TOC]

目前系统支持 system heap, carveout heap, cma heap，首先需要了解这几种 heap 的使用场景，以及怎样使用的。

### 数据结构

按照老规矩，先看数据结构。

#### 用户态

在用户态都是调用 libdmabufheap 的接口，只需要获取到 fd 即可，可以参考 aosp/system/memory/libdmabufheap。

#### 内核态

##### dma_heap

每种 heap 都有一个 dma_heap 表示（理论上来说，dma_heap 里面应该有个链表，管理所有的 dma_buf），

```c
/**
 * struct dma_heap - represents a dmabuf heap in the system
 * @name:		used for debugging/device-node name
 * @ops:		ops struct for this heap
 * @heap_devt		heap device node
 * @list		list head connecting to list of heaps
 * @heap_cdev		heap char device
 * @heap_dev		heap device struct
 *
 * Represents a heap of memory from which buffers can be made.
 */
struct dma_heap {
	const char *name;
	const struct dma_heap_ops *ops; // 在初始化的时候注册
	void *priv;
	dev_t heap_devt;
	struct list_head list;
	struct cdev heap_cdev;
	struct kref refcount;
	struct device *heap_dev; // 每个 heap 都是以字符设备的方式在 /dev/dma_heap/ 目录下提供给用户态
};
```

##### dma_buf

heap 以 dma_buf 为单位管理内存，dma_buf 可以在不同设备间共享，

```c
/**
 * struct dma_buf - shared buffer object
 *
 * This represents a shared buffer, created by calling dma_buf_export(). The
 * userspace representation is a normal file descriptor, which can be created by
 * calling dma_buf_fd().
 *
 * Shared dma buffers are reference counted using dma_buf_put() and
 * get_dma_buf().
 *
 * Device DMA access is handled by the separate &struct dma_buf_attachment.
 */
struct dma_buf {
	size_t size;
	struct file *file;
	struct list_head attachments; // 管理所有使用该 dma_buf 的设备
	const struct dma_buf_ops *ops;

    ...

	struct dma_buf_map vmap_ptr;
	const char *name;

	/** @list_node: node for dma_buf accounting and debugging. */
	struct list_head list_node;

	/** @priv: exporter specific private data for this buffer object. */
	void *priv;

    ...

};
```

##### dma_buf_attachment

每个设备在使用一块 dma_buf 之前，需要进行 attach 操作，所有 attach 到 dma_buf 的设备都由下面这个结构统一管理，

```c
/**
 * struct dma_buf_attachment - holds device-buffer attachment data
 * @dmabuf: buffer for this attachment.
 * @dev: device attached to the buffer.
 * @node: list of dma_buf_attachment, protected by dma_resv lock of the dmabuf.
 * @sgt: cached mapping.
 * @dir: direction of cached mapping.
 * @peer2peer: true if the importer can handle peer resources without pages.
 * @priv: exporter specific attachment data.
 * @importer_ops: importer operations for this attachment, if provided
 * dma_buf_map/unmap_attachment() must be called with the dma_resv lock held.
 * @importer_priv: importer specific attachment data.
 * @dma_map_attrs: DMA attributes to be used when the exporter maps the buffer
 * through dma_buf_map_attachment.
 *
 * This structure holds the attachment information between the dma_buf buffer
 * and its user device(s). The list contains one attachment struct per device
 * attached to the buffer.
 */
struct dma_buf_attachment {
	struct dma_buf *dmabuf;
	struct device *dev;
	struct list_head node;
	struct sg_table *sgt;
	enum dma_data_direction dir;
	bool peer2peer;
	const struct dma_buf_attach_ops *importer_ops;
	void *importer_priv;
	void *priv;
	unsigned long dma_map_attrs;

    ...
};
```

### 使用流程

在用户态申请 dma buf 是通过 andorid 实现的 libdmabufheap 来申请的，其实现了如下接口，

```c++
BufferAllocator* CreateDmabufHeapBufferAllocator();

void FreeDmabufHeapBufferAllocator(BufferAllocator* buffer_allocator);

int DmabufHeapAlloc(BufferAllocator* buffer_allocator, const char* heap_name, size_t len,
                    unsigned int heap_flags, size_t legacy_align);
int DmabufHeapAllocSystem(BufferAllocator* buffer_allocator, bool cpu_access, size_t len,
                          unsigned int heap_flags, size_t legacy_align);

int DmabufHeapCpuSyncStart(BufferAllocator* buffer_allocator, unsigned int dmabuf_fd,
                           SyncType sync_type, int (*legacy_ion_cpu_sync)(int, int, void *),
                           void *legacy_ion_custom_data);

int DmabufHeapCpuSyncEnd(BufferAllocator* buffer_allocator, unsigned int dmabuf_fd,
                         SyncType sync_type, int (*legacy_ion_cpu_sync)(int, int, void*),
                         void* legacy_ion_custom_data);
```

#### BufferAllocator

在使用时，其先调用 `CreateDmabufHeapBufferAllocator` 来创建一个 `BufferAllocator` 实例，该实例有对应的 Alloc 等成员函数，

```c++
// 1. 创建 BufferAllocator
BufferAllocator* CreateDmabufHeapBufferAllocator() {
    return new BufferAllocator();
}

// 2. 申请 dma buf，返回 fd
int DmabufHeapAlloc(BufferAllocator* buffer_allocator, const char* heap_name, size_t len,
                    unsigned int heap_flags, size_t legacy_align) {
    if (!buffer_allocator)
        return -EINVAL;
    return buffer_allocator->Alloc(heap_name, len, heap_flags, legacy_align);
}

// 3. 只是一个封装
int BufferAllocator::Alloc(const std::string& heap_name, size_t len,
                           unsigned int heap_flags, size_t legacy_align) {
    int fd = DmabufAlloc(heap_name, len);

    return fd;
}

// 4. 首先根据 heap name 获取要申请的 heap 的 fd
// 然后使用 iotcl 系统调用，向内核发起请求，最终返回的也是 dma buf 的 fd
int BufferAllocator::DmabufAlloc(const std::string& heap_name, size_t len) {
    int fd = OpenDmabufHeap(heap_name);
    if (fd < 0) return fd;

    struct dma_heap_allocation_data heap_data{
        .len = len,  // length of data to be allocated in bytes
        .fd_flags = O_RDWR | O_CLOEXEC,  // permissions for the memory to be allocated
    };

    auto ret = TEMP_FAILURE_RETRY(ioctl(fd, DMA_HEAP_IOCTL_ALLOC, &heap_data));

    ...

    return heap_data.fd;
}
```

#### dma_heap_buffer_alloc

`DMA_HEAP_IOCTL_ALLOC` 在内核态的处理流程是 `dma_heap_ioctl_allocate` -> `dma_heap_bufferfd_alloc` -> `dma_heap_buffer_alloc` -> `heap->ops->allocate` 这里 ops->allocate 每种 heap 在初始化时都会注册，以 cma 为例，是`cma_heap_allocate`，其实这个命名我觉得有点问题，因为这个函数是分配 dma_buf 的，叫 `cma_heap_buf_allocate` 比较好。

```c
static struct dma_buf *cma_heap_allocate(struct dma_heap *heap,
					 unsigned long len,
					 unsigned long fd_flags,
					 unsigned long heap_flags)
{
	struct cma_heap *cma_heap = dma_heap_get_drvdata(heap);
	struct cma_heap_buffer *buffer; // 这个 buffer 就是在内核中表示一块物理内存的结构
	DEFINE_DMA_BUF_EXPORT_INFO(exp_info);
	size_t size = PAGE_ALIGN(len);
	pgoff_t pagecount = size >> PAGE_SHIFT;
	unsigned long align = get_order(size);
	struct page *cma_pages;
	struct dma_buf *dmabuf;
	int ret = -ENOMEM;
	pgoff_t pg;

	buffer = kzalloc(sizeof(*buffer), GFP_KERNEL);

	INIT_LIST_HEAD(&buffer->attachments);
	mutex_init(&buffer->lock);
	buffer->len = size;

	if (align > CONFIG_CMA_ALIGNMENT)
		align = CONFIG_CMA_ALIGNMENT;

    // 分配内存
    // cma 采用 bitmap 的方式管理内存
    // 未来的开发中，如果有管理小块内存（100MB左右）的需求，可以参考这种管理方式
	cma_pages = cma_alloc(cma_heap->cma, pagecount, align, false);
	if (!cma_pages)
		goto free_buffer;

	/* Clear the cma pages */
    // 将这块 cma_pages 空间写 0
	if (PageHighMem(cma_pages)) {
        ...
	} else {
		memset(page_address(cma_pages), 0, size);
	}

	buffer->pages = kmalloc_array(pagecount, sizeof(*buffer->pages), GFP_KERNEL);

	for (pg = 0; pg < pagecount; pg++)
		buffer->pages[pg] = &cma_pages[pg];

	buffer->cma_pages = cma_pages;
	buffer->heap = cma_heap;
	buffer->pagecount = pagecount;

	/* create the dmabuf */
	exp_info.exp_name = dma_heap_get_name(heap);
	exp_info.ops = &cma_heap_buf_ops; // 这里就是该 buf 所有的回调函数，之后的处理都在里面，后面分析
	exp_info.size = buffer->len;
	exp_info.flags = fd_flags;
	exp_info.priv = buffer;
	dmabuf = dma_buf_export(&exp_info); // 做个转化

	return dmabuf;
}
```

这样就完成了从用户态到内核态申请一个 dma_buf 的过程，**不过返回到用户态的是该 dma_buf 对应的 fd，所以在返回前还需要通过 `dma_buf_fd` 申请一个 fd，之后使用只需要这个 fd**。

#### 设备使用 dma_buf

接下来我们看看怎样使用。Application 通过上述的操作是申请到一块用 dma_buf 表示的物理内存，并得到 fd，但是要使用这块内存还需要对应的驱动做一些操作，Application 通过系统调用将 fd 及需要做的操作传到驱动，然后驱动调用对应的回调函数，通过一个简单的例子来看一下，

```c
static int heap_test(struct heap_dma_buf *dma_buf, int len) // 这个 dma_buf 需要包含上述申请到的 fd
{
	...

	misc_fd = open(XXX, O_RDWR | O_CLOEXEC); // 获取对应的设备节点 fd

	ret = heap_test_map(misc_fd, dma_buf); // 该设备节点使用 dma_buf

	...

	heap_dma_buf_free(dma_buf);
	close(misc_fd);

	return 0;
}

static inline int heap_test_ioctl_helper(int misc_fd,
			struct dma_buf_priv *dma_buf)
{
	struct dma_heap_allocation_data data;
	int ret;

	memset(&data, 0, sizeof(struct dma_heap_allocation_data));
	data.fd = dma_buf->fd; // 操作申请到的 fd
	data.len = dma_buf->len;
	ret = ioctl(misc_fd, HEAP_TEST_IOCTL_MAP, &data); // 向对应的驱动发起系统调用

	return ret;
}
```

驱动中需要响应用户态发起的系统调用，可以参考下面的代码，以 map 为例，

```c
static long heap_test_ioctl(struct file *file, unsigned int ioctl,
			unsigned long arg)
{
	struct dma_heap_allocation_data heap_data;
	long ret = 0;

	if (copy_from_user(&heap_data, (void __user *)arg,
			sizeof(struct dma_heap_allocation_data))) {
		pr_err("heap_test: copy_from_user fail\n");
		return -1;
	}

	switch (ioctl) {
	case HEAP_TEST_IOCTL_KERN_MAP:
		ret = heap_test_map(file, &heap_data);
		break;
	...

	default:
		break;
	}

	return ret;
}
```

驱动开发者需要按照 google 的规则来响应请求，下面是响应 map 请求的示例，

```c
static struct heap_test_dma_buf *heap_test_map(struct device *dev, int fd)
{
	struct heap_test_dma_buf *priv = NULL;
	struct scatterlist *sgl = NULL;
	int i = 0;
	int ret = 0;

	priv = kzalloc(sizeof(struct heap_test_dma_buf), GFP_KERNEL);

	priv->dma_buf = dma_buf_get(fd); // 这个 fd 就是从用户态传下来的，fd 可以唯一指定一个 dma_buf

    // 1. 这个 dma_buf 其实任何设备都可以使用，使用前需要 attach 一下
    // 而 attach 就是将该设备添加到 dma_buf 的 attachments list 里面
    // 这些都是内核提供的接口，按照流程使用即可
	priv->attachment = dma_buf_attach(priv->dma_buf, dev);

    // 2. 这个操作还没搞懂
    // sg_table 是用于管理散布/聚集（scatter/gather）I/O 的数据结构
    // 它允许多个物理内存块（散布）或者多个数据片段（散布）组合成一个连续的内存区域
    // 以便 I/O 进行高效的数据传输
    // 这个对应的流程还是很复杂的，暂时不看
	priv->sgt = dma_buf_map_attachment(priv->attachment, DMA_BIDIRECTIONAL);

	heap_test_dump_sg_info(priv->sgt);

    // 映射到内核地址空间
    // 以 cma 为例，调用的是 cma_heap_vmap
    // 最终也是调用 vmap 进行映射
	/* map to kernel and access the mem */
	ret = dma_buf_vmap(priv->dma_buf, &priv->map);

	priv->fd = fd;
	priv->buf = (char *)priv->map.vaddr;
	priv->len = 0;
	for_each_sg(priv->sgt->sgl, sgl, priv->sgt->nents, i) {
		priv->len += sg_dma_len(sgl);
	}
	memset(priv->buf, 0, priv->len);

	return priv;
}
```

之后就可以正常的往 dma_buf 中读写，

```c
static int xxx_write(struct dma_buf_priv *dma_buf)
{
	int i;

	printf("%s\n", __func__);
	for (i = 0; i < dma_buf->len; i++) {
		dma_buf->buf[i] = i % 256;
		printf("buf[%d]=0x%x\n", i, dma_buf->buf[i]);
	}
	dma_buf_cpu_sync_end(dma_buf, DMA_BUF_SYNC_WRITE); // 刷 cache

	return 0;
}
```

**总结**

整个来看，使用 dma_buf 首先在需要内核中分配物理内存，每个 dma_buf 指定一个 fd，传到用户态，然后在驱动中将 `struct device *dev` attach 到 dma_buf 的 attachment list。

所以按照上面的方式，用户态程序或设备驱动想要使用 dma_buf 只需要知道 fd 就行，共享数据十分简单，这里要确保 fd 的正确传递。

### cma_heap

#### 初始化

cma_heap 是 dma-heap 的类型之一，它使用的是 CMA 内存，在系统内存不足时，会被回收。一般系统都会有一块 default cma，在 dts 中一般按如下方式定义：

```c
linux,cma {
            compatible = "shared-dma-pool";
            reg = <0x? 0x? 0X0 0x?>;
            reusable;
            linux,cma-default;
        };
```

#### rmem_cma_setup

这种类型的预留内存在系统初始化时完成解析 `RESERVEDMEM_OF_DECLARE(cma, "shared-dma-pool", rmem_cma_setup);` ，这个函数有些地方需要注意，

```c
static const struct reserved_mem_ops rmem_cma_ops = {
	.device_init	= rmem_cma_device_init,
	.device_release = rmem_cma_device_release,
};

static int __init rmem_cma_setup(struct reserved_mem *rmem)
{
	phys_addr_t align = PAGE_SIZE << max(MAX_ORDER - 1, pageblock_order);
	phys_addr_t mask = align - 1;
	unsigned long node = rmem->fdt_node;
	bool default_cma = of_get_flat_dt_prop(node, "linux,cma-default", NULL);
	struct cma *cma;
	int err;

	...

    // 所有的 cma 都会保存在 cma_area 数组中
	err = cma_init_reserved_mem(rmem->base, rmem->size, 0, rmem->name, &cma);

    ...


	if (default_cma)
		dma_contiguous_default_area = cma;

    // 这个地方需要注意，设置的是这个 reserved_mem 的 ops
    // device_init = rmem_cma_device_init 只有在调用 of_reserved_mem_device_init 时才会被调用
    // 如果该设备节点使用 memory-region 属性引用了某个预留内存节点
    // 那么就会调到这个 ops
    // device_init 就是配置 dev->cma_area 为该 reserved_mem 的
	rmem->ops = &rmem_cma_ops;
	rmem->priv = cma;

	pr_info("Reserved memory: created CMA memory pool at %pa, size %ld MiB\n",
		&rmem->base, (unsigned long)rmem->size / SZ_1M);

	return 0;
}
```

#### add_default_cma_heap

default cma 初始化由 cma_heap.ko 完成，主要是设置 cma_heap_ops 以及将 default cma 挂载到 /dev/dma-heap 上。

```c
static const struct dma_heap_ops cma_heap_ops = {
	.allocate = cma_heap_allocate,
};

int __add_cma_heap(struct cma *cma, void *data)
{
	struct cma_heap *cma_heap;
	struct dma_heap_export_info exp_info;

	cma_heap = kzalloc(sizeof(*cma_heap), GFP_KERNEL);
	if (!cma_heap)
		return -ENOMEM;
	cma_heap->cma = cma;

	exp_info.name = cma_get_name(cma);
    // 用户态申请 cma 内存都会走到这个函数，在这个函数里面又会设置 cma_heap_buf_ops
    // 这个函数就是 cma heap 和 heap core 建立关联的地方
    // 在用户态通过 ioctl 向 dma heap core 发起请求，会根据不同的 heap 走到对应的回调函数
	exp_info.ops = &cma_heap_ops;
	exp_info.priv = cma_heap;

	cma_heap->heap = dma_heap_add(&exp_info); // 这里调用 device_create 添加 char 设备

    ...

	return 0;
}
EXPORT_SYMBOL(__add_cma_heap);

static int add_default_cma_heap(void)
{
    // 这里获取 default cma
    // 其他的 cma 默认是不会加入到 /dev/dma-heap 中的
    // 但是可以通过 of_reserved_mem_lookup 遍历所有的 reserved mem
    // 然后添加
	struct cma *default_cma = dev_get_cma_area(NULL);
	int ret = 0;

	if (default_cma)
		ret = __add_cma_heap(default_cma, NULL);

	return ret;
}
module_init(add_default_cma_heap);
```

#### dma_heap_add

这个函数主要是添加 cma_heap 设备到 /dev/dma_heap 目录下。其实所有的 dma heap 设备的创建都要走到这里，

```c
struct dma_heap *dma_heap_add(const struct dma_heap_export_info *exp_info)
{
	struct dma_heap *heap, *err_ret;
	unsigned int minor;
	int ret;

	...

	heap = kzalloc(sizeof(*heap), GFP_KERNEL);
	if (!heap)
		return ERR_PTR(-ENOMEM);

	kref_init(&heap->refcount);
	heap->name = exp_info->name;
	heap->ops = exp_info->ops;
	heap->priv = exp_info->priv;

	/* Find unused minor number */
	ret = xa_alloc(&dma_heap_minors, &minor, heap,
		       XA_LIMIT(0, NUM_HEAP_MINORS - 1), GFP_KERNEL);
	if (ret < 0) {
		pr_err("dma_heap: Unable to get minor number for heap\n");
		err_ret = ERR_PTR(ret);
		goto err0;
	}

	/* Create device */
	heap->heap_devt = MKDEV(MAJOR(dma_heap_devt), minor);

    // 这个 ops 是该 heap 对应的回调函数，之后操作该 heap 都是这个回调函数完成的
	cdev_init(&heap->heap_cdev, &dma_heap_fops);
	ret = cdev_add(&heap->heap_cdev, heap->heap_devt, 1);

    ...

	heap->heap_dev = device_create(dma_heap_class,
				       NULL,
				       heap->heap_devt,
				       NULL,
				       heap->name);

	/* Make sure it doesn't disappear on us */
	heap->heap_dev = get_device(heap->heap_dev);

	/* Add heap to the list */
	mutex_lock(&heap_list_lock);
	list_add(&heap->list, &heap_list);
	mutex_unlock(&heap_list_lock);

	return heap;
}
EXPORT_SYMBOL_GPL(dma_heap_add);
```

上述过程是在内核中完成 cma_heap 的初始化。

### 问题

- carveout_heap, system_heap, cma_heap 之间有什么区别？

  carveout heap 是系统初始化时预留好的，不会被回收；system heap 本身的物理地址不保证连续；cma heap 使用的是 CMA 内存，在系统内存不足时，会被回收。

- carveout_heap, carveout_heap_buffer, carveout_heap_buffer_attachment 三个数据结构之间的关系是什么？

  系统中有 3 个 heap，每个 heap 管理多个 buffer，而每个 buffer 能够被多个用户使用，每个用户在使用前需要 attach 一下，得到一个 attachment，以 carveout heap 为例，就是 `carveout_heap_buffer_attachment`。之后这个 attachment 会加入到 buffer 下的链表进行管理。

- 为什么要做 carveout_heap_begin_cpu_access 和 carveout_heap_end_cpu_access 操作?

  cpu 在读取内存之前是要做下 cache 同步操作的，保证 cpu 访问正确的数据；同理，在 CPU 操作结束后，将 heap 使用权限返还给设备后，也需要进行 cache 同步操作，保证设备访问到正确的数据。

- cma-heap 初始化流程；

  见 cma_heap 小节。

- multi-cma 流程；

- cma-heap 和 dma-heap core 如何建立关联；

  每种 heap 都可以理解为 dma-heap 的子类型，它们都是通过 dma_heap_add 来添加到 /dev/dma-heap 下的，有自己的回调函数，如 cma_heap 是 cma_heap_allocate。用户通过对应的 heap name 使用 ioctl 来申请内存时，首先是由 dma-heap core 的 dma_heap_ioctl 函数来处理，最终会调用到每种 heap 对应的回调函数，如 cma_heap_allocate 。

- libdmabufheap 提供了哪些接口；

  CreateDmabufHeapBufferAllocator,  FreeDmabufHeapBufferAllocator, DmabufHeapAlloc, DmabufHeapAllocSystem,

  DmabufHeapCpuSyncStart, DmabufHeapCpuSyncEnd 等，使用时序参考上图。

- libdma-heap 与 dma-core 如何关联；

  通过系统调用 ioctl，

  ```c
  int BufferAllocator::DmabufAlloc(const std::string& heap_name, size_t len) {
      int fd = OpenDmabufHeap(heap_name);
      if (fd < 0) return fd;

      struct dma_heap_allocation_data heap_data{
          .len = len,  // length of data to be allocated in bytes
          .fd_flags = O_RDWR | O_CLOEXEC,  // permissions for the memory to be allocated
      };

      auto ret = TEMP_FAILURE_RETRY(ioctl(fd, DMA_HEAP_IOCTL_ALLOC, &heap_data));
      if (ret < 0) {
          PLOG(ERROR) << "Unable to allocate from DMA-BUF heap: " << heap_name;
          return ret;
      }

      return heap_data.fd;
  }
  ```

- 不同的 heap name 如何管理；

  提供一个头文件，由我们自己维护，添加了 heap 后在该头文件中同步添加，Application 引用我们的头文件。

- cache 同步如何使用；

- dma-heap 的初始化时序；

- dma-heap 如何管理各个 heap；

- dma-heap 与 dma-buf 的关系；

- dma-buf 怎样进行引用计数；

- system heap 与 dma-buf 怎样关联；

- uncache 内存申请时如何 flush cache；

- system-heap 延迟释放是怎么回事；

- system-heap 与 dma-heap core 怎样关联；

- dma_map_ops 如何加 hook；

- device-specified dma 如何申请内存；

- direct alloc 内存如何申请 coherent 内存；

### Reference

[^1]:[Destaging ION](https://lwn.net/Articles/792733/)
[^2]:[LWN：ION 变了个形，就要打入 Linux 内部了！](https://mp.weixin.qq.com/s/P6eWS_brN4pUO97B-fWTCA)
