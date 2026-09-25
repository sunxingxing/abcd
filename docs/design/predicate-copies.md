# Private Copies in Predicates

The shared AST pipeline now folds single-use private register copies into
already recovered branch predicates. This is local copy propagation using the
original SSA def-use graph, not an expression-text rewrite. The predicate may
be a scalar, unary or binary expression, or a typed constant-pool string
comparison. Property reads are excluded from this substitution visitor.

`FoldPredicateCopies` runs after branch/stored-expression/receiver folding and
before known-reload elimination. A forward scan within each basic block keeps
the previous emitted candidate. Only independently removed pure NoOperation
anchors, or its exact adjacent retargeted register-save anchor, may intervene.
Effectful instructions and all other merged origins stop the scan.

`PrivateCopyOutput` is shared with field receiver recovery. It resolves either
a direct original register definition or the exact save definition after a
retargeted accumulator expression. Both the intermediate accumulator and final
register definitions retain single-use checks where applicable. Counts include
handler phi observers. The final register must be read by the original
predicate instruction immediately preceding the branch, identified by the
existing fused-read PC/distance contract. Exactly one typed predicate operand
must match; rewriting is committed only after all checks succeed.

Only private slot reads and primitive constants move. Allocation, property
access, coercion, calls and branch evaluation do not move. No normal or
exceptional observer can consume the removed register commit. The original
copy keeps a zero-width PC anchor, retarget anchors continue referring to it,
and predicate/branch mapping metadata remains unchanged. Reload elimination
cannot infer a surviving accumulator write from the removed statement.

The pass performs O(I) work with bounded operands and O(1) extra storage,
using existing indexed SSA views. It does not perform repeated backward scans
or allocate a per-variable graph. No original CFG, SSA or semantic IR is changed.

## Evidence and Deferred Checks

Both production CLI targets built successfully; read-only review reported no
concrete defect. ABC-only export `build/pasteboard-predicate-copies-01` removes
the three comparison copies in CommonConstants.GetValue and two copies in each
of the six published page records. Combined source is 9,510 lines / 162,568
bytes, fifteen lines fewer than `build/pasteboard-retarget-receivers-01`.
Eight incomplete app records, two constructor refusals at PC14 and nine metadata
records remain. Method maps retain 195 method entities / 9,745 spans.

Tests, generated JS execution and RSS measurement remain paused. Deferred
regressions include direct and retargeted copies, overwritten accumulator
inputs, handler-phi observation, effectful/no-op barriers, multiple predicate
occurrences, loop conditional continues, staged argument consumers and fused
PC positions. Export evidence alone does not prove runtime equivalence,
complete method coverage, source-style completion or memory-cap compliance.
