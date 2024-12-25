## I/O 体系结构和设备驱动程序

这玩意我觉得非常抽象难懂，要想一次性搞定设备驱动模型比较困难，多看几次，就明白了。本文的主线是用于举例的 smmu-v3 驱动，在分析驱动的过程中，遇到什么补充什么，逐渐把整个框架搭建起来。

[TOC]

### 设备驱动程序模型

设备驱动程序模型：为系统中所有的总线、设备以及设备驱动程序提供了一个统一的视图。

#### smmu-v3

##### 驱动初始化

我们看一下 smmu-v3 中和初始化相关的代码，

```c
static int arm_smmu_device_probe(struct platform_device *pdev)
{
	int ret;
	struct resource *res;
	resource_size_t ioaddr;
	struct arm_smmu_device *smmu;
	struct device *dev = &pdev->dev;
	bool bypass;

	smmu = devm_kzalloc(dev, sizeof(*smmu), GFP_KERNEL);
	smmu->dev = dev;

	ret = arm_smmu_fw_probe(pdev, smmu, &bypass);

	/* Base address */
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	ioaddr = res->start;

	...
}

static void arm_smmu_device_remove(struct platform_device *pdev)
{
	struct arm_smmu_device *smmu = platform_get_drvdata(pdev);

	...
}

static void arm_smmu_device_shutdown(struct platform_device *pdev)
{
	struct arm_smmu_device *smmu = platform_get_drvdata(pdev);

	...
}

static const struct of_device_id arm_smmu_of_match[] = {
	{ .compatible = "arm,smmu-v3", }, // 其他变量不需要么
	{ },
};
MODULE_DEVICE_TABLE(of, arm_smmu_of_match);

static void arm_smmu_driver_unregister(struct platform_driver *drv)
{
	platform_driver_unregister(drv);
}

static struct platform_driver arm_smmu_driver = {
	.driver	= {
		.name			= "arm-smmu-v3",
		.of_match_table		= arm_smmu_of_match,
		.suppress_bind_attrs	= true,
	},
	.probe	= arm_smmu_device_probe, // 这些回调函数是怎样执行到的呢？
	.remove_new = arm_smmu_device_remove,
	.shutdown = arm_smmu_device_shutdown,
};

// 这是一个封装好的宏，拆开来就是
// module_init(arm_smmu_driver_init)
// module_exit(arm_smmu_driver_exit)
// 最后还是执行 platform_driver_register 和 arm_smmu_driver_unregister
// 正常来说应该是配套的执行 platform_driver_unregister
// 这里 smmu 还要做其他事情，所有封装了一下
module_driver(arm_smmu_driver, platform_driver_register,
	 arm_smmu_driver_unregister);
```

接下来看看 `platform_driver_register` 做了什么，能够让系统在解析到 dts 中有配置 `arm-smmu-v3` 就能够执行到对应的 probe 函数。

```c
#define platform_driver_register(drv) \
	__platform_driver_register(drv, THIS_MODULE)

/**
 * __platform_driver_register - register a driver for platform-level devices
 * @drv: platform driver structure
 * @owner: owning module/driver
 */
int __platform_driver_register(struct platform_driver *drv,
				struct module *owner) // 这个 owner 有什么用？
{
	drv->driver.owner = owner; // why THIS_MODULE?
	drv->driver.bus = &platform_bus_type; // 这个就是挂在到 platform bus 上

	return driver_register(&drv->driver);
}
```

##### 关键函数 driver_register

这是一个公共函数，所有的驱动注册都走这里，platform driver 可以理解为是 device_driver 的子类，其中有指向 device_driver 的指针，也有独有的变量。

```c
/**
 * driver_register - register driver with bus
 * @drv: driver to register
 *
 * We pass off most of the work to the bus_add_driver() call,
 * since most of the things we have to do deal with the bus
 * structures.
 */
int driver_register(struct device_driver *drv)
{
	int ret;
	struct device_driver *other;

	...

 	// 检查是否注册过了
	other = driver_find(drv->name, drv->bus); // name: arm-smmu-v3

	ret = bus_add_driver(drv);
	ret = driver_add_groups(drv, drv->groups);
	kobject_uevent(&drv->p->kobj, KOBJ_ADD); // uevent 机制通知用户有设备注册了

	return ret;
}
```

##### 关键函数 bus_add_driver

```c
/**
 * bus_add_driver - Add a driver to the bus.
 * @drv: driver.
 */
int bus_add_driver(struct device_driver *drv)
{
	struct subsys_private *sp = bus_to_subsys(drv->bus);
	struct driver_private *priv; // 各种 device driver 的 kobject 都保存在该变量中
	int error = 0;

	priv = kzalloc(sizeof(*priv), GFP_KERNEL);
	klist_init(&priv->klist_devices, NULL, NULL); // 一个 driver 可对应多个 device
	priv->driver = drv;
	drv->p = priv;
 	// 通过这种方式将 sysfs 搭起来么
 	// 这个 drivers_kset 是在 bus_register 是创建的
	priv->kobj.kset = sp->drivers_kset;
	// 初始化该 driver 对应的 kobject，这样才能挂载到 sysfs 中
	// 所有 driver 都使用 driver_ktype 么
	// 这显然不对
	// 传入的 parent == NULL
	error = kobject_init_and_add(&priv->kobj, &driver_ktype, NULL,
				 "%s", drv->name);

	// 在这里将 driver 添加到 list 中
	// 每个 bus 都有一个 sp
	klist_add_tail(&priv->knode_bus, &sp->klist_drivers);
	if (sp->drivers_autoprobe) {
		error = driver_attach(drv); // 然后执行到具体驱动的初始化
	}
	// 涉及到 /sys/module 目录，不清楚是干嘛的
	module_add_driver(drv->owner, drv);

	// 创建 /sys/bus/platform/xxx/uevent 文件
	// uevent 是内核态通知用户态的机制
	error = driver_create_file(drv, &driver_attr_uevent);
	// 配置 attribute
	error = driver_add_groups(drv, sp->bus->drv_groups);

 ...

	return 0;
}
```

