# lmbench bw_mem 测试逻辑分析

## 项目概述

`bw_mem` 是 lmbench 基准测试套件中用于测量**内存带宽**的工具。它通过执行不同类型的内存访问模式，评估系统在各种工作负载下的内存子系统性能。

## 测试目标

测量不同内存访问模式下的**带宽**（MB/s），包括：
- 顺序读取
- 顺序写入
- 读写混合
- 内存拷贝
- 系统调用（bzero, bcopy）

## 命令行参数

```bash
bw_mem [-P <parallelism>] [-W <warmup>] [-N <repetitions>] <size> <what> [conflict]
```

### 参数说明

| 参数               | 含义            | 默认值     | 说明                                |
| ------------------ | --------------- | ---------- | ----------------------------------- |
| `-P <parallelism>` | 并行进程/线程数 | 1          | 多进程并发测试，提高CPU利用率       |
| `-W <warmup>`      | 预热次数        | 0          | 预热缓存，使测试更稳定              |
| `-N <repetitions>` | 重复次数        | -1（自动） | 控制测试重复次数                    |
| `<size>`           | 测试数据大小    | 必填       | 必须 ≥512 字节（如：64k, 1m, 256m） |
| `<what>`           | 测试类型        | 必填       | 见下表                              |
| `[conflict]`       | 对齐标志        | 可选       | 控制 buf2 的内存对齐方式            |

### 测试类型（what 参数）

| 类型    | 全称       | 访问模式                  | 说明                     |
| ------- | ---------- | ------------------------- | ------------------------ |
| `rd`    | Read       | 4 字节读，32 字节步长     | 稀疏读取，测试缓存行填充 |
| `wr`    | Write      | 4 字节写，32 字节步长     | 稀疏写入，测试缓存行失效 |
| `rdwr`  | Read-Write | 读后写同位置，32 字节步长 | 测试读-修改-写性能       |
| `cp`    | Copy       | 读写不同位置，32 字节步长 | 稀疏内存拷贝             |
| `frd`   | Full Read  | 读取每个 4 字节字         | **密集顺序读取**         |
| `fwr`   | Full Write | 写入每个 4 字节字         | **密集顺序写入**         |
| `fcp`   | Full Copy  | 拷贝每个 4 字节字         | **密集内存拷贝**         |
| `bzero` | Bzero      | 系统调用 bzero()          | 测试系统提供的内存清零   |
| `bcopy` | Bcopy      | 系统调用 bcopy()          | 测试系统提供的内存拷贝   |

## 核心数据结构

```c
typedef struct _state {
    double   overhead;      // 循环开销（未使用）
    size_t   nbytes;        // 测试数据总大小
    int      need_buf2;     // 是否需要第二个缓冲区（cp, fcp, bcopy）
    int      aligned;       // 缓冲区对齐标志
    TYPE     *buf;          // 主缓冲区（源数据）
    TYPE     *buf2;         // 第二缓冲区（目标数据）
    TYPE     *buf2_orig;    // buf2 原始指针（用于释放）
    TYPE     *lastone;      // 最后一个有效块的指针
    size_t   N;             // 总字节数
} state_t;
```

其中 `TYPE` 定义为 `int`（4 字节）。

## 测试流程

### 1. 主函数执行流程

```
main()
├── 解析命令行参数（-P, -W, -N）
├── 验证参数有效性
│   ├── nbytes >= 512（必须）
│   └── 检查测试类型是否有效
│
├── 根据测试类型设置 need_buf2 标志
│   └── cp, fcp, bcopy 需要第二个缓冲区
│
├── 调用 benchmp() 执行基准测试
│   ├── init_loop(): 初始化内存缓冲区
│   ├── <测试函数>(): 执行实际测试（如 frd, fwr）
│   └── cleanup(): 释放内存
│
└── 调用 adjusted_bandwidth() 计算并输出带宽
```

### 2. 初始化阶段（init_loop）

```c
void init_loop(iter_t iterations, void *cookie)
{
    state_t *state = (state_t *) cookie;
    
    if (iterations) return;  // 仅在第一次调用时初始化
    
    // 1. 分配主缓冲区（对齐到页边界）
    state->buf = (TYPE *)valloc(state->nbytes);
    bzero((void*)state->buf, state->nbytes);
    
    // 2. 计算最后一个 512 字节块的起始位置
    state->lastone = (TYPE*)((char *)state->buf + state->nbytes - 512);
    
    // 3. 如果需要第二个缓冲区
    if (state->need_buf2 == 1) {
        // 分配比需要大 2048 字节的空间（用于对齐）
        state->buf2_orig = state->buf2 = (TYPE *)valloc(state->nbytes + 2048);
        
        // 如果设置了 aligned 标志，调整对齐
        if (state->aligned) {
            char *tmp = (char *)state->buf2;
            tmp += 2048 - 128;  // 偏移到特定对齐位置
            state->buf2 = (TYPE *)tmp;
        }
    }
}
```

