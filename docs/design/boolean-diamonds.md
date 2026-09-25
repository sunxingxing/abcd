# Boolean Assignments From If Regions

ProveBooleanDiamond consumes the existing typed RegionIf and SourceAst, not
instruction names or printed code. RegionIf already owns a single-entry header,
two exclusive arms and their continuation. Each arm must have one normal CFG
successor, that continuation, and a fallthrough or matching final join jump.
Each contains exactly one stored true/false assignment to the same private slot;
the values must differ. Other nodes may only be unmerged NoOperation anchors.
Extra effects, writes, returns, origin-bearing nodes and missing arms refuse.

The result is a BooleanDiamond containing the original target and fallthrough
arm value. The shared IfRegion printer emits header effects unchanged, evaluates
the original predicate once and commits the resulting boolean to that target.
Because the conditional branch's true edge goes to the alternate, a true value
in the fallthrough arm requires predicate inversion. PredicateForm::kBooleanValue
requires explicit ToBoolean (`!!`) when no logical negation already produces
a Boolean, so a truthy object cannot become the assigned value.
This applies to value-producing regions, unlike condition-only
polarity simplification. Numeric VM predicates retain their existing writer.

The removed branch, assignments, pure empty anchors and join jump PCs all map
to the emitted assignment. Fused header predicate/reload PCs are relocated by
the same common origin path. All PCs remain ordered as header, body, alternate;
no map is reconstructed from source text. Slot census still passes the target
through Target and the predicate through its normal slot-aware writer.

Cross-block argument owners keep their current region output. Existing argument
overlap checks run before diamond recognition. Proofs inspect only the two owned
arms: linear total instruction work over non-overlapping If regions, constant
scratch, no new dependency or execution premise. No effects or CFG/SSA values
are discarded to obtain an apparent coverage increase.

Tests, JS execution and RSS remain paused. Deferred regressions include opposite
polarity arms, differing targets, extra writes/effects, returns, mismatched join
edges, argument overlap, boolean-vs-truthy-object result values, header throws,
fused origins and follow-on predicate/slot reads.

## Production Evidence

Both production tools built successfully after the Boolean-value mode change.
ABC-only export build/pasteboard-boolean-diamonds-value-20260925 succeeded.
Three diamonds in PasteBoard method 9193 now emit simple Boolean assignments,
including `$acc = !$r4;`. The first diamond's PCs 263 (fused reload), 267
(predicate), 270 (branch), 272 and 275 (arm values), and 273 (join jump) share
the actual assignment span [18587,18600), including newline.

Totals are 4,192 lines / 90,070 bytes, down 12 lines / 111 bytes from
pasteboard-boolean-polarity-20260925. Maps retain 195 methods / 9,745 spans.
Eight records remain incomplete, two host constructors remain refused at PC 14,
and nine records remain metadata-only. Read-only reviews found no concrete
defect in region admission or Boolean-value polarity. Tests, generated JS
execution and RSS checks were not run. Follow-on boolean assignments and
labeled control flow remain; this is not whole-function or application
equivalence evidence.

## Topological Arm Order

The shared Kahn topological sorter can enqueue a taken arm before a fallthrough
arm, depending on successor storage order. That order is semantically valid but
previously hid otherwise valid RegionIf diamonds from the consecutive-block
recognizer. OrderIfArms now normalizes adjacent reversed arms in the acyclic
region builder, after terminal-arm placement and before order-position rebuild
and full forward-edge validation. The shared graph sorter is unchanged.

Each swap requires the existing CanFormIf proof with the two arms reversed:
the header has the exact taken/fallthrough edges, both arms have single normal
entry, and each either terminates or has only the common join as successor.
Thus neither arm can depend on the other, and swapping adjacent arms preserves
all topological constraints. It moves no other block and changes no CFG edge or
instruction. Loop and exception-scope region construction remain unchanged.
One constant-size window per block gives O(B + E) including the existing final
edge validation, with constant additional storage and no dependency change.

Deferred checks include both arm orders, terminal arms, shared-arm entries,
cross-arm edges, nested/distant joins, final-edge validation, and source-origin
ordering after canonical arm placement. Tests, JS execution and RSS remain
paused.

Production builds and ABC-only export build/pasteboard-if-arm-order-20260925
succeeded. Six pages recover additional structured regions. PageA Builder
method 7449 now selects its receiver through a native if/else; predicate PCs
35/38 map to [686,698), body PC 40 to [698,721), and alternate PC 44 to
[730,753). Initialization's Boolean diamond also reaches the shared Boolean
assignment path, instead of nested label breaks.

Totals become 4,096 lines / 88,972 bytes, down 96 lines / 1,098 bytes from
pasteboard-boolean-diamonds-value-20260925. Maps still retain 195 methods /
9,745 spans. Record statuses and the two refused PC-14 host constructors are
unchanged. Read-only review found no concrete defect. Tests, JS execution and
RSS were not run; remaining labels and broader region recovery are unfinished.
