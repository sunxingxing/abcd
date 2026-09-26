# Implementation Progress

Development is performed directly by Codex. The external Claude controller
was stopped at the user's request; it must not be restarted for this plan.
Scope remains T00 through T12 in the dependency order from the implementation
plan. No task scheduler, additional host assumptions or sidecar source inputs.

Latest state: T05 now integrates compact natural loops into checked control
regions, owned loop AST/printer syntax, independent target/scope/sequence checks,
edge-copy placement and once-only MethodEntry phi initialization. New loop and
entry provenance is typed and does not invent PCs. T07's fresh empty object/array
allocation slice and proved fresh named object initialization are integrated;
buffered literals, broader own-defines and captures are not yet admitted.
Checked native lexical declarations and actual-tree correspondence are now
integrated; occurrence-bound ordinary/arrow owners now share class-method body
preparation under source rules 20. Real cell-method source evidence, real
loop-method acceptance and behavioral verification remain outstanding. T03's G1
method slices and T04's G2 slice have prior source evidence, not whole-record
completion. Whole-record integration and test acceptance remain pending.
Sections below retain chronological evidence;
earlier rule versions/stages are observations of those earlier implementations.

Current T05 control-layer verification: production targets `abc_semantic` and
`abc_structure` built successfully after loop-control integration; whitespace
validation passed. Added control regression sources are neither compiled nor run
under the test pause. No new ABC source, behavior or RSS evidence is claimed.
See `loop-discovery.md` for the exact admitted subset and remaining source gate.
The same production targets subsequently built successfully with typed loop
syntax and target checking. Syntax regression sources also remain uncompiled
and unrun; printer support alone is not loop source-publication evidence.
The subsequent body integration removes the blanket loop/MethodEntry guards
only after adding actual generation and independent sequence/origin checks.
ABC-only G2 inspection (pasteboard 7559, record 6897) still reports rules 16,
Printed/complete METHOD, range [2638,2924), 286 bytes; CLI exits 1 because the
record remains incomplete. This is not loop coverage or generated-JS execution.

### T07 Fresh Empty Allocations

ValueObjectAllocation now imports EmptyObjectCreation with its exact accumulator
definition, original allocation effects and exceptional exit. Existing
ValueArrayAllocation also reaches source evaluation. Both have dedicated fresh
expression nodes, ordered evaluation and explicit exclusion from pure erasure;
neither is a freely duplicated constant. Shared uses retain one allocation via
the existing snapshot proof. Independent AST correspondence checks aggregate
kind, actual operands/ownership and effect order. No own-define or buffered
initializer folding is admitted by this step.

Added unrun sources cover bytecode import/correspondence, object/array kind
mutation, fresh allocation sharing and actual snapshot identity. Tests and RSS
remain paused. Production `abc_semantic` and `abc_structure` builds exited 0
after the rules-17 update; whitespace validation passed. No real aggregate-method
source success is claimed from these build results.
A candidate inspection of pasteboard method 9468 still refuses
at ContextStage; its instructions include async/exception operations and it is
not a successful ordinary-loop sample. T05 real-loop acceptance, T07 LiteralPlan,
cells/captures and the overall T00-T12 goal remain incomplete.

### T07 Own-Property Import

Added ValueOwnWrite, preserving own-store versus DefineField, pool name and IC
slot. Operand import retains receiver/accumulator read order and accumulator
preservation; input correspondence covers metadata and normal/exception effects.
Pure erasure explicitly excludes the new operation. Source evaluation/lowering
still refuses it until descriptor-aware initialization can be independently
proved. No Object.defineProperty global assumption or ordinary-assignment
substitution is introduced. Rules remain 17: this does not expand source syntax.

Production abc_semantic/abc_structure builds exited 0. Added unrun specifications
cover all four named formats, wide cache metadata, accumulator preservation,
kind/name/cache/assignment mutations and source-stage refusal. Also corrected
uncompiled earlier phi tests to compare EdgeId alternatives rather than requiring
equality on the entire IncomingKey variant. Tests remain uncompiled/unrun.
ABC-only inspection of real P3 method 7366 still reports ContextStage refusal,
not a new success; creation/capture and object initialization remain required.

### T07 Literal Initialization Proof

Added LiteralPlan and independent verification for exact empty-object allocation
identity, ordered named definitions, pre-allocation RHS availability and absence
of early observation. The verifier checks supplied groups against fresh flow and
disjoint allocation use chains, not a regenerated plan. Grouping is O(D log D),
with linear operation/use checking and bounded storage; no repeated whole-graph
walk per object. See `literal-initialization.md` for the complete domain, pinned
descriptor provenance and mandatory source integration contract.

This proof component is linked into production but not yet consumed by the
source route. Own-write lowering remains gated; source rules stay 17. Added
unrun tests cover reordered/missing/duplicate definitions, wrong receiver kind,
domain, observation, late RHS, revisions and 0/1/2/200/201/4096-field budgets.
No source/behavior/corpus completion follows from this internal proof alone.
Production `abc_semantic` and `abc_structure` builds exited 0 after this change;
whitespace validation passed. Test sources remain uncompiled/unrun and RSS
measurements remain paused.

### T07 Object AST Integration

Evaluation now consumes and independently checks LiteralPlan, with a distinct
initializer placement. Allocation and pre-allocation RHS producers are pinned;
getter/call results cannot silently move into initializers. A typed property
arena preserves every ordered data definition, including duplicate names and
computed `__proto__` keys. Independent AST checks cover actual ownership, order,
origins, keys and snapshot identities. Printer events map each PropertyId to its
own byte range; inspection output includes those events. No CFG, SSA or scope
rewrite is introduced by this source representation. Rules advance to 18.

Added regression sources cover 1/2/200/201 properties, missing proof, incorrect
placement/origin, shared syntax and pinned RHS producer mutations. They remain
uncompiled/unrun; no behavioral, full-corpus or RSS acceptance is claimed.
Buffered/late-RHS/cross-block/escaped initialization and capture recovery remain
pending, as does overall T07 acceptance.

Production `abc_semantic` and `abc_structure` builds exited 0; whitespace
validation passed. ABC-only G2 inspection (pasteboard 7559, record 6897) reports
rules 18, Printed/complete METHOD, range [2638,2924), 286 bytes. CLI exits 1
because the record remains incomplete. This existing scalar method is not new
object-initializer coverage. Added class-context fixture source describes the
ABC-to-property printer range/PC and segment transport checks but remains
uncompiled/unrun with the other regression specifications.

### T07 Creation-Specific Capture Binding

Added a shared ScopeFacts view over verified environment/local initialization,
capture barriers and defined-domain facts. The existing semantic capture
resolver now consumes it. Added CapturePlan with per-creation PC/target context,
ordered rebased cells and preserved outer-domain obligations; its checker
rechecks actual binding identities, not source names. This preserves shared cell
semantics and does not infer immutability or merge dynamic allocation instances.
See `capture-binding.md` for proof ownership, complexity and integration limits.

Production `abc_semantic` and `abc_structure` builds exited 0. Capture regression
sources are written but uncompiled/unrun; tests and RSS remain paused. Source
rules stay 18: the new batch plan is not yet consumed by the universal tree
publisher. Value IR cell/creation import, child AST publication and dependency
cycle handling remain required; T07 and the overall T00-T12 goal are incomplete.

### T07 Function Creation Value Import

ValueFunctionCreation now carries DEFINEFUNC/DEFINEMETHOD kind, target pool
index, length, home-object input and IC slot for both pinned cache widths.
Accumulator home objects retain their typed protocol SSA read; implicit current
function context remains metadata, not an invented JS argument. Normal and
pre-throw effects, fresh allocation identity and original sites use the shared
import/correspondence pipeline. Pure erasure explicitly excludes creation even
with empty effects. Source evaluation still refuses until child/capture proofs
are integrated; source rules remain 18.

Production `abc_semantic` and `abc_structure` builds exited 0. New regression
sources describe four formats, metadata/protocol mutations, unused creation
retention and source-gate refusal. They are uncompiled/unrun under the ongoing
test pause. Cell/environment Value IR, child trees and corpus acceptance remain
pending; this import is not a closure source-publication result.

### T07 Lexical and Environment Value Import

Added distinct Value IR operations for lexical load/store, environment push/pop
and current-environment reads. Original relative addresses, slot counts, optional
scope pool indices, actual stored scalar sources and internal result domains
are preserved. Shared correspondence checks effects, exception timing, register
reads/writes and metadata. None is pure-erasable or source-admitted merely
because it now has an IR representation. Source rules remain 18.

Production `abc_semantic` and `abc_structure` builds exited 0. New uncompiled,
unrun regression sources cover 4/8/16-bit cell formats, wide slot metadata,
internal environment result domains, pop/acc preservation, original effects,
metadata mutations, historical register-store input and source refusal. Cell
identity/lifetime binding into the source tree and closed child verification
remain pending. No new closure/source, test or RSS acceptance is claimed.

### T07 Working Environment Topology

BuildValueStage now retains ValueEnvironments for methods containing lexical or
function-creation operations. The adapter reuses the existing environment-flow
algorithm on OpId-indexed working operations after simplification. Results bind
IR/CFG revisions, allocation sites, per-operation environment states and cell
addresses. Independent checking walks actual block transitions and edge joins,
checks allocation metadata and resolves each supplied lexical binding, without
rerunning the planner. Removed effectful operations are rejected. Dynamic cell
lifetime, initialization and source scopes are deliberately not inferred from
this topology. Input, working graph and environment storage share byte admission.

Regression sources now include revision/state/binding mutation checks and exact
work/resource limits on simplified imported lexical graphs. They remain
uncompiled/unrun, with RSS still paused. No new source domain is opened; rules
stay 18. Cell lifetime and capture-aware AST publication remain pending.
Production `abc_semantic` and `abc_structure` builds exited 0 after integration;
whitespace validation passed. No new real-method source success is claimed.

### T07 Cell Storage and Access Plan

Added CellPlan with typed AllocatedCell/IncomingCell storage, flat ordered
per-cell access ranges and constant-time operation-to-cell indexing. Local
storage keys use allocation OpId and slot; incoming cells retain captured depth
and slot. No source name, scalar register, constness or initialization is inferred.
Independent checking verifies storage identity, read/write shape, range/order,
complete coverage and revisions against freshly verified environment facts.
Grouping sorts once rather than scanning the graph per cell.

VerifiedFunction preparation now builds the plan before evaluation and rebuilds
working environment topology after default-body projection when revisions differ.
Production `abc_semantic` and `abc_structure` builds exited 0; whitespace checking
passed. Added uncompiled/unrun regression sources cover 0/1/2/200/201 allocation
groups, repeated writes, distinct allocation identities, incoming captures,
mutated storage/access/coverage/revisions and exact budgets. Testing/RSS remain
paused. Source rules stay 18; dynamic lifetime/AST/child-tree proof is still
required, so neither T07 nor the full T00-T12 goal is complete.

### T07 Native Cell Lifetime Candidates

Added CellLifetimes and connected it after control planning in VerifiedFunction
preparation. It maps checked control structure to typed body/branch-arm/loop-body
containers, preserves allocation-site declaration placement and checks direct
access enclosure using linear Euler indexing. Incoming cells remain explicit
owner obligations. No source scope IDs are guessed, declarations are not hoisted
to make escaping accesses fit, and no CFG/SSA mutation occurs. The candidate is
not an AST publication certificate; initialization and actual declaration/use/
capture placement remain to be independently checked in generated trees.

Production `abc_semantic` and `abc_structure` builds exited 0; whitespace checks
passed. Uncompiled/unrun regression additions specify body residence, incoming
lifetimes, exact work limits and per-iteration loop-body residence. Tests and RSS
remain paused. Source rules remain 18; cell expressions/statements and closed
child ASTs are still required before any new closure source success or T07
completion can be claimed.

### T07 Cell Initialization Witnesses

Added CellInitializations and connected it to VerifiedFunction preparation.
Sparse dominator preorder plus per-cell undo state derives dominating language
store witnesses, resets at allocation and preserves incoming owner obligations.
The independent checker validates witnesses by cell identity, order and
dominance instead of replaying candidate selection. A witness is initialization
only, never the reaching value. Unknown creations refuse; no dominance-local
barrier can incorrectly ignore writes on another predecessor branch. Explicit
Language closure writes remain a conditional owner premise, not child success.

Production `abc_semantic` and `abc_structure` builds exited 0; whitespace checks
passed. Uncompiled/unrun specifications cover repeated-store witness alternatives,
invalid witness kind, exact work limits, incoming reads, loop allocation and
closure-premise mismatch. Tests/RSS stay paused. Initialization-phi joins,
actual AST declarations/accesses and closed child trees remain pending. Source
rules remain 18 and no new executable-source coverage is claimed.

### T07 Cell Evaluation Integration

Shared EvaluationPlan now owns CellEvaluation evidence and an explicit
environment-operation placement. Cell reads/writes use normal ordered operand
evaluation; environment operations are omitted from expression order only after
fresh topology, cell and initialization checks plus rejection of all observed
internal environment identities. Incoming capture obligations are not silently
turned into locals. No child-write success is assumed. Evaluation checking
validates supplied plans under the shared remaining work/byte allowance.

Production `abc_semantic` and `abc_structure` builds exited 0; whitespace checks
passed. Added uncompiled/unrun regression sources specify missing cell proof,
forged environment placement and mislabeled internal environment results.
AST lowering remains explicitly gated: declarations, actual scope/operand/origin
checks and child trees still need integration. Source rules stay 18; neither
new source coverage nor T07 completion is claimed. Tests and RSS remain paused.

### T07 Cell Symbols and Expressions

BindingPlan now allocates typed CellBindings and a direct CellId-to-SymbolId
index after scalar snapshots/phis/copy temporaries. Loaded SSA values remain
separate bindings, preventing an old read snapshot from becoming a later mutable
cell read. ExpressionForest emits ordinary SymbolExpr reads and AssignmentExpr
writes with exact cell targets and original operation origins; environment
placements intentionally have no expression and remain statement obligations.

Production `abc_semantic` and `abc_structure` builds exited 0; whitespace checks
passed. Uncompiled/unrun regression additions specify common read/write symbols,
distinct allocation symbols, original origins, environment-expression absence
and read-before-mutation snapshot retention. Complete FunctionTree construction
still refuses CellBinding until declarations and independent actual-tree checks
are integrated. No new executable-source coverage, test result or RSS result is
claimed; source rules remain 18 and T07 remains incomplete.

