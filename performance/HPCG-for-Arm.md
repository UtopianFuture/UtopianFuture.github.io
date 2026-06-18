# HPCG for Arm 测试逻辑分析

## 项目概述

HPCG (High Performance Conjugate Gradient) 是一个用于评估 HPC 系统性能的基准测试工具，实现了预条件共轭梯度求解器。该基准测试生成一个类似于三维热扩散方程有限差分离散化的规则稀疏线性系统，并使用区域分解方法求解。

**HPCG for Arm** 是针对 ARM 架构优化的版本，利用了 ARM 性能库、NEON 和 SVE 指令集等优化技术。

## 核心测试目标

HPCG 基准测试的核心是求解线性方程组：**Ax = b**

其中：
- **A** 是稀疏矩阵（27 点三维模板）
- **x** 是未知解向量
- **b** 是已知右端向量

## 主程序执行流程

### 1. 初始化阶段

```
main()
├── MPI_Init() (如果启用 MPI)
├── HPCG_Init()
│   ├── 读取 hpcg.dat 配置文件或命令行参数
│   │   ├── nx, ny, nz: 本地网格维度
│   │   └── runningTime: 基准测试运行时长（秒）
│   └── 设置 QuickPath（当 runningTime=0 时，最小化测试路径）
└── 获取 MPI 进程信息（size, rank）
```

**配置文件 hpcg.dat 示例**：
```
HPCG benchmark input file
Sandia National Laboratories; University of Tennessee, Knoxville
104 104 104      # 本地网格维度（必须 ≥32 且为偶数）
60               # 测试运行时长（秒，有效运行需≥1800秒）
```

### 2. 问题设置阶段

```
问题设置
├── GenerateGeometry()
│   ├── 计算全局网格拓扑（npx × npy × npz 个进程）
│   ├── 确定本地网格在全局坐标系中的位置
│   └── 设置 MPI 通信拓扑（如果启用）
│
├── GenerateProblem()
│   ├── 初始化稀疏矩阵 A
│   │   ├── 27 点三维模板（最多每行 27 个非零元素）
│   │   ├── 分配 matrixValues[][]（矩阵值）
│   │   ├── 分配 mtxIndL[][]（本地列索引）
│   │   ├── 分配 mtxIndG[][]（全局列索引）
│   │   └── 创建 globalToLocalMap 映射
│   │
│   ├── 生成向量 b（右端项）
│   ├── 初始化向量 x（解向量，初值为 0）
│   └── 生成 xexact（精确解，用于验证）
│
├── SetupHalo()
│   └── 设置 MPI 进程间边界交换所需的数据结构
│
└── GenerateCoarseProblem()
    └── 生成 4 层多重网格层次（包括最细层）
        ├── Level 0: 最细网格（用户指定的 nx×ny×nz）
        ├── Level 1: 中等网格（每维减半）
        ├── Level 2: 粗网格（再次减半）
        └── Level 3: 最粗网格（再次减半）
```

**稀疏矩阵结构**：
- 总行数 = nx × ny × nz（本地）
- 总非零元素数 ≈ 27 × localNumberOfRows
- 存储格式：CSR（压缩行存储）的变体
- 每行最多 27 个非零元素（3×3×3 模板）

### 3. 参考内核计时阶段

```
参考 SpMV+MG 计时
├── 初始化临时向量
│   ├── x_overlap (ncol 长度，包含重叠区域)
│   └── b_computed (nrow 长度)
│
└── 执行 10 次（QuickPath 时为 1 次）
    ├── ComputeSPMV_ref(A, x_overlap, b_computed)
    │   └── 稀疏矩阵-向量乘法：b = A * x
    │
    └── ComputeMG_ref(A, b_computed, x_overlap)
        └── 多重网格预条件器：x = M^{-1} * b
```

**计时目的**：
- 测量未优化参考实现的性能基线
- 用于计算优化版本的加速比

### 4. 参考 CG 求解器阶段

```
参考 CG 迭代
├── 设置参数
│   ├── maxIters = 50
│   ├── tolerance = 0.0（强制执行所有迭代）
│   └── numberOfCalls = 1
│
└── CG_ref(A, data, b, x, maxIters, tolerance, ...)
    └── 返回 refTolerance = normr / normr0
```

