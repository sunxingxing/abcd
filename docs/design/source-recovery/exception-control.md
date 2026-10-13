# Exception Control Projection

This is the T09 control step of the common source pipeline. It is not an
alternative body writer, a source publication receipt, or completion of group 1.
The original phased `ValueProgram` and `ExceptionPlan` remain authoritative.
Current publication boundary is documented in **Native Module Enclosure** below;
earlier checkpoints record how the shared path reached that boundary.

## Finally-throw oracle boundary

Emitted source must preserve the ABC's actual handler ranges, including
es2abc's inlined finalizers that remain inside a protected range. Dispatch
uses the pinned interpreter's first matching try in file order; a finalizer
throw can therefore enter a user catch and another inlined finalizer. The
original JS executed in Node is not an oracle for finally-throw scenarios:
use an explicit bytecode-derived handler/trace expectation (or the pinned VM),
while retaining source oracles for scenarios where the lowering agrees. See
[Round 45 fixture evidence](../../../tests/integration/fixtures/decision_guards/README.md)
for m473 PC170 → H247, PC325 → H337, and PC377 → caller.

## Same-File Module Context

`PhasedMethod::Build` optionally borrows a `PhasedModuleBinding` during construction.
The existing alias binder verifies phased guards, record ownership, metadata slots
and exact decoded guard names before evaluation receives module symbols. The owner
retains immutable aliases, not borrowed metadata. Syntax names may be rebased when
appended to the shared arena; `VerifyModuleAliasTree` independently checks identity
and spelling after tree construction. This does not prove record initialization or
authorize standalone module publication; the existing publication gates remain.

At the initial module-binding checkpoint, Immersive method 8284 passed the bound phase/evaluation path and
shared bindings. Copy identity projection now resolves its callee to the property
read. It then failed `CallPlanError::kReference`: that read was a materialized root
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

## Ordered Phase Expressions

Rules 50 replace the property-only corridor check with one shared scalar,
reference and invocation expression planner. The original IR and control
projection remain unchanged. The planner uses effective (alias-resolved) use
counts and normal-corridor coordinates; every selected forest edge must have
one retained use, move forward within a unique fallthrough chain and cross no
normal phi copies. Raw receiver uses, first phi/catch snapshots, conditional
argument reads and predicate-repeat boundaries remain protected. Environment,
module, creation, initializer and control protocols are not newly authorized.

Within a corridor, each observable operation has an ordinal. Discovery consumes
available child-effect suffixes in reverse source operand order. Verification
does not rerun discovery: it checks the proposed children in forward operand
order and requires their intervals to concatenate without gaps or overlaps,
ending immediately before the parent's own event. The existing forest checker
still requires one visit per original live operation; a cross-phase event is
not discarded merely because it moved into another block's expression.

Exception ownership is checked at the FINAL expression root. In particular,
`typeof obj.value` can carry a throwing getter despite TYPEOF itself being pure.
A backward ID pass computes final roots in O(I), without following a parent
chain once per operation. Each moved observable operation must have the same
exception dispatch and equal canonical catch-slot sources as that root.
All original staging writes stay in place. Trees lacking this proof are restored
to their original snapshots in one indexed pass, not repeatedly rescheduled.
The verifier independently rejects forged order, ownership or dispatch claims.

File-backed publication fixtures cover an argument getter, a pure TYPEOF wrapper
and nested string concatenations containing multiple getters/coercions, followed
by a branch and catch. Generated JS is executed with arbitrary thrown values,
Proxy callees and non-callable targets. The non-callable check must occur AFTER
all argument effects. PC ranges retain separate getter, conversion operation,
call and control origins. Generic numeric ADD remains refused without its
existing value-domain proof; the negative fixture preserves that boundary.

At Rules 50, Immersive 8284 reached the shared FunctionTree and passed module-alias
tree validation, including its previously refused effectful log-call arguments.
That checkpoint was conditional diagnostic evidence, not executable publication.
Rules 51 below close the external-import method-fragment obligation through the
immutable emission path, without bypassing it with a raw printer.
Loop/try integration, typed completion/finally and suspension protocols retain
the previously approved order and full acceptance requirements.

## Native Module Enclosure

