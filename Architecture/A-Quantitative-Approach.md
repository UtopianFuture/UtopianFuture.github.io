## 量化研究方法

### 1:  Fundamentals of Quantitative Design and Analysis

#### 1.1 Instruction

Computer technology has made incredible progress in the roughly 70 years since the first general-purpose electronic computer was created.  The effect of this dramatic growth rate was fourfold:

It has significantly enhanced the capability available to computer. Computer performance is much better.

It led to new  classes of computers. PC, workstations, mobile phone.

Improvement of semiconductor manufacturing as predicted by Moore's law has led to the dominance of microprocessor-based computers across the entire range of computer design.

Software development. More and more languages like C, C++, Java, JavaScript and Python to make programming more productive.

However, we can't sustain the higher rate of performance improvement anymore. The fundamental reason is the two characteristics of semiconductor process that were true for decades no longer hold.

In 1974 Robert Dennard observed that power density was constant for a given area of silicon even as you increased the number of transistors because of smaller dimensions of each transistor. **Dennard scaling** ended around 2004 because current and voltage couldn't keep dropping and still maintain the dependability of integrated circuits.

This change forced the microprocessor designers to use multiple efficient processors or cores instead of a single inefficient processor. Designers switch from relying solely on ILP to DLP, TLP and RLP, it also requiring the restructuring of the application  so that it can exploit explicit parallelism.

The end of Moore's Law.

The only path left to improve energy-performance-cost is specialization. Future microprocessors will include several domain-specific cores that perform only one class of computations well, but they remarkably better than general-purpose cores.

Summarize: the end of **Dennard scaling** and **Moore's Law** force designers to exploit more parallelism and **domain-specific architecture**. This is the first reason why the authors say the next decade is the golden age of **computer architecture**. So what should we do in this age?

#### 1.2 Defining Computer Architecture

**Instruction set architecture**(ISA) serves as the boundary between the software and hardware. The seven dimensions below is what we should care about when we design or study a ISA.

**Class of ISA**. The two popular versions of this class are register-memory ISAs, such as 80x86, which can access memory as part of many instructions, and load-store ISAs, such as ARMv8 and RISC-V, which can access memory only with load or store instructions. All ISAs announced since 1985 are load-store, even 80x86 use micro-operation in underlying to get better pipeline performance.

**Memory addressing**. Using byte addressing to access memory operands. ARMv8 require that objects must be aligned, the 80x86 and RISC-V don't need alignment, but accesses are generally faster if operands are aligned.

**Addressing modes**. It specify the address of a memory object. RISC-V addressing modes are Register, Immediate, and Displacement, where a constant offset is added to a register to form the memory address.

**Types and sizes of operands**. Like most ISAs, 80x86, ARMv8, and RISC-V support operand sizes of 8-bit, 16-bit, 32-bit, 64-bit and IEEE 754 floating point in 32-bit and 64-bit. The 80x86 also support 80-bit floating point.

**Operations**. Data transfer, arithmetic logic, control, floating point.

**Control flow instruction**. Virtually all ISAs, including these three, support conditional branches, unconditional jumps, procedure calls and return. All three use PC-relative addressing, where the branch address is specified by an offset plus PC.

**Encoding an ISA**. There are two basic choices on encoding: **fixed length**(ARMv8 and RISC-V instructions are 32-bit fixed length, which simplifies instruction decoding) and **variable length**(80x86, ranging from 1 to 18 bytes, which can take less space).

There are some definition we should clear.

**Organization**. The high-level aspects of a computer's design, such as the memory system, the memory interconnect, and the design of internal process or CPU. I think it more like definition.

**Hardware**. The specifics of computer, including the detailed logic design and the packaging of the computer. It's implement.

**Architecture**. Cover all three aspects of computer design -- ISA, organization and hardware.

#### 1.3 Trends in Technology

If an ISA is to prevail, it must be designed to survive rapid development in computer technology. Five implementation technologies, which change at a dramatic pace, are critical to modern implementation.

Integrated circuit logic technology. The Moore's Law almost end, what next?

Semiconductor DRAM. Main memory.

Semiconductor Flash. The standard storage device in PMDs.

Magnetic disk technology. This technology is central to server- and warehouse-scale storage.

Network technology. Network  performance depends both on the performance of switched and transmission system.

Although technology improves continuously, the impact of these increases can be in discrete leaps, as a threshold that allows a new capability is reached.

#### 1.4 Trends in Power and Energy in Integrated Circuits

**Power and Energy: A Systems Perspective**

There are three primary concerns.

What is the maximum power a processor ever requires?

Meeting this demand to ensure correct operation in anytime. If a processor attempts to draw more power than a power-supply system can provide,

What is the sustained power consumption?

This metric is widely called the **thermal design power**(TDP), because it determines the cooling requirements. The typical power-supply system is typically sized to exceed the TDP, and a cooling system is usually designed to match or exceed TDP. The primary legitimate use is as a constrain is that an air-cooled chip might be limited to 100W.

The energy and energy efficiency.

In general, energy is always a better metric for comparing system. In particular, the energy to complete a workload is equal to the average power times the execution time for the workload.

**Energy and Power Within a Microprocessor**

For CMOS chips, the traditional primary energy consumption has been in switching transistors, also called **dynamic energy**. The energy required per transistor:

Energydynamic ∝ Capacitive load * Voltage^2

This equation is the energy of pulse of the logic transition of 0 -> 1 -> 0 or 1 -> 0 ->1. The energy of a single transition (0 -> 1 or 1 -> 0) is then:

Energydynamic ∝  1/2Capacitive load * Voltage^2

The power required per transistor is just the product of the energy of a transition multiplied by the frequency of transitions:

Energydynamic ∝  1/2Capacitive load * Voltage^2 * Frequency switched

For a fixed task, slowing clock rate reduces power, but not energy. So that, why we need high clock rate, the clock rate don't effect the energy, so don't effect the execution time of the task. I think the average power is down, the execution time is up.

And from the equation, we know that is we can't reduce voltage or increase power per chip, the clock frequency growth to slow down and the energy efficiency is limited. But the typical air-cooled power is limited to 100W. Therefore modern microprocessors offer many techniques to try to improve energy efficiency:

**Do nothing well**. Turning off the clock of inactive modules to save energy and dynamic power. For example, if no floating-point instructions are executing, the clock of the floating-point unit is disabled.

**Dynamic voltage-frequency scaling(DVFS)**. Modern microprocessors typically offer a few clock frequencies and voltages in which to operate that use lower power and energy.

**Design for the typical case**. Given that PMDs and laptops are often idle, memory and storage offer low power modes to save energy.

