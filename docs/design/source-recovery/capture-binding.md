# Capture Binding

T07 now shares a borrowed `ScopeFacts` view between the established semantic
capture resolver and the new creation-specific `CapturePlan`. This is binding
infrastructure, not permission to emit a child FunctionTree or scalarize cells.
Source rules are 23 for checked native local declarations and occurrence-bound
ordinary/arrow tree preparation; no closure-expression publication is admitted.

## Inputs and Ownership

`CaptureSite` now distinguishes an original `CreationBinding` from a borrowed
`ClassCaptureSite`. The latter carries the genuine constructor or buffer-member
`FunctionContext` and the original creator environment at
`DEFINECLASSWITHBUFFER`. Defined members keep their actual `DEFINEMETHOD`
binding; no class root is represented by a fabricated function creation.

Normal and exception `ScopeFacts` match the complete original class operation,
PC and environment. Exception matching requires the actual original Before
node, never Commit/Catch or a nearby point. Cell binding additionally checks
the creator, file identity, child method and immutable child owner stamp, then
matches the working `ValueClassCreation` through its retained input backlink.
Class points have no synthetic PC fallback. The existing initialization,
write-domain, unexposed-prefix and independent original/work cell checks remain
unchanged. Scope matching by itself does not certify the child owner or AST.

The shared target binder and verifier also handle forwarded zero-access root
cells supplied by `PlanFamilyCells`. Their obligations remain separate from
direct lexical opcode occurrences and must resolve to the same parent storage.
Plans retain owned identity/revision/site/cell evidence, never a borrowed
context pointer. Dispatch adds constant work per binding; it reuses the existing
indexed rebasing and sparse target table, without scanning all descendants or
parent operations for each capture.

Decoded class fixtures cover constructor, buffer member, defined getter and an
actual nested function whose class member only forwards the capture. They also
check uninitialized cells, wrong original environments, modified PC/targets
and original Before versus Commit. This is cell-binding infrastructure:
`ReadFunctionFamily` still rejects incoming root cells, and actual class-root
source aliases, enclosing scope visibility and family write/publication checks
must be integrated before claiming executable class capture recovery.

FunctionInput now owns a same-body SemanticMethod and SemanticProof, with file
identity, method, record and explicit Closed/Conditional capture mode. Its
private constructor requires both successful products; there is no default
invalid state or public constructor accepting arbitrary proof structs. Views
are immutable and the envelope is movable, not copyable.

Conditional mode retains all incoming lexical reads AND writes in instruction
order, plus the verifier's function/class/buffer creation barriers. Its read
ledger is independently matched to captured_reads. Write-only captures also
require an initialized parent binding under this conservative policy. Such
obligations are not proof of initialization, constness or completed children.
Closed mode preserves the existing Reject/Unknown input contract; BuildValueStage
uses ONLY that mode and cannot publish from a conditional premise.

BindFunctionInputs checks input identity, record, conditional child mode and
resolved target against the unchanged metadata index, then reuses CapturePlan
at the actual parent instruction. Its caller must supply the genuine same-file
BindCreation result; this is not a parser for untrusted fabricated bindings.
Outward captures and every transitive child obligation remain unresolved.
Envelope construction adds linear scans and O(K) storage for K incoming
occurrences beyond the existing semantic build/proof. Capture rebasing retains
the indexed bounds below. Existing upstream pass work grants are not yet a
single cumulative method-wide work account; no such guarantee is claimed here.

BuildFunctionValues is now the shared normal import, independent input
correspondence, pure simplification and working-environment implementation.
BuildValueStage calls it with Closed FunctionInput only. Conditional callers
can prepare a child working graph through the same implementation, but receive
FunctionValues, NOT a MethodRouteReceipt. Its typed capture_mode remains an
undischarged premise. The original FunctionInput must remain available for
parent-site capture binding; the mutable work graph is not an input certificate.
Body and input storage are admitted by the caller alongside this grant.

VerifyFunctionCaptures freshly checks working environments, then matches every
incoming read/write to the immutable input ledger using retained origin sites
and exact input-site-to-OpId backlinks. It checks PC/provenance, access kind,
captured depth and slot. Exact occurrence count plus unique backlinks prevents
missing/duplicated obligations. No cross-index EnvironmentId comparison, value
substitution or source-name matching occurs. Beyond environment verification,
the pass costs O(L + K log(K+1)) for L lexical accesses and K incoming accesses,
with constant additional scratch. Environment construction and this check share
their remaining work grant; scratch is admitted alongside retained topology.

