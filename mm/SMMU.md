## SMMU

SMMU 就是 ARM 架构的 IOMMU，以前接触的都是 X86，还奇怪了 😅

首先从根本上来讲，SMMU 是做什么的，这是我之前了解的 IOMMU 的解释：

> IOMMU(**input–output memory management unit**) 有两大功能：控制设备 dma 地址映射到机器物理地址（DMA Remmaping），中断重映射(intremap)（可选）
>
> 第一个功能进一步解释是这样的，在没有 Iommu 的时候，设备通过 dma 可以访问到机器的全部的地址空间，这样会导致系统被攻击。当出现了 iommu 以后，iommu 通过控制每个设备 dma 地址到实际物理地址的映射转换，使得在一定的内核驱动框架下，用户态驱动能够完全操作某个设备 dma 和中断成为可能，同时保证安全性。

所以 SMMU 的功能是一样的，一个是地址映射，控制外设访问内存的地址，提高安全性，一个是虚拟化中会用到的中断重映射。

整体系统架构分为四大部分：

- SMMU Driver on AP: 软件部分，运行在 CPU 上；
- SMMU 硬件：
  - configuration table: SMMU 的配置信息，如 STE, Context Descriptor, CmdQ, EventQ；
  - TLB/PageWalkTable: SMMU 转换的缓存，以及转换缓存 miss 时，发起 page table walk；
  - cmdq: 软件下发一些控制命令所在的队列；
  - eventq: SMMU 产生 event 需要 SMMU driver 处理时所在的队列；

- IODevice: 连接 SMMU 外设；

- STE, CD, PageTable: 这些 SMMU 的配置位于 DDR。

### SMMU 硬件介绍

SMMU 为 System Memory Management Unit，它的功能与 MMU 功能类似，将 IO 设备的 DMA 地址请求(IOVA)转化为系统总线地址(PA)，实现地址映射、属性转换、权限检查等功能，实现不同设备的 DMA 地址空间隔离。

![smmu](D:\gitlab\xiaomi\image\smmu.jpeg)

#### STE 介绍

STE 为 Stream Table Entry，用于描述 stream（设备）的配置信息，它分为线性 STE 和二级 STE。

