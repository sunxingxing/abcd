# Named Store Receiver Recovery

FoldStoreReceivers runs after stored-expression folding and before identity-
based reload elision. It removes a single-use private register copy used only
as the receiver of a named store. The current typed scalar RHS becomes the
store's receiver; the Set, key and value remain at the original store PC.
Empty-array allocation is excluded. This does not prove an own property or
turn an ordinary Set into a class-field declaration.

Within each basic block a forward scan remembers only the previous emitted
statement. It may cross independently eliminated NoOperation anchors only
when their original effects are empty/nonthrowing and no fused/previous/class
origins exist. Consequently no emitted operation can mutate the substituted
private slot between its previous read and the store's receiver evaluation.

The producer must remain a scalar assignment to the same register as its
original IR definition. The original NamedWrite must match the receiver/key,
and the SSA definition must have one use, consumed by that store. Original
use counts include exception-handler phis and remain conservative after
earlier passes. The removed copy keeps its zero-width PC anchor. Later reload
elision resets at that anchor and cannot rely on the removed commit.

The algorithm is O(I) with bounded SSA reads per instruction, O(1) additional
storage, and no repeated backward/suffix search. No source-text matching or
untyped alias expression is introduced. ScalarNames remains restricted to
identifiers: ABI this recovery needs a separate typed binding proof, not the
string `this` in an identifier table.

## Evidence And Limits

Both production CLIs built successfully; read-only review found no concrete
defect. ABC-only export `build/pasteboard-store-receivers-01` removes one
receiver transport from paramsGenerator_ assignment in each of Home, PageA,
PageB, PasteBoard, ProgressBar and ShareDitect. Combined output is 9,718 lines /
165,098 bytes. Eight incomplete app records and two host-constructor refusals
at PC14 remain. No runtime-equivalence claim follows from this export.

Logger's copies retargeted by the preceding expression pass are deliberately
not handled: their current destination differs from the original SSA-producing
instruction. Chained receiver/accumulator recovery needs origin-aware proofs;
this pass does not guess across that mismatch. Complete variable recovery and
nested final-file PC positions remain unfinished.

Tests, generated JS execution and RSS measurement remain paused. Deferred
regressions cover accumulator/register/constant receivers, effectful barriers,
handler phi uses, overwritten inputs, merged origins, cross-block boundaries,
retargeted producers, subsequent reload elision and 200/201 scaling.
