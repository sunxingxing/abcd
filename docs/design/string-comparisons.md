# String Comparison Expressions

The shared source AST now represents strict comparisons against a constant-
pool string with StringComparison. Its equality kind is typed; the string
remains a PoolIndex until the printer resolves and escapes it. PredicateInput
admits either the existing ScalarExpression or this string comparison, so
assignment, return and conditional output use the same expression semantics.
This does not add textual operand tags or parse generated JS.

FoldStringComparisons runs before branch-expression folding. Within one basic
block it checks adjacent string-load/comparison pairs: original pool index and
strict operator must match the AST, the load must be pure and nonthrowing with
an accumulator result, and that SSA definition must have exactly one use,
consumed by the comparison. Use counts include handler phis. Both statements
must lack merged origins. The comparison's RHS is acc; its LHS cannot be acc or
an allocating EmptyArray. Only a private slot/primitive read moves before the
constant string. Strict equality introduces no coercion effects.

The original comparison PC remains. The string-load instruction retains a
zero-width anchor. Subsequent branch folding still requires a pure boolean
producer, an exact single-use SSA result, a matching boolean branch and no
normal/exceptional observer of the removed accumulator commit. Both comparison
and branch PCs then share the condition span through the existing origin
mechanism. No labels or control-flow regions are inferred from string content.

This pass is O(I) for I instructions, with bounded SSA reads per instruction
and O(1) additional scratch. Added AST storage is inline and covered by existing
sizeof(SourceStatement) capacity admission. Loose comparisons, coercions,
cross-effect motion and arbitrary non-adjacent string propagation are not
introduced by this change.

## ABC-Only Evidence

Both production CLIs built successfully and read-only review found no concrete
defect. Export `build/pasteboard-string-comparisons-01` prints direct strict
string tests in CommonConstants.GetValue and the page records. Combined source
is 9,665 lines / 163,742 bytes, removing 31 accumulator-transport lines from
the preceding export. The 195 method entities / 9,745 spans remain present,
with endpoints regenerated for the changed text. Eight incomplete application
records and two host-constructor refusals at PC14 remain. This does not prove
runtime equivalence; the generic next_* control-flow labels are unchanged.

Tests, generated JS execution and RSS measurement remain paused. Deferred
regressions include equal/not-equal cases, coercive operator exclusion, both
operands using acc, handler-phi observers, merged origins, empty/Unicode/lone-
surrogate strings, assignment/return/branch rendering, source positions and
200/201 scaling. Full source-style recovery and runtime equivalence remain
unproven.
