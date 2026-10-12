# 先于初始化的受保护捕获（TDZ 捕获）

状态：设计共识（Claude 与 Codex gpt-6-astra/xhigh，2026-10-12）。第 1 步（原因链）已实施；
第 2–3 步的第一个增量（同块延迟声明）已实施，见 §5；其余仍未实施。

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
  一致。若验收合同把消息字符串算作可观察行为，就必须保留诊断语义或拒绝。现有模块读取设计也
  没有声称消息等价。
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

## 6. 剩余类别（探索集诊断）

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