Rules 51 close external-import publication for the immutable phased method
route. `ModuleEnclosure` is built once from the same file's record module table:
regular-import slots followed by namespace-import slots, exact strict local
identifiers, unique across both groups. The existing import writer emits that
inventory in dependency order. The enclosure is move-only and owns its names;
its input file and shared metadata budget remain caller-owned. It neither
asserts initialization nor snapshots an imported value. Native ESM preserves
live binding and cyclic TDZ behavior under the versioned language contract.

Preparation rebinds original guarded pairs from the same record, attaches typed
module aliases to the shared syntax, then verifies actual tree correspondence
and destination scope before publishing `VerifiedFunction`. Printing rechecks
owner/slot/spelling/access and complete intervening lexical bindings. Bare
`GlobalScope` name lists cannot grant permission. Global lookup verification
also includes all enclosure imports, not just aliases read by this method.
Position output records an owned module-binding requirement. Moving a method
fragment into another enclosure requires a fresh check and the exact prelude.

Only external read-only aliases are admitted. Local exports are not declaration
or initialization proof; module stores, incoming captures, family publication,
record initialization and complete class/record assembly remain independent
gates. No fallback printer or sample-specific engine branch is introduced.

Validation includes three historical profiles, namespace slot order, duplicate
and invalid identifiers, foreign file/identity/owner, local-slot and mutable
alias refusal, shadows, moved ownership, work limits and 1/200/201/402/804/1608
inventory sizes. File-backed source tests load actual native ESM modules, test
cyclic TDZ and changed exports, and check load/guard PC ranges.

Immersive 8284 now passes immutable emission and behavior comparison against a
decoded-operation test oracle (no sidecar source input). It exercises property
reads, member calls, catch, imported logging, throwing coercions and all 53 PC
placements. The diagnostic `--emit` reports method-fragment completion only;
the record remains incomplete. This real method has no conditional branch:
existing file-backed branch/catch tests still do not replace a real application
branching-method acceptance case. Immersive 8257/8404 still refuse evaluation.

Targeted real-method check (requires the pinned local fixture hash):

```sh
cmake --build build --target exception_candidates -j 2
node --experimental-vm-modules tests/integration/immersive_exception.mjs \
  build/exception_candidates /Users/sun/abcd/samples/abc_with_code/Immersive/modules.abc
```

This explicitly selected test is not registered as a mandatory CTest dependency
on a developer's private corpus path. Portable enclosure fixtures are registered
as `module_enclosure_test` and `module_enclosure_source_test`.

## Phased Own-Data Initialization

Rules 52 reuse `LiteralPlan` for normal and phased input. Phase availability and
copy equations are independently checked before initializer discovery. A single
indexed pass assigns coordinates along unique normal fallthrough paths; catch,
branch, join and jump boundaries cannot connect initializer groups. Allocation
must be a fresh ordinary object; definitions retain original order and supported
own-data semantics. The source value for every field must be available before
allocation, resolving only already-verified copy aliases. Direct phi leaves
cannot migrate across an intervening edge-copy boundary.

Canonical-use checking visits each use once. Before the final definition, only
proved copies and that object's own-definition base uses are admitted. Earlier
reads, escapes, stored-value observations and exceptional snapshots reject.
Observers on other corridors remain conservative refusals until a separate
completion-dominance proof exists. This is not general store scalarization.
Resource-exhaustion equivalence remains the existing V2/V3 publication premise:
combining nonescaping ordinary own-data definitions cannot invoke a getter or
setter, but may change allocation-failure timing. V1 publication stays closed.

The expression forest emits existing computed-key `DataProperty` nodes, including
`__proto__`, never an inherited setter or prototype-setter literal. Every own
definition retains its original PC through its property node. The original phase
graph, exceptional copies and control projection are unchanged. Tree checks and
printer are shared; there is no phase-specific source writer.

The file-backed source fixture tests getter evaluation, own-data descriptors,
arbitrary throws through catch and instruction/property positions. Unit cases
cover three profiles, all four supported named-own-write formats, early/catch
observation, late RHS, intervening branches, invalid aliases/revisions/coverage,
exact initializer work limits, copy chains and 200/201/geometric field counts.
Large AST composition fixtures use an explicit test work allowance; this neither
raises production defaults nor proves whole-pipeline complexity or RSS.

