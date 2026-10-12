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

## 5. phased 路径的诊断与第二轮共识（2026-10-13）

探索集剩余 17 个 `CallPlanError::kReference` 都出现在带异常处理器的方法中，走 phased 路径。
临时插桩已回退。逐项定位如下：
- phased 普通参数的融合本身已经存在：`BuildPhaseExpressions` 按逆语法顺序组合效果区间。callee
  读取和函数创建参数的 `CanFuse`、`SameCatch` 在这些样本中都成立。
- 失败的直接判据是：在 `Schedule` 中，callee 的效果区间与调用期望的后缀不衔接
  （`child.end != expected`）。
- 统计不衔接的情形：在 o12、o24 中，callee 读取与调用之间都夹着 branch/jump，即参数里含有选择
  区域（共 22 处）；o36 中有 2 处没有控制流的情形，待单独定位。

所以，主要剩余形态是 **phased 路径上带选择参数的成员调用**。现有 `PhaseSelectionCall` 的前提
（已有 inline argument 链、explicit receiver 等）没有覆盖它。

Codex 的共识方案（第二轮）：
1. 先补齐失败定位，不改变准入。区分以下几类，并保留类型化原因：引用多用、区间不闭合、
   Commit 不可用、快照差异、作用域差异、表示限制。
2. 把 `PhaseCallReference` 抽成与选择无关的独立成员引用证据和 verifier，先让现有的选择调用
   改用它，并证明准入没有扩大。
3. 接入普通参数与选择参数的跨 phase 组合。
4. 处理各抛出点需要不同快照的情形，这涉及发射和 AST 的独立验证。
5. normal 与 phased 逐步共用同一份证据契约。

同时必须让 `ExpressionScopes()` 在普通跨 phase inline 时也启动。测试矩阵见共识原文：各位置
抛出、callee/receiver 改写、catch 快照、环境与控制反例、伪造证据，以及 es2abc 与底层 IR 两类
fixture。

### 5.1 定位结论（Codex 只读追踪与插桩复核）

在方法 6084 中核对了失败的调用（原始 PC 373→470，归一化后为 370→467），它的形态是：

```text
obj.f('…' + (c1 ? a : b) + … + (c2 ? x : y))
```

也就是“选择 → Binary 串接 → 成员调用的参数”。逐层核对的结果：
- 范围内的 7 个选择区域，`Operand()` 和 `Check()` 都接受，分组也完整，owner 是普通的 Binary
  消费者。
- 沿 inline 边向上爬，经过若干 Binary 后，到达的是**被判为 root 的顶层 Binary**，而不是调用的
  inline 参数。

原因是 `Schedule` 在选择融合之前就决定了参数和 callee 的放置。那时选择区域的分支使区间不连续，
所以参数表达式和 callee 都保留了快照；选择被提交为 inline 之后，没有任何一步重新考虑这些决定。
此外，`.then` 链上看到的 `role != kArgument` 只说明后续调用没有拿到证据，不会撤销已有引用；
它不是这些失败的原因。

**所需修复**（共识第 2–3 步）：以调用为中心，在选择提交之后，把每个已提交的选择当作一个原子
事件，对整棵调用表达式（callee 加上各参数子树）重新做效果区间组合与 catch 状态证明，然后联合
提交 callee 和参数的 placement。成员引用证据需要独立登记，并由独立 verifier 接管
`phase_references`。这是 phased evaluation 的算法改动，不能靠放宽 `Ancestors` 的条件来实现：
已试过“沿 eager 表达式上爬”，在顶层 root 处就终止了，因此未提交。

### 5.2 调用级二次组合的设计共识（Codex，基于 `5438e829`）

在选择提交之后、`BuildIntegerNaNs` 之前，新增 `ComposePhaseExpressions`：以包含已提交选择的成员
调用为入口，复用 normal 路径中“选择原子化、逆序组合、正序验证”的方法，不重跑 `Schedule`（它的
retry 分区不能合并原本分开的 root）。

1. **受影响的森林**：从选择结果沿规范化 def-use 的 eager 消费边向上，可以穿过当前为 root 的
   Binary。逐条边检查：唯一消费（包括快照、phi 用法）、`DescribeEvaluation` 允许内联、Commit 可用性
   与存储作用域、base 与 receiver 相同。
2. **选择作为原子事件**：使用类型化节点 `OpId | SelectionId`。摘要包含：
   - 结果与消费 `UseId`；
   - 入口和正常完成出口；
   - prefix、arm、嵌套的所有权；
   - 内部异常出口；
   - 外部输入。

   join 只收缩到结果汇合的边界，不吞并 join 块里的后续操作。phased 的 `Producer()` 要能解析选择
   产生的 phi。
3. **收缩后的事件顺序**：扩展 `EffectOrder`。普通 op 和 Binary 都是事件，选择是一个事件；回边、
   额外入口和绕过路径要参与执行次数证明。
4. **后序加逆序消费**：区间带事件域身份 `{chain, begin, end}`，联合提交 callee 与参数的 placement；
   失败时整体撤销这棵组合树的变更。

**验证职责**：
- 选择验证器只验证内部路径，并拆分出“结构证明”与随最终所有权重算的 `execution`。
- `VerifyPhaseExpressions` 用带域的正序串接方程，把选择事件区间计入父节点，不再豁免
  `CrossLogicalInline`。
- 独立的引用验证器负责 `phase_references` 的身份和完整性。

**catch**：每个被移动的抛出点（getter、key、各 Binary、predicate、各 arm、调用）都要与最终求值上下文
兼容。第一个增量不新增表达式内部的 staging 能力。

**落地顺序**：
1. 拆分验证器（准入不变）；
2. 选择事件索引与带域区间验证；
3. 组合 builder；
4. 差分、变异和 IR 反例测试，然后看探索集。
