# 先于初始化的受保护捕获（TDZ 捕获）

状态：设计共识（Claude 与 Codex gpt-6-astra/xhigh，2026-10-12）。第 1 步（原因链）已实施；
第 2–3 步已实施三个增量，见 §5 和 §7：同块延迟声明（Codex 第十一轮复审 APPROVE）、
受保护义务的转发、唯一写入在后续块。其余仍未实施。

## 1. 问题

帕累托头号缺口 `CaptureCellError::kInput` 在探索集中有 64 个 record、77 次拒绝（只在探索集上
诊断，留出集不参与调优）。临时插桩（已回退）确认，全部拒绝来自
`NormalScopeFacts::Rebase` 返回 `ScopeError::kInitialization`，异常路径
`ExceptionScopeFacts` 为 0 次。所有义务都是 `CaptureDomain::kLanguage`，`ClosureWrites` 都是
`kLanguage`，被拒 cell 都没有未暴露前缀（`unexposed_initializations`）或模块前缀证明。

拒绝都对应普通的 JS 写法：子函数在 cell 初始化之前被创建。

- **类内自名绑定**：`newlexenvwithname` → `ldhole` → `defineclasswithbuffer` → `stlexvar`。类方法
  在类创建时生成，而类内名字绑定在创建之后才写入。未暴露前缀在 `LoadHole` 和 `ClassCreation`
  处中断。
- **函数提升**：模块作用域先 `definefunc` 并写入函数槽，之后经过调用、模块读取等操作，才写入
  `let`/`const` 槽。提升的函数捕获这些后写入的槽。另有一个变体：函数创建后紧跟
  `stmodulevar` 导出。

两种情况下，子函数对这些 cell 的读取都是 `ldlexvar` 后紧跟
`throw.undefinedifholewithname`。锁定运行时把新环境的所有槽初始化为 Hole
（`ets_runtime/ecmascript/object_factory.cpp:2715`）。因此原程序本身就带有 TDZ 语义：初始化
之前调用子函数会抛出 ReferenceError。

## 2. 为什么不能直接放宽

当前子函数一律产生 `kLanguage` 义务（`function_input.cc` 中构造 uses 的位置）。
`semantic_validation.cc` 把所有捕获的 `LexicalLoad` 同时放进 `language_loads` 和
`captured_reads`。`cell_evaluation.cc` 把 `IncomingInitialization` 当作已初始化的证据，
`evaluation_plan.cc` 据此把对应 guard 归为 `kCellGuard`，`function_tree.cc` 最终不发射这个运行时
检查。也就是说，**guard 能被消除，前提是父侧已证明初始化**。

父侧声明目前在环境分配处输出无初始化器的 `let x;`（`function_tree.cc` 的 `EmitCells`，并由
`tree_flow.cc` 独立验证）。`binding_initializer.cc` 会把闭包创建视为最早的观察点。如果只跳过
`Rebase` 的初始化检查，生成的源码在初始化前调用时会读到 `undefined`，而不是抛出 TDZ 错误，
语义就变了。

还有一点：锁定解释器的 `STLEXVAR` 直接写槽，不检查 TDZ；而 JS 对未初始化的 lexical 绑定赋值
会抛出。所以子函数向外层 cell 写入时，不能把“该槽的第一次 `stlexvar`”自动当作初始化。

## 3. 共识方案

1. **原因链（已实施）**。`CaptureFailure{CaptureError, ScopeError}` 和
   `CaptureCellFailure{CaptureCellError, CaptureFailure}` 保留了此前被丢弃的原因；两者都与各自的
   kind 枚举比较相等，所以只按 kind 分支的调用方不需要改动。现在原因链为
   `CaptureCellError::kInput → CaptureError::kScope → ScopeError::kInitialization`。原因只用于诊断，
   不改变准入。
