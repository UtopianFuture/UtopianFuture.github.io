# 快捷工具

1. SwitchyOmega 结合 Qv2ray 能够解决科学上网问题。在 SwitchyOmega 里配置端口信息，会将所有的数据通过设置的端口传输。将端口信息设置成和 Qv2ray 一致即可。https://github.com/FelisCatus/SwitchyOmega

2. tldr 可以用来快速查询工具的用法，会给出一些常用，简单的变量信息，如 tldr git-config, tldr git-push 等。

3. ocrmypdf 可以将图片形式的 pdf 转换成文字形式的 pdf。

   ```plain
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

4. qpdf 可以 remove the encryption

   ```plain
   qpdf --decrypt [--password=[password]] infilename outfilename
   ```

5. uname 查看内核版本。

   ```plain
   - Print hardware-related information: machine and processor:
      uname -mp
    - Print software-related information: operating system, release number, and version:
      uname -srv
    - Print the nodename (hostname) of the system:
      uname -n
    - Print all available system information (hardware, software, nodename):
      uname -a
   ```

6. xclip 可以复制标准输出到粘贴板，具体使用待探索。

7. fish 能自动联想指令，安装过程很简单，sudo apt install fish，之后直接`fish`即可使用。亦可将其作为默认 shell。

8. stat 查看文件的状态

   ```plain
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

