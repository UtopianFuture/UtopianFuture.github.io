## 在 qemu 上调试 loongson 内核

### 一、安装编译支持 LoongArch 版本的 qemu

1. 从内核组网站下载源码

   ```plain
   git clone ssh://liuguanshun@rd.loongson.cn:29418/kernel/qemu && scp -p -P 29418 liuguanshun@rd.loongson.cn:hooks/commit-msg qemu/.git/hooks/
   ```

   进入到 qemu 目录，切换到`uos-dev-3.1.0`分支

   ```plain
   git checkout uos-dev-3.1.0
   ```

2. 运行 configure

   ```plain
   ./configure --target-list=loongarch64-softmmu --enable-debug --enable-profiler --disable-rdma --disable-pvrdma --disable-libiscsi --disable-libnfs --disable-libpmem --disable-glusterfs --disable-opengl --disable-xen --disable-werror --disable-capstone --disable-kvm
   ```

   configure 脚本用于生成 Makefile，其选项可以用`./configure --help`查看。这里使用到的选项含义如下：

   ```plain
   --enable-kvm：编译KVM模块，使QEMU可以利用KVM来访问硬件提供的虚拟化服务。
   --enable-vnc：启用VNC。
   --enalbe-werror：编译时，将所有的警告当作错误处理。
   --target-list：选择目标机器的架构。默认是将所有的架构都编译，但为了更快的完成编译，指定需要的架构即可。mips是big-endian的mips架构，mipsel是little-endian的mips架构。
   ```

   配置 kvm-qemu:

   ```plain
   ./configure --target-list=loongarch64-softmmu --enable-debug --enable-profiler --disable-rdma --disable-pvrdma --disable-libiscsi --disable-libnfs --disable-libpmem --disable-glusterfs --disable-opengl --disable-xen --disable-werror --disable-capstone --disable-spice --disable-libusb --disable-usb-redir --audio-drv-list='' --enable-kvm --enable-tcg-interpreter
   ```

   配置 tcg-qemu：

   ```plain
   ./configure --target-list=loongarch64-softmmu --enable-debug --enable-profiler --disable-rdma --disable-pvrdma --disable-libiscsi --disable-libnfs --disable-libpmem --disable-glusterfs --disable-opengl --disable-xen --disable-werror --disable-capstone --disable-spice --disable-libusb --disable-usb-redir --audio-drv-list='' --disable-kvm
   ```

   注意，KVM 只支持同架构的机器，所以想用 qemu-la 的 kvm 支持只能在 LA 的机器上编译。

3. 编译

   ```plain
   make -j12
   ```

4. 如果 configure 或编译出错，在 github 上找到 dtc 仓库，clone 到 qemu 的根目录中，重新编译。

   ```plain
   git clone git@github.com:dgibson/dtc.git
   ```

5. 编译好后应该出现这样的指示：

   ```plain
   LINK    loongarch64-softmmu/qemu-system-loongarch64
   ```


### 二、编译内核

