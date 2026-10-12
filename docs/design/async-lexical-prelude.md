# Async Lexical Prelude

## Contract and Reference

Under the approved V2 contract, VM-internal resource exhaustion differences
are outside source equivalence. Application and host exceptions remain
observable. The decompiler's own memory cap is unchanged.

The pinned `OpenHarmony-v7.0-Release` runtime implements NEWLEXENV and its
named/wide forms in `ecmascript/interpreter/interpreter-inl.cpp:4583` onward.
They allocate a private environment and update acc and the frame environment.
`RuntimeNewLexicalEnvWithName` in `ecmascript/stubs/runtime_stubs-inl.h:2922`
sets parent/global environment and extracts scope metadata. The extractor in
`ecmascript/jspandafile/scope_info_extractor.cpp` allocates debug metadata;
it does not invoke user callbacks. Valid scope metadata still needs independent
validation, not an assumption based on the instruction name.

## Admission

`ProveAsyncCompletion` now takes explicit `ResourceFailureScope`, defaulting to
`kObservable`. Only `kExcluded` with the existing modern resume profiles
1202/1206/1301 admits a single leading typed `PushEnvironment`. Remaining
pre-promise instructions still must be validated ABI register moves, never
arbitrary calls, stores, pops or loads. Source owners pass the versioned policy.
Other profiles retain their previous behavior.

The allocation is retained in normalized instructions and origin PCs, not
deleted or replaced by undefined. Existing environment/SSA, local initialization,
capture, named-scope and exception-source admission must subsequently pass.
No application exception edge is dropped. The original wrapper's handler-range
checks still prevent user handlers covering the prelude. Branch targets cannot
reenter the prelude. Added recognition is constant space and linear in prefix
length; no new per-instruction table is allocated.

## Evidence and Remaining Work

Both production CLIs build. Read-only review found no concrete defect; tests,
generated JS execution and RSS measurement remain paused.

ABC-only ProgressBar entry 9915 generation reaches async method 10333 through
the common pipeline. Debugger inspection after `BuildSemanticTemplate` reports
5,350 text bytes, 160 spans and three capture obligations under V2. Original
allocation PC0 remains an instruction anchor; nine await-protocol aliases are
included (the first at PC322). This is a template, not executable standalone
source or a final-file mapping.

The record export in untracked `build/pasteboard-async-lexical-01` still publishes
seven incomplete application records. ProgressBar remains refused at outer
PC264: the first observed shared-body failure is method 10639's nested creation
at PC24, `kUnsupportedOperation`. A legacy fallback also fails through method
9804's child creation at PC24. Neither the record nor source equivalence is
claimed complete. Next work must trace capture propagation through that nested
creation chain, without replacing the shared pipeline with a special writer.

Deferred regressions: observable-resource default refusal, malformed/named
scope metadata, unsupported profiles, prefix side effects, branch reentry,
preserved scope across await and handlers, original-PC propagation and budgets.

## 2026-10-12：参数写入 cell 的前缀与前导 NOP

探索集中 `FunctionEmitError::kKind`（20 个 record）的诊断（临时插桩，已回退）显示：15 次都来自
本前缀之后只允许 `mov` 的检查。其中的实际指令序列是：

`newlexenvwithname; mov…; lda vArg; stlexvar 0, slot; asyncfunctionenter`

也就是说，async 函数的参数或 `this` 被闭包捕获，函数实例化阶段就把它拷贝进新环境的 cell。

**准入（`ProveAsyncCompletion`）**：已接受上述 `PushEnvironment` 前缀时，`asyncfunctionenter` 之前
还可以出现若干 `lda v; stlexvar d, s` 对，条件是：
- `v` 是传入参数寄存器（`frame.locals ≤ v < frame.registers`）；
- 这次 store 经类型化的 `LiftLexicalInstruction` 提升后，是 depth 为 0、取值来自累加器的
  `LexicalStore`。

这些指令没有用户可见的效果，与 `mov` 拷贝一样保留在归一化前缀中，由后续的环境/SSA、局部初始化、
捕获和异常来源准入各自独立检查。拷贝校验循环会对每一对重新识别并校验。另外，前缀之前的 NOP
没有语义，同样保留在前缀中。

以下情形仍然拒绝：
- 局部寄存器（不是参数）；
- 外层 depth；
- 没有前导分配的写入；
- 前缀之外的任何其他指令。

**证据**：
- `control_composition_test` 新增 `AsyncParameterCells`，覆盖：准入并完整保留前缀，以及局部
  寄存器、外层 depth、无环境分配三种拒绝。
- 探索集 455 → **461/620**，无回退；新完整的 6 个 record 都通过 `node --check`。典型输出是
  `async m() { let $local1 = this; … }`，其中的箭头函数捕获了 `this`。
- 本机 es2abc 生成的 fixture 的 async 尾部布局不同，会在 epilogue 模板检查处被拒绝（与本改动
  无关的另一项模板限制），所以这次没有 Node 差分。

**仍未覆盖**：上述 epilogue 布局；本前缀条件仍是局部的充分条件，按审计共识应迁移到通用的效果与
初始化证明上。