2. **受保护访问与 cell 生命周期证明**。不新增“同块紧邻”这类字面判定，而是用 def-use、控制
   依赖、效果和异常顺序证明以下几点：
   - guard 检查的是该 load 的同一 SSA 值；
   - Hole 在 guard 之前没有被观察或泄漏；
   - 合并后的读取保留执行次数和异常时机（结果被丢弃时，TDZ 异常也要保留）。

   读取、初始化、更新、guard 以及创建与转发义务，都按稳定的 cell 和 occurrence 身份关联。同一
   cell 上的 guarded、unguarded 和写入义务全部保留。这一步不放宽生产出口。
3. **声明构建与独立验证**。生命周期计划区分三个事件：环境分配、未初始化、初始化提交。对新准入
   路径，`let x = rhs` 必须对应原始 store，且 RHS 抛出时 cell 仍保持未初始化。无法表示时返回带类型
   的原因，不能退回到提前声明 `let x;`。先只支持可证明的可变 `let`；`const` 和类内自名绑定需要
   各自的可写性证明。只有声明、guard 异常行为和全族写入证明都成立时，`Rebase` 才把 guarded
   义务转为待履行的生命周期义务，而不是直接视为已初始化。
4. **测试**。必须覆盖以下几类：
   - 初始化前后调用闭包；初始化为 `undefined`；RHS 重入或抛出；
   - guarded 写与 unguarded 写、复合赋值以及 RHS 与 guard 的顺序；
   - 快照语义、复制与寄存器重分配、块拆分、跨层转发、多创建点、混合访问；
   - 逐迭代环境、try/catch/finally、await/yield；
   - 类内与类外的名字、静态与实例初始化器、导出与循环模块；
   - Hole 重置、guard 绑错 cell、sendable 等反例。

   正向验收要求对引擎生成的源码做差分执行，比较值、身份、效果轨迹和异常时机；再用“提前声明、
   删除检查、错绑 cell、移动 RHS”等变异证明测试能检出错误。
5. 实现与测试冻结后，再按 cohort 评估。探索集只用于衡量这一通用能力解决了多少缺口；不能为了
   清零某个数字而添加模式。

## 4. 未决边界

- **错误消息**：运行时只在 Hole 路径读取 guard 名字，并把它拼进错误消息
  （`interpreter-inl.cpp:4107`、`runtime_stubs-inl.h:1727`）。原生 TDZ 不保证 `e.message` 与 Ark
  一致。Round 38 的裁决：es2abc 会把原生 `let` TDZ 访问编译为同样的 throwing hole-check，
  输出源码在 Ark 上遵循这一异常协议。Node 差分只比较异常构造器（`ReferenceError`）、控制流、
  返回值和日志轨迹，不比较 `e.message` 或 stack；不以 Node 的消息验证 Ark 的消息。现有模块读取
  设计也没有声称跨运行时消息等价。
- **sendable 环境**：保持现有的排除。

## 5. 已实施的第一个增量：同块延迟 TDZ 声明

适用范围刻意保持在可独立证明的最小一类，其余情形一律保持拒绝。

**子函数（`function_input.cc`）**：普通流程（方法无 try 区域）里，一个捕获 `LexicalLoad` 如果
满足下列条件，就记为 `CaptureDomain::kGuarded`，否则仍是 `kLanguage`：
- 在同一基本块内，它与对累加器的 `HoleGuard(Accumulator, HoleNameSlot)` 之间只有无副作用、
  不会异常完成、只把累加器复制到寄存器的 `ScalarAssignment`。

这些复制在 guard 完成之前不会被读取。guard 抛出时方法没有处理器，所以 Hole 不会离开
load/guard 这一对。子函数原有的 Language 前提和 guard 消除（`InitializedCellGuardInput`）不变：
除 guard 之外，加载值的每次使用都在 guard 成功完成之后。

**父函数（`NormalScopeFacts::Rebase`）**：cell 在创建点尚未初始化时，`kGuarded` 用法只有在下列
条件全部满足时才被接受，并通过 `deferred` 报告该 store：
- `ClosureWrites::kLanguage`，即子函数不会写回 Hole；
- 环境分配、创建点，以及该 cell 按指令序的第一个 store，三者在同一基本块，并按此顺序出现。

