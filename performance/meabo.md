# Meabo 微基准测试套件分析

## 项目概述

**Meabo (Memory Access Benchmark)**是由 ARM Limited 开发的综合性微基准测试套件，专门用于评估ARM架构处理器的**内存访问模式、计算性能和并行效率**。

### 核心设计理念

- **微内核方法**：10个独立的Phase，每个测试特定的计算模式；
- **多线程并行**：基于OpenMP的线程级并行；
- **CPU亲和性**：精确控制每个Phase的CPU绑定；
- **性能监控**：可选的PAPI硬件计数器和能耗监控；
- **灵活配置**：支持选择性运行Phase，独立配置CPU亲和性；

## 测试架构

### 10个测试Phase概览

| Phase        | 名称         | 测试重点   | 主要操作                                  | 内存模式   |
| ------------ | ------------ | ---------- | ----------------------------------------- | ---------- |
| **Phase 1**  | 浮点整数混合 | 寄存器重用 | 平方、除法、模运算                        | 块遍历     |
| **Phase 2**  | 间接向量乘加 | 间接访问   | `dest[i] += src1[i] * src2[ind[i]]`       | 单层间接   |
| **Phase 3**  | 归约操作     | 并行归约   | 向量累加 + reduction                      | 顺序访问   |
| **Phase 4**  | 向量加法     | 基础SIMD   | `dest[i] = src1[i] + src2[i]`             | 流式访问   |
| **Phase 5**  | 双间接加法   | 复杂间接   | `dest[i] = src1[ind1[i]] + src2[ind2[i]]` | 双层间接   |
| **Phase 6**  | 稀疏矩阵乘法 | 不规则访问 | SpMV (CSR格式)                            | 稀疏访问   |
| **Phase 7**  | 链表遍历     | 指针追踪   | 链表顺序访问                              | 链式访问   |
| **Phase 8**  | 静电力计算   | 科学计算   | 3D距离 + 库仑定律                         | 相邻元素   |
| **Phase 9**  | 回文数计算   | 整数运算   | 数字回文检测                              | 控制流密集 |
| **Phase 10** | 随机内存访问 | GUPS类测试 | 随机读写                                  | 完全随机   |

## 数据结构

### 核心数组

```c
// Phase 1, 3: 浮点和整数数组
double *vals;                          // array_size (默认 2^20)
int *int_vals;                         // array_size

// Phase 2, 4, 5: 向量操作数组
double *src1, *src2, *dest;           // array_size
int *ind_src1, *ind_src2;             // 间接索引数组

// Phase 6: 稀疏矩阵 (CSR格式)
double **sparse_matrix_values;         // nrow × nonzeros_per_row
int **sparse_matrix_indeces;           // 列索引
int *sparse_matrix_nonzeros;           // 每行非零元素数
double *vect_in;                       // ncol (默认 2^14)
double *vect_out;                      // nrow (默认 2^14)

// Phase 7: 链表
typedef struct linked_list {
    struct linked_list *next;
    double value;
} linked_list;
linked_list **llist;                   // num_cpus个独立链表

// Phase 8: 粒子系统
typedef struct particle {
    int x, y, z;                       // 3D坐标
    double charge;                     // 电荷
} particle;
particle *particles;                   // num_particles (默认 2^20)
double *forces;                        // 静电力结果

// Phase 9: 回文数
unsigned long *palindromes;            // num_palindromes (默认 2^10)

// Phase 10: 随机访问
int *randomloc;                        // num_randomloc (默认 2^21)
```

### 默认配置参数

```c
int num_iterations = 1000;             // 迭代次数
long array_size = 1 << 20;            // 1,048,576 (1M)
long llist_size = 1 << 24;            // 16,777,216 (16M)
int nrow = 1 << 14;                    // 16,384 行
int ncol = 1 << 14;                    // 16,384 列
int num_particles = 1 << 20;          // 1,048,576 粒子
int num_palindromes = 1 << 10;        // 1,024 个回文数
int num_randomloc = 1 << 21;          // 2,097,152 位置
int block_size = 8;                    // 块大小
```

## Phase 详细分析

### Phase 1: 浮点整数混合运算（数据重用）

**测试目标**：评估寄存器重用、L1缓存性能、混合浮点/整数运算

**核心代码**：
```c
for (int iter = 0; iter < num_iterations; ++iter) {
  #pragma omp for simd
  for (int i = 0; i < array_size; i += block_size) {
    for (int j = i; j < i + block_size; ++j) {
      // 浮点运算链
      temp1 = vals[j];
      temp1 *= temp1;                  // 平方
      temp2 = temp1 + vals[j];         // 加法
      temp3 = temp2 / (1024 + temp1);  // 除法
      temp3 -= vals[j];                // 减法
      vals[j] = temp3;
      
      // 整数运算链
      int_temp1 = int_vals[j];
      int_temp1 *= int_temp1;
      int_temp2 = int_temp1 + int_vals[j];
      int_temp3 = int_temp2 / (1024 + int_temp1);
      int_temp3 -= int_vals[j];
      int_vals[j] += (int_temp1 + int_temp2 + int_temp3) % 1024;
    }
  }
}
```

