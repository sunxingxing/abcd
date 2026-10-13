# Compact Natural-Loop Discovery

This refines the approved T05 normal-loop stage. It does not authorize source
publication without the subsequent control-edge and executable AST checks.

## Choice and Provenance

The existing bytecode `FindLoopIntervals` is not reused for working IR: its
fallthrough adjacency and numbered backward-interval prerequisites need not
survive graph transformations or block reordering. Nor do we enumerate a full
reverse predecessor closure for every header and store inclusive membership;
deeply nested loops would make that representation quadratic.

For reducible normal flow, use dominator-driven reverse discovery with subloop
contraction, the algorithmic approach documented by LLVM LoopInfo. Consulted
primary reference: locally installed LLVM 18.1.3,
`/usr/include/llvm-18/llvm/Support/GenericLoopInfoImpl.h`, sections
`discoverAndMapSubloop` and `LoopInfoBase::analyze`.
Reference SHA-256:
`708643f735c0cb13706717cc828f7bf5a5f8c4417b0a5ac4868bb4c61e9fa0d5`.
That reference is licensed Apache-2.0 WITH LLVM-exception. This implementation
is independently written for the project's strong IDs and budgets; no LLVM
source/template is vendored or linked and no new runtime dependency is added.

Unlike the reference's inclusive block vectors and outer-loop parent walks,
we retain only each block's innermost owner and each loop's immediate parent.
Union by rank with path compression tracks the currently contracted outer loop;
a separate semantic label prevents the union representative from becoming an
accidental header identity. Existing simple Lengauer-Tarjan dominance is reused.

This is a narrower *analysis domain*, not a reduced final goal: T05's structured
source admits natural/reducible loops, and explicitly refuses irreducible flow.
The initial algorithm catalogue's Ramalingam/Havlak family remains a possible
extension if non-natural loop-region recovery becomes necessary. It is not
required merely to identify and reject a residual irreducible cycle.

## Data and Invariants

- Input is the original working normal CFG. It must have one reachable entry,
  bounded successor counts, consistent edge ranges and no resume edges.
  Operand/effect/exception correctness is a separate Value IR obligation.
- `NaturalLoop` stores a header and optional parent. Discovery IDs need not
  match block/PC order. Parents are discovered after descendants.
- `owners[WorkBlockId]` is the innermost owner, not all transitive memberships.
- `backedges[EdgeId]` identifies exactly the loop headed by that edge's target;
  two alternatives with identical endpoints retain distinct original IDs.
- `forward_order` is a topological order after removing dominance backedges.
  No edge is removed from the original graph and no phi key is changed.
- Dominator preorder stamps give a linear-space reverse order in which every
  descendant header precedes its ancestors. Physical block ordering is ignored.
- An unowned predecessor is claimed once. An already owned predecessor is
  resolved through its contracted set; a new subloop is attached once, and
  traversal resumes at that subloop header's predecessors. Never expand its
  inclusive body again.
- A claimed block must be dominated by the active header. Once all headers are
  processed, removing all dominance backedges must yield a DAG. Remaining
  cycles are typed `kIrreducible`, not invented source loop owners.

## Cost and Resources

Let B be blocks, E normal edges and L natural headers (L <= B). Predecessor CSR,
dominator ordering, owner/parent arrays and the residual topological pass are
O(B+E). Each edge seeds at most one initial backedge visit and one predecessor
expansion, so a work stack reserved for 2E entries bounds pending pushes. Each
block is claimed once and each child loop is contracted once. Set operations
have union-by-rank/path-compression cost O((B+E) alpha(B)); existing dominance
has its documented O((B+E) log B) bound. Storage is O(B+E), including results.

Every array capacity, traversal and set-parent walk is budgeted. The dominance
builder receives only the remaining grant; its peak scratch charge is retained
conservatively while discovery arrays live. No recursion, per-loop whole-graph
scan, hash-map expected-time assumption or nested inclusive membership appears
in production. Tests may use a separately bounded three-block slow oracle.
Performance/RSS measurements remain paused, so asymptotic reasoning is not a
claim of measured scaling or process-wide 1G compliance.

