## CPU performance analysis methodology

### Core Architecture

![image-20260509154538214](C:\Users\xpeng\AppData\Roaming\Typora\typora-user-images\image-20260509154538214.png)

核心流水线的前端由取指单元和解码单元组成。前端还包含分支预测单元，用于预测分支目标地址，并提前预取流水线后续指令。该单元能够掩盖流水线中控制流气泡带来的延迟。取指单元每个时钟周期可读取多条指令，**其吞吐带宽由微架构设计决定**，读取的指令会存入解码队列。解码队列每个周期可送出多条指令进行解码，其吞吐带宽由可用解码槽位数量决定。解码单元将 Arm 架构指令拆解为微操作，也称作微指令（µops）。该单元每个时钟周期可解码多条微操作，随后送入重命名单元进行整理，以供核心后端乱序执行。指令吞吐带宽由微架构中**可用的重命名槽位数量决定**，槽位数量可通过 PMMIR_EL1 寄存器的 SLOTS 字段读取。从微架构角度来看，重命名单元是核心前端与后端的分界点。

核心后端设有调度器，负责调度微操作执行，当微操作对应的发射队列可接收指令时进行调度。当执行单元空闲且源操作数就绪时，发射队列会送出微操作执行。执行完成后，**运算结果送入提交重排序缓冲区（ROB）**，当推测执行结果确认无误后，缓冲区中的指令按规则完成提交退役。核心后端采用乱序执行微操作，并借助重排序缓冲区暂存运算结果。分发单元负责跟踪微操作间的数据依赖关系，判定执行所需操作数是否就绪。**寄存器重命名也在此阶段完成，用以缓解数据依赖冲突问题**。

在分发单元中，发射队列主要用于：

- 将微操作（µops）排队调度至对应执行端口；
- 管理微操作之间的数据依赖关系；
- 跟踪执行所需操作数的就绪状态；

每个执行端口支持不同类型的运算操作。微操作执行完毕后，执行结果会更新至重排序缓冲区。已完成的微操作会严格按照程序原有架构顺序完成指令退役(retried)。当分支预测错误或出现异常导致程序执行流程变更时，**会清空作废相关微操作**。以下各类执行单元（EU）支持常规运算类型：

- 整数执行单元：执行分支指令与算术运算指令；
- 浮点单元（FPU）/ 单指令多数据流（SIMD）执行单元：执行浮点指令与向量指令；
- 加载存储单元：执行加载、存储及原子操作指令；

核心的内存子系统负责处理加载和存储操作，这类操作高度依赖存储层次架构。Neoverse V3 核心为每个内核配置独立的一级缓存与二级缓存，其中二级缓存为一级数据缓存和一级指令缓存共享。加载存储单元管控各级缓存与内存之间的数据流转。核心集成多个加载存储单元，可同时处理读写操作。一级缓存与二级缓存均为组相联缓存架构。缓存容量可根据具体芯片实现进行配置，同时决定每路缓存的组数。内核专用二级缓存通过 AMBA® 5 CHI 接口与系统其他模块互联。

### CPU performance analysis methodology

The methodology is conducted in two stages.

- Stage 1: Topdown analysis

  The first stage is to perform Topdown analysis to detect and identify any performance bottlenecks in the CPU, and provide direction for further analysis at Stage 2. Stage 1 uses hierarchical pipeline stall-related metrics.

- Stage 2: Microarchitecture Exploration
  The second stage is to conduct microarchitecture exploration to further analyze bottlenecked CPU resources, based on the Stage 1 findings. Stage 2 uses a set of CPU resource-effectiveness metrics.

![Topdown methodology overview for Neoverse V3](C:\Users\xpeng\AppData\Roaming\Typora\typora-user-images\image-20260427102849851.png)

#### Stage 1: Topdown analysis

![Neoverse V3 Topdown methodology Stage 1 overview](C:\Users\xpeng\AppData\Roaming\Typora\typora-user-images\image-20260427103019681.png)

The following implementation criteria apply for this metric group:
• The sum of the metrics in Topdown_L1 equals 100% of the total execution cycles in the core implementation.
• The total execution bandwidth of the core is the same as the number of execution slots for operations.
• **Slots are the number of ports in the rename unit, which is considered the boundary between the frontend and backend of the core.**
• A frontend stall is counted when there are no micro-ops to rename.
• A backend stall is counted when there are micro-ops in the rename unit, but they cannot dispatch to the backend due to backend resource constraints.

The metrics in the Topdown Level 1 metric group are:

