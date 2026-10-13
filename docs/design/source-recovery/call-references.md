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

### 5.3 phased 路径的两处语义修正（2026-10-13）

用 es2abc 构建的最小复现（`--opt-level=0`，见下文）把 phased 路径上的失败拆成了三层。其中两层
不是选择组合问题，而是通用语义分类缺口：

1. **封闭纯值被类型列表挡在融合之外。** `ldglobal`（`ValueGlobalObject`）读取当前 realm 的全局
   对象：无输入、无效果、不会抛出，整个活动期内身份不变。但 `PhaseCorridors::Expression` 的类型
   列表里没有它，所以 phased 路径上 `globalThis` 永远是独立的 root。只要它夹在 callee 读取与参数链
   之间，调用的效果区间就不衔接（`child.end != expected`），callee 只能保留快照，最终报
   `CallPlanError::kReference`。连不含选择的 `globalThis.obj.f(globalThis.x.g.h)` 也会因此失败。
   修正：`CanFuse` 先用共享求值形状计算“纯”（eager 表达式、无效果、不抛出、语言域结果），再把
   其中无任何输入者视为封闭值，让它越过类型列表，进入原有的“无绑定输入纯表达式”快速通道。
   这是按语义判定，不是 opcode 列表；原有表达式的判定条件不变。
   **撤回的第一版**（84cbdc24）是在 `DescribeEvaluation` 中把 `ValueGlobalObject` 标为
   `SourceEvaluation::kPure`。Codex 第十九轮指出（R1，已复现）：`literal_plan` 对 `kPure` 额外要求
   `CanErasePureOp`，而分析层的可擦除谓词仍排除它，导致 `{g: globalThis}` 从可恢复退化为
   `EvaluationError::kUnsupportedOperation`。扩大 `CanErasePureOp` 会牵动 IR 化简的死代码删除，
   超出本修复的范围。因此恢复原分类，只修改 `CanFuse` 的门禁。回归 fixture 为 `realmlit`。
2. **phi 输入消费者未被当作执行根。** `r = o.f(c ? a : b)` 中，调用结果是 phi 的唯一输入，放置为
   `EvaluationKind::kPhiInput`。按 `evaluation_plan.h` 的契约，它在原始边上拥有唯一可执行的
   incoming-copy 右值，其可用性、效果与异常 staging 已经独立证明；`IndexPhaseInlineRoots` 也把它
   当作自身的根。`OperandSelections::Check` 原先只接受 root 或 inline 消费者，现在同样接受
   kPhiInput。其余证明（callee 前缀、catch 一致性、作用域、Inert 区间）不变，验证器重跑同一个
   `CheckAll`。
3. **剩余：Binary 串接中的选择**（`o.f('a' + (c ? x : y) + 'b')`，fixture `phconcat`）仍需 §5.2 的
   调用级二次组合，保持拒绝，原因链不变。

测试：`call_references_cli_test` 新增 `phplain`、`phtrue`、`phfalse`、`phthrow`、`phrewrite`、
`phnoncall` 六个完整类（在 harness 下与原始源码的 getter、调用、接收者日志逐条相等，并做 detach、
reread 变异检测），以及 `phconcat` 拒绝类。

**旁路发现：catch `code_size` 为 0。** 这个 es2abc 默认的字节码优化器（opt-level 2）输出的 catch
条目 `code_size=0`。固定版本运行时 `Method::FindCatchBlock`（ets_runtime/ecmascript/method.cpp:98）
只使用 try 范围和 `handler_pc`，所以这是合法输入。但 `BuildExceptionRegions` 要求处理器长度大于
0，报 `CatchRegionError::kHandlerShape`，导致整个方法以 `PhasedMethodError::kRegion` 失败。
需要改为由 CFG 推导处理器范围，尚未实施。抽查的两个 24.0.0.0 语料文件（共 60 个处理器）长度都
不为 0，影响面待评估。

### 5.4 跨已提交选择的组合（第一增量，2026-10-13）

按 Codex 第二十一轮共识实施，实现位于 `src/source/phase_composition.{h,cc}`。

