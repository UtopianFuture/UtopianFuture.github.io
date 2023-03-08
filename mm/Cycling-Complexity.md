## 静态检查和 CC

### 静态检查
首先要了解什么是静态检查。静态检查又名程序静态分析(Program Static Analysis)是指在不运行代码的方式下，通过词法分析、语法分析、控制流、数据流分析等技术对程序代码进行扫描，验证代码是否满足规范性、安全性、可靠性、可维护性等指标的一种代码分析技术。

[静态分析工具](https://www.jetbrains.com/zh-cn/teamcity/ci-cd-guide/ci-cd-tools/)会扫描代码，查找常见的已知错误和漏洞，例如内存泄漏或缓冲区溢出。 这一分析还可以强制执行编码标准。

在优先考虑安全的情况下，专业的静态应用安全测试 (SAST) 工具可以检查已知的安全缺陷。 由于静态分析是在源代码上执行，而不执行程序，它既可以在 CI/CD 管道的最开始运行，也可以在提交变更之前直接从 IDE 运行。

然而，静态分析只能识别违反预编程规则的实例，不能仅通过阅读源代码找出所有缺陷。 也存在误报的风险，因此需要对结果进行解释。

从这个意义上说，静态代码分析是代码审查的有价值补充，因为它突出了已知的问题，并为对整体设计和方法的审查等更有趣的任务腾出了时间。

静态代码分析构成[自动化检查](https://www.jetbrains.com/zh-cn/teamcity/ci-cd-guide/automated-testing/)系列的一部分，可保持代码质量，应与其他形式的动态分析（执行代码以检查已知问题）和自动化测试结合使用。

下面介绍一些工具
#### Corax
问题：
- 需要构建的环境？C/C++
	这个构建应该是指需要编译，corax 使用编译的结果进行分析。
- 探针？探针是 corax 为每个环境创建的配置命令
- 静态检查怎样做到如此精确？

#### [vera++](https://bitbucket.org/verateam/vera/src/master/)

这是一个代码格式化工具，可以通过配置自己的规则脚本来检查代码格式是否规范，如每行不超过 100 字符、文件命名规范、关键字后根空格等。有需要可以使用。

#### [AdLint](https://adlint.sourceforge.net/pmwiki/upload.d/Main/users_guide_en.html)

用 ruby 编写的开源静态检查器，主要检查 ANSI C89, ISO C90 和 部分 ISO C99 标准，能够在 windows, MAC OS X, linux 等多个平台上运行。

##### 安装

目前只在 Linux 上使用过，先介绍 Linux 上的安装使用。

- 下载 [ruby 环境](http://www.ruby-lang.org/en/downloads/)
- `sudo gem install adlint`
- 使用 `adlintize -v` 和 `adlint -v` 检查是否安装成功

##### 使用

- 在项目根目录中运行 `adlintize -o adlint` 命令，在新创建的 `adlint` 目录中会产生如下文件
  - GNUmakefile — Make file for GNU make describing the analysis procedure
  - adlint_traits.yml：配置文件，可自由配置，取消某些 warning
  - adlint_pinit.h：这两个头文件还没搞懂有什么用
  - adlint_cinit.h：
  - adlint_all.sh：linux 下的运行脚本
  - adlint_all.bat：windows 下的运行脚本
  - adlint_files.txt：需要分析的文件列表
- 在 linux 下使用 `./adlint_all.sh` 运行即可。其对 adlint_files.txt 中的每个文件都会产生如下几个分析文件：
  - intro_demo.i：源码预处理文件，目前无需关注；
  - intro_demo.c.met.csv：所有的分析数据，adlint 对每个文件的每个函数都会给出分析数据，下面介绍；
  - intro_demo.c.msg.csv：warning, error 信息，每种 warning, error 手册都有详细的解释以及解决方案；
  - intro_demo.met.csv：交叉模块分析数据（目前还未遇到）；
  - intro_demo.msg.csv：交叉模块分析 warning, error 信息；

运行一个简单的 [demo](https://github.com/UtopianFuture/timer-interrupt)，

![image-20230308172837311](/home/guanshun/.config/Typora/typora-user-images/image-20230308172837311.png)

从结果来看，`hello_period.c` 这个文件是分析失败的，在 `adlint/hello_period.c.msg.csv` 中可找到失败原因，

```
E,../hello_period.c,26,53,core,E0008,ERR,X99,Syntax error is found in token `] __attribute__'.
```

错误编码 `E0008`，在手册中可以找到该编码的[意义](https://adlint.sourceforge.net/pmwiki/upload.d/Main/users_guide_en.html#index-E0008-60)，修改 `adlint/adlint_traits.yml` 后不再报错。

结果分为两部分，先看 warning 部分，

```
...
W,../memcpy.c,148,5,c_builtin,W0413,UNC,X99,The body of control statement is unenclosed by curly brace `{}' block.
W,../memcpy.c,148,7,c_builtin,W0512,UNC,X99,The result of `++' or `--' operator is used in the statement.
W,../memcpy.c,148,14,c_builtin,W0512,UNC,X99,The result of `++' or `--' operator is used in the statement.
W,../memcpy.h,4,1,c_builtin,W0071,UNC,X99,"Included ""../irq.h"" is not referenced in the translation unit. It can be removed."
...
```

这里给出了 warning 编号，可以对照手册修改。

再来看一下分析结果，

```
...

MET,FN_STMT,memcpy,"void * memcpy(void *,void const *,size_t)",../memcpy.c,4,7,5
MET,FN_UNRC,memcpy,"void * memcpy(void *,void const *,size_t)",../memcpy.c,4,7,0
MET,FN_CSUB,memcpy,"void * memcpy(void *,void const *,size_t)",../memcpy.c,4,7,0
MET,FN_GOTO,memcpy,"void * memcpy(void *,void const *,size_t)",../memcpy.c,4,7,0
MET,FN_RETN,memcpy,"void * memcpy(void *,void const *,size_t)",../memcpy.c,4,7,1
MET,FN_UELS,memcpy,"void * memcpy(void *,void const *,size_t)",../memcpy.c,4,7,0
MET,FN_NEST,memcpy,"void * memcpy(void *,void const *,size_t)",../memcpy.c,4,7,1
MET,FN_CYCM,memcpy,"void * memcpy(void *,void const *,size_t)",../memcpy.c,4,7,1
MET,FL_STMT,../memcpy.c,5
MET,FL_FUNC,../memcpy.c,1
```

每个变量含义如下：

```
     file_metric_name
       : "FL_STMT"  <- Number of statements
       | "FL_FUNC"  <- Number of functions

     func_metric_name
       : "FN_STMT"  <- Number of statements
       | "FN_UNRC"  <- Number of unreached statements
       | "FN_LINE"  <- Number of lines
       | "FN_PARA"  <- Number of parameters
       | "FN_UNUV"  <- Number of not use /not reuse variables
       | "FN_CSUB"  <- Location number of call function
       | "FN_CALL"  <- Location number of called from function
       | "FN_GOTO"  <- Number of goto statement
       | "FN_RETN"  <- Number of return point in a function
       | "FN_UELS"  <- Number of 'if' statement unless 'else'
       | "FN_NEST"  <- Maximum number of nest of control flow graph
       | "FN_PATH"  <- Presumed  number of static path
       | "FN_CYCM"  <- Cyclomatic complexity
```

总的来说，功能比较多，自由度也很高，但和上面的 corax 和 coverity 相比还有差距，不知道能够用于公司的项目。

### 圈复杂度

一种度量程序复杂度的方法，由 Thomas McCabe 于 1976 年定义，用来**衡量一个模块判定结构的复杂程度**，数量上表现为独立路径条数(if else; switch case?)，即合理的预防错误所需测试的最少路径条数，圈复杂度大说明程序代码质量低且难于测试和维护，根据经验，高的圈复杂度和程序出错的可能性有着很大关系。
#### sourcemonitor
首先要清楚 [SourceMonitor](https://www.derpaul.net/SourceMonitor/) 是为了检查代码[圈复杂度](http://kaelzhang81.github.io/2017/06/18/%E8%AF%A6%E8%A7%A3%E5%9C%88%E5%A4%8D%E6%9D%82%E5%BA%A6/)的工具。

#### [Lizard](https://github.com/terryyin/lizard)

该工具能够检测如下参数：

- 不包含注释代码行数；
- CCN 圈复杂度；
- 函数字符数；
- 函数参数；

| 优点                                                         | 缺点                                                         |
| ------------------------------------------------------------ | ------------------------------------------------------------ |
| 开源，开箱即用                                               | 统计的信息过于简单，有用的只有 CCN                           |
| 快速检查圈复杂度，检测内核时间：761s                         | 需要语法正确的代码才能检查                                   |
| 可设置 CCN 报警限制，默认 15                                 | 无法检查语法错误                                             |
| 可输出 xml, html 格式                                        | 不支持[高级语言特性](https://github.com/terryyin/lizard#limitations) |
| 可用 python 脚本[配置](https://github.com/terryyin/lizard#using-lizard-as-python-module) |                                                              |
| 对于不需要检查的函数可以使用白名单忽略，或者在代码中使用特定的注释忽略 |                                                              |

Example:

```
================================================
  NLOC    CCN   token  PARAM  length  location
------------------------------------------------
       4      1     38      3       4 csr_xchgl@7-10@./entry.h
       3      1     15      1       3 arch_local_irq_disable@12-14@./entry.h
       5      1     35      1       5 io_serial_in@46-50@./hello_period.c
       4      1     35      2       4 io_serial_out@52-55@./hello_period.c
       1      1     10      0       1 read_serial_rx@57-57@./hello_period.c
       6      2     42      1       6 print@59-64@./hello_period.c
       7      2     48      1       7 serial_print@66-72@./hello_period.c
       1      1     10      0       1 preempt_schedule_irq@73-73@./hello_period.c
       1      1     10      0       1 resume_userspace_check@75-75@./hello_period.c
       1      1     10      0       1 syscall_trace_leave@77-77@./hello_period.c
       1      1     10      0       1 schedule@79-79@./hello_period.c
       1      1     10      0       1 do_notify_resume@81-81@./hello_period.c
       1      1     10      0       1 trace_irqs_on@83-83@./hello_period.c
       1      1     10      0       1 say_hello@85-85@./hello_period.c
       1      1     10      0       1 say_hi@87-87@./hello_period.c
      11      1     39      0      11 arch_local_irq_disable@89-99@./hello_period.c
       7      1     33      0       7 arch_local_irq_enable@101-107@./hello_period.c
       5      1     23      0       5 timer_interrupt@109-113@./hello_period.c
      17      3     75      0      17 extioi_interrupt@115-131@./hello_period.c
      11      1     47      1      13 setitimer@133-145@./hello_period.c
       4      1     28      0       4 clear_time_intr@147-150@./hello_period.c
       1      1      9      0       1 irq_exit@152-152@./hello_period.c
       1      1      5      0       1 receive@154-154@./hello_period.c
       1      1      5      0       1 transmit@156-156@./hello_period.c
      10      2     36      1      10 do_vi@158-167@./hello_period.c
       5      1     18      1       5 do_res@169-173@./hello_period.c
      11      1     65      1      19 setup_vint_size@175-193@./hello_period.c
       4      1     31      1       5 configure_exception_vector@195-199@./hello_period.c
       4      1     41      3       4 set_handler@202-205@./hello_period.c
       8      1     46      0       9 fifo_trigger@207-215@./hello_period.c
       6      1     32      0       6 enable_mcr@217-222@./hello_period.c
      18      1    112      0      18 enable_serial_interrupt@224-241@./hello_period.c
       4      1     18      0       4 read_serial_interrupt@243-246@./hello_period.c
       5      1     33      0       5 enable_interruput@249-253@./hello_period.c
       9      2     52      0      17 trap_init@255-271@./hello_period.c
      14      2     53      1      15 start_entry@273-287@./hello_period.c
     131     24   1254      3     147 memcpy@4-150@./memcpy.c
9 file analyzed.
==============================================================
NLOC    Avg.NLOC  AvgCCN  Avg.token  function_cnt    file
--------------------------------------------------------------
      8       3.5     1.0       26.5         2     ./entry.h
      0       0.0     0.0        0.0         0     ./loongarchregs.h
      0       0.0     0.0        0.0         0     ./serial_reg.h
      0       0.0     0.0        0.0         0     ./irq.h
      1       0.0     0.0        0.0         0     ./page.h
    216       5.5     1.2       30.9        34     ./hello_period.c
    185       0.0     0.0        0.0         0     ./genex.h
    132     131.0    24.0     1254.0         1     ./memcpy.c
      9       0.0     0.0        0.0         0     ./memcpy.h

===========================================================================================================
!!!! Warnings (cyclomatic_complexity > 15 or length > 1000 or nloc > 1000000 or parameter_count > 100) !!!!
================================================
  NLOC    CCN   token  PARAM  length  location
------------------------------------------------
     131     24   1254      3     147 memcpy@4-150@./memcpy.c
==========================================================================================
Total nloc   Avg.NLOC  AvgCCN  Avg.token   Fun Cnt  Warning cnt   Fun Rt   nloc Rt
------------------------------------------------------------------------------------------
       551       8.8     1.8       63.7       37            1      0.03    0.40
```
