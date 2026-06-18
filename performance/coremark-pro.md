# CoreMark-Pro 测试逻辑分析

## 项目概述

**CoreMark-Pro** 是 EEMBC（嵌入式微处理器基准测试联盟）开发的**综合性处理器基准测试套件**，是经典 CoreMark 的增强版本。CoreMark 主要测试 CPU 流水线，而 CoreMark-Pro 测试整个处理器系统，包括：

- **多核支持**：全面的多核技术测试；
- **混合负载**：整数和浮点混合工作负载；
- **大数据集**：利用更大的内存子系统；
- **真实应用**：模拟实际应用场景；

### 核心定位

```
低端微控制器 ←─── CoreMark ───→ 高性能计算处理器
                ↑                    ↑
            单核基准            CoreMark-Pro
            CPU流水线          完整处理器系统
```

## 测试架构

### 9个工作负载（Workloads）

CoreMark-Pro 包含 **5个整数负载** 和 **4个浮点负载**：

#### 整数工作负载

| 工作负载               | 全称            | 测试重点         | 应用场景   |
| ---------------------- | --------------- | ---------------- | ---------- |
| **cjpeg-rose7-preset** | JPEG 压缩       | 图像压缩算法     | 多媒体处理 |
| **zip-test**           | ZIP 压缩        | 数据压缩（ZLIB） | 文件压缩   |
| **parser-125k**        | XML 解析        | 文本解析         | 数据处理   |
| **sha-test**           | SHA-256 哈希    | 加密哈希算法     | 安全加密   |
| **core**               | CoreMark 增强版 | CPU 流水线       | 通用计算   |

#### 浮点工作负载

| 工作负载                      | 全称            | 测试重点         | 应用场景 |
| ----------------------------- | --------------- | ---------------- | -------- |
| **radix2-big-64k**            | Radix-2 FFT     | 快速傅里叶变换   | 信号处理 |
| **linear_alg-mid-100x100-sp** | LINPACK         | 高斯消元法       | 线性代数 |
| **nnet_test**                 | 神经网络        | 简单神经网络     | 机器学习 |
| **loops-all-mid-10k-sp**      | Livermore Loops | 24个科学计算核心 | HPC应用  |

### 测试层次结构

```
CoreMark-Pro 套件
├── Workloads (工作负载)
│   ├── cjpeg-rose7-preset.exe
│   ├── core.exe
│   ├── linear_alg-mid-100x100-sp.exe
│   ├── loops-all-mid-10k-sp.exe
│   ├── nnet_test.exe
│   ├── parser-125k.exe
│   ├── radix2-big-64k.exe
│   ├── sha-test.exe
│   └── zip-test.exe
│
├── Benchmarks (基准测试核心)
│   ├── core/           (CoreMark 核心算法)
│   ├── consumer_v2/    (JPEG 压缩)
│   ├── darkmark/       (SHA, ZIP, Parser)
│   └── fp/             (浮点核心)
│       ├── fft_radix2/ (FFT)
│       ├── linpack/    (线性代数)
│       ├── loops/      (Livermore Loops)
│       └── nnet/       (神经网络)
│
└── MITH (多实例测试框架)
    ├── th_lib.c        (测试框架库)
    ├── al/             (抽象层)
    │   ├── al_smp.c    (SMP 多线程)
    │   └── th_al.c     (硬件抽象 ⭐移植接口)
    └── mith_main.c     (主控制器)
```

## MITH 测试框架

### Multi-Instance Test Harness（多实例测试框架）

**设计理念**：
- 高层级测试启动和控制
- 低层级硬件/操作系统抽象
- 支持多种线程调度系统
- 允许裸机（baremetal）运行

### 框架组件

