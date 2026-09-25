# Private Array Expressions

The shared AST can now recover dense array literals whose elements require
ordered assignments, module/property reads or other admitted expressions.
This is a common normal/phased-SSA path, not a pasteboard-specific writer.
The earlier buffered-object-only fold remains available for its existing cases.

## Admission

`FoldArrayExpressions` requires one normal block containing an empty-array
creation, an accumulator save, consecutive own-index definitions starting at
zero, and an immediate final load of the saved array. Between definitions,
only source assignments and no-ops are admitted. Nested array candidates,
staged calls, control transfer, receiver overwrites and fused source spans
are not incorporated into this interval.

The created SSA value has exactly one remaining use: its save. Each indexed
definition must read that exact saved SSA value once; intermediate expressions
must not read it. Its total remaining use count must equal the definitions
plus the final load. Counts include exceptional phi inputs and are updated by
the preceding dead-copy pass. Thus additional aliases, escaping references,
partial-array reads and handler observations all prevent folding. The final
load is in the same block and after the last definition, so its normal use
occurs only after construction. Original SSA identities are retained; this
does not rebuild normal SSA for exceptional control flow.

## Emission

`SourceArrayLiteral` owns element instruction endpoints and references the
existing assignment/no-op statements. The printer emits a direct array literal
with parenthesized comma expressions, preserving assignment and property-read
order and the final accumulator value. Array allocation stays before element
evaluation. Saving the array into the private receiver register moves to the
end, permitted only by the non-observation proof. No inherited setter can be
called by array-literal own-index creation.

No Object/Reflect intrinsic, additional helper function, IIFE, temporary realm
binding or async wrapper is introduced. A native await remains at its original
expression position; this pass does not relax async admission or its execution
contract. No new resource-exhaustion premise is adopted.

Creation, save, assignments, no-ops and index definitions each retain their
original PC span. The save spans the whole literal, including its eventual
assignment. No mapping is derived by searching generated text. A literal is
indivisible when the printer extracts the unconditional root prefix.

## Cost and Evidence

The pass reuses the AST owner's SSA use counts. Candidate intervals are disjoint
even on rejection; a nested creation becomes the next candidate. With I
instructions, R visited reads and K accepted elements, phased lookup gives
O(I+R) scan time; normal SSA adds its native per-instruction read-lookup cost.
Extra retained storage is O(K). Geometric growth charges old plus new capacity
before allocation, and all accepted literals share the remaining AST budget.
No new dependency or imported implementation is involved.

ABC-only owner inspection of pasteboard entry 8831 now produces a
`PrintedTemplate` for async child 9165 with 362 original-PC spans and six
capture obligations. This crosses the previous own-index failure at PC272,
covering the dense array built at PCs254-310. The record owner advances from
PC101 to PC220, where child 8552 independently fails internal-value validation
at PC53. Ordinary call return values are not proved by the pending-exception
contract, so that gate remains intact.

Production `abc_semantic` and `abc_structure` builds passed. Read-only review
found no concrete defect. Export `build/pasteboard-array-expressions-01` still
contains three explicitly incomplete source records; neither complete-record
equivalence nor execution validation is claimed. Tests, generated JS execution
and RSS measurement remain paused.

Deferred regression coverage: inherited numeric setters, throwing getters and
module loads, handler phi observations, dead versus live aliases, receiver
overwrites, sparse/out-of-order indices, nested candidates, native awaits,
root-prefix extraction, fused/previous-origin spans and exact capacity limits.
