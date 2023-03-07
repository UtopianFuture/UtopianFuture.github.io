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
