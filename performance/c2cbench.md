## c2cbench (Cache-to-Cache Benchmark) 测试逻辑分析

### 项目概述

**c2cbench** 是一个专门用于评估**缓存到缓存（Cache-to-Cache）传输性能**的基准测试工具，由加州大学伯克利分校开发。它测量多核处理器上远程缓存访问的性能和缓存一致性协议对数据传输的影响。

### 关键测试场景

- **Producer-Consumer 模式**：生产者写入数据，消费者读取；

- **缓存一致性开销**：测量 MESI/MOESI 协议的性能影响；

- **核间通信延迟**：不同 CPU 核心之间的数据传输速度；

- **竞争条件分析**：本地和远程请求对性能的影响；

### 核心概念

#### Cache-to-Cache Transfer（缓存到缓存传输）

在现代多核处理器中，当一个核心需要访问另一个核心缓存中的数据时：

```
CPU 0 (生产者)          CPU 1 (消费者)
   |                        |
   | 写入数据                |
   v                        |
L1 Cache (Modified)         |
   |                        |
   | 缓存一致性协议           |
   | (MESI/MOESI)           |
   |                        v
   +-----------------> L1 Cache (Shared/Invalid)
                            |
                            | 读取数据
                            v
                        CPU 1 使用
```

#### 同步间隔（Synchronization Interval）

- **kMin**：消费者与生产者的最小距离（块数）；
- **kMax**：生产者与消费者的最大距离（块数）；

```
生产者位置: [====|====|====|====|====]
                  ^         ^
                  |         |
              消费者      kMax 限制
              (kMin后面)
```

### 数据结构

#### 命令行选项（cmdlineopts）

```c
typedef struct _cmdlineopts {
    int prodId;          // 生产者绑定的 CPU ID
    int consId;          // 消费者绑定的 CPU ID
    int prodOp;          // 生产者操作：RD/WR/RW
    int consOp;          // 消费者操作：RD/WR/RW
    long dataSize;       // 总数据大小（字节）
    long blkSize;        // 块大小（字节）
    long stride;         // 访问步长：1/4/8/16
    long kMin;           // 同步间隔下限
    long kMax;           // 同步间隔上限
    int warmConsumer;    // 是否预热消费者
    long warmIters;      // 预热迭代次数
    long benchIters;     // 基准测试迭代次数
    int measSteadyState; // 是否测量稳态性能
    int exclusiveData;   // 生产者/消费者是否访问不同数据
    int verbose;         // 详细输出标志
} cmdlineopts;
```

#### 线程属性（attribs）

```c
typedef struct _attribs {
    cmdlineopts* opts;   // 命令行选项指针
    WORD* mPtr;          // 内存缓冲区指针
    TIMEVAL* timeVals;   // 时间测量数组
    long* distances;     // 同步距离数组
} attribs;
```

#### 全局同步变量

```c
// 所有变量都使用缓存行填充，避免伪共享
volatile long prodPos[CACHE_LINE_SIZE/sizeof(long)];     // 生产者位置
volatile long consPos[CACHE_LINE_SIZE/sizeof(long)];     // 消费者位置
volatile long resetDoneFlg[CACHE_LINE_SIZE/sizeof(long)]; // 重置完成标志
```

**缓存行填充**：确保这些同步变量各占一个独立的缓存行（64 字节），避免伪共享（false sharing）。

### 测试流程

#### 主函数执行流程

```
main()
├── 解析命令行参数 (parseCmdLineOpts)
├── 验证参数有效性 (checkCmdLineOpts)
│   ├── blkSize >= 8KB
│   ├── dataSize >= blkSize
│   └── benchIters > ITERS_UNTIL_STEADY_STATE (如启用稳态测量)
│
├── 分配内存
│   ├── 如果 exclusiveData=YES:
│   │   ├── mPtr_P (生产者独立数据)
│   │   └── mPtr_C (消费者独立数据)
│   └── 否则:
│       └── mPtr_P = mPtr_C (共享数据)
│
├── 分配测量数组
│   ├── prodTimeVals[benchIters × numBlocks]
│   ├── consTimeVals[benchIters × numBlocks]
│   ├── prodDistances[benchIters × numBlocks]
│   └── consDistances[benchIters × numBlocks]
│
├── 初始化同步变量
│   ├── prodPos[0] = -1
│   ├── consPos[0] = -1
│   └── resetDoneFlg[0] = 0
│
├── 创建线程
│   ├── pthread_create(&prodThr, produceC2CData, &prodAttrs)
│   └── pthread_create(&consThr, consumeC2CData, &consAttrs)
│
├── 等待线程完成
│   ├── pthread_join(prodThr)
│   └── pthread_join(consThr)
│
├── 输出统计信息 (printStats)
└── 释放内存
```

