# Core-to-Core Latency 测试逻辑分析

## 项目概述

这是一个用 Rust 编写的 CPU 核心间延迟测试工具，通过多种测试方法**测量不同 CPU 核心之间的缓存一致性延迟**。

## 核心测试架构

### 1. 主程序流程 (main.rs)

```
main()
├── 解析命令行参数 (CliArgs)
├── 获取可用 CPU 核心列表
├── 过滤用户指定的核心
├── 显示 CPU 信息和测试参数
├── 创建高精度时钟 (quanta::Clock)
└── 根据 --bench 参数执行不同的测试
    ├── 测试 1: CAS 延迟测试 (单缓存行)
    ├── 测试 2: 读写延迟测试 (双缓存行)
    └── 测试 3: 消息传递测试 (多缓存行)
```

### 2. 命令行参数

| 参数             | 说明                           | 默认值   |
| ---------------- | ------------------------------ | -------- |
| `num_iterations` | 每个样本的迭代次数（往返次数） | 1000     |
| `num_samples`    | 样本数量                       | 300      |
| `--csv`          | 以 CSV 格式输出均值矩阵        | false    |
| `--bench`        | 选择测试方法 (1,2,3)           | 1        |
| `--cores`        | 指定要测试的核心 ID            | 所有核心 |

**典型调用**：
```bash
# 默认测试（所有核心，1000次迭代，300个样本）
./core-to-core-latency-aarch64-static

# 高精度测试
./core-to-core-latency-aarch64-static 5000

# 指定核心测试
./core-to-core-latency-aarch64-static 5000 --cores 0,1,2,3

# CSV 输出
./core-to-core-latency-aarch64-static 5000 --csv > results.csv
```

## 三种测试方法详解

### 测试 1: CAS 延迟 (cas.rs) - **默认测试**

**原理**：两个线程通过 Compare-And-Swap (CAS) 操作在同一个缓存行上进行 ping-pong 通信。

CAS（Compare-And-Swap，比较并交换）是一种**无锁的原子操作**，用于解决多线程环境下共享变量的并发修改问题。它通过硬件指令保证操作的原子性，无需使用互斥锁（如`synchronized`、`pthread_mutex`），从而减少线程阻塞的开销，提升并发性能。

**CAS 的核心特点**

- **原子性**：由 CPU 硬件指令（如`cmpxchg`）直接支持，操作不可中断，保证多线程环境下不会出现 “半更新” 状态；

- **无锁性**：无需加锁，线程不会因等待锁而阻塞，减少了上下文切换的开销；

- **乐观锁思想**：假设冲突概率较低，直接尝试修改，失败后再重试（而非提前加锁避免冲突），适合并发冲突较少的场景；

- **硬件依赖**：依赖 CPU 的原子指令支持（几乎所有现代 CPU 都支持，如 x86 的`cmpxchg`、ARM 的`ldrex/strex`）。

**测试流程**：
```
线程 Ping (Core A)                    线程 Pong (Core B)
      |                                      |
   绑定到 Core A                          绑定到 Core B
      |                                      |
   等待同步栅栏 ←────── Barrier ──────→ 等待同步栅栏
      |                                      |
   开始计时                                  |
      |                                      |
  while CAS(flag, PONG→PING) 失败:         |
      自旋等待 ────────────────────→  while CAS(flag, PING→PONG) 失败:
      |                                  自旋等待
   CAS 成功 ←────────────────────────  CAS 成功
      |                                      |
  (重复 num_iterations 次)                  |
      |                                      |
   结束计时                                  |
      |                                      |
   计算延迟 = 总时间 / 迭代次数 / 2          |
```

**关键代码**：
```rust
// Ping 线程
while state.flag.compare_exchange(
    PONG, PING, 
    Ordering::Relaxed, 
    Ordering::Relaxed
).is_err() {}

// Pong 线程
while state.flag.compare_exchange(
    PING, PONG,
    Ordering::Relaxed,
    Ordering::Relaxed
).is_err() {}
```

