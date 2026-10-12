# 先于初始化的受保护捕获（TDZ 捕获）

状态：设计共识（Claude 与 Codex gpt-6-astra/xhigh，2026-10-12）。第 1 步（原因链）已实施；
第 2–5 步未实施，生产准入不变。

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