Real Immersive 8566 now clears its buffered-object initialization and reaches a
later member-reference refusal: callee PC 331, module load/guard PCs 337/339,
call PC 363. Extending the common effect forest to paired module reads must keep
the original two-operation correspondence, TDZ and throw-owner checks. Do not
replace the saved callee with a fresh getter or untrusted `.call`/`Reflect.apply`.
Immersive 8257/8404 instead require child-family ownership; they fail the existing
`ValueFunctionCreation`/`FunctionBinding` coverage check, not exception parsing.

## Guarded Module Effect Subtrees

Rules 53 extend the same phase-expression forest, not the decoder or exception
CFG, to verified `ModuleEvaluation` pairs. `IndexModuleEvaluations` remains the
prerequisite: exact read/guard identities and effects, original read-result guard
operand, adjacent operation census and unique read/Commit/guard order. The
immutable publication boundary still proves original ABC, record and native ESM
binding correspondence. A language-valued result alone never authorizes a read.

The corridor indexes pair peers once. Only the guard's internal dependency is
removed from the effective-use census; other uses, including catch snapshots,
retain their ownership. The read subtree spans two consecutive original effect
events. Independent forward interval equations verify any proposed composition;
the backward builder cannot move the pair across another unconsumed effect.
Both original operations remain in the graph and map to the same expression's
combined origin. Guard placement remains `kModuleGuard`, not an arbitrary elision.

Normal flow keeps its prior pinned-read policy. Phased fusion requires the same
unique corridor/no-edge-copy/single-use and final-owner catch checks as other
effectful expressions. `VerifyModuleCatchCopies` independently checks read/guard
dispatch snapshots; this and read/final-owner equality preserve the guard's
exception state transitively. Closed cycles stay unindexed and cannot fuse.
Index construction adds O(I+M) time/storage, where M is the number of module pairs;
it does not add per-read graph walks or alter the existing proof budgets.

Decoded unit/integration coverage includes three profiles, getter success and
arbitrary thrown values, lexical TDZ, live bindings, cross-handler rejection,
forged inline ownership, paired-guard mutations and 200/201/402 pairs. Real
Immersive 8566 now publishes `updateWindowInfo` through `VerifiedFunction` and
`FunctionEmission`; `tests/integration/immersive_window.mjs` checks every normal-path
observable failure point, live rebinding during callee lookup, writes, receiver identity,
logs and all 330 operation placements. This selected-method test uses explicit
`--work 4000000`; default preparation still reports `kWorkLimit` due to repeated
independent graph/tree proofs. Production budgets and memory caps are unchanged.
This is not a record-completion or all-ISA claim. No finally, coroutine, scheduler,
full-corpus or RSS acceptance follows from this test.

## Staged Family Preparation

`PhasedBody` owns the original flow/SSA/environment/value proofs, imported graph,
module obligations and checked exception control independently of evaluation.
`PhasedMethod::Build` composes its construction with `PhasedMethod::Evaluate`;
existing publication still uses that complete route. The staged API does not
take guessed child bindings or expose mutable analyses. Moving into evaluation
retains the original graph allocation and views, while total admission includes
the body's retained storage and work already consumed. A fresh stage is not a
fresh budget. The caller still owns the unchanged decoded body, file and context.

This removes a construction-order obstacle: child creation discovery can inspect
the parent before evaluation demands complete child bindings. The diagnostic
tool proves this for Immersive 8257 (PCs 95/118) and 8404 (PC 52), while evaluation
still refuses and reports no executable source. `exception_children.mjs` pins
the input and exact creation census. Unit tests check move/view stability, exact
body work, one-less work, exhausted evaluation budgets and unchanged missing
module-binding rejection.

The remaining family bridge is substantive, not a matter of supplying IDs:
resolve each actual creation through its caller's pool and occurrence context;
retain original exception environment state for capture binding; prove incoming
child cells and transitive writes; construct and verify all child trees; then
compose the complete family and certify the destination enclosure. Existing
normal-only `FunctionInput`/`FunctionValues`/capture APIs cannot be passed a
phase graph under a normal-flow proof. Do not bypass their checks or label this
preparation split as completion of the exception-family or all-ISA goal.

