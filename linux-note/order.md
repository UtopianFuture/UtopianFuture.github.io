# linux命令

1. su root账户

2. su guanshun 返回普通账户

3. ls -al 该文件夹下的所有文件

4. tab 命令补全和文件补全  

5. ctrl + c 退出

6. --help 命令解释

7. man + 命令  命令详情

8. sudo apt-get install 下载

9. sudo apt-get --purge remove 软件卸载

10. rm -f 删除该文件夹中的指定文件

11. sudo wget 下载指定链接文件

12. 修改文件夹所属用户：chown -R guanshun ***

    修改文件夹所属用户组：chgrp -R guanshun ***

13. 查看当前终端的环境变量：env | grep 'proxy'

    设置当前终端的环境变量：export all_proxy=socks://127.0.0.1:1089/（等）

14. cat /proc/cpuinfo用于查看cpu信息，lscpu也能查看。

15. | 操作符允许我们将一个程序的输出和另外一个程序的输入连接起来。

16. mount可以查看磁盘的挂载情况，unmount + dir 可以解挂某个扇区，mount + dir可以挂载某个扇区。linux读取windows的文件系统，在windows重启进入linux时文件系统时只读的，而开机直接进入linux时文件系统是可写的。

17. qemu-img -V查看qemu版本

    ```
    guanshun@Jack-ubuntu /dev> qemu-img -V
    qemu-img version 5.1.0
    Copyright (c) 2003-2020 Fabrice Bellard and the QEMU Project developers
    ```

    按照qemu安装方法安装之后的版本

    ```
    guanshun@Jack-ubuntu ~/r/b/qemu-6.0.0> qemu-img -V
    qemu-img version 6.0.0
    Copyright (c) 2003-2021 Fabrice Bellard and the QEMU Project developers
    ```

18. 