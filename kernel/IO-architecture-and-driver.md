## I/O 体系结构和设备驱动程序

在实现 serial 和 keyboard 时陷入无穷的细节，但由于缺乏对 I/O 系统整体的认识，实现不了，进入死循环，遂借此机会对 I/O 和驱动做一个整体的了解。

问题：

1. 按照这种结构，serial 只是一个 I/O 接口，负责在 I/O 端口和设备控制器传输数据，那我们通常说的使用 serial 输出是什么意思？
2. 我现在在裸机上输出是通过写一个 I/O 端口来实现，但输出肯定是需要设备，那么也就是说写端口的这个过程隐藏了使用 serial 传输和 tty 设备解析输出的过程。
3. 了解 I/O 体系结构之后就能解释为什么键盘输入产生的中断确是 `8250serial_interrupt` ，这是因为 serial 是接口，设备需要通过 I/O 接口来提出中断，所以我们看到都是 I/O 接口中断。

### I/O 体系结构

CPU 和设备之间需要总线连接来进行数据交互。所有的计算机都有一条系统总线，用于连接大部分硬件设备。一种典型的系统总线是 PCI 总线。

CPU 和 I/O 设备之间的数据通路通常称为 I/O 总线。每个 I/O 设备依次连接到 I/O 总线上，这种连接包含 3 个元素的硬件组织层次： I/O 端口、接口和设备控制器。

#### I/O 端口

每个连接到 I/O 总线的设备都有自己的 I/O 地址集，通常称为 I/O 端口（I/O port）。在 x86 体系结构中，有 4 条指令允许 CPU 对 I/O 端口进行读写，in, ins, out 和 outs。在执行其中一条指令时， CPU 使用地址总线选择所请求的 I/O 端口，使用数据总线在 CPU 寄存器和端口之间传送数据。I/O 端口还可以直接映射到物理地址空间，这样就可以直接使用对内存进行操作的汇编语言指令操作 I/O 设备。

系统使用 resource 来表示 I/O 端口，一个 resource 表示 I/O 端口的一个范围。可以使用 `request_resource` 来把给定范围的 I/O 端口分配给 I/O 设备。

#### I/O 接口

I/O 接口是位于 I/O 端口和对应设备控制器之间的硬件电路。它将 I/O 端口的命令发送设备控制器，更新设备寄存器的状态，并可以代表设备提出中断请求。

其可以分为专用接口（键盘接口、图形接口、网络接口等）和通用接口（并口、串口、USB 等）

#### 设备控制器

主要做两个工作：

- 对从 I/O 接口接收到的高级命令进行解释，发送给设备执行。

- 对从设备的接受到的电信号进行转换和解释，更新 I/O 端口的状态。

### 设备驱动程序模型

设备驱动程序模型：为系统中所有的总线、设备以及设备驱动程序提供了一个统一的视图。

#### sysfs 文件系统

sysfs 的目标是要展现设备驱动程序模型组件间的层次关系。

#### kobject

kobject 是设备驱动程序模型的核心数据结构，每个 kobject 对应于 sysfs 文件系统中的一个目录。

#### 设备驱动程序模型的组件（没懂）

设备驱动程序模型建立在总线、设备、设备驱动器等结构上。

##### 设备

设备驱动程序模型中的每个设备都是由一个 device 对象来描述的。

通常 device 不会单独使用，而是被包含在一个具体设备结构体中，如 `struct usb_device`。就是被包含到一个具体的总线下的设备结构体中。

```c
struct device {
	struct device		*parent;

	struct device_private	*p;

	struct kobject kobj;
	const char		*init_name; /* initial name of the device */
	const struct device_type *type;

	struct mutex		mutex;	/* mutex to synchronize calls to
					 * its driver.
					 */

	struct bus_type	*bus;		/* type of bus device is on */
	struct device_driver *driver;	/* which driver has allocated this
					   device */
	void		*platform_data;	/* Platform specific data, device
					   core doesn't touch it */
	void		*driver_data;	/* Driver data, set and get with
					   dev_set/get_drvdata */
	struct dev_links_info	links;
	struct dev_pm_info	power;
	struct dev_pm_domain	*pm_domain;

#ifdef CONFIG_GENERIC_MSI_IRQ_DOMAIN
	struct irq_domain	*msi_domain;
#endif
#ifdef CONFIG_PINCTRL
	struct dev_pin_info	*pins;
#endif
#ifdef CONFIG_GENERIC_MSI_IRQ
	struct list_head	msi_list;
#endif

#ifdef CONFIG_NUMA
	int		numa_node;	/* NUMA node this device is close to */
#endif
	const struct dma_map_ops *dma_ops;
	u64		*dma_mask;	/* dma mask (if dma'able device) */
	u64		coherent_dma_mask;/* Like dma_mask, but for
					     alloc_coherent mappings as
					     not all hardware supports
					     64 bit addresses for consistent
					     allocations such descriptors. */
	u64		bus_dma_mask;	/* upstream dma_mask constraint */
	unsigned long	dma_pfn_offset;

	struct device_dma_parameters *dma_parms;

	struct list_head	dma_pools;	/* dma pools (if dma'ble) */

	struct dma_coherent_mem	*dma_mem; /* internal for coherent mem
					     override */
#ifdef CONFIG_DMA_CMA
	struct cma *cma_area;		/* contiguous memory area for dma
					   allocations */
#endif
	/* arch specific additions */
	struct dev_archdata	archdata;

	struct device_node	*of_node; /* associated device tree node */
	struct fwnode_handle	*fwnode; /* firmware device node */

	dev_t			devt;	/* dev_t, creates the sysfs "dev" */
	u32			id;	/* device instance */

	spinlock_t		devres_lock;
	struct list_head	devres_head;

	struct klist_node	knode_class;
	struct class		*class;
	const struct attribute_group **groups;	/* optional groups */

	void	(*release)(struct device *dev);
	struct iommu_group	*iommu_group;
	struct iommu_fwspec	*iommu_fwspec;

	bool			offline_disabled:1;
	bool			offline:1;
	bool			of_node_reused:1;
};
```

