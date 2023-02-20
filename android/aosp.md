​

## Android Open Source Project

### Android 系统架构

![android-structure](/home/guanshun/gitlab/UFuture.github.io/image/android-structure.png)

### AOSP 目录结构

- art: Android Runtime 安卓运行时。这个会提前把字节码编译成二进制机器码保存起来，执行的时候加载速度比较快。Dalvik 虚拟机则是在加载以后再去编译的，所以速度上 ART 会比 Dalvik 快一点。牺牲空间来赢取时间。；
- bionic: 基础 C 库源码，Android 改写的 C/C++ 库。；
- bootable: 系统启动引导相关程序；
- build: 用于构建 Android 系统的工具，也就是用于编译 Android 系统的；
- cts: Compatibility Test Suite 兼容性测试；
- dalvik: dalvik 虚拟机，用于解析执行 dex 文件的虚拟机；
- developers: 开发者目录；
- development: Android 应用开发基础设施相关；
- devices: Android 支持的各种设备及相关配置，什么索尼、HTC、自己的产品，就可以定义在这个目录下；
- external: Android 中使用的外部开源库；
- frameworks: 系统架构，Android 的核心；
- hardware: hal 层代码，硬件抽象层；
- libcore: Android Java 核心类库；
- libnativehelper: native 帮助库，实现 JNI 库的相关文件；
- out: 输出目录，编译以后生成的目录，相关的产出就在这里；
- packages: 应用程序包。一些系统的应用就在这里了，比如说蓝牙，Launcher，相机，拨号之类的；
- pdk: 本地开发套件；
- platform_testing: 平台测试；
- prebuilts: x86/arm 架构下预编译的文件；
- sdk: Android 的 Java 层 sdk；
- system: Android 底层文件系统库、应用和组件；
- toolchain: 工具链；
- tools: 工具文件。