这里只是初始化驱动，对应的另外一部分是将设备添加到这套框架里面，最终执行到 probe 函数，调用栈如下，

```c
#0 arm_smmu_device_probe (pdev=0xffffffc081ade9d0 <randomize_kstack_offset>) at drivers/iommu/arm/arm-smmu-v3/arm-smmu-v3.c:3809
#1 0xffffffc0808e1814 in platform_probe (_dev=0xffffff80c0118400) at drivers/base/platform.c:1404
#2 0xffffffc0808de9d4 in call_driver_probe (drv=<optimized out>, drv=<optimized out>, dev=<optimized out>) at drivers/base/dd.c:579
#3 really_probe (dev=0xffffff80c0118410, drv=0xffffffc0808baedc <arm_smmu_device_probe>) at drivers/base/dd.c:658
#4 0xffffffc0808deba4 in __driver_probe_device (drv=0xffffff80c0118400, dev=0xffffffc0808baedc <arm_smmu_device_probe>) at drivers/base/dd.c:800
#5 0xffffffc0808dec90 in driver_probe_device (drv=0xffffff80c0118400, dev=0xffffff80c0118410) at drivers/base/dd.c:830
#6 0xffffffc0808def10 in __driver_attach (dev=0xffffff80c0118410, data=0xffffffc0827d2c70 <arm_smmu_driver+48>) at drivers/base/dd.c:1216
#7 0xffffffc0808dc40c in bus_for_each_dev (bus=0xffffff80c0118400, start=0x0, data=0xffffffc0827d2c70 <arm_smmu_driver+48>,
 fn=0xffffffc0808dee64 <__driver_attach>) at drivers/base/bus.c:368
// 关键在这里，原来挂载 driver 的时候就会检测 devices
// 从这里看，是先挂载 devices，再挂载 driver
#8 0xffffffc0808de10c in driver_attach (drv=0xffffff80c0118400) at drivers/base/dd.c:1233
#9 0xffffffc0808dd768 in bus_add_driver (drv=0xffffffc0827d2c70 <arm_smmu_driver+48>) at drivers/base/bus.c:673
#10 0xffffffc0808dfb54 in driver_register (drv=0xffffffc0827d2c70 <arm_smmu_driver+48>) at drivers/base/driver.c:246
#11 0xffffffc0808e14f8 in __platform_driver_register (drv=0x837bbb2b916b2378, owner=0xffffffc0808baedc <arm_smmu_device_probe>) at drivers/base/platform.c:867
#12 0xffffffc081bcf7a4 in arm_smmu_driver_init () at drivers/iommu/arm/arm-smmu-v3/arm-smmu-v3.c:3947
#13 0xffffffc080014da8 in do_one_initcall (fn=0xffffffc081bcf784 <arm_smmu_driver_init>) at init/main.c:1239
#14 0xffffffc081b71244 in do_initcall_level (command_line=<optimized out>, level=<optimized out>) at init/main.c:1301
#15 do_initcalls () at init/main.c:1317
#16 do_basic_setup () at init/main.c:1336
#17 kernel_init_freeable () at init/main.c:1554
#18 0xffffffc0810a0554 in kernel_init (unused=0xffffff80c0118400) at init/main.c:1444
#19 0xffffffc080015dac in ret_from_fork () at arch/arm64/kernel/entry.S:861
```

##### 关键函数 driver_attach

```c
int driver_attach(struct device_driver *drv)
{
	// 遍历 sp->klist_devices，sp 就是存储每个 bus 都有的一些变量，后面有介绍
	// 那 devices 是在哪里添加到 sp->klist_devices 的呢
	return bus_for_each_dev(drv->bus, NULL, drv, __driver_attach);
}
```

##### 关键函数 __driver_attach

这个函数很有意思，匹配失败不会返回异常，而是继续执行，

```c
static int __driver_attach(struct device *dev, void *data)
{
	struct device_driver *drv = data;
	bool async = false;
	int ret;

	/*
	 * Lock device and try to bind to it. We drop the error
	 * here and always return 0, because we need to keep trying
	 * to bind to devices and some drivers will return an error
	 * simply if it didn't support the device.
	 *
	 * driver_probe_device() will spit a warning if there
	 * is an error.
	 */

	// 执行各个 bus 的 match 回调函数，就 platform 而言
	// 是 platform_match
	// 最终还是通过 struct of_device_id 来匹配的
	ret = driver_match_device(drv, dev);

	// 支持异步 probe 的设备
	if (driver_allows_async_probing(drv)) {

	...

		if (async)
			async_schedule_dev(__driver_attach_async_helper, dev);
		return 0;
	}

	__device_driver_lock(dev, dev->parent);
	driver_probe_device(drv, dev);
	__device_driver_unlock(dev, dev->parent);

	return 0;
}
```

##### 关键函数 really_probe

`driver_probe_device` -> `__driver_probe_device` -> `really_probe`

```c
static int really_probe(struct device *dev, struct device_driver *drv)
{

	... // 延迟/异步 prbe 的操作

re_probe:
	dev->driver = drv; // 这里不就关联上了么

	/* If using pinctrl, bind pins now before probing */
	ret = pinctrl_bind_pins(dev); // pinctrl 是用来干嘛的？

	// 这里具体执行什么操作？
	// 后续 iommu 设备初始化就要走这里
	if (dev->bus->dma_configure) {
		ret = dev->bus->dma_configure(dev);
	}

	// 和前面类似的，将 device 添加到 sysfs 中
	ret = driver_sysfs_add(dev);
	// 后面就是调用 platform_probe
	// 然后通过 contain_of(dev->driver) 找到 drv->probe 回调函数
	ret = call_driver_probe(dev, drv);
	ret = device_add_groups(dev, drv->dev_groups);

	...

	pinctrl_init_done(dev);

	if (dev->pm_domain && dev->pm_domain->sync)
		dev->pm_domain->sync(dev);

	driver_bound(dev);

 ...

	return ret;
}
```

