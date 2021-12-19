# debug 相关

1. 用 GDB 调试 xv6：

   （1）在文件夹中使用：qemu gdb 命令

   （2）另开一个终端，使用：gdb kernel 命令，在 gdb 中使用 target remote localhost:26000

2. GDB 中用 layout 查看源码

   ```plain
   layout src		显示源代码窗口
   layout asm		显示汇编窗口
   layout regs		显示源代码/汇编和寄存器窗口
   layout split	显示源代码和汇编窗口
   layout next		显示下一个layout
   layout prev		显示上一个layout
   Ctrl + L		刷新窗口
   Ctrl + x + 1	单窗口模式，显示一个窗口
   Ctrl + x + 2	双窗口模式，显示两个窗口
   Ctrl + x + a	回到传统模式，即退出layout，回到执行layout之前的调试窗口。
   ```

3. 由于 LA 上的 GDB 用不了 layout src 看源码，所以用 list 命令。

   list 快捷键是 l，可以接函数名，列出当前函数源码，每次 10 行。

4. Gdbserver 可以远程调试程序。首先在远程机器上运行：gdbserver :1234(port) /xxx(elf)，然后在本地用 target remote ip:port 连接，gdbgui 是 gdb 的图形化界面工具，直接使用 gdbgui 指令就可以使用，然后选择需要调试的进程，或者远程调试的端口。https://www.gdbgui.com/gettingstarted/

5. gdb 在 3a5000 不能用 layout：

   ​	安装 ncurses-base 包，之后用./configure --enable-tui --disable-werror 配置 gdb，make 编译，make install 安装。

6. 这个命令用来配置 qemu，用 qemu 跑同样的程序，输出和 latx 对比，看有哪些不同。

   ```plain
   ../configure --target-list=x86_64-linux-user --enable-debug --enable-tcg-interpreter –disable-werror
   ```

   这个命令用来在本地操作远程的机器执行程序，“2>”代表重定向到标准输出，即>重定向指定的目录。

   ```plain
   ssh guanshun /home/guanshun/gitlab/qemu/build/qemu-x86_64 -d cpu -singlestep /home/guanshun/GDB/hello 2> /home/guanshun/research/loongson/qemu_log
   ```

   也可以将标准输出和标准错误流都输出到一个文件

   ```plain
   build_loongson/kernel.bin 1> loongson.md 2>&1
   ```

   1 表示标准输出流，`2>&1` 表示将标准错误流也输出到标准输出流中，也就是 loongson.md 中。

7. 将需要对比的文件加到 vscode 中，然后 select for compare，就可以对比两个文件的不同。如果文件过大还是用 diff 和 vimdiff。

8. 终极的调试手段就是将目标程序与标准程序对比，将两个程序的执行环境设置为一样，然后 printf 出程序的 reg 信息，一个个对比。这其中要设置环境变量一样，两个程序的执行命令一样，还要知道 reg 信息怎么打印。

9. latx-x64 的 bug：

   (1) 寄存器高位清 0；

   (2) 无效指令，或是 latx 没有实现，或是 capstone 没有实现；

10. tui reg group（gdb 打印寄存器参数）

   ```plain
   next: Repeatedly selecting this group will cause the display to cycle through all of the available register groups.

   prev: Repeatedly selecting this group will cause the display to cycle through all of the available register groups in the reverse order to next.

   general: Display the general registers.

   float: Display the floating point registers.

   system: Display the system registers.

   vector: Display the vector registers.

   all: Display all registers.
   ```

11. gcc 中的`-fno-builtin`命令
        -fno-builtin-function
        	Don't recognize built-in functions that do not begin with ******************************************__builtin_ as prefix.
        	GCC normally generates special code to handle certain built-in functions more efficiently; for instance, calls to "alloca" may become single instructions which adjust the stack directly, and calls to "memcpy" may become inline copy loops.  The resulting code is often both smaller and faster, but since the function calls no longer appear as such, you cannot set a breakpoint on those calls, nor can you change the behavior of the functions by linking with a different library.  In addition, when a function is recognized as a built-in function, GCC may use information about that function to warn about problems with calls to that function, or to generate more efficient code, even if the resulting code still contains calls to that function.  For example, warnings are given with -Wformat for bad calls to "printf" when "printf" is built in and "strlen" is known not to modify global memory.
        	With the -fno-builtin-function option only the built-in function function is disabled.  function must not begin with __******************************************builtin_.  If a function is named that is not built-in in this version of GCC, this option is ignored.  There is no corresponding -fbuiltin-function option; if you wish to enable built-in functions selectively when using -fno-builtin or -ffreestanding, you may define macros such as:

                       #define abs(n)          __builtin_abs ((n))plainplainplainplainplainplainplainplainplainplainplainplainplainplainplainplainplainplain
                       #define strcpy(d, s)    __builtin_strcpy ((d), (s))

    即不用内联函数，便于设置断点调试。而`-fno-builtin-function`则是指定某个函数不用，这个函数的命名可以和 bulit-in 函数重名。

12. gcc 中的`-ffreestanding`命令

    ```plain
    -ffreestanding
    	Assert that compilation targets a freestanding environment.  This implies -fno-builtin.  A freestanding environment is one in which the standard library may not exist, and program startup may not necessarily be at "main".  The most obvious example is an OS kernel.  This is equivalent to -fno-hosted.
    ```

    即程序的入口可以不是 main，而是指定的入口。

13. gcc 中的`-nostdlib`
        	Do not use the standard system startup files or libraries when linking.  No startup files and only the libraries you specify are passed to the linker, and options specifying linkage of the system libraries, such as -static-libgcc or -shared-libgcc, are ignored.
        	The compiler may generate calls to "memcmp", "memset", "memcpy" and "memmove".  These entries are usually resolved by entries in libc.  These entry points should be supplied through some other mechanism when this option is specified.
        	One of the standard libraries bypassed by -nostdlib and -nodefaultlibs is libgcc.a, a library of internal subroutines which GCC uses to overcome shortcomings of particular machines, or special needs for some languages.
        	In most cases, you need libgcc.a even when you want to avoid other standard libraries.  In other words, when you specify -nostdlib or -nodefaultlibs you should usually specify -lgcc as well.
               This ensures that you have no unresolved references to internal GCC library subroutines.  (An example of such an internal subroutine is "__main", used to ensure C++ constructors are called.)

    即不使用标准库，仅使用 gcc 自带的`libgcc.a`。

14. gcc 中的 -H

    ```plain
    除了其他普通的操作, GCC 显示引用过的头文件名.
    ```

15. gdb 中出现

    ![image-20211127104830859](/home/guanshun/.config/Typora/typora-user-images/image-20211127104830859.png)

    ```plain
    因为暂时适应的是 signal 来实现 timer 的
    这个会导致 qemu 提前停下来

    使用这个命令可以屏蔽信号:
    handle SIG127 nostop noprint
    https://stackoverflow.com/questions/24999208/how-to-handle-all-signals-in-gdb
    ```

16. show print elements 显示 gdb 能够打印的最大字符数量。

    set print elements 500 设置 gdb 能够打印的最大字符数量。
