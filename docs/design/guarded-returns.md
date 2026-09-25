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