### Original Creation Bindings

`BindExceptionCreation` accepts an explicit original `ExceptionNodeId`, not a
decoded instruction index. It rejects Commit/Catch nodes, malformed phase table
shape and unavailable/invalid environments, then shares the existing target and
ordinary/sendable capture rules with `BindCreation`. Exception environments are
indexed by nodes; using the ordinary binder with that table is invalid. A catch
creation captures the throw-time environment, not the later normal POPLEXENV
state. No graph is copied or relabeled to work around that distinction.

`BindPhasedCreations` walks live typed creation operations once, checks original
operation/site/PC correspondence and all creation operands, binds with the exact
Before environment, resolves the caller-regional pool, and finds the precise
target/caller/PC occurrence. Each entry owns a resolved `CreationTreeContext`;
shared method templates are not deduplicated into a single runtime function.
The parent retains immutable route identity and method stamps. As with the normal
family reader, supplied file/index/body/role/site analyses must be unchanged and
from that parent. Captured environment IDs remain borrowed from its retained
original environment index. There is no source receipt or capture initialization
permission, and unsupported callable contexts are typed failures.

Additional work is two operation censuses plus indexed metadata/occurrence/name
resolution per creation, not per-creation scans of the CFG or all target uses.
The result and each name/context are budget-admitted. The shared binding test
covers three profiles, creations before/after POPLEXENV and in a catch handler,
invalid phase/environment inputs and 200/201/4096 repeated targets. The source
adapter also tests identity and exact/insufficient work and memory allowances.
Pinned real edges are 8257@95 -> 7810, 8257@118 -> 8118 and 8404@52 -> 7978.
Their parent evaluations remain refused until the capture/family bridge closes
the actual child obligations; target resolution alone is not decompilation.

### Working Environment Topology

`BuildPhasedValueEnvironments` and its independent verifier share the normal
environment representation and ancestry solver. The operation-indexed views
remain indexed by `OpId`; a separate `phase_entries` table includes empty
Commit/Catch blocks. Lexical accesses bind at Before. NEWLEXENV/POPLEXENV change
the chain at successful Commit only, so allocation failure propagates the old
environment to catch. Explicit throw's unreachable Commit has no entry state.

The temporary ancestry adapter preserves every edge and treats exception edges
as ordinary connectivity only for that solver. It never changes the value CFG,
handler identity, effects, SSA or source control. Fresh phased-flow validation
precedes both construction and verification. The verifier checks entry states,
all joins, allocation identities, operation views and exact lexical bindings;
it does not rerun the environment planner. Ordinary APIs still reject phased
graphs. No normal-flow initialization or capture certificate is reused here.

This is a prerequisite for the parent-cell/child-capture bridge, not yet connected
to its publication gate. Normal cell initialization/lifetime proofs still reject
phased graphs. No new function family becomes executable from topology alone. Tests cover profiles
1202/1206/1301, allocation failure versus successful commit, explicit throw,
forged states/bindings/allocations, exact and insufficient budgets, and
200/201/402 independent environment groups. These are correctness cases, not
RSS or timing measurements; loop/finally/coroutine composition remains pending.

### Retained Phased Cells

`BuildPhasedCellPlan`/`VerifyPhasedCellPlan` reuse the same typed storage keys,
sorting, exact access census and allocation-site index as normal cell planning.
Only their environment validator differs. Normal APIs continue to reject phase
graphs, so callers cannot accidentally feed a phase plan into normal dominance
or lifetime proofs. These plans distinguish incoming storage from local
allocation sites even when both accesses encode depth zero and slot zero.
They neither prove initialization nor allocate source bindings.

`PhasedBody` now owns both working environments and cells when environment,
lexical or creation operations require them. Both immutable views borrow that
body; construction debits the same remaining bytes/work as other stages, and
evaluation inherits that charge. Original environment IDs must not be compared
with working IDs; future capture rebasing uses allocation provenance. Pure
bodies without such operations do not construct these optional products.
The real-parent regression now reads retained production results, not an
independent diagnostic analysis: 8257 has zero cells and 8404 has one. Both
continue to reject incomplete child-family evaluation.

