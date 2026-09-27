# Exception Control Projection

This is the T09 control step of the common source pipeline. It is not an
alternative body writer, a source publication receipt, or completion of group 1.
The original phased `ValueProgram` and `ExceptionPlan` remain authoritative.

## Same-File Module Context

`PhasedMethod::Build` optionally borrows a `PhasedModuleBinding` during construction.
The existing alias binder verifies phased guards, record ownership, metadata slots
and exact decoded guard names before evaluation receives module symbols. The owner
retains immutable aliases, not borrowed metadata. Syntax names may be rebased when
appended to the shared arena; `VerifyModuleAliasTree` independently checks identity
and spelling after tree construction. This does not prove record initialization or
authorize standalone module publication; the existing publication gates remain.

The selected Immersive method 8284 now passes this bound phase/evaluation path and
shared bindings. Copy identity projection now resolves its callee to the property
read. It still fails `CallPlanError::kReference`: that read is a materialized root
at phase block 18, while the call is at block 28. Do not infer receiver-call support
from copy resolution alone. The next dependency is verified cross-phase reference
evaluation with original exception snapshots and source origins preserved. Loops, typed finally and resumable protocols
remain separate unfinished steps of the approved common pipeline.

## Pure Evaluation Elision

Rules 47 add a source projection without editing the imported phase graph. The
normal pipeline retains its existing verified IR simplification. For phased input,
`ElidePureEvaluations` marks dependencies backwards from all non-erasable operations
and all phi inputs. It reuses `CanErasePureOp` from the normal simplifier: only the
explicit pure-operation allowlist with no normal/pre-throw effects or exceptional
completion qualifies. Getter/coercion/call/environment/control events are retained.

`kElidedPure` differs from a removed IR operation. Independent evaluation checking
requires each elision to be pure and every result use to belong to another elision;
a phi consumer is forbidden. This checks closure locally instead of replaying the
builder. All phi inputs are conservative roots because existing normal and catch
copy plans still execute them. Dead phi elimination is not claimed. No CFG edge,
scope, Before/Commit/Catch node, pending exception or SSA identity is changed.

The shared binding and ABI census ignore only operand reads owned by these freshly
verified elisions. Actual ABI reads still refuse unsupported callable/new-target
entries. The source tree emits no expression or declaration for the omitted work,
but retains one checked original-PC anchor at its lexical statement boundary.
Flow checking verifies that boundary; the body origin skips the omitted prologue.
This is not a replacement source writer or a text cleanup pass.

The mark queue visits each operation at most once and each operand a bounded
number of times: O(I+U) work and O(I) scratch, where I is operation count and U is
operand occurrences. Independent closure checking is O(I+U) via result-use links.
The implementation adapts the repo's `value_simplify.cc::MarkLive` strategy without
copying external code or adding dependencies. Existing phase/tree verification has
its own separately charged cost; these bounds do not claim the entire pipeline is
linear. Tests cover 200/201 and geometric chains, precise work exhaustion, retained
getters/returns, live ABI reads, catch snapshot dependencies and misplaced anchors.

## Copy Identity Projection

Rules 48 add flat `EvaluationPlan::aliases` and `kAlias`, distinct from dead pure
elision. The original graph, input correspondence and phi/catch copy schedules stay
unchanged. Only effect-free, nonthrowing one-input `ValueCopy` operations with a
ValueId input qualify. Literal producers remain operations. The source domain must
be a subset of the copy's certified domain; projection cannot weaken a refinement.

The first copy from a phi or catch definition remains materialized. A phi is a
mutable edge-copy destination, and a catch parameter is scoped to its handler;
rewriting their consumers to the raw binding would require different scheduling or
scope proofs. Subsequent copies may alias that retained snapshot. Unsupported ABI
entries cannot become source parameters or undefined literals through this table.
Parameters/receivers and retained operation results are the admitted root kinds.

The builder uses functional-graph path compression adapted from the existing
`value_simplify.cc` implementation: O(V+I) work/storage, no per-use chain traversal.
An independent verifier checks each local copy equation, identity for non-alias
values, flat roots and operation ownership after original availability/use checks.
The latter establish acyclic copy dependencies; no phi is traversed. The table is
owned by the immutable method evaluation plan and its capacity is budgeted.

Bindings, expression operands, parallel/catch copy RHS leaves, ABI observation and
source operand correspondence all resolve the same checked identity. Effects stay
at their original operations, and each alias PC remains a checked boundary anchor.
Call discovery can now identify a property behind copies; it still cannot emit a
member call unless the independent reference/order conditions pass. This change
does not authorize `.call`, re-reading a getter or moving a getter past arguments.

