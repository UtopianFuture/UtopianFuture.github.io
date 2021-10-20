## 在qemu上调试loongson内核

### 一、安装编译支持LoongArch版本的qemu

1. 从内核组网站下载源码

   ```
   git clone ssh://liuguanshun@rd.loongson.cn:29418/kernel/qemu && scp -p -P 29418 liuguanshun@rd.loongson.cn:hooks/commit-msg qemu/.git/hooks/
   ```

   进入到qemu目录，切换到`uos-dev-3.1.0`分支

   ```
   git checkout uos-dev-3.1.0
   ```

2. 运行configure

   ```
   ./configure --target-list=loongarch64-softmmu --enable-debug --enable-profiler --disable-rdma --disable-pvrdma --disable-libiscsi --disable-libnfs --disable-libpmem --disable-glusterfs --disable-opengl --disable-xen --disable-werror --disable-capstone --disable-kvm
   ```

   configure脚本用于生成Makefile，其选项可以用`./configure --help`查看。这里使用到的选项含义如下：

   ```
   --enable-kvm：编译KVM模块，使QEMU可以利用KVM来访问硬件提供的虚拟化服务。
   --enable-vnc：启用VNC。
   --enalbe-werror：编译时，将所有的警告当作错误处理。
   --target-list：选择目标机器的架构。默认是将所有的架构都编译，但为了更快的完成编译，指定需要的架构即可。mips是big-endian的mips架构，mipsel是little-endian的mips架构。
   ```

   配置kvm-qemu:

   ```
   ./configure --target-list=loongarch64-softmmu --enable-debug --enable-profiler --disable-rdma --disable-pvrdma --disable-libiscsi --disable-libnfs --disable-libpmem --disable-glusterfs --disable-opengl --disable-xen --disable-werror --disable-capstone --disable-spice --disable-libusb --disable-usb-redir --audio-drv-list='' --enable-kvm --enable-tcg-interpreter
   ```

   配置tcg-qemu：

   ```
   ./configure --target-list=loongarch64-softmmu --enable-debug --enable-profiler --disable-rdma --disable-pvrdma --disable-libiscsi --disable-libnfs --disable-libpmem --disable-glusterfs --disable-opengl --disable-xen --disable-werror --disable-capstone --disable-spice --disable-libusb --disable-usb-redir --audio-drv-list='' --disable-kvm
   ```

3. 编译

   ```
   make -j12
   ```

4. 如果configure或编译出错，在github上找到dtc仓库，clone到qemu的根目录中，重新编译。

   ```
   git clone git@github.com:dgibson/dtc.git
   ```

5. 编译好后应该出现这样的指示：

   ```
   LINK    loongarch64-softmmu/qemu-system-loongarch64
   ```


### 二、编译内核

