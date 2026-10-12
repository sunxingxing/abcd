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

## 2026-10-12：参数写入 cell 的前缀（已撤回）

探索集中 `FunctionEmitError::kKind`（20 个 record）的诊断（临时插桩，已回退）显示：15 次都来自
本前缀之后只允许 `mov` 的检查。实际的指令序列是：

`newlexenvwithname; mov…; lda vArg; stlexvar 0, slot; asyncfunctionenter`

也就是说，async 函数的参数或 `this` 被闭包捕获，函数实例化阶段就把它拷贝进新环境的 cell。

`eadd4c1f` 曾按相邻的 `lda vArg; stlexvar 0,s` 对放宽准入，探索集 455 → 461。Codex 第十五轮
指出两项阻断，该扩展已撤回：
- **R1**：前缀会在累加器中留下参数值，而归一化随后删除了定义累加器的
  `ASYNCFUNCTIONENTER; sta vPromise`。现有检查只限制对 `vPromise` 的访问，没有证明这个被删除
  的累加器定义没有后续读取。依据锁定解释器：enter 把 async 状态对象写入 acc，resolve 从 acc
  取兑现值。反例：入口之后不重新加载 acc 就 resolve。这一缺口同样涉及原有的
  `newlexenv` 前缀（它也会写 acc），需要一并证明。
- **R2**：按相邻 opcode 对扩充旧模板，违反 AGENTS.md 关于通用恢复准入的规则。

**正确的做法**：在原始 CFG 上，用类型化标量和累加器的 def-use，证明以下几点：
- 参数来源与写入目标属于本次新分配的环境；
- 前缀的效果可以保留；
- 被删除的协议定义（acc 与 promise 寄存器）没有被保留指令或返回值读取。

验收需包括以下变体：寄存器复制、插入 NOP、入口后未覆盖 acc、只有部分分支覆盖、先复制 acc
再使用。