This does not yet bind IncomingCell to an enclosing AST symbol, include
child-only referenced cells in parent declarations, close transitive writes,
or emit nested function expressions. The default CellEvaluation route still
refuses unresolved incoming cells. Independent child-tree and actual-scope checks remain required
before either the parent or child can receive executable-source evidence.

Conditional FunctionValues now retains file/method/record identity and a checked
CellPlan once per method. CellPlan retains its working EnvironmentId-to-allocation
OpId index and exposes indexed storage lookup. The independent cell checker
checks that allocation index against fresh topology; capacity accounting includes
it. Closed preparation does not eagerly construct this extra capture index.

BindCaptureCells connects one original creation proof to the parent's and
child's unchanged working graphs. Original parent LocalCell IDs are projected
through their original allocation instruction and current input-site backlink,
not equated with work EnvironmentIds. Working capture topology is independently
rebased at the actual creation OpId. Both descriptions must select the same
parent mutable cell for every original child read/write occurrence.

The result is a sparse, sorted child CellId-to-CaptureTarget table. ParentCell
refers to the parent's CellPlan; OuterCell retains an unresolved outward depth
and slot only when no direct parent incoming cell exists. A parent incoming cell
can be shared without discharging its own capture obligation. Child local cells
have no entries, and original initialized capture occurrences remain attached
as CapturePlan evidence. Two sites for the same target remain distinct plans.

The borrowed CaptureFunction contract requires products of the shared verified
preparation, with no graph, index or plan mutation. Revision/identity checks
reject stale combinations but are not substitutes for that precondition.
VerifyCaptureCells checks a supplied binding against original initialization
evidence and working topology, not by replaying the binding planner. It does
not revalidate whole graphs per creation; AST publication must still independently
validate the final graph/tree and all owner assumptions.

For K input capture occurrences, U unique incoming child cells, Cp parent cells
and Cc child cells, added rebasing costs O(log(Cc+1) + (K+U)(log(Cp+1)+32)),
in addition to the original CapturePlan queries documented below. Each context
retains O(K+U), not O(Cc): it never copies or scans unrelated child locals.
Original and working queries plus the independent check share a call budget.
Source scope visibility, per-iteration AST placement, child completion and SCC
closure are still separate pending obligations; this is not closure output.

## Conditional Capture Syntax

IncomingCells::kConditional now allows leaf-child evaluation through the same
BuildEvaluationPlan, BindingPlan, ExpressionForest and FunctionTree pipeline.
Default admission remains kReject. Without explicit FunctionBindings, closure
writes remain Unknown and nested creations remain refused. A separate
conditional creation path is described below; it is not child verification.
Incoming reads retain IncomingInitialization; mutable storage is still distinct
from scalar snapshots, with normal ordered read/write effects preserved.

FunctionSyntax receives explicit CaptureSyntax entries in incoming CellId order.
Each supplies a qualified enclosing CapturedSymbol and a resolved name-arena
entry, not a lexical depth encoded in source text. The constructed FunctionTree
owns TreeCapture aliases in a kCaptures scope outside parameter/body scopes.
These aliases use kCapture symbols and ordinary SymbolExpr/SymbolTarget nodes.
They generate no local DeclarationStmt and the capture scope is not an emitted
block. Actual source target existence, name equality and creation-site visibility
are still parent/family obligations, not facts certified by this child tree.

TreeScopeCheck activates capture aliases before formals, checks their census,
and rejects a parameter/local shadowing an actual captured reference. It forbids
declaring an alias in the body or claiming the child's own owner as its target.
TreeFlow derives incoming CellId-to-SymbolId correspondence from the actual
capture table; TreeOperands checks read/write identities against that result.
Local allocation declarations retain their original scopes and PCs. ABI census
recognizes explicit capture symbols as free bindings, not invented parameters.

Generated formals/locals/snapshots now avoid every supplied name, including
incoming capture aliases. A sorted NameId index uses no borrowed string views
across name-arena growth. Preferred namespaces use unique symbol indices;
conflicts enter a separate monotone `$binding` namespace. Across G generated
symbols and N supplied names, candidate attempts are O(G+N), each with O(log N)
indexed comparisons (UTF-16 comparison work is charged separately). Sorting is
O(N log N), scratch O(N); candidate storage is admitted before allocation and
discarded retries remain conservatively charged. This does not move declarations
or change scope/CFG/cell identity. Metadata/debug formal-name conflicts still
refuse. Enclosing targets require VerifyCaptureTree and complete family
validation, not numeric ID agreement. VerifyTreeSemantics remains conditional.
VerifiedFunction's standalone route explicitly rejects conditional cell mode
and capture-bearing trees; its current printer also rejects them. No conditional
tree can obtain executable publication merely by passing syntax/operand checks.
The added binding/flow passes are linear in symbols/cells/captures beyond the
existing name sort and graph checks; storage accounting includes capture arenas.

