## 静态检查和 CC

### 静态检查
首先要了解什么是静态检查。静态检查又名程序静态分析(Program Static Analysis)是指在不运行代码的方式下，通过词法分析、语法分析、控制流、数据流分析等技术对程序代码进行扫描，验证代码是否满足规范性、安全性、可靠性、可维护性等指标的一种代码分析技术。

静态分析工具会扫描代码，查找常见的已知错误和漏洞，例如内存泄漏或缓冲区溢出。 这一分析还可以强制执行编码标准。

在优先考虑安全的情况下，专业的静态应用安全测试 (SAST) 工具可以检查已知的安全缺陷。 由于静态分析是在源代码上执行，而不执行程序，它既可以在 CI/CD 管道的最开始运行，也可以在提交变更之前直接从 IDE 运行。

然而，静态分析只能识别违反预编程规则的实例，不能仅通过阅读源代码找出所有缺陷。 也存在误报的风险，因此需要对结果进行解释。从这个意义上说，静态代码分析是代码审查的有价值补充，因为它突出了已知的问题，并为对整体设计和方法的审查等更有趣的任务腾出了时间。静态代码分析构成自动化检查系列的一部分，可保持代码质量，应与其他形式的动态分析（执行代码以检查已知问题）和自动化测试结合使用。

这里总结了很多工具[^2]，下面挑一些开源的学习一下。

#### [AdLint](https://adlint.sourceforge.net/pmwiki/upload.d/Main/users_guide_en.html)

用 ruby 编写的开源静态检查器，主要检查 ANSI C89, ISO C90 和 部分 ISO C99 标准，能够在 windows, MAC OS X, linux 等多个平台上运行。

##### 安装

