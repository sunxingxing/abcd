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