## Conditional Creation Expressions

`BuildFunctionTree` now optionally accepts an `IncomingCaptureTree`: the checked
creation-specific `CaptureCells` plus the actual parent tree/flow. In this mode
the caller must not supply hand-built CaptureSyntax rows. `BindCaptureAliases`
resolves the creation's real FunctionExpr, verifies the expected child owner and
implicit capture census, and derives each child alias from the corresponding
actual parent cell symbol. Parent local and incoming-capture symbols are both
admitted; snapshots, guessed equal SymbolIds and unresolved OuterCell targets
are not. Exact spelling and naming evidence are copied after the existing child
name arena, preserving all pool NameIds and module alias name positions.

After normal structure/flow/operand/origin checking, construction obtains a
fresh child flow and independently calls VerifyCaptureTree on the resulting
parent/child relationship. This uses the same ordinary function tree pipeline,
not a closure body writer or constructor-specific path. Missing or extra child
captures cannot disappear merely because alias construction succeeded. Mutable
cell identity, lifetime and read snapshots remain distinct and unchanged.

Alias construction costs O(K + S_out), where K is unique incoming child cells
and S_out is copied UTF-16 output. It does not scan a parent graph per capture.
The additional child flow check is once per constructed child; it uses existing
bounded shared verification. Alias/name buffers and replacement name-arena
capacity are charged alongside the syntax builder, and tree storage is credited
back only while the verifier accounts for that same retained tree. This is not
a whole-family memory or transitive-write certificate.

The existing function_input regression source now feeds actual original/work
capture proofs through this production construction entry instead of assembling
child aliases in the test. It includes empty captures, distinct creation sites,
200/201 distinct cells, nonzero name prefixes, wrong owners, stale revisions,
injected aliases, missing parent cells, outward targets and budget refusal.
Those tests remain uncompiled/unrun. File-driven family traversal, child
callable/name/strictness binding and publication remain outstanding.

`VerifyCaptureTree` connects a verified CaptureCells mapping to actual parent
and child syntax. Its borrowed trees/flows must be unchanged common
structure/flow/operand-check products; a matching revision alone is not proof.
It locates the actual FunctionExpr using the parent flow, checks the child
owner and exact capture census, resolves each parent and child CellId through
their own flow maps, and compares outgoing edges with incoming alias targets.
Captured bindings must have identical UTF-16 spelling across name arenas.
Parent lexical visibility and child shadowing are established by their common
structure checks, not reimplemented here. Work is O(K + compared name units),
with constant scratch and caller-admitted trees/proof. Outward captures remain
explicit failures. This is one relationship check, not complete family
ownership, global-name resolution, callable/strictness or publication proof.

`BindCaptureEvaluation` is the input/work-proof adapter for an actual creation.
It independently runs `VerifyCaptureCells` before constructing a FunctionBinding
in child incoming-cell order. Each edge must resolve to ParentCell; OuterCell
returns a typed outstanding-forwarding failure. Self-owner binding is refused,
and only creation forms described by the shared evaluation model are accepted.
The resulting binding can enter BuildEvaluationPlan without a caller inventing
cell numbers. Additional work/storage is O(K) for K captured cells, beyond the
existing capture verification cost. Caller-owned input/work/proof products stay
unchanged and are admitted separately; result storage and verification work are
reported. This is not a child AST, callable/name proof or source certificate.

EvaluationPlan now owns sorted FunctionBindings for every live DEFINEFUNC
creation admitted by this path. Each records a child FunctionOwnerId and parent
CellIds in child incoming-cell order. A binding is a child-body/capture/write
obligation, NOT proof that the child's method, captures or behavior match.
Missing/extra/unsorted creation entries refuse. Accumulator home-object method
creation is still unsupported. Sendability, function kind, name, length and
home-object validity must be bound through the real creation owner before any
family publication; operation shape alone does not establish them.

Explicit bindings allow the cell initializer to carry a Language child-write
premise. Evaluation verification checks that this premise appears exactly when
the creation bindings require it. No default path silently changes Unknown to
Language. Every function creation is pinned at its original ordered root:
scalar operand inlining is not a proof that its implicit lexical environment
can move over push/pop, branches or loop scopes. Multiple value uses keep one
snapshot of the single created function object.