![ste.png](https://github.com/UtopianFuture/UtopianFuture.github.io/blob/master/image/ste.png?raw=true)

![ste_structure.png](https://github.com/UtopianFuture/UtopianFuture.github.io/blob/master/image/ste_structure.png?raw=true)

##### SECSID/SID/SSID 含义

![sid](D:\gitlab\UtopianFuture.github.io\image\sid.png)

- SID
  - SID 用于区分不同的 Stream，不同 Stream 会使用不同的 STE，从而得到不同的 SMMU 配置；
  - **STE 可以配置 Stream bypass，attributes override，CD 基地址**等；
  - SID 用于区分不同的 Master，对不同 Master 使用不同的配置；
  - 当 1 个 Master 需要对不同的数据流使用不同的配置时，可以增加 SID 数量进行区分；
  - **安全 Stream 和非安全 Stream 属于两个独立世界，所以可以使用相同的 SID**；
- SSID、SSIDV
  - SSID 用于细分某个 Stream 里的 SubStream，不同 SubStream 会使用不同的 CD，从而可以使用不同的页表；
  - **SSID 用于区分不同的进程，如果 1 个 Master 可以同时被多个进程使用，则需要通过 SSID 对属于不同进程的传输进行区分**；
  - SSID 由 SSIDV 信号标记是否有效，SSIDV 为 0 时，传输属于默认 SubStream(SSID=0)；

##### 线性 STE

线性 STE 为一组连续的 STE，由 StreamID=0 开始，大小 2^n 大小。但每个 STE 大小为 60byte，当 STE 数目比较多时，需要分配的连续内存比较大，对于连续内存不够的系统，可能分配比较困难，因此又定义了二级 STE，可以节省分配的连续内存。

![line_ste.png](https://github.com/UtopianFuture/UtopianFuture.github.io/blob/master/image/line_ste.png?raw=true)

二级 STE 如下图所示，第一级 STE 并不指向真实的 STE，而是指向第二级 STE，第二级 STE 指向真实的 STE 且为线性的 STE。第一级 STE 的索引由 StreamID[n: SPT]决定，其中 SPT 由 SMMU_STRTAB_BASE_CFG.SPLIT 决定。第二级 STE 的最大数目由 StreamID[SPT:0]决定。

![noline-ste.png](https://github.com/UtopianFuture/UtopianFuture.github.io/blob/master/image/noline-ste.png?raw=true)

#### CD

Context Descriptors : **包含 stage1 的页表基地址、ASID 等信息，多个 CD 表由 Sub streamID 选择**。CD 可以配置 VA 地址范围、页表粒度、页表基地址、页表起始安全属性等。

![cd.png](https://github.com/UtopianFuture/UtopianFuture.github.io/blob/master/image/cd.png?raw=true)![cd_structure.png](https://github.com/UtopianFuture/UtopianFuture.github.io/blob/master/image/cd_structure.png?raw=true)



#### 整体翻译过程

在使能 SMMU 两阶段地址翻译的情况下，stage1 负责将设备 DMA 请求发出的 VA 翻译为 IPA 并作为 stage2 的输入， stage2 则利用 stage1 输出的 IPA 再次进行翻译得到 PA，从而 DMA 请求正确地访问到 Guest 的要操作的地址空间上。

![smmu_translate.png](https://github.com/UtopianFuture/UtopianFuture.github.io/blob/master/image/smmu_translate.png?raw=true)

##### 三级页表查找过程

![smmu_translate2.png](https://github.com/UtopianFuture/UtopianFuture.github.io/blob/master/image/smmu_translate2.png?raw=true)

![pagetable_lookup.png](https://github.com/UtopianFuture/UtopianFuture.github.io/blob/master/image/pagetable_lookup.png?raw=true)

**虚拟地址[63:39]用来区分用户空间和内核空间**，从而在不同的 TTBR(Translation Table Base Register)寄存器中获取 Level1 页表基址，**内核地址用 TTBR1(代表 EL1)，用户地址用 TTBR0(代表 EL0)**。而**对于 smmu 来说 TTBR 是在 CD 表项中，并且没有区分内核空间和用户空间**。

![pagetable.png](https://github.com/UtopianFuture/UtopianFuture.github.io/blob/master/image/pagetable.png?raw=true)



##### 页表 table

![address_split.png](https://github.com/UtopianFuture/UtopianFuture.github.io/blob/master/image/address_split.png?raw=true)

![address_split2.png](https://github.com/UtopianFuture/UtopianFuture.github.io/blob/master/image/address_split2.png?raw=true)

##### block 和 page

![block.png](https://github.com/UtopianFuture/UtopianFuture.github.io/blob/master/image/block.png?raw=true)

![block2.png](https://github.com/UtopianFuture/UtopianFuture.github.io/blob/master/image/block2.png?raw=true)

![block3.png](https://github.com/UtopianFuture/UtopianFuture.github.io/blob/master/image/block3.png?raw=true)

![block4.png](https://github.com/UtopianFuture/UtopianFuture.github.io/blob/master/image/block4.png?raw=true)

![block5.png](https://github.com/UtopianFuture/UtopianFuture.github.io/blob/master/image/block5.png?raw=true)

![block6.png](https://github.com/UtopianFuture/UtopianFuture.github.io/blob/master/image/block6.png?raw=true)

Smmu STE 表可以覆盖写上游 master 传递下来的属性，例如访问权限、数据和指令等。

![privcfg.jpg](https://github.com/UtopianFuture/UtopianFuture.github.io/blob/master/image/privcfg.jpg?raw=true)

![instcfg.jpg](https://github.com/UtopianFuture/UtopianFuture.github.io/blob/master/image/instcfg.jpg?raw=true)

### SMMU 驱动介绍

#### 数据结构

一个完整的 dma buffer 申请流程，在为 io 设备绑定了 iommu 设备后，在使用 dma_alloc_coherent 等 dma 接口申请 dma buffer 时，会使用 iommu 的 iova 框架从 iova 空间申请出 iova，然后从 buddy、dma pool 中申请出物理页，最后调用所绑定的 iommu 设备驱动所实现的 map 接口，为物理页创建到 iova 的映射，页表建立使用 io-pgtable_ops 来完成。

下面介绍一些关键的数据结构：

老规矩，先放图，

![img](https://raw.githubusercontent.com/UtopianFuture/UtopianFuture.github.io/8100b8adba48d0147a384dfa630ca0430596b68d/image/smmu_structure.svg)

##### iommu_device

存放 smmu 的属性和方法，基础结构体，**对应一个 io 设备**，

```c
/**
 * struct iommu_device - IOMMU core representation of one IOMMU hardware
 *			 instance
 * @list: Used by the iommu-core to keep a list of registered iommus
 * @ops: iommu-ops for talking to this iommu
 * @dev: struct device for sysfs handling
 */
struct iommu_device {
	struct list_head list;
	const struct iommu_ops *ops;
	struct fwnode_handle *fwnode;
	struct device *dev;
};
```

##### arm_smmu_device

对应一个 smmu 设备，**arm_smmu_device 是 iommu_device 的子类，定义了 arm smmu 硬件属性**，如 cmdq, evtq priq 等队列。`struct arm_smmu_master` 作为 `struct dev_iommu` 的私有数据保存，

```c
struct arm_smmu_device {
	struct device			*dev;
	void __iomem			*base;
	void __iomem			*page1;

	...

	u32				features;

	u32				options;

	struct arm_smmu_cmdq		cmdq;
	struct arm_smmu_evtq		evtq;
	struct arm_smmu_priq		priq;

	int				cmd_sync_irq;
	int				gerr_irq;
	int				combined_irq;

	unsigned long			ias; /* IPA */
	unsigned long			oas; /* PA */
	unsigned long			pgsize_bitmap;

#define ARM_SMMU_MAX_ASIDS		(1 << 16)
	unsigned int			asid_bits;

#define ARM_SMMU_MAX_VMIDS		(1 << 16)
	unsigned int			vmid_bits;
	DECLARE_BITMAP(vmid_map, ARM_SMMU_MAX_VMIDS);

	unsigned int			ssid_bits;
	unsigned int			sid_bits;

	struct arm_smmu_strtab_cfg	strtab_cfg;

	/* IOMMU core code handle */
	struct iommu_device		iommu; // 继承自 iommu_device

	struct rb_root			streams;
	struct mutex			streams_mutex;
};
```

##### arm_smmu_master

保存各个 master 私有的数据。从开发经验来看，各个 master 都共用一套 smmu 驱动，而 smmu 又是在各个 master 中，实现略有不同。从代码上来看，很多函数都会使用 `struct arm_smmu_master *master = dev_iommu_priv_get(dev);` 来获取 `arm_smmu_master`，这个应该是核心的数据结构。

```c
/* SMMU private data for each master */
struct arm_smmu_master {
	struct arm_smmu_device		*smmu;
	struct device			*dev;
	struct arm_smmu_domain		*domain;
	struct list_head		domain_head;
	struct arm_smmu_stream		*streams;
	unsigned int			num_streams;
	bool				ats_enabled;
	bool				stall_enabled;
	bool				sva_enabled;
	bool				iopf_enabled;
	struct list_head		bonds;
	unsigned int			ssid_bits;
};
```

##### iommu_group

**一个 group 表示使用同一个 streamid 的一组 io 设备**，io 设备的 struct device 中保存了指向 iommu_group 的指针；

```c
struct iommu_group {
	struct kobject kobj;
	struct kobject *devices_kobj;
	struct list_head devices;
	struct mutex mutex;
	struct blocking_notifier_head notifier;
	void *iommu_data;
	void (*iommu_data_release)(void *iommu_data);
	char *name;
	int id;
	struct iommu_domain *default_domain;
	struct iommu_domain *domain;
	struct list_head entry;
};
```

不是很理解。

##### iommu_domain

这个结构抽象出了一个 domain 的结构，**domain 是一个管理设备和系统内存之间的直接内存访问（DMA）事务的系统组件**，它提供了设备 I/O 内存空间和系统内存空间之间的虚拟地址映射。范围和 iommu_group 一样，但它定义的是 group 范围内对应的操作的集合，这种设计能够使得多个 master 在同一个 smmu 中共享一个地址空间。

```c
struct iommu_domain {
	unsigned type;
	const struct iommu_ops *ops;
	unsigned long pgsize_bitmap;	/* Bitmap of page sizes in use */
	iommu_fault_handler_t handler;
	void *handler_token;
	struct iommu_domain_geometry geometry;
	struct iommu_dma_cookie *iova_cookie;
};
```

从代码上来看，这个其实就是一个 ops 的封装，其他的成员变量暂时不清楚。

##### arm_smmu_domain

也很好理解，各个 master 私有的访存操作集合，最重要的是 `pgtbl_ops`，map, unmap 等操作都包含在其中。

```c
struct arm_smmu_domain {
	struct arm_smmu_device		*smmu;
	struct mutex			init_mutex; /* Protects smmu pointer */

	struct io_pgtable_ops		*pgtbl_ops;
	bool				stall_enabled;
	atomic_t			nr_ats_masters;

	enum arm_smmu_domain_stage	stage;
	union {
		struct arm_smmu_s1_cfg	s1_cfg;
		struct arm_smmu_s2_cfg	s2_cfg;
	};

	struct iommu_domain		domain;

	struct list_head		devices;
	spinlock_t			devices_lock;

	struct list_head		mmu_notifiers;
};
```

#### smmu 关键函数

我们先来看一下整个 smmu 驱动的注册、加载以及通过 dts 配置与 IOMMU 关联的过程。

```c
| platform_driver_register
| -> driver_register

| bus_probe_device // 这里是遍历链表，添加设备
| -> device_initial_probe
| 	-> __device_attach
| 		-> bus_for_each_drv
| 			-> __device_attach_driver
| 				-> really_probe
| 					-> pci_dma_configure
| 						-> of_dma_configure
| 							-> of_dma_configure_id
| 								-> of_iommu_configure // 这个函数看起来比较重要，和 iommu 关联起来
|								       of_iommu_configure_device // 解析 dts
| 									-> iommu_probe_device
| 										-> __iommu_probe_device
|											   // smmu 关键数据结构的初始化，也会给 smmu 硬件发命令
| 											-> arm_smmu_probe_device
| 												->dev_iommu_priv_set
|								-> arch_setup_dma_ops // 这里还设置了一个 hook 点
|										// 这里设置 dev->dma_ops = &iommu_dma_ops;
|										// 后面在使用 dma_alloc_coherent 分配内存时
|										// 如果定义了 dma_ops，那么就会走 dma_ops->alloc 回调函数
|									-> iommu_setup_dma_ops
```

我们看一个基本的 platform 驱动注册代码，

```c
static const struct of_device_id arm_smmu_of_match[] = {
	{ .compatible = "arm,smmu-v3", }, // dts 中匹配到该 compatible 就执行 probe 函数
	{ },
};
MODULE_DEVICE_TABLE(of, arm_smmu_of_match);

static struct platform_driver arm_smmu_driver = {
	.driver	= {
		.name			= "arm-smmu-v3",
		.of_match_table		= arm_smmu_of_match,
		.suppress_bind_attrs	= true,
	},
	.probe	= arm_smmu_device_probe, // 这个是 smmu 的核心函数，上下电，回调函数的配置等
	.remove	= arm_smmu_device_remove,
	.shutdown = arm_smmu_device_shutdown,
};

static int __init arm_smmu_init(void)
{
    // 该函数在 probe 前执行
    // 最后会调用 bus_add_driver 和 driver_add_groups
    // 而之后 bus_probe_device 函数就能对挂载的设备进行初始化操作
    // bus 和 device 之间怎样管理之后再分析
	return platform_driver_register(&arm_smmu_driver);
}

static void __exit arm_smmu_exit(void)
{
	arm_smmu_sva_notifier_synchronize();
	platform_driver_unregister(&arm_smmu_driver);
}
module_init(arm_smmu_init);
module_exit(arm_smmu_exit);
```

smmu 和 iommu 强相关，所以分析一下驱动怎样和 iommu 关联上的。

```c
const struct iommu_ops *of_iommu_configure(struct device *dev,
					   struct device_node *master_np, // 这个表示设备树中的节点
					   const u32 *id)
{
	const struct iommu_ops *ops = NULL;
	struct iommu_fwspec *fwspec = dev_iommu_fwspec_get(dev);
	int err = NO_IOMMU;

	...

	if (dev_is_pci(dev)) {

        ...

	} else {
		err = of_iommu_configure_device(master_np, dev, id); // 这里解析 dts
	}

	...

	/*
	 * If we have reason to believe the IOMMU driver missed the initial
	 * probe for dev, replay it to get things in order.
	 */
	if (!err && dev->bus && !device_iommu_mapped(dev))
		err = iommu_probe_device(dev);

	...

	return ops;
}
```

和 iommu 关联需要在 dts 中配置 `iommus` 和 `#iommu-cells` 属性。所以如果 dts 中没有添加类似下面的配置，但是后续开发中又使用 smmu，系统就会报异常。

```c
media2_test {
        compatible = "media2_test";
        iommus = <&media2_smmu 0x?>;
        tbu = "media2_smmu_tbu3";
        status = "okay";
};
```

这里就会回调到各个厂商自己开发的 smmu 驱动中。

```c
static int __iommu_probe_device(struct device *dev, struct list_head *group_list)
{
	const struct iommu_ops *ops = dev->bus->iommu_ops;
	struct iommu_device *iommu_dev;
	struct iommu_group *group;
	int ret;

	...

    // 从调用栈上来看，这个回调函数是 arm_smmu_probe_device
    // 这个回调函数是在 smmu 的驱动挂载的时候做的
    // drivers/iommu/arm/arm-smmu-v3/arm-smmu-v3.c: arm_smmu_set_bus_ops(&arm_smmu_ops);
    // 最后调用到 bus_set_iommu
	iommu_dev = ops->probe_device(dev);

    ...

	dev->iommu->iommu_dev = iommu_dev;

	group = iommu_group_get_for_dev(dev);
	if (IS_ERR(group)) {
		ret = PTR_ERR(group);
		goto out_release;
	}
	iommu_group_put(group);

	if (group_list && !group->default_domain && list_empty(&group->entry))
		list_add_tail(&group->entry, group_list);

	iommu_device_link(iommu_dev, dev);

	return 0;

    ...
}
```

##### arm_smmu_probe_device

和下面的 `arm_smmu_device_probe` 不同，这个函数是各个 master 在初始化时调用的，用于配置该 master 特有的配置，如 sid 等信息。

```c
static struct iommu_device *arm_smmu_probe_device(struct device *dev)
{
	int ret;
	struct arm_smmu_device *smmu;
	struct arm_smmu_master *master;
	struct iommu_fwspec *fwspec = dev_iommu_fwspec_get(dev);

	...

	master->dev = dev;
	master->smmu = smmu;
	INIT_LIST_HEAD(&master->bonds);
	dev_iommu_priv_set(dev, master);

    // 这里配置 smmu
    // 主要是检查 sid，初始化 stream table 以及将 sid 插入到 SID tree 中进行管理
	ret = arm_smmu_insert_master(smmu, master);
	if (ret)
		goto err_free_master;

	device_property_read_u32(dev, "pasid-num-bits", &master->ssid_bits);
	master->ssid_bits = min(smmu->ssid_bits, master->ssid_bits);

	/*
	 * Note that PASID must be enabled before, and disabled after ATS:
	 * PCI Express Base 4.0r1.0 - 10.5.1.3 ATS Control Register
	 *
	 *   Behavior is undefined if this bit is Set and the value of the PASID
	 *   Enable, Execute Requested Enable, or Privileged Mode Requested bits
	 *   are changed.
	 */
	arm_smmu_enable_pasid(master);

	...

	return &smmu->iommu;

    ...
}
```

在这里就会给 smmu 发 command 了，所有在对应的使用了 smmu 的设备驱动 `platform_driver_register` 前就要保证 smmu 上电了，不然系统就挂了。

##### arm_smmu_device_probe

这个函数和上文的 `arm_smmu_device_probe` 非常像，注意不要看错了，这个函数是 smmu 驱动初始化时调用的。

arm-smmu-v3 驱动加载的入口为 arm_smmu_device_probe 函数，其主要做了如下几件事情：

- 从 dts 的 SMMU 节点或 ACPI 的 SMMU 配置表中读取 SMMU 中断等属性；

- 用 struct resource 来从设备获取到其资源信息，并 IO 重映射；

- probe SMMU 的硬件特性；

- 中断和事件队列初始化；

- 建立 STE 表；

- 设备 reset；

- 将 SMMU 注册到 IOMMU；

```c
static int arm_smmu_device_probe(struct platform_device *pdev)
{
	int irq, ret;
	struct resource *res;
	resource_size_t ioaddr;
	struct arm_smmu_device *smmu; // 和 arm_smmu_probe_device 不一样，这里初始化一个 smmu 设备
	struct device *dev = &pdev->dev;
	bool bypass;

	...

	if (dev->of_node) {
		ret = arm_smmu_device_dt_probe(pdev, smmu); // 从 dts 中解析
	} else {
		...
	}

	...

	/*
	 * Don't map the IMPLEMENTATION DEFINED regions, since they may contain
	 * the PMCG registers which are reserved by the PMU driver.
	 */
	smmu->base = arm_smmu_ioremap(dev, ioaddr, ARM_SMMU_REG_SZ); // smmu 基地址也是 iova

    ...

	/* Interrupt lines */
	// 这些也是在 dts 中配置
	irq = platform_get_irq_byname_optional(pdev, "combined");
	if (irq > 0)
		smmu->combined_irq = irq;
	else {
		irq = platform_get_irq_byname_optional(pdev, "eventq");
		if (irq > 0)
			smmu->evtq.q.irq = irq;

		irq = platform_get_irq_byname_optional(pdev, "priq");
		if (irq > 0)
			smmu->priq.q.irq = irq;

		irq = platform_get_irq_byname_optional(pdev, "gerror");
		if (irq > 0)
			smmu->gerr_irq = irq;
	}
	/* Probe the h/w */
    // 配置各种寄存器，这个要根据 smmu 的手册配置，一般不需要修改
	ret = arm_smmu_device_hw_probe(smmu);

	/* Initialise in-memory data structures */
	ret = arm_smmu_init_structures(smmu);

	/* Record our private device structure */
	platform_set_drvdata(pdev, smmu);

	/* Reset the device */
	ret = arm_smmu_device_reset(smmu, bypass);

	/* And we're up. Go go go! */
	ret = iommu_device_sysfs_add(&smmu->iommu, dev, NULL,
				     "smmu3.%pa", &ioaddr);

	ret = iommu_device_register(&smmu->iommu, &arm_smmu_ops, dev); // 这里设置自己开发的 ops

	ret = arm_smmu_set_bus_ops(&arm_smmu_ops); // 调用到 bus_set_iommu

	return 0;

    ...
}
```

##### bus_set_iommu

iommu 核心框架中提供了 `bus_set_iommu` 函数，该函数可以被 iommu 驱动调用，**用以将自身挂入到对应总线中**。函数中除了设置 iommu_ops 指针之外，还进行了两个工作：

- 向 bus 中注册一个 listener：对于 bus 上设备的插入与移除的设备，调用 iommu_ops 中对应的 add_device 和 remove_device 回调函数。对于 bus 接收到的其他设备事件（如 bind，unbind 等），则将其传播给该设备所处于的 group 中；
- 对于 bus 中已经存在的设备，则挨个调用 `add_device` 将其纳入 iommu 的管辖，并设置其 group。

这个函数比想象的复杂，

```c
| bus_set_iommu
| -> bus->iommu_ops = ops; // 有个问题，pci_bus_type 是全局变量，那每个设备配置一次不都会修改这个 ops，怎样保证不覆盖
| -> iommu_bus_init
|	-> bus_register_notifier // 对于 bus 上设备的插入与移除，调用 iommu_ops 对应的 add, remove 函数
|	-> bus_iommu_probe // 调用 add 函数将该设备纳入 iommu 管理，并设置 iommu_group，很复杂，之后再分析

```

##### arm_smmu_device_group

该函数为 device 分配 group，是 arm_smmu_ops 中的回调函数。

- 判断当前设备是否为 PCI 设备，若为 PCI 设备，PCIE 设备存在 alias，尝试使用 alias 对应的 iommu_group；若 PCIE 设备到 root bus 之间存在没有使能 ACS 特性的设备，那么此部分为同一个 group，使用对应的 group；若 PCIE 设备对应的 function 存在 alias，尝试使用 alias 对应的 iommu_group；最后调用 `iommu_group_alloc` 分配 iommu_group，此函数生成 iommu_group，且生成 SYSFS 属性 reserved_regions 和 type。
- 若为其他设备如 platform 设备，同样调用 `iommu_group_alloc`。

```C
static struct iommu_group *arm_smmu_device_group(struct device *dev)
{
    struct iommu_group *group;

    if (dev_is_pci(dev))
        group = pci_device_group(dev);
    else
        group = generic_device_group(dev);

    return group;
}
```

##### __iommu_probe_device

此函数主要是将设备添加到 iommu_group 中，

```c
static int __iommu_probe_device(struct device *dev, struct list_head *group_list)
{
	const struct iommu_ops *ops = dev->bus->iommu_ops;
	struct iommu_device *iommu_dev;
	struct iommu_group *group;
	int ret;

	...

	iommu_dev = ops->probe_device(dev); // 这里会调用到 arm_smmu_probe_device

	dev->iommu->iommu_dev = iommu_dev;

	group = iommu_group_get_for_dev(dev);

	iommu_group_put(group);

	if (group_list && !group->default_domain && list_empty(&group->entry))
		list_add_tail(&group->entry, group_list);

	iommu_device_link(iommu_dev, dev);

	...

	return ret;
}
```

##### arm_smmu_attach_dev

这个函数也是 arm_smmu_ops 中的函数，其将 IO 设备连接到 iommu_domain:

- `arm_smmu_domain_finalise` 页表相关的设置；
- `arm_smmu_install_ste_for_dev` STE 表相关设置；

```c
static int arm_smmu_attach_dev(struct iommu_domain *domain, struct device *dev)
{
	int ret = 0;
	unsigned long flags;
	struct iommu_fwspec *fwspec = dev_iommu_fwspec_get(dev);
	struct arm_smmu_device *smmu;
	struct arm_smmu_domain *smmu_domain = to_smmu_domain(domain);
	struct arm_smmu_master *master;

	...

	master = dev_iommu_priv_get(dev); // attach 前一定要执行 arm_smmu_device_probe
	smmu = master->smmu;

	...

	arm_smmu_detach_dev(master);

	mutex_lock(&smmu_domain->init_mutex);

	...

	master->domain = smmu_domain;

	if (smmu_domain->stage != ARM_SMMU_DOMAIN_BYPASS)
		master->ats_enabled = arm_smmu_ats_supported(master);

	arm_smmu_install_ste_for_dev(master);

	spin_lock_irqsave(&smmu_domain->devices_lock, flags);
	list_add(&master->domain_head, &smmu_domain->devices);
	spin_unlock_irqrestore(&smmu_domain->devices_lock, flags);

	arm_smmu_enable_ats(master);

out_unlock:
	mutex_unlock(&smmu_domain->init_mutex);
	return ret;
}
```

##### iova 管理

```C
void init_iova_domain(struct iova_domain *iovad, unsigned long granule,
    unsigned long start_pfn)
{
    /*
     * IOVA granularity will normally be equal to the smallest
     * supported IOMMU page size; both *must* be capable of
     * representing individual CPU pages exactly.
     */
    BUG_ON((granule > PAGE_SIZE) || !is_power_of_2(granule));

    spin_lock_init(&iovad->iova_rbtree_lock);
    iovad->rbroot = RB_ROOT;
    //cached_node 指向上次访问的node。anchor 是一个iova 结构体   --struct iova    anchor;        /* rbtree lookup anchor */
    iovad->cached32_node = &iovad->anchor.node;
    iovad->cached_node = &iovad->anchor.node;
    iovad->granule = granule;
    iovad->start_pfn = start_pfn;//base 地址，也是起始地址
    iovad->dma_32bit_pfn = 1UL << (32 - iova_shift(iovad));
    iovad->flush_cb = NULL;
    iovad->fq = NULL;
    // pfn_lo  实际保存着虚拟地址，默认都是0xFFFFFFFFF
    iovad->anchor.pfn_lo = iovad->anchor.pfn_hi = IOVA_ANCHOR;
    rb_link_node(&iovad->anchor.node, NULL, &iovad->rbroot.rb_node);//插入新的node 节点
    rb_insert_color(&iovad->anchor.node, &iovad->rbroot);//调整红黑树
    iovad->best_fit = false;  //是否使能最佳匹配，默认是不使能
    init_iova_rcaches(iovad);
}
```

### SMMU 和 DMA-Buffer heap 关联

内存管理分为两部分：通用内存管理：buddy, slab, kswapd 等；媒体内存管理：libdmabufheap->dma-heap->cma/system/carveout heap->iommu->smmu->页表释放。前面分析的是 smmu 软硬件，但是用户态是看不到 smmu 的，他们使用的是 libdmabufheap，然后到内核的 [dma-heap](../DMA-heap.md)，dma-heap 再与 smmu 关联起来。接下来分析一下 dma-heap 如何与 smmu 关联。

### Reference

[1] IHI0070E_a-System_Memory_Management_Unit_Architecture_Specification.pdf

[2] corelink_mmu600_system_memory_management_unit_technical_reference_manual_100310_0202_00_en.pdf

[3] DDI0487_I_a_a-profile_architecture_reference_manual.pdf

### 问题

- cma-heap 和 smmu 如何关联；
- system-heap 与 smmu 怎样关联；
- smmu 建断链时序；
- smmu 外部映射接口和使用时序；
- smmu 上电解耦（smmu 初始化后下电么？）；
- dma-heap 如何使用 smmu 映射；
- 不同进程的 fd 如何传递（binder）；
- 加扰功能如何实现；
- smmu 初始化流程；
- 各个 master 如何与 smmu 关联；
- master 与 smmu detach 解除关联；
- smmu 映射内存流程；
- smmu 共 domain 如何实现；
- dma-alloc 内存如何区分 mmu 还是 smmu 映射；
