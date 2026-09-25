# Return Expressions

The common AST folds an adjacent accumulator assignment and return into a
typed `SourceReturnExpression`, keeping RHS evaluation at its original
instruction position. It is a local single-use expression fold, not text
rewriting or movement across effects.

Admission requires both statements at the end of one normal block, a return
of acc, an assignment to acc, no already-fused/special origin metadata and
exact SSA definition/use identity. The assigned value must have only that
return use, including handler phi uses. The private acc commit can then be
removed without changing an observed register snapshot. The original return
becomes a no-op with `previous_origin` referencing the producer's original PC.
Both PCs map to the generated return expression. Existing control regions
continue to use the unchanged CFG terminator and edges.

The pass runs after array and class folding. Expression-owned creation sites
and capture obligations survive the move into the new typed AST alternative.
Record entry emission still rejects expression returns; this does not grant
new top-level return semantics. Effects, callee/reference evaluation, source
contract and constructor admission are unchanged.

For B blocks and R indexed reads the pass costs O(B) with phased lookup and
O(B log R) with normal SSA lookup; it uses O(1) scratch and no new heap tables.
AST storage uses the existing statement-variant capacity accounting. The
original SSA use counts remain conservative after earlier copy elimination.

Both production CLIs build and read-only review found no concrete defect.
ABC-only export of unchanged pasteboard modules.abc to untracked
`build/pasteboard-return-expressions-02` retains eight incomplete app records.
Compared with `pasteboard-copy-identity-01`, text decreases by 25 lines and
325 bytes. Debugger inspection of actual ProgressBar child 10611
(`paramsLambda`) yields `return ({});` with five retained spans; original PCs
6 and 7 both map to bytes [0,13), line1/column0 through line2/column0. These
are template-relative positions, not completed nested final-file mapping.

Tests, generated JS execution and RSS measurement remain paused. Deferred
regressions include normal and phased single-use returns, handler observations,
block-boundary refusals, fused calls, function/class captures, async returns,
constructor results, PC aliases and output budgets. All artifacts remain
explicitly incomplete; this does not establish full semantic equivalence or
source-style completion.
# Member Call Results

An already-proved MemberCallExpression may now become a return expression
without dropping its fused property-read/spill/reload origins. Admission
requires a read origin with nonzero distance and either no spill/reload pair
or both with nonzero distances. Class/previous origins remain excluded. The
existing same-block adjacency and exact one-use SSA proof (including handler
phis) remain mandatory. Staged calls are a separate node and are not admitted.

The call expression, receiver and arguments stay in the original head node;
only its private acc assignment becomes return. Unified origin relocation
therefore maps its existing origins to the return-call span, and the original
RETURN statement continues to alias that span through previous_origin. No
getter/call proof is repeated or weakened, and no additional storage or graph
traversal is introduced.

Both production CLIs build; read-only review found no concrete defect.
ABC-only export to `build/pasteboard-return-calls-01` preserves eight incomplete
application records and two host-constructor refusals at PC14. ProgressBar's
curBp and progressInfo getters now emit `return ($r5).get()` instead of writing
acc and immediately returning it. Nested final-file PC maps remain incomplete;
this export does not claim to verify all nested alias positions. Tests, JS
execution and RSS remain paused. Deferred checks include getter/call throws,
handler snapshots, receiver identity, source-span aliases and output budgets.
