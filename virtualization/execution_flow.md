# QEMU Plugin 执行流程详解

## 概述

本文档介绍 QEMU plugin 的执行流程，包括 TB 翻译、callback 统计机制，以及 Checkpoint 和 BBV 插件的实现原理。

---

## 1. 核心概念

### 1.1 Translation Block (TB)
- **定义**：一段连续的客户机指令序列，从入口点到控制流改变（跳转/分支/异常）
- **生命周期**：翻译（首次）→ 缓存 → 执行（多次）
- **缓存机制**：避免重复翻译，提高性能

### 1.2 Plugin 回调类型

| 回调类型 | 调用时机 | 频率 | 用途 |
|---------|---------|------|------|
| **TB 翻译回调** | TB 首次翻译 | 每个 TB 1次 | 注册插桩操作 |
| **Inline 操作** | TB 执行时 | 每次执行 | 简单计数（~5 cycles） |
| **普通 Callback** | TB 执行时 | 每次执行 | 复杂逻辑（~30 cycles） |
| **条件 Callback** | TB 执行且满足条件 | 低频 | 条件触发（~1 cycle 检查） |

---

## 2. TB 翻译和执行流程

### 2.1 四个关键 Hook Points

```c
// accel/tcg/translator.c : translator_loop()
void translator_loop(...) {
    // 【Hook 1】TB 开始
    plugin_enabled = plugin_gen_tb_start(cpu, db);
    
    while (true) {  // 指令循环
        // 【Hook 2】每条指令开始
        if (plugin_enabled) plugin_gen_insn_start(cpu, db);
        
        ops->translate_insn(db, cpu);  // 翻译指令
        
        // 【Hook 3】每条指令结束
        if (plugin_enabled) plugin_gen_insn_end();
        
        if (db->is_jmp != DISAS_NEXT) break;
    }
    
    // 【Hook 4】TB 结束 - 最关键！
    if (plugin_enabled) {
        plugin_gen_tb_end(cpu, db->num_insns);
        // └─→ 调用 qemu_plugin_tb_trans_cb()
        //     └─→ 调用我们的 vcpu_tb_trans()
        //         └─→ 注册 inline & callback
        //     └─→ plugin_gen_inject() 注入代码
    }
}
```

### 2.2 plugin_gen_tb_end() - 核心函数

```c
// accel/tcg/plugin-gen.c
void plugin_gen_tb_end(CPUState *cpu, size_t num_insns)
{
    struct qemu_plugin_tb *ptb = tcg_ctx->plugin_tb;
    
    ptb->n = num_insns;
    
    // 【步骤 1】调用插件的 TB 翻译回调
    qemu_plugin_tb_trans_cb(cpu, ptb);
    //   └─→ 遍历所有注册的插件
    //       └─→ 调用 vcpu_tb_trans(id, tb)  ← 我们的函数！
    //           └─→ 注册 inline 操作和 callback
    
    // 【步骤 2】将注册的操作注入到 TCG IR
    plugin_gen_inject(ptb);
    //   └─→ 遍历 TCG IR，找到 plugin_cb 标记
    //       └─→ 生成实际的 TCG 代码（inline/callback）
    //       └─→ 移除标记操作码
}
```

### 2.3 plugin_gen_inject() - 代码注入

```c
// accel/tcg/plugin-gen.c
static void plugin_gen_inject(struct qemu_plugin_tb *plugin_tb)
{
    // 遍历 TCG IR，处理 plugin_cb 标记
    QTAILQ_FOREACH_SAFE(op, &tcg_ctx->ops, link, next) {
        if (op->opc == INDEX_op_plugin_cb) {
            switch (from) {
            case PLUGIN_GEN_FROM_TB:
                cbs = plugin_tb->cbs;  // 我们注册的回调
                for (i = 0; i < cbs->len; i++) {
                    inject_cb(&cbs[i]);  // 生成实际代码
                }
                break;
            }
            tcg_op_remove(tcg_ctx, op);  // 移除标记
        }
    }
}

// 生成 inline 加法
gen_inline_add_u64_cb():
    tcg_gen_ld_i64(val, ptr, 0);     // val = *ptr
    tcg_gen_addi_i64(val, val, imm); // val += n_insns
    tcg_gen_st_i64(val, ptr, 0);     // *ptr = val
    
// 生成回调函数调用
gen_udata_cb():
    tcg_gen_call2(callback, cpu_index, userdata);
```

