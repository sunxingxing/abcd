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

## Direct Condition Expressions

After predicate refinement, `FoldBranchExpressions` admits the last two
instructions in one normal block when the first still assigns a pure boolean
scalar expression to acc and the second tests that value. Both original IR
and AST must retain the admitted scalar shape. Original language effects and
exceptional completion must be absent. Exact SSA use count must be one,
including handler phis, and the branch must consume that same value. Existing
fused origins are excluded. Throwing comparisons remain separate statements.

The branch's typed optional input owns the scalar expression. The old producer
becomes NoOperation; the branch keeps its target and predicate polarity. Use
counts remain conservative, so later DCE retains the expression's dependencies.
No local binding, allocation, call or property access is moved. The block scan
adds O(blocks) plus indexed SSA lookups and O(1) scratch; the enlarged typed
statement size remains charged by existing AST capacity admission.

The producer PC aliases the condition's generated span through the existing
fused-origin mechanism. Ordinary conditional breaks/continues and structured
if headers now share that relocation operation. Root-prefix emission keeps the
producer's zero-width anchor until the consuming branch relocates it.

Both CLIs build and read-only review found no concrete defect. ABC-only export
to `build/pasteboard-branch-expressions-01` retains the same eight incomplete
application records and two constructor refusals. ProgressBarBuilder emits
`if (($r5 === $acc))` without the intermediate comparison-result assignment.
ProgressBar entry PCs 46 and 49 both map to byte interval [3555,3571), line
223 column 0 through line 224 column 0 in the generated record file.
This demonstrates one entry-position alias, not complete nested-method maps.
Deferred checks also include fused-condition PC aliases, root-prefix splitting,
loop continues, multiple consumers and handler-visible accumulator values.
Tests, JS execution and RSS remain paused.