**Overlocking(超频）**. The **Turbo mode** offered by Intel.

Although dynamic power is traditionally thought of as the primary source of power dissipation in CMOS, static power is becoming an important issue because leakage current flows even when a transistor is off:

Powerstatic ∝  Currentstatic * Voltage

That is, static power is proportional to the number of devices.

The importance of power and energy has increased the scrutiny on the efficiency of an innovation, so the primary evaluation now is tasks per joule or performance per watt, contrary to performance per mm^2 of silicon as in the past. The new  design principle has inspired a new direction for computer architecture -- domain-specific processors.

Domain-specific processors save energy by reducing wide floating-point operations and deploying special-purpose memories to reduce accesses to DRAM. They use those saving to provide 10–100 more (narrower) integer arithmetic units than a traditional processor. Although such processors perform only a limited set of tasks, they perform them remarkably faster and more energy efficiently than a general-purpose processor. This is the second reason why the authors say the next decade is the golden age of **computer architecture**.

#### 1.5 Quantitative Principles of Computer Design

This section is exploring guidelines and principles that are useful in the design and analysis of computers.

**Take Advantages of Parallelism**.

**Principle of Locality**.

**Focus on the Common Case**.

**Amdahl's Law**. Amdahl's Law use **speedup** to states that the performance improvement to be gained from using some faster mode of execution is limited by the fraction of the time the faster mode can be used(this law we only need know how to use it and how it can guide out design).

Speedup = Performance for entire task using the enhancement when possible / Performance for entire task without using the enhancement

Performance = 1 / execution time

The enhancement is depends on two factors:

Fractionenhanced. **The fraction of the computation time in the original computer that can be converted to take advantage of the enhancement**.

Speedupenhanced. **The improvement gained by the enhanced execution mode, that is, how much faster the task would run if the enhanced mode were used for the entire program**.

The execution time using the original computer with the enhanced mode:

Execution timenew = Execution timeold * ((1 - Fractionenhanced) + Fractionenhanced / Speedupenhanced)

Speedupoverall = Execution timeold / Execution timenew = 1 / ((1 - Fractionenhanced) + Fractionenhanced / Speedupenhanced)

### 2: Memory Hierarchy Design

#### 2.1 Instruction

The goal of the memory hierarchy designis to provide a memory system with a cost per byte that is almost as low as the cheapest level of memory and a speed almost as fast as the fastest level.

Traditionally, designers of memory hierarchies focused on optimizing average memory access time, which is determined by the cache access time, miss rate, and miss penalty. More recently, however, power has become a major consideration. The problem is even more acute in processors in PMDs where the CPU is less aggressive and the power budget may be 20 to 50 times smaller. In such cases, the caches can account

for 25% to 50% of the total power consumption.

To gain insights into the causes of high miss rates, which can inspire better cache designs, the three Cs model sorts all misses into three simple categories:

**Compulsory.** The very first access to a block cannot be in the cache, so the block must be brought into the cache. Compulsory misses are those that occur even if you were to have an infinite-sized cache.

**Capacity**. If the cache cannot contain all the blocks needed during execution of a program, capacity misses (in addition to compulsory misses) will occur because of blocks being discarded and later retrieved.

**Conflict**. If the block placement strategy is not fully associative, conflict misses (in addition to compulsory and capacity misses) will occur because a block may be discarded and later retrieved if multiple blocks map to its set and accesses to the different blocks are intermingled.

There are six basic cache optimization, Note that each of the six optimizations has a potential disadvantage that can lead to increased, rather than decreased, average memory access time.

**Larger block size to reduce miss rate**. The simplest way to reduce the miss rate is to take advantage of spatial locality and increase the block size. Larger blocks reduce compulsory misses, but they also increase the miss penalty.

**Bigger caches to reduce miss rate**. Reduce capacity misses. Drawbacks include potentially longer hit time of the larger cache memory , higher cost and power, increase both static and dynamic power.

**Higher associativity to reduce miss rate**. Reduces conflict misses. Greater associativity can come at the cost of increased hit time and also increases power consumption.

**Multilevel caches to reduce miss penalty**.  Multilevel caches are more power-efficient than a single aggregate cache.  the average memory access time:

Hit timeL1 + Miss rateL1 * (Hit timeL2 + Miss rateL2 * Miss penaltyL2)

**Giving priority to read misses over writes to reduce miss penalty**. Write buffers create hazards because they hold the updated value of a location needed on a read miss. One solution is to check the contents of the write buffer on a read miss. If there are no conflicts, and if the memory system is available, sending the read before the writes reduces the miss penalty.

**Avoiding address translation during indexing of the cache to reduce hit time**. Caches must cope with the translation of a virtual address from the processor to a physical address to access memory.

#### 2.2 Memory Technology and Optimization

(暂时不做详细了解）

#### 2.3 Ten Advanced Optimizations of Cache Performance

There are five metrics to evaluating the performance of the 10 advanced cache optimizations:

**Reducing the hit time**. Small and simple first-level caches and way-prediction.Both techniques also generally decrease power consumption.

**Increasing cache bandwidth**. Pipelined caches, multibanked caches, and nonblocking caches. These techniques have varying impacts on power consumption.

**Reducing the miss penalty**. Critical word first and merging write buffers. These optimizations have little impact on power.

**Reducing the miss rate**. Compiler optimizations. Obviously any improvement at compile time improves power consumption.

**Reducing the miss penalty or miss rate via parallelism**. Hardware prefetching and compiler prefetching. These optimizations generally increase power consumption, primarily because of prefetched data that are unused.

**First Optimization: Small and Simple First-Level Caches** **to Reduce Hit Time and Power**

Although the total amount of on-chip cache has increased dramatically with new generations of microprocessors, because of the clock rate impact arising from a larger L1 cache, the size of the L1 caches has recently increased either slightly or not at all. In many recent processors, designers have opted for more associativity rather than larger caches. There are three other factors that have led to the use of higher associativity in first-level caches despite the energy and access time costs.

many processors take at least 2 clock cycles to access the cache and thus the impact of a longer hit time may not be critical.

keep the TLB out of the critical path, almost all L1 caches should be virtually indexed.(这个不懂)

with the introduction of multithreading, conflict misses can increase, making higher associativitymore attractive.

Those two figure are the relative access time and energy with cache size and associativity.

![quantitative-1.jpg](https://github.com/UtopianFuture/UtopianFuture.github.io/blob/master/image/quantitative-1.jpg?raw=true)

![quantitative-2.jpg](https://github.com/UtopianFuture/UtopianFuture.github.io/blob/master/image/quantitative-2.jpg?raw=true)

**Second Optimization: Way Prediction to Reduce Hit Time**

In way prediction, extra bits are kept in the cache to predict the way (or block within the set) of the next cache access.

Simulations suggest that set prediction accuracy is in excess of 90% for a two-way set associative cache and 80% for a four-way set associative cache, with better accuracy on I-caches than D-caches.

For very fast processors, it may be challenging to implement the one-cycle stall that is critical to keeping the way prediction penalty small.

**Third Optimization: Pipelined Access and Multibanked** **Caches to Increase Bandwidth**

These optimizations increase cache bandwidth either by pipelining the cache access or by widening the cache with multiple banks to allow multiple accesses per clock. Pipelining L1 allows a higher clock cycle, at the cost of increased latency.

To handle multiple data cache accesses per clock, we can divide the cache into independent banks, each supporting an independent access. Multibanking can also reduce energy consumption.

**Fourth Optimization: Nonblocking Caches** **to Increase Cache Bandwidth**

A nonblocking cache or lockup-free cache escalates the potential benefits of such a scheme by allowing the data cache to continue to supply cache hits during a miss.

Although nonblocking caches have the potential to improve performance, they are nontrivial to implement. Two initial types of challenges arise:

**Arbitrating contention between hits and misses**. Hits can collide with misses returning from the next level of the memory hierarchy.  These collisions must be resolved, usually by first giving priority to hits over misses, and second by ordering colliding misses (if they can occur).

**Tracking outstanding misses so that we know when loads or stores can proceed**. When a miss returns, the processor must know which load or store caused the miss, so that instruction can now go forward; and it must know where in the cache the data should be placed (as well as the setting of tags for that block). Now, we use the Miss Status Handling Registers (MSHRs) to store these informations.

**Fifth Optimization: Critical Word First and** **Early Restart to Reduce Miss Penalty**

This technique is based on the observation that the processor normally needs just one word of the block at a time. This strategy is impatience: don't wait for the full block to be loaded before sending the requested word and restarting the processor. Here are two specific strategies:

**Critical word first**. Request the missed word first from memory and send it to the processor as soon as it arrives; let the processor continue execution while filling the rest of the words in the block.

**Early restart**. Fetch the words in normal order, but as soon as the requested word of the block arrives, send it to the processor and let the processor continue execution.

Generally, these techniques only benefit designs with large cache blocks because the benefit is low unless blocks are large.

**Sixth Optimization: Merging Write Buffer** **to Reduce Miss Penalty**

![quantitative-3.jpg](https://github.com/UtopianFuture/UtopianFuture.github.io/blob/master/image/quantitative-3.jpg?raw=true)

This optimization uses the memory more efficiently because multiword writes are usually faster than writes performed one word at a time.

**Seventh Optimization: Compiler Optimizations** **to Reduce Miss Rate**

**Loop interchange**. Reordering the loop, so that access data in memory in sequential order.

**Blocking**.  Instead of operating on entire rows or columns of an array, blocked algorithms operate on submatrices or blocks. The goal is to maximize accesses to the data loaded into the cache before the data are replaced. To ensure that the elements being accessed can fit in the cache, the original code is changed to compute on a submatrix of size B by B. B is called the blocking factor.

![quantitative-4.jpg](https://github.com/UtopianFuture/UtopianFuture.github.io/blob/master/image/quantitative-4.jpg?raw=true)

![quantitative-5.jpg](https://github.com/UtopianFuture/UtopianFuture.github.io/blob/master/image/quantitative-5.jpg?raw=true)

**Eighth Optimization: Hardware Prefetching of Instructions** **and Data to Reduce Miss Penalty or Miss Rate**

Typically, the processor fetches two blocks on a miss: **the requested block** and **the next consecutive** **block.** The requested block is placed in the instruction cache when it returns, and the prefetched block is placed in **the instruction streambuffer**. If the requested block is present in the instruction stream buffer, the original cache request is canceled, the block is read from the stream buffer, and the next prefetch request is issued.

Prefetching relies on utilizing memory bandwidth that otherwise would be unused, but if it interferes with demand misses, it can actually lower performance.

**Ninth Optimization: Compiler-Controlled Prefetching to Reduce Miss Penalty or Miss Rate(****这个优化没懂****)**

An alternative to hardware prefetching is for the compiler to insert prefetch instructions to request data before the processor needs it. There are two flavors of prefetch:

Register prefetch loads the value into a register.

Cache prefetch loads data only into the cache and not the register.

**Tenth Optimization: Using HBM to Extend** **the Memory Hierarchy**

The inpackage DRAMs be used to build massive L4 caches, with upcoming technologies ranging from 128 MiB to 1 GiB and more, considerably more than current on-chip L3 caches.

The tag storage is the major drawback for using a smaller block size. Suppose we were to use a 64B block size; then a 1 GiB L4 cache requires 96 MiB of tags—far more static memory than exists in the caches on the CPU. There are three solutions for this problem:

**LH-cache**.

**SRAM-Tags**.

**Alloy cache**.

![quantitative-6.jpg](https://github.com/UtopianFuture/UtopianFuture.github.io/blob/master/image/quantitative-6.jpg?raw=true)

#### 2.4 Virtual Memory and Virtual Machines

A virtual machine is taken to be an efficient, isolated duplicate of the real machine. We explain these notions through the idea of a virtual machine

monitor (VMM)…… a VMM has three essential characteristics. First, the VMM provides an environment for programs which is essentially identical with the original machine; second, programs run in this environment show at worst only minor decreases in speed; and last, the VMM is in complete control of system resources.

​                                                                                             Gerald Popek and Robert Goldberg

Virtualization has an important function -- protection and privacy.

TLB is the cornerstone of virtual memory, a TLB entry is like a cache entry where the tag holds portions of the virtual address and the data portion holds a physical page address, protection field, valid bit, and usually a use bit and a dirty bit.

**Protection via Virtual Memory**

Page-based virtual memory, including a TLB that caches page table entries, is the primary mechanism that protects processes from each other. Multiprogramming, where several programs running concurrently share a computer, has led to demands for protection and sharing among processes.

To do this, the architecture must limit what a process can access when running a user process yet allow an operating system process to access more. At a minimum, the architecture must do the following:

Provide at least two modes.

Provide a portion of the processor state that a user process can use but not write.

Provide mechanisms whereby the processor can go from user mode to supervisor mode and vice versa.

Provide mechanisms to limit memory accesses to protect the memory state of a process without having to swap the process to disk on a context switch.

**Protection via Virtual Machines**

Virtual machines have recently gained popularity because of

 the increasing importance of isolation and security in modern systems;

 the failures in security and reliability of standard operating systems;

the sharing of a single computer among many unrelated users, such as in a data center or cloud; and

the dramatic increases in the raw speed of processors, which make the overhead of VMs more acceptable.

The virtual machine is an independent machine, a complete OS, but it shows hardware with other VMs. The VMM determines how to map virtual resources to physical resources. if any instruction that tries to read or write such sensitive information traps when executed in user mode, the VMM can intercept it and support a virtual version of the sensitive information as the guest OS expects.

Although our interest here is in VMs for improving protection, VMs provide two other benefits that are commercially significant:

**Managing software**.

**Managing hardware**.

**Impact of Virtual Machines on Virtual Memory and I/O**

For virtualization of virtual memory, there are two methods:

Virtual memory -> real memory -> physical memory. It's simple, but has higher performance loss.

Shadow page table. Maps directly from the guest virtual address space to the physical address space of the hardware. By detecting all modifications

to the guest's page table, theVMMcan ensure that the shadow page table entries being used by the hardware for translations correspond to those of the guest OS environment, with the exception of the correct physical pages substituted for the real pages in the guest tables. This method has lower performance loss, but it's more complexity.

The final portion of the architecture to virtualize is I/O. This is by far the most difficult part of system virtualization because of the increasing number of I/O devices attached to the computer and the increasing diversity of I/O device types. Another difficulty is the sharing of a real device among multiple VMs, and yet another comes from supporting the myriad of device drivers that are required, especially if different guest OSes are supported on the same VM system.

#### 2.5 Putting It All Together: Memory Hierarchies in the ARM Cortex-A53 and Intel Core i7 6700

![quantitative-7.jpg](https://github.com/UtopianFuture/UtopianFuture.github.io/blob/master/image/quantitative-7.jpg?raw=true)

I7 的 cache 图只是熟悉了它的执行流程，至于多少 bits 访问每一级 cache 没有记。需要注意的几点：（1）I/D-cache 和 I/D-TLB 是同时访问的，I/D-TLB 中得到的 physical address 并不会用来在 I/D-cache 搜索；（2）I/D-TLB 是 L2-TLB 的子集，为的是加快访问速度；（3）L2-TLB 命中后要更新 I/D-TLB，然后再去访问 L2 cache。

#### 2.6 Fallacies and Pitfalls

***\*Fallacy\**** **Predicting cache performance of one program from another.**

Generalizing cache performance from one program to another is unwise.

***\*Pitfall\**** **Simulating enough instructions to get accurate performance measures** **of the memory hierarchy.**

There are really three pitfalls here.

Trying to predict performance of a large cache using a small trace.

A program's locality behavior is not constant over the run of the entire program.

A program's locality behavior may vary depending on the input.

### 3: Instruction-Level Parallelism and Its Exploitation

#### 3.1 Instruction-Level Parallelism: Concepts and Challenges

There are two largely separable approaches to exploiting ILP: (1) an approach that relies on hardware to help discover and exploit the parallelism dynamically, and (2) an approach that relies on software technology to find parallelism statically at compile time. Despite enormous efforts, the statical approaches have been successful only in domain-specific environments or in well-structured scientific applications with significant data-level parallelism.

#### 3.2 Basic Compiler Techniques for Exposing ILP

**Basic Pipeline Scheduling and Loop Unrolling**

This is the original loop code:

```c
for (i=999; i>=0; i=i1)
   x[i] = x[i] + s;
```

Without any scheduling, the loop will execute as follows, taking nine cycles:

![quantitative-8.jpg](https://github.com/UtopianFuture/UtopianFuture.github.io/blob/master/image/quantitative-8.jpg?raw=true)

We can schedule the loop to obtain only two stalls and reduce the time to seven cycles:

![quantitative-9.jpg](https://github.com/UtopianFuture/UtopianFuture.github.io/blob/master/image/quantitative-9.jpg?raw=true)

The stalls after fadd.d are for use by the fsd, and repositioning the addi prevents the stall after the fld.

We can copy the loop body four times, so that can eliminate any obviously redundant computations and do not reuse any of the registers.

![quantitative-10.jpg](https://github.com/UtopianFuture/UtopianFuture.github.io/blob/master/image/quantitative-10.jpg?raw=true)

We have eliminated three branches and three decrements of x1.

This is the unrolled loop in the previous example after it has been scheduled.

![quantitative-11.jpg](https://github.com/UtopianFuture/UtopianFuture.github.io/blob/master/image/quantitative-11.jpg?raw=true)

**Summary of the Loop Unrolling and Scheduling**

To obtain the final unrolled code, we had to make the following decisions and transformations:

Determine that unrolling the loop would be useful by finding that the loop iterations were independent, except for the loop maintenance code.

Use different registers to avoid unnecessary constraints(e.g., name dependences).

Eliminate the extra test and branch instructions and adjust the loop termination and iteration code.

Determine that the loads and stores in the unrolled loop can be interchanged by observing that the loads and stores from different iterations are independent.

Schedule the code, preserving any dependences needed to yield the same result as the original code.

 Three different effects limit the gains from loop unrolling:

A decrease in the amount of overhead amortized with each unroll. As we unrolling the loop more and more times, the gain we can get form loop overhead is less.

Code size limitations. For larger loops, the code size growth may causes an increase in the instruction cache miss rate.

Compiler limitations. **The register pressure**, this arises because scheduling code to increase ILP causes the number of live values to increase.

Loop unrolling is a simple but useful method for increasing the size of straightline code fragments that can be scheduled effectively.

#### 3.3 Reducing Branch Costs With Advanced Branch Prediction

Loop unrolling is one way to reduce the number of branch hazards. As the number of instructions in flight has increased with deeper pipelines and more issues per clock, the importance of more accurate branch prediction has grown, we should reduce the performance losses of branches by predicting how they will behave.

**Correlating Branch Predictors**

The 2-bit predictor schemes in Appendix C use only the recent behavior of a single branch to predict the future behavior of that branch. It may be possible to improve the prediction accuracy if we also look at the recent behavior of other branches rather than just the branch we are trying to predict.

根据某条件跳转指令以往的跳转记录来做出预测的方法，称为动态预测，而根据某些规律直接写死的预测则成为静态预测。动态预测需要记录每一条曾经执行过的条件跳转指令所在的地址以及以往的跳转结果，这个跳转结果和下文的跳转范式不同，它是 n-bit predictor，就是 Appendix C 中描述的 2-bit predictor。这些信息被记录在 PHT（Pattern History Table）上。每条跳转指令位于指令存储器中的位置是不变的，但这并不意味着每条跳转指令只能执行一次，有些循环代码，会导致同一条跳转指令多次执行，但不管执行多少次，其所处的位置是不变的。

![quantitative-12.jpg](https://github.com/UtopianFuture/UtopianFuture.github.io/blob/master/image/quantitative-12.jpg?raw=true)

如果使用多位分别记录每次跳转的结果，例如使用给 10 bits，那么每个 bit 就记录每次跳转的结果，然后比较其中 1 和 0 的比例，如果 1 多，就跳转，反之亦然。针对每条跳转指令，使用 10 位移位寄存器（shift register）来记录其跳转指令，初始值为 0000000000（这就是范式，m-bit 就有 2^m 个范式），某时刻跳转了一次，将其更新为 0000000001，下次执行时如果又跳转了，更新为 0000000011，第三次执行时没有跳转，更新为 0000000110，以此类推。更新是通过移位来实现的，并不谁第一次执行将第 0 位置为 1 这样的形式。这个用于保存跳转历史记录的寄存器被称为 BHR（Branch History Register），如果追踪多条跳转执行，就形成 BHT（Binary History Table），这个应该是全局预测表。从 10 bits 的记录中，可以观察到该指令前 10 次跳转的范式/关联模式，比如范式 0101010101 表示隔一次跳转，0011001100 表示隔两次跳转两次。根据这些范式，就能从更长远的执行中分析程序的执行规律，从而创造更好的分支预测器。

关联预测（correlating predictors）或二级预测器（two-level prodictors）：表示法（m, n）。m 表示记录某跳转指令前几次的跳转结果，如上文中描述的 10 bits，n 表示用几位去记录某一范式的执行次数。每个范式都需要用 n bits 记录，那么 （m, n）预测器需要：2^m * n * number of prediction entries selected by the branch address 位额外的硬件。用 PC 中的某几位（比如 4 位）来定位到 PHT 中的某个部分，（如果 PHT 有 256 行，4 位就能将搜索的范围缩小到 16 行），然后再用 BHR（范式）中的位来定位确定的行，这样不同指令预测到同一地址的概率就会小。（还有些地方不清楚，需要再看一遍《大话计算机》）。

**Tournament Predictors: Adaptively Combining Local andGlobal Predictors**

Tournament predictors using multiple predictors, usually a global predictor and a local predictor, and choosing between them with a selector, as shown in this figure. A global predictor uses the most recent branch history to index the predictor, while a local predictor uses the address of the branch as the index. Tournament predictors are another form of hybrid or alloyed predictors.

![quantitative-13.jpg](https://github.com/UtopianFuture/UtopianFuture.github.io/blob/master/image/quantitative-13.jpg?raw=true)

The selector acts like a 2-bit predictor, changing the preferred predictor for a branch address when two mispredicts occur in a row（不懂）.

#### 3.4 Overcoming Data Hazards With Dynamic Scheduling

A simple statically scheduled pipeline fetches an instruction and issues it, unless there is a data dependence between an instruction already in the pipeline and the fetched instruction that cannot be hidden with bypassing or forwarding. If there is a data dependence that cannot be hidden, then the hazard detection hardware stalls the pipeline starting with the instruction that uses the result. No new instructions are fetched or issued until the dependence is cleared.

**Dynamic scheduling**, a technique by which the hardware reorders the instruction execution to reduce the stalls while maintaining data flow and exception behavior. Dynamic scheduling offers several advantages:

Allow code that was compiled with one pipeline in mind to run efficiently on a different pipeline, eliminating the need to have multiple binaries and recompile for a different microarchitecture. In today's computing environment, where much of the software is from third parties and distributed in binary form, this advantage is significant.

Enable handling some cases when dependences are unknown at compile time;

Allow the processor to tolerate unpredictable delays, such as cache misses, by executing other code while waiting for the miss to resolve.

**Dynamic Scheduling: The Idea**

A major limitation of simple pipelining techniques is that they use **in-order** instruction issue and execution: instructions are issued in program order, and if an instruction is stalled in the pipeline, no later instructions can proceed. If we want an instruction to begin execution as soon as its data operands are available, we need an another execution mode, named **out-of-order** **execution**, which implies **out-of-order completion**.

Howover, **Out-of-order execution** will introduces the possibility of WAR and WAW hazards, which do not exist in the five-stage integer pipeline and its logical extension to an in-order floating-point pipeline, both these hazards are avoided by the use of**register renaming.

Out-of-order completion also creates major complications in handling exceptions. Dynamic scheduling with out-of-order completion must preserve exception behavior in the sense that exactly those exceptions that would arise if the program were executed in strict program order actually do arise. Dynamically scheduled processors preserve exception behavior by delaying the notification of an associated exception until the processor knows that the instruction should be the next one completed.

Although exception behavior must be preserved, dynamically scheduled processors could generate **imprecise exceptions**. An exception is imprecise if the

processor state when an exception is raised does not look exactly as if the instructions were executed sequentially in strict program order. Imprecise exceptions can occur because of two possibilities:

The pipeline may have already completed instructions that are later in program order than the instruction causing the exception.

The pipeline may have not yet completed some instructions that are earlier in program order than the instruction causing the exception.

In a dynamically scheduled pipeline, all instructions pass through the issue stage in order (in-order issue); however, they can be stalled or can bypass each other in the second stage (read operands) and thus enter execution out of order.

**Scoreboarding** is a technique for allowing instructions to execute out of order when there are sufficient resources and no data dependences.  Here we focus on a more sophisticated technique, called **Tomasulo****'****s algorithm**. The primary difference is that Tomasulo's algorithm handles antidependences and output dependences by effectively ***\*renaming the registers dynamically\****. Additionally, Tomasulo's algorithm can be extended to handle speculation, a technique to reduce the effect of control dependences by predicting the outcome of a branch, executing instructions at the predicted destination address, and taking corrective actions when the prediction was wrong.

**Dynamic Scheduling Using Tomasulos Approach**

The two key principles of Tomasulo's Approach is dynamically determining when an instruction is ready to execute and renaming registers to avoid unnecessary hazards. Register renaming eliminates WAR and WAW hazards by renaming all destination registers, including those with a pending read or write for an earlier instruction, so that the out-of-order write does not affect any instructions that depend on an earlier value of an operand. The compiler could typically implement such renaming, if there were enough registers available in the ISA.

In Tomasulo's scheme, register renaming is provided by **reservation stations**, which buffer the operands of instructions waiting to issue and are associated with the functional units. The basic idea is that a reservation station fetches and buffers an ***operand*** as soon as it is available, eliminating the need to get the operand from a register. In addition, pending instructions designate the reservation station that will provide their input. Finally, when successive writes to a register overlap in execution, only the last one is actually used to update the register. As instructions are issued, the register specifiers for pending operands are renamed to the names of the reservation station, which provides register renaming.

This figure shows the basic structure of a Tomasulo-based processor, including both the floating-point unit and the load/store unit; none of the execution control tables is shown.

![quantitative-14.jpg](https://github.com/UtopianFuture/UtopianFuture.github.io/blob/master/image/quantitative-14.jpg?raw=true)

Each reservation station has seven fields:

· Op. The **operation** to perform on source operands S1 and S2.

· Qj, Qk. The **reservation stations** that will produce the corresponding source operand; a value of zero indicates that the source operand is already available in Vj or Vk, or is unnecessary.

· Vj, Vk. The value of the **source operands**. Note that only one of the V fields or the Q field is valid for each operand. For loads, the Vk field is used to hold the offset field.

· A. Used to hold information for the **memory address** calculation for a load or store. Initially, the **immediate** field of the instruction is stored here; after the address calculation, the effective address is stored here.

· Busy. Indicates that this reservation station and its accompanying functional unit are **occupied**.

The register file has a field, Qi:

· Qi. **The number of the reservation station** that contains the operation whose result should be stored into this register. If the value of Qi is blank (or 0), no currently active instruction is computing a result destined for this register, meaning that the value is simply the register contents.

#### 3.5 Dynamic Scheduling: Examples and the Algorithm

These figures are the information tables for the following code sequence when only the first load has completed and written its result:

```c
1. fld f6, 32(x2)
2. fld f2, 44(x3)
3. fmul.d f0, f2, f4
4. fsub.d f8, f2, f6
5. fdiv.d f0, f0, f6
6. fadd.d f6, f8, f2
```

![quantitative-15.jpg](https://github.com/UtopianFuture/UtopianFuture.github.io/blob/master/image/quantitative-15.jpg?raw=true)

这种方法是多年前的设计，其设计之初并没有考虑到现在计算机技术会是什么样的，也就是说，他的实现并不是完全适应现在的计算机。但是我们应该考虑怎样将 reservation station 这种设计思想应用于现在的计算机。

#### 3.6 Hardware-Based Speculation

Branch prediction reduces the direct stalls attributable to branches, but for a processor executing multiple instructions per clock, just predicting branches accurately may not be sufficient to generate the desired amount of instruction-level parallelism. A wide-issue processor may need to execute a branch every clock cycle to maintain maximum performance. Thus exploiting more parallelism requires that we overcome the limitation of control dependence.

Overcoming control dependence is done by speculating on the outcome of branches and executing the program as if our guesses are correct. This mechanism represents a subtle, but important, extension over branch prediction with dynamic scheduling.

Hardware-based speculation combines three key ideas:

Dynamic branch prediction to choose which instructions to execute;

Speculation to allow the execution of instructions before the control dependences are resolved (with the ability to undo the effects of an incorrectly speculated sequence);

Dynamic scheduling to deal with the scheduling of different combinations of basic blocks.不懂

Hardware-based speculation follows the predicted flow of data values to choose when to execute instructions. This method of executing programs is essentially a data flow execution: Operations execute as soon as their operands are available.

The key idea behind implementing speculation is to allow instructions to execute out of order but to force them to **commit (When an instruction is no longer speculative, we allow it to update the register file or memory) in order** and to prevent any irrevocable action (such as updating state or taking an exception) until an instruction commits.

Adding this commit phase to the instruction execution sequence requires an additional set of hardware buffers that hold the results of instructions that have finished execution but have not committed. This hardware buffer, which we call the **reorder buffer**, is also used to pass results among instructions that may be speculated. The key difference between Tomasulo's algorithm and ROB is that in Tomasulo's algorithm, once an instruction writes its result, all subsequently issued instructions will find the result in the register file. With speculation, the register file is not updated until the instruction commits; thus, the ROB supplies operands in the interval between completion of instruction execution and instruction commit.

This figure shows the hardware structure of the processor including the ROB. Each entry in the ROB contains four fields:

**The instruction type**. It indicates whether the instruction is a **branch** (and has no destination result), a **store** (which has a memory address destination), or a **register operation** (ALU operation or load, which has register destinations).

**The destination field**. It supplies the register number (for loads and ALU operations) or the memory address (for stores) where the instruction result

should be written.

**The value field**. It's used to hold the value of the instruction result until the instruction commits.

**The ready field**. It indicates that the instruction has completed execution, and the value is ready.

![quantitative-16.jpg](https://github.com/UtopianFuture/UtopianFuture.github.io/blob/master/image/quantitative-16.jpg?raw=true)

Although the renaming function of the reservation stations is replaced by the ROB, we still need reservation stations to buffer operations (and operands) between the time they issue and the time they begin execution. And because every instructionhas a position in the ROB until it commits, we tag a result using **the** **ROB** **entry number** rather than using the reservation station number. This tagging requires that the ROB assigned for an instruction must be tracked in the reservation station.

Here are the four steps involved in instruction execution:

**Issue**.

**Execute**. If one or more of the operands is not yet available, monitor the CDB while waiting for the register to be computed.

**Write result**. When the result is available, write it on the CDB and from the CDB into the ROB, as well as to any reservation stations waiting for this result.

**Commit**.  This is the final stage of completing an instruction, after which only its result remains. There are three different sequences of actions at commit depending on whether the committing instruction is a branch with an incorrect prediction, a store, or any other instruction (normal commit). The normal commit case occurs when an instruction reaches the head of the ROB and its result is present in the buffer; at this point, the processor updates the register with the result and removes the instruction from the ROB. Committing a store is similar except that memory is updated rather than a result register. When a branch with incorrect prediction reaches the head of the ROB, it indicates that the speculation was

wrong. The ROB is flushed and execution is restarted at the correct successor of the branch. If the branch was correctly predicted, the branch is finished.

How the ROB restart at the correct successor of the branch?

In practice, processors that speculate try to recover as early as possible after a branch is mispredicted. This recovery can be done by clearing the ROB for all entries that appear after the mispredicted branch, allowing those that are before the branch in the ROB to continue, and restarting the fetch at the correct branch successor.

Once an instruction commits, its entry in the ROB is reclaimed, and the register or memory destination is updated, eliminating the need for the ROB entry. If the ROB fills, we simply stop issuing instructions until an entry is made free.

These three status tables show the result of the following code, and the fmul.d is ready to go to commit.

```c
fld f6, 32(x2)
fld f2, 44(x3)
fmul.d f0, f2, f4
fsub.d f8, f2, f6
fdiv.d f0, f0, f6
fadd.d f6, f8, f2
```

![quantitative-17.jpg](https://github.com/UtopianFuture/UtopianFuture.github.io/blob/master/image/quantitative-17.jpg?raw=true)

#### 3.7 Exploiting ILP Using Multiple Issue and Static Scheduling

In multiple-issue processors, we can decrease the CPI to less than one, so that improve performance further. Multiple-issue processors come in three major flavors:

Statically scheduled superscalar processors. In-order execution.

VLIW (very long instruction word) processors. Issue a fixed number of instructions formatted either as one large instruction or as a fixed instruction packet with the parallelism among instructions explicitly indicated by the instruction.

Dynamically scheduled superscalar processors. Out-of-order execution.

This figure summarizes the basic approaches to multiple issue and their distinguishing characteristics and shows processors that use each approach(what' s the difference between the **issue structure** and **scheduling**).

![quantitative-18.jpg](https://github.com/UtopianFuture/UtopianFuture.github.io/blob/master/image/quantitative-18.jpg?raw=true)

For this code sequence, if we use VLIW, the issue is 2.5 operations per cycle.

```c
Loop:   fld   f0, 0(x1)
fadd.d   f4, f0, f2
fsd     f4, 0(x1)
addi    x1, x1, -8
bne    x1, x2, Loop
```

![quantitative-19.jpg](https://github.com/UtopianFuture/UtopianFuture.github.io/blob/master/image/quantitative-19.jpg?raw=true)

But for the original VLIW model, there were both technical and logistical problems that made the approach less efficient. The technical problems were the increase in code size and the limitations of the lockstep operation.

Two different elements combine to increase code size substantially for a VLIW. First, generating enough operations in a straight-line code fragment requires ambitiously unrolling loops. Second, whenever instructions are not full, the unused functional units translate to wasted bits in the instruction encoding. Early VLIWs operated in lockstep; there was no hazard-detection hardware at all. This structure dictated that a stall in any functional unit pipeline must cause the entire processor to stall because all the functional units had to be kept synchronized.

#### **3.8 Exploiting ILP Using Dynamic Scheduling, Multiple Issue,** **and Speculation**

In this section, we put the individual mechanisms of dynamic scheduling, multiple issue, and speculation together, which yields a microarchitecture quite similar to those in modern microprocessors.

Issuing multiple instructions per clock in a dynamically scheduled processor (with or without speculation) is very complex for the simple reason that the multiple instructions may depend on one another. Because of this, the tables must be updated for the instructions in parallel; otherwise, the tables will be incorrect or the dependence may be lost.

Two different approaches have been used to issue multiple instructions per clock in a dynamically scheduled processor, and both rely on the observation that the key is assigning a reservation station and updating the pipeline control tables.

One approach is to run this step in half a clock cycle so that two instructions can be processed in one clock cycle; this approach cannot be easily extended to handle four instructions per clock, unfortunately. A second alternative is to build the logic necessary to handle two or more instructions at once, including any possible dependences between the instructions.

A key observation is that we cannot simply pipeline away the problem. By making instruction issues take multiple clocks because new instructions are issuing every clock cycle, we must be able to assign the reservation station and to update the pipeline tables so that a dependent instruction issuing on the next clock can use the updated information.

#### 3.9 Advanced Techniques for Instruction Delivery and Speculation

In a high-performance pipeline, especially one with multiple issues, predicting branches well is not enough; we actually have to be able to deliver a high-bandwidth instruction stream and find a set of key issues in implementing advanced speculation techniques.

**Increasing Instruction Fetch Bandwidth**

**Specialized Branch Predictors: Predicting Procedure Returns,** **Indirect Jumps, and Loop Branches**

**Speculation: Implementation Issues and Extensions**

#### 3.10 Cross-Cutting Issues

**Hardware Versus Software Speculation**

**Speculative Execution and the Memory System**

#### 3.11 Multithreading: Exploiting Thread-Level Parallelism to Improve Uniprocessor Throughput

**Effectiveness of Simultaneous Multithreading on Superscalar** **Processors**

#### 3.12 Putting It All Together: The Intel Core i7 6700 and ARM Cortex-A53

**The ARM Cortex-A53**

**The Intel Core i7**

### Appendix A Instruction Set Principles

#### A.1 Classifying Instruction Set Architecture

| Type                  | Advantages                                                   | Disadvantages                                                | Examples                                               |
| --------------------- | ------------------------------------------------------------ | ------------------------------------------------------------ | ------------------------------------------------------ |
| Register-registerRISC | Simple, fixed-length instruction encoding.Simple code generation model. Instructionstake similar numbers of clocks to execute, effective pipeline. | Higher instruction count than architectures withmemory references in instructions. More instructionsand lower instruction density lead to larger programs,which may have some instruction cache effects | ARM, MIPS, PowerPC, SPARC, RISC-V                      |
| Register-memoryCISC   | Data can be accessed without a separate loadinstruction first. Instruction format tends tobe easy to encode and yields good density | Operands are not equivalent because a sourceoperand in a binary operation is destroyed. Encodinga register number and a memory address in eachinstruction may restrict the number of registers.Clocks per instruction vary by operand location | IBM 360/370, Intel 80x86, Motorola68000, TI TMS320C54x |

RISC ISA can get better ILP, so every ISA create after 1980s is RISC ISA, even 80x86 use hardware to translate from 80x86 instructions to RISC-like instructions(micro-operation) and then execute the translated operations inside the chip.

#### A.2 Memory Addressing

Independent of whether the architecture is load-store or allows any operand to be a memory reference, it must define how memory addresses are interpreted and how they are specified.

**Addressing Modes**

Addressing modes -- how architectures specify the address of an object they will access.

| Addressing mode    | Example instruction | Meaning                                               | When used                                                    |
| ------------------ | ------------------- | ----------------------------------------------------- | ------------------------------------------------------------ |
| Register           | Add R4, R3          | Regs[R4] <- Regs[R4] + Regs[R3]                       | When a value is in a register                                |
| Immediate          | Add R4, 3           | Regs[R4] <- Regs[R4]+3                                | For constants                                                |
| Register indirect  | Add R4, (R1)        | Regs[R4] <- Regs[R4] + Mem[Regs[R1]]                  | Accessing using a pointer or a computed address              |
| Displacement       | Add R4, 100(R1)     | Regs[R4] <- Regs[R4] + Mem[100+Regs[R1]]              | Accessing local variables(+ simulates register indirect, direct addressing modes) |
| Indexed            | Add R3, (R1 + R2)   | Regs[R3] <- Regs[R3] + Mem[Regs[R1]+Regs[R2]]         | Sometimes useful in arrayaddressing: R1 = base of array; R2 = index amount |
| Direct or absolute | Add R1, (1001)      | Regs[R1] <- Regs[R1] + Mem[1001]                      | Sometimes useful for accessing static data; address constant may need to be large |
| Memory indirect    | Add R1, @(R3)       | Regs[R1] <- Regs[R1] + Mem[Mem[Regs[R3]]]             | If R3 is the address of a pointer p, then mode yields *p     |
| Scaled             | Add R1, 100(R2)[R3] | Regs[R1] <- Regs[R1] + Mem[100+Regs[R2]+Regs[R3] * d] | Used to index arrays. May be applied to any indexed addressingmode in some computers |

When we design the addressing modes, we should depend on the real situation, which addressing mode has highest usage frequency in software. And we also need depend on the real software to decide how many bits we should use in each addressing mode.

![quantitative-20.jpg](https://github.com/UtopianFuture/UtopianFuture.github.io/blob/master/image/quantitative-20.jpg?raw=true)

Through this figure, we would expect a new architecture to support at least the following addressing modes: displacement, immediate, and register indirect.

![quantitative-21.jpg](https://github.com/UtopianFuture/UtopianFuture.github.io/blob/master/image/quantitative-21.jpg?raw=true)

Through this figure, we would expect the size of the address for displacement mode to be at least 12-16 bits.

![quantitative-22.jpg](https://github.com/UtopianFuture/UtopianFuture.github.io/blob/master/image/quantitative-22.jpg?raw=true)

Through this figure, we would expect the size of immediate field to be at least 8-16 bits.

#### A.3 Type and Size of Operands

Usually the type of an operand—integer, single-precision floating point, character, and so on—effectively gives its size. Common operand types include character (8 bits), half word (16 bits), word (32 bits), single-precision floating point (also 1 word), and doubleprecision floating point (2 words). But these operands usually store in the register, so we define these operands in the program level in the majority situation.

#### A.4 Operation in the instruction Set

This figure shows 10 simple instructions that account for 96% of instructions executed for a collection of integer programs running on the popular Intel 80x86.

![quantitative-23.jpg](https://github.com/UtopianFuture/UtopianFuture.github.io/blob/master/image/quantitative-23.jpg?raw=true)

![quantitative-24.jpg](https://github.com/UtopianFuture/UtopianFuture.github.io/blob/master/image/quantitative-24.jpg?raw=true)

Through this figure and according the Amdahl's Law, we know which type of operation we should optimize firstly.

#### A.5 Instruction for Control Flow

Because the measurements of branch and jump behavior are fairly independent of other measurements and applications, we now examine the use of control flowinstructions, which have little in common with the operations of the previous sections.

We can distinguish four different types of control flow change:

Conditional branches

Jumps

Procedure calls

Procedure returns

This figure shows the frequencies of these control flow instructions:

![img](file:////tmp/wps-guanshun/ksohtml/wpscb7Tdn.jpg)

Same as before, Through this figure and according the Amdahl's Law, we know why we need research the branch predictor.

**Addressing Modes for Control Flow Instructions**

The destination address of a control flow instruction must always be specified. But there are another two exception that the target address are not know at compile time.

Procedure return. Supply a displacement that is added to the program counter (PC).

Returns and indirect jumps. To implements these, there must be a way to specify the target dynamically, so that it can change at runtime. So we use the register indirect jumps.

![quantitative-26.jpg](https://github.com/UtopianFuture/UtopianFuture.github.io/blob/master/image/quantitative-26.jpg?raw=true)

The most frequent branches in the integer programs are to targets that can be encoded in 4–8 bits.

**Conditional Branch Options****(this subsection don't understand completely, so I record all of it)**

Because most changes in control flow are branches, deciding how to specify the branch condition is important. This figure shows the three primary techniques in use today and their advantages and disadvantages.

| Name                                | Examples                         | How condition is tested                                      | Advantages                                 | Disadvantages                                                |
| ----------------------------------- | -------------------------------- | ------------------------------------------------------------ | ------------------------------------------ | ------------------------------------------------------------ |
| Conditioncode (CC)                  | 80x86, ARM,PowerPC,SPARC, SuperH | Tests special bits set byALU operations, possibly under program control. | Sometimes conditionis set for free.        | CC is extra state. Condition codes constrain the ordering of instructionsbecause they passinformation from one instruction to a branch. |
| Conditionregister/limitedcomparison | Alpha, MIPS                      | Tests arbitrary register with the result of a simple comparison (equality or zero tests) | Simple                                     | Limited compare may affect critical path or require extra comparison for generalcondition. |
| Compareand branch                   | PA-RISC, VAX,RISC-V              | Compare is part of the branch. Fairly general compares are allowed (greater then, less then) | One instructionrather than two for abranch | May set critical path for branchinstructions                 |

One of the most noticeable properties of branches is that a large number of the comparisons are simple tests, and a large number are comparisons with zero. Thus, some architectures choose to treat these comparisons as special cases, especially if a compare and branch instruction is being used. This figure shows the frequency of different comparisons used for conditional branching.

![quantitative-27.jpg](https://github.com/UtopianFuture/UtopianFuture.github.io/blob/master/image/quantitative-27.jpg?raw=true)

In other words, this is the reason why we use **compare and branch**.

#### A.6 Encoding an Instruction Set

 这一节主要考虑指令的长度，是变长还是定长。变长指令生成的程序短，节省空间，但是性能相对来说较差；定长指令生成的程序长，但相对来说性能更好。当然，也有面对特殊情况设计的混长指令，这里不做详细讨论。这里采用的是 32 位定长指令。

#### A.7 Cross-Cutting Issues: The Roles of Compilers

（这小节其实没讲啥，当然也有可能是我对编译器不了解）

In this section, we discuss the critical goals in the instruction set primarily from the compiler viewpoint.

A compiler writer’s first goal is correctness—all valid programs must be compiled correctly. The second goal is usually speed of the compiled code. Optimizations performed by modern compilers can be classified by the style of the transformation, as follows:

High-level optimizations are often done on the source with output fed to later optimization passes.

Local optimizations optimize code only within a straight-line code fragment (basic block, use branch instruction as bound).

Global optimizations extend the local optimizations across branches and introduce a set of transformations aimed at optimizing loops.

Register allocation associates registers with operands.

Processor-dependent optimizations attempt to take advantage of specific architectural knowledge.

**Impact of Optimizations on Performance**

This figure show the typical optimization, the last column of the figure indicates the frequency with which the listed optimizing transforms were applied to the source program.

![quantitative-28.jpg](https://github.com/UtopianFuture/UtopianFuture.github.io/blob/master/image/quantitative-28.jpg?raw=true)

The figure below shows the effect of various optimizations on instructions executed for two programs. In this case, optimized programs executed roughly 25%–90% fewer instructions than unoptimized programs. The figure illustrates the importance of looking at optimized code before suggesting new instruction set features, because a compiler might completely remove the instructions the architect was trying to improve. But which improvements would be removed by compiler?

![quantitative-29.jpg](https://github.com/UtopianFuture/UtopianFuture.github.io/blob/master/image/quantitative-29.jpg?raw=true)

How the Architect Can Help the Compiler Writer（这小节仅仅记录关键点，不展开，因为不懂）

Compiler writers often are working under their own corollary of a basic principle in architecture: make the frequent cases fast and the rare case correct. These instruction set properties help the compiler writer.

**Provide regularity**

**Provide primitives, not solutions**

**Simplify trade-offs among alternatives**

**Provide instructions that bind the quantities known at compile time as constants**

#### A.8 Putting It All Together: The RISC-V Architecture

我认为 RISC-V 的设计思路是这样的：只提供最基本的指令，核心指令集足够简洁，高效，可能相比于 80X86 针对特定工作场景的指令还不够高效，但是它是开源的，开发人员可以在核心指令集的基础上根据自己的需求进行扩展，然后再把扩展的指令集开源出来，这样 RV64 就能发展成为适用于各种生产场景的 ISA。

#### A.9 Fallacies and Pitfalls

***\*Pitfall\**** **Designing a** **“high-level” instruction set feature specifically oriented to supporting a high-level language structure.**

Attempts to incorporate high-level language features in the instruction set have led architects to provide powerful instructions with a wide range of flexibility. However, often these instructions do more work than is required in the frequent case, or they don't exactly match the requirements of some languages.

***\*Pitfall\****  **Innovating at the instruction set architecture to reduce code size without accounting** **for the compiler.**

Similar to performance optimization techniques, the architect should start with the tightest code the compilers can produce before proposing hardware innovations to save space.

***\*Fallacy\**** **An architecture with flaws cannot be successful.**

The 80x86 provides a dramatic example:

Disadvantages:

the 80x86 supports segmentation, whereas all others picked paging;

it uses extended accumulators for integer data, but other processors use general-purpose registers;

it uses a stack for floating-point data, when everyone else abandoned execution stacks long before.

Advantages:

its selection as the microprocessor in the initial IBM PC makes 80x86 binary compatibility extremely valuable.

Moore's Law provided sufficient resources for 80x86 microprocessors to translate to an internal RISC instruction set and then execute RISC-like instructions. This mix enables binary compatibility with the valuable PC software base and performance on par with RISC processors.

the very high volumes of PC microprocessors mean Intel can easily pay for the increased design cost of hardware translation.

the high volumes allow the manufacturer to go up the learning curve, which lowers the cost of the product.

***\*Fallacy\**** **You can design a flawless architecture.**

All architecture design involves trade-offs made in the context of a set of hardware and software technologies. Over time those technologies are likely to change, and decisions that may have been correct at the time they were made look like mistakes.

### Appendix C Pipelining: Basic and Intermediate Concepts

#### C.1 Instruction

**The Basic of the RISC V Instruction Set**

All RISC architectures are characterized by a few key properties:

All operations on data apply to data in registers and typically change the entire register (32 or 64 bits per register).

The only operations that affect memory are load and store operations that move data from memory to a register or to memory from a register, respectively. Load and store operations that load or store less than a full register (e.g., a byte, 16 bits, or 32 bits) are often available.

The instruction formats are few in number, with all instructions typically being one size. In RISC V, the register specifiers: rs1, rs2, and rd are always in the same place simplifying the control.

**A Simple Implementation of a RISC Instruction Set**

Every instruction in this RISC subset can be implemented in, at most, 5 clock cycles. The 5 clock cycles are as follows. Only record the critical point.

**Instruction fetch cycle (IF)**. Update the PC to the next sequential instruction by adding 4 (because each instruction is 4 bytes) to the PC.

**Instruction decode/register fetch cycle (ID)**. Decode the instruction and read the registers corresponding to register source specifiers from the register file. (1) Compute the possible branch target address by adding the sign-extended offset to the incremented PC. (2) Decoding is done in parallel with reading registers, which is possible because the register specifiers are at a fixed location in a RISC architecture. This technique is known as fixed-field decoding.

**Execution/effective address cycle (EX)**. There are four instruction types:

(1) **Mem****ory reference**. The ALU adds the base register and the offset to form the effective address.

(2) **Register-Register ALU instruction**. The ALU performs the operation specified by the ALU opcode on the values read from the register file.

(3) **Register-Immediate ALU instruction**. The ALU performs the operation specified by the ALU opcode on the first value read from the register file and the sign-extended immediate.

(4) **Conditional branch**. Determine whether the condition is true.

**Memory access (MEM)**.

**Write-back cycle (WB)**.

In this implementation, branch instructions require three cycles, store instructions require four cycles, and all other instructions require five cycles.

**The Classic Five-Stage Pipeline for a RISC Processor**

There are three observation on which we use five-stage pipeline:

We use separate instruction and data memories, which we would typically implement with separate instruction and data caches. The use of separate caches eliminates a conflict for a single memory that would arise between instruction fetch and data memory access.

The register file is used in the two stages: one for reading in ID and one for writing in WB. we perform the register write in the first half of the clock cycle and the read in the second half.

Increment and store the PC for the next instruction in IF. Furthermore, we must also have an adder to compute the potential branch target address during ID

Pipeline register: there are four pipeline registers: IF/ID, ID/EX, EX/MEM, and MEM/WB. We use it to ensure that instructions in different stages of the pipeline do not interfere with one another. The pipeline registers also play the key role of carrying intermediate results from one stage to another where the source and destination may not be directly adjacent.

#### C.2 The Major Hurdle of Pipelining -- Pipeline Hazards

There are situations, called **hazards**, that prevent the next instruction in the instruction stream from executing during its designated clock cycle. Hazards reduce the performance from the ideal speedup gained by pipelining. There are three classes of hazards:

Structural hazards arise from resource conflicts when the hardware cannot support all possible combinations of instructions simultaneously in overlapped execution.

Data hazards arise when an instruction depends on the results of a previous instruction in a way that is exposed by the overlapping of instructions in the pipeline.

Control hazards arise from the pipelining of branches and other instructions that change the PC.

Hazards in pipelines can make it necessary to **stall** the pipeline. Avoiding a hazard often requires that some instructions in the pipeline be allowed to proceed while others are delayed.

**Data Hazards**

**Read After Write (RAW) hazard**.

**Write After Read (WAR) hazard**. WAR hazards are impossible in the simple five stage, integrer pipeline, but they occur when instructions are reordered.

**Write After Write (WAW) hazard**. WAR hazards are also impossible in the simple five stage, integrer pipeline, but they occur when instructions are reordered or when running times vary.

**Minimizing Data Hazard Stalls by Forwarding**

We use **forwarding**(also called **bypassing** and sometimes short-circuiting) to solve the data hazard.

![quantitative-30.jpg](https://github.com/UtopianFuture/UtopianFuture.github.io/blob/master/image/quantitative-30.jpg?raw=true)

**Branch Hazards**

Control hazards can cause a greater performance loss  than do data hazards. When a branch is executed, it may or may not change the PC to something other than its current value plus 4. Recall that if a branch changes the PC to its target address, it is a **taken** branch; if it falls through, it is **not** **taken**, or untaken. If instruction i is a taken branch, then the PC is usually not changed until the end of ID, after the completion of the address calculation and comparison.

This figure shows that the simplest method of dealing with branches is to redo the fetch of the instruction following a branch, once we detect the branch during ID (when instructions are decoded). The first IF cycle is essentially a stall, because it never performs useful work. If the branch is untaken, then the repetition of the IF stage is unnecessary because the correct instruction was indeed fetched.

One stall cycle for every branch will ***\*yield a performance loss of 10% to 30%\*******\*(this is the most critical point, why we need research the predict technology)\**** depending on the branch frequency, so we will examine some techniques to deal with this loss.

![quantitative-31.jpg](https://github.com/UtopianFuture/UtopianFuture.github.io/blob/master/image/quantitative-31.jpg?raw=true)

**Reducing Pipeline Branch Penalties**

We discuss four schemes the actions for a branch are **static**—they are fixed for each branch during the entire execution. The software can try to minimize the branch penalty using knowledge of the hardware scheme and of branch behavior.

**Freezing or flush the pipeline**. Holding or deleting any instructions after the branch until the branch destination is known.  its simplicity both for hardware and software.

**Treating every branch as not taken**. Simply allowing the hardware to continue as if the branch were not executed. The complexity of this scheme

arises from having to know when the state might be changed by an instruction and how to "back out" such a change.

![quantitative-31.jpg](https://github.com/UtopianFuture/UtopianFuture.github.io/blob/master/image/quantitative-32.jpg?raw=true)

**Treating every branch as taken**. As soon as the branch is decoded and the target address is computed, we assume the branch to be taken and begin fetching and executing at the target. This buys us a one-cycle improvement when the branch is actually taken, because we know the target address at the end of ID, one cycle before we know whether the branch condition is satisfied in the ALU stage.

**Delayed branch**.

I think there is a mistake about those two graphs, we know whether the branch instruction is token or not token until the end of the EX stage, so, the branch successor and branch target should stall 2 cycles not 1 cycle.

Reducing the Cost of Branches Through Prediction

Predicting branche schemes fall into two classes: **low-cost static schemes** that rely on information available at compile time and strategies that **predict branches dynamically** based on program behavior.

**Static Branch Prediction**（不做详细展开）

A key way to improve compile-time branch prediction is to use profile information collected from earlier runs.

**Dynamic Branch Prediction and Branch-Prediction Buffers**

The simplest dynamic branch-prediction scheme is a **branch-prediction buffer** or **branch history table**. A branch-prediction buffer is a small memory indexed by the lower portion of the address of the branch instruction.

无条件跳转指令必然会跳转，而条件跳转指令有时候跳转,有时候不跳转，一种简单的预测方式就是根据该指令上一次是否跳转来预测当前时刻是否跳转。如果该跳转指令上次发生跳转,就预测这一次也会跳转，如果上一次没有跳转，就预测这一次也不会跳转。这种预测方式称为：1 位预测(1- bit prediction)。2 位预测(2- bit predictor)，即每个跳转指令的预测状态信息从 1bit 增加到 2bit 计数器，如果这个跳转执行了，就加 1，加到 3 就不加了，如果这个跳转不执行，就减 1，减到 0 就不减了，当计数器值为 0 和 1 时，就预测这个分支不执行，当计数器值为 2 和 3 时，就预测这个分支执行。2 位的计数器比 1 位的计数器拥有更好的稳定性。

![quantitative-33.jpg](https://github.com/UtopianFuture/UtopianFuture.github.io/blob/master/image/quantitative-33.jpg?raw=true)

#### C.3 How Is Pipelining Implemented

**A Simple Implementation of RISC V**

Every RISC V instruction can be implemented in, at most, 5 clock cycles. The 5 clock cycles are as follows(this is the typically design):

**Instruction fetch cycle (IF)**:

IR <- Mem[PC];

NPC <- PC + 4;

**Instruction decode/register fetch cycle (ID)**:

A <- Regs[rs1];

B <- Regs[rs2];

Imm <- sign-extended immediate field of IR;

**Operation** -- Decode the instruction and access the register file to read the registers(rs1 and rs2 are the register specifiers). Decoding is done in parallel with reading registers, which is possible because these fields are at a fixed location in the RISC V instruction format.

**Execution/effective address cycle (EX)**:

The ALU operates on the operands prepared in the prior cycle, performing one of four functions depending on the RISC V instruction type:

*·* **Memory reference**:

ALUOutput <- A + Imm;

*·* **Register-register ALU instruction**:

ALUOutput <- A func B;

*·* **Register-Immediate ALU instruction**:

ALUOutput <- A op Imm;

*·* **Branch**:

ALUOutput <- NPC + (Imm << 2);

Cond <- (A == B)

**Operation** -- The ALU adds the NPC to the sign-extended immediate value in Imm, which is shifted left by 2 bits to create a word offset, to compute the address of the branch target. But actually the target address is computed in ID stage.

**Memory access/branch completion cycle (MEM)**:

The PC is updated for all instructions: PC <- NPC;

*·* **Memory reference**:

LMD <- Mem[ALUOutput] or

Mem[ALUOutput] <- B;

**Operation** -- Access memory if needed. If the instruction is a load, data return from memory and are placed in the LMD (load memory data) register; if it is a store, then the data from the B register are written into memory.

*·* **Branch**:

if (cond) <- PC ALUOutput

**Operation** -- If the instruction branches, the PC is replaced with the branch destination address in the register ALUOutput.

**Write-back cycle (WB)**:

*·* **Register-register or Register-immediate ALU instruction**:

Regs[rd] <- ALUOutput;

*·* **Load instruction**:

Regs[rd] <- LMD;

Rather than optimize this simple implementation, we will leave the design as it is in this figure, because this provides us with a better base for the pipelined implementation.

![quantitative-34.jpg](https://github.com/UtopianFuture/UtopianFuture.github.io/blob/master/image/quantitative-34.jpg?raw=true)

**A Basic Pipeline for RISC V**

This figure shows the RISC V pipeline with the appropriate registers, called pipeline registers or pipeline latches, between each pipeline stage.

![quantitative-35.jpg](https://github.com/UtopianFuture/UtopianFuture.github.io/blob/master/image/quantitative-35.jpg?raw=true)

**Implementing the Control for the RISC V Pipeline**(how to implement the forwarding)

The process of letting an instruction move from the instruction decode stage (ID) into the execution stage (EX) of this pipeline is usually called **instruction issue**; an instruction that has made this step is said to have issued. For the RISC V integer pipeline, all the data hazards can be checked during the ID phase of the pipeline. If a data hazard exists, the instruction is stalled before it is issued.

If there is a RAW hazard with the source instruction being a load, the load instruction will be in the EX stage when an instruction that needs the load data will be in the ID stage. Thus, we can describe all the possible hazard situations with a small table, which can be directly translated to an implementation.

![quantitative-36.jpg](https://github.com/UtopianFuture/UtopianFuture.github.io/blob/master/image/quantitative-36.jpg?raw=true)

Implementing the forwarding logic is similar, although there are more cases to consider. The key observation needed to implement the forwarding logic is that the pipeline registers contain both the data to be forwarded as well as the source and destination register fields. All forwarding logically happens from the ALU or data memory output to the ALU input, the data memory input, or the zero detection unit. Thus, we can implement the forwarding by a comparison of the destination registers of the IR contained in the EX/MEM and MEM/WB stages against the source registers of the IR contained in the ID/EX and EX/MEM registers. This figure shows the comparisons and possible forwarding operations where the destination of the forwarded result is an ALU input for the instruction currently in EX.

![quantitative-37.jpg](https://github.com/UtopianFuture/UtopianFuture.github.io/blob/master/image/quantitative-37.jpg?raw=true)

In addition to the comparators and combinational logic that we must determine when a forwarding path needs to be enabled, we also must enlarge the multiplexers at the ALU inputs and add the connections from the pipeline registers that are used to forward the results. This figure shows the relevant segments of the pipelined data path with the additional multiplexers and connections in place.

<img src="https://github.com/UtopianFuture/UtopianFuture.github.io/blob/master/image/quantitative-38.jpg?raw=true">

#### C.4 What Makes Pipelining Hard to Implement

(这小节主要是介绍在实现指令集过程中会遇到的问题，但是这些问题我还没有切实的认识，所以就当个了解，之后会遇到再回过头来看)

**Dealing With Exceptions**

Exceptional situations are harder to handle in a pipelined processor because the overlapping of instructions makes it more difficult to know whether an instruction can safely change the state of the processor. This subsection isn't the primary mission now, so use a table introduce it briefly.

![quantitative-39.jpg](https://github.com/UtopianFuture/UtopianFuture.github.io/blob/master/image/quantitative-39.jpg?raw=true)

**Stopping and Restarting Execution**

As in unpipelined implementations, the most difficult exceptions have two properties: (1) they occur within instructions (that is, in the middle of the instruction execution corresponding to EX or MEM pipe stages), and (2) they must be restartable. Restarting is usually implemented by saving the PC of the instruction at which to restart.

When an exception occurs, the pipeline control can take the following steps to save the pipeline state safely:

Force a trap instruction into the pipeline on the next IF.

Until the trap is taken, turn off all writes for the faulting instruction and for all instructions that follow in the pipeline; this can be done by placing zeros into the pipeline latches of all instructions in the pipeline, starting with the instruction that generates the exception, but not those that precede that instruction. This prevents any state changes for instructions that will not be completed before the exception is handled.

After the exception-handling routine in the operating system receives control, it immediately saves the PC of the faulting instruction. This value will be used to return from the exception later.

After the exception has been handled, special instructions return the processor from the exception by reloading the PCs and restarting the instruction stream (using the exception return in RISC V). If the pipeline can be stopped so that the instructions just before the faulting instruction are completed and those after it can be restarted from scratch, the pipeline is said to have **precise exceptions**.

**Exceptions in RISC V**

If exceptions are occur in-order, we can handle them in-order too.  In reality, the situation is not as straightforward as this simple example. Exceptions

may occur out of order; that is, an instruction may cause an exception before an earlier instruction causes one.

Consider this sequence of instructions, ld followed by add. The ld can get a data page fault, seen when the instruction is in MEM, and the add can get an instruction page fault, seen when the add instruction is in IF. The instruction page fault will actually occur first, even though it is caused by a later instruction!

![quantitative-40.jpg](https://github.com/UtopianFuture/UtopianFuture.github.io/blob/master/image/quantitative-40.jpg?raw=true)

Because we are implementing precise exceptions, the pipeline is required to handle the exception caused by the ld instruction first. So, the hardware posts all exceptions caused by a given instruction in a status vector associated with that instruction. The exception status vector is carried along as the instruction goes down the pipeline. Once an exception indication is set in the exception status vector, any control signal that may cause a data value to be written is turned off (this includes both register writes and memory writes).

**Instruction Set Complications**(这小节看不懂)

#### C.5 Extending the RISC V Integer Pipeline to Handle Multicycle Operations

This section explores how RISC V pipeline can be extended to handle floating-point operations.

It is impractical to require that all RISC V FP operations complete in 1 clock cycle, or even in 2. Doing so would mean accepting a slow clock or using enormous amounts of logic in the FP units(why can't use this, energy efficiency?), or both. Instead, the FP pipeline will allow for a longer latency for operations. This is easier to grasp if we imagine the FP instructions as having the same pipeline as the integer instructions, with two important changes.

The EX cycle may be repeated as many times as needed(more than one EX stage) to complete the operation -- the number of repetitions can vary for different operations.

There may be multiple FP functional units.

A stall will occur if the instruction to be issued will cause either a structural hazard for the functional unit it uses or a data hazard.

For this section, let's assume that there are four separate functional units in our RISC V implementation:

The main integer unit that handles loads and stores, integer ALU operations, and branches

FP and integer multiplier

FP adder that handles FP add, subtract, and conversion

FP and integer divider

This figure show the resulting pipeline structure.

![quantitative-41.jpg](https://github.com/UtopianFuture/UtopianFuture.github.io/blob/master/image/quantitative-41.jpg?raw=true)

The EX pipeline stage has some number of clock delays larger than 1. To describe such a pipeline, we must define two delays:

**The latency of the functional units**. The number of intervening cycles between an instruction that produces a result and an instruction that uses the result.

**The initiation or repeat interval**. The number of cycles that must elapse between issuing two operations of a given type.

Because most operations consume their operands at the beginning of EX, the latency is usually the number of stages after EX that an instruction produces a result(how many stage that the type of operation need) -- for example, zero stages for ALU operations and one stage for loads.

To achieve a higher clock rate, designers need to put fewer logic levels in each pipe stage(why?), which makes the number of pipe stages required for more complex operations larger. The penalty for the faster clock rate is thus longer latency for operations.

 This figure shows how this pipeline can be drawn by extending the previous figure. The repeat interval is implemented in this figure by adding additional pipeline stages, which will be separated by additional pipeline registers. Naturally, the longer latency of the FP operations increases the frequency of RAW hazards and resultant stalls. The structure of the pipeline in this figure requires the introduction of the additional pipeline registers (e.g.,A1/A2, A2/A3, A3/A4) and the modification of the connections to those registers.

![quantitative-42.jpg](https://github.com/UtopianFuture/UtopianFuture.github.io/blob/master/image/quantitative-42.jpg?raw=true)

**Hazards and Forwarding in Longer Latency Pipelines**

There are a number of different aspects to the hazard detection and forwarding for a pipeline like that shown in previous figure.

Because the divide unit is not fully pipelined, structural hazards can occur. These will need to be detected and issuing instructions will need to be stalled.

Because the instructions have varying running times, the number of register writes required in a cycle can be larger than 1.

Write after write (WAW) hazards are possible, because instructions no longer reach WB in order. Note that write after read (WAR) hazards are not possible, because the register reads always occur in ID.

Instructions can complete in a different order than they were issued, causing problems with exceptions;

Because of longer latency of operations, stalls for RAW ha zards will be more frequent.

Assuming that the pipeline does all hazard detection in ID, there are three checks that must be performed before an instruction can issue:

**Check for structural hazards**.

**Check for a RAW data hazard**.

**Check for a WAW data hazard**.

**Maintaining Precise Exceptions**

Another problem caused by these long-running instructions can be illustrated with the following sequence of code:

```c
fdiv.d f0,f2,f4
fadd.d f10,f10,f8
fsub.d f12,f12,f14
```

This code sequence looks straightforward; there are no dependences. A problem arises, however, because an instruction issued early may complete after an instruction issued later. In this example, we can expect fadd.d and fsub.d to complete before the fdiv.d completes. This is called **out-of-order completion** and is common in pipelines with long-running operations. The result will be an imprecise exception(virtual memory and the IEEE floating-point standard), because the fadd.d destroys one of its operands, we could not restore the state to what it was before the fdiv.d, even with software help.

There are four possible approaches to dealing with out-of-order completion.

Certain classes of exceptions were not allowed or were handled by the hardware without stopping the pipeline.

Buffering the results of an operation until all the operations that were issued earlier are complete.

Allowing the exceptions to become somewhat imprecise, but to keep enough information so that the trap-handling routines can create a precise sequence for the exception.

Allowing the instruction issue to continue only if it is certain that all the instructions before the issuing instruction will complete without causing an exception.

#### C.6 Putting It All Together: The MIPS R4000 Pipeline

The R4000 implements MIPS64 but uses a deeper pipeline than that of our five-stage design both for integer and FP programs. This deeper pipeline allows it to achieve higher clock rates by decomposing the five-stage integer pipeline into eight stages. Because cache access is particularly time critical, the extra pipeline stages come from decomposing the memory access. This type of deeper pipelining is sometimes called **superpipelining**.

This figure shows the eight-stage pipeline structure using an abstracted version of the data path.

![quantitative-43.jpg](https://github.com/UtopianFuture/UtopianFuture.github.io/blob/master/image/quantitative-43.jpg?raw=true)

The function of each stage is as follows:

· IF -- First half of instruction fetch; PC selection actually happens here, together with initiation of instruction cache access.

· IS -- Second half of instruction fetch, complete instruction cache access.

· RF -- Instruction decode and register fetch, hazard checking, and instruction cache hit detection.

· EX -- Execution, which includes effective address calculation, ALU operation, and branch-target computation and condition evaluation.

· DF -- Data fetch, first half of data cache access.

· DS -- Second half of data fetch, completion of data cache access.

· TC -- Tag check, to determine whether the data cache access hit.

· WB -- Write-back for loads and register-register operations.

其实这些步骤五级流水线里都有，只是八级流水线把它们显式 的表示出来了。因为流水线的每个 stage 就是一个 cycle，而这个 cycle 是要和最长的那个 stage 一致的，如果将最长的 stage 分为几个短的 cycle，这样每个 cycle 的时间就会缩短，时钟频率就会增加。同时，虽然单独从指令访存和数据访存上来看，时间还是一样的，但由于每个 cycle 更短了，其他 stage 执行时间也缩短了，比如五级流水线中最大的 MEM stage 执行时间的 3 个单位时间，那么整条流水线执行时间就是 15 个单位时间，而八级流水线最大的 stage 执行时间是 1 个单位时间，整条流水线执行时间就是 8 个单位时间，性能大幅提升。所以设计流水线要保证每个 stage 的执行时间相似，这样才不会造成浪费。

 This figure shows the overlap of successive instructions in the pipeline. Notice that, although the instruction and data memory occupy multiple cycles, they are fully pipelined, so that a new instruction can start on every clock.

![quantitative-44.jpg](https://github.com/UtopianFuture/UtopianFuture.github.io/blob/master/image/quantitative-44.jpg?raw=true)

**Performance of the R4000 Pipeline**

There are four major causes of pipeline stalls or losses:

**Load stalls** -- Delays arising from the use of a load result one or two cycles after the load.

**Branch stalls** -- Two-cycle stalls on every taken branch plus unfilled or canceled branch delay slots. The version of the MIPS instruction set implemented in the R4000 supports instructions that predict a branch at compile time and cause the instruction in the branch delay slot to be canceled when the branch behavior differs from the prediction. This makes it easier to fill branch delay slots.

**FP result stalls** -- Stalls because of RAW hazards for an FP operand

**FP structural stalls** -- Delays because of issue restrictions arising from conflictsfor functional units in the FP pipeline

#### C.7 Cross-Cutting Issues

Simple data dependence can overcome by forwarding, but if there are unavoidable hazard, the hazard detection hardware stalls the pipeline. To overcome these performance losses, the compiler can attempt to schedule instructions to avoid the hazard; this approach is called **compiler or static scheduling**.

There are another approach, called **dynamic scheduling**, whereby the hardware rearranges the instruction execution to reduce the stalls.

We decode and issue instructions in order, however, we want to an instruction to begin execution as soon as its operands are available, even if a predecessor is stalled, we must separate the issue process into two parts:

**Issue** -- Decode instructions, check for structural hazards.

**Read operands** -- Wait until no data hazards, then read operands.

Thus, the pipeline will do **out-of-order execution**, which implies out-of-order completion.

**Dynamic Scheduling With a Scoreboard**

In a dynamically scheduled pipeline, all instructions pass through the issue stage in order (in-order issue); however, they can be stalled or bypass each other in the second stage (read operands) and thus enter execution out of order. **Scoreboarding** is a technique for allowing instructions to execute out of order when there are sufficient resources and no data dependences.

The goal of a scoreboard is to maintain an execution rate of one instruction per clock cycle (when there are no structural hazards) by executing an instruction as early as possible. Thus, when the next instruction to execute is stalled, other instructions can be issued and executed if they do not depend on any active or stalled instruction. The scoreboard takes full responsibility for instruction issue and execution, including all hazard detection. Taking advantage of out-of-order execution requires multiple instructions to be in their EX stage simultaneously. This can be achieved with multiple functional units, with pipelined functional units, or with both.

This figure shows what the processor which use the scoreboard technology looks like.

![quantitative-45.jpg](https://github.com/UtopianFuture/UtopianFuture.github.io/blob/master/image/quantitative-45.jpg?raw=true)

Every instruction goes through the scoreboard, where a record of the data dependences is constructed; this step corresponds to instruction issue and replaces part of the ID step in the RISC V pipeline. The scoreboard then determines when the instruction can read its operands and begin execution. If the scoreboard decides the instruction cannot execute immediately, it monitors every change in the hardware and decides when the instruction can execute. The scoreboard also controls when an instruction can write its result into the destination register. Thus, all hazard detection and resolution are centralized in the scoreboard.

Based on its own data structure, the scoreboard controls the instruction progression from one step to the next by communicating with the functional units.

There is a small complication, however. There are only a limited number of source operand buses and result buses to the register file, which represents a structural hazard. The scoreboard must guarantee that the number of functional units allowed to proceed into steps 2 and 4 does not exceed the number of buses available.其他的 out-of-order execution 应该也会有这个问题。

### Summary

指令集是程序的最底层，所有的程序都要由指令集提供的指令来执行，每一条指令对应的硬件操作是固定的。首先我现在学习的是 load-store architecture，load-store 是这种指令集和其他指令集区分开来的主要特点。**Load-store **是用来访存的，访存自然要知道地址，由这个就引出了几种不同的访存方式，常用的有：立即数寻址，寄存器寻址，寄存器间接寻址，偏移寻址（基址寻址），变址寻址等。这些寻址方式是为了适应不同的程序类型而设计的，最终目的是提高性能。说完 load-store 指令，指令集中还有其他类型的指令，让人不省心的是**分支指令**。为什么让人不省心呢，因为它的目的地址是动态的，不能在编译时确定，得在程序执行时才知道是否要跳转，跳转到哪里去。要处理这个就比较麻烦，所以设计了一系列的分支预测器，这是后话。为了方便跳转，我们为分支预测指令使用了 PC 相关跳转，即通过 PC 值加上一个偏移量来确定跳转地址。为什么使用这种方法呢，因为研究人员发现实际的跳转地址都是在当前 PC 的不远处，这样就可以降低偏移量的长度。同时还使用了寄存器间接跳转，因为有些跳转指令是保存在寄存器中的。OK，指令除了指令类型，还有操作数，操作数的长度根据实际情况就设计了 8 位、16 位、32 位、和 64 位，类型包括字符型，整型，浮点型。要记住，所有的设计方案都是根据实际情况而设计的，为的就是达到最好的性能。

有了指令集就需要执行，现在所有的微处理器都是用流水线的方法来执行。而流水线的性能又和流水线分为几级有着很大的联系。一般是五级流水线，分为 IF, ID, EX, MEM, WB，流水线每个 stage 都是一个 cycle 的执行时间，而 cycle 的执行时间和最长的 stage 一样，所以如果某个 stage 花费的时间太多，整个流水线就会有很多时间被浪费。在这五级流水线中，只有 IF, MEM 是需要访存的，耗时很多，所以很多微处理器将 IF, MEM 又分为几个并行的 stage，这样整个流水线效率就得到提高。同时，为了流水线不阻塞，我们也需要高性能的存储系统。

流水线的结构很容易理解，但是实现起来很困难，会遇到这些问题：（1）DATA HAZARD; (2) CONTROL HAZARD; (3)STRUCTURE HAZARD。围绕着如何解决这些问题又延伸出一大堆知识点。这里由于只关注结构方面的问题，就不延伸讨论。

流水线另一个非常重要的问题就是如何扩展到浮点数也能正常运行。由于浮点数的 EX stage 往往需要多个步骤，它和整形运算结合起来运行又会造成一系列的问题。

对于 load-store 指令集来说，流水线的能够正常运行，除了与分支指令能否正确的预测有关，还与指令所需的数据，以及指令本身能够及时的获取有关。后面两项就是存储系统的工作，我们根据（1）hit time; (2) Bandwith; (3) Miss penalty; (4) Miss rate; (5) Power comsumption; (6) Hardware cost/complexity 这几项参数来衡量一个存储（主要是 cache）优化方法的性能。