**访问模式**：
```
块遍历 (block_size=8):
[0-7] [8-15] [16-23] ...

内存访问: 顺序，块内重复访问
计算强度: 高 (13个浮点运算 + 13个整数运算 per element)
```

**性能特点**：
- ✅ **寄存器压力测试**：多个临时变量（temp1-3, int_temp1-3）
- ✅ **L1缓存友好**：块内重复访问同一元素
- ✅ **SIMD友好**：`#pragma omp for simd` 显式向量化
- ✅ **计算密集**：13 FLOP + 13 整数运算 per load/store

**典型性能指标**：
- 执行时间主要受限于计算能力而非内存带宽
- 良好的向量化可提升3-4倍性能（AVX2/NEON）

---

### Phase 2: 单层间接向量乘加

**测试目标**：评估间接内存访问、预取效率、Gather操作

**核心代码**：
```c
for (int iter = 0; iter < num_iterations; ++iter) {
  #pragma omp for
  for (int i = 0; i < array_size; ++i) {
    dest[i] += src1[i] * src2[ind_src2[i]];
  }
}
```

**访问模式**：
```
src1[i]:      顺序访问 [0, 1, 2, 3, ...]
src2[ind[i]]: 间接访问 [rand, rand, rand, ...]
dest[i]:      顺序写入 [0, 1, 2, 3, ...]

间接索引生成: ind_src2[i] = rand() % array_size
```

**性能瓶颈**：
- ❌ **随机访问**：`src2[ind_src2[i]]` 破坏预取
- ❌ **TLB压力**：大范围随机访问导致TLB miss
- ✅ **Gather指令**：AVX2/AVX-512可硬件加速

**与顺序访问对比**：
```
顺序访问 (Phase 4): ~50 GB/s
间接访问 (Phase 2): ~10-20 GB/s (降低60-80%)
```

---

### Phase 3: 向量归约操作

**测试目标**：评估并行归约、原子操作、同步开销

**核心代码**：
```c
for (int iter = 0; iter < num_iterations; ++iter) {
  #pragma omp for reduction(+:tmp_reduction_var)
  for (int i = 0; i < array_size; ++i) {
    vals[i] += 8;
    tmp_reduction_var += vals[i];
  }
  
  // 精度修正
  int tmp_rounding = tmp_reduction_var * 1000000;
  tmp_reduction_var = tmp_rounding / 1000000;
  *reduction_var = fmod(tmp_reduction_var, 1024);
  
  // 广播结果
  #pragma omp for
  for (int i = 0; i < array_size; ++i) {
    vals[i] = *reduction_var;
  }
}
```

**关键机制**：
1. **OpenMP归约**：`reduction(+:tmp_reduction_var)`
   - 编译器自动生成线程本地副本
   - 最后合并所有线程的结果
   
2. **精度修正**：避免浮点累积误差
   ```c
   tmp_rounding = tmp_reduction_var * 1000000;
   tmp_reduction_var = tmp_rounding / 1000000;
   ```

3. **广播操作**：所有元素设为相同值

**性能特点**：
- ⚠️ **归约开销**：多线程同步成本
- ✅ **向量化友好**：加法和写入都可SIMD化
- ⚠️ **伪共享风险**：如果`*reduction_var`在共享缓存行

**扩展性分析**：
```
1核:  100% 基准
2核:  180% (归约开销10%)
4核:  340% (归约开销15%)
8核:  600% (归约开销25%)
```

---

### Phase 4: 基础向量加法（STREAM风格）

**测试目标**：评估内存带宽、SIMD效率、Load-Store吞吐量

**核心代码**：
```c
for (int iter = 0; iter < num_iterations; ++iter) {
  #pragma omp for
  for (int i = 0; i < array_size; ++i) {
    dest[i] += src1[i] + src2[i];
  }
}
```

**内存访问分析**：
```
读取: src1[i] (8 bytes) + src2[i] (8 bytes) + dest[i] (8 bytes)
写入: dest[i] (8 bytes)
总传输: 4 × 8 bytes = 32 bytes per iteration

数据量 = array_size × 32 bytes × num_iterations
       = 1,048,576 × 32 × 1000
       = 32 GB (默认配置)
```

**与STREAM对比**：
| 测试         | 操作                | 读   | 写   | 总传输 |
| ------------ | ------------------- | ---- | ---- | ------ |
| STREAM Triad | `a[i]=b[i]+s*c[i]`  | 16B  | 8B   | 24B    |
| Meabo Phase4 | `d[i]+=s1[i]+s2[i]` | 24B  | 8B   | 32B    |

