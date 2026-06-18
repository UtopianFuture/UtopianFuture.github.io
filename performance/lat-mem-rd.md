# **lat_mem_rd 实现原理与测试重点分析**

## **1. 核心目标**

测量**内存读取延迟 (Memory Load Latency)**，通过指针追踪链表结构来强制产生数据依赖，从而精确测量不同层次存储器（L1/L2/L3 Cache、DRAM）的访问延迟。

## **2. 实现原理**

### **2.1 核心数据结构**

```c
struct mem_state {
    char*  addr;        // malloc 返回的原始指针
    char*  base;        // 页对齐的基地址
    char*  p[16];       // 16 个并行指针（支持多线程测试）
    size_t len;         // 测试范围大小（range）
    size_t maxlen;      // 最大内存大小（len）
    size_t line;        // 步长（stride）
    size_t pagesize;    // 页大小（通常 4KB）
    size_t nlines;      // 每页的缓存行数
    size_t npages;      // 总页数
    size_t nwords;      // 每个缓存行的指针数
    ...
};
```

### **2.2 关键技术：指针追踪链表 (Pointer Chasing)**

**核心思想**：构造一个循环链表，每个节点是一个指针，指向下一个节点。CPU 必须先读取当前指针的值，才能计算下一个访问地址，这样形成**数据依赖链**，阻止乱序执行和预取。

#### **stride_initialize（顺序访问模式）**

```c
void stride_initialize(...) {
    // 按固定步长构造线性链表
    for (i = 0; i < nbytes; i += stride) {
        *(char **)&addr[i] = (char*)&addr[i + stride];
    }
    // 最后一个指向开头，形成循环
    *(char **)&addr[i - stride] = (char*)&addr[0];
}
```

**访问模式**：

**优点**：

- **顺序访问**：充分利用硬件预取器
- **Cache 友好**：空间局部性好
- 适合测量 **带宽 + 延迟**

#### **thrash_initialize（随机访问模式）**

```c
void thrash_initialize(...) {
    // 随机化页面访问顺序
    state->pages = words_initialize(state->npages, state->pagesize);
    
    // 随机化缓存行内的访问顺序
    state->words = words_initialize(state->nwords, state->line);
    
    // 构造随机链表：每次访问不同页面的不同缓存行
    for (i = 0; i < state->npages - 1; ++i) {
        cpage = state->pages[i];       // 当前页
        npage = state->pages[i + 1];   // 下一个页（随机）
        for (j = 0; j < state->nwords; ++j) {
            cur = cpage + state->words[...];   // 当前偏移（随机）
            next = npage + state->words[...];  // 下一个偏移（随机）
            *(char **)&addr[cur] = (char*)&addr[next];
        }
    }
}
```

**访问模式**：

```
随机页1:随机行A → 随机页2:随机行B → 随机页3:随机行C → ...
```

**优点**：

- **击败预取器**：随机访问无法预测
- **TLB 压力测试**：每次访问不同页面
- **Cache 颠簸**：最大化缓存失效率
- 适合测量**纯延迟**（最坏情况）

### **2.3 核心测试循环**

```c
#define ONE     p = (char **)*p;   // 读取指针指向的值
#define FIVE    ONE ONE ONE ONE ONE
#define TEN     FIVE FIVE
#define FIFTY   TEN TEN TEN TEN TEN
#define HUNDRED FIFTY FIFTY        // 展开 100 次

void benchmark_loads(iter_t iterations, void *cookie) {
    register char **p = (char**)state->p[0];  // 寄存器变量
    register size_t count = state->len / (state->line * 100) + 1;
    
    while (iterations-- > 0) {
        for (i = 0; i < count; ++i) {
            HUNDRED;  // 100 次指针追踪
        }
    }
}
```

**关键点**：

- `p = (char **)*p;` 形成严格的数据依赖：必须先读 `*p`，才能更新 `p`
- **100 次循环展开**：减少循环开销
- **register 变量**：强制使用寄存器，减少栈访问

## **3. 测试策略**

### **3.1 参数扫描**

**range 增长策略（step 函数）**：

```c
size_t step(size_t k) {
    if (k < 1024)           return k * 2;       // 512 → 1KB：2倍增长
    else if (k < 4*1024)    return k + 1024;    // 1KB → 4KB：1KB 步进
    else {
        size_t s;
        for (s = 4KB; s <= k; s *= 2);
        return k + s / 4;                       // >4KB：25% 增长
    }
}
```

**测试序列示例**（`lat_mem_rd 128` 测试 128MB）：

```c
512B, 1KB, 2KB, 3KB, 4KB,      // L1 Cache 范围
5KB, 6KB, 8KB, 12KB, 16KB,     // L1 → L2 过渡
24KB, 32KB, 48KB, 64KB,        // L2 Cache 范围
96KB, 128KB, 192KB, 256KB,     // L2 → L3 过渡
384KB, 512KB, 768KB, 1MB,      // L3 Cache 范围
1.5MB, 2MB, 3MB, 4MB,          // L3 → DRAM 过渡
6MB, 8MB, 12MB, 16MB,          // DRAM 范围
...
128MB                           // 超出所有 Cache
```

**为什么这样设计？**

- **密集采样小范围**：L1/L2 边界敏感，需要精确定位
- **稀疏采样大范围**：DRAM 延迟平稳，无需过密
- **非线性增长**：平衡测试时间和精度

### **3.2 Stride（步长）影响**

