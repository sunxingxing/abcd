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