**性能指标**：
```
理论带宽 (DDR4-3200双通道): 51.2 GB/s
实际带宽 (Phase 4):         38-45 GB/s (75-88%)
SIMD加速:                   2-4倍 (AVX2/NEON)
```

---

### Phase 5: 双层间接向量加法

**测试目标**：评估复杂间接访问、多Gather操作、缓存友好性

**核心代码**：
```c
for (int iter = 0; iter < num_iterations; ++iter) {
  #pragma omp for
  for (int i = 0; i < array_size; ++i) {
    dest[i] += src1[ind_src1[i]] + src2[ind_src2[i]];
  }
}
```

**访问模式**：
```
顺序访问: dest[i], ind_src1[i], ind_src2[i]
随机访问: src1[ind_src1[i]], src2[ind_src2[i]]

每次迭代需要5次内存访问 (3顺序 + 2随机)
```

**性能退化分析**：
```
Phase 4 (顺序):      ~40 GB/s
Phase 5 (双间接):    ~8-12 GB/s (降低70-80%)

原因:
1. 两次随机Gather操作
2. 预取器失效
3. TLB miss率高
4. 缓存局部性差
```

**硬件优化**：
- AVX-512: `vgatherdpd` 指令
- ARM SVE: Gather-load指令
- 但仍受限于随机访问延迟

---

### Phase 6: 稀疏矩阵-向量乘法 (SpMV)

**测试目标**：评估不规则访问、分支预测、内存访问随机性

**核心代码**：
```c
for (int iter = 0; iter < num_iterations/5; ++iter) {
  #pragma omp for private(reduction_var)
  for (int i = 0; i < nrow; ++i) {
    reduction_var = 0.0;
    double *restrict values = sparse_matrix_values[i];
    int *restrict cols = sparse_matrix_indeces[i];
    const int nonzeros = sparse_matrix_nonzeros[i];
    
    // 稀疏行内积
    for (int j = 0; j < nonzeros; ++j) {
      reduction_var += values[j] * vect_in[cols[j]];
    }
    vect_out[i] = reduction_var;
  }
}
```

**稀疏矩阵格式 (CSR)**：
```
矩阵:
  [1.0  0   3.0  0  ]  → values=[1.0, 3.0], cols=[0, 2], nonzeros=2
  [0    2.0 0    4.0]  → values=[2.0, 4.0], cols=[1, 3], nonzeros=2
  [5.0  0   0    6.0]  → values=[5.0, 6.0], cols=[0, 3], nonzeros=2

向量: vect_in = [x0, x1, x2, x3]
结果: vect_out[0] = 1.0*x0 + 3.0*x2
```

**数据初始化**：
```c
for (i = 0; i < nrow; ++i) {
  sparse_matrix_nonzeros[i] = rand() % ncol;  // 随机非零元素数
  const int nonzeros = sparse_matrix_nonzeros[i];
  
  // 为每行分配存储
  sparse_matrix_indeces[i] = malloc(sizeof(int) * nonzeros);
  sparse_matrix_values[i] = malloc(sizeof(double) * nonzeros);
  
  // 随机生成列索引（无重复）
  for (j = 0; j < nonzeros; ++j) {
    col = rand() % ncol;
    while (已存在(col)) {
      col = rand() % ncol;
    }
    cols[j] = col;
    values[j] = (rand() % 1024) * cos(rand() % 1024);
  }
}
```

**性能特点**：
- ❌ **极度不规则**：每行非零元素数不同
- ❌ **随机列访问**：`vect_in[cols[j]]` 破坏缓存局部性
- ❌ **内循环不定长**：分支预测困难
- ⚠️ **并行可扩展性差**：行间独立，但每行内串行归约

**优化策略**：
1. **行重排序**：按非零元素数分组
2. **列索引排序**：提高缓存命中率
3. **分块CSR**：减少间接访问
4. **向量化**：需要特殊硬件支持（SVE）

**典型性能**：
```
FLOP: 2 × sum(nonzeros_per_row) × nrow
内存: 极不规则，难以预测

实际吞吐: 1-5 GFLOPS (远低于峰值)
受限因素: 内存随机访问延迟
```

---

### Phase 7: 链表遍历

**测试目标**：评估指针追踪、L1缓存延迟、顺序依赖性

**核心代码**：
```c
#pragma omp parallel firstprivate(llist)
{
  linked_list *start_node = llist[omp_get_thread_num()];
  linked_list *orig_cur_node = malloc(sizeof(linked_list*));
  linked_list *cur_node;
  
  for (int iter = 0; iter < num_iterations; ++iter) {
    cur_node = orig_cur_node;
    cur_node->value = start_node->value;
    cur_node->next = start_node->next;
    
    // 链表遍历
    while (cur_node != NULL) {
      cur_node = cur_node->next;  // 指针追踪
    }
  }
}
```