**参考 CG 目的**：
- 确定收敛所需的迭代次数
- 计算残差降低率作为后续测试的基准

### 5. 优化设置阶段

```
OptimizeProblem(A, data, b, x, xexact)
├── 数据重排序
│   ├── 创建 whichNewRowIsOldRow 映射
│   ├── 创建 whichOldRowIsNewRow 映射
│   └── 重排矩阵行以提高数据局部性
│
├── 选择并行化策略
│   ├── 最细层 (Level 0): 任务依赖图 (TDG)
│   │   └── A.TDG = true
│   └── 粗糙层 (Level 1-3): 多块着色 (Block Coloring)
│       ├── 计算块大小 (blockSize)
│       └── 创建着色方案
│
└── 向量化准备
    ├── 启用 NEON 优化 (HPCG_USE_NEON)
    └── 启用 SVE 优化 (HPCG_USE_SVE)
```

### 6. 验证测试阶段

#### 6.1 TestCG - CG 求解器正确性测试

```
TestCG(A, data, b, x, testcg_data)
├── 修改矩阵对角线（夸大对角线值）
│   ├── globalRowID < 9: scale = (globalRowID+2) * 1.0e6
│   └── globalRowID ≥ 9: scale = 1.0e6
│
├── 测试 1: 无预条件 CG
│   ├── 执行 2 次 CG(doPreconditioning=false)
│   ├── 期望迭代次数 ≤ 12
│   └── tolerance = 1.0e-12
│
└── 测试 2: 预条件 CG
    ├── 执行 2 次 CG(doPreconditioning=true)
    ├── 期望迭代次数 ≤ 2
    └── tolerance = 1.0e-12
```

**测试目的**：
- 验证 CG 实现的正确性
- 确认预条件器的有效性（大幅减少迭代次数）

#### 6.2 TestSymmetry - 矩阵对称性测试

```
TestSymmetry(A, b, xexact, testsymmetry_data)
├── 计算 y = A * x
├── 计算 z = A^T * x（通过分析矩阵结构）
└── 验证 ||y - z|| < ε（矩阵是否对称）
```

### 7. 优化 CG 设置阶段

```
优化 CG 准备
├── optMaxIters = 10 * refMaxIters (500)
├── optNiters = refMaxIters (50)
└── 执行预热迭代
    ├── ZeroVector(x)
    └── CG(A, data, b, x, optMaxIters, refTolerance, ...)
        └── 记录最大迭代次数和最差时间
```

### 8. 正式基准测试阶段（计时运行）

```
基准测试执行
├── 计算 CG 集合数
│   └── numberOfCgSets = ⌈total_runtime / opt_worst_time⌉ + 1
│
├── 对于每个 CG 集合 (i = 0 to numberOfCgSets-1):
│   ├── ZeroVector(x)  # 重置初值
│   ├── CG(A, data, b, x, optMaxIters, 0.0, ...)
│   │   └── 强制执行所有迭代（tolerance=0.0）
│   └── 记录残差 testnorms_data.values[i] = normr/normr0
│
└── TestNorms(testnorms_data)
    └── 验证所有运行的残差一致性
```

**关键点**：
- 运行足够多的 CG 集合以达到指定的运行时间
- 每次 CG 从零初值开始（公平性）
- 记录所有运行的性能指标

## 共轭梯度 (CG) 算法详解

### CG 算法流程

```
CG(A, data, b, x, max_iter, tolerance, doPreconditioning)

初始化:
├── p = x (方向向量)
├── Ap = A * p
├── r = b - Ap (残差向量)
└── normr0 = ||r||₂ (初始残差范数)

for k = 1 to max_iter:
    ├── if doPreconditioning:
    │   └── z = M^{-1} * r  (应用多重网格预条件器)
    │   else:
    │   └── z = r
    │
    ├── if k == 1:
    │   ├── p = z
    │   └── rtz = r^T * z
    │   else:
    │   ├── oldrtz = rtz
    │   ├── rtz = r^T * z
    │   ├── beta = rtz / oldrtz
    │   └── p = z + beta * p
    │
    ├── Ap = A * p (SpMV)
    ├── pAp = p^T * Ap
    ├── alpha = rtz / pAp
    ├── x = x + alpha * p
    ├── r = r - alpha * Ap
    ├── normr = ||r||₂
    │
    └── if normr/normr0 < tolerance: break

返回: niters, normr, normr0
```

