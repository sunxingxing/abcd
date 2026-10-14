# 拒绝义务与位置（Round 42）

这是诊断协议，不改变准入、控制布局、表达式组合、声明位置或打印。
所有既有顶层错误枚举值保留；新增的义务、下层原因和位置不参与成功判断。

## 树阶段

`FunctionTreeFailure` 保留控制校验、completion、tree-flow、tree-structure、
树语义和 expression 的实际原因。`TreeObligation` 区分 control verification、
completion、decision edge copies、decision prefix、catch scope/staging、expression
ownership、deferred declaration、structure/flow/semantics、bindings/capture/placement。
检查拥有操作时记录 `OpId`；只拥有边、块、控制节点或整体验证时，不能用“最后访问的
操作”补一个 PC。嵌套检查使用作用域 witness，返回后恢复上下文。

`TreeLocation` 的 method、operation、block、control、edge、expression、statement
均为可选强类型身份。`pc` 是原始输入 PC，`working_pc` 是当前工作体 PC；二者来自
不可变 origin。真实 PC 0 与缺失 PC 不同。try/try-exit/loop/loop-transfer 使用
synthetic kind + ID，不制造字节码 PC。expected/actual owner 只在检查确实持有该
关系时记录，不重跑求值计划猜测归属。

树 flow 的下层拒绝通过可选诊断输出保留；检查条件、检查顺序与错误返回值
不变；新增固定 witness 随 builder/verifier 的对象大小计费。没有局部身份的全局验证仍只报告方法与义务，不声称定位到某条指令。

## Deferred cell

`FamilyTreeError::kDeferredCell` 细分为 `ExternalRoot`、`PhasedParent`、`Placement`
和 `Binding`。从实际 incoming capture/deferred store 记录 creator、child、occurrence、
cell、requested domain、环境 allocation、创建和初始化 store 的位置。creator 是
必须发射声明的拥有者，不是 family 遍历最后访问的成员。

仅失败时扫描已经计费的捕获事实以定位对应 occurrence；不重新计算绑定、不改选
store、不改变需求格。缓冲仍由原有 owner 计费；witness 为固定大小，查找最坏为
一次线性扫描，PC 的指令边界查找为 O(log N)。没有新增成功路径索引。

## 输入与记录入口

`PhasedFlowFailure` 原样保留 `ExceptionFlowResult` 的 Semantic/Flow/Dispatch 失败
alternative；有真实指令边界时同时记录原始/工作 PC，创建点保持单独字段。
资源/工作预算失败不伪造指令位置。

正常 `FunctionInputFailure` 携带实际被读方法的 identity，避免类 initializer 的
错误 PC 被挂到外层创建者。记录入口的 module kind、模块绑定检查和属性读取失败
保留真实类型化原因；`RecordProgramFailure::pc` 也允许缺失。

## 发布

- Family route：`source_failure.tree` / `source_failure.deferred`。
- Record entry：`source_tree` / `source_deferred`。
- 两者具有同样的 JSON witness 结构；`abc_semantic --record-source` 使用相同的
  `DescribeRecordFailure`，不再另行推测或折叠原因。
- method receipts 的同一结构支持解析往返；非法枚举、超宽 ID 和 synthetic 的
  伪造 PC 被拒绝。有限原因链沿用现有长度与截断规则。
- 新暴露的 tree-verifier 原因不重新分类既有外层失败；资源、工作、输出以及
  进程级故障的原有判据不变。

验收要求：相对 Round 40 两套探索 manifest，所有 record 的状态不变，所有原已完整
源码逐字节相同；固定集、定向诊断测试和完整构建分别记录，不能互相替代。

## 本轮验证

完整构建成功；`ctest --preset source-regression -j 1` 65/65。
`tree_failure_test`、`tree_failure_cli_test`、`source_route_test`、
`record_failure_json_test`、`family_route_failure_test` 5/5。
相关定向串行组 27/27（含上述部分测试）：

- source_route_cli、control_terminal、control_decision、control_plan、class_tree_context；
- function_tree、function_input、capture_plan、family_cells、phased_guarded_reads；
- phased_reachability_cli、phased_method_source；
- record_import_tree、record_tree、record_tree_source、record_import_tree_source；
- record_function、record_function_js、record_function_construct_js；
- call_references_cli、language_source_cli、language_source_corpus、guarded_captures_cli；
- family_route_failure、record_failure_json、tree_failure、tree_failure_cli。

名称均带 `_test`。本组无失败；未重跑其他已知失败矩阵。
探索集的完整重计和无漂移证据见 [round-42-refusals.md](round-42-refusals.md)。

## Decision-prefix checks (Round 62)

`DecisionPrefixError` is a fixed-size diagnostic enum, separate from unchanged
`FunctionTreeError` and `TreeObligation` values. It names synthetic/control/phase
unit, block, commit/empty/branch predicate, copy-index, prefix arms/decision/end,
and missing predicate/statement-expression checks. It does not grant admission.
`tree.decision_prefix` publishes the sub-reason; `tree.prefix_event` identifies
the event that cannot be placed. The main `location` retains the actual owning
control node. Normal control nodes do not acquire a guessed PC: their real
branch event carries its own original/working PC in `prefix_event`. Synthetic
Try units retain their typed identity. A selection's current placement is
reported when known. Both record-entry and route JSON use the same fields and
receipt parser; `abc_semantic --record-source` exposes the same witness.

Every sub-reason is round-trip tested on both JSON paths, including real PC 0,
normalized PC, missing PC, synthetic identity, invalid tags and unchanged
resource/work classification. A real phased TCP prefix remains refused and
its Op47 at original114/working111 is checked through the CLI and manifest.