1. 这里开始犯了错误，即在 x86 的机器上编译内核组的支持 LoongArch 架构的内核，这样肯定只能编译出 x86 的内核。因此要用交叉编译器来编译支持 LoongArch 架构的内核，或者直接在 LA 的机器上编译。下面的 bug 都是在 x86 上编译遇到的，不具代表性，看看就行。

   遇到的 bug：

   -  `fatal error: openssl/opensslv.h: No such file or directory`

     原因：缺少 openssl 库。

     解决：安装 OpenSSL development package:

     ```plain
     sudo apt-get install libssl-dev
     ```

   - `The frame size of 1032 bytes is larger than 1024 bytes`

     原因：内核中设置了堆栈报警大小，其默认为 1024bytes。

     解决：将其修改为 4096 既可以消除告警信息。

     ```plain
     make menuconfig
     kernel hacking
     修改warn for stack frames larger than 的数值，将其修改为4096（最好不要大过这个数值）
     ```

   - `No rule to make target 'debian/canonical-certs.pem', needed by 'certs/x509_certificate_list'. Stop`

     解决：修改.config 文件中的`CONFIG_SYSTEM_TRUSTED_KEYS`

     ```plain
     CONFIG_SYSTEM_TRUSTED_KEYS=""
     ```

   - `warning: Cannot use CONFIG_STACK_VALIDATION=y, please install libelf-dev, libelf-devel or elfutils-libelf-devel`

     ```plain
     sudo apt install libelf-dev
     ```

   - `fatal error: asm/dma-coherence.h: No such file or directory`

     原因：asm 中没有`dma-coherence.h`头文件。

     解决：在网上找到`dma-coherence.h`头文件，发现不仅仅是`dma-coherence.h`没有，还有很多相关头文件没有，于是[下载](https://git.whoi.edu/edison/edison-linux/tree/64131a87f2aae2ed9e05d8227c5b009ca6c50d98/arch/mips/include)整个文件，将`arch/mips/include`中的 asm 和 uapi 分别`cp`到`/usr/include/x86_64-linux-gnu/asm`和`/usr/include/x86_64-linux-gnu`中，重新`make`。当然不是所有的头文件都有，遇到同样的问题还是在网上找头文件，然后手动写到`/usr/include/x86_64-linux-gnu`里。

   - `BTF: .tmp_vmlinux.btf: pahole (pahole) is not available`

     解决：

     ```plain
     sudo apt install dwarves
     ```

     `BTF: .tmp_vmlinux.btf: pahole version v1.15 is too old, need at least v1.16`

   ​		最新的安装包[在这](http://archive.ubuntu.com/ubuntu/pool/universe/d/dwarves-dfsg/dwarves_1.17-1_amd64.deb)。

2. 尝试了在 x86 上编译原版的内核。

   ```plain
   make menuconfig
   ```

   在 menu 中，需要设置以下几个选项，否则会导致无法断点调试：

   a. 取消 Processor type and features -> Build a relocatable kernel

   b. 取消后 Build a relocatable kernel 的子项 Randomize the address of the kernel image (KASLR) 也会一并被取消

   c. 打开 Kernel hacking -> Compile-time checks and compiler options 下的选项：

   - Compile the kernel with debug info
     - Generate dwarf4 debuginfo
   - Compile the kernel with frame pointers

   完成设置之后 Save 保存为 .config 后退出 menu。

   之后编译。编译需要很久，编译成功后会在 arch/x86/boot 中找到 bzImage 文件。

   ```plain
   make -j12
   ```

3. 之后又尝试了在 LA 上编译内核。

   ```plain
   make defconfig
   make menuconfig
   make -j12
   ```

   上述的 menu 选项还是要选的。编译好后在根目录下会有 vmlinx, vmlinuz 文件，vmlinux 就是编译好的内核。

### 三、在 qemu 上调试内核

1. 运行参数

   ```plain
   tcg:
   ./qemu-system-loongarch64 -m 8192M -nographic -cpu Loongson-3A5000 -serial mon:stdio -bios /home/guanshun/gitlab/qemu-la/pc-bios/loongarch_bios.bin -M loongson7a,kernel_irqchip=off -drive file=/home/guanshun/research/bmbt/Loongnix-20.mini.loongarch64.rc1.b2.qcow2,if=virtio -kernel /home/guanshun/research/bmbt/linux-4.19-loongson/vmlinux -append "console=ttyS0 root=/dev/vda1"

   kvm:
   ./qemu-system-loongarch64 -m 8192M -nographic -cpu Loongson-3A5000 -serial mon:stdio -bios /home/guanshun/gitlab/qemu-la/pc-bios/loongarch_bios.bin -M --enable-kvm loongson7a,kernel_irqchip=off -drive file=/home/guanshun/research/bmbt/Loongnix-20.mini.loongarch64.rc1.b2.qcow2,if=virtio -kernel /home/guanshun/research/bmbt/linux-4.19-loongson/vmlinux -append "console=ttyS0 root=/dev/vda1"
   ```

   这里`Loongnix-20.mini.loongarch64.rc1.b2.qcow2`可以从[这里](http://pkg.loongnix.cn:8080/loongnix/isos/)下载，`/home/guanshun/gitlab/qemu-la/pc-bios/loongarch_bios.bin`是 bios，需要换成自己的路径，后面的`-append "console=ttyS0 root=/dev/vda1"`是用于控制台输出的。

   按照这个参数运行就能正常启动内核，用户名：loongson，密码：Loongson20。

   要用 gdb 调试内核的话需要增加`-s -S`参数，即

   ```plain
   ./qemu-system-loongarch64 -m 8192M -nographic -cpu Loongson-3A5000 -serial mon:stdio -bios /home/guanshun/gitlab/qemu-la/pc-bios/loongarch_bios.bin -M loongson7a,kernel_irqchip=off -drive file=/home/guanshun/research/bmbt/Loongnix-20.mini.loongarch64.rc1.b2.qcow2,if=virtio -kernel /home/guanshun/research/bmbt/linux-4.19-loongson/vmlinux -append "console=ttyS0 root=/dev/vda1" -s -S
   ```

   ```plain
   -m 指定内存数量
   -kernel 指定 vmlinux 的镜像路径
   -s 等价于 -gdb tcp::1234 表示监听 1234 端口，用于 gdb 连接
   -S 表示加载后立即暂停，等待调试指令。不设置这个选项内核会直接执行
   -nographic 以及后续的指令用于将输出重新向到当前的终端中，这样就能方便的用滚屏查看内核的输出日志了。
   ```

   这个时候 qemu 会进入暂停状态，如果需要打开 qemu 控制台，可以输入 CTRL + A 然后按 C。

2. 在另一个终端中执行 gdb：

   ```plain
   loongarch64-linux-gnu-gdb vmlinux
   target remote:1234
   b kernel_entry
   c
   ```

   注意这里的`loongarch64-linux-gnu-gdb`是能在 x86 上运行的支持 LA 架构的 gdb，x86 自带的 gdb 跑不了 vmlinux。

3. 运行效果

   ```plain
   guanshun@Jack-ubuntu ~/r/b/linux-4.19-loongson (linux-4.19.167-next)> loongarch64-linux-gnu-gdb vmlinux
   GNU gdb (GDB) 8.1.50.20190122-git
   Copyright (C) 2018 Free Software Foundation, Inc.
   License GPLv3+: GNU GPL version 3 or later <http://gnu.org/licenses/gpl.html>
   This is free software: you are free to change and redistribute it.
   There is NO WARRANTY, to the extent permitted by law.
   Type "show copying" and "show warranty" for details.
   This GDB was configured as "--host=x86_64-pc-linux-gnu --target=loongarch64-linux-gnu".
   Type "show configuration" for configuration details.
   For bug reporting instructions, please see:
   <http://www.gnu.org/software/gdb/bugs/>.
   Find the GDB manual and other documentation resources online at:
       <http://www.gnu.org/software/gdb/documentation/>.

   For help, type "help".
   Type "apropos word" to search for commands related to "word"...
   Reading symbols from vmlinux...done.
   (gdb) target remote:1234
   Remote debugging using :1234
   0x000000001c000000 in ?? ()
   (gdb) b kernel_entry
   Breakpoint 1 at 0x90000000010086d0: file arch/loongarch/kernel/head.S, line 42.
   (gdb) c
   Continuing.

   Breakpoint 1, kernel_entry () at arch/loongarch/kernel/head.S:42
   42              la      t0, 0f
   ```

   以上的源码都可以找我要。

### 四、在 LA 上用 QEMU + KVM 调试 LA 内核

1. 运行参数

   ```plain
   sudo ./qemu-system-loongarch64 -nographic -m 2G -cpu Loongson-3A5000 -serial mon:stdio -bios /home/niugen/lgs/qemu-la/pc-bios/loongarch_bios.bin -enable-kvm -M loongson7a_v1.0,accel=kvm -drive file=/home/niugen/lgs/Loongnix-20.mini.loongarch64.rc1.b2.qcow2,if=virtio -kernel /home/niugen/lgs/vmlinux -append "console=ttyS0 root=/dev/vda1" -s -S
   ```

2. 结果

   ![img](https://user-images.githubusercontent.com/66514719/143871503-116ebc5e-b02f-4a22-916c-a6b3817fa38e.png)

### 五、更新 linux 内核

下载需要的内核，

```plain
make menuconfig
```

 配置 .config 文件，这里注意不要打开调试选项: kernel hacking -> Compile-time checkes and compiler options 中的 DEBUG_INFO 选项，不然会导致生成的内核太大以至于不能装入 /boot 目录下，而 /boot 目录扩容又比较麻烦。

之后就是正常的编译

```plain
make -jn
```

编译完成之后首先安装内核模块

```plain
sudo make modules_install
```

然后安装内核

```plain
sudo make intall
```

将内核安装好后还需要修改 `/boot/grub/grub.cfg` 文件，这里不用手动修改，而是通过 initramfs-tool 来做

```plain
sudo apt install initramfs-tools
```

下载后更新一下 grub 即可。

```plain
sudo updata_grub
```

### reference

[1] https://cloud.tencent.com/developer/article/1039421

[2] https://imkira.com/QEMU-GDB-Linux-Kernel/

[3] https://www.cnblogs.com/harrypotterjackson/p/11846222.html