```c
// MITH 工作项结构
typedef struct ee_work_item_s {
    char shortname[MITH_MAX_NAME];     // 工作项名称
    void *params;                       // 参数
    void * (*init_func)(void *);       // 初始化函数
    void * (*bench_func)(TCDef *, void *); // 基准测试函数
    void * (*fini_func)(void *);       // 清理函数
    int (*veri_func)(void *);          // 验证函数
    int (*cleanup)(void *);            // 清理函数
    e_u32 num_contexts;                // 上下文数量
    e_u32 kernel_id;                   // 内核ID
    e_u32 instance_id;                 // 实例ID
    e_u32 repeats;                     // 重复次数
} ee_work_item_t;

// 工作负载结构
typedef struct ee_workload_s {
    char shortname[MITH_MAX_NAME];     // 负载名称
    e_u32 rev_M;                       // 主版本号
    e_u32 rev_m;                       // 次版本号
    e_u32 uid;                         // 唯一ID
    e_u32 iterations;                  // 迭代次数
    ee_work_item_t **items;            // 工作项数组
    e_u32 num_items;                   // 工作项数量
} ee_workload;
```

### 执行流程

```
main()
├── al_main()                  // 抽象层初始化
│   ├── 平台启动代码
│   ├── 时钟初始化
│   └── 线程系统初始化
│
├── mith_wl_init()             // 工作负载初始化
│   └── 创建 workload 结构
│
├── 解析命令行参数
│   ├── -i <iterations>        // 迭代次数
│   ├── -c <num_contexts>      // 上下文数量
│   ├── -w <num_workers>       // 工作线程数
│   ├── -b <bench_repeats>     // 基准测试重复次数
│   ├── -v <verify_output>     // 验证级别
│   └── -o <oversubscribe>     // 允许超订阅
│
├── helper_<workload>()        // 工作项初始化
│   ├── define_params()        // 定义参数
│   ├── mith_item_init()       // 创建工作项
│   └── mith_wl_add()          // 添加到负载
│
├── mith_main()                // MITH 主控制器
│   ├── mith_wl_spawn()        // 生成工作线程
│   │   ├── 创建上下文
│   │   ├── 分配线程
│   │   └── 启动执行
│   │
│   ├── bmark_init()           // 基准测试初始化
│   │   └── 初始化数据结构
│   │
│   ├── t_run_test()           // 执行测试 (计时)
│   │   ├── al_signal_start()  // 开始计时
│   │   ├── bench_func()       // 核心算法
│   │   └── al_signal_finished() // 结束计时
│   │
│   ├── bmark_verify()         // 结果验证
│   │   └── 比对参考数据
│   │
│   └── bmark_fini()           // 清理资源
│
└── mith_wl_report()           // 报告结果
    ├── 计算迭代/秒
    ├── 输出性能数据
    └── UU编码输出
```

## 工作负载详解

### 1. cjpeg-rose7-preset (JPEG 压缩)

**测试目标**：图像压缩性能、DCT变换、霍夫曼编码

**数据集**：Rose256_bmp (256x256 RGB 位图)

**核心算法**：
```c
// JPEG 压缩流程
1. RGB → YCbCr 颜色空间转换
2. 色度子采样（4:2:0）
3. 8×8 块分割
4. 离散余弦变换（DCT）
5. 量化
6. 之字形扫描
7. 霍夫曼编码
8. 输出压缩数据
```

**关键宏定义**：
```c
SELECT_PRESET_ID=1     // 选择预设配置
USE_PRESET             // 使用预设参数
```

**性能特征**：
- 计算密集型：DCT 变换
- 内存访问：块数据读写
- 分支预测：霍夫曼编码
- SIMD友好：向量化DCT

**典型性能**：
```
单核: 156.25 iter/s
4核:  555.56 iter/s (3.56倍加速)
```

---

### 2. core (CoreMark 增强版)

**测试目标**：CPU 流水线效率、指令级并行、分支预测

**核心算法**：
```c
// CoreMark 三大测试
1. 链表操作（指针追踪）
   - 链表查找
   - 链表排序
   - 链表反转

2. 矩阵运算
   - 矩阵乘法
   - 矩阵加法

3. 状态机
   - CRC 计算
   - 字符串处理
```

**数据结构**：
```c
typedef struct list_data_s {
    e_s16 data16;
    e_s16 idx;
} list_data;

typedef struct list_head_s {
    struct list_head_s *next;
    struct list_data_s *info;
} list_head;
```