9. tar 文件解压，最常用的参数是`-xvf`

   ```plain
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

10. tmux 工具可以用来分屏，常用的快捷键：

```plain
Ctrl+a %					划分左右两个窗格；
Ctrl+a "					划分上下两个窗格；
Ctrl+a <arrow key>			光标切换到其他窗格。<arrow key>是指向要切换到的窗格的方向键，比如切换到下方窗格，就按方向键↓。
Ctrl+a x					关闭当前窗格。
Ctrl+a Ctrl+<arrow key>		按箭头方向调整窗格大小。
Ctrl+a z					当前窗格全屏显示，再使用一次会变回原来大小
Ctrl+a c					创建一个新窗口，状态栏会显示多个窗口的信息。
tmux swap-window -s 3 -t 1  交换 3 号和 1 号窗口
tmux swap-window -t 1       交换当前和 1 号窗口
tmux move-window -t 1       移动当前窗口到 1 号
tmux attach -t 0			重新接入某个已存在的会话
tmux kill-session -t 0		杀死某个会话
tmux switch -t 0			切换会话
Ctrl+q d					分离当前会话
Ctrl+q s					列出所有会话
```

tmux 的配置主要是修改`~/.tmux/tmux.conf`来完成的，修改完后用`tmux source ~/.tmux/tmux.conf`来更新配置。可以将 `.tmux.conf` 移到 `~/` 目录下，tmux 就会将其作为默认配置文件，下次开机使用 tmux 会默认读取这个配置文件。

按下 PREFIX+[快捷键进入复制模式，在复制模式下按下 q 字符退出复制模式。

复制模式类似于 Vim 的普通模式，键盘操作风格也类似，在复制模式下，按下 v 字符，进行待复制内容的选取，类似于进入 Vim 的可视模式，键盘操作风格也类似。

在复制模式下 crtl + s 进入查找模式，同样按 q 退出。

11. termux: arm 中使用的终端。

12. 输入输出重定向是

    ```plain
    missing:~$ echo hello > hello.txt
    missing:~$ cat hello.txt
    hello
    missing:~$ cat < hello.txt
    hello
    missing:~$ cat < hello.txt > hello2.txt
    missing:~$ cat hello2.txt
    hello
     通过“>” “<”完成的。
    ```

13. history 程序可以查询所有用过的指令。

14. split 可以将文件分为大小不同的块。如 split -l 1000000 latx_log -d -a 4 latx_log_，-l 表示每个文件多少行，也可以按比特分；-d 表示后缀用数字表示，-a 4 表示后缀是 4 位数字。

15. rsync 可以用来在不同机器上同步文件，注意 host 和 target 文件名最后不加斜杠表示同步整个文件夹到目的文件夹中。

    ```plain
    rsync -avzP /home/guanshun/gitlab/latx/target guanshun:/home/guanshun/gitlab/latx-x64
    ```

16. vim

    ​	v         字符选

    ​	shift v 行选

    ​	ctrl v   块选

    ​	块选之后’d’批量删除，’I’ + ESC 批量添加。

    ​	visual 模式下：

    ​	d   ------ 剪切操作

    ​	y   -------复制操作

    ​	p   -------粘贴操作

    ​	^  --------选中当前行，光标位置到行首（或者使用键盘的 HOME 键）

    ​	$  --------选中当前行，光标位置到行尾（或者使用键盘的 END 键）

17. pkill：杀进程，-9 强制杀死

    ```plain
     - Kill all processes which match:
       pkill -9 "{{process_name}}"

     - Kill all processes which match their full command instead of just the process name:
       pkill -9 --full "{{command_name}}"
    ```

18. diff 和 vimdiff 可以以行为单位对比两个文件的不同。

19. lsblk - list block devices

20. mmap, munmap - map or unmap files or devices into memory

21. cat file1 file2 file3…… > file 将多个文件合并成同一个文件。

22. truncate 可以设置文件的大小，如`truncate --size 50M qemu_log`，超过 50M 的就会截断。

23. sed 可以删除文件的任意行，`sed ‘n, md’ filename`；

    或者删除含有某个字符的行，将结果输出到指定文件，`sed ‘/xxx/d’  filename1 > filename2`；

    删除不包含某一字符的行，`sed ‘/xxx/!d’  filename1 > filename2`；

    删除某一字符 `sed 's/a//g' filename1 > filename2` ；

24. sort - sort lines of text files

    ```plain
    - Sort a file in ascending order:
      sort {{path/to/file}}

    - Sort a file in descending order:
      sort --reverse {{path/to/file}}
    ```

    同时也能去除文件中相同的行 `sort -n headpath1.md | uniq > headpath2.md`

25. du 查看文件大小

    ```plain
    - List the sizes of a directory and any subdirectories, in the given unit (B/KB/MB):
       du -{{b|k|m}} {{path/to/directory}}

     - List the sizes of a directory and any subdirectories, in human-readable form (i.e. auto-selecting the appropriate unit for each size):
       du -h {{path/to/directory}}

     - Show the size of a single directory, in human readable units:
       du -sh {{path/to/directory}}

     - List the human-readable sizes of a directory and of all the files and directories within it:
       du -ah {{path/to/directory}}
    ```

26. df 查看磁盘使用情况。

    ```plain
    - Display all filesystems and their disk usage:
       df

     - Display all filesystems and their disk usage in human readable form:
       df -h

     - Display the filesystem and its disk usage containing the given file or directory:
       df {{path/to/file_or_directory}}

     - Display statistics on the number of free inodes:
       df -i

     - Display filesystems but exclude the specified types:
       df -x {{squashfs}} -x {{tmpfs}}
    ```

27. wc -l 查看文件行数。

28. Loongarch 下的 gdb 不能查看浮点寄存器，将

    ```plain
    gdb/features/loongarch/lbt64.c
    gdb/features/loongarch/lbt32.c
    gdb/features/loongarch/lasx.c
    gdb/features/loongarch/lsx.c
    ```

    中的 lasx 等变量修改成 NULL，然后重新编译，记得修改环境变量。

29. pkg-config

    ```plain
    pkg-config - Return metainformation about installed libraries
    - Get the list of libraries and their dependencies:
       pkg-config --libs {{library1 library2 ...}}

     - Get the list of libraries, their dependencies, and proper cflags for gcc:
       pkg-config --cflags --libs {{library1 library2 ...}}
    ```

30. gdb 显示数据的不同格式：

    ​	x  按十六进制格式显示变量。

    ​	d  按十进制格式显示变量。

    ​	u  按十六进制格式显示无符号整型。

    ​	o  按八进制格式显示变量。

    ​	t  按二进制格式显示变量。

    ​	a  按十六进制格式显示变量。

    ​	c  按字符格式显示变量。

    ​	f  按浮点数格式显示变量。

31. gdb 使用 examine 命令（简写是 x）来查看内存地址中的值。x 命令的语法如下所示：

    ​	x/<n/f/u> <addr>  n、f、u 是可选的参数。

    ​	n 是一个正整数，表示显示内存的长度，也就是说从当前地址向后显示几个地址的内容。

    ​	f 表示显示的格式，参见 44。

    ​	u 表示显示的单位，b 表示单字节，h 表示双字节，w 表示四字节，g 表示八字节。当我们指定了字节长度后，GDB 会从指内存定的内存地址开始，读写指定字节，并把其当作一个值取出来。

    ​	<addr>表示一个内存地址。

    ​	n/f/u 三个参数可以一起使用。例如：

    ​	命令：x/8xb 0x54320 表示，从内存地址 0x54320 读取内容，b 表示以字节为一个单位，8 表示八个单位，x 表示按十六进制显示。

32. sensor 能够查看硬件温度，psensor 能够 gui 显示。

33. vscode 中 alt + 方向键能交换相邻行。

34. 根据时间删除文件和文件夹

    ```plain
    sudo find . -mtime +2 -name "*" -exec rm -rf {} \;
    ```

    . ：准备要进行清理的任意目录
    -mtime：标准语句写法
    ＋2：查找 2 天前的文件，这里用数字代表天数，＋30 表示查找 30 天前的文件
    "*.*"：希望查找的数据类型，"*.jpg"表示查找扩展名为 jpg 的所有文件，"*"表示查找所有文件
    -exec：固定写法
    rm -rf：强制删除文件，包括目录
     {} \; ：固定写法，一对大括号+空格+/+;

35. vim 中能够画代码树的插件——[DrawIt](http://www.drchip.org/astronaut/vim/index.html#DRAWIT)

    下载后用 vim 打开 DrawIt.vba.gz

    ```plain
    vim DrawIt.vba.gz
    ```

    然后使用 `:so %` 进行解压，最后 `:q` 退出 `vim`，`DrawIt` 就安装完成。

    进入 vim 后用`\di`，`\ds`即可进入，退出 DrawIt。

    ```plain
    \di to start DrawIt，
    \ds to stop  DrawIt.
    ```

    ```plain
    Supported Features
       <left>       move and draw left
       <right>      move and draw right, inserting lines/space as needed
       <up>         move and draw up, inserting lines/space as needed
       <down>       move and draw down, inserting lines/space as needed
       <s-left>     move left
       <s-right>    move right, inserting lines/space as needed
       <s-up>       move up, inserting lines/space as needed
       <s-down>     move down, inserting lines/space as needed
       <space>      toggle into and out of erase mode
       >            draw -> arrow
       <            draw <- arrow
       ^            draw ^  arrow
       v            draw v  arrow
       <pgdn>       replace with a \, move down and right, and insert a \
       <end>        replace with a /, move down and left,  and insert a /
       <pgup>       replace with a /, move up   and right, and insert a /
       <home>       replace with a \, move up   and left,  and insert a \
       \>           draw fat -> arrow
       \<           draw fat <- arrow
       \^           draw fat ^  arrow
       \v           draw fat v  arrow
       \a           draw arrow based on corners of visual-block
       \b           draw box using visual-block selected region
       \e           draw an ellipse inside visual-block
       \f           fill a figure with some character
       \h           create a canvas for \a \b \e \l
       \l           draw line based on corners of visual block
       \s           adds spaces to canvas
       <leftmouse>  select visual block
     <s-leftmouse>  drag and draw with current brush (register)
       \ra ... \rz  replace text with given brush/register
       \pa ...      like \ra ... \rz, except that blanks are considered
                    to be transparent
    ```

36. objcopy

    将目标文件的一部分或者全部内容拷贝到另外一个目标文件中，或者实现目标文件的格式转换。

    ```plain
    SYNOPSIS
           objcopy [-F bfdname|--target=bfdname]
                   [-I bfdname|--input-target=bfdname]
                   [-O bfdname|--output-target=bfdname]
                   [-B bfdarch|--binary-architecture=bfdarch]
                   ...
    ```

    例如：

    ```plain
    objcopy -O binary test.o test.bin
    ```

37. spacevim 快捷键
    ```plain
    space f t 		打开/关闭目录
    f3        		快捷键打开/关闭目录
    space f o 		打开当前文件所在目录
    >		  		放大文件树窗口宽度
    <		  		缩小文件树窗口宽度
    SPC f y	  		复制并显示当前文件的绝对路径
    SPC f Y	  		复制并显示当前文件的相对路径
    Ctrl-<Down>		切换至下方窗口
    Ctrl-<Up>		切换至上方窗口
    Ctrl-<Left>		切换至左边窗口
    Ctrl-<Right>	切换至右边窗口
    SPC 1～9			跳至窗口 1～9
    q				智能关闭当前窗口
    在目录窗口中
    N				新建文件
    .				切换显示隐藏文件
    yY				复制文件
    yy				复制文件路径
    P				粘贴文件
    i				显示文件夹历史
    sg				垂直分屏打开文件
    Ctrl+r			刷新文件树
    r				替换或者重命名
    ```

38. 利用 voidkiss/folaterm 可以实现将终端以 float window 的形式打开，映射的快捷键分别为:
    ```plain
    Ctrl n : 创建新的 terminal window
    Ctrl p : 切换到 prev 的 terminal window
    Fn5 : 显示/隐藏窗口
    ```


37. 利用 'telescope' 快速搜索 file，buffer，function 等

    ```plain
    , o             		 在当前文件中搜索该符号
    , s             		 在整个工程中搜索该符号
    , f + 文件名       搜索文件
    , b + buffer 名 搜索buffer
    ```

38. vim 替换

    ```plain
    :[addr]s/源字符串/目的字符串/[option]
    ```

    全局替换命令为：

    ```plain
    :%s/源字符串/目的字符串/g
    ```

    **（1）[addr] 表示检索范围，省略时表示当前行。**

    “1,20” ：表示从第 1 行到 20 行；

    “%” ：表示整个文件，同“1,$”；

    “. ,$” ：从当前行到文件尾；

    **（2）s : 表示替换操作**

    **（3）[option] : 表示操作类型**

    g 表示全局替换；

    c 表示进行确认；

    p 表示替代结果逐行显示（Ctrl + L 恢复屏幕）;

    省略 option 时仅对每行第一个匹配串进行替换;

    如果在源字符串和目的字符串中出现特殊字符，需要用”\”转义 如 \t 。

39. cloc - Count, or compute differences of, lines of source code and comments

    ```plain
    - Count all the lines of code in a directory:                                                             	cloc {{path/to/directory}}                                                                             - Count all the lines of code in a directory, displaying a progress bar during the counting process:
      cloc --progress=1 {{path/to/directory}}
    ```

40. locate 查找文件比 find 快，配合 grep 使用很方便

    ```plain
    Find filenames quickly.
     - Look for pattern in the database. Note: the database is recomputed periodically (usually weekly or daily):
       locate {{pattern}}
     - Look for a file by its exact filename (a pattern containing no globbing characters is interpreted as *pattern*):
       locate */{{filename}}
     - Recompute the database. You need to do it if you want to find recently added files:
       sudo updatedb
    ```

    ```shell
    locate cpu.h|grep gitlab/bmbt
    /home/guanshun/gitlab/bmbt/.ccls-cache/@home@guanshun@gitlab@bmbt/include@hw@core@cpu.h
    /home/guanshun/gitlab/bmbt/.ccls-cache/@home@guanshun@gitlab@bmbt/include@hw@core@cpu.h.blob
    /home/guanshun/gitlab/bmbt/.ccls-cache/@home@guanshun@gitlab@bmbt/src@i386@cpu.h
    /home/guanshun/gitlab/bmbt/.ccls-cache/@home@guanshun@gitlab@bmbt/src@i386@cpu.h.blob
    /home/guanshun/gitlab/bmbt/include/hw/core/cpu.h
    /home/guanshun/gitlab/bmbt/src/i386/cpu.h
    ```

41. alias 比 ln -s 更好用，例如：

    ```shell
    alias gc 'git commit'
    alias gs 'git status'
    alias lg lazygit
    ```

    之后就可以直接使用 gc, gs, lg 等等。

    ```shell
     - List all aliases:
       alias
     - Create a generic alias:
       alias {{word}}="{{command}}"
     - View the command associated to a given alias:
       alias {{word}}
     - Remove an aliased command:
       unalias {{word}}
    ```

    不过 alias 的生存期只是当前终端，终端关了就失效。

42. ps 指令可以查看当前进程 `ps -ejH | grep frp`

    ```shell
    Information about running processes.
     - List all running processes:
       ps aux
     - List all running processes including the full command string:
       ps auxww
     - Search for a process that matches a string:
       ps aux | grep {{string}}
     - List all processes of the current user in extra full format:
       ps --user $(id -u) -F
     - List all processes of the current user as a tree:
       ps --user $(id -u) f
     - Get the parent pid of a process:
       ps -o ppid= -p {{pid}}
     - Sort processes by memory consumption:
       ps --sort size
    ```

43. [Rsyncing directories through ssh tunnel](https://superuser.com/questions/964244/rsyncing-directories-through-ssh-tunnel)

    通过 58 服务器穿透到公司内网的 la 机器，先开 frpc 代理，然后使用命令：

    ```shell
    ssh -f -N -L local_port_machineA:ip_machineC:remoteport_machineC user_machineB@ip_machineB
    ```

    具体到自己的机器就是

    ```shell
    ssh -L 16902:10.90.50.112:22 lx58
    ```

    然后就可以正常使用 rsync 同步文件

    ```shell
    rsync -avzP /home/guanshun/gitlab/bmbt ng5k:/home/niugen/lgs
    ```

44. 自动换行是每行超过 n 个字的时候 vim 自动加上换行符用

    set textwidth=70 来设置 n

    自动折行是把长的一行用多行显示 , 不在文件里加换行符用
    :set wrap 设置自动折行
    :set nowrap 设置不自动折行

45. xdg-open 能够自动使用对应的软件打开任意文件。

46. 在脚本中使用 alias，这样就不用每次打开一个新的终端就敲一遍 alias。

    ```shell
    # custom alias
    alias gc="git commit"
    alias gs="git status"
    alias gl="git log"
    alias gsh="git stash"
    alias ga="git add"
    alias gp="git push -origin"

    funcsave gc
    funcsave gs
    funcsave gl
    funcsave gsh
    funcsave ga
    funcsave gp
    ```

    将这段脚本放到 `/home/guanshun/.config/fish/config.fish` 中重新运行 fish 即可。

    如果没有使用 fish 而是使用原来的 bash，那么将下面这段代码添加到 `/home/guanshun/.bashrc` 中即可。

    ```shell
    # custom alias
    alias gc='git commit'
    alias gs='git status'
    alias gl='git log'
    alias gsh='git stash'
    alias ga='git add'
    alias gp='git push -origin'
    ```