### CG 时间统计

| 索引     | 操作              | 说明                      |
| -------- | ----------------- | ------------------------- |
| times[0] | 总时间            | CG 迭代总耗时             |
| times[1] | DotProduct        | 点积操作累计时间          |
| times[2] | WAXPBY            | 向量线性组合累计时间      |
| times[3] | SpMV              | 稀疏矩阵-向量乘法累计时间 |
| times[4] | AllReduce         | MPI 全局归约累计时间      |
| times[5] | MG Preconditioner | 多重网格预条件器累计时间  |

## 核心计算内核详解

### 1. ComputeSPMV - 稀疏矩阵-向量乘法

**功能**: 计算 y = A * x

**优化版本**：

#### (1) NEON 实现
```cpp
// 使用 ARM NEON 指令集进行 SIMD 优化
for (i = 0; i < nrow-1; i += 2) {
    float64x2_t sum0 = vdupq_n_f64(0.0);
    float64x2_t sum1 = vdupq_n_f64(0.0);
    
    for (j = 0; j < nonzerosInRow[i]-1; j += 2) {
        // 加载矩阵值
        float64x2_t values0 = vld1q_f64(&matrixValues[i][j]);
        float64x2_t values1 = vld1q_f64(&matrixValues[i+1][j]);
        
        // 加载向量值（gather 操作）
        float64x2_t xvValues0 = {xv[mtxIndL[i][j]], xv[mtxIndL[i][j+1]]};
        float64x2_t xvValues1 = {xv[mtxIndL[i+1][j]], xv[mtxIndL[i+1][j+1]]};
        
        // FMA: sum = sum + values * xvValues
        sum0 = vfmaq_f64(sum0, values0, xvValues0);
        sum1 = vfmaq_f64(sum1, values1, xvValues1);
    }
    
    yv[i] = vgetq_lane_f64(sum0, 0) + vgetq_lane_f64(sum0, 1);
    yv[i+1] = vgetq_lane_f64(sum1, 0) + vgetq_lane_f64(sum1, 1);
}
```

#### (2) SVE 实现
```cpp
// 使用 ARM SVE 可伸缩向量扩展
for (i = 0; i < nrow; i++) {
    svfloat64_t sum = svdup_f64(0.0);
    
    for (j = 0; j < nonzerosInRow[i]; j += svcntd()) {
        // 创建谓词（处理尾部元素）
        svbool_t pg = svwhilelt_b64(j, nonzerosInRow[i]);
        
        // 加载矩阵值
        svfloat64_t values = svld1_f64(pg, &matrixValues[i][j]);
        
        // Gather 加载向量值
        svuint64_t indices = svld1sw_u64(pg, &mtxIndL[i][j]);
        svfloat64_t xvValues = svld1_gather_u64index_f64(pg, xv, indices);
        
        // FMA: sum = sum + values * xvValues
        sum = svmla_f64_m(pg, sum, values, xvValues);
    }
    
    yv[i] = svaddv_f64(svptrue_b64(), sum);
}
```

#### (3) ArmPL 实现
```cpp
#ifdef HPCG_USE_ARMPL_SPMV
// 使用 ARM Performance Libraries 的优化稀疏矩阵例程
armpl_sparse_spmv(...);
#endif
```

### 2. ComputeSYMGS - 对称 Gauss-Seidel 平滑器

**功能**: 对称 Gauss-Seidel 迭代，用作预条件器

**算法**：
- **前向扫描**: 从第一行到最后一行
- **后向扫描**: 从最后一行到第一行

#### 任务依赖图 (TDG) 版本

```cpp
ComputeSYMGS_TDG_SVE(A, r, x)

前向扫描:
for each level l in tdg:
    #pragma omp parallel for
    for each row in tdg[l]:
        sum = rv[row]
        for each nonzero in row:
            sum -= matrixValues[row][j] * xv[colIndex[j]]
        
        sum += xv[row] * diagonal[row]
        xv[row] = sum / diagonal[row]

后向扫描:
for each level l in tdg (逆序):
    #pragma omp parallel for (逆序)
    for each row in tdg[l] (逆序):
        (同前向扫描)
```