**数据结构**：
```c
typedef struct linked_list {
    struct linked_list *next;  // 8 bytes (64-bit)
    double value;              // 8 bytes
} linked_list;                 // 总共16 bytes

每个线程独立链表: llist_size = 16M 节点 (默认)
总内存: num_cpus × 16M × 16 bytes
```

**初始化**：
```c
for (i = 0; i < num_cpus; ++i) {
  linked_list *node = llist[i];
  for (j = 0; j < llist_size; ++j) {
    node->value = j;
    if (j == llist_size - 1) {
      node->next = NULL;
    } else {
      node->next = (linked_list*) malloc(sizeof(linked_list));
    }
    node = node->next;
  }
}
```

**性能特点**：
- ❌ **完全串行**：每次访问依赖前一次 (`cur_node = cur_node->next`)
- ❌ **无法预取**：硬件预取器无法识别链式模式
- ❌ **无法向量化**：指针追踪无法SIMD化
- ✅ **测量延迟**：直接反映L1/L2/L3缓存延迟

**延迟计算**：
```
总时间 = num_iterations × llist_size × 平均访问延迟

如果 Phase 7 耗时 T 纳秒:
平均延迟 = T / (num_iterations × llist_size)

典型值:
- L1命中: 4-5 cycles (~1-2 ns @ 3GHz)
- L2命中: 12-15 cycles (~4-5 ns)
- L3命中: 40-60 cycles (~15-20 ns)
- 内存: 200-300 cycles (~70-100 ns)
```

**链表大小影响**：
```
小链表 (< L1):         ~1-2 ns/access
中链表 (L1 < size < L2): ~4-5 ns/access
大链表 (> L3):         ~70-100 ns/access (16M默认)
```

**多线程扩展性**：
- ✅ **完美并行**：每线程独立链表，无数据共享
- ✅ **线性加速**：8核 = 8倍吞吐量
- ⚠️ **内存带宽瓶颈**：多线程可能饱和内存带宽

---

### Phase 8: 静电力计算（科学计算）

**测试目标**：评估浮点计算密集型负载、FMA效率、数学函数性能

**核心代码**：
```c
#pragma omp parallel shared(forces) firstprivate(particles)
{
  double k = 8.987551 * 1e9;  // 库仑常数 (N·m²/C²)
  
  for (int iter = 0; iter < num_iterations; ++iter) {
    #pragma omp for simd
    for (int i = 0; i < num_particles-1; ++i) {
      // 3D距离计算 (欧几里得距离平方)
      double r = (particles[i+1].x - particles[i].x) *
                 (particles[i+1].x - particles[i].x) +
                 (particles[i+1].y - particles[i].y) *
                 (particles[i+1].y - particles[i].y) +
                 (particles[i+1].z - particles[i].z) *
                 (particles[i+1].z - particles[i].z);
      
      // 库仑定律: F = k * q1 * q2 / r²
      forces[i] = (k * particles[i].charge * particles[i+1].charge) / r;
    }
  }
}
```

**计算分析**：
```
每个粒子对的计算:
- 6次减法 (x, y, z各2次)
- 3次乘法 (平方)
- 2次加法 (累加距离)
- 3次乘法 (k × q1 × q2)
- 1次除法 (/ r²)

总计: 15个浮点运算/粒子对
```

**内存访问**：
```
读取: particles[i]   (16 bytes: 3×int + 1×double)
      particles[i+1] (16 bytes)
写入: forces[i]      (8 bytes)

总传输: 40 bytes/粒子对
计算强度: 15 FLOP / 40 bytes = 0.375 FLOP/byte
```

**计算强度分析**：
```
Roofline模型:
- 内存带宽限制: 50 GB/s → 18.75 GFLOPS (50 × 0.375)
- CPU峰值计算: 200 GFLOPS (假设)

瓶颈: 计算密集型 (0.375 < 1)
      但接近平衡点，受益于FMA指令
```

**性能优化**：
1. **FMA融合乘加**：
   ```c
   // 编译器优化: x² → x * x 使用FMA
   r = fma(dx, dx, fma(dy, dy, dz * dz));
   ```

2. **SIMD向量化**：
   - AVX2: 4个double并行 → 4倍加速
   - AVX-512: 8个double并行 → 8倍加速
   - NEON: 2个double并行 → 2倍加速

3. **访问模式优化**：
   - 相邻粒子访问 (`i` 和 `i+1`)
   - 顺序读取，预取友好
   - 可能的AoS→SoA转换提升性能

**真实物理意义**：
```
模拟: N体问题的简化版本
应用: 分子动力学、静电模拟、天体物理
复杂度: O(N) (仅相邻粒子)，真实N体是O(N²)
```

---

### Phase 9: 回文数计算（整数密集）

**测试目标**：评估整数运算、分支预测、控制流密集型负载

