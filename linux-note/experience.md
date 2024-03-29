# linux 经验

1. 启动微信： sudo dpkg -i deepin.com.wechat_2.6.8.65deepin0_i386.deb

2. 解决 tim, 微信字体的问题：（1）从 win10 上将字体复制过来；（2）输入命令：sudo apt install 	fonts-wqy-microhe

3. 更换 vultr 的配置之后，用“ssh-keygen -R "66.42.99.19"来重新配置本地的密钥。

4. 连接服务器：ssh [root@66.42.99.19](mailto:root@66.42.99.19) ，密码：

5. 外网访问公司服务器：

   先在 frp 中运行 frpc：./frpc &

   然后帐号：ssh -p 16901 [lgs@localhos](mailto:lgs@localhost)[t](mailto:lgs@localhost)  密码：

6. 尽量不要使用 su 进入 root 用户，可以设置 sudo 不要密码。

7. 从终端快速打开程序：

   先用：apt list | grep ‘qq’找到软件的包名， 然后使用 dpkg -L xxx-qq(包名)查看软件安装的文件路径。之后用 ln -s /xx /xx /xx /run.sh /usr/bin 建立软链接到 bin 目录，注意 run.sh 要绝对目录。之后就可以在终端用快捷键打开。

   快捷启动：

   wechat, tim, tbird, office, ray, code。

   ln p1 p2 是建立硬链接，ln -s p1 p2 是建立软链接。

8. 用 ssh 进行远程连接。将

   Host 3a5000

   ​    HostName 10.90.50.233

   ​    Port 22

   ​    User loongson

   写入.ssh/config 中，可以直接使用 ssh 3a5000 进行连接。然后将 id_rsa.pub 写入远程主机的~/.ssh/authorized_keys 中，这样可以不用输入密码登陆。

   直接使用 `ssh-copy-id username@remote-server` 命令更方便。

9. 设置开机自启动
   `sudo systemctl enable ssh`

9. 遇到这个问题，diffie-hellman-xxxxxx

   ![](https://github.com/UtopianFuture/UtopianFuture.github.io/blob/master/image/experience.1.png?raw=true)


   需要在用户目录下的.ssh/config 中添加


~/.ssh/config
Host rd.loongson.cn
HostName 10.2.5.20
Port 29148
KexAlgorithms +diffie-hellman-group14-sha1
   ```plain

   配置，如果访问其他的设备有问题则重新配置，会提示配置什么算法。

10. ssh 连接服务器掉线：

    将 etc/ssh/sshd_config 中的 ClientAliveInterval 900, ClientAliveCountMax 1 注释掉即可。

    ```plain
    rsync -avzP /home/guanshun/gitlab/latx guanshun:/home/guanshun/gitlab
   ```

11. ​	Linux 中有三个标准流，在一个程序启动的时候它们会自动开启。它们分别是：

    ​	标准输入流（stdin: standard input）

    ​	标准输出流（stdout: standard output）

    ​	标准错误流（stderr: standard error

    ​	对应的文件描述符分别是：stdin：0, stdout：1,  stderr：2

12. 忘记 root 密码可以用 u 盘启动盘进入到系统，然后将原系统的系统盘挂载到一个临时挂载点，用 chroot 指令切换到该挂载点的 root 用户，这时就可以用 passwd 指令修改 root 密码。

13. QEMU 如果没有打开 SDL 支持就不会默认打开小黑框。SDL 支持需要安装如下几个库

    ```plain
    sudo apt-get install libvte-2.91-dev
    sudo apt-get install -y libsdl2-dev
    sudo apt-get install -y libsdl2-image-dev
    ```

    然后打开 SDL 支持

    ```plain
    ../configure --enable-kvm --enable-debug --enable-werror --target-list=x86_64-softmmu --enable-vte --enable-sdl
    ```

14. 发现一个有用的[网站](https://documentation.suse.com/zh-cn/sles/15-SP2/html/SLES-all/book-virt.html)

15. 运行 qemu 通过 ctrl alt 2 进入 monitor ，ctrl alt 1 进入 guest os ，可以通过 -monitor stdio 将 monitor 重定向到终端。

16. md 文件中换行需要在行末输入 2 个空格。

17. 创建新用户后遇到

    ```plain
    user not in sudoers
    ```

    的问题，使用如下命令查看正常用户所在的 group，

    ```plain
    groups xxx
    ```

    然后使用

    ```plain
    sudo usermod -aG wheel <username>
    ```

    将当前用户一个个加到这些 group 中去即可。

18. 当 grub.cfg 出现问题导致内核不能正常启动时，按照如下步骤即可恢复：

    首先确定 `/` 目录所在的硬盘分区，使用 `ls`，会出现不同的硬盘号和分区，如 `hd0,msdos0` 等等，然后用 `ls hd0,msdos0` 命令检查所有的硬盘分区，如果某个分区是 linux ext2/ext3/ext4 等文件文件系统，那么就是 `/` 目录所有的分区。这里假设是 `hd0,msdos0`。

    然后使用 `linux (hd0,msdos0)/vm-xxx root=/dev/sda3` 命令，敲出 `vm` 后使用 `tab` 建补全，同时确定 initrd，`initrd (hd0,msdos0)/init` 同样使用 `tab` 补全。

    最后 `boot` 命令重启即可。

19. 使用 emulator 调试 android 时，需要用到 gdbserver，在 adb push 的时候遇到 /system ro 的问题时，需要在 emulator 启动时增加 -writable-system 命令。

20. aosp 是使用 soong 编译的，其支持直接生成 compile_commands.json 文件，不需要使用 bear 生成，需要使用如下指令生成，生成的 compile_commands.json 在 out/soong/development/ide/compdb 中。

    ```shell
    export SOONG_GEN_COMPDB=1
    export SOONG_GEN_COMPDB_DEBUG=1
    export SOONG_LINK_COMPDB_TO=$ANDROID_HOST_OUT
    ```

21. nokaslr 禁止内核起始地址随机化，这个很重要， 否则 GDB 调试可能有问题；console=ttyAMA0 指定了串口，没有这一步就看不到 linux 的输出；同时在 menuconfig 中要将所有 AMBA 相关的驱动使能。
