# 统一控制区域审计

2026-09-28 后续决策：本文保留基线审计事实，不将其发布/全面验证建议作为全部
源码输出的前置门槛。当前采用 [FunctionTree 与部分输出设计](function-tree-output.md)：
功能优先、共同 AST/printer、函数与区域缺口标记，新增 budget/证明包装/发布审计暂缓。
下文“拒绝”表示不能完整恢复相应结构；已有可靠片段仍可按新策略输出。

日期：2026-09-28。审计基线：`1c7e44d2a23e14b91fb91ab98244f3dec355f6ba`，
Rules 58，源码执行契约 V3。本文是按用户要求进行的一次性审计，不恢复定期审视。
行号均针对该基线；后续实现以符号和测试为准。

## 结论

方向与最初设计兼容，但**不能把剩余工作描述成只缺一个区域树**。
值 IR、基本块表示、部分控制算法、源码树和 printer 已共享；控制投影、区域组合、
树构建调度、词法生命周期和控制验证仍受普通/异常两种模式约束。
completion 和 suspension 尚未进入共同源码通路。

不推倒重来，也不在异常路径复制循环实现。下一步是让现有表示和算法消费一个
可验证的区域组合契约，并同步迁移其消费者。
实施顺序见 [演进计划](../../superpowers/plans/2026-09-28-unified-control-regions.md)。

## 审计发现

以下按架构风险排序。P1 表示必须在扩大接受域前解决，不表示已经发现发布源码错误；
目前若干拒绝检查正是在保护语义，不能为了覆盖率直接移除。

### F1 / P1：异常投影不具备循环与多出口组合能力

- `src/source/exception_control.cc:55` 的构建器只调用无环 `BuildControlSkeleton`。
- `src/source/control_skeleton_verify.cc:12` 要求无 loop；
  `src/source/exception_control.h:49` 明确约定投影循环和一般 completion 拒绝。
- `src/source/exception_topology.cc:220` 要求每个 try 只有一个正常出口目标。
- `src/source/control_plan.cc:192`、`src/source/control_verify.cc:208` 的普通入口
  拒绝非空 phase 图；普通循环路径不能直接接收完整异常图。
- `tests/unit/source/exception_plan_test.cc:426` 明确把投影回边作为 `kCycle` 拒绝用例。

影响：不能由“普通循环成功 + try/catch 成功”推出任意嵌套成功。
正常边视图中 catch 入口可能不可达；catch 中的 continue 又可能参与整个循环。
把完整异常图交给普通自然循环发现，或删除异常边后直接运行它，都不是正确整合。

### F2 / P1：树构建及生命周期仍依赖模式和投影编号约定

- `src/source/function_tree.cc:149` 按 `exceptions_` 优先派发
  `EmitExceptionUnit`，否则才可能走 `EmitLoop`；不是统一的区域节点派发。
- 同文件 `EmitTry`、`EmitLoop`、入口 origin 寻址和容量估算也依赖两套布局。
- `src/source/cell_lifetime.cc:69` 同时识别 `LoopControl` 与异常 unit，
  第 83 行用 `node.block.value` 索引异常投影 units。
- `src/source/tree_flow.cc:209` 有独立 Try 匹配逻辑，入口仍分别验证两种 control。

影响：仅换控制构建器可能造成 scope、捕获、edge copy、PC 对应和计费错误。
循环每次迭代创建的 cell 与 catch 每次进入创建的绑定不能按静态 PC 合并。
生命周期、树控制验证和 origin 追踪必须与区域入口一起迁移。

### F3 / P1：AST 统一不等于 finally、外层跳转和协程已贯通

- `include/abcd/source/source_tree.h:150` 的 `TryStmt` 可存 finalizer，
  但 `src/source/tree_flow.cc:216` 明确要求 `!syntax->finalizer`。
- `source_tree.h:158` 的跳转目标是 AST owner；当前仅允许最近的 loop。
  `src/source/tree_structure.cc:340` 强制该限制，printer 输出无标签跳转。
- `include/abcd/analysis/value_ir.h:208` 的操作集合和
  `source_tree.h:134` 的表达式集合尚无共同 await/yield/suspend/resume 表示。
- 旧 `SemanticProgram` 中有 `AwaitValue`，`base_function.cc` 有
  `AwaitSites` / `ProveAsyncCompletion` 路径；这不是共同 ValueProgram 的协程证明。

影响：不能通过启用已存在的 finally 打印或移植一个旧 writer 完成 T09/T10。
待完成的 return/throw/break/continue，与暂停后恢复的执行状态必须分别建模。
yield 暂停本身不意味着离开 try，也不应触发 finally。

### F4 / P2：投影验证的独立性不完整

`src/source/exception_control.cc:78` 的 `VerifyExceptionControl` 再次运行
`ProjectExceptionControl`，比较 units/arcs/regions，并独立校验 skeleton。
其中 skeleton 校验是真实防线；但投影归属、遗漏和出口错误可能在构建与重建中一致。

影响：现有验证不能单独作为未来复杂区域组合的充分证明。
需要从原始边、操作级异常出口和 handler dispatch 出发，独立检查覆盖与转移，
并用“可构建但证据遭篡改”的负例检验验证器。重建比较可保留作差分，不是唯一依据。

### F5 / P2：原始块 ID 与投影节点 ID 的域未由类型隔离

