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