- frontend_bound
  The frontend_bound metric measures pipeline inefficiency due to **slots that were stalled because of resource constraints in the frontend**.
- backend_bound
  The backend_bound metric measures pipeline inefficiency due to **slots that were stalled because of resource constraints in the backend**.
- bad_speculation
  The bad_speculation metric is **the percentage of total slots that executed operations and did not retire due to a pipeline flush**, which indicates cycles that were utilized inefficiently. This metric measures pipeline inefficiency caused by flushes in the pipeline, due to branch mispredictions or machine clears.

- retiring
  The retiring metric is **the percentage of total slots that retired operations**, which indicates cycles that were utilized efficiently. This metric covers the total slots that were utilized efficiently by the processor. **A high retirement rate can also mean that there is an opportunity for further performance optimization**. Scalar code that shows high retirement **can be optimized by vectorization** if there is data parallelism. In some cases, the code that is executed can be made redundant or optimized. When you analyze high retirement rates, evaluation of the instruction or operation mix is a recommended next step in the characterization of execution units that are heavily utilized.

##### Topdown Frontend metric group

![image-20260509160339185](C:\Users\xpeng\AppData\Roaming\Typora\typora-user-images\image-20260509160339185.png)

The metrics in the Topdown Frontend metric group are:
**frontend_mem_bound**
The frontend_mem_bound metric is **the percentage of total cycles stalled in the frontend due to frontend core resource constraints** related to the instruction fetch latency issues **caused by memory access components**.
This metric breaks down into the following metrics:

- frontend_mem_cache_bound
  - frontend_cache_l1i_bound: the ratio of frontend memory bound cycles waiting on an L1 miss;
  - frontend_cache_l2i_bound: the ratio of frontend memory bound cycles waiting on an L2 miss;
- frontend_mem_tlb_bound: the ratio of frontend memory bound cycles waiting on a TLB miss.

**frontend_core_bound**
The frontend_core_bound metric is the percentage of total cycles stalled in the frontend due to frontend core resource constraints **not related to** instruction fetch latency issues **caused by memory access components**.

Reasons for a stall in the frontend due to the core units are as follows:

- Resteer: Refers to the frontend resteer that cause PC updates, which can either be:
  - A flush that is caused by mispredictions;
  - Other machine clears due to microarchitectural reasons such as exceptions and memory hazards;

- Flow: Refers to the frontend structural flow stall that is caused in Fetch/Decode unit fetches **due to a branch prediction unit that does not provide predictions on time**.

This metric breaks down into the following metrics:

- frontend_core_flush_bound

- frontend_core_flow_bound

##### Topdown Backend metric group

![image-20260509162720956](C:\Users\xpeng\AppData\Roaming\Typora\typora-user-images\image-20260509162720956.png)

The metrics in the Topdown Backend metric group are:
**backend_mem_bound**

The backend_mem_bound metric is the percentage of total cycles stalled in the backend due to backend core resource constraints related to memory access latency issues **caused by memory access components**. backend_mem_bound counts the stall cycles in which no operation can be sent to the dispatch unit and a memory operation is being executed.

This metric breaks down into the following metrics:

- backend_mem_cache_bound
  - backend_cache_l1d_bound: the ratio of backend memory bound cycles waiting on an L1 data cache miss;
  - backend_cache_l2d_bound: the ratio of backend memory bound cycles waiting on an L2 unified cache miss;
- backend_mem_tlb_bound: the ratio of backend memory bound cycles waiting on data for a TLB miss or walk;
- backend_mem_store_bound: counts backend memory bound stall cycles while stores are outstanding.

**backend_core_bound**

The backend_core_bound metric is the percentage of total cycles stalled in the backend due to backend core resource constraints **not related to** instruction fetch latency issues **caused by memory access components**.

Reasons for a stall in the backend due to the core units are as follows:

- Rename: Refers to rename stalls caused by **the register renaming unit that sends operations to the dispatch unit**.
- Dispatch Busy: Refers to execution stalls caused by **either issue queue unit stalls, execution port starvation, or data dependency issues** for operations to make further progress in execution.

#### Stage 2: Microarchitecture exploration

Topdown analysis Stage 2 helps to locate the hot spots in the code and conduct root cause analysis. This stage reveals more details about the CPU component that caused the bottleneck and was identified during the Stage 1 process.

![image-20260509164130316](C:\Users\xpeng\AppData\Roaming\Typora\typora-user-images\image-20260509164130316.png)