**性能特征**：
- 内存密集：链表遍历
- 计算密集：矩阵运算
- 分支密集：状态机
- 缓存敏感：数据局部性

**扩展比**：
```
单核: 1.30 iter/s
4核:  4.87 iter/s (3.75倍)
```

**与原版 CoreMark 区别**：
- 更大数据集（内存密集）
- 支持多核并行
- 更长执行时间

---

### 3. linear_alg-mid-100x100-sp (LINPACK)

**测试目标**：浮点运算性能、矩阵运算、FMA 效率

**核心算法**：高斯消元法求解线性方程组
```c
// Ax = b 求解过程
for (k = 0; k < n-1; k++) {
    // 部分主元选取
    pivot = find_max_pivot(A, k, n);
    swap_rows(A, k, pivot);
    
    // 消元
    for (i = k+1; i < n; i++) {
        factor = A[i][k] / A[k][k];
        for (j = k+1; j < n; j++) {
            A[i][j] -= factor * A[k][j];  // FMA 操作
        }
        b[i] -= factor * b[k];
    }
}

// 回代
for (i = n-1; i >= 0; i--) {
    x[i] = b[i];
    for (j = i+1; j < n; j++) {
        x[i] -= A[i][j] * x[j];
    }
    x[i] /= A[i][i];
}
```

**矩阵规模**：
- 100×100 单精度浮点矩阵
- 约 40 KB 数据

**计算量**：
```
FLOP = (2/3) × n³ + O(n²)
     ≈ (2/3) × 100³
     ≈ 666,667 FLOP per solve
```

**宏定义**：
```c
USE_FP32=1             // 使用单精度浮点
```

**性能特征**：
- 计算密集：O(n³) 复杂度
- FMA友好：乘加融合
- 缓存友好：块矩阵优化
- 向量化：内循环可SIMD

**典型性能**：
```
单核: 409.84 iter/s
4核:  1428.57 iter/s (3.49倍)
```

---

### 4. loops-all-mid-10k-sp (Livermore Loops)

**测试目标**：科学计算常见模式、浮点性能、内存访问模式

**24个核心**（Kernels）：

| Kernel | 名称                  | 计算模式          | 典型应用     |
| ------ | --------------------- | ----------------- | ------------ |
| 1      | Hydro Fragment        | 三维流体力学      | CFD          |
| 2      | ICCG Excerpt          | 不完全共轭梯度    | 线性求解器   |
| 3      | Inner Product         | 向量内积          | 线性代数     |
| 4      | Banded Linear Eq      | 带状线性方程      | 稀疏矩阵     |
| 5      | Tri-diagonal Elim     | 三对角消元        | 微分方程     |
| 6      | Linear Recurrence     | 线性递归          | 动态规划     |
| 7      | Equation of State     | 状态方程          | 物理模拟     |
| 8      | ADI Integration       | 交替方向隐式      | PDE求解      |
| 9      | Integrate Predictors  | 预测积分          | 数值积分     |
| 10     | Difference Predictors | 差分预测          | 数值微分     |
| 11     | First Sum             | 累加求和          | 基础归约     |
| 12     | First Difference      | 一阶差分          | 数值导数     |
| 13     | 2-D PIC               | 二维粒子网格法    | 等离子体模拟 |
| 14     | 1-D PIC               | 一维粒子网格法    | 简化PIC      |
| 15     | Casual Fortran        | 随机 Fortran 代码 | 混合负载     |
| 16     | Monte Carlo           | 蒙特卡洛搜索      | 随机模拟     |
| 17     | Implicit Conditional  | 隐式条件计算      | 分支密集     |
| 18     | 2-D Explicit Hydro    | 二维显式流体      | 爆炸模拟     |
| 19     | Linear Recurrence     | 线性递归          | 动态系统     |
| 20     | Discrete Ordinates    | 离散纵标法        | 辐射传输     |
| 21     | Matrix × Matrix       | 矩阵乘法          | BLAS         |
| 22     | Planckian Dist        | 普朗克分布        | 量子统计     |
| 23     | 2-D Implicit Hydro    | 二维隐式流体      | 稳态模拟     |
| 24     | Find Min Location     | 查找最小值        | 搜索优化     |