接下来的问题就是，`bus_for_each_dev` 怎样就能遍历到 bus 上所有的 devices 呢，在哪里创建/添加设备的。

##### 设备初始化

使用 gdb 调试的话，调用栈如下，

```c
#0 bus_add_device (dev=0xffffff80c010fc10) at drivers/base/bus.c:483
#1 0xffffffc0808dad00 in device_add (dev=0xffffff80c010fc10) at drivers/base/core.c:3603
#2 0xffffffc080e288c8 in of_device_add (ofdev=0xffffff80c010fc10) at drivers/of/platform.c:70
#3 0xffffffc080e289a0 in of_platform_device_create_pdata (np=0xffffff81feff8600, bus_id=0xffffff80c010fc00 "", platform_data=0x0, parent=0x0) at drivers/of/platform.c:223
#4 0xffffffc080e28be0 in of_platform_bus_create (bus=0xffffff81feff8600, matches=0xffffffc0815b7a98 <of_default_bus_match_table>, lookup=0x0, parent=0x0, strict=true) at drivers/of/platform.c:418
// 该函数中有 for 循环，会遍历 / 下的所有 device_node
#5 0xffffffc080e28f98 in of_platform_populate (root=0xffffff81fefefa78, matches=0xffffffc0815b7a98 <of_default_bus_match_table>, lookup=0x0, parent=0x0) at drivers/of/platform.c:511
#6 0xffffffc081be831c in of_platform_default_populate (parent=<optimized out>, lookup=<optimized out>, root=<optimized out>) at drivers/of/platform.c:530
#7 of_platform_default_populate_init () at drivers/of/platform.c:628
#8 0xffffffc080014da8 in do_one_initcall (fn=0xffffffc081be8248 <of_platform_default_populate_init>) at init/main.c:1239
#9  0xffffffc081b71244 in do_initcall_level (command_line=<optimized out>, level=<optimized out>) at init/main.c:1301
#10 do_initcalls () at init/main.c:1317
#11 do_basic_setup () at init/main.c:1336
#12 kernel_init_freeable () at init/main.c:1554
#13 0xffffffc0810a0554 in kernel_init (unused=0xffffff80c010fc10) at init/main.c:1444
#14 0xffffffc080015dac in ret_from_fork () at arch/arm64/kernel/entry.S:861
```

##### 关键函数 of_platform_device_create_pdata

其实从调用栈就大概清楚设备的初始化流程，涉及的具体细节之后再分析。

```c
static struct platform_device *of_platform_device_create_pdata(
					struct device_node *np,
					const char *bus_id,
					void *platform_data,
					struct device *parent)
{
	struct platform_device *dev;

    // 创建/初始化 struct device * 指针
    // 对于 dev->resource 指针，最终会调用到 __of_address_to_resource 函数
    // 解析 dts 的 "reg"/"assigned-addresses" 配置，这些就是所有的 resource
    // 后续 platform_get_resource 函数其实就是获取这些 resource
	dev = of_device_alloc(np, bus_id, parent);

	dev->dev.coherent_dma_mask = DMA_BIT_MASK(32);
	if (!dev->dev.dma_mask)
		dev->dev.dma_mask = &dev->dev.coherent_dma_mask;
	dev->dev.bus = &platform_bus_type;
	dev->dev.platform_data = platform_data;
	of_msi_configure(&dev->dev, dev->dev.of_node);

    // 添加到 sysfs 中
	if (of_device_add(dev) != 0) {
		platform_device_put(dev);
		goto err_clear_flag;
	}

	return dev;
}
```

##### 关键函数 device_initialize

device 结构体是怎样创建的？

```c
void device_initialize(struct device *dev)
{
	dev->kobj.kset = devices_kset;
	kobject_init(&dev->kobj, &device_ktype);
	INIT_LIST_HEAD(&dev->dma_pools);
	mutex_init(&dev->mutex);
	lockdep_set_novalidate_class(&dev->mutex);
	spin_lock_init(&dev->devres_lock);
	INIT_LIST_HEAD(&dev->devres_head);
	device_pm_init(dev);
	set_dev_node(dev, NUMA_NO_NODE);
	INIT_LIST_HEAD(&dev->links.consumers);
	INIT_LIST_HEAD(&dev->links.suppliers);
	INIT_LIST_HEAD(&dev->links.defer_sync);
	dev->links.status = DL_DEV_NO_DRIVER;
#if defined(CONFIG_ARCH_HAS_SYNC_DMA_FOR_DEVICE) || \
    defined(CONFIG_ARCH_HAS_SYNC_DMA_FOR_CPU) || \
    defined(CONFIG_ARCH_HAS_SYNC_DMA_FOR_CPU_ALL)
	dev->dma_coherent = dma_default_coherent;
#endif
	swiotlb_dev_init(dev);
}
```

##### 关键函数 device_add

