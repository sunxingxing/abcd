# Direct Stored Expressions

The common AST redirects a single-use accumulator expression directly into
the private register saved by the immediately following instruction. The RHS
remains at its original PC; its register destination commits only on normal
completion. The old save becomes a no-op sharing the producer's generated
span through `previous_origin`.

Both instructions must be in the same normal block. The producer must define
acc, and the save must still be a nonthrowing acc-to-register scalar copy in
both typed IR and source AST, with the same register destination. Exact SSA
identity must connect them; the producer has only that save use, including
exceptional phi inputs. No fused or special-origin statement participates.
This preserves the old destination if RHS evaluation throws and admits no
other observer of the removed acc commit.

`FoldStoredExpressions` also accepts an adjacent verified `SourceLocalStore`.
Its original IR must be a nonthrowing `LexicalStore` taking acc, and the AST
must still store acc. The destination is the existing certified `LocalCellId`,
never a raw environment slot or a guessed source name. The producer becomes
`SourceLocalAssignment` with the same expression and cell; all block, SSA-use,
origin and aggregate exclusions above remain in force. Cell initialization and
capture proofs are prerequisites, not inferred by this presentation pass.

## Member Values

The same block-local scan also removes a pure scalar producer immediately
before a named member store. The existing `SourceMemberStore.value` receives
the scalar directly; there is no new printer path. The source and original
producer must both be scalar-value loads (excluding EmptyArray), and original
normal/exceptional language effects must be absent. Receiver register and
pool key must match the original `NamedWrite`; its acc input must consume the
producer's sole SSA use, including handler phi observations.

Only private-slot reads and primitive constants move past receiver/key
evaluation. The effectful Set remains at its original PC, retaining ordinary
setter/Proxy and strict-assignment behavior. The producer becomes a zero-width
PC anchor; it does not acquire the store's effect or claim its generated span.
This is neither an own-property proof nor permission to replace constructor
assignments with class field declarations. String loads, allocation, property
reads, calls and conversions are not admitted by this fold.

The scan is O(I) plus the indexed SSA read lookup costs described below. Named
stores have bounded operand counts; there is no per-field scan or new storage.

## Pass Order

The pass runs after dead-copy and single-use adjacent save/reload removal,
but before identity-based reload elimination. This order is mandatory: a
previously elided reload could rely on the acc definition this pass removes.
The later identity pass rejects the changed target as a mismatch with original
IR, preventing an invalid acc-preservation inference. Original use counts
remain conservative; they are not recomputed by scanning source text.
The new local-assignment node also stops reload identity propagation and is
not admitted into comma-only argument/array folds.

Class, buffered-object, buffered-array and empty-array creation retain their
acc/save shape for subsequent aggregate folding. Other expression-owned
creation coordinates and capture obligations are unchanged. Existing array
element and call argument regions consume the resulting typed assignments.

For I instructions and R indexed reads, cost is O(I) with phased lookup and
O(I log R) with normal SSA lookup. Scratch is O(1), no additional heap table
or statement payload is allocated, and no per-variable graph walk occurs.

## Evidence

Both production CLIs build; read-only review found no concrete defect.
ABC-only export of unchanged pasteboard modules.abc to untracked
`build/pasteboard-register-assignments-01` retains eight incomplete app records
and the two rejected host constructors at PC14. Compared with
`pasteboard-member-access-01`, combined source decreases from 11,422 lines /
194,770 bytes to 10,463 lines / 181,937 bytes. Logger emits direct assignments
such as `$r7 = ($acc).domain` and `$r3 = args` through the shared AST.

The local-cell extension builds both production CLIs and received read-only
review without a concrete defect. ABC-only export to
`build/pasteboard-local-assignments-01` preserves the same eight incomplete
records and two constructor refusals. Combined source is 10,443 lines /
181,677 bytes. ProgressBar methods 10693 and 10639 now directly initialize
their certified cells from parameter/receiver registers, for example
`$inner10639_0_0 = $r10`. This is expression integration, not recovery of the
original variable names or completion of the globals/signatures gate.

The member-value extension builds both CLIs and passed read-only review.
ABC-only export to `build/pasteboard-member-values-01` retains eight incomplete
records and the same two constructor refusals. Compared with
`pasteboard-dead-primitives-01`, source decreases from 10,263 lines / 178,667
bytes to 10,241 lines / 178,381 bytes. Logger's constructor now emits
`($r5).prefix = $r3` and `($r5).domain = (65280)` without acc transport.
Deferred execution checks must cover throwing setters, Proxy receivers,
handler-visible accumulator values, receiver/RHS register aliases, primitive
receivers and zero-width producer positions. No execution or RSS claim follows
from this export.

Tests, generated JS execution and RSS measurement remain paused. Deferred
regressions include throwing RHSs and handler snapshots, destination aliases,
copy chains and pass ordering, aggregate folding, staged calls, captures,
source-span aliases and output budgets. All artifacts remain incomplete;
this does not establish full source-style recovery or semantic equivalence.

## Member Call Destinations

`CanRetargetExpression` centralizes the origin-preservation admission shared
by stored expressions and return expressions. An already-proved
MemberCallExpression may retain its fused read and complete spill/reload pair
while changing the result destination. Missing distances, partial pairs,
class origins and previous-origin aliases remain excluded. Ordinary unfused
expressions keep the existing admission rules. Property stores do not use
this relaxation, and staged-call nodes remain outside it.

The stored-expression fold still requires the same-block adjacent nonthrowing
register save or certified LocalCell store and the exact single-use SSA value,
including exception observers. Receiver/arguments are evaluated before target
commit, so a callback sees the old cell value until normal return. RHS creation
coordinates and origin spans stay attached to the producer; the old save
aliases its complete generated assignment span. No new allocation or scan is
introduced. Reload elision must continue to reject the changed acc destination;
a newly visible later acc reload can be required, not a regression to erase.

Both CLIs build and read-only review found no concrete defect. ABC-only export
`build/pasteboard-stored-calls-01` preserves eight incomplete application records
and the two host-constructor refusals. ProgressBar emits direct getSystemPasteboard,
getUIContext and getUriFromPath result assignments. Combined output is 9,869
lines / 168,134 bytes. Tests, generated JS execution and RSS remain paused;
deferred checks include receiver/target aliasing, captured-cell reentry, throwing
calls, handler snapshots, fused span aliases and required post-call reloads.