**模型。** 已提交的顶层选择（`plan.logical_values` 指向的 `kLogicalValue` 分支，非
`PhaseSelectionArm`）在外层树中是**一个原子事件**。它的前缀、谓词、各 arm 与嵌套子选择，
仍由原有的选择证明在原始视图中验证。外层不使用整数坐标桥接，而是用一个共享的有序事件证明
`Composition::Prove(root)`：

1. **展开预期序列。** 对执行根 C 的最终所有权树做后序展开，操作数按源语言求值顺序处理。
   接收者（`IsMemberReceiver`）、需要快照的操作数和外部 root 不属于该树。已提交选择记为
   `Sel(branch)`；其余有序操作（`PhaseCorridors::Event`）记为自身。字面量区间、受保护模块对、
   phased 对象操作数以及已有 `phase_references` 的树，不在第一增量范围内。
2. **沿实际正常路径反向匹配。** 从 C 开始，每个有序事件必须恰好是预期序列中的下一项。
   遇到预期中选择的 join 块时：该 join 的正常入边必须恰好是此选择的 `keep_input` 与
   `evaluate_input`；然后跳到 header，并吞掉恰好 `owned_count` 个由该选择拥有的事件（前缀
   root 及谓词链）。其他每一步都要求目标块只有一个正常入口、入边上没有 phi 拷贝、源块只有
   一个正常后继。纯的非事件操作不参与匹配。
3. **catch 普查。** 树中所有被移动的可抛出事件，以及每个选择区域内（header 到 join 之间）
   的全部可抛出操作，都必须满足 `SameCatch(op, C)` 且没有 catch 拷贝。第一增量不新增表达式
   内部的 staging。

**第二版（Codex 第二十二轮 R1–R3 之后）：自底向上逐边证明。** 第一版“对每个候选根整体试探、
失败整棵撤销”有三个问题：伪造计划可绕过单一用途（R1）；失败的链会被重复展开，形成二次
复杂度（R2）；新增缓冲没有计入预算（R3）。第二版改为按操作递增顺序，对每个节点调用一次
`Compose`：

- **顺序。** 游标从消费者 C 向前扫描（只走单入口、无拷贝、源块单后继的正常步）。操作数按源语言
  逆序逐个消费：
  - 已有子节点：下一个停点必须正好是该子树的最后事件，然后游标跳到它记录的跨度起点；
  - 已提交选择：停点必须是该选择 join 的入口，且 join 的正常入边恰为它的两条 arm；然后从
    header 吞掉恰好 `owned_count` 个由它拥有的事件；
  - 可吸收的 root（仅 builder）：能接上就内联，接不上就保留快照。

  每段间隙只扫描一次，因此总代价线性。
- **catch，逐边局部判定。** 子树或选择区域内若有可抛出点，只能被同样可能抛出、handler 相同的
  消费者吸收，且被移动的抛出点都不能有 catch 拷贝。不变量为：已组合节点内吸收的抛出点都与该
  节点同 handler、无拷贝；不可抛出的节点不吸收任何可抛出子树。因此只需检查子节点本身，传递性
  即覆盖整棵子树。外层某个消费者不合适，只会让它不再吸收，不会推翻内层已经成立的组合。
- **语义复核（R1）。** 每条成员边，无论是已有边还是新边，都要重验 `Fusable`：规范单一用途、
  操作数角色与快照规则，以及 `CanFuse` 中与坐标无关的部分 `PhaseCorridors::FuseSemantics`
  （含 `ValueCopy` 的保存期规则）。transport 边由选择证明负责。
- **根的决定。** 没有被任何节点吸收的节点为根，不论它自身是否组合成功。根有效、含新边且含选择，
  就登记为组合。否则沿已有边遍历整棵树，撤回遇到的每条新边，被切断的子节点重新成为根
  （OpId 更小，会在递减循环中再被判定）。最后检查闭包：每条保留的新边都必须可从某个已登记的
  根到达，否则撤回（Codex 第二十三轮 R1）。失败节点的清理与 `Withdraw` 一样，使用规范化的
  `Edge()`（经过 alias 解析）。
- **停点索引（Codex 第二十三轮 R2）。** 事件属性是静态的（组合只会把 root 改为 inline）。按块
  预先索引“当前位置及之前最近的事件”，并对块入口停点做记忆化（一串直通块共用同一个停点）。
  因此 `Next` 均摊 O(1)，连续的纯 Unary 或 alias 链不会被反复扫描。