**核心代码**：
```c
#pragma omp parallel private(found, num, is_palindrome)
{
  for (int iter = 0; iter < num_iterations/10; ++iter) {
    #pragma omp for
    for (int i = 0; i < num_entries; ++i) {
      if (i == 0) {
        palindromes[i] = 0;
      } else {
        // 从上次找到的回文数继续
        if (i > latest_i) {
          found = latest_i;
          num = palindromes[found] + 1;
        } else {
          found = -1;
          num = 0;
        }
        
        // 寻找第i个回文数
        while (found < i) {
          if (num < 10) {
            is_palindrome = 1;
          } else {
            // 检查是否回文
            is_palindrome = 1;
            unsigned long tmp_num = num;
            unsigned int length = 0;
            
            // 计算数字长度
            while (tmp_num) {
              length += 1;
              tmp_num /= 10;
            }
            
            tmp_num = num;
            // 两端比较
            while (tmp_num) {
              if ((tmp_num % 10) !=
                  (tmp_num / (unsigned long)pow(10, (length-1)))) {
                is_palindrome = 0;
                break;
              }
              tmp_num %= (unsigned long)pow(10, (length-1));
              tmp_num /= 10;
              length -= 2;
            }
          }
          
          if (is_palindrome) {
            ++found;
          }
          ++num;
        }
        palindromes[i] = num - 1;
      }
    }
  }
}
```

**回文数示例**：
```
前20个回文数:
0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 11, 22, 33, 44, 55, 66, 77, 88, 99, 101, ...

palindromes[0] = 0
palindromes[1] = 1
palindromes[10] = 11
palindromes[100] = 1001
```

**算法复杂度**：
```
对于palindromes[i]:
- 需要找到第i个回文数
- 从0开始检查每个数是否回文
- 最坏情况: O(palindromes[i] × log10(palindromes[i]))

总复杂度: O(num_entries × avg_palindrome_value × log10(avg))
```

**性能特点**：
- ⚠️ **分支密集**：每个数检查回文涉及多次分支
- ⚠️ **不可预测分支**：回文分布不规则
- ❌ **难以向量化**：控制流复杂
- ⚠️ **整数除法/模运算**：高延迟操作 (20-40 cycles)

**优化挑战**：
```
1. pow(10, length-1): 浮点幂运算开销大
   → 预计算10的幂次表: [1, 10, 100, 1000, ...]

2. 数字拆分: tmp_num % 10, tmp_num / 10
   → 编译器优化: 使用乘法魔数替代除法

3. 并行化难度: latest_i, latest_pal 共享变量
   → 数据竞争风险
```

**典型执行时间**：
```
num_palindromes = 1024:
- 单线程: ~500-1000 ms
- 多线程: 加速比 < 线程数 (同步开销)
```

---

### Phase 10: 随机内存访问 (GUPS类)

**测试目标**：评估随机内存访问性能、TLB效率、内存控制器随机访问能力

**核心代码**：
```c
#pragma omp parallel private(index, seed)
{
  #ifdef _OPENMP
    seed = omp_get_thread_num();
  #else
    seed = rand();
  #endif
  
  #pragma omp for
  for (unsigned long iter = 0; iter < num_iterations * num_randomloc; ++iter) {
    index = rand_r(&seed) % num_randomloc;
    randomloc[index] = index;  // 随机写入
  }
}
```

**GUPS (Giga Updates Per Second)**：
```
总操作数 = num_iterations × num_randomloc
         = 1000 × 2,097,152
         = 2,097,152,000 (~2.1 billion)

GUPS = 总操作数 / 执行时间(秒)

典型值:
- L1缓存: 10+ GUPS
- L2缓存: 5-8 GUPS
- L3缓存: 2-4 GUPS
- 主内存: 0.1-0.5 GUPS
```

**随机数生成**：
```c
rand_r(&seed):
- 线程安全的伪随机数生成器
- 每线程独立seed，避免锁竞争
- 质量: 线性同余生成器 (LCG)
```

**内存访问模式**：
```
完全随机: randomloc[rand() % num_randomloc]

数组大小: 2^21 × 4 bytes = 8 MB (默认)
如果 > L3缓存 → 大部分访问到主内存

TLB条目: 8 MB / 4 KB = 2048页
如果 TLB < 2048 → 频繁TLB miss
```

**性能分析**：
```
随机访问延迟:
- L1命中: ~4 cycles
- L2命中: ~12 cycles
- L3命中: ~40 cycles
- 内存访问: ~200 cycles

假设8MB数组 > L3:
平均延迟 ≈ 200 cycles
GUPS = CPU频率 / 200
     = 3 GHz / 200 = 0.015 GUPS (单线程)

多线程: 受限于内存带宽，扩展性差
```