同一基本块内指令序就是执行序，其他块里的访问只会在离开该块之后发生，所以这个 store 就是
本次激活中第一个执行的写入。异常路径（`ExceptionScopeFacts`）对所有 domain 都保持严格初始化。
`kLanguage` 用法和子函数写入的处理不变。子函数写入的 VM store 不检查 TDZ，所以仍然拒绝。

**传递与发射**：
- `CaptureBinding.deferred` 记录 store。
- `CaptureCells.deferred` 把它解析为父函数的 `CellId` 和 live store `OpId`，由
  `VerifyCaptureTargets` 复核。
- 父树把它记入 `CellEvaluation.deferred`：record 根在 `record_tree.cc`，其余成员在
  `family_tree.cc`。`MarkDeferredCells` 检查：
  - cell 是已分配的 cell；
  - store 与分配在同一个 value 块，且在分配之后；
  - 两者之间没有该 cell 的其他访问。
- 外部的类根创建者无法承接这些 store，因此拒绝（`FamilyTreeError::kDeferredCell`）。
- `EmitCells` 不在分配处输出该 cell 的声明。`expression_forest` 把这个 store 降为声明的初始化值，
  `function_tree` 在 store 处输出 `let x = v`，并要求该 op 是 root 求值，且 draft 的作用域正是
  分配所在的块作用域。

**独立验证**：
- `tree_flow` 先用 `VerifyDeferredCells` 复核每一项（异常流程中出现即拒绝）。分配处不接受声明，
  store 处要求同块作用域、`let`、尚未声明的 `DeclarationStmt`，并由它绑定 cell 的符号和声明点。
- `tree_operands` 像模块初始化那样核对声明的初始化值。
- `lexical_stores` 接受这种声明形态，并对照原始流程的 cell 声明点。
- 源码位置对应检查与模块初始化一样，由声明语句持有 store 的 PC。

**证据**：
- `guarded_captures_cli_test` 使用随测试提交的 es2abc 编译 fixture，来源见
  `tests/integration/fixtures/guarded_captures/README.md`。
  - 五个完整场景在 Node 下与原始源码差分执行，结果一致，其中都包含初始化前调用抛出的
    ReferenceError。
  - 把延迟声明改回提前的 `let x;`，测试必然能检出差异。
  - 子函数提前写入，以及环境被 es2abc 提到函数入口而 store 在分支里，这两种情形以
    `ScopeError::kInitialization` 拒绝。
- `capture_plan_test` 覆盖以下情形：同块延迟、已初始化不延迟、伪造延迟、未知写入、后续块的
  store。
- 固定 65 项全部通过。定向 218 项中失败的仅为 14 个基线原有失败。
- 探索集完整 record 从 430/620 升至 434/620，无回退。原 64 个 `kInput` record 中：
  - 4 个完整恢复；
  - 47 个仍为 `ScopeError::kInitialization`；
  - 9 个停在 `FamilyTreeError::kDeferredCell`，即类根路径；
  - 4 个进入下一层的 `FamilyFileError`。

## 6. 第一个增量之后的剩余类别（探索集诊断，§7 已处理前两项）

1. **转发义务**：剩余拒绝中占比最大的一类。提升的函数本身不读该 cell，但它内部创建的箭头
   函数读取，且带 guard。`PlanFamilyCells` 把零访问的 incoming cell 一律转发为 `kLanguage`，
   `VerifyCaptureTargets` 也要求如此，于是 guarded 性质在上一层丢失了。需要按 cell 汇总子级
   义务：所有子级用法都是 `kGuarded` 时，才转发为 `kGuarded`。
2. **类根路径（9 个 record）**：类内自名绑定的方法属于以 record 根为外部创建者的类家族。延迟
   store 需要在根树构建之前传到 `record_tree`，而类树在根树之后才构建。
3. **store 在后续块**：需要把同块条件推广为“该 store 支配该 cell 的其他所有访问”，并由发射端
   的同作用域检查保证结构。
4. **作用域收窄**：es2abc 把块环境提到函数入口，而 store 在分支内。要把声明移入分支块，需要
   证明该 cell 的所有引用都在这个块内。

