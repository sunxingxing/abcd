# Scalar Constant Operands

The common AST substitutes an adjacent primitive accumulator definition into
a scalar consumer when exact SSA proves that consumer is its only use. This
is local SSA constant propagation, not JS evaluation or constant arithmetic.
The whitelist is typed SpecialValue, IntegerLiteral and Float64Bits; strings,
arrays, objects, calls, environment values and unknown definitions are excluded.
Original producer effects must be empty and completion must be normal.

Both statements must remain ordinary scalar assignments in one normal block,
without fused origins. The consumer's acc operands are replaced directly in
ScalarValue, UnaryExpression, BinaryExpression or PropertyRead. The effectful
consumer stays at its own PC and retains its operand order. The eliminated
constant load keeps a zero-width PC anchor. Handler phi uses participate in
the single-use check, so an exception observer prevents removal of a live acc
commit. Constant payloads are copied exactly, including floating-point bits.

Original SSA use counts remain conservative. A subsequently deleted original
copy may decrement the removed producer's count, but the producer is already
a NoOperation and cannot trigger deletion of additional dependencies. Reload
identity elimination checks AST/original inputs and stops on a substituted
constant. Branch-expression folding can consume the modified scalar without
another interpretation of its printed text.

The pass scans I statements with bounded scalar operand counts. SSA lookup
cost is O(I) with phased indexing and O(I log R) with normal indexed reads R.
Extra scratch is O(1); there are no new tables, allocations or dependencies.

## Evidence And Limits

Both production CLIs build; read-only review found no concrete defect.
ABC-only export to `build/pasteboard-scalar-constants-01` retains eight
incomplete application records and two constructor refusals at PC14. The
combined sources contain 10,074 lines / 170,817 bytes. ProgressBarBuilder now
emits `if (($r5 === (void 0)))` through constant substitution and existing
branch-expression folding. Generic registers and control labels remain;
original names and full source-style recovery are not established.

Tests, generated JS execution and RSS remain paused. Deferred cases include
negative zero, NaN/infinity, all scalar operand positions, repeated acc reads,
throwing coercions and property access, handler-observed acc values, original
copy consumers removed by DCE, chained folds, PC anchors and byte budgets.
Production export is not proof of runtime equivalence.
