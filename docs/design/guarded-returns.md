# Returns Across Proven Guards

FoldReturnExpressions can now cross a contiguous suffix of already-eliminated
SuperCheck nodes before a return. BuildSourceAst must have changed each guard
to an unmerged NoOperation after its whole-constructor or Defined value/capture
proof. Only RequireInitialized and Noop kinds are admitted; RequireUninitialized,
emitted guards, writes, catches and phased SSA are excluded from this extension.
This does not alter superclass or host execution premises.

Every guard and the return must read exactly the candidate accumulator SSA
definition, whose total use count equals those reads, including phi consumers.
The candidate must still be a stored accumulator assignment without object
initialization metadata, and retain the existing expression-origin admission.
No effectful instruction, binding check or observable accumulator write moves.
The return expression remains at its producer; redundant guard and return PCs
share its emitted span through adjacent previous_origin anchors. Existing
capture-domain requirements remain part of the returned expression.

Each block's return suffix is scanned at most twice; suffixes are disjoint.
For I instructions and U indexed SSA reads, work is O(I log(I + U)), with
constant scratch, no allocation and no new dependency. This uses the existing
SSA/AST/printer pipeline, not constructor-name matching or generated-text edits.

Tests, JS execution and RSS remain paused. Deferred regressions include
consecutive guards, uninitialized/emitted checks, mismatched SSA operands,
extra normal/phi uses, capture requirements, merged origins and throwing
producers. Build/export evidence alone cannot prove complete source equivalence.

## Production Evidence

Both production CLIs build. ABC-only export `build/pasteboard-guarded-returns-01`
folds the final return transport in all six page constructors, retaining their
actual register or lexical return values rather than inventing `this` aliases.
Source totals become 5,080 lines / 99,870 bytes, down six lines / 78 bytes.
PageA constructor 7201 PCs 250 (load), 252 (guard) and 255 (return) all map to
bytes [3657,3669), `return $r2;` including its newline.

Partial maps retain 195 methods / 9,745 spans. Statuses remain eight incomplete,
two host constructors refused at PC 14 and nine metadata-only records. Read-only
review found no concrete defect. Tests, JS execution and RSS were not run.
Complete constructor/source-style recovery and full semantic equivalence remain
unfinished; neither the guard contract nor superclass admission was widened.

## Shared Guarded Commits

IsElidedInitializedGuard now centralizes the same proof-consumption contract
for returns, receiver reads and stored expressions. FoldStoredExpressions may
cross a contiguous sequence of these guards to a register or admitted lexical
cell save. Each guard and the save must read the exact producer SSA value;
its use count must equal their total, including phi consumers. The original
RHS and capture obligations stay at the producer, and the destination commits
only after successful evaluation. Other expression/origin restrictions remain.

Only emitted assignments start guard scans, so candidate intervals are disjoint;
the existing field-value scan and this guard scan are bounded constant passes.
No scratch allocation or new dependency is added. Guard and save PCs retain an
adjacent previous_origin chain. Downstream copy proofs still reject unsupported
non-adjacent definitions rather than inventing an identity or deleting live uses.

Production builds and ABC-only export `build/pasteboard-guarded-stores-01`
succeeded. Six pages change; totals become 5,020 lines / 99,140 bytes, down
33 lines / 445 bytes. PageA constructor 7201 PCs 183/185/188 share the direct
save span [3422,3434), `$r10 = $r2;` including newline. Partial maps retain
195 methods / 9,745 spans with the same eight incomplete, two refused and nine
metadata-only statuses. Read-only review found no concrete defect. Tests, JS
execution and RSS remain paused. Deferred checks include lexical commits,
multiple guards, self-copies, extra uses and downstream non-adjacent-copy
interaction. Complete field recovery and equivalence remain unfinished.

## Saved SSA Identity

RetargetedOutput now shares saved-definition recovery between argument admission
and private-copy consumers. An exclusive bound supplied by the caller prevents
crossing its block or consumer. From the original accumulator producer, it
checks each guarded previous_origin link, exact SSA read, and the total guard
plus save count. The final node must anchor a pure original accumulator-to-the-
requested-register save. The returned identity is that save's SSA definition,
not the producer's accumulator value. No SSA or use count is rewritten.

The initialized-guard helper accepts an explicit expected previous origin for
this already-folded chain; default absence preserves its prior callers' strict
unmerged contract. Field-receiver folding can traverse these anchors but still
requires the saved register's sole consumer to be the original named write.
Other copy consumers retain their original effect and operand restrictions.
Producer intervals and ordered argument targets bound scans to disjoint chains;
the indexed SSA cost and constant scratch requirements are unchanged.

Production builds and ABC-only export `build/pasteboard-guarded-commits-01`
succeeded. Five page constructors now write paramsGenerator_ directly through
the original receiver register instead of a temporary. Totals become 5,015
lines / 99,075 bytes, down five lines / 65 bytes. Maps retain 195 methods /
9,745 spans, with eight incomplete, two refused and nine metadata-only records.
Read-only review found no concrete defect. Tests, JS execution and RSS remain
paused. Deferred checks include non-adjacent argument commits, malformed origin
chains, mismatched counts, end boundaries and multi-use saved definitions.
