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

## Private Slot Operands

The pass is now named `FoldScalarValues` and also accepts acc/register reads
as producers. The producer still has no effects and is immediately adjacent
to the consumer, so no instruction can change the private slot before the
replacement read. RHS operands are evaluated before the destination commits,
including when source and destination use the same register. No heap, lexical
or module binding is substituted merely because it has a similar name.

Slot-to-slot consumer copies retain their old form for the subsequent paired
save/reload elimination. An initial export showed that prematurely rewriting
those copies hid existing reload pairs and increased Logger temporaries. The
final boundary admits slot values only into unary/binary/property expressions;
primitive-constant substitution keeps its previous admission rules. No new
storage, scalar operation kind or complexity class is introduced.

Both CLIs build and read-only review found no concrete correctness defect.
Final ABC-only export `build/pasteboard-scalar-values-02` preserves Logger's
text exactly compared with `pasteboard-scalar-constants-01`. ProgressBar now
prints direct `if ((!$r4))`, `typeof $r7` and `$r14 + $r4` expressions. Total
output falls from 10,074 lines / 170,817 bytes to 10,047 lines / 170,464 bytes.
Slot/destination aliasing, coercion ordering, copy-pair interaction and chained
boolean folds remain deferred execution regressions; tests, JS and RSS remain
paused. Generic names and labels still prevent a full source-style claim.

## Named Read Receivers

`FoldMemberReceivers` reuses the same proof for a MemberExpression whose
original instruction is NamedRead, whose pool key matches, and whose receiver
is acc. Only the adjacent pure slot/primitive producer is omitted. Getter,
Proxy and null/undefined receiver behavior stays at the original read PC.
Existing call-reference fusion is neither rerun nor relaxed.

The two public phases share a typed OperandForm implementation: scalar
operands run before boolean refinement, while member receivers run after
paired reload elimination and before direct stored-expression folding. An
initial ABC export demonstrated that running the member phase earlier hid
copy pairs and retained extra temporaries. The final ordering preserves those
existing simplifications, with the same linear scans/indexed lookup costs and
no new allocation.

Both CLIs build. Final ABC export `build/pasteboard-member-receivers-02`
contains 10,036 lines / 170,321 bytes versus 10,047 / 170,464 previously.
Logger's prototype read now uses `($r5).prototype` directly; the rest of its
body remains unchanged. PC anchors, throwing receivers, observed handler
values, copy-pair ordering and call receiver identity remain deferred runtime
checks. No tests, generated JS execution or RSS measurements were run.

## Eliminated Transport Gaps

The member phase now retains one producer candidate while scanning across
independently eliminated transports. Each skipped statement must be NoOperation
without fused origins, have no original language/exception effects, and be
either an original NoOperation or a pure scalar load/copy whose SSA output has
at most one use. Other statements reset the candidate. No instruction that
still executes, invokes a getter or changes the lexical environment is crossed.

For nonadjacent candidates, the completed CopyOrigins index must identify the
consumer input with the producer output, and the consumer's SSA input must have
one use. The producer retains its one-use requirement. Thus a removed save/
reload pair does not obscure identity, while exceptional phi observations,
escaping values and unknown phi identities still block the fold. The pass
never follows a copy chain per field: one forward scan with indexed lookups
uses O(1) new scratch and the existing immutable copy-origin table.

Both CLIs build and read-only review found no concrete defect. ABC-only export
`build/pasteboard-receiver-copies-01` retains eight incomplete application
records and two host-constructor refusals. Total source falls from 10,036 lines /
170,321 bytes to 9,899 lines / 168,540 bytes. Logger's four logging methods
read domain, prefix and format directly from `$r2` without intermediate acc
loads. This is receiver expression recovery, not original local naming.

Deferred checks additionally cover long removed-copy sequences, live or
handler-observed intermediates, mixed unrelated dead constants, producer
register redefinitions, original no-ops, loop/phi barriers, and source anchors.
Tests, JS execution and RSS remain paused; no runtime-equivalence claim is made.