**关键点**：
- 使用 `valloc()` 分配页对齐内存，提高访问效率
- `lastone` 指向最后一个完整的 512 字节块
- `buf2` 对齐方式影响缓存行冲突测试

### 3. 测试循环结构

所有测试函数都遵循相同的循环结构：

```c
while (iterations-- > 0) {           // 外层：重复测试
    register TYPE *p = state->buf;   // 重置指针到缓冲区起始
    while (p <= lastone) {           // 内层：遍历整个缓冲区
        // 执行 512 字节（128 个 int）的访问操作
        // ...
        p += 128;                    // 移动到下一个 512 字节块
    }
}
```

**每次迭代处理 512 字节块**（128 个 `int`，每个 4 字节）。

## 九种测试类型详解

### 测试 1: rd (稀疏读取)

**访问模式**：每 32 字节读取一个 4 字节 `int`（步长 stride = 8 个 int）

```c
void rd(iter_t iterations, void *cookie)
{
    state_t *state = (state_t *) cookie;
    register TYPE *lastone = state->lastone;
    register int sum = 0;

    while (iterations-- > 0) {
        register TYPE *p = state->buf;
        while (p <= lastone) {
            sum += 
            p[0] + p[4] + p[8] + p[12] + p[16] + p[20] + p[24] +
            p[28] + p[32] + p[36] + p[40] + p[44] + p[48] + p[52] +
            p[56] + p[60] + p[64] + p[68] + p[72] + p[76] +
            p[80] + p[84] + p[88] + p[92] + p[96] + p[100] +
            p[104] + p[108] + p[112] + p[116] + p[120] + p[124];
            p += 128;  // 移动 512 字节
        }
    }
    use_int(sum);  // 防止编译器优化掉计算
}
```

**访问特征**：
- 每个 512 字节块中读取 32 个位置（索引 0, 4, 8, ..., 124）
- 每次读取间隔 16 字节（4 个 int）
- 总共读取 32 × 4 = **128 字节** / 512 字节块
- **缓存行利用率低**：测试缓存行填充开销

### 测试 2: wr (稀疏写入)

**访问模式**：每 32 字节写入一个 4 字节 `int`

```c
void wr(iter_t iterations, void *cookie)
{
    state_t *state = (state_t *) cookie;
    register TYPE *lastone = state->lastone;

    while (iterations-- > 0) {
        register TYPE *p = state->buf;
        while (p <= lastone) {
            p[0] = 1; p[4] = 1; p[8] = 1; p[12] = 1; 
            p[16] = 1; p[20] = 1; p[24] = 1; p[28] = 1;
            // ... 共 32 次写入
            p[120] = 1; p[124] = 1;
            p += 128;
        }
    }
}
```

**访问特征**：
- 和 `rd` 相同的访问模式，但是写操作
- 测试**写分配**（write allocate）策略
- 测试缓存行失效（invalidation）开销

### 测试 3: rdwr (读-修改-写)

**访问模式**：先读后写同一位置

```c
void rdwr(iter_t iterations, void *cookie)
{
    state_t *state = (state_t *) cookie;
    register TYPE *lastone = state->lastone;
    register int sum = 0;

    while (iterations-- > 0) {
        register TYPE *p = state->buf;
        while (p <= lastone) {
            // 读取 + 写入同一位置
            sum += p[0]; p[0] = 1;
            sum += p[4]; p[4] = 1;
            // ... 共 32 次
            sum += p[124]; p[124] = 1;
            p += 128;
        }
    }
    use_int(sum);
}
```

**访问特征**：
- 测试**读-修改-写**（RMW）性能
- 模拟原子操作的访问模式
- 每个位置既读又写，测试缓存一致性

### 测试 4: cp (稀疏拷贝)

**访问模式**：从 buf 读取，写入到 buf2

```c
void mcp(iter_t iterations, void *cookie)
{
    state_t *state = (state_t *) cookie;
    register TYPE *lastone = state->lastone;
    TYPE* p_save = NULL;

    while (iterations-- > 0) {
        register TYPE *p = state->buf;      // 源
        register TYPE *dst = state->buf2;   // 目标
        while (p <= lastone) {
            // 拷贝 32 个位置
            dst[0] = p[0];
            dst[4] = p[4];
            // ...
            dst[124] = p[124];
            p += 128;
            dst += 128;
        }
        p_save = p;
    }
    use_pointer(p_save);
}
```