### T07 Native Cell Declarations

FunctionTree now emits native local declarations at original environment
allocations in their actual body/arm/loop container. Private snapshots retain
the existing prologue; cells never use it. Environment operations keep explicit
boundary anchors, and each declaration retains the allocation PC through the
existing statement origin/range tables. Multiple referenced slots share that PC
without an artificial grouping scope.

Independent TreeFlow derives cell symbols from actual allocation-site
declarations and checks order, scope, uniqueness and boundary positions.
TreeOperands checks cell identity and stored RHS correspondence; TreeScope
checks visibility, and original-origin verification handles allocation-owned
declarations. Loaded-value snapshots stay separate from mutable cell bindings.
Lookup is O(P log(C+1)+C) for P allocations and C cells, not repeated full scans.
Rules advance to 19 because source admission has changed.

Added uncompiled/unrun specifications for 0/1/2/200/201 allocations, multiple
slots sharing one PC, per-iteration scopes, wrong cell write targets, moved
anchors, altered declaration origins and forged temporary kinds. Tests,
generated-JS execution and RSS remain paused. Real cell-method printer evidence,
capture/child integration and T07 acceptance remain outstanding.

Production `abc_semantic` and `abc_structure` builds exited 0 after the rules-19
change; whitespace validation passed. ABC-only inspection of the existing G2
slice (pasteboard method 7559, record 6897) still reports Printed/complete METHOD
under rules 19, range [2638,2924), 286 bytes. CLI exit 1 correctly retains
incomplete record coverage. This is not native-cell coverage or JS execution.

### T07 Created Function Owners

FunctionCreationSites now indexes modern DEFINEFUNC/DEFINEMETHOD occurrences
with caller, PC, resolved target/attributes, full encoded length/cache metadata
and home-object input. It does not deduplicate sites sharing a target. Two
bounded body scans and one sort avoid per-target whole-file rescans; the shared
method lookup resolves caller-regional pool references. This is an immutable
occurrence index, not a reachability, environment or capture proof.

CreationTreeContext admits occurrence-bound ordinary/arrow DEFINEFUNC owners
with the existing same-file/profile/name/ABI and non-class-role contracts.
VerifiedFunction now has a single body preparation implementation for both
these owners and class members: fresh input/Value IR, defaults, CFG, cells,
evaluation, AST verification, globals and ABI checks. Dynamic ABI receiver
observations in arrows are explicitly refused. Incoming captures, child-write
obligations, async and explicit-home method creation remain unclosed. Nullable
typed creation accessors prevent class emitters from consuming a non-class
owner. The printer still rejects created-function expressions before output;
TreeVerified is not Printed or complete closure publication.

The CLI uses this path when no class-member site exists, refusing ambiguous
multiple creation occurrences instead of selecting one. ABC-only inspection of
pasteboard 7366 now reaches InputStage and refuses at PC 92 (use of an unresolved
outer capture), rather than MissingSite/ContextStage. Its PC 90 load, buffered
objects at PC 24/180, own-defines at PC 34/190 and child creation at PC 219 ->
7421 remain actual P3 obligations, not successful source coverage. Existing G2
7559 remains Printed/complete METHOD under rules 20, [2638,2924), 286 bytes;
the CLI still exits 1 for incomplete record coverage.

Uncompiled/unrun specifications cover 0/1/2/200/201 sites, both cache widths,
function/method distinction, repeated target sites, exact work limits, ordinary
and arrow owner preparation, local cells, incoming-capture refusal, arrow
receiver refusal and no output through the class printer. Testing, JS execution
and RSS remain paused. T07 and T00-T12 acceptance remain incomplete.
Production `abc_semantic` and `abc_structure` builds exited 0 after the final
changes; whitespace validation passed. Test targets were not compiled or run.

### T07 Owned Function Inputs and Capture Obligations

FunctionInput now retains same-body semantic IR/proof with immutable identity,
method, record and explicit capture mode. Conditional mode records incoming
reads and writes separately and preserves function/class/buffer child barriers.
BindFunctionInputs checks parent/child identity and resolved target before
reusing creation-specific CapturePlan. Outward obligations remain typed;
binding does not discharge transitive writes, dependency cycles or child ASTs.
The shared BuildValueStage uses Closed mode only. Source rules remain 20:
this internal step does not relax executable-source admission.

Production targets abc_semantic and abc_structure built successfully. ABC-only
inspection retains G2 method 7559 Printed/complete METHOD, range [2638,2924),
286 bytes; the record remains incomplete (CLI exit 1). P3 method 7366 still
refuses input proof at PC 92, as required before incoming captures are closed.
New regression sources specify 0/1/2/200/201 capture occurrences, read/write
order, two creation environments, forwarded obligations, identity/target/site
rejection and exact binding work limits. They are uncompiled and unrun under
the test pause. No generated JS, benchmarks or RSS checks were run. T07 and
the complete T00-T12 goal remain unfinished.

### T07 Shared Conditional Work-Graph Preparation

The normal import/correspondence/simplification/environment stages now have one
BuildFunctionValues implementation used by the existing closed source route
and available to conditional child preparation. Internal FunctionValues carries
the capture mode without a publishable route receipt. Closed BuildValueStage
explicitly rejects a conditional result. No source admission is relaxed.

VerifyFunctionCaptures checks fresh working topology and exact incoming
read/write occurrence correspondence against immutable FunctionInput, using
original sites, backlinks, PC/provenance, kind, depth and slot. It is connected
after environment rebuilding, not just an unused verifier. Work is indexed,
with no per-capture graph traversal. CellEvaluation still refuses unresolved
IncomingCell; enclosing AST symbols and owned child expressions remain pending.

Regression sources specify 0/1/2/200/201 occurrences, shared incoming cell
identity, exact checker work limits, changed origin/backlink/binding/revision,
conditional graph preparation, closed-route refusal and budget failures. They
remain uncompiled/unrun. Production abc_semantic and abc_structure built
successfully; ABC-only G2 inspection remains Printed/complete METHOD under
rules 20, range [2638,2924), with the record incomplete. No generated JS, tests,
benchmarks or RSS measurement were executed. T07 and T00-T12 remain incomplete.

### T07 Creation-Site Work-Cell Binding

Conditional FunctionValues now retains identity and the checked CellPlan once
per function. CellPlan owns a separately verified working allocation index.
BindCaptureCells projects original initialized captures into working parent
cells and independently checks them against actual work creation topology and
each original child occurrence. It preserves unresolved outward captures and
shares parent incoming cells without treating either as initialized locals.
Plans are creation-specific and revision-bound, with sparse incoming-only
targets; unrelated child locals are not scanned or copied per creation.

The conditional preparation path builds the reusable cell indexes. The binding
primitive connects it to existing BindFunctionInputs/CapturePlan, but parent
AST-symbol binding, child expression construction and transitive/SCC closure
are still not integrated into publication. No new executable coverage is claimed.

Production abc_semantic and abc_structure builds passed. ABC-only G2 method
7559 remains Printed/complete METHOD under rules 20, range [2638,2924); record
publication remains incomplete. Expanded regression sources specify distinct
creation environments, forwarded versus shared incoming cells, missing/wrong
targets, stale revisions, identity and budget rejection, uninitialized parents,
and 200/201 distinct incoming cells alongside unrelated child locals. Tests
are uncompiled/unrun; generated JS, benchmarks and RSS remain paused. T07 and
the T00-T12 objective remain unfinished.

### T07 Conditional Incoming Cells in the Shared AST

IncomingCells::kConditional now uses the existing evaluation/expression/tree
pipeline for leaf children. Incoming initialization remains an owner premise;
closure-write assumptions remain Unknown and nested creations are not admitted.
CaptureSyntax supplies typed enclosing symbol references. FunctionTree stores
capture aliases in a separate enclosing scope, never as local declarations.
TreeScope checks alias census and shadowing; TreeFlow derives actual incoming
cell symbols and TreeOperands checks accesses. Scalar snapshots stay separate.
ABI census treats explicit captures as free bindings, not extra parameters.

Standalone VerifiedFunction and its current printer explicitly reject these
conditional trees. CaptureCells-to-enclosing-symbol integration, capture-aware
name selection, actual creation visibility, child expressions and transitive/SCC
proofs are still pending. This is AST capability, not new executable coverage.

Production abc_semantic and abc_structure builds succeeded. ABC-only G2 method
7559 remains Printed/complete METHOD under rules 20, range [2638,2924), while
its record is incomplete. New capture_tree_test sources specify ordinary/arrow
leaf trees, local versus capture declarations, 200/201 distinct incoming cells,
shadowing/owner/census mutations and retained snapshots across a captured write.
They remain uncompiled/unrun under the testing pause. No JS execution, benchmark
or RSS measurement was performed. T07 and T00-T12 remain unfinished.

### T07 Parent Creation Expressions and Lexical Edges

Explicit FunctionBindings now enter the shared evaluation/AST path for normal
DEFINEFUNC with implicit current-function home input. They remain conditional
child-body/capture/write obligations. The complete creation census is checked;
Language child-write premises are explicit and matched to that census. Every
creation is pinned at its original root because scalar input order does not
prove preservation of its implicit environment. Multiple uses keep one value.

FunctionExpr owns a unique child owner and implicit parent-symbol capture edges.
TreeScope checks those references at the actual creation expression, including
branch/loop scope and shadowing. TreeOperands checks the actual child owner and
parent cell symbols, while existing flow/origin checks preserve order and PCs.
Standalone publication still rejects creation obligations; the current printer
rejects child nodes before writing a header. Child-tree assembly, real target
and capture closure, callable/name/strictness handling and SCC validation remain
pending. No executable closure coverage is claimed.

Production abc_semantic and abc_structure builds passed. ABC-only G2 method
7559 still reports Printed/complete METHOD under rules 20, [2638,2924), with an
incomplete record. New function_expression_test sources specify 0/1/2/200/201
creation contexts, distinct owners, actual capture symbols, shared function
values, ownership/edge mutations, sibling-arm visibility and per-iteration
declarations. Tests remain uncompiled/unrun; no JS execution, benchmarks or RSS
measurement were performed. T07 and the T00-T12 objective remain unfinished.

### T07 Capture Proof to Evaluation Binding

BindCaptureEvaluation now checks the genuine input/work CaptureCells mapping
and lowers it into a common FunctionBinding. Parent cells retain child incoming
order; unresolved outward captures and identical parent/child owners are typed
failures. The adapter does not discharge child-body, naming, callable or
transitive-write obligations and does not widen standalone source publication.

Production abc_semantic and abc_structure built successfully. Extended test
sources cover two contexts of one method, malformed parent mappings, owner
identity, outward forwarding, work limits and 1/2/200/201 distinct captures;
they remain uncompiled/unrun under the testing pause. No JS execution or RSS
measurement was performed. Complete immutable parent/child AST assembly and
publication remain next; T07 is not complete.

### T07 Actual Parent/Child Capture Correspondence

VerifyCaptureTree now links a checked CaptureCells map with actual parent and
child FunctionTrees and their common TreeFlow results. It independently checks
creation owner, capture census, each side's cell/symbol mapping, alias target
and cross-arena spelling. It does not compare unrelated symbol IDs or rebuild
scope analysis. Its unchanged verified-product precondition is explicit;
revision checks alone are not treated as validation certificates.

The existing input/capture regression source now constructs both trees through
BuildFunctionTree and specifies correspondence and owner/revision/target/name/
census/work-limit mutations. Tests remain uncompiled/unrun. Production
abc_semantic and abc_structure built successfully; no generated JS or RSS work
was run. Full immutable family assembly, name allocation, free-global context,
callable/strictness checks and publication are still outstanding. T07 remains
in progress; this relationship checker alone is not an executable source gate.

### T07 Capture-Aware Synthetic Names

Common tree construction now indexes supplied names once and keeps generated
formals, lexical locals and snapshots from shadowing incoming capture names.
Preferred names remain unchanged when available. Conflicts use a disjoint
monotone fallback namespace; this is binding-name allocation, not source text
rewriting or scope/CFG transformation. Supplied textual names are conservatively
reserved, including global references and property names. Metadata/debug formal
collisions still refuse rather than silently renaming evidenced parameters.

Source rules advance to 21 because generated output names can change. Regression
sources cover captured local/formal-name conflicts, deterministic rebuilding and
0/1/2/200/201 occupied fallback names; tests remain uncompiled/unrun. Complete
family assembly and publication remain outstanding; this does not close T07.
Both production targets built successfully. ABC-only G2 method 7559 retains
Printed/complete method coverage at [2638,2924) with rules 21; the enclosing
record remains incomplete (CLI exit 1). Generated JS was not executed and RSS
was not measured. Distinct-cell parent/child test sources now also exercise
1/2/200/201 captured cells alongside same-numbered child local declarations.

### T07 Owned Conditional Function Families

BuildFunctionFamily now owns the supplied common tree/flow/capture construction
products together, derives exact parent edges from FunctionExpr nodes, rejects
missing/duplicate/unreferenced owners, runs capture correspondence on each edge
and computes parent-first order with an iterative cycle-detecting walk. Owner
IDs need not equal vector positions. Additional layout/scratch is bounded and
does not rescan ancestor chains per function. Syntax cycles and runtime closure
recursion are explicitly different concerns.

Production abc_semantic and abc_structure built successfully. Regression sources
use the shared input-to-parent/child-tree fixture with reverse member order,
1/2/200/201 captures, missing children/proofs, duplicate owners and resource/work
limits. They remain uncompiled/unrun; no JS execution, benchmarks or RSS work
was performed. The result is owned conditional construction state, not frozen
publication evidence. File-driven family preparation, callable/name/strictness,
root enclosure, transitive effects and printing remain outstanding. Rules stay
21 because executable admission is unchanged. T07 is still in progress.

### T07 File-Driven Conditional Inputs

ReadFunctionValues now owns actual decoded body, file-derived ABI frame,
immutable original FunctionInput and shared working FunctionValues. It has
typed failures, unchanged-file checks and combined retained-capacity admission;
existing per-pass work limits are not claimed as cumulative family accounting.
The explicit --conditional-values inspection selector enters this path before
legacy decoding, emits no source/receipt and never substitutes for --value-source.

Both production targets built successfully. ABC-only observations:
- Pasteboard 7366: conditional input passes; Value IR import refuses at PC 24
  (buffered object allocation). This replaces the closed-input capture refusal
  only in the explicit conditional diagnostic, not executable admission.