## 7. 第二、三个增量：转发与后续块唯一写入

**受保护义务的转发（`PlanFamilyCells`）**：
- `CellDemand.domain` 记录子级对这个 incoming cell 的最弱用法。子级自身的访问和它已经转发的
  义务一起汇总，只有全部为 `kGuarded` 时才是 `kGuarded`。
- 父级用 `FindDemandCell` 把需求映射到自己的 cell。只有当映射到某个零访问 incoming cell 的
  需求全部为 `kGuarded` 时，转发用法才是 `kGuarded`。签名入口需求一律按 Language 处理。
  phased 成员不做汇总，转发用法仍为 Language。
- `VerifyCaptureTargets` 接受 `kLanguage` 或 `kGuarded` 的转发用法。
- 同一 cell 上的混合用法逐条 Rebase，其中任何一条 Language 用法在未初始化时都会拒绝。
- 嵌套箭头写入时，转发义务保持 Language，因此拒绝（fixture `forwardwrite`）。

**唯一写入在后续块（`Rebase`、`MarkDeferredCells`）**：
- 环境分配与创建点同块时，若该 cell 的唯一一次 store 位于别的块，这次 store 就是本次激活中
  第一个执行的写入。从未执行到它的路径会让绑定一直处于 TDZ，这与原生 `let x = v` 从未执行
  的效果相同。
- value 层检查该 cell 恰有一次 store 访问。父函数自身的读取仍须由 `CellInitializations` 证明
  有支配它的 store。
- 结构上从“作用域相同”收紧为“同一个块语句”：
  - `function_tree` 记录分配所在的 draft，要求 store 发射到同一个 draft；
  - `tree_flow` 记录分配所在的 `BlockStmt`，要求声明语句直接位于其中。

  因此循环体或分支臂永远不会承载这个声明。

**其他**：采纳 Codex 第十一轮建议，`lexical_stores` 先按 flow 绑定的声明语句判定声明形态，
不再看初始化值的形状。

**证据**：
- 新增 fixture，均通过 Node 差分：`forward`、`forwardmixed`（嵌套箭头经提升函数转发）、
  `laterblock`（分支之后的唯一 store，分支内的提前调用同样抛出 TDZ）。`forwardwrite` 拒绝。
- 固定 65 项全部通过。定向 219 项中失败的仅为 14 个基线原有失败。
- 探索集完整 record：430（基线）→ 434 → 435 → **448/620**，无回退。新完整的 18 个 record
  全部通过 `node --check`。`ScopeError::kInitialization` 叶子从 64 个 record 降到 27 个。
- 剩余：类根路径、作用域收窄、未受保护的读取（例如提升的函数声明槽，这不是 TDZ 语义）。
  这些以及 Codex 关于动态激活测试和独立 allocation 证据的建议，留作后续。

## 8. Codex 第十二轮阻断项 R1 及修复

**问题**：中间函数自己访问某个 incoming cell 时，只原样复制它自身的用法，后代对同一 cell 的
Language 读取或写入没有进入它的 `storage.uses`。因此，中间函数自身带 guard 的读取会掩盖后代
更强的要求，向上汇总出 `kGuarded`，父函数就可能错误地延迟声明。98d806ec 也有同样的缺口。
反例：子函数无 guard 地写入外层 Hole 槽，在 VM 中会成功，而原生 `x = value` 会抛出 TDZ。

**修复（`PlanFamilyCells`）**：
- 对所有 incoming cell 汇总后代需求和 entry 需求，不再只汇总零访问的 cell。
- 中间函数自身的 `kGuarded` 用法，只要该 cell 上有任何 Language 需求，就加强为 `kLanguage`。
  向上的需求由加强后的用法计算。
- `capture_cells` 的 owner 检查只允许“与原始相同”或“原始 Guarded 加强为 Language”，绝不允许
  减弱。

**测试**：
- 新增 fixture `ownwrite`：中间函数带 guard 读取，嵌套箭头写入，现在被拒绝。修复前该 record
  会被完整恢复。在这个例子里 es2abc 恰好在写入前插入了 guard，所以修复前的 Node 行为碰巧一致，
  但对任意 ABC 并不健全。