**TDG 原理**：
- 将网格节点组织成依赖层次
- 同一层次的节点没有依赖关系，可并行处理
- 适合最细层网格（更多并行度）

#### 多块着色 (Block Coloring) 版本

```cpp
ComputeSYMGS_BLOCK(A, r, x)

前向扫描:
for each color c:
    #pragma omp parallel for
    for each block in color c:
        for each row in block:
            (Gauss-Seidel 更新)

后向扫描:
for each color c (逆序):
    #pragma omp parallel for
    for each block in color c:
        (Gauss-Seidel 更新)
```

**着色原理**：
- 将连续节点分组成块
- 给块分配颜色，使同色块之间没有依赖
- 同色块可并行处理
- 适合粗网格（减少同步开销）

### 3. ComputeWAXPBY - 向量线性组合

**功能**: 计算 w = α*x + β*y

**SVE 优化版本**：

```cpp
if (alpha == 1.0 && beta == 1.0):
    // w[i] = x[i] + y[i]
    for (i = 0; i < n; i += svcntd()):
        svbool_t pg = svwhilelt_b64(i, n);
        svfloat64_t svx = svld1(pg, &xv[i]);
        svfloat64_t svy = svld1(pg, &yv[i]);
        svfloat64_t svw = svadd_f64_z(pg, svx, svy);
        svst1_f64(pg, &wv[i], svw);

else if (alpha == 1.0):
    // w[i] = x[i] + beta*y[i]
    svfloat64_t svb = svdup_f64(beta);
    for (i = 0; i < n; i += svcntd()):
        ...
        svw = svmla_f64_z(pg, svx, svb, svy);  // svx + svb*svy

else if (beta == 1.0):
    // w[i] = alpha*x[i] + y[i]
    svfloat64_t sva = svdup_f64(alpha);
    ...
    svw = svmad_f64_z(pg, svx, sva, svy);  // svx*sva + svy

else:
    // w[i] = alpha*x[i] + beta*y[i]
    svfloat64_t sva = svdup_f64(alpha);
    svfloat64_t svb = svdup_f64(beta);
    ...
    svax = svmul_f64_z(pg, svx, sva);
    svw = svmad_f64_z(pg, svb, svy, svax);
```

**优化策略**：
- 根据 α 和 β 的值选择最优指令序列
- 使用 FMA（融合乘加）指令
- 利用 SVE 的谓词处理任意长度向量

### 4. ComputeDotProduct - 点积计算

**功能**: 计算 result = x^T * y

**实现**（含 MPI 归约）：
```cpp
local_result = 0.0
for (i = 0; i < n; i++):
    local_result += xv[i] * yv[i]

#ifndef HPCG_NO_MPI
MPI_Allreduce(&local_result, &result, 1, MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD)
#else
result = local_result
#endif
```

### 5. ComputeMG - 多重网格预条件器

**功能**: 实现 V-cycle 多重网格迭代

```
ComputeMG(A, r, x)  # x = M^{-1} * r

if A 有粗网格:
    # V-cycle
    ├── 前平滑（Pre-smoothing）
    │   └── for i = 1 to numberOfPresmootherSteps:
    │       └── ComputeSYMGS(A, r, x)
    │
    ├── 计算残差
    │   └── Axf = A * x
    │
    ├── 限制（Restriction）
    │   └── rc = R * (r - Axf)  # 将残差投影到粗网格
    │
    ├── 粗网格求解（递归）
    │   └── ComputeMG(Ac, rc, xc)
    │
    ├── 延拓（Prolongation）
    │   └── x = x + P * xc  # 粗网格修正插值回细网格
    │
    └── 后平滑（Post-smoothing）
        └── for i = 1 to numberOfPostsmootherSteps:
            └── ComputeSYMGS(A, r, x)
else:
    # 最粗层：直接平滑
    └── ComputeSYMGS(A, r, x)
```

**多重网格层次**：
```
Level 0 (最细):  nx   × ny   × nz
Level 1:         nx/2 × ny/2 × nz/2
Level 2:         nx/4 × ny/4 × nz/4
Level 3 (最粗):  nx/8 × ny/8 × nz/8
```