**测量内容**：
- **缓存一致性延迟**：修改共享缓存行的延迟
- **自旋锁性能**：适合评估锁竞争场景
- **MESI 协议开销**：缓存行在 Modified/Exclusive/Shared/Invalid 状态间切换的延迟

### 测试 2: 读写延迟 (read_write.rs)

**原理**：两个线程各自拥有独立的缓存行，通过读写操作进行通信。

**数据结构**：
```rust
struct Bench {
    barrier: CachePadded<Barrier>,        // 同步栅栏
    owned_by_ping: CachePadded<AtomicBool>,  // Ping 线程的缓存行
    owned_by_pong: CachePadded<AtomicBool>,  // Pong 线程的缓存行
}
```

**测试流程**：
```
线程 Ping (Core A)                    线程 Pong (Core B)
      |                                      |
   拥有缓存行 1                           拥有缓存行 2
      |                                      |
   等待同步                               等待同步
      |                                      |
   开始计时                                  |
      |                                      |
  while read(缓存行2) != v:                 |
      自旋等待 ←───────────────────  write(缓存行2, !v)
      |                             (Ordering::Release)
   (Acquire 语义)                            |
      |                                      |
  write(缓存行1, v)  ──────────────→  while read(缓存行1) != v:
  (Release 语义)                         自旋等待
      |                                (Acquire 语义)
   结束计时                                  |
```

**内存顺序**：
```rust
// Acquire-Release 保证因果依赖
while state.owned_by_pong.load(Ordering::Acquire) != v {}  // 读取
state.owned_by_ping.store(v, Ordering::Release);           // 写入
```

**测量内容**：
- **单写单读延迟**：两个独立缓存行的通信延迟
- **Store-Load 转发**：写入传播到其他核心的延迟
- **内存顺序开销**：Acquire-Release 语义的性能影响

### 测试 3: 消息传递 (msg_passing.rs)

**原理**：使用多个缓存行和高精度时钟（RDTSC）进行测试。

**前置要求**：
- 仅支持 x86/x86_64 架构
- 需要不变时间戳计数器 (Invariant TSC)
- RDTSC 读取延迟必须在 0.1-1000ns 范围内

**测量内容**：
- **消息传递延迟**：多缓存行场景下的通信延迟
- **RDTSC 性能**：验证时钟读取开销

## 统计分析方法

### 1. 测试执行策略

```
对于每对核心 (Core i, Core j):
    预热迭代 1 次（丢弃）
    for sample in 0..num_samples:
        测量 num_iterations 次往返的总时间
        计算单程延迟 = 总时间 / num_iterations / 2
        保存结果
```

### 2. 结果矩阵

生成一个 N×N×S 三维数组：
- **维度 1 (N)**: 核心 i
- **维度 2 (N)**: 核心 j  
- **维度 3 (S)**: num_samples 个延迟值

### 3. 统计指标

**均值计算**：
```rust
mean = Σ(samples) / num_samples
```

**标准差估计** (使用中心极限定理):
```rust
sample_stddev = sqrt(Σ(x - mean)² / (n-1))
stddev_of_mean = sample_stddev / sqrt(num_samples)
```

**输出格式**：
```
       0       1       2       3
  0
  1   52±6
  2   38±6    39±4
  3   39±5    39±6    38±6
      ^^^^    ^^^^
      |  |    |  +-- 标准差（经 CLT 调整）
      |  +------- 误差
      +---------- 平均延迟 (ns)
```

### 4. 全局统计

- **最小延迟**：所有核心对中的最小平均延迟
- **最大延迟**：所有核心对中的最大平均延迟  
- **平均延迟**：所有有效测量值的平均值

## 核心技术实现

### 1. 线程绑定

```rust
// 使用 core_affinity crate 实现 CPU 绑定
core_affinity::set_for_current(core_id);
```

### 2. 同步机制