ExpressionForest lowers creation to FunctionExpr with an owned range of implicit
parent SymbolId references in FunctionTree::closure_captures. These are lexical
capture edges, not arguments or source-text fragments. Structural checking
resolves each reference at the actual creation expression, rejects shadowing
or out-of-scope symbols, requires mutable local/capture bindings, owns every
range occurrence and rejects duplicate/self child owners. TreeOperands compares
the actual child owner and actual parent cell symbols to the conditional plan;
normal statement ordering and original-PC checks remain unchanged.

Parent capture edges are now represented and scope-checked in the AST. They
still need CaptureCells evidence tied to the child tree's actual incoming
symbols and names. Complete child trees, forwarded-only outer requirements,
name/strictness/callable syntax, dependencies and SCC closure are not yet part
of a frozen publication owner. Standalone VerifiedFunction rejects creation
bindings; its printer rejects FunctionExpr before writing any header. There
is no placeholder body or partial child output.

Added preparation/storage is O(F+K) for F creations and K capture edges, beyond
existing graph/evaluation checks. Child-owner uniqueness costs O(F log F) and
O(F) scratch; lexical references use the existing indexed name activation, not
per-capture scope-chain scans. All creation/capture arenas are capacity-charged.

## Original Capture Evidence

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
The AST places native declarations at the actual allocation container and
independently accounts for those operation placements before source success.
All nested construction/check work shares the remaining invocation budget;
no per-cell CFG walk is added. Retained evaluation storage includes both plans.

BindingPlan now appends one typed CellBinding per CellId, with a separate
CellId-to-SymbolId table. Loaded SSA values still use ordinary ValueBindings and
snapshots; they are never rebound to mutable storage. ExpressionForest lowers
cell reads to SymbolExpr and stores to AssignmentExpr with SymbolTarget, retaining
each original operation origin. Environment placements have no expression and
remain statement-construction obligations. No source-text matching is involved.

FunctionTree now emits native local `let` declarations at each original push,
in its actual block/arm/loop body, not in the private-temporary prologue. The
allocation owns a boundary anchor followed by one declaration per referenced
slot, each carrying the allocation's original origin. Empty pushes, pops and
unused current-environment operations own only boundary anchors. Printer
statement ranges and origins provide allocation-PC-to-declaration ranges;
multiple declarations may intentionally share one original PC. No grouping
block is inserted and no text is reparsed to recover positions.

Fresh TreeFlow checks exact allocation boundaries, consumes the actual local
declarations in slot order, enforces unique symbol ownership and checks actual
container ScopeIds. It derives a separate CellId-to-SymbolId table from that
syntax, not BindingPlan. TreeOperands checks every cell read/write against this
table and every stored RHS against SSA; TreeScope checks actual visibility.
Snapshot/phi/copy symbols cannot alias cells. Initialization remains separately
proved; an initialization witness never becomes a value-substitution license.

For P pushes and C referenced cells, ordered CellPlan lookup costs
O(P log(C+1)+C), with O(C) additional verifier output. All work/storage is charged
to the shared invocation grant. No per-allocation scan over all cells is used.
Native declaration syntax is integrated, but real cell-method printer evidence
and behavioral acceptance remain outstanding. Incoming captures, unknown child
writes and observed VM environment identities remain rejected.
The read-snapshot regression specification retains an old loaded value across a
later cell store and requires a distinct snapshot symbol; it is uncompiled/unrun.

Required next
integration remains: proving dynamic cell lifetimes in the work graph and
capture-aware symbol binding and AST;
complete child trees validated before parent publication; dependency/SCC handling
without assuming success for cycles. P3 method 7366, independent-app examples,
per-iteration cells and child mutations still require end-to-end source evidence.
Neither this component nor its production build closes the T07 gate.

## Creation Owners

`ResolveFunctionSyntax` is now the shared file-to-syntax naming boundary used
by VerifiedFunction, and available to file-driven family construction. It takes
an actual FunctionContext, same-method body and verified working graph, and an
occurrence-specific FunctionOwnerId. It resolves operation constants through
the existing ABC resolver, preserves their name/text prefix, and appends formal
names through the existing RecoverParameterNames proof. Unproved debug names
stay synthesized; resource or malformed-metadata failures do not silently
select a different naming path.

The returned ResolvedSyntax owns names/texts, typed operation constants and
buffered properties without retaining file readers or borrowed metadata names.
Its View must be taken before moving syntax into BuildFunctionTree, and its
operation/property arenas stay alive during construction and checking. Work
revisions are retained. Caller admits the context/input; constant/name payloads
and replaced arena capacities share the preparation allowance. Existing debug
reader work is not relabeled as a new cumulative family-work guarantee.

