# 跨选择区域的成员调用引用

状态：设计共识（Claude 与 Codex gpt-6-astra/xhigh，2026-10-12）。第 1 步（原因链）已实施；
第 2 步的 normal 路径第一个增量（融合域沿内联消费链延伸）已实施，见 §4。phased 路径共用证据和
没有唯一消费者的嵌套选择尚未实施。

## 1. 问题

探索集帕累托第二位是 `FunctionTreeError::kExpression`（23 个 record）。诊断（临时插桩，已回退）
显示，全部 40 次拒绝都来自 `call_plan.cc` 的 `CallPlanError::kReference`：

- `MemberCall` 的 callee 属性读取被判为 `kRoot`、`TemporaryReason::kCrossRegionValue`。
- 原因是 callee 读取所在的块早于调用：调用自身某个参数里的可选链、`??` 或条件表达式把区域
  拆开了，调用落在汇合块。

典型源码为 `obj.f(G(x.g?.h))`。被单独保存的 callee 会丢失 receiver，所以 CallPlan 拒绝本身是
正确的。

原生 `obj.f(args)` 会先求值成员引用，取得函数值和 receiver，再从左到右求值参数。因此按原样
输出，与字节码的执行顺序一致。参数改写 `obj.f` 后，仍应调用旧函数；getter 抛出时参数不执行；
callee 不可调用的检查发生在参数求值之后（ECMAScript EvaluateCall）。

phased 路径已有 `PhaseSelectionOperand`/`PhaseCallReference` 的 ancestor 链
（phased-call-references.md），但它仍要求已有 inline argument 链和 explicit receiver。normal 路径的
logical fusion 以选择值的直接消费者作为融合域的终点，所以外层成员调用的 callee 落在融合域
之外。

## 2. 共识方案

1. **原因链（已实施）**：
   - `ExpressionFailure{ExpressionError, CallPlanError}` 和
     `FunctionTreeFailure{FunctionTreeError, ExpressionFailure}` 沿用 S4 的封装方式，与各自的 kind
     枚举比较相等。
   - `expression_forest` 在 CallPlan 失败处携带原因；`function_tree` 在 expression 失败处携带原因。
   - `FamilyTreeFailure` 和 `ClassTreeFailure` 的 cause variant 改为持有 `FunctionTreeFailure`。
   - resource/work 分类和 partial recovery 的范围都不变。
   - 现在的原因链为 `FunctionTreeError::kExpression → ExpressionError::kUnsupportedOperation →
     CallPlanError::kReference`。
2. **共同的成员引用与求值区间证明（EvaluationPlan 层，normal 与 phased 共用）**。需证明的义务：
   - **引用身份**：callee 就是原属性读取的结果，receiver 与该读取的 base 是同一规范化值；base、
     computed key 和属性读取各执行原定次数。
   - **所有权闭包**：从选择值出发，沿规范化 def-use 和 eager ownership 边建立完整的消费树，每个
     可观察的求值都有唯一的语法 owner。
   - **执行域与次数**：不能把无条件执行的 getter 移进条件调用，也不能改变循环中的执行次数。
   - **求值顺序**：每条可执行路径上都是“引用 → 参数 0 → 参数 1 … → 调用”，各子区间之间没有遗漏、
     重叠或无法解释的效果。
   - **异常与完成顺序**：保持不变。phased 路径还要保留 handler 和 catch scope。

   实现方式：
   - 复用 normal 路径中逆序 Build、正序 Verify 的效果区间组合；抽出共同的成员引用证据。
   - 所有选择参数联合证明之后，才提交 placement。
   - 输出仍用现有的 `MemberExpr` 和 `CallExpr{kMember}`；CallPlan 与 TreeOperands 的身份检查保留。
   - 不增加“参数不能改写 callee”这一条件。
3. **端到端验收**：
   - 用构造的 fixture 在 Node 下做差分，覆盖：
     - getter 与 callee 改写、computed key 的副作用；
     - 各步骤抛出同一个哨兵对象，以及不可调用的 callee；
     - 多个选择参数和嵌套选择。
   - 用输出变异确认测试能检出错误：把 getter 推迟到参数之后、重复读取、交换参数、丢失 receiver。
   - 必须拒绝的情形：
     - 重复读取 getter；
     - receiver 不同；
     - 存在无法解释的效果；
     - getter 无条件执行，而调用是条件执行；
     - 回边改变执行次数。
   - 伪造的证据必须被独立 verifier 拒绝。
   - 预算按 200/201 和几何规模测试。

   之后用探索集复评。留出集按冻结制度另行评估。

## 3. 第 1 步的证据

- `call_references_cli_test` 使用随测试提交的 es2abc fixture `optarg`（`o.f(G(x.g?.h))`），断言当前
  拒绝带有完整的三级原因链。第 2 步实施后，它将改为差分验收。
- 准入结果不变，见 status.md 的验证记录。

## 4. normal 路径第一个增量：融合域沿内联消费链延伸

`logical_evaluation.cc` 的融合域原以选择值的直接消费者作为终点（`last_owner_`）。若这个消费者
本身是汇合块中更大表达式的 inline 操作数，例如 `o.f(G(x?.y))` 中的 `G(...)`，那么外层调用
较早读取的 callee 就落在融合域之外，只能被单独保存，CallPlan 也就只能拒绝。

现在由 `InlineAncestors` 沿实际的 inline consumer 边，在同一块内向后走到最外层表达式，并把它作为
最终消费者，链上每个 op 都标记为 owner。链在 root 或其他 placement 处终止，对象字面量路线不
延伸。汇合块已消费前缀的终点（`prefix_end`）也同步使用这个最终消费者，后续的融合域因此不会
重叠。

是否真的能够融合，仍然由已有机制判定：
- 逆序 Build、正序 Verify 的效果区间组合；
- `Candidate` 条件：receiver 角色和需要快照的形态都不放行；
- 独立的 Verify。

callee 读取若与选择之间夹着未归属的效果，区间就不连续，于是仍然保留快照，CallPlan 拒绝。
输出继续使用原生 `MemberExpr`/`CallExpr{kMember}`；CallPlan 和 TreeOperands 的身份检查不变。

**证据**：
- `call_references_cli_test` 使用 6 个 es2abc fixture，在带日志的 harness 下与原始源码差分执行，
  执行轨迹完全一致：
  - `single`：单个参数；
  - `twoargs`：选择参数之前还有一个 eager 参数；
  - `computed`：computed key 带副作用；
  - `rewrite`：参数替换了 `obj.f`，仍调用旧函数；
  - `throwing`：参数 getter 抛出哨兵对象，调用不发生；
  - `noncallable`：callee 不可调用，参数求值之后才抛出 `TypeError`。
- 输出变异必须被检出：
  - 解绑 receiver，即 `(0, obj.f)(…)`；在 `throwing` 中调用不发生、receiver 无从观察，因此该项
    除外；
  - 重复读取 getter。
- `optarg` 中嵌套的 `??` 选择没有唯一消费者，仍以完整的三级原因链拒绝。
- 固定 65 项全部通过。定向 221 项中失败的仅为 14 个基线原有失败。
- 探索集 448 → **455/620**，`CallPlanError::kReference` 从 23 个 record 降到 16 个，无回退。新完整
  的 7 个 record 全部通过 `node --check`；原本已完整的 record，输出文本逐字节不变。

**仍待实施**：
- phased 路径消费抽出的共同引用证据；
- 没有唯一消费者的嵌套或多选择参数；
- 共识 §2 中其余的反例与预算测试：执行次数和回边的迁移、伪造证据、200/201 规模。
