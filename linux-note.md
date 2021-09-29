# Linux Note

## 一、linux命令

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

16. ​	mount可以查看磁盘的挂载情况，unmount + dir 可以解挂某个扇区，mount + dir可以挂载某个扇区。linux读取windows的文件系统，在windows重启进入linux时文件系统时只读的，而开机直接进入linux时文件系统是可写的。



## 二、linux经验

1. 启动微信： sudo dpkg -i deepin.com.wechat_2.6.8.65deepin0_i386.deb  

2. 解决tim, 微信字体的问题：（1）从win10上将字体复制过来；（2）输入命令：sudo apt install 	fonts-wqy-microhe

3. 更换vultr的配置之后，用“ssh-keygen -R "66.42.99.19"来重新配置本地的密钥。

4. 连接服务器：ssh [root@66.42.99.19](mailto:root@66.42.99.19) ，密码：[Z5v$S4z@gPPt8](mailto:Z5v$S4z@gPPt8)*z{

5. 外网访问公司服务器：

   先在frp中运行frpc：./frpc &

   然后帐号：ssh -p 16901 [lgs@localhos](mailto:lgs@localhost)[t](mailto:lgs@localhost)  密码：731124396liu

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

   ![img](/home/guanshun/Pictures/notes picture/image1.png)  


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

    ​	将etc/ssh/sshd_config中的ClientAliveInterval 900, ClientAliveCountMax 1注释掉即可。

    ​	rsync -avzP /home/guanshun/gitlab/latx guanshun:/home/guanshun/gitlab

11. ​	Linux中有三个标准流，在一个程序启动的时候它们会自动开启。它们分别是：

    ​	标准输入流（stdin: standard input）

    ​	标准输出流（stdout: standard output）

    ​	标准错误流（stderr: standard error

    ​	对应的文件描述符分别是：stdin：0, stdout：1,  stderr：2

## 三、快捷工具

1. SwitchyOmega结合Qv2ray能够解决科学上网问题。在SwitchyOmega里配置端口信息，会将所有的数据通过设置的端口传输。将端口信息设置成和Qv2ray一致即可。https://github.com/FelisCatus/SwitchyOmega

2. tldr可以用来快速查询工具的用法，会给出一些常用，简单的变量信息，如tldr git-config, tldr git-push等。

3. ocrmypdf 可以将图片形式的pdf转换成文字形式的pdf。

   ```
   ocrmypdf                      # it's a scriptable command line program
      -l eng+fra                 # it supports multiple languages
      --rotate-pages             # it can fix pages that are misrotated
      --deskew                   # it can deskew crooked PDFs!
      --title "My PDF"           # it can change output metadata
      --jobs 4                   # it uses multiple cores by default
      --output-type pdfa         # it produces PDF/A by default
      input_scanned.pdf          # takes PDF input (or images)
      output_searchable.pdf      # produces validated PDF output
   ```

4. qpdf可以remove the encryption

   ```
   qpdf --decrypt [--password=[password]] infilename outfilename
   ```

5. uname查看内核版本。

   ```
   - Print hardware-related information: machine and processor:
      uname -mp
    - Print software-related information: operating system, release number, and version:
      uname -srv
    - Print the nodename (hostname) of the system:
      uname -n
    - Print all available system information (hardware, software, nodename):
      uname -a
   ```

6. xclip可以复制标准输出到粘贴板，fish可以自动联想指令。这两个工具的使用还需要进一步探索。

7. stat 查看文件的状态

   ```
   Display file and filesystem information.
    - Show file properties such as size, permissions, creation and access dates among others:
      stat {{file}}
    - Same as above but in a more concise way:
      stat -t {{file}}
    - Show filesystem information:
      stat -f {{file}}
    - Show only octal file permissions:
      stat -c "%a %n" {{file}}
    - Show owner and group of the file:
      stat -c "%U %G" {{file}}
    - Show the size of the file in bytes:
      stat -c "%s %n" {{file}}
   ```

8. tar 文件解压

   ```
   - [c]reate an archive and write it to a [f]ile:
      tar cf {{target.tar}} {{file1}} {{file2}} {{file3}}
    - [c]reate a g[z]ipped archive and write it to a [f]ile:
      tar czf {{target.tar.gz}} {{file1}} {{file2}} {{file3}}
    - [c]reate a g[z]ipped archive from a directory using relative paths:
      tar czf {{target.tar.gz}} --directory={{path/to/directory}} .
    - E[x]tract a (compressed) archive [f]ile into the current directory [v]erbosely:
      tar xvf {{source.tar[.gz|.bz2|.xz]}}
    - E[x]tract a (compressed) archive [f]ile into the target directory:
      tar xf {{source.tar[.gz|.bz2|.xz]}} --directory={{directory}}
    - [c]reate a compressed archive and write it to a [f]ile, using [a]rchive suffix to determine the compression program:
      tar caf {{target.tar.xz}} {{file1}} {{file2}} {{file3}}
    - Lis[t] the contents of a tar [f]ile [v]erbosely:
      tar tvf {{source.tar}}
    - E[x]tract files matching a pattern from an archive [f]ile:
      tar xf {{source.tar}} --wildcards "{{*.html}}"
   ```

9. tmux工具可以用来分屏，常用的快捷键：

   Ctrl+a %：划分左右两个窗格；

   Ctrl+a "：划分上下两个窗格；

   Ctrl+a <arrow key>：光标切换到其他窗格。<arrow key>是指向要切换到的窗格的方向键，比如切换到下方窗格，就按方向键↓。

   Ctrl+a x：关闭当前窗格。

   Ctrl+a Ctrl+<arrow key>：按箭头方向调整窗格大小。

   Ctrl+a z：当前窗格全屏显示，再使用一次会变回原来大小

   Ctrl+a c：创建一个新窗口，状态栏会显示多个窗口的信息。

   tmux的配置主要是修改~/.tmux/tmux.conf来完成的，修改完后用tmux source ~/.tmux/tmux.conf来更新配置。

   按下PREFIX+[快捷键进入复制模式，在复制模式下按下q字符退出复制模式。

   复制模式类似于Vim的普通模式，键盘操作风格也类似，在复制模式下，按下v字符，进行待复制内容的选取，类似于进入Vim的可视模式，键盘操作风格也类似。

   在复制模式下crtl + s进入查找模式，同样按q退出。

10. Termux arm中使用终端。

11. 输入输出重定向是

    ![img](/home/guanshun/Pictures/notes picture/image2.png)
     通过“>” “<”完成的。

12. history程序可以查询所有用过的指令。

13. split可以将文件分为大小不同的块。如split -l 1000000 latx_log -d -a 4 latx_log_，-l表示每个文件多少行，也可以按比特分；-d表示后缀用数字表示，-a 4表示后缀是4位数字。

14. rsync可以用来在不同机器上同步文件，注意host和target文件名最后不加斜杠表示同步整个文件夹到目的文件夹中。

15. vim   

    ​	v         字符选

    ​	shift v 行选

    ​	ctrl v   块选

    ​	块选之后’d’批量删除，’I’ + ESC批量添加。

    ​	visual模式下：

    ​	d   ------ 剪切操作

    ​	y   -------复制操作

    ​	p   -------粘贴操作

    ​	^  --------选中当前行，光标位置到行首（或者使用键盘的HOME键）

    ​	$  --------选中当前行，光标位置到行尾（或者使用键盘的END键）

16. pkill：杀进程，-9强制杀死

17. diff和vimdiff可以以行为单位对比两个文件的不同。

18. lsblk - list block devices

19. mmap, munmap - map or unmap files or devices into memory

20. cat file1 file2 file3… > file将多个文件合并成同一个文件。

21. truncate可以设置文件的大小，如truncate --size 50M qemu_log，超过50M的就会截断。

22. sed可以删除文件的任意行，sed ‘n, md’ filename；或者删除含有某个字符的行，将结果输出到指定文件，sed ‘/xxx/d’  filename1 > filename2,，删除不包 含某一字符的行，sed ‘/xxx/!d’  filename1 > filename2。

23. du查看文件大小。

24. df查看磁盘使用情况。

25. wc -l 查看文件行数。

26. Loongarch下的gdb不能查看浮点寄存器，将

    ​	gdb/features/loongarch/lbt64.c

    ​	gdb/features/loongarch/lbt32.c

    ​	gdb/features/loongarch/lasx.c

    ​	gdb/features/loongarch/lsx.c

    ​	中的lasx等变量修改成NULL，然后重新编译，记得修改环境变量。

27. gdb显示数据的不同格式：

    ​	x  按十六进制格式显示变量。

    ​	d  按十进制格式显示变量。

    ​	u  按十六进制格式显示无符号整型。

    ​	o  按八进制格式显示变量。

    ​	t  按二进制格式显示变量。

    ​	a  按十六进制格式显示变量。

    ​	c  按字符格式显示变量。

    ​	f  按浮点数格式显示变量。

28. gdb使用examine命令（简写是x）来查看内存地址中的值。x命令的语法如下所示：

    ​	x/<n/f/u> <addr>  n、f、u是可选的参数。

    ​	n 是一个正整数，表示显示内存的长度，也就是说从当前地址向后显示几个地址的内容。

    ​	f 表示显示的格式，参见44。

    ​	u表示显示的单位，b表示单字节，h表示双字节，w表示四字节，g表示八字节。当我们指定了字节长度后，GDB会从指内存定的内存地址开始，读写指定字节，并把其当作一个值取出来。

    ​	<addr>表示一个内存地址。

    ​	n/f/u三个参数可以一起使用。例如：

    ​	命令：x/8xb 0x54320 表示，从内存地址0x54320读取内容，b表示以字节为一个单位，8表示八个单位，x表示按十六进制显示。

29. sensor能够查看硬件温度，psensor能够gui显示。

30. vscode 中alt + 方向键能交换相邻行。

## 四、git使用

1. gitlab中的latx “origin”是rd.loongson.cn，gitlab2才是binarytranslation，所以在和公司的库同步时用的是git pull origin master, 与组里的gitlab同步用的是git pull gitlab2 master/(其他branch)

2. 将其他仓库的代码直接上传到自己的仓库中：

   （1）cp ***/. .

   （2）git ckeckout -b branch

   （3）git remote add {{remote_name}} {{remote_url}}

   （4）git merge master //将其merge到目标branch

   （5）之后更新本地的仓库都是用的remote_name，即git push remote_name ***(branch)

   git remote add {{remote_name}} {{remote_url}}也可以将某个仓库命名为助记符。

3. git remote set-url origin git@github.com:UtopianFuture/xv6-labs.git，将本地仓库的origin直接修改成自己的仓库路径，之后push, pull就会对自己的仓库进行操作。

   git push origin main:master，会将main分支的内容上传到master分支，可用于多分支的情况。

4. 遇到 fatal: not a git repository (or any parent up to mount point /)

   是因为没有在git文件夹中操作，即没有进行git init。

5. 记录git密码，从而不要每次输入密码：

   在需要的仓库中先输入：

   git config credential.helper store //将密码存储到本地

   git pull

   之后再输入用户名密码就能保存到本地的~/.git-credentials中，在这里应该可以直接添加或修改用户名和密码。

6. git 最重要的就是使用man

   ​	(1) -rebase：修改多个提交或旧提交的信息。http://jartto.wang/2018/12/11/git-rebase/

   ​	使用 git rebase -i HEAD~n 命令在默认文本编辑器中显示最近 n 个提交的列表。

   ​	

   1. *# 	Displays a list of the last 3 commits on the current branch*

   2. *$ 	git rebase -i HEAD~3*

   3. 

   ​	 (2) -reset：man git-reset

   ​	 (3) -checkout：man git-commit, git checkout . 删除所有本地修改。

   ​	 (4) --amend：git commit –amend，重写head的commit消息。

   ​	 (5) format-patch：If you want to format only <commit> itself, you can do this with  

   ​	 git format-patch -1 <commit>.

   ​	 (6) stash：The command saves your local modifications away and reverts the working directory to match the HEAD commit. 就是去掉unstaged modify。

   ​	 (7) restore：Restore specified paths in the working tree with some contents from a restore source.

   ​	 (8) log：·

   ​	 (9) reflog：

   ​	 (10) tig：查看提交的详细记录，包括代码的修改。

   ​	 (11) cherry-pick：对于多分支的代码库，将代码从一个分支转移到另一个分支有两种情况。一种情况是，合并另一个分支的所有代码变动，那么就采用合并（git merge）。另一种情况是，只需要合并部分代码变动（**某几个提交**），这时可以采用 Cherry pick。

   ​	rebase也可以合并冲突。在gerrit中，多人同时开发同一个分支，合并不同的代码修改就用git pull –rebase。

7. git add 可以加某个目录名从而添加所有该目录下的文件。

8. 在master-mirror分支上进行开发，如果master分支上有新的commit，要将该commit应用到master-mirror上。

   首先git checkout master-mirror，然后git rebase master，解决冲突后git rebase --continue，然后重新git add, commit。

   用git merge master同样能合并commit，但是这个merge会产生一个新的commit，如果merge频繁，会使得log丑陋。

9. 本地分支重命名(还没有推送到远程)

   ​	git branch -m oldName newName

   ​	删除远程分支

   ​	git push --delete origin oldName

   ​	上传新命名的本地分支

   ​	git push origin newName

   ​	把修改后的本地分支与远程分支关联

   ​	git branch --set-upstream-to origin/newName

10. git需要丢弃本地修改，同步到远程仓库（即遇到error: You have not concluded your merge (MERGE_HEAD exists).错误时）：

   ​	git fetch –all

   ​	git reset –hard origin/master

   ​	git fetch

   ​	之后再正常的pull就行。

11. git在push是遇到问题

    *squash commits first*

    ​	这是因为两个commit的Change-ID相同，先用git-rebase -i HEAD~~查看commit的情况，然后选择一个commit进行squash即可。

12. fit reset commitid只是改变git的指针，如果要将内容也切换过去，要用git reset –hard commitid或者是git checkout -f commitid, 不过这个命令会将本地的修改丢弃，有风险。

13. ferge conflict在code里修改之后要git add, git commit，这样conflict才算解决了。

14. fit status看到的未提交的文件可以用git checkout + filename删掉，如果有需要保存的文件，但是库里同步过来又会覆盖的，用git stash保存，然后在pull –rebase，之后再git stash apply恢复。

15. git diff               工作区 vs 暂存区

    ​	git diff head      工作区 vs 版本库

    ​	git diff –cached 暂存区 vs 版本库

16. 在上传修改前一定记得要git pull，和最新的版本库合并，之后再git add, commit, push，不然太麻烦了。

17. 如果忘了pull，则git reset恢复到未push的commit的前一个commit，然后pull，解决冲突后再git stash apply，将所有的修改恢复回来，解决冲突，然后再进行git push。可以用git stash list查看所有的stash。

18. 之后开发要checkout一条新的分支，所有的修改都在这条新的Fix分支上进行，修改完后在push到主分支，然后删掉新分支。问题：如果fix分支落后主分支，可以直接push么。

19. 提交代码，push到GitHub上，突然出现这个问题。

    ​	remote: Support for password authentication was removed on August 13, 2021. Please use a personal access token instead.

    ​	remote: Please see https://github.blog/2020-12-15-token-authentication-requirements-for-git-operations/ for more information.

    ​	fatal: unable to access 'https://github.com/zhoulujun/algorithm.git/': The requested URL returned error: 403

    ​	原因：从8月13日起不在支持密码验证，改用personal access token 替代。

    ​	方法：用ssh clone，设置密钥，将id_rsa.pub粘贴到githab上即可。



## 五、debug相关

1. 用GDB调试xv6：

   （1）在文件夹中使用：qemu gdb命令

   （2）另开一个终端，使用：gdb kernel命令，在gdb中使用target remote localhost:26000

2. 由于LA上的GDB用不了layout src看源码，所以用list命令。

   list快捷键是l，可以接函数名，列出当前函数源码，每次10行。

3. Gdbserver可以远程调试程序。首先在远程机器上运行：gdbserver :1234(port) /xxx(elf)，然后在本地用target remote ip:port连接，gdbgui是gdb的图形化界面工具，直接使用gdbgui指令就可以使用，然后选择需要调试的进程，或者远程调试的端口。https://www.gdbgui.com/gettingstarted/

4. gdb在3a5000不能用layout：

   ​	安装ncurses-base包，之后用./configure --enable-tui –disable-werror配置gdb，编译，make install安装。

5. 这个命令用来配置qemu，用qemu跑同样的程序，输出和latx对比，看有哪些不同。

   ​	../configure --target-list=x86_64-linux-user --enable-debug --enable-tcg-interpreter –disable-werror

   ​	这个命令用来在本地操作远程的机器执行程序，“2>”代表重定向到标准输出，即>重定向指定的目录。

   ​	ssh guanshun /home/guanshun/gitlab/qemu/build/qemu-x86_64 -d cpu -singlestep /home/guanshun/GDB/hello 2> /home/guanshun/research/loongson/qemu_log

6. ​	将需要对比的文件加到vscode中，然后select for compare，就可以对比两个文件的不同。如果文件过大还是用diff和vimdiff。

7. ​	终极的调试手段就是将目标程序与标准程序对比，将两个程序的执行环境设置为一样，然后printf出程序的reg信息，一个个对比。这其中要设置环境变量一样，两个程序的执行命令一样，还要知道reg信息怎么打印。

8. ​	latx-x64的bug：

   ​	(1) 寄存器高位清0；

   ​	(2) 无效指令，或是latx没有实现，或是capstone没有实现；

9. ​	tui reg group（gdb 打印寄存器参数）

   ​	next: Repeatedly selecting this group will cause the display to cycle through all of the available register groups.

   ​	prev: Repeatedly selecting this group will cause the display to cycle through all of the available register groups in the reverse order to next.

   ​	general: Display the general registers.

   ​	float: Display the floating point registers.

   ​	system: Display the system registers.

   ​	vector: Display the vector registers.

   ​	all: Display all registers.

10. ​	

## 六、编程相关

1. 静态全局变量有以下特点：

   ​	（1）该变量在全局数据区分配内存；

   ​	（2）未经初始化的静态全局变量会被程序自动初始化为0（自动变量的自动初始化值是随机的）；

   ​	（3）**静态全局变量在声明它的整个文件都是可见的，而在文件之外是不可见的**； 　

   ​	（4）静态变量都在全局数据区分配内存，包括后面将要提到的静态局部变量。对于一个完整的程序，在内存中的分布情况如下：代码区、全局数据区、堆区、栈区，一般程序的由new产生的动态数据存放在堆区，函数内部的自动变量存放在栈区，静态数据（即使是函数内部的静态局部变量）存放在全局数据区。**自动变量一般会随着函数的退出而释放空间**，而全局数据区的数据并不会因为函数的退出而释放空间。

2. 静态函数

   ​	（1）只能在声明它的文件当中可见，不能被其它文件所用；

   ​	（2）其它文件中可以定义相同名字的函数，不会发生冲突；

## 七、组会记录

8.20

（1）hamt的stream提升微小。

（2）stlb，mtlb? 16K, 4K页什么意思？页表？会有哪些影响。ioctl接口？

（3）memory module

（4）hamt

8.24

（1）dune：让普通用户态进程能够直接访问特权级资源。让qemu以镜像的方式运行在guest模式下，这样就可以访问tlb之类的资源。这样guest下没有os支持qemu进程，所以当qemu需要syscall时就会切换会root模式，由运行在root模式的qemu进行处理。

（2）

问题：

（1）user, kernel转换到底是怎么进行的。

（2）除了改bug，更重要的是理解代码的结构，设计思路，这点对以后的学习有帮助。同样思考一下，如果我要给别人做一个latx-x64的报告，我要怎么做？

8.27

（1）插桩工具pin。

（2）memory region。https://martins3.github.io/qemu-memory.html

9.3

(1) qemu的中断只发一次，不会像物理时钟那样重复的发送时中中断。

(2)

9.10

(1) qemu的中断，信号机制。

(2) qemu的线程，事件循环和锁。



## 八、杂项

### page coloring

​	系统在为进程分配内存空间时，是按照virtual memory分配的，然后virtual memory会映射到不同的physical memory，这样会导致一个问题，相邻的virtual memory会映射到不相邻的physical memory中，然后在缓存到cache中时，相邻的virtual memory会映射到同一个cache line，从而导致局部性降低，性能损失。

​	Page coloring则是将physical memory分为不同的color，不同color的physical memory缓存到不同的cache line，然后映射到virtual memory时，virtual memory也有不同的color。这样就不存在相邻的virtual memory缓存到cache时在同一个cache line。（很有意思，现在没能完全理解。和组相联映射有什么关系？4k页和16k页和这个又有什么关系？）

![img](/home/guanshun/Pictures/notes picture/image3.png)



### 程序优化技术

优化程序性能的基本策略：

（1）高级设计。为遇到的问题选择合适的算法和数据结构。

（2）基本编码原则。避免限制优化的因素，这样就能产生高效的代码。

消除连续的函数调用。在可能时，将计算移到循环外。考虑有选择地妥协程序的模块性以获得更大的效率。

消除不必要的内存引用。引入临时变量来保存中间结果。只有在最后的值计算出来时，才将结果存放到数组或全局变量中。

（3）低级优化。结构化代码以利用硬件功能。

展开循环，降低开销，并且使得进一步的优化成为可能。

通过使用多个累积变量和重新结合等技术，找到方法提高指令级并行。

用功能性的风格重新些条件操作，使得编译采用条件数据传送而不是条件跳转。

最后，由于优化之后的代码结构发生变化，每次优化都需要对代码进行测试，以此保证这个过程没有引入新的错误。

（4）程序剖析工具：

gcc -Og -pg xxx.c -o xxx

./xxx file.txt

​	gprof xxx

### 代理

​       代理（Proxy）是一种特殊的网络服务，允许一个网络终端（一般为客户端）通过这个服务与另一个网络终端（一般为服务器）进行非直接的连接。一般认为代理服务有利于保障网络终端的隐私或安全，防止攻击。

​       代理服务器提供代理服务。一个完整的代理请求过程为：客户端首先与代理服务器创建连接，接着根据代理服务器所使用的代理协议，请求对目标服务器创建连接、或者获得目标服务器的指定资源。在后一种情况中，代理服务器可能对目标服务器的资源下载至本地缓存，如果客户端所要获取的资源在代理服务器的缓存之中，则代理服务器并不会向目标服务器发送请求，而是直接传回已缓存的资源。一些代理协议允许代理服务器改变客户端的原始请求、目标服务器的原始响应，以满足代理协议的需要。代理服务器的选项和设置在计算机程序中，通常包括一个“防火墙”，允许用户输入代理地址，它会遮盖他们的网络活动，可以允许绕过互联网过滤实现网络访问。

​       代理服务器的基本行为就是接收客户端发送的请求后转发给其他服务器。代理不改变请求URI，并不会直接发送给前方持有资源的目标服务器。

​       HTTP/HTTPS/SOCKS 代理指的是客户端连接代理服务器的协议，指客户端和代理服务器之间交互的协议。

### Socks

​       SOCKS是一种网络传输协议，主要用于客户端与外网服务器之间通讯的中间传递。SOCKS工作在比HTTP代理更低的层次：SOCKS使用握手协议来通知代理软件其客户端试图进行的SOCKS连接，然后尽可能透明地进行操作，而常规代理可能会解释和重写报头（例如，使用另一种底层协议，例如FTP；然而，HTTP代理只是将HTTP请求转发到所需的HTTP服务器）。虽然HTTP代理有不同的使用模式，HTTP CONNECT方法允许转发TCP连接；然而，SOCKS代理还可以转发UDP流量（仅SOCKS5），而HTTP代理不能。

### fpu, xmm, mmx之间的关系和区别

​		(1) x87 fpu 浮点部分，有8个浮点寄存器，st(i)，top指针指向当前使用的寄存器。

​               	           ![img](/home/guanshun/Pictures/notes picture/image4.png)   

![img](/home/guanshun/Pictures/notes picture/image5.png)

(2) xmm 支持simd指令的技术。8个xmm寄存器，和fpu一样的组织形式。

​	The MMX state is aliased to the x87 FPU state. No new states or modes have been added to IA-32 architecture to support the MMX technology. The same floating-point instructions that save and restore the x87 FPU state also handle the MMX state.

![img](/home/guanshun/Pictures/notes picture/image6.png)

![img](/home/guanshun/Pictures/notes picture/image7.png)

(3) sse extensions(The streaming SIMD extensions)

​	和mmx一样，也是simd扩展，但是sse是128-bit扩展。在32-bit模式下有8个128-bit寄存器，也称作xmm寄存器，在64-bit模式下有16个128-bit寄存器。

32-bit的mxcsr寄存器为xmm寄存器提供控制和状态位。

![img](/home/guanshun/Pictures/notes picture/image8.png)

![img](/home/guanshun/Pictures/notes picture/image9.png)



### mmap

`mmap`, `munmap` - map or unmap files or devices into memory

```
#include <sys/mman.h>

void *mmap(void *addr, size_t length, int prot, int flags,
           int fd, off_t offset);
int munmap(void *addr, size_t length);
```

`mmap()`  creates  a  new  mapping in the virtual address space of the calling process for a device, file and etc, so process can operate the device use va, rather than use read and write.  The starting address for the new mapping is specified in **addr**.  The **length** argument specifies the length of the mapping (which must be greater than 0).

If addr is **NULL**, then the kernel chooses **the (page-aligned) address** at which to create the mapping.  If addr is not NULL, then the  kernel takes  it  as  a hint about where to place the mapping;

The  contents  of a file mapping are initialized using **length** bytes starting at offset **offset** in the file (or other object) referred to by the file descriptor **fd**.

The **prot** argument describes the desired memory protection of the mapping.

The **flags** argument determines whether updates to the mapping are visible to other processes mapping the same region, and whether updates are carried through to the underlying file.

The `munmap()` system call deletes the mappings for the specified address range, and causes further references to addresses within the range to generate invalid memory references. If the process has modified the memory and has it mapped `MAP_SHARED`, the modifications should first be written to the file.