`exception_control.h:35` 已解释 skeleton 编号指向 units，不是 ValueProgram 块。
但 `ControlNode::block` 和投影 topology 仍借用 `WorkBlockId` / `WorkEdge`；
`exception_topology.cc` 的 wrapper 编号还使用块数偏移。

影响：当前有边界检查，不等于已有错误；但组合后容易把 projection node 当原始块
送进 scope、phi 或映射分析。应新增投影 ID 域及显式映射，不新增另一套语义基本块。

### F6 / P2：测试能够确认现有边界，尚不能证明组合闭合

普通 loop、嵌套 try、真实异常函数族和模块发布都有验证基础。
`tests/integration/exception_tree.mjs` 验证 old-value catch、分支、嵌套 handler，
并不验证循环与异常任意组合。Rules 58 的模块函数族成功也不证明 finally/协程。
`src/source/family_file.cc` 仍拒绝带异常表的子函数，限制跨异常闭包组合。

必须增加组合矩阵、真实解码切片、独立行为 oracle 和映射检查；不能仅扩充各层单测。
不能从 Pasteboard 或某个函数族外推全样本覆盖。

## 应保留的基础

| 基础 | 已成立的内容 | 不可越过的边界 |
|---|---|---|
| ValueProgram | 普通和 phased 输入共用 WorkBlock、ValueOp | 同一结构不意味着所有算法可用同一图视图 |
| 异常值可用性 | ValuePhaseSite、Commit 结果、CatchDefinition、before_throw | catch 不能看到失败操作的正常结果 |
| 操作级异常出口 | ValueExceptionalExit 保留 operation、origin、handler、效果 | 不能压成不带来源的块末异常边 |
| 异常分析 | 原始 dispatch、ExceptionSsa、环境/模块/cell 证明 | 区域重排不准重写原 handler 优先级 |
| 控制算法 | 正常支配、自然循环、DAG skeleton、edge copies | 抽取图适配边界，不复制算法或默认过滤边 |
| 共享源码树 | BlockStmt/IfStmt/LoopStmt/TryStmt + 显式 ScopeId | 语法节点存在不是来源证明 |
| printer/来源 | 统一事件输出、origin DAG、原始 PC 核验 | 不从文本反推 PC，不丢失被折叠操作 |
| 发布 | 文件族 owner、module/enclosure、receipt 验证 | 普通/异常混合扩大后仍须逐成员重验 |

普通 SSA 与异常 SSA 可以继续使用不同的分析入口。统一要求是相同的值可用性、
边携带状态和后续消费契约，不要求把所有分析强行写成一个函数。

## 当前与目标

```text
当前
原始解码/身份/来源
  +-- 普通分析 --------------------+
  +-- ExceptionFlow/SSA/环境 ------+--> ValueProgram
                                         |
                    +--------------------+-------------------+
                    |                                        |
             ControlPlan + Loop                      ExceptionControl
                    |                                DAG/single-exit
                    +---------------+------------------------+
                                    |
                      模式分派的 TreeBuilder/TreeFlow
                      两种布局的 CellLifetimes
                                    |
                         FunctionTree -> printer -> receipt

目标
解码 + 原始 PC + profiles + file/owner/版本身份
                   |
    ValueProgram + before/commit/catch + effects/env/capture
                   |
    显式图视图 + handler 事实 + loop 事实 + 原始边出口证据
                   |
         RegionPlan（同一节点/出口/归属契约）
         Sequence / Leaf / If / Loop / Try
         + typed transfers + finally completion proof
         + suspension/resumption proof（后续阶段）
                   |
         独立区域验证 + copies/effects/lifetime 验证
                   |
         FunctionTree + ScopeId + typed targets + origins
                   |
          统一 printer + PC mapping + publication
```

这里的“同一模型”不等于只保留一棵树：异常处理关系、词法作用域树、结构区域树
和循环森林是不同关系；通过强类型身份和校验证据关联，不互相替代。

## 本次验证与限制

以下在审计基线重新构建并执行，退出码均为 0：

```sh
cmake --build build --target control_plan_test loop_forest_test exception_plan_test -j 2
ctest --test-dir build -R '^(control_plan_test|loop_forest_test|exception_plan_test)$' --output-on-failure
```

3/3 通过。其中包含现有预期拒绝，不是新功能验收。本轮不修改生产代码，未重跑
真实函数族、全部单测、全语料、RSS 或基准；不新增覆盖率或 1GB 达标结论。

## 外部语义参考

- [LLVM 异常处理](https://llvm.org/docs/ExceptionHandling.html)：正常/异常转移及
  handler 归属均显式建模，支持“基本块共享但异常不只是边标签”的边界判断。
  不建议把 LLVM C++ 异常 ABI 直接套用到 Ark/JS。
- [ECMAScript try 语义](https://tc39.es/ecma262/multipage/ecmascript-language-statements-and-declarations.html#sec-try-statement-runtime-semantics-evaluation)：
  finally 正常完成保留待完成结果，非正常完成替换它。
- [GeneratorResumeAbrupt](https://tc39.es/ecma262/multipage/control-abstraction-objects.html#sec-generatorresumeabrupt)：
  恢复方式与暂停状态共同决定后续执行，不能把 yield 当作 return。

以上是语义交叉核验，不把在线规范草案当作新的 Ark profile。实施必须继续依据
已锁定的 OpenHarmony-v7.0-Release 和各历史 profile 的具体行为。