**编译后的主机代码（x86_64）**：
```asm
; Inline: insn_count += 5
mov rax, [r14+offset]    ; 加载
add rax, 5               ; 累加
mov [r14+offset], rax    ; 存储
; ~5 cycles

; Callback: vcpu_insn_exec()
mov edi, vcpu_index      ; 参数
call vcpu_insn_exec      ; 调用
; ~30 cycles
```

---

## 3. Checkpoint 插件实现

### 3.1 vcpu_tb_trans - 注册插桩操作

```c
static void vcpu_tb_trans(qemu_plugin_id_t id, struct qemu_plugin_tb *tb)
{
    size_t n_insns = qemu_plugin_tb_n_insns(tb);

    // 【Inline】累加指令计数（~5 cycles）
    qemu_plugin_register_vcpu_tb_exec_inline_per_vcpu(
        tb, QEMU_PLUGIN_INLINE_ADD_U64, insn_count_u64(), n_insns);
    // 效果：vcpu->insn_count += n_insns

    // 【Callback】检查 checkpoint（~30 cycles）
    qemu_plugin_register_vcpu_tb_exec_cb(
        tb, vcpu_insn_exec, QEMU_PLUGIN_CB_NO_REGS, NULL);
}
```

| 操作 | 实现 | 性能 | 用途 |
|------|------|------|------|
| **Inline** | 直接嵌入 TCG 代码 | ~5 cycles | 简单计数 |
| **Callback** | 生成函数调用 | ~30 cycles | 复杂逻辑 |

---

### 3.2 TB 执行流程

```
翻译阶段（首次，1次）          执行阶段（每次执行）
─────────────────────        ─────────────────────
vcpu_tb_trans()              TB 主机代码:
 ├─ 注册 inline: +=5          ├─ insn_count += 5  (5 cycles)
 └─ 注册 callback             ├─ call vcpu_insn_exec() (30 cycles)
      ↓                       └─ 执行 5 条客户机指令
生成 TCG → 编译主机码
```

**关键**：
- 翻译：每个 TB 1次
- 执行：可重复多次（TB 缓存）

---

## 5. vcpu_insn_exec 执行检查

```c
static void vcpu_insn_exec(unsigned int vcpu_index, void *udata) {
    Vcpu *vcpu = qemu_plugin_scoreboard_find(vcpus, vcpu_index);
    if (vm_paused || !simpoints) return;  // 快速路径
    
    g_rw_lock_reader_lock(&simpoints_lock);
    for (i = 0; i < simpoints->len; i++) {
        if (!sp->created && vcpu->insn_count >= sp->checkpoint_at) {
            create_checkpoint(vcpu_index, sp);
            break;
        }
    }
    g_rw_lock_reader_unlock(&simpoints_lock);
}
```

**性能优化**：
- 快速检查：vm_paused 立即返回
- 读锁：允许多 vCPU 并发
- 精度：±TB_size（通常<10条指令）

---

## 6. Checkpoint 创建

```c
static void create_checkpoint(unsigned int vcpu_index, SimPoint *sp) {
    g_mutex_lock(&checkpoint_mutex);
    pause_vm();  // vm_paused = TRUE
    
    // 1. 保存 SimPoint 信息（id, interval, weight, insn_count）
    // 2. 保存所有 vCPU 的 CPU 状态
    // 3. 保存内存快照
    // 4. 生成恢复脚本
    
    resume_vm();  // vm_paused = FALSE
    sp->created = TRUE;
    g_mutex_unlock(&checkpoint_mutex);
}
```

---

## 7. 完整流程总结

```
初始化: qemu_plugin_install()
 ├─ load_simpoints() → 19个 SimPoints
 └─ register_vcpu_tb_trans_cb(vcpu_tb_trans)

TB 翻译（每个TB 1次）: translator_loop()
 ├─ plugin_gen_tb_start() → 插入 TCG 标记
 ├─ 翻译指令循环
 └─ plugin_gen_tb_end()
     ├─ 调用 vcpu_tb_trans()
     │   ├─ register inline: insn_count += N
     │   └─ register callback: vcpu_insn_exec
     └─ plugin_gen_inject() → 注入主机代码

TB 执行（可重复多次）:
 ├─ inline: insn_count += 5  (~5 cycles)
 ├─ callback: vcpu_insn_exec() (~30 cycles)
 │   └─ if (count >= checkpoint_at) create_checkpoint()
 └─ 执行客户机指令
```

**频率统计**（1000次热点TB）:
| 操作 | 翻译阶段 | 执行阶段 |
|------|---------|---------|
| vcpu_tb_trans | 1次 | - |
| inline 执行 | - | 1000次 |
| callback 调用 | - | 1000次 |

---

## 8. 性能关键点