- 新增 fixture `ownread`：中间函数和嵌套箭头都带 guard 读取，仍然完整恢复，并通过 Node 差分。
- 新增动态激活 fixture（Codex 第十一轮建议）：`recursion`（每层激活各有一个新 cell）、
  `initthrow`（初始化抛出后，已逃逸的闭包仍处于 TDZ）、`fnowner`（普通函数作为所有者）。
- 变异检测推广到缩进的声明：把声明提到所在块的块首。

**验证**：
- 固定 65 项全部通过。
- 定向 220 项中失败的仅为 14 个基线原有失败。
- 探索集保持 448/620：修复只收紧了反例情形，探索集中没有受影响的 record。

**复审**：Codex 第十三轮（`31db00d2`）结论为 **APPROVE**。待办的非阻断建议：
- 补手写的无 guard 字节码 fixture：外层 store 或读取不带 guard，直接覆盖 R1 的语义反例。
  es2abc 在这种情况下会插入 guard，无法生成该反例。
- 对汇总后的用法做独立校验，并补篡改测试：保留后代的 Language 需求，同时把汇总用法改回
  Guarded，必须被拒绝。
- 补单元测试：同一 cell 同时收到 guarded、Language 和 entry 三类需求，以及不同子级顺序下的
  汇总结果。


## 9. Round 38：phased guarded capture occurrence

`CaptureFailure.occurrence` 保留 use ordinal、cell、请求域以及最深的子函数方法/原始 PC/工作 PC。
家族需求加强和无本地访问的转发保留实际后代来源；父创建位置仍是独立的诊断字段。

原始证据由 `analysis/guarded_reads` 计算：SSA copy/phi 恒等式给出 load 与 guard 的同一值；
共享的“上一个有序事件”数据流证明二者之间没有其他可观察事件；guard 成功 Commit 支配所有
非恒等传输的使用。异常出口、Catch 入口和成功 Commit 逐一核对。这里的事实不声称 cell 在
创建时已初始化，也不单独授权源码发射。候选读取必须是原始 named HoleGuard 的操作数；
没有引入相邻 opcode、固定寄存器或同块识别。

phased 输入只将获得完整证据的严格 Language 读取 occurrence 改为 Guarded。原有无 handler
`kSourceAccess` 路径保留。家族仍使用 Guarded < SourceAccess < Language 的需求格，写入和
未经保护的 handler 读取保持严格；异常父函数和外部类根的延迟声明准入不变。

只有绑定到实际 deferred cell 的 occurrence 才启用新投影。load 与 guard 共用一个带两个
origin 的标识符读取，但在原 guard 处发射并保存成功结果；catch staging 保持原位置。
`kGuardedCellRead` 与非抛出的 `kCellGuard` 分开；第一增量不允许 phase corridor 或
phase_composition 吸收该读取。树和 recovery 的 `OriginalActionRegion::kCapture` 验证
动态 cell 身份、guard 异常对应以及成功值的 Commit，不借用模块绑定或初始化事实。

复杂度：共享 SSA 恒等式、支配树和有限格数据流，索引/扫描线性，受支配算法约束不超过
O(N log N)；不为每个 cell 重走 CFG。新增数组/工作队列在使用前计费；资源与工作失败保持
原分类。`GuardedReadError::kCatchState` / `EvaluationError::kCatchState` 区分不一致的异常证据。

测试由 `guarded_reads_test`、`phased_guarded_reads_test` 和
`phased_guarded_captures_cli_test` 覆盖：copy/块拆分、catch 原始值泄漏、删除 guard/成功值/
异常出口、伪造 placement/composition、recovery literal 替换、200/201/400/800 与预算边界；
真实 es2abc fixture 验证提前读取、正常读取、catch 本地状态、finally 轨迹、多层转发，handler
中的写入继续拒绝。fixture 中的前移声明、删除读取和错误 catch staging 变异必须改变 oracle。