The dedicated cell test starts from decoded ABC and checks three profiles,
normal versus allocation-failure catch storage, 200/201/402 allocation groups,
corrupt access/storage/revision certificates, exact/insufficient work and zero
memory, plus rejection by normal initialization APIs. Remaining work includes
phase-aware source lifetimes and actual parent/child capture binding before
tree admission; completed-store witnesses are described below.

### Completed-Store Initialization

Phased cell initialization now reuses the normal typed witnesses and dominator
walk with undo state, but indexes successful allocation/store Commit phases.
Loads query state at Before. The independent verifier requires allocation
Commit to dominate the witnessed store's Before, and store Commit to dominate
the load's Before. All exceptional edges participate; no normal-only dominance
certificate is substituted. Explicit throw's dead Commit is provenance-only.
Incoming reads still require a parent proof; branch-union initialization without
a single dominating store remains unsupported, rather than defaulting to JS
undefined or borrowing a value from a previous allocation.

`BuildPhasedCellEvaluation` and its verifier compose phased environment, storage
and initialization checks under one allowance. The shared phased evaluation
planner/verifier select these entries. This does not certify source declaration
lifetimes: the exception scope bridge below supplies that separate proof.
Ordinary initialization/evaluation entries still reject phased graphs. Original
`PhasedBody` lexical-load premises and complete child capture ownership remain
separate obligations, not inferred from domains.

Rules 54 also require closure-write evidence for unresolved buffered objects,
whose methods can capture the current environment. This applies to shared
normal/phased initialization. Empty objects remain distinct. A future proven
pure-data buffer exemption must carry evidence; the buffer's presence or printed
text cannot silently assert purity. The pinned interpreter provenance matches
the existing `ProveExceptionLocals` capture rule.

Decoded tests compare a store completed before a throwing call with one placed
after it: both normal and catch reads succeed in the former, while the latter
rejects even with deliberately forged Language load premises and store witnesses.
They also cover three profiles, explicit throw, buffered capture rejection,
200/201/402 fresh allocation groups, exact/insufficient budgets, corrupted
witnesses and source-admission refusal. These are IR/evaluation proof tests, not
new executable-function or loop/finally/coroutine acceptance.

### Phase Cell Source Lifetimes (Rules 55)

`BuildPhasedCellLifetimes` shares the normal scope-tree/Euler-interval planner.
Typed try-body/handler containers map projected control units to original work
blocks; skeleton IDs are never used as work-block IDs. Fresh phase cell and
exception-control validation precedes enclosure checks. Only certified empty,
disconnected explicit-throw Commit nodes may be unvisited. Declarations stay at
their allocation site, and every local access must be enclosed by that source
scope. A valid initialized VM cell allocated inside try but read after try is
still rejected: native declaration lifting needs a separate proof.

`FunctionTree` replaces the blanket phased-cell rejection with that lifetime
check. Existing independent tree checks validate actual declaration origins,
positions, symbol scopes and uses; the candidate plan cannot authorize moving
declarations. `PhasedBody` now obtains original lexical-load premises from
`ProveExceptionLocals` when needed and validates EVERY store after propagation.
No assumption about incoming cells, unresolved captures or uninitialized reads
is added. Module and local facts share one budgeted load-fact boundary.

A file-backed decoded class method now passes immutable preparation, shared
tree, printer and executable JS checks for local initialization, a throwing
call, catch write/read and return. Tests compare arbitrary JS exception-value
identity, call receiver/arguments, effect sequence, reentrant activation
isolation and all ten original PCs. Scope escape and declaration-scope forgery
are negative tests across profiles 1202/1206/1301. This does not close captured
child families or admit exception cycles, finally or suspension protocols;
fresh bindings for repeated loop allocations remain part of that later gate.

### Shared Original Capture Inputs

`ScopeFacts` now holds typed normal/exception views. Both feed the existing
`CapturePlan` builder and independent verifier. Exception queries are fixed to
one genuine original Before node, use the same environment ancestry lookup and
query completed-store intervals on the full exception graph. Commit, Catch,
wrong occurrence and uninitialized handler captures cannot substitute for that
creation. Outward bindings retain their depth/slot and domain obligation;
Language initialization does not establish a local Defined/TDZ-guard claim.

