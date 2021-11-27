# linux经验

1. 启动微信： sudo dpkg -i deepin.com.wechat_2.6.8.65deepin0_i386.deb

2. 解决tim, 微信字体的问题：（1）从win10上将字体复制过来；（2）输入命令：sudo apt install 	fonts-wqy-microhe

3. 更换vultr的配置之后，用“ssh-keygen -R "66.42.99.19"来重新配置本地的密钥。

4. 连接服务器：ssh [root@66.42.99.19](mailto:root@66.42.99.19) ，密码：

5. 外网访问公司服务器：

   先在frp中运行frpc：./frpc &

   然后帐号：ssh -p 16901 [lgs@localhos](mailto:lgs@localhost)[t](mailto:lgs@localhost)  密码：

6. 尽量不要使用su进入root用户，可以设置sudo不要密码。

7. 从终端快速打开程序：

   先用：apt list | grep ‘qq’找到软件的包名， 然后使用dpkg -L xxx-qq(包名)查看软件安装的文件路径。之后用ln -s /xx /xx /xx /run.sh /usr/bin 建立软链接到bin目录，注意run.sh要绝对目录。之后就可以在终端用快捷键打开。

   快捷启动：

   wechat, tim, tbird, office, ray, code。

   ln p1 p2是建立硬链接，ln -s p1 p2是建立软链接。

8. 用ssh进行远程连接。将

   Host 3a5000

   ​    HostName 10.90.50.233

   ​    Port 22

   ​    User loongson

   写入.ssh/config中，可以直接使用ssh 3a5000进行连接。然后将id_rsa.pub写入远程主机的~/.ssh/authorized_keys中，这样可以不用输入密码登陆。

9. 遇到这个问题，diffie-hellman-xxxxxx

   ![](https://github.com/UtopianFuture/UtopianFuture.github.io/blob/master/image/experience.1.png?raw=true)


   需要在用户目录下的.ssh/config中添加

   ```
\# ~/.ssh/config
Host rd.loongson.cn
HostName 10.2.5.20
Port 29148
KexAlgorithms +diffie-hellman-group14-sha1
   ```

   配置，如果访问其他的设备有问题则重新配置，会提示配置什么算法。

10. ssh连接服务器掉线：

    将etc/ssh/sshd_config中的ClientAliveInterval 900, ClientAliveCountMax 1注释掉即可。

    ```
    rsync -avzP /home/guanshun/gitlab/latx guanshun:/home/guanshun/gitlab
    ```

11. ​	Linux中有三个标准流，在一个程序启动的时候它们会自动开启。它们分别是：

    ​	标准输入流（stdin: standard input）

    ​	标准输出流（stdout: standard output）

    ​	标准错误流（stderr: standard error

    ​	对应的文件描述符分别是：stdin：0, stdout：1,  stderr：2

12. 忘记 root 密码可以用 u 盘启动盘进入到系统，然后将原系统的系统盘挂载到一个临时挂载点，用 chroot 指令切换到该挂载点的 root 用户，这时就可以用 passwd 指令修改 root 密码。

13. QEMU 如果没有打开 SDL 支持就不会默认打开小黑框。SDL 支持需要安装如下几个库

    ```
    sudo apt-get install libvte-2.91-dev
    sudo apt-get install -y libsdl2-dev
    sudo apt-get install -y libsdl2-image-dev
    ```

    然后打开 SDL 支持

    ```
    ../configure --enable-kvm --enable-debug --enable-werror --target-list=x86_64-softmmu --enable-vte --enable-sdl
    ```

14. 发现一个有用的[网站](https://documentation.suse.com/zh-cn/sles/15-SP2/html/SLES-all/book-virt.html)

15. 运行 qemu 通过 ctrl alt 2 进入 monitor ，ctrl alt 1 进入 guest os ，可以通过 -monitor stdio 将 monitor 重定向到终端。