## Inputs And Output

`BuildExceptionControl` first verifies the original body, phase import, and
exception-region plan. It then constructs an explicit topology projection and
uses the existing acyclic postdominator/region discovery. The projection is a
`ControlTopology`, not a copied `ValueProgram` with its phases removed.

`ExceptionControl` owns a shared `ControlPlan` skeleton and typed coordinates:

- `PhaseControlUnit` refers to an original Before, Commit, or Catch block.
  Original blocks retain their index prefix; no phase is merged or erased.
- `TryControlUnit` refers to an original TryId. Its body and handler are separate
  skeleton roots; the wrapper's continuation is the common normal exit, if any.
- `TryExitUnit` terminates a protected/handler root normally. It records its
  original EdgeId, or the nested TryId whose wrapper continuation exits its
  parent. This is not automatically a JS `break` and carries no instruction PC.
- `ControlArc.edge` maps a projected edge to the original EdgeId. Absence is a
  synthetic wrapper continuation. Raising edges are never included among normal
  arcs: their dispatch, operation, and catch payload remain in `ExceptionPlan`.
- `inactive_commits` retains only empty, disconnected Commit nodes of explicit
  throw. They participate in the structural census, but must not become emitted
  statements or declarations. Arbitrary unreachable operations are refused.

Do not use skeleton node/edge indices as original IR IDs. In particular, do not
pass this skeleton into the normal function-tree builder. Its original phased
graph still fails normal control verification.

## Region And Exit Algorithm

Validated exception regions are disjoint or nested. Build one innermost lexical
owner per phase and DFS intervals for the region parent forest. A Catch phase
belongs to its region's handler, even when a nested protected region starts at
that same instruction. Before/Commit initially follow the instruction's
innermost scope. These are structural owners; the original semantic catch and
environment layouts remain immutable.

Before collecting exits, propagate structural ownership through control-only
jump corridors. Candidates are Before/Commit phases of `ValueJump` with no
operands/result/effects/exception and exactly one successor. Method/try entries
and Catch gateways cannot be absorbed. A Kahn worklist resolves each candidate
only after all predecessor owners are known: every predecessor must agree, and
the candidate's old owner must be the immediate parent scope of that agreed
region. Mixed body/handler entries stay outside both arms. Candidate cycles
remain unchanged, not silently broken. This adds O(B+E+R) work/storage, not a
walk along the corridor for each exit. All original blocks and arcs survive;
normal phi-copy assignments and jump PC anchors still pass through shared tree
construction and verification. In particular an outside getter, call, guard,
environment change or even a value-producing pure operation is not absorbed.
This is forwarding normalization, not general multiple-completion lowering.

Inspect each normal edge once. If it leaves its innermost region, record that
region's normal exit target. Propagate child exit summaries once in reverse
outer-before-inner order. Multiple distinct exit targets are a typed refusal;
return/throw paths do not require normal continuations.

Index region entries by `(parent scope, original protected entry block)`.
Target resolution either stays in the same scope, enters the immediate nested
wrapper through this index, or terminates at a checked region-exit unit. This
avoids walking every ancestor for every edge and correctly handles nested
regions sharing a protected entry. An arbitrary jump into a region is refused.

Run the existing reverse-topological postdominator recurrence with binary-lifted
LCA on the projected forest. Existing normal CFG/natural-loop paths are not
replaced. Projected cycles currently return `kCycle`; future loop integration
must prove transfers across try boundaries instead of dropping backedges.

For I instructions, B phase blocks, E edges, and R regions, the adapter adds
O(B + E + R) storage and O(B + E + (E + R) log(R+1)) work. With U projected units
and A projected arcs, common discovery adds O(U log(U+1) + A) storage and
O((U + A) log(U+1)) work. Original import/region verification has its existing
separately documented costs. A shared quota charges validation, projection,
discovery, and skeleton checks; returned `allocated_bytes` counts owned results,
not peak process RSS. Inputs are caller-owned.

## Verification And Next Integration

`VerifyExceptionControl` rechecks the original phase/region evidence and typed
mapping. `VerifyControlSkeleton` independently verifies every projected edge,
branch arm, continuation, root, and unique node owner. It does not replay
postdominator discovery. Revision changes, missing arcs/roots, altered handler
coordinates, cycles, and duplicated nodes are rejected.