**数据集**：10K (10,240 字节)

**宏定义**：
```c
USE_FP32=1             // 单精度浮点
```

**示例 Kernel (K3: Inner Product)**：
```c
// 向量内积
for (k = 0; k < n; k++) {
    q += z[k] * x[k];  // FMA 操作
}
```

**示例 Kernel (K21: Matrix × Matrix)**：
```c
// 矩阵乘法
for (i = 0; i < 25; i++) {
    for (j = 0; j < 25; j++) {
        for (k = 0; k < n; k++) {
            px[j][i] += vy[k][i] * cx[j][k];
        }
    }
}
```

**性能特征**：
- 混合负载：24种不同模式
- 浮点密集：大量FMA操作
- 内存模式多样：顺序、随机、跨步
- 分支变化：部分kernel有条件跳转

**典型性能**：
```
单核: 6.25 iter/s
4核:  22.56 iter/s (3.61倍)
```

---

### 5. nnet_test (神经网络)

**测试目标**：简单神经网络推理、激活函数、矩阵运算

**网络结构**：
```
输入层 → 隐藏层 → 输出层
```

**核心算法**：
```c
// 前向传播
void forward_pass(double *input, double *hidden, double *output,
                  double **w_ih, double **w_ho, int n_input,
                  int n_hidden, int n_output) {
    
    // 输入 → 隐藏层
    for (int h = 0; h < n_hidden; h++) {
        hidden[h] = 0.0;
        for (int i = 0; i < n_input; i++) {
            hidden[h] += input[i] * w_ih[i][h];  // 矩阵乘法
        }
        hidden[h] = sigmoid(hidden[h]);  // 激活函数
    }
    
    // 隐藏层 → 输出层
    for (int o = 0; o < n_output; o++) {
        output[o] = 0.0;
        for (int h = 0; h < n_hidden; h++) {
            output[o] += hidden[h] * w_ho[h][o];
        }
        output[o] = sigmoid(output[o]);
    }
}

// Sigmoid 激活函数
double sigmoid(double x) {
    return 1.0 / (1.0 + exp(-x));
}
```

**宏定义**：
```c
USE_FP64=1             // 双精度浮点
```

**性能特征**：
- 计算密集：矩阵乘法
- 超越函数：exp() 调用
- FMA友好：内积计算
- 缓存友好：小数据集

**典型性能**：
```
单核: 10.56 iter/s
4核:  33.22 iter/s (3.15倍)
```

---

### 6. parser-125k (XML 解析)

**测试目标**：文本解析、字符串处理、内存分配

**数据集**：125 KB XML 文档

**核心算法**：
```c
// XML 解析流程
1. 词法分析（Tokenization）
   - 识别标签：<tag>
   - 识别属性：attr="value"
   - 识别文本内容
   
2. 语法分析（Parsing）
   - 构建 DOM 树
   - 验证标签匹配
   - 处理嵌套结构
   
3. 数据提取
   - 查找特定节点
   - 提取属性值
   - 遍历树结构
```

**数据结构**：
```c
typedef struct xml_node {
    char *tag_name;
    char *text;
    struct xml_node **children;
    int num_children;
    struct xml_node *parent;
} xml_node;
```

**性能特征**：
- 字符串密集：大量字符串操作
- 内存分配：动态树构建
- 分支密集：字符判断
- 缓存敏感：指针追踪

**典型性能**：
```
单核: 19.23 iter/s
4核:  70.18 iter/s (3.65倍)
```

---

### 7. radix2-big-64k (Radix-2 FFT)

**测试目标**：快速傅里叶变换、复数运算、位反转

**FFT 规模**：64K 点（65,536）

**核心算法**：
```c
// Cooley-Tukey FFT 算法
void fft_radix2(double complex *x, int n) {
    // 位反转
    bit_reverse(x, n);
    
    // 蝶形运算
    for (int s = 1; s <= log2(n); s++) {
        int m = 1 << s;  // 2^s
        double complex wm = cexp(-2.0 * I * PI / m);
        
        for (int k = 0; k < n; k += m) {
            double complex w = 1.0;
            for (int j = 0; j < m/2; j++) {
                double complex t = w * x[k + j + m/2];
                double complex u = x[k + j];
                x[k + j] = u + t;
                x[k + j + m/2] = u - t;
                w = w * wm;
            }
        }
    }
}
```

