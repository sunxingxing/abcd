# Standalone for-of advance and key-definition evidence

This covers Increment C part 2 and the standalone reference/event slice of part 3. It does not change production
planning, exception control, or the `kProtocolEvaluationPending` gate. It is not
a complete ForOfPlan and cannot authorize source publication.

## Input and algorithm

`ProveForOfIterations` requires the immutable original phased input, B's sealed
close ownership/control, and the checked ForOfIdentityIndex. It reconstructs
B's projection with ordinary body nodes and completion-specific close ports.
Natural loops are rediscovered by the shared loop forest. No opcode sequence,
register, PC, or block-shape admission is used.

A finite forward must-property query starts at each actual iteration header.
Its correlated stages are unadvanced, advanced, and value-read. The next call
can be entered only unadvanced; the done decision only advanced; the value
read only advanced. Body-protection ordinary nodes must be value-read. Every
backedge for this loop must return to the same header in value-read state;
its next logical iteration restarts unadvanced. Exhaustion must leave the loop
and is a cut, so it cannot define a success binding. Nested ordinary loops do
not reset this state. A pre-advance cycle is refused, rather than treating
read-free divergence as a control-elimination proof. Normal exits/terminals
and possible exceptions before advance are refused too; an unknown throwing
header event is not assumed inert.

A separate activation query propagates absent/fresh/cached states from the
real projection entries. Only acquisition Commit resets fresh. The next read
requires fresh, its normal Commit establishes cached, and advance requires
cached. A second read without a new acquisition is rejected. This certifies read/call
occurrence order; the separate reference closure below supplies the operand
identity proof. The count query alone must not be used as that proof.

Each actual next-base, next-call callee and receiver UseId is traced backwards
to its normal defining Commit. Copy definitions switch to their physical input;
phi definitions switch to every reachable original incoming edge/value. Crossing
a new acquisition while carrying an older definition refuses; cached-next
queries also stop at the next definition. Root definitions are the exact SSA
results of acquisition/next, not canonical SCC aliases. All finite paths must
be accounted for and the actual root must be reached. Original SSA availability
and B ownership are independently rechecked first. This closure has a shared
prepaid sparse state and edge-visit allowance, so dense propagation refuses
rather than producing a partial identity proof.

The result records the real SSA Commit definitions of the extracted value and
its copies reached in value-read state. Exact SSA operands are grounded at the
current value read; static SCC canonical identity alone is insufficient. Phi
transport requires all physical inputs to be grounded in that iteration; an
ungrounded cycle stays a typed missing binding proof. Post-loop copies stay ordinary. It
also retains every original input edge/value of affected phis (including the
non-key input), following copy/phi transport transitively. No ExprId, source
variable or source assignment is fabricated. Exception edges bypass Commit;
that correspondence is independently rechecked with the immutable import/B.

`VerifyForOfIterations` rediscovers the graph and facts, processes the sparse
closures in reverse worklist order, and compares the complete result. Producer
count states and claimed definitions are never premises.

## Accounting

PMR scratch is charged before allocation. The two finite-state queries and
copy/phi lineage have bounded method-wide sparse pair arenas. Query-edge
visits are bounded too, so high-fanout SSA hubs cannot hide quadratic
propagation behind a node-only allowance. Exhaustion
returns a typed resource refusal, not a partial result. Each pair changes a
constant number of times; ordered maps and constant-time loop ancestry
intervals give O((V+E) log V) admitted work. Dense output/query relations which
exceed the prepaid allowance refuse. Output capacities remain charged while
all scratch is alive.

## Publication obligations still open

The recorded definitions are success-qualified **scalar transport evidence**.
They do not choose the source loop variable. That choice must still prove the
real declaration, TDZ/write availability, activation and zero/last/break
live-out against these original phi relations. The new live-out fixture
currently fails B ownership and stays refused. This slice does not claim that
live-out native binding recovery has passed.

`ForOfEvents` now retains acquire, cached-next read, next-call, object-check,
done-read/decision and value-read Before/Commit/original/exception identities.
Physical value commits remain distinct from edge-qualified success and exhaustion
and from B's correlated completion ports. Its verifier recomputes the original
iteration/reference/ownership proof and every event; a mask is never a premise.
This is standalone correspondence, not selection of a source binding.

The common evaluation/temporary/phi disposition, retained original-input ownership
for its independent verifier, and the real tree correspondence gate remain to
be implemented. The production evaluation gate has deliberately not moved.
No iterator/next/result source symbol is authorized by this standalone API.

## Tests

The real compiler fixture's always-abrupt loop and all four target methods
exercise the proof in tests/probes. Unit variants include register relocation,
NOP/block splitting, 200/201/400/800 instruction padding, exact byte/work
boundaries, wrong advance/done/value identities, changed/deleted/duplicated
Commit evidence, and wrong original phi-edge evidence when present. These
integrity tests do not replace the remaining original-protocol mutation suite
or native-source end-to-end acceptance. Reference/event mutations additionally
cover swapped callee/receiver, a missing exception exit, wrong Commit, a success
commit moved to exhaustion, a forged completion port and altered UseId roots.
Event construction has exact byte/work boundary tests on all scale variants.

## Increment C part 4 wiring (acceptance incomplete)

PhasedBody now retains the sealed ownership and a prepaid copy of the matching
MethodBody. Its original SSA/flow and ownership are passed by an explicit
ForOfEvaluationInput to the common phased evaluator, including the family-tree
call site. EvaluationPlan retains only owned evidence, not borrowed pointers.
The independent evaluation verifier requires the original view and rebuilds
identity, iterations/references, events and dispositions before using any mask.
For-of phi/catch-copy consumers require the checked use view explicitly; an
input list without that view is never evidence. Existing non-protocol calls
retain their empty-view behavior.