```c
int device_add(struct device *dev)
{
	struct subsys_private *sp;
	struct device *parent;
	struct kobject *kobj;
	struct class_interface *class_intf;
	int error = -EINVAL;
	struct kobject *glue_dir = NULL;

	dev = get_device(dev);

    ...

	parent = get_device(dev->parent);
	kobj = get_device_parent(dev, parent);

	if (kobj)
		dev->kobj.parent = kobj;

	/* use parent numa_node */
	if (parent && (dev_to_node(dev) == NUMA_NO_NODE))
		set_dev_node(dev, dev_to_node(parent));

	/* first, register with generic layer. */
	/* we require the name to be set before, and pass NULL */
	error = kobject_add(&dev->kobj, dev->kobj.parent, NULL);

	/* notify platform of device entry */
	device_platform_notify(dev);

	error = device_create_file(dev, &dev_attr_uevent);

	error = device_add_class_symlinks(dev);
	error = device_add_attrs(dev);
	error = bus_add_device(dev);
	error = dpm_sysfs_add(dev);
	device_pm_add(dev);

	if (MAJOR(dev->devt)) {
		error = device_create_file(dev, &dev_attr_dev);
		if (error)
			goto DevAttrError;

		error = device_create_sys_dev_entry(dev);
		if (error)
			goto SysEntryError;

		devtmpfs_create_node(dev);
	}

	/* Notify clients of device addition.  This call must come
	 * after dpm_sysfs_add() and before kobject_uevent().
	 */
	bus_notify(dev, BUS_NOTIFY_ADD_DEVICE);
	kobject_uevent(&dev->kobj, KOBJ_ADD);

	/*
	 * Check if any of the other devices (consumers) have been waiting for
	 * this device (supplier) to be added so that they can create a device
	 * link to it.
	 *
	 * This needs to happen after device_pm_add() because device_link_add()
	 * requires the supplier be registered before it's called.
	 *
	 * But this also needs to happen before bus_probe_device() to make sure
	 * waiting consumers can link to it before the driver is bound to the
	 * device and the driver sync_state callback is called for this device.
	 */
	if (dev->fwnode && !dev->fwnode->dev) {
		dev->fwnode->dev = dev;
		fw_devlink_link_device(dev);
	}

	bus_probe_device(dev);

	/*
	 * If all driver registration is done and a newly added device doesn't
	 * match with any driver, don't block its consumers from probing in
	 * case the consumer device is able to operate without this supplier.
	 */
	if (dev->fwnode && fw_devlink_drv_reg_done && !dev->can_match)
		fw_devlink_unblock_consumers(dev);

	if (parent)
		klist_add_tail(&dev->p->knode_parent,
			       &parent->p->klist_children);

	sp = class_to_subsys(dev->class);
	if (sp) {
		mutex_lock(&sp->mutex);
		/* tie the class to the device */
		klist_add_tail(&dev->p->knode_class, &sp->klist_devices);

		/* notify any interfaces that the device is here */
		list_for_each_entry(class_intf, &sp->interfaces, node)
			if (class_intf->add_dev)
				class_intf->add_dev(dev);
		mutex_unlock(&sp->mutex);
		subsys_put(sp);
	}
done:
	put_device(dev);
	return error;
}
```

#### sysfs 文件系统[^4]

> 用户对 kernel 空间特定数据属性访问是通过 sysfs 来实现，具体形式为读写设备文件属性

我的理解，sysfs 就是为了在用户态能够控制/管理驱动实现的一套接口。

##### kobject[^5]

kobject 是设备驱动程序 模型的核心数据结构，每个 kobject 对应于 sysfs 文件系统中的一个目录。

```c
struct kobject {
	const char		*name;
	struct list_head	entry; // 用于将 Kobject 加入到 Kset 中的 list_head
    // 指向 parent kobject，以此形成层次结构
    // 这就好理解了，都是以“子->父”的方式形成层次结构
	struct kobject		*parent;
	struct kset		*kset; // 该 kobject 属于的 Kset
	const struct kobj_type	*ktype; // 该 Kobject 属于的 kobj_type
	struct kernfs_node	*sd; /* sysfs directory entry */
	struct kref		kref;

	unsigned int state_initialized:1; // 是否已经初始化
	unsigned int state_in_sysfs:1; // 是否已在 sysfs 中呈现
	unsigned int state_add_uevent_sent:1;
	unsigned int state_remove_uevent_sent:1;
	unsigned int uevent_suppress:1;

	...
};
```

##### ktype

Ktype 代表 Kobject（严格地讲，是包含 了Kobject 的数据结构）的属性操作集合（由于通用性，多个 Kobject 可能共用同一个属性操作集，因此把 Ktype 独立出来了）。

```c
struct kobj_type {
    // 通过该回调函数，可以将包含该种类型 kobject 的数据结构的内存空间释放掉
	void (*release)(struct kobject *kobj);
	const struct sysfs_ops *sysfs_ops;
	const struct attribute_group **default_groups;
	const struct kobj_ns_type_operations *(*child_ns_type)(const struct kobject *kobj);
	const void *(*namespace)(const struct kobject *kobj);
	void (*get_ownership)(const struct kobject *kobj, kuid_t *uid, kgid_t *gid);
};
```

##### kset

kset 是一个特殊的 kobject（因此它也会在"/sys/“文件系统中以目录的形式出现），**用来集合相似的 kobject**，它在 sysfs 中也会以目录的形式体现。

```c
struct kset {
	struct list_head list; // 用于保存该 kset 下所有的 kobject
	spinlock_t list_lock;
	struct kobject kobj; // 该 kset 自己的 kobject（kset 是一个特殊的 kobject，也会在 sysfs 中以目录的形式体现）
	const struct kset_uevent_ops *uevent_ops;
};
```