**Round 38 工作区验证（2026-10-14，尚未提交）。** 最终 keep-going 构建成功；固定集
65/65；三个新增测试通过（含 read OpId 大于 guard OpId 的等价块重排）。定向 12 项为
11/12，唯一失败是 `language_source_corpus_test` 的旧拒绝预期。`--language-source`
探索集为 510/620，相比 c329fd2d 的 506 个已完成 record，没有回退或输出文本变化。
指定的 12 个 record 全部越过原首个 capture 初始化拒绝，3 个完成，9 个停在后续
FamilyWrite/Control/PhasedMethod 门禁；不能把越过首个拒绝说成整个 record 已验证完成。

**范围待 cc 确认。** 第四个新增完成的是原先排除的 o16 record 14751
`InputDeviceRoutingSwitching`。method 16458 的 slot 2/3 读取已有 named hole-check，
本轮仅对这些 occurrence 建立 Guarded 证据；无 guard 的 slot 6/7/8/9 仍为 Language，
通过原有初始化证明。因此该结果没有开放未经保护的 handler 访问，也没有实现 Round B。
单文件复验显示该 record 在原 flags 下也完成。为避免用样本禁用规则压回合法结果，
工作区保留通用算法，并暂不改 corpus 测试及 `language-source.md` 中此前的历史预期；
其预期更新与这项自然覆盖收益一并交 cc 确认。未运行原 flags 全探索集或留出集。

## 10. External class-root requirements (Round 43)

Class members can capture a creator cell whose declaration cannot execute until
its initializing store, including the class's separate self-binding cell. The
member requirement must reach the creator **before** its tree is frozen. Preparing
an input is not a fulfilment certificate and does not authorize guard erasure.

`FamilyPreparation::Read` / `FamilyTreeInput::Prepare` retain the residual family,
apply existing default projection and whole-family demand rebinding, and verify
`root_capture` against the unchanged creator. `PrepareClassTree` visits the same
constructor/member/defined-member order as final construction, reserving the same
owner ranges without allocating AST symbols or names. Each prepared body keeps
its own occurrence/context; equal `DeferredCellInitialization` storage entries
are not request identities. Retained partial-input failures are charged before
reading another member; resource/work failures are not deferred as omissions.

The record adapter combines ordinary child requests and prepared class-root
requests into one `MarkDeferredCells` input before `BuildFunctionTree`. The
original declaration/store/scope proof decides placement. It does not move an
already frozen declaration. Failure selection follows the request ordinal, so
two members sharing storage cannot overwrite each other's diagnostic provenance.

`FamilyTrees::Build` replaces the unconditional ExternalRoot refusal with
`VerifyDeferredCaptureTree`: identity/revisions, cell symbol, the actual `let`
with an initializer, scope, and the store-to-statement/initializer correspondence
must match the independently checked creator flow. The declaration's missing
activation evidence remains missing. Through `root_capture.targets`, the actual
parent declaration seeds the root member's TDZ mask, then the existing family
lattice and Round 38 per-occurrence read proof propagate it. Whole-family writes,
class-anchor/self ordering, tree semantics and recovery checks still run. Neither
an external request nor a TDZ mask substitutes for these proofs.

This increment does not admit the cross-block CAPTURE cases, exceptional-parent
deferral, a new static-field execution time, or inner/outer class-name merging.
No source-language premise is added: Guarded uses the raw check, and SourceAccess
still requires the approved LanguageSourceV1 premise. The exception boundary is
unchanged: compare exception constructors and traces, never Node/Ark message or
stack spelling.

The preparation products are moved, not copied, into final construction. Arrays
are reserved/charged before filling; requirements are scanned once per pass and
existing sorted cell indexes perform O(log N) lookup. Total added work is bounded
by O(N log N) in retained IR plus request size, with no per-cell CFG walk. Budget
failures remain typed. More class inputs are live before the creator tree than in
the old sequential build; their costs are admitted cumulatively.

