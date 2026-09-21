# Environment Flow

`AnalyzeEnvironments` computes ordinary lexical environment allocation sites
and load/store bindings over the unchanged `BuildNormalFlow` graph of a
validated method. Semantics follow the pinned OpenHarmony-v7.0-Release
interpreter's NEWLEXENV, POPLEXENV, LDLEXVAR and STLEXVAR handlers.

The entry environment is an unresolved captured chain. A bounded FIFO worklist
assigns each block an incoming environment once. Matching later edges require
no work; disagreement invalidates the entire result, including previously
visited blocks. Stable-environment loops are supported. An allocation site in a
loop denotes fresh runtime instances, never a single shared runtime object.
Parent-before-child definitions feed the immutable environment ancestry index.

Time is O(I+B+E+(P+L)log D), for instructions, blocks, edges, pushes, lexical
accesses and the bounded lexical-depth domain. Each instruction is visited at
most three times. Result and scratch vector capacities plus the ancestry index
share the analysis budget; caller-owned body and graph storage are additional.

Handlers, resume edges, conflicting joins, malformed lexical operands and
invalid local slots are explicit typed failures. Captured bounds, slot values,
Hole/TDZ, closure capture semantics, function ABI and source emission are not
proved. Historical runtime semantic equivalence is not established by this
analysis alone. IDs are relative to their owning result.

## Inspection Contract

`abc_environments` uses the corpus manifest and cached static exclusions.
Exit zero means inspection completed, not all methods were supported or any
record was decompiled. Environment refusals have a typed histogram; underlying
file, decode or graph failures still fail inspection.

Allocation, binding, instruction-visit and peak analysis-payload counters cover
successful analyses only. Rejected analyses can consume time and scratch memory
without contributing those counters. These metrics are not whole-pass RSS.
The CLI applies the shared 1,000,000,000-byte address-space ceiling; whole-process
RSS and eventual aggregate child-process memory require separate measurement.

## Verification

Unit tests cover stable and accumulating loops, diamonds, conflicting joins,
backward edges, unreachable code, capture/local slot bounds, admission budgets,
and geometric scaling through 65,536 environments. CLI fixtures cover success,
semantic refusal, decoding failure and cached static exclusions.

The current Debug build passed all 54 CTest tests on 2026-09-22. Full-corpus
environment-flow measurements and complete-decompiler memory verification
remain pending; no application record source-recovery claim is made.