> Kobject 的核心功能是：**保持一个引用计数，当该计数减为 0 时，自动释放（由本文所讲的 kobject 模块负责） Kobject 所占用的内存空间**。这就决定了 Kobject 必须是动态分配的（只有这样才能动态释放）。
>
> 而 Kobject 大多数的使用场景，是内嵌在大型的数据结构中（如 Kset、device_driver 等），因此这些大型的数据结构，也必须是动态分配、动态释放的。那么释放的时机是什么呢？**是内嵌的 Kobject 释放时**。但是 Kobject 的释放是由 Kobject 模块自动完成的（在引用计数为 0 时），那么怎么一并释放包含自己的大型数据结构呢？
>
> 这时 Ktype 就派上用场了。我们知道，Ktype 中的 release 回调函数负责释放 Kobject（甚至是包含 Kobject 的数据结构）的内存空间，那么 Ktype 及其内部函数，是由谁实现呢？是由上层数据结构所在的模块！因为只有它，才清楚 Kobject 嵌在哪个数据结构中，并通过 Kobject 指针以及自身的数据结构类型，找到需要释放的上层数据结构的指针，然后释放它。
>
> 讲到这里，就清晰多了。所以，每一个内嵌 Kobject 的数据结构，例如 kset、device、device_driver 等等，都要实现一个 Ktype，并定义其中的回调函数。同理，sysfs 相关的操作也一样，必须经过 ktype 的中转，因为 sysfs 看到的是 Kobject，而真正的文件操作的主体，是内嵌 Kobject 的上层数据结构！

下面就是看这几个数据结构怎样用起来，

##### 关键函数 kobject_create_and_add

```c
struct kobject *kobject_create_and_add(const char *name, struct kobject *parent)
{
	struct kobject *kobj;
	int retval;

	kobj = kobject_create();

	retval = kobject_add(kobj, parent, "%s", name);
	return kobj;
}
```

##### 关键函数 kobject_add

`kobject_add` -> `kobject_add_varg`

```c
static __printf(3, 0) int kobject_add_varg(struct kobject *kobj,
					   struct kobject *parent,
					   const char *fmt, va_list vargs)
{
	int retval;

	retval = kobject_set_name_vargs(kobj, fmt, vargs);
	kobj->parent = parent; // parent 可能为 NULL
	return kobject_add_internal(kobj);
}
```

##### 关键函数 kobject_add_internal

```c
static int kobject_add_internal(struct kobject *kobj)
{
	int error = 0;
	struct kobject *parent;

	...

	parent = kobject_get(kobj->parent);

	/* join kset if set, use it as parent if we do not already have one */
	if (kobj->kset) {
		if (!parent) // parent 为 NULL 的话就用 kset 作为 parent
			parent = kobject_get(&kobj->kset->kobj);
		kobj_kset_join(kobj); // 将 kobject 添加到 kset->list 中，这样才好形成完整的架构
		kobj->parent = parent; // 双向指针就建立起来了
	}

    // 调用 sysfs 的相关接口，在 sysfs 下创建该 kobject 对应的目录
    // 这块之后有需要再深入跟踪
	error = create_dir(kobj);
	if (error) {
		...
	} else
		kobj->state_in_sysfs = 1; // 前面介绍过，该 kobject 已经添加到 sysfs 中

	return error;
}
```

##### 关键函数 kset_create_and_add

```c
struct kset *kset_create_and_add(const char *name,
				 const struct kset_uevent_ops *uevent_ops,
				 struct kobject *parent_kobj)
{
	struct kset *kset;
	int error;

    // 分配 kset 然后初始化各种变量
	kset = kset_create(name, uevent_ops, parent_kobj);

    // kset 是一个特殊的 kobject，所有这里也是调用 kobject_add_internal
    // 添加到 sysfs 中
	error = kset_register(kset);

	return kset;
}
```

要记住，kset 是用来集合相似的 kobject，就以 platform 而言，在创建 platform bus 时，会在该 bus 下面创建两个 kset（目录），devices/drivers，后续所有的挂载到 platform bus 的 devices/drivers 都可以从这里面找到。

提供的接口种类很多，create 和 init 的区别就是 create 是先申请内存，再 init，没啥区别。

#### 设备驱动程序模型的组件

设备驱动程序模型建立在总线、设备、设备驱动器等结构上。

##### bus_type

内核所支持的每一种总线类型都由 `bus_type` 描述。