**访问特征**：
- 测试双缓冲区带宽
- 同时读写不同内存区域
- 模拟内存拷贝操作

### 测试 5: frd (密集顺序读取) ⭐

**访问模式**：读取所有 4 字节字（无间隔）

```c
void frd(iter_t iterations, void *cookie)
{
    state_t *state = (state_t *) cookie;
    register int sum = 0;
    register TYPE *lastone = state->lastone;

    while (iterations-- > 0) {
        register TYPE *p = state->buf;
        while (p <= lastone) {
            sum +=
            p[0] + p[1] + p[2] + p[3] + p[4] + p[5] + p[6] +
            p[7] + p[8] + p[9] + p[10] + p[11] + p[12] +
            // ... 读取所有 128 个 int
            p[126] + p[127];
            p += 128;
        }
    }
    use_int(sum);
}
```

**访问特征**：
- **连续读取所有数据**（100% 缓存行利用率）
- 每个 512 字节块读取 **512 字节**
- 测试**最大顺序读取带宽**
- 最接近实际流式读取应用

### 测试 6: fwr (密集顺序写入) ⭐

**访问模式**：写入所有 4 字节字

```c
void fwr(iter_t iterations, void *cookie)
{
    state_t *state = (state_t *) cookie;
    register TYPE *lastone = state->lastone;
    TYPE* p_save = NULL;

    while (iterations-- > 0) {
        register TYPE *p = state->buf;
        while (p <= lastone) {
            // 写入所有 128 个 int
            p[0] = 1; p[1] = 1; p[2] = 1; p[3] = 1;
            // ...
            p[127] = 1;
            p += 128;
        }
        p_save = p;
    }
    use_pointer(p_save);
}
```

**访问特征**：
- **连续写入所有数据**
- 测试**最大顺序写入带宽**
- 测试写缓冲区（write buffer）性能

### 测试 7: fcp (密集内存拷贝) ⭐

**访问模式**：拷贝所有 4 字节字

```c
void fcp(iter_t iterations, void *cookie)
{
    state_t *state = (state_t *) cookie;
    register TYPE *lastone = state->lastone;

    while (iterations-- > 0) {
        register TYPE *p = state->buf;
        register TYPE *dst = state->buf2;
        while (p <= lastone) {
            // 拷贝所有 128 个 int
            dst[0] = p[0];
            dst[1] = p[1];
            // ...
            dst[127] = p[127];
            p += 128;
            dst += 128;
        }
    }
}
```

**访问特征**：
- **连续拷贝所有数据**
- 测试**最大内存拷贝带宽**
- 同时读写，测试总线/内存控制器瓶颈

### 测试 8: bzero (系统调用清零)

**访问模式**：调用系统 `bzero()` 函数

```c
void loop_bzero(iter_t iterations, void *cookie)
{
    state_t *state = (state_t *) cookie;
    register TYPE *p = state->buf;
    register size_t N = state->N;

    while (iterations-- > 0) {
        bzero(p, N);  // 将 N 字节清零
    }
}
```

**访问特征**：
- 使用系统优化的 `bzero()` 实现
- 通常使用 SIMD 指令（SSE, AVX, NEON）
- 对比手工循环的性能差异

### 测试 9: bcopy (系统调用拷贝)

**访问模式**：调用系统 `bcopy()` 函数

```c
void loop_bcopy(iter_t iterations, void *cookie)
{
    state_t *state = (state_t *) cookie;
    register TYPE *p = state->buf;
    register TYPE *dst = state->buf2;
    register size_t N = state->N;

    while (iterations-- > 0) {
        bcopy(p, dst, N);  // 拷贝 N 字节
    }
}
```

**访问特征**：
- 使用系统优化的 `bcopy()` 实现
- 对比手工 `fcp` 的性能

## 性能计算方法

### 带宽计算公式

```c
void adjusted_bandwidth(uint64 time, uint64 bytes, uint64 iter, double overhd)
{
    double secs = ((double)time / (double)iter - overhd) / 1000000.0;
    double mb = bytes / 1000000.0;  // 转换为 MB（10^6）
    
    if (secs <= 0.) return;
    
    // 输出格式：数据量(MB)  带宽(MB/s)
    fprintf(ftiming, "%.2f %.2f\n", mb, mb/secs);
}
```

**关键点**：
- `time`: 总耗时（微秒）
- `iter`: 迭代次数
- `overhd`: 循环开销（本测试中为 0）
- `bytes`: 总数据量（字节）

