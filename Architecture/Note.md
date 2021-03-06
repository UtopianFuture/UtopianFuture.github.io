## Note

**一个操作系统包含了内核**（是一个提供硬件抽象层、磁盘及文件系统控制、多任务等功能的系统软件）**以及其他计算机系统所必须的组件**（如函数库、编译器、调式工具、文本编辑器、网站服务器，以及一个 Unix 的使用者接口（Unix shell）等，这些都是操作系统的一部分，而且每一个模块如编译器都是一个单独的进程，运行在操作系统中）。**所以一个内核不是一套完整的操作系统，拿 Linux 来说，**Linux 这个词本身只表示 Linux 内核，但现在大家已经默认的把 Linux 理解成整个 Linux 系统，这是由于历史原因造成的，也就是说人们已经习惯了用 Linux 来形容整个基于 Linux 内核，并且使用 GNU 工程各种工具和应用程序的操作系统（也被称为 GNU/Linux），而基于这些组件的 Linux 软件被称为 Linux 发行版。一般来讲，一个 Linux 发行版本出来包括 Linux 内核之外，还包含大量的软件（套件），比如软件开发工具，数据库，Web 服务器（例如 Apache），X Window，桌面环境（比如 GNOME 和 KDE），办公套件（比如 OpenOffice、org）等等。

处理器会迭代更新，但是不可能操作系统也跟着更新，那么应该是这样的：操作系统在开机初始化的时候，会根据接口去将所有的硬件都加载上，而硬件上保存有该硬件的基本信息。

CPU 依靠指令来计算和控制系统，每款 CPU 在设计时就规定了一些列与其硬件电路相配合的指令系统，或者说某款 CPU 的硬件设计其实就是针对某个指令集的一种硬件实现。指令集也就是所谓的目标代码（或称为机器代码，是可以直接在 CPU 上运行的代码）可以看作是要求 CPU 对外提供的功能，某款 CPU 的设计肯定是朝着某个指令集来的。所以，不同的 CPU 架构，也表示这他的指令集要么较之前的指令集有所拓展或者就是实现了一种全新的指令集。指令集中的一条指令，就是让 CPU 完成一系列的动作，而该动作的完成则表明了某种运算的完成。一个功能可能需要一条或几条指令来实现。比如汇编的 mov 或者 ld 语句就可能对应着几条 CPU 指令。

不同的 CPU 所实现的指令集不同，不同的指令集对应的编译器也不尽相同，编译器不同，相同的高级语言程序经过编译后所得到的二进制代码也不同。Windows 操作系统只能够运行在 X86 架构的 CPU 上，不能运行在 Power 或 ARM 上，因为指令集不同，但其实可以移植，只要通过编译器编译后的二进制代码能在目标 CPU 上运行即可，但 Windows 是封闭的，得不到源代码，而 MS 自己又没有移植到别的 CPU 平台上的打算，所以就只能在 X86 上运行了。Linux 可以运行在不同架构的 CPU 上，因为 Linux 是开源的，因此就可以将它移植到不同的 CPU 上，然后再用相应的编译器编译，就得到了可以在该 CPU 上运行的二进制代码了。有的编译器是有硬件厂家提供的，比如 Intel 就提供 C 和 C++的编译器，这样编译出来的程序就能更好的利用硬件的性能。

何谓计算机体系结构？

计算机系统分为软件和硬件，组成原理这门课是介绍硬件可以分为哪些部分以及这些硬件大致是怎样工作的，以什么样的方式链接起来。而体系结构是软件与硬件之间的接口，它研究的是硬件要怎样组织、怎样优化才能发挥更好的性能、满足需求、和软件一起发挥更好的功能。

思考一个问题：苹果的机器（intel 的 cpu）上装 win 系统性能会不会下降。要从系统与 cpu 之间的关系来想，这其中涉及到涉及到编译器，指令集及特有的硬件。