| 操作 | 开销 | 频率 | 优化方法 |
|------|------|------|---------|
| inline (+= N) | ~5 cycles | 每次TB执行 | 编译为机器码（无函数调用） |
| callback | ~100 cycles | 每次TB执行 | 快速路径 if (paused) return |
| vcpu_tb_trans | ~1000 cycles | 每个TB 1次 | TB缓存避免重复翻译 |

**关键优化**:
- Inline 比回调快 **20倍**（无函数调用开销）
- 快速检查：99.9% 情况立即返回
- 读锁：允许多 vCPU 并发读取 SimPoints

---

## 9. 调试技巧

```bash
# 查看 TB 翻译日志
qemu-system-aarch64 -d plugin,exec,tb_op -D /tmp/qemu.log ...

# 插件内打印TB信息
g_print("TB: vaddr=0x%lx, insns=%zu\n", vaddr, n_insns);

# 验证指令计数
if (vcpu->insn_count % 1000000 == 0)
    g_print("vCPU %u: %lu instructions\n", vcpu_index, vcpu->insn_count);
```

---

## 10. Hook 调用位置

**翻译阶段** (accel/tcg/translator.c: translator_loop):
```c
plugin_gen_tb_start()                    // 1. TB开始 → 插入标记
[指令翻译循环]
  plugin_gen_insn_start()                // 2. 指令开始
  ops->translate_insn()
  plugin_gen_insn_end()                  // 3. 指令结束
plugin_gen_tb_end()                      // 4. TB结束 → 关键！
  ├─ qemu_plugin_tb_trans_cb()           //    调用 vcpu_tb_trans()
  └─ plugin_gen_inject()                 //    注入 inline/callback代码
```

**执行阶段** (生成的主机代码):
```asm
mov rax, [r14+offset]   ; inline: insn_count += 5
add rax, 5
mov [r14+offset], rax
call vcpu_insn_exec     ; callback
```

---

## 11. 总结

| 阶段 | 操作 | 文件位置 | 频率 | 开销 |
|------|------|---------|------|------|
| 初始化 | 注册回调 | chpt.c | 1次 | 忽略 |
| 翻译 | vcpu_tb_trans | translator.c → plugin-gen.c | 每TB 1次 | 低 |
| 执行 | inline += | 主机代码 | 每次TB执行 | ~5 cycles |
| 执行 | vcpu_insn_exec | 主机代码 | 每次TB执行 | ~30 cycles |
| Checkpoint | 暂停/保存/恢复 | chpt.c | 19次 | 高（但罕见） |

**设计精髓**:
- **Inline计数**: 极快统计（无函数调用）
- **Callback检查**: 灵活判断（快速路径优化）
- **Scoreboard**: 无锁per-vCPU数据
- **TB缓存**: 翻译1次，执行多次
- **总开销**: < 5% (长时间运行)

---

## 12. Callback 统计机制

### 数据结构: Scoreboard

```c
typedef struct Vcpu {
    uint64_t insn_count;  // 关键：总指令计数
} Vcpu;

static struct qemu_plugin_scoreboard *vcpus;
vcpus = qemu_plugin_scoreboard_new(sizeof(Vcpu));

// 获取insn_count句柄
static qemu_plugin_u64 insn_count_u64(void) {
    return qemu_plugin_scoreboard_u64_in_struct(vcpus, Vcpu, insn_count);
}
```

**作用**: Per-vCPU数据，无锁访问

### 注册统计操作

```c
static void vcpu_tb_trans(qemu_plugin_id_t id, struct qemu_plugin_tb *tb) {
    size_t n_insns = qemu_plugin_tb_n_insns(tb);
    
    // Inline: 累加指令数（编译为3条机器指令）
    qemu_plugin_register_vcpu_tb_exec_inline_per_vcpu(
        tb, QEMU_PLUGIN_INLINE_ADD_U64, insn_count_u64(), n_insns);
    // 效果: vcpu->insn_count += n_insns (~5 cycles)
    
    // Callback: 检查checkpoint（函数调用）
    qemu_plugin_register_vcpu_tb_exec_cb(
        tb, vcpu_insn_exec, QEMU_PLUGIN_CB_NO_REGS, NULL);
    // 效果: call vcpu_insn_exec() (~30 cycles)
}
```

### Inline生成代码

```c
// TCG中间代码 (plugin-gen.c: gen_inline_add_u64_cb):
tcg_gen_ld_i64(val, ptr, 0);         // val = *ptr
tcg_gen_addi_i64(val, val, n_insns); // val += n_insns
tcg_gen_st_i64(val, ptr, 0);         // *ptr = val

// 编译后的x86_64机器码:
mov rax, [r14+offset]    // 加载 insn_count
add rax, 5               // += TB指令数
mov [r14+offset], rax    // 写回
```