Tests use three historical profiles, decoded getter/call old-local cases,
protected diamonds with normal joins, shared-start protected nesting,
handler-start protected nesting, and 200/201/402 nested regions. Mutation tests
cover mapping, handler, exit, inactive commits, revision, edges, and node order.
Work-limit boundaries and zero-byte admission are exercised. Multiple normal
exits and region-local loops have explicit refusal tests.

## Shared Tree Draft

`BuildExceptionFunctionDraft` drives the existing function-tree builder and
printer with the original phased graph and typed projection. Catch parameters
are handler-scoped. Snapshot assignments precede throwing operations; catch
copies commit at handler entry; normal copies retain original edge identities.
Predicates move to Commit only across a checked empty Before-to-Commit boundary
without copies. `TryOrigin` identifies structural regions without invented PCs.

This is deliberately a draft, not a publication receipt. Decoded instruction
fixtures for three profiles execute generated JS and compare getter/call
effects, old-local snapshots, protected branches and nested catch/rethrow.
Expression origins include the original operation PC and printed ranges are
nonempty. This does not certify complete PC mapping or file-backed methods.

## Independent Tree Correspondence

`VerifyExceptionTreeFlow` extends the common sequence checker, not the tree
builder. It freshly verifies original region/control/evaluation inputs, then
consumes the actual tree's blocks, branches, try clauses and copy statements.
Each TryId owns exactly one TryStmt; its protected and handler roots have the
required sibling scopes. The actual catch parameter binds the original
CatchDefinition and cannot become a body-local or another region's parameter.

The shared edge-copy checker indexes throw bundles by operation and commits by
Catch block. It consumes each once, at its exact boundary. Staging symbols are
recovered from actual assignment targets and must be distinct from phi, cycle,
ordinary snapshot and parameter bindings. Catch commits must read those same
slots. Exceptional bundle expressions follow the original parallel-copy list,
not its abstract cycle-breaking schedule; normal bundles retain their existing
schedule. No exceptional assignment is authorized at a normal edge boundary.

`VerifyExceptionTreeSemantics` then uses the common operand and origin checks.
Every snapshot RHS must match the original pre-throw operand; every catch read
must reference the actual bound exception value. Try syntax uses checked TryId
origins; copies use original edge origins; operations preserve original sites
and PCs, including combined origins. The draft builder now requires this check.
It does not rebuild the expected AST or trust BindingPlan symbol identities.

Beyond existing input, structure and common flow checks, exceptional indexing
and traversal add O(B + E + O + V + R + C) work/storage, for blocks, edges,
operations, values, regions and copy occurrences. Work and byte budgets include
fresh checks and additional tables; these counters are not peak-RSS evidence.
Traversal is iterative, including the 200/201/402-region fixture cases.

Mutation tests cover shifted snapshots, staging/phi aliasing, wrong committed
slot, exchanged body/handler, escaped catch scope, wrong snapshot RHS/catch
value, forged PC and stale revision. The latter operand/PC changes deliberately
remain structurally legal to distinguish correspondence from syntax checking.
Exact work and exhausted-work/byte boundaries are checked. Decoded branch,
return and throw placements also have generated statement ranges; jumps have
generated anchors, without reverse-parsing source text.

## File-Backed Admission

`PhasedMethod` owns the flow, SSA, environments, value proof, imported graph,
region projection and evaluation plan. `ExceptionTreeInput` copies its small
reference descriptor, not the referenced analyses; moving the heap-backed owner
preserves those references. The owner exposes no partial product on failure.
Imported phases report their byte/work accounting to the enclosing budget.
Environment analysis remains byte-bounded with instruction-visit work accounting;
these counters are not a process RSS measurement or a complete work bound for
every upstream pass.

`VerifiedFunction` now admits this owner for decoded methods with try tables,
then reuses same-file syntax/constants, the shared tree correspondence checker,
callable ABI, global binding checks and immutable publication. Catch parameters
are not formal parameters. V1 resource-failure equivalence is not proved and is
rejected; V2/V3 retain their explicit resource-exhaustion exclusion. Phased
default-parameter recovery and family/capture publication are still unsupported.

The file-backed integration fixture executes getter/call/branch/catch behavior
and checks all nine original PC placements through this publication path. It is
not a real application acceptance result. Source evidence rules are now 46.

`PhasedMethod` now carries original guarded-module facts through import and
`BuildPhasedModuleGuards`. Its independent verifier checks unchanged imported
operations/phase edges against the original input, then checks original
module/guard routes through `ProveExceptionModules`. It does not regenerate a
source tree. The resulting `ModuleGuards` retains both OpIds; no CFG, effect,
guard, PC or snapshot is removed. Analysis remains indexed with bounded passes,
not a separate graph walk per module read. Guard facts are conditional on the
whole pair, not initialization premises for raw module slots.