#### 生产者线程流程（produceC2CData）

```
produceC2CData()
├── 绑定到指定 CPU (setCPUAffinity)
├── 初始化内存缓冲区 (memset)
│
├── 预热阶段 (warmIters 次迭代)
│   └── for n = 0 to warmIters:
│       └── for b = 0 to numBlocks:
│           ├── 等待 kMax 限制: while (prodPos - consPos) > kMax
│           ├── 执行操作 (RD/WR/RW)
│           └── prodPos++
│       └── 等待重置: while resetDoneFlg == 0
│
└── 基准测试阶段 (benchIters 次迭代)
    └── for n = 0 to benchIters:
        └── for b = 0 to numBlocks:
            ├── 记录当前距离: distance = prodPos - consPos
            ├── 等待 kMax 限制: while (prodPos - consPos) > kMax
            ├── 开始计时: gettimeofday(&tStart)
            ├── 执行操作:
            │   ├── RD: 读取数据块 (LOAD_S1/S4/S8/S16)
            │   ├── WR: 写入数据块 (STORE_S1/S4/S8/S16)
            │   └── RW: 读后写 (LDST_S1/S4/S8/S16)
            ├── 停止计时: gettimeofday(&tStop)
            ├── 记录时间和距离
            │   ├── timeVals[index] = tStop - tStart
            │   └── distances[index] = distance
            └── prodPos++
        └── 等待重置: while resetDoneFlg == 0
```

#### 消费者线程流程（consumeC2CData）

```
consumeC2CData()
├── 绑定到指定 CPU (setCPUAffinity)
├── 初始化内存缓冲区 (如果 exclusiveData=YES)
│
├── 预热阶段 (如果 exclusiveData || warmConsumer)
│   └── for n = 0 to warmIters:
│       └── for b = 0 to numBlocks:
│           ├── 等待 kMin 限制: while (prodPos - consPos) < kMin
│           ├── 执行读取操作 (LOAD_S1/S4/S8/S16)
│           └── consPos++
│       └── 执行重置:
│           ├── while prodPos < (numBlocks - 1)
│           ├── prodPos = -1, consPos = -1
│           └── resetDoneFlg = 1
│
└── 基准测试阶段 (benchIters 次迭代)
    └── for n = 0 to benchIters:
        └── for b = 0 to numBlocks:
            ├── 记录当前距离: distance = prodPos - consPos
            ├── 等待 kMin 限制: while (prodPos - consPos) < kMin
            ├── 开始计时: gettimeofday(&tStart)
            ├── 执行操作 (RD/WR/RW)
            ├── 停止计时: gettimeofday(&tStop)
            ├── 记录时间和距离
            └── consPos++
        └── 执行重置:
            ├── while prodPos < (numBlocks - 1)
            ├── prodPos = -1, consPos = -1
            └── resetDoneFlg = 1
```

### 访问模式详解

#### 操作类型

| 操作            | 宏定义 | 说明                           |
| --------------- | ------ | ------------------------------ |
| RD (Read)       | 0      | 只读操作，测试缓存行共享       |
| WR (Write)      | 1      | 只写操作，测试缓存行修改和失效 |
| RW (Read-Write) | 2      | 读后写，测试 RMW 性能          |

#### 步长（Stride）

支持 4 种步长，对应不同的缓存行利用率：

| Stride | 访问间隔   | 缓存行利用率           | 宏定义              |
| ------ | ---------- | ---------------------- | ------------------- |
| 1      | 连续访问   | 100%                   | LOAD_S1, STORE_S1   |
| 4      | 每 16 字节 | ~25% (假设 64B 缓存行) | LOAD_S4, STORE_S4   |
| 8      | 每 32 字节 | ~12.5%                 | LOAD_S8, STORE_S8   |
| 16     | 每 64 字节 | ~6.25%                 | LOAD_S16, STORE_S16 |

#### 循环展开优化

所有访问宏都展开 32 次操作，减少循环开销：

- LOAD_S1 (stride=1) 

