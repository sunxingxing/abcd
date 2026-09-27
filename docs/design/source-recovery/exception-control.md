# Exception Control Projection

This is the T09 control step of the common source pipeline. It is not an
alternative body writer, a source publication receipt, or completion of group 1.
The original phased `ValueProgram` and `ExceptionPlan` remain authoritative.

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
phase evaluation, exception snapshot correspondence and explicit receiver calls
remain publication obligations. No module source coverage is claimed merely
because pair correspondence succeeds. Different handler routes stay unpaired;
the previous normal-flow module API retains its same-block requirement.

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
