# Implementation Progress

Development is performed directly by Codex. The external Claude controller
was stopped at the user's request; it must not be restarted for this plan.
Scope remains T00 through T12 in the dependency order from the implementation
plan. No task scheduler, additional host assumptions or sidecar source inputs.

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
