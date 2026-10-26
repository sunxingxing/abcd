# Array destructuring patterns

User-approved premise (2026-10-23, `LanguageSourceV10`, array-destructuring
acquisition): extends the for-of acquisition premise to array patterns. The
accepted difference is the step at which a non-callable `@@iterator` / `next`
throws. Close order and every other protocol step must be proven.

## Pinned emission

es2panda `compiler/base/destructuring.cpp` `GenArray` and
`compiler/base/iterators.cpp` (`Iterator`, `DestructuringIterator`,
`DestructuringIteratorContext`) emit, for `[e0, …, en] = rhs`:

1. Acquisition: `GETITERATOR; sta it; lda it; ldobjbyname "next"; sta method;
   ldfalse; sta closed; ldfalse; sta done; ldundefined; sta result`.
2. The context's protected region starts. For each element its `Step`:
   a protected region `lda done; istrue; jnez noClose; lda method; callthis0
   it; sta next; throw.ifnotobject next; lda next; ldobjbyname "done"; sta done;
   isfalse; jnez normal; ldtrue; sta done; ldundefined; jmp noClose; normal: lda
   next; ldobjbyname "value"; sta result; jmp end; noClose: ldundefined; jmp end`
   whose handler is `sta result; ldtrue; sta done; lda result; throw`. Then the
   element's default (`BranchIfStrictUndefined` …) and its target `SetValue`.
   A hole is a bare `Step`.
3. Context end: normal completion `lda done; istrue; jnez end; Close(false)`;
   its handler `sta result; lda done; istrue; jnez rethrow; Close(true);
   rethrow: lda result; throw`.

`Close` is ECMA IteratorClose: on throw completion, errors from GetMethod/Call
are ignored and the original exception is rethrown; on normal completion they
propagate and a non-object result throws TypeError (Hole sentinel premise of
2026-10-16 for the caught close error).

## Increment 1 (O0 shape, pure bindings)

Admitted elements: holes and targets whose `SetValue` is a single register
store (`sta`) or `stlexvar`, with no default, rest or nested pattern. Such
bindings cannot throw, so an exception inside the context region can only come
from a `Step`, which sets `done` before rethrowing: the context handler is a
pure rethrow, and the normal-completion close runs exactly when the last step
left the iterator not done. Under these conditions the protocol is

    r = Acquire(rhs)       // GetIterator + cached next (may throw)
    v_k = Step(r)          // one IteratorStep with done tracking (may throw)
    Close(r)               // IteratorClose(normal) unless done (may throw)

with the binding stores of each `v_k` in between, which is exactly native
`[t0, …, tn] = rhs`.

`NormalizeArrayPatterns` (source layer, like `NormalizeGenerator`) matches the
exact pinned sequence with register-slot discipline, string keys resolved from
the regional pool, all branch targets checked, and no outside branch or handler
entering the region. It removes the protocol instructions and their try
regions and emits PC-keyed `PatternSite` certificates that the semantic lifter
honors (as it honors yield/await sites). Unmatched shapes are left untouched and
keep their existing typed refusals.

## Increment 1b (source)

Pattern actions are ordinary effectful operations through evaluation, tree
construction and verification (`ArrayPatternExpr`, record operands typed
`kPatternRecord`). Native syntax is a printer projection only:
`PlanArrayPatternSurfaces` accepts a run of retained ExpressionStmts, contiguous
in one block, `r = Acquire(e)`, then each `t_k = Step(r)` / `Step(r)`, then
`Close(r)`, none of them omitted, moved, replaced or printed in a loop header.
Since nothing else executes between those statements and the targets are local
bindings, the run is exactly `[t_0, …, t_n] = e` under the approved premise.
It prints as an assignment pattern, or as one declaration when every bound
target is a `let`/`const` initializer of distinct storage (`const` only if all
are `const`). Any other run leaves its nodes unprintable (typed refusal).

## Protocol coordinate composition (2026-10-26)

Array-pattern folding compacts **working byte offsets**, not just instruction
indices. The parallel `source_pcs` vector still names immutable original byte
offsets. Earlier async/generator normalization has already compacted the body;
its `AwaitSite.pc`/`mode_pc` and `YieldSite.pc`/optional `mode_pc` name that
intermediate working body. They must be relocated again after folding.
`AwaitRegion` and `YieldRegion` are original-coordinate evidence: resume,
rejection, continuation, and yield RETURN-route fields must **not** be rebased.
Any such original anchor retained in the intermediate body must survive the
subsequent fold. Original protocol instructions already removed by their owner
are not invented as new working instructions.

`protocol_rebase.{h,cc}` joins the two monotone original-provenance streams and
establishes an injective input-instruction to output-offset map. Its producer
and verifier check surviving opcode/operands/branches, primary anchor origins,
exclusive claims, mode order and selected catch identity (including handler
type, through mapped handler origins rather than renumbered TryIds). Fold ranges
cannot absorb an anchor or split a retained await/yield suspend-to-mode interval;
deleted anchors never map to a neighbouring instruction. Invalid maps/anchors/
dispatch produce typed refusals; byte/work exhaustion retains its typed limit.
The file reader converts invalid fold composition to `MetadataError::kRead`.

The composition and independent verification run unconditionally before any
phased input consumes the streams. Ordinary async, `NormalizeConcurrentAsync`,
generators and async generators use the same call. There is no legacy-first
retry for this soundness fix. New work is
O((I+T+H+A+Y) log(I+T+H)), with prepaid linear mapping/claim storage and bounded
shared dispatch scratch; this does not change the older pattern-discovery bound.

The original `array_patterns` fixture's `awaited` destructures **after** its
only await. Folding therefore removed no instruction before either retained
await anchor, so the stale-coordinate bug was not exposed. The new
`normalized_protocols` fixture covers patterns before/between/after awaits,
multiple folds, rejection/catches, generator NEXT/THROW/RETURN and value/error
identity. Unit mutations cover removed/split anchors, corrupted input/output
mode mappings, cross-stream collisions, original RETURN routes, and changed
catch destinations, plus exact/exhausted grants and 200/201/400/800 scales.

Remaining independent gates are explicit negative fixtures: the current async
generator owner refuses these raw array-pattern protocols with
`FunctionEmitError::kKind` before folding; concurrent async reaches folding and
remaps its awaits, then `VerifyConcurrentAsync` refuses its existing pre-fold
one-to-one body correspondence (`ConcurrentError::kCorrespondence`). Neither
publishes stale certificates. Extending those owner proofs is separate work.
No new source-language premise or opcode admission is introduced.