- Pasteboard 7421: input rejects exception regions (SemanticError::kExceptionRegions,
  diagnostic PC 0). Instruction inspection also shows async transfers. Thus
  complete P3 family publication needs T09/T10 capabilities as well as T07;
  an ordinary closure-only path cannot honestly complete that gate.
- Pasteboard 7559: 43 working operations, zero incoming captures/child obligations.
- Immersive 21082: 21 working operations, zero incoming captures/child obligations.

All conditional commands return exit 1 with source_complete=false and
executable=false. File-backed regression sources cover ABI/input retention,
conditional versus closed captures, identity and memory rejection; they remain
uncompiled/unrun. No generated JS, benchmarks or RSS measurement ran. Full
file-driven family traversal and publication are still pending; T07 remains open.

### T07 Buffered Object Value Import

ValueObjectAllocation now retains an optional typed original literal pool slot.
Both modern buffered-object IC widths import with the original PC/effects and
fresh result. Empty and buffered objects cannot compare equal accidentally.
Evaluation, empty-object own-write folding, expression construction and actual
AST operand checks explicitly refuse unresolved buffers rather than printing
an empty object. Buffer contents/properties/methods remain unresolved obligations.

Production targets built successfully. ABC-only conditional P3 method 7366 now
advances past PC 24 and refuses import at PC 110 (opcode 126, external module
load); G2 7559 still prepares 43 working operations. Neither report is executable
source. Regression sources cover narrow/wide slots and mutations replacing or
changing the literal slot, plus empty-object source-gate rejection. The existing
no-child-proof creation test expectation was corrected to the current ownership
failure. Tests were not compiled/run; no generated JS or RSS work ran. Rules
stay 21 because buffered-object executable admission has not been opened.

### T07 Typed Buffered Object Constants

ResolveTreeConstants now resolves primitive data buffers into shared typed
ObjectConstant ranges and ConstantProperty records. It groups allocation uses
by literal pool slot, preserving key order and primitive values without invoking
the legacy writer. ReadObjectBuffer's existing validation is reused and now
meters entry/key/comparison work with a typed work-limit failure. Growing output
arenas use geometric capacity rather than per-buffer exact reallocation.

Unsigned integer values follow the pinned runtime's uint32 constructor; values
above int32 use exact double bits. String values enter the common text arena.
Methods/accessors/duplicate keys remain refused. Source evaluation still rejects
buffer allocations until common AST lowering and operand/origin checks are
connected. Constant metadata is not T07 completion or executable publication.

Extended regression sources specify primitive values, __proto__ data keys,
200/201/402 fields, repeated slots, unsupported buffers and work limits. Tests
remain uncompiled/unrun; generated JS and RSS work remain paused.

Production abc_semantic and abc_structure built successfully after fixing the
shared budget default's header visibility. ABC-only G2 7559 remains Printed with
complete method coverage, rules 21 and [2638,2924); its enclosing record is still
incomplete. This does not validate buffered-object AST lowering, still pending.

### T07 Primitive Buffer Properties Through Common AST

Buffered allocations with resolved primitive properties now lower through the
common evaluation/expression/tree path to ObjectExpr/DataProperty/TreeLiteral.
Each creation owns distinct syntax nodes even when constants share a range.
Output-size admission includes all instantiated fields, not just unique buffer
size. TreeFlow independently checks property census and allocation origins;
TreeOperands checks each ordered key and exact literal kind/value, including
raw double bits and TextId. Existing computed-key object printing is reused.

Unresolved buffers fail during constant/construction checks. Own-write merging
onto buffered allocations remains refused, and methods/accessors/duplicate keys
are outside the primitive constant adapter. Source rules advance to 22. New
test sources build real imported buffered instructions through CFG/evaluation/
AST, with value/origin mutations and 200/201/402 properties; they remain
uncompiled/unrun. Complete P3/family output, exceptions/async and later T tasks
remain open. This step is not whole-stage completion.

Production abc_semantic and abc_structure built successfully. ABC-only G2
7559 remains Printed/complete at [2638,2924) under rules 22; enclosing record
remains incomplete. This unchanged non-buffer sample is only a route inspection,
not behavioral evidence for new buffered syntax. No JS or RSS checks ran.

### T07 Buffered Prefix and Own-Write Composition

The existing fresh-object LiteralPlan now admits buffered allocations as
receivers without weakening same-block order, early RHS availability or
unobserved-object checks. Common ObjectExpr construction emits the resolved
primitive buffer prefix followed by the verified ordered own-write suffix.
Repeated computed keys remain ordered; no spread clone, deduplication or new
object identity is introduced. Unsupported buffer contents still fail constant
resolution before source admission.

TreeFlow independently derives actual prefix counts and verifies each suffix's
PropertyId/origin. TreeOperands compares the prefix against typed constants and
each own-write against its original RHS/key. Output/scratch admission includes
the extra indexed prefix counts. Rules advance to 23. Regression sources cover
empty/buffered origins, repeated __proto__ keys, 1/2/200/201 writes, malformed
placements and retained observed/late-RHS refusals; they remain uncompiled/unrun.
P3 is still blocked by further import/family/async requirements, not declared
complete by this local feature.

Both production targets built successfully. ABC-only G2 7559 still reports
Printed/complete method coverage at [2638,2924), now rules 23; enclosing record
remains incomplete. This unchanged method is not behavioral validation of the
new composition. No tests, generated JS, benchmarks or RSS measurements ran.

### T07/P3 Typed Module Reads in Working IR

Local/external module loads now import as ValueModuleRead with typed space and
slot. Original domains, effects, exception completion and PC are retained.
Pure-operation erasure excludes these reads, and evaluation/source lowering
continues to refuse them without an enclosing module-binding proof. This is
not a constant import snapshot, a global name, or a new Language-value premise.

Both production targets built successfully. ABC-only conditional P3 7366 now
prepares all 85 working operations at revision 1, with one original capture
occurrence, one indexed cell and three child obligations. It no longer fails
import at PC 110. The command still exits 1 and reports source_complete=false,
executable=false; no AST/publication/async completion is implied.

Regression sources cover narrow/wide local/external slot identity, u16 maximum,
space/slot tampering and retained source refusal/order checks. They remain
uncompiled/unrun; no generated JS, benchmark or RSS work ran. Rules remain 23
because module-read executable admission has not expanded.

## T00

Implemented route admission, identity-bound receipts, central validation,
checked serialization, parsing and obligation closure. Evidence rules are now
version 2; schema remains 2 and working revisions are explicitly absent.
Invalid receipts cannot be serialized as successful evidence. CLI publication
handles typed serialization errors. Holdout hashes exclude all development
path aliases; G1/G2/G3 independent candidates are explicitly unfrozen until
method-level inspection establishes their capabilities.

Regression specifications cover CLI option ordering, receipt contradictions,
missing fields, numerical narrowing, direct body/template/entry/record guards,
and a legacy-success versus required-route-refusal function. These tests have
NOT been compiled or executed under the current test pause.

Production build evidence after `e23d304`:

```text
cmake --build build --target abc_structure abc_semantic -j2
exit 0, both tools built
```

ABC-only inspection on 2026-09-26, without paired source:

```text
build/abc_semantic /home/rabbit/sample/abc_with_code/pasteboard/modules.abc 7586 --value-source
exit 1: route=value_tree, stage=decoded, coverage=unsupported
reason=route_unavailable, legacy_writer_used=false, evidence_rules=2
file_size=112024, record=6897, work_revision=null
```

This is expected admission refusal, not recovered source. Behavioral regression
and resource validation remain unverified. Existing review findings have been
addressed in implementation/specifications; this does not assert tests passed.

## T01 In Progress

Normal operand mapping, owned work graph, SSA import, use-chain checking and
independent input correspondence traversal are implemented. The method CLI now
calls BuildValueStage instead of refusing every decoded method at admission.
It builds fresh input analysis/proof without legacy AST/printer participation.
Receipt schema/evidence rules 3 require a working revision from ValuesImported
onward; pre-import and legacy products keep typed absence.

ABC-only inspection of pasteboard method 7586 now reports:

```text
stage=values_imported, coverage=incomplete, work_revision=0
operations=21, values=29, uses=23, blocks=3, edges=3, exceptional_exits=3
legacy_writer_used=false, failure=null, source_complete=false, executable=false
exit 1 (no complete source yet)
```

This supersedes its T00-only route-unavailable observation above. Independent
application/profile candidates, entry/phi adversarial cases and stronger
input/owner verification remain outstanding; no full T01 validation claim.
Regression specifications and generated-JS/resource execution remain paused.

Independent application/profile observation: Immersive method 21082, record
19072, hash `8de95a4c6579b38133367422d43ffac146cba5e8222a874df4c3d7942380dc6a`,
profile 13.0.1, reached ValuesImported through the same CLI. Its decoded body
contains named reads at PC14/36, a branch at PC24 and named write at PC40.
The graph has 21 operations, 29 values, 23 uses, 3 blocks/edges/exceptional
exits. Candidate 12473 was inspected but has no branch or member effects and
is not counted as G1 evidence. No paired source or holdout inputs were read.

Input correspondence now checks entry definition kind and formal ordinal
against FrameLayout, not merely the value domain. An entry-loop fixture keeps
both MethodEntryKey and its real backedge input; this specification is unrun.

### Remaining Input Work

Use immutable SemanticMethod/SSA and its source proof as the input layer.
Import a separate owned ValueProgram with typed operation operands, explicit
read occurrences and source correspondence. Retain entry-phi inputs and
per-operation exceptional exits. Reject handler/resume inputs at the normal
import boundary. Do not invoke the legacy SourceAst or a legacy printer to
fill missing stages. Admission progress must produce truthful intermediate
receipts, not complete-source claims. T02 through T12 remain outstanding.

### T02 Candidate Edits Started

Internal ReplaceUse and ErasePureOp now maintain bidirectional occurrence lists
without scanning unrelated users. Erasure retains stable IDs and marks origins
ElidedPure. The structural verifier understands removed occurrences and rejects
live users of removed definitions. Observable/throwing operations and control or
owner operations cannot be erased by this primitive. Regression specifications
cover multi-use relinking, removal and effect retention; execution remains paused.

The internal SimplifyValues batch now admits original capacities, one candidate
copy and all simultaneous scratch. It resolves identity-copy chains with a
linear functional-graph walk/path compression and replaces each occurrence once.
Reverse liveness starts at observable/control/owner roots and includes protocol
operands and phi dependencies. Dead phi cycles are detached as a batch, avoiding
erase-to-fixpoint scans. A failed edit, budget, revision or structural check leaves
the original untouched; a successful nonempty batch increments IR revision once
without changing CFG revision. A no-op batch preserves the revision.

The pure-op allowlist is exhaustive over the current operation variant; even a
member read with incorrectly empty effects cannot become erasable. Specifications
cover copy lengths 200/201/4096, multiple uses, stale/overflow revisions, budget
rollback, copy cycles, unused getters and dead phi cycles. They remain unrun.
The production abc_semantic target builds with these changes.

The independent transformed effect/input correspondence checker and source-stage
integration are described below. Complete source emission remains outstanding.

### Working Flow Verification

Normal import and both sides of a simplification batch now run VerifyValueFlow.
It checks stable operation placement, complete block/edge ranges, terminal
control shape, dominance and within-block definition order. A normal result is
available after the producing operation, never before it or after an always-
throwing completion. Each phi must have exactly one input for each incoming edge,
plus MethodEntry for block zero. Edge IDs preserve parallel branch alternatives;
an entry input must be an actual entry definition, not a loop-body value.

The verifier reuses the existing iterative simple Lengauer-Tarjan dominator
implementation with a remaining-work allowance. Sparse phi keys are sorted to
check uniqueness and combined with predecessor counts to prove completeness:
O(I+V+U+(B+E) log B+P log P), no value-by-block or phi-by-edge matrix. Additional
storage is admitted alongside the graph; dominance builder peak storage is
conservatively charged even after its scratch is freed. This is not RSS evidence.

Unreachable blocks and resume flow are explicit unsupported normal-import cases,
not silently discarded instructions. Rules revision 4 records the stronger
ValuesImported boundary; receipt schema stays 3. Unit specifications include
parallel edges, duplicate/missing phi inputs, unavailable values, malformed
terminators, unreachable blocks and exact dominator work limits. They are unrun.

Production abc_semantic build passed. Fresh ABC-only inspections of pasteboard
7586 and Immersive 21082 both passed the rules-4 import gate with the same
21 operations / 29 values / 23 uses / 3 blocks / 3 edges / 3 exceptional exits.
Both reported ValuesImported, revision 0, no failure, no legacy writer and
incomplete/non-executable output (exit 1). No test executable, generated JS,
holdout sample, sidecar source or RSS measurement was run.

### Phi Dependency Components

SimplifyValues now runs a single iterative Tarjan traversal of live phi
dependencies after identity-copy canonicalization. Components finish in dependency
order. A component collapses only when all external inputs resolve to one value
whose nonempty domain is no broader than each replaced phi's domain. Components
with no external input or distinct external values stay intact. One final
occurrence pass updates users, followed by reverse liveness and normal-flow
verification inside the existing rollback/revision transaction.

The additional algorithm is O(V+U) with explicitly admitted arrays and stacks;
no recursion, per-variable CFG walks, repeated input rescans or dense relation
matrix. References remain those in algorithms.md; this is a local implementation,
not a new third-party dependency. Tests specify chains and rings of 200/201/4096
phis, mixed external inputs, domain refinement, and the unanchored/self-only
case. The full batch rejects malformed entry flow before SCC processing. Test
execution and RSS measurement remain paused; no ValuesVerified/source claim yet.

### Verified Rewrite Boundary

VerifyValueRewrite independently checks the proposed representative map instead
of rerunning copy/SCC optimization. Only pure identity copies and phi equations
may substitute a value. All equations preserve or refine domains, and a reverse
use-list traversal must ground every alias in a fixed representative. Equations
without an anchor are rejected. Graph layout, operation payloads, input read
identities, incoming-edge keys, observable effects, exceptional exits and source
PCs must remain unchanged. Only proved pure deletion may mark an origin ElidedPure;
old tombstones cannot be resurrected or silently rewritten.