`ProveExceptionLocals` accepts an explicit `ClosureWrites::kLanguage` conditional
family premise, retaining it in both its result and `CapturePlan`. It permits
mutation, not snapshots or alias elimination. Every original store must still
pass input validation, and every descendant's write-domain obligation remains
pending. The default stays Unknown; exact preservation evidence remains a
distinct alternative. Standalone `PhasedBody` does not opt into conditional
writes or grant a source receipt. Cyclic capture initialization remains refused.

`BindPhasedFunctionInputs` checks the real child owner, file identity, target,
creator/PC and original-to-working creation occurrence before applying the
common capture plan. The selected real 8257→7810/8118 and 8404→7978 inputs now
bind zero/zero/one capture occurrences respectively. These are conditional
original-input bindings only, not executable parent families. Parent evaluation
still refuses missing child completion. Rules 55 remain unchanged.

The working-cell/evaluation bridge below connects these plans. Complete child
trees, enclosure and transitive writes still precede family publication. No
read-only sample shortcut or specialized body writer substitutes for that gate.

### Shared Capture Targets and Evaluation

Normal and phased owners now feed the same `BindCaptureTargets` and independent
`VerifyCaptureTargets`. The internal borrowed `CaptureParent` view carries
original instructions/allocation provenance, the working graph/environment/cell
plans and typed scope facts. It neither owns another graph nor equates original
and working environment index namespaces. Existing normal public APIs remain;
the phased wrapper checks the real same-file/record child owner and occurrence.

Planning groups child incoming cells and resolves each depth/slot in the working
parent environment at creation. Verification checks that map independently and
walks every original capture occurrence: original local environment → allocation
instruction → retained working OpId → parent CellId. Child accesses are connected
through their retained original origins. Missing stores/cells are never invented;
outward captures remain explicit obligations, not snapshots or declarations.
Revisions, identity, coverage, original initialization and write premises are
checked again. The original-input verifier does not rerun its builder.

`BindPhasedCaptureEvaluation` reuses the normal verified binding conversion.
Distinct syntax owners and supported creation semantics remain mandatory; an
unresolved outward capture still rejects. The selected 8257 and 8404 parents now
reach the common phased evaluation planner/verifier using their real child
bindings (0/0/1 parent-cell targets). This is explicitly conditional evaluation,
not a complete family or source receipt. The standalone `PhasedMethod::Evaluate`
still receives no unproved child trees and refuses publication as before.

Remaining work: retain phase/normal family members under one owner, build the
actual child ASTs, connect incoming symbols to the actual parent declarations,
verify family enclosure and transitive writes, then execute the resulting real
parent/child family and verify its PC map. Loop/finally/suspension stages are not
completed by these capture facts. Source rules remain 55.

### Callback Expressions and Actual Child Trees

Function creation now participates in the existing phased expression corridor,
as an ordered event, not a pure allocation or a snapshot of captured values.
Its binding must already pass evaluation validation. Single effective use,
ordered effect suffix, unchanged catch target/staged values and final-root
environment epoch checks remain mandatory. A push/pop is an unconsumable event;
moving a callback across it or across handler changes is rejected. No new graph
walk or complexity term is introduced.

The real `8404 queryDegree -> 7978` combination now passes shared parent and
child tree builders, actual incoming capture aliasing, independent tree flow,
family layout and lexical enclosure. The test diagnostic retains real decoded
child inputs; it does not fabricate an AST or introduce a body writer. A forged
child capture target fails independently. This is not executable publication:
unified file-family ownership, transitive write proof, complete ABI/PC evidence
and execution verification still remain.

Before Rules 56, `8257 recover` remained refused. Its property Op 9 / member call Op 12 subtree
is carried to pure final root Op 22 without exception dispatch evidence. The
current fallback restores the entire proposed tree, leaving a member reference
that cannot be emitted faithfully. Improve general tree partition/fallback
while retaining exception/effect proofs; do not admit the pure root by assumption,
read the getter twice, or synthesize a mutable `.call` lookup. Source rules stay
55 at that step. The general retry below now removes this refusal.

### Bounded Retry for Rejected Trees

