# 编程相关

### 1. 静态全局变量

（1）该变量在全局数据区分配内存；

（2）未经初始化的静态全局变量会被程序自动初始化为 0（自动变量的自动初始化值是随机的）；

（3）**静态全局变量在声明它的整个文件都是可见的，而在文件之外是不可见的**； 　

（4）静态变量都在全局数据区分配内存，包括后面将要提到的静态局部变量。对于一个完整的程序，在内存中的分布情况如下：代码区、全局数据区、堆区、栈区，一般程序的由 new 产生的动态数据存放在堆区，函数内部的自动变量存放在栈区，静态数据（即使是函数内部的静态局部变量）存放在全局数据区。**自动变量一般会随着函数的退出而释放空间**，而全局数据区的数据并不会因为函数的退出而释放空间。

### 2. 静态函数

（1）只能在声明它的文件当中可见，不能被其它文件所用；

（2）其它文件中可以定义相同名字的函数，不会发生冲突；

### 3. goto

允许汇编指令跳转到 C 语言定义的 GotoLabels 任一个，多个 GotoLabels 用逗号分割。允许 goto 跳转的汇编块不允许包含输出。如下代码示例显示了内联跳转。

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

注意上述示例中，根据 gcc 规则，常数必须是 cmpl 的第一个参数。

### 4. volatile

volatile 和 C 语言中的一致，如果汇编指令产生边界效应，需要添加 volatile 禁止优化。

### 5. __builtin_expect

这个指令是 gcc 引入的，作用是**允许程序员将最有可能执行的分支告诉编译器**，目的是将“分支转移”的信息提供给编译器，这样编译器可以对代码进行优化，以减少指令跳转带来的性能下降。。这个指令的写法为：`__builtin_expect(EXP, N)`。
意思是：EXP==N 的概率很大。

一般的使用方法是将`__builtin_expect`指令封装为 `likely`和 `unlikely`宏。这两个宏的写法如下：

```c
#define likely(x) __builtin_expect(!!(x), 1) //x很可能为真
#define unlikely(x) __builtin_expect(!!(x), 0) //x很可能为假
```

### 6. 条件判断

内核很喜欢将这种条件判断封装成一个函数，应该是使用的地方多，方便吧。

```c
static bool kvm_pgtable_walk_skip_cmo(const struct kvm_pgtable_visit_ctx *ctx)
{
	return unlikely(ctx->flags & KVM_PGTABLE_WALK_SKIP_CMO);
}

static bool kvm_phys_is_valid(u64 phys)
{
	return phys < BIT(id_aa64mmfr0_parange_to_phys_shift(ID_AA64MMFR0_EL1_PARANGE_MAX));
}
```