This O(V+U+I+E+origins) checker runs inside the admitted transaction, before revision
commit. It complements, rather than replaces, both normal-flow checks and the
immutable input correspondence verifier. The public analysis/value_simplify.h
boundary requires verified input identity/effects. The source wrapper establishes
those premises from a fresh method/proof before calling it. Rules 5 expose the
result as ValuesVerified with its committed revision, never as complete source.
CLI counts now distinguish retained operations/values/uses from stable-ID arena
slots. The structural source tree, binding/control plans and printer are still
required before publishing executable source. Test and RSS execution remain paused.

Fresh production-build ABC-only observations: pasteboard 7586 and Immersive 21082
both reached ValuesVerified under rules 5, revision 1, without legacy output or
failure. Each retains 7 operations, 9 operand occurrences and 15 value entries
from stable arenas of 21/23/29; all 3 original exceptional exits remain. They
still report incomplete/non-executable (exit 1). This is real pipeline evidence,
not execution of the regression suites or proof of full-corpus coverage.

### T03 Syntax and Control Foundations

The new FunctionTree is separate from legacy SourceAst. It owns typed expression,
statement, symbol and lexical-scope arenas, name/literal storage, source-origin
references and explicit no-text control anchors. Expressions contain syntax IDs,
not registers or ValueIds. Working operation IDs occur only in validation side
tables. Storage measurement includes arena capacities and UTF-16 payloads with
checked arithmetic; it is not a tree certificate or a process RSS measurement.

BuildControlPlan consumes the verified working CFG without invoking old regions
or source writers. For the G1 DAG subset, Kahn ordering plus reverse-topological
postdominator construction finds shared continuations. The recurrence is
postdom(v) = {v} union intersection(postdom(successors)); with successor trees
already built, the intersection's immediate root is their LCA. Binary lifting
provides O(log B) LCA, with O((B+E) log B) time and O(B log B+E) storage. A virtual
function exit handles separate return arms. There is no repeated set intersection,
per-variable walk or branch-body duplication. This local implementation adds no
dependency; its prerequisite is acyclicity, not a claim to handle general loops.

An independent VerifyControlPlan checks revision, unique block ownership and every
edge placement against the original CFG, including parallel taken/fallthrough
edges. Region syntax is not a lexical scope and does not modify basic blocks.
Cycles, cross-entry shapes and uncovered blocks are typed refusals. Specifications
cover diamonds, skipped/early-return arms, nested branches, parallel edges, stale
revisions and 200/201/4096-block chains. Execution remains paused. Control/AST
construction is not yet connected to source publication; the current public route
still stops truthfully at ValuesVerified.

### T03 Evaluation Ownership

BuildEvaluationPlan now assigns every retained operation exactly one evaluation
root or one inlining occurrence. A typed schema specifies JS operand order for
the G1 operation subset; protocol reads, unresolved live phis and operations
without an admitted syntax shape are explicit refusals. It does not infer order
from opcode names or SSA read ordinal.

Within a basic block, each candidate subtree carries a contiguous ordered-effect
interval. Reverse operand traversal admits only a suffix of the effects already
executed before the consumer. Pure ancestors carry their inlined children's
intervals too, so they cannot hide a getter moving across a write. Rejected
inlining remains a snapshot at the original operation; multiple uses and
cross-block uses retain distinct typed temporary reasons. Heap reads/writes and
control operations are always ordered. Pure DAGs are not recursively duplicated.

VerifyEvaluationPlan independently expands the proposed forest using an explicit
stack, compares its ordered occurrences with the original block sequence, and
checks unique ownership and same-block producer availability. It does not rerun
the suffix-selection algorithm. Construction and checking are O(I+V+U), with
admitted storage and work limits. This is an evaluation certificate, not yet a
binding, lexical-scope or complete source-tree certificate. Regression sources
cover two getters, reversed operands, writes between reads and consumers, effects
inside pure subtrees, shared values, cross-block snapshots and 200/201/4096-node
expression chains. Tests and RSS execution remain paused.

VM FalseOrZero/TrueOrNonzeroNumber predicates need additional care: their exact
JS expansion can read the operand repeatedly. Non-Boolean producer expressions
therefore keep a PredicateSnapshot, even for a single SSA use. Proven Boolean
results may inline because the predicate reduces to one Boolean evaluation.
This rule applies to unary results and branches, and the independent verifier
rejects a forged inlining plan that would duplicate a getter through a predicate.

### T03 Binding Identities

BuildBindingPlan consumes verified evaluation ownership and imported ABI entry
facts. Parameter identities map directly to SymbolId, retaining unused formals;
receiver identities remain typed receiver bindings, not synthetic local slots.
Only evaluation snapshots allocate temporary symbols, with producer/value
identity and the original TemporaryReason. Live callable/new-target/unknown
entry reads and live phis are refused rather than initialized to undefined.
The same mapping applies to ordinary functions and constructors; it does not
prove receiver initialization, callable kind, strictness or declaration scope.

Mapping is linear with pre-admitted arrays and bounded work. Regression sources
cover direct parameter return, unused formals, receiver return, unsupported ABI
reads, resource refusal and 200/201/4096 formal counts. They are not compiled or
executed while the test pause remains in force. The production abc_semantic
target builds. Name assignment, tree assembly/verification, printer and source
positions remain pending; the CLI still stops at ValuesVerified, not executable
source. This is progress within T03, not acceptance of T03.

### T03 Expression and Statement Construction

BuildExpressionForest now lowers the verified evaluation forest into owned
Literal/Symbol/This/Member/Unary/Binary/Logical/Assignment nodes. Each property
read remains its own occurrence. FalseOrZero and TrueOrNonzeroNumber lower to
their exact predicates; repeated reads clone only binding/literal/receiver
leaves, never effectful subtrees. Boolean operands use the one-evaluation form.
ToNumeric and increment/decrement remain typed refusals until their
Number-or-BigInt preserving syntax is implemented. Pool text resolution remains
an adapter obligation; syntax nodes contain NameId/TextId, not pool operands.

BuildFunctionTree connects ControlPlan, EvaluationPlan and BindingPlan to raw
function syntax. Iterative region assembly creates nested lexical block scopes,
If/Return/Expression statements and explicit function-body snapshot declarations.
Snapshot assignment stays at the original evaluation root. Direct formal and
receiver reads produce no VM register declarations. Unused formals stay in the
signature. Validated debug/metadata names share the existing conservative ASCII
boundary; generated $arg/$value names cannot collide with admitted input names.
Name uniqueness uses sorting; expression and statement assembly are linear in
their input/output sizes and use admitted arenas, not recursive graph walks.

Origins retain input site, PC and optional original PC; pure identity lowering
combines provenance without copying PC lists. Normalized jumps use a zero-width
BlockBoundary at the exact child ordinal, not a misleading enclosing-block exit.
Operation placements connect retained operations to expressions, statements or
these anchors. No text-based origin inference or legacy AST/writer is involved.

This product is deliberately named ConstructedFunction, not VerifiedFunction.
Independent scope/ownership/effect/edge/origin verification and a frozen printer
API are still pending, as are callable/strictness evidence integration, ABC pool
adapter integration and public source-route advancement. Construction alone is
not source acceptance. The CLI still stops at ValuesVerified. Regression sources
cover a branch with two distinct reads and PC40 write, a shared getter snapshot,
both repeated-read VM predicates, retained original PC, names/constants refusal
and work-budget failure. Production abc_semantic builds; tests, generated-JS
execution and RSS measurements remain paused.

### T03 Independent Syntax Validation

VerifyTreeStructure checks the actual constructed arenas, without replaying
BuildFunctionTree. It requires one executable parent per node, rejects cycles,
unreachable nodes and overlapping/orphan child slices, and walks deep trees
with an admitted explicit stack. Function/body/block scope ownership is checked
against actual nesting, not PC order or basic-block numbers.

TreeScopeCheck sorts symbol spellings once, then uses active binding slots on
scope entry/exit. Each SymbolExpr and assignment target must resolve to that
exact symbol, including shadowing: a reference to an outer SymbolId beneath a
same-name inner binding is rejected. Parameter/body redeclarations, duplicate
bindings, missing/duplicate declarations, const assignments and invalid names
are refused. Name validation now reuses the existing ASCII identifier/reserved
word boundary directly on UTF-16 views without allocating a conversion buffer.
Scope queries are constant time; construction is O(S log S) name comparisons,
plus linear syntax/adjacency traversal and spelling work.

Origin verification checks canonical backward-only origin DAG links, complete
part-list ownership, anchor positions, unique operation placements and anchor
ownership. PC/instruction/edge correspondence still requires IR verification;
the syntax checker does not infer it from arena sizes. Builder output passes
this independent structural check before it is returned, sharing the builder's
remaining budget while both construction scratch and output are still live.

TreeStructure is deliberately not a frozen source certificate. It does not
prove TDZ/initialization, original effect occurrence/order, edge correspondence,
callable/receiver semantics, or constant-pool fidelity. These remain required
before the printer/publication boundary. New regression sources exercise
shadowing, cycles/shared nodes, declaration/name errors, invalid origins and
anchors, and 200/201/4096-deep trees; existing function-construction fixtures also
invoke the independent checker. Production builds pass. Tests and RSS execution
remain paused; no T03 completion or executable-source claim is made.

### T03 IR-to-Tree Correspondence

VerifyTreeFlow now consumes the independently checked tree and walks its real
statement sequences against ControlPlan and EvaluationPlan. It checks every
retained operation placement, branch arm, return, jump boundary and root
expression. Snapshot declarations are private function-body let bindings;
their unique assignments remain at the original evaluation root, with the exact
typed TemporaryReason. Swapped arms, extra statements, moved snapshots and
incorrect jump boundaries are refusals. This is a linear indexed walk, not
BuildFunctionTree replay or textual comparison.

VerifyTreeSemantics adds operand/operator checks to that flow result. Parameter
reads refer to the ABI-indexed formal symbol; receivers retain their owner;
inline inputs refer to the exact producer node; snapshot reads refer to the
actual assignment target recorded by the flow walk. It checks named/computed
property access and writes, literal values/float bits, binary operators, supported
unary conversions and both VM predicate expansions. Every repeated predicate
operand must still resolve to its original input; changing a comparator, key,
receiver, returned value or snapshot symbol cannot pass as equivalent syntax.

The origin pass compares the full imported origin prefix (site, PC, optional
original PC and retained/elided disposition), then verifies operation-to-node
membership through combined origins. Combined chains have unique expression
ownership, and each part edge is visited once; there is no per-operation search
through an arbitrary provenance DAG. Snapshot declarations use matching binding
origins; root statements, snapshot assignments and jump anchors retain their
actual operation origin. Unused or invented combined/binding origins are refused.

BuildFunctionTree now runs the complete structural/flow/operand/origin check
before returning ConstructedFunction. Verification shares its remaining work
and memory allowance; only already-admitted tree storage is recredited while
builder scratch remains live. Regression sources mutate branch arms, operators,
assignment operands, member names, return literals, original PCs, combined
origins and revisions, and cover bounded-work refusal.

This remains a conditional correspondence proof: the input IR must have passed
import/rewrite checks, constants must be resolved from the same ABC, and callable
identity/strictness, complete ABI parameter census and name evidence still need
to be bound at the source adapter. No immutable publication/printer capability
is minted from this report alone. The public CLI still ends at ValuesVerified;
printer/position emission and real-method adapter integration remain pending.
Production builds pass; all new tests and generated-JS/RSS runs remain paused.

### T03 ABC Constants and Creation Context

ResolveTreeConstants reads only same-file regional pool references and validated
ABC string entities. It sorts/deduplicates pool slots, then resolved entity
offsets, so aliased slots and repeated instructions do not reread the same
payload. Name/literal namespaces retain typed IDs and exact UTF-16 text. Named
element keys retain the existing explicit refusal; they are not silently
reinterpreted as ordinary property access. The result records method/value/CFG
revision, and method-owned storage/work is admitted before materialization.

ClassMethodSites is a reusable immutable file-level creation-occurrence index.
It scans ordinary DEFINECLASSWITHBUFFER sites and decodes each unique class
buffer twice (count/admit, then populate). Every method occurrence retains the
creator, PC, literal, property ordinal, key, placement, kind and encoded length.
Queries are binary searches, not whole-file rescans for each method. With C
class references, L unique-buffer entries, M methods and O output occurrences,
the cost is decoded bytes + C log C + L log M + O log O, plus bounded reader
work. Dense occurrence output is honestly counted/admitted, not discarded.
Only one decoded method or literal/class-buffer pair is live as scan scratch.

ClassTreeContext resolves one selected occurrence against MethodRoleIndex,
local method membership, initial attributes, runtime name, class key, call-type
annotation and ABI frame. Initial NONE is not used alone as evidence. The
pinned v7 DefineFunctionTemplate refinement and absence of competing roles
justify the ordinary method context; source class methods have strict syntax.
It preserves the selected occurrence rather than claiming a unique owner for
every invocation of a reused method. Unproved/unsupported profiles, conflicting
roles, sendable/async/accessor kinds, reserved keys and signature-length mismatch
remain typed refusals at this stage. Later callable/default/async work must
close those obligations before expanding admission.

Regression sources cover pool/entity aliases at 200/201/4096 references, named
element refusal, a real encoded class site, context/name/ABI matching and
signature/name mutation refusals. They are not run during the test pause.
Production builds pass. The immutable function publication owner, pure printer
and public route integration are still pending; these adapters alone do not
advance the CLI beyond ValuesVerified or satisfy T03 acceptance.

### T03 Immutable Function Preparation

VerifiedFunction now owns the selected creation context, checked FunctionTree
and route receipt behind a move-only, read-only interface. Its factory decodes
the selected ABC method itself, runs the fresh value proof and rewrite route,
constructs control/evaluation plans, resolves same-file constants, recovers
debug-backed parameter names where proven, and builds independently verified
syntax. No public constructor accepts a raw tree or caller-created certificate.
Unproved debug names use synthesized parameter bindings; failed context,
resource exhaustion and changed input remain typed refusals.

The factory preserves actual method identity and strictness rather than
publishing an ordinary function with different constructibility. Its receipt
stops at TreeVerified with incomplete coverage. The printer still needs to
consume this facade; public CLI integration and PC-to-output spans remain
pending. Preparation alone does not meet T03 acceptance.

Method-owned live arenas and builder transfer storage share a byte allowance;
file-level indexes remain caller-owned. Later preparation passes share work,
but the constituent BuildValueStage work counters are still separately bounded.
This is not proof of whole-process memory or whole-method work compliance.
Regression sources cover immutable access, successful encoded-method preparation,
identity/signature preservation, context rejection and a zero-byte budget.
Production compilation is the only verification performed; tests, JS execution
and RSS measurement remain paused.

