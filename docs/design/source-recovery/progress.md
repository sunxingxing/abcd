# Implementation Progress

Development is performed directly by Codex. The external Claude controller
was stopped at the user's request; it must not be restarted for this plan.
Scope remains T00 through T12 in the dependency order from the implementation
plan. No task scheduler, additional host assumptions or sidecar source inputs.

Latest state: T03's two real G1 method slices reach Printed through the new
immutable tree/printer/emission route (schema 3, evidence rules 7), with complete
method receipts and separate incomplete record coverage. Whole-record integration
and test acceptance remain pending. Sections below retain chronological evidence;
earlier rule versions/stages are observations of those earlier implementations.

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