**与HPC Challenge GUPS对比**：
| 特性     | HPC GUPS              | Meabo Phase 10      |
| -------- | --------------------- | ------------------- |
| 操作     | `T[rand()] ^= rand()` | `T[rand()] = index` |
| 数组大小 | ≥ 4×内存              | 可配置 (默认8MB)    |
| 随机性   | 更强                  | 简单LCG             |
| 目的     | 极限测试              | 微基准              |

**优化方向**：
- ✅ **大页支持**：减少TLB miss
- ⚠️ **预取**：随机访问预取效果差
- ⚠️ **并行扩展**：内存带宽瓶颈

---

## CPU亲和性绑定

### 绑定模式

**两种绑定策略**：

1. **绑定到CPU集合** (`bind_to_cpu_set=1`)
   ```c
   bind_to_cpu_w_reset(phase_cpu_id, num_cpus, 0);
   // 所有线程绑定到phase_cpu_id指定的CPU集合
   ```

2. **绑定到CPU集合内的特定核心** (`bind_to_cpu_set=0`)
   ```c
   bind_to_available_cpu_w_reset(phase_cpu_id, num_cpus, 0, thread_num);
   // 每个线程绑定到CPU集合内的不同核心
   ```

### CPU ID编码

**位掩码格式**：
```
phase_cpu_id = 二进制表示的十进制值

示例:
- CPU 0,1:      phase_cpu_id = 3    (0b0011)
- CPU 0,2,4:    phase_cpu_id = 21   (0b10101)
- CPU 0-7:      phase_cpu_id = 255  (0b11111111)
- CPU 0,1,4,5:  phase_cpu_id = 51   (0b110011)
```

### 在cpu_affinity_test.sh中的应用

**示例函数**：
```bash
run_3meabo() {
    local cpu_list=$1
    local process_count=$2
    
    # 将CPU列表转换为位掩码
    local cpu_mask=$(cpu_list_to_mask "$cpu_list")
    
    echo "[$(date)] 测试 3.Meabo (CPU: $cpu_list, Threads: $process_count)"
    
    cd /path/to/3.Meabo
    
    # 运行所有Phase，使用相同的CPU绑定
    ./meabo \
        -C $process_count \      # CPU数量
        -T $process_count \      # 线程数
        -i 100 \                 # 迭代次数
        -s $((1<<20)) \          # 数组大小
        -1 $cpu_mask \           # Phase 1 CPU mask
        -2 $cpu_mask \           # Phase 2 CPU mask
        -3 $cpu_mask \           # Phase 3 CPU mask
        -4 $cpu_mask \           # Phase 4 CPU mask
        -5 $cpu_mask \           # Phase 5 CPU mask
        -6 $cpu_mask \           # Phase 6 CPU mask
        -7 $cpu_mask \           # Phase 7 CPU mask
        -8 $cpu_mask \           # Phase 8 CPU mask
        -9 $cpu_mask \           # Phase 9 CPU mask
        -0 $cpu_mask             # Phase 10 CPU mask
}
```

### 不同核数配置测试策略

**1核配置** (`cpu_list=0`):
```bash
./meabo -C 1 -T 1 -1 1 -2 1 -3 1 -4 1 -5 1 -6 1 -7 1 -8 1 -9 1 -0 1
```
- 测试单核性能
- 无并行开销
- 反映CPU单核能力

**4核配置** (`cpu_list=0-3`, mask=15):
```bash
./meabo -C 4 -T 4 -1 15 -2 15 -3 15 -4 15 -5 15 -6 15 -7 15 -8 15 -9 15 -0 15
```
- 测试小规模并行
- 通常在同一CCX内
- 良好的缓存共享

**12核配置** (`cpu_list=0-11`, mask=4095):
```bash
./meabo -C 12 -T 12 -1 4095 -2 4095 ... -0 4095
```
- 测试大规模并行
- 可能跨CCX/CCD
- NUMA效应显著

## 性能监控

### PAPI硬件计数器

**启用方式** (`ENABLE_PAPI=1`):
```c
#if ENABLE_PAPI
  PAPI_info *papi_info = init_PAPI(num_hwcntrs);
  
  // 每个Phase开始
  PAPI_start_counters(papi_info->event_code, papi_info->num_hwcntrs);
  
  // Phase计算...
  
  // Phase结束
  PAPI_stop_counters(event_values, num_hwcntrs);
  
  // 输出结果
  for (i = 0; i < num_hwcntrs; ++i) {
    printf("%s value = %lld\n", papi_info->event_code_str[i], event_values[i]);
  }
#endif
```

**常见计数器** (ARM Cortex-A57/A53):
```
PAPI_TOT_CYC:  总周期数
PAPI_TOT_INS:  总指令数
PAPI_L1_DCM:   L1数据缓存miss
PAPI_L2_DCM:   L2缓存miss
PAPI_L3_TCM:   L3缓存miss
PAPI_TLB_DM:   数据TLB miss
PAPI_FP_OPS:   浮点操作数
```