- Linux 安装

  - 下载 [ruby 环境](http://www.ruby-lang.org/en/downloads/)

  - `sudo gem install adlint`

  - 使用 `adlintize -v` 和 `adlint -v` 检查是否安装成功

- windows 安装

  - 下载 [ruby 环境](http://www.ruby-lang.org/en/downloads/)
  - 安装 ruby development kit（注意上面的分开安装）
  - 将 ruby 安装目录的 bin 文件夹和 ruby devkit 安装目录的 bin, mingw\bin 文件夹添加到系统环境变量
  - 运行命令行工具，`gem install adlint` 命令安装
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
- 在 linux 下使用 `./adlint_all.sh` 运行，在 windows 下使用 `./adlint_all.bat`。其对 adlint_files.txt 中的每个文件都会产生如下几个分析文件：
  - intro_demo.i：源码预处理文件，目前无需关注；
  - intro_demo.c.met.csv：所有的分析数据，adlint 对每个文件的每个函数都会给出分析数据，下面介绍；
  - intro_demo.c.msg.csv：warning, error 信息，每种 warning, error 手册都有详细的解释以及解决方案；
  - intro_demo.met.csv：交叉模块分析数据（目前还未遇到）；
  - intro_demo.msg.csv：交叉模块分析 warning, error 信息；

运行一个简单的 [demo](https://github.com/UtopianFuture/timer-interrupt)，在 windows 和 linux 中都运行 adlint，

![adlint](https://github.com/UtopianFuture/UtopianFuture.github.io/blob/master/image/adlint.png?raw=true)

![adlint-linux.png](https://github.com/UtopianFuture/UtopianFuture.github.io/blob/master/image/adlint-linux.png?raw=true)

从结果来看，`hello_period.c` 这个文件是分析失败的，在 `adlint/hello_period.c.msg.csv` 中可找到失败原因，

```plain
E,../hello_period.c,26,53,core,E0008,ERR,X99,Syntax error is found in token `] __attribute__'.
```

错误编码 `E0008`，在手册中可以找到该编码的[意义](https://adlint.sourceforge.net/pmwiki/upload.d/Main/users_guide_en.html#index-E0008-60)，修改 `adlint/adlint_traits.yml` 后不再报错。

结果分为两部分，先看 warning 部分，

```plain
...
W,../memcpy.c,148,5,c_builtin,W0413,UNC,X99,The body of control statement is unenclosed by curly brace `{}' block.
W,../memcpy.c,148,7,c_builtin,W0512,UNC,X99,The result of `++' or `--' operator is used in the statement.
W,../memcpy.c,148,14,c_builtin,W0512,UNC,X99,The result of `++' or `--' operator is used in the statement.
W,../memcpy.h,4,1,c_builtin,W0071,UNC,X99,"Included ""../irq.h"" is not referenced in the translation unit. It can be removed."
...
```

这里给出了 warning 编号，可以对照手册修改。

再来看一下分析结果，

```plain
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

```plain
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

总的来说，功能比较多，自由度也很高，但其应该只是一个能用的水平，和 coverity 相比差距在哪，还需要进一步对比。尤其是只能支持 c 语言，局限性较大，不知道能够用于公司的项目。

#### [cppcheck](https://cppcheck.sourceforge.io/)

这个我看组内有同事在看，它能做的比下面两个圈复杂度工具要多，主要用来检测 C/C++ 项目。其支持如下代码标准，

- Misra C 2012: Full coverage in open source tool.
- Autosar: Partial coverage in Cppcheck Premium.
- Cert C: Full coverage in Cppcheck Premium.
- Misra C++ 2008: Partial coverage in Cppcheck Premium.

能够检查出如下问题：

error：代码中的错误项，包括内存泄漏等；

warning：为了避免产生 bug 而提供的编程改进意见；

style：编码风格，提示你哪些函数没有使用、哪些为多余代码等；

portability：移植性警告。该部分如果移植到其他平台上，可能出现兼容性问题；

performance：该部分代码可以优化，这些建议只是基于常识，即使修复这些消息，也不确定会得到任何可测量的性能提升；

information：其他信息（ 配置问题，建议在配置期间仅启用这些），可以忽略；

##### 安装

- linux

  `sudo apt-get install cppcheck` 开箱即用，很方便

- windows

  下载 [exe 文件](https://github.com/danmar/cppcheck/releases/download/2.10/cppcheck-2.10-x64-Setup.msi)默认安装即可

##### 使用

来看一个 linux 下的简单例子（windows 下同样可使用命令行），

```shell
./cppcheck ./ -i .ccls-cache/
Checking hello_period.c ...
hello_period.c:275:4: error: Array 'a[10]' accessed at index 10, which is out of bounds. [arrayIndexOutOfBounds]
  a[10] = 0;
   ^
1/2 files checked 69% done
Checking memcpy.c ...
Checking memcpy.c: __GNUC__...
2/2 files checked 100% done
```

对于异常代码，其会给出清晰的标注。其还有丰富的功能可在项目开发中不断探索。

#### [infer](https://infer.liaohuqiu.net/docs/getting-started.html)

Infer 是 facebook 开源的一款代码静态分析工具，现支持的语言有 Java、Objective-C、C 和 C++; 对 Android 和 Java 代码可以发现 null pointer exceptions 和 resource leaks 等；对 iOS、C 和 C++代码可以发现 memory leak 等。

其分析方式分为两步：

- 捕获阶段

  Infer 捕获编译命令，将文件翻译成 Infer 内部的中间语言。

  这种翻译和编译类似，Infer 从编译过程获取信息，并进行翻译。在调用 Infer 时需要带上一个编译命令，比如: `infer -- clang -c file.c`, `infer -- javac File.java`。结果就是文件照常编译，同时被 Infer 翻译成中间语言，留作第二阶段处理。特别注意的就是，如果没有文件被编译，那么也没有任何文件会被分析。

  Infer 把中间文件存储在结果文件夹中，一般来说，这个文件夹会在运行 `infer` 的目录下创建，命名是 `infer-out/`（可通过 -o 自定义）。

- 分析阶段

  在分析阶段，Infer 分析 `infer-out/` （或其他自定义文件）下的所有文件。分析时，会单独分析每个方法和函数。在分析一个函数的时候，如果发现错误，将会停止分析，但这不影响其他函数的继续分析。

  在检查问题时，修复输出的错误之后，需要继续运行 Infer 进行检查，知道确认所有问题都已经修复。

##### 安装

infer 只能运行在 macos 和 linux 上，这里只介绍 linux。

- linux
  - [下载](https://github.com/facebook/infer/releases/download/v1.1.0/infer-linux64-v1.1.0.tar.xz)文件
  - 加压后将 infer/bin 目录设置到环境变量中

##### 使用

infer 有如下几种运行机制：

- 初次运行时，确保项目是清理过的。可以通过 (`make clean`，`gradle clean` 等等)；

- 两次运行之间，记得清理项目，或者通过 `--incremental` 选项，防止因为增量编译而无结果输出；

- 如果使用非增量编译系统，则无需如此，比如：`infer -- javac Hello.java`，编译整个 Java 文件；

- 成功运行之后，在同一目录下，可以通过 `inferTraceBugs` 浏览更加详细的报告；
- 除此之外，还可以和其他许多编译系统一起使用 Infer。比如：`infer -- make -j8`。

经过测试，发现其对于一些 bug 无法检测，来看一个例子：

```c
#include "stdio.h"

int fib(int n) {
  if (n == 1 || n == 2) {
    return 1;
  } else if (n == 0) {
    return 0;
  }
  int pre = 1, cur = 1;
  for (int i = 3; i <= n; i++) {
    cur += pre;
    pre = cur - pre;
  }
  return cur;
}

int main() {
  // int n = 0;
  char a[10];
  a[10] = 0; // test code
  int * test = NULL;
  test = (int *)&a[10];

  for (int i = 0; i <= 30; i++) { // test 0 ~ 30
    printf("fib %d: %d\n", i, fib(i));
  }
  // scanf("%d", &n);
  // printf("%d\n", fib(n));
  return 0;
}
```

对于这段测试代码，有两个问题，一个是数组越界访问，一个是空指针问题。但是 infer 只检查出一个问题，

```shell
guanshun@guanshun-ubuntu ~/g/leetcode (main)> infer -- gcc -c fib.c
Capturing in make/cc mode...
Found 1 source file to analyze in /home/guanshun/gitlab/leetcode/infer-out
1/1 [################################################################################] 100% 116ms

fib.c:22: error: Dead Store
  The value written to &test (type int*) is never used.
  20.   a[10] = 0;
  21.   int * test = NULL;
  22.   test = (int *)&a[10];
        ^
  23.
  24.   for (int i = 0; i <= 30; i++) { // test 0 ~ 30


Found 1 issue
  Issue Type(ISSUED_TYPE_ID): #
      Dead Store(DEAD_STORE): 1
```

这只是简单的使用，具体检查效果有待进一步测试。

#### [codacy](https://docs.codacy.com/)

方便好用的集成工具，能够直接测试 gitlab, github, bitbucket 代码托管平台上的代码。文档丰富，底层用的是 flawfinder, cppcheck 等检查工具。

##### 安装

codacy 是在线工具，可以直接[使用](https://app.codacy.com/organizations/gh/UtopianFuture/dashboard)。

##### 使用

其功能可以分为如下几大类：

- commits：对于每次提交的 commit 会作增量检查，

  ![codacy-commits.png](https://github.com/UtopianFuture/UtopianFuture.github.io/blob/master/image/codacy-commits.png?raw=true)

​	可以清晰的看到每次 commit 引入的 bug。

- files：每个文件所有的 bug；

- issues：所有未解决的 issue

  ![codacy-issues.png](https://github.com/UtopianFuture/UtopianFuture.github.io/blob/master/image/codacy-issues.png?raw=true)

- security：项目的安全漏洞。这项应该和代码的静态检查关系不到，主要是项目的安全性，因为这里的项目是从一些代码托管平台导入的，所以可能会存在安全问题？

  ![codacy-security.png](https://github.com/UtopianFuture/UtopianFuture.github.io/blob/master/image/codacy-security.png?raw=true)

- code patterns：这里可以设置使用哪些检查工具、检查规则，从我目前了解到的信息来看，这个模块是 CI/CD 最关心的部分，

  ![codacy-code.png](https://github.com/UtopianFuture/UtopianFuture.github.io/blob/master/image/codacy-code.png?raw=true)

这个平台的文档很全面，上面的只是做个简单介绍，具体的说明可以看[这里](https://docs.codacy.com/repositories/repository-dashboard/)。

#### [semgrep](https://semgrep.dev/orgs/utopianfuture)

类似于上面的 codacy，有待研究。

### 圈复杂度

一种度量程序复杂度的方法，由 Thomas McCabe 于 1976 年定义，用来**衡量一个模块判定结构的复杂程度**，数量上表现为独立路径条数(if else; switch case?)，即合理的预防错误所需测试的最少路径条数，圈复杂度大说明程序代码质量低且难于测试和维护，根据经验，高的圈复杂度和程序出错的可能性有着很大关系。

我们可以采取直接数的方式计算圈复杂度，

```c
 U32 find (string match){
         for(auto var : list)
         {
             if(var == match && from != INVALID_U32) return INVALID_U32;
         }
         //match step1
         if(session == getName() && key == getKey())
         {
             for (auto& kv : Map)
             {
                 if (kv.second == last && match == kv.first)
                 {
                     return last;
                 }
             }

         }
         //match step2
         auto var = Map.find(match);
         if(var != Map.end()&& (from != var->second)) return var->second;

         //match step3
         for(auto var: Map)
         {
             if((var.first, match) && from != var.second)
             {
                 return var.second;
             }
         }
         return INVALID_U32;
 };
```

其圈复杂度为：V(G) = 1(for) + 2(if) + 2(if) + 1(for) + 2(if) + 2(if) + 1(for) + 2(if) + 1= 14。

常见的判定语句圈复杂度为：while: 1； switch case: case 个数；三元语句：2。

#### sourcemonitor[^3]
首先要清楚 SourceMonitor 是为了检查代码[圈复杂度](http://kaelzhang81.github.io/2017/06/18/%E8%AF%A6%E8%A7%A3%E5%9C%88%E5%A4%8D%E6%9D%82%E5%BA%A6/)的工具。

这个工具和下面介绍的 Lizard 功能类似，其运行在 windows 平台上（貌似也能运行在 linux 上，但没找到开源代码），能够达到开箱即用。

其能够测试如下信息：

- **总行数(Lines)**：包括空行在内的代码行数；

- **语句数目(Statements)**：在 C 语言中，语句是以分号结尾的。分支语句 if，循环语句 for、while，跳转语句 goto 都被计算在内，预处理语句#include、#define 和#undef 也被计算在内，对其他的预处理语句则不作计算，在#else 和#endif、#elif 和#endif 之间的语句将被忽略；

- **分支语句比例(Percent Branch Statements)**：该值表示分支语句占语句数目的比例，这里的“分支语句”指的是使程序不顺序执行的语句，包括 if、else、for、while 和 switch；

- **注释比例(Percent Lines with Comments)**：该值指示注释行（包括/*……*/和//……形式的注释）占总行数的比例；

- **函数数目(Functions)**：指示函数的数量；

- **平均每个函数包含的语句数目(Average Statements per Function)**：总的函数语句数目除以函数数目得到该值；

- **函数圈复杂度(Function Complexity)**：圈复杂度指示一个函数可执行路径的数目，以下语句为圈复杂度的值贡献 1：if/else/for/while 语句，三元运算符语句，if/for/while 判断条件中的"&&"或“||”，switch 语句，后接 break/goto/ return/throw/continue 语句的 case 语句，catch/except 语句；

- **函数深度(Block Depth)**：函数深度指示函数中分支嵌套的层数。

我们跑一个简单的项目：

![sourcemonitor](https://github.com/UtopianFuture/UtopianFuture.github.io/blob/master/image/sourcemonitor.png?raw=true)

这里可以构建多个 checkpoint，每个 checkpoint 可自由配置检查的文件。还可以进一步检查每个文件中的函数情况，包括 CC，语句数量，分支嵌套次数，引用次数。

| 优点                                    | 缺点                                                   |
| --------------------------------------- | ------------------------------------------------------ |
| 开箱即用，使用简单                      | 统计的信息过于简单，有用的只有 CCN，每个函数的嵌套层数 |
| 检测内核时间：小时级别                  | 难以集成到 CI/CD 中                                    |
| 可设置多个 checkpoint，自由设置检测文件 | 无法检查语法错误                                       |
| UI 界面丰富                             |                                                        |

#### [Lizard](https://github.com/terryyin/lizard)

该工具运行在 linux 平台上，能够检测如下参数：

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

##### 安装

- pip install lizard
- 使用 lizard -h 查看是否安装成功

##### 使用

- 使用方便，可以用 `lizard -h` 查看所有的功能，这里列举些常用的

  ```plain
  positional arguments:
    paths                 list of the filename/paths.

  optional arguments:
    -l LANGUAGES			项目使用的语言
    -C CCN, --CCN CCN     限制 CC，超过报警
    -f INPUT_FILE			检查特定文件
    -o OUTPUT_FILE		将结果输出到特定文件
    -w, --warnings_only   只输出报警信息
    -x EXCLUDE			不包含某些信息
    --csv                 将结果以 csv 方式输出
    -H, --html            将结果以 html 方式输出
    -W WHITELIST			设置白名单，不用检测
  ```

Example:

```shell
lizard ./ -x"./image/*" -x"./.git/*" -x"./.ccls-cache/*" -x"./script/*" -l c
```



```plain
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

#### [vera++](https://bitbucket.org/verateam/vera/src/master/)

这是一个代码格式化工具，可以通过配置自己的规则脚本来检查代码格式是否规范，如每行不超过 100 字符、文件命名规范、关键字后根空格等。有需要可以使用。

#### 总结

这三个工具个人觉得只能作为小组件检测 CC 或者自动化格式检查，无法作为静态检查工具（相比于 corax 和 coverity 相比不是一个级别的工具）。

### Reference

[^1]:[圈复杂度](http://kaelzhang81.github.io/2017/06/18/%E8%AF%A6%E8%A7%A3%E5%9C%88%E5%A4%8D%E6%9D%82%E5%BA%A6/)
[^2]:[静态检查工具总结](https://en.wikipedia.org/wiki/List_of_tools_for_static_code_analysis)
[^3]:[sourcemonitor](https://www.derpaul.net/SourceMonitor/)