```c
#define LOAD_S1(firstWord, lastWord, sum) \
{ \
  while (firstWord <= lastWord) \
  { \
    sum += \
    LOAD(0) LOAD(1) LOAD(2) LOAD(3) \
    LOAD(4) LOAD(5) LOAD(6) LOAD(7) \
    // ... 共 32 次加载
    LOAD(28) LOAD(29) LOAD(30) firstWord[31]; \
    firstWord += 32; \
  } \
}
```

- STORE_S4 (stride=4) 

```c
#define STORE_S4(firstWord, lastWord) \
{ \
  while (firstWord <= lastWord) \
  { \
    STORE(0) STORE(4) STORE(8) STORE(12) \
    STORE(16) STORE(20) STORE(24) STORE(28) \
    // ... 每隔 4 个 WORD 访问一次
    STORE(112) STORE(116) STORE(120) STORE(124) \
    firstWord += 128;  // 跳过 128 个 WORD
  } \
}
```

#### 读后写（RW）操作

```c
#define LDST_S1(firstWord, lastWord, sum) \
{ \
  while (firstWord <= lastWord) \
  { \
    // 先读后写同一位置
    sum += firstWord[0]; firstWord[0] = WORD_VAL; \
    sum += firstWord[1]; firstWord[1] = WORD_VAL; \
    // ... 32 次读写对
    firstWord += 32; \
  } \
}
```

### 同步机制详解

#### 生产者-消费者同步

- kMin 和 kMax 的作用

```
块位置:  [0][1][2][3][4][5][6][7][8][9]
生产者:           ^                    (prodPos = 5)
消费者:   ^                            (consPos = 1)
距离 K:   <------>                     (K = 5 - 1 = 4)

kMin = 1:  消费者必须至少落后 1 个块
kMax = 8:  生产者最多领先 8 个块
```

- 生产者等待条件

```c
while ((prodPos - consPos) > kMax)
    WAIT();  // 等待消费者追上来
```

- 消费者等待条件

```c
while ((prodPos - consPos) < kMin && prodPos < (numBlocks - 1))
    WAIT();  // 等待生产者前进
```

#### 迭代重置机制

**消费者负责重置**：
```c
// 等待生产者完成当前迭代
while (prodPos < (numBlocks - 1))
    WAIT();

// 重置位置
prodPos = -1;
consPos = -1;

// 通知生产者
resetDoneFlg = 1;
```

**生产者等待重置**：
```c
while (resetDoneFlg == 0)
    WAIT();

// 确认并清除标志
resetDoneFlg = 0;
```

### 测试模式

#### 模式 1: 生产者写，消费者读（默认）

**典型场景**：测量缓存一致性协议的开销

```
生产者 (CPU 0):  写入 → L1 Cache (Modified)
                               ↓
                    缓存一致性协议 (MESI)
                               ↓
消费者 (CPU 1):  L1 Cache (Shared) ← 读取
```

**命令示例**：

```bash
./c2cbench -P 0 -C 1 -p wr -c rd -d 8192 -b 32
```

**测量内容**：

- 写入数据到本地缓存的时间（生产者）；
- 从远程缓存获取数据的时间（消费者）；
- 缓存行从 Modified → Shared 的开销；

#### 模式 2: 独占数据测试

**启用方式**：`-x` 标志

```
生产者数据: [====|====|====]  (mPtr_P)
消费者数据: [====|====|====]  (mPtr_C)
           (不同的内存区域)
```

**用途**：

- 测量无缓存一致性干扰的纯计算性能
- 作为对照组，评估缓存一致性的实际开销

#### 模式 3: 稳态性能测量

**启用方式**：默认启用，`-S` 禁用

```
迭代:     0   1   2   3   4   5   6   7   8   9  10  11
状态:   [预热期 ] [稳态期                           ]
                  ^
                  ITERS_UNTIL_STEADY_STATE=2
```

**计算方法**：

```c
if (measSteadyState == 1) {
    firstBenchIter = ITERS_UNTIL_STEADY_STATE;  // 跳过前 2 次
    effBenchIters = benchIters - ITERS_UNTIL_STEADY_STATE;
}
```

### 性能统计方法

#### 时间测量

每个块的处理时间：
```c
TIMEVAL tStart, tStop, tDiff;

gettimeofday(&tStart, NULL);
// 执行操作 (RD/WR/RW)
gettimeofday(&tStop, NULL);

timersub(&tStop, &tStart, &tDiff);  // tDiff = tStop - tStart
timeVals[index] = tDiff;
```

#### 时间开销校正