目前查到的资料是说影响不大，也能跑（都是用的 intel 的芯片），但是由于苹果机器上除了 cpu 之外的硬件还是自己的，而 Mac os 肯定会针对这些硬件做优化的，从而能够达到更高的性能，更低的功耗，装 win 的话就达不到了。未来苹果要用自己的 cpu 了，差别肯定更大了。

总结一下程序在整个计算机系统中的执行流程：程序要用高级语言编写，高级语言在创造的时候肯定没想要去适配硬件，而是要更好的解决业务需要，那么要靠编译器使得程序能够在硬件上运行。编译器通过预编译、汇编来将程序编译成目标代码，之后通过链接器来调用相关的依赖库和依赖函数形成可执行代码。那么相关的函数怎样就可以调用系统资源来完成任务呢？在不同的系统实现方式略有不同。以 Linux 系统为例，每一个高级语言的函数都要使用系统内核提供的系统调用来使用系统资源，而不是直接使用，这样为了系统安全，也方便管理硬件资源。那么调用系统调用是个什么过程呢？系统调用也不是直接就可以用的，在高级语言的函数和系统调用之间还有一个 runtime 层。这是为什么呢？系统调用很依赖硬件实现，如果发生寄存器改变，堆栈名改名等等问题，那么系统调用也要改变，如果高级语言的函数直接调用系统调用，那么一旦系统调用改变了，很多程序就不能正常运行了。runtime 层保证提供给高级语言的接口长时间不会改变，系统调用改变了，只需要改变 runtime 层与系统之间的接口就好了。那么系统调用是怎样就可以使用系统资源完成操作呢？系统调用也是 C 语言写的，但是它的底层实现是汇编代码完成的，而汇编代码的指令是由指令集规定的，包括 ALU 运算、数据存取、控制指令等。我们所说的 x86 架构和 ARM 架构就是它们的指令集不同，从而导致他们的寻址方式不同，指令格式不同，分支预测不同。那么对应的底层的硬件就不同，如通用寄存器数量不同。这样形成的目标代码就不一样。形成可执行代码要怎样在硬件上执行呢？每个程序都有自己的地址空间，这个地址空间里存放二进制代码，这个可执行代码不是简单的二进制代码，它有不同的段，如 .data, .code 等等，同时还用程序入口函数，这些不同的段、函数都有自己的地址，当 CPU 要执行这个进程时，会获取到入口函数的地址，之后就按照代码正常的执行。这中间会涉及到中断，包括数据读取、异常等，这就需要用到不同的寄存器用于保存变量、环境参数等；需要总线传输数据；需要操作系统分配资源。大致流程就是这样，其中的具体实现还需要多研究。接下来是还存在的问题：（1）编译器应该是不同的系统用的编译器不一样，但为什么 Linux, Mac os, win 都是用的 gcc 编译器。

目前的二进制翻译是在程序编译形成二进制可执行码进行反汇编，形成源平台的汇编代码，再进行翻译，形成目的平台的汇编代码，再进行正常的编译执行。这样的话性能很低，能不能在龙芯的机器上加一个中间层，然后当程序调用系统调用时通过中间层去调用。这样不行，首先没有 win 的环境跟本就装不了程序，别说执行了。那要怎么办呢？《Machine-Adaptable Dynamic Binary Translation》这篇文章看看能不能找到答案。

SPARC 是 risc 架构，Solaris 系统是基于 unix 的 x86 架构，那为什么 SPARC 能和 Solaris 百分百兼容。龙芯应该是进程级和系统级翻译都要做。

Memory Hierarchy Design 这章详细的介绍了存储结构，尤其是介绍了 cahce on-chip 的优化方式，实现细节。总结起来有两点需要主要：（1）乱序执行的指令级并行和线程级并行能够掩盖访存延迟的缺点（需要搞清楚为什么）；（2）Dennard scaling 和 Moore's Law 减缓和失效，使得未来要提高计算机的性能只要在计算机体系结构和相关的软件架构上。