### Callback检查逻辑

```c
static void vcpu_insn_exec(unsigned int vcpu_index, void *udata) {
    Vcpu *vcpu = qemu_plugin_scoreboard_find(vcpus, vcpu_index);
    
    if (vm_paused || !simpoints) return;  // 快速路径
    
    g_rw_lock_reader_lock(&simpoints_lock);
    for (i = 0; i < simpoints->len; i++) {
        if (!sp->created && vcpu->insn_count >= sp->checkpoint_at) {
            create_checkpoint(vcpu_index, sp);  // 达到阈值！
            break;
        }
    }
    g_rw_lock_reader_unlock(&simpoints_lock);
}
```

**统计流程**:
1. **Inline累加** → 每个TB自动更新insn_count（无开销）
2. **Callback检查** → 读取insn_count与阈值对比（快速返回）
3. **创建Checkpoint** → 达到阈值时保存状态（罕见）

---

## 13. BBV Plugin 分析

### 核心差异

| 方面 | BBV Plugin | Checkpoint Plugin |
|------|-----------|------------------|
| 目标 | 生成BB向量 | 创建checkpoint |
| 统计 | 每个BB执行次数 | 全局指令数 |
| Inline | 2个（总数+BB数） | 1个（总数） |
| Callback | 条件回调（interval达到） | 普通回调（每TB） |
| 开销 | ~0.01% | ~0.35% |

### BBV数据结构

```c
typedef struct Bb {
    uint64_t vaddr;
    size_t n_insns;
    qemu_plugin_u64 count;  // Per-vCPU的执行计数数组
} Bb;

GHashTable *bb_hash;  // vaddr → Bb*
```

### BBV注册方式

```c
static void vcpu_tb_trans(qemu_plugin_id_t id, struct qemu_plugin_tb *tb) {
    uint64_t vaddr = qemu_plugin_tb_vaddr(tb);
    Bb *bb = lookup_or_create_bb(vaddr);  // 哈希表查找
    
    // Inline 1: 总指令数
    qemu_plugin_register_vcpu_tb_exec_inline_per_vcpu(
        tb, QEMU_PLUGIN_INLINE_ADD_U64, insn_count_u64(), n_insns);
    
    // Inline 2: BB执行次数
    qemu_plugin_register_vcpu_tb_exec_inline_per_vcpu(
        tb, QEMU_PLUGIN_INLINE_ADD_U64, bb->count, n_insns);
    
    // 条件回调: 达到interval时输出BBV
    qemu_plugin_register_vcpu_tb_exec_cond_cb(
        tb, vcpu_interval_exec, QEMU_PLUGIN_CB_NO_REGS,
        QEMU_PLUGIN_COND_GE, insn_count_u64(), interval, NULL);
}
```

### 条件回调优化

```asm
; 条件回调编译后的代码:
mov rax, [r14+offset]     ; 加载 insn_count
cmp rax, 100000000        ; 比较 count >= interval
jl  skip                  ; 小于则跳过 (99.999%情况)
call vcpu_interval_exec   ; 达到阈值才调用
skip:
```

**性能优势**: 99.999%时只有1条比较指令（~1 cycle）

### BBV输出格式

```
T:1:50000 :5:30000 :12:20000
```
- `T`: 时间片标记
- `:index:count`: BB索引:执行次数
- 每个interval输出一行

### 关键技术

1. **哈希表**: 动态发现BB，O(1)查找
2. **双Scoreboard**: vcpus + bb->count
3. **条件回调**: 硬件级判断（~1 cycle）
4. **Per-vCPU文件**: 避免多核冲突

**总开销**: 2个inline(10 cycles) + 条件检查(1 cycle) = **0.01%**

---

## 附录：源码位置

| 功能 | 文件路径 | 关键函数 |
|------|---------|---------|
| TB翻译循环 | accel/tcg/translator.c | translator_loop() |
| Plugin Hook点 | accel/tcg/plugin-gen.c | plugin_gen_tb_start/end() |
| 回调注册 | plugins/core.c | qemu_plugin_tb_trans_cb() |
| Inline代码生成 | accel/tcg/plugin-gen.c | gen_inline_add_u64_cb() |
| API实现 | plugins/api.c | qemu_plugin_register_*() |
| Checkpoint插件 | contrib/plugins/chpt.c | vcpu_tb_trans(), vcpu_insn_exec() |
| BBV插件 | contrib/plugins/bbv.c | vcpu_tb_trans(), vcpu_interval_exec() |

---