```c
long measureTimingOverhead() {
    TIMEVAL t1, t2, diff;
    long overhead = 0;
    
    for (int i = 0; i < 1000; i++) {
        gettimeofday(&t1, NULL);
        gettimeofday(&t2, NULL);
        timersub(&t2, &t1, &diff);
        overhead += diff.tv_usec;
    }
    
    return overhead / 1000;  // 平均开销（微秒）
}
```

#### 统计计算

**平均时间**：
```c
double avgTime = 0.0;
for (long i = firstIter; i < benchIters; i++) {
    for (long b = 0; b < numBlocks; b++) {
        long index = i * numBlocks + b;
        avgTime += timeVals[index].tv_usec - timeAdj;
    }
}
avgTime /= (effBenchIters * numBlocks);
```

**吞吐量**：
```c
// 块大小（字节）/ 平均时间（秒）
double throughput = blkSize / (avgTime / 1e6);  // Bytes/s
```

### 命令行参数详解

#### 基础参数

```bash
-P <prodId>       # 生产者绑定的 CPU ID (0+)
-C <consId>       # 消费者绑定的 CPU ID (0+)
-p <prodOp>       # 生产者操作: rd, wr, rw
-c <consOp>       # 消费者操作: rd, wr, rw
-d <dataSize>     # 总数据大小 (KB)
-b <blkSize>      # 块大小 (KB，至少 8KB)
-s <stride>       # 访问步长: 1, 4, 8, 16
```

#### 同步控制

```bash
-k <kMin>         # 同步间隔下限 (默认 1)
-K <kMax>         # 同步间隔上限 (默认 1024)
```

#### 测试控制

```bash
-w                # 禁用消费者预热
-i <warmupIters>  # 预热迭代次数 (默认 2)
-I <benchIters>   # 基准测试迭代次数 (默认 12)
-S                # 禁用稳态性能测量
-x                # 生产者/消费者访问独占数据
-v                # 显示详细的块级统计
```

### 测试示例

#### 示例 1: 基本 Producer-Consumer 测试

```bash
# CPU 0 写入，CPU 1 读取，8MB 数据，32KB 块
./c2cbench -P 0 -C 1 -p wr -c rd -d 8192 -b 32
```

**预期输出**：
```
# DataSize=8192K, BlkSize=32K, ProdId=0, ConsId=1
# ProdOp=wr, ConsOp=rd, Stride=16, KMin=1, KMax=1024
# ConsWarmup=Yes, SteadyState=Yes, ExclusiveData=No

Average Producer Time:     234.5 usecs
Average Consumer Time:     456.7 usecs
Producer Throughput:       136.4 MB/s
Consumer Throughput:        70.1 MB/s
```

**解读**：
- 消费者时间 > 生产者时间：从远程缓存读取比本地写入慢
- 吞吐量差异反映了缓存一致性开销

#### 示例 2: 不同 CPU 距离测试

```bash
# 同一 CCX 内的核心（近距离）
./c2cbench -P 0 -C 1 -d 4096 -b 32

# 跨 CCX 的核心（中等距离）
./c2cbench -P 0 -C 4 -d 4096 -b 32

# 跨 NUMA 节点的核心（远距离）
./c2cbench -P 0 -C 8 -d 4096 -b 32
```

**预期结果**：
- 近距离：最低延迟，最高吞吐量
- 中等距离：中等延迟
- 远距离：最高延迟，跨 NUMA 惩罚明显

#### 示例 3: 对比独占数据 vs 共享数据

```bash
# 共享数据（测试缓存一致性）
./c2cbench -P 0 -C 1 -d 8192 -b 32 -p wr -c rd

# 独占数据（无缓存一致性）
./c2cbench -P 0 -C 1 -d 8192 -b 32 -p wr -c rd -x
```

**分析**：
- 性能差异 = 缓存一致性协议的纯开销
- 独占数据应该接近理论峰值性能

#### 示例 4: 不同步长对比

```bash
# Stride=1: 100% 缓存行利用率
./c2cbench -P 0 -C 1 -d 4096 -b 32 -s 1

# Stride=4: ~25% 缓存行利用率
./c2cbench -P 0 -C 1 -d 4096 -b 32 -s 4

# Stride=16: ~6% 缓存行利用率
./c2cbench -P 0 -C 1 -d 4096 -b 32 -s 16
```

**预期**：
- Stride=1: 最高吞吐量（充分利用带宽）
- Stride=16: 最低吞吐量（大量缓存行浪费）