看 Memory Hierarchy Design 这章主要了解了现在主流使用的存储器，还有 CACHE 的结构，以及怎样优化 CACHE 的性能，还有认识到了虚拟机技术的重要作用，之前都是大致了解，约等于不了解，现在要进一步搞懂。至于 ARM Cortex 和 Intel 的 CACHE 的实现细节真的看不懂，或者说要花很多时间才能搞懂，这不是我目前需要花精力的，之后有需要再看。

选中分支个未选中分支应该是这样理解的：比如 if 语句，如果判断为 true，就是未命中，继续执行下一条指令，如果判断为 false，就是命中了，跳过下一条指令，转移到目标地址，而且要等到 ID 段执行完才知道判断的结果。

无条件跳转指令必然会跳转，而条件跳转指令有时候跳转,有时候不跳转，一种简单的预测方式就是根据该指令上一次是否跳转来预测当前时刻是否跳转。如果该跳转指令上次发生跳转,就预测这一次也会跳转,如果上一次没有跳转,就预测这一次也不会跳转。这种预测方式称为：1 位预测(1- bit prediction)。2 位预测(2- bit predictor)。每个跳转指令的预测状态信息从 1bit 增加到 2bit 计数器,如果这个跳转执行了,就加 1，加到 3 就不加了，如果这个跳转不执行，就减 1，减到 0 就不减了，当计数器值为 0 和 1 时，就预测这个分支不执行，当计数器值为 2 和 3 时，就预测这个分支执行。2 位的计数器比 1 位的计数器拥有更好的稳定性。

 ![note-1.jpg](https://github.com/UtopianFuture/UtopianFuture.github.io/blob/master/image/note-1.jpg?raw=true)

根据某条件跳转指令以往的跳转记录来做出预测的方法，称为动态预测，而根据某些规律直接写死的预测则成为静态预测。动态预测需要记录每一条曾经执行过的条件跳转指令所在的地址以及以往的跳转结果，这个跳转结果和下文的跳转范式不同，它是 nbit predictor，就是 13 中描述的。这些信息被记录在 PHT（Pattern History Table）上。每条跳转指令位于指令存储器中的位置是不变的，但这并不意味着每条跳转指令只能执行一次，有些循环代码，会导致同一条跳转指令多次执行，但不管执行多少次，其所处的位置是不变的。

![note-2.jpg](https://github.com/UtopianFuture/UtopianFuture.github.io/blob/master/image/note-2.jpg?raw=true)

如果使用多位分别记录每次跳转的结果，例如使用给 10bit，那么每个 bit 就记录每次跳转的结果，然后比较其中 1 和 0 的比例，如果 1 多，就跳转，反之亦然。针对每条跳转指令，使用 8 位移位寄存器来记录其跳转指令，初始值为 0000000000，某时刻跳转了一次，将其更新为 0000000001，下次执行时如果又跳转了，更新为 0000000011，第三次执行时没有跳转，更新为 0000000110，以此类推。更新是通过移位来实现的，并不谁第一次执行将第 0 位置为 1 这样的形式。这个用于保存跳转历史记录的寄存器被称为 BHR（Branch History Register），如果追踪多条跳转执行，就形成 BHT（Binary History Table）。从 10bit 的记录中，可以观察到该指令前 10 次跳转的范式/关联模式，比如范式 0101010101 表示隔一次跳转，0011001100 表示隔两次跳转两次。根据这些范式，就能从更长远的执行中分析程序的执行规律，从而创造更好的分支预测器。

关联预测（corrrlating predictors）或二级预测器（two-level prodictors）：表示法（m, n）。m 表示记录某跳转指令前几次的跳转结果，如 15 中描述的 10bit，n 表示用几位去记录某一范式的执行次数。每个范式都需要用 n bits 记录，那么 （m, n）预测器需要：2^m * n * number of prediction entries selected by the branch address 位额外的硬件。 用 PC 中的某几位（比如 4 位）来定位到 PHT 中的某个部分，（如果 PHT 有 256 行，4 位就能将搜索的范围缩小到 16 行），然后再用 BHR（范式）中的位来定位确定的行，这样不同指令预测到同一地址的概率就会小。 （还有些地方不清楚，如什么是局部预测；具体的预测过程。需要再看一遍《大话计算机》）

流水线深度越深，CPU 的主频越高，是因为流水线越深，每一个电路的设计就越简单，就越容易工作在高频环境下。但同时计算机的性能取决于 CPI 和时钟频率的乘积，所以高时钟频率情况下要保持低 CPI 才能获得高性能。

ABI（应用程序二进制接口）：定义了在该体系架构上编译应用程序所需要遵循的一套规则。主要包括：基本数据类型、通用寄存器的使用、参数的传递规则以及堆栈的使用等等。一个完整的 ABI，像 Intel 二进制兼容标准（iBCS），允许支持他的操作系统上的程序在不经修改的情况下在其他支持此 ABI 的 OS 上运行。比如，内核系统调用用哪些寄存器或者干脆用堆栈来传递参数，返回值又是通过哪个寄存器传递回去，内核里面定义的某个结构体的某个字段偏移是多少等等，这些都是二进制层面上的接口。这些接口是直接给编译好的二进制用的。换句话说，如果 ABI 保持稳定的话，你在之前版本上编译好的二进制应用程序、内核模块，完全可以无须重新编译直接在新版本上运行。而 API 定义的是源代码和库之间的接口。

计算机科学领域的任何问题都可以通过增加一个间接的中间层来解决。

API 的提供者就是运行库，什么样的运行库提供什么样的 API，比如 Linux 下的 Glibc 库提供 POSIX 的 API，Windows 的运行库提供 Windows API，最常见的 32 位 Windows 提供的 API 又被称为 Win32。

 ![note-3.png](https://github.com/UtopianFuture/UtopianFuture.github.io/blob/master/image/note-3.png?raw=true)

虚拟化技术的实现形式是在系统中加入一个虚拟化层，虚拟化层将下层的资源抽象成另一形式的资源，提供给上层使用。通过空间上的分割、时间上的分时以及模拟，虚拟化可以将一份资源抽象成多份，反过来，虚拟化也可以将多份资源抽象成一份。即虚拟化可以把一个纷繁复杂、无计划性的世界改造成一个似乎是为人们的特定需求而量身订造的世界。

在虚拟环境里，VMM 抢占了 OS 的位置，变成了硬件的管理者，同时向上层的软件呈现出虚拟的硬件平台。

系统中有一些操作和管理关键系统资源的指令被称为特权指令。这些指令只有在最高特权级上才能执行，如果在非最高特权级上执行，特权指令会引发一个异常，处理器会陷入到最高特权级，交由系统软件来处理。在虚拟化的世界里，还有一类指令被称为敏感指令，即操作特权资源的指令。包括修改虚拟机的运行模式或者下面物理机的状态；读写敏感的寄存器或内存，如时钟或者中断寄存器；访问存储保护系统、内存系统或地址重定位系统以及所有的 I/O 指令。显而易见，所有的特权指令都是敏感指令，但不是所有的敏感指令都是特权指令。

VM 的敏感指令都要在 VMM 的监控下或者通过 VMM 执行。如果一个系统上所有的敏感指令都是特权指令，那么只需要将 VMM 运行在系统最高特权级，当 VM 操作系统因执行敏感指令（特权指令）而陷入到 VMM 时，VMM 模拟执行引起异常的敏感指令，这种方法被称为“陷入再模拟”，这样的结构就称为“可虚拟化结构”。否则，无法支持再所有的敏感指令上触发异常（因为不是特权指令，无法陷入到最高特权级进行执行），就是“不可虚拟化结构”，我们称其存在“虚拟化漏洞”。所以，判断一个结构是否可虚拟化，关键就在于该结构对敏感指令的支持。

体系结构软硬件不能完全分家，有对硬件的深刻理解才能做好软件，尤其系统级软件。

### 注意

这些笔记是研一看书是记录的，当时用 OneNode，现在统一搬到这里。