**位反转**：
```c
// 将索引的二进制位反转
void bit_reverse(double complex *x, int n) {
    for (int i = 0; i < n; i++) {
        int j = reverse_bits(i, log2(n));
        if (j > i) {
            swap(&x[i], &x[j]);
        }
    }
}
```

**计算量**：
```
复数乘法: n × log2(n) / 2
        = 65536 × 16 / 2
        = 524,288 次复数乘法

FLOP: 5 × 524,288 = 2,621,440 FLOP
(每次复数乘法约5个浮点运算)
```

**宏定义**：
```c
USE_FP64=1             // 双精度浮点
```

**性能特征**：
- 计算密集：大量复数运算
- 内存访问：位反转阶段不规则
- 缓存敏感：跨步访问
- 向量化：蝶形运算可SIMD

**典型性能**：
```
单核: 453.72 iter/s
4核:  1666.67 iter/s (3.67倍)
```

---

### 8. sha-test (SHA-256)

**测试目标**：加密哈希算法、位运算、整数运算

**SHA-256 算法**：
```c
// SHA-256 核心轮函数
for (int i = 0; i < 64; i++) {
    // 扩展消息
    if (i >= 16) {
        w[i] = sigma1(w[i-2]) + w[i-7] + 
               sigma0(w[i-15]) + w[i-16];
    }
    
    // 压缩函数
    e_u32 t1 = h + Sigma1(e) + Ch(e, f, g) + K[i] + w[i];
    e_u32 t2 = Sigma0(a) + Maj(a, b, c);
    
    h = g;
    g = f;
    f = e;
    e = d + t1;
    d = c;
    c = b;
    b = a;
    a = t1 + t2;
}

// 位运算函数
#define ROTR(x, n) (((x) >> (n)) | ((x) << (32 - (n))))
#define Sigma0(x) (ROTR(x, 2) ^ ROTR(x, 13) ^ ROTR(x, 22))
#define Sigma1(x) (ROTR(x, 6) ^ ROTR(x, 11) ^ ROTR(x, 25))
#define Ch(x, y, z) (((x) & (y)) ^ (~(x) & (z)))
#define Maj(x, y, z) (((x) & (y)) ^ ((x) & (z)) ^ ((y) & (z)))
```

**性能特征**：
- 整数密集：32位整数运算
- 位运算密集：循环移位、XOR
- 分支少：利于流水线
- 缓存友好：小工作集

**典型性能**：
```
单核: 172.41 iter/s
4核:  588.24 iter/s (3.41倍)
```

---

### 9. zip-test (ZIP 压缩)

**测试目标**：DEFLATE 压缩算法、LZ77、霍夫曼编码

**ZLIB 压缩流程**：
```c
// DEFLATE 算法
1. LZ77 滑动窗口
   - 查找重复字符串
   - 输出 <长度, 距离> 对
   
2. 霍夫曼编码
   - 构建霍夫曼树
   - 生成变长编码
   - 输出压缩数据
```

**宏定义**：
```c
MITH_MEMORY_ONLY_VERSION   // 仅内存模式
ZLIB_COMPAT_ALL            // ZLIB 兼容
ZLIB_ANSI                  // ANSI 标准
```

**性能特征**：
- 字符串搜索：哈希表查找
- 内存访问：滑动窗口
- 分支密集：匹配判断
- 缓存敏感：历史数据访问

**典型性能**：
```
单核: 142.86 iter/s
4核:  500.00 iter/s (3.50倍)
```

## 命令行参数

### 基本参数

```bash
-i <iterations>        # 迭代次数 (默认 1)
-c <num_contexts>      # 上下文数量 (线程数)
-w <num_workers>       # 工作线程数
-b <bench_repeats>     # 基准测试重复次数
-o <oversubscribe>     # 允许超订阅 (0/1)
-v <verify_output>     # 验证级别
                       #   0: 关闭验证（性能测试）
                       #   1: 基础验证
                       #   2: 详细验证
-V <reporting_threshold> # 报告阈值
-pgo=<value>           # PGO 训练运行
-D=<dataname>          # 数据集名称
-P=<hardware_desc>     # 硬件描述
```

