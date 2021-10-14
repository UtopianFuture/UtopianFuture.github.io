# debug相关

1. 用GDB调试xv6：

   （1）在文件夹中使用：qemu gdb命令

   （2）另开一个终端，使用：gdb kernel命令，在gdb中使用target remote localhost:26000

2. 由于LA上的GDB用不了layout src看源码，所以用list命令。

   list快捷键是l，可以接函数名，列出当前函数源码，每次10行。

3. Gdbserver可以远程调试程序。首先在远程机器上运行：gdbserver :1234(port) /xxx(elf)，然后在本地用target remote ip:port连接，gdbgui是gdb的图形化界面工具，直接使用gdbgui指令就可以使用，然后选择需要调试的进程，或者远程调试的端口。https://www.gdbgui.com/gettingstarted/

4. gdb在3a5000不能用layout：

   ​	安装ncurses-base包，之后用./configure --enable-tui –disable-werror配置gdb，编译，make install安装。

5. 这个命令用来配置qemu，用qemu跑同样的程序，输出和latx对比，看有哪些不同。

   ```
   ../configure --target-list=x86_64-linux-user --enable-debug --enable-tcg-interpreter –disable-werror
   ```

   这个命令用来在本地操作远程的机器执行程序，“2>”代表重定向到标准输出，即>重定向指定的目录。

   ```
   ssh guanshun /home/guanshun/gitlab/qemu/build/qemu-x86_64 -d cpu -singlestep /home/guanshun/GDB/hello 2> /home/guanshun/research/loongson/qemu_log
   ```

6. 将需要对比的文件加到vscode中，然后select for compare，就可以对比两个文件的不同。如果文件过大还是用diff和vimdiff。

7. 终极的调试手段就是将目标程序与标准程序对比，将两个程序的执行环境设置为一样，然后printf出程序的reg信息，一个个对比。这其中要设置环境变量一样，两个程序的执行命令一样，还要知道reg信息怎么打印。

8. latx-x64的bug：

   (1) 寄存器高位清0；

   (2) 无效指令，或是latx没有实现，或是capstone没有实现；

9. tui reg group（gdb 打印寄存器参数）

   ```
   next: Repeatedly selecting this group will cause the display to cycle through all of the available register groups.
   
   prev: Repeatedly selecting this group will cause the display to cycle through all of the available register groups in the reverse order to next.
   
   general: Display the general registers.
   
   float: Display the floating point registers.
   
   system: Display the system registers.
   
   vector: Display the vector registers.
   
   all: Display all registers.
   ```

10. ​	