## Integration and Remaining Gate

ControlPlan invokes discovery when the acyclic attempt encounters a cycle. It
now consumes natural loops with at most one normal exit target per loop;
multiple targets return `kLoopExits`, irreducible flow returns `kIrreducible`.
Function returns remain terminal blocks, not normal loop exits.

Each original block keeps its canonical control ID. Child loops are represented
by wrapper units in the parent region, with a separate body entry. Every backedge
or exit gets a terminal typed continue/break unit that retains its exact original
EdgeId (or the identity of a child wrapper's exit). Nested exits cascade through
wrappers without duplicating blocks or inventing original edge identities.
Euler intervals answer containment in O(1); child-first exit propagation visits
each loop once. All body entries share one acyclic region-planning invocation,
not a full graph scan per loop. Adapter size is O(B+E+L); the binary-lifted
postdominator skeleton takes O((B+E+L) log(B+E+L)) work and storage.

The independent checker follows supplied region structure and checks every arc
against the unchanged original CFG, context ownership, wrapper/body entry,
exact transfer origin, and complete one-time coverage. It does not rerun loop
discovery or postdominator planning. Shared budgets include the failed acyclic
attempt conservatively. This certifies control structure, not executable source.

BuildFunctionTree and independent TreeFlow now consume this control plan through
owned Loop/Break/Continue statements. This integration is described below; the
control plan alone still cannot authorize source publication.

Unrun regression sources include self-loops, multiple/parallel backedges, nested
headers, non-layout-order blocks, unreachable and irreducible graphs, exact work
limits, and 1/2/200/201/4096 nesting depths. A slow oracle enumerates all 4096
three-block graphs with up to two successors and derives dominance by vertex
removal, independent of production discovery. Tests are not yet compiled/run.
Control-plan regression sources additionally cover loop transfer kind/origin
mutations, header/body/exit corruption, parallel backedge identity, nested wrapper
exits, multiple-exit refusal and 1/2/200/201/4096-depth budget boundaries.

## Executable Syntax Contract

The source arena now represents `LoopStmt` with a uniquely owned block body,
and distinct `BreakStmt`/`ContinueStmt` carrying a target StmtId. Executable
syntax does not contain LoopId, ControlId or WorkBlockId. Canonical printing is
`while (true) { ... }`: header operations and the original condition stay inside
the repeated body, rather than being hoisted into an invented pure predicate.
Transfer printing is unlabelled only after structural verification proves its
target is the nearest enclosing LoopStmt. Outer-loop jumps cannot be silently
retargeted; the checked control plan instead cascades nested wrapper exits.

The existing iterative syntax ownership traversal carries the current loop
owner in each frame. Entering a child loop replaces that frame's owner, while
returning restores the parent's context without a backward ancestor scan.
Block scopes retain their existing distinct ownership and parent checks.
Transfers outside loops, transfers to sibling/outer/nonloop statements, shared
bodies and invalid scope parents are rejected. No recursion or additional
per-transfer graph walk is used. Structure tests include 200/201/4096 nesting
and exact budget boundaries; they remain uncompiled and unrun.

## Body Integration and Provenance

FunctionTree now handles original blocks and synthetic control units separately.
A wrapper creates one loop statement and one distinct block scope, and queues
its body once. Transfer nodes create typed breaks/continues to the actual loop
statement. Existing original-edge copy placement runs before each transfer:
straight-edge copies follow the source operations; branch copies prefix the
selected arm. Child wrapper exits have no additional original edge and therefore
must not duplicate a phi bundle. Loops do not create duplicate OpPlacements.

TreeFlow independently traverses actual statement sequences against the checked
control plan. It derives loop target IDs from the actual wrapper statements,
checks body ownership, transfer kind/target, exact copy boundaries and complete
child sequence consumption. It does not regenerate the AST or read printed text.
The existing operand, effect, scope and original-site checks remain mandatory.

LoopOrigin records a header identity for structural wrapper syntax or a nested
wrapper-exit transfer. EdgeOrigin records an exact original edge for direct
transfers. These origins never invent an instruction PC. TreeFlow records their
checked origin IDs; correspondence verification permits only those records and
independently checked copy origins in the synthetic-origin suffix. Original
operations keep their instruction origins and anchors, so generated positions
continue to distinguish operations from synthetic syntax.

MethodEntry bundles now have EntryOrigin and typed IncomingKey throughout copy
lowering and flow verification. They execute once after private declarations,
before original body control. JavaScript formal defaults execute before that
body phase; default-signature proofs remain separate. Flow checking consumes the
entry bundle only at that phase and requires every bundle exactly once. Moving
it into a loop, after body operations, or relabelling it as a real edge refuses.
Phi RHS values still require fresh value-flow availability and independently
checked parallel-copy schedules; this does not authorize arbitrary early reads.

Evidence rules are 16. Production tools compile, but new loop/entry regression
sources are not compiled/run under the testing pause. No generated JS has been
executed and no real loop-method acceptance is claimed yet. The existing G2
method 7559 remains Printed/complete METHOD at 286 bytes under rules 16; its
enclosing record remains incomplete. T05 acceptance and broader T00-T12 work
remain open, including real loop samples, multi-exit extension and behavior
validation when authorized.

## Private terminal regions and stable follows (Round 39)

The shared `ControlGraphView` exposes normal adjacency or the independently
checked exception/loop projection. The acyclic skeleton uses two separate trees:
entry dominance certifies ownership; lexical follow ancestry chooses layout. A
follow is not a postdominator of a path that has already returned or thrown.

On the DAG, build entry dominators by predecessor LCA in topological order. A
virtual entry precedes every scoped root, including handlers and loop bodies.
In reverse order, compute:

- `AllAbrupt(v)`: at a sink, the input/projection must certify abrupt completion;
  otherwise all successors must satisfy `AllAbrupt`. A normal try-exit port or
  unknown/nonterminating loop is not abrupt just because it has no outgoing arc.
- `ReachMeet(v) = LCA_dom(v, ReachMeet(s) for each successor s)`. This is the
  entry-dominator meet of every reachable vertex, not a normal follow.
- `Private(v)`: v is not a scoped entry, has one incoming arc,
  `AllAbrupt(v)`, and `ReachMeet(v) == v`. Thus every reachable vertex is dominated
  by v, including internal joins; another arm cannot enter this owned region.

First compute the existing straight-terminal follow preference unchanged. Freeze
all its concrete follows. At a binary branch b with a virtual follow, the summary
may supply the other arm c only if `AllAbrupt(b)`, `Private(t)`, and
`LCA_dom(b, ReachMeet(c)) != b`. The latter proves that c reaches a normal successor
also reachable from outside b's dominated region. The private t region cannot
reach that shared successor. In a reverse pass, refined successor follow ancestry
may also reveal a real common follow. No concrete old follow is replaced.
Two privately terminal arms with no shared continuation retain the virtual exit;
neither layout order nor a failed-layout retry selects an arm as a follow.

This separates additional ownership evidence from layout preference. The earlier
WIP changed the preference everywhere and moved an enclosing continuation under
an if/else. In o32/49868 that moved the captured `searchResult` initialization into
a branch, causing declaration recovery to emit `let searchResult;` plus assignment.
That layout change is rejected, irrespective of completion counts. Compatibility
requires byte-identical source for already-complete records under both flags.

The independent region checker uses the original arcs, not the builder's
summaries. It checks coverage, unique ownership, taken/fallthrough destinations,
normal continuation, loop scope and transfer identity. A zero-arc normal port
cannot end an arm whose lexical continuation still executes. For normal input,
completion is checked against the last live operation: removed origin-only NOPs
after a return do not turn it into normal completion. This scan is cached once
per vertex and charged; it does not alter the legacy layout preference. Exception
verification additionally reconstructs the projection and compares phase, try,
try-exit, inactive-node and edge mappings against immutable input.

The two ancestor tables need O(V log V) storage; predecessor CSR, work queues and
ownership records need O(V+E). Topological dominance, ReachMeet and follow passes
cost O((V+E) log V). The additional completion scan is O(I), over disjoint input
operation ranges. `ControlQuota` charges every new buffer before allocation and
every traversal/LCA step. There is no per-region reachable-set walk, recursive
region expansion, depth threshold, instruction template, or sample dispatch.
This is an independent implementation of standard DAG dominator/LCA recurrences,
using the repository's existing binary-lifting approach; no external source or
new dependency was copied. These bounds do not constitute an RSS benchmark.

Tests: `control_terminal_test` covers internal joins, external entry, shared-tail
ownership, a still-normal/unknown completion, loop/backedge mutations, swapped
arms, missing/changed continuations, wrong edge ownership, dead origin tails, and
the double-terminal virtual exit. Edge subdivision with copy operations is tested
at 200/201/400/800 regions, with exact work and byte boundaries. Forged try-exit
mapping tests remain in `exception_plan_test`. `terminal_regions_cli_test` uses
real es2abc plus independent Node source execution, including getter/call effects,
throws, receivers, return values and caller-finally traces, and rejects four
semantic source mutations. See the fixture README for the precise oracle boundary.

Under `--language-source`, Class B remains refused: a shared-decision
continuation crosses its enclosing stop. The six o19 `stop` methods (7958/13192/18648/23270/29433/35440) and o26 method
5088 now fail at original/working PC 239/234, with current region starting at
210/205 and stopping at 147/144. Renderer method 11425 fails at 227/222, with
current bounds 198/193 to 146/143. These are duplicate-ownership witnesses, not
permission to extend a region across the stop; this round does not implement B.

## Typed control failure evidence

`ControlFailure` retains the existing `ControlError` plus a typed ownership cause,
the duplicate node, the exact incoming projection arc when traversal has one,
and current/previous region bounds. An absent stop means virtual exit; a lexical
jump to a join has no invented incoming arc. `ControlLocation` distinguishes
original blocks/phases from synthetic try, try-exit, loop and loop-transfer units.
Synthetic units retain typed IDs and never acquire fabricated instruction PCs.

`ExceptionControlFailure` maps real phase locations to original and working PCs.
`PhasedControlFailure` adds the failing method, separately from parent creation
location. The enum values and resource/work failure classes are unchanged. Family
route receipts, record manifests and `abc_semantic --record-source` preserve the
leaf chain and the structured `control` witness. Serialization rejects unknown
origin tags and synthetic nodes carrying PCs. This is additional refusal evidence,
not source admission or a promise that every control refusal has an ownership
witness: other typed control failures retain their own enum without invented data.

## Bounded decision continuations (Round 40)

Decision discovery and final continuation selection are separate. `Decisions()`
records the existing layout preference and a reverse-topological last-predicate /
fallback summary. `Regions()` resolves a maximal decision once, when its actual
region stop is known, before it claims any node. Child decisions receive that
same resolved join; their input edges and conditional prefixes remain unchanged.

There are three distinct trees over the checked acyclic `ControlGraphView`:

1. **Entry dominance:** predecessor LCA, rooted at every scoped entry. Together
   with `AllAbrupt` and `ReachMeet` it proves private terminal ownership.
2. **Lexical follow:** the existing `Postdominators()` and `CompleteFollows()`
   preferences, including the Round 39 shared-continuation rule. This is a layout
   tree, not a proof that all paths pass its parent. It is unchanged here.
3. **Normal postdominance:** successor LCA in reverse topological order, with
   every sink connected to the virtual exit and **no abrupt arm omitted**. This
   proves that every maximal normal path from S passes T. A normal/unknown port
   remains its original completion kind; a synthetic sink never becomes a return.

Let D be a decision entry, S its shared body, R its final fallback, J0 its old
join preference, and T the enclosing region stop. Replace J0 by T only when T is
real, J0 == R, Private(R), T postdominates S in the original checked projection,
and R does not dominate T. Discovery supplies the conditional-prefix entry and
edge evidence; before selecting the new join, each member's shared edge and the
final fallback are rechecked. Scope/ownership and all original edge destinations
are independently verified before any plan is published. R cannot own T: every
vertex reachable from a private R is dominated by R. S must reach T, so arranging
S to search for R would cross the enclosing boundary. R stays a complete private
conditional arm, S ends at T, and the enclosing region keeps ownership of T.

This is a deterministic incompatibility proof, not a failed-layout retry. Valid
old decisions retain every layout field, including early-exit and double-terminal
forms. No follow, declaration placement, expression fusion, phi/catch staging,
raise transfer or try-exit semantics are changed. If lexical ancestry witnesses a
reachable T but real postdominance cannot prove all paths reach it, the first
increment refuses with `DecisionContinuationError::kPostdominator`; it does not
replace postdominance with `AllAbrupt(S)`. A broken shared-prefix correspondence
has `kPrefix`. Cases outside this proven extension retain the old candidate and
the existing independent verifier. In particular, arbitrary overlapping dispatch
bodies and case fallthroughs are not implemented by this change.

`ControlFailure` retains `ControlError` and a typed ownership-or-continuation
cause. Continuation evidence contains the entry, last predicate, shared body,
fallback, old join, enclosing stop, and incompatibility witness. For that cause,
the general bounds describe the proposed arm and its enclosing region, not prior
node ownership. Normal evidence uses an available input origin; phase adapters
map original/working PCs, while try/loop synthetic locations retain typed IDs.
The cause chain, receipt and manifest serialization preserve the evidence and
resource classifications. Original control-error and ownership-enum values are
unchanged. An absent instruction origin never acquires an invented PC.

The independent verifier consumes the final region plan and immutable original
edges; it does not consume Private or postdominance flags. It checks every actual
conditional edge, the final fallback and shared region, unique ownership,
coverage, scoped normal continuation and loop/exception correspondence. Restoring
the old private-return join in a new plan must fail this verifier. The existing
exception projection comparison also rechecks original phase/try-exit mappings.

The third ancestor table adds O(V log V) storage; retained entry dominance and
constant-sized decision-tail summaries avoid per-decision graph searches. Each
maximal chain is resolved and emitted once. DAG construction and LCA queries cost
O((V+E) log V), with all tables charged through `ControlQuota` before allocation.
This is the standard DAG postdominator recurrence, independently implemented
using the repository's binary-lifting convention; no dependency or copied code.
No new source-language premise is required.

Tests: `control_decision_test`, the converted Class B case in
`control_terminal_test`, and `decision_continuations_cli_test`. The IR tests cover
reversed polarities, private/internal joins, continuing joins, edge splitting and
copies, original/projected views, exact old-layout preservation, external entry,
unknown completion, a continuing path that bypasses T, shared-tail and backedge
mutations, old-join/edge/condition/fallback mutations, 200/201/400/800 scaling and
exact work/byte boundaries. Existing try-exit mutation coverage remains in
`exception_plan_test`. The real es2abc fixture and Node oracle cover short-circuit
side effects, private returns, getters/calls throwing, catch, async, and compiled
finally wrappers. See [fixture provenance and oracle limits](../../../tests/integration/fixtures/decision_continuations/README.md).

Workspace validation against `e0d667a3`: full keep-going build succeeded; fixed
source regression 65/65; control/phased/loop/try/logical plus the new CLI, route
and exception-plan slice 81/86. The five failures are the same as the Round 39
log (try_tree, phased_literal, phased_arm_selection_source, try_tree_source,
file_generator_return_loop_source); their expectations were not changed. The
final focused six-test run passed. Both exploration flag sets have zero status
regressions and zero already-complete text drift, including o32/49868. Completion
is 479/620 (old flags) and 511/620 (source), up one each: o5/44477 YearView. All
11 source-flag Class B first refusals are gone, but ten records expose later
control/tree/capture/reference gaps. o18/57687 and o38/13920 retain byte-equivalent
failure objects under their respective flags. No holdout, full-suite or RSS run.

Round 45 does not change these control boundaries. Decision-prefix emission now
consumes the same verified placement disposition as ordinary blocks; initialized
cell guards become boundary anchors, while throwing checked reads remain
separate events. See [guarded-captures.md §12](guarded-captures.md#12-initialized-guards-in-decision-prefixes-round-45-wip)
for refusal-location evidence and independent anchor checks.
