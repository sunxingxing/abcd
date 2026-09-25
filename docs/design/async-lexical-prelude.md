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
