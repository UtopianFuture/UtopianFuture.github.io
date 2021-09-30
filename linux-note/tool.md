# 快捷工具

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

6. xclip可以复制标准输出到粘贴板，具体使用待探索。

7. fish能自动联想指令，安装过程很简单，sudo apt install fish，之后直接`fish`即可使用。亦可将其作为默认shell。

8. stat 查看文件的状态

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

9. tar 文件解压，最常用的参数是`-xvf`

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

10. tmux工具可以用来分屏，常用的快捷键：

   Ctrl+a %：划分左右两个窗格；

   Ctrl+a "：划分上下两个窗格；

   Ctrl+a <arrow key>：光标切换到其他窗格。<arrow key>是指向要切换到的窗格的方向键，比如切换到下方窗格，就按方向键↓。

   Ctrl+a x：关闭当前窗格。

   Ctrl+a Ctrl+<arrow key>：按箭头方向调整窗格大小。

   Ctrl+a z：当前窗格全屏显示，再使用一次会变回原来大小

   Ctrl+a c：创建一个新窗口，状态栏会显示多个窗口的信息。

   tmux的配置主要是修改`~/.tmux/tmux.conf`来完成的，修改完后用`tmux source ~/.tmux/tmux.conf`来更新配置。

   按下PREFIX+[快捷键进入复制模式，在复制模式下按下q字符退出复制模式。

   复制模式类似于Vim的普通模式，键盘操作风格也类似，在复制模式下，按下v字符，进行待复制内容的选取，类似于进入Vim的可视模式，键盘操作风格也类似。

   在复制模式下crtl + s进入查找模式，同样按q退出。

11. termux: arm中使用的终端。

12. 输入输出重定向是

    ```
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

13. history程序可以查询所有用过的指令。

14. split可以将文件分为大小不同的块。如split -l 1000000 latx_log -d -a 4 latx_log_，-l表示每个文件多少行，也可以按比特分；-d表示后缀用数字表示，-a 4表示后缀是4位数字。

15. rsync可以用来在不同机器上同步文件，注意host和target文件名最后不加斜杠表示同步整个文件夹到目的文件夹中。

    ```
    rsync -avzP /home/guanshun/gitlab/latx/target guanshun:/home/guanshun/gitlab/latx-x64
    ```

16. vim   

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

17. pkill：杀进程，-9强制杀死

    ```
     - Kill all processes which match:
       pkill -9 "{{process_name}}"
    
     - Kill all processes which match their full command instead of just the process name:
       pkill -9 --full "{{command_name}}"
    ```

18. diff和vimdiff可以以行为单位对比两个文件的不同。

19. lsblk - list block devices

20. mmap, munmap - map or unmap files or devices into memory

21. cat file1 file2 file3… > file将多个文件合并成同一个文件。

22. truncate可以设置文件的大小，如`truncate --size 50M qemu_log`，超过50M的就会截断。

23. sed可以删除文件的任意行，`sed ‘n, md’ filename`；或者删除含有某个字符的行，将结果输出到指定文件，`sed ‘/xxx/d’  filename1 > filename2`，删除不包含某一字符的行，`sed ‘/xxx/!d’  filename1 > filename2`。

24. du查看文件大小

    ```
    - List the sizes of a directory and any subdirectories, in the given unit (B/KB/MB):
       du -{{b|k|m}} {{path/to/directory}}
    
     - List the sizes of a directory and any subdirectories, in human-readable form (i.e. auto-selecting the appropriate unit for each size):
       du -h {{path/to/directory}}
    
     - Show the size of a single directory, in human readable units:
       du -sh {{path/to/directory}}
    
     - List the human-readable sizes of a directory and of all the files and directories within it:
       du -ah {{path/to/directory}}
    ```

25. df查看磁盘使用情况。

    ```
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

26. wc -l 查看文件行数。

27. Loongarch下的gdb不能查看浮点寄存器，将

    ​	gdb/features/loongarch/lbt64.c

    ​	gdb/features/loongarch/lbt32.c

    ​	gdb/features/loongarch/lasx.c

    ​	gdb/features/loongarch/lsx.c

    ​	中的lasx等变量修改成NULL，然后重新编译，记得修改环境变量。

28. gdb显示数据的不同格式：

    ​	x  按十六进制格式显示变量。

    ​	d  按十进制格式显示变量。

    ​	u  按十六进制格式显示无符号整型。

    ​	o  按八进制格式显示变量。

    ​	t  按二进制格式显示变量。

    ​	a  按十六进制格式显示变量。

    ​	c  按字符格式显示变量。

    ​	f  按浮点数格式显示变量。

29. gdb使用examine命令（简写是x）来查看内存地址中的值。x命令的语法如下所示：

    ​	x/<n/f/u> <addr>  n、f、u是可选的参数。

    ​	n 是一个正整数，表示显示内存的长度，也就是说从当前地址向后显示几个地址的内容。

    ​	f 表示显示的格式，参见44。

    ​	u表示显示的单位，b表示单字节，h表示双字节，w表示四字节，g表示八字节。当我们指定了字节长度后，GDB会从指内存定的内存地址开始，读写指定字节，并把其当作一个值取出来。

    ​	<addr>表示一个内存地址。

    ​	n/f/u三个参数可以一起使用。例如：

    ​	命令：x/8xb 0x54320 表示，从内存地址0x54320读取内容，b表示以字节为一个单位，8表示八个单位，x表示按十六进制显示。

30. sensor能够查看硬件温度，psensor能够gui显示。

31. vscode 中alt + 方向键能交换相邻行。