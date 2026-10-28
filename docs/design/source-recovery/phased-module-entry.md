# 模块入口走 phased 路线（2026-10-28，步骤 A 已实现）

## 问题

`func_main_0`（模块初始化函数）只要含有真实的 `try/catch`，整条记录就会被拒
（`CleanupError::kHandler`）。big.abc 中 12,660 条拒绝里有 11,175 条（88%）属于这种
情况，主要来自两类代码：

- 工具注入的顶层 `try { … } catch (e) { _qf_globals_*["get$e$…"] = e; }`；
- 空的 `catch {}`：handler 的 `code_size` 为 0，handler 直接就是正常的后续代码。

根因有两层：

- 模块根没有 `FunctionContext` owner，而支持 try/catch 的 phased 路线需要 owner；
- 记录装配（`record_tree.cc`、`script_tree.cc`）只接受 normal 入口。

cleanup-entry 路线只能擦除"poplexenv 后 rethrow"这一种环境清理 handler。

## 统一方案（扩展现有 owner，不新增并行机制）

1. **入口上下文**：给 `FunctionContext` 增加 `ModuleEntryContext` 变体，内容包括
   entry 方法、位置、身份、帧（`a0`/`a1`/`a2`）、`kFunction`、strict，没有创建点。
   phased 的 `PhasedOwner` 也为它增加对应的入口形态。
2. **按结构选路**：
   - 入口的全部 handler 都是环境清理 handler（`sta*`、`poplexenv+`、`lda*`、
     `throw acc`）时，继续走现有 cleanup-entry 路线，已完成记录的输出保持字节一致；
   - 否则走 phased。

   选路依据是对 handler 的结构分类，不是"先失败再重试"。
3. **记录装配**：phased 入口作为 family 的根成员，由 `family_tree` 统一构建，与
   phased 子函数共用同一条路径，然后对根树套用模块层步骤（导出、globals、destination
   校验）。
4. **模块变量声明**：首次写入位于 `try`（或 `catch`）内时，在模块顶层先声明
   `let x;`，在原位置写赋值。复用函数里已有的 deferred-declaration 证明：声明点
   必须支配所有访问，且模块绑定的 TDZ 语义不变（声明前读取仍会抛出）。
5. **校验**：phased 已有的 control、evaluation、TreeFlow、RecoveryFlow 全部照常运行；
   记录装配阶段按原样重新检查导出、模块别名与 destination。

## 分步交付

| 步骤 | 内容 | 验收 |
|---|---|---|
| A | 入口上下文、按结构选路、family 根成员的 phased 构建（先只支持首次写入不在 `try` 内的模块变量） | fixture 差分；cohort 零回退、零文本变化；big.abc 完成数上升 |
| B | try/catch 内首次写入的模块变量：顶层 `let` 加原位赋值 | fixture 差分（含 TDZ 与 catch 路径）；big.abc |
| C | 清理 big.abc 中剩余的长尾原因 | 同上 |

不新增 source premise：顶层 `try/catch` 和模块级 `let` 都是标准 ES module 语义。

## 实现记录（步骤 A）

- big.abc 中有真实 try/catch 的 11,180 个模块入口，其 `stmodulevar` 全部位于真实
  try 之外，因此步骤 B（try 内首次写入的提升声明）目前不需要。
- 还发现一个更早被拒的独立问题：新编译器常把 catch handler 的 code_size 记为 0。
  现由 `BuildExceptionRegions` 按正常汇合点推断 handler 范围，并支持空 catch。
- 实现位置：
  - `function_context.h`：`EntryTreeContext`；
  - `environment_cleanup`：`OnlyEnvironmentCleanupHandlers`；
  - `module_aliases` 与 `module_alias_tree`：entry 模式；
  - `family_tree`：`EntryDeclarations`；
  - `module_initialization`：`ProveEntryModuleLoads`；
  - `family_writes`：独立重算；
  - `record_tree`：选路与 phased 分支。