The standalone real fixture reaches a full common evaluation plan. Production
m7171 reaches the located tree kForOfCorrespondence gate. The requested four
production gates are not yet achieved: m28799's ordinary staged literal plan
requires whole-CFG acyclicity; the record containing m39073 encounters m39263's control refusal during family
discovery; the record containing m40502 encounters m40670's ordinary control
refusal before m40502 reaches the tree stage. No child binding is invented and no ordinary proof is skipped
to change those leaves. Source publication remains impossible for this path.

## Slice II-b: unpublished production draft and TreeFlow

`ForOfEmissionView` interprets the immutable ProtocolOwned control. It retains
LoopIds, the original header, success/exhaustion edges and entry-qualified
close outcome ports. It never uses for-in's transparent exception forwarding.
The block number in B's compact control projection is a unit identity; source
emission/TreeFlow resolve the `PhaseControlUnit` to its real working block
instead of indexing ValueProgram with that unit number. Synthetic-node storage
is bounded against the actual unit prefix, not the unprojected block count.

The common forest/binding/call planners receive the original for-of input for
fresh verification. Protocol-owned iterator/next/result/close values have no
source binding or expression. A native assignment header binds the success-only
value to its checked scalar target; the existing declaration stays outside the
header. Ordinary body operations and phi/copy schedules keep their original
control identity. Exhaustion copies may be placed after the loop only if there
is no competing break exit, or if that schedule is empty; unsupported staging
is refused rather than executed on the wrong edge.

TreeFlow independently rebuilds the view and protocol proofs, checks the actual
RHS leaf against the original parameter/snapshot, checks unique protocol-member
ownership, native loop/exit targets and copy positions, and retains the typed
close port table. Exhaustion transfers are explicitly marked as implicit native
control, not attributed to an invented source break. Source syntax can contain
neither a second iterator acquisition nor a binding write on the exhausted
edge. These checks still do not replace the joint OriginalFlow/recovery proof.

`BuildForOfTreeDraft` returns a distinct unpublished type with TreeFlow but no
OriginalFlow. The production path runs the same builder/checker, then returns
`TreeObligation::kForOfRecovery` at the acquisition. `VerifyTreeSemantics` also
refuses a for-of plan until recovery support exists. Direct AST printing in the
unit/Node harness is not a publication receipt.

The m7171 draft and the new ordinary-loop fixture exercise this path. The
separate explicit break/return fixture still fails the pre-existing close seal
at `kRegionCoverage`, TryId 4, PC133; it is a strict boundary test, not positive
coverage for those source exits. This slice does not expand close ownership.

## Increment III: conditional value commits and effectful close recovery

A freshly checked `OriginalFlow` retains the ForOfEvents/iteration identities,
entry-qualified close ports and the actual native statement/binding/body. It
retains raw Before/Commit/exception identities and the original success and
exhaustion edges. It does not attach an ExprId to an iterator, cached next,
iterator-result object, close state or caught close payload. Ordinary body and
phi inputs still use the existing operand proof; successful value-read inputs
have a separate occurrence-to-binding correspondence.

Recovery constructs acquire, cached-next read, next call, object check, done
read/decision, value read and binding write/commit ports. Continue returns to the
next call, not acquisition or next-property read. Exhaustion bypasses value and
binding. A key definition is conditional on successful PutValue; the common
initialization analysis independently proves that the original declaration is
available before that write. Iteration inputs must read this binding after the
successful commit. Initial phi/copy verification and every trial flow retain
zero-iteration, last-iteration and live-out contributions.

The implicit close graph is effectful: return-property read, nullish decision,
return call and result check retain separate outcome ports. Throw entries keep
the pending payload even when close fails; successful/missing-return closes
keep non-Throw completions, while close errors replace them. These are checked
against B's outcome-indexed ports, not a transparent forwarding route. A close
failure exits the loop's exception context; it must not re-enter close and
close twice. Original private phases remain typed protocol correspondence.
No interior Catch is presented as a source catch binding or invented expression.
The immutable original proof is shared by initial and every recovery proposal;
protocol coverage, references, effects, exception edges, binding availability
and native continue/break edges are checked anew.

Only after initial OriginalFlow/recovery verification does the normal builder
return a publishable function. The syntax-only draft API remains distinct.
The binding planner's syntax-only refusal remains for trees without original
iteration evidence. Nested native completion transport and an enclosing source
finally remain explicit recovery limitations in this increment; the existing
close seal still refuses the explicit multi-exit fixture documented in II-b.
This increment does not widen close ownership or per-iteration binding admission.

Tests exercise m7171 and the real `for_of_draft/main.abc`: zero iterations,
last value, a throwing body, getter/call order and return() close priority. The
engine record output, rather than draft printing, is Node-checked and compared
with the independent source oracle. Recompiling this output with the pinned
DevEco es2abc re-runs acquisition/cached-next/receiver/iteration/close proofs.
As in captured-default round trips, only identifier-valued string export aliases
are unquoted at the compiler boundary (the pinned parser predates that ES2022
syntax); the emitted function text is unchanged. Raw Hole exceptions cannot be
represented in Node: the V2 acquisition and V4 external-exception premises remain
explicit. Recovery mutations cover exhausted-edge writes, wrong close ports,
missing exception evidence, forged next/receiver and a wrong continue, with
200/201/400/800 variants and exact work/byte boundaries.
