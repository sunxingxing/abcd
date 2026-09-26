# Capture Binding

T07 now shares a borrowed `ScopeFacts` view between the established semantic
capture resolver and the new creation-specific `CapturePlan`. This is binding
infrastructure, not permission to emit a child FunctionTree or scalarize cells.
Source rules remain 18; no closure source domain is newly admitted.

## Inputs and Ownership

The caller must supply one unchanged, freshly verified SemanticMethod,
EnvironmentFlow, LocalAccessPlan, DefinedCaptures and ClosureWrites premise.
ScopeFacts borrows them; it must not outlive them or mix methods. Local cell,
store, barrier and guard indexes are the semantic verifier's sorted indexes,
not names, debug scopes or guessed declaration sites. No per-query graph scan
or duplicate environment index is introduced.

CapturePlan also requires a successful BindCreation result in that same file,
profile and owner, and the child's proved capture-use obligations. It checks
the instruction PC, typed creation metadata, ordinary capture environment and
non-sendable target. It retains instruction, PC and target MethodPosition. Those
IDs are owner-relative, not a persistent cache key or independent file identity.
The immutable function publication owner must retain their identity envelope.

## Binding and Obligations

Each capture occurrence is rebased through the actual creation environment;
occurrences are neither deduplicated by target method nor merged by slot name.
Local bindings require an initialization certificate and the verifier's filtered
closure-write barriers. Defined demands retain the stricter guard intervals.
Unknown earlier creation writes cannot be ignored just because a store exists.
Outer captures remain typed CapturedSlot obligations with their original domain.
Returning one is not proof that the outer value is initialized or defined.

ClosureWrites::Language is conditional on complete child validation, including
transitive writes. It is not constness or a snapshot proof. Local EnvironmentId
identifies an allocation site; loop iterations and repeated parent activations
still create different dynamic cells. This component performs no cell-to-value
rewrite and does not change CFG, SSA, block or source-scope ownership.

VerifyCapturePlan rechecks the supplied occurrence count, creation context,
actual rebased cell and domain against ScopeFacts. It does not call the planner
or accept an emitter-provided name as evidence. ScopeFacts itself consumes the
semantic proof's preconditions; it is not a replacement independent verifier
of arbitrary caller-fabricated SemanticProof structs.

## Cost and Remaining Integration

For K capture occurrences, C indexed cells, W stores, B blocks, A barriers and
G defined guards, binding/checking costs
O(K (log(C+1)+log(W+1)+log(B+1)+log(A+1)+log(G+1)+32)). The final term is the
uint32 environment-depth domain's fixed ancestry bound. Retained storage is
O(K); query scratch is constant. Planning and its check share one work allowance,
and output capacity is admitted before allocation. These are invocation bounds,
not evidence of whole-process RSS compliance.

Regression sources cover 0/1/200/201/4096 captures, different allocation contexts
for the same target, outer Defined obligations, changed domain/cell/site,
sendable rejection, uninitialized locals, unknown write barriers and exact work
limits. They remain uncompiled/unrun during the user's testing pause.

Typed environment/cell/creation operations now enter Value IR with input/effect
correspondence. ValueEnvironments also binds topology on the current work graph:
BuildValueStage retains it for lexical/creation operations, with IR/CFG revisions.
It reuses AnalyzeEnvironmentSites, preserving OpId-indexed states, allocation
sites and relative cell bindings. An independent checker walks each block and
edge, verifies allocation metadata and lexical bindings, and rejects inconsistent
joins or removed effectful operations. No environment planner replay is used.
Normal-flow/reachability proof is fresh. Working environment IDs belong to this
new index, not to SemanticProof; origin sites connect the contexts explicitly.

The adapter/check is O(O+B+E+32L) beyond fresh normal-flow checking, with O(O+B)
scratch, for O operations, B blocks, E edges and L lexical/allocation operations.
The planner conservatively debits the upstream four-visits/site contract and
two fixed-depth ancestry passes before running it. Input/proof and working graph
storage are admitted together with topology scratch; returned charges retain
the upstream scratch estimate conservatively, not a claimed peak RSS result.