Constants moved to a lightweight expression_constants header so this metadata
boundary does not import register binding planning. FunctionSyntax now directly
includes its CellId dependency rather than relying on include order. The former
private parameter-name assembly in VerifiedFunction was removed, not duplicated.
No defaults, captures, module TDZ, global enclosure, callable ABI or publication
proof was relaxed. The existing created-context regression source now specifies
this boundary for ordinary/arrow contexts and conditional captured bodies,
occurrence owner IDs, 200/201 formals, budgets and context mismatch; it remains
uncompiled/unrun.

### Owned Conditional Families

ReadFunctionValues supplies file-driven members: it reads call annotations and
the actual body, derives the ABI frame, creates the immutable FunctionInput,
then runs the shared BuildFunctionValues importer/verifier/simplifier and
environment/cell preparation. It retains body, frame, input and working values
under one owned FileFunction. File/header/index/hash identity remains the
caller's same-file precondition; header identity and unchanged-file checks are
performed at entry/exit. Body/input/value retained capacities are admitted
together with each pass's additional allowance. The existing per-pass work
caps are not relabeled as a cumulative family budget. No context callable or
source receipt is inferred from successful conditional preparation.

The explicit `abc_semantic ... --conditional-values` diagnostic uses this
reader and reports open captures/children or typed failure location. It rejects
combination with other selectors, emits no source and always reports
source_complete=false/executable=false. It is not fallback from --value-source.

BuildFunctionFamily moves common tree/flow and verified incoming CaptureCells
products into one owned construction result. It indexes FunctionOwnerId values
independently of storage order, derives parent edges from actual FunctionExpr
nodes, requires every non-root member exactly once, and checks that each
incoming proof's creation maps to that exact parent expression. Every edge
passes VerifyCaptureTree. The root has no incoming creation proof; its possible
external bindings remain an enclosure obligation, not implicitly closed cells.

An iterative three-state parent walk rejects syntax ownership cycles and emits
parent-before-child order. This is not runtime recursion analysis or dependency
SCC discharge: recursive values stored in captured cells do not imply cyclic
syntax ownership. Missing children and unreferenced extra members are errors.
For F functions, E total expressions, K capture edges and L compared name units,
work is O(F log F + E log(F+1) + K + L); additional layout/scratch is O(F).
Caller admits all moved input storage alongside additional builder budget.
The returned allocated_bytes counts retained layout only, not member storage.

ConstructedFamily remains mutable internal construction state, not the frozen
publication owner. It requires unchanged previously verified input products;
it neither reparses ABC nor licenses arbitrary raw trees by checking revisions.
Free globals, callable/name/strictness, transitive effects, root enclosure and
publication remain required. No standalone printer admission was expanded.

FunctionCreationSites supplies an immutable file-level index of modern
DEFINEFUNC/DEFINEMETHOD occurrences, including both IC widths. It retains the
full FunctionCreation and MethodTarget, caller entity and creation PC. Index
order is target/caller/PC; Find returns a range, never a guessed unique owner.
Unreachable sites are retained because reachability belongs to the eventual
parent analysis. Sendable/kind metadata is retained rather than relabeled as
ordinary syntax. Historical forms outside LiftFunctionCreation's support do
not become modern sites.

For M methods, D decoded bytes, C creations and R pool regions, construction
costs O(M+D+C(log(M+1)+log(R+1))+C log(C+1)). Storage is O(C) plus one decoded
body; exact output capacity is admitted between two bounded scans. Lookup is
O(log(C+1)). File/index/lookup lifetime and budget are owned by the caller.
No source/sidecar input, method-per-target scan, or live reader is retained.

CreationTreeContext establishes initial name/frame/kind for an actual site;
FunctionContext is a typed class/created-owner boundary, not a second body
pipeline. VerifiedFunction::PrepareClass and PrepareCreated use the same
private preparation implementation. Initial admitted DEFINEFUNC kinds are
BASE_CONSTRUCTOR (ordinary function) and ARROW, non-sendable and with kUnseen
class/literal role. Arrow dynamic receiver reads refuse after ABI census.
Incoming captures remain rejected by the fresh input proof. Closure writes and
home-object uses are not licensed merely by obtaining an owner context.

This stage enables parent/child preparation without fabricating class members.
It deliberately does not print a created function, infer a lexical this binding,
assume a capture summary, or resolve dependency cycles. The required next step
is parent-site capture obligations plus owned child ASTs and proper function
expression/name/strictness emission, followed by independent graph/tree checks.
