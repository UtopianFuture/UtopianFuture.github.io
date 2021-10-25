# 编程相关

1. 静态全局变量有以下特点：

   （1）该变量在全局数据区分配内存；

   （2）未经初始化的静态全局变量会被程序自动初始化为0（自动变量的自动初始化值是随机的）；

   （3）**静态全局变量在声明它的整个文件都是可见的，而在文件之外是不可见的**； 　

   （4）静态变量都在全局数据区分配内存，包括后面将要提到的静态局部变量。对于一个完整的程序，在内存中的分布情况如下：代码区、全局数据区、堆区、栈区，一般程序的由new产生的动态数据存放在堆区，函数内部的自动变量存放在栈区，静态数据（即使是函数内部的静态局部变量）存放在全局数据区。**自动变量一般会随着函数的退出而释放空间**，而全局数据区的数据并不会因为函数的退出而释放空间。

2. 静态函数

   （1）只能在声明它的文件当中可见，不能被其它文件所用；

   （2）其它文件中可以定义相同名字的函数，不会发生冲突；

3. goto
    允许汇编指令跳转到C语言定义的GotoLabels任一个，多个GotoLabels用逗号分割。允许 goto跳转的汇编块不允许包含输出。如下代码示例显示了内联跳转。

    ```c
    #include <stdio.h>
    void func(int i)
    {
            asm goto ("cmpl $1,%0\n\t"
                      "je %l[Label1]\n\t"
                      "cmpl $2, %0\n\t"
                      "je %l[Label2]\n\t"
                      : /* no outputs */
                      : "r"(i)
                      : /* no clobber */
                      : Label1, Label2
                     );

            printf("asm no goto\n");
            return;

    Label1:
            printf("asm goto Label1\n");
            return;

    Label2:
            printf("asm goto Label2\n");
            return;
    }

    int main(int argc, char *argv[argc])
    {
            func(0);
            func(1);
            func(2);
            func(3);
            return 0;
    }
    ```

    注意上述示例中，根据gcc规则，常数必须是cmpl的第一个参数。

4. volatile
    volatile和C语言中的一致，如果汇编指令产生边界效应，需要添加volatile禁止优化。