#### 示例 5: 紧密同步 vs 松散同步

```bash
# 紧密同步 (kMin=1, kMax=2)
./c2cbench -P 0 -C 1 -d 4096 -b 32 -k 1 -K 2

# 松散同步 (kMin=1, kMax=128)
./c2cbench -P 0 -C 1 -d 4096 -b 32 -k 1 -K 128
```

**分析**：
- 紧密同步：频繁的生产者-消费者交互，测试最坏情况
- 松散同步：更多并行性，接近流水线性能

### 优化技术

#### 缓存行填充（避免伪共享）

```c
// 同步变量各占一个独立缓存行
volatile long prodPos[CACHE_LINE_SIZE/sizeof(long)];  // 64 字节
volatile long consPos[CACHE_LINE_SIZE/sizeof(long)];  // 64 字节
volatile long resetDoneFlg[CACHE_LINE_SIZE/sizeof(long)]; // 64 字节
```

**好处**：
- prodPos 和 consPos 不会在同一缓存行中
- 避免生产者和消费者因修改不同变量而导致的缓存行乒乓

#### 循环展开

所有访问宏都展开 32 次：
```c
LOAD(0) LOAD(1) ... LOAD(31)  // 32 次展开
```

**优势**：
- 减少循环分支预测失败
- 提高指令级并行度（ILP）
- 减少循环计数器开销

#### CPU 亲和性绑定

```c
void setCPUAffinity(int procId) {
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(procId, &cpuset);
    pthread_setaffinity_np(pthread_self(), sizeof(cpuset), &cpuset);
}
```

**好处**：
- 确保线程在指定 CPU 上运行
- 避免迁移导致的缓存失效
- 精确控制核间距离

#### 防编译器优化

```c
void useWord(WORD w) {
    static volatile WORD sink;
    sink = w;
}
```

**作用**：
- 确保累加变量 `sum` 被实际使用
- 防止编译器优化掉整个循环

### 结果分析指南

#### 性能指标

**生产者性能**：
- 平均时间（微秒）
- 吞吐量（MB/s）
- 最小/最大时间

**消费者性能**：
- 平均时间（微秒）
- 吞吐量（MB/s）
- 缓存一致性开销

#### 典型性能模式

**Producer-Consumer (wr → rd)**：
```
生产者时间:  100 usecs  (本地写入)
消费者时间:  300 usecs  (远程读取)
              ↑
        缓存一致性开销 = 200 usecs
```

**SDM (Shared Data Manager) 模式**：
- kMax = kMin：强制同步执行
- 测量最坏情况下的缓存一致性延迟

#### 架构影响因素

**同一 CCX/CCD 内**：
- 共享 L3 缓存；
- 低延迟（50-100ns）；
- 高带宽；

**跨 CCX/CCD**：

- 通过 Infinity Fabric 互联；
- 中等延迟（100-200ns）；
- 中等带宽；

**跨 NUMA 节点**：

- 远程内存访问；
- 高延迟（200-500ns）；
- 显著的性能惩罚；

### 应用场景

#### 多核性能评估
- 评估不同核心配置的缓存一致性性能；
- 识别最优的线程绑定策略；
- 发现 NUMA 架构瓶颈；

#### 并行算法优化
- 评估 Producer-Consumer 模式性能；
- 优化数据共享模式；
- 选择最佳同步间隔；

#### 硬件性能对比
- 对比不同 CPU 架构的缓存一致性协议；
- 评估 CCX/CCD 互联带宽；
- 验证 NUMA 性能特性；

#### 软件调试
- 识别伪共享问题；
- 评估线程迁移影响；
- 优化缓存行对齐；

### 总结

#### 核心特点

- **专注缓存一致性**：测量真实的 cache-to-cache 传输性能；
- **灵活配置**：支持多种操作、步长、同步模式；
- **精确控制**：CPU 绑定、缓存行对齐、循环展开；
- **全面统计**：块级时间、距离、吞吐量分析；

#### 关键测试能力

- **缓存一致性开销**：量化 MESI/MOESI 协议的实际成本；

- **核间通信延迟**：测量不同 CPU 距离的性能影响；

- **访问模式影响**：评估读、写、读写混合的性能差异；

- **同步策略优化**：找到最佳的生产者-消费者同步间隔；

c2cbench 是理解和优化多核并行应用的重要工具，特别适合评估共享内存并行编程的性能瓶颈。