- **预算（R3）。** 所有工作区在构造时按上界一次性计费、复用，不再扩容；选择刷新改为逐字段
  比较，不复制容器。
- **Verifier。** 对每个登记的根，沿实际边展开，并以强制模式按后序对每个成员运行同一个
  `Compose`：每个已有子节点和选择都必须接上，否则拒绝；根的树必须含选择；各树成员互不重叠。
  仍会在最终计划上重算选择记录并逐项比较。

**测试。**
- `phase_composition_test`：手工组装的 IR，三个 profile。
  - 第二版补充：被调用值在调用后另有观察时，builder 拒绝组合，伪造的内联加组合登记被
    verifier 拒绝（R1）；由外部 getter 隔开的 Binary 链在 200/201/400/800 规模下工作量线性
    （R2）；连续纯 `typeof` 链同样线性（第二十三轮 R2）；以二分法求出最小字节预算，少一字节时必须得到 `kResourceLimit`（R3）。
  - 基本情形会组合并通过验证，且会核对工作量预算的精确上限。
  - 选择之前插入的外部事件、调用移出 try：拒绝组合。
  - join 之后插入外部事件、span 内有 catch 拷贝：选择本身就不会被提交。
  - 伪造计划一律被拒绝：删除、重复或插入非根的组合登记，陈旧的 `execution`，把快照声称为组合。
- `call_references_cli_test`：新增 8 个完整类，日志逐条相等，并做变异检测：`phconcat`、
  `phtwo`、`phsiblings`、`phprim`、`phprimthrow`、`phrewritearm`、`phassign`、`phthrowarm`。
  新增 3 个拒绝类：`phnested`（选择嵌套在另一选择的 arm 中）、`phcomma`（逗号副作用是 span
  内的外部事件）、`phorphan`（一个调用在纯 `typeof` 包装处失败，而同一方法内另一个调用组合
  成功；若遗留孤立的新边，会报 `EvaluationError::kOrder`，已用旧逻辑验证过这一点）。

**后续。** 按共识拆分选择验证器；支持嵌套选择、literal、模块与对象操作数；补充等价变体，
以及深度与数量为 200、201 的规模检查。

## 6. Round 37：赋值表达式作为调用实参（2026-10-13）

基线 `c7e4fc1f`。原始 Value IR 的 `ValueNamedWrite` / `ValuePropertyWrite`
没有结果，不能直接把其后调用的 RHS 用法当作写入结果，也不能跳过该写入。
本增量新增独立的 assignment occurrence 证据，不改原始操作、SSA 定义或抛出效果。

### 6.1 定位证据

`CallPlanFailure` 包装 `CallPlanError`，将 `kReference` 细分为
`CallReferenceError::kNotPropertyResult` 和 `kNotInline`。证据保留 call/property
OpId、callee 的最终 placement、temporary reason 和 consumer，以及失败方法与原始/工作 PC。
它沿 Expression/FunctionTree/Family 封装进入回执；record 入口失败同样保留证据。
manifest 的 route refusal 使用 `source_failure.call_reference`，record-program 失败
使用 `source_call_reference` / `source_method` / `source_working_pc`，原 `source_pc`
为叶子原始 PC。资源类错误的分类不变。

第 1 步的单文件导出（仅 o13/o36）确认全部是 `kNotInline`，不是非属性 callee：

| record | 方法 | call / property OpId | 原始 / 工作 PC | placement / temporary |
|---|---:|---:|---:|---|
| o13 MultitonAbility 5312 | 5545 | 43 / 33 | 113 / 113 | Root / EffectSnapshot |
| o13 SingletonAbility 15218 | 15453 | 30 / 20 | 75 / 75 | Root / EffectSnapshot |
| o13 SpecifiedAbility 16255 | 16490 | 44 / 34 | 118 / 118 | Root / EffectSnapshot |
| o13 SpecifiedFirstAbility 17005 | 17245 | 44 / 34 | 118 / 118 | Root / EffectSnapshot |
| o13 SpecifiedSecondAbility 17762 | 18003 | 44 / 34 | 118 / 118 | Root / EffectSnapshot |
| o36 ModeComponent 36985 | 37638 | 137 / 123 | 378 / 352 | Root / CrossRegionValue |