### T03 Pure Printer and Real Method Slices

PrintFunctionTree consumes only VerifiedFunction, an output stream and budgets.
Its explicit action stack emits the supported literals, symbol/receiver reads,
properties, unary/binary/logical expressions, assignments, blocks, declarations,
if and return. It performs syntax/escaping/parenthesization only: no bytecode
reader, SSA access, source-fragment rewrite or legacy writer. Numeric emission
preserves negative zero and uses arithmetic for NaN/infinities without referring
to mutable global names; string emission preserves UTF-16 code units.

Node begin/end events produce half-open UTF-8 byte ranges. Structural anchors
are looked up by sorted block/child boundary, not rescanned for every statement.
With N nodes, A anchors and O output bytes, traversal is O(N log(A+1) +
A log(A+1) + O), plus bounded literal/name scans. Arena ranges and the origin
DAG stay shared; printing/inspection does not copy transitive PC sets into
every ancestor. Operations identify their actual execution placements. Container
node provenance is not evidence that an elided copy executes over its range.
Output and scratch/work have explicit limits; failed partial output cannot be
published. These bounds are not whole-process RSS evidence.

The method CLI now builds same-file lookup/roles/class sites and prepares the
unique selected occurrence before the legacy path. It explicitly refuses
ambiguous sites and unsupported class/record source requests rather than serving
a method fragment as a larger artifact. Context/tree/print failures have typed
receipt reasons and central stage validation. Evidence rules advanced to 6.
File-level resource failures are not mislabeled as ordinary unsupported syntax.

ABC-only inspection exposed and fixed a real adapter mismatch: recovered formal
names append to the constant name arena, so the borrowed constant view must
describe that final namespace. Existing member NameIds retain prefix positions;
no verifier was weakened to admit the old mismatched counts.

Production build: `cmake --build build --target abc_semantic -j 2`, exit 0.
The following production inspections ran without paired-source/sidecar input:

```text
build/abc_semantic /home/rabbit/sample/abc_with_code/pasteboard/modules.abc 7586 --value-source
build/abc_semantic /home/rabbit/sample/abc_with_code/Immersive/modules.abc 21082 --value-source
Both: stage=printed, coverage=incomplete, failure=null, work_revision=1,
legacy_writer_used=false, fragment=class_member, method_slice_complete=true,
source_complete=false, executable=false, exit=1.
```

Pasteboard emits `setInitiallyProvidedValue(params)` with two `pathStack` reads
and a `this.pathStack` assignment, with no accumulator/register declarations or
parameter copies. Actual byte ranges are read PC14 [45,61), read PC36 [113,129),
write PC40 [95,130). Immersive follows the same route for `buttonText`: read PC14
[45,62), read PC36 [115,132), write PC40 [96,133). Input identities remain those
recorded above. Empty taken branches and explicit return-undefined remain;
the printer does not invert branches or erase statements for appearance.

Added unrun specifications: immutable-owner printer ranges/limits/stream errors;
receipt-stage negatives; updated source_route_cli_test; shared independent
getter/Proxy-write/exception trace oracle in value_tree_slice.mjs, registered as
value_tree_pasteboard_slice_test and value_tree_immersive_slice_test. The latter
also checks class-method name/length/nonconstructibility and PC placements.
These replace the plan's provisional per-PageA harness filename, not its scope.
Tests are not compiled/run; generated JS and RSS are not executed/measured.

Remaining T03: FunctionEmission ABI/signature/position transport at the old
record/base_function boundary before VM declarations; broader printer/adversarial
cases and authorized acceptance runs. A class-member inspection is not proof of
record integration, all-function migration or full T03 completion.

### T03 ABI Census and Callable Emission Boundary

VerifiedFunction now retains its source ABI census alongside the creation/frame,
signature and tree. BuildFunctionAbi scans symbols, operand occurrences and
expression arenas once: O(S+U+X+P) time and O(S+P) storage. It distinguishes static
IR operand counts from static source-reference counts, preserving differences
caused by safe pure predicate expansion. These are not dynamic invocation/read
counts. Formal ordinal and SymbolId stay linked even for unused parameters;
receiver references retain the checked owner. Observed callable/new.target/
unknown entries, signature mismatch and unsupported parameter writes refuse.
Arguments/capture/dynamic-scope operations remain excluded by G1 admission.

The factory accounts constructed tree work/storage before the additional census.
This closes the new local ownership overlap, not the previously documented
separate work accounting in BuildValueStage or whole-process RSS obligations.

WriteClassMethod has an explicit VerifiedFunction overload at the callable
boundary. It matches method target/kind, placement/key, ABI frame, length and
effective method kind before printing; it never invokes WriteCallable, legacy
header generation, register-use scanning, captures or fallback writers. The
old overload is unchanged. ClassMemberTarget is the renamed private legacy
lookup result, avoiding collision with the AST assignment MemberTarget when
both representations are included at this boundary.

FunctionEmission is a move-only, read-only owner of the verified function,
printed positions, destination range and method receipt. Callers cannot pair a
raw tree or unrelated position table into this product. Emission retains the
origin DAG and signature/census without file-reader lifetime dependencies.
The destination start transports every local byte range by one checked offset;
the output bytes themselves stay in the caller's stream. Shape refusal writes
nothing. Other failures require the enclosing atomic publisher to discard any
prefix. A complete METHOD receipt is not a complete class/record receipt.
Evidence rules 7 record this distinction; enclosing coverage is not upgraded.

The production method CLI now exercises that overload, not just the printer.
Fresh ABC-only inspections of methods 7586 and 21082 both report stage Printed,
method coverage Complete, no legacy writer, one simple formal (length 1), two
parameter operands/references, one receiver operand/reference, zero parameter
writes and unobserved callable/new.target/arguments. Source/PC spans remain as
above; complete segment lengths are 157 and 160 bytes respectively. The public
report still has source_complete=false, executable=false and exit 1 because
no containing record was emitted.

Production `abc_semantic` compilation succeeded. Regression sources add ABI
census mismatches, work bounds, owned segment lifetime, nonzero segment offsets,
wrong target/frame/key refusal, offset overflow and method/record coverage
distinction. They are not compiled/run; no generated JS execution or RSS run.

Remaining T03 integration is concrete: the record/class assembly path must
select this overload using the actual creation occurrence and transport child
emissions/receipts before its self/VM declarations. It must not merely inherit
the CLI method success. Default/rest/constructor/closure cases still require
their planned later-stage proofs. T03 is not marked validated or complete.

### T03 Function Segment Transport

PrintedSource and PrintedTemplate can now retain typed FunctionSegments. Each
segment shares one immutable FunctionEmission and records only its enclosing
begin/end coordinates. ClassSource replay, SourcePrinter's class/leaf sinks,
legacy callable wrapping, capture binding and late record declarations carry
those envelopes forward. They do not flatten the new origin DAG into old spans,
reparse JS, reconstruct ABI declarations, or upgrade the container's route.
Sinks that cannot retain new-route evidence explicitly refuse it.

Capture rebasing checks that no capture occurs inside the complete G1 member:
endpoint capture cursors must match, and byte length must remain the emitted
method length. Container prefixes, earlier capture insertions and declaration
insertion move both endpoints together. Publication checks segment byte bounds
and unresolved-capture bounds before returning a source artifact. Transport is
linear in segment count, with logarithmic capture-prefix lookup per endpoint;
there is no per-PC source scan or ancestor-origin expansion.

VerifiedFunction and FunctionEmission expose conservative persistent-capacity
estimates, distinct from preparation scratch. Parent buffers admit segment
arrays plus retained payloads; repeated shared occurrences are conservatively
charged again. This is capacity admission, not allocator-accurate or RSS proof.
The function factory also accounts ABI work before measuring retained storage.

Regression specifications reuse the encoded-class fixture's real sealed method
to cover nested class-template replay, an earlier capture, wrapper prefixes,
no-capture replay, missing sink refusal, interior-capture rejection, resource
refusal and 200/201 segment collections. No test-only raw certificate API was
introduced. These specifications remain uncompiled/unrun under the test pause.

Production build `cmake --build build --target abc_semantic abc_structure -j 2`
completed with exit 0. Fresh ABC-only inspections of 7586 and 21082 still produce
their complete method slices, rules 7, with enclosing source/executable false.
No JS or RSS run occurred. The transport is not yet activated by record method
selection: the next integration step must select actual creation occurrences,
invoke the explicit class-member route and serialize child segment evidence.
Until then no real record-level migration is claimed.

### T03 Explicit Record Member Migration

The record API now accepts an optional ClassMethodSelection: one immutable
file-level ClassMethodSites occurrence and its input identity. CreationExpansion
borrows this selection for the synchronous invocation. ClassWriter matches the
actual creator, creation PC and property ordinal before invoking any legacy
Body/parameter/register setup for that member. Matching is O(1) per property,
not a whole-file lookup or per-member scan. The selected route verifies the
member afresh, emits the complete signature/body through the new writer, and
retains its FunctionSegment in the private class template. Selected failures
never retry a legacy body. Unselected code remains explicitly legacy.

Preparation and printing have disjoint byte/work grants; the retained tree is
charged in the parent's segment budget. Existing process-level memory and
BuildValueStage work-accounting limitations remain open. The record API refuses
an otherwise emitted result if the exact selected occurrence/identity did not
survive transport. RequireValueTree still refuses the legacy record shell.

`abc_semantic INPUT METHOD --record-value-slice` is the explicit mixed-record
inspection entry, mutually exclusive with all other mode flags. It serializes
the record text, each child's complete receipt and local origin/position tables
with the relocated enclosing byte range. It never infers positions from text
and never labels the entire mixed record value_tree or complete. Exit remains 1.

Fresh ABC-only inspection of Pasteboard method 7586 now emits its real PageA
record shell with one value-tree segment at [2307,2464). The 157-byte member
retains its verified signature, two distinct pathStack reads and write, with
no legacy register declarations inside that member. All other record methods
remain legacy. This is the first actual record-path migration, not a fabricated
class wrapper around a method-only inspection.

Independent Immersive method 21082 still emits as a standalone verified member,
but its containing record refuses at creation PC 159 (SourceError::kCreation).
No partial record or invented child receipt is published. That enclosing-class
blocker must be traced before claiming cross-application record integration.

Production abc_semantic and abc_structure builds succeeded. Added an unrun
ABC-only integration specification comparing the embedded member and all local
position/origin tables with standalone emission, plus option-conflict refusals.
Tests, generated-JS execution and RSS measurement remain paused. T03 remains
in progress; this does not satisfy its behavioral-validation acceptance gate.

### T03 Independent Record Refusal Investigation

ABC-only debugger inspection localized Immersive's outer creation-PC159 refusal.
The inner failure is LinearLexicalError::kInstruction at PC24 of method 20505,
`ProductTitle.initialRender`, an attached prototype method. That instruction
creates child method 19292. It is not the selected method 21082 and not a failed
FunctionSegment rebase. The selected method's complete standalone emission must
not be used to certify this unrelated closure's body. No rejection predicate
was relaxed, no fabricated method body was inserted, and no legacy fallback was
added to the selected route. Nested creation/capture support belongs to T07;
the whole independent record remains incomplete until that capability exists.

### T06 Parameter Entry Identity

Started the scheduled T06 parameter work after T03's first real embedded slice;
this is implementation progress, not closure of T03's paused validation gate.
BuildParameterPlan now records each ABI ordinal's original live SSA ValueId,
with typed absence for unused/unrepresented entries. It rejects duplicate live
entries and out-of-range ordinals instead of silently merging parameter
identities. Later OpResult/phi definitions cannot replace an invocation entry.
BindingPlan consumes this explicit mapping before assigning parameter symbols.
The module is callable-kind independent and never consults register spelling.

Complexity is O(V+P) time and O(P) storage with prepaid work and vector-capacity
admission. BindingPlan retains and charges the plan; its existing downstream
forest budget includes the added storage/work. Missing formals remain present
in source signatures, not fabricated undefined SSA entries. This step does not
move default prologues, mutate CFG/scopes, recover rest, or authorize mutable
parameter aliasing. Those remain subsequent T06 work with signature-placement,
initializer scope, length/arguments and old-value snapshot proofs.

Unrun regression sources cover 0/1/200/201/4096 formals, absent unused entries,
later definitions versus invocation entries, duplicate/out-of-range entries,
and byte/work refusal. Tests, JS execution and RSS measurement remain paused.

Production abc_semantic/abc_structure compilation passed after integration.
Fresh ABC-only inspections still emit Pasteboard's selected segment at
[2307,2464) and Immersive's complete standalone method 21082. Both retain the
existing rules-7 method receipts and incomplete enclosing coverage.

### T06 Value-IR Default Candidates

Added default_candidates as a read-only Value-IR discovery stage. The old
RecoverParameterDefaults consumes register SSA and already-folded SourceAst;
its output is not reused as a new-route certificate. The new discovery starts
at the invocation entry and follows only consecutive undefined-test diamonds.
It records the original parameter entry, exact comparison/branch, both header
edges, sole default-arm edge, and the phi's two incoming edge identities.
The first ordinal must agree with encoded length; subsequent ordinals increase.
Foreign arm/join predecessors, entry backedges, wrong comparison polarity,
non-parameter tests, non-admitted fallback literals and extra arm operations do not
match. It does not search arbitrary body conditionals for defaults.

Literal fallback operands are typed IR values. The numeric-negation case
retains its original OpId and computes a Number-bit literal from an integer
operand, preserving -0 and avoiding int32 negation overflow. Conservative
operation effects are NOT deleted by this recognition: the original operation
remains part of the candidate and later migration must prove the refinement.
Arbitrary parameter/object coercion is not treated as a constant initializer.

This is intentionally NOT a signature-migration certificate and is not yet
consumed by source publication. No CFG, phi, declaration or signature changes
occur. Next implementation must verify all original-entry/comparison/initializer
uses, bind the merged parameter version, preserve signature origins, and prove
parameter/default/body scope plus length/arguments compatibility before erasing
the prologue. Explicit undefined defaults whose phi disappeared also still need
a separate proof. ClassTreeContext's existing signature admission stays intact
until that integration exists; candidate count is not decompilation coverage.