Rules 56 retains the speculative planner and independent verifier. After final
catch checking, accepted trees stay unchanged. Rejected trees restore their
original placements, then run the same effect-suffix scheduler once within their
original root partition. No candidate can consume a producer from another
partition. Each effect-carrying child must additionally have the same catch
signature as its immediate new parent. By induction its entire retained effect
subtree has that signature; a pure parent without dispatch evidence cannot move
it. Pure zero-effect children still participate normally. The final verifier is
unchanged and checks final roots rather than trusting this induction.

This is a bounded conservative partition, not a maximal expression search or
an iterative rollback loop. It can retain a legal getter/call subtree at the
call's original site while materializing its result for a later pure expression.
All work/space remains O(B+E+I+V+U+P+S); each producer has one effective consumer,
and extra catch comparisons are charged to its original copy volume. No source
text inspection, handler assumption or mutable `.call` fallback is involved.

The real `recover` parent and both actual children now pass shared trees,
capture/layout and lexical enclosure, alongside `queryDegree`. File-family
ownership and transitive writes remain publication gates; this does not finish
loops with exceptions, finally or suspension. Unit coverage includes deep pure
result chains and callbacks across profiles 1202/1206/1301, exact work budgets,
200/201/geometric counts and forced invalid final-root fusion.

A file-backed standalone method reaches immutable preparation, shared AST and
printer for `try { return typeof obj.run(7); } catch (e) { return e; }`.
Execution checks cover 27 combinations of arbitrary JS values and normal/getter/
call failure, original receiver/arguments and effect order; all ten original PCs
have checked generated placements. This fixture has no child family and does
not discharge the separate real-family publication obligations above.

### Shared Lexical Store Obligations

`VerifyLexicalStores` now checks normal and phased members through typed input
views. It verifies every original store's Language operands, retained original
site/PC/opcode provenance, working cell classification and the actual AST symbol
assignment recovered by checked TreeFlow. It also compares the residual store
census. A phased input indexes original Before nodes once and reads their SSA
facts; Commit and Catch are not substitutes for the store's input. Captured
classification comes from original input access/environment facts, not names.

The existing normal `VerifyFamilyWrites` consumes this same checker, retaining
its independent complete child/barrier/capture checks. Native-default signature
stores still require the owner's checked signature placements; phased signature
migration is not admitted. Resource failures keep typed byte/work distinctions.
No graph copies or per-variable graph walks are introduced.

Real `recover`, `queryDegree` and their three children now pass member-store
checking alongside trees and enclosure. Normal/captured stores have three-profile
and 200/201/402 coverage; file-backed phased normal/catch stores and corrupted
targets have separate coverage. This is deliberately not a family certificate:
complete phase/normal file ownership, creation-barrier census and transitive
write induction still have to be connected before source publication. Rules 56
remain unchanged by this shared member check.

### Mixed Family Write Closure

`VerifyFamilyWrites` now accepts borrowed typed normal/phased members. The
existing `FileFamily` overload adapts each member without allocating or copying
graphs and uses the same checker. The phase view borrows its immutable body,
original method, once-computed conditional `ExceptionLocals` and method lookup;
each actual child edge retains its original creation and resolved owner.

Checks run parent-first: identity/revision and conditional domain premises,
closed root, original and working capture correspondence, actual AST capture
targets, and the shared original-to-AST store obligations. Only after every
member passes are original creation barriers discharged. Phase barriers come
from all original typed instructions, not `ExceptionLocals.plan.capture_barriers`:
that normal-plan field is not populated by the phase analysis, which may return
early without local cells. Function/class/object-buffer/array-buffer creations
all require coverage, including those absent from residual working operations.
Only retained primitive-object evidence can discharge an object-buffer barrier;
unsupported class/array barriers stay refused. Duplicate/omitted edges fail.

The real `recover` and `queryDegree` families now pass this complete write-domain
gate. Tests remove descendants from BOTH member censuses to check original-PC
coverage rather than mere matching sizes, and reject unknown write premises,
missing/self/duplicate/wrong-kind edges and one-less byte/work budgets. Normal
file-backed families compare both overloads; three-profile and 200/201/402 store
cases also exercise the common gate.

This is a fresh, borrowed verification result, not an immutable source receipt
or an independent input loader. Phase-parent to normal-child edges and normal
descendants are admitted; phase children remain explicitly refused. Mixed
file-family ownership, ABI/PC census and destination/module publication gates
still need integration, followed by executable combined-family verification.
Rules 56 and the standalone source refusals remain unchanged. Loop/exception,
completion/finally and suspension work retain their original acceptance scope.

