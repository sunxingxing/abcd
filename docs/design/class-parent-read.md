# Native Class Parent Reads

The common AST now carries an optional GlobalExpression as a class's parent
read. This recovers source heritage such as `extends ViewPU` from a proved
temporary snapshot, without text rewriting or a ViewPU-specific case.

## Proof And Emission

FoldClassParentReads runs after existing retargeting and argument ownership
passes. It considers only the same-block three-instruction shape: global name
read, private accumulator-to-register save, class creation. The global read
must already be retargeted to the exact parent register and its save must be
the existing no-op origin anchor. RetargetedOutput verifies the original
accumulator definition and pure save. The saved register definition must have
exactly one remaining SSA use, the class parent read; counts include phi uses.
Phased SSA, catches, cross-block argument owners and merged origins are excluded.

No application effect separates the lookup and creation. The global read
executes once in native extends; only its private register commit disappears.
The printer validates the global identifier against the enclosing source
scope and the class adapter rejects shadowing by the class's own inner name
or generated wrapper names. A distinct NamedClassParent type distinguishes
this possibly throwing source read from an initialized local snapshot.
It carries no JSFunction/non-Proxy lifetime premise and does not relax any
constructor-body or superclass checks.

The erased load/save retain their original PC anchors. The class statement's
typed fused-origin coordinates relocate both to the complete emitted class
expression. This is a broad expression span, not a claim that the parent
identifier alone has a separately recorded range. Nested method mappings
continue through the existing class expansion buffer. No JS text is parsed
to reconstruct provenance.

The pass makes one constant-window check per instruction: O(I) traversal,
plus existing SSA lookups, constant scratch and no retained allocation beyond
the optional field included in sizeof-based AST admission. The temporary
validated parent name is charged alongside class-emission storage. No new
dependency or eager body loading is introduced.

## Remaining Work

This does not yet turn a stored class expression into a source declaration,
recover lexical-cell names, or replace constructor captures with native this.
Longer parent-expression chains, imports/member parents, intervening effects
and inner-name collisions require their own admission/fallback proof; current
unsupported native-name cases may still refuse source instead of simplifying.
Do not generalize by deleting register assignments from printed source.

Tests, generated JS execution and RSS measurement remain paused. Deferred
regressions: different parent registers, extra/phi uses, impure or nonadjacent
saves, getters and thrown lookups, inner-class/global/formal name collisions,
unknown heritage, direct/forwarding/spread constructors, exact PC anchors,
budget limits and 200/201 scaling. Full application equivalence remains open.

## Production Evidence

Both production CLI targets built. ABC-only pasteboard export changes all six
page classes to `extends ViewPU`; Home now contains
`$acc = (class Index extends ViewPU {` and no preceding `$r7 = ViewPU`.
The class is still an expression assigned to the accumulator. Other remaining
register declarations are not removed unless the slot census proves them dead.

The checked export replaces out/pasteboard-latest. Publication remains eight
incomplete sources, two refused constructors and nine metadata-only records.
Position counts remain 195 methods / 9,745 spans; counts alone do not establish
exact range correctness. Read-only review found no concrete correctness,
origin, scope or budget defect. No tests, JS execution or RSS tracking ran.