1. 这里开始犯了错误，即在x86的机器上编译内核组的支持LoongArch架构的内核，这样肯定只能编译出x86的内核。因此要用交叉编译器来编译支持LoongArch架构的内核，或者直接在LA的机器上编译。下面的bug都是在x86上编译遇到的，不具代表性，看看就行。

   遇到的bug：

   -  `fatal error: openssl/opensslv.h: No such file or directory`

     原因：缺少openssl库。

     解决：安装OpenSSL development package:

     ```
     sudo apt-get install libssl-dev
     ```

   - `The frame size of 1032 bytes is larger than 1024 bytes`

     原因：内核中设置了堆栈报警大小，其默认为1024bytes。

     解决：将其修改为4096既可以消除告警信息。

     ```
     make menuconfig
     kernel hacking
     修改warn for stack frames larger than 的数值，将其修改为4096（最好不要大过这个数值）
     ```

   - `No rule to make target 'debian/canonical-certs.pem', needed by 'certs/x509_certificate_list'. Stop`

     解决：修改.config文件中的`CONFIG_SYSTEM_TRUSTED_KEYS`

     ```
     CONFIG_SYSTEM_TRUSTED_KEYS=""
     ```

   - `warning: Cannot use CONFIG_STACK_VALIDATION=y, please install libelf-dev, libelf-devel or elfutils-libelf-devel`

     ```
     sudo apt install libelf-dev
     ```

   - `fatal error: asm/dma-coherence.h: No such file or directory`

     原因：asm中没有`dma-coherence.h`头文件。

     解决：在网上找到`dma-coherence.h`头文件，发现不仅仅是`dma-coherence.h`没有，还有很多相关头文件没有，于是[下载](https://git.whoi.edu/edison/edison-linux/tree/64131a87f2aae2ed9e05d8227c5b009ca6c50d98/arch/mips/include)整个文件，将`arch/mips/include`中的asm和uapi分别`cp`到`/usr/include/x86_64-linux-gnu/asm`和`/usr/include/x86_64-linux-gnu`中，重新`make`。当然不是所有的头文件都有，遇到同样的问题还是在网上找头文件，然后手动写到`/usr/include/x86_64-linux-gnu`里。

   - `BTF: .tmp_vmlinux.btf: pahole (pahole) is not available`

     解决：

     ```
     sudo apt install dwarves
     ```

     `BTF: .tmp_vmlinux.btf: pahole version v1.15 is too old, need at least v1.16`

   ​		最新的安装包[在这](http://archive.ubuntu.com/ubuntu/pool/universe/d/dwarves-dfsg/dwarves_1.17-1_amd64.deb)。

2. 尝试了在x86上编译原版的内核。

   ```
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

   之后编译。编译需要很久，编译成功后会在arch/x86/boot中找到bzImage文件。

   ```
   make -j12
   ```

3. 之后又尝试了在LA上编译内核。

   ```
   make defconfig
   make menuconfig
   make -j12
   ```

   上述的menu选项还是要选的。编译好后在根目录下会有vmlinx, vmlinuz文件，vmlinux就是编译好的内核。

### 三、在qemu上调试内核

1. 运行参数

   ```
   tcg:
   ./qemu-system-loongarch64 -m 8192M -nographic -cpu Loongson-3A5000 -serial mon:stdio -bios /home/guanshun/gitlab/qemu-la/pc-bios/loongarch_bios.bin -M loongson7a,kernel_irqchip=off -drive file=/home/guanshun/research/bmbt/Loongnix-20.mini.loongarch64.rc1.b2.qcow2,if=virtio -kernel /home/guanshun/research/bmbt/linux-4.19-loongson/vmlinux -append "console=ttyS0 root=/dev/vda1"

   kvm:
   ./qemu-system-loongarch64 -m 8192M -nographic -cpu Loongson-3A5000 -serial mon:stdio -bios /home/guanshun/gitlab/qemu-la/pc-bios/loongarch_bios.bin -M --enable-kvm loongson7a,kernel_irqchip=off -drive file=/home/guanshun/research/bmbt/Loongnix-20.mini.loongarch64.rc1.b2.qcow2,if=virtio -kernel /home/guanshun/research/bmbt/linux-4.19-loongson/vmlinux -append "console=ttyS0 root=/dev/vda1"
   ```

   这里`Loongnix-20.mini.loongarch64.rc1.b2.qcow2`可以从[这里](http://pkg.loongnix.cn:8080/loongnix/isos/)下载，`/home/guanshun/gitlab/qemu-la/pc-bios/loongarch_bios.bin`是bios，需要换成自己的路径，后面的`-append "console=ttyS0 root=/dev/vda1"`是用于控制台输出的。

   按照这个参数运行就能正常启动内核，用户名：loongson，密码：Loongson20。

   要用gdb调试内核的话需要增加`-s -S`参数，即

   ```
   ./qemu-system-loongarch64 -m 8192M -nographic -cpu Loongson-3A5000 -serial mon:stdio -bios /home/guanshun/gitlab/qemu-la/pc-bios/loongarch_bios.bin -M loongson7a,kernel_irqchip=off -drive file=/home/guanshun/research/bmbt/Loongnix-20.mini.loongarch64.rc1.b2.qcow2,if=virtio -kernel /home/guanshun/research/bmbt/linux-4.19-loongson/vmlinux -append "console=ttyS0 root=/dev/vda1" -s -S
   ```

   ```
   -m 指定内存数量
   -kernel 指定 vmlinux 的镜像路径
   -s 等价于 -gdb tcp::1234 表示监听 1234 端口，用于 gdb 连接
   -S 表示加载后立即暂停，等待调试指令。不设置这个选项内核会直接执行
   -nographic 以及后续的指令用于将输出重新向到当前的终端中，这样就能方便的用滚屏查看内核的输出日志了。
   ```

   这个时候 qemu 会进入暂停状态，如果需要打开 qemu 控制台，可以输入 CTRL + A 然后按 C。

2. 在另一个终端中执行 gdb：

   ```
   loongarch64-linux-gnu-gdb vmlinux
   target remote:1234
   b kernel_entry
   c
   ```

   注意这里的`loongarch64-linux-gnu-gdb`是能在x86上运行的支持LA架构的gdb，x86自带的gdb跑不了vmlinux。

3. 运行效果

   ```
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

### reference

[1]https://cloud.tencent.com/developer/article/1039421

[2]https://imkira.com/QEMU-GDB-Linux-Kernel/