### Shared Instruction Census and Conditional Family Execution

`BuildInstructionSources` now borrows either normal input/work products or an
immutable `PhasedBody` with its original method. Both use the same bounded dense
source index. Original instruction ordinals, not phase-node IDs, determine the
census. Projected phase aliases and dead copies retain `AnchorId` targets at
their original lexical boundaries; only already-proved removed normal IR uses
`ElidedInstruction`. Phase signature migration remains refused. This pass
checks identity/revisions, original provenance, unique retained placement and
complete coverage; it does not independently prove body semantics or erasure.

The two real mixed families now retain ABI and instruction-source censuses for
all five members, then use the shared class-family printer. All 145 original
instructions of `recover` plus children and all 90 of `queryDegree` plus child
map through AST IDs to generated event ranges/anchors. Tests also cover decoded
standalone branch/catch, receiver calls, live/dead copies and normal/catch stores,
including wrong original PC/revision, missing/duplicate placements and exact or
one-less byte/work budgets. No generated-text search or source-map input is used.

Controlled execution of these CONDITIONAL drafts checks both status branches,
distinct repeated receiver reads, getter/call failure, code/message evaluation
order, callback parameters/length, synchronous versus later callback throws,
mutable receiver capture after the parent returns and separate per-invocation
cells. The harness supplies named host/module bindings explicitly. It does not
prove native module initialization, general destination enclosure or actual
host behavior, and does not turn the diagnostic draft into a publication
receipt. Mixed file ownership and sealed publication remain the next gate;
no claim is made for loops/finally/suspension or full ISA completion. Rules 56
are unchanged because executable publication routes are not broadened.

### Owned File-backed Phased Input

`FilePhasedFunction::Read` now loads the actual method body and call-type ABI
from the unchanged file/index, checks them against the resolved occurrence,
and keeps the body, immutable `PhasedBody` and once-computed conditional capture
scopes under one immutable heap owner. Views survive moving the envelope or
growing the future family-member vector; no context, file or module descriptor
pointer is retained. Optional module binding must use the same file/header/index
objects and the actual owning record, not another metadata snapshot.

The byte allowance admits the owner, decoded vector capacities, retained phase
analysis and capture scopes while passing the remaining allowance to each
builder. Work includes an instruction-byte charge and the recorded phase/local
work; metadata/container parsers retain their existing byte limits, not a newly
claimed complete work census. Lower-level typed failures are preserved, including
the current combined resource failure of `ProveExceptionLocals`. This is not an
RSS measurement or closure-write publication certificate.

The real family probe now consumes this owned input instead of separately
decoding a parent and constructing a scope proof in the test driver. Existing
235-PC mapping and controlled execution checks still run through its views.
File-decoded tests cover move/vector-growth stability at 200/201/402 envelopes,
reuse of the original control view after moves, exact/insufficient recorded work,
zero bytes, wrong ABI/method/header, missing code, malformed exception metadata
and mixed module snapshots. The shared `FileFamily` DFS/tree factory and sealed
publication have not yet adopted a mixed member variant; this owner is their
required input boundary, not a second exceptional source writer. Rules 56 and
the remaining loop/finally/suspension scope are unchanged.

## Private terminal ownership and control failure locations (Round 39)

The exception projection shares the [terminal-region proof and stable follow
rule](loop-discovery.md#private-terminal-regions-and-stable-follows-round-39) with
normal control. It adds internal-branch/merge ownership without changing try-exit,
loop or completion semantics. The independent projection verifier remains the
source of phase/handler/try-exit identity. See the same note's typed control failure
section for method, original/working PC, synthetic-node and region-bound evidence.

Round 40 resolves shared-decision continuations against their enclosing stop
using a separate real normal-postdominator tree. The [bounded continuation
proof](loop-discovery.md#bounded-decision-continuations-round-40) is shared with
normal control. It does not alter this projection, raise edges, handler entries,
try-exit ports or catch staging. Failure-location translation visits every new
decision witness endpoint, preserving phase PCs and synthetic identities.
