# Boolean Branch Recovery

The common source AST refines VM false-or-zero and true-or-nonzero-number
branch predicates only when the exact SSA input has a boolean-producing
definition. The existing CopyOrigins index crosses pure copies without
duplicating a walk per branch. Entry, phi and catch identities remain unknown.
Normal/phased SSA are consumed through the same InstructionSsa facade.

Admitted producers are true/false constants, boolean conversion/not, the two
boolean-valued branch tests, comparisons and `in`. Arithmetic, numeric/bitwise
conversion, typeof, property reads and other expressions are not proofs.
Only a producer's normal committed result is considered: a throwing comparison
retains all its evaluation and exception effects at its original instruction.
No expression, operand, edge, label, handler or source PC is moved.

The source branch becomes LogicalNot or ToBoolean. The shared printer knows
it is printing an if condition, so it can omit ToBoolean and combine logical
not with the region's inversion. Unknown VM predicates still print their full
numeric/boolean test. This is a typed transformation, not source-text rewriting.
All output continues through the position cursor and byte budget.

The pass scans I statements with at most one SSA operand query per branch B:
O(I+B) for phased lookup, O(I+B log R) for normal indexed reads R. It uses O(1)
new scratch and the already-built copy index. It runs before DCE and later
expression folds; original SSA and use counts are unchanged.

## Evidence And Limits

Both production CLIs build and read-only review found no concrete defect.
ABC-only export to `build/pasteboard-boolean-branches-02` preserves eight
incomplete application records and both host constructor refusals at PC14.
ProgressBarBuilder's strict-undefined comparison now controls `if ($acc)`;
its later logical-not result also uses a plain boolean branch. Other pages
receive the same common-path refinement. Generic labels and intermediate
register values still remain, so this is not complete control-flow recovery.

Tests, generated JS execution and RSS remain paused. Deferred regressions
include numeric/NaN/BigInt/object inputs that must retain VM tests, copy chains,
unknown and boolean-only phi inputs (both conservatively retained), throwing
comparisons with handlers, loops, region inversion, source positions and
resource limits. This export is not runtime-equivalence evidence.
