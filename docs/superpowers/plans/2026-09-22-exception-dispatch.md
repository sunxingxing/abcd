# Exception Dispatch Implementation Plan

> Execute the approved decompiler design using TDD, bounded ownership and
> independent specification then quality review. Existing feature worktree is
> isolated; no additional worktree is needed.

**Goal:** Recover the exact pinned dynamic-runtime handler selection for each
protected instruction, without quadratic instruction-by-handler expansion.

**Architecture:** A sorted event sweep and a file-order min-heap produce a
compact non-overlapping interval map. Queries use binary search. Keep this
dispatch-on-existing-exception map separate from instruction throw effects,
exceptional SSA state and source generation.

**Tech Stack:** C++20 STL sort/heap, decoded MethodBody, existing normal graph,
manifest-backed bounded CLI, CMake/CTest.

## Runtime Contract

Pinned ets_runtime `ecmascript/method.cpp:87-111`, `Method::FindCatchBlock`,
scans try entries in encoded order. A try covers `[start, start+length)`.
The first covering try with any catch selects its first encoded handler;
catch type and handler length do not participate in selection. An empty catch
list permits later tries to match. This is not a smallest-range rule.
`interpreter/interpreter-inl.cpp` EXCEPTION handler installs the thrown value in
the accumulator, clears pending exception state and transfers to that handler.
No matching handler means propagation to the caller, not a fabricated block.

These are explicit OpenHarmony-v7.0-Release ECMAScript semantics applied to
recognized dynamic profiles. Historical equivalence still requires independent
evidence; static/unknown profiles must not silently use this policy.

## Implementation

- [ ] Add `include/abcd/analysis/exception_dispatch.h` and
  `src/analysis/exception_dispatch.cc`. Use `TryId`, `HandlerId`, `DispatchSpan`
  and typed failure/budget records; expose
  `BuildExceptionDispatch(const MethodBody&, ProfileId, DispatchBudget)` and
  `ExceptionDispatch::Find(CodeOffset)` returning optional selected target.
- [ ] Test before implementation in `tests/unit/analysis/exception_dispatch_test.cc`:
  no tries, disjoint/overlapping/nested/identical ranges, encoded-order priority,
  typed first catch versus later catch-all, empty catch lists, zero lengths,
  half-open ends, malformed slices/targets/layout, unsupported profile and budget
  rejection. Compare a tiny slow reference scanner against generated cases.
- [ ] Validate all source ranges against sorted instruction boundaries, with
  checked arithmetic. Sort at most two events per eligible try; maintain a
  min-heap of encoded try indexes with lazy expiry. Every try is pushed/popped
  at most once. Merge adjacent spans only when selected try/handler is identical.
  Exact-reserve bounded event, heap and output vectors; catch allocation failure.
  Time O(I+(R+H)log(I+1)+Rlog(R+1)), storage O(R). Query O(log(R+1)).
- [ ] Test 200/201 and geometric overlapping ranges without a quadratic oracle
  at large sizes. Do not sort/rewrite the source try or catch arrays.
- [ ] Extract method-stage inspection from `tools/corpus_inspection.cc` into
  `tools/method_inspection.h/.cc` so adding the stage does not grow the corpus
  driver into a monolith. Preserve streamed diagnostics and existing counters.
- [ ] Add `abc_exceptions` and an explicit exception-dispatch stage. Count
  successful maps, spans and protected blocks/instructions separately from
  normal edges. Check each selected handler is an existing normal block entry.
  No exception effect claims or fabricated source output.
- [ ] Add CLI fixtures with real try metadata and first-handler selection,
  stream all failure stages without diagnostic caps, reuse static classifications
  and enforce the same 1,000,000,000-byte process cap.
- [ ] Run Debug/Release/ASan/UBSan, independent reviews and the complete dynamic
  corpus. Persist implementation/input identities, coverage, failures and RSS;
  commit coherent verified changes promptly.

## Remaining Objective

Exception dispatch alone is not a complete exception-aware SSA graph. Recover
references/literals/modules, throw effects and exact pre-throw states, closures,
async/generator semantics, bounded function tasks and verified JS/TS/ArkTS
output in one file per record. Do not substitute assembly or placeholder bodies.
