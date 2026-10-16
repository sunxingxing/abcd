# Standalone for-of advance and key-definition evidence

This is Increment C part 2's standalone slice. It does not change production
planning, exception control, or the `kProtocolEvaluationPending` gate. It is not
ForOfPlan/ForOfEvents and cannot authorize source publication.

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
occurrence order; the full ForOfPlan must additionally certify the dynamic
callee/receiver identity of aliases across acquisition epochs. The count
query alone must not be used as that value-identity proof.

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

ForOfEvents, the common evaluation/temporary/phi disposition, its independent
verification, and the tree correspondence gate remain to be implemented.
No iterator/next/result source symbol is authorized by this standalone API.

## Tests

The real compiler fixture's always-abrupt loop and all four target methods
exercise the proof in tests/probes. Unit variants include register relocation,
NOP/block splitting, 200/201/400/800 instruction padding, exact byte/work
boundaries, wrong advance/done/value identities, changed/deleted/duplicated
Commit evidence, and wrong original phi-edge evidence when present. These
integrity tests do not replace the remaining original-protocol mutation suite
or native-source end-to-end acceptance.