Discovery uses dense block/phi occurrence indexes and bounded constant-size
matching: O(B+E+O+V+U+P) time, O(B+V+P) admitted storage, no per-parameter graph
walks. Returned capacity/work are separate from the admitted scratch peak.
Unrun regression sources cover exact join edges, polarity, foreign entries,
entry backedges, -1/-0, coercion rejection and resource/revision bounds. Tests,
generated JS execution and RSS measurement remain paused.

### T06 Independent Default Value Proof

DefaultValueProof independently checks the proposed default candidates; it
does not call FindDefaultCandidates again or trust its inferred polarity or
fallback literal. The proof is move-only and has no public raw constructor.
Its inputs must have passed normal-IR import/rewrite, use-link and dominance
verification. It validates the candidate proposal rather than replacing those
whole-graph contracts.

Dense block/value/formal indexes bind each candidate to its entry, comparison,
initializer and merged phi. The checker verifies exact source/target edge IDs,
incoming predecessor counts, consecutive prologue ownership, comparison
operands, branch polarity, literal bits, phi incoming keys and all live use
occurrences. Old entry uses outside the comparison/merge, comparison-result
escapes and initializer-result escapes refuse migration. A merged value can
be used by the remaining body but not fed back into the removed prologue.
Unexpected operations/effects or additional affected-block phis refuse.

Integer Number negation is independently evaluated, including negative zero;
only the imported scalar-negation effect envelope is admitted. This operand
proof refines conservative user-coercion/throw flags without dropping an
arbitrary user's conversion. Every affected live operation retains a typed
DefaultOpPlacement naming its parameter, ready for signature-origin mapping.
No PC is inferred from source text and no CFG edit runs in this checker.

Complexity is O(B+E+O+V+U+P+D) time and bounded dense indexes plus retained
candidate/placement arrays. All arrays share one allowance; work is charged
for construction and scans. D is the number of proposed defaults. The result
is an IR-value proof only: source scope/TDZ, native parameter-environment and
callable ABI proofs and publication integration are still pending. No new
default source signature is emitted yet, and T06 remains incomplete.

Production abc_semantic/abc_structure compilation passed. Unrun regression
sources now include tampered literals/operation IDs, escaped original values,
escaped comparisons/initializers, negative-zero refinement, stale revisions
and work refusal. Static inspection also corrected two earlier uncompiled
fixture references (Float64Bits.value and the actual SpecialValue enum).
Tests, JS execution and RSS measurement remain paused.

### T06 Literal Parameter AST, Scope and ABI

Tree structural verification now visits literal default initializers in the
parameter scope before entering the body scope. Each initializer owns its own
expression occurrence: sharing a body expression or another parameter's node
is rejected. Literal-only admission rules out reads of body names, later
parameters in TDZ, receiver initialization or computed properties. Parameter
symbol uniqueness and body lexical redeclaration checks remain in force.
Nonliteral initializers and rest are still unsupported; this is not a blanket
default-expression scope proof.

FunctionAbi distinguishes Simple from LiteralDefaults and verifies length as
the index of the first initializer, including an explicit undefined initializer.
The full ABI parameter count remains independent of native function length.
The census rejects incompatible length, rest and nonliteral initializer nodes.
It is a wrapper-facing shape check, not authorization to move an ABC prologue.

The pure tree printer can render these literal parameter nodes with its same
typed expression printer and byte-range table; no source-fragment substitution
or bytecode access was added. Initializer actions drain before the next formal
and before body emission. Class-member emission accepts the corresponding ABI
layout only from an immutable VerifiedFunction. There is no public raw-tree
publication constructor.

The factory still admits only its previously proved simple signatures: the
default-proof-to-body transformation and independent signature-origin/ABI
integration are not finished. Therefore this change does NOT yet emit default
signatures from real ABCs. Route evidence rules remain 7 until the new factory
path is authorized. Added unrun structural/census cases cover -1, -0, explicit
undefined, shared-node refusal, parameter/receiver reads in initializers, rest
and native length mismatch. Tests, JS execution and RSS remain paused.

Production abc_semantic/abc_structure compilation passed. An ABC-only PageA
record inspection still retains method 7586 at [2307,2464), rules 7 and
incomplete enclosing coverage. This is a compatibility observation, not a
default-signature end-to-end acceptance result.

Production `abc_semantic` and `abc_structure` compilation completed successfully
with the discovery module included. No default-parameter output success is
claimed before its proof and publication integration.

### T06 Residual Default Body

Static review corrected a discovery assumption: normalized phi inputs must be
ValueIds, not literal operands. Literal defaults now follow the retained
ValueCopy producer (or numeric-negation producer), and the independent checker
validates that producer's operand and effects. The fixture now follows the same
ValueId-only phi contract rather than modeling an invalid normalized graph.

DefaultBody is a move-only owner of a residual graph and its signature proof.
Its factory builds a fresh proof against the exact supplied graph; matching
revision numbers alone cannot authorize a caller-supplied proof. The original
graph is unchanged. The residual graph interprets parameter entries as already
initialized source formals, so it is NOT a standalone equivalent ABC method.
Final publication must pair it with the verified native default signature.

Projection removes only proved prologue operations, replaces merged default
values with initialized formals, compacts edges and rebuilds live use chains.
Stable operation/value/block IDs preserve provenance. Empty prologue blocks
remain reachable along a linear fallthrough path. Original and residual graphs
both undergo fresh use/normal-flow/dominance verification. Removed prologue PCs
remain retained origins for pending signature placement, not ElidedPure origins.
The original input-correspondence certificate is deliberately not copied.

Projection scans and indexes are linear in graph size, in addition to the
existing flow verifier's cost. Scratch, graph copies and proof storage share a
bounded allowance; proof and projection work are deducted from one work budget.
This is not process peak-memory evidence. Regression sources cover unchanged
input, revised CFG, rebuilt uses, residual tree construction, stale/tampered
inputs and budget refusal; these tests have not been compiled or executed.

Production abc_semantic and abc_structure builds passed. DefaultBody is not yet
connected to VerifiedFunction publication: typed signature PC placement and
complete signature/body correspondence remain required. No real default-method
output, JS behavior equivalence or T06 completion is claimed. Tests, generated-JS
execution and RSS measurement remain paused.

### T06 Signature Position Events and Body Origins

PrintedFunction now owns a formal-ordinal range table. The printer records each
complete parameter (name plus any initializer) directly during emission, without
including comma separators. FunctionEmission capacity accounting includes the
table; method and record inspection serialize it together with the symbol and
optional initializer ExprId. It remains fragment-local UTF-8 byte evidence and
travels with the existing immutable method envelope.

The body root previously inherited the first entry operation's origin even when
that operation was removed. This was unsuitable for moved default prologues and
already attributed ordinary body braces to elided parameter copies. Construction
now walks the straight entry prefix to the first retained operation; independent
flow verification reconstructs that choice. Empty/tombstoned prefix blocks do
not own the body, and a live branch stops the scan before either arm. Work is
bounded and charged; the original CFG and scopes are not changed by this rule.

Production abc_semantic/abc_structure builds passed. ABC-only inspection of
Pasteboard method 7586 and Immersive method 21082 emitted the unchanged 157/160
byte method fragments, respectively. Both locate params at [26,32), with no
initializer. Their body-root origin now points to retained PC14, not elided
PC0. The tools intentionally exit 1 because neither fragment is a complete
record. This is not generated-JS execution or behavioral acceptance.

Unrun regression sources cover formal spans, record transport, retained root
origins and refusal of a tampered root that names the removed default prologue.
SignaturePlacement construction and its independent default-to-source proof
remain pending; adding output events does not authorize default publication.
Tests and RSS measurement remain paused; T06 is still incomplete.

### T06 Default Signature Construction and Independent Check

FunctionTree now has typed SignaturePlacement records (operation, ParameterId,
origin). Structural verification checks their parameter/default association and
OriginalSite shape and rejects operation ownership duplicated within signatures
or between signature and body tables. Capacity accounting includes the table;
inspection serializes it separately from body operation placements. Its formal
ordinal refers directly to the printer's existing parameter range events.

BuildDefaultSignature adds dedicated literal initializer expression occurrences
and complete prologue placements from the sealed DefaultBody. It does not move
body syntax or infer defaults from generated text. On failure its construction
tree must be discarded. VerifyDefaultSignature independently checks the actual
nodes, exact literal values (including numeric bits), parameter ordinals, removed
operation identities and original site/PC evidence against that owner. It also
requires a strict ordinary class-method context; broader callable/environment
admission remains a separate task.

Moved-origin membership propagates once through the backward origin DAG, then
checks every body expression, statement and anchor. This rejects migrated PCs
leaking into body origins without expanding transitive PC sets per expression.
All signature placements must be present exactly once. Independent verification
includes full structural/scope/ownership checks. Its correspondence is limited
to the signature: residual body semantics, original input identity, parameter
census and callable ABI are still mandatory, separate publication obligations.

The signature-specific scans are linear in parameters, operations, syntax and
origin adjacency size; structural verification retains its existing sorting
cost. Constructor and verifier charge storage/work conservatively under one
allowance, including the tree and reserve replacement peaks. This is not a
whole-process RSS claim.

Production abc_semantic/abc_structure compilation passed with both new modules.
Unrun regression sources cover complete literal attachment plus residual
semantic checking, modified literals, missing/duplicate placements, wrong PCs,
body-origin leakage, stale revisions and resource/work refusal. Static review
also fixed an ambiguous IntegerLiteral name in the uncompiled fixture after
both source and analysis literal types became visible.

VerifiedFunction still does not admit default signatures from ABC. Next is
factory integration of ParameterPlan/discovery, DefaultBody, syntax attachment,
fresh body/signature/ABI checks and matching route revisions. No T06 completion,
real default-source output or test success is claimed. Tests, generated-JS
execution and RSS measurement remain paused.

### T06 Verified Factory Integration (Rules 8)

VerifiedFunction now conditionally runs parameter-entry planning, candidate
discovery and DefaultBody construction when encoded native length is less than
the formal count. Class context accepts that metadata shape but does not certify
defaults. Failure to prove the prologue refuses preparation; it never falls back
to a legacy writer. The original graph remains alive while the residual graph
passes control/evaluation/tree construction. Signature attachment independently
checks actual default nodes and placements, then fresh tree semantic verification
checks the completed tree against the residual body before the callable ABI
census. Only that joint result can construct the immutable published owner.

Receipts use the residual graph revision and evidence rules 8. The tree and ABI
retain full formal count, while length is checked against the first initializer.
All stages use the preparation allowance with conservative retained charges.
The previously documented constituent work accounting inside BuildValueStage
and whole-process RSS verification remain unresolved; no process-cap claim is
made by this integration.

Review of the actual simplifier found that undefined constants used by the
comparison remain ValueCopy producers. Discovery and independent proof now
accept that producer in the header, check pure effects and dominance order, and
restrict its result to the comparison. The producer itself receives signature
placement. Direct literal comparison operands remain supported. Arbitrary
constant expressions and multi-operation initializer chains are not admitted.

Production abc_semantic/abc_structure builds passed. ABC-only PageA method 7586
still emits the same 157-byte simple method with rules 8 and an empty signature
migration table. Metadata-driven inspection identified Pasteboard Logger 4658
and Push CustomButton 8705 as short-length candidates. They refuse at IR import
PC8 and PC0 respectively, before parameter migration. CustomButton begins with
lexical-environment creation and later creates a closure; no closure capability
was bypassed to manufacture a default-method success.

Unrun regression sources now include an ABC-only class-method fixture whose
default diamond should traverse PrepareClass through immutable emission, assert
the literal-default ABI, and map prologue PCs to the complete formal span.
An IR fixture covers the retained undefined producer and its escaped-use refusal.
The tests remain uncompiled/unexecuted. Factory wiring is implemented, but a real
default-method end-to-end acceptance result is still missing; T06 and T00-T12
remain incomplete. Tests, JS execution and RSS measurement remain paused.

### T06 Materialized Negative Defaults (Rules 9)

Default discovery now follows a retained integer ValueCopy feeding numeric
negation in the missing-parameter arm. This is the normal loaded-constant form,
not just a unary operation with an immediate literal operand. The candidate
records the producer's typed operation identity. Independent proof checks its
block, order, pure effects, exact integer operand and exclusive consumption by
the initializer; its result cannot escape into the body or another default.
Both operations migrate through the existing DefaultBody/SignaturePlacement
route, so both PCs remain attributable to the formal rather than being dropped.

Number negation still uses double conversion before negation, retaining -0 and
avoiding signed int32 overflow. No arbitrary user coercion, floating producer
chain, general constant folding or closure-environment admission was added.
The extra work is bounded producer lookup plus existing indexed use scans, not
a new per-variable graph traversal. Admission provenance is evidence rules 9.

Regression sources now cover a materialized 1/-1 pair, materialized zero/-0,
refined exceptional-exit removal and escaped producer refusal. The class-method
factory fixture additionally encodes ldai/neg with adjusted jump/body lengths,
using v7 ISA opcode provenance, and expects the existing immutable printer path
to render the native default. These are unrun specifications, not passing tests.

Production abc_semantic and abc_structure builds completed successfully. Real
default-method acceptance is still outstanding, alongside the broader T06
parameter-version/scope/callable cases. Tests, generated-JS execution and RSS
measurement remain paused. T00-T12 remains the active, incomplete objective.

## T04 Call Operand Occurrences

Started the planned ordinary-call prerequisite while retaining T06's incomplete
status. The fixed scalar operand array cannot represent variable call ranges.
CallOperands is a separate, owned compact view over typed OrdinaryCall,
ReceiverCall and ConstructorCall descriptors. It exposes indexed operands with
explicit callee/receiver/constructor/argument roles and independent read
ordinals. Repeated register identities remain distinct occurrences. No role is
inferred from opcode spelling, a register's name or printed source.

Construction checks the typed call against the normal-register effect contract,
including fixed/range counts, each read slot and accumulator result destination.
Malformed fixed counts, empty constructor ranges, register arithmetic overflow
and SSA ordinal overflow refuse. The descriptor uses constant storage and O(1)
indexed access; validation is O(argument occurrences), charging one work unit
per checked occurrence. It creates no expanded temporary argument array.

This is a contract adapter only, not a call evaluation-order proof or source
reference certificate. The existing normal importer still rejects call
operations; Invoke/Construct working nodes, adapter integration, variable-use
capacity admission, evaluation ownership, source Call AST and direct-eval/member
reference checks remain next. In particular, no mutable .call/.apply workaround
or special-case record writer was introduced. Rules remain 9 because source
admission is unchanged.