**性能分析**：
```
IPC = PAPI_TOT_INS / PAPI_TOT_CYC
L1 Miss Rate = PAPI_L1_DCM / (PAPI_TOT_INS × load_ratio)
FLOPS = PAPI_FP_OPS / 执行时间(秒)
```

### 能耗监控

**启用方式** (`ENABLE_ENERGY=1`):
```c
#if ENABLE_ENERGY
  init_read_energy();
  
  read_energy("#PHASE1_START");
  // Phase 1计算...
  read_energy("#PHASE1_STOP");
  
  exit_read_energy();
#endif
```

**能耗计算**：
```
Phase能耗 = STOP能量 - START能量
平均功耗 = Phase能耗 / Phase执行时间
能效 = 计算量 / Phase能耗
```

## 验证机制

### 两种验证模式

**1. 精简验证** (`RED_VALIDATION=1`):
```c
#if RED_VALIDATION
  // 在计算时同步保存结果
  valid_red_vals[i] = computed_vals[i];
  
  // 测试后比较
  validation_array("Reduced", array_size, vals, valid_red_vals, phase_id, time);
#endif
```

**特点**：
- ✅ 快速：无需重新计算
- ✅ 检测内存冲突：验证线程不写到其他线程的位置
- ❌ 不检测数值正确性：只验证多线程一致性

**2. 完整验证** (`FULL_VALIDATION=1`):
```c
#if FULL_VALIDATION
  // 复制输入数据
  memcpy(valid_vals, vals, array_size * sizeof(double));
  
  // 串行重新计算
  phase1_compute(..., valid_vals, ..., validation_phase=1, num_threads=1);
  
  // 逐元素比较
  for (i = 0; i < array_size; ++i) {
    if (fabs(vals[i] - valid_vals[i]) > epsilon) {
      printf("Mismatch at index %d\n", i);
    }
  }
#endif
```

**特点**：
- ✅ 完全验证：检查数值正确性
- ✅ 串行一致性：与串行结果对比
- ❌ 慢：需要完整串行重算
- ❌ 内存开销大：双倍数据副本

### 验证精度

```c
if (sizeof(STREAM_TYPE) == 4) {        // float
    epsilon = 1.e-6;
}
else if (sizeof(STREAM_TYPE) == 8) {   // double
    epsilon = 1.e-13;
}
```

## 命令行参数

### 基本参数

```bash
-s <array_size>       # 数组大小 (默认 2^20)
-r <nrow>             # 稀疏矩阵行数 (默认 2^14)
-c <ncol>             # 稀疏矩阵列数 (默认 2^14)
-i <num_iterations>   # 迭代次数 (默认 1000)
-b <block_size>       # Phase 1块大小 (默认 8)
-l <llist_size>       # 链表大小 (默认 2^24)
-p <num_particles>    # 粒子数 (默认 2^20)
-x <num_palindromes>  # 回文数数量 (默认 2^10)
-R <num_randomloc>    # 随机位置数 (默认 2^21)
```

### 线程和CPU配置

```bash
-C <num_cpus>         # CPU数量 (默认 6)
-T <num_threads>      # 线程数 (0=自动，默认0)
-B <bind_to_cpu_set>  # 绑定模式 (1=集合, 0=独立核心)
```

### Phase CPU绑定

```bash
-1 <phase1_cpu_id>    # Phase 1 CPU位掩码
-2 <phase2_cpu_id>    # Phase 2 CPU位掩码
-3 <phase3_cpu_id>    # Phase 3 CPU位掩码
-4 <phase4_cpu_id>    # Phase 4 CPU位掩码
-5 <phase5_cpu_id>    # Phase 5 CPU位掩码
-6 <phase6_cpu_id>    # Phase 6 CPU位掩码
-7 <phase7_cpu_id>    # Phase 7 CPU位掩码
-8 <phase8_cpu_id>    # Phase 8 CPU位掩码
-9 <phase9_cpu_id>    # Phase 9 CPU位掩码
-0 <phase10_cpu_id>   # Phase 10 CPU位掩码
```

### Phase选择

```bash
-P <run_phases>       # 运行哪些Phase的位掩码
                      # 例如: -P 1023 运行所有Phase (0b1111111111)
                      #      -P 1 只运行Phase 1
                      #      -P 3 运行Phase 1和2 (0b11)
```

### PAPI配置

```bash
-H <num_hwcntrs>      # 硬件计数器数量 (默认 7)
```

## 测试示例

### 示例 1: 全面测试（所有Phase）