**计算步骤**：
1. 计算每次迭代的平均时间：`time / iter`
2. 减去开销，转换为秒：`(time/iter - overhd) / 1e6`
3. 数据量转换为 MB：`bytes / 1e6`
4. 带宽 = 数据量 / 时间（MB/s）

### 实际数据量分析

对于不同测试类型，实际访问的数据量不同：

| 测试类型                 | 每 512 字节块实际访问 | 有效数据比例 |
| ------------------------ | --------------------- | ------------ |
| `rd`, `wr`, `rdwr`, `cp` | 128 字节（32 × 4）    | 25%          |
| `frd`, `fwr`, `fcp`      | 512 字节（128 × 4）   | 100%         |
| `bzero`, `bcopy`         | 全部数据              | 100%         |

**示例计算**（假设 `nbytes = 256MB`）：

- **frd/fwr/fcp**: 实际访问 256MB
- **rd/wr**: 实际访问 64MB（256MB × 25%）
- 但**输出带宽基于 256MB 计算**（报告的是"有效带宽"）

## 测试示例和结果解读

### 示例 1: 测试不同大小的顺序读取

```bash
# 测试 1MB 数据的顺序读取，单进程
./bw_mem 1m frd

# 输出示例
1.00 12345.67
# 解读：1.00 MB 数据，带宽 12345.67 MB/s
```

### 示例 2: 多进程并行测试

```bash
# 4 进程并行测试 256MB 内存拷贝
./bw_mem -P 4 256m fcp

# 输出示例
256.00 45678.90
# 解读：256 MB 数据，4 进程总带宽 45678.90 MB/s
```

### 示例 3: 测试多种内存大小

```bash
# 测试不同大小对带宽的影响
for size in 64k 512k 1m 4m 16m 64m 256m; do
    echo "=== 测试大小: $size ==="
    ./bw_mem -P 8 $size frd
done
```

**典型输出曲线**：
```
64k   → 高带宽（L1/L2 缓存内）
512k  → 中等带宽（L3 缓存内）
4m    → 带宽下降（开始访问主内存）
64m+  → 稳定带宽（纯主内存带宽）
```

### 示例 4: 对比不同访问模式

```bash
# 对比稀疏 vs 密集访问
./bw_mem -P 8 128m rd   # 稀疏读
./bw_mem -P 8 128m frd  # 密集读

# 典型结果
# rd:  ~8000 MB/s   (缓存行填充开销大)
# frd: ~32000 MB/s  (充分利用缓存行)
```

## 测试优化技巧

### 1. 循环展开（Loop Unrolling）

所有测试函数都使用了**显式循环展开**：

```c
// 宏定义简化代码
#define DOIT(i) p[i] = 1;

DOIT(0) DOIT(1) DOIT(2) ... DOIT(127);
```

**优势**：
- 减少循环控制开销（比较、跳转）
- 提高指令级并行度（ILP）
- 便于编译器优化（流水线、寄存器分配）

### 2. 寄存器变量

```c
register TYPE *p = state->buf;     // 指针存储在寄存器
register TYPE *lastone = ...;      // 避免重复内存访问
register int sum = 0;              // 累加器在寄存器
```

### 3. 防止编译器优化

```c
use_int(sum);        // 确保 sum 被实际使用
use_pointer(p_save); // 确保指针不被优化掉
```

这些函数定义在 `bench.h` 中，通过副作用阻止编译器消除"无用"计算。

### 4. 内存对齐

```c
state->buf = (TYPE *)valloc(state->nbytes);  // 页对齐分配
```

**好处**：
- 避免跨页访问
- 提高 TLB 命中率
- 减少缓存行分裂（cache line split）

## 与 CPU 绑核测试脚本的集成

在 `cpu_affinity_test.sh` 中的使用：

```bash
# 计算核心数
local process_count=4  # 例如 cpu0-3

# 测试多种内存大小
local memory_sizes=(64k 512k 1m 2m 4m 16m 32m 64m 128m 256m)

# 测试多种访问模式
local test_functions=(fwr frd rd wr rdwr cp fcp bzero bcopy)

# 执行测试
for mem_size in "${memory_sizes[@]}"; do
    for func in "${test_functions[@]}"; do
        taskset -c $cpu_set ./bw_mem -N 10 -W 3 -P $process_count $mem_size $func
    done
done
```

**参数选择**：
- `-N 10`: 10 次重复测试，取最小值（最稳定）
- `-W 3`: 3 次预热，稳定缓存状态
- `-P $process_count`: 并行数 = 绑定核心数