Production abc_semantic/abc_structure compilation passed. The new separate
call_operands_test source covers aliased receiver/arguments, wrong slots/result,
constructor counts, 200/201 and geometric argument sizes, the 65535 argument
boundary, ordinal overflow and byte/work refusal. It has not been compiled or
run. Tests, generated-JS execution and RSS measurement remain paused. Neither
T04 nor T06 is marked complete.

### T04 Typed Call Import (Rules 10)

Normal import now creates ValueInvoke (implicit-undefined versus explicit raw
receiver, argument count) and ValueConstruct (argument count, constructor also
newTarget). OperandView unifies scalar and compact call occurrence descriptions
without truncating ranges. Use capacity includes expanded SSA reads in addition
to scalar literal capacity and phi inputs; overflow/resource admission happens
before reserve. Both importer and independent correspondence checker charge
call-contract validation work against their remaining allowance and compare
every role/read identity. Effects, original PCs and exceptional exits remain
unchanged. New call variants are explicitly non-erasable even if supplied effect
fields are empty; unused results do not imply removable calls.

The source evaluation/AST layers deliberately still reject invocation and
construction until reference/argument ownership is proved. Evidence rules 10
version the expanded IR admission, not completed source-call coverage. No
direct-eval assumption, mutable .call shim, source-text matching or legacy
fallback was added.

An ABC-only PageA method 7613 inspection exposed a fixed-view scratch regression:
the caller supplied only one byte per use to correspondence checking, smaller
than OperandView for this method. ImportedValueScratch now exposes the required
maximum of the disjoint use-chain/view scratch lifetimes; memory failures are
reported as memory limits instead of invariant violations. Debugger evidence
identified ValueImportError::kResourceLimit at correspondence line 136 before
the fix. No process or debugger session remains running.

After the fix, production abc_semantic/abc_structure builds passed. Method 7613
reaches control_frozen/work revision 1 under rules 10, then refuses with
FunctionPrepareError::kEvaluation (inspection detail 4), as expected for the
pending call source planner. Its receiver call is at PC34, after property reads
at PC14 and PC22. PageA method 7586 still emits its unchanged 157-byte fragment.
Neither observation executes JS or proves behavioral equivalence.

New unrun value_call_import_test specifications cover ordinary/receiver/
constructor ranges at 0/1/3/200/201 arguments, exact occurrence roles, unused
call effects, exception exits, tampered call counts/roles and the correspondence
scratch boundary. Tests, JS execution and RSS measurement remain paused.
Next is variable-arity evaluation planning and source reference ownership, then
Call/Construct AST/printer integration; T04 and the full objective are unfinished.

### T04 Variable-Arity Evaluation Forest

EvaluationShape now represents a fixed role prefix followed by argument
occurrences. Count uses a widened sum; Role is bounded indexed access. Ordinary
invocation, explicit-receiver invocation and construction use distinct prefixes.
Both planning and independent verification check the full use count, roles and
result presence without allocating a per-call role array.

The existing effect-interval algorithm now handles these operations, with calls
remaining ordered events even if malformed input reports no effects. Callee and
argument subtrees can fuse only when the checked effect sequence is preserved;
shared results keep one snapshot, and unused call results keep an ordered root.
Iterative verification visits the resulting forest rather than rerunning the
planner's decisions. Work remains linear in operations and use occurrences;
range arity does not trigger repeated graph scans or recursive argument walks.

This is staged IR-input evaluation, not yet a JS reference certificate. In
particular the raw receiver occurrence cannot simply become another source
evaluation, and callee inlining alone does not prove member receiver identity or
direct-eval behavior. Call/Construct AST lowering still refuses. Source admission
and published-stage semantics remain unchanged, so evidence rules remain 10.

Production abc_semantic/abc_structure builds passed. ABC-only PageA method 7613
now passes evaluation planning and refuses at FunctionPrepareError::kTree
(inspection detail 7), previously kEvaluation/detail 4. The record and method
remain unsupported for source publication; no JS was executed. Unrun regression
sources cover getter-before-argument order, reversed order, intervening writes,
forged motion refusal, shared and unused call results, and 200/201/4096 argument
plans with role tampering and a linear work bound. Tests and RSS remain paused.

Next: a separate call-reference plan, then variable-arity Call/Construct syntax,
independent operand correspondence and printer support. T04, T06 and T00-T12
remain incomplete.

### T05 Edge Assignment Expression Trees

ExpressionForest now lowers each verified normal-edge copy schedule into owned
AssignmentExpr roots, with separate flat ranges keyed by exact EdgeId. Each RHS
is a fresh syntax occurrence; parameter/phi/snapshot reads cannot share an ExprId
between alternatives. Destinations must be provisional PhiBinding locations or
the owning bundle's CycleBinding temporaries. Incoming operation results must
already have a binding: the builder refuses to inline an unbound operation tree
onto an edge, preserving evaluation order and side effects. Synthetic nodes
carry EdgeOrigin rather than a fabricated original PC.

The forest reserves two expression nodes per scheduled copy and one origin per
bundle, checks ID arithmetic, and charges retained arrays and traversal work to
its remaining budget. This lowering adds O(C + B) time/storage for C scheduled
copies and B bundles. MethodEntry is still explicitly refused by syntax lowering
until its initialization phase is proved; it is never reinterpreted as an edge.

This is expression construction, not executable function publication. The
BuildFunctionTree phi guard remains: branch-arm/exit statement insertion,
independent actual-statement/operand/origin correspondence, and loops are still
pending. No new source coverage or rule version is claimed. Tests added for
distinct same-target edges, owned RHS leaves, retained getter snapshots,
MethodEntry refusal, work limits and 200/201/4096 scale remain uncompiled and
unrun. Production abc_semantic/abc_structure builds exited 0; diff whitespace
checking passed. No generated JS, tests, benchmarks or RSS measurements ran.

### T05 Acyclic Phi Statement Integration

FunctionTree now declares distinct PhiJoin/ParallelCopyCycle provisional symbols
and consumes ExpressionForest edge assignments. A branch's copies precede that
alternative's region, including an otherwise empty arm that directly reaches the
join. A single-successor block's copies follow its operations/jump anchor, before
the successor or enclosing join. Thus same-source/same-target alternatives stay
distinct, and conditional critical edges use dedicated source arms without
mutating the original CFG, block IDs or phi incoming keys.

The blanket BuildFunctionTree phi rejection is removed in favor of fresh
control/evaluation checks and actual syntax verification. TreeEdgeFlow consumes
the AST statements at those exact edge boundaries. It derives phi/cycle symbols
from actual assignment targets, prohibits aliasing with parameters/snapshots or
other phi values, and requires complete bundle and live-phi coverage. TreeOperands
checks every actual RHS against the independently symbolically verified schedule,
including parameter/receiver/snapshot/phi identities and literal bits. No effectful
operation can be inlined onto an edge. Origin correspondence admits EdgeOrigin
only for the checked copy roots and leaves, retaining original PC ownership for
real operations and jump anchors.

Added indexes/visits are linear in graph values/edges, scheduled copies and tree
nodes, with bounded storage charged to the existing grant. Existing phi grouping,
control, dominance and structural name/origin checks retain their own documented
costs. No coalescing or source-local/debug-name recovery is claimed for provisional
phi locations. The printer uses its existing assignment/block implementation;
no new text-rewrite route was added. Admission evidence rules advance to 14.

Unrun regression sources cover full diamond statement construction, parallel
same-target edges, retained getter snapshots, source-operand and edge-origin
mutations, phi-symbol aliasing, switched alternatives, copies moved before their
snapshot, and 200/201/4096-phi function trees. They remain uncompiled/unrun while
testing is paused. No generated JS or RSS measurement was executed.

ABC-only candidate inspection before the rules bump emitted Pasteboard method
4357 (GetValue, record 4182) as a 309-byte complete class-member fragment; it is
an early-return tree with no live phi and therefore is not phi coverage evidence.
Immersive method 7559 remained refused at ControlFrozen/TreeStage; method 8404
remained refused at input PC 0. Pasteboard 9332 lacked standalone creation
context. No successful real phi-method slice is claimed by this integration.
MethodEntry, natural loops, general cross-entry control, phi receiver call
references and real-method/differential acceptance remain pending; T05 and the
overall T00-T12 objective remain incomplete.

Production abc_semantic/abc_structure builds exited 0 after the rules-14 bump;
git diff whitespace checking passed. ABC-only reinspection of Pasteboard G2
method 7559 still emits its 286-byte member at record offsets [2638, 2924), with
Printed/complete METHOD coverage under rules 14. The enclosing record remains
explicitly incomplete/nonexecutable and the CLI intentionally exits 1.

### T05 Phi Receiver References

CallPlan now admits a private provisional phi as a member-call receiver after
fresh evaluation/phi-flow verification. Existing exact base/receiver identity,
same-block property-before-call and exclusive callee-inline ownership checks
remain mandatory. Incoming-edge copies cannot execute between two operations
in that block, and the admitted provisional symbol is neither coalesced nor a
captured/source cell. This does not admit arbitrary mutable references or use
observable `.call` lookups to reconstruct invocation.

The independent tree operand checker requires the actual phi symbol recovered
by TreeEdgeFlow from complete edge assignments. It checks that symbol against
the actual member base, rather than trusting CallPlan alone. Named and computed
member syntax use the existing owned argument arena and unified printer; raw
receiver occurrences do not cause an extra evaluation. Evidence rules advance
to 15.

Added unrun phi_call_test fixtures pass two distinct incoming parameter values
through a join, named/computed property lookup, explicit-receiver call and
return. They include forged AST base/reference mode, mismatched raw receiver
and internal-marker domain refusals. Test sources remain uncompiled/unrun.

ABC-only candidate selection also inspected pre-existing generated metadata
for MultiCommunityApplication (ABC 12.0.2.0/profile 3; SHA-256
45ffdc7d5075b4de3f1c6860dcbc929bae048097af4478d09136afbec1b6c5a8).
Method 24329 (getPadding, record 24096) and method 25469 (isShow, record 25350)
were refused at Decoded/ContextStage with MissingSite, before value analysis;
the latter was requested through record-value-slice. ClassMethodSites indexes
ordinary DEFINECLASSWITHBUFFER occurrences, not legacy/sendable sites. No
fallback site or callable evidence was invented. These observations are not
successful phi-source evidence. Legacy creation-context coverage remains work
for the shared creation adapters, not an exception for this application.

Natural loops, MethodEntry initialization and real phi-method execution/source
acceptance remain unfinished. No tests, generated JS, benchmarks or RSS runs
were performed; T05 and the full T00-T12 goal remain active.

Production abc_semantic/abc_structure builds exited 0 and git diff whitespace
checking passed. ABC-only G2 reinspection under rules 15 still emits Pasteboard
method 7559 with Printed/complete METHOD coverage and 286 bytes. The record is
still incomplete/nonexecutable (intentional CLI exit 1); this is existing-path
evidence, not a new phi-receiver real-method success claim.

### T05 Compact Natural-Loop Discovery

Added LoopForest candidate discovery for the original normal working CFG.
Existing Lengauer-Tarjan dominance identifies headers/backedges; reverse
dominator order and contracted subloops discover innermost block ownership and
immediate loop parents. Union by rank/path compression keeps representative
lookup bounded without repeated walks through deep loop-parent chains. The
result stores no inclusive member lists. Exact original EdgeIds remain separate,
including parallel backedges, and a residual topological pass rejects cycles
not removed by dominance backedges as irreducible.

ControlPlan's cycle path now invokes this analysis with its remaining grant;
natural-loop syntax pending remains kCycle, while irreducible CFG returns the
new typed kIrreducible. This is runtime integration, not source loop admission:
no Loop/Break/Continue syntax is emitted, no CFG or phi input is rewritten, and
the candidate forest is not a source certificate. Evidence rules remain 15
because source admission/output has not expanded in this step.

The algorithm choice, LLVM 18.1.3 primary-reference hash/licensing, prerequisites,
original implementation boundaries and cost are documented in
[loop-discovery.md](loop-discovery.md), with the algorithm catalogue updated.
Production storage is O(B+E); discovery uses O((B+E) alpha(B)) set operations
plus existing O((B+E) log B) dominance. Budget checks include array capacity,
dominance peak scratch, worklist entries and set-parent visits. No measurement
or process-wide memory compliance is inferred from those bounds.

Added unrun loop_forest_test sources for self/nested/non-layout-order loops,
parallel backedges, irreducible/unreachable/malformed CFGs, work/memory limits,
and 1/2/200/201/4096 nesting depths. A separately bounded slow oracle enumerates
all 4096 three-block graphs and computes dominance by vertex removal; it does
not reuse production union-find or dominator discovery. Tests remain uncompiled
and unrun. Next work must consume the forest in loop regions and independent
edge checks, then connect loop statements and MethodEntry initialization.
T05 and the T00-T12 objective remain incomplete.

Production abc_semantic/abc_structure builds exited 0 after integration;
git diff whitespace checking passed. No new real-method source claim accompanies
this loop-analysis step. Tests, generated-JS execution, benchmarks and RSS
measurement remain paused.

### T04 Call Reference Proof

CallPlan now distinguishes function-value calls, member-reference calls and
construction with typed records. Function-value calls require indirect-call
syntax, including when the callee originated in a property read or is named
eval. Explicit receiver calls require a same-block, earlier property read whose
result is exclusively inlined into the callee occurrence. Its base must equal
the raw receiver and be an immutable entry, primitive or materialized snapshot.
This deliberately refuses shared callees and arbitrary receiver rebinding; it
does not introduce a mutable `.call` lookup or duplicate a getter.

The independent verifier checks supplied property/receiver associations against
the verified evaluation forest. Build and verification are linear in operations
and operand occurrences with budgeted storage/work. ExpressionForest now admits
this proof before lowering, but Call/Construct AST lowering still refuses and no
new source coverage is claimed. The final AST checker must separately verify the
suppressed receiver occurrence and the actual syntax association; CallPlan alone
is not a source certificate. Future parameter reassignment must preserve entry
snapshots, not reinterpret these entries as mutable source parameter reads.

Production targets compiled. Added unrun regression sources for member receiver
identity, shared callee rejection, forged associations/revisions, indirect value
calls, construction and resource/work boundaries. Tests, generated-JS execution
and RSS measurement remain paused. Rules remain 10; call syntax and its source
correspondence are still the next integration step.

### T04 Call Syntax Through Printed Source (Rules 11)