**默认值**：`STRIDE = 512 / sizeof(char*) = 64`（假设 64 位系统）

**不同 stride 的含义**：

- **stride = 64B**：每个 Cache Line（64 字节）访问一次
  - **测量**：Cache Line 填充延迟
  - **场景**：流式访问（如数组扫描）
- **stride = 256B**：跨越 4 个 Cache Line
  - **测量**：减少 Cache 污染
  - **场景**：稀疏数据结构
- **stride = 4KB**：每个页面访问一次
  - **测量**：TLB 性能
  - **场景**：大内存遍历

**命令行示例**：

```c
# 测试不同步长
lat_mem_rd 128 64    # 64 字节步长
lat_mem_rd 128 256   # 256 字节步长
lat_mem_rd 128 4k    # 4KB 步长（TLB 测试）
```

## **4. 测试重点**

### **4.1 检测 Cache 层次结构**

**典型延迟曲线**：

```c
延迟 (ns)
   |
 8 |                                    ┌─────────── DRAM
   |                                ┌───┘
 4 |                        ┌───────┘ L3 Cache
   |                  ┌─────┘
 2 |            ┌─────┘ L2 Cache
   |      ┌─────┘
 1 |──────┘ L1 Cache
   |
   └─────────────────────────────────────────> Range (KB)
     32KB    256KB     8MB            128MB
```

**关键拐点**：

- **32KB → 256KB**：L1 → L2 跳变
- **256KB → 8MB**：L2 → L3 跳变
- **8MB+**：L3 → DRAM 跳变

### **4.2 并行性测试（`-P` 参数）**

```c
lat_mem_rd -P 1 128   # 单线程
lat_mem_rd -P 4 128   # 4 线程并行
```

**测量内容**：

- **Cache 一致性协议开销**
- **内存带宽竞争**
- **NUMA 影响**

### **4.3 预热与重复（`-W` 和 `-N`）**

```c
lat_mem_rd -W 100 -N 10 128
```

- **Warmup**：让数据进入 Cache，测量最佳情况延迟
- **Repetitions**：多次测试取最小值，减少噪声

### **4.4 Thrash 模式（`-t`）**

```c
lat_mem_rd -t 128
```

**对比测试**：

```c
默认模式：       L1=1ns, L2=3ns, L3=10ns, DRAM=50ns
Thrash 模式：    L1=2ns, L2=8ns, L3=30ns, DRAM=100ns
```

**差异来源**：

- 随机访问 → Cache 预取失效
- 页面跳跃 → TLB 失效
- Cache 颠簸 → 替换策略压力

## **5. 典型应用场景**

### **5.1 性能调优**

```c
# 发现 L2 Cache 过小导致性能下降
lat_mem_rd 128 | grep "0\.25"  # 256KB 附近延迟跳变
```

### **5.2 架构分析**

```c
# 识别 Cache Line 大小
lat_mem_rd 1 32 64 128 256  # 对比不同 stride
```

### **5.3 NUMA 评估**

```c
# 本地 vs 远程内存延迟
numactl --cpubind=0 --membind=0 lat_mem_rd 128  # 本地
numactl --cpubind=0 --membind=1 lat_mem_rd 128  # 远程
```

## **6. 代码优化技巧**

### **6.1 为什么用指针追踪？**

**对比其他方法**：

```c
// ❌ 错误：可被编译器优化
for (i = 0; i < len; i++)
    sum += array[i];  // 编译器可能向量化

// ✅ 正确：强制数据依赖
p = array;
for (i = 0; i < len; i++)
    p = (char**)*p;   // 必须等待上一次读取完成
```

### **6.2 为什么展开 100 次？**

```c
// 减少循环开销：
// 100 次内存访问 + 1 次循环判断
// vs
// 100 次内存访问 + 100 次循环判断
```

### **6.3 为什么用 register？**

```c
register char **p;  // 强制编译器使用寄存器
// 避免 p 本身访问造成额外内存读取
```

## **7. 实际输出解读**

**示例输出**：

```c
"stride=64
0.00049 1.234     # 512B: L1 Cache (1.2ns)
0.03125 1.456     # 32KB: L1 边界
0.06250 3.789     # 64KB: L2 Cache (3.8ns)
0.25000 4.123     # 256KB: L2 边界
8.00000 12.567    # 8MB: L3 Cache (12.6ns)
32.00000 45.890   # 32MB: DRAM (45.9ns)
```

**列说明**：

- **第 1 列**：range（MB）
- **第 2 列**：每次 load 的平均延迟（纳秒）

------

## **总结**

| 测试重点       | 技术手段              | 关键参数                                                     |
| -------------- | --------------------- | ------------------------------------------------------------ |
| **Cache 层次** | 指针追踪 + range 扫描 | [len](vscode-file://vscode-app/d:/program/vscode/Microsoft VS Code/resources/app/out/vs/code/electron-browser/workbench/workbench.html), `stride` |
| **TLB 性能**   | Thrash 模式           | `-t`, `stride=4k`                                            |
| **并行竞争**   | 多线程测试            | `-P 4/8/16`                                                  |
| **预取效果**   | stride vs thrash      | `-t` vs 默认                                                 |
| **最坏延迟**   | 随机访问              | `-t`                                                         |
| **最佳延迟**   | 预热 + 重复           | `-W 100 -N 10`                                               |

这是一个**极其精巧的微基准测试**，通过最小化的代码量，实现了对整个存储层次结构的全面测试！