Tests: `class_deferred_test` (real-file preparation/root-obligation mutations,
independent declaration fulfilment, owner/store/scope/activation mutations,
copy/block splitting, 200/201/400/800 and byte/work boundaries) and
`class_deferred_cli_test` ([fixture provenance](../../../tests/integration/fixtures/class_deferred/README.md))
cover self-reference, forwarded reads, multiple creators sharing storage, early
static-method reads, initializer re-entry/throw, heritage throw, instance construction re-entry and a member-owned catch. Static-field early read and loop-local class construction
remain refusal tests. Existing capture/root-anchor/write/recovery tests are part
of the targeted regression slice; positive loop/static-initializer recovery is
not claimed.

Round 43 workspace verification (2026-10-14, uncommitted): keep-going full build
passed; fixed source-regression set 65/65; the class/capture/deferred/guarded/recovery
slice passed 46/59, with the 13 failures already listed in status.md. Both new
tests passed. A resource-gate ordering regression discovered in
`deferred_capture_source_test` was corrected without changing its assertions;
that test passes in the final slice. Against explore42/explore42s, completion is
485/620 and 517/620 (six new records in each), with zero regressions and zero
byte drift among previously complete outputs, including o32/49868. The three
excluded cross-block CAPTURE records retain identical manifests in both modes.
Of the ten ExternalRoot candidates, six complete; TimeUtils now reports an
unsupported instruction, and the three KeyboardController records reach
expression/evaluation ownership failures. The old-contract o15/37098 remains
blocked by the lazy-import contract. These next failures are not completion.

## 11. Checked capture arguments (Round 44)

A verified Round 38 pair has two original operations but one source read. The
load owns the value identity; the named guard owns the exceptional exit and
success Commit. `checked_read_event` exposes this ordered, MayThrow evaluation
without changing raw `ValueLexicalLoad` semantics or granting `kCellGuard`.
The message boundary remains the one above: compare exception constructors and
control/value/log traces, not Node/Ark message or stack spelling.

Only an unmet typed member-reference obligation seeds `ComposeCheckedCalls`.
Already-satisfied references and standalone checked reads retain their plans;
proofs allocate no AST symbols, names or owners. The existing composition pass
is reused in a separate checked-call mode. An atom consumes the exact read and
guard events over independently proved single-entry, copy-free normal edges.
Input-free, effect-free normal control contributes an edge, not a source event;
this permits block splitting without recognizing an opcode sequence. Existing
inlined module pairs may retain their already-proved atomic interval, including
both throw identities; this does not admit new module projections.

The forest invariant remains: every absorbed throw point has its execution
owner's handler and needs no catch copies; a nonthrowing node absorbs no throwing
subtree. SameCatch/HasCatchCopies use the guard's exception identity for checked
reads. New catch staging is not synthesized. A rejected proof carries typed
`GuardedCallError` with call/read/guard/use and original/working read/guard PCs
through CallPlan, route receipts, record-entry failures and manifests. Existing
failure enum values and resource/work classes are unchanged.

The evaluation verifier accepts exactly standalone-root or uniquely-consumed
inline-with-composition states. It re-proves raw guarded evidence before checking
the complete composition membership and before producing an equation-replacement
mask. The guard remains `kGuardedCellRead`. Tree construction emits the shared
SymbolExpr in the argument, without a duplicate guard statement. Tree operands
and recovery preserve `OriginalActionRegion::kCapture`: load Before, guard
success Commit, cell reference, and the guard's exceptional completion are
recomputed rather than borrowed from the final call's action.

Complexity is O(N log N) in the original operations/edges/uses/copies, bounded by
existing dominance analysis. Pair, use, path-stop, root and diagnostic indexes
are built once, with precharged arrays. No per-call CFG walk or repeated whole-
method retry is used. Family requirements, activation/declaration placement,
normal-flow capture handling and unguarded handler accesses are unchanged. No
new source-language premise is introduced.

Tests: `guarded_call_test` covers atomic membership and success/catch mutations,
receiver mutation, foreign events, catch staging, a call outside the guard's
handler, a nonthrowing wrapper, multiple checked arguments, duplicate consumers,
copy/block splitting, 200/201/400/800 and precise work/byte boundaries. Existing
standalone checks remain in `phased_guarded_reads_test`. `guarded_calls_cli_test`
uses the [real es2abc fixture](../../../tests/integration/fixtures/guarded_calls/README.md)
for Node differential and source mutations. Receipt and both manifest paths
have diagnostic round-trip tests, including PC zero and absent locations.