FunctionTree now owns CallExpr/ConstructExpr plus a flat ExprId argument arena.
Executable nodes contain syntax IDs and typed reference modes, not working IR
IDs. Structural verification streams callee then arguments, checks ranges and
unique ownership of every arena slot/expression, and enters the existing lexical
scope unchanged. Wide calls do not create recursive walks or wide action stacks.
Storage accounting includes argument capacity.

ExpressionForest consumes CallPlan and suppresses only the proved raw receiver
occurrence. The independent operand checker derives the property association
from the actual callee syntax and checks receiver identity, snapshot stability,
inline ownership, each argument and each call kind against IR. It does not
accept the builder's plan as its source certificate. Existing evaluation proof
still establishes ordering of effects; origin/flow/scope/ABI checks still run.
The printer emits function-value calls as `(0, callee)(args)` to prevent direct
eval and implicit member binding, member calls as actual references, and
construction with the target preserved as newTarget. No `.call` lookup or IIFE
is introduced. All expression occurrences retain generated spans.

Production abc_semantic and abc_structure builds exited 0. ABC-only inspection
of Pasteboard method 7613 now reaches Printed/complete method coverage, rules 11,
without a legacy writer. The method remains a non-executable class-member
fragment, with the enclosing record incomplete. Exact 157-byte output:

```js
purgeVariableDependenciesOnElmtId(rmElmtId) {
  let $value1;
  ($value1 = this.__curBp);
  ($value1.purgeDependencyOnElmtId(rmElmtId));
  return (void 0);
}
```

PC 14 maps to the base read [74,86), PC 22 to the callee property [92,123),
PC 34 to the call [91,134), and PC 39 to return [138,154). The base snapshot is
retained because the raw IR uses it in both the property and receiver roles;
removing that snapshot needs a separate reference-aware use-count proof, not a
printer substitution. Immersive method 21082 still emits its existing 160-byte
G1 branch fragment under rules 11; this is not independent call coverage.

Unrun specifications cover named/computed calls and construction with
0/1/3/200/201/4096 arguments, argument swaps, duplicate/orphan argument slots,
invalid ranges and forged call modes. A new ABC-only-input integration test
specifies getter/callee/invoke trace, receiver identity, arbitrary thrown JS
values and the PC spans above. Neither these tests nor generated JS, benchmarks
or RSS measurement were run. T04 is not accepted as complete: broader call
shapes, reference snapshots, super, exception integration and deferred execution
evidence remain. T06 and the T00-T12 goal also remain incomplete.

### T04 G2 Global Read Import (Rules 12)

The planned G2 method is Pasteboard 7559/aboutToBeDeleted, not the smaller
member-call method 7613. Fresh ABC-only inspection before this change refused
7559 at PC31, InputVerified/import_stage. TRYLDGLOBALBYNAME is now imported as
ValueGlobalRead with a typed GlobalLookup::kName discriminator and its pool
identity. LDGLOBALVAR is separately retained as GlobalLookup::kVar: its
IC-dependent domain must not be silently equated with a free JS identifier.
Both cache widths of TRYLDGLOBALBYNAME use the existing typed lifting contract.
Global writes and record initialization remain unsupported by this import path.

Global reads consume no explicit value operands, produce the normal accumulator
value and retain input effects/exceptional exits. Independent correspondence
checks both name identity and lookup domain. Dead-value cleanup cannot erase
them, even when presented with corrupt empty effect flags. Evaluation treats
them as ordered events and materializes results across intervening effects.
No global declaration, stable value, or source binding is inferred.

Production abc_semantic/abc_structure builds exited 0. Fresh inspection now
reaches ControlFrozen/work_revision 1 and refuses at tree_stage/detail 7 under
rules 12. This is progress through import, simplification, control and evaluation,
not G2 source completion. No source or legacy fallback was produced. Unrun test
sources cover both name-cache widths, var lookup, pool/domain tampering, unused
observable results and forged motion across a write. Tests/JS/RSS remain paused.

Next integration boundary: reuse GlobalScope/AllowsGlobalReference through an
owned source-context proof. Validate exact resolved name against the actual
record imports/module bindings, class/self binding and all generated formals/
locals. Avoid per-reference scans by indexed/sorted binding facts. A detached
ClassTreeContext currently proves callable identity, not the absence of enclosing
bindings. Neither an empty GlobalScope nor the spelling SubscriberManager is
evidence. Preserve the independent source-tree shadowing check and exact global
read origin, and bind any embedding obligations to FunctionEmission so record
assembly cannot place the method under a new shadow. Name lookup is not LDGLOBAL:
do not substitute globalThis property syntax or introduce a new host assumption.

### T04 G2 Global Names Through Record Embedding (Rules 13)

GlobalNameExpr now represents a free global-name value lookup, distinct from
SymbolExpr and MemberExpr. Only ValueGlobalRead/kName can lower to it; kVar
remains refused because its IC-dependent domain is not proved equivalent.
Same-file constant resolution supplies NameId, and independent IR/tree operand
correspondence checks exact lookup kind and name identity.

VerifyTreeGlobals rejects missing context, invalid identifiers and shadowing by
any generated formal/local, record/import/enclosing parameter, class/self name
or host realm binding. It consumes the existing caller-proven GlobalScope
contract: shared complete sorted outer indexes are borrowed and queried, never
copied or sorted per method. Local symbol names are sorted once per invocation.
Complexity is O(E + S log S + G(log S + log B)) name comparisons, additionally
charging compared characters; storage is O(S). This is deliberately conservative
for disjoint local scopes. No name spelling proves a host intrinsic.

The immutable VerifiedFunction factory checks the actual generated enclosure.
PrintFunctionTree/FunctionEmission recheck the destination GlobalScope before
writing any bytes, so a prepared function cannot silently acquire a new shadow.
The verified tree retains the global-name requirements, not borrowed scope
pointers. Coordinate-only segment transport must preserve lexical enclosure;
new wrappers/bindings require reprinting with checked context. Class assembly
passes its existing record scope plus actual generated class binding. Detached
inspection does not invent an empty scope and still refuses global methods.

Unary typeof now explicitly performs GetValue with `(typeof (0, operand))`.
This preserves the ReferenceError from TRYLDGLOBALBYNAME of an absent name;
printing `typeof Name` would incorrectly suppress that error. Calls already
carry their own value/member reference mode. No globalThis rewrite, mutable
intrinsic lookup, new host premise or generated-JS text analysis was introduced.

Production abc_semantic/abc_structure builds exited 0. Fresh ABC-only inspection
of G2 method 7559 in record 6897 via --record-value-slice now reaches Printed,
rules 13, work_revision 1, complete METHOD coverage and legacy_writer_used=false.
The 286-byte method lies at [2638,2924) in the still-legacy/incomplete record:

```js
aboutToBeDeleted() {
  let $value0;
  let $value1;
  let $value2;
  ($value0 = this.__curBp);
  ($value0.aboutToBeDeleted());
  ($value1 = SubscriberManager);
  ($value2 = ($value1.Get()));
  ($value2["delete"]((this.id__())));
  (this.aboutToBeDeletedInternal());
  return (void 0);
}
```

Local positions: global read PC31 [139,156), property PC66 [212,221), argument
call PC74 [211,224), outer call PC81 [192,226). PC77 is an elided copy, not a
lost call. Detached --value-source refuses kContext/detail 0 without output.
The enclosing record remains explicitly incomplete/non-executable; CLI exits 1.

Unrun tests specify global-name correspondence, var refusal, missing/changed
enclosures, local/import/class/self/realm collisions, budgets, 200/201/4096
indexed-binding cases and global typeof GetValue syntax. The G2 integration
specification checks the actual embedded method/PC spans and an independent
getter/global/callee/argument/call trace with arbitrary JS thrown values at each
event. Tests and generated-JS execution remain paused; no behavior, RSS or full
T04 acceptance claim is made. Next work includes independent application G2
evidence, reference-aware snapshot reduction and remaining T06/T05 obligations.

### Independent G2 Candidate Inspection and T05 Copy Scheduling

ABC-derived metadata identified Immersive aboutToBeDeleted candidates 18852
(record 18101/Categories) and 21055 (record 19072/CommonView). Fresh
--record-value-slice inspection refused both in the existing enclosing record
assembly, at PC94 and PC159 respectively (error variant 5/detail 6). These are
not cross-application call successes; no missing source context was fabricated
to force standalone publication. T04 cross-application evidence remains open.

T05 now has an original implementation of the dependency-count/ready-queue
parallel-copy algorithm required by source-tree.md section 3.2. Each destination
has a distinct private provisional phi location. A sorted destination index
avoids hash worst cases; sparse user links remove dependencies once. A cycle
saves one old location into a typed CopyTempId, redirects its pending users and
resumes the queue. The next-cycle cursor advances monotonically rather than
rescanning all assignments per component. Self copies disappear without erasing
other consumers of the old value. Constants and external values stay operands,
not semantic strings. No external implementation/dependency was copied.

For C assignments, planning is O(C log C) time and O(C) space; the queue/user
traversal itself is O(C). At most C writes and C cycle saves are reserved with
checked arithmetic. Owned storage and work are budgeted. Tradeoff: a sorted
index adds logarithmic lookups but avoids a ValueId-sized dense allocation and
unbounded hash behavior. Each cycle has its own temporary; no coalescing or
minimal-local-count claim is made, and measurements remain paused.

VerifyEdgeCopies independently symbolically executes the proposed sequential
steps from distinct pre-edge tokens and checks every destination against its
original parallel RHS. It rejects duplicate input destinations, writes outside
the bundle, repeated destination writes, missing/uninitialized/reassigned/unused
temporaries and wrong final values. It does not rerun the scheduler. Exact
literal bits, including negative zero, are retained. The shared index/equality
helpers contain no scheduling decisions.

Production abc_semantic/abc_structure builds exited 0. Unrun regression sources
cover swaps, fanout, chains, self copies, constants, exhaustive small three-node
graphs, destructive sequential swaps, forged locations/temporaries, work/memory
limits and 200/201/4096 geometric shapes including disjoint cycles. No test,
generated JS, benchmark or RSS execution occurred.

This is the bundle scheduling core, not completed phi lowering. Next connect
verified phi inputs to EdgeId/MethodEntryKey bundles, provisional bindings and
proved structured edge placements. Method entry must remain distinct from a
CFG predecessor and default-parameter initialization; exceptional edges cannot
use this ordinary copy path. Existing source admission still rejects live phis,
so rules remain 13 and no new phi/loop source coverage is claimed. T05 and the
T00-T12 goal remain incomplete.

### T05 Phi Occurrence Bundles

BuildPhiCopies now connects the parallel-copy scheduler to actual ValueProgram
phi input occurrences. It first freshly verifies normal-flow availability and
requires language-value domains. It gathers live phi inputs in two linear scans,
sorts them once by IncomingKey and UseId, and builds flat ParallelCopy/UseId
arenas plus disjoint bundle ranges. Each bundle retains the actual MethodEntryKey
or EdgeId. Two branch alternatives with the same source and target do not merge;
entry-header initialization is not assigned a fabricated predecessor. Each
bundle owns the independently checked EdgeCopies schedule from the prior step.

VerifyPhiCopies checks IR/CFG revisions, freshly verifies flow, then validates
the supplied bundle ranges, strict key ordering, exact destination/RHS identity,
matching incoming key and complete one-time coverage of live phi occurrences.
It independently symbolically verifies every supplied schedule. Missing/duplicated
inputs, changed edges, stale revisions, internal-value domains and destructive
copy orders are typed refusals. The checker never regenerates the bundle plan.

For U uses and P phi inputs, gathering is O(U), grouping/scheduling is
O(P log P), and storage is O(P); independent coverage checking additionally uses
O(U) scratch. Fresh value-flow verification has its existing dominance and sparse
phi-key costs. All bundle scheduling and checks share the remaining work/byte
grant, rather than obtaining a full allowance per edge. Per-bundle transient
scratch is released before the next bundle while retained schedules stay charged.

Production abc_semantic/abc_structure builds exited 0. Added unrun regression
sources for parallel same-source/same-target edges, entry-header backedges,
rotating phi values, domain/input/edge/revision/schedule mutations, missing
coverage, resource/work boundaries and 1/2/200/201/4096-phi cases. Tests and RSS
remain paused. No new ABC source inspection claim accompanies this internal
planning step.

The graph and its control/scope structures remain unchanged. This plan is not a
source certificate: provisional symbols, edge-specific AST placement, default
initialization ordering, loops and final tree correspondence remain to be wired.
Existing evaluation/source admission still refuses live phis. Rules remain 13;
T05 and the overall objective are not complete.

### T05 Evaluation and Provisional Phi Bindings

EvaluationPlan now owns optional PhiCopies whenever live phis are present.
Planning constructs that plan with the same remaining grant; independent
evaluation checking freshly verifies its revisions, occurrence coverage, flow
availability and symbolic copy schedules. Missing or forged phi plans refuse.
Phi uses read already assigned provisional locations rather than recursively
expanding incoming expressions. An operation result used by a phi remains a
root snapshot at its original evaluation point, with CrossRegionValue reason
for a sole incoming-edge use; multiple uses still have MultipleUses reason.
The verifier rejects forged inlining into a phi-owned UseId.

BindingPlan now creates distinct typed PhiBinding entries and per-bundle
CycleBinding entries, with optional dense temporary-symbol bases. Parameters
remain first and unchanged, and normal result snapshots remain distinct from
phi locations. Symbol-count arithmetic, retained storage and traversal work are
bounded; no interference graph, source parameter reassignment or cell alias is
introduced. Cyclic and entry-key plans are representable here without claiming
that the current acyclic control/tree printer can emit them.

BuildFunctionTree explicitly refuses evaluation plans containing phi bundles
until edge-specific assignments and independent AST correspondence exist.
This gate prevents a partial implementation from emitting mere declarations
with missing incoming writes. No new phi-source coverage or rule version is
claimed. The next integration step must implement those concrete placements,
not weaken this guard in isolation.

Production abc_semantic/abc_structure builds exited 0. ABC-only inspection of
the existing G2 record slice still emits method 7559 with Printed/complete METHOD
coverage and 286 bytes under rules 13. Unrun test sources extend entry/parallel
edge and 200/201/4096-phi fixtures through evaluation and binding, and cover
getter-result snapshots, missing/forged phi plans and independent cycle-temp
symbols. Tests, generated-JS execution and RSS remain paused. T05 and T00-T12
remain incomplete.