### 使用示例

**单核性能测试**：
```bash
./cjpeg-rose7-preset.exe -v0 -i100
./core.exe -v0 -i100
./linear_alg-mid-100x100-sp.exe -v0 -i100
# ... 其他7个工作负载
```

**4核并行测试**：
```bash
./cjpeg-rose7-preset.exe -v0 -c4 -i100
./core.exe -v0 -c4 -i100
# ... 其他工作负载
```

**完整认证测试**：
```bash
make TARGET=linux64 XCMD='-c4' certify-all
```

## 性能评分计算

### 几何平均加权评分

**公式**：
```
CoreMark-Pro Score = 1000 × geometric_mean(normalized_scores)

normalized_score[i] = (measured_score[i] / reference_score[i]) × scale_factor[i]

geometric_mean = (∏(normalized_score[i]))^(1/n)
```

### 参考分数和缩放因子

| 工作负载                  | 参考分数 | 缩放因子 | 说明            |
| ------------------------- | -------- | -------- | --------------- |
| cjpeg-rose7-preset        | 40.3438  | 1        | JPEG 压缩       |
| core                      | 2855     | 10000    | CoreMark 增强   |
| linear_alg-mid-100x100-sp | 38.5624  | 1        | LINPACK         |
| loops-all-mid-10k-sp      | 0.87959  | 1        | Livermore Loops |
| nnet_test                 | 1.45853  | 1        | 神经网络        |
| parser-125k               | 4.81116  | 1        | XML 解析        |
| radix2-big-64k            | 99.6587  | 1        | FFT             |
| sha-test                  | 48.5201  | 1        | SHA-256         |
| zip-test                  | 21.3618  | 1        | ZIP 压缩        |

### 计算示例

**假设测试结果**：
```
SingleCore (iter/s):
  cjpeg:       156.25
  core:          1.30
  linear_alg:  409.84
  loops:         6.25
  nnet:         10.56
  parser:       19.23
  radix2:      453.72
  sha:         172.41
  zip:         142.86

计算归一化分数:
  cjpeg:    (156.25 / 40.3438) × 1 = 3.873
  core:     (1.30 / 2855) × 10000 = 4.554
  linear:   (409.84 / 38.5624) × 1 = 10.628
  loops:    (6.25 / 0.87959) × 1 = 7.105
  nnet:     (10.56 / 1.45853) × 1 = 7.241
  parser:   (19.23 / 4.81116) × 1 = 3.997
  radix2:   (453.72 / 99.6587) × 1 = 4.552
  sha:      (172.41 / 48.5201) × 1 = 3.553
  zip:      (142.86 / 21.3618) × 1 = 6.688

几何平均:
  GM = (3.873 × 4.554 × 10.628 × 7.105 × 7.241 × 3.997 × 4.552 × 3.553 × 6.688)^(1/9)
     = 5.440

CoreMark-Pro SingleCore = 1000 × 5.440 = 5440.00
```

### 多核扩展比

```
Scaling = MultiCore Score / SingleCore Score

示例:
MultiCore (4核) = 19183.84
SingleCore      =  5439.59
Scaling         = 19183.84 / 5439.59 = 3.53
```

## 内存占用

### 静态占用 (text + data + bss)

| 工作负载                  | text (KB) | data (KB) | bss (KB) | 总计 (KB) |
| ------------------------- | --------- | --------- | -------- | --------- |
| cjpeg-rose7-preset        | 110       | 262       | 0.2      | 372       |
| core                      | 69        | 2         | 2        | 73        |
| linear_alg-mid-100x100-sp | 74        | 3         | 1        | 78        |
| loops-all-mid-10k-sp      | 89        | 5         | 4        | 98        |
| nnet_test                 | 72        | 3         | 39       | 115       |
| parser-125k               | 88        | 2         | 0.2      | 91        |
| radix2-big-64k            | 1415      | 2         | 1        | 1418      |
| sha-test                  | 79        | 2         | 0.2      | 82        |
| zip-test                  | 132       | 3         | 0.2      | 135       |