Round 44 workspace evidence (2026-10-14, uncommitted): keep-going full build
passed; the first fixed-set run was 64/65 with a rich-text worker failure, its
isolated retry passed, and the complete serial fixed-set rerun passed 65/65.
The targeted slice passed 50/59; all nine failures are on the pre-existing list.
The new unit and Node differential tests passed. Final exploration comparison
against explore43/explore43s is 486/620 and 517/620, with zero regressions and
zero byte drift. o16/8916 completes under the old flags. All three o19 target
calls pass their former reference refusal, but the records remain incomplete
at stop methods 7958/23270/35440, ExpressionOwnership PC 88/85. Old-flags
coverageCollect reaches DecisionPrefix PC 347/342 instead of its former call
refusal. No holdout or RSS run was performed.

## 12. Initialized guards in decision prefixes (Round 45 WIP)

This change consumes an already verified evaluation placement. `kCellGuard`
means `IsInitializedCellGuard` proved a lexical read's initialized premise;
it owns only a zero-width `BlockBoundary` anchor. It is not an expression, a
comma operand, a consumer update or a new symbol. `kGuardedCellRead` remains an
ordered throwing event and cannot take the anchor disposition.

`evaluation_disposition.h` provides the common disposition used by ordinary
blocks and decision prefixes. Environment declarations and enclosing
class/array projections retain their block-owner paths; the new decision
admission is the initialized guard only. Existing catch-scope and catch-copy
checks still precede decision emission. Control joins/follows, declaration
placement and naming are unchanged. The independent tree-flow index rechecks
original guards against cell initialization facts, requires an anchor and
checks its exact block boundary; recovery still records
`OriginalInitializedGuard`. It does not trust the emitter's disposition.

Evaluation failures now carry fixed-size typed obligations and optional
operation/block/use/value/owner locations. Original and working PCs come only
from input origins. Nested logical/snapshot checks retain their own failing
witness, including the actual final assignment-fallback attempt. Missing
logical consumers refuse at the selected branch, rather than dereferencing an
empty optional and publishing spurious Op0/PC0. Resource and work enum values
and classification are unchanged. Receipts, record-entry JSON and
`abc_semantic --record-source` retain the same evidence. Tree ownership records
`decision_entry` separately from an actual `expected_owner` consumer.

Before changing emission, all seven T candidates were re-exported: all were
`kMissingExpression` with `kCellGuard`; none was a `kGuardedCellRead`. The new IR
fixture checks actual decision discovery, initialized anchors, real guarded
read refusal, missing/duplicate/expression/incorrect-boundary anchor mutations,
removed initialization premises, identity copies and split blocks. Sizes
200/201/400/800 exercise scaling and exact byte/work boundaries. Production
classification is O(1) per operation, without new indexes or buffer allocation;
the diagnostic witnesses are fixed-size. No source-language premise is added.

The real es2abc fixture and Node oracle are documented in
[decision_guards/README.md](../../../tests/integration/fixtures/decision_guards/README.md).
Its finally-throw case uses the ABC-handler oracle: es2abc leaves the inlined
finalizers in protected ranges (m473 PC170 → H247, PC325 → H337, PC377 → caller).
The repeated finally trace is bytecode-faithful, not an engine bug. Original
Node source is used for the remaining modes; exception messages/stacks remain
outside the comparison. See the exception-control oracle boundary.

## 13. Cross-block normal-owner activation (Round 68)

An early Guarded capture may now defer through the original allocation's
normal-flow dominance over creation and the cell's unique direct store.
Independent working-flow and source-scope checks still require that same
activation and a declaration at the original store in the allocation's source
scope. The new rule is Guarded-only; SourceAccess, strict writes and exceptional
parents keep their previous boundaries. See the
[proof, evidence and tests](round-68-capture-activation.md).