## 编译优化选项

### 主要编译宏定义

| 宏定义                      | 功能                         |
| --------------------------- | ---------------------------- |
| `HPCG_NO_MPI`               | 禁用 MPI（单进程运行）       |
| `HPCG_NO_OPENMP`            | 禁用 OpenMP（单线程运行）    |
| `HPCG_USE_NEON`             | 启用 NEON 指令集优化         |
| `HPCG_USE_SVE`              | 启用 SVE 指令集优化          |
| `HPCG_USE_ARMPL_SPMV`       | 使用 ArmPL 库优化 SpMV       |
| `HPCG_USE_DDOT_ARMPL`       | 使用 ArmPL 库优化点积        |
| `HPCG_USE_WAXPBY_ARMPL`     | 使用 ArmPL 库优化 WAXPBY     |
| `HPCG_USE_FUSED_SYMGS_SPMV` | 融合 SYMGS 和 SpMV（非官方） |
| `HPCG_CONTIGUOUS_ARRAYS`    | 使用连续内存数组             |

### 配置文件示例

**setup/Make.ARM_SVE_INTRINSICS**:
```makefile
CXX = aarch64-linux-gnu-g++
CXXFLAGS = -O3 -march=armv8-a+sve -fopenmp

HPCG_OPTS = -DHPCG_USE_SVE -DHPCG_CONTIGUOUS_ARRAYS
```

## 性能指标和输出

### 主要性能指标

1. **GFLOPS** (每秒十亿次浮点运算)
   ```
   GFLOPS = 总浮点操作数 / 总时间(秒) / 1e9
   ```

2. **Memory Bandwidth** (内存带宽)
   ```
   带宽(GB/s) = 数据传输量(字节) / 总时间(秒) / 1e9
   ```

3. **残差降低率**
   ```
   Scaled Residual = ||r|| / ||r₀||
   ```

### 输出文件

#### 1. HPCG-Benchmark_<version>_<date>_<time>.txt
实时输出文件，包含：
- 每次 CG 迭代的残差
- 测试进度信息
- 错误和警告信息

#### 2. HPCG-Benchmark_<version>_<date>_<time>.yaml
最终结果文件，包含：
```yaml
HPCG-Benchmark version: 3.1
Problem dimensions:
  Global nx: 416
  Global ny: 416
  Global nz: 416
Processor topology:
  npx: 2
  npy: 2
  npz: 2
  nthreads: 8
Performance:
  GFLOPS: 123.45
  Total CG iterations: 50
  Final residual: 1.234e-10
Kernel timings:
  Total CG time: 12.34 s
  SpMV time: 5.67 s
  MG time: 4.56 s
  DotProduct time: 0.89 s
  WAXPBY time: 0.78 s
```

## 有效运行标准

### 官方提交要求

1. **运行时长**: ≥ 1800 秒（30 分钟）
2. **问题规模**: 数据数组占用 ≥ 1/4 主内存
   - 确保数据不全在缓存中
   - 测试真实内存带宽
3. **网格维度约束**:
   - nx, ny, nz 必须是偶数（所有层次）
   - nx, ny, nz ≥ 32
4. **收敛性**: 所有运行的残差必须一致
5. **无优化限制**: 不能使用 `HPCG_USE_FUSED_SYMGS_SPMV`

### QuickPath 模式

当 `runningTime = 0` 时：
- 仅执行 1 次参考内核调用
- 仅执行 1 次 CG 迭代
- 用于快速验证和调试
- **不适用于性能提交**

## 并行优化策略总结

### 1. 共享内存并行（OpenMP）

**任务依赖图 (TDG)**:
- 节点按依赖关系分层
- 并行度随层次增加然后减少
- 适合大网格（Level 0）

**多块着色**:
- 连续节点分组成块
- 同色块无依赖，可并行
- 启用向量化优化
- 适合小网格（Level 1-3）

### 2. 向量化（SIMD）

**NEON (128-bit)**:
- 2 个 double 并行处理
- 硬件支持广泛
- 适合固定向量宽度优化

**SVE (可伸缩)**:
- 向量长度硬件决定（128-2048 bit）
- 谓词机制处理任意长度
- 更好的代码可移植性
- 支持 gather/scatter 操作