```rust
// 使用 Barrier 确保两个线程同时开始
let barrier = Barrier::new(2);
barrier.wait();  // 两个线程都到达后继续
```

### 3. 高精度计时

```rust
// 使用 quanta crate 提供纳秒级精度
let clock = Clock::new();
let start = clock.raw();      // 获取原始时钟值
// ... 执行测试 ...
let end = clock.raw();
let duration = clock.delta(start, end).as_nanos();
```

### 4. 缓存行对齐

```rust
// 使用 CachePadded 避免伪共享
owned_by_ping: CachePadded<AtomicBool>,  // 独占一个缓存行
owned_by_pong: CachePadded<AtomicBool>,  // 独占另一个缓存行
```

## 测试结果解读

### 延迟值含义

| 延迟范围 | 可能原因                  |
| -------- | ------------------------- |
| < 20ns   | 同一 L2/L3 缓存，优秀性能 |
| 20-40ns  | 同一物理 CPU 包，正常范围 |
| 40-60ns  | 跨 NUMA 节点或不同 CCX    |
| > 60ns   | 跨 CPU 插槽或系统瓶颈     |

### 典型输出示例

```
Num cores: 10
Num round trips per samples: 1000
Num samples: 300

       0       1       2       3       4       5       6       7       8       9
  0
  1   52±6
  2   38±6    39±4
  3   39±5    39±6    38±6
  4   34±6    38±4    37±6    36±5
  5   38±5    38±6    38±6    38±6    37±6
  6   38±5    37±6    39±6    36±4    49±6    38±6
  7   36±6    39±5    39±6    37±6    35±6    36±6    38±6
  8   37±5    38±6    35±5    39±5    38±6    38±5    37±6    37±6
  9   48±6    39±6    36±6    39±6    38±6    36±6    41±6    38±6    39±6

Min  latency: 34.5ns ±6.1 cores: (4,0)
Max  latency: 52.1ns ±9.4 cores: (1,0)
Mean latency: 38.4ns
```

**分析**：
- 核心 4 和核心 0 之间延迟最低（34.5ns）→ 可能共享 L2/L3 缓存
- 核心 1 和核心 0 之间延迟最高（52.1ns）→ 可能跨不同缓存域
- 整体平均延迟 38.4ns → 系统整体缓存一致性性能良好

## 应用场景

### 1. 性能调优
- 识别最优线程绑定策略
- 发现 NUMA 架构的缓存拓扑
- 优化多线程应用的核心分配

### 2. 硬件评估
- 对比不同 CPU 的缓存一致性性能
- 验证 CPU 规格声称
- 评估超频/降频对延迟的影响

### 3. 系统诊断
- 检测硬件问题（异常高延迟）
- 验证 CPU 亲和性设置是否生效
- 分析虚拟化环境的性能损失

## 编译优化说明

### 静态链接的重要性

```bash
# 使用 musl libc 实现完全静态链接
cargo build --release --target aarch64-unknown-linux-musl
```

**优势**：
- ✅ 无需目标系统安装 glibc
- ✅ 可在任何 ARM64 Linux 系统运行
- ✅ 避免动态库版本冲突
- ✅ 便于在嵌入式系统部署

### 性能相关编译选项

```toml
[target.aarch64-unknown-linux-musl]
rustflags = [
    "-C", "target-feature=+crt-static",  # 静态 C 运行时
    "-C", "link-arg=-static"              # 静态链接
]
```

## 总结

core-to-core-latency 是一个专业的 CPU 微基准测试工具，通过三种不同的测试方法全面评估 CPU 核心间的缓存一致性性能：

1. **CAS 测试**（默认）：测量共享缓存行的竞争延迟
2. **读写测试**：测量独立缓存行的通信延迟
3. **消息传递**：使用高精度时钟的多缓存行测试

通过精心设计的统计方法和线程同步机制，该工具能够提供准确、可重复的核心间延迟测量结果，是系统性能分析和优化的重要工具。