##### 驱动程序

设备驱动程序模型中的每个驱动程序都由 `device_driver` 对象描述，

```c
/**
 * struct device_driver - The basic device driver structure
 * @name:	Name of the device driver.
 * @bus:	The bus which the device of this driver belongs to.
 * @owner:	The module owner.
 * @mod_name:	Used for built-in modules.
 * @suppress_bind_attrs: Disables bind/unbind via sysfs.
 * @probe_type:	Type of the probe (synchronous or asynchronous) to use.
 * @of_match_table: The open firmware table.
 * @acpi_match_table: The ACPI match table.
 * @probe:	Called to query the existence of a specific device,
 *		whether this driver can work with it, and bind the driver
 *		to a specific device.
 * @remove:	Called when the device is removed from the system to
 *		unbind a device from this driver.
 * @shutdown:	Called at shut-down time to quiesce the device.
 * @suspend:	Called to put the device to sleep mode. Usually to a
 *		low power state.
 * @resume:	Called to bring a device from sleep mode.
 * @groups:	Default attributes that get created by the driver core
 *		automatically.
 * @pm:		Power management operations of the device which matched
 *		this driver.
 * @coredump:	Called when sysfs entry is written to. The device driver
 *		is expected to call the dev_coredump API resulting in a
 *		uevent.
 * @p:		Driver core's private data, no one other than the driver
 *		core can touch this.
 *
 * The device driver-model tracks all of the drivers known to the system.
 * The main reason for this tracking is to enable the driver core to match
 * up drivers with new devices. Once drivers are known objects within the
 * system, however, a number of other things become possible. Device drivers
 * can export information and configuration variables that are independent
 * of any specific device.
 */
struct device_driver {
	const char		*name;
	struct bus_type		*bus;

	struct module		*owner;
	const char		*mod_name;	/* used for built-in modules */

	bool suppress_bind_attrs;	/* disables bind/unbind via sysfs */
	enum probe_type probe_type;

	const struct of_device_id	*of_match_table;
	const struct acpi_device_id	*acpi_match_table;

	int (*probe) (struct device *dev);
	int (*remove) (struct device *dev);
	void (*shutdown) (struct device *dev);
	int (*suspend) (struct device *dev, pm_message_t state);
	int (*resume) (struct device *dev);
	const struct attribute_group **groups;

	const struct dev_pm_ops *pm;
	void (*coredump) (struct device *dev);

	struct driver_private *p;
};
```

`device_driver` 对象包括 4 个方法，它们用于处理热拔插、即插即用和电源管理。

##### 总线

内核所支持的每一种总线类型都由 `bus_type` 描述。

```c
struct bus_type {
	const char		*name;
	const char		*dev_name;
	struct device		*dev_root;
	const struct attribute_group **bus_groups;
	const struct attribute_group **dev_groups;
	const struct attribute_group **drv_groups;

	int (*match)(struct device *dev, struct device_driver *drv);
	int (*uevent)(struct device *dev, struct kobj_uevent_env *env);
	int (*probe)(struct device *dev);
	int (*remove)(struct device *dev);
	void (*shutdown)(struct device *dev);

	int (*online)(struct device *dev);
	int (*offline)(struct device *dev);

	int (*suspend)(struct device *dev, pm_message_t state);
	int (*resume)(struct device *dev);

	int (*num_vf)(struct device *dev);

	int (*dma_configure)(struct device *dev);

	const struct dev_pm_ops *pm;

	const struct iommu_ops *iommu_ops;

	struct subsys_private *p;
	struct lock_class_key lock_key;

	bool need_parent_lock;
};
```

##### 类

类本质上是要提供一个标准的方法，从而为向用户态应用程序导出逻辑设备的接口。

类的真正意义在于作为同属于一个类的多个设备的容器。也就是说，类是一种人造概念，目的就是为了对各种设备进行分类管理。当然，类在分类的同时还对每个类贴上了一些“标签”，这也是设备驱动模型为我们写驱动提供的基础设施。所以一个设备可以从类这个角度讲它是属于哪个类，也可以从总线的角度讲它是挂在哪个总线下的。

### 设备文件



### Reference

[1] https://www.cnblogs.com/deng-tao/p/6033932.html