```c
struct bus_type {
	const char		*name; // 显示在 /sys 目录下的 bus name
	const char		*dev_name; // 两者有啥区别？
	struct device		*dev_root; // 和 sys system 功能有关
	const struct attribute_group **bus_groups;
	const struct attribute_group **dev_groups;
	const struct attribute_group **drv_groups;
	// 一个由具体的 bus driver 实现的回调函数
	// 当任何属于该 Bus 的 device 或者 device_driver 添加到内核时，内核都会调用该接口
	// 如果新加的 device 或 device_driver 匹配上了自己的另一半的话
	// 该接口要返回非零值，此时 Bus 模块的核心逻辑就会执行后续的处理
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
	// 最新的内核已经没有该变量了，改成通过 bus_to_subsys 用遍历的方式找到对应的 sp 了
	struct subsys_private *p;
	struct lock_class_key lock_key;
	bool need_parent_lock;
};
```
###### subsys_private
在 bus/driver 注册的过程中，多次用到这个结构体，
```c
struct subsys_private {
	struct kset subsys; // 代表了本 bus（如/sys/bus/spi），它下面可以包含其它的 kset 或者其它的 kobject
	// devices_kset 和 drivers_kset 则是 bus 下面的两个 kset
	// 如 /sys/bus/spi/devices 和 /sys/bus/spi/drivers
	// 分别包括本 bus 下所有的 device 和 device_driver
	struct kset *devices_kset;
	struct list_head interfaces; // interfaces 又是干啥的
	struct mutex mutex;
	struct kset *drivers_kset;
	struct klist klist_devices;
	struct klist klist_drivers;
	struct blocking_notifier_head bus_notifier;
	unsigned int drivers_autoprobe:1;
	const struct bus_type *bus; // 保存上层的 bus 指针
	struct device *dev_root;
	struct kset glue_dirs;
	const struct class *class; // 保存上层的 class 指针
	struct lock_class_key lock_key;
};
```
> 按理说，这个结构就是集合了一些 bus 模块需要使用的私有数据，如 kset、klist 等，命名为 bus_private 会好点（就像 device_driver 模块一样）。不过为什么内核没这么做呢？看看 include/linux/device.h 中的 `struct class` 结构就知道了，因为 class 结构中也包含了一个一模一样的 `struct subsys_private` 指针，看来 class 和 bus 很相似啊。
>
> 想到这里，就好理解了，无论是 bus，还是 class，还是我们会在后面看到的一些虚拟的子系统，它都构成了一个“子系统（sub-system）”，该子系统会包含形形色色的 device 或 device_driver，就像一个独立的王国一样，存在于内核中。而这些子系统的表现形式，就是 /sys/bus（或 /sys/class，或其它）目录下面的子目录，每一个子目录，都是一个子系统（如/sys/bus/spi/）。
###### 总线的注册
bus 的注册是由 bus_register 接口实现的，内核中有很多种总线，
![system-all-bus.png](https://github.com/UtopianFuture/UtopianFuture.github.io/blob/master/image/system-all-bus.png?raw=true)

```c
/**
 * bus_register - register a driver-core subsystem
 * @bus: bus to register
 *
 * Once we have that, we register the bus with the kobject
 * infrastructure, then register the children subsystems it has:
 * the devices and drivers that belong to the subsystem.
 */
int bus_register(const struct bus_type *bus)
{
	int retval;
	struct subsys_private *priv;
	struct kobject *bus_kobj;
	struct lock_class_key *key;
	// 该变量保存了 bus 的私有数据
	// 原来是在这里初始化的
	priv = kzalloc(sizeof(struct subsys_private), GFP_KERNEL);
	// 后续通过这个条件来找到 bus 对应的 sp
	priv->bus = bus;
	BLOCKING_INIT_NOTIFIER_HEAD(&priv->bus_notifier);
	bus_kobj = &priv->subsys.kobj;
	retval = kobject_set_name(bus_kobj, "%s", bus->name);
	bus_kobj->kset = bus_kset;
	bus_kobj->ktype = &bus_ktype;
	priv->drivers_autoprobe = 1;
	retval = kset_register(&priv->subsys);
	retval = bus_create_file(bus, &bus_attr_uevent);
	// /sys/bus/xxx/ 下面都有 devices 和 drivers 目录
	// 就是在这里创建的，表示该 bus 中所有的 devices 和 drivers 的 kset
	// kset 也是特殊的 kobject
	priv->devices_kset = kset_create_and_add("devices", NULL, bus_kobj);
	priv->drivers_kset = kset_create_and_add("drivers", NULL, bus_kobj);
	INIT_LIST_HEAD(&priv->interfaces);
	key = &priv->lock_key;
	lockdep_register_key(key);
	__mutex_init(&priv->mutex, "subsys mutex", key);
	klist_init(&priv->klist_devices, klist_devices_get, klist_devices_put);
	klist_init(&priv->klist_drivers, NULL, NULL);
	retval = add_probe_files(bus);
 ...
	retval = sysfs_create_groups(bus_kobj, bus->bus_groups);
	return 0;
}
```
```
#0 bus_register (bus=0xffffffc0827d3ca0 <platform_bus>) at drivers/base/bus.c:851
#1 0xffffffc081bd014c in platform_bus_init () at drivers/base/platform.c:1528
#2 0xffffffc081bd02a4 in driver_init () at drivers/base/init.c:36 // /sys/bus 目录也是在这个函数中创建的
#3 0xffffffc081b71188 in do_basic_setup () at init/main.c:1333
#4 kernel_init_freeable () at init/main.c:1554
#5 0xffffffc0810a0554 in kernel_init (unused=0xffffffc0827d3f98 <platform_bus_type>) at init/main.c:1444
#6 0xffffffc080015dac in ret_from_fork () at arch/arm64/kernel/entry.S:861
```
##### class
类本质上是要提供一个标准的方法，从而为向用户态应用程序导出逻辑设备的接口。
类的真正意义在于作为同属于一个类的多个设备的容器。也就是说，类是一种人造概念，目的就是为了对各种设备进行分类管理。当然，类在分类的同时还对每个类贴上了一些“标签”，这也是设备驱动模型为我们写驱动提供的基础设施。所以一个设备可以从类这个角度讲它是属于哪个类，也可以从总线的角度讲它是挂在哪个总线下的。
##### device
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
	...
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
};
```
##### device_driver
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
	const struct of_device_id	*of_match_table; // 这个变量和 dts/acpi 关联起来
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
##### attribute
> 在 sysfs 中，为什么会有 attribute 的概念呢？其实它是对应 kobject 而言的，指的是 kobject 的“属性”。我们知道，sysfs 中的目录描述了 kobject，而 kobject 是特定数据类型变量（如 struct device）的体现。因此 kobject 的属性，就是这些变量的属性。它可以是任何东西，名称、一个内部变量、一个字符串等等。而 attribute，在 sysfs 文件系统中是以文件的形式提供的，即：kobject 的所有属性，都在它对应的 sysfs 目录下以文件的形式呈现。这些文件一般是可读、写的，而 kernel 中定义了这些属性的模块，会根据用户空间的读写操作，记录和返回这些 attribute 的值。
>
> 总结一下：所谓的 attribute，就是内核空间和用户空间进行信息交互的一种方法。例如某个 driver 定义了一个变量，却希望用户空间程序可以修改该变量，以控制 driver 的运行行为，那么就可以将该变量以 sysfs attribute 的形式开放出来。
##### resource
```c
struct resource {
	resource_size_t start;
	resource_size_t end;
	const char *name;
	unsigned long flags;
	unsigned long desc;
	struct resource *parent, *sibling, *child;
};
```

 ### plaform device
相对于 USB、PCI、I2C、SPI 等物理总线来说，**platform 总线是一种虚拟、抽象出来的总线**，实际中并不存在这样的总线。
那为什么需要 platform 总线呢？
因为对于 usb 设备、i2c 设备、pci 设备、spi 设备等等，他们与 cpu 的通信都是直接挂在相应的总线下面与我们的 cpu 进行数据交互的，但是在嵌入式系统中，并不是所有的设备都能够归属于这些常见的总线。在嵌入式系统里面，SoC 系统中集成的独立的外设控制器、挂接在 SoC 内存空间的外设（IOMMU/SMMU 等）却不依附与此类总线。所以 Linux 驱动模型为了保持完整性，将这些设备挂在一条虚拟的总线上（platform总线），而不至于使得有些设备挂在总线上，另一些设备没有挂在总线上。
#### platform_bus_type
这是一个初始化好的全局变量，在 start_kernel 阶段就会注册到系统中，
```c
struct bus_type platform_bus_type = {
	.name		= "platform",
	.dev_groups	= platform_dev_groups,
	.match		= platform_match,
	.uevent		= platform_uevent,
	.probe		= platform_probe,
	.remove		= platform_remove,
	.shutdown	= platform_shutdown,
	.dma_configure	= platform_dma_configure,
	.dma_cleanup	= platform_dma_cleanup,
	.pm		= &platform_dev_pm_ops,
};
```
按照如下路径添加到 kernel 中，
```c
#0 bus_register (bus=0xffffffc0827d3ca0 <platform_bus>) at drivers/base/bus.c:851
#1 0xffffffc081bd014c in platform_bus_init () at drivers/base/platform.c:1528
#2 0xffffffc081bd02a4 in driver_init () at drivers/base/init.c:36
#3 0xffffffc081b71188 in do_basic_setup () at init/main.c:1333
#4 kernel_init_freeable () at init/main.c:1554
#5 0xffffffc0810a0554 in kernel_init (unused=0xffffffc0827d3f98 <platform_bus_type>) at init/main.c:1444
#6 0xffffffc080015dac in ret_from_fork () at arch/arm64/kernel/entry.S:861
```
```c
int __init platform_bus_init(void)
{
	int error;
	early_platform_cleanup();
	error = device_register(&platform_bus); // 为啥是先注册 device，再注册 bus 呢？
	error = bus_register(&platform_bus_type);
	return error;
}
```
#### platform_device
```c
struct platform_device {
	const char	*name;
	int		id;
	bool		id_auto;
	struct device	dev;
	u64		platform_dma_mask;
	struct device_dma_parameters dma_parms;
	u32		num_resources;
	struct resource	*resource; // 在创建 struct device 时调用 of_device_alloc 初始化的
	const struct platform_device_id	*id_entry; // 用来进行与设备驱动匹配用的 id_table 表