```bash
./meabo \
    -C 8 \                # 8个CPU
    -T 8 \                # 8个线程
    -i 1000 \             # 1000次迭代
    -s $((1<<20)) \       # 1M数组
    -1 255 \              # Phase 1: CPU 0-7
    -2 255 \              # Phase 2: CPU 0-7
    -3 255 \              # Phase 3: CPU 0-7
    -4 255 \              # Phase 4: CPU 0-7
    -5 255 \              # Phase 5: CPU 0-7
    -6 255 \              # Phase 6: CPU 0-7
    -7 255 \              # Phase 7: CPU 0-7
    -8 255 \              # Phase 8: CPU 0-7
    -9 255 \              # Phase 9: CPU 0-7
    -0 255                # Phase 10: CPU 0-7
```

### 示例 2: 单Phase测试（Phase 4向量加法）

```bash
./meabo \
    -C 4 \
    -T 4 \
    -P 8 \                # 0b1000 = 只运行Phase 4
    -i 10000 \
    -s $((1<<22)) \       # 4M数组
    -4 15                 # CPU 0-3
```

### 示例 3: 不同Phase使用不同CPU

```bash
# Phase 1-5在大核 (CPU 4-7)
# Phase 6-10在小核 (CPU 0-3)
./meabo \
    -C 8 \
    -1 240 \              # 0b11110000 = CPU 4-7
    -2 240 \
    -3 240 \
    -4 240 \
    -5 240 \
    -6 15 \               # 0b00001111 = CPU 0-3
    -7 15 \
    -8 15 \
    -9 15 \
    -0 15
```

### 示例 4: NUMA系统测试

```bash
# Socket 0: CPU 0-11
# Socket 1: CPU 12-23

# 测试Socket 0
./meabo -C 12 -T 12 -1 4095 -2 4095 ... -0 4095

# 测试Socket 1  
./meabo -C 12 -T 12 -1 16773120 -2 16773120 ... -0 16773120
# 16773120 = 0xFFF000 (CPU 12-23)
```

## 性能分析指南

### 不同Phase的性能特征

| Phase | 主要瓶颈       | 扩展性    | SIMD效果    | 缓存敏感度 |
| ----- | -------------- | --------- | ----------- | ---------- |
| 1     | 计算           | 高        | 高 (3-4×)   | 低         |
| 2     | 内存随机访问   | 中        | 中 (Gather) | 高         |
| 3     | 归约同步       | 中-高     | 高          | 低         |
| 4     | 内存带宽       | 高        | 高 (4-8×)   | 中         |
| 5     | 内存随机访问   | 低-中     | 低          | 极高       |
| 6     | 内存不规则访问 | 低        | 极低        | 极高       |
| 7     | 内存延迟       | 高 (独立) | 无          | 极高       |
| 8     | 计算+内存平衡  | 高        | 高          | 中         |
| 9     | 分支+整数      | 低-中     | 极低        | 低         |
| 10    | 内存随机延迟   | 低        | 无          | 极高       |

### 典型性能指标

**单核性能** (假设3GHz CPU):
```
Phase 1:  ~15-20 GFLOPS
Phase 2:  ~8-12 GB/s
Phase 3:  ~10-15 GFLOPS + 归约开销
Phase 4:  ~40-45 GB/s
Phase 5:  ~5-8 GB/s
Phase 6:  ~2-5 GFLOPS
Phase 7:  ~50-100 ns/access
Phase 8:  ~10-15 GFLOPS
Phase 9:  高度可变 (取决于实现)
Phase 10: ~0.01-0.05 GUPS
```

**多核扩展比** (理想=核数):
```
Phase 1:  0.9-0.95 (优秀)
Phase 2:  0.7-0.8  (良好)
Phase 3:  0.6-0.75 (中等，归约开销)
Phase 4:  0.5-0.7  (受内存带宽限制)
Phase 5:  0.4-0.6  (随机访问瓶颈)
Phase 6:  0.3-0.5  (不规则访问)
Phase 7:  0.95-1.0 (完美，独立链表)
Phase 8:  0.8-0.9  (良好)
Phase 9:  0.3-0.5  (分支+同步)
Phase 10: 0.2-0.4  (内存带宽饱和)
```

## 总结

### Meabo的核心价值

✅ **综合性**：10个Phase覆盖多种计算模式  
✅ **灵活性**：可选择性运行Phase，独立CPU配置  
✅ **并行性**：基于OpenMP，易于扩展到多核  
✅ **可监控性**：支持PAPI、能耗监控  
✅ **ARM优化**：专为ARM架构设计和验证  

### 适用场景

1. **处理器性能评估**：新CPU采购决策
2. **编译器优化验证**：对比不同编译选项
3. **NUMA配置调优**：多插槽系统优化
4. **负载特征分析**：识别应用瓶颈
5. **CPU亲和性研究**：评估核心绑定策略

### 互补工具

- **STREAM**：纯内存带宽测试
- **SPEC CPU**：真实应用负载
- **LMBench**：延迟测试
- **HPL/HPCG**：大规模线性代数

Meabo提供了比STREAM更全面、比SPEC CPU更轻量的微基准测试套件，特别适合快速诊断多核ARM系统的性能特征。