Module correspondence moved actual Immersive method 8284 past the PC 47 input
refusal into `ExceptionControlError::kMultipleExits`; forwarding normalization
now moves it through control to the still-unsupported evaluation stage.
The file-backed module fixture reaches
the still-closed evaluation gate. Module binding/TDZ-name equivalence, fused
phase evaluation and explicit receiver calls remain publication obligations.
No module source coverage is claimed merely
because pair correspondence succeeds. Different handler routes stay unpaired;
the previous normal-flow module API retains its same-block requirement.

## Conditional Module Evaluation

The common evaluation index now accepts a phased pair only after checking its
unique load Before -> Commit -> guard Before corridor and identical exceptional
dispatch. Ordinary same-block rules are unchanged. The effect verifier carries
an exact pending guard OpId across the empty phase boundary, consuming that
specific second event once; it never treats arbitrary guards as pure.

Catch fusion additionally requires equal source values in every staging slot
on both raising edges, and no normal phi copies inside the corridor. Edge and
slot indexes compare copies without depending on bundle order or searching each
graph per read. Beyond existing phase/phi verification, these checks add
O(B+E+I+P+S+M) work/storage for blocks, edges, operations, phi copies, catch slots
and module pairs. All scratch shares the caller's budget. Original load and
guard staging writes remain in the tree: the load's slots are already correct
if its fused read throws a TDZ error, and repeated guard staging after success
is private and identical. Different slot values are rejected.

Decoded three-profile tests reach shared FunctionTree/printer with conditional
module symbols. Execution uses actual JS lexical TDZs: caller vs catch routing,
the old parameter on the first failed read, the first successful module value
on the second failed read, and later live-binding changes. Both original PCs
map to the same expression's combined origin. This does NOT establish file
metadata/name, record enclosure or initialization obligations: the file-backed
owner still supplies no module evaluation bindings, so Immersive 8284 remains
unsupported at evaluation. Next bind its actual record metadata through the
existing alias machinery rather than synthesize a module symbol or weaken the
publication gate. Rules remain 46 until publication evidence changes.

Still required before phase completion:

1. Complete a real application method through the file-backed publication path
   and verify its emitted PC map and behavior. File-backed fixture publication
   does not satisfy this gate; standalone diagnostic drafts remain non-receipts.
2. Close capture/family obligations where present; do not treat a legal catch
   parameter read as proof that a created closure captures the correct cell.
3. Integrate loop/try nesting in both directions and cross-region exits, then
   typed completion/finally (including nesting), then await/yield and delegated
   protocols in the same model. Validate combinations, not only isolated layers.
   Current refusal is not final coverage or completed ISA support.

## Cross-Phase Member References

Rules 49 add a shared evaluation certificate for property reads consumed by
explicit-receiver calls. Canonical copy identities expose the callee producer;
an effective-use census requires exactly one retained callee occurrence. The
property and invocation must belong to one unique normal fallthrough corridor,
with no branch, join, catch entry, jump, observable intermediate operation or
normal phi-copy assignment. Prefix indexes establish these conditions without
walking the graph once per call. Only independently erasable pure computations
may remain between the sites; they cannot observe the getter's result.

Both sites must have identical exceptional dispatch and equal canonical source
values for every catch staging slot. Original staging writes and phase nodes
remain unchanged. The getter can then execute at the invocation expression,
after private pure temporaries, without changing effects or the state observed
by catch. Evaluation verification checks relocation separately from per-block
event order and still requires one forest visit per operation. CallPlan and
actual AST operand verification independently check the reference's base and
suppressed receiver. No lookup of user-overridable `.call`/`.apply` is introduced.

This is not permission to move across arbitrary argument getters or calls.
Immersive 8284's first member reference (blocks 18 to 28) now becomes inline;
its later reference (46 to 100) remains materialized because argument evaluation
has effects. General effect-subtree ordering and per-throw snapshot placement
are the next obligation, not another sample-name special case. The diagnostic
still reports non-executable/incomplete; module publication remains conditional.

Named/computed property unit fixtures exercise three historical profiles,
pure corridors at 1/200/201/402/1608, effect/jump/branch/duplicate-use barriers,
changed versus equal catch snapshots, forged placements and exact work limits.
File-backed member-call/branch/catch fixtures use the same immutable publication
path as ordinary functions; runtime checks cover receiver/arguments, Proxy
invocation, arbitrary throw values, catch snapshots and original PC ranges.
These fixtures do not replace real application acceptance or steps 2 through 5.