    ...
};
```
#### platform_driver
```c
struct platform_driver {
	int (*probe)(struct platform_device *);
	/*
	 * Traditionally the remove callback returned an int which however is
	 * ignored by the driver core. This led to wrong expectations by driver
	 * authors who thought returning an error code was a valid error
	 * handling strategy. To convert to a callback returning void, new
	 * drivers should implement .remove_new() until the conversion it done
	 * that eventually makes .remove() return void.
	 */
	int (*remove)(struct platform_device *);
	void (*remove_new)(struct platform_device *);
	void (*shutdown)(struct platform_device *);
	int (*suspend)(struct platform_device *, pm_message_t state);
	int (*resume)(struct platform_device *);
	struct device_driver driver;
 	// 该设备驱动支持的设备的列表，这个指针去指向 platform_device_id 类型的数组
	const struct platform_device_id *id_table;

    ...
};
```
### 设备文件
### AIO[^2]
在介绍 AIO 之前，我们先缕一缕内核 I/O 系统的演进历史。
- 同步阻塞 I/O
 最常用的一个模型是同步阻塞 I/O 模型。在这个模型中，用户空间的应用程序执行一个系统调用，这会导致应用程序阻塞。这意味着应用程序会一直阻塞，直到系统调用完成为止（数据传输完成或发生错误）。调用应用程序处于一种不再消费 CPU 而只是简单等待响应的状态，因此从处理的角度来看，这是非常有效的。
  而从应用程序的角度来说，系统调用会延续很长时间。实际上，在内核执行读操作和其他工作时，应用程序的确会被阻塞。
- 同步非阻塞 I/O
 在这种模型中，设备是以非阻塞的形式打开的。这意味着 I/O 操作不会立即完成，I/O 系统调用可能会返回一个错误代码，说明这个命令不能立即满足（EAGAIN 或 EWOULDBLOCK）。
  非阻塞的实现是 **I/O 系统调用可能并不会立即满足，需要应用程序调用许多次来等待操作完成**。这可能效率不高，因为在很多情况下，当内核执行这个命令时，应用程序必须要进行忙碌等待，直到数据可用为止，或者试图执行其他工作。这个方法可以引入 I/O 操作的延时，因为数据在内核中变为可用到用户调用 read 返回数据之间存在一定的间隔，这会导致整体数据吞吐量的降低。
  总结起来就是同步 IO 必须等待内核把 IO 操作处理完成后系统调用才返回。而异步 IO 不必等待 IO 操作完成，而是向内核发起一个 IO 操作就立刻返回，当内核完成 IO 操作后，会通过信号的方式通知应用程序。其适用于 I/O 密集型应用，可以在内核处理 I/O 时继续执行，同时也能批量发起 I/O 操作。
  内核中的 I/O 框架为 AIO，这个在内核 2.5 就添加进去了，很成熟，网上有很多资料，这里记录一下[2]，之后有需要再详细分析。
  `Linux Native AIO` 是 Linux 支持的原生 AIO，原生是区别与 Linux 存在很多第三方的异步 IO 库，如 `libeio` 和 `glibc AIO`。很多第三方的异步 IO 库都不是真正的异步 IO，而是使用多线程来模拟异步 IO，如 `libeio` 就是使用多线程来模拟异步 IO 的。
  Linux 原生 AIO 主要有以下步骤：
- 通过调用 `open` 系统调用打开要进行异步 I/O 的文件，要注意的是 AIO 操作必须设置 `O_DIRECT` 直接 I/O 标志位；
- 调用 `io_setup` 系统调用创建一个异步 I/O 上下文；
- 调用 `io_prep_pwrite` 或者 `io_prep_pread` 函数创建一个异步写或者异步读任务；
- 调用 `io_submit` 系统调用把异步 I/O 任务提交到内核；
- 调用 `io_getevents` 系统调用获取异步 I/O 的结果；
我们获取异步 IO 操作的结果可以在一个无限循环中进行，也可以使用基于 `eventfd` 事件通知的机制，通过 `eventfd` 和 `epoll` 结合来实现事件驱动的方式来获取异步 I/O 操作的结果。
### io_uring[^3]
`io_uring` 是 2019 年 **Linux 5.1** 内核首次引入的高性能 **异步 I/O 框架**，能显著加速 I/O 密集型应用的性能。
它的革命性体现在：
- 它首先和最大的贡献在于：**统一了 Linux 异步 I/O 框架**，
 - Linux AIO **只支持 direct I/O** 模式的**存储文件** （storage file），而且主要用在**数据库这一细分领域**；
 - `io_uring` 支持存储文件和网络文件（network sockets），也支持更多的异步系统调用 （`accept/openat/stat/...`），而非仅限于 `read/write` 系统调用。
- 在**设计上是真正的异步 I/O**，作为对比，Linux AIO 虽然也是异步的，但仍然可能会阻塞，某些情况下的行为也无法预测；
- **灵活性和可扩展性**非常好，甚至能基于 `io_uring` 重写所有系统调用，而 Linux AIO 设计时就没考虑扩展性。
`io_uring` **首先和最重要的贡献**在于： **将 linux-aio 的所有优良特性带给了普罗大众**（而非局限于数据库这样的细分领域）。
更详细的内容可以看[这篇](https://arthurchiao.art/blog/intro-to-io-uring-zh/#21-%E4%B8%8E-linux-aio-%E7%9A%84%E4%B8%8D%E5%90%8C)文章。
### 驱动编写
如下是一个驱动的示例：
```c
static int __init system_test_init(void)
{
	int ret;
	dma_mask = DMA_BIT_MASK(64);
	system_test_major = register_chrdev(0, "system_test", &system_test_fops);
	if (system_test_major < 0) {
		pr_err("%s, register_chrdev failed\n", __func__);
		return system_test_major;
	}
	system_test_class = class_create("system_test");
	if (IS_ERR(system_test_class)) {
		pr_err("%s, class_create failed\n", __func__);
		goto fail_class_create;
	}
	system_test_dev = device_create(system_test_class, NULL,
					MKDEV(system_test_major, 0), NULL, "system_test");
	if (IS_ERR(system_test_dev)) {
		pr_err("%s, device_create failed\n", __func__);
		goto fail_device_create;
	}
	if (!system_test_dev->dma_mask)
		system_test_dev->dma_mask = &dma_mask;
	ret = dma_set_mask_and_coherent(system_test_dev, DMA_BIT_MASK(64));
	if (ret) {
		pr_err("%s, dma_set_mask_and_coherent failed\n", __func__);
		goto fail_set;
	}
	return 0;

fail_set:
	device_destroy(system_test_class, MKDEV(system_test_major, 0));
fail_device_create:
	class_destroy(system_test_class);
fail_class_create:
	unregister_chrdev(system_test_major, "system_test");
	return -1;
}

