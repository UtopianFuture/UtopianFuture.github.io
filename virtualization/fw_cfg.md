## fw_cfg 设备

QEMU 通过 Fireware Configuration(fw_cfg) Device 机制将虚拟机的启动引导顺序、ACPI 和 SMBIOS 表、NUMA 等信息传递给虚拟机。

### fw_cfg 设备的初始化

fw_cfg 时虚拟机用来获取 QEMU 提供数据的一个接口。
