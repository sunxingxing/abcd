# Register Reloads in Predicate Inputs

FoldPredicateCopies now handles the accumulator destination in addition to
private register copies. Admission requires an emitted stored scalar register
reload, the same register in the original normally completing pure copy, and
an exact SSA output with one remaining use: the original predicate instruction
already owned by the adjacent conditional terminator. Empty source nodes may
intervene, but no emitted effect or write is crossed. Property expressions are
not rewritten, and exactly one typed operand must match the copied slot.

The predicate consumes the original register directly and the accumulator
commit disappears. This differs from known-reload elimination: this pass
rewrites the sole consumer and does not leave a hidden physical-accumulator
dependency for later result retargeting. The copy's register value cannot change
across the admitted empty interval. Original SSA counts include phi observers.
Both ordinary conditional breaks and continues use the same typed input path.

The conditional retains its predicate PC in fused_read and adds the original
register reload PC in fused_reload, with an explicit instruction distance.
RelocateFusedOrigins handles spill and reload origins independently, so the
reload no longer needs an unrelated spill to map to its generated condition.
Existing paired call origins retain their previous behavior. No output text is
parsed, no expression AST is duplicated, and no allocation or dependency is
added. Block scanning and indexed SSA queries retain existing complexity.

Tests, JS execution and RSS remain paused. Deferred regressions cover extra/phi
uses, operand mismatch, effects or writes between reload and predicate,
unsupported property operands, repeated occurrences, stored-result/known-reload
composition, conditional continues and malformed independent origin distances.

## Production Evidence

Both production tools built successfully. ABC-only export to
build/pasteboard-predicate-reloads-20260925 succeeded. PasteBoard method 9193
now directly tests r4 after fromStyledString, instead of reloading r4 into acc.
PCs 263 (reload), 267 (boolean conversion) and 270 (branch) all map to bytes
[18605,18621), the emitted `if (!(!!$r4)) {` header including newline.

Totals are 4,204 lines / 90,309 bytes, down one line / 13 bytes from
pasteboard-reload-proof-order-20260925. Partial maps retain 195 methods /
9,745 spans. Record statuses remain eight incomplete, two host constructors
refused at PC 14 and nine metadata-only records. Read-only review found no
concrete defect. Tests, generated JS and RSS checks were not run; these results
are not proof of full semantic equivalence.
