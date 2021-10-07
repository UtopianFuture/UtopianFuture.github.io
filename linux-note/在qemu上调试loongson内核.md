## 在qemu上调试loongson内核

### 零、环境

1. ubuntu 20.04LTS
2. LINUX-4.19-LOONGSON
3. gcc 9.3.0

### 一、安装6.0.0版本的qemu

1. 从[qemu官网](https://www.qemu.org/download/)下载6.0.0版本的qemu

2. 进入qemu-6.0.0目录，进行配置安装

   ```
   $cd qemu-6.0.0
   $./configure --enable-kvm --enable-debug --enable-vnc --enable-werror  --target-list="mips64-softmmu"
   $make -j8
   $sudo make install
   ```

   configure脚本用于生成Makefile，其选项可以用`./configure --help`查看。这里使用到的选项含义如下：

   ```
   --enable-kvm：编译KVM模块，使QEMU可以利用KVM来访问硬件提供的虚拟化服务。
   --enable-vnc：启用VNC。
   --enalbe-werror：编译时，将所有的警告当作错误处理。
   --target-list：选择目标机器的架构。默认是将所有的架构都编译，但为了更快的完成编译，指定需要的架构即可。这里选择mips64架构，mips是big-endian的mips架构，mipsel是little-endian的mips架构。
   ```

3. 编译内核

   

   遇到的bug：

   a. `fatal error: openssl/opensslv.h: No such file or directory`

   原因：缺少openssl库。

   解决：安装OpenSSL development package:

   ```
   sudo apt-get install libssl-dev
   ```

   b. `The frame size of 1032 bytes is larger than 1024 bytes`

   原因：内核中设置了堆栈报警大小，其默认为1024bytes。

   解决：将其修改为4096既可以消除告警信息。

   ```
   make menuconfig
   kernel hacking
   修改warn for stack frames larger than 的数值，将其修改为4096（最好不要大过这个数值）
   ```

   c. `No rule to make target 'debian/canonical-certs.pem', needed by 'certs/x509_certificate_list'. Stop`

   解决：修改.config文件中的`CONFIG_SYSTEM_TRUSTED_KEYS`

   ```
   CONFIG_SYSTEM_TRUSTED_KEYS=""
   ```

   d. `warning: Cannot use CONFIG_STACK_VALIDATION=y, please install libelf-dev, libelf-devel or elfutils-libelf-devel`

   ```
   sudo apt install libelf-dev
   ```

   e. `fatal error: asm/dma-coherence.h: No such file or directory`

   原因：asm中没有`dma-coherence.h`头文件。

   解决：在网上找到`dma-coherence.h`头文件，发现不仅仅是`dma-coherence.h`没有，还有很多相关头文件没有，于是[下载](https://git.whoi.edu/edison/edison-linux/tree/64131a87f2aae2ed9e05d8227c5b009ca6c50d98/arch/mips/include)整个文件，将`arch/mips/include`中的asm和uapi分别`cp`到`/usr/include/x86_64-linux-gnu/asm`和`/usr/include/x86_64-linux-gnu`中，重新`make`。当然不是所有的头文件都有，遇到同样的问题还是在网上找头文件，然后手动写到`/usr/include/x86_64-linux-gnu`里。

   f. `BTF: .tmp_vmlinux.btf: pahole (pahole) is not available`

   解决：

   ```
   sudo apt install dwarves
   ```

   `BTF: .tmp_vmlinux.btf: pahole version v1.15 is too old, need at least v1.16`

   最新的安装包[在这](http://archive.ubuntu.com/ubuntu/pool/universe/d/dwarves-dfsg/dwarves_1.17-1_amd64.deb)。

### reference

[1]https://cloud.tencent.com/developer/article/1039421