### 动态峰值内存 (运行时)

| 工作负载                  | 峰值内存 (KB) | 主要占用    |
| ------------------------- | ------------- | ----------- |
| cjpeg-rose7-preset        | 138           | 图像缓冲区  |
| core                      | 12            | 链表节点    |
| linear_alg-mid-100x100-sp | 66            | 矩阵数据    |
| loops-all-mid-10k-sp      | 3346          | 多个数组    |
| nnet_test                 | 49            | 权重矩阵    |
| parser-125k               | 1019          | XML DOM树   |
| radix2-big-64k            | 1543          | 64K复数数组 |
| sha-test                  | 1027          | 哈希缓冲区  |
| zip-test                  | 3340          | 滑动窗口    |

## 移植指南

### 抽象层接口 (th_al.c)

**必须实现的函数**：

```c
// 初始化
void al_main(int argc, char *argv[]);

// 时钟/计时
void al_signal_start(void);
void al_signal_finished(void);
e_u32 al_ticks_per_sec(void);
size_t al_microseconds_per_tick(void);

// 线程（可选，单线程模式不需要）
void *al_smp_worker_thread(void *);
int al_smp_create(ee_work_item_t *);
int al_smp_spawn(ee_work_item_t *);
int al_smp_wait(ee_work_item_t *);

// 输出
int al_printf(const char *fmt, ...);
```

### 移植选项

**1. POSIX 线程（默认）**：
```c
// 使用 al_smp.c
#include <pthread.h>

void *al_smp_worker_thread(void *arg) {
    pthread_create(...);
}
```

**2. 单线程模式**：
```c
// 使用 al_single.c 替代 al_smp.c
// 不需要线程支持
```

**3. 自定义线程**：
```c
// 实现 al_smp.c 中的函数
// 使用目标平台的线程SDK
```

### 裸机移植

**启动流程**：
```c
void al_main(int argc, char *argv[]) {
    // 1. 硬件初始化
    SystemInit();
    
    // 2. 时钟初始化
    TimerInit();
    
    // 3. 串口初始化（用于输出）
    UARTInit();
    
    // 4. 中断使能
    EnableInterrupts();
}
```

**计时实现**：
```c
volatile e_u32 tick_counter = 0;

void SysTick_Handler(void) {
    tick_counter++;
}

void al_signal_start(void) {
    start_tick = tick_counter;
}

void al_signal_finished(void) {
    end_tick = tick_counter;
}

e_u32 al_ticks_per_sec(void) {
    return 1000;  // 1ms tick
}
```

### 输出重定向

**选项 1：串口输出**：
```c
int al_printf(const char *fmt, ...) {
    char buffer[256];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);
    
    UART_SendString(buffer);
    return strlen(buffer);
}
```

**选项 2：半主机调试**：
```c
int al_printf(const char *fmt, ...) {
    // 使用调试器的 printf
    return debug_printf(fmt, ...);
}
```

## 运行规则

### 必须遵守

1. ✅ **最小运行时间**：
   ```
   每个工作负载运行时间 >= 1000 × 计时器分辨率
   
   示例: 10ms 计时器 → 至少运行 10 秒
   ```

2. ✅ **认证流程**：
   ```bash
   make TARGET=<target> certify-all
   # 或手动运行所有9个工作负载
   ```

3. ✅ **验证模式**：
   ```bash
   每个工作负载必须通过 -v1 验证
   ```

4. ✅ **统一编译选项**：
   ```
   所有工作负载使用相同编译器标志
   结果报告必须披露编译选项
   ```

### 允许修改

✅ 迭代次数 (`-i`)  
✅ 编译器工具链  
✅ 编译/链接选项  
✅ `mith/al` 抽象层实现  
✅ Makefile 或 IDE 项目  
✅ PGO（必须用于所有工作负载）  

### 禁止修改

❌ `benchmarks/` 源码  
❌ `workloads/` 源码  
❌ 数据集内容  