CellPlan now indexes each verified lexical access by a stable CellId and groups
ordered accesses in one flat arena. AllocatedCell uses the actual allocation
OpId plus slot, not the EnvironmentIndex's temporary numbering. IncomingCell
uses an explicit captured depth plus slot. Operation-to-cell lookup is O(1).
Grouping sorts once, O(A log A) for A accesses; its independent checker is
O(O+A+C) beyond environment verification for O operations and C cells. It checks
unique storage keys, partitioned ranges, access kind/order/coverage and actual
bindings. No per-cell graph traversal, value snapshot or constness inference.

VerifiedFunction preparation builds this plan before source evaluation. A
default-body projection must first rebuild environment topology when revisions
change. New/old graph and topology charges remain conservatively admitted during
replacement. The cell plan is preparation-local, not an executable certificate.
Same allocation OpId on another loop iteration does not imply the same dynamic
cell; later declaration placement must preserve that lifetime.

CellLifetimes now projects checked control regions to native lexical-container
candidates: body, branch arm and loop body. Each local cell keeps its declaration
at the allocation operation in that exact container. Euler intervals check that
every direct access remains enclosed; escaping accesses refuse instead of
lifting declarations out of an arm/loop. Incoming cells remain unresolved owner
lifetimes. Candidate CellScopeIds are not actual AST ScopeIds and must be mapped
by their typed control origins, not by matching numeric IDs.

This projection is linear in control nodes/scopes, operations and cell accesses,
after the fresh cell/control checks; each access uses constant-time interval
containment. It performs no per-cell graph traversal, CFG rewrite or SSA edit.
It is called during VerifiedFunction preparation, but is NOT an independent
certificate of emitted declarations. Initialization, actual scope enclosure,
declaration positions and child-capture lifetime still require AST-side checks.
The per-iteration test specification requires loop-body residence rather than
function-body residence; that source is not yet compiled or run.

CellInitializations now supplies an initial native-cell initialization proof.
It uses the existing Lengauer-Tarjan dominator tree, traversed in preorder with
per-cell state and a bounded undo stack. Allocation resets only its indexed
cells; stores update initialization witnesses; leaving a dominator subtree
restores prior state. Each update/restore occurs once, not once per cell/CFG
pair. Every stored scalar and loaded result must have a Language domain.

The independent checker verifies supplied witnesses: exact cell, a real live
store, allocation preceding/dominating that store and store preceding/dominating
the load. It does not rebuild the candidate or require the planner's particular
store choice. An earlier dominating store can prove initialization even when
later writes change the value; this proof NEVER licenses value substitution.
Incoming reads preserve parent initialization obligations. Unknown creation
writes conservatively refuse across the whole method, including non-dominating
branches; a dominance-local barrier alone would be unsound at joins. The explicit
ClosureWrites::Language premise requires complete transitive child verification.
VerifiedFunction currently supplies Unknown, not a fabricated child summary.

After the shared cell/environment checks, planning/checking adds one dominator
construction and linear O(B+E+O+C) scans with O(B+E+O+C) bounded storage. Joins
requiring initialization phis remain refused pending that extension; this is
not claimed as general definite-assignment completion. Testing remains paused.

CellEvaluation now carries the copyable cell/initialization evidence into the
shared EvaluationPlan. Its independent check recomputes environment topology
from the current graph and checks the supplied cell/witness plans, not a new
cell grouping. Incoming captures and observed internal environment identities
remain refused. Push/current results must be unused Environment values; pop
must preserve acc. Unknown child writes remain refused by initialization proof.

EvaluationKind::kEnvironment explicitly owns those environment operations;
they cannot silently disappear as pure operations. Cell loads/stores retain
ordered evaluation and normal operand validation. This uses the established
contract excluding VM resource-exhaustion differences, not a new host premise.
The AST must still place native declarations at each proved allocation scope
and independently account for those operation placements before source success.
All nested construction/check work shares the remaining invocation budget;
no per-cell CFG walk is added. Retained evaluation storage includes both plans.

These operations are still refused by AST lowering. Required next
integration remains: proving dynamic cell lifetimes in the work graph and
capture-aware symbol binding and AST;
complete child trees validated before parent publication; dependency/SCC handling
without assuming success for cycles. P3 method 7366, independent-app examples,
per-iteration cells and child mutations still require end-to-end source evidence.
Neither this component nor its production build closes the T07 gate.