### 3. 分布式内存并行（MPI）

**区域分解**:
- 全局网格划分到多个进程
- 每个进程处理本地子域
- 边界通过 MPI 交换（Halo exchange）

**负载均衡**:
- 尽量均匀分配网格点
- 最小化通信量
- 优化进程拓扑（npx × npy × npz）

### 4. 数据局部性优化

**重排序**:
- 按计算顺序重排矩阵行
- 提高缓存命中率
- 减少 TLB miss

**内存对齐**:
- 使用 `posix_memalign` 对齐分配
- 优化向量加载/存储
- 减少缓存行冲突

## 典型性能分析

### 计算瓶颈识别

```
时间分布（典型值）:
├── SpMV:        40-50%  # 稀疏矩阵操作，内存带宽受限
├── SYMGS:       30-40%  # 预条件器，依赖关系多
├── DotProduct:  5-10%   # MPI 全局归约开销
├── WAXPBY:      5-10%   # 向量操作，内存带宽受限
└── 其他:        5-10%
```

### 优化效果

| 优化技术    | 加速比   | 适用场景         |
| ----------- | -------- | ---------------- |
| NEON 向量化 | 1.5-2.0× | 所有内核         |
| SVE 向量化  | 1.8-2.5× | 支持 SVE 的 CPU  |
| TDG 并行    | 1.2-1.5× | 最细层 SYMGS     |
| 多块着色    | 1.3-1.8× | 粗层 SYMGS       |
| ArmPL 库    | 1.1-1.3× | SpMV, DotProduct |
| 数据重排序  | 1.1-1.2× | 提高缓存效率     |

## 使用建议

### 1. 网格维度选择

```bash
# 计算合适的本地网格维度
total_memory_GB=256
processes=8
threads_per_process=16
memory_per_process=$(($total_memory_GB / $processes))

# 占用约 1/4 内存
target_memory=$(($memory_per_process / 4))

# 每个网格点约 8 * 27 * 8 = 1728 字节（矩阵 + 向量）
bytes_per_point=1728
target_points=$(($target_memory * 1024 * 1024 * 1024 / $bytes_per_point))

# 取立方根并向下取偶数
n=$(echo "scale=0; sqrt($target_points)/1" | bc)
n=$(($n - $n % 2))

echo "建议使用: nx=$n ny=$n nz=$n"
```

### 2. 运行命令示例

```bash
# 设置 OpenMP 线程数
export OMP_NUM_THREADS=16

# MPI 运行（4 进程 × 16 线程 = 64 核心）
mpirun -np 4 ./xhpcg --nx=208 --ny=208 --nz=208 --rt=1800

# 单进程运行
export OMP_NUM_THREADS=64
./xhpcg --nx=416 --ny=416 --nz=416 --rt=1800

# QuickPath 验证
./xhpcg --nx=104 --ny=104 --nz=104 --rt=0
```

### 3. 性能调优建议

1. **进程/线程配置**:
   - 优先使用 OpenMP 多线程（减少通信）
   - MPI 进程数选择 2^n（便于网格划分）
   - 避免超线程（HPC 性能通常更差）

2. **内存配置**:
   - 启用大页（Huge Pages）
   - 绑定进程到 NUMA 节点
   - 使用 `numactl` 优化内存分配

3. **编译优化**:
   - 使用 `-O3 -march=native`
   - 启用 LTO（链接时优化）
   - Profile-guided optimization (PGO)

## 总结

HPCG for Arm 是一个综合性的 HPC 基准测试，通过以下方式评估系统性能：

1. **计算密集型**: SpMV, SYMGS（浮点运算能力）
2. **内存密集型**: 向量操作（内存带宽）
3. **通信密集型**: MPI 归约（网络性能）
4. **并行可扩展性**: OpenMP + MPI（多核/多节点）

**核心优化技术**：
- ✅ ARM SVE/NEON 向量化指令
- ✅ 任务依赖图 + 多块着色并行策略
- ✅ 数据重排序提高局部性
- ✅ 多重网格预条件器加速收敛
- ✅ ARM Performance Libraries 集成

该基准测试更接近实际科学计算应用，相比 HPL（Linpack）更能反映真实 HPC 工作负载的性能。