## CPU 亲和性集成

### 在 cpu_affinity_test.sh 中的应用

```bash
run_2coremark_pro() {
    local cpu_list=$1
    local process_count=$2
    
    echo "[$(date)] 测试 2.CoreMark-pro (CPU: $cpu_list, Threads: $process_count)"
    
    cd /path/to/2.CoreMark-pro/builds/linux64/gcc64/bin
    
    # 使用 taskset 绑定 CPU
    for workload in cjpeg-rose7-preset core linear_alg-mid-100x100-sp \
                    loops-all-mid-10k-sp nnet_test parser-125k \
                    radix2-big-64k sha-test zip-test; do
        
        echo "  运行 $workload..."
        taskset -c $cpu_list ./$workload.exe \
            -v0 \                    # 关闭验证
            -c $process_count \      # 上下文数量
            -i 100 \                 # 100次迭代
            | tee -a "$LOG_FILE"
    done
    
    # 计算 CoreMark-Pro 总分
    perl /path/to/util/perl/coremark-pro.pl \
        --input "$LOG_FILE" \
        --output coremark_pro_score.txt
}
```

### 不同核数配置

**1核测试**：
```bash
run_2coremark_pro "0" 1
# 测量单核性能基准
```

**4核测试**：
```bash
run_2coremark_pro "0-3" 4
# 测量小规模并行
# 预期扩展比: 3.0-3.7倍
```

**12核测试**：
```bash
run_2coremark_pro "0-11" 12
# 测量大规模并行
# 可能受 Amdahl 定律限制
```

### 扩展性分析

**理想扩展**：
```
1核:   基准 (1.00×)
2核:   2.00×
4核:   4.00×
8核:   8.00×
12核: 12.00×
```

**实际扩展** (典型值):
```
工作负载              1核    2核    4核    8核    12核
cjpeg-rose7-preset   1.00   1.90   3.56   6.50    9.20
core                 1.00   1.88   3.75   7.10   10.50
linear_alg           1.00   1.85   3.49   6.40    9.00
loops                1.00   1.92   3.61   6.80   10.10
nnet_test            1.00   1.75   3.15   5.50    7.80
parser-125k          1.00   1.95   3.65   6.90   10.30
radix2-big-64k       1.00   1.92   3.67   7.00   10.40
sha-test             1.00   1.85   3.41   6.20    8.90
zip-test             1.00   1.88   3.50   6.60    9.80

平均扩展比           1.00   1.88   3.53   6.56    9.56
```

**扩展瓶颈**：
- 内存带宽饱和（4核以上）
- Amdahl 定律（串行部分）
- 缓存一致性开销
- NUMA 影响（多 socket）

## 总结

### CoreMark-Pro 的核心价值

✅ **全面性**：9个工作负载覆盖整数和浮点  
✅ **真实性**：模拟实际应用场景  
✅ **可扩展性**：支持单核到多核测试  
✅ **标准化**：行业认可的基准测试  
✅ **可移植性**：支持从微控制器到HPC  

### 适用场景

1. **处理器选型**：对比不同 CPU 性能
2. **编译器优化**：验证优化效果
3. **多核扩展性**：评估并行能力
4. **系统调优**：识别性能瓶颈
5. **硬件验证**：新芯片性能测试

### 与其他基准测试对比

| 基准测试         | 测试范围   | 复杂度 | 多核 | 适用场景      |
| ---------------- | ---------- | ------ | ---- | ------------- |
| **CoreMark**     | CPU流水线  | 简单   | ✓    | 嵌入式        |
| **CoreMark-Pro** | 完整处理器 | 中等   | ✓✓   | 通用处理器    |
| **SPEC CPU**     | 应用性能   | 复杂   | ✓✓   | 服务器/工作站 |
| **STREAM**       | 内存带宽   | 简单   | ✓    | 内存子系统    |
| **LINPACK**      | 浮点性能   | 中等   | ✓✓   | HPC           |

CoreMark-Pro 提供了比 CoreMark 更全面、比 SPEC CPU 更轻量的综合性能评估，特别适合评估多核处理器的整体性能。