static void __exit system_test_exit(void)
{
	device_destroy(system_test_class, MKDEV(system_test_major, 0));
	class_destroy(system_test_class);
	unregister_chrdev(system_test_major, "system_test");
}
module_init(system_test_init);
module_exit(system_test_exit);
```
### 问题
- kobject 到底是用来干什么的，它和总线，设备，类之间有什么关系；sysfs 下面有各种各样的目录，某个设备接口会出现在多个目录下，为什么？
 看各种博客的解释是这样的，
 > 用来导出内核对象(kernel object)的数据、属性到用户空间，以文件目录结构的形式为用户空间提供对这些数据、属性的访问支持。从驱动开发的角度，/sysfs 为用户提供了除了设备文件 /dev 和 /proc 之外的另外一种通过用户空间访问内核数据的方式。
 >  用户态访问内核数据结构，这一点有那么重要么？
- 每个设备到底是怎样组织的，都在 /dev 目录下么？
 不对啊，/dev 下面和 /sysfs 下面的东西完全不一样；
- kobject 和 kset 又是什么关系？
 kset 是一个相同类型的 kobject 的集合，所谓相同类型，即使用相同的回调函数；
### Reference
[^1]: [设备驱动模型](https://www.cnblogs.com/deng-tao/p/6033932.html)
[^2]: [异步IO](https://zhuanlan.zhihu.com/p/368913613)
[^3]: [io_uring](https://arthurchiao.art/blog/intro-to-io-uring-zh/#21-%E4%B8%8E-linux-aio-%E7%9A%84%E4%B8%8D%E5%90%8C)
[^4]: [sysfs](https://www.cnblogs.com/linfeng-learning/p/9313757.html)