五个 Ability 都是 definefunc 的同一 SSA 值先被 `[[Set]]` 写入、随后作为参数使用；
ModeComponent 是属性读取、递增、写入后的同一值作为参数。该结论来自原始指令及
类型化导出证据，不以应用名或 PC 作为生产准入。

### 6.2 证明、发射与边界

- `EvaluationShape::assignment_result` 只说明成功写入后可返回 stored-value；
  原始 store 的 `result` 仍为空、`expression` 仍为 false。
- `assignment_values` 以 alias-resolved SSA identity 建一次 use census，要求该值
  有且只有一次 stored-value 观察和一次调用 argument 观察。常量已折叠为 immediate operand、没有可索引的 SSA ValueId 的情况暂不提议该投影。额外观察暂需保留快照，
  不复制表达式。证据将这个 argument occurrence 指向原始 store，而不是改写 SSA。
- normal 和 phased 共享该依赖边：逆序效果后缀消费及正序独立验证同时纳入
  target/key、RHS、`[[Set]]`、参数结果。成员 receiver 规则、未知 computed key 的
  RHS 快照、环境屏障、异常副本与最终执行根的 SameCatch 均保留。
- 发射复用现有 AssignmentExpr；tree_operands 要求参数精确引用原 store 的表达式，
  再分别核验 store 的 target/key/RHS。不增加打印期语义恢复，也不伪造 acc 提交。
- 可选投影不成立时至多退回原计划一次。两个尝试预留互不重叠的工作额度，失败尝试
  的整个额度计费，存储只在失败计划释放后复用。成功尝试按双倍实际工作计入预留；
  这是保守资源策略，不是对失败工作量的测量。所有新索引预先计费；扫描/索引均线性，
  总复杂度仍受原有 CFG/支配分析约束。
- 已支持跨 fallthrough phase 的写入/调用及 copy 变体；现有 PhaseCorridors 不连接
  taken jump，新增测试保留这一拒绝边界。本轮没有扩展此既有走廊限制。
- computed key 若可能运行用户代码，现有 reference-before-RHS 检查仍要求 RHS 快照；
  不以“都是赋值”放宽转换顺序。未知 catch staging 同样继续拒绝。

### 6.3 结果与测试边界

o13 五个 record 都已 complete，均通过 Node 语法检查。ModeComponent 的赋值结果
投影已成功，但 Op127 的目标读取被属性读取和写入共同使用，仍需快照；它作为独立事件
留在 callee 与参数之间。callee 区间 end=41，调用消费参数后 expected=42，因此仍以
上述 `kNotInline` 拒绝。这需要后续共享引用/快照组合证明，不能在此跳过事件。

`assignment_values_test` 覆盖生成、删除/替换证据、伪造 placement、替换 RHS identity、
外部事件屏障、多实参写入顺序反例、phased catch 快照、copy、taken-jump 边界，以及 200/201/400/800 工作量
和精确 work/byte 预算边界。`assignment_arguments_cli_test` 使用四个 es2abc fixture，
逐次确认 `STOBJBYNAME`/`STOBJBYVALUE`，然后在两种 flags 下比较 Node 源码 oracle：
setter、RHS、只读属性、callee 与 key 抛出，receiver、多个赋值实参、callee 改写与
计算 key 的顺序；另保留行内 computed key + literal RHS 的类型化拒绝测试。替换 RHS 和解绑 receiver 的变异都必须被检出。
编译命令、编译器和 SHA256 见 fixtures/assignment_arguments/README.md。

本轮验证：keep-going 全目标构建成功；固定集 `ctest --preset source-regression -j 1`
65/65；call/member/assignment/property 加求值/phase/回执定向组 42/42。
未运行探索集、留出集、全套测试或 RSS 基准。

## 7. Checked capture arguments

Round 44 adds an obligation-driven entry to the common phased composition proof.
It only repairs unmet member references using a verified read + named HoleGuard
atom; already-valid reference plans keep their placements. See
[guarded-captures.md §11](guarded-captures.md#11-checked-capture-arguments-round-44)
for event identity, catch-state limits, independent verification and tests.
