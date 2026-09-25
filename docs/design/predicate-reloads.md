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

## Boolean Polarity at Emission

In conditional contexts, Predicate now composes an outer ToBoolean/LogicalNot
with a typed inner ToBoolean/LogicalNot. Its polarity is the XOR of the outer
negation (including structural if inversion) and the inner logical negation.
The operand is emitted once through Value, so slot observation and ABI binding
remain intact. ToBoolean does not invoke object coercion; evaluating it again
only as the native condition cannot add getter, Proxy or host effects.

This identity does not rewrite general expression-valued results, FalseOrZero
or TrueOrNonzeroNumber. Boolean region emission additionally uses an explicit
Boolean-value mode (see boolean-diamonds.md), retaining coercion when polarity
alone would yield the operand value. It neither changes the AST's effect/SSA proof nor source
origins: condition spans are measured while emitting the simplified condition.
The local typed check is constant time with no allocation. Deferred checks
include all polarity/inversion combinations, primitive/object truthiness,
numeric VM predicates remaining unchanged, and operand/PC preservation.

Production builds and ABC-only export to
build/pasteboard-boolean-polarity-20260925 succeeded. All six pages have simpler
conditional spelling, including `if (!$r4)` and `if ($acc)` instead of composed
boolean conversions. Totals retain 4,204 lines and decrease to 90,181 bytes
(128 fewer). Method 9193 PCs 263, 267 and 270 now map to [18587,18599), the
actual `if (!$r4) {` header including newline. Maps retain 195 methods / 9,745
spans and the same eight incomplete, two refused and nine metadata-only record
statuses. Read-only review found no concrete defect. Tests, JS execution and
RSS remain paused; control-flow recovery and full equivalence remain unfinished.