## 结果分析指南

### 1. 理解带宽曲线

**典型带宽 vs 数据大小曲线**：

```
带宽 (MB/s)
    ^
    |        ┌─────────┐  L1 Cache (高带宽)
50000|        │         │
    |    ┌───┘         └───┐  L2 Cache
40000|    │                 │
    |  ┌─┘                 └──┐  L3 Cache
30000|  │                      │
    |──┘                       └────  Main Memory (稳定)
20000|
    └─────────────────────────────────> 数据大小
      64k   512k  4m    16m    256m
```

### 2. 对比不同访问模式

| 访问模式         | 预期带宽比例 | 原因                       |
| ---------------- | ------------ | -------------------------- |
| `frd` (密集读)   | 100%         | 最大顺序读带宽             |
| `rd` (稀疏读)    | ~25%         | 缓存行利用率低             |
| `fwr` (密集写)   | 80-100%      | 写缓冲优化                 |
| `fcp` (密集拷贝) | 50-70%       | 同时读写竞争               |
| `bcopy`          | 60-80%       | 系统优化，但有函数调用开销 |

### 3. 多核扩展性分析

**理想情况**：
- 1 核心：10000 MB/s
- 2 核心：20000 MB/s（线性扩展）
- 4 核心：40000 MB/s
- 8 核心：60000 MB/s（开始饱和内存带宽）

**实际情况**：
- 受限于内存控制器带宽
- NUMA 架构影响
- 缓存一致性开销

### 4. 识别性能瓶颈

**带宽过低的可能原因**：
1. **数据大小太小**：完全在缓存内，无法测试内存带宽
2. **并行度不足**：单核无法饱和内存带宽
3. **访问模式不佳**：稀疏访问（rd/wr）缓存效率低
4. **内存频率限制**：DDR4-2400 vs DDR4-3200
5. **NUMA 远端访问**：跨 NUMA 节点性能下降

## 应用场景

### 1. 系统性能评估
- 评估内存子系统性能
- 对比不同内存配置（频率、通道数、NUMA）
- 验证硬件规格

### 2. 应用优化
- 选择最优访问模式（顺序 vs 随机）
- 评估数据结构布局影响
- 确定最佳数据块大小

### 3. 缓存层次分析
- 识别 L1/L2/L3 缓存大小
- 测量各级缓存带宽
- 优化工作集大小

### 4. 多核优化
- 确定最佳并行度
- 识别内存带宽瓶颈
- 优化线程/进程分配

## 实用测试建议

### 1. 基础测试

```bash
# 测试主内存带宽（数据远大于缓存）
./bw_mem -P 8 512m frd   # 顺序读
./bw_mem -P 8 512m fwr   # 顺序写
./bw_mem -P 8 512m fcp   # 内存拷贝
```

### 2. 缓存层次分析

```bash
# 测试不同大小找出缓存边界
for size in 32k 64k 128k 256k 512k 1m 2m 4m 8m 16m 32m; do
    ./bw_mem -P 1 $size frd
done
```

### 3. 多核扩展性测试

```bash
# 测试不同核心数的带宽
for cores in 1 2 4 8 12 16; do
    echo "=== $cores 核心 ==="
    taskset -c 0-$((cores-1)) ./bw_mem -P $cores 256m fcp
done
```

### 4. 访问模式对比

```bash
# 对比所有访问模式
size=128m
for pattern in rd wr rdwr cp frd fwr fcp bzero bcopy; do
    echo "=== $pattern ==="
    ./bw_mem -P 8 $size $pattern
done
```

## 总结

### 核心特点

✅ **测试全面**：9 种访问模式覆盖各种场景  
✅ **参数灵活**：支持并行、预热、重复控制  
✅ **优化充分**：循环展开、寄存器优化  
✅ **结果准确**：最小值统计，减少干扰  

### 关键优化技术

1. **512 字节块处理**：平衡循环开销和缓存效率
2. **显式循环展开**：减少 90%+ 循环控制开销
3. **寄存器变量**：关键指针/计数器常驻寄存器
4. **页对齐分配**：提高 TLB 和缓存效率
5. **防优化措施**：确保测试代码被实际执行

### 测试价值

- **内存带宽基准**：评估系统峰值内存性能
- **优化指导**：选择最优数据访问模式
- **性能调试**：识别内存瓶颈和缓存问题
- **硬件验证**：确认内存配置和性能

`bw_mem` 是理解和优化内存密集型应用的重要工具，结合 `lat_mem_rd`（延迟测试）可以全面评估内存子系统性能。