# Native Rest Local Binding

The callable owner already requires ProveRestPrologue before emitting a native
rest formal. The original ABI argument slot is not the resulting rest array:
its copied local is overwritten by COPYRESTARGS and the following save. Do not
rename that original ABI value into the native rest parameter.

FindRestLocal instead inspects the shared AST for the single retargeted
RestCopy assignment. It requires an unchanged RestCopy semantic origin and
the adjacent previous_origin save anchor. The original save must be a pure,
normally completing accumulator-to-target copy, reading exactly the producer's
SSA value. The resulting register SSA definition and slot must match the target.

Every original SSA read of that local must read this exact initialized value.
Entry-value reads, later overwrite reads and different phi definitions refuse
the substitution. A later unused write remains an ordinary register target;
the proof does not rename targets or erase arbitrary assignments. Normal SSA
and the whole-body bound-read audit are required; catch regions stay excluded.

The printer records the proven local separately from owner ScalarNames.
For each scalar read it supplies RestBinding.local to the scalar writer; for
targets it clears that field. The writer validates and prints the existing
native rest name. Legacy writers receive no populated local substitution.
The unique RestCopy assignment becomes a zero-width source anchor, with the
following save retaining its previous-origin anchor. Native formal allocation
and identity are already certified by the owner, not newly assumed here.

One AST scan and one SSA read scan cost O(I + U), with a constant number of
existing SSA lookups and O(1) extra state. No per-variable graph walk, temporary
name vector or output-text inspection is introduced. Independent read-only
review found no concrete correctness issue.

Tests, generated JS execution and RSS measurements remain paused. Deferred
regressions include unchanged rest identity, multiple runtime rest allocations,
entry and overwritten-local reads, phi reads, target-only later writes,
legacy fallback, name validation, and both zero-width initialization anchors.

## Production Evidence

Production targets abc_semantic and abc_structure built successfully. ABC-only
export `build/pasteboard-rest-binding-01` contains 8,396 lines / 146,649 bytes,
compared with 8,404 / 146,729 previously. All four Logger logging methods now
pass the native args parameter directly, without the intermediate rest-local
declaration or assignment. Other record source totals are unchanged.

Statuses remain eight incomplete source records, two refused constructors at
PC 14 and nine metadata records. Partial position maps remain 195 methods /
9,745 spans. No tests, generated JS execution or RSS checks were run; these
structural observations do not establish complete source